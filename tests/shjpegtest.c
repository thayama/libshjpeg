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
