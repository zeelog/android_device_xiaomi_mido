/*
 * Copyright (c) 2011 - 2017, The Linux Foundation. All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <cutils/log.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <media/msm_media_info.h>
#include <qdMetaData.h>
#include <utils/Singleton.h>
#include <utils/Mutex.h>
#include <algorithm>

#include "gralloc_priv.h"
#include "alloc_controller.h"
#include "memalloc.h"
#include "ionalloc.h"
#include "gr.h"
#include "qd_utils.h"

#define ASTC_BLOCK_SIZE 16

#ifndef ION_FLAG_CP_PIXEL
#define ION_FLAG_CP_PIXEL 0
#endif

#ifndef ION_FLAG_ALLOW_NON_CONTIG
#define ION_FLAG_ALLOW_NON_CONTIG 0
#endif

#ifndef ION_FLAG_CP_CAMERA_PREVIEW
#define ION_FLAG_CP_CAMERA_PREVIEW 0
#endif

#ifdef MASTER_SIDE_CP
#define CP_HEAP_ID ION_SECURE_HEAP_ID
#define SD_HEAP_ID ION_SECURE_DISPLAY_HEAP_ID
#define ION_CP_FLAGS (ION_SECURE | ION_FLAG_CP_PIXEL)
#define ION_SD_FLAGS (ION_SECURE | ION_FLAG_CP_SEC_DISPLAY)
#define ION_SC_FLAGS (ION_SECURE | ION_FLAG_CP_CAMERA)
#define ION_SC_PREVIEW_FLAGS (ION_SECURE | ION_FLAG_CP_CAMERA_PREVIEW)
#else // SLAVE_SIDE_CP
#define CP_HEAP_ID ION_CP_MM_HEAP_ID
#define SD_HEAP_ID CP_HEAP_ID
#define ION_CP_FLAGS (ION_SECURE | ION_FLAG_ALLOW_NON_CONTIG)
#define ION_SD_FLAGS ION_SECURE
#define ION_SC_FLAGS ION_SECURE
#define ION_SC_PREVIEW_FLAGS ION_SECURE
#endif

#ifndef COLOR_FMT_P010_UBWC
#define COLOR_FMT_P010_UBWC 9
#endif

using namespace gralloc;
using namespace qdutils;
using namespace android;

ANDROID_SINGLETON_STATIC_INSTANCE(AdrenoMemInfo);
ANDROID_SINGLETON_STATIC_INSTANCE(MDPCapabilityInfo);

static void getYuvUBwcWidthHeight(int, int, int, int&, int&);
static unsigned int getUBwcSize(int, int, int, const int, const int);

//Common functions

/* The default policy is to return cached buffers unless the client explicity
 * sets the PRIVATE_UNCACHED flag or indicates that the buffer will be rarely
 * read or written in software. Any combination with a _RARELY_ flag will be
 * treated as uncached. */
static bool useUncached(const int& usage) {
    if ((usage & GRALLOC_USAGE_PROTECTED) or
        (usage & GRALLOC_USAGE_PRIVATE_UNCACHED) or
        ((usage & GRALLOC_USAGE_SW_WRITE_MASK) == GRALLOC_USAGE_SW_WRITE_RARELY) or
        ((usage & GRALLOC_USAGE_SW_READ_MASK) ==  GRALLOC_USAGE_SW_READ_RARELY))
        return true;

    return false;
}

//------------- MDPCapabilityInfo-----------------------//
MDPCapabilityInfo :: MDPCapabilityInfo() {
  qdutils::querySDEInfo(HAS_UBWC, &isUBwcSupported);
  qdutils::querySDEInfo(HAS_WB_UBWC, &isWBUBWCSupported);
}

//------------- AdrenoMemInfo-----------------------//
AdrenoMemInfo::AdrenoMemInfo()
{
    LINK_adreno_compute_aligned_width_and_height = NULL;
    LINK_adreno_compute_padding = NULL;
    LINK_adreno_compute_compressedfmt_aligned_width_and_height = NULL;
    LINK_adreno_isUBWCSupportedByGpu = NULL;
    LINK_adreno_get_gpu_pixel_alignment = NULL;

    libadreno_utils = ::dlopen("libadreno_utils.so", RTLD_NOW);
    if (libadreno_utils) {
        *(void **)&LINK_adreno_compute_aligned_width_and_height =
                ::dlsym(libadreno_utils, "compute_aligned_width_and_height");
        *(void **)&LINK_adreno_compute_padding =
                ::dlsym(libadreno_utils, "compute_surface_padding");
        *(void **)&LINK_adreno_compute_compressedfmt_aligned_width_and_height =
                ::dlsym(libadreno_utils,
                        "compute_compressedfmt_aligned_width_and_height");
        *(void **)&LINK_adreno_isUBWCSupportedByGpu =
                ::dlsym(libadreno_utils, "isUBWCSupportedByGpu");
        *(void **)&LINK_adreno_get_gpu_pixel_alignment =
                ::dlsym(libadreno_utils, "get_gpu_pixel_alignment");
    }

    // Check if the overriding property debug.gralloc.gfx_ubwc_disable
    // that disables UBWC allocations for the graphics stack is set
    gfx_ubwc_disable = 0;
    char property[PROPERTY_VALUE_MAX];
    property_get("debug.gralloc.gfx_ubwc_disable", property, "0");
    if(!(strncmp(property, "1", PROPERTY_VALUE_MAX)) ||
       !(strncmp(property, "true", PROPERTY_VALUE_MAX))) {
        gfx_ubwc_disable = 1;
    }
}

