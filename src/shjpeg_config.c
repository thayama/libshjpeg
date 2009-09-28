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

#include "shjpeg_regs.h"

/*
 * For Debug Purpose
 */

const char *jpu_reg_str[] = {
    "JCMOD",			// 0x00
    "JCCMD",
    "JCSTS",
    "JCQTN",

    "JCHTN",			// 0x10
    "JCDRIU",
    "JCDRID",
    "JCVSZU",

    "JCVSZD",			// 0x20
    "JCHSZU",
    "JCHSZD",
    "JCDTCU",

    "JCDTCM",			// 0x30
    "JCDTCD",
    "JINTE",
    "JINTS",

    "JCDERR",			// 0x40
    "JCRST",
    "-",
    "-",

    "-", "-", "-", "-",		// 0x50

    "JIFCNT",			// 0x60
    "-", "-", "-",

    "JIFECNT",			// 0x70
    "JIFESYA1",
    "JIFESCA1",
    "JIFESYA2",

    "JIFESCA2",			// 0x80
    "JIFESMW",
    "JIFESVSZ",
    "JIFESHSZ",
		
    "JIFEDA1",			// 0x90
    "JIFEDA2",
    "JIFEDRSZ", "-",
		
    "JIFDCNT",			// 0xa0
    "JIFDSA1",
    "JIFDSA2",
    "JIFDDRSZ",

    "JIFDDMW",			// 0xb0
    "JIFDDVSZ",
    "JIFDDHSZ",
    "JIFDDYA1",
		
    "JIFDDCA1",			// 0xc0
    "JIFDDYA2",
    "JIFDDCA2", "-",
};

const char *veu_reg_str[] = {
    "VESTR",		// 0x000
    "-",
    "-",
    "-",

    "VESWR",		// 0x010
    "VESSR",
    "VSAYR",
    "VSACR",

    "VBSSR",		// 0x020
    "-",
    "-",
    "-",

    "VEDWR",		// 0x030
    "VDAYR",
    "VDACR",
    "-",

    "-",		// 0x040
    "-",
    "-",
    "-",

    "VTRCR",		// 0x050
    "VRFCR",
    "VRFSR",
    "VENHR",

    "-",		// 0x060
    "-",
    "-",
    "-",

    "VFMCR",		// 0x070
    "VVTCR",
    "VHTCR",
    "-",

    "VAPCR",		// 0x080
    "VECCR",
    "-",
    "-",

    "VAFXR",		// 0x090
    "VSWPR",
    "-",
    "-",

    "VEIER",		// 0x0a0
    "VEVTR",
    "-",
    "-",

    "VSTAR",		// 0x0b0
    "VBSRR",
    "-",
    "-"
};
