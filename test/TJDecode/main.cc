#include <cstdio>
#include <iostream>
#include <fstream>

#include <string.h>
#include "turbojpeg.h"

//#define _DO_TRNSFORM_

using namespace std;

#define THROW(action, message) { \
  printf("ERROR in line %d while %s:\n%s\n", __LINE__, action, message); \
  retval = -1;  goto bailout; \
}

#define THROW_TJ(action)  THROW(action, tjGetErrorStr2(tjInstance))
#define THROW_UNIX(action)  THROW(action, strerror(errno))

const char *subsampName[TJ_NUMSAMP] = {
  "4:4:4", "4:2:2", "4:2:0", "Grayscale", "4:4:0", "4:1:1"
};

const char *colorspaceName[TJ_NUMCS] = {
  "RGB", "YCbCr", "GRAY", "CMYK", "YCCK"
};

const char *pixelformatName[TJ_NUMPF] = {
  "RGB",
  "BGR",
  "RGBX",
  "BGRX",
  "XBGR",
  "XRGB",
  "GRAY",
  "RGBA",
  "BGRA",
  "ABGR",
  "ARGB",
  "CMYK",
};

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
	int retval = 0;
	tjscalingfactor *scalingFactors = NULL;
	int numScalingFactors = 0;
	tjhandle tjInstance = NULL;
	tjscalingfactor scalingFactor = { 1, 1 };
	int i;
	FILE *jpegFile = NULL;
	FILE *imgFile = NULL;
	unsigned long jpegSize;
	unsigned long imgSize;
	unsigned char *imgBuf = NULL, *jpegBuf = NULL;
	long fileSize;  
	int width, height;
	int inSubsamp, inColorspace;
	int pixelFormat = TJPF_UNKNOWN;
	int flags = 0;
	char imgFileName[50];
	int scaleIndex = 8;
	double startTime, endTime;
	tjtransform xform;

#ifdef _DO_TRNSFORM_
	int doTransform = 1;
#else
	int doTransform = 0;
#endif	

	memset(&xform, 0, sizeof(tjtransform));

	if ((scalingFactors = tjGetScalingFactors(&numScalingFactors)) == NULL)
		THROW_TJ("getting scaling factors");

	for(i = 0; i < numScalingFactors; i ++)
		 printf("supported scaling factors index %d: %d/%d \n", i, scalingFactors[i].num, scalingFactors[i].denom);

	if (argc >= 3)
		scaleIndex = atoi(argv[2]);

	if(scaleIndex >= numScalingFactors)
		scaleIndex = numScalingFactors - 1;
	
	scalingFactor = scalingFactors[scaleIndex];
	printf("using scaling factors %d/%d \n", scalingFactor.num, scalingFactor.denom);

    /* Read the JPEG file into memory. */
	if ((jpegFile = fopen(argv[1], "rb")) == NULL)
		THROW_UNIX("opening input file");
	if (fseek(jpegFile, 0, SEEK_END) < 0 || ((jpegSize = ftell(jpegFile)) < 0) ||
        fseek(jpegFile, 0, SEEK_SET) < 0)
		THROW_UNIX("determining input file size");
    if (jpegSize == 0)
		THROW("determining input file size", "Input file contains no data");
    if ((jpegBuf = (unsigned char *)tjAlloc(jpegSize)) == NULL)
		THROW_UNIX("allocating JPEG buffer");
    if (fread(jpegBuf, jpegSize, 1, jpegFile) < 1)
		THROW_UNIX("reading input file");
    fclose(jpegFile);
    jpegFile = NULL;

	/* init decompress engine*/
	if (!doTransform) {
		if ((tjInstance = tjInitDecompress()) == NULL)
			THROW_TJ("initializing decompressor");		
	}
	else {
		//transform operation test
//		  xform.op = TJXOP_HFLIP;
//		  xform.op = TJXOP_VFLIP;
//		  xform.op = TJXOP_TRANSPOSE;
//		  xform.op = TJXOP_TRANSVERSE;
		  xform.op = TJXOP_ROT90;
//		  xform.op = TJXOP_ROT180;

		/* Transform it. */
		unsigned char *dstBuf = NULL;  /* Dynamically allocate the JPEG buffer */
		unsigned long dstSize = 0;

		if ((tjInstance = tjInitTransform()) == NULL)
			THROW_TJ("initializing transformer");
		xform.options |= TJXOPT_TRIM;
		if (tjTransform(tjInstance, jpegBuf, jpegSize, 1, &dstBuf, &dstSize,
					  &xform, flags) < 0) {
			tjFree(dstBuf);
			THROW_TJ("transforming input image");
		}
		tjFree(jpegBuf);
		jpegBuf = dstBuf;
		jpegSize = dstSize;
	}

    if (tjDecompressHeader3(tjInstance, jpegBuf, jpegSize, &width, &height,
                            &inSubsamp, &inColorspace) < 0)
		THROW_TJ("reading JPEG header");

    printf("%s Image:  %d x %d pixels, %s subsampling, %s colorspace\n",
           (doTransform ? "Transformed" : "Input"), width, height,
           subsampName[inSubsamp], colorspaceName[inColorspace]);

    width = TJSCALED(width, scalingFactor);
    height = TJSCALED(height, scalingFactor);

	/* create image file */
   pixelFormat = TJPF_BGRA;
//    pixelFormat = TJPF_ARGB;
//    pixelFormat = TJPF_RGB;
//    pixelFormat = TJPF_BGR;
	sprintf(imgFileName, "Decompress_%s_%d_%d.bin", pixelformatName[pixelFormat], width, height);

	if ((imgFile = fopen(imgFileName, "w")) == NULL)
		THROW_UNIX("cretae output file");

	/* allocate image buffer */
	imgSize = width * height * tjPixelSize[pixelFormat];

    if ((imgBuf = (unsigned char *)tjAlloc(imgSize)) == NULL)
		THROW_UNIX("allocating uncompressed image buffer");

	startTime = getTimeSec();
	
    if (tjDecompress2(tjInstance, jpegBuf, jpegSize, imgBuf, width, 0, height,
                      pixelFormat, flags) < 0)
		THROW_TJ("decompressing JPEG image");

	endTime = getTimeSec();
	fwrite(imgBuf, imgSize, 1, imgFile);
	fclose(imgFile);

    printf("Decompress image to %s, width %d, height %d, time %f sec\n", pixelformatName[pixelFormat], width, height, endTime - startTime); 

	tjFree(jpegBuf);
	jpegBuf = NULL;
	tjDestroy(tjInstance);
	tjInstance = NULL;

bailout:

	tjFree(jpegBuf);
	if (jpegFile) fclose(jpegFile);

	return retval;
}
