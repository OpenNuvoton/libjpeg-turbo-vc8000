#include <cstdio>
#include <iostream>
#include <cstring>

#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/fb.h>

#include "jpeglib_ext.h"

#define FB_DEV_NAME "/dev/fb0"
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

using namespace std;

static uint32_t s_u32FrameBufSize = 0;
static uint8_t *s_pu8FrameBufAddr = NULL;
static int s_fb_fd = 0;

//Open /dev/fb0 aka ultrafb
static int openFBDev(struct fb_var_screeninfo *psFBVar)
{
	int err = 0;
	
	s_fb_fd = open(FB_DEV_NAME, O_RDWR, 0);
	if (s_fb_fd < 0) {
		cerr << "Failed to open fb device: " << FB_DEV_NAME << endl;
		return -1;
	}

	err = ioctl(s_fb_fd, FBIOGET_VSCREENINFO, psFBVar);
	if (err < 0) {
	    cerr << "FBIOGET_VSCREENINFO failed!" << endl;
		return err;
	}

	s_u32FrameBufSize = psFBVar->xres * psFBVar->yres * 4 *2;	//4:argb8888, 2: two planes
	
	s_pu8FrameBufAddr = (uint8_t *)mmap(NULL, s_u32FrameBufSize, PROT_READ|PROT_WRITE, MAP_SHARED, s_fb_fd, 0);
	if (s_pu8FrameBufAddr == MAP_FAILED) {
		cerr << "mmap() failed" << endl;
		err = -2;
		return err;
	}
	return 0;
}

static void closeFBDev(void)
{
	if(s_fb_fd > 0)
	{
		if(s_pu8FrameBufAddr)
		{
			munmap(s_pu8FrameBufAddr, s_u32FrameBufSize);
			s_pu8FrameBufAddr = NULL;
		}

		close(s_fb_fd);
	}
}

static int decodeTo(
	uint8_t *jpegBuf,
	uint32_t jpegSize,
	struct fb_var_screeninfo *psFBVar,
	uint32_t u32OuputImgWidth,
	uint32_t u32OuputImgHeight,
	uint32_t u32OuputImgPosX,
	uint32_t u32OuputImgPosY,
	JXFORM_CODE xfrom	
)
{
	jpeg_decompress_struct dinfo;     
	jpeg_error_mgr eMgr;     
	uint32_t u32DecodeSrcWidth;
	uint32_t u32DecodeSrcHeight;

	dinfo.err = jpeg_std_error(&eMgr);
	jpeg_CreateDecompress_Ext(&dinfo, JPEG_LIB_VERSION, (size_t)sizeof(struct jpeg_decompress_struct), TRUE); 

	jpeg_mem_src(&dinfo, jpegBuf, jpegSize);
	
	if(jpeg_fb_dest(&dinfo, 
					psFBVar->xres, 
					psFBVar->yres, 
					u32OuputImgWidth, 
					u32OuputImgHeight, 
					u32OuputImgPosX, 
					u32OuputImgPosY, xfrom) != 0)
	{
		cout << "set frame buffer destination failed" << endl;
		return -1;
	}

	jpeg_read_header(&dinfo, 1);

	cout << "JPEG source image width" <<  dinfo.image_width << endl;
	cout << "JPEG source image height" <<  dinfo.image_height << endl;

	if((xfrom == JXFORM_ROT_90) || (xfrom == JXFORM_ROT_270))
	{
		u32DecodeSrcWidth = dinfo.image_height;
		u32DecodeSrcHeight = dinfo.image_width;		
	}
	else
	{
		u32DecodeSrcWidth = dinfo.image_width;
		u32DecodeSrcHeight = dinfo.image_height;		
	}

	//Align to VC8000 MCU dimension (16x16)
  	u32DecodeSrcWidth = DIV_ROUND_UP(u32DecodeSrcWidth, 16)*16;
	u32DecodeSrcHeight = DIV_ROUND_UP(u32DecodeSrcHeight, 16)*16;

	if(((u32DecodeSrcWidth > u32OuputImgWidth) && (u32DecodeSrcHeight < u32OuputImgHeight)) ||
		((u32DecodeSrcWidth < u32OuputImgWidth) && (u32DecodeSrcHeight > u32OuputImgHeight)))
	{
		cout << "Image scale up or down ???" << endl;
		jpeg_destroy_decompress(&dinfo);
		return -2;
	}

	dinfo.out_color_space = JCS_EXT_BGRA;
	jpeg_start_decompress(&dinfo);  

	dinfo.output_scanline = dinfo.output_height; //if output_scanline != output_height, jpeg_finish_decompress will abnormal exit program.
	jpeg_finish_decompress(&dinfo); 
	jpeg_destroy_decompress(&dinfo);
	return 0;
}

