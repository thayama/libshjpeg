#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <time.h>

#include <shjpeg/shjpeg.h>
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
    int fd = *(int *) private;

    lseek(fd, 0L, SEEK_SET);

    return 0;
}

int sops_read(void *private, size_t * nbytes, void *dataptr)
{
    int fd = *(int *) private;
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
    .init = sops_init,
    .read = sops_read,
    .write = NULL,
    .finalize = NULL,
};

const char *argv0;

void print_usage() {
    fprintf(stderr,
    	    "Usage: %s [OPTION] <directory>\n", argv0);
    fprintf(stderr,
    	    "Photo frame sample.\n"
	    "\n"
	    "Options:\n"
	    "  -h, --help                 this message.\n"
	    "  -v, --verbose              verbose output.\n"
	    "  -f <font>, --font=<font>   font file to use.\n"
	    "  -t, --trim                 trim to to fit screen.\n"
	    "  -a, --aspect               keep aspect ratio. (default)\n"
	    "  -d, --duration             duration in sec. (default 1sec)\n");
}

/* mode change */
enum scale_t {
    SCALE_TRIM,
    SCALE_ASPECT
} scale_option; 

int display_date = 1;
IDirectFBSurface *primary, *image, *tmp;
shjpeg_context_t *context = NULL;

char *update_mode(int code)
{
    static char str[128];
    char *rc = str;

    switch(code) {
    case 0x31:
    	snprintf(str, sizeof(str), "H/W acceleration on");
    	DFBCHK(primary->DisableAcceleration(primary, DFXL_NONE));
    	DFBCHK(tmp->DisableAcceleration(tmp, DFXL_NONE));
    	if (context)
    	    context->libjpeg_disabled = 0;
    	break;

    case 0x32:
    	snprintf(str, sizeof(str), "H/W acceleration off");
    	DFBCHK(primary->DisableAcceleration(primary, DFXL_ALL));
    	DFBCHK(tmp->DisableAcceleration(tmp, DFXL_ALL));
    	if (context)
    	    context->libjpeg_disabled = -1;
    	break;

    case 0x33:
    	snprintf(str, sizeof(str), "Date on");
    	display_date = 1;
    	break;

    case 0x34:
    	snprintf(str, sizeof(str), "Date off");
    	display_date = 0;
    	break;

    case 0x35:
    	snprintf(str, sizeof(str), "Fit to screen");
    	scale_option = SCALE_TRIM;
    	break;
    	
    case 0x36:
    	snprintf(str, sizeof(str), "Keep aspect");
    	scale_option = SCALE_ASPECT;
    	break;

    default:
    	rc = NULL;
    }

    return rc;
}

