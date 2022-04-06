/*
 * jdapistd.c
 *
 * This file was part of the Independent JPEG Group's software:
 * Copyright (C) 1994-1996, Thomas G. Lane.
 * libjpeg-turbo Modifications:
 * Copyright (C) 2010, 2015-2020, 2022, D. R. Commander.
 * Copyright (C) 2015, Google, Inc.
 * For conditions of distribution and use, see the accompanying README.ijg
 * file.
 *
 * This file contains application interface code for the decompression half
 * of the JPEG library.  These are the "standard" API routines that are
 * used in the normal full-decompression case.  They are not used by a
 * transcoding-only application.  Note that if an application links in
 * jpeg_start_decompress, it will end up linking in the entire decompressor.
 * We thus must separate this file from jdapimin.c to avoid linking the
 * whole decompression library into a transcoder.
 */

#include "jinclude.h"
#include "jdmainct.h"
#include "jdcoefct.h"
#include "jdmaster.h"
#include "jdmerge.h"
#include "jdsample.h"
#include "jmemsys.h"

/* Forward declarations */
LOCAL(boolean) output_pass_setup(j_decompress_ptr cinfo);


/*
 * Decompression initialization.
 * jpeg_read_header must be completed before calling this.
 *
 * If a multipass operating mode was selected, this will do all but the
 * last pass, and thus may take a great deal of time.
 *
 * Returns FALSE if suspended.  The return value need be inspected only if
 * a suspending data source is used.
 */

#ifdef WITH_VC8000
typedef enum
{
	eJPEG_SUBSAMPLING_411,
	eJPEG_SUBSAMPLING_420,
	eJPEG_SUBSAMPLING_422,
	eJPEG_SUBSAMPLING_444,
	eJPEG_SUBSAMPLING_UNKNOWN,
}E_JPEG_SUBSAMPLING;

static E_JPEG_SUBSAMPLING
get_subsampling(j_decompress_ptr cinfo)
{
  if(cinfo->num_components != 3)
    return eJPEG_SUBSAMPLING_UNKNOWN;
    
  if((cinfo->comp_info[0].h_samp_factor == 2) && (cinfo->comp_info[0].v_samp_factor == 2))
    return eJPEG_SUBSAMPLING_420;
    
  if((cinfo->comp_info[0].h_samp_factor == 4) && (cinfo->comp_info[0].v_samp_factor == 1))
    return eJPEG_SUBSAMPLING_411;
    
  if((cinfo->comp_info[0].h_samp_factor == 2) && (cinfo->comp_info[0].v_samp_factor == 1))
    return eJPEG_SUBSAMPLING_422;

  if((cinfo->comp_info[0].h_samp_factor == 1) && (cinfo->comp_info[0].v_samp_factor == 1))
    return eJPEG_SUBSAMPLING_444;

  return eJPEG_SUBSAMPLING_UNKNOWN;
}

#define SCALED(dimension, scalingFactor_num, scalingFactor_denom) \
  ((dimension * scalingFactor_num + scalingFactor_denom - 1) / \
   scalingFactor_denom)

static void vc8000_CreateDecompress(j_decompress_ptr cinfo)
{
  //open vc8000 v4l2 device for JPEG decoder
  if(vc8000_v4l2_open(&cinfo->master->sHWJpegVideo) == 0)
    cinfo->master->bHWJpegCodecOpened = TRUE;
  else
    cinfo->master->bHWJpegCodecOpened = FALSE;  
}

#include <stdlib.h>
#include <sys/time.h>
#include <limits.h>

static double getTimeSec(void)
{
  struct timeval tv;

  if (gettimeofday(&tv, NULL) < 0)
	return 0.0;
  else 
	return (double)(tv.tv_sec ) + ((double)(tv.tv_usec / 1000000.));
}


