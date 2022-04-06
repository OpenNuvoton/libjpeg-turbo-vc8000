/**
 * @file vc8000_v4l2.c: vc8000 for v4l2 driver
 *
 * Copyright (C) 2021 nuvoton
 */
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>
#include <poll.h>

#include <pthread.h>

#define VC8000_DEV_MAX_NO 4
#define DEFAULT_VC8000_DEV_NAME "/dev/video0"

//#define ENABLE_DBG

#include "vc8000_v4l2.h"
#include "msm-v4l2-controls.h"


static char *dbg_type[2] = {"OUTPUT", "CAPTURE"};
static char *dbg_status[2] = {"ON", "OFF"};

static uint8_t *s_pu8FrameBufAddr = NULL;
static uint32_t s_u32FrameBufSize = 0;
static uint32_t s_u32FrameBufPlanes = 0;

static pthread_mutex_t s_tHantroLock = PTHREAD_MUTEX_INITIALIZER;

////////////////////////////////////////////////////////////////////////////////////////
static int v4l2_queue_buf(
	struct video *psVideo,
	int n,
	int l1,
	int l2,
	int type,
	int nplanes
)
{
	struct video *vid = psVideo;
	struct v4l2_buffer buf;
	struct v4l2_plane planes[MAX_PLANES];
	int ret;
	int p;

	memzero(buf);
	memset(planes, 0, sizeof(planes));
	buf.type = type;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = n;
	buf.length = nplanes;
	buf.m.planes = planes;

	for(p = 0; p < nplanes; p ++)
	{
		buf.m.planes[p].bytesused = psVideo->cap_buf_planes_size[n][p];
		buf.m.planes[p].data_offset = 0;

		if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
			buf.m.planes[p].length = psVideo->cap_buf_planes_size[n][p];
	}

	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		buf.m.planes[0].bytesused = l1;
		//fprintf(stdout, "queue output buffer  %d lenght \n", l1);
		buf.m.planes[0].length = vid->out_buf_size;
		if (l1 == 0)
			buf.flags |= V4L2_QCOM_BUF_FLAG_EOS;
	}

	ret = ioctl(vid->fd, VIDIOC_QBUF, &buf);
	if (ret) {
		fprintf(stderr, "Failed to queue buffer (index=%d) on %s (ret:%d)",
		    buf.index,
		    dbg_type[type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE], ret);
		return -1;
	}

//	dbg("  Queued buffer on %s queue with index %d",
//	    dbg_type[type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE], buf.index);

#if defined (ENABLE_DBG)
	fprintf(stdout, "queue buffer buf.index %d done \n", buf.index);
#endif

	return 0;
}

static int v4l2_dequeue_buf(
	struct video *psVideo,
	struct v4l2_buffer *buf
)
{
	struct video *vid = psVideo;
	int ret;

	ret = ioctl(vid->fd, VIDIOC_DQBUF, buf);
	if (ret < 0) {
		fprintf(stderr, "Failed to dequeue buffer (%d)", -errno);
		return -errno;
	}

//	dbg("Dequeued buffer on %s queue with index %d (flags:%x, bytesused:%d)",
//	    dbg_type[buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE],
//	    buf->index, buf->flags, buf->m.planes[0].bytesused);

	return 0;
}


/////////////////////////////////////////////////////////////////////////////////////////////////

/*
 *  Private V4L2 post processing ioctl for VC8K
 */

struct vc8k_pp_params {
	int   enable_pp;
	unsigned int   frame_buf_paddr;           /* physical address of frame buffer          */
	int   frame_buff_size;
	int   frame_buf_w;               /* width of frame buffer width               */
	int   frame_buf_h;               /* height of frame buffer                    */
	int   img_out_x;                 /* image original point(x,y) on frame buffer */
	int   img_out_y;                 /* image original point(x,y) on frame buffer */
	int   img_out_w;                 /* image output width on frame buffer        */
	int   img_out_h;                 /* image output height on frame buffer       */
	int   img_out_fmt;               /* image output format                       */
	int   rotation;
	int   pp_out_dst;                /* PP output destination.                    */
					 /* 0: fb0                                    */
					 /* 1: fb1                                    */
					 /* otherwise: frame_buf_paddr                */
	int   libjpeg_mode;		 /* 0: v4l2-only; 1: libjpeg+v4l2             */
	int   resserved[8];
};


