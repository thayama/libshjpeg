/*
 * Copyright 2009 IGEL Co.,Ltd.
 * Copyright 2008,2009 Renesas Solutions Co.
 * Copyright 2008 Denis Oliver Kropp
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/mman.h>

#include <shjpeg/shjpeg.h>

/* for /dev/mem */
static int memfd = -1;

/* for BMP bitmap */

typedef struct {
    /* BITMAPFILEHEADER (except 2byte TYPE info) */
    uint32_t	bmp_size;
    uint32_t	dummy;
    uint32_t	bmp_offset;

    /* BITMAPV4HEADER */
    uint32_t	header_size;
    int32_t	width;
    int32_t	height;
    uint16_t	num_planes;
    uint16_t	bpp;
    uint32_t	compression;
    uint32_t	raw_size;
    int32_t	hres;
    int32_t	vres;
    uint32_t	palette_size;
    uint32_t	important_colors;
    uint32_t	red_mask;
    uint32_t	green_mask;
    uint32_t	blue_mask;
    uint32_t	alpha_mask;
    uint32_t	cstype;
    uint32_t	endpoints[9];
    uint32_t	gamma_red;
    uint32_t	gamma_green;
    uint32_t	gamma_blue;
} bmp_header_t;

void *map_image(unsigned long phys, int pitch, int height, int *size)
{
    int	page_sz = getpagesize() - 1;

    *size = ((pitch * height) + page_sz) & ~page_sz;

    if (memfd < 0) {
	if ((memfd = open("/dev/mem", O_RDWR)) < 0) {
	    perror("map_image(): opening /dev/mem -");
	    return MAP_FAILED;
	}
    }

    // map
    return mmap(NULL, *size, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, phys);
}

void munmap_image(void *mem, int *size)
{
    munmap(mem, *size);
}

void write_bmp(const char *filename, int bpp,
	       unsigned long phys, int pitch, int width, int height)
{
    bmp_header_t bmp_header;
    int h, w, bw, size, raw_size, stride;
    void *mem, *ptr;
    FILE *file;
    char *buffer = NULL, tmp;

    /* mamp memory */
    mem = map_image(phys, pitch, height, &size);
    if (mem == MAP_FAILED) {
	perror("write_bmp(): mmaping /dev/mem -");
	return;
    }

    /* Open BMP file to write */
    file = fopen(filename, "wb");
    if (!file) {
	perror("write_bmp(): opening file to write - ");
	munmap_image(mem, &size);
	return;
    }
    
    /* create BMP header */
    memset((void*)&bmp_header, 0, sizeof(bmp_header_t));
    stride = (((width * bpp) + 0x1f) & ~0x1f) >> 3;
    raw_size = stride * height;
printf("width=%d, stride=%d, pitch=%d, height=%d, size=%d(%x)\n",
	width, stride, pitch, height, raw_size, raw_size);
    bmp_header.bmp_size    = 2 + sizeof(bmp_header_t) + raw_size;
    bmp_header.bmp_offset  = 2 + sizeof(bmp_header_t);
    bmp_header.header_size = sizeof(bmp_header_t) - 12;
    bmp_header.width	   = width;
    bmp_header.height      = -height;
    bmp_header.num_planes  = 1;
    bmp_header.bpp	   = bpp;
    bmp_header.raw_size	   = raw_size;

    /* mask */
    switch(bpp) {
    case 16:
	bmp_header.red_mask   = 0xf800;
	bmp_header.green_mask = 0x07e0;
	bmp_header.blue_mask  = 0x001f;
	bmp_header.compression = 3;
	buffer = NULL;
	break;
    case 24:
	bmp_header.red_mask   = 0x00ff0000;
	bmp_header.green_mask = 0x0000ff00;
	bmp_header.blue_mask  = 0x000000ff;
	bmp_header.compression = 0;
	buffer = malloc(stride);
	break;
    }    

    /* Write BMP header */
    fprintf(file, "BM");
    fwrite(&bmp_header, 1, sizeof(bmp_header), file);

    /* Write data */
    bw = bpp >> 3;
//    mem += (height - 1) * pitch;
    ptr = mem;
    for (h = 0; h < height; h++) {
	switch(bpp) {
	case 16:
	    buffer = ptr;
	    break;
	case 24:
	    memcpy(buffer, ptr, stride);
	    for (w = 0; w < width; w++) {
		tmp = buffer[w * 3 + 2];
		buffer[w * 3 + 2] = buffer[w * 3];
		buffer[w * 3] = tmp;;
	    }
	    break;
	}
	fwrite(buffer, 1, stride, file);
	ptr += pitch;
    }

    /* done */
    fclose(file);

    if (bpp == 24)
	free(buffer);

    /* unmap memory */
    munmap_image(mem, &size);
}