static int vc8000_start_decompress(j_decompress_ptr cinfo)
{
  int pixel_format;
  int i32Ret;

  cinfo->master->bHWJpegDecodeDone = FALSE;

  if(cinfo->out_color_space == JCS_EXT_BGRA)
  {
    pixel_format = V4L2_PIX_FMT_ABGR32;
  }
  else if(cinfo->out_color_space == JCS_EXT_ARGB)
  {
    pixel_format = V4L2_PIX_FMT_ABGR32;
  }
  else if(cinfo->out_color_space == JCS_EXT_BGR)
  {
    pixel_format = V4L2_PIX_FMT_ABGR32;
  }
  else if(cinfo->out_color_space == JCS_RGB)
  {
    pixel_format = V4L2_PIX_FMT_ABGR32;
  }
  else if(cinfo->out_color_space == JCS_EXT_RGB)
  {
    pixel_format = V4L2_PIX_FMT_ABGR32;
  }
  else if(cinfo->out_color_space == JCS_RGB565)
  {
    pixel_format = V4L2_PIX_FMT_RGB565;
  }
  else if(cinfo->raw_data_out)
  {
    E_JPEG_SUBSAMPLING eSubsampling = get_subsampling(cinfo);
    if(eSubsampling == eJPEG_SUBSAMPLING_420)
      pixel_format = V4L2_PIX_FMT_NV12;  //Planar format
    else if(eSubsampling == eJPEG_SUBSAMPLING_422)
      pixel_format = V4L2_PIX_FMT_YUYV;  //packet format
    else
      return -1;    
  }
//  else if(cinfo->out_color_space == JCS_YCbCr)
//  {
//    pixel_format = V4L2_PIX_FMT_YUYV;
//  }
  else 
	return -2;

  jpeg_calc_output_dimensions(cinfo);

  uint32_t decode_src_width;
  uint32_t decode_src_height;
  uint32_t estimate_output_width;
  uint32_t estimate_output_height;
  
  //Align to VC8000 MCU dimension (16x16)
  decode_src_width = jdiv_round_up(cinfo->image_width, 16) * 16;
  decode_src_height = jdiv_round_up(cinfo->image_height, 16) * 16;
  
  estimate_output_width = SCALED(decode_src_width, cinfo->scale_num, cinfo->scale_denom);  
  estimate_output_height = SCALED(decode_src_height, cinfo->scale_num, cinfo->scale_denom);  
  
  struct video_fb_info sFBInfo;
  int iRotOP = PP_ROTATION_NONE;

  sFBInfo.frame_buf_no = UINT_MAX;
  
  if(cinfo->master->bHWJpegDirectFBEnable)
  {
	sFBInfo.frame_buf_w = cinfo->master->sDirectFBParam.fb_width;
	sFBInfo.frame_buf_h = cinfo->master->sDirectFBParam.fb_height;
	sFBInfo.frame_buf_no = cinfo->master->sDirectFBParam.fb_no;
	estimate_output_width = cinfo->master->sDirectFBParam.img_width;
	estimate_output_height = cinfo->master->sDirectFBParam.img_height;	
	iRotOP = cinfo->master->sDirectFBParam.rotation_op;
  }
  else
  {
	//output to memory buffer case. 
	//output dimension is too small, maybe using software decoder is better.
    if((estimate_output_width < 64) && (estimate_output_height < 64))
		return -3;
  }

  if((iRotOP == PP_ROTATION_RIGHT_90) || (iRotOP == PP_ROTATION_LEFT_90))
  {
	uint32_t u32Temp;
	u32Temp = decode_src_width;
	decode_src_width = decode_src_height;
	decode_src_height = u32Temp;
  }

  if(((decode_src_width > estimate_output_width) && (decode_src_height < estimate_output_height)) ||
	((decode_src_width < estimate_output_width) && (decode_src_height > estimate_output_height)))
  {
	//the scale up/down of width and height must be consistent
	return -4;
  }

  double dStartTime = getTimeSec();
  i32Ret = vc8000_jpeg_prepare_decompress(
			&cinfo->master->sHWJpegVideo,
			cinfo->image_width,
			cinfo->image_height,
			estimate_output_width,
			estimate_output_height,
			cinfo->master->bHWJpegDirectFBEnable,
			&sFBInfo,
			cinfo->master->sDirectFBParam.img_pos_x,
			cinfo->master->sDirectFBParam.img_pos_y,
			iRotOP,
			pixel_format);

  if(i32Ret != 0) {
//	printf("Not support by VC8000, output dimension width: %d, height: %d \n", estimate_output_width, estimate_output_height);
    return -5;
  }

//  printf("vc8000_jpeg_prepare_decompress time %f sec\n", getTimeSec() - dStartTime);
    
  char *pchStreamBuf = NULL;
  unsigned int u32StreamBufSize = 0;
  unsigned int u32StreamLen = 0;
  
  u32StreamBufSize = vc8000_jpeg_get_bitstream_buffer(&cinfo->master->sHWJpegVideo, &pchStreamBuf);
  
  if(pchStreamBuf == NULL)
  {
	  //release resource
	  vc8000_jpeg_release_decompress(&cinfo->master->sHWJpegVideo);
	  return -6;
  }
  
  //fill bitstream to bitstream buffer
  struct jpeg_source_mgr *src_mgr = cinfo->master->src_hw_jpeg;

  if(cinfo->master->eJpegSrcType == eJPEG_SRC_MEM)
  {
    if(src_mgr->bytes_in_buffer > u32StreamBufSize)
    {
	  //release resource
	  vc8000_jpeg_release_decompress(&cinfo->master->sHWJpegVideo);
	  return -7;
	}
    memcpy(pchStreamBuf, src_mgr->next_input_byte, src_mgr->bytes_in_buffer);
	u32StreamLen = src_mgr->bytes_in_buffer; 
  }
  else if(cinfo->master->eJpegSrcType == eJPEG_SRC_FILE)
  {
    long u64CurFilePos = cinfo->master->seek_file_pos(cinfo, 0, SEEK_SET);

    while(src_mgr->fill_input_buffer(cinfo) == TRUE)
    {
	  if((u32StreamLen + src_mgr->bytes_in_buffer) <= u32StreamBufSize)
	  {
		memcpy(pchStreamBuf + u32StreamLen, src_mgr->next_input_byte, src_mgr->bytes_in_buffer);
	  }
	  u32StreamLen += src_mgr->bytes_in_buffer;
	}    
    cinfo->master->seek_file_pos(cinfo, u64CurFilePos, SEEK_SET);

	if(u32StreamLen > u32StreamBufSize)
    {
	  //release resource
	  vc8000_jpeg_release_decompress(&cinfo->master->sHWJpegVideo);
	  return -8;
	}
  }
  else
  {
    struct jpeg_source_mgr *src = cinfo->src;

	if(src)
	{
      vc8000_jpeg_release_decompress(&cinfo->master->sHWJpegVideo);
      return -9;
	}
  }

//  printf("fill bitstream time %f sec\n", getTimeSec() - dStartTime);

  //inqueue bitstream buffer
  vc8000_jpeg_inqueue_bitstream_buffer(&cinfo->master->sHWJpegVideo, pchStreamBuf, u32StreamLen);

//  printf("vc8000_jpeg_inqueue_bitstream_buffer time %f sec\n", getTimeSec() - dStartTime);
  //wait decode done
  int i32DecBufIndex = 0;
  
  i32Ret = vc8000_jpeg_poll_decode_done(&cinfo->master->sHWJpegVideo, &i32DecBufIndex);
  
  if(i32Ret != 0)
  {
    //release resource
    vc8000_jpeg_release_decompress(&cinfo->master->sHWJpegVideo);
    return -10;
  }

//  printf("vc8000_jpeg_poll_decode_done time %f sec\n", getTimeSec() - dStartTime);

  cinfo->master->pu8DecodedBuf = cinfo->master->sHWJpegVideo.cap_buf_addr[i32DecBufIndex][0];
  cinfo->master->i32PixelFormat = pixel_format;
  cinfo->master->u32DecodeImageWidth = cinfo->master->sHWJpegVideo.cap_w;
  cinfo->master->u32DecodeImageHeight = cinfo->master->sHWJpegVideo.cap_h;
  cinfo->master->bHWJpegDecodeDone = TRUE;

  return 0;
}


#endif

GLOBAL(boolean)
jpeg_start_decompress(j_decompress_ptr cinfo)
{
#ifdef WITH_VC8000
  int ret;

  if(cinfo->master->bHWJpegDeocdeEnable == TRUE) {
    vc8000_CreateDecompress(cinfo);
  }

  if(cinfo->master->bHWJpegCodecOpened) {
	ret = vc8000_start_decompress(cinfo);
    if(ret != 0)
    {
		printf("fallback to software decompress reason %d \n", ret);
	}
  }

#endif

  if (cinfo->global_state == DSTATE_READY) {
    /* First call: initialize master control, select active modules */
    jinit_master_decompress(cinfo);
    if (cinfo->buffered_image) {
      /* No more work here; expecting jpeg_start_output next */
      cinfo->global_state = DSTATE_BUFIMAGE;
      return TRUE;
    }
    cinfo->global_state = DSTATE_PRELOAD;
  }
  if (cinfo->global_state == DSTATE_PRELOAD) {
    /* If file has multiple scans, absorb them all into the coef buffer */
    if (cinfo->inputctl->has_multiple_scans) {
#ifdef D_MULTISCAN_FILES_SUPPORTED
      for (;;) {
        int retcode;
        /* Call progress monitor hook if present */
        if (cinfo->progress != NULL)
          (*cinfo->progress->progress_monitor) ((j_common_ptr)cinfo);
        /* Absorb some more input */
        retcode = (*cinfo->inputctl->consume_input) (cinfo);
        if (retcode == JPEG_SUSPENDED)
          return FALSE;
        if (retcode == JPEG_REACHED_EOI)
          break;
        /* Advance progress counter if appropriate */
        if (cinfo->progress != NULL &&
            (retcode == JPEG_ROW_COMPLETED || retcode == JPEG_REACHED_SOS)) {
          if (++cinfo->progress->pass_counter >= cinfo->progress->pass_limit) {
            /* jdmaster underestimated number of scans; ratchet up one scan */
            cinfo->progress->pass_limit += (long)cinfo->total_iMCU_rows;
          }
        }
      }
#else
      ERREXIT(cinfo, JERR_NOT_COMPILED);
#endif /* D_MULTISCAN_FILES_SUPPORTED */
    }
    cinfo->output_scan_number = cinfo->input_scan_number;
  } else if (cinfo->global_state != DSTATE_PRESCAN)
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  /* Perform any dummy output passes, and set up for the final pass */
  return output_pass_setup(cinfo);
}