AdrenoMemInfo::~AdrenoMemInfo()
{
    if (libadreno_utils) {
        ::dlclose(libadreno_utils);
    }
}

void AdrenoMemInfo::getAlignedWidthAndHeight(const private_handle_t *hnd, int& aligned_w,
                          int& aligned_h) {
    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;
    if(metadata && metadata->operation & UPDATE_BUFFER_GEOMETRY) {
        int w = metadata->bufferDim.sliceWidth;
        int h = metadata->bufferDim.sliceHeight;
        int f = hnd->format;
        int usage = 0;

        if (hnd->flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) {
            usage = GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
        }

        getAlignedWidthAndHeight(w, h, f, usage, aligned_w, aligned_h);
    } else {
        aligned_w = hnd->width;
        aligned_h = hnd->height;
    }

}

void AdrenoMemInfo::getUnalignedWidthAndHeight(const private_handle_t *hnd, int& unaligned_w,
                                               int& unaligned_h) {
    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;
    if(metadata && metadata->operation & UPDATE_BUFFER_GEOMETRY) {
        unaligned_w = metadata->bufferDim.sliceWidth;
        unaligned_h = metadata->bufferDim.sliceHeight;
    } else {
        unaligned_w = hnd->unaligned_width;
        unaligned_h = hnd->unaligned_height;
    }
}

bool isUncompressedRgbFormat(int format)
{
    bool is_rgb_format = false;

    switch (format)
    {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_RGB_888:
        case HAL_PIXEL_FORMAT_RGB_565:
        case HAL_PIXEL_FORMAT_BGR_565:
        case HAL_PIXEL_FORMAT_BGRA_8888:
        case HAL_PIXEL_FORMAT_RGBA_5551:
        case HAL_PIXEL_FORMAT_RGBA_4444:
        case HAL_PIXEL_FORMAT_R_8:
        case HAL_PIXEL_FORMAT_RG_88:
        case HAL_PIXEL_FORMAT_BGRX_8888:
        case HAL_PIXEL_FORMAT_BGR_888:
        case HAL_PIXEL_FORMAT_RGBA_1010102:
        case HAL_PIXEL_FORMAT_ARGB_2101010:
        case HAL_PIXEL_FORMAT_RGBX_1010102:
        case HAL_PIXEL_FORMAT_XRGB_2101010:
        case HAL_PIXEL_FORMAT_BGRA_1010102:
        case HAL_PIXEL_FORMAT_ABGR_2101010:
        case HAL_PIXEL_FORMAT_BGRX_1010102:
        case HAL_PIXEL_FORMAT_XBGR_2101010:    // Intentional fallthrough
            is_rgb_format = true;
            break;
        default:
            break;
    }

    return is_rgb_format;
}

void AdrenoMemInfo::getAlignedWidthAndHeight(int width, int height, int format,
                            int usage, int& aligned_w, int& aligned_h)
{
    bool ubwc_enabled = isUBwcEnabled(format, usage);

    // Currently surface padding is only computed for RGB* surfaces.
    if (isUncompressedRgbFormat(format) == true) {
        int tileEnabled = ubwc_enabled;
        getGpuAlignedWidthHeight(width, height, format, tileEnabled, aligned_w, aligned_h);
    } else if (ubwc_enabled) {
        getYuvUBwcWidthHeight(width, height, format, aligned_w, aligned_h);
    } else {
        aligned_w = width;
        aligned_h = height;
        int alignment = 32;
        switch (format)
        {
            case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            case HAL_PIXEL_FORMAT_YCbCr_420_SP:
                if (LINK_adreno_get_gpu_pixel_alignment) {
                  alignment = LINK_adreno_get_gpu_pixel_alignment();
                }
                aligned_w = ALIGN(width, alignment);
                break;
            case HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO:
                aligned_w = ALIGN(width, alignment);
                break;
            case HAL_PIXEL_FORMAT_RAW16:
            case HAL_PIXEL_FORMAT_Y16:
            case HAL_PIXEL_FORMAT_Y8:
                aligned_w = ALIGN(width, 16);
                break;
            case HAL_PIXEL_FORMAT_RAW12:
                aligned_w = ALIGN(width * 12 / 8, 8);
                break;
            case HAL_PIXEL_FORMAT_RAW10:
                aligned_w = ALIGN(width * 10 / 8, 8);
                break;
            case HAL_PIXEL_FORMAT_RAW8:
                aligned_w = ALIGN(width, 8);
                break;
            case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:
                aligned_w = ALIGN(width, 128);
                break;
            case HAL_PIXEL_FORMAT_YV12:
            case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            case HAL_PIXEL_FORMAT_YCrCb_422_SP:
            case HAL_PIXEL_FORMAT_YCbCr_422_I:
            case HAL_PIXEL_FORMAT_YCrCb_422_I:
            case HAL_PIXEL_FORMAT_YCbCr_420_P010:
            case HAL_PIXEL_FORMAT_CbYCrY_422_I:
                aligned_w = ALIGN(width, 16);
                break;
            case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
            case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
                aligned_w = VENUS_Y_STRIDE(COLOR_FMT_NV12, width);
                aligned_h = VENUS_Y_SCANLINES(COLOR_FMT_NV12, height);
                break;
            case HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS:
                aligned_w = VENUS_Y_STRIDE(COLOR_FMT_NV21, width);
                aligned_h = VENUS_Y_SCANLINES(COLOR_FMT_NV21, height);
                break;
            case HAL_PIXEL_FORMAT_BLOB:
            case HAL_PIXEL_FORMAT_RAW_OPAQUE:
                break;
            case HAL_PIXEL_FORMAT_NV21_ZSL:
                aligned_w = ALIGN(width, 64);
                aligned_h = ALIGN(height, 64);
                break;
            case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_4x4_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_5x4_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_5x5_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_6x5_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_6x6_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_8x5_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_8x6_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_8x8_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x5_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x6_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x8_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x10_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_12x10_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_12x12_KHR:
            case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR:
                if(LINK_adreno_compute_compressedfmt_aligned_width_and_height) {
                    int bytesPerPixel = 0;
                    int raster_mode         = 0;   //Adreno unknown raster mode.
                    int padding_threshold   = 512; //Threshold for padding
                    //surfaces.

                    LINK_adreno_compute_compressedfmt_aligned_width_and_height(
                        width, height, format, 0,raster_mode, padding_threshold,
                        &aligned_w, &aligned_h, &bytesPerPixel);
                } else {
                    ALOGW("%s: Warning!! Symbols" \
                          " compute_compressedfmt_aligned_width_and_height" \
                          " not found", __FUNCTION__);
                }
                break;
            default: break;
        }
    }
}