#define VC8KIOC_PP_SET_CONFIG	_IOW ('v', 91, struct vc8k_pp_params)
#define VC8KIOC_PP_GET_CONFIG	_IOW ('v', 92, struct vc8k_pp_params)
#define VC8KIOC_GET_BUF_PHY_ADDR	_IOWR ('v', 193, struct v4l2_buffer)

int vc8000_v4l2_open(struct video *psVideo)
{
	struct v4l2_capability cap;
	int ret;
	char strVideoDevNode[50];
	int i32DefaultDevNodeLen = strlen(DEFAULT_VC8000_DEV_NAME);
	int i = 0;
	
	memset(strVideoDevNode, 0, 50);
	strcpy(strVideoDevNode, DEFAULT_VC8000_DEV_NAME);

	for( i = 0; i < VC8000_DEV_MAX_NO; i ++) {
		sprintf(strVideoDevNode + i32DefaultDevNodeLen - 1, "%d", i);

		pthread_mutex_lock(&s_tHantroLock);
		psVideo->fd = open(strVideoDevNode, O_RDWR, 0);
		if (psVideo->fd < 0) {
			fprintf(stderr, "Failed to open video decoder: %s \n", strVideoDevNode);
			psVideo->fd = -1;
			continue;
		}

		memzero(cap);
		ret = ioctl(psVideo->fd, VIDIOC_QUERYCAP, &cap);
		if (ret) {
			fprintf(stderr, "Failed to verify capabilities \n");
			close(psVideo->fd);
			psVideo->fd = -1;
			continue;
		}

#if defined (ENABLE_DBG)
		fprintf(stdout, "caps (%s): driver=\"%s\" bus_info=\"%s\" card=\"%s\" fd=0x%x \n",
			 strVideoDevNode, cap.driver, cap.bus_info, cap.card, psVideo->fd);
#endif

		if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) ||
			!(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE) ||
			!(cap.capabilities & V4L2_CAP_STREAMING)) {
			fprintf(stderr, "Insufficient capabilities for video device (is %s correct?) \n", strVideoDevNode);
			close(psVideo->fd);
			psVideo->fd = -1;
			continue;
		}

		break;
	}

	if(psVideo->fd < 0) {
		pthread_mutex_unlock(&s_tHantroLock);
		return -1;
	}
	
	return 0;
}

void vc8000_v4l2_close(struct video *psVideo)
{
#if defined (ENABLE_DBG)
	fprintf(stdout, "vc8000_v4l2_close video fd %x \n", psVideo->fd);
#endif
	close(psVideo->fd);
	pthread_mutex_unlock(&s_tHantroLock);
}