/*
 * Set up for an output pass, and perform any dummy pass(es) needed.
 * Common subroutine for jpeg_start_decompress and jpeg_start_output.
 * Entry: global_state = DSTATE_PRESCAN only if previously suspended.
 * Exit: If done, returns TRUE and sets global_state for proper output mode.
 *       If suspended, returns FALSE and sets global_state = DSTATE_PRESCAN.
 */

LOCAL(boolean)
output_pass_setup(j_decompress_ptr cinfo)
{
  if (cinfo->global_state != DSTATE_PRESCAN) {
    /* First call: do pass setup */
    (*cinfo->master->prepare_for_output_pass) (cinfo);
    cinfo->output_scanline = 0;
    cinfo->global_state = DSTATE_PRESCAN;
  }
  /* Loop over any required dummy passes */
  while (cinfo->master->is_dummy_pass) {
#ifdef QUANT_2PASS_SUPPORTED
    /* Crank through the dummy pass */
    while (cinfo->output_scanline < cinfo->output_height) {
      JDIMENSION last_scanline;
      /* Call progress monitor hook if present */
      if (cinfo->progress != NULL) {
        cinfo->progress->pass_counter = (long)cinfo->output_scanline;
        cinfo->progress->pass_limit = (long)cinfo->output_height;
        (*cinfo->progress->progress_monitor) ((j_common_ptr)cinfo);
      }
      /* Process some data */
      last_scanline = cinfo->output_scanline;
      (*cinfo->main->process_data) (cinfo, (JSAMPARRAY)NULL,
                                    &cinfo->output_scanline, (JDIMENSION)0);
      if (cinfo->output_scanline == last_scanline)
        return FALSE;           /* No progress made, must suspend */
    }
    /* Finish up dummy pass, and set up for another one */
    (*cinfo->master->finish_output_pass) (cinfo);
    (*cinfo->master->prepare_for_output_pass) (cinfo);
    cinfo->output_scanline = 0;
#else
    ERREXIT(cinfo, JERR_NOT_COMPILED);
#endif /* QUANT_2PASS_SUPPORTED */
  }
  /* Ready for application to drive output pass through
   * jpeg_read_scanlines or jpeg_read_raw_data.
   */
  cinfo->global_state = cinfo->raw_data_out ? DSTATE_RAW_OK : DSTATE_SCANNING;
  return TRUE;
}


/*
 * Enable partial scanline decompression
 *
 * Must be called after jpeg_start_decompress() and before any calls to
 * jpeg_read_scanlines() or jpeg_skip_scanlines().
 *
 * Refer to libjpeg.txt for more information.
 */

GLOBAL(void)
jpeg_crop_scanline(j_decompress_ptr cinfo, JDIMENSION *xoffset,
                   JDIMENSION *width)
{
  int ci, align, orig_downsampled_width;
  JDIMENSION input_xoffset;
  boolean reinit_upsampler = FALSE;
  jpeg_component_info *compptr;
  my_master_ptr master = (my_master_ptr)cinfo->master;

  if (cinfo->global_state != DSTATE_SCANNING || cinfo->output_scanline != 0)
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);

  if (!xoffset || !width)
    ERREXIT(cinfo, JERR_BAD_CROP_SPEC);

  /* xoffset and width must fall within the output image dimensions. */
  if (*width == 0 || *xoffset + *width > cinfo->output_width)
    ERREXIT(cinfo, JERR_WIDTH_OVERFLOW);

  /* No need to do anything if the caller wants the entire width. */
  if (*width == cinfo->output_width)
    return;

  /* Ensuring the proper alignment of xoffset is tricky.  At minimum, it
   * must align with an MCU boundary, because:
   *
   *   (1) The IDCT is performed in blocks, and it is not feasible to modify
   *       the algorithm so that it can transform partial blocks.
   *   (2) Because of the SIMD extensions, any input buffer passed to the
   *       upsampling and color conversion routines must be aligned to the
   *       SIMD word size (for instance, 128-bit in the case of SSE2.)  The
   *       easiest way to accomplish this without copying data is to ensure
   *       that upsampling and color conversion begin at the start of the
   *       first MCU column that will be inverse transformed.
   *
   * In practice, we actually impose a stricter alignment requirement.  We
   * require that xoffset be a multiple of the maximum MCU column width of all
   * of the components (the "iMCU column width.")  This is to simplify the
   * single-pass decompression case, allowing us to use the same MCU column
   * width for all of the components.
   */
  if (cinfo->comps_in_scan == 1 && cinfo->num_components == 1)
    align = cinfo->_min_DCT_scaled_size;
  else
    align = cinfo->_min_DCT_scaled_size * cinfo->max_h_samp_factor;

  /* Adjust xoffset to the nearest iMCU boundary <= the requested value */
  input_xoffset = *xoffset;
  *xoffset = (input_xoffset / align) * align;

  /* Adjust the width so that the right edge of the output image is as
   * requested (only the left edge is altered.)  It is important that calling
   * programs check this value after this function returns, so that they can
   * allocate an output buffer with the appropriate size.
   */
  *width = *width + input_xoffset - *xoffset;
  cinfo->output_width = *width;
  if (master->using_merged_upsample && cinfo->max_v_samp_factor == 2) {
    my_merged_upsample_ptr upsample = (my_merged_upsample_ptr)cinfo->upsample;
    upsample->out_row_width =
      cinfo->output_width * cinfo->out_color_components;
  }

  /* Set the first and last iMCU columns that we must decompress.  These values
   * will be used in single-scan decompressions.
   */
  cinfo->master->first_iMCU_col = (JDIMENSION)(long)(*xoffset) / (long)align;
  cinfo->master->last_iMCU_col =
    (JDIMENSION)jdiv_round_up((long)(*xoffset + cinfo->output_width),
                              (long)align) - 1;

  for (ci = 0, compptr = cinfo->comp_info; ci < cinfo->num_components;
       ci++, compptr++) {
    int hsf = (cinfo->comps_in_scan == 1 && cinfo->num_components == 1) ?
              1 : compptr->h_samp_factor;

    /* Set downsampled_width to the new output width. */
    orig_downsampled_width = compptr->downsampled_width;
    compptr->downsampled_width =
      (JDIMENSION)jdiv_round_up((long)(cinfo->output_width *
                                       compptr->h_samp_factor),
                                (long)cinfo->max_h_samp_factor);
    if (compptr->downsampled_width < 2 && orig_downsampled_width >= 2)
      reinit_upsampler = TRUE;

    /* Set the first and last iMCU columns that we must decompress.  These
     * values will be used in multi-scan decompressions.
     */
    cinfo->master->first_MCU_col[ci] =
      (JDIMENSION)(long)(*xoffset * hsf) / (long)align;
    cinfo->master->last_MCU_col[ci] =
      (JDIMENSION)jdiv_round_up((long)((*xoffset + cinfo->output_width) * hsf),
                                (long)align) - 1;
  }

  if (reinit_upsampler) {
    cinfo->master->jinit_upsampler_no_alloc = TRUE;
    jinit_upsampler(cinfo);
    cinfo->master->jinit_upsampler_no_alloc = FALSE;
  }
}


