#include <cstdio>
#include <iostream>
#include <fstream>

#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "turbojpeg.h"

using namespace std;

#define THROW(action, message) { \
  printf("ERROR in line %d while %s:\n%s\n", __LINE__, action, message); \
  retval = -1;  goto thread_out; \
}

#define THROW_TJ(action)  THROW(action, tjGetErrorStr2(tjInstance))
#define THROW_UNIX(action)  THROW(action, strerror(errno))

#define TEST_PATTERN_JPEG_1_FN "Pattern/test_1.jpg"
#define TEST_PATTERN_JPEG_2_FN "Pattern/test_2.jpg"
#define TEST_PATTERN_JPEG_3_FN "Pattern/test_3.jpg"

#define TEST_PATTERN_BIN_1_FN "Pattern/test_1.bin"
#define TEST_PATTERN_BIN_2_FN "Pattern/test_2.bin"
#define TEST_PATTERN_BIN_3_FN "Pattern/test_3.bin"

#define REPORT_DURATION_TIME 3
#define DECODE_MAX_CNT 500

#define MAX_THREAD_CNT 3


#include <sys/time.h>

static double getTimeSec(void)
{
  struct timeval tv;

  if (gettimeofday(&tv, NULL) < 0)
	return 0.0;
  else 
	return (double)(tv.tv_sec ) + ((double)(tv.tv_usec / 1000000.));
}

typedef struct
{
	int testPatten;
}S_THREAD_PARAM;


void *JpegDecodeTest(void *data)
{
	int retval;
	S_THREAD_PARAM *psThreadParam = (S_THREAD_PARAM *)data;
	int testPattern = psThreadParam->testPatten;
	FILE *jpegFile = NULL;
	FILE *binFile = NULL;
	unsigned long jpegSize;
	unsigned long binSize;
	unsigned long imgSize;
	tjhandle tjInstance = NULL;
	int width, height;
	int inSubsamp, inColorspace;
	int pixelFormat = TJPF_UNKNOWN;
	unsigned char *binBuf = NULL, *imgBuf = NULL, *jpegBuf = NULL;
	int flags = 0;
	unsigned long compareSuss = 0;
	unsigned long compareFail = 0;
	double reportTime;
	
	if(testPattern == 1)
	{
		pixelFormat = TJPF_ARGB;
		if ((jpegFile = fopen(TEST_PATTERN_JPEG_1_FN, "rb")) == NULL)
			THROW_UNIX("opening input file");

		if ((binFile = fopen(TEST_PATTERN_BIN_1_FN, "rb")) == NULL)
			THROW_UNIX("opening binary file");
	}
	else if(testPattern == 2)
	{
		pixelFormat = TJPF_BGR;
		if ((jpegFile = fopen(TEST_PATTERN_JPEG_2_FN, "rb")) == NULL)
			THROW_UNIX("opening input file");

		if ((binFile = fopen(TEST_PATTERN_BIN_2_FN, "rb")) == NULL)
			THROW_UNIX("opening binary file");
	}
	else
	{
		pixelFormat = TJPF_BGRA;
		if ((jpegFile = fopen(TEST_PATTERN_JPEG_3_FN, "rb")) == NULL)
			THROW_UNIX("opening input file");

		if ((binFile = fopen(TEST_PATTERN_BIN_3_FN, "rb")) == NULL)
			THROW_UNIX("opening binary file");
	}
	
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


	if (fseek(binFile, 0, SEEK_END) < 0 || ((binSize = ftell(binFile)) < 0) ||
        fseek(binFile, 0, SEEK_SET) < 0)
		THROW_UNIX("determining binary file size");
    if (binSize == 0)
		THROW("determining binary file size", "Binary file contains no data");
    if ((binBuf = (unsigned char *)tjAlloc(binSize)) == NULL)
		THROW_UNIX("allocating JPEG buffer");
    if (fread(binBuf, binSize, 1, binFile) < 1)
		THROW_UNIX("reading input file");
    fclose(binFile);
    binFile = NULL;

	reportTime = getTimeSec() + REPORT_DURATION_TIME;
	printf("TID %d: JPEG decode run test pattern %d \n", gettid(), testPattern);

	while(1)
	{
		if ((tjInstance = tjInitDecompress()) == NULL)
			THROW_TJ("initializing decompressor");		

		if (tjDecompressHeader3(tjInstance, jpegBuf, jpegSize, &width, &height,
								&inSubsamp, &inColorspace) < 0)
			THROW_TJ("reading JPEG header");

		/* allocate image buffer */
		imgSize = width * height * tjPixelSize[pixelFormat];

		if ((imgBuf = (unsigned char *)tjAlloc(imgSize)) == NULL)
			THROW_UNIX("allocating uncompressed image buffer");

		if(imgSize != binSize){
			printf("binary file size %d and image decode size %d is different \n", binSize, imgSize);
			tjDestroy(tjInstance);
			tjInstance = NULL;
			break;
		}

		memset(imgBuf, 0x0, imgSize);

		if (tjDecompress2(tjInstance, jpegBuf, jpegSize, imgBuf, width, 0, height,
						  pixelFormat, flags) < 0)
			THROW_TJ("decompressing JPEG image");

		//compare decoded data and test pattern binary file
		if(memcmp(imgBuf, binBuf, imgSize) == 0)
			compareSuss ++;
		else
			compareFail ++;
		
		if(getTimeSec() > reportTime)
		{
			printf("TID %d: JPEG decode success %d, fail %d \n", gettid(), compareSuss, compareFail);
			reportTime = getTimeSec() + REPORT_DURATION_TIME;
		}
		
		tjDestroy(tjInstance);
		tjInstance = NULL;

		tjFree(imgBuf);

		if((compareSuss + compareFail) > DECODE_MAX_CNT)
			break;
	}

	printf("TID %d: JPEG decode success %d, fail %d \n", gettid(), compareSuss, compareFail);
	
thread_out:
	tjFree(binBuf);
	tjFree(jpegBuf);

	return (void *)retval;
}

int main(int argc, char* argv[]) {
	int i;
	pthread_t threadID[MAX_THREAD_CNT];
	S_THREAD_PARAM threadParam[MAX_THREAD_CNT];
	int threadCnt = 1;

	for(i = 0; i < MAX_THREAD_CNT; i ++)
		threadID[i] = -1;

	if(argc >= 2)
		threadCnt = atoi(argv[1]);

	if(threadCnt > MAX_THREAD_CNT)
		threadCnt = MAX_THREAD_CNT;

	for(i = 0; i < threadCnt; i ++)
	{
		threadParam[i].testPatten = i + 1;
		pthread_create(&threadID[i], NULL, JpegDecodeTest, (void *)&threadParam[i]);
	}

	//wait threads working done
	for(i = 0; i < threadCnt; i ++)
	{
		if(threadID[i] != -1)
		{
			pthread_join(threadID[i], NULL);
		}
	}

	return 0;
}
