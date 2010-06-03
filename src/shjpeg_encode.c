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

#include <shjpeg/shjpeg.h>
#include "shjpeg_internal.h"
#include "shjpeg_jpu.h"
#include "shjpeg_veu.h"

static inline int
coded_data_amount(shjpeg_internal_t *data)
{
    return (shjpeg_jpu_getreg32(data, JPU_JCDTCU) << 16) | 
	(shjpeg_jpu_getreg32(data, JPU_JCDTCM) <<  8) | 
	(shjpeg_jpu_getreg32(data, JPU_JCDTCD));
}

static int
encode_hw(shjpeg_internal_t	*data,
	  shjpeg_context_t	*context,
	  shjpeg_pixelformat	 format,
	  unsigned long		 phys,
	  int		 	 width,
	  int		 	 height,
	  int			 pitch)
{
    int			ret = 0;
    int			i, fd = -1;
    int			written = 0;
    u32			vtrcr   = 0;
    u32			vswpin  = 0;
    bool 		mode420 = false;
    shjpeg_jpu_t	jpeg;

    D_DEBUG_AT(SH7722_JPEG, "( %p, 0x%08lx|%d [%dx%d])", 
	       data, phys, pitch, width, height);

    /* Init VEU transformation control (format conversion). */
    if (format == SHJPEG_PF_NV12)
	mode420 = true;
    else
	vtrcr |= (1 << 22);

    switch (format) {
    case SHJPEG_PF_NV12:
	vswpin = 0x66;
	break;

    case SHJPEG_PF_NV16:
	vswpin  = 0x77;
	vtrcr	 |= (1 << 14);
	break;

    case SHJPEG_PF_RGB16:
	vswpin  = 0x76;
	vtrcr	 |= (3 << 8) | 3;
	break;

    case SHJPEG_PF_RGB32:
	vswpin  = 0x44;
	vtrcr	 |= (0 << 8) | 3;
	break;

    case SHJPEG_PF_RGB24:
	vswpin  = 0x77;
	vtrcr	 |= (2 << 8) | 3;
	break;

    default:
	D_BUG( "unexpected format %d", format);
	return -1;
    }

    vtrcr |= (0x1) << 2;

    /* Calculate source base address. */
    //phys += rect->x + rect->y * pitch;

    D_DEBUG_AT( SH7722_JPEG, "	 -> locking JPU...");

    /* Locking JPU using lockf(3) */
    if ( lockf( data->jpu_uio_fd, F_LOCK, 0 ) < 0 ) {
	D_PERROR( "libshjpeg: Could not lock JPEG engine!");
	return -1;
    }

    D_DEBUG_AT(SH7722_JPEG, "	 -> opening file for writing...");

    if (context->sops->init)
	context->sops->init(context->private);

    D_DEBUG_AT( SH7722_JPEG, "	 -> setting...");

    /* Initialize JPEG state. */
    jpeg.state	  = SHJPEG_JPU_START;
    jpeg.flags	  = SHJPEG_JPU_FLAG_ENCODE;
    jpeg.buffers = 3;

    /* Always enable reload mode. */
    jpeg.flags |= SHJPEG_JPU_FLAG_RELOAD;

    /* Program JPU from RESET. */
    shjpeg_jpu_reset(data);
    shjpeg_jpu_setreg32(data, JPU_JCMOD, 
			JPU_JCMOD_INPUT_CTRL | JPU_JCMOD_DSP_ENCODE | 
			(mode420 ? 2 : 1));

    shjpeg_jpu_setreg32(data, JPU_JCQTN,   0x14); //0x14
    shjpeg_jpu_setreg32(data, JPU_JCHTN,   0x3c); //0x3c
    shjpeg_jpu_setreg32(data, JPU_JCDRIU,  0x02);
    shjpeg_jpu_setreg32(data, JPU_JCDRID,  0x00);
    shjpeg_jpu_setreg32(data, JPU_JCHSZU,  width >> 8);
    shjpeg_jpu_setreg32(data, JPU_JCHSZD,  width & 0xff);
    shjpeg_jpu_setreg32(data, JPU_JCVSZU,  height >> 8);
    shjpeg_jpu_setreg32(data, JPU_JCVSZD,  height & 0xff);
    shjpeg_jpu_setreg32(data, JPU_JIFCNT,  JPU_JIFCNT_VJSEL_JPU);
    shjpeg_jpu_setreg32(data, JPU_JIFDCNT, JPU_JIFDCNT_SWAP_4321);
    shjpeg_jpu_setreg32(data, JPU_JIFEDA1, data->jpeg_phys);
    shjpeg_jpu_setreg32(data, JPU_JIFEDA2, 
			data->jpeg_phys + SHJPEG_JPU_RELOAD_SIZE);
    shjpeg_jpu_setreg32(data, JPU_JIFEDRSZ, SHJPEG_JPU_RELOAD_SIZE);
    shjpeg_jpu_setreg32(data, JPU_JIFESHSZ, width);
    shjpeg_jpu_setreg32(data, JPU_JIFESVSZ, height);

    if (format == SHJPEG_PF_NV12 || format == SHJPEG_PF_NV16)
    {
	/* Setup JPU for encoding in frame mode (directly from surface). */
	shjpeg_jpu_setreg32(data, JPU_JINTE,	  
			    JPU_JINTS_INS10_XFER_DONE |JPU_JINTS_INS13_LOADED);
	shjpeg_jpu_setreg32(data, JPU_JIFECNT, 
			    JPU_JIFECNT_SWAP_4321 | 
			    JPU_JIFECNT_RELOAD_ENABLE | (mode420 ? 1 : 0));

	shjpeg_jpu_setreg32(data, JPU_JIFESYA1, phys);
	shjpeg_jpu_setreg32(data, JPU_JIFESCA1, phys + pitch * height);
	shjpeg_jpu_setreg32(data, JPU_JIFESMW,  pitch);
    }
    else {
	shjpeg_veu_t veu;

	jpeg.flags |= SHJPEG_JPU_FLAG_CONVERT;
	jpeg.height = height;

	/* Setup JPU for encoding in line buffer mode. */
	shjpeg_jpu_setreg32(data, JPU_JINTE, 
			    JPU_JINTS_INS11_LINEBUF0 |
			    JPU_JINTS_INS12_LINEBUF1 |
			    JPU_JINTS_INS10_XFER_DONE |
			    JPU_JINTS_INS13_LOADED);
	shjpeg_jpu_setreg32(data, JPU_JIFECNT, 
			    JPU_JIFECNT_LINEBUF_MODE | 
			    (SHJPEG_JPU_LINEBUFFER_HEIGHT << 16) |
			    JPU_JIFECNT_SWAP_4321 |
			    JPU_JIFECNT_RELOAD_ENABLE | (mode420 ? 1 : 0));

	shjpeg_jpu_setreg32(data, JPU_JIFESYA1, data->jpeg_lb1);
	shjpeg_jpu_setreg32(data, JPU_JIFESCA1, 
			    data->jpeg_lb1 + SHJPEG_JPU_LINEBUFFER_SIZE_Y);
	shjpeg_jpu_setreg32(data, JPU_JIFESYA2, data->jpeg_lb2);
	shjpeg_jpu_setreg32(data, JPU_JIFESCA2, 
			    data->jpeg_lb2 + SHJPEG_JPU_LINEBUFFER_SIZE_Y);
	shjpeg_jpu_setreg32(data, JPU_JIFESMW,  
			    SHJPEG_JPU_LINEBUFFER_PITCH);

	/* Setup VEU for conversion/scaling (from surface to line buffer). */
	memset((void*)&veu, 0, sizeof(shjpeg_veu_t));

	/* source */
	veu.src.width	= context->width;
	veu.src.height	= SHJPEG_JPU_LINEBUFFER_HEIGHT;
	veu.src.pitch	= pitch;
	veu.src.yaddr	= phys;
	veu.src.caddr	= phys + pitch * height;

	/* destination */
	veu.dst.width	= context->width;
	veu.dst.height	= context->height;
	veu.dst.pitch	= SHJPEG_JPU_LINEBUFFER_PITCH;
	veu.dst.yaddr	= data->jpeg_lb1;
	veu.dst.caddr	= data->jpeg_lb1 + SHJPEG_JPU_LINEBUFFER_SIZE_Y;

	/* transformation parameter */
	veu.vbssr	= 16;
	veu.vtrcr	= vtrcr;
	veu.vswpr	= vswpin;

	/* set VEU */
	shjpeg_veu_init(data, &veu);

	/* configs */
	jpeg.sa_y = phys;
	jpeg.sa_c = phys + pitch * height;
	jpeg.sa_inc = pitch * 16;
    }

    /* init QT/HT */
    shjpeg_jpu_init_quantization_table(data);
    shjpeg_jpu_init_huffman_table(data);

    D_DEBUG_AT( SH7722_JPEG, "	 -> starting...");

    /* State machine. */
    for(;;) {
	/* Run the state machine. */
	if (shjpeg_jpu_run(context, data, &jpeg) < 0) {
	    D_PERROR( "libshjpeg: shjpeg_jpu_run() failed!");
	    ret = -1;
	    break;
	}

	D_ASSERT(jpeg.state != SHJPEG_JPU_START);

	/* Check for loaded buffers. */
	for (i=1; i<=2; i++) {
	    if (jpeg.buffers & i) {
		int amount = coded_data_amount(data) - written;
		size_t len;
		void *ptr;

		if (amount > SHJPEG_JPU_RELOAD_SIZE)
		    amount = SHJPEG_JPU_RELOAD_SIZE;

		D_INFO("libshjpeg: Coded data amount: + %5d (buffer %d)", 
		       amount, i);

		ptr = (void*)data->jpeg_virt + (i-1) * SHJPEG_JPU_RELOAD_SIZE;
		len = amount;
		context->sops->write(context->private, &len, ptr);
	    }
	}

	/* Handle end (or error). */
	if (jpeg.state == SHJPEG_JPU_END) {
	    if (jpeg.error) {
		D_ERROR("libshjpeg: ERROR 0x%x!", jpeg.error);
		ret = -1;
	    }

	    break;
	}
    }

    D_INFO("libshjpeg: Coded data amount: = %5d (written: %d, buffers: %d)",
	   coded_data_amount(data), written, jpeg.buffers);


    /* Unlocking JPU using lockf(3) */
    if ( lockf(data->jpu_uio_fd, F_ULOCK, 0 ) < 0 ) {
	ret = -1;
	D_PERROR( "libshjpeg: Could not unlock JPEG engine!");
    }

    close(fd);

    return ret;
}

/*
 * shpjpeg_encode()
 */

int
shjpeg_encode(shjpeg_context_t	*context,
	      shjpeg_pixelformat format,
	      unsigned long	 phys,
	      int		 width,
	      int		 height,
	      int		 pitch)
{
    shjpeg_internal_t *data;

    if (!context) {
	D_ERROR("libjpeg: invalid context passed.");
	return -1;
    }

    data = (shjpeg_internal_t*)context->internal_data;

    /* check ref counter */
    if (!data->ref_count) {
        D_ERROR("libshjpeg: not initialized yet.");
        return -1;
    }

    /* if physical address is not given, use the default */
    if (phys == SHJPEG_USE_DEFAULT_BUFFER)
	phys = data->jpeg_data;

    switch (format) {
    case SHJPEG_PF_NV12:
    case SHJPEG_PF_NV16:
    case SHJPEG_PF_RGB16:
    case SHJPEG_PF_RGB32:
    case SHJPEG_PF_RGB24:
	break;

    default:
	return -1;
    }

    /* TODO: Support for clipping and resize */

    /* start hardware encoding */
    return encode_hw(data, context, format, phys, width, height, pitch);
}