/*
 * Read some scanlines of data from the JPEG decompressor.
 *
 * The return value will be the number of lines actually read.
 * This may be less than the number requested in several cases,
 * including bottom of image, data source suspension, and operating
 * modes that emit multiple scanlines at a time.
 *
 * Note: we warn about excess calls to jpeg_read_scanlines() since
 * this likely signals an application programmer error.  However,
 * an oversize buffer (max_lines > scanlines remaining) is not an error.
 */

#ifdef WITH_VC8000

/**
 * Pixel size (in bytes) for a given pixel format
 */
static const int csPixelSize[JPEG_NUMCS] = {
  -1,	/*JCS_UNKNOWN*/ 
  1,	/*JCS_GRAYSCALE*/
  3,	/*JCS_RGB*/
  2,	/*JCS_YCbCr*/
  4,	/*JCS_CMYK*/
  4,	/*JCS_YCCK*/
  3,	/*JCS_EXT_RGB*/
  4,	/*JCS_EXT_RGBX*/
  3,	/*JCS_EXT_BGR*/
  4,	/*JCS_EXT_BGRX*/
  4,	/*JCS_EXT_XBGR*/
  4,	/*JCS_EXT_XRGB*/
  4,	/*JCS_EXT_RGBA*/
  4,	/*JCS_EXT_BGRA*/
  4,	/*JCS_EXT_ABGR*/
  4,	/*JCS_EXT_ARGB*/
  2,	/*JCS_RGB565*/
};

#if 1
//from https://www.twblogs.net/a/5ef44dd90cb8aa7778837d67
// size must multiple of 64
static void _memcpy_fast(volatile void *dst, volatile void *src, int sz)
{
    asm volatile (
        "NEONCopyPLD: \n"
		"sub %[dst], %[dst], #64 \n"
		"1: \n"
		"ldnp q0, q1, [%[src]] \n"
		"ldnp q2, q3, [%[src], #32] \n"
		"add %[dst], %[dst], #64 \n"
		"subs %[sz], %[sz], #64 \n"
		"add %[src], %[src], #64 \n"
		"stnp q0, q1, [%[dst]] \n"
		"stnp q2, q3, [%[dst], #32] \n"
		"b.gt 1b \n"
		: [dst]"+r"(dst), [src]"+r"(src), [sz]"+r"(sz) : : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "cc", "memory");
}
#else
//from https://www.twblogs.net/a/5ef44dd90cb8aa7778837d67
static void _memcpy_fast(volatile void *dst, volatile void *src, int sz)
{
    asm volatile (
        "NEONCopyPLD: \n"
		"sub %[src], %[src], #32 \n"
		"sub %[dst], %[dst], #32 \n"
		"1: \n"
		"ldp q0, q1, [%[src], #32] \n"
		"ldp q2, q3, [%[src], #64]! \n"
		"subs %[sz], %[sz], #64 \n"
		"stp q0, q1, [%[dst], #32] \n"
		"stp q2, q3, [%[dst], #64]! \n"
		"b.gt 1b \n"
		: [dst]"+r"(dst), [src]"+r"(src), [sz]"+r"(sz) : : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "cc", "memory");
}
#endif

