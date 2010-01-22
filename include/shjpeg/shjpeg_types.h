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

#ifndef __shjpeg_types_h__
#define __shjpeg_types_h__

#include <stdbool.h>
#include <stdint.h>

#include <jpeglib.h>

/**
 * \file shjpeg_types.h
 *
 * A library for controlling SH-Mobile JPEG hardware codec.
 */

//! Use default physically contigous buffer
#define SHJPEG_USE_DEFAULT_BUFFER	0xffffffffUL

/*
 * id  - Unique Identifier
 * bpp - number of bits per pixel
 * mp  - multiplier for planes calculation (units of 1/2 plane)
 */

#define SHJPEG_PIXELFORMAT(id, pitch, bpp, mul)		\
    ((((id) & 0xffff) << 24) | (((pitch) & 0xff) << 16) | (((bpp) &   0xff) <<  8) | \
     ((mul)  &   0xff))

#define SHJPEG_PF_PITCH_MULTIPLY(format) \
    (((format) & 0x00ff0000) >> 16)

#define SHJPEG_PF_BPP(format) \
    (((format) & 0x0000ff00) >> 8)

#define SHJPEG_PF_PLANE_MULTIPLY(format, height) \
    (((format) & 0xff) * (height) / 2)

/**
 * \brief Pixel format
 *
 * Supported pixel formats.
 */

typedef enum {
    SHJPEG_PF_RGB16 = SHJPEG_PIXELFORMAT(1, 2, 16, 2),		/*!< RGB16 pixel format. */
    SHJPEG_PF_RGB24 = SHJPEG_PIXELFORMAT(2, 3, 24, 2),		/*!< RGB24 pixel format. */
    SHJPEG_PF_RGB32 = SHJPEG_PIXELFORMAT(3, 4, 32, 2),		/*!< RGB32 pixel format. */
    SHJPEG_PF_NV12  = SHJPEG_PIXELFORMAT(4, 1, 12, 3),		/*!< NV12 pixel format. */
    SHJPEG_PF_NV16  = SHJPEG_PIXELFORMAT(5, 1, 16, 4),		/*!< NV16 pixel format. */
} shjpeg_pixelformat;

typedef struct shjpeg_stream_ops_struct shjpeg_sops;

/**
 * \brief JPEG Stream Operations
 *
 * Defines a set of operations to read/write data from/to JPEG stream.
 */

struct shjpeg_stream_ops_struct {
    //! A method to init JPEG stream.
    /*!
      \param [in] private user data.
      \return should return 0 if success, otherwise non-zero value.
     */
    int (*init)(void *private);

    //! A method to read JPEG data.
    /*!
      \param [in] private user data.
      \param [in,out] nbytes number of bytes to read, and returns bytes actually read.
      \param [in] dataptr a pointer to the buffer to be filled.
      \return should return 0 if success, otherwise non-zero value.
     */
    int	(*read)(void *private, size_t *nbytes, void *dataptr);

    //! A method to write JPEG data.
    /*!
      \param [in] private user data.
      \param [in,out] nbytes number of bytes to write, and returns bytes actually written.
      \param [in] dataptr a pointer to the buffer to be writen.
      \return should return 0 if success, otherwise non-zero value.
     */
    int	(*write)(void *private, size_t *nbytes, void *dataptr);

    //! A method to finalize JPEG data.
    /*!
      \param [in] private user data.
     */
    void (*finalize)(void *private);
};

/**
 * \brief JPEG Compressoin/Decompression Context
 * 
 * When the file to be decoed is opened, the details of the JPEG files
 * is stored in this structure.
 */

typedef struct shjpeg_context_struct shjpeg_context_t;

struct shjpeg_context_struct {
    //! Width of the current image.
    int		width;

    //! Height of the current image.
    int		height;

    //! True if the image is YUV420 (valid during decode only).
    bool	mode420;

    //! True if the image is YUV444 (valid during decode only).
    bool	mode444;

    //! Stream operations
    shjpeg_sops	*sops;

    //! User defined private data
    void	*private;

    //! libshjpeg private data
    void	*internal_data;

    //! Set to non-zero, if fallback to libjpeg is NOT desired.
    int		 libjpeg_disabled;

    //! libshjpeg set this to non-zero, if decoding falled back to libjpeg.
    int		 libjpeg_used;

    //! libshjpeg private data
    int		 verbose;
    struct jpeg_compress_struct    jpeg_comp;
    struct jpeg_decompress_struct  jpeg_decomp;
};

#endif /* !__shjpeg_types_h__ */