void AdrenoMemInfo::getGpuAlignedWidthHeight(int width, int height, int format,
                            int tile_enabled, int& aligned_w, int& aligned_h)
{
    aligned_w = ALIGN(width, 32);
    aligned_h = ALIGN(height, 32);

    // Don't add any additional padding if debug.gralloc.map_fb_memory
    // is enabled
    char property[PROPERTY_VALUE_MAX];
    if((property_get("debug.gralloc.map_fb_memory", property, NULL) > 0) &&
       (!strncmp(property, "1", PROPERTY_VALUE_MAX ) ||
       (!strncasecmp(property,"true", PROPERTY_VALUE_MAX )))) {
        return;
    }

    int bpp = 4;
    switch(format)
    {
        case HAL_PIXEL_FORMAT_RGB_888:
            bpp = 3;
            break;
        case HAL_PIXEL_FORMAT_RGB_565:
        case HAL_PIXEL_FORMAT_BGR_565:
        case HAL_PIXEL_FORMAT_RGBA_5551:
        case HAL_PIXEL_FORMAT_RGBA_4444:
            bpp = 2;
            break;
        default: break;
    }

    if (libadreno_utils) {
        int raster_mode         = 0;   // Adreno unknown raster mode.
        int padding_threshold   = 512; // Threshold for padding surfaces.
        // the function below computes aligned width and aligned height
        // based on linear or macro tile mode selected.
        if(LINK_adreno_compute_aligned_width_and_height) {
            LINK_adreno_compute_aligned_width_and_height(width,
                                 height, bpp, tile_enabled,
                                 raster_mode, padding_threshold,
                                 &aligned_w, &aligned_h);

        } else if(LINK_adreno_compute_padding) {
            int surface_tile_height = 1;   // Linear surface
            aligned_w = LINK_adreno_compute_padding(width, bpp,
                                 surface_tile_height, raster_mode,
                                 padding_threshold);
            ALOGW("%s: Warning!! Old GFX API is used to calculate stride",
                                                            __FUNCTION__);
        } else {
            ALOGW("%s: Warning!! Symbols compute_surface_padding and " \
                 "compute_aligned_width_and_height not found", __FUNCTION__);
        }
   }
}

int AdrenoMemInfo::isUBWCSupportedByGPU(int format)
{
    if (!gfx_ubwc_disable && libadreno_utils) {
        if (LINK_adreno_isUBWCSupportedByGpu) {
            ADRENOPIXELFORMAT gpu_format = getGpuPixelFormat(format);
            return LINK_adreno_isUBWCSupportedByGpu(gpu_format);
        }
    }
    return 0;
}

ADRENOPIXELFORMAT AdrenoMemInfo::getGpuPixelFormat(int hal_format)
{
    switch (hal_format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            return ADRENO_PIXELFORMAT_R8G8B8A8;
        case HAL_PIXEL_FORMAT_RGBX_8888:
            return ADRENO_PIXELFORMAT_R8G8B8X8;
        case HAL_PIXEL_FORMAT_RGB_565:
            return ADRENO_PIXELFORMAT_B5G6R5;
        case HAL_PIXEL_FORMAT_BGR_565:
            return ADRENO_PIXELFORMAT_R5G6B5;
        case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
            return ADRENO_PIXELFORMAT_NV12;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
            return ADRENO_PIXELFORMAT_NV12_EXT;
        case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
            return ADRENO_PIXELFORMAT_TP10;
        case HAL_PIXEL_FORMAT_YCbCr_420_P010:
        case HAL_PIXEL_FORMAT_YCbCr_420_P010_UBWC:
            return ADRENO_PIXELFORMAT_P010;
        case HAL_PIXEL_FORMAT_RGBA_1010102:
            return ADRENO_PIXELFORMAT_R10G10B10A2_UNORM;
        case HAL_PIXEL_FORMAT_RGBX_1010102:
            return ADRENO_PIXELFORMAT_R10G10B10X2_UNORM;
        case HAL_PIXEL_FORMAT_ABGR_2101010:
            return ADRENO_PIXELFORMAT_A2B10G10R10_UNORM;
        default:
            ALOGE("%s: No map for format: 0x%x", __FUNCTION__, hal_format);
            break;
    }
    return ADRENO_PIXELFORMAT_UNKNOWN;
}

