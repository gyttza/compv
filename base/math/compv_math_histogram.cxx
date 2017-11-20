/* Copyright (C) 2016-2017 Doubango Telecom <https://www.doubango.org>
* File author: Mamadou DIOP (Doubango Telecom, France).
* License: GPLv3. For commercial license please contact us.
* Source code: https://github.com/DoubangoTelecom/compv
* WebSite: http://compv.org
*/
#include "compv/base/math/compv_math_histogram.h"
#include "compv/base/math/compv_math_utils.h"
#include "compv/base/image/compv_image.h"
#include "compv/base/parallel/compv_parallel.h"

#define COMPV_HISTOGRAM_BUILD_MIN_SAMPLES_PER_THREAD		(256 * 5)
#define COMPV_HISTOGRAM_EQUALIZ_MIN_SAMPLES_PER_THREAD		(256 * 4)

COMPV_NAMESPACE_BEGIN()

#if COMPV_ASM
#	if COMPV_ARCH_X64
	COMPV_EXTERNC void CompVMathHistogramProcess_8u32s_Asm_X64_SSE2(COMPV_ALIGNED(SSE) const uint8_t* dataPtr, compv_uscalar_t width, compv_uscalar_t height, COMPV_ALIGNED(SSE) compv_uscalar_t stride, COMPV_ALIGNED(SSE) uint32_t* histogramPtr);
	COMPV_EXTERNC void CompVMathHistogramProcess_8u32s_Asm_X64(const uint8_t* dataPtr, compv_uscalar_t width, compv_uscalar_t height, compv_uscalar_t stride, uint32_t* histogramPtr);
#   elif COMPV_ARCH_ARM32
    COMPV_EXTERNC void CompVMathHistogramProcess_8u32s_Asm_NEON32(const uint8_t* dataPtr, compv_uscalar_t width, compv_uscalar_t height, compv_uscalar_t stride, uint32_t* histogramPtr);
#   elif COMPV_ARCH_ARM64
    COMPV_EXTERNC void CompVMathHistogramProcess_8u32s_Asm_NEON64(const uint8_t* dataPtr, compv_uscalar_t width, compv_uscalar_t height, compv_uscalar_t stride, uint32_t* histogramPtr);
#	endif
#endif /* COMPV_ASM */

COMPV_ERROR_CODE CompVMathHistogram::build(const CompVMatPtr& data, CompVMatPtrPtr histogram)
{
	COMPV_CHECK_EXP_RETURN(!data || data->isEmpty() || !histogram, COMPV_ERROR_CODE_E_INVALID_PARAMETER, "Input data is null or empty");

	if (data->elmtInBytes() == sizeof(uint8_t)) { // no plane count check needed
		COMPV_CHECK_CODE_RETURN(CompVMat::newObjAligned<uint32_t>(histogram, 1, 256));
		COMPV_CHECK_CODE_RETURN((*histogram)->zero_rows());
		uint32_t* histogramPtr = (*histogram)->ptr<uint32_t>();
		const int planes = static_cast<int>(data->planeCount());
		for (int p = 0; p < planes; ++p) {
			COMPV_CHECK_CODE_RETURN(CompVMathHistogram::process_8u32u(data->ptr<uint8_t>(0, 0, p), data->cols(p), data->rows(p), data->strideInBytes(p), histogramPtr));
		}
		return COMPV_ERROR_CODE_S_OK;
	}

	COMPV_DEBUG_ERROR("Input format not supported, we expect 8u data");
	return COMPV_ERROR_CODE_E_NOT_IMPLEMENTED;
}

static void CompVMathHistogramEqualiz_32u8u_C(const uint32_t* ptr32uHistogram, uint8_t* ptr8uLut, const compv_float32_t* scale1);

