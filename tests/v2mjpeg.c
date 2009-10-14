/*
 * Copyright 2009 IGEL Co.,Ltd.
 * Copyright 2008,2009 Renesas Solutions Co.
 * Copyright 2008 Denis Oliver Kropp
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <asm/types.h>
#include <linux/videodev2.h>

#include "shjpeg.h"

#define MJPEG_BOUNDARY "++++++++"

int getinfo(int vd)
{
    struct v4l2_capability cap;

    if (ioctl(vd, VIDIOC_QUERYCAP, &cap) < 0) {
	fprintf(stderr, "ioctl failed\n");
	return 1;
    }

    fprintf(stderr, "Driver Name = %s\n", cap.driver);
    fprintf(stderr, "Card Name   = %s\n", cap.card);
    fprintf(stderr, "Bus Info    = %s\n", cap.bus_info);
    fprintf(stderr, "Version     = %08x\n", cap.version);
    fprintf(stderr, "Capabilty   = %08x\n", cap.capabilities);

    return 0;
}

#define DATASIZE	64 * 1024

typedef struct {
    void *data;
    size_t offset;
    size_t size;
} sops_data_t;

int sops_init(void *private)
{
    sops_data_t *data = (sops_data_t*)private;

    if (data->data)
    	free(data->data);

    data->data = malloc(DATASIZE);
    if (!data->data)
    	return -1;

    data->size = DATASIZE;
    data->offset = 0L;

    return 0;
}

int sops_write(void *private, size_t *nbytes, void *dataptr)
{
    sops_data_t *data = (sops_data_t*)private;

    if (data->size - data->offset < *nbytes) {
    	data->size = ((data->offset + *nbytes) + DATASIZE - 1) & ~(DATASIZE - 1);
	data->data = realloc(data->data, data->size);
    }

    memcpy(data->data + data->offset, dataptr, *nbytes);
    data->offset += *nbytes;

    return 0;
}

void sops_finalize(void *private)
{
}

shjpeg_sops my_sops = {
    .init     = sops_init,
    .read     = NULL,
    .write    = sops_write,
    .finalize = sops_finalize,
};

int main(int argc, char *argv[])
{
    int i, bufsiz;
    int vd;
    char *videodev = "/dev/video0";
    unsigned long jpeg_phys;
    void *jpeg_virt;
    size_t jpeg_size;
    struct v4l2_requestbuffers reqbuf;
    struct {
	unsigned long		 start;
	size_t	 		 length;
	struct v4l2_buffer	 buffer;
    } *buffers;
    enum v4l2_buf_type type;
    struct v4l2_format fmt;
    unsigned int page_size = getpagesize();
    shjpeg_context_t *ctx;
    sops_data_t data = { .data = NULL, .size = 0L };
    int verbose = 0, n = 1;

    if (!strcmp(argv[1], "-v")) {
    	verbose = 1;
	n++;
    }

    /* ready */
    videodev = argv[n] ? argv[n] : videodev;

    if (!(ctx = shjpeg_init(verbose)))
	return 1;

    if (shjpeg_get_frame_buffer(ctx, &jpeg_phys, &jpeg_virt, &jpeg_size ))
	return 1;
    printf("jpeg mem buffer at 0x%08lx/%p, size = 0x%08x\n", jpeg_phys, jpeg_virt, jpeg_size);

    if ((vd = open(videodev, O_RDWR)) < 0) {
	fprintf(stderr, "Can't open '%s'\n", videodev);
	return 1;
    }

    if (getinfo(vd))
	return 1;

    /* prepare capturing */
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
//	ioctl(vd, VIDIOC_S_FMT, &fmt);
    ioctl(vd, VIDIOC_G_FMT, &fmt);
    fmt.fmt.pix.pixelformat = v4l2_fourcc('N', 'V', '1', '6');
    fmt.fmt.pix.width=640;
    fmt.fmt.pix.height=480;
    ioctl(vd, VIDIOC_S_FMT, &fmt);
    ioctl(vd, VIDIOC_G_FMT, &fmt);
    fprintf(stderr, "width=%d\n", fmt.fmt.pix.width);
    fprintf(stderr, "height=%d\n", fmt.fmt.pix.height);
    fprintf(stderr, "pxformat=%4s\n", (char*)&fmt.fmt.pix.pixelformat);
    fprintf(stderr, "field=%d\n", fmt.fmt.pix.field);
    fprintf(stderr, "bytesperline=%d\n", fmt.fmt.pix.bytesperline);
    fprintf(stderr, "VIDIOC_S_FMT done\n");

    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_USERPTR;
    reqbuf.count = 2;

    if (ioctl(vd, VIDIOC_REQBUFS, &reqbuf) < 0) {
	fprintf(stderr, "ioctl REQBUFS failed\n");
	return 1;
    }
    fprintf(stderr, "VIDIOC_REQBUFS done\n");

    if (reqbuf.count < 2) {
	fprintf(stderr, "could only get %d buffers\n", reqbuf.count);
	return 1;
    }

    /* prepare buffer information */
    buffers = calloc(reqbuf.count, sizeof(*buffers));
    bufsiz = fmt.fmt.pix.height * fmt.fmt.pix.bytesperline;
    bufsiz = (bufsiz + page_size - 1) & ~(page_size - 1);

    for(i = 0; i < reqbuf.count; i++) {
	struct v4l2_buffer buffer;

	fprintf(stderr, "registering buffer %d\n", i);

	/* create buffer information to queue */
	memset(&buffer, 0, sizeof(buffer));
	buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buffer.memory = V4L2_MEMORY_USERPTR;
	buffer.index = i;
	buffer.length = bufsiz;
	buffer.m.userptr =  (uint32_t)jpeg_virt + i * bufsiz;

	/* queue buffer */
	if (ioctl(vd, VIDIOC_QBUF, &buffer) < 0) {
	    perror("ioctl - VIDIOC_QBUF");
	    return 1;
	}

	/* copy buffer information */
	buffers[i].length = buffer.length;
	buffers[i].start  = jpeg_phys + i * bufsiz;

	memcpy((void*)&buffers[i].buffer, (void*)&buffer, sizeof(buffer));

	/* debug */
	fprintf(stderr,
		"buffer %d: addr=%08x/%08lx, size=%08x\n", 
		i, buffer.m.offset, buffers[i].start, buffer.length);
    }

    /* start capturing */
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(vd, VIDIOC_STREAMON, &type) < 0) {
	perror("ioctl - VIDIOC_STREAMON");
	return 1;
    }

    /* set sops callbacks */
    ctx->sops = &my_sops;
    ctx->private = &data;

    /* now ready to capture */
    fprintf(stderr, "Starting Encoding...\n");
    while(1) {
	struct v4l2_buffer buffer;

	/* capture */
	memset(&buffer, 0, sizeof(buffer));
	buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buffer.memory= V4L2_MEMORY_USERPTR;
	if (ioctl(vd, VIDIOC_DQBUF, &buffer) < 0) {
	    perror("ioctl - VIDIOC_DQBUF");
	    return 1;
	}

	shjpeg_encode(ctx, SHJPEG_PF_NV16, buffers[buffer.index].start,
		      fmt.fmt.pix.width, fmt.fmt.pix.height, 
		      fmt.fmt.pix.width);
		
	/* queue again */
	if (ioctl(vd, VIDIOC_QBUF, &buffer) < 0) {
	    perror("ioctl - VIDIOC_QBUF");
	    return 1;
	}

	// output buffered data
	printf("\r\n\r\n--%s\r\n", MJPEG_BOUNDARY);
	printf("Content-Type: image/jpeg\r\n");
	printf("Content-length: %d\r\n\r\n", data.offset);
	fwrite(data.data, data.size, 1, stdout);
	printf("\r\n");

	fprintf(stderr, "+");
	fflush(stderr);

	usleep(300000);
    }

    return 0;
}
