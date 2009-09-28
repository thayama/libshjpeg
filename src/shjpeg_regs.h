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

#ifndef __shjpu_regs_h__
#define __shjpu_regs_h__

/*
 * VEU Registers
 */

#define VEU_VESTR	0x0000
#define VEU_VESWR	0x0010
#define VEU_VESSR	0x0014
#define VEU_VSAYR	0x0018
#define VEU_VSACR	0x001c
#define VEU_VBSSR	0x0020
#define VEU_VEDWR	0x0030
#define VEU_VDAYR	0x0034
#define VEU_VDACR	0x0038
#define VEU_VTRCR	0x0050
#define VEU_VRFCR	0x0054
#define VEU_VRFSR	0x0058
#define VEU_VENHR	0x005c
#define VEU_VFMCR	0x0070
#define VEU_VVTCR	0x0074
#define VEU_VHTCR	0x0078
#define VEU_VAPCR	0x0080
#define VEU_VECCR	0x0084
#define VEU_VAFXR	0x0090
#define VEU_VSWPR	0x0094
#define VEU_VEIER	0x00a0
#define VEU_VEVTR	0x00a4
#define VEU_VSTAR	0x00b0
#define VEU_VBSRR	0x00b4

/*
 * JPU Registers
 */

#define JPU_JCMOD	0x0000	/* JPEG code mode register */
#define JPU_JCCMD	0x0004	/* JPEG code command register */
#define JPU_JCSTS	0x0008	/* JPEG code status register */
#define JPU_JCQTN	0x000C	/* JPEG code quantization table no. register */
#define JPU_JCHTN	0x0010	/* JPEG code Huffman table number register */
#define JPU_JCDRIU	0x0014	/* JPEG code DRI upper register */
#define JPU_JCDRID	0x0018	/* JPEG code DRI lower register */
#define JPU_JCVSZU	0x001C	/* JPEG code vertical size upper register */
#define JPU_JCVSZD	0x0020	/* JPEG code vertical size lower register */
#define JPU_JCHSZU	0x0024	/* JPEG code horizontal size upper register */
#define JPU_JCHSZD	0x0028	/* JPEG code horizontal size lower register */
#define JPU_JCDTCU	0x002C	/* JPEG code data count upper register */
#define JPU_JCDTCM	0x0030	/* JPEG code data count middle register */
#define JPU_JCDTCD	0x0034	/* JPEG code data count lower register */
#define JPU_JINTE	0x0038	/* JPEG interrupt enable register */
#define JPU_JINTS	0x003C	/* JPEG interrupt status register */
#define JPU_JCDERR	0x0040	/* JPEG code decode error register */
#define JPU_JCRST	0x0044	/* JPEG code reset register */
#define JPU_JIFCNT	0x0060	/* JPEG interface control register */
#define JPU_JIFECNT	0x0070	/* JPEG interface encoding control register */
#define JPU_JIFESYA1	0x0074	/* JPEG I/F encode src Y address register 1 */
#define JPU_JIFESCA1	0x0078	/* JPEG I/F encode src C address register 1 */
#define JPU_JIFESYA2	0x007C	/* JPEG I/F encode src Y address register 2 */
#define JPU_JIFESCA2	0x0080	/* JPEG I/F encode src C address register 2 */
#define JPU_JIFESMW	0x0084	/* JPEG I/F encode src memory width register */
#define JPU_JIFESVSZ	0x0088	/* JPEG I/F encode src V size register */
#define JPU_JIFESHSZ	0x008C	/* JPEG I/F encode src H size register */
#define JPU_JIFEDA1	0x0090	/* JPEG I/F encode dst address register 1 */
#define JPU_JIFEDA2	0x0094	/* JPEG I/F encode dst address register 2 */
#define JPU_JIFEDRSZ	0x0098	/* JPEG I/F encode data reload size register */
#define JPU_JIFDCNT	0x00A0	/* JPEG I/F decoding control register */
#define JPU_JIFDSA1	0x00A4	/* JPEG I/F decode src address register 1 */
#define JPU_JIFDSA2	0x00A8	/* JPEG I/F decode src address register 2 */
#define JPU_JIFDDRSZ	0x00AC	/* JPEG I/F decode data reload size register */
#define JPU_JIFDDMW	0x00B0	/* JPEG I/F decode dst memory width register */
#define JPU_JIFDDVSZ	0x00B4	/* JPEG I/F decode dst V size register */
#define JPU_JIFDDHSZ	0x00B8	/* JPEG I/F decode dst H size register */
#define JPU_JIFDDYA1	0x00BC	/* JPEG I/F decode dst Y address register 1 */
#define JPU_JIFDDCA1	0x00C0	/* JPEG I/F decode dst C address register 1 */
#define JPU_JIFDDYA2	0x00C4	/* JPEG I/F decode dst Y address register 2 */
#define JPU_JIFDDCA2	0x00C8	/* JPEG I/F decode dst C address register 2 */