//setup output(bitstream) plane
int vc8000_v4l2_setup_output(
	struct video *psVideo,
	unsigned long codec,
	unsigned int size,
	int count
)
{
	struct video *vid = psVideo;
	struct v4l2_format fmt;
	struct v4l2_requestbuffers reqbuf;
	struct v4l2_buffer buf;
	struct v4l2_plane planes[OUT_PLANES];
	int ret;
	int n;

	memzero(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	fmt.fmt.pix_mp.width = MAX_DEC_OUTPUT_WIDTH;
	fmt.fmt.pix_mp.height = MAX_DEC_OUTPUT_HEIGHT;
	fmt.fmt.pix_mp.pixelformat = codec;
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage = size;

	ret = ioctl(vid->fd, VIDIOC_S_FMT, &fmt);
	if (ret) {
		fprintf(stderr, "Failed to set format on OUTPUT (%s) \n", strerror(errno));
		return -1;
	}

#if defined (ENABLE_DBG)
	fprintf(stdout, "Setup decoding OUTPUT buffer size=%u (requested=%u) \n",
	    fmt.fmt.pix_mp.plane_fmt[0].sizeimage, size);
#endif

	vid->out_buf_size = fmt.fmt.pix_mp.plane_fmt[0].sizeimage;

	memzero(reqbuf);
	reqbuf.count = count;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	reqbuf.memory = V4L2_MEMORY_MMAP;

	ret = ioctl(vid->fd, VIDIOC_REQBUFS, &reqbuf);
	if (ret) {
		fprintf(stderr, "REQBUFS failed on OUTPUT queue \n");
		return -1;
	}

	vid->out_buf_cnt = reqbuf.count;

#if defined (ENABLE_DBG)
	fprintf(stdout, "Number of video decoder OUTPUT buffers is %d (requested %d) \n",
	    vid->out_buf_cnt, count);
#endif

	for (n = 0; n < vid->out_buf_cnt; n++) {
		memzero(buf);
		memset(planes, 0, sizeof(planes));
		buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n;
		buf.m.planes = planes;
		buf.length = OUT_PLANES;

		ret = ioctl(vid->fd, VIDIOC_QUERYBUF, &buf);
		if (ret != 0) {
			fprintf(stderr, "QUERYBUF failed on OUTPUT buffer \n");
			return -1;
		}

		vid->out_buf_off[n] = buf.m.planes[0].m.mem_offset;
		vid->out_buf_size = buf.m.planes[0].length;

		vid->out_buf_addr[n] = mmap(NULL, buf.m.planes[0].length,
					    PROT_READ | PROT_WRITE, MAP_SHARED,
					    vid->fd,
					    buf.m.planes[0].m.mem_offset);

		if (vid->out_buf_addr[n] == MAP_FAILED) {
			fprintf(stderr, "Failed to MMAP OUTPUT buffer \n");
			return -1;
		}

		vid->out_buf_flag[n] = eV4L2_BUF_DEQUEUE;
	}

#if defined (ENABLE_DBG)
	fprintf(stdout, "Succesfully mmapped %d OUTPUT buffers \n", n);
#endif

	return 0;
}

void vc8000_v4l2_release_output(
	struct video *psVideo
)
{
	struct video *vid = psVideo;
	int n;
	
	for(n = 0; n < vid->out_buf_cnt; n++)
	{
		if(vid->out_buf_addr[n])
		{
#if defined (ENABLE_DBG)
			fprintf(stdout, "unmap output memeory n:%d , addr:%x, size %d \n", n, vid->out_buf_addr[n], vid->out_buf_size);
#endif
			if(munmap(vid->out_buf_addr[n], vid->out_buf_size) != 0)
			{
#if defined (ENABLE_DBG)
				fprintf(stdout, "unable unmap output memeory %p\n", vid->out_buf_addr[n]);
#endif
			}
			vid->out_buf_addr[n] = NULL;			
		}

	}

}

/* 
Setup capture(decoded) plane
pixel_format: 
	V4L2_PIX_FMT_NV12
	V4L2_PIX_FMT_ARGB32
	V4L2_PIX_FMT_RGB565
*/

int vc8000_v4l2_setup_capture(struct video *psVideo, int pixel_format, int buf_cnt, int w, int h)
{
	struct video *vid = psVideo;
	struct v4l2_format fmt;
	struct v4l2_requestbuffers reqbuf;
	struct v4l2_buffer buf;
	struct v4l2_plane planes[MAX_PLANES];
	int ret;
	int n,p;

	memzero(fmt);
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

#if 0
	if(psVideoPP->enabled)
	{
		//driver directly output framebuffer, so set tiny image size to save memory usage
		fmt.fmt.pix_mp.height = 48;
		fmt.fmt.pix_mp.width = 48;
	}
	else
	{
		fmt.fmt.pix_mp.height = h;
		fmt.fmt.pix_mp.width = w;
	}
#else
	fmt.fmt.pix_mp.height = h;
	fmt.fmt.pix_mp.width = w;
#endif

#if defined (ENABLE_DBG)
	fprintf(stdout, "video_setup_capture: %dx%d\n", w, h);
#endif
	fmt.fmt.pix_mp.pixelformat = pixel_format;

	if(fmt.fmt.pix_mp.pixelformat == V4L2_PIX_FMT_RGB565)
	{
		//Kernel(v4l2_format_info) not support sizeimage for RGB565, set it by myself
		fmt.fmt.pix_mp.plane_fmt[0].sizeimage = fmt.fmt.pix_mp.height * fmt.fmt.pix_mp.width * 2; //2:bpp 
	}

	ret = ioctl(vid->fd, VIDIOC_S_FMT, &fmt);
	if (ret) {
		fprintf(stderr, "Failed to set format (%dx%d) \n", w, h);
		return -1;
	}

	vid->cap_w = fmt.fmt.pix_mp.width;
	vid->cap_h = fmt.fmt.pix_mp.height;

	vid->cap_buf_num_planes = fmt.fmt.pix_mp.num_planes;
	
	for(p = 0; p < vid->cap_buf_num_planes; p ++)
	{
		if(fmt.fmt.pix_mp.plane_fmt[p].sizeimage == 0)
		{
			fprintf(stderr, "video decoder buffer plane[%d]:%d bytes \n",
				p, fmt.fmt.pix_mp.plane_fmt[p].sizeimage);
		}
	}

	vid->cap_buf_cnt = buf_cnt;
	vid->cap_buf_cnt_min = 1;
	vid->cap_buf_queued = 0;

#if defined (ENABLE_DBG)
	fprintf(stdout, "video decoder buffer parameters: %dx%d, planes: %d \n",
	    fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height, fmt.fmt.pix_mp.num_planes);
#endif

	memzero(reqbuf);
	reqbuf.count = vid->cap_buf_cnt;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	reqbuf.memory = V4L2_MEMORY_MMAP;

	ret = ioctl(vid->fd, VIDIOC_REQBUFS, &reqbuf);
	if (ret != 0) {
		fprintf(stderr, "REQBUFS failed on CAPTURE queue (%s) \n", strerror(errno));
		return -1;
	}

#if defined (ENABLE_DBG)
	fprintf(stdout, "Number of CAPTURE buffers is %d (requested %d, extra %d) \n",
	    reqbuf.count, vid->cap_buf_cnt, buf_cnt);
#endif

	vid->cap_buf_cnt = reqbuf.count;

	for (n = 0; n < vid->cap_buf_cnt; n++) {
		memzero(buf);
		memset(planes, 0, sizeof(planes));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n;
		buf.m.planes = planes;
		buf.length = vid->cap_buf_num_planes;

		ret = ioctl(vid->fd, VIDIOC_QUERYBUF, &buf);
		if (ret != 0) {
			fprintf(stderr, "QUERYBUF failed on CAPTURE queue (%s) \n", strerror(errno));
			return -1;
		}
		
		for(p = 0; p < vid->cap_buf_num_planes; p ++){

			vid->cap_buf_off[n][p] = buf.m.planes[p].m.mem_offset;

			vid->cap_buf_addr[n][p] = mmap(NULL, buf.m.planes[p].length,
							   PROT_READ | PROT_WRITE,
							   MAP_SHARED,
							   vid->fd,
							   buf.m.planes[p].m.mem_offset);

			if (vid->cap_buf_addr[n][p] == MAP_FAILED) {
				fprintf(stderr, "Failed to MMAP CAPTURE buffer on plane0 \n");
				return -1;
			}

			vid->cap_buf_flag[n] = eV4L2_BUF_DEQUEUE;
			vid->cap_buf_planes_size[n][p] = buf.m.planes[p].length;
#if defined (ENABLE_DBG)
			fprintf(stdout, "video decoder buffer plane[%d]:%d bytes and address %x \n",
				p, vid->cap_buf_planes_size[n][p], vid->cap_buf_addr[n][p]);
#endif
		}
	}

#if 0
	//get capture buffer DMA address
	for (n = 0; n < vid->cap_buf_cnt; n++) {
		memzero(buf);
		memset(planes, 0, sizeof(planes));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n;

		ret = ioctl(vid->fd, VC8KIOC_GET_BUF_PHY_ADDR, &buf);
		if (ret != 0) {
			fprintf(stderr, "VC8KIOC_GET_BUF_PHY_ADDR failed on CAPTURE queue (%s) \n", strerror(errno));
			return -1;
		}
		
		fprintf(stderr, "VC8KIOC_GET_BUF_PHY_ADDR address:%x \n", buf.reserved);
		
//		vid->cap_buf_dma_addr[n][0] = buf.reserved;
	}
#endif
#if defined (ENABLE_DBG)
	fprintf(stdout, "Succesfully mmapped %d CAPTURE buffers \n", n);
#endif
	return 0;
}


int vc8000_v4l2_setup_post_processing(struct video *psVideo,
	bool bEnablePP,
	int pixel_format,
	int w,
	int h,
	int x,
	int y,
	int rot_op,
	struct video_fb_info *psFBInfo)
{
	struct vc8k_pp_params  sVC8K_PP;

	sVC8K_PP.enable_pp = bEnablePP;
	sVC8K_PP.frame_buff_size = psFBInfo->frame_buf_size;
	sVC8K_PP.frame_buf_paddr= psFBInfo->frame_buf_paddr;
	sVC8K_PP.frame_buf_w = psFBInfo->frame_buf_w;
	sVC8K_PP.frame_buf_h = psFBInfo->frame_buf_h;
	sVC8K_PP.img_out_x = x;
	sVC8K_PP.img_out_y = y;
	sVC8K_PP.img_out_w = w;
	sVC8K_PP.img_out_h = h;
	sVC8K_PP.rotation = rot_op;
	sVC8K_PP.img_out_fmt = pixel_format;
	sVC8K_PP.pp_out_dst = psFBInfo->frame_buf_no;
	sVC8K_PP.libjpeg_mode = 1;
		
	ioctl(psVideo->fd, VC8KIOC_PP_SET_CONFIG, &sVC8K_PP);

#if defined (ENABLE_DBG)
//	printf("DDDDD vc8000_v4l2_setup_post_processing sVC8K_PP.enable_pp %x \n", sVC8K_PP.enable_pp);
	printf("DDDDD vc8000_v4l2_setup_post_processing sVC8K_PP.frame_buf_paddr %p \n", sVC8K_PP.frame_buf_paddr);
//	printf("DDDDD vc8000_v4l2_setup_post_processing sVC8K_PP.frame_buff_size %d \n", sVC8K_PP.frame_buff_size);
	printf("DDDDD vc8000_v4l2_setup_post_processing sVC8K_PP.frame_buf_w %d \n", sVC8K_PP.frame_buf_w);
	printf("DDDDD vc8000_v4l2_setup_post_processing sVC8K_PP.frame_buf_h %d \n", sVC8K_PP.frame_buf_h);
	printf("DDDDD vc8000_v4l2_setup_post_processing sVC8K_PP.img_out_x %d \n", sVC8K_PP.img_out_x);
	printf("DDDDD vc8000_v4l2_setup_post_processing sVC8K_PP.img_out_y %d \n", sVC8K_PP.img_out_y);
	printf("DDDDD vc8000_v4l2_setup_post_processing sVC8K_PP.img_out_w %d \n", sVC8K_PP.img_out_w);
	printf("DDDDD vc8000_v4l2_setup_post_processing sVC8K_PP.img_out_h %d \n", sVC8K_PP.img_out_h);
	printf("DDDDD vc8000_v4l2_setup_post_processing sVC8K_PP.img_out_fmt %d \n", sVC8K_PP.img_out_fmt);
#endif
	return 0;

}

void vc8000_v4l2_release_capture(
	struct video *psVideo
)
{
	struct video *vid = psVideo;
	int n,p;
	
	for(n = 0; n < vid->cap_buf_cnt; n++)
	{

		for(p = 0; p < vid->cap_buf_num_planes; p++)
		{

			if(vid->cap_buf_addr[n][p])
			{
#if defined (ENABLE_DBG)
				fprintf(stdout, "unmap capture memeory n:%d, p:%d, addr:%x, size %d \n", n, p, vid->cap_buf_addr[n][p], vid->cap_buf_planes_size[n][p]);
#endif
				if(munmap(vid->cap_buf_addr[n][p], vid->cap_buf_planes_size[n][p]) != 0)
				{
#if defined (ENABLE_DBG)
					fprintf(stdout, "unable unmap capture memeory %p\n", vid->cap_buf_addr[n][p]);
#endif
				}
				vid->cap_buf_addr[n][p] = NULL;			
			}
		}
	}
}

int vc8000_v4l2_queue_output(
	struct video *psVideo,
	int n,
	int length
)
{
	struct video *vid = psVideo;

	if (n >= vid->out_buf_cnt) {
		fprintf(stderr, "Tried to queue a non exisiting buffer \n");
		return -1;
	}

	return v4l2_queue_buf(psVideo, n, length, 0,
			       V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, OUT_PLANES);
}

int vc8000_v4l2_queue_capture(
	struct video *psVideo,
	int n
)
{
	struct video *vid = psVideo;

	if (n >= vid->cap_buf_cnt) {
		fprintf(stderr, "Tried to queue a non exisiting buffer \n");
		return -1;
	}

	return v4l2_queue_buf(psVideo, n, vid->cap_buf_planes_size[n][0], vid->cap_buf_planes_size[n][0],
			       V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, psVideo->cap_buf_num_planes);
}

int vc8000_v4l2_dequeue_output(
	struct video *psVideo,
	int *n
)
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes[OUT_PLANES];
	int ret;

	memzero(buf);
	buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.m.planes = planes;
	buf.length = OUT_PLANES;

	ret = v4l2_dequeue_buf(psVideo, &buf);
	if (ret < 0)
		return ret;

	*n = buf.index;

	return 0;
}

