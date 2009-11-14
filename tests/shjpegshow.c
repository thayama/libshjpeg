#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include "shjpeg.h"
#include "directfb.h"
#include "core/gfxcard.h"

static IDirectFB *dfb = NULL;

#define DFBCHK(x...) \
	{	\
		DFBResult err = x;	\
		if (err != DFB_OK) {	\
			fprintf(stderr, "%s <%d>:\n\t", __FILE__, __LINE__);\
			DirectFBErrorFatal( #x, err );	\
		}	\
	}	\

int sops_init(void *private)
{
	int fd = *(int*)private;

	lseek(fd, 0L, SEEK_SET);

	return 0;
}

int sops_read(void *private, size_t *nbytes, void *dataptr)
{
	int fd = *(int*)private;
	int n;

	n = read(fd, dataptr, *nbytes);
	if (n < 0) {
		*nbytes = 0;
		return -1;
	}

	*nbytes = n;
	return 0;
}

shjpeg_sops my_sops = {
	.init		= sops_init,
	.read		= sops_read,
	.write		= NULL,
	.finalize	= NULL,
};

int main(int argc, char *argv[])
{
	int			verbose = 1;
	int			disable_libjpeg = 0;
	char			*dirname = ".";
	DIR			*dp;
	struct dirent		*dirent;

	DFBSurfaceDescription	dsc;
	IDirectFBSurface	*primary, *image, *tmp;
	DFBRectangle		dest_rect;
	DFBSurfacePixelFormat	pixformat;
	double			scale_w, scale_h;
	int			target_w, target_h;
	int			offset_x, offset_y;
	int			screen_width;
	int			screen_height;

	shjpeg_context_t	*context;
	shjpeg_pixelformat	format;
	int			pitch;
	int			offset;
	static int		fd;
	unsigned long		jpeg_phys;
	void			*jpeg_virt;
	size_t			jpeg_size;

	int			n;

	/* Initialize DirectFB */
	DirectFBInit(&argc, &argv);
	DirectFBCreate(&dfb);

	DFBCHK(dfb->SetCooperativeLevel(dfb, DFSCL_FULLSCREEN));
	dsc.flags = DSDESC_CAPS;
	dsc.caps  = DSCAPS_PRIMARY;

	DFBCHK(dfb->CreateSurface(dfb, &dsc, &primary));
	DFBCHK(primary->GetSize(primary, &screen_width, &screen_height));
	DFBCHK(primary->Clear(primary, 0, 0, 0, 0));
	DFBCHK(primary->Flip(primary, NULL, DSFLIP_NONE));

	/* create temporary color convereted image */
	DFBCHK(primary->GetPixelFormat(primary, &pixformat));
	dsc.flags 	= DSDESC_WIDTH | DSDESC_HEIGHT | 
			  DSDESC_PIXELFORMAT | DSDESC_CAPS;
	dsc.caps	= DSCAPS_VIDEOONLY;
	dsc.width 	= screen_width;
	dsc.height	= screen_height;
	dsc.pixelformat	= pixformat;
	DFBCHK(dfb->CreateSurface(dfb, &dsc, &tmp));

	/* Open directory */
	if (argv[1])
		dirname = argv[1];
	if ((dp = opendir(dirname)) == NULL) {
		fprintf(stderr, "Can't open directory - %s (%s)\n",
			dirname, strerror(errno));
		return 1;
	}

	/* intialize libshjpeg */
	if ((context = shjpeg_init(verbose)) == NULL) {
		fprintf(stderr, "shjpeg_init() failed - %s\n",
			strerror(errno));
		return 1;
	}
	context->sops = &my_sops;
	context->libjpeg_disabled = disable_libjpeg;

	/* For all files */
	while(dirent = readdir(dp)) {
		fprintf(stderr, "Processing - %s\n", dirent->d_name);

		if (dirent->d_type == DT_DIR) {
			fprintf(stderr, "Skipping directory.\n");
			continue;
		}

		/* open file */
		if ((fd = open(dirent->d_name, O_RDONLY)) < 0) {
			fprintf(stderr, "Warning: Can't open %s (%s)\n",
				dirent->d_name, strerror(errno));
			return 1;
		}
		context->private = (void*)&fd;

		/* see if we can decode this file */
		fprintf(stderr, "initializing decoder\n");
		if (shjpeg_decode_init(context) < 0) {
			fprintf(stderr, "Warning: skipping %s\n",
				dirent->d_name);
			continue;
		}

		/* allocate surface */
		dsc.flags 	= DSDESC_WIDTH | DSDESC_HEIGHT | 
				  DSDESC_PIXELFORMAT | DSDESC_CAPS;
		dsc.caps	= DSCAPS_VIDEOONLY;
		dsc.width 	= context->width;
		dsc.height	= context->height;
		dsc.pixelformat	= (context->mode420) ? DSPF_NV12 : DSPF_NV16;

		DFBCHK(dfb->CreateSurface(dfb, &dsc, &image));
		DFBCHK(image->Lock(image, DSLF_WRITE, &jpeg_virt, &pitch));
		DFBCHK(image->GetFramebufferOffset(image, &offset));
		jpeg_phys = dfb_gfxcard_memory_physical(NULL, offset);

		/* decode */
		fprintf(stderr, 
			"start decoding: phys=%08x(%x), pitch=%d, %dx%d\n",
			jpeg_phys, offset, pitch, context->width, context->height);
		format = (context->mode420) ? SHJPEG_PF_NV12 : SHJPEG_PF_NV16;
		if (shjpeg_decode_run(context, format, jpeg_phys,
				      context->width, context->height,
				      pitch) < 0) {
			fprintf(stderr, "shjpeg_decode_run() failed\n");
			return 1;
		}

		DFBCHK(image->Unlock(image));

		/* close */
		shjpeg_decode_shutdown(context);
		close(fd);
	
		/* calculate the size */
		scale_w = (context->width > screen_width) ? 
			  ((double)screen_width / context->width) : 1.0;
		scale_h = (context->height > screen_height) ? 
			  ((double)screen_height / context->height) : 1.0;
		if (scale_w < scale_h) {
			target_w = scale_w * context->width;
			target_h = scale_w * context->height;
		} else {
			target_w = scale_h * context->width;
			target_h = scale_h * context->height;
		}

		offset_x = (screen_width  - target_w) / 2;
		offset_y = (screen_height - target_h) / 2;

		dest_rect.x = offset_x;
		dest_rect.y = offset_y;
		dest_rect.w = target_w;
		dest_rect.h = target_h;

		fprintf(stderr, "sw=%d, sh=%d, ow=%d, oh=%d\n",
			screen_width, screen_height,
			context->width, context->height);
		fprintf(stderr, "x=%d, y=%d, w=%d, h=%d\n", 
			offset_x, offset_y, target_w, target_h);
		fprintf(stderr, "scale_w=%lf, scale_h=%lf\n",
			scale_w, scale_h);

		/* render to tmp surface (color conversion) */
		DFBCHK(tmp->Clear(tmp, 0, 0, 0, 0xff));
		DFBCHK(tmp->StretchBlit(tmp, image, NULL, &dest_rect));
		DFBCHK(tmp->ReleaseSource(tmp));
		DFBCHK(image->Release(image));
		
		/* now fade-in */
		DFBCHK(primary->SetDrawingFlags(primary, DSDRAW_BLEND));
		DFBCHK(primary->SetBlittingFlags(primary, DSBLIT_BLEND_COLORALPHA));
		for(n = 2; n <= 256; n <<= 1) {
			DFBCHK(primary->SetColor(primary, 0, 0, 0, n - 1));
			DFBCHK(primary->FillRectangle(primary, 0, 0, screen_width, screen_height));
			DFBCHK(primary->Blit(primary, tmp, NULL, 0, 0));
			DFBCHK(primary->Flip(primary, NULL, DSFLIP_NONE));
			usleep(100000);
		}

		/* release memory */
		DFBCHK(primary->ReleaseSource(primary));

		/* wait for the next */
		sleep(1);
	}

	return 0;
}
