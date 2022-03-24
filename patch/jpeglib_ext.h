#ifndef JPEGLIB_EXT_H
#define JPEGLIB_EXT_H

#include "jpeglib.h"
#include "transupp.h"

#ifdef __cplusplus
#ifndef DONT_USE_EXTERN_C
extern "C" {
#endif
#endif

EXTERN(void) jpeg_CreateDecompress_Ext(j_decompress_ptr cinfo, int version, size_t structsize, boolean enalbeHWDecode);

EXTERN(int)
jpeg_fb_dest(j_decompress_ptr cinfo, 
			unsigned int fb_no,
			unsigned int fb_width,
            unsigned int fb_height,
            unsigned int img_width,
            unsigned int img_height,
            unsigned int img_pos_x,
            unsigned int img_pos_y,
            JXFORM_CODE xform);


#ifdef __cplusplus
#ifndef DONT_USE_EXTERN_C
}
#endif
#endif

#endif
