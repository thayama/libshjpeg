/*
 * libshjpeg: A library for controlling SH-Mobile JPEG hardware codec
 *
 * Copyright (C) 2009 IGEL Co.,Ltd.
 * Copyright (C) 2008,2009 Renesas Technology Corp.
 * Copyright (C) 2008 Denis Oliver Kropp
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA	 02110-1301 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>

#include <shjpeg/shjpeg.h>
#include "shjpeg_internal.h"
#include "shjpeg_jpu.h"

/*
 * UIO related routines
 */

/* open file, read bytes and then close */
static int
uio_readfile(shjpeg_context_t *context, 
	     const char *path, size_t n, char *buffer)
{
    FILE *fp;

    /* open file */
    if ((fp = fopen( path, "r" )) == NULL) {
	D_PERROR("libshjpeg: Can't open %s!", path);
	return -1;
    }

    /* clear buffer */
    memset(buffer, 0, n);

    /* read content */
    if (!fgets( buffer, n, fp)) {
	D_PERROR("libshjpeg: Can't read %d counts from %s.", n, path);
	return -1;
    }

    fclose(fp);

    return 0;
}

/*
 * search UIO class and open device
 */

static int
uio_open_dev(shjpeg_context_t 	*context, 
	     const char 	*name, 
	     int 		*uio_num)
{
    char path[MAXPATHLEN];
    int found = 0, uio_fd, i, len;
    shjpeg_internal_t *data = context->internal_data;

    /* search uio that has given name */
    len = strlen(name);
    for(i = 0; i < data->uio_count; i++) {
	if (!strncmp(name, data->uio_device[i], len)) {
	    sscanf(data->uio_dpath[i], "/sys/class/uio/uio%i", uio_num);
	    found = 1;

	    /*
	     * Set a flag if we have VEU3F.
	     * XXX: we need to find better place to do this...
	     */
	    if (!strncmp(data->uio_device[i], "VEU3F", 5))
		data->uio_caps |= UIO_CAPS_VEU3F;

	    break;
	}
    }
    
    if (!found)
	return -1;

    /* now open uio device, and return */
    snprintf(path, MAXPATHLEN, "/dev/uio%d", *uio_num);
    uio_fd = open(path, O_RDWR | O_SYNC);
    if (uio_fd < 0)
	D_PERROR("libshjpeg: Can't open %s!", path);

    return uio_fd;
}

/*
 * get list of available UIO
 */

static int
uio_enum_dev(shjpeg_context_t *context)
{
    char path[MAXPATHLEN];
    char uio_name[128];
    struct dirent **namelist;
    int n, i;
    shjpeg_internal_t *data = context->internal_data;

    /* already initialized? */
    if (data->uio_count > 0 ||
	data->uio_device != NULL)
	return 0;
    
    /* open uio kobjects */
    n = scandir("/sys/class/uio", &namelist, 0, alphasort);
    if (n < 3) {
	/* we must have at least 3 entries (".", "..", "uio0", ...)  */
	D_PERROR("libshjpeg: Could not open /sys/class/uio!");
	return -1;
    }

    /* alloc memory for device list */
    data->uio_count = 0;
    data->uio_device = malloc(sizeof(char*) * (n - 2));
    data->uio_dpath = malloc(sizeof(char*) * (n - 2));
    if (!data->uio_device || !data->uio_dpath) {
	D_PERROR("libshjpeg: Couldn't allocate uio device list");
	return -1;
    }

    /* enumerate available device */
    for(i = 0; i < n; i++) {
	if (strncmp(namelist[i]->d_name, "uio", 3) != 0)
	    continue;

	snprintf(path, MAXPATHLEN, 
		 "/sys/class/uio/%s/name", namelist[i]->d_name);

	/* read UIO device name */
	if (uio_readfile(context, path, 128, uio_name) < 0) {
	    D_ERROR("libshjpeg: Can't read '%s'", path);
	    free(data->uio_device);
	    free(data->uio_dpath);
	    return -1;
	} else {
	    data->uio_device[data->uio_count] = strdup(uio_name);
	    data->uio_dpath[data->uio_count]  = strdup(path);
	    data->uio_count++;
	}
	free(namelist[i]);
    }
    free(namelist);

    return 0;
}

/*
 * get map information
 */

