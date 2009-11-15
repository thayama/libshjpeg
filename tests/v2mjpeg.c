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
#include <getopt.h>

#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <signal.h>

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

static char *argv0;
static struct timeval start_tv;
static int frame_count = 0;

void print_usage() {
    fprintf(stderr, 
	    "Usage: %s [OPTION] [<v4l2 device>]\n", argv0);
    fprintf(stderr, 
	    "- Encode frames captured via V4L2 device.\n"
	    "- Default is to catpure from /dev/video0 and output to stdout.\n"
	    "- To transmit over HTTP, use with sighttpd.\n"
	    "\n"
	    "Options:\n"
	    "  -h, --help                         this message.\n"
	    "  -v, --verbose                      libshjpeg verbose output.\n"
	    "  -q, --quiet                        quiet mode.\n"
	    "  -f, --show-fps                     show fps.\n"
	    "  -s <w>x<h>, --size=<w>x<h>         capture size.\n"
	    "  -o [<prefix>], --output[=<prefix>] dump to the file.\n"
	    "  -c <count>, --count=<count>        # of JPEGs to capture.\n"
	    "                                     (Default: infinite)\n"
	    "  -i <n>, --interval=<n>             xmit at <n> msec interval. (Default: 0msec)\n");
}

void show_fps(int dummy)
{
    struct timeval end_tv;

    gettimeofday(&end_tv, NULL);

    if (frame_count) {
    	unsigned long diff;

	diff = (end_tv.tv_sec - start_tv.tv_sec) * 1000 + 
		(end_tv.tv_usec - start_tv.tv_usec) / 1000;

	fprintf(stderr, "Frame count = %d\n", frame_count);
	fprintf(stderr, "Duration    = %ldms\n", diff);
	fprintf(stderr, "Average     = %lffps\n", 
		(double)(frame_count * 1000) / diff);
    } else {
    	fprintf(stderr, "No frames encoded\n");
    }

    exit(0);
}

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
    int verbose = 0;
    int interval = 0;
    int quiet = 0;
    int fps = 0;
    int output = 0;
    char *prefix = "jpegdata-";
    int num_count = 0;
    unsigned int width = 640;
    unsigned int height = 480;

    argv0 = argv[0];

    /* parse args */
    while(1) {
	int c, option_index = 0;
	static struct option long_options[] = {
	    {"help", 0, 0, 'h'},
	    {"verbose", 0, 0, 'v'},
	    {"quiet", 0, 0, 'q'},
	    {"show-fps", 0, 0, 'f'},
	    {"output", 2, 0, 'o'},
	    {"count", 1, 0, 'c'},
	    {"size", 1, 0, 's'},
	    {"interval", 1, 0, 'i'},
	    {0, 0, 0, 0}
	};

	if ((c = getopt_long(argc, argv, "hvqfo::c:i:s:",
			     long_options, &option_index)) == -1)
	    break;

	switch(c) {
	case 'h':
	    print_usage();
	    return 0;

	case 'v':
	    verbose = 1;
	    break;

	case 'q':
	    quiet = 1;
	    break;

	case 'f':
	    fps = 1;
	    break;

	case 'o':
	    output = 1;
	    if (optarg)
	    	prefix = optarg;
	    break;

	case 'c':
	    num_count = strtol(optarg, NULL, 0);
	    break;

	case 's':
	    if (sscanf(optarg, "%ux%u", &width, &height) != 2) {
	        fprintf(stderr, 
			"capture size must be specified in <w>x<h>.\n");
		return 1;
	    }
	    if ((width % 4) || (height % 4)) {
	    	fprintf(stderr, 
			"size must be multiple of 4 (four) - %dx%d given.\n",
			width, height);
		return 1;
	    }
	    break;

	case 'i':
	    interval = strtol(optarg, NULL, 0);
	    break;

	default:
	    fprintf(stderr, "unknown option 0%x.\n", c);
	    print_usage();
	    return 1;
	}
    }

    videodev = argv[optind] ? argv[optind] : videodev;

    /* set signal handler */
    if (fps)
	signal(SIGINT, show_fps);

    /* ready */
    if (!(ctx = shjpeg_init(verbose)))
	return 1;

    if (shjpeg_get_frame_buffer(ctx, &jpeg_phys, &jpeg_virt, &jpeg_size ))
	return 1;

    if (!quiet)
	fprintf(stderr, "jpeg mem buffer at 0x%08lx/%p, size = 0x%08x\n", jpeg_phys, jpeg_virt, jpeg_size);

    if ((vd = open(videodev, O_RDWR)) < 0) {
	fprintf(stderr, "Can't open '%s'\n", videodev);
	return 1;
    }

    if (!quiet && getinfo(vd))
	return 1;

    /* prepare capturing */
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
//	ioctl(vd, VIDIOC_S_FMT, &fmt);
    ioctl(vd, VIDIOC_G_FMT, &fmt);
    fmt.fmt.pix.pixelformat = v4l2_fourcc('N', 'V', '1', '6');
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    if (ioctl(vd, VIDIOC_S_FMT, &fmt) < 0) {
    	fprintf(stderr, "VIDIOC_S_FMT failed - %08x, %dx%d\n",
		fmt.fmt.pix.pixelformat, 
		fmt.fmt.pix.width,
		fmt.fmt.pix.height);
	return 1;
    }
    ioctl(vd, VIDIOC_G_FMT, &fmt);
    if (!quiet) {
	fprintf(stderr, "width=%d (requested %d)\n", 
		fmt.fmt.pix.width, width);
	fprintf(stderr, "height=%d (requested %d)\n",
		fmt.fmt.pix.height, height);
	fprintf(stderr, "pxformat=%4s\n", (char*)&fmt.fmt.pix.pixelformat);
	fprintf(stderr, "field=%d\n", fmt.fmt.pix.field);
	fprintf(stderr, "bytesperline=%d\n", fmt.fmt.pix.bytesperline);
	fprintf(stderr, "VIDIOC_S_FMT done\n");
    }

    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_USERPTR;
    reqbuf.count = 2;

    if (ioctl(vd, VIDIOC_REQBUFS, &reqbuf) < 0) {
	fprintf(stderr, "ioctl REQBUFS failed\n");
	return 1;
    }
    if (!quiet)
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

	if (!quiet)
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
	if (!quiet)
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
    if (!quiet)
	fprintf(stderr, "Starting Encoding...\n");

    frame_count = 0;
    gettimeofday(&start_tv, NULL);
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
	if (output) {
	    FILE *fp;
	    char fn[64];

	    snprintf(fn, sizeof(fn), "%s%03d.jpg", prefix, frame_count);

	    if ((fp = fopen(fn, "w")) == NULL) {
	    	fprintf(stderr, "Can't create file: %s\n", fn);
		return 1;
	    }
	    fwrite(data.data, data.offset, 1, fp);
	    fclose(fp);
	} else {
	    printf("\r\n\r\n--%s\r\n", MJPEG_BOUNDARY);
	    printf("Content-Type: image/jpeg\r\n");
	    printf("Content-length: %d\r\n\r\n", data.offset);
	    fwrite(data.data, data.offset, 1, stdout);
//	    printf("\r\n");
	}

	if (!quiet)
	    fprintf(stderr, "+");
	fflush(stderr);

	frame_count++;
	if ((num_count > 0) && (num_count <= frame_count))
	    break;

	if (interval)
	    usleep(interval * 1000);
    }

    if (fps)
    	show_fps(0);

    return 0;
}
