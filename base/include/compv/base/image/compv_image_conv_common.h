/* Copyright (C) 2016-2018 Doubango Telecom <https://www.doubango.org>
* File author: Mamadou DIOP (Doubango Telecom, France).
* License: GPLv3. For commercial license please contact us.
* Source code: https://github.com/DoubangoTelecom/compv
* WebSite: http://compv.org
*/
#if !defined(_COMPV_BASE_IMAGE_CONV_COMMON_H_)
#define _COMPV_BASE_IMAGE_CONV_COMMON_H_

#if defined(_COMPV_BASE_API_H_)
#error("This is a private file and must not be part of the API")
#endif

#include "compv/base/compv_config.h"
#include "compv/base/compv_common.h"

#if !defined(COMPV_IMAGE_CONV_MIN_SAMPLES_PER_THREAD)
#define COMPV_IMAGE_CONV_MIN_SAMPLES_PER_THREAD		(200 * 50) // minimum number of samples to consider per thread when multi-threading
#endif


// Read "documentation/yuv_rgb_conv.txt" for more info on how we computed these coeffs
// RGBA -> YUV
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kRGBAToYUV_YCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kRGBAToYUV_UCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kRGBAToYUV_VCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kRGBAToYUV_UVCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kRGBAToYUV_U2V2Coeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kRGBAToYUV_U4V4Coeffs8[];
// ARGB -> YUV
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kARGBToYUV_YCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kARGBToYUV_UCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kARGBToYUV_VCoeffs8[];
//  BGRA -> YUV
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kBGRAToYUV_YCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kBGRAToYUV_UCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kBGRAToYUV_VCoeffs8[];
// ABGR -> YUV
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kABGRToYUV_YCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kABGRToYUV_UCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kABGRToYUV_VCoeffs8[];
// RGB -> YUV
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kRGBToYUV_YCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kRGBToYUV_UCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kRGBToYUV_VCoeffs8[];
// BGR -> YUV
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kBGRToYUV_YCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kBGRToYUV_UCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kBGRToYUV_VCoeffs8[];

// YUV -> RGBA
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kYUVToRGBA_RCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kYUVToRGBA_GCoeffs8[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int8_t kYUVToRGBA_BCoeffs8[];

COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int32_t kShuffleEpi8_RgbToRgba_i32[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int32_t kShuffleEpi8_Yuyv422ToYuv_i32[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() int32_t kShuffleEpi8_Uyvy422ToYuv_i32[];

COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() uint16_t kRGB565ToYUV_RMask_u16[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() uint16_t kRGB565ToYUV_GMask_u16[];
COMPV_EXTERNC COMPV_BASE_API COMPV_ALIGN_DEFAULT() uint16_t kRGB565ToYUV_BMask_u16[];

#endif /* _COMPV_BASE_IMAGE_CONV_COMMON_H_ */
