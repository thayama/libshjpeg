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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

#ifndef __utils_h__
#define __utils_h__

/**
 * \file utils.h
 *
 * Utilities
 */

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#define D_ERROR(s, x...) \
    { if (context->verbose) fprintf(stderr, s "\n", ## x); }

#define D_DERROR(r, s, x...) \
    { if (context->verbose) \
	    fprintf(stderr, s " - return value=%d\n", ## x, r); }

#define D_PERROR(s, x...) \
    { if (context->verbose) \
	    fprintf(stderr, s " - %s.\n", ## x, strerror(errno)); }

#ifdef SHJPEG_DEBUG

#  define D_BUG(s, x...)	fprintf( stderr, s "\n", ## x )
#  define D_INFO(s, x...)	fprintf( stderr, s "\n", ## x )
#  define D_ONCE(s, x...) \
    { static int once = 1; \
      if (once-- > 0) fprintf( stderr, s "\n", ## x ); } 
#  define D_DEBUG_AT(d, s, x...)         fprintf( stderr, "%s - " s "\n", __FUNCTION__, ## x )
#  define D_ASSERT(exp)  assert(exp)
#  define D_UNIMPLEMENTED()  \
    fprintf( stderr, "Unimplemented %s!\n", __FUNCTION__ )

#else

#  define D_BUG(x...)                
#  define D_INFO(x...)               
#  define D_ONCE(x...)               
#  define D_DEBUG_AT(d,x...)         
#  define D_ASSERT(exp)              
#  define D_UNIMPLEMENTED()          

#endif

#define _PAGE_SIZE (getpagesize())
#define _PAGE_ALIGN(len) (((len) + _PAGE_SIZE - 1) & ~(_PAGE_SIZE - 1))

#define direct_page_align(a)	_PAGE_ALIGN(a)	// deprecated

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

/*
 * register access
 */

static inline u32
shjpeg_getreg32(void *base, 
		u32 address)
{
    return *(volatile u32*)(base + address);
}

static inline void
shjpeg_setreg32(void *base, 
		u32   address,
		u32   value)
{
    *(volatile u32*)(base + address) = value;
}

#endif
