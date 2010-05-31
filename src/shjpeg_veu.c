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
#include <unistd.h>

#include <shjpeg/shjpeg.h>
#include "shjpeg_internal.h"
#include "shjpeg_jpu.h"
#include "shjpeg_veu.h"

int shjpeg_veu_init(shjpeg_internal_t *data, shjpeg_veu_t *veu)
{
    /* 
     * Setup VEU for conversion/scaling (from line buffer to surface). 
     */

    /*
     * 0. Generic setting
     */

    /* stop VEU - make sure VEU is stopped */
    shjpeg_veu_setreg32(data, VEU_VESTR, 0x00000000);
    while(shjpeg_veu_getreg32(data, VEU_VESTR))
	usleep(1);
    
    /* reset VEU module */
    shjpeg_veu_setreg32(data, VEU_VBSRR, 0x00000100);

    /*
     * 1. Set source details
     */

    /* set source stride */
    shjpeg_veu_setreg32(data, VEU_VESWR, veu->src.pitch);

    /* set width and height */
    shjpeg_veu_setreg32(data, VEU_VESSR, 
			(veu->src.height << 16) | veu->src.width);

    /* set source Y address */
    shjpeg_veu_setreg32(data, VEU_VSAYR, veu->src.yaddr);

    /* set source address */
    shjpeg_veu_setreg32(data, VEU_VSACR, veu->src.caddr);

    /* set lines to read during bundle mode */
    shjpeg_veu_setreg32(data, VEU_VBSSR, veu->vbssr);

    /* 
     * 2. Set destination details
     */

    /* set destination stride */
    shjpeg_veu_setreg32(data, VEU_VEDWR, veu->dst.pitch);

    /* set destination Y address */
    shjpeg_veu_setreg32(data, VEU_VDAYR, veu->dst.yaddr);

    /* set destination C address */
    shjpeg_veu_setreg32(data, VEU_VDACR, veu->dst.caddr);

    /*
     * 3. Set transformation related parameters
     */

    /* set transformation parameter */
    shjpeg_veu_setreg32(data, VEU_VTRCR, veu->vtrcr);
    
    /* set resize register */
    shjpeg_veu_setreg32(data, VEU_VRFCR, 0);	/* XXX: no resize for now */
    shjpeg_veu_setreg32(data, VEU_VRFSR, 
			(veu->dst.height << 16) | veu->dst.width);

    /* edge enhancer */
    shjpeg_veu_setreg32(data, VEU_VENHR, veu->venhr);

    /* filter mode */
    shjpeg_veu_setreg32(data, VEU_VFMCR, veu->vfmcr);

    /* chroma-key */
    shjpeg_veu_setreg32(data, VEU_VAPCR, veu->vapcr);

    /* byte swap setting */
    shjpeg_veu_setreg32(data, VEU_VSWPR, veu->vswpr);

    /* 
     * 4. VEU3F work around
     */
    if (data->uio_caps & UIO_CAPS_VEU3F) {
//	shjpeg_veu_setreg32(data, VEU_VRPBR, 0x00000000);
	shjpeg_veu_setreg32(data, VEU_VRPBR, 0x00400040);
    }

    /*
     * 5. Enable interrupt
     */
    shjpeg_veu_setreg32(data, VEU_VEIER, 0x00000101);

    return 0 ;
}