//-------------- IAllocController-----------------------//
IAllocController* IAllocController::sController = NULL;
IAllocController* IAllocController::getInstance(void)
{
    if(sController == NULL) {
        sController = new IonController();
    }
    return sController;
}


//-------------- IonController-----------------------//
IonController::IonController()
{
    allocateIonMem();

    char property[PROPERTY_VALUE_MAX];
    property_get("video.disable.ubwc", property, "0");
    mDisableUBWCForEncode = atoi(property);
}

void IonController::allocateIonMem()
{
   mIonAlloc = new IonAlloc();
}

int IonController::allocate(alloc_data& data, int usage)
{
    int ionFlags = 0;
    int ionHeapId = 0;
    int ret;

    data.uncached = useUncached(usage);
    data.allocType = 0;

    if(usage & GRALLOC_USAGE_PROTECTED) {
        if (usage & GRALLOC_USAGE_PRIVATE_SECURE_DISPLAY) {
            ionHeapId = ION_HEAP(SD_HEAP_ID);
            /*
             * There is currently no flag in ION for Secure Display
             * VM. Please add it to the define once available.
             */
            ionFlags |= ION_SD_FLAGS;
        } else if (usage & GRALLOC_USAGE_HW_CAMERA_MASK) {
            ionHeapId = ION_HEAP(SD_HEAP_ID);
            ionFlags |= (usage & GRALLOC_USAGE_HW_COMPOSER) ? ION_SC_PREVIEW_FLAGS : ION_SC_FLAGS;
        } else {
            ionHeapId = ION_HEAP(CP_HEAP_ID);
            ionFlags |= ION_CP_FLAGS;
        }
    } else if(usage & GRALLOC_USAGE_PRIVATE_MM_HEAP) {
        //MM Heap is exclusively a secure heap.
        //If it is used for non secure cases, fallback to IOMMU heap
        ALOGW("GRALLOC_USAGE_PRIVATE_MM_HEAP \
                                cannot be used as an insecure heap!\
                                trying to use system heap instead !!");
        ionHeapId |= ION_HEAP(ION_SYSTEM_HEAP_ID);
    }

    if(usage & GRALLOC_USAGE_PRIVATE_CAMERA_HEAP)
        ionHeapId |= ION_HEAP(ION_CAMERA_HEAP_ID);

    if(usage & GRALLOC_USAGE_PRIVATE_ADSP_HEAP)
        ionHeapId |= ION_HEAP(ION_ADSP_HEAP_ID);

    if(ionFlags & ION_SECURE)
         data.allocType |= private_handle_t::PRIV_FLAGS_SECURE_BUFFER;

    // if no ion heap flags are set, default to system heap
    if(!ionHeapId)
        ionHeapId = ION_HEAP(ION_SYSTEM_HEAP_ID);

    //At this point we should have the right heap set, there is no fallback
    data.flags = ionFlags;
    data.heapId = ionHeapId;
    ret = mIonAlloc->alloc_buffer(data);

    if(ret >= 0 ) {
        data.allocType |= private_handle_t::PRIV_FLAGS_USES_ION;
    } else {
        ALOGE("%s: Failed to allocate buffer - heap: 0x%x flags: 0x%x",
                __FUNCTION__, ionHeapId, ionFlags);
    }

    return ret;
}

IMemAlloc* IonController::getAllocator(int flags)
{
    IMemAlloc* memalloc = NULL;
    if (flags & private_handle_t::PRIV_FLAGS_USES_ION) {
        memalloc = mIonAlloc;
    } else {
        ALOGE("%s: Invalid flags passed: 0x%x", __FUNCTION__, flags);
    }

    return memalloc;
}