static int
uio_get_maps(shjpeg_context_t   *context, 
	     const int		 uio_num,
	     const int		 maps_num,
	     unsigned long 	*addr,
	     unsigned long 	*size)
{
    char path[MAXPATHLEN];
    char buffer[128];

    snprintf(path, MAXPATHLEN, 
	     "/sys/class/uio/uio%d/maps/map%d/addr", uio_num, maps_num);
    if (uio_readfile(context, path, 128, buffer) < 0 )
	return -1;
    sscanf(buffer, "%lx", addr);

    snprintf(path, MAXPATHLEN, 
	     "/sys/class/uio/uio%d/maps/map%d/size", uio_num, maps_num);
    if (uio_readfile(context, path, 128, buffer) < 0)
	return -1;
    sscanf(buffer, "%lx", size);

    return 0;
}

/*
 * shutdown UIO dev
 */

static void
uio_shutdown(shjpeg_internal_t*data)
{
    /* unmap */
    if (data->jpu_base)
	munmap((void*) data->jpu_base, data->jpu_size);

    if (data->veu_base)
	munmap((void*) data->veu_base, data->veu_size);

    if (data->jpeg_virt)
	munmap((void*) data->jpeg_virt, data->jpeg_size);

    /* close UIO dev */
    close(data->jpu_uio_fd);
    close(data->veu_uio_fd);

    /* deinit */
    data->jpu_base = NULL;
    data->veu_base = NULL;
    data->jpeg_virt = NULL;

    data->veu_uio_fd = 0;
    data->jpu_uio_fd = 0;
}

/*
 * initialize UIO
 */
static int
uio_init(shjpeg_context_t *context, shjpeg_internal_t *data)
{
    D_DEBUG_AT(SH7722_JPEG, "( %p )", data );

    /* enum UIO device */
    if (uio_enum_dev(context) < 0) {
	D_ERROR("libshjpeg: Cannot list UIO device");
	return -1;
    }

    /* Open UIO for JPU. */
    if ((data->jpu_uio_fd = uio_open_dev(context, "JPU", 
					 &data->jpu_uio_num)) < 0) {
	D_ERROR("libshjpeg: Cannot find UIO for JPU!");
	return -1;
    }

    /* Open UIO for VEU. */
    if ((data->veu_uio_fd = uio_open_dev(context, "VEU", 
					 &data->veu_uio_num)) < 0) {
	D_ERROR( "libshjpeg: Cannot find UIO for VEU!" );
	return -1;
    }

    /*
     * Get registers and contiguous memory for JPU and VEU. 
     */

    /* for JPU registers */
    if (uio_get_maps(context, data->jpu_uio_num, 0, &data->jpu_phys,  
		     &data->jpu_size) < 0) {
	D_ERROR("libshjpeg: Can't get JPU base address!");
	goto error;
    }

    /* for VEU registers */
    if (uio_get_maps(context, data->veu_uio_num, 0, &data->veu_phys,  
		     &data->veu_size) < 0) {
	D_ERROR("libshjpeg: Can't get JPU base address!");
	goto error;
    }

    /* for JPEG memory */
    if (uio_get_maps(context, data->jpu_uio_num, 1, &data->jpeg_phys, 
		     &data->jpeg_size) < 0) {
	D_ERROR("libshjpeg: Can't get JPU base address!");
	goto error;
    }

    D_INFO("libshjpeg: uio#=%d, jpu_phys=%08lx(%08lx), jpeg_phys=%08lx(%08lx)",
	   data->jpu_uio_num, data->jpu_phys, data->jpu_size,
	   data->jpeg_phys, data->jpeg_size);
    D_INFO("libshjpeg: uio#=%d, veu_phys=%08lx(%08lx)",
	   data->veu_uio_num, data->veu_phys, data->veu_size);

    /* Map JPU registers and memory. */
    data->jpu_base = mmap(NULL, data->jpu_size,
			  PROT_READ | PROT_WRITE,
			  MAP_SHARED, data->jpu_uio_fd, 0);
    if (data->jpu_base == MAP_FAILED) {
	D_PERROR("libshjpeg: Could not map JPU MMIO!" );
	goto error;
    }

    /* Map contiguous memory for JPU. */
    data->jpeg_virt = mmap(NULL, data->jpeg_size,
			   PROT_READ | PROT_WRITE,
			   MAP_SHARED, data->jpu_uio_fd, getpagesize());
    if (data->jpeg_virt == MAP_FAILED) {
	D_PERROR("libshjpeg: Could not map /dev/mem at 0x%08x (length %lu)!",
		 getpagesize(), data->jpeg_size);
	goto error;
    }

    /* Map VEU registers. */
    data->veu_base = mmap(NULL, data->veu_size,
			  PROT_READ | PROT_WRITE,
			  MAP_SHARED, data->veu_uio_fd, 0);
    if (data->veu_base == MAP_FAILED) {
	D_PERROR( "libshjpeg: Could not map VEU MMIO!" );
	goto error;
    }

    /* initialize buffer base address */
    data->jpeg_lb1  = 
	data->jpeg_phys + SHJPEG_JPU_RELOAD_SIZE * 2; // line buffer 1
    data->jpeg_lb2  = 
	data->jpeg_lb1  + SHJPEG_JPU_LINEBUFFER_SIZE; // line buffer 2
    data->jpeg_data = 
	data->jpeg_lb2  + SHJPEG_JPU_LINEBUFFER_SIZE; // jpeg data

    /*
     * XXX: just in case, for the pending IRQ from the previous user
     * we release must unblock interrupt. unless we won't get IRQ.
     */
    if (lockf(data->jpu_uio_fd, F_LOCK, 0) < 0) {
    	D_PERROR("libshjpeg: Couldn't lock JPU UIO.");
	goto error;
    } else {
	int n = 1;

	if (write(data->jpu_uio_fd, &n, sizeof(n)) < sizeof(n)) {
	    D_PERROR("libshjpeg: unblock JPU IRQ failed.");
	    if (lockf(data->jpu_uio_fd, F_ULOCK, 0) < 0)
		D_PERROR("libshjpeg: may have failed to unlock JPU.");
	    goto error;
	}

	if (lockf(data->jpu_uio_fd, F_ULOCK, 0) < 0) {
	    D_PERROR("libshjpeg: Couldn't unlock JPU UIO.");
	    goto error;
	}
    }

    if (lockf(data->veu_uio_fd, F_LOCK, 0) < 0) {
    	D_PERROR("libshjpeg: Couldn't lock VEU UIO.");
	goto error;
    } else {
	int n = 1;

	if (write(data->veu_uio_fd, &n, sizeof(n)) < sizeof(n)) {
	    D_PERROR("libshjpeg: unblock VEU IRQ failed.");
	    if (lockf(data->veu_uio_fd, F_ULOCK, 0) < 0)
		D_PERROR("libshjpeg: may have failed to unlock VEU.");
	    goto error;
	}

	if (lockf(data->veu_uio_fd, F_ULOCK, 0) < 0) {
	    D_PERROR("libshjpeg: Couldn't unlock VEU UIO.");
	    goto error;
	}
    }

    return 0;

error:
    /* unmap memory in the case of error */
    uio_shutdown(data);

    return -1;
}

