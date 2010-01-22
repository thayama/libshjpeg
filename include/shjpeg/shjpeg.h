/*
 * libshjpeg: A library for controlling SH-Mobile JPEG hardware codec
 *
 * Copyright (C) 2008-2009 IGEL Co.,Ltd.
 * Copyright (C) 2008-2009 Renesas Technology Corp.
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

#ifndef __shjpeg_h__
#define __shjpeg_h__

#include <shjpeg/shjpeg_types.h>

/**
 * \file shjpeg.h
 *
 * A library for controlling SH-Mobile JPEG hardware codec.
 */

/**
 * \mainpage
 *
 * This library supports JPEG encoding and decoding via SH-Mobile
 * JPU. JPU is a hardware JPEG codecs. The library make use of JPU via
 * UIO. To use this library, the UIO for JPU shall be enabled.
 *
 * The library tries to decode JPEG image via JPU where
 * possible. However, when it finds that is not possible due to the
 * hardware specification, it automatically falss back to software
 * decoding via libjpeg.
 *
 * The encoding always success, thus there is no software fallback.
 *
 * This version of library supports only NV12/NV16 pixel format. When
 * YUV420 profile is passed to the library, it automatically decode in
 * NV12 pixel format. When YUV422 profile or YUV444 profile is passed,
 * it automatically decode in NV16 pixel format.
 *
 * Encoding can only be from/to NV12/NV16 pixel format.
 */

/**
 * \brief Initialize SH-Mobile JPEG library
 *
 * Initialize the SH-Mobile JPEG library. This must be called before
 * calling any APIs in this library.
 *
 * \param verbose [in] if non-zero value is set, verbose debug message is enabled.
 *
 * \retval 0 success
 * \retval -1 failed
 */

shjpeg_context_t *shjpeg_init(int verbose);

/**
 * \brief Shutdown SH-Mobile JPEG library
 *
 * De-initialize the SH7722 JPEG library. This must be called before
 * closing the process.
 *
 * \retval 0 success
 * \retval -1 failed
 */

void shjpeg_shutdown(shjpeg_context_t *context);

/**
 * \brief Get frame buffer information.
 *
 * Kernel allocated contiguous memory that could be used to place
 * uncompressed image for decoding and encoding is returned. This
 * physcal contiguous memory will be default memory used by
 * encoder/decoder when 0L is passed as the physcial memory address to
 * shjpeg_decode() or shjpeg_encode(). As the memory is made
 * accessible from user space only after calling shjpeg_init(), you cannot
 * call this function before calling shjpeg_init().
 *
 * \param context [in] a pointer to the JPEG image context.
 *        Pass the value set by shjpeg_open().
 *
 * \param phys start address of physical contiguous memory is set.
 *
 * \param buffer pointer to the memory mapped physical contiguous
 *	 memory is set.
 *
 * \param size size of the physical contiguous memory region is set.
 *
 * \retval 0 success
 * \retval -1 failed
 *
 * \sa shjpeg_decode(), and shjpeg_encode().
 */

int shjpeg_get_frame_buffer(shjpeg_context_t	 *context,
			    unsigned long	 *phys,
			    void		**buffer,
			    size_t		 *size );

/**
 * \brief Open JPEG file.
 *
 * Parse the passed JPEG file stream, and returns the context required
 * for decompression. Subsequently after calling this function,
 * shjpeg_decode_run() can be called.
 *
 * Width and height of the JPEG image is returned in the context.
 *
 * \param [in,out] context a pointer to the JPEG image context to be returned.
 *
 * \retval 0 success
 * \retval -1 failed
 *
 * \sa shjpeg_decode_run(), shjpeg_decode_shutdown()
 */

int shjpeg_decode_init(shjpeg_context_t *context);

/**
 * \brief Decode JPEG stream.
 *
 * Start decoding JPEG file. This could be called only after
 * shjpeg_decode_init() is called.
 *
 * \param context [in] a pointer to the JPEG image context to be
 *        decoded. Pass the value set by shjpeg_open().
 *
 * \param format [in] desired pixelformat of the decoded image.  
 *
 * \param phys [in] physical memory address for decoded image. If the value
 *       is set to 0L, then memory allocated by the kernel will be
 *       automatically used. The kernel allocated memory can be
 *       obtained using shjpeg_get_frame_buffer().
 *
 * \param width [in] width of the destination frame buffer.
 *
 * \param height [in] height of the destination frame buffer.
 *
 * \param pitch [in] pitch of the frame buffer.
 *
 * \retval 0 success
 * \retval -1 failed
 *
 * \sa shjpeg_decode_init(), and shpeg_get_frame_buffer().
 */
int shjpeg_decode_run(shjpeg_context_t		*context,
		      shjpeg_pixelformat	 format,
		      unsigned long          	 phys,
		      int			 width,
		      int			 height,
		      int                    	 pitch);

/**
 * \brief Close JPEG stream context.
 *
 * You should call this function after decompression of image is
 * completed.
 *
 * \param context [in] a pointer to the JPEG image decompression
 * context.
 *
 * \retval 0 success
 * \retval -1 failed
 *
 * \sa shjpeg_open().
 */
void shjpeg_decode_shutdown(shjpeg_context_t *context);

/**
 * \brief Encode the image to JPEG file.
 *
 * Image passed to this function is encoded and written as a file.
 *
 * \param context [in] a pointer to the JPEG image context to be
 *        encoded. Pass the value set by shjpeg_open().
 *
 * \param format pixelformat of the image. Only NV12 and NV16 are
 *	  supported.
 *
 * \param phys physical memory address for input image. If the value
 *       is set to 0L, then memory allocated by the kernel will be
 *       automatically used. The kernel allocated memory can be
 *       obtained using shjpeg_get_frame_buffer().
 *
 * \param width width of the input image. 
 *
 * \param height height of the input image.
 *
 * \param pitch pitch of the input image buffer.
 *
 * \retval 0 success
 * \retval -1 failed
 *
 * \sa shjpeg_get_frame_buffer().
 */
int shjpeg_encode(shjpeg_context_t	*context,
		  shjpeg_pixelformat	 format,
		  unsigned long          phys,
		  int		         width,
		  int           	 height,
		  int                    pitch);

#endif /* !__shjpeg_h__ */