static JDIMENSION
vc8000_read_scanlines(j_decompress_ptr cinfo, JSAMPARRAY scanlines,
                    JDIMENSION max_lines)
{
  JDIMENSION row_ctr;
  int i,j;
  unsigned char *pu8DecodedSrc;
  unsigned char *pu8RGBRow;
  J_COLOR_SPACE eDecodedSrcCS;
  int i32DecodedSrcPixelSize;
  int i32OutputPixelSize;
  unsigned int u32DecodedSrcRowBytes;
  unsigned int u32RowBytes;

  eDecodedSrcCS = cinfo->out_color_space;
  
  if(cinfo->master->i32PixelFormat == V4L2_PIX_FMT_ABGR32)
    eDecodedSrcCS = JCS_EXT_ABGR;

  i32DecodedSrcPixelSize = csPixelSize[eDecodedSrcCS];
  i32OutputPixelSize = csPixelSize[cinfo->out_color_space];
  
  u32DecodedSrcRowBytes = cinfo->master->u32DecodeImageWidth * i32DecodedSrcPixelSize;
  u32RowBytes = cinfo->output_width * i32OutputPixelSize;

  row_ctr = max_lines;
  
  if((cinfo->output_scanline + row_ctr) >= cinfo->output_height)
	row_ctr = cinfo->output_height - cinfo->output_scanline;

  for(i = 0; i < row_ctr; i ++)
  {
    pu8DecodedSrc = cinfo->master->pu8DecodedBuf + ((cinfo->output_scanline + i) * u32DecodedSrcRowBytes);
    pu8DecodedSrc += cinfo->master->first_iMCU_col * i32DecodedSrcPixelSize;

    if(cinfo->master->i32PixelFormat == V4L2_PIX_FMT_ABGR32)
    {
	  if((cinfo->out_color_space == JCS_EXT_RGB) || (cinfo->out_color_space == JCS_RGB))
	  {
	    //copy BGRA data to RGB888
	    pu8RGBRow = scanlines[i];
#if 0
		//1608x1072: ~863.1ms
        for(j = 0; j < cinfo->output_width; j ++)
        {
		  //RGB pixel size 3 bytes, BGRA pixel size 4 bytes
          *pu8RGBRow++ = pu8DecodedSrc[2];  
          *pu8RGBRow++ = pu8DecodedSrc[1];
          *pu8RGBRow++ = pu8DecodedSrc[0];
		  pu8DecodedSrc += i32DecodedSrcPixelSize;
	    }
#else
		//1608x1072: ~76.3ms
		int u32RemainBytes = u32RowBytes & 11;
		int u32FirstCopyBytes = u32RowBytes - u32RemainBytes;

		register uint32_t u32Temp0;
		register uint32_t u32Temp1;
		register uint32_t u32Temp2;

		if(u32FirstCopyBytes)
		{
			for(j = 0; j < u32FirstCopyBytes; j = j + 12)
			{
			  //BGR pixel size 3 bytes, BGRA pixel size 4 bytes
			  u32Temp0 = (pu8DecodedSrc[2]) | (pu8DecodedSrc[1] << 8) | (pu8DecodedSrc[0] <<  16) | (pu8DecodedSrc[6] <<  24);  
			  u32Temp1 = (pu8DecodedSrc[5]) | (pu8DecodedSrc[4] << 8) | (pu8DecodedSrc[10] <<  16) | (pu8DecodedSrc[9] <<  24);  
			  u32Temp2 = (pu8DecodedSrc[8]) | (pu8DecodedSrc[14] << 8) | (pu8DecodedSrc[13] <<  16) | (pu8DecodedSrc[12] <<  24);  

			  *(uint32_t *)pu8RGBRow = u32Temp0;
			  *(uint32_t *)(pu8RGBRow + 4) = u32Temp1;
			  *(uint32_t *)(pu8RGBRow + 8) = u32Temp2;           
			  pu8RGBRow += 12;
			  pu8DecodedSrc += 16; //i32DecodedSrcPixelSize;
			}
		}

		//remain bytes ( < 12bytes) 
		if(u32RemainBytes)
		{
			for(j = 0; j < u32RemainBytes; j = j + 3)
			{
			  *pu8RGBRow++ = pu8DecodedSrc[2];  
			  *pu8RGBRow++ = pu8DecodedSrc[1];
			  *pu8RGBRow++ = pu8DecodedSrc[0];
			  pu8DecodedSrc += i32DecodedSrcPixelSize;
			}
		}

#endif


	  }
	  else if(cinfo->out_color_space == JCS_EXT_BGR)
	  {
	     //copy BGRA data to BGR888
	    pu8RGBRow = scanlines[i];

#if 0		
         for(j = 0; j < cinfo->output_width; j ++)
         {
		   //BGR pixel size 3 bytes, BGRA pixel size 4 bytes
           *pu8RGBRow++ = pu8DecodedSrc[0];  
           *pu8RGBRow++ = pu8DecodedSrc[1];
           *pu8RGBRow++ = pu8DecodedSrc[2];
		   pu8DecodedSrc += i32DecodedSrcPixelSize;
	     }
#else
		int u32RemainBytes = u32RowBytes & 11;
		int u32FirstCopyBytes = u32RowBytes - u32RemainBytes;

		register uint32_t u32Temp0;
		register uint32_t u32Temp1;
		register uint32_t u32Temp2;

		if(u32FirstCopyBytes)
		{
			for(j = 0; j < u32FirstCopyBytes; j = j + 12)
			{
			  //BGR pixel size 3 bytes, BGRA pixel size 4 bytes
			  u32Temp0 = (pu8DecodedSrc[0]) | (pu8DecodedSrc[1] << 8) | (pu8DecodedSrc[2] <<  16) | (pu8DecodedSrc[4] <<  24);  
			  u32Temp1 = (pu8DecodedSrc[5]) | (pu8DecodedSrc[6] << 8) | (pu8DecodedSrc[8] <<  16) | (pu8DecodedSrc[9] <<  24);  
			  u32Temp2 = (pu8DecodedSrc[10]) | (pu8DecodedSrc[12] << 8) | (pu8DecodedSrc[13] <<  16) | (pu8DecodedSrc[14] <<  24);  

			  *(uint32_t *)pu8RGBRow = u32Temp0;
			  *(uint32_t *)(pu8RGBRow + 4) = u32Temp1;
			  *(uint32_t *)(pu8RGBRow + 8) = u32Temp2;           
			  pu8RGBRow += 12;
			  pu8DecodedSrc += 16; //i32DecodedSrcPixelSize;
			}
		}

		//remain bytes ( < 12bytes) 
		if(u32RemainBytes)
		{
			for(j = 0; j < u32RemainBytes; j = j + 3)
			{
			  *pu8RGBRow++ = pu8DecodedSrc[0];  
			  *pu8RGBRow++ = pu8DecodedSrc[1];
			  *pu8RGBRow++ = pu8DecodedSrc[2];
			  pu8DecodedSrc += i32DecodedSrcPixelSize;
			}
		}
#endif

	  }
	  else if(cinfo->out_color_space == JCS_EXT_ARGB)
	  {
	    //copy BGRA data to ARGB
	    pu8RGBRow = scanlines[i];

#if 0		
        for(j = 0; j < cinfo->output_width; j ++)
        {
		  //ARGB pixel size 4 bytes, BGRA pixel size 4 bytes
          *pu8RGBRow++ = pu8DecodedSrc[3];  
          *pu8RGBRow++ = pu8DecodedSrc[2];
          *pu8RGBRow++ = pu8DecodedSrc[1];
          *pu8RGBRow++ = pu8DecodedSrc[0];
		  pu8DecodedSrc += i32DecodedSrcPixelSize;
	    }
#else
		int u32RemainBytes = u32RowBytes & 15;
		int u32FirstCopyBytes = u32RowBytes - u32RemainBytes;

		register uint32_t u32Temp0;
		register uint32_t u32Temp1;
		register uint32_t u32Temp2;
		register uint32_t u32Temp3;

		if(u32FirstCopyBytes)
		{
			for(j = 0; j < u32FirstCopyBytes; j = j + 16)
			{
			  //ARGB pixel size 4 bytes, BGRA pixel size 4 bytes
			  u32Temp0 = (pu8DecodedSrc[3]) | (pu8DecodedSrc[2] << 8) | (pu8DecodedSrc[1] <<  16) | (pu8DecodedSrc[0] <<  24);  
			  u32Temp1 = (pu8DecodedSrc[7]) | (pu8DecodedSrc[6] << 8) | (pu8DecodedSrc[5] <<  16) | (pu8DecodedSrc[4] <<  24);  
			  u32Temp2 = (pu8DecodedSrc[11]) | (pu8DecodedSrc[10] << 8) | (pu8DecodedSrc[9] <<  16) | (pu8DecodedSrc[8] <<  24);  
			  u32Temp3 = (pu8DecodedSrc[15]) | (pu8DecodedSrc[14] << 8) | (pu8DecodedSrc[13] <<  16) | (pu8DecodedSrc[12] <<  24);  
			  *(uint32_t *)pu8RGBRow = u32Temp0;
			  *(uint32_t *)(pu8RGBRow + 4) = u32Temp1;
			  *(uint32_t *)(pu8RGBRow + 8) = u32Temp2;
			  *(uint32_t *)(pu8RGBRow + 12) = u32Temp3;    
			  pu8RGBRow += 16;
			  pu8DecodedSrc += 16; //i32DecodedSrcPixelSize;
			}
		}

		//remain bytes ( < 16 bytes) 
		if(u32RemainBytes)
		{
			for(j = 0; j < u32RemainBytes; j = j + 4)
			{
			  *pu8RGBRow++ = pu8DecodedSrc[3];  
			  *pu8RGBRow++ = pu8DecodedSrc[2];
			  *pu8RGBRow++ = pu8DecodedSrc[1];
			  *pu8RGBRow++ = pu8DecodedSrc[0];
			  pu8DecodedSrc += i32DecodedSrcPixelSize;
			}
		}
#endif

	  }	   
	  else
	  {

#if 1
		int u32RemainBytes = u32RowBytes & 63;
		int u32FastCopyBytes = u32RowBytes - u32RemainBytes;

	    pu8RGBRow = scanlines[i];
		
		if(u32FastCopyBytes)
			_memcpy_fast(pu8RGBRow, pu8DecodedSrc, u32FastCopyBytes); //1608x1072: 57.6ms

		if(u32RemainBytes)
			memcpy(pu8RGBRow + u32FastCopyBytes, pu8DecodedSrc + u32FastCopyBytes, u32RemainBytes);
#else
		memcpy(scanlines[i], pu8DecodedSrc, u32RowBytes); //1608x1072: 97.3ms
#endif
	  }
    }
	else
	{
      memcpy(scanlines[i], pu8DecodedSrc, u32RowBytes);
    }
  }	

  return row_ctr;
}

#endif