// helper function
unsigned int getSize(int format, int width, int height, int usage,
        const int alignedw, const int alignedh) {

    if (isUBwcEnabled(format, usage)) {
        return getUBwcSize(width, height, format, alignedw, alignedh);
    }

    unsigned int size = 0;
    switch (format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
        case HAL_PIXEL_FORMAT_RGBA_1010102:
        case HAL_PIXEL_FORMAT_ARGB_2101010:
        case HAL_PIXEL_FORMAT_RGBX_1010102:
        case HAL_PIXEL_FORMAT_XRGB_2101010:
        case HAL_PIXEL_FORMAT_BGRA_1010102:
        case HAL_PIXEL_FORMAT_ABGR_2101010:
        case HAL_PIXEL_FORMAT_BGRX_1010102:
        case HAL_PIXEL_FORMAT_XBGR_2101010:
            size = alignedw * alignedh * 4;
            break;
        case HAL_PIXEL_FORMAT_RGB_888:
            size = alignedw * alignedh * 3;
            break;
        case HAL_PIXEL_FORMAT_RGB_565:
        case HAL_PIXEL_FORMAT_BGR_565:
        case HAL_PIXEL_FORMAT_RGBA_5551:
        case HAL_PIXEL_FORMAT_RGBA_4444:
        case HAL_PIXEL_FORMAT_RAW16:
        case HAL_PIXEL_FORMAT_Y16:
            size = alignedw * alignedh * 2;
            break;
        case HAL_PIXEL_FORMAT_RAW12:
            size = ALIGN(alignedw * alignedh, 4096);
            break;
        case HAL_PIXEL_FORMAT_RAW10:
            size = ALIGN(alignedw * alignedh, 4096);
            break;
        case HAL_PIXEL_FORMAT_RAW8:
        case HAL_PIXEL_FORMAT_Y8:
            size = alignedw * alignedh;
            break;
            // adreno formats
        case HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO:  // NV21
            size  = ALIGN(alignedw*alignedh, 4096);
            size += ALIGN(2 * ALIGN(width/2, 32) * ALIGN(height/2, 32), 4096);
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:   // NV12
            // The chroma plane is subsampled,
            // but the pitch in bytes is unchanged
            // The GPU needs 4K alignment, but the video decoder needs 8K
            size  = ALIGN( alignedw * alignedh, 8192);
            size += ALIGN( alignedw * ALIGN(height/2, 32), 8192);
            break;
        case HAL_PIXEL_FORMAT_YV12:
            if ((format == HAL_PIXEL_FORMAT_YV12) && ((width&1) || (height&1))) {
                ALOGE("w or h is odd for the YV12 format");
                return 0;
            }
            size = alignedw*alignedh +
                    (ALIGN(alignedw/2, 16) * (alignedh/2))*2;
            size = ALIGN(size, (unsigned int)4096);
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            size = ALIGN((alignedw*alignedh) + (alignedw* alignedh)/2 + 1, 4096);
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_P010:
            size = ALIGN((alignedw * alignedh * 2) + (alignedw * alignedh) + 1, 4096);
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
        case HAL_PIXEL_FORMAT_YCrCb_422_SP:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
        case HAL_PIXEL_FORMAT_YCrCb_422_I:
        case HAL_PIXEL_FORMAT_CbYCrY_422_I:
            if(width & 1) {
                ALOGE("width is odd for the YUV422_SP format");
                return 0;
            }
            size = ALIGN(alignedw * alignedh * 2, 4096);
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
        case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
            size = VENUS_BUFFER_SIZE(COLOR_FMT_NV12, width, height);
            break;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS:
            size = VENUS_BUFFER_SIZE(COLOR_FMT_NV21, width, height);
            break;
        case HAL_PIXEL_FORMAT_BLOB:
        case HAL_PIXEL_FORMAT_RAW_OPAQUE:
            if(height != 1) {
                ALOGE("%s: Buffers with format HAL_PIXEL_FORMAT_BLOB \
                      must have height==1 ", __FUNCTION__);
                return 0;
            }
            size = width;
            break;
        case HAL_PIXEL_FORMAT_NV21_ZSL:
            size = ALIGN((alignedw*alignedh) + (alignedw* alignedh)/2, 4096);
            break;
        case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_4x4_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_5x4_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_5x5_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_6x5_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_6x6_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_8x5_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_8x6_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_8x8_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x5_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x6_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x8_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x10_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_12x10_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_12x12_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR:
        case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR:
            size = alignedw * alignedh * ASTC_BLOCK_SIZE;
            break;
        default:
            ALOGE("%s: Unrecognized pixel format: 0x%x", __FUNCTION__, format);
            return 0;
    }
    return size;
}

unsigned int getBufferSizeAndDimensions(int width, int height, int format,
        int& alignedw, int &alignedh)
{
    unsigned int size;

    AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(width,
            height,
            format,
            0,
            alignedw,
            alignedh);

    size = getSize(format, width, height, 0 /* usage */, alignedw, alignedh);

    return size;
}


unsigned int getBufferSizeAndDimensions(int width, int height, int format,
        int usage, int& alignedw, int &alignedh)
{
    unsigned int size;

    AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(width,
            height,
            format,
            usage,
            alignedw,
            alignedh);

    size = getSize(format, width, height, usage, alignedw, alignedh);

    return size;
}

void getYuvUbwcSPPlaneInfo(uint64_t base, int width, int height,
                           int color_format, struct android_ycbcr* ycbcr)
{
    // UBWC buffer has these 4 planes in the following sequence:
    // Y_Meta_Plane, Y_Plane, UV_Meta_Plane, UV_Plane
    unsigned int y_meta_stride, y_meta_height, y_meta_size;
    unsigned int y_stride, y_height, y_size;
    unsigned int c_meta_stride, c_meta_height, c_meta_size;
    unsigned int alignment = 4096;

    y_meta_stride = VENUS_Y_META_STRIDE(color_format, width);
    y_meta_height = VENUS_Y_META_SCANLINES(color_format, height);
    y_meta_size = ALIGN((y_meta_stride * y_meta_height), alignment);

    y_stride = VENUS_Y_STRIDE(color_format, width);
    y_height = VENUS_Y_SCANLINES(color_format, height);
    y_size = ALIGN((y_stride * y_height), alignment);

    c_meta_stride = VENUS_UV_META_STRIDE(color_format, width);
    c_meta_height = VENUS_UV_META_SCANLINES(color_format, height);
    c_meta_size = ALIGN((c_meta_stride * c_meta_height), alignment);

    ycbcr->y  = (void*)(base + y_meta_size);
    ycbcr->cb = (void*)(base + y_meta_size + y_size + c_meta_size);
    ycbcr->cr = (void*)(base + y_meta_size + y_size +
                        c_meta_size + 1);
    ycbcr->ystride = y_stride;
    ycbcr->cstride = VENUS_UV_STRIDE(color_format, width);
}