#include <stdlib.h>
#include <sys/time.h>

static double getTimeSec(void)
{
  struct timeval tv;

  if (gettimeofday(&tv, NULL) < 0)
	return 0.0;
  else 
	return (double)(tv.tv_sec ) + ((double)(tv.tv_usec / 1000000.));
}

int main(int argc, char* argv[]) {

	FILE *jpegFile = NULL;
	uint32_t jpegSize;
	uint8_t *jpegBuf = NULL;
	struct fb_var_screeninfo sFBVar;
	int testCnt = 5;
	uint32_t u32OuputImgWidth;
	uint32_t u32OuputImgHeight;
	uint32_t u32OuputImgPosX;
	uint32_t u32OuputImgPosY;	
	JXFORM_CODE xfrom;
	double startTime, endTime;

    /* Read the JPEG file into memory. */
	if ((jpegFile = fopen(argv[1], "rb")) == NULL)
	{
		cerr << "opening input file" << endl;
		return -1;
	}

	if (fseek(jpegFile, 0, SEEK_END) < 0 || ((jpegSize = ftell(jpegFile)) < 0) ||
        fseek(jpegFile, 0, SEEK_SET) < 0)
    {
		cerr << "determining input file size" << endl;
		return -2;
	}

    if (jpegSize == 0)
    {
		cerr << "determining input file size" << "Input file contains no data" << endl;
		return -3;
	}

    if ((jpegBuf = (uint8_t *)malloc(jpegSize)) == NULL)
	{
		cerr << "allocating JPEG buffer" << endl;
		return -4;
	}

    if (fread(jpegBuf, jpegSize, 1, jpegFile) < 1)
    {
		cerr << "reading input file" << endl;
		return -5;
	}

    fclose(jpegFile);
    jpegFile = NULL;

	//open ultrafb
	if(openFBDev(&sFBVar) != 0)
	{
		cerr << "unable open ultrafb" << endl;
		goto prog_out;
	} 
	
	while(testCnt)
	{
		if(testCnt == 5)
		{
			//show image to left-top
			u32OuputImgWidth = sFBVar.xres / 2;
			u32OuputImgHeight = sFBVar.yres / 2;
			u32OuputImgPosX = 0;
			u32OuputImgPosY = 0;
			xfrom = JXFORM_NONE;

		}
		else if(testCnt == 4)
		{
			//show image to right-top
			u32OuputImgWidth = sFBVar.yres / 4;
			u32OuputImgHeight = sFBVar.xres / 4;
			u32OuputImgPosX = sFBVar.xres / 2;
			u32OuputImgPosY = 0;
			xfrom = JXFORM_ROT_90;
			

		}
		else if(testCnt == 3)
		{
			//show image to left-bottom
			u32OuputImgWidth = sFBVar.xres / 2;
			u32OuputImgHeight = sFBVar.yres / 2;
			u32OuputImgPosX = 0;
			u32OuputImgPosY = u32OuputImgHeight;
			xfrom = JXFORM_ROT_180;
			

		}
		else if(testCnt == 2)
		{
			//show image to right-bottom
			u32OuputImgWidth = sFBVar.yres / 4;
			u32OuputImgHeight = sFBVar.xres / 4;
			u32OuputImgPosX = sFBVar.xres / 2;
			u32OuputImgPosY = sFBVar.yres / 2;
			xfrom = JXFORM_ROT_270;
			
		}
		else if(testCnt == 1)
		{
			//show image to full screen
			u32OuputImgWidth = sFBVar.xres;
			u32OuputImgHeight = sFBVar.yres;
			u32OuputImgPosX = 0;
			u32OuputImgPosY = 0;
			xfrom = JXFORM_NONE;
		}

		memset(s_pu8FrameBufAddr, 0, s_u32FrameBufSize); //clean frame buffer

		startTime = getTimeSec();
		decodeTo(jpegBuf,
				jpegSize,
				&sFBVar,
				u32OuputImgWidth,
				u32OuputImgHeight,
				u32OuputImgPosX,
				u32OuputImgPosY,
				xfrom);
		endTime = getTimeSec();

		cout << "Decompress image to width " << u32OuputImgWidth  << ",height " << u32OuputImgHeight << ",time " << (endTime - startTime) << "sec" << endl; 

		testCnt --;
		sleep(5);
	}

prog_out:
	closeFBDev();
	free(jpegBuf);

	return 0;
}