/*
 * Main routines
 */

static shjpeg_internal_t data = {
    .ref_count = 0,
    .uio_count = 0,
    .uio_device = NULL,
    .uio_dpath = NULL,
    .uio_caps = 0,
};

/*
 * init libshjpeg
 */

shjpeg_context_t*
shjpeg_init(int verbose)
{
    shjpeg_context_t *context;

    /* initialize context */
    if ((context = malloc(sizeof(shjpeg_context_t))) == NULL) {
	if (verbose)
	    perror("libshjpeg: Can't allocate libshjpeg context - ");
	return NULL;
    }
    memset((void*)context, 0, sizeof(shjpeg_context_t)); 

    data.context = context;
    context->internal_data = &data;
    context->verbose = verbose;

    D_INFO("libshjpeg: %s - allocated memory.", __FUNCTION__);

    /* check ref count */
    if (data.ref_count) {
	data.ref_count++;
	return context;
    }

    /* init uio */
    if (uio_init(context, &data)) {
	D_ERROR("libshjpeg: UIO initialization failed.");
	free(context);
	return NULL;
    }

    data.ref_count = 1;

    return context;
}

/*
 * shutdown libshjpeg
 */

void
shjpeg_shutdown(shjpeg_context_t *context)
{
    /* shutdown uio */
    uio_shutdown(&data);

    /* clean up */
    if (context)
	free(context);

    if (!data.ref_count)
	goto quit;

    if (--data.ref_count)
	goto quit;

quit:
    return;
}

/*
 * get contiguous memory info
 */

int
shjpeg_get_frame_buffer(shjpeg_context_t *context,
			unsigned long	 *phys,
			void		**buffer,
			size_t 		 *size )
{
    if ( !data.ref_count ) {
	D_ERROR("libshjpeg: not initialized yet.");
	return -1;
    }

    if ( phys )
	*phys	  = data.jpeg_data;

    if ( buffer )
	*buffer = (void*)data.jpeg_virt + SHJPEG_JPU_SIZE;

    if ( size )
	*size	  = data.jpeg_size - SHJPEG_JPU_SIZE;

    return 0;
}
