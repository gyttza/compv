/* Copyright (C) 2016-2018 Doubango Telecom <https://www.doubango.org>
* File author: Mamadou DIOP (Doubango Telecom, France).
* License: GPLv3. For commercial license please contact us.
* Source code: https://github.com/DoubangoTelecom/compv
* WebSite: http://compv.org
*/

/* @description
This class implement LMSER (Linear Time Maximally Stable Extremal Regions) algorithm.
Some literature about MSER:
- Linear Time Maximally Stable Extremal Regions: https://github.com/Stanley/043/blob/master/docs/bibl/Linear%20Time%20Maximally%20Stable%20Extremal%20Regions/53030183.pdf
*/

#include "compv/core/ccl/compv_core_ccl_lmser.h"
#include "compv/core/compv_core.h"
#include "compv/base/parallel/compv_parallel.h"
#include "compv/base/compv_cpu.h"
#include "compv/base/compv_memz.h"
#include "compv/base/compv_mem_pool_light.h"

#define COMPV_CORE_LMSER_ACCESSIBILITY_BUILD_SAMPLES_PER_THREAD		(40 * 40)
#define COMPV_CORE_LMSER_FILL_REGIONS_SAMPLES_PER_THREAD			(1 * 1) // This is the number of regions -> use max threads

#define LMSER_HIGHEST_GREYLEVEL		256
#define LMSER_GOTO(stepx) goto __________________________##stepx##__________________________
#define LMSER_POOL_ADD_POINT_TO_LINKED_LIST(poolPointsPtr, linked_list, pixel_idx) { \
		poolPointsPtr->data = pixel_idx; \
		(linked_list).push_front(poolPointsPtr++); \
}
#define LMSER_POOL_ADD_BOUNDARY_PIXEL_TO_LINKED_LIST(poolBoundaryPixelsPtr, linked_list, boundary_pixel) { \
	poolBoundaryPixelsPtr->data = boundary_pixel; \
	(linked_list).push_back(poolBoundaryPixelsPtr++); \
}
#define LMSER_CHECK_EDGE() { \
		if (current_edge < maxEdges) { \
			const uint8_t edge_mask = LMSER_EDGES_MASKS[current_edge]; \
			if ((ptr8uAccessibleRef[current_pixel] & edge_mask) == edge_mask) { \
				const int32_t neighbor_pixel = current_pixel + LMSER_EDGES_OFFSETS[current_edge]; \
				if (!(ptr8uAccessibleRef[neighbor_pixel] & 1)) { \
					ptr8uAccessibleRef[neighbor_pixel] |= 1; \
					const uint8_t neighbor_level = ptr8uPixelsRef[neighbor_pixel]; \
					if (neighbor_level >= current_level) { \
						LMSER_POOL_ADD_BOUNDARY_PIXEL_TO_LINKED_LIST(poolBoundaryPixelsPtr, boundaryPixels[neighbor_level], (neighbor_pixel << 4)); \
						if (neighbor_level < current_priority) current_priority = neighbor_level; \
					} \
					else { \
						LMSER_POOL_ADD_BOUNDARY_PIXEL_TO_LINKED_LIST(poolBoundaryPixelsPtr, boundaryPixels[current_level], ((current_pixel << 4) | ++current_edge)); \
						if (current_level < current_priority) current_priority = current_level; \
						current_edge = 0; \
						current_pixel = neighbor_pixel; \
						current_level = neighbor_level; \
						LMSER_GOTO(step3); \
					} \
				} \
			} \
			++current_edge; \
		} \
		else { \
			break; /* break do{...} while(0) */ \
		} \
}

#define LMSER_EDGE_RIGHT		16	// 0b000 1000 0
#define LMSER_EDGE_BOTTOM		8	// 0b000 0100 0
#define LMSER_EDGE_LEFT			4	// 0b000 0010 0
#define LMSER_EDGE_TOP			2	// 0b000 0001 0
#define LMSER_EDGE_RIGHT_TOP	18  // (RIGTH | TOP)
#define LMSER_EDGE_LEFT_TOP		6	// (LEFT | TOP)
#define LMSER_EDGE_LEFT_BOTTOM	12	// (LEFT | BOTTOM)
#define LMSER_EDGE_RIGHT_BOTTOM	24	// (RIGHT | BOTTOM)