int vc8000_v4l2_dequeue_capture(
	struct video *psVideo,
	int *n, 
	int *finished,
	unsigned int *bytesused
)
{
	struct v4l2_buffer buf;
	struct v4l2_plane planes[MAX_PLANES];

	memzero(buf);
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.m.planes = planes;
	buf.length = psVideo->cap_buf_num_planes; //CAP_PLANES;

	if (v4l2_dequeue_buf(psVideo, &buf))
		return -1;

	*finished = 0;

	if (buf.flags & V4L2_BUF_FLAG_DONE ||
	    buf.m.planes[0].bytesused)
		*finished = 1;

	if (buf.flags & V4L2_BUF_FLAG_ERROR)
		*finished = 0;

	*bytesused = buf.m.planes[0].bytesused;
	*n = buf.index;

	return 0;
}

int vc8000_v4l2_stream(
	struct video *psVideo,
	enum v4l2_buf_type type, 
	int status
)
{
	struct video *vid = psVideo;
	int ret;

	ret = ioctl(vid->fd, status, &type);
	if (ret) {
		fprintf(stderr, "Failed to change streaming (type=%s, status=%s) \n",
		    dbg_type[type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE],
		    dbg_status[status == VIDIOC_STREAMOFF]);
		return -1;
	}

#if defined (ENABLE_DBG)
	fprintf(stdout, "Stream %s on %s queue \n", dbg_status[status==VIDIOC_STREAMOFF],
	    dbg_type[type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE]);
#endif

	return 0;
}