/* JPEG code quantization table 0 register */
#define JPU_JCQTBL0(n)	(0x10000 + (((n)*4) & 0x3C))	// to 0x1003C

/* JPEG code quantization table 1 register */
#define JPU_JCQTBL1(n)	(0x10040 + (((n)*4) & 0x3C))	// to 0x1007C

/* JPEG code quantization table 2 register */
#define JPU_JCQTBL2(n)	(0x10080 + (((n)*4) & 0x3C))	// to 0x100BC

/* JPEG code quantization table 3 register */
#define JPU_JCQTBL3(n)	(0x100C0 + (((n)*4) & 0x3C))	// to 0x100FC

/* JPEG code Huffman table DC0 register */
#define JPU_JCHTBD0(n)	(0x10100 + (((n)*4) & 0x1C))	// to 0x10118

/* JPEG code Huffman table AC0 register */
#define JPU_JCHTBA0(n)	(0x10120 + (((n)*4) & 0xFC))	// to 0x10118

/* JPEG code Huffman table DC1 register */
#define JPU_JCHTBD1(n)	(0x10200 + (((n)*4) & 0x1C))	// to 0x1020C

/* JPEG code Huffman table AC1 register */
#define JPU_JCHTBA1(n)	(0x10220 + (((n)*4) & 0xFC))	// to 0x1022C

/*
 * JPU_JCCMD values
 */ 
#define JPU_JCCMD_START			0x00000001
#define JPU_JCCMD_RESTART		0x00000002
#define JPU_JCCMD_END			0x00000004
#define JPU_JCCMD_LCMD2			0x00000100
#define JPU_JCCMD_LCMD1			0x00000200
#define JPU_JCCMD_RESET			0x00000080
#define JPU_JCCMD_READ_RESTART		0x00000400
#define JPU_JCCMD_WRITE_RESTART		0x00000800
#define JPU_JCMOD_DSP_ENCODE		0x00000000
#define JPU_JCMOD_DSP_DECODE		0x00000008
#define JPU_JCMOD_INPUT_CTRL		0x00000080	// must always be set
#define JPU_JIFCNT_VJSEL_JPU		0x00000000
#define JPU_JIFCNT_VJSEL_VPU		0x00000002

/*
 * JPU_JIFECNT values
 */
#define JPU_JIFECNT_LINEBUF_MODE	0x00000002
#define JPU_JIFECNT_RELOAD_ENABLE	0x00000040
#define JPU_JIFECNT_SWAP_1234		0x00000000
#define JPU_JIFECNT_SWAP_2143		0x00000010
#define JPU_JIFECNT_SWAP_3412		0x00000020
#define JPU_JIFECNT_SWAP_4321		0x00000030

/*
 * JPU_JIFDCNT values
 */
#define JPU_JIFDCNT_LINEBUF_MODE	0x00000001
#define JPU_JIFDCNT_SWAP_1234		0x00000000
#define JPU_JIFDCNT_SWAP_2143		0x00000002
#define JPU_JIFDCNT_SWAP_3412		0x00000004
#define JPU_JIFDCNT_SWAP_4321		0x00000006
#define JPU_JIFDCNT_RELOAD_ENABLE	0x00000008

/*
 * JPU_JINTS values
 */
#define JPU_JINTS_MASK			0x00007CE8
#define JPU_JINTS_INS3_HEADER		0x00000008
#define JPU_JINTS_INS5_ERROR		0x00000020
#define JPU_JINTS_INS6_DONE		0x00000040
#define JPU_JINTS_INS10_XFER_DONE	0x00000400
#define JPU_JINTS_INS11_LINEBUF0	0x00000800
#define JPU_JINTS_INS12_LINEBUF1	0x00001000
#define JPU_JINTS_INS13_LOADED		0x00002000
#define JPU_JINTS_INS14_RELOAD		0x00004000

/*
 * For Debug Purpose (defined in shjpu_regs.h)
 */

extern const char *jpu_reg_str[];
extern const char *veu_reg_str[];

#endif /* !__shjpu_regs_h__ */