void
write_ppm(const char    *filename,
	  unsigned long  phys,
	  int            pitch,
	  unsigned int   width,
	  unsigned int   height)
{
    int i, size;
    void *mem;
    FILE *file;

    /* mamp memory */
    mem = map_image(phys, pitch, height, &size);
    if (mem == MAP_FAILED) {
	perror("write_ppm(): mmaping /dev/mem -");
	return;
    }

    /* Open PPM file to write */
    file = fopen(filename, "wb");
    if (!file) {
	perror("write_ppm(): opening file to write - ");
	munmap_image(mem, &size);
	return;
    }

    /* Write PPM header */
    fprintf(file, "P6\n%d %d\n255\n", width, height);

    /* Write data */
    for (i=0; i<height; i++) {
	fwrite(mem, 3, width, file);
	mem += pitch;
    }

    /* done */
    fclose(file);

    /* unmap memory */
    munmap_image(mem, &size);
}


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

int sops_write(void *private, size_t *nbytes, void *dataptr)
{
    int fd = *(int*)private;
    int n;

    n = write(fd, dataptr, *nbytes);
    if (n < 0) {
	*nbytes = 0;
	return -1;
    }

    *nbytes = n;
    return 0;
}

void sops_finalize(void *private)
{
}

shjpeg_sops my_sops = {
    .init     = sops_init,
    .read     = sops_read,
    .write    = sops_write,
    .finalize = sops_finalize,
};

const char *argv0;

void 
print_usage() {
    fprintf(stderr, 
	    "Usage: %s [OPTION] <jpegfile> [<output>]\n", argv0);
    fprintf(stderr, 
	    "- Decode given JPEG file, and then re-encode.\n"
	    "- Default re-encoed JPEG filename is with '.out' as a suffix.\n"
	    "\n"
	    "Options:\n"
	    "  -h, --help                this message.\n"
	    "  -v, --verbose             libshjpeg verbose output.\n"
	    "  -q, --quiet		 no messages from this program.\n"
	    "  -d[<ppm>], --dump[=<ppm>] dump decoded image in PPM (default: test.ppm).\n"
	    "  -D[<bmp>], --bmp[=<bmp>]  dump decoded image in BMP (default: test.bmp).\n"
	    "  -b <bpp>, --bpp=<bpp>     Bits-per-pixel for BMP image (default: 24)"
	    "  -p <phys>, --phys=<phys>  specify physical memory to use.\n"
	    "  -n, --no-libjpeg          disable fallback to libjpeg.\n");
}