int vc8000_v4l2_stop_capture(
	struct video *psVideo
)
{
	int ret;
	struct v4l2_requestbuffers reqbuf;
	struct video *vid = psVideo;

	ret = vc8000_v4l2_stream(psVideo, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
			   VIDIOC_STREAMOFF);
	if (ret < 0)
		fprintf(stderr, "STREAMOFF CAPTURE queue failed (%s) \n", strerror(errno));

	memzero(reqbuf);
	reqbuf.memory = V4L2_MEMORY_MMAP;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	ret = ioctl(vid->fd, VIDIOC_REQBUFS, &reqbuf);
	if (ret < 0) {
		fprintf(stderr, "REQBUFS with count=0 on CAPTURE queue failed (%s) \n", strerror(errno));
		return -1;
	}

	vc8000_v4l2_release_capture(psVideo);

	return 0;
}

int vc8000_v4l2_stop_output(
	struct video *psVideo
)
{
	int ret;
	struct v4l2_requestbuffers reqbuf;
	struct video *vid = psVideo;

	ret = vc8000_v4l2_stream(psVideo, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
			   VIDIOC_STREAMOFF);
	if (ret < 0)
		fprintf(stderr, "STREAMOFF OUTPUT queue failed (%s) \n", strerror(errno));

	memzero(reqbuf);
	reqbuf.memory = V4L2_MEMORY_MMAP;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

	ret = ioctl(vid->fd, VIDIOC_REQBUFS, &reqbuf);
	if (ret < 0) {
		fprintf(stderr, "REQBUFS with count=0 on OUTPUT queue failed (%s) \n", strerror(errno));
		return -1;
	}

	vc8000_v4l2_release_output(psVideo);

	return 0;
}