COMPV_ERROR_CODE CompVMathHistogram::equaliz(const CompVMatPtr& dataIn, CompVMatPtrPtr dataOut)
{
	COMPV_CHECK_EXP_RETURN(!dataOut || !dataIn || dataIn->isEmpty() || dataIn->elmtInBytes() != sizeof(uint8_t) || dataIn->planeCount() != 1,
		COMPV_ERROR_CODE_E_INVALID_PARAMETER);
	CompVMatPtr histogram;
	COMPV_CHECK_CODE_RETURN(CompVMathHistogram::build(dataIn, &histogram));
	COMPV_CHECK_CODE_RETURN(CompVMathHistogram::equaliz(dataIn, histogram, dataOut));
	return COMPV_ERROR_CODE_S_OK;
}

COMPV_ERROR_CODE CompVMathHistogram::equaliz(const CompVMatPtr& dataIn, const CompVMatPtr& histogram, CompVMatPtrPtr dataOut)
{
	COMPV_CHECK_EXP_RETURN(!dataOut || !dataIn || dataIn->isEmpty() || dataIn->elmtInBytes() != sizeof(uint8_t) || dataIn->planeCount() != 1
		|| !histogram || histogram->isEmpty() || histogram->cols() != 256 || histogram->subType() != COMPV_SUBTYPE_RAW_UINT32,
		COMPV_ERROR_CODE_E_INVALID_PARAMETER);

	const size_t width = dataIn->cols();
	const size_t height = dataIn->rows();
	const size_t stride = dataIn->stride();

	// This function allows dataOut to be equal to dataIn
	CompVMatPtr dataOut_ = *dataOut;
	if (!dataOut_ || dataOut_->cols() != width || dataOut_->rows() != height || dataOut_->stride() != stride || dataOut_->elmtInBytes() != sizeof(uint8_t) || dataOut_->planeCount() != 1) {
		COMPV_CHECK_CODE_RETURN(CompVImage::newObj8u(&dataOut_, COMPV_SUBTYPE_PIXELS_Y, width, height, stride));
	}

	const compv_float32_t scale = 255.f / (width * height); 
	COMPV_ALIGN(16) uint8_t lut[256];

	void(*CompVMathHistogramEqualiz_32u8u)(const uint32_t* ptr32uHistogram, uint8_t* ptr8uLut, const compv_float32_t* scale1)
		= CompVMathHistogramEqualiz_32u8u_C;

	CompVMathHistogramEqualiz_32u8u(histogram->ptr<const uint32_t>(), lut, &scale);

	// Get Number of threads
	CompVThreadDispatcherPtr threadDisp = CompVParallel::threadDispatcher();
	const size_t maxThreads = threadDisp ? static_cast<size_t>(threadDisp->threadsCount()) : 1;
	const size_t threadsCount = (threadDisp && !threadDisp->isMotherOfTheCurrentThread())
		? CompVThreadDispatcher::guessNumThreadsDividingAcrossY(width, height, maxThreads, COMPV_HISTOGRAM_EQUALIZ_MIN_SAMPLES_PER_THREAD)
		: 1;

	auto funcPtr = [&](const size_t ystart, const size_t yend) -> COMPV_ERROR_CODE {		
		const uint8_t* ptr8uDataIn = dataIn->ptr<const uint8_t>(ystart);
		uint8_t* ptr8uDataOut_ = dataOut_->ptr<uint8_t>(ystart);
		for (size_t j = ystart; j < yend; ++j) {
			for (size_t i = 0; i < width; ++i) {
				ptr8uDataOut_[i] = lut[ptr8uDataIn[i]];
			}
			ptr8uDataIn += stride;
			ptr8uDataOut_ += stride;
		}

		return COMPV_ERROR_CODE_S_OK;
	};

	if (threadsCount > 1) {
		CompVAsyncTaskIds taskIds;
		taskIds.reserve(threadsCount);
		const size_t heights = (height / threadsCount);
		size_t YStart = 0, YEnd;
		for (size_t threadIdx = 0; threadIdx < threadsCount; ++threadIdx) {
			YEnd = (threadIdx == (threadsCount - 1)) ? height : (YStart + heights);
			COMPV_CHECK_CODE_RETURN(threadDisp->invoke(std::bind(funcPtr, YStart, YEnd), taskIds), "Dispatching task failed");
			YStart += heights;
		}
		COMPV_CHECK_CODE_RETURN(threadDisp->wait(taskIds), "Failed to wait for tasks execution");
	}
	else {
		COMPV_CHECK_CODE_RETURN(funcPtr(0, height));
	}

	*dataOut = dataOut_;
	return COMPV_ERROR_CODE_S_OK;
}