int
main(int argc, char *argv[])
{
    int                    pitch;
    void                  *jpeg_virt;
    unsigned long          jpeg_phys;
    unsigned long	   phys = SHJPEG_USE_DEFAULT_BUFFER;
    size_t                 jpeg_size;
    shjpeg_context_t	  *context;
    int			   fd;
    shjpeg_pixelformat	   format;
    char		   filename[1024];
    char		  *input, *output;
    char		  *dumpfn = "test.ppm";
    char		  *dumpfn2 = "test.bmp";
    int			   verbose = 0;
    int			   dump = 0;
    int			   bpp = 24;
    int			   disable_libjpeg = 0;
    int			   quiet = 0;

    argv0 = argv[0];

    /* parse arguments */
    while (1) {
	int c, option_index = 0;
	static struct option   long_options[] = {
	    {"help", 0, 0, 'h'},
	    {"verbose", 0, 0, 'v'},
	    {"quiet", 0, 0, 'q'},
	    {"dump", 2, 0, 'd'},
	    {"bmp", 2, 0, 'D'},
	    {"bpp", 1, 0, 'b'},
	    {"phys", 1, 0, 'p'},
	    {"no-libjpeg", 0, 0, 'n'},
	    {0, 0, 0, 0}
	};
	
	if ((c = getopt_long(argc, argv, "hvd::D::b:nqp:",
			     long_options, &option_index)) == -1)
	    break;

	switch(c) {
	case 'h':	// help
	    print_usage();
	    return 0;

	case 'v':
	    verbose = 1;
	    break;

	case 'd':
	    dump = 1;
	    if (optarg)
		dumpfn = optarg;
	    break;

	case 'D':
	    dump = 2;
	    if (optarg)
		dumpfn2 = optarg;
	    break;

	case 'b':
	    bpp =  strtol(optarg, NULL, 0);
	    break;

	case 'n':
	    disable_libjpeg = 1;
	    break;

	case 'q':
	    quiet = 1;
	    break;

	case 'p':
	    phys = strtol(optarg, NULL, 0);
	    break;

	default:
	    fprintf(stderr, "unknown option 0%x.\n", c);
	    print_usage();
	    return 1;
	}
    }


    if (optind >= argc) {
	print_usage();
	return 1;
    }

    /* check option */
    input = argv[optind++];
    if (argv[optind]) 
	output = argv[optind];
    else {
	snprintf(filename, sizeof(filename), "%s.out", input);
	output = filename;
    }

    /* verbose */
    if (!quiet) {
	printf("Input file = %s\n", input);
	printf("Output file = %s\n", output);
	printf("Physical addr: 0x%08lx\n", phys);
	printf("Use libjpeg: %s\n", (disable_libjpeg) ? "no" : "yes");
    }

    /* open jpegfile */
    if ((fd = open(input, O_RDONLY)) < 0) {
	fprintf(stderr, "%s: Can't open '%s'.\n", argv[0], input);
	return 1;
    }

    /* initialize - verbose mode */
    if ((context = shjpeg_init(verbose)) == NULL) {
	fprintf(stderr, "shjpeg_init() failed\n");
	return 1;
    }

    /* set callbacks to context */
    context->sops = &my_sops;
    context->private = (void*)&fd;
    context->libjpeg_disabled = disable_libjpeg;

    /* init decoding */
    if (shjpeg_decode_init(context) < 0) {
	fprintf(stderr, "shjpeg_decode_init() failed\n");
	return 1;
    }

    if (verbose)
	printf("%s: opened %dx%d image (4:%s)\n",
	       argv[0], context->width, context->height,
	       (context->mode420) ? "2:0" : 
	       ((context->mode444) ? "4:4" : "2:2?"));

    if (dump) {
	/* When PPM is requested, bpp must be 24 */
	if (dump == 1)
	    bpp = 24;
	
	switch(bpp) {
	case 24:
	    format = SHJPEG_PF_RGB24;
	    break;
	case 16:
	    format = SHJPEG_PF_RGB16;
	    break;
	default:
	    fprintf(stderr, "unsupported bpp (%d)\n", bpp);
	    return 1;
	}
    } else {
	format = !context->mode420 ? SHJPEG_PF_NV16 : SHJPEG_PF_NV12;
    }
    pitch  = (SHJPEG_PF_PITCH_MULTIPLY(format) * context->width + 7) & ~7;

    /* start decoding */
    if (shjpeg_decode_run(context, format, phys,
			  context->width, context->height, pitch) < 0) {
	fprintf(stderr, "shjpeg_deocde_run() failed\n");
	return 1;
    }

    if (!quiet) {
	printf("Decoded by: %s\n",
	       context->libjpeg_used ? "libjpeg" : "JPU");
    }

    /* shutdown decoder */
    shjpeg_decode_shutdown(context);

    /* get framebuffer information */
    if (phys == SHJPEG_USE_DEFAULT_BUFFER) {
	shjpeg_get_frame_buffer(context, &jpeg_phys, &jpeg_virt, &jpeg_size);
	if (!quiet) {
	    printf("jpu uio: JPEG Buffer - 0x%08lx(%p) - size = %08x\n",
		   jpeg_phys, jpeg_virt, jpeg_size );
	   }
    } else {
   	jpeg_phys = phys; 
    }

    /* dump intermediate file */
    switch(dump) {
    case 2:
	write_bmp(dumpfn2, bpp, jpeg_phys, pitch, 
		  context->width, context->height);
	break;

    case 1:
	write_ppm(dumpfn, jpeg_phys, pitch, context->width, context->height);
    }

    close(fd);

    /* now prep to re-encode */
    if ((fd = open(output, O_RDWR | O_CREAT, 0644)) < 0) {
	fprintf(stderr, "%s: Can't open '%s'.\n", argv[0], output);
	return 1;
    }

    /* start encoding */
    if (shjpeg_encode(context, format, jpeg_phys, 
		      context->width, context->height, pitch) < 0) {
	fprintf(stderr, "%s: shjpeg_encode() failed.\n", argv[0]);
	return 1;
    }
    close(fd);

    shjpeg_shutdown(context);

    if (!quiet)
    	printf("done!\n");

    return 0;
}