GLOBAL(JDIMENSION)
jpeg_read_scanlines(j_decompress_ptr cinfo, JSAMPARRAY scanlines,
                    JDIMENSION max_lines)
{
  JDIMENSION row_ctr;

  if (cinfo->global_state != DSTATE_SCANNING)
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  if (cinfo->output_scanline >= cinfo->output_height) {
    WARNMS(cinfo, JWRN_TOO_MUCH_DATA);
    return 0;
  }

#ifdef WITH_VC8000
  if(cinfo->master->bHWJpegDecodeDone)
  {
    row_ctr = 0;
    double dStartTime = getTimeSec();
    row_ctr = vc8000_read_scanlines(cinfo, scanlines, max_lines);
    //printf("vc8000_read_scanlines time %f sec\n", getTimeSec() - dStartTime);

    if(row_ctr > 0)
    {
      cinfo->output_scanline += row_ctr;
      return row_ctr;
    }
  }
#endif

  /* Call progress monitor hook if present */
  if (cinfo->progress != NULL) {
    cinfo->progress->pass_counter = (long)cinfo->output_scanline;
    cinfo->progress->pass_limit = (long)cinfo->output_height;
    (*cinfo->progress->progress_monitor) ((j_common_ptr)cinfo);
  }

  /* Process some data */
  row_ctr = 0;
  (*cinfo->main->process_data) (cinfo, scanlines, &row_ctr, max_lines);
  cinfo->output_scanline += row_ctr;
  return row_ctr;
}


/* Dummy color convert function used by jpeg_skip_scanlines() */
LOCAL(void)
noop_convert(j_decompress_ptr cinfo, JSAMPIMAGE input_buf,
             JDIMENSION input_row, JSAMPARRAY output_buf, int num_rows)
{
}


/* Dummy quantize function used by jpeg_skip_scanlines() */
LOCAL(void)
noop_quantize(j_decompress_ptr cinfo, JSAMPARRAY input_buf,
              JSAMPARRAY output_buf, int num_rows)
{
}


/*
 * In some cases, it is best to call jpeg_read_scanlines() and discard the
 * output, rather than skipping the scanlines, because this allows us to
 * maintain the internal state of the context-based upsampler.  In these cases,
 * we set up and tear down a dummy color converter in order to avoid valgrind
 * errors and to achieve the best possible performance.
 */

LOCAL(void)
read_and_discard_scanlines(j_decompress_ptr cinfo, JDIMENSION num_lines)
{
  JDIMENSION n;
  my_master_ptr master = (my_master_ptr)cinfo->master;
  JSAMPLE dummy_sample[1] = { 0 };
  JSAMPROW dummy_row = dummy_sample;
  JSAMPARRAY scanlines = NULL;
  void (*color_convert) (j_decompress_ptr cinfo, JSAMPIMAGE input_buf,
                         JDIMENSION input_row, JSAMPARRAY output_buf,
                         int num_rows) = NULL;
  void (*color_quantize) (j_decompress_ptr cinfo, JSAMPARRAY input_buf,
                          JSAMPARRAY output_buf, int num_rows) = NULL;

  if (cinfo->cconvert && cinfo->cconvert->color_convert) {
    color_convert = cinfo->cconvert->color_convert;
    cinfo->cconvert->color_convert = noop_convert;
    /* This just prevents UBSan from complaining about adding 0 to a NULL
     * pointer.  The pointer isn't actually used.
     */
    scanlines = &dummy_row;
  }

  if (cinfo->cquantize && cinfo->cquantize->color_quantize) {
    color_quantize = cinfo->cquantize->color_quantize;
    cinfo->cquantize->color_quantize = noop_quantize;
  }

  if (master->using_merged_upsample && cinfo->max_v_samp_factor == 2) {
    my_merged_upsample_ptr upsample = (my_merged_upsample_ptr)cinfo->upsample;
    scanlines = &upsample->spare_row;
  }

  for (n = 0; n < num_lines; n++)
    jpeg_read_scanlines(cinfo, scanlines, 1);

  if (color_convert)
    cinfo->cconvert->color_convert = color_convert;

  if (color_quantize)
    cinfo->cquantize->color_quantize = color_quantize;
}


/*
 * Called by jpeg_skip_scanlines().  This partially skips a decompress block by
 * incrementing the rowgroup counter.
 */

LOCAL(void)
increment_simple_rowgroup_ctr(j_decompress_ptr cinfo, JDIMENSION rows)
{
  JDIMENSION rows_left;
  my_main_ptr main_ptr = (my_main_ptr)cinfo->main;
  my_master_ptr master = (my_master_ptr)cinfo->master;

  if (master->using_merged_upsample && cinfo->max_v_samp_factor == 2) {
    read_and_discard_scanlines(cinfo, rows);
    return;
  }

  /* Increment the counter to the next row group after the skipped rows. */
  main_ptr->rowgroup_ctr += rows / cinfo->max_v_samp_factor;

  /* Partially skipping a row group would involve modifying the internal state
   * of the upsampler, so read the remaining rows into a dummy buffer instead.
   */
  rows_left = rows % cinfo->max_v_samp_factor;
  cinfo->output_scanline += rows - rows_left;

  read_and_discard_scanlines(cinfo, rows_left);
}

/*
 * Skips some scanlines of data from the JPEG decompressor.
 *
 * The return value will be the number of lines actually skipped.  If skipping
 * num_lines would move beyond the end of the image, then the actual number of
 * lines remaining in the image is returned.  Otherwise, the return value will
 * be equal to num_lines.
 *
 * Refer to libjpeg.txt for more information.
 */