void getYuvSPPlaneInfo(uint64_t base, int width, int height, int bpp,
                       struct android_ycbcr* ycbcr)
{
    unsigned int ystride, cstride;

    ystride = cstride = width * bpp;
    ycbcr->y  = (void*)base;
    ycbcr->cb = (void*)(base + ystride * height);
    ycbcr->cr = (void*)(base + ystride * height + 1);
    ycbcr->ystride = ystride;
    ycbcr->cstride = cstride;
    ycbcr->chroma_step = 2 * bpp;
}

int getYUVPlaneInfo(private_handle_t* hnd, struct android_ycbcr* ycbcr)
{
    int err = 0;
    int width = hnd->width;
    int height = hnd->height;
    int format = hnd->format;

    unsigned int ystride, cstride;

    memset(ycbcr->reserved, 0, sizeof(ycbcr->reserved));
    MetaData_t *metadata = (MetaData_t *)hnd->base_metadata;

    // Check if UBWC buffer has been rendered in linear format.
    if (metadata && (metadata->operation & LINEAR_FORMAT)) {
        format = metadata->linearFormat;
    }

    // Check metadata if the geometry has been updated.
    if(metadata && metadata->operation & UPDATE_BUFFER_GEOMETRY) {
        int usage = 0;

        if (hnd->flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) {
            usage = GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
        }

        AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(metadata->bufferDim.sliceWidth,
                   metadata->bufferDim.sliceHeight, format, usage, width, height);
    }

    // Get the chroma offsets from the handle width/height. We take advantage
    // of the fact the width _is_ the stride
    switch (format) {
        //Semiplanar
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
        case HAL_PIXEL_FORMAT_NV12_ENCODEABLE: //Same as YCbCr_420_SP_VENUS
            getYuvSPPlaneInfo(hnd->base, width, height, 1, ycbcr);
        break;

        case HAL_PIXEL_FORMAT_YCbCr_420_P010:
            getYuvSPPlaneInfo(hnd->base, width, height, 2, ycbcr);
        break;

        case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
            getYuvUbwcSPPlaneInfo(hnd->base, width, height,
                                  COLOR_FMT_NV12_UBWC, ycbcr);
            ycbcr->chroma_step = 2;
        break;

        case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
            getYuvUbwcSPPlaneInfo(hnd->base, width, height,
                                  COLOR_FMT_NV12_BPP10_UBWC, ycbcr);
            ycbcr->chroma_step = 3;
        break;
        case HAL_PIXEL_FORMAT_YCbCr_420_P010_UBWC:
            getYuvUbwcSPPlaneInfo(hnd->base, width, height,
                                  COLOR_FMT_P010_UBWC, ycbcr);
            ycbcr->chroma_step = 4;
        break;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        case HAL_PIXEL_FORMAT_YCrCb_422_SP:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS:
        case HAL_PIXEL_FORMAT_NV21_ZSL:
        case HAL_PIXEL_FORMAT_RAW16:
        case HAL_PIXEL_FORMAT_Y16:
        case HAL_PIXEL_FORMAT_RAW12:
        case HAL_PIXEL_FORMAT_RAW10:
        case HAL_PIXEL_FORMAT_RAW8:
        case HAL_PIXEL_FORMAT_Y8:
            getYuvSPPlaneInfo(hnd->base, width, height, 1, ycbcr);
            std::swap(ycbcr->cb, ycbcr->cr);
        break;

        //Planar
        case HAL_PIXEL_FORMAT_YV12:
            ystride = width;
            cstride = ALIGN(width/2, 16);
            ycbcr->y  = (void*)hnd->base;
            ycbcr->cr = (void*)(hnd->base + ystride * height);
            ycbcr->cb = (void*)(hnd->base + ystride * height +
                    cstride * height/2);
            ycbcr->ystride = ystride;
            ycbcr->cstride = cstride;
            ycbcr->chroma_step = 1;
        break;
        case HAL_PIXEL_FORMAT_CbYCrY_422_I:
            ystride = width * 2;
            cstride = 0;
            ycbcr->y  = (void*)hnd->base;
            ycbcr->cr = NULL;
            ycbcr->cb = NULL;
            ycbcr->ystride = ystride;
            ycbcr->cstride = 0;
            ycbcr->chroma_step = 0;
        break;
        //Unsupported formats
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
        case HAL_PIXEL_FORMAT_YCrCb_422_I:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:
        default:
        ALOGD("%s: Invalid format passed: 0x%x", __FUNCTION__, format);
        err = -EINVAL;
    }
    return err;

}



// Allocate buffer from width, height and format into a
// private_handle_t. It is the responsibility of the caller
// to free the buffer using the free_buffer function
int alloc_buffer(private_handle_t **pHnd, int w, int h, int format, int usage)
{
    alloc_data data;
    int alignedw, alignedh;
    gralloc::IAllocController* sAlloc =
        gralloc::IAllocController::getInstance();
    data.base = 0;
    data.fd = -1;
    data.offset = 0;
    data.size = getBufferSizeAndDimensions(w, h, format, usage, alignedw,
                                            alignedh);

    data.align = getpagesize();
    data.uncached = useUncached(usage);
    int allocFlags = usage;

    int err = sAlloc->allocate(data, allocFlags);
    if (0 != err) {
        ALOGE("%s: allocate failed", __FUNCTION__);
        return -ENOMEM;
    }

    if(isUBwcEnabled(format, usage)) {
      data.allocType |= private_handle_t::PRIV_FLAGS_UBWC_ALIGNED;
    }

    private_handle_t* hnd = new private_handle_t(data.fd, data.size,
                                                 data.allocType, 0, format,
                                                 alignedw, alignedh, -1, 0, 0, w, h);
    hnd->base = (uint64_t) data.base;
    hnd->offset = data.offset;
    hnd->gpuaddr = 0;
    *pHnd = hnd;
    return 0;
}

