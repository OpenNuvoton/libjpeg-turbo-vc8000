#pragma warning(disable: 4786)
#include <vector>
typedef std::vector<unsigned char> buffer;

#define NEW_BUFFER(param, len)  if(NULL != param) \
{delete param;param = new uint8_t[len];}\
else{param = new uint8_t[len];}

#define DELETE_BUFFER(param)  if (NULL != param)\
{	delete param;	param = NULL;	}
			

class CDecompressor 
{
public:  
    
	CDecompressor()  : yuvbuf_(3) 
	{         
		info_.err = jpeg_std_error(&e_);
		jpeg_create_decompress(&info_); 
		
		m_pYbuffer = NULL;
		m_pUbuffer= NULL;
		m_pVbuffer = NULL;
		m_pDummyBuffer = NULL;
		
		for (int i = 0; i < 3; ++i)
		{
			yuvptr_[i] = NULL;
		}
		
	}   
	
	virtual ~CDecompressor()     
	{        
		jpeg_destroy_decompress(&info_); 

		DELETE_BUFFER(m_pYbuffer);
		DELETE_BUFFER(m_pUbuffer);
		DELETE_BUFFER(m_pVbuffer);
		DELETE_BUFFER(m_pDummyBuffer);			
	}   


	virtual void DecodeJpeg(unsigned char* pBuffer, int nSize, FILE *inFile)
	{
		int i;
		unsigned int lines_per_iMCU_row;

		if(inFile)
		  jpeg_stdio_src(&info_, inFile);
		else
		  jpeg_mem_src(&info_, pBuffer, nSize);   //// 指定圖片在記憶體的地址及大小

		jpeg_read_header(&info_, 1);
		cout << "jpeg state " << info_.global_state << endl;       

		cout << "DecodeJpeg image width" <<  info_.image_width << endl;
		cout << "DecodeJpeg image height" <<  info_.image_height << endl;

		lines_per_iMCU_row = info_.max_v_samp_factor * info_.min_DCT_scaled_size;
		info_.raw_data_out = 1;          
		jpeg_start_decompress(&info_);  

		cout << "DecodeJpeg output width" <<  info_.output_width << endl;
		cout << "DecodeJpeg output height" <<  info_.output_height << endl;

		int maxScanlines = info_.output_height;
		
		if(info_.output_height % lines_per_iMCU_row){
			maxScanlines = ((info_.output_height / lines_per_iMCU_row) + 1) * lines_per_iMCU_row;
		}

		cout << "DecodeJpeg max scanlines " << maxScanlines << "lines per iMCU " << lines_per_iMCU_row << endl;
		
		for (i = 0; i < 3; ++i)
		{
			yuvbuf_[i].resize(maxScanlines); //Change by CHChen59 
			yuvptr_[i] = &yuvbuf_[i][0]; 		
		}

		int nLen = info_.output_width * info_.output_height;

		NEW_BUFFER(m_pYbuffer, nLen);
		NEW_BUFFER(m_pUbuffer, nLen);
		NEW_BUFFER(m_pVbuffer, nLen);
		NEW_BUFFER(m_pDummyBuffer, info_.output_width);

		unsigned char* row = m_pYbuffer;
		for ( i = 0; i < maxScanlines; i++, row += info_.output_width)         
		{            
			if(i >= info_.output_height)
				yuvptr_[0][i] = m_pDummyBuffer;
			else
				yuvptr_[0][i] = row;      //y 分量空間初始化   
		} 
        
		row = m_pUbuffer;	
		for (i = 0; i < maxScanlines; i += 2, row += info_.output_width / 2)         
		{            
			if(i >= info_.output_height)
				yuvptr_[1][i / 2] = m_pDummyBuffer;
			else
				yuvptr_[1][i / 2] = row;      //u 分量初始化
			
		} 
		
		row = m_pVbuffer;  
		for ( i = 0; i < maxScanlines; i += 2, row += info_.output_width / 2)        
		{            
			if(i >= info_.output_height)
				yuvptr_[2][i / 2] = m_pDummyBuffer;
			else
				yuvptr_[2][i / 2] = row;         
		}   
		
		for ( i = 0; i < info_.output_height; i += lines_per_iMCU_row)        
		{            
			int nRows = lines_per_iMCU_row;
			if((info_.output_height) < (i + lines_per_iMCU_row))
			{
//				nRows = info_.output_height - i;
			}
			
			jpeg_read_raw_data(&info_, yuvptr_, nRows);              
			yuvptr_[0] += lines_per_iMCU_row;            
			yuvptr_[1] += lines_per_iMCU_row / 2;            
			yuvptr_[2] += lines_per_iMCU_row / 2;         
		}		
		jpeg_finish_decompress(&info_);   	
	}

	virtual void GetYUVData(unsigned char* &pYData,  unsigned char* &pUData, unsigned char* &pVData, 
						int &nHeight, int &nWidth)
	{
		nWidth = info_.output_width ;           
		nHeight = info_.output_height;
		int nLen1 = info_.output_height * info_.output_width;	

		pYData = m_pYbuffer;
		pUData = m_pUbuffer;
		pVData = m_pVbuffer;

		return ;

	}
protected:     
	jpeg_decompress_struct info_;     
	jpeg_error_mgr e_;     
	std::vector< std::vector< unsigned char* > > yuvbuf_;      
	unsigned char** yuvptr_[3]; 

	uint8_t* m_pYbuffer;
	uint8_t* m_pUbuffer;
	uint8_t* m_pVbuffer;
	uint8_t* m_pDummyBuffer;
}; 