GLOBAL(JDIMENSION)
jpeg_skip_scanlines(j_decompress_ptr cinfo, JDIMENSION num_lines)
{
  my_main_ptr main_ptr = (my_main_ptr)cinfo->main;
  my_coef_ptr coef = (my_coef_ptr)cinfo->coef;
  my_master_ptr master = (my_master_ptr)cinfo->master;
  my_upsample_ptr upsample = (my_upsample_ptr)cinfo->upsample;
  JDIMENSION i, x;
  int y;
  JDIMENSION lines_per_iMCU_row, lines_left_in_iMCU_row, lines_after_iMCU_row;
  JDIMENSION lines_to_skip, lines_to_read;

  /* Two-pass color quantization is not supported. */
  if (cinfo->quantize_colors && cinfo->two_pass_quantize)
    ERREXIT(cinfo, JERR_NOTIMPL);

  if (cinfo->global_state != DSTATE_SCANNING)
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);

  /* Do not skip past the bottom of the image. */
  if (cinfo->output_scanline + num_lines >= cinfo->output_height) {
    num_lines = cinfo->output_height - cinfo->output_scanline;
    cinfo->output_scanline = cinfo->output_height;
    (*cinfo->inputctl->finish_input_pass) (cinfo);
    cinfo->inputctl->eoi_reached = TRUE;
    return num_lines;
  }

  if (num_lines == 0)
    return 0;

  lines_per_iMCU_row = cinfo->_min_DCT_scaled_size * cinfo->max_v_samp_factor;
  lines_left_in_iMCU_row =
    (lines_per_iMCU_row - (cinfo->output_scanline % lines_per_iMCU_row)) %
    lines_per_iMCU_row;
  lines_after_iMCU_row = num_lines - lines_left_in_iMCU_row;

  /* Skip the lines remaining in the current iMCU row.  When upsampling
   * requires context rows, we need the previous and next rows in order to read
   * the current row.  This adds some complexity.
   */
  if (cinfo->upsample->need_context_rows) {
    /* If the skipped lines would not move us past the current iMCU row, we
     * read the lines and ignore them.  There might be a faster way of doing
     * this, but we are facing increasing complexity for diminishing returns.
     * The increasing complexity would be a by-product of meddling with the
     * state machine used to skip context rows.  Near the end of an iMCU row,
     * the next iMCU row may have already been entropy-decoded.  In this unique
     * case, we will read the next iMCU row if we cannot skip past it as well.
     */
    if ((num_lines < lines_left_in_iMCU_row + 1) ||
        (lines_left_in_iMCU_row <= 1 && main_ptr->buffer_full &&
         lines_after_iMCU_row < lines_per_iMCU_row + 1)) {
      read_and_discard_scanlines(cinfo, num_lines);
      return num_lines;
    }

    /* If the next iMCU row has already been entropy-decoded, make sure that
     * we do not skip too far.
     */
    if (lines_left_in_iMCU_row <= 1 && main_ptr->buffer_full) {
      cinfo->output_scanline += lines_left_in_iMCU_row + lines_per_iMCU_row;
      lines_after_iMCU_row -= lines_per_iMCU_row;
    } else {
      cinfo->output_scanline += lines_left_in_iMCU_row;
    }

    /* If we have just completed the first block, adjust the buffer pointers */
    if (main_ptr->iMCU_row_ctr == 0 ||
        (main_ptr->iMCU_row_ctr == 1 && lines_left_in_iMCU_row > 2))
      set_wraparound_pointers(cinfo);
    main_ptr->buffer_full = FALSE;
    main_ptr->rowgroup_ctr = 0;
    main_ptr->context_state = CTX_PREPARE_FOR_IMCU;
    if (!master->using_merged_upsample) {
      upsample->next_row_out = cinfo->max_v_samp_factor;
      upsample->rows_to_go = cinfo->output_height - cinfo->output_scanline;
    }
  }

  /* Skipping is much simpler when context rows are not required. */
  else {
    if (num_lines < lines_left_in_iMCU_row) {
      increment_simple_rowgroup_ctr(cinfo, num_lines);
      return num_lines;
    } else {
      cinfo->output_scanline += lines_left_in_iMCU_row;
      main_ptr->buffer_full = FALSE;
      main_ptr->rowgroup_ctr = 0;
      if (!master->using_merged_upsample) {
        upsample->next_row_out = cinfo->max_v_samp_factor;
        upsample->rows_to_go = cinfo->output_height - cinfo->output_scanline;
      }
    }
  }

  /* Calculate how many full iMCU rows we can skip. */
  if (cinfo->upsample->need_context_rows)
    lines_to_skip = ((lines_after_iMCU_row - 1) / lines_per_iMCU_row) *
                    lines_per_iMCU_row;
  else
    lines_to_skip = (lines_after_iMCU_row / lines_per_iMCU_row) *
                    lines_per_iMCU_row;
  /* Calculate the number of lines that remain to be skipped after skipping all
   * of the full iMCU rows that we can.  We will not read these lines unless we
   * have to.
   */
  lines_to_read = lines_after_iMCU_row - lines_to_skip;

  /* For images requiring multiple scans (progressive, non-interleaved, etc.),
   * all of the entropy decoding occurs in jpeg_start_decompress(), assuming
   * that the input data source is non-suspending.  This makes skipping easy.
   */
  if (cinfo->inputctl->has_multiple_scans) {
    if (cinfo->upsample->need_context_rows) {
      cinfo->output_scanline += lines_to_skip;
      cinfo->output_iMCU_row += lines_to_skip / lines_per_iMCU_row;
      main_ptr->iMCU_row_ctr += lines_to_skip / lines_per_iMCU_row;
      /* It is complex to properly move to the middle of a context block, so
       * read the remaining lines instead of skipping them.
       */
      read_and_discard_scanlines(cinfo, lines_to_read);
    } else {
      cinfo->output_scanline += lines_to_skip;
      cinfo->output_iMCU_row += lines_to_skip / lines_per_iMCU_row;
      increment_simple_rowgroup_ctr(cinfo, lines_to_read);
    }
    if (!master->using_merged_upsample)
      upsample->rows_to_go = cinfo->output_height - cinfo->output_scanline;
    return num_lines;
  }

  /* Skip the iMCU rows that we can safely skip. */
  for (i = 0; i < lines_to_skip; i += lines_per_iMCU_row) {
    for (y = 0; y < coef->MCU_rows_per_iMCU_row; y++) {
      for (x = 0; x < cinfo->MCUs_per_row; x++) {
        /* Calling decode_mcu() with a NULL pointer causes it to discard the
         * decoded coefficients.  This is ~5% faster for large subsets, but
         * it's tough to tell a difference for smaller images.
         */
        if (!cinfo->entropy->insufficient_data)
          cinfo->master->last_good_iMCU_row = cinfo->input_iMCU_row;
        (*cinfo->entropy->decode_mcu) (cinfo, NULL);
      }
    }
    cinfo->input_iMCU_row++;
    cinfo->output_iMCU_row++;
    if (cinfo->input_iMCU_row < cinfo->total_iMCU_rows)
      start_iMCU_row(cinfo);
    else
      (*cinfo->inputctl->finish_input_pass) (cinfo);
  }
  cinfo->output_scanline += lines_to_skip;

  if (cinfo->upsample->need_context_rows) {
    /* Context-based upsampling keeps track of iMCU rows. */
    main_ptr->iMCU_row_ctr += lines_to_skip / lines_per_iMCU_row;

    /* It is complex to properly move to the middle of a context block, so
     * read the remaining lines instead of skipping them.
     */
    read_and_discard_scanlines(cinfo, lines_to_read);
  } else {
    increment_simple_rowgroup_ctr(cinfo, lines_to_read);
  }

  /* Since skipping lines involves skipping the upsampling step, the value of
   * "rows_to_go" will become invalid unless we set it here.  NOTE: This is a
   * bit odd, since "rows_to_go" seems to be redundantly keeping track of
   * output_scanline.
   */
  if (!master->using_merged_upsample)
    upsample->rows_to_go = cinfo->output_height - cinfo->output_scanline;

  /* Always skip the requested number of lines. */
  return num_lines;
}

/*
 * Alternate entry point to read raw data.
 * Processes exactly one iMCU row per call, unless suspended.
 */

