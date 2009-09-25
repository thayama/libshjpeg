/*
 * Copyright (c) 2009, Takanari Hayama <taki@igel.co.jp>
 * Copyright (c) 2008, Denis Oliver Kropp <dok@directfb.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met: 
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 * * Neither the name of the <ORGANIZATION> nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "shjpeg.h"

void
write_ppm(const char    *_filename,
	  unsigned long  phys,
	  int            pitch,
	  unsigned int   width,
	  unsigned int   height)
{
    int i, fd, size;
    void *mem, *_mem;
    FILE *file;
    int	  page_sz = getpagesize() - 1;

    size = ((pitch * height) + page_sz) & ~page_sz;

    fd = open( "/dev/mem", O_RDWR );
    if (fd < 0) {
	perror("write_ppm(): opening /dev/mem -");
	return;
    }

    // map
    mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, phys);
    if (mem == MAP_FAILED) {
	perror("write_ppm(): mmaping /dev/mem -");
	close( fd );
	return;
    }
    _mem = mem;

    // Open PPM file to write
    file = fopen( _filename, "wb" );
    if (!file) {
	perror("write_ppmn(): opening file to write - ");
	munmap( mem, size );
	return;
    }

    // Write PPM header
    fprintf(file, "P6\n%d %d\n255\n", width, height);

    for (i=0; i<height; i++) {
	fwrite(mem, 3, width, file);
	mem += pitch;
    }
    fclose( file );
    munmap( mem, size );
    close( fd );
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


int
main(int argc, char *argv[])
{
    int                    pitch;
    void                  *jpeg_virt;
    unsigned long          jpeg_phys;
    size_t                 jpeg_size;
    shjpeg_context_t	  *context;
    int			   fd;
    shjpeg_pixelformat	   format;
    char		   filename[1024];

    if (argc != 2) {
	fprintf(stderr, "Usage: %s <jpegfile>\n", argv[0]);
	fprintf(stderr, 
		"- Decode given JPEG file, and then re-encode.\n");
	fprintf(stderr, 
		"- Re-encoded JPEG file will have '.out' as a suffix.\n");
	return 1;
    }

    /* open jpegfile */
    if ((fd = open(argv[1], O_RDONLY)) < 0) {
	fprintf(stderr, "%s: Can't open '%s'.\n", argv[0], argv[1]);
	return 1;
    }

    /* initialize - verbose mode */
    printf("shjpeg_init() - start\n");
    if ((context = shjpeg_init(1)) == NULL) {
	fprintf(stderr, "shjpeg_init() failed\n");
	return 1;
    }
    printf("shjpeg_init() - done ... context = %08p\n", context);

    /* set callbacks to context */
    context->sops = &my_sops;
    context->private = (void*)&fd;

    /* init decoding */
    printf("shjpeg_decode_init() - start\n");
    if (shjpeg_decode_init(context) < 0) {
	fprintf(stderr, "shjpeg_decode_init() failed\n");
	return 1;
    }
    printf("shjpeg_decode_init() - done\n");

    printf("%s: opened %dx%d image (4:%s)\n",
	   argv[0], context->width, context->height,
	   (context->mode420) ? "2:0" : ((context->mode444) ? "4:4" : "2:2?"));
    format = !context->mode420 ? SHJPEG_PF_NV16 : SHJPEG_PF_NV12;
    pitch  = (((SHJPEG_PF_BPP(format) >> 3) * context->width) + 31) & ~31;

    /* start decoding */
    printf("shjpeg_decode_run() - start\n");
    if (shjpeg_decode_run(context, format, SHJPEG_USE_DEFAULT_BUFFER,
			  context->width, context->height, pitch) < 0) {
	fprintf(stderr, "shjpeg_deocde_run() failed\n");
	return 1;
    }
    printf("shjpeg_decode_run() - done\n");

    /* shutdown decoder */
    printf("shjpeg_decode_shutdown() - start\n");
    shjpeg_decode_shutdown(context);
    printf("shjpeg_decode_shutdown() - done\n");

//  Use RGB24 format to dump image properly
//     write_ppm( "test.ppm", SH7722_JPEG_PHYSMEM, pitch, info.width, info.height );

    /* get framebuffer information */
    printf("shjpeg_get_frame_buffer() - start\n");
    shjpeg_get_frame_buffer(context, &jpeg_phys, &jpeg_virt, &jpeg_size);
    printf("shjpeg_get_frame_buffer() - done\n");
    printf( "%s: JPEG Buffer - 0x%08lx(%p) - size = %08x\n",
	    argv[0], jpeg_phys, jpeg_virt, jpeg_size );

    /* now prep to re-encode */
    snprintf(filename, sizeof(filename), "%s.out", argv[1]);
    close(fd);
    if ((fd = open(filename, O_RDWR | O_CREAT)) < 0) {
	fprintf(stderr, "%s: Can't open '%s'.\n", argv[0], filename);
	return 1;
    }

    /* start encoding */
    printf("shjpeg_encode() - start\n");
    if (shjpeg_encode(context, format, jpeg_phys, 
		      context->width, context->height, pitch) < 0) {
	fprintf(stderr, "%s: shjpeg_encode() failed.\n");
	return 1;
    }
    printf("shjpeg_encode() - done\n");
    close(fd);

    printf("shjpeg_shutdown() - start\n");
    shjpeg_shutdown(context);
    printf("shjpeg_shutdown() - done\n");

    return 0;
}