int main(int argc, char *argv[])
{
    int verbose = 1;
    int disable_libjpeg = 0;
    char *dirname = ".";
    DIR *dp;
    struct dirent *dirent;

    DFBSurfaceDescription dsc;
    DFBRectangle dest_rect;
    DFBSurfacePixelFormat pixformat;
    IDirectFBFont *font;
    IDirectFBEventBuffer *events;
    DFBFontDescription fdsc;
    double scale_w, scale_h, scaler;
    int target_w, target_h;
    int offset_x, offset_y;
    int screen_width;
    int screen_height;

    shjpeg_pixelformat format;
    int pitch;
    int offset;
    static int fd;
    unsigned long jpeg_phys;
    void *jpeg_virt;
    size_t jpeg_size;

    int n;
    char path[PATH_MAX];
    char *fontfile = "/usr/local/share/directfb-examples/fonts/decker.ttf";
    int rc = 0;
    int duration = 1;
    time_t now;

    scale_option = SCALE_ASPECT;
    argv0 = argv[0];

    /* Initialize DirectFB */
    DirectFBInit(&argc, &argv);
    DirectFBCreate(&dfb);

    /* parse arguments */
    while(1) {
    	int c, option_index = 0;
	static struct option long_options[] = {
	    {"help", 0, 0, 'h'},
	    {"verbose", 0, 0, 'v'},
	    {"font", 1, 0, 'f'},
	    {"trim", 0, 0, 't'},
	    {"aspect", 0, 0, 'a'},
	    {"default", 1, 0, 'd'},
	    {0, 0, 0, 0}
	};

	if ((c  = getopt_long(argc, argv, "hvtaf:d:",
			      long_options, &option_index)) == -1)
	    break;

	switch(c) {
	case 'h':	// help
	    print_usage();
	    goto quit;

	case 'v':
	    verbose = 1;
	    break;

	case 'f':
	    fontfile = optarg;
	    break;

	case 'd':
	    duration = atoi(optarg);
	    break;

	case 't':
	    scale_option = SCALE_TRIM;
	    break;

	case 'a':
	    scale_option = SCALE_ASPECT;
	    break;

	default:
	    fprintf(stderr, "unknown option 0%x.\n", c);
	    print_usage();
	    rc = 1;
	    goto quit;
	}
    }

    DFBCHK(dfb->SetCooperativeLevel(dfb, DFSCL_FULLSCREEN));
    dsc.flags = DSDESC_CAPS;	// | DSDESC_PIXELFORMAT;
    dsc.caps = DSCAPS_PRIMARY;
    //dsc.pixelformat       = DSPF_RGB24;

    DFBCHK(dfb->CreateSurface(dfb, &dsc, &primary));
    DFBCHK(primary->GetSize(primary, &screen_width, &screen_height));
    DFBCHK(primary->Clear(primary, 0, 0, 0, 0));
    DFBCHK(primary->Flip(primary, NULL, DSFLIP_NONE));

    /* prepare events */
    DFBCHK(dfb->CreateInputEventBuffer(dfb, DICAPS_KEYS, DFB_FALSE, &events));

    /* load font */
    fdsc.flags = DFDESC_HEIGHT;
    fdsc.height = screen_width / 30;
    DFBCHK(dfb->CreateFont(dfb, fontfile, &fdsc, &font));
    DFBCHK(primary->SetFont(primary, font));

    /* create temporary color convereted image */
    DFBCHK(primary->GetPixelFormat(primary, &pixformat));
    dsc.flags = DSDESC_WIDTH | DSDESC_HEIGHT |
	DSDESC_PIXELFORMAT | DSDESC_CAPS;
    dsc.caps = DSCAPS_VIDEOONLY;
    dsc.width = screen_width;
    dsc.height = screen_height;
    dsc.pixelformat = pixformat;
    DFBCHK(dfb->CreateSurface(dfb, &dsc, &tmp));

    /* intialize libshjpeg */
    if ((context = shjpeg_init(verbose)) == NULL) {
	fprintf(stderr, "shjpeg_init() failed - %s\n", strerror(errno));
	rc = 1;
	goto quit2;
    }
    context->sops = &my_sops;
    context->libjpeg_disabled = disable_libjpeg;

    /* Open directory */
    if (argv[optind])
	dirname = argv[optind];

  again:
    if ((dp = opendir(dirname)) == NULL) {
	fprintf(stderr, "Can't open directory - %s (%s)\n",
		dirname, strerror(errno));
	return 1;
    }

    /* For all files */
    while (dirent = readdir(dp)) {
	fprintf(stderr, "Processing - %s\n", dirent->d_name);

	if (dirent->d_type == DT_DIR) {
	    fprintf(stderr, "Skipping directory.\n");
	    continue;
	}

	/* open file */
	snprintf(path, sizeof(path), "%s/%s", dirname, dirent->d_name);
	if ((fd = open(path, O_RDONLY)) < 0) {
	    fprintf(stderr, "Warning: Can't open %s (%s)\n",
		    dirent->d_name, strerror(errno));
	    return 1;
	}
	context->private = (void *) &fd;

	/* see if we can decode this file */
	fprintf(stderr, "initializing decoder\n");
	if (shjpeg_decode_init(context) < 0) {
	    fprintf(stderr, "Warning: skipping %s\n", dirent->d_name);
	    continue;
	}

	/* allocate surface */
	dsc.flags = DSDESC_WIDTH | DSDESC_HEIGHT |
	    DSDESC_PIXELFORMAT | DSDESC_CAPS;
	dsc.caps = DSCAPS_VIDEOONLY;
	dsc.width = context->width;
	dsc.height = context->height;
	dsc.pixelformat = (context->mode420) ? DSPF_NV12 : DSPF_NV16;

	DFBCHK(dfb->CreateSurface(dfb, &dsc, &image));
	DFBCHK(image->Lock(image, DSLF_WRITE, &jpeg_virt, &pitch));
	DFBCHK(image->GetFramebufferOffset(image, &offset));
	jpeg_phys = dfb_gfxcard_memory_physical(NULL, offset);

	/* decode */
	fprintf(stderr,
		"start decoding: phys=%08lx(%x), pitch=%d, %dx%d\n",
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
	    ((double) screen_width / context->width) : 1.0;
	scale_h = (context->height > screen_height) ?
	    ((double) screen_height / context->height) : 1.0;

	switch(scale_option) {
	case SCALE_ASPECT:
	    scaler = (scale_w < scale_h) ? scale_w : scale_h;
	    break;

	case SCALE_TRIM:
	    scaler = (scale_w > scale_h) ? scale_w : scale_h;
	    break;
	}

	target_w = scaler * context->width;
	target_h = scaler * context->height;

	offset_x = (screen_width - target_w) / 2;
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
	fprintf(stderr, "scale_w=%lf, scale_h=%lf\n", scale_w, scale_h);

	/* render to tmp surface (color conversion) */
	DFBCHK(tmp->Clear(tmp, 0, 0, 0, 0xff));
	DFBCHK(tmp->StretchBlit(tmp, image, NULL, &dest_rect));
	DFBCHK(tmp->ReleaseSource(tmp));
	DFBCHK(image->Release(image));

	/* now fade-in */
	DFBCHK(primary->SetDrawingFlags(primary, DSDRAW_BLEND));
	DFBCHK(primary->
	       SetBlittingFlags(primary, DSBLIT_BLEND_COLORALPHA));
	for (n = 2; n <= 256; n <<= 1) {
	    char str[32];
	    time_t ct;
	    struct tm *tp;

	    /* fade image */
	    DFBCHK(primary->SetColor(primary, 0, 0, 0, n - 1));
	    DFBCHK(primary->
		   FillRectangle(primary, 0, 0, screen_width,
				 screen_height));

	    /* render new image */
	    DFBCHK(primary->Blit(primary, tmp, NULL, 0, 0));

	    /* render date & time */
	    if (display_date) {
		time(&ct);
		tp = localtime(&ct);
		strftime(str, sizeof(str), "%Y/%m/%d %R", tp);
		DFBCHK(primary->SetColor(primary, 0x20, 0x20, 0x20, 0xff));
		DFBCHK(primary->
		       DrawString(primary, str, -1, screen_width - 20,
				  screen_height, DSTF_BOTTOMRIGHT));
		DFBCHK(primary->SetColor(primary, 0xf0, 0xf0, 0xf3, 0xff));
		DFBCHK(primary->
		       DrawString(primary, str, -1, screen_width - 21,
			          screen_height - 1, DSTF_BOTTOMRIGHT));

	    }

	    /* flip */
	    DFBCHK(primary->Flip(primary, NULL, DSFLIP_NONE));
	    usleep(100000);
	}

	/* release memory */
	DFBCHK(primary->ReleaseSource(primary));

	/* wait for the next */
	now = time(NULL);
	do {
	    DFBInputEvent evt;

	    if (events->WaitForEventWithTimeout(events, duration, 0) != 
		DFB_TIMEOUT) {
		DFBCHK(events->GetEvent(events, DFB_EVENT(&evt)));
	    	if (evt.type == DIET_KEYPRESS) {
	    	    char *str = update_mode(evt.key_symbol);

	    	    if (str) {
	    	    	DFBCHK(primary->SetColor(primary, 196,209,210,255));
	    	    	DFBCHK(primary->DrawString(primary, str, -1, 10, 10, DSTF_TOPLEFT));
	    	    	DFBCHK(primary->Flip(primary, NULL, DSFLIP_NONE));
	    	    }
	    	}
	    }
	} while(now + duration >= time(NULL));
    }

    fprintf(stderr, "done\n");
    closedir(dp);
    goto again;

  quit2:
    DFBCHK(font->Release(font));
    DFBCHK(tmp->ReleaseSource(tmp));
    DFBCHK(tmp->Release(tmp));
    DFBCHK(primary->ReleaseSource(primary));
    DFBCHK(primary->Release(primary));
  quit:
    DFBCHK(dfb->Release(dfb));

    shjpeg_shutdown(context);

    return 0;
}