int vc8000_v4l2_stop(
	struct video *psVideo
)
{
	vc8000_v4l2_stop_capture(psVideo);
	vc8000_v4l2_stop_output(psVideo);

	return 0;
}

#define PP_OUT_MAX_WIDTH_UPSCALED(d) (3*(d))
#define PP_OUT_MAX_HEIGHT_UPSCALED(d) (3*(d) - 2)

int vc8000_jpeg_prepare_decompress(
	struct video *psVideo,
	uint32_t u32ImageWidth,
	uint32_t u32ImageHeight,
	uint32_t u32OutputWidth,
	uint32_t u32OutputHeight,
	bool bDirectFBOut,
	struct video_fb_info *psFBInfo,
	uint32_t u32ImgFBPosX,
	uint32_t u32ImgFBPosY,	
	int i32RotOP,
	int pixel_format
)
{
	int i32Ret = 0; 
	int n;

	if(psVideo->fd < 0)
		return -1;

	if((u32OutputWidth > MAX_DEC_OUTPUT_WIDTH) || (u32OutputHeight > MAX_DEC_OUTPUT_HEIGHT))
		return -2;

	//check PP parameter
	if(u32OutputWidth > PP_OUT_MAX_WIDTH_UPSCALED(u32ImageWidth))
	{
		fprintf(stderr, "Upscale factory is over PP support\n");
		return -3;
	}

	if(u32OutputHeight > PP_OUT_MAX_HEIGHT_UPSCALED(u32ImageHeight))
	{
		fprintf(stderr, "Upscale factory is over PP support \n");
		return -4;
	}


	i32Ret = vc8000_v4l2_setup_output(psVideo, 
								V4L2_PIX_FMT_JPEG, 
								u32ImageWidth * u32ImageHeight, 
								1);
	if(i32Ret != 0){
		return -5;
	}

	if(bDirectFBOut == true){
		i32Ret = vc8000_v4l2_setup_capture(psVideo, 
									pixel_format, 
									1, 
									32,
									32
									);
	}
	else {
		i32Ret = vc8000_v4l2_setup_capture(psVideo, 
									pixel_format, 
									1, 
									u32OutputWidth,
									u32OutputHeight
									);
	}

	if(i32Ret != 0){
		vc8000_v4l2_release_output(psVideo);
		return -6;
	}

//    struct video_fb_info sFBInfo;
	if(bDirectFBOut == true)
	{
		//psFBInfo frame buffer parameter provided by caller
		psFBInfo->frame_buf_paddr = 0;
		psFBInfo->direct_fb_out = 1;
	}
	else
	{
		psFBInfo->frame_buf_paddr = 0;
		psFBInfo->frame_buf_size = 0;
		psFBInfo->frame_buf_w = psVideo->cap_w;
		psFBInfo->frame_buf_h = psVideo->cap_h;
		psFBInfo->direct_fb_out = 0;
		u32OutputWidth = psVideo->cap_w;
		u32OutputHeight = psVideo->cap_h;		
	}
	
	vc8000_v4l2_setup_post_processing(psVideo, true, pixel_format, u32OutputWidth, u32OutputHeight, u32ImgFBPosX, u32ImgFBPosY, i32RotOP, psFBInfo);
	    
	vc8000_v4l2_stream(psVideo, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, VIDIOC_STREAMON);
    vc8000_v4l2_stream(psVideo, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, VIDIOC_STREAMON);                

	//put capture dequeued buffer into queue
	for(n = 0; n < psVideo->cap_buf_cnt; n ++)
	{
			if(psVideo->cap_buf_flag[n] == eV4L2_BUF_DEQUEUE)
			{
					vc8000_v4l2_queue_capture(psVideo, n);
					psVideo->cap_buf_flag[n] = eV4L2_BUF_INQUEUE;
			}       
	}

	return 0;
}