#ifdef WITH_VC8000
static JDIMENSION
vc8000_read_raw_data(j_decompress_ptr cinfo, JSAMPIMAGE data,
                   JDIMENSION max_lines)
{
  if((cinfo->master->i32PixelFormat != V4L2_PIX_FMT_YUYV) && (cinfo->master->i32PixelFormat != V4L2_PIX_FMT_NV12))
    return 0;

  int i, j;  

  if((max_lines + cinfo->output_scanline) > cinfo->output_height)
    max_lines = cinfo->output_height - cinfo->output_scanline;

  JSAMPROW *y_comp = data[0];
  JSAMPROW *u_comp = data[1];
  JSAMPROW *v_comp = data[2];
  
  uint32_t y_pos = 0;
  uint32_t u_pos = 0;
  uint32_t v_pos = 0;

  unsigned char *pu8YComp;
  unsigned char *pu8UComp;
  unsigned char *pu8VComp;


  if(cinfo->master->i32PixelFormat == V4L2_PIX_FMT_YUYV)
  {
    unsigned char *pu8DecodedSrc;
    unsigned int u32YUYVPixelSize = 2;
    unsigned int u32RowBytes = cinfo->master->u32DecodeImageWidth * u32YUYVPixelSize;

    for(j = 0; j < max_lines; j  ++)
    {
      pu8DecodedSrc = cinfo->master->pu8DecodedBuf + ((cinfo->output_scanline + j) * u32RowBytes);
      pu8DecodedSrc += cinfo->master->first_iMCU_col * u32YUYVPixelSize;

	  pu8YComp = y_comp[j];
	  pu8UComp = u_comp[j];
	  pu8VComp = v_comp[j];
	  y_pos = 0;
	  v_pos = 0;
	  u_pos = 0;

      for( i = 0; i < cinfo->output_width; i ++)
      {
		pu8YComp[y_pos] = pu8DecodedSrc[i * u32YUYVPixelSize];
		y_pos ++;
		
		if(i & 0x1)
		{
		  pu8UComp[v_pos] = pu8DecodedSrc[(i * u32YUYVPixelSize) + 1];
          v_pos ++;
        }
        else
        {
		  pu8VComp[u_pos] = pu8DecodedSrc[(i * u32YUYVPixelSize) + 1];
          u_pos ++;
		}
	  }
	}
  }

  if(cinfo->master->i32PixelFormat == V4L2_PIX_FMT_NV12)
  {
    unsigned char *pu8DecodedYSrc;
    unsigned char *pu8DecodedUVSrc;
    unsigned char *pu8DecodedSrc;
    unsigned int u32NV12PixelSize = 1;
    unsigned int u32RowBytes = cinfo->master->u32DecodeImageWidth * u32NV12PixelSize;

    //copy Y component first
	pu8DecodedYSrc = cinfo->master->pu8DecodedBuf;
    for(j = 0; j < max_lines; j ++)
    {
	  pu8YComp = y_comp[j];
	  pu8DecodedSrc = pu8DecodedYSrc + ((cinfo->output_scanline + j)* u32RowBytes);
	  pu8DecodedSrc += cinfo->master->first_iMCU_col * u32NV12PixelSize;
      memcpy(pu8YComp, pu8DecodedSrc, cinfo->output_width * u32NV12PixelSize);
    }

    //copy UV component
	pu8DecodedUVSrc = pu8DecodedYSrc + (cinfo->master->u32DecodeImageWidth * cinfo->master->u32DecodeImageHeight * u32NV12PixelSize);
    u32NV12PixelSize = 2;
	u32RowBytes = cinfo->master->u32DecodeImageWidth / 2 * u32NV12PixelSize;
    for(j = 0; j < (max_lines / 2); j  ++)
    {
      pu8DecodedSrc = pu8DecodedUVSrc + (((cinfo->output_scanline / 2) + j) * u32RowBytes);
      pu8DecodedSrc += (cinfo->master->first_iMCU_col / 2) * u32NV12PixelSize;

	  pu8UComp = u_comp[j];
	  pu8VComp = v_comp[j];
	  u_pos = 0;
	  v_pos = 0;
	  
      for( i = 0; i < (cinfo->output_width / 2); i ++)
      {
        pu8UComp[u_pos] = pu8DecodedSrc[(i * u32NV12PixelSize)];
        u_pos ++;
        pu8VComp[v_pos] = pu8DecodedSrc[(i * u32NV12PixelSize) + 1];
        v_pos ++;
      }
    }
  }

  return max_lines;
}

#endif


GLOBAL(JDIMENSION)
jpeg_read_raw_data(j_decompress_ptr cinfo, JSAMPIMAGE data,
                   JDIMENSION max_lines)
{
  JDIMENSION lines_per_iMCU_row;

  if (cinfo->global_state != DSTATE_RAW_OK)
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  if (cinfo->output_scanline >= cinfo->output_height) {
    WARNMS(cinfo, JWRN_TOO_MUCH_DATA);
    return 0;
  }

#ifdef WITH_VC8000
  if(cinfo->master->bHWJpegDecodeDone)
  {
	lines_per_iMCU_row = vc8000_read_raw_data(cinfo, data, max_lines);
	if(lines_per_iMCU_row > 0){
      cinfo->output_scanline += lines_per_iMCU_row;
	  return lines_per_iMCU_row;
	}
  }

#endif

  /* Call progress monitor hook if present */
  if (cinfo->progress != NULL) {
    cinfo->progress->pass_counter = (long)cinfo->output_scanline;
    cinfo->progress->pass_limit = (long)cinfo->output_height;
    (*cinfo->progress->progress_monitor) ((j_common_ptr)cinfo);
  }

  /* Verify that at least one iMCU row can be returned. */
  lines_per_iMCU_row = cinfo->max_v_samp_factor * cinfo->_min_DCT_scaled_size;
  if (max_lines < lines_per_iMCU_row)
    ERREXIT(cinfo, JERR_BUFFER_SIZE);

  /* Decompress directly into user's buffer. */
  if (!(*cinfo->coef->decompress_data) (cinfo, data))
    return 0;                   /* suspension forced, can do nothing more */

  /* OK, we processed one iMCU row. */
  cinfo->output_scanline += lines_per_iMCU_row;
  return lines_per_iMCU_row;
}


/* Additional entry points for buffered-image mode. */

#ifdef D_MULTISCAN_FILES_SUPPORTED

/*
 * Initialize for an output pass in buffered-image mode.
 */

GLOBAL(boolean)
jpeg_start_output(j_decompress_ptr cinfo, int scan_number)
{
  if (cinfo->global_state != DSTATE_BUFIMAGE &&
      cinfo->global_state != DSTATE_PRESCAN)
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  /* Limit scan number to valid range */
  if (scan_number <= 0)
    scan_number = 1;
  if (cinfo->inputctl->eoi_reached && scan_number > cinfo->input_scan_number)
    scan_number = cinfo->input_scan_number;
  cinfo->output_scan_number = scan_number;
  /* Perform any dummy output passes, and set up for the real pass */
  return output_pass_setup(cinfo);
}


/*
 * Finish up after an output pass in buffered-image mode.
 *
 * Returns FALSE if suspended.  The return value need be inspected only if
 * a suspending data source is used.
 */

GLOBAL(boolean)
jpeg_finish_output(j_decompress_ptr cinfo)
{
  if ((cinfo->global_state == DSTATE_SCANNING ||
       cinfo->global_state == DSTATE_RAW_OK) && cinfo->buffered_image) {
    /* Terminate this pass. */
    /* We do not require the whole pass to have been completed. */
    (*cinfo->master->finish_output_pass) (cinfo);
    cinfo->global_state = DSTATE_BUFPOST;
  } else if (cinfo->global_state != DSTATE_BUFPOST) {
    /* BUFPOST = repeat call after a suspension, anything else is error */
    ERREXIT1(cinfo, JERR_BAD_STATE, cinfo->global_state);
  }
  /* Read markers looking for SOS or EOI */
  while (cinfo->input_scan_number <= cinfo->output_scan_number &&
         !cinfo->inputctl->eoi_reached) {
    if ((*cinfo->inputctl->consume_input) (cinfo) == JPEG_SUSPENDED)
      return FALSE;             /* Suspend, come back later */
  }
  cinfo->global_state = DSTATE_BUFIMAGE;
  return TRUE;
}

#endif /* D_MULTISCAN_FILES_SUPPORTED */