static void CompVMathHistogramProcess_8u32u_C(const uint8_t* dataPtr, compv_uscalar_t width, compv_uscalar_t height, compv_uscalar_t stride, uint32_t* histogramPtr);

// Up to the caller to set 'histogramPtr' values to zeros
COMPV_ERROR_CODE CompVMathHistogram::process_8u32u(const uint8_t* dataPtr, size_t width, size_t height, size_t stride, uint32_t* histogramPtr)
{
	// Private function, no need to check imput parameters

	void(*CompVMathHistogramProcess_8u32u)(const uint8_t* dataPtr, compv_uscalar_t width, compv_uscalar_t height, compv_uscalar_t stride, uint32_t* histogramPtr)
		= CompVMathHistogramProcess_8u32u_C;
    
#   if COMPV_ARCH_ARM32
    if (width > 3) {
        COMPV_EXEC_IFDEF_ASM_ARM32(CompVMathHistogramProcess_8u32u = CompVMathHistogramProcess_8u32s_Asm_NEON32);
    }
#   endif /* COMPV_ARCH_ARM32 */
	if (width > 7) {
		COMPV_EXEC_IFDEF_ASM_X64(CompVMathHistogramProcess_8u32u = CompVMathHistogramProcess_8u32s_Asm_X64);
        COMPV_EXEC_IFDEF_ASM_ARM64(CompVMathHistogramProcess_8u32u = CompVMathHistogramProcess_8u32s_Asm_NEON64);
#       if 0
        COMPV_DEBUG_INFO_CODE_FOR_TESTING("Tried using #4 histograms but not faster at all and code not correct");
		if (CompVCpu::isEnabled(kCpuFlagSSE2) && COMPV_IS_ALIGNED_SSE(dataPtr) && COMPV_IS_ALIGNED_SSE(stride) && COMPV_IS_ALIGNED_SSE(histogramPtr)) {
			COMPV_EXEC_IFDEF_ASM_X64(CompVMathHistogramProcess_8u32u = CompVMathHistogramProcess_8u32s_Asm_X64_SSE2);
		}
#       endif /* 0 */
	}

	// Compute number of threads
	CompVThreadDispatcherPtr threadDisp = CompVParallel::threadDispatcher();
	const size_t maxThreads = (threadDisp && !threadDisp->isMotherOfTheCurrentThread()) ? static_cast<size_t>(threadDisp->threadsCount()) : 1;
	const size_t threadsCount = CompVThreadDispatcher::guessNumThreadsDividingAcrossY(width, height, maxThreads, COMPV_HISTOGRAM_BUILD_MIN_SAMPLES_PER_THREAD);

	if (threadsCount > 1) {
		size_t threadIdx, padding;
		std::vector<CompVMatPtr> mt_histograms;
		const size_t countAny = (height / threadsCount);
		const size_t countLast = countAny + (height % threadsCount);
		auto funcPtr = [&](const uint8_t* mt_dataPtr, size_t mt_height, size_t mt_threadIdx) -> COMPV_ERROR_CODE {
			uint32_t* mt_histogramPtr;
			if (mt_threadIdx == 0) {
				mt_histogramPtr = histogramPtr;
			}
			else {
				const size_t mt_histogram_index = (mt_threadIdx - 1);
				COMPV_CHECK_CODE_RETURN(CompVMat::newObjAligned<uint32_t>(&mt_histograms[mt_histogram_index], 1, 256));
				COMPV_CHECK_CODE_RETURN(mt_histograms[mt_histogram_index]->zero_rows());
				mt_histogramPtr = mt_histograms[mt_histogram_index]->ptr<uint32_t>();
			}
			CompVMathHistogramProcess_8u32u(mt_dataPtr, static_cast<compv_uscalar_t>(width), static_cast<compv_uscalar_t>(mt_height), static_cast<compv_uscalar_t>(stride), mt_histogramPtr);
			return COMPV_ERROR_CODE_S_OK;
		};
		const uint8_t* mt_dataPtr = dataPtr;
		const size_t paddingPerThread = (countAny * stride);
		CompVAsyncTaskIds taskIds;
		mt_histograms.resize(threadsCount - 1);
		// Invoke
		for (threadIdx = 0, padding = 0; threadIdx < (threadsCount - 1); ++threadIdx, padding += paddingPerThread) {
			COMPV_CHECK_CODE_RETURN(threadDisp->invoke(std::bind(funcPtr, &mt_dataPtr[padding], countAny, threadIdx), taskIds));
		}
		COMPV_CHECK_CODE_RETURN(threadDisp->invoke(std::bind(funcPtr, &mt_dataPtr[padding], countLast, (threadsCount - 1)), taskIds));
		// Wait and sum the histograms
		COMPV_CHECK_CODE_RETURN(threadDisp->waitOne(taskIds[0]));
		for (threadIdx = 1; threadIdx < threadsCount; ++threadIdx) {
			COMPV_CHECK_CODE_RETURN(threadDisp->waitOne(taskIds[threadIdx]));
			COMPV_CHECK_CODE_RETURN((CompVMathUtils::sum2<uint32_t, uint32_t>(mt_histograms[threadIdx - 1]->ptr<const uint32_t>(), histogramPtr, histogramPtr, 256, 1, 256))); // stride is useless as height is equal to 1
		}
	}
	else {
		CompVMathHistogramProcess_8u32u(dataPtr, static_cast<compv_uscalar_t>(width), static_cast<compv_uscalar_t>(height), static_cast<compv_uscalar_t>(stride), histogramPtr);
	}
    
	return COMPV_ERROR_CODE_S_OK;
}