int vc8000_jpeg_get_bitstream_buffer(
	struct video *psVideo,
	char **ppchBufferAddr
)
{
	int n;
	*ppchBufferAddr = NULL;
	
	//Get output dequeued buffer 
	for(n = 0; n < psVideo->out_buf_cnt; n ++)
	{
		if(psVideo->out_buf_flag[n] == eV4L2_BUF_DEQUEUE)
		{
			*ppchBufferAddr = psVideo->out_buf_addr[n];
			break;
		}
	}

	return psVideo->out_buf_size;
}

int vc8000_jpeg_inqueue_bitstream_buffer(
	struct video *psVideo,
	char *pchBufferAddr,
	uint32_t u32StreamLen
)
{
	int n;
	int ret;
	//Get output dequeued buffer index
	for(n = 0; n < psVideo->out_buf_cnt; n ++)
	{
		if(psVideo->out_buf_addr[n] == pchBufferAddr)
		{
			ret = vc8000_v4l2_queue_output(psVideo, n, u32StreamLen);
			psVideo->out_buf_flag[n] = eV4L2_BUF_INQUEUE;
			return 0;
		}
	}

	return -1;
}

int vc8000_jpeg_poll_decode_done(
        struct video *psVideo,
        int *p_cap_index
)
{
	struct pollfd pfd;
	short revents;
	int ret, cap_index, finished, output_index;

	pfd.fd = psVideo->fd;
	pfd.events = POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM |
				 POLLRDBAND | POLLPRI;

	ret = -1;
	cap_index = -1;
	output_index = -1;

	while (1) {
		//ret = poll(&pfd, 1, 2000);
		ret = poll(&pfd, 1, -1);

		if (!ret) {
			//timeout
			ret = -1;
			break;
		} else if (ret < 0) {
			fprintf(stderr, "poll error");
			ret = -2;
			break;
		}

		revents = pfd.revents;

		if (revents & (POLLIN | POLLRDNORM)) {
			unsigned int bytesused;

			/* capture buffer is ready */

			ret = vc8000_v4l2_dequeue_capture(psVideo, &cap_index, &finished,
										&bytesused);
			if (ret < 0)
					goto next_event;

			psVideo->cap_buf_flag[cap_index] = eV4L2_BUF_DEQUEUE;
			psVideo->total_captured++;

			//fprintf(stdout, "decoded frame %ld", vid->total_captured);
		}

next_event:

		if (revents & (POLLOUT | POLLWRNORM)) {

			ret = vc8000_v4l2_dequeue_output(psVideo, &output_index);
			if (ret < 0) {
					fprintf(stderr, "dequeue output buffer fail");
			} else {
					psVideo->out_buf_flag[output_index] = eV4L2_BUF_DEQUEUE;
			}

			break;
			// dbg("dequeued output buffer %d", n);
		}
	}

	*p_cap_index = cap_index;

	if (finished == 0) {
		return -3;
	}

//	fprintf(stdout, "vc8000_jpeg_poll_decode_done result %d \n", ret);
	return ret;
}

int vc8000_jpeg_release_decompress(
        struct video *psVideo
)
{
	vc8000_v4l2_stop(psVideo);
}



