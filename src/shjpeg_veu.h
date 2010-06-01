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

#ifndef __shjpeg_veu_h__
#define __shjpeg_veu_h__

#include "shjpeg_regs.h"
#include "shjpeg_utils.h"

typedef struct {
    u32		width;
    u32		height;
    u32		pitch;
    u32		yaddr;
    u32		caddr;
} shjpeg_veu_plane_t;

typedef struct {
    shjpeg_veu_plane_t	src;		/* source plane setting */
    shjpeg_veu_plane_t	dst;		/* destination plane setting */
    u32			vbssr;		/* # of lines for bundle read mode */
    u32			vtrcr;		/* transform register */
    u32			venhr;
    u32			vfmcr;
    u32			vapcr;		/* chroma key */
    u32			vswpr;		/* swap register */
} shjpeg_veu_t;


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
    D_ASSERT( address < data->veu_size );
    
    *(volatile u32*)(data->veu_base + address) = value;

#ifdef SHJPEG_DEBUG
    {
	shjpeg_context_t *context = data->context;
	D_INFO("%s: written %08x(%08x) at %s(%08x)",
	       __FUNCTION__, value, shjpeg_veu_getreg32(data, address),
	       veu_reg_str[address >> 2], address );
    }
#endif
}

/* external function */
int shjpeg_veu_init(shjpeg_internal_t *data, shjpeg_veu_t *veu);
void shjpeg_veu_set_dst_jpu(shjpeg_internal_t*);
void shjpeg_veu_set_src_jpu(shjpeg_internal_t*);
void shjpeg_veu_set_src(shjpeg_internal_t*, u32, u32);
void shjpeg_veu_start(shjpeg_internal_t*, int);
void shjpeg_veu_stop(shjpeg_internal_t*);

#endif /* !__shjpeg_jpu_h__ */