static void CompVMathHistogramProcess_8u32u_C(const uint8_t* dataPtr, compv_uscalar_t width, compv_uscalar_t height, compv_uscalar_t stride, uint32_t* histogramPtr)
{
	//!\\ TODO(dmi): optiz issue -> on AArch64 MediaPad2 the asm code is almost two times faster than the intrin code.
	COMPV_DEBUG_INFO_CODE_NOT_OPTIMIZED("No SIMD or GPU implementation found");
	compv_uscalar_t i, j;
	const compv_uscalar_t maxWidthStep1 = width & -4;
	int a, b, c, d;
	for (j = 0; j < height; ++j) {
		for (i = 0; i < maxWidthStep1; i += 4) {
			a = dataPtr[i + 0];
			b = dataPtr[i + 1];
			c = dataPtr[i + 2];
			d = dataPtr[i + 3];
			++histogramPtr[a];
			++histogramPtr[b];
			++histogramPtr[c];
			++histogramPtr[d];
		}
		for (; i < width; ++i) {
			++histogramPtr[dataPtr[i]];
		}
		dataPtr += stride;
	}
}

static void CompVMathHistogramEqualiz_32u8u_C(const uint32_t* ptr32uHistogram, uint8_t* ptr8uLut, const compv_float32_t* scale1)
{
#if 1 // Doesn't worth SIMD (in all cases, not used in any commercial product for now)
	COMPV_DEBUG_INFO_CODE_NOT_OPTIMIZED("No SIMD or GPU implementation could be found");
#endif
	const compv_float32_t scale = *scale1;
	uint32_t sum = 0;
	for (size_t i = 0; i < 256; ++i) {
		sum += ptr32uHistogram[i];
		ptr8uLut[i] = static_cast<uint8_t>(sum * scale); // no need for saturation (thanks to scaling factor)
	}
}

COMPV_NAMESPACE_END()
