/**
 * @file vc8000_v4l2.h vc8000 v4l2 driver
 *
 * Copyright (C) 2021 nuvotn
 */

#ifndef __VC8000_V4L2_H__
#define __VC8000_V4L2_H__

#include <stdio.h>
#include <time.h>
#include <inttypes.h>
#include <linux/videodev2.h>

/*
 * Boolean type
 * see http://www.opengroup.org/onlinepubs/000095399/basedefs/stdbool.h.html
 *     www.gnu.org/software/autoconf/manual/html_node/Particular-Headers.html
 */
#ifdef HAVE_STDBOOL_H
# include <stdbool.h>
#else
# ifndef HAVE__BOOL
#  ifdef __cplusplus
typedef bool _Bool;
#  else
#   define _Bool signed char
#  endif
# endif
# define bool _Bool
# define false 0
# define true 1
# define __bool_true_false_are_defined 1
#endif

/* Maximum number of output buffers */
#define MAX_OUT_BUF		16

/* Maximum number of capture buffers (32 is the limit imposed by MFC */
#define MAX_CAP_BUF		32

/* Number of output planes */
#define OUT_PLANES		1

/* Number of capture planes */
//#define CAP_PLANES		1
#define CAP_PLANES		3

/* Maximum number of planes used in the application */
#define MAX_PLANES		CAP_PLANES

/* Maximum decode resolution*/
#define MAX_DEC_OUTPUT_WIDTH          1920
#define MAX_DEC_OUTPUT_HEIGHT         1080

/* Post processing rotation operation */
#define PP_ROTATION_NONE                                0U
#define PP_ROTATION_RIGHT_90                            1U
#define PP_ROTATION_LEFT_90                             2U
#define PP_ROTATION_HOR_FLIP                            3U
#define PP_ROTATION_VER_FLIP                            4U
#define PP_ROTATION_180                                 5U

#define memzero(x)	memset(&(x), 0, sizeof (x));

typedef enum {
	eV4L2_BUF_DEQUEUE,	
	eV4L2_BUF_INQUEUE
}E_V4L2_BUF_STATUS;

/* video decoder related parameters */
struct video {
	int fd;

	/* Output queue related for encoded bitstream*/ 
	int out_buf_cnt;
	int out_buf_size;
	int out_buf_off[MAX_OUT_BUF];
	char *out_buf_addr[MAX_OUT_BUF];
	E_V4L2_BUF_STATUS out_buf_flag[MAX_OUT_BUF];

	/* Capture queue related for decoded buffer*/
	int cap_w;
	int cap_h;
	int cap_crop_w;
	int cap_crop_h;
	int cap_crop_left;
	int cap_crop_top;
	int cap_buf_cnt;
	int cap_buf_cnt_min;
	int cap_buf_num_planes;
	int cap_buf_planes_size[MAX_CAP_BUF][MAX_PLANES]; //each plane (for example: YUV422P planes)size
	int cap_buf_off[MAX_CAP_BUF][MAX_PLANES];
	char *cap_buf_addr[MAX_CAP_BUF][MAX_PLANES];
//	uint32_t cap_buf_dma_addr[MAX_CAP_BUF][MAX_PLANES];
	E_V4L2_BUF_STATUS cap_buf_flag[MAX_CAP_BUF];
	int cap_buf_queued;
	unsigned long total_captured;
};

// video decode post processing
struct video_fb_info {
	void  *frame_buf_vaddr;          /* virtual address of frame buffer           */
	int   frame_buff_size;
	int   frame_buf_w;               /* width of frame buffer width               */
	int   frame_buf_h;               /* height of frame buffer                    */
};

int vc8000_v4l2_open(struct video *psVideo);
void vc8000_v4l2_close(struct video *psVideo);

/*setup vc8000 v4l2 output(bitstream) plane
codec:
	V4L2_PIX_FMT_H264
	V4L2_PIX_FMT_JPEG
*/

int vc8000_v4l2_setup_output(
	struct video *psVideo,
	unsigned long codec,
	unsigned int size,
	int count
);

/* 
Setup capture(decoded) plane
pixel_format: 
	V4L2_PIX_FMT_NV12
	V4L2_PIX_FMT_ARGB32
	V4L2_PIX_FMT_RGB565
*/

int vc8000_v4l2_setup_capture(
	struct video *psVideo,
	int pixel_format,
	int extra_buf,
	int w,
	int h
);

//Release output and capture plane
void vc8000_v4l2_release_output(
	struct video *psVideo
);

void vc8000_v4l2_release_capture(
	struct video *psVideo
);

//queue output buffer
int vc8000_v4l2_queue_output(
	struct video *psVideo,
	int n,
	int length
);

//queue capture buffer
int vc8000_v4l2_queue_capture(
	struct video *psVideo,
	int n
);

//dequeue output buffer
int vc8000_v4l2_dequeue_output(
	struct video *psVideo,
	int *n
);

//dequeue capture buffer
int vc8000_v4l2_dequeue_capture(
	struct video *psVideo,
	int *n, 
	int *finished,
	unsigned int *bytesused
);

//set output/capture stream on/off
int vc8000_v4l2_stream(
	struct video *psVideo,
	enum v4l2_buf_type type, 
	int status
);

int vc8000_v4l2_stop_capture(
	struct video *psVideo
);

int vc8000_v4l2_stop_output(
	struct video *psVideo
);

//stop vc8000 v4l2 decode all plane
int vc8000_v4l2_stop(
	struct video *psVideo
);

//Prepare JPEG decompress
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
);

//Get JPEG decompress bitstream buffer
int vc8000_jpeg_get_bitstream_buffer(
	struct video *psVideo,
	char **ppchBufferAddr
);

//Inqueue bitstream and trigger decompress
int vc8000_jpeg_inqueue_bitstream_buffer(
	struct video *psVideo,
	char *pchBufferAddr,
	uint32_t u32StreamLen
);

//Wait JPEG decode done
int vc8000_jpeg_poll_decode_done(
        struct video *psVideo,
        int *p_cap_index
);

//Release JPEG decompress
int vc8000_jpeg_release_decompress(
        struct video *psVideo
);

#endif
