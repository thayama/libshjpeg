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
#include <poll.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "shjpeg.h"
#include "shjpeg_internal.h"
#include "shjpeg_regs.h"
#include "shjpeg_jpu.h"


/**********************************************************************************************************************/

int
shjpeg_run_jpu(shjpeg_context_t	 *context,
	       shjpeg_internal_t *data,
	       shjpeg_jpu_t 	 *jpeg)
{
    int ret, ints, val;
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

    D_DEBUG_AT( SH7722_JPEG, "%s: entering", __FUNCTION__);

    switch (jpeg->state) {
    case SHJPEG_JPU_START:
	D_INFO( "START (buffers: %d, flags: 0x%x)", 
		jpeg->buffers, jpeg->flags );
	 
	data->jpeg_line	    	= 0;
	data->jpeg_end			= 0;
	data->jpeg_error	    	= 0;
	data->jpeg_encode	    	= encode;
	data->jpeg_reading	    	= 0;
	data->jpeg_writing	    	= encode ? 2 : 0;
	data->jpeg_reading_line 	= encode && !convert;
	data->jpeg_writing_line 	= !encode;
	data->jpeg_height	    	= jpeg->height;
	data->jpeg_linebuf	    	= 0;
	data->jpeg_linebufs	    	= 0;
	data->jpeg_buffer	    	= 0;
	data->jpeg_buffers	    	= jpeg->buffers;
	data->veu_linebuf	    	= 0;
	data->veu_running	    	= 0;

	jpeg->state	    = SHJPEG_JPU_RUN;
	jpeg->error	    = 0;

	// JPU_JCCMD = JCCMD_START;
	shjpeg_jpu_setreg32(data, JPU_JCCMD, JPU_JCCMD_START);
	break;

    case SHJPEG_JPU_RUN:
	D_INFO( "RUN (buffers: %d)", jpeg->buffers );

	/* Validate loaded buffers. */
	data->jpeg_buffers |= jpeg->buffers;
	break;

    default:
	D_ERROR( "lisbhjpeg: %s: "
		 "INVALID STATE %d! (status 0x%08x, ints 0x%08x)",
		 __FUNCTION__, jpeg->state,
		 shjpeg_jpu_getreg32(data, JPU_JCSTS), 
		 shjpeg_jpu_getreg32(data, JPU_JINTS));
	errno = EINVAL;
	return -1;
    }

    if (encode) {
	if (convert) {
	    if (data->jpeg_linebufs != 3 && !data->veu_running) {
		D_INFO( "libshjpeg: '-> "
			"convert start (buffers: %d, veu linebuf: %d)",
			data->jpeg_buffers, data->veu_linebuf );
		// XXX : better way to check JPU start?
		usleep(1000);

		data->veu_running = 1;

		shjpeg_veu_setreg32(data, VEU_VDAYR,
				    (data->veu_linebuf) ?
				    shjpeg_jpu_getreg32(data, JPU_JIFESYA2) :
				    shjpeg_jpu_getreg32(data, JPU_JIFESYA1));
		shjpeg_veu_setreg32(data, VEU_VDACR,
				    (data->veu_linebuf) ?
				    shjpeg_jpu_getreg32(data, JPU_JIFESCA2) :
				    shjpeg_jpu_getreg32(data, JPU_JIFESCA1));
		shjpeg_veu_setreg32(data, VEU_VESTR, 0x101);
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
    for(;;) {
	int done = 0;

	// wait for IRQ.
	fds[0].revents = fds[1].revents = 0;
	ret = poll( fds, 2, 5000 );

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
		    D_INFO( "libshjpeg:      -> HEADER (%dx%d)",
			    shjpeg_jpu_getreg32(data, JPU_JIFDDHSZ),
			    shjpeg_jpu_getreg32(data, JPU_JIFDDVSZ));
		}
		
		/* Error */
		if (ints & JPU_JINTS_INS5_ERROR) {
		    data->jpeg_error = shjpeg_jpu_getreg32(data, JPU_JCDERR);
		    
		    D_INFO("libshjpeg:      -> ERROR 0x%08x!", data->jpeg_error);
		    done = 1;
		}
		
		/* Done */
		if (ints & JPU_JINTS_INS6_DONE) {
		    data->jpeg_end = 1;
		    data->jpeg_linebufs = 0;
		    
		    D_INFO( "	      -> DONE" );
		    
		    done = 1;
		}
		
		/* Done */
		if (ints & JPU_JINTS_INS10_XFER_DONE) {
		    data->jpeg_end = 1;
		    
		    D_INFO( "	      -> XFER DONE" );
		    
		    done = 1;
		}
		
		/* Line buffer ready? FIXME: encoding */
		if (ints & 
		    (JPU_JINTS_INS11_LINEBUF0 | JPU_JINTS_INS12_LINEBUF1)) {
		    D_INFO("	      -> LINEBUF %d DONE",
			   data->jpeg_linebuf );
		    
		    if (data->jpeg_encode) {
			data->jpeg_linebufs &= ~(1 << data->jpeg_linebuf);
			data->jpeg_linebuf = data->jpeg_linebuf ? 0 : 1;
			data->jpeg_reading_line = 0;
			
			if (data->jpeg_linebufs) {
			    /* should still be one */
			    data->jpeg_reading_line = 1; 
			    
			    if (!data->jpeg_end)  {
				shjpeg_jpu_setreg32(data, JPU_JCCMD,
						    JPU_JCCMD_LCMD2 | 
						    JPU_JCCMD_LCMD1 );
			    }
			}

			data->jpeg_line += 16;

			if (convert && !data->veu_running && !data->jpeg_end) {
#ifdef SHJPEG_DEBUG
			    u32 vdayr = shjpeg_veu_getreg32(data, VEU_VDAYR);
			    u32 vdacr = shjpeg_veu_getreg32(data, VEU_VDACR);
#endif
			    
			    D_INFO( "		-> CONVERT %d", 
				    data->veu_linebuf );
			    data->veu_running = 1;

			    shjpeg_veu_setreg32(data, VEU_VDAYR,
						(data->veu_linebuf) ?
						shjpeg_jpu_getreg32(data, 
								    JPU_JIFESYA2):
						shjpeg_jpu_getreg32(data, 
								    JPU_JIFESYA1));
			    shjpeg_veu_setreg32(data, VEU_VDACR,
						(data->veu_linebuf) ?
						shjpeg_jpu_getreg32(data, 
								    JPU_JIFESCA2):
						shjpeg_jpu_getreg32(data, 
								    JPU_JIFESCA1));
			    shjpeg_veu_setreg32(data, VEU_VESTR, 0x101);
			    
			    D_INFO( "		-> SWAP, "
				    "VEU_VSAYR = %08x (%08x->%08x, %08x->%08x)",
				    shjpeg_veu_getreg32( data, VEU_VSAYR ), vdayr,
				    shjpeg_veu_getreg32( data, VEU_VDAYR ), vdacr,
				    shjpeg_veu_getreg32( data, VEU_VDACR ) );
			}
		    }
		    else {
			data->jpeg_linebufs |= (1 << data->jpeg_linebuf);
			
			data->jpeg_linebuf = data->jpeg_linebuf ? 0 : 1;
			
			if (data->jpeg_linebufs != 3) {
			    /* should still be one */
			    data->jpeg_writing_line = 1; 
			    
			    if (data->jpeg_line > 0 && !data->jpeg_end) {
				shjpeg_jpu_setreg32( data, JPU_JCCMD,
						     JPU_JCCMD_LCMD2 |
						     JPU_JCCMD_LCMD1 );
			    }
			}
			else {
			    data->jpeg_writing_line = 0;
			}
			
			data->jpeg_line += 16;
			
			if (convert && !data->veu_running && 
			    !data->jpeg_end && !data->jpeg_error) {
			    D_INFO("		-> CONVERT %d",
				   data->veu_linebuf );
			    
			    data->veu_running = 1;
			    
			    shjpeg_veu_setreg32(data, VEU_VSAYR,
						(data->veu_linebuf) ?
						shjpeg_jpu_getreg32(data, 
								    JPU_JIFDDYA2):
						shjpeg_jpu_getreg32(data, 
								    JPU_JIFDDYA1));
			    shjpeg_veu_setreg32(data, VEU_VSACR,
						(data->veu_linebuf) ?
						shjpeg_jpu_getreg32(data, 
								    JPU_JIFDDCA2):
						shjpeg_jpu_getreg32(data, 
								    JPU_JIFDDCA1));
			    shjpeg_veu_setreg32(data, VEU_VESTR, 0x101);
			}
		    }
		}
		
		/* Loaded */
		if (ints & JPU_JINTS_INS13_LOADED) {
		    D_INFO( "	      -> LOADED %d (writing: %d)", 
			    data->jpeg_buffer, data->jpeg_writing );
		    
		    data->jpeg_buffers &= ~(1 << data->jpeg_buffer);
		    
		    data->jpeg_buffer = data->jpeg_buffer ? 0 : 1;
		    
		    data->jpeg_writing--;
		    
		    done = 1;
		}
		
		/* Reload */
		if (ints & JPU_JINTS_INS14_RELOAD) {
		    D_INFO( "	      -> RELOAD %d", 
			    data->jpeg_buffer );
		    
		    data->jpeg_buffers &= ~(1 << data->jpeg_buffer);
		    
		    data->jpeg_buffer = data->jpeg_buffer ? 0 : 1;
		    
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

	    if (data->jpeg_encode) {
		D_INFO("         -> CONVERTED %d" , data->veu_linebuf);
		if (data->jpeg_end || data->jpeg_error || 
		    (data->jpeg_line + 16 >= data->jpeg_height)) {
		    D_INFO("         -> STOP VEU");
                         
		    data->veu_running = 0;
		    data->jpeg_linebufs = 0;
		}
                    
		data->jpeg_linebufs |= 1 << data->veu_linebuf;
                    
		if (!data->jpeg_reading_line) {
		    D_INFO("         -> ENCODE %d", data->veu_linebuf);
		    shjpeg_jpu_setreg32(data, JPU_JCCMD,
					JPU_JCCMD_LCMD1 | JPU_JCCMD_LCMD2);
		    data->jpeg_reading_line = 1;
		}

		if (data->veu_running) {
		    data->veu_linebuf = data->veu_linebuf ? 0 : 1;
		    data->veu_running = 0;
                    
		    if (!(data->jpeg_linebufs & (1 << data->veu_linebuf))) {
#ifdef SHJPEG_DEBUG
			u32 vdayr, vdacr;
                              
			vdayr = shjpeg_veu_getreg32(data, VEU_VDAYR);
			vdacr = shjpeg_veu_getreg32(data, VEU_VDACR);
#endif
                              
			D_INFO("         -> CONVERT %d", data->veu_linebuf);
                              
			data->veu_running = 1;   /* should still be one */
			jpeg->sa_y += jpeg->sa_inc;
			jpeg->sa_c += jpeg->sa_inc;
			shjpeg_veu_setreg32(data, VEU_VSAYR, jpeg->sa_y);
			shjpeg_veu_setreg32(data, VEU_VSACR, jpeg->sa_c);
			shjpeg_veu_setreg32(data, VEU_VDAYR,
					    (data->veu_linebuf) ? 
					    shjpeg_jpu_getreg32(data, JPU_JIFESYA2) : 
					    shjpeg_jpu_getreg32(data, JPU_JIFESYA1));
			shjpeg_veu_setreg32(data, VEU_VDACR,
					    (data->veu_linebuf) ? 
					    shjpeg_jpu_getreg32(data, JPU_JIFESCA2) : 
					    shjpeg_jpu_getreg32(data, JPU_JIFESCA1));
			shjpeg_veu_setreg32(data, VEU_VESTR, 0x1);
			
			D_INFO("         -> SWAP, VEU_VSAYR = %08x (%08x->%08x, %08x->%08x)", 
			       shjpeg_veu_getreg32(data, VEU_VSAYR), vdayr, 
			       shjpeg_veu_getreg32(data, VEU_VDAYR), vdacr, 
			       shjpeg_veu_getreg32(data, VEU_VDACR));
		    }

		} 
	    } else { // not encoding
		/* Release line buffer. */
		data->jpeg_linebufs &= ~(1 << data->veu_linebuf);
                    
		/* Resume decoding if it was blocked. */
		if (!data->jpeg_writing_line && !data->jpeg_end && 
		    !data->jpeg_error && data->jpeg_linebufs != 3) {
		    D_INFO( "         -> RESUME %d", data->jpeg_linebuf );
                         
		    data->jpeg_writing_line = 1;
                         
		    shjpeg_jpu_setreg32(data, JPU_JCCMD, 
					JPU_JCCMD_LCMD1 | JPU_JCCMD_LCMD2 );
		}
                    
		data->veu_linebuf = data->veu_linebuf ? 0 : 1;
                    
		if (data->jpeg_linebufs) {
		    D_INFO("         -> CONVERT %d", data->veu_linebuf);
                         
		    data->veu_running = 1;   /* should still be one */

		    // VEU_VSAYR = veu_linebuf ? JPU_JIFDDYA2 : JPU_JIFDDYA1;
		    shjpeg_veu_setreg32(data, VEU_VSAYR,
					data->veu_linebuf ? 
					shjpeg_jpu_getreg32(data, JPU_JIFDDYA2) : 
					shjpeg_jpu_getreg32(data, JPU_JIFDDYA1));
		    // VEU_VSACR = veu_linebuf ? JPU_JIFDDCA2 : JPU_JIFDDCA1;
		    shjpeg_veu_setreg32(data, VEU_VSACR,
					data->veu_linebuf ? 
					shjpeg_jpu_getreg32(data, JPU_JIFDDCA2) : 
					shjpeg_jpu_getreg32(data, JPU_JIFDDCA1));
		    shjpeg_veu_setreg32(data, VEU_VESTR, 0x0101);
		} else {
		    if (data->jpeg_end)
			done = 1;

		    data->veu_running = 0;
		}
	    }

	    /* re-enable IRQ */
	    val = 1;
	    if (write(data->veu_uio_fd, &val, sizeof(val)) != sizeof(val)) {
		D_ERROR("libshjpeg: re-enabling IRQ failed.\n");
		return -1;
	    }
	}

	/* are we done? */
	if ((done) && 
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