void free_buffer(private_handle_t *hnd)
{
    gralloc::IAllocController* sAlloc =
        gralloc::IAllocController::getInstance();
    if (hnd && hnd->fd > 0) {
        IMemAlloc* memalloc = sAlloc->getAllocator(hnd->flags);
        memalloc->free_buffer((void*)hnd->base, hnd->size, hnd->offset, hnd->fd);
    }
    if(hnd)
        delete hnd;

}

// UBWC helper functions
static bool isUBwcFormat(int format)
{
    // Explicitly defined UBWC formats
    switch(format)
    {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
        case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
        case HAL_PIXEL_FORMAT_YCbCr_420_P010_UBWC:
            return true;
        default:
            return false;
    }
}

static bool isUBwcSupported(int format)
{
    if (MDPCapabilityInfo::getInstance().isUBwcSupportedByMDP()) {
        // Existing HAL formats with UBWC support
        switch(format)
        {
            case HAL_PIXEL_FORMAT_BGR_565:
            case HAL_PIXEL_FORMAT_RGBA_8888:
            case HAL_PIXEL_FORMAT_RGBX_8888:
            case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
            case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
            case HAL_PIXEL_FORMAT_RGBA_1010102:
            case HAL_PIXEL_FORMAT_RGBX_1010102:
                return true;
            default:
                break;
        }
    }
    return false;
}

bool isUBwcEnabled(int format, int usage)
{
    // Allow UBWC, if client is using an explicitly defined UBWC pixel format.
    if (isUBwcFormat(format))
        return true;

    if ((usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) &&
        gralloc::IAllocController::getInstance()->isDisableUBWCForEncoder()) {
            return false;
    }

    // Allow UBWC, if an OpenGL client sets UBWC usage flag and GPU plus MDP
    // support the format. OR if a non-OpenGL client like Rotator, sets UBWC
    // usage flag and MDP supports the format.
    if ((usage & GRALLOC_USAGE_PRIVATE_ALLOC_UBWC) && isUBwcSupported(format)) {
        bool enable = true;
        // Query GPU for UBWC only if buffer is intended to be used by GPU.
        if (usage & (GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_RENDER)) {
            enable = AdrenoMemInfo::getInstance().isUBWCSupportedByGPU(format);
        }
        // Allow UBWC, only if CPU usage flags are not set
        if (enable && !(usage & (GRALLOC_USAGE_SW_READ_MASK |
            GRALLOC_USAGE_SW_WRITE_MASK))) {
            return true;
        }
    }
    return false;
}

static void getYuvUBwcWidthHeight(int width, int height, int format,
        int& aligned_w, int& aligned_h)
{
    switch (format)
    {
        case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
            aligned_w = VENUS_Y_STRIDE(COLOR_FMT_NV12_UBWC, width);
            aligned_h = VENUS_Y_SCANLINES(COLOR_FMT_NV12_UBWC, height);
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
            // The macro returns the stride which is 4/3 times the width, hence * 3/4
            aligned_w = (VENUS_Y_STRIDE(COLOR_FMT_NV12_BPP10_UBWC, width) * 3) / 4;
            aligned_h = VENUS_Y_SCANLINES(COLOR_FMT_NV12_BPP10_UBWC, height);
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_P010_UBWC:
            // The macro returns the stride which is 2 times the width, hence / 2
            aligned_w = (VENUS_Y_STRIDE(COLOR_FMT_P010_UBWC, width) / 2);
            aligned_h = VENUS_Y_SCANLINES(COLOR_FMT_P010_UBWC, height);
            break;
        default:
            ALOGE("%s: Unsupported pixel format: 0x%x", __FUNCTION__, format);
            aligned_w = 0;
            aligned_h = 0;
            break;
    }
}

static void getRgbUBwcBlockSize(int bpp, int& block_width, int& block_height)
{
    block_width = 0;
    block_height = 0;

    switch(bpp)
    {
         case 2:
         case 4:
             block_width = 16;
             block_height = 4;
             break;
         case 8:
             block_width = 8;
             block_height = 4;
             break;
         case 16:
             block_width = 4;
             block_height = 4;
             break;
         default:
             ALOGE("%s: Unsupported bpp: %d", __FUNCTION__, bpp);
             break;
    }
}

static unsigned int getRgbUBwcMetaBufferSize(int width, int height, int bpp)
{
    unsigned int size = 0;
    int meta_width, meta_height;
    int block_width, block_height;

    getRgbUBwcBlockSize(bpp, block_width, block_height);

    if (!block_width || !block_height) {
        ALOGE("%s: Unsupported bpp: %d", __FUNCTION__, bpp);
        return size;
    }

    // Align meta buffer height to 16 blocks
    meta_height = ALIGN(((height + block_height - 1) / block_height), 16);

    // Align meta buffer width to 64 blocks
    meta_width = ALIGN(((width + block_width - 1) / block_width), 64);

    // Align meta buffer size to 4K
    size = ALIGN((meta_width * meta_height), 4096);
    return size;
}