#define COMPV_THIS_CLASSNAME	"CompVConnectedComponentLabelingLMSER"

COMPV_NAMESPACE_BEGIN()

struct CompVConnectedComponentLmserLinkedListNodeBoundaryPixel {
	int32_t data;
	struct CompVConnectedComponentLmserLinkedListNodeBoundaryPixel* prev;
};
struct CompVConnectedComponentLmserLinkedListBoundaryPixel {
	CompVConnectedComponentLmserLinkedListBoundaryPixel()
		: tail(nullptr)
	{ }
	COMPV_ALWAYS_INLINE bool empty()const {
		return !tail;
	}
	COMPV_ALWAYS_INLINE void push_back(CompVConnectedComponentLmserLinkedListNodeBoundaryPixel* node) {
		node->prev = tail, tail = node;
	}
	COMPV_ALWAYS_INLINE void pop_back() {
#if 0 // tail never null -> checked by the caller
		tail = tail ? tail->prev : nullptr;
#else
		tail = tail->prev;
#endif
	}
	CompVConnectedComponentLmserLinkedListNodeBoundaryPixel* tail;
};

typedef CompVMemPoolLightUnstructured<CompVConnectedComponentLmserLinkedListNodeBoundaryPixel > CompVMemPoolLightUnstructuredBoundaryPixel;
typedef CompVPtr<CompVMemPoolLightUnstructuredBoundaryPixel* > CompVMemPoolLightUnstructuredBoundaryPixelPtr;
typedef CompVMemPoolLightUnstructuredBoundaryPixelPtr* CompVMemPoolLightUnstructuredBoundaryPixelPtrPtr;

typedef CompVMemPoolLightUnstructured<CompVConnectedComponentLmserLinkedListNodePixelIdx > CompVMemPoolLightUnstructuredPixelIdx;
typedef CompVPtr<CompVMemPoolLightUnstructuredPixelIdx* > CompVMemPoolLightUnstructuredPixelIdxPtr;
typedef CompVMemPoolLightUnstructuredPixelIdxPtr* CompVMemPoolLightUnstructuredPixelIdxPtrPtr;

static const uint8_t LMSER_EDGES_MASKS[8] = {
	LMSER_EDGE_RIGHT,
	LMSER_EDGE_BOTTOM,
	LMSER_EDGE_LEFT,
	LMSER_EDGE_TOP,
	LMSER_EDGE_RIGHT_TOP,
	LMSER_EDGE_LEFT_TOP,
	LMSER_EDGE_LEFT_BOTTOM,
	LMSER_EDGE_RIGHT_BOTTOM,
};

CompVConnectedComponentLabelingLMSER::CompVConnectedComponentLabelingLMSER()
	: CompVConnectedComponentLabeling(COMPV_LMSER_ID)
{

}

CompVConnectedComponentLabelingLMSER::~CompVConnectedComponentLabelingLMSER()
{

}

COMPV_ERROR_CODE CompVConnectedComponentLabelingLMSER::set(int id, const void* valuePtr, size_t valueSize) /*Overrides(CompVConnectedComponentLabeling)*/
{
	switch (id) {
	case COMPV_CCL_SET_INT_CONNECTIVITY:
	default:
		COMPV_CHECK_CODE_RETURN(CompVConnectedComponentLabeling::set(id, valuePtr, valueSize));
		return COMPV_ERROR_CODE_S_OK;
	}
}

