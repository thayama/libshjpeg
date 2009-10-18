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

#include "shjpeg.h"

void
write_ppm(const char    *filename,
	  unsigned long  phys,
	  int            pitch,
	  unsigned int   width,
	  unsigned int   height)
{
    int i, fd, size;
    void *mem;
    FILE *file;
    int	  page_sz = getpagesize() - 1;

    size = ((pitch * height) + page_sz) & ~page_sz;

    fd = open("/dev/mem", O_RDWR);
    if (fd < 0) {
	perror("write_ppm(): opening /dev/mem -");
	return;
    }

    // map
    mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, phys);
    if (mem == MAP_FAILED) {
	perror("write_ppm(): mmaping /dev/mem -");
	close(fd);
	return;
    }

    // Open PPM file to write
    file = fopen(filename, "wb");
    if (!file) {
	perror("write_ppmn(): opening file to write - ");
	munmap(mem, size);
	return;
    }

    // Write PPM header
    fprintf(file, "P6\n%d %d\n255\n", width, height);

    for (i=0; i<height; i++) {
	fwrite(mem, 3, width, file);
	mem += pitch;
    }
    fclose(file);
    munmap(mem, size);
    close(fd);
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
	    "  -d[<ppm>], --dump[=<ppm>] dump intermediate image in ppm (default: test.ppm).\n"
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
    char		  *input, *output, *dumpfn = "test.ppm";
    int			   verbose = 0;
    int			   dump = 0;
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
	    {"phys", 1, 0, 'p'},
	    {"no-libjpeg", 0, 0, 'n'},
	    {0, 0, 0, 0}
	};
	
	if ((c = getopt_long(argc, argv, "hvd::nqp:",
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

    format =(dump) ? SHJPEG_PF_RGB24 :
	(!context->mode420 ? SHJPEG_PF_NV16 : SHJPEG_PF_NV12);
    pitch  = (SHJPEG_PF_PITCH_MULTIPLY(format) * context->width + 7) & ~7;

    /* start decoding */
    if (shjpeg_decode_run(context, format, phys,
			  context->width, context->height, pitch) < 0) {
	fprintf(stderr, "shjpeg_deocde_run() failed\n");
	return 1;
    }
    if (!quiet)
	printf("Decoded by: %s\n",
	       context->libjpeg_used ? "libjpeg" : "JPU");

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
    if (dump) {
	// Use RGB24 format to dump image properly
	write_ppm(dumpfn, jpeg_phys, pitch, context->width, context->height);
    }

    /* now prep to re-encode */
    close(fd);
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
