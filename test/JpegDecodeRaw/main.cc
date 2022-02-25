
#include <cstdio>
#include <iostream>
#include <fstream>

using namespace std;

#include "jpeglib.h"
#include "DecodeRaw.h"

//#define MEM_STREAM //for mem stream or file stream test

int main(int argc, char* argv[]) {

	CDecompressor sDecoder;
	
	if (argc != 2) {
			cerr << "JpegDecodeRaw <jpeg file> \n";
			return -1;
	}
	
	string strJpegFile = argv[1];
	string strJpegRawYFile = "Jpeg_Y.bin";
	string strJpegRawUFile = "Jpeg_U.bin";
	string strJpegRawVFile = "Jpeg_V.bin";
	
	cout << "Decode " << strJpegFile << endl;

#if defined (MEM_STREAM)
	ifstream jpegFile(strJpegFile, ios::binary);

	if (!jpegFile.is_open())
	{
		cerr << "Unable open jpeg file" << endl;
		return -2;
	}

	const auto begin = jpegFile.tellg();

	jpegFile.seekg (0, ios::end);

	const auto end = jpegFile.tellg();
	const auto fsize = (end-begin);

	jpegFile.seekg (0, ios::beg);

	unsigned char *fileBuffer = new unsigned char[fsize + 100];
	
	jpegFile.read((char *)fileBuffer, fsize);


	sDecoder.DecodeJpeg(fileBuffer, fsize, NULL);
#else
	FILE *jpegFile = NULL;
	
	jpegFile = fopen(strJpegFile.c_str(), "r");
	if(jpegFile == NULL)
	{
		cerr << "Unable open jpeg file" << endl;
		return -2;
	}

	cout << "Decode from file stream" << endl;
	sDecoder.DecodeJpeg(NULL, 0, jpegFile);

#endif

	unsigned char* pu8YData;
	unsigned char* pu8UData;
	unsigned char* pu8VData;

	int i32Width;
	int i32Height;

	sDecoder.GetYUVData(pu8YData, pu8UData, pu8VData, i32Height, i32Width);

	cout << "Jpeg file resolution width " <<  i32Width << endl;
	cout << "Jpeg file resolution height " <<  i32Height << endl;

	ofstream jpegRawYFile(strJpegRawYFile, ios::binary | ios_base::trunc);
	ofstream jpegRawUFile(strJpegRawUFile, ios::binary | ios_base::trunc);
	ofstream jpegRawVFile(strJpegRawVFile, ios::binary | ios_base::trunc);

	jpegRawYFile.write((char *)pu8YData, i32Width * i32Height);
	jpegRawUFile.write((char *)pu8UData, (i32Width * i32Height) / 4);
	jpegRawVFile.write((char *)pu8VData, (i32Width * i32Height) / 4);

	jpegRawYFile.close();
	jpegRawUFile.close();
	jpegRawVFile.close();
	
#if defined (MEM_STREAM)
	jpegFile.close();
	delete fileBuffer;
#else
	fclose(jpegFile);
#endif

	return 0;
}
