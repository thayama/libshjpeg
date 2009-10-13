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

#ifndef __shjpeg_jpu_h__
#define __shjpeg_jpu_h__

#include "shjpeg_regs.h"
#include "shjpeg_utils.h"

#define SHJPEG_JPU_RELOAD_SIZE       (64 * 1024)
#define SHJPEG_JPU_LINEBUFFER_PITCH  (2560)
#define SHJPEG_JPU_LINEBUFFER_HEIGHT (16)
#define SHJPEG_JPU_LINEBUFFER_SIZE   (SHJPEG_JPU_LINEBUFFER_PITCH * SHJPEG_JPU_LINEBUFFER_HEIGHT * 2)
#define SHJPEG_JPU_LINEBUFFER_SIZE_Y (SHJPEG_JPU_LINEBUFFER_PITCH * SHJPEG_JPU_LINEBUFFER_HEIGHT)
#define SHJPEG_JPU_SIZE              (SHJPEG_JPU_LINEBUFFER_SIZE * 2 + SHJPEG_JPU_RELOAD_SIZE * 2)

typedef enum {
    SHJPEG_JPU_START,
    SHJPEG_JPU_RUN,
    SHJPEG_JPU_END
} shjpeg_jpu_state_t;

typedef enum {
    SHJPEG_JPU_FLAG_RELOAD  = 0x00000001, /* enable reload mode */
    SHJPEG_JPU_FLAG_CONVERT = 0x00000002, /* enable conversion through VEU */
    SHJPEG_JPU_FLAG_ENCODE  = 0x00000004  /* set encoding mode */
} shjpeg_jpu_flags_t;

typedef struct {
    /* starting, running or ended (done/error) */
    shjpeg_jpu_state_t state;   
    /* control decoding options */  
    shjpeg_jpu_flags_t flags;   

    /* input = loaded buffers, output = buffers to reload */
    u32             buffers; 
    /* valid in END state, non-zero means error */
    u32             error;   

    int             height;

    /* to update VEU_VSAYR/VEU_VSACR */
    u32	     sa_y;
    u32	     sa_c;
    u32	     sa_inc;
} shjpeg_jpu_t;

/* read/write from/to registers */
static inline u32
shjpeg_jpu_getreg32(shjpeg_internal_t  *data,
		    u32		   	address)
{
    D_ASSERT( address < data->jpu_size );

    return *(volatile u32*)(data->jpu_base + address);
}

static inline void
shjpeg_jpu_setreg32(shjpeg_internal_t *data,
		    u32		       address,
		    u32		       value)
{
#ifdef SHJPEG_DEBUG
    shjpeg_context_t *context = data->context;
#endif
    D_ASSERT( address < data->jpu_size );

    *(volatile u32*)(data->jpu_base + address) = value;

    if (address <= JPU_JIFDDCA2)
    D_INFO("%s: written %08x(%08x) at %s(%08x)",
	   __FUNCTION__, value, shjpeg_jpu_getreg32(data, address),
	   jpu_reg_str[address >> 2], address );
}

static inline u32
shjpeg_veu_getreg32(shjpeg_internal_t *data,
		    u32                address)
{
    D_ASSERT( address < data->veu_size );
    
    return *(volatile u32*)(data->veu_base + address);
}

static inline void
shjpeg_veu_setreg32(shjpeg_internal_t	*data,
		    u32			 address,
		    u32			 value)
{
#ifdef SHJPEG_DEBUG
    shjpeg_context_t *context = data->context;
#endif
    D_ASSERT( address < data->veu_size );
    
    *(volatile u32*)(data->veu_base + address) = value;

    D_INFO("%s: written %08x(%08x) at %s(%08x)",
	   __FUNCTION__, value, shjpeg_veu_getreg32(data, address),
	   veu_reg_str[address >> 2], address );
}

/* external function */
int shjpeg_run_jpu(shjpeg_context_t *context, shjpeg_internal_t *data,
		   shjpeg_jpu_t *jpeg);

#endif /* !__shjpeg_jpu_h__ */
