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

#include "shjpeg.h"
#include "shjpeg_internal.h"
#include "shjpeg_jpu.h"

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

    /*
     * Kernel based state machine
     *
     * Execution enters the kernel and only returns to user space for
     *	 - end of encoding
     *	 - error in encoding
     *	 - buffer loaded
     *
     * TODO
     * - finish clipping (maybe not all is possible without tricky code)
     */

    /*
      if (format != DSPF_NV12 && format != DSPF_NV16) {
      D_UNIMPLEMENTED();
      return -1;
      }
    */

    /* Init VEU transformation control (format conversion). */
    if (format != SHJPEG_PF_NV16)
	mode420 = true;
    else
	vtrcr |= (1 << 22);

    switch (format) {
    case SHJPEG_PF_NV12:
	vswpin = 0x06; //0x07
	break;

    case SHJPEG_PF_NV16:
	vswpin  = 0x07;
	vtrcr	 |= (1 << 14);
	break;

    case SHJPEG_PF_RGB16:
	vswpin  = 0x06;
	vtrcr	 |= (3 << 8) | 3;
	break;

    case SHJPEG_PF_RGB32:
	vswpin  = 0x04;
	vtrcr	 |= (0 << 8) | 3;
	break;

    case SHJPEG_PF_RGB24:
	vswpin  = 0x07;
	vtrcr	 |= (2 << 8) | 3;
	break;

    default:
	D_BUG( "unexpected format %d", format);
	return -1;
    }

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
    shjpeg_jpu_setreg32(data, JPU_JCCMD, JPU_JCCMD_RESET);
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

	/* FIXME: Setup VEU for conversion/scaling (from surface to line buffer). */
	shjpeg_veu_setreg32(data, VEU_VESTR, 0x00000000);
	shjpeg_veu_setreg32(data, VEU_VESWR, pitch);
	shjpeg_veu_setreg32(data, VEU_VESSR, 
			    (context->height << 16) | context->width);
	shjpeg_veu_setreg32(data, VEU_VBSSR, 16);
	shjpeg_veu_setreg32(data, VEU_VEDWR, SHJPEG_JPU_LINEBUFFER_PITCH);
	shjpeg_veu_setreg32(data, VEU_VDAYR, data->jpeg_lb1);
	shjpeg_veu_setreg32(data, VEU_VDACR, 
			    data->jpeg_lb1 + SHJPEG_JPU_LINEBUFFER_SIZE_Y);
	shjpeg_veu_setreg32(data, VEU_VSAYR, phys);
	shjpeg_veu_setreg32(data, VEU_VSACR, phys + pitch * height);
	shjpeg_veu_setreg32(data, VEU_VTRCR, vtrcr);

	jpeg.sa_y = phys;
	jpeg.sa_c = phys + pitch * height;
	jpeg.sa_inc = pitch * 16;

	shjpeg_veu_setreg32(data, VEU_VRFCR, 0x00000000);
	shjpeg_veu_setreg32(data, VEU_VRFSR, 
			    (context->height << 16) | context->width);

	shjpeg_veu_setreg32(data, VEU_VENHR, 0x00000000);
	shjpeg_veu_setreg32(data, VEU_VFMCR, 0x00000000);
	shjpeg_veu_setreg32(data, VEU_VAPCR, 0x00000000);
	shjpeg_veu_setreg32(data, VEU_VSWPR, 0x00000070 | vswpin);
	shjpeg_veu_setreg32(data, VEU_VEIER, 0x00000101);
    }

    /* Init quantization tables. */
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0( 0), 0x100B0B0E);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0( 1), 0x0C0A100E);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0( 2), 0x0D0E1211);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0( 3), 0x10131828);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0( 4), 0x1A181616);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0( 5), 0x18312325);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0( 6), 0x1D283A33);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0( 7), 0x3D3C3933);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0( 8), 0x38374048);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0( 9), 0x5C4E4044);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0(10), 0x57453738);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0(11), 0x506D5157);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0(12), 0x5F626768);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0(13), 0x673E4D71);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0(14), 0x79706478);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL0(15), 0x5C656763);

    shjpeg_jpu_setreg32(data, JPU_JCQTBL1( 0), 0x11121218);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL1( 1), 0x15182F1A);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL1( 2), 0x1A2F6342);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL1( 3), 0x38426363);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL1( 4), 0x63636363);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL1( 5), 0x63636363);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL1( 6), 0x63636363);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL1( 7), 0x63636363);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL1( 8), 0x63636363);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL1( 9), 0x63636363);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL1(10), 0x63636363);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL1(11), 0x63636363);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL1(12), 0x63636363);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL1(13), 0x63636363);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL1(14), 0x63636363);
    shjpeg_jpu_setreg32(data, JPU_JCQTBL1(15), 0x63636363);

    /* Init huffman tables. */
    shjpeg_jpu_setreg32(data, JPU_JCHTBD0(0), 0x00010501);
    shjpeg_jpu_setreg32(data, JPU_JCHTBD0(1), 0x01010101);
    shjpeg_jpu_setreg32(data, JPU_JCHTBD0(2), 0x01000000);
    shjpeg_jpu_setreg32(data, JPU_JCHTBD0(3), 0x00000000);
    shjpeg_jpu_setreg32(data, JPU_JCHTBD0(4), 0x00010203);
    shjpeg_jpu_setreg32(data, JPU_JCHTBD0(5), 0x04050607);
    shjpeg_jpu_setreg32(data, JPU_JCHTBD0(6), 0x08090A0B);

    shjpeg_jpu_setreg32(data, JPU_JCHTBD1(0), 0x00030101);
    shjpeg_jpu_setreg32(data, JPU_JCHTBD1(1), 0x01010101);
    shjpeg_jpu_setreg32(data, JPU_JCHTBD1(2), 0x01010100);
    shjpeg_jpu_setreg32(data, JPU_JCHTBD1(3), 0x00000000);
    shjpeg_jpu_setreg32(data, JPU_JCHTBD1(4), 0x00010203);
    shjpeg_jpu_setreg32(data, JPU_JCHTBD1(5), 0x04050607);
    shjpeg_jpu_setreg32(data, JPU_JCHTBD1(6), 0x08090A0B);

    shjpeg_jpu_setreg32(data, JPU_JCHTBA0( 0), 0x00020103);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0( 1), 0x03020403);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0( 2), 0x05050404);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0( 3), 0x0000017D);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0( 4), 0x01020300);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0( 5), 0x04110512);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0( 6), 0x21314106);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0( 7), 0x13516107);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0( 8), 0x22711432);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0( 9), 0x8191A108);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(10), 0x2342B1C1);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(11), 0x1552D1F0);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(12), 0x24336272);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(13), 0x82090A16);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(14), 0x1718191A);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(15), 0x25262728);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(16), 0x292A3435);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(17), 0x36373839);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(18), 0x3A434445);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(19), 0x46474849);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(20), 0x4A535455);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(21), 0x56575859);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(22), 0x5A636465);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(23), 0x66676869);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(24), 0x6A737475);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(25), 0x76777879);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(26), 0x7A838485);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(27), 0x86878889);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(28), 0x8A929394);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(29), 0x95969798);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(30), 0x999AA2A3);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(31), 0xA4A5A6A7);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(32), 0xA8A9AAB2);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(33), 0xB3B4B5B6);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(34), 0xB7B8B9BA);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(35), 0xC2C3C4C5);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(36), 0xC6C7C8C9);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(37), 0xCAD2D3D4);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(38), 0xD5D6D7D8);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(39), 0xD9DAE1E2);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(40), 0xE3E4E5E6);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(41), 0xE7E8E9EA);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(42), 0xF1F2F3F4);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(43), 0xF5F6F7F8);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA0(44), 0xF9FA0000);

    shjpeg_jpu_setreg32(data, JPU_JCHTBA1( 0), 0x00020102);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1( 1), 0x04040304);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1( 2), 0x07050404);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1( 3), 0x00010277);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1( 4), 0x00010203);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1( 5), 0x11040521);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1( 6), 0x31061241);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1( 7), 0x51076171);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1( 8), 0x13223281);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1( 9), 0x08144291);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(10), 0xA1B1C109);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(11), 0x233352F0);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(12), 0x156272D1);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(13), 0x0A162434);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(14), 0xE125F117);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(15), 0x18191A26);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(16), 0x2728292A);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(17), 0x35363738);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(18), 0x393A4344);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(19), 0x45464748);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(20), 0x494A5354);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(21), 0x55565758);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(22), 0x595A6364);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(23), 0x65666768);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(24), 0x696A7374);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(25), 0x75767778);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(26), 0x797A8283);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(27), 0x84858687);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(28), 0x88898A92);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(29), 0x93949596);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(30), 0x9798999A);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(31), 0xA2A3A4A5);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(32), 0xA6A7A8A9);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(33), 0xAAB2B3B4);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(34), 0xB5B6B7B8);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(35), 0xB9BAC2C3);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(36), 0xC4C5C6C7);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(37), 0xC8C9CAD2);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(38), 0xD3D4D5D6);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(39), 0xD7D8D9DA);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(40), 0xE2E3E4E5);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(41), 0xE6E7E8E9);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(42), 0xEAF2F3F4);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(43), 0xF5F6F7F8);
    shjpeg_jpu_setreg32(data, JPU_JCHTBA1(44), 0xF9FA0000);

    D_DEBUG_AT( SH7722_JPEG, "	 -> starting...");

    /* State machine. */
    for(;;) {
	/* Run the state machine. */
	if (shjpeg_run_jpu(context, data, &jpeg) < 0) {
	    D_PERROR( "libshjpeg: shjpeg_run_jpu() failed!");
	    ret = -1;
	    break;
	}

	D_ASSERT( jpeg.state != SHJPEG_JPU_START);

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