COMPV_ERROR_CODE CompVConnectedComponentLabelingLMSER::process(const CompVMatPtr& ptr8uImage, CompVConnectedComponentLabelingResultPtrPtr result) /*Overrides(CompVConnectedComponentLabeling)*/
{
	COMPV_CHECK_EXP_RETURN(!ptr8uImage || ptr8uImage->isEmpty() || ptr8uImage->planeCount() != 1 || ptr8uImage->elmtInBytes() != sizeof(uint8_t) || !result
		, COMPV_ERROR_CODE_E_INVALID_PARAMETER);

	/* Create result */
	CompVConnectedComponentLabelingResultLMSERImplPtr result_;
	if (*result && (*result)->id() == id()) {
		result_ = reinterpret_cast<CompVConnectedComponentLabelingResultLMSERImpl*>(**result);
	}
	if (!result_) {
		COMPV_CHECK_CODE_RETURN(CompVConnectedComponentLabelingResultLMSERImpl::newObj(&result_));
	}
	COMPV_CHECK_CODE_RETURN(result_->reset());
	CompVConnectedComponentLabelingRegionMserVector& vecRegions_final = result_->vecRegions();

	const int16_t width = static_cast<int16_t>(ptr8uImage->cols());
	const int16_t widthMinus1 = width - 1;
	const int16_t height = static_cast<int16_t>(ptr8uImage->rows());
	const int16_t heightMinus1 = height - 1;
	const int16_t stride = static_cast<int16_t>(ptr8uImage->stride());

	const bool b8Connectivity = (connectivity() == 8);
	const int8_t maxEdges = b8Connectivity ? 8 : 4;
	const int16_t LMSER_EDGES_OFFSETS[8] = {
		1,
		stride,
		-1,
		-stride,
		1 - stride,
		-(1 + stride),
		stride - 1,
		stride + 1
	};

	// Create a pool of points to speedup allocation
	CompVMemPoolLightUnstructuredPixelIdxPtr poolPoints;
	COMPV_CHECK_CODE_RETURN(CompVMemPoolLightUnstructuredPixelIdx::newObj(&poolPoints, (width * height)));
	CompVConnectedComponentLmserLinkedListNodePixelIdx* poolPointsPtr = poolPoints->ptr();

	//Create a poll of boundary pixels to speedup allocation
	CompVMemPoolLightUnstructuredBoundaryPixelPtr poolBoundaryPixels;
	COMPV_CHECK_CODE_RETURN(CompVMemPoolLightUnstructuredBoundaryPixel::newObj(&poolBoundaryPixels, (width * height)));
	CompVConnectedComponentLmserLinkedListNodeBoundaryPixel* poolBoundaryPixelsPtr = poolBoundaryPixels->ptr();

	// A binary mask of accessible pixels. These are the pixels to which the water
	// already has access.
	CompVMatPtr ptr8uAccessible;
	COMPV_CHECK_CODE_RETURN(CompVMat::newObjAligned<uint8_t>(&ptr8uAccessible, height, width, stride));
	uint8_t* ptr8uAccessibleRef = ptr8uAccessible->ptr<uint8_t>();
	auto funcPtrSetAccessibility = [&](const size_t ystart, const size_t yend) -> COMPV_ERROR_CODE {
		size_t ystart_ = ystart;
		size_t yend_ = yend;
		/* TOP line */
		if (!ystart) {
			uint8_t* mt_ptr8u = ptr8uAccessible->ptr<uint8_t>(0);
			CompVMem::set(&mt_ptr8u[1], 28, (width - 2), sizeof(uint8_t));
			mt_ptr8u[0] = 24;
			mt_ptr8u[widthMinus1] = 12;
			ystart_ = 1;
		}
		/* BOTTOM line */
		if (yend == height) {
			uint8_t* mt_ptr8u = ptr8uAccessible->ptr<uint8_t>(heightMinus1);
			CompVMem::set(&mt_ptr8u[1], 22, (width - 2), sizeof(uint8_t));
			mt_ptr8u[0] = 18;
			mt_ptr8u[widthMinus1] = 6;
			yend_ = heightMinus1;
		}
		/* OTHER lines */
		uint8_t* mt_ptr8uAccessibleRef = ptr8uAccessible->ptr<uint8_t>(ystart_);
		const int16_t width64_ = (widthMinus1 < 8) ? 0 : ((widthMinus1 - 8) & -8);
		int16_t x;
		for (size_t y = ystart_; y < yend_; ++y) {
			*reinterpret_cast<uint64_t*>(&mt_ptr8uAccessibleRef[0]) = 2170205185142300186ul;
			for (x = 8; x < width64_; x += 8) {
				*reinterpret_cast<uint64_t*>(&mt_ptr8uAccessibleRef[x]) = 2170205185142300190ul;
			}
			for (; x < widthMinus1; ++x) {
				mt_ptr8uAccessibleRef[x] = 30;
			}
			mt_ptr8uAccessibleRef[widthMinus1] = 14;
			mt_ptr8uAccessibleRef += stride;
		}
		return COMPV_ERROR_CODE_S_OK;
	};
	COMPV_CHECK_CODE_RETURN(CompVThreadDispatcher::dispatchDividingAcrossY(
		funcPtrSetAccessibility,
		width,
		height,
		std::max(COMPV_CORE_LMSER_ACCESSIBILITY_BUILD_SAMPLES_PER_THREAD, (width << 1)) // At least #2 lines per thread
	));

	// A priority queue of boundary pixels, where priority is minus the grey - level.
	// These pixels can be thought of as partially flooded pixels in the sense that
	// water has access to the pixel in question, but has either not yet entered, or
	// not yet explored all the edges out from the pixel. Along with the pixel id,
	// an edge number indicating the next edge to be explored can be stored.
	CompVConnectedComponentLmserLinkedListBoundaryPixel boundaryPixels[256];
	int16_t current_priority = LMSER_HIGHEST_GREYLEVEL;

	// A stack C of component information.Each entry holds the pixels in a component
	// and / or the first and second order moments of the pixels in the component,
	// as well as the size history of the component and the current grey - level
	// at which the component is being processed.The maximum number of entries
	// on the stack will be the number of grey - levels.
	std::vector<CompVConnectedComponentLmserRef, CompVAllocatorNoDefaultConstruct<CompVConnectedComponentLmserRef> > stackC;

	// The heap could be dynamically allocated and implemented as linked lists, but
	// we prefer to count the number pixels at each grey - level in a single pre - sweep of
	// the image, just like the standard algorithm does. This allows us to both preallocate
	// and use fixed arrays for the stacks without stacks ever running into one
	// another.The total number of entries in the stacks is the number of pixels plus
	// the number of grey - levels(due to one stop - element for each stack).
	CompVConnectedComponentLabelingLMSERStackMem& stackMem = result_->stackMem();
	CompVConnectedComponentLmserRef stackMemRef_;

	// 1. Clear the accessible pixel mask, the heap of boundary pixels and the component
	// 	stack.Push a dummy - component onto the stack, with grey - level higher
	// 	than any allowed in the image.
	COMPV_CHECK_CODE_RETURN(stackMem.requestNewItem(&stackMemRef_, LMSER_HIGHEST_GREYLEVEL));
	stackC.push_back(stackMemRef_);

	// 2. Make the source pixel(with its first edge) the current pixel, mark it as
	// 	accessible and store the grey - level of it in the variable current_level.
	const uint8_t* ptr8uPixelsRef = ptr8uImage->ptr<const uint8_t>();
	int32_t current_pixel = 0;
	int8_t current_edge = 0;
	ptr8uAccessibleRef[0] |= 1;
	uint8_t current_level = ptr8uPixelsRef[0];

__________________________step3__________________________:
	// 3. Push an empty component with current level onto the component stack.
	COMPV_CHECK_CODE_RETURN(stackMem.requestNewItem(&stackMemRef_, current_level));
	stackC.push_back(stackMemRef_);

	// 4. Explore the remaining edges to the neighbors of the current pixel, in order,
	// as follows : For each neighbor, check if the neighbor is already accessible. If it
	// is not, mark it as accessible and retrieve its grey - level.If the grey - level is not
	// lower than the current one, push it onto the heap of boundary pixels. If on
	// the other hand the grey - level is lower than the current one, enter the current
	// pixel back into the queue of boundary pixels for later processing(with the
	// next edge number), consider the new pixel and its grey - level and go to 3.
	do {
		do {
			/* No need to check if we're using 4-connectivity or 8-connectivity (thanks to maxEdges) */
			LMSER_CHECK_EDGE(); LMSER_CHECK_EDGE(); LMSER_CHECK_EDGE(); LMSER_CHECK_EDGE(); // 4-connectivity
			LMSER_CHECK_EDGE(); LMSER_CHECK_EDGE(); LMSER_CHECK_EDGE(); LMSER_CHECK_EDGE(); // 8-connectivity
		} while (0);

		  // 5. Accumulate the current pixel to the component at the top of the stack(water
		  // 	saturates the current pixel).
		CompVConnectedComponentLmserRef& top = stackC.back();
		++top->area;
		LMSER_POOL_ADD_POINT_TO_LINKED_LIST(poolPointsPtr, top->points, current_pixel);

		// 6. Pop the heap of boundary pixels. If the heap is empty, we are done. If the
		// 	returned pixel is at the same grey - level as the previous, go to 4.
		if (current_priority == LMSER_HIGHEST_GREYLEVEL) {
			LMSER_GOTO(we_are_done);
		}

		CompVConnectedComponentLmserLinkedListNodeBoundaryPixel*& tail = boundaryPixels[current_priority].tail;
		current_pixel = tail->data >> 4;
		current_edge = tail->data & 0x0f;
		tail = tail->prev; // pop_back()

		for (; current_priority < LMSER_HIGHEST_GREYLEVEL && !boundaryPixels[current_priority].tail; ++current_priority)
			/* do nothing */;

		// 7. The returned pixel is at a higher grey - level, so we must now process all
		// 	components on the component stack until we reach the higher grey - level.
		// 	This is done with the ProcessStack sub - routine, see below.Then go to 4.
		const uint8_t new_pixel_grey_level = ptr8uPixelsRef[current_pixel];
		if (new_pixel_grey_level != current_level) {
			current_level = new_pixel_grey_level;

			///////////////////////////////////////////////////////
			// The ProcessStack sub - routine is as follows :
			// Sub - routine ProcessStack(new pixel grey level)
			///////////////////////////////////////////////////////
			do {
				// 1. Process component on the top of the stack.The next grey - level is the minimum
				// 	of new pixel grey level and the grey - level for the second component on
				// 	the stack.
				CompVConnectedComponentLmserRef top = stackC.back();
				stackC.pop_back();

				// 2. If new pixel grey level is smaller than the grey - level on the second component
				// on the stack, set the top of stack grey - level to new pixel grey level and return
				// from sub - routine(This occurs when the new pixel is at a grey - level for which
				// there is not yet a component instantiated, so we let the top of stack be that
				// level by just changing its grey - level.
				if (new_pixel_grey_level < stackC.back()->greyLevel) {
					COMPV_CHECK_CODE_RETURN(stackMem.requestNewItem(&stackMemRef_, new_pixel_grey_level));
					stackMemRef_->merge(top);
					stackC.push_back(stackMemRef_);
					break;
				}

				// 3. Remove the top of stack and merge it into the second component on stack
				// as follows : Add the first and second moment accumulators together and / or
				// join the pixel lists.Either merge the histories of the components, or take the
				// history from the winner.Note here that the top of stack should be considered
				// one �time - step� back, so its current size is part of the history.Therefore the
				// top of stack would be the winner if its current size is larger than the previous
				// size of second on stack.
				CompVConnectedComponentLmserRef back = stackC.back();
				back->merge(top);

			} while (new_pixel_grey_level > stackC.back()->greyLevel); // 4. If(new pixel grey level>top of stack grey level) go to 1.
		}
	} while (true);

__________________________we_are_done__________________________:
	// Compute stability and collect regions
	const int input_area = width * height;
	const int min_area_ = static_cast<int>(input_area * minArea());
	const int max_area_ = static_cast<int>(input_area * maxArea());
	const double one_minus_min_diversity = 1.0 - minDiversity();
	const double one_minus_min_diversity_scale = 1.0 / one_minus_min_diversity;
	CompVConnectedComponentLmserRefVector vecRegions_stable;
	COMPV_CHECK_CODE_RETURN(stackMem.computeVariation(delta()));
	COMPV_CHECK_CODE_RETURN(stackMem.computeStability(min_area_, max_area_, maxVariation()));
	CompVConnectedComponentLabelingLMSER::collect(stackC.back(), one_minus_min_diversity, one_minus_min_diversity_scale, vecRegions_stable);

	/* Building final points from stable regions */
	if (!vecRegions_stable.empty()) {
		const float stride_scale = 1.f / float(stride);
		const size_t count = vecRegions_stable.size();
		vecRegions_final.resize(count);

		auto funcPtrFill = [&](const size_t start, const size_t end) -> COMPV_ERROR_CODE {
			CompVConnectedComponentLmserRefVector::iterator it_src = vecRegions_stable.begin();
			CompVConnectedComponentLabelingRegionMserVector::iterator it_dst = vecRegions_final.begin();
			for (size_t i = start; i < end; ++i) {
				size_t index = 0;
				CompVConnectedComponentLabelingLMSER::fill(it_src[i], it_dst[i], index, stride, stride_scale);
			}
			return COMPV_ERROR_CODE_S_OK;
		};

		COMPV_CHECK_CODE_RETURN(CompVThreadDispatcher::dispatchDividingAcrossY(
			funcPtrFill,
			1,
			count,
			COMPV_CORE_LMSER_FILL_REGIONS_SAMPLES_PER_THREAD
		));
	}

	*result = *result_;

	return COMPV_ERROR_CODE_S_OK;
}