static unsigned int getUBwcSize(int width, int height, int format,
        const int alignedw, const int alignedh) {

    unsigned int size = 0;
    switch (format) {
        case HAL_PIXEL_FORMAT_BGR_565:
            size = alignedw * alignedh * 2;
            size += getRgbUBwcMetaBufferSize(width, height, 2);
            break;
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_RGBA_1010102:
        case HAL_PIXEL_FORMAT_RGBX_1010102:
            size = alignedw * alignedh * 4;
            size += getRgbUBwcMetaBufferSize(width, height, 4);
            break;
        case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
            size = VENUS_BUFFER_SIZE(COLOR_FMT_NV12_UBWC, width, height);
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
            size = VENUS_BUFFER_SIZE(COLOR_FMT_NV12_BPP10_UBWC, width, height);
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_P010_UBWC:
            size = VENUS_BUFFER_SIZE(COLOR_FMT_P010_UBWC, width, height);
            break;
        default:
            ALOGE("%s: Unsupported pixel format: 0x%x", __FUNCTION__, format);
            break;
    }
    return size;
}

int getRgbDataAddress(private_handle_t* hnd, void** rgb_data)
{
    int err = 0;

    // This api is for RGB* formats
    if (!isUncompressedRgbFormat(hnd->format)) {
        return -EINVAL;
    }

    // linear buffer
    if (!(hnd->flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED)) {
        *rgb_data = (void*)hnd->base;
        return err;
    }

    // Ubwc buffers
    unsigned int meta_size = 0;
    switch (hnd->format) {
        case HAL_PIXEL_FORMAT_BGR_565:
            meta_size = getRgbUBwcMetaBufferSize(hnd->width, hnd->height, 2);
            break;
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_RGBA_1010102:
        case HAL_PIXEL_FORMAT_RGBX_1010102:
            meta_size = getRgbUBwcMetaBufferSize(hnd->width, hnd->height, 4);
            break;
        default:
            ALOGE("%s:Unsupported RGB format: 0x%x", __FUNCTION__, hnd->format);
            err = -EINVAL;
            break;
    }

    *rgb_data = (void*)(hnd->base + meta_size);
    return err;
}

int getBufferLayout(private_handle_t *hnd, uint32_t stride[4],
        uint32_t offset[4], uint32_t *num_planes) {
    if (!hnd || !stride || !offset || !num_planes) {
        return -EINVAL;
    }

    struct android_ycbcr yuvInfo = {};
    *num_planes = 1;
    stride[0] = 0;

    switch (hnd->format) {
        case HAL_PIXEL_FORMAT_RGB_565:
        case HAL_PIXEL_FORMAT_BGR_565:
        case HAL_PIXEL_FORMAT_RGBA_5551:
        case HAL_PIXEL_FORMAT_RGBA_4444:
            stride[0] = hnd->width * 2;
            break;
        case HAL_PIXEL_FORMAT_RGB_888:
            stride[0] = hnd->width * 3;
            break;
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_BGRX_8888:
        case HAL_PIXEL_FORMAT_RGBA_1010102:
        case HAL_PIXEL_FORMAT_ARGB_2101010:
        case HAL_PIXEL_FORMAT_RGBX_1010102:
        case HAL_PIXEL_FORMAT_XRGB_2101010:
        case HAL_PIXEL_FORMAT_BGRA_1010102:
        case HAL_PIXEL_FORMAT_ABGR_2101010:
        case HAL_PIXEL_FORMAT_BGRX_1010102:
        case HAL_PIXEL_FORMAT_XBGR_2101010:
            stride[0] = hnd->width * 4;
            break;
    }

    // Format is RGB
    if (stride[0]) {
        return 0;
    }

    (*num_planes)++;
    int ret = getYUVPlaneInfo(hnd, &yuvInfo);
    if (ret < 0) {
        ALOGE("%s failed", __FUNCTION__);
        return ret;
    }

    stride[0] = static_cast<uint32_t>(yuvInfo.ystride);
    offset[0] = static_cast<uint32_t>(
                    reinterpret_cast<uint64_t>(yuvInfo.y) - hnd->base);
    stride[1] = static_cast<uint32_t>(yuvInfo.cstride);
    switch (hnd->format) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
        case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
        case HAL_PIXEL_FORMAT_YCbCr_420_P010:
        case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
        case HAL_PIXEL_FORMAT_YCbCr_420_P010_UBWC:
            offset[1] = static_cast<uint32_t>(
                    reinterpret_cast<uint64_t>(yuvInfo.cb) - hnd->base);
            break;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS:
        case HAL_PIXEL_FORMAT_YCrCb_422_SP:
            offset[1] = static_cast<uint32_t>(
                    reinterpret_cast<uint64_t>(yuvInfo.cr) - hnd->base);
            break;
        case HAL_PIXEL_FORMAT_YV12:
            offset[1] = static_cast<uint32_t>(
                    reinterpret_cast<uint64_t>(yuvInfo.cr) - hnd->base);
            stride[2] = static_cast<uint32_t>(yuvInfo.cstride);
            offset[2] = static_cast<uint32_t>(
                    reinterpret_cast<uint64_t>(yuvInfo.cb) - hnd->base);
            (*num_planes)++;
            break;
        default:
            ALOGW("%s: Unsupported format %s", __FUNCTION__,
                    qdutils::GetHALPixelFormatString(hnd->format));
            ret = -EINVAL;
    }

    if (hnd->flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) {
        std::fill(offset, offset + 4, 0);
    }

    return 0;
}
