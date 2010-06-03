/*
 * libshjpeg: A library for controlling SH-Mobile JPEG hardware codec
 *
 * Copyright (C) 2009,2010 IGEL Co.,Ltd.
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
#include <poll.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <shjpeg/shjpeg.h>
#include "shjpeg_internal.h"
#include "shjpeg_regs.h"
#include "shjpeg_jpu.h"
#include "shjpeg_veu.h"

/*
 * reset JPU
 */
void 
shjpeg_jpu_reset(shjpeg_internal_t *data)
{
    /* bus reset */
    shjpeg_jpu_setreg32(data, JPU_JCCMD, 0x80);
    
    /* software reset */
    shjpeg_jpu_setreg32(data, JPU_JCCMD, 0x1000);

    /* wait for reset */
    while(shjpeg_jpu_getreg32(data, JPU_JCCMD) & 0x1000)
	usleep(1);
}

/*
 * Main JPU control
 */

int
shjpeg_jpu_run(shjpeg_context_t	 *context, 
	       shjpeg_internal_t *data,
	       shjpeg_jpu_t 	 *jpeg)
{
    int ret, ints, val, done;
    int encode = (jpeg->flags & SHJPEG_JPU_FLAG_ENCODE);
    int convert = (jpeg->flags & SHJPEG_JPU_FLAG_CONVERT);
    struct pollfd fds[] = {
	{
	    .fd     = data->jpu_uio_fd,
	    .events = POLLIN,
	},
	{
	    .fd	    = data->veu_uio_fd,
	    .events = POLLIN,
	}
    };

    D_DEBUG_AT(SH7722_JPEG, "%s: entering", __FUNCTION__);

    switch (jpeg->state) {
    case SHJPEG_JPU_START:
	D_INFO("START (buffers: %d, flags: 0x%x)", jpeg->buffers, jpeg->flags);
	 
	data->jpeg_line	  	  	= 0;
	data->jpeg_end			= 0;
	data->jpeg_error	    	= 0;
	data->jpeg_encode	    	= encode;
	data->jpeg_reading	    	= 0;
	data->jpeg_writing	    	= encode ? 2 : 0;
	data->jpeg_height	    	= jpeg->height;
	data->jpeg_linebuf	    	= 0;
	data->jpeg_linebufs	    	= (encode) ? 3 : 0;
	data->jpeg_buffer	    	= 0;
	data->jpeg_buffers	    	= jpeg->buffers;

	data->jpu_running		= (encode) ? 0 : 1;
	data->jpu_lb_first_irq	        = (encode) ? 0 : 1;

	data->veu_linebuf	    	= 0;
	data->veu_running	    	= 0;

	jpeg->state	    = SHJPEG_JPU_RUN;
	jpeg->error	    = 0;

	// JPU_JCCMD = JCCMD_START;
	shjpeg_jpu_setreg32(data, JPU_JCCMD, JPU_JCCMD_START);
	break;

    case SHJPEG_JPU_RUN:
	D_INFO("RUN (buffers: %d)", jpeg->buffers);

	/* Validate loaded buffers. */
	data->jpeg_buffers |= jpeg->buffers;
	break;

    default:
	D_ERROR("lisbhjpeg: %s: "
		"INVALID STATE %d! (status 0x%08x, ints 0x%08x)",
		__FUNCTION__, jpeg->state,
		shjpeg_jpu_getreg32(data, JPU_JCSTS), 
		shjpeg_jpu_getreg32(data, JPU_JINTS));
	errno = EINVAL;
	return -1;
    }

    /*
     * bootstrap
     */
    if (data->jpeg_encode) {
	if (convert) {
	    if (!data->veu_running && 
		(data->jpeg_linebufs & (1 << data->veu_linebuf))) {
		D_INFO("veu: start veu on %d", data->veu_linebuf);
		shjpeg_veu_set_dst_jpu(data);
		shjpeg_veu_start(data, 0);
	    }
	}

	if (data->jpeg_buffers && !data->jpeg_writing) {
	    D_INFO( " '-> write start (buffers: %d)", data->jpeg_buffers );
	    data->jpeg_writing = 1;

	    // JPU_JCCMD = JCCMD_WRITE_RESTART;
	    shjpeg_jpu_setreg32(data, JPU_JCCMD, JPU_JCCMD_WRITE_RESTART);
	}
    }
    else if (data->jpeg_buffers && !data->jpeg_reading) {
	D_INFO( " '-> read start (buffers: %d)", data->jpeg_buffers );
	data->jpeg_reading = 1;

	// JPU_JCCMD = JCCMD_READ_RESTART;
	shjpeg_jpu_setreg32(data, JPU_JCCMD, JPU_JCCMD_READ_RESTART);
    }

    // Read from UIO dev here to wait for IRQ....
    done = 0;
    for(;;) {
	// wait for IRQ. time out set to 1sec.
	fds[0].revents = fds[1].revents = 0;
	ret = poll(fds, 2, 1000);

	// timeout or some error.
	if (ret == 0) {
	    D_ERROR("libshjpeg: waitevent - jpeg_end=%d, jpeg_linebufs=%d",
		    data->jpeg_end, data->jpeg_linebufs);
	    D_ERROR("libshjpeg: TIMEOUT at %s - "
		    "(JCSTS 0x%08x, JINTS 0x%08x(0x%08x), "
		    "JCRST 0x%08x, JCCMD 0x%08x, VSTAR 0x%08x)",
		    __FUNCTION__,
		    shjpeg_jpu_getreg32(data, JPU_JCSTS),
		    shjpeg_jpu_getreg32(data, JPU_JINTS),
		    shjpeg_jpu_getreg32(data, JPU_JINTE),
		    shjpeg_jpu_getreg32(data, JPU_JCRST),
		    shjpeg_jpu_getreg32(data, JPU_JCCMD),
		    shjpeg_veu_getreg32(data, VEU_VSTAR));
	    errno = ETIMEDOUT;
	    return -1;
	}

	if (ret < 0) {
	    D_ERROR("libshjpeg: no IRQ - poll() failed");
	    return -1;
	}

	if (fds[0].revents & POLLIN) {
	    /* read number of interrupts */
	    if (read(data->jpu_uio_fd, &val, sizeof(val)) != sizeof(val)) {
		D_ERROR("libshjpeg: no IRQ - read() failed");
		errno = EIO;
		return -1;
	    }
	    
	    /* sanity check */
	    D_INFO( "libshjpeg: IRQ counts = %d", val );
	    
	    /* get JPU IRQ stats */
	    ints = shjpeg_jpu_getreg32(data, JPU_JINTS);
	    shjpeg_jpu_setreg32(data, JPU_JINTS, ~ints & JPU_JINTS_MASK);
	    
	    if (ints & (JPU_JINTS_INS3_HEADER | JPU_JINTS_INS5_ERROR | 
			JPU_JINTS_INS6_DONE))
		shjpeg_jpu_setreg32(data, JPU_JCCMD, JPU_JCCMD_END);

	    D_INFO("libshjpeg: JPU interrupt 0x%08x(%08x) "
		   "(veu_linebuf: %d, jpeg_linebuf: %d, "
		   "jpeg_linebufs: %d, jpeg_line: %d, jpeg_buffers: %d)",
		   ints, shjpeg_jpu_getreg32(data, JPU_JINTS),
		   data->veu_linebuf, data->jpeg_linebuf, data->jpeg_linebufs, 
		   data->jpeg_line, data->jpeg_buffers );

	    if (ints) {
		/* Header */
		if (ints & JPU_JINTS_INS3_HEADER) {
		    D_INFO("libshjpeg: header=%dx%d",
			   shjpeg_jpu_getreg32(data, JPU_JIFDDHSZ),
			   shjpeg_jpu_getreg32(data, JPU_JIFDDVSZ));
		}
		
		/* Error */
		if (ints & JPU_JINTS_INS5_ERROR) {
		    data->jpeg_error = shjpeg_jpu_getreg32(data, JPU_JCDERR);
		    D_INFO("libshjpeg: error");
		    done = 1;
		}
		
		/* Done */
		if (ints & JPU_JINTS_INS6_DONE) {
		    data->jpeg_end = 1;
		    data->jpeg_linebufs = 0;
		    D_INFO("libshjpeg: done");
		    done = 1;
		}
		
		/* Done */
		if (ints & JPU_JINTS_INS10_XFER_DONE) {
		    data->jpeg_end = 1;
		    D_INFO("libshjpeg: xfer done");
		    done = 1;
		}
		
		/* line bufs */
		if (ints & 
		    (JPU_JINTS_INS11_LINEBUF0 | JPU_JINTS_INS12_LINEBUF1)) {
		    D_INFO("libshjpeg: jpu: done w/ LB%d", data->jpeg_linebuf);

		    /* mark buffer as ready, and move pointer */
		    data->jpeg_linebufs |= (1 << data->jpeg_linebuf);
		    data->jpeg_linebuf = (data->jpeg_linebuf + 1) % 2;

		    /*
		     * check if the next buffer is not ready yet. if so,
		     * start JPU.
		     */
		    data->jpu_running = 0;
		    if (!(data->jpeg_linebufs & (1 << data->jpeg_linebuf))) {
			if (!data->jpeg_end) {
			    D_INFO("libshjpeg: jpu: process LB%d", 
				   data->jpeg_linebuf);

			    if (data->jpu_lb_first_irq)
				data->jpu_lb_first_irq = 0;
			    else 
				shjpeg_jpu_setreg32(data, JPU_JCCMD,
						    JPU_JCCMD_LCMD2 | 
						    JPU_JCCMD_LCMD1 );
			    data->jpu_running = 1;
			}
		    } else {
			D_INFO("libshjpeg: jpu: wait for LB%d", 
			       data->jpeg_linebuf);
		    }
		}
		
		/* Loaded */
		if (ints & JPU_JINTS_INS13_LOADED) {
		    D_INFO("libshjpeg: load complete (%d/%d)",
			   data->jpeg_buffer, data->jpeg_writing);
		    data->jpeg_buffers &= ~(1 << data->jpeg_buffer);
		    data->jpeg_buffer = (data->jpeg_buffer + 1) % 2;
		    data->jpeg_writing--;
		    done = 1;
		}
		
		/* Reload */
		if (ints & JPU_JINTS_INS14_RELOAD) {
		    D_INFO("libshjpeg: reload complete (%d/%d)",
			   data->jpeg_buffer, data->jpeg_reading);
		    data->jpeg_buffers &= ~(1 << data->jpeg_buffer);
		    data->jpeg_buffer = (data->jpeg_buffer + 1) % 2;
		    
		    if (data->jpeg_buffers) {
			data->jpeg_reading = 1;   /* should still be one */
			shjpeg_jpu_setreg32(data, JPU_JCCMD, 
					    JPU_JCCMD_READ_RESTART);
		    }
		    else
			data->jpeg_reading = 0;
		    done = 1;
		}
	    }
	    
	    /* re-enable IRQ */
	    val = 1;
	    if (write(data->jpu_uio_fd, &val, sizeof(val) ) != sizeof(val)) {
		D_PERROR("libshjpeg: write() to uio failed.");
		return -1;
	    }
	}

	if (fds[1].revents & POLLIN) {	// VEU IRQ
	    D_INFO("libshjpeg: VEU IRQ - VEVTR=%08x, VSTAR=%08x, %d lines", 
		   shjpeg_veu_getreg32(data, VEU_VEVTR),
		   shjpeg_veu_getreg32(data, VEU_VSTAR),
		   shjpeg_veu_getreg32(data, VEU_VRFSR) >> 16);
	    shjpeg_veu_setreg32(data, VEU_VEVTR, 0);

	    /* read number of interrupts */
	    if (read(data->veu_uio_fd, &val, sizeof(val)) != sizeof(val)) {
		D_ERROR("libshjpeg: read IRQ count from VEU failed.");
		return -1;
	    }
          
	    /* sanity check */
	    D_INFO("libshjpeg: VEU IRQ counts = %d", val);
	    D_INFO("libshjpeg: veu: done w/ LB%d", data->veu_linebuf);

	    data->jpeg_linebufs &= ~(1 << data->veu_linebuf);

	    /* if JPU is not running - start */
	    if (!data->jpeg_end && !data->jpu_running &&
		(!(data->jpeg_linebufs & (1 << data->jpeg_linebuf)))) {
		D_INFO("libshjpeg: jpu: process LB%d", data->jpeg_linebuf);

		if (data->jpu_lb_first_irq)
		    data->jpu_lb_first_irq = 0;
		else 
		    shjpeg_jpu_setreg32(data, JPU_JCCMD,
					JPU_JCCMD_LCMD1 | JPU_JCCMD_LCMD2);
		data->jpu_running = 1;
	    } else {
		D_INFO("libshjpeg: jpu: wait for LB%d", data->jpeg_linebuf);
	    }

	    /* point to the other buffer */
	    shjpeg_veu_stop(data);
	    data->veu_linebuf = (data->veu_linebuf + 1) % 2;

	    /* re-enable IRQ */
	    val = 1;
	    if (write(data->veu_uio_fd, &val, sizeof(val)) != sizeof(val)) {
		D_ERROR("libshjpeg: re-enabling IRQ failed.\n");
		return -1;
	    }
	}

	/*
	 * ready to start veu?
	 */
	if (convert) {
	    if (!data->veu_running && 
		(data->jpeg_linebufs & (1 << data->veu_linebuf))) {
		D_INFO("libshjpeg: veu: process LB%d", data->veu_linebuf);
		if (data->jpeg_encode) {
		    jpeg->sa_y += jpeg->sa_inc;
		    jpeg->sa_c += jpeg->sa_inc;
		    
		    shjpeg_veu_set_src(data, jpeg->sa_y, jpeg->sa_c);
		    shjpeg_veu_set_dst_jpu(data);
		    shjpeg_veu_start(data, 0);
		} else {
		    shjpeg_veu_set_src_jpu(data);
		    shjpeg_veu_start(data, 1);
		}
	    } else {
		D_INFO("libshjpeg: veu: wait for LB%d", data->veu_linebuf);
	    }
	}

	/* are we done? */
	if ((done) && (!data->veu_running) &&
	    ((data->jpeg_end && !data->jpeg_linebufs) || 
	     (data->jpeg_error) ||
	     ((data->jpeg_buffers != 3) && 
	      (jpeg->flags & SHJPEG_JPU_FLAG_RELOAD))))
	    break;
    }

    if (data->jpeg_error) {
	/* Return error. */
	jpeg->state = SHJPEG_JPU_END;
	jpeg->error = data->jpeg_error;

	D_INFO( "libshjpeg: '-> ERROR (0x%x)", jpeg->error );
    }
    else {
	/* Return buffers to reload or to empty. */
	jpeg->buffers = data->jpeg_buffers ^ 3;

	if (data->jpeg_end) {
	    D_INFO( "libshjpeg: '-> END" );

	    /* Return end. */
	    jpeg->state    = SHJPEG_JPU_END;
	    jpeg->buffers |= 1 << data->jpeg_buffer;
	}
	else if (encode) {
	    D_INFO( "libshjpeg: '-> LOADED (%d)", jpeg->buffers );
	}
	else {
	    D_INFO( "libshjpeg: '-> RELOAD (%d)", jpeg->buffers );
	}
    }

    return 0;
}

/*
 * Init quantization table
 */

void shjpeg_jpu_init_quantization_table(shjpeg_internal_t *data)
{
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
}

/*
 * Init huffman table
 */
void shjpeg_jpu_init_huffman_table(shjpeg_internal_t *data)
{
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
}