void CompVConnectedComponentLabelingLMSER::collect(CompVConnectedComponentLmserRef& component, const double& one_minus_min_diversity, const double& one_minus_min_diversity_scale, CompVConnectedComponentLmserRefVector& vecRegions)
{
	int8_t& stable_ = component->stable;
	if (stable_) {
		const int min_parent_area = COMPV_MATH_ROUNDFU_2_NEAREST_INT((component->area * one_minus_min_diversity_scale), int);
		for (CompVConnectedComponentLmserRef parent = component->parent;
			(parent && (parent->area < min_parent_area) && (stable_ = (!parent->stable || (parent->variation > component->variation))));
			(parent = parent->parent)
			) /* do noting */;

		stable_ =
			stable_ &&
			(CompVConnectedComponentLabelingLMSER::checkCrit(
				component,
				COMPV_MATH_ROUNDFU_2_NEAREST_INT((component->area * one_minus_min_diversity), int), // max_child_area
				component->variation
			));

		if (stable_) {
			vecRegions.push_back(component);
		}
	}

	CompVConnectedComponentLmserRef child_ = component->child;
	while (child_) {
		CompVConnectedComponentLabelingLMSER::collect(child_, one_minus_min_diversity, one_minus_min_diversity_scale, vecRegions);
		child_ = child_->sister;
	}
}

bool CompVConnectedComponentLabelingLMSER::checkCrit(const CompVConnectedComponentLmserRef& component, const int& area, const double& variation)
{
	if (component->area <= area) {
		return true;
	}
	if (component->stable && (component->variation < variation)) {
		return false;
	}

	CompVConnectedComponentLmserRef child_ = component->child;
	while (child_) {
		if (!CompVConnectedComponentLabelingLMSER::checkCrit(child_, area, variation)) {
			return false;
		}
		child_ = child_->sister;
	}

	return true;
}

void CompVConnectedComponentLabelingLMSER::fill(const CompVConnectedComponentLmser* cc_stable, CompVConnectedComponentLabelingRegionMser& cc_final, size_t& index, const int16_t& stride, const float& stride_scale)
{
	CompVConnectedComponentPoints& points_final = cc_final.points;
	if (!index) {
		points_final.resize(static_cast<size_t>(cc_stable->area));
	}
	const CompVConnectedComponentLmserLinkedListPixelIdx& points_stable = cc_stable->points;
	for (const CompVConnectedComponentLmserLinkedListNodePixelIdx* node = points_stable.head; node; node = node->next) {
		CompVPoint2DInt16& point = points_final[index++];
		 point.y = static_cast<int16_t>(node->data * stride_scale);
		 point.x = static_cast<int16_t>(node->data - (point.y * stride));
	}
	const CompVConnectedComponentLmserNodesVector& merge_nodes_stable = cc_stable->merge_nodes;
	for (CompVConnectedComponentLmserNodesVector::const_iterator merge_node = merge_nodes_stable.begin(); merge_node < merge_nodes_stable.end(); ++merge_node) {
		CompVConnectedComponentLabelingLMSER::fill(*merge_node, cc_final, index, stride, stride_scale);
	}
}

COMPV_ERROR_CODE CompVConnectedComponentLabelingLMSER::newObj(CompVConnectedComponentLabelingPtrPtr ccl)
{
	COMPV_CHECK_CODE_RETURN(CompVCore::init());
	COMPV_CHECK_EXP_RETURN(!ccl, COMPV_ERROR_CODE_E_INVALID_PARAMETER);
	CompVConnectedComponentLabelingPtr _ccl = new CompVConnectedComponentLabelingLMSER();
	COMPV_CHECK_EXP_RETURN(!_ccl, COMPV_ERROR_CODE_E_OUT_OF_MEMORY);
	*ccl = *_ccl;
	return COMPV_ERROR_CODE_S_OK;
}

COMPV_NAMESPACE_END()
