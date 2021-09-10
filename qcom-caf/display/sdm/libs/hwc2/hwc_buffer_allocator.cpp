/*
* Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*  * Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above
*    copyright notice, this list of conditions and the following
*    disclaimer in the documentation and/or other materials provided
*    with the distribution.
*  * Neither the name of The Linux Foundation nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
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

#include <gralloc_priv.h>

#include <core/buffer_allocator.h>
#include <utils/constants.h>
#include <utils/debug.h>

#include "hwc_buffer_allocator.h"
#include "hwc_debugger.h"
#include "gr_utils.h"

#define __CLASS__ "HWCBufferAllocator"

namespace sdm {

DisplayError HWCBufferAllocator::Init() {
  int err = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module_);
  if (err != 0) {
    DLOGE("FATAL: can not get GRALLOC module");
    return kErrorResources;
  }

  err = gralloc1_open(module_, &gralloc_device_);
  if (err != 0) {
    DLOGE("FATAL: can not open GRALLOC device");
    return kErrorResources;
  }

  if (gralloc_device_ == nullptr) {
    DLOGE("FATAL: gralloc device is null");
    return kErrorResources;
  }

  ReleaseBuffer_ = reinterpret_cast<GRALLOC1_PFN_RELEASE>(
      gralloc_device_->getFunction(gralloc_device_, GRALLOC1_FUNCTION_RELEASE));
  Perform_ = reinterpret_cast<GRALLOC1_PFN_PERFORM>(
      gralloc_device_->getFunction(gralloc_device_, GRALLOC1_FUNCTION_PERFORM));
  Lock_ = reinterpret_cast<GRALLOC1_PFN_LOCK>(
      gralloc_device_->getFunction(gralloc_device_, GRALLOC1_FUNCTION_LOCK));
  Unlock_ = reinterpret_cast<GRALLOC1_PFN_UNLOCK>(
      gralloc_device_->getFunction(gralloc_device_, GRALLOC1_FUNCTION_UNLOCK));

  return kErrorNone;
}

DisplayError HWCBufferAllocator::Deinit() {
  if (gralloc_device_ != nullptr) {
    int err = gralloc1_close(gralloc_device_);
    if (err != 0) {
      DLOGE("FATAL: can not close GRALLOC device");
      return kErrorResources;
    }
  }
  return kErrorNone;
}

DisplayError HWCBufferAllocator::AllocateBuffer(BufferInfo *buffer_info) {
  const BufferConfig &buffer_config = buffer_info->buffer_config;
  AllocatedBufferInfo *alloc_buffer_info = &buffer_info->alloc_buffer_info;
  uint32_t width = buffer_config.width;
  uint32_t height = buffer_config.height;
  int format;
  uint64_t alloc_flags = 0;
  int error = SetBufferInfo(buffer_config.format, &format, &alloc_flags);
  if (error != 0) {
    return kErrorParameters;
  }

  if (buffer_config.secure) {
    alloc_flags |= GRALLOC1_PRODUCER_USAGE_PROTECTED;
  }

  if (buffer_config.secure_camera) {
    alloc_flags |= GRALLOC1_PRODUCER_USAGE_CAMERA;
  }

  if (!buffer_config.cache) {
    // Allocate uncached buffers
    alloc_flags |= GRALLOC_USAGE_PRIVATE_UNCACHED;
  }

  if (buffer_config.gfx_client) {
    alloc_flags |= GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE;
  }

  uint64_t producer_usage = alloc_flags;
  uint64_t consumer_usage = (alloc_flags | GRALLOC1_CONSUMER_USAGE_HWCOMPOSER);
  // CreateBuffer
  private_handle_t *hnd = nullptr;
  Perform_(gralloc_device_, GRALLOC1_MODULE_PERFORM_ALLOCATE_BUFFER, width, height, format,
           producer_usage, consumer_usage, &hnd);

  if (hnd) {
    alloc_buffer_info->fd = hnd->fd;
    alloc_buffer_info->stride = UINT32(hnd->width);
    alloc_buffer_info->aligned_width = UINT32(hnd->width);
    alloc_buffer_info->aligned_height = UINT32(hnd->height);
    alloc_buffer_info->size = hnd->size;
  } else {
    DLOGE("Failed to allocate memory");
    return kErrorMemory;
  }

  buffer_info->private_data = reinterpret_cast<void *>(hnd);
  return kErrorNone;
}

DisplayError HWCBufferAllocator::FreeBuffer(BufferInfo *buffer_info) {
  DisplayError err = kErrorNone;
  buffer_handle_t hnd = static_cast<private_handle_t *>(buffer_info->private_data);
  ReleaseBuffer_(gralloc_device_, hnd);
  AllocatedBufferInfo *alloc_buffer_info = &buffer_info->alloc_buffer_info;

  alloc_buffer_info->fd = -1;
  alloc_buffer_info->stride = 0;
  alloc_buffer_info->size = 0;
  buffer_info->private_data = NULL;
  return err;
}

void HWCBufferAllocator::GetCustomWidthAndHeight(const private_handle_t *handle, int *width,
                                                 int *height) {
  Perform_(gralloc_device_, GRALLOC_MODULE_PERFORM_GET_CUSTOM_STRIDE_AND_HEIGHT_FROM_HANDLE, handle,
           width, height);
}

void HWCBufferAllocator::GetAlignedWidthAndHeight(int width, int height, int format,
                                                  uint32_t alloc_type, int *aligned_width,
                                                  int *aligned_height) {
  int tile_enabled;
  gralloc1_producer_usage_t producer_usage = GRALLOC1_PRODUCER_USAGE_NONE;
  gralloc1_consumer_usage_t consumer_usage = GRALLOC1_CONSUMER_USAGE_NONE;
  if (alloc_type & GRALLOC_USAGE_HW_FB) {
    consumer_usage = GRALLOC1_CONSUMER_USAGE_CLIENT_TARGET;
  }
  if (alloc_type & GRALLOC_USAGE_PRIVATE_ALLOC_UBWC) {
    producer_usage = static_cast<gralloc1_producer_usage_t> GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
  }

  Perform_(gralloc_device_, GRALLOC_MODULE_PERFORM_GET_ATTRIBUTES, width, height, format,
           producer_usage, consumer_usage, aligned_width, aligned_height, &tile_enabled);
}

uint32_t HWCBufferAllocator::GetBufferSize(BufferInfo *buffer_info) {
  const BufferConfig &buffer_config = buffer_info->buffer_config;
  uint64_t alloc_flags = GRALLOC_USAGE_PRIVATE_IOMMU_HEAP;

  int width = INT(buffer_config.width);
  int height = INT(buffer_config.height);
  int format;

  if (buffer_config.secure) {
    alloc_flags |= INT(GRALLOC_USAGE_PROTECTED);
  }

  if (!buffer_config.cache) {
    // Allocate uncached buffers
    alloc_flags |= GRALLOC_USAGE_PRIVATE_UNCACHED;
  }

  if (SetBufferInfo(buffer_config.format, &format, &alloc_flags) < 0) {
    return 0;
  }

  uint32_t aligned_width = 0, aligned_height = 0, buffer_size = 0;
  gralloc1_producer_usage_t producer_usage = GRALLOC1_PRODUCER_USAGE_NONE;
  gralloc1_consumer_usage_t consumer_usage = GRALLOC1_CONSUMER_USAGE_NONE;
  // TODO(user): Currently both flags are treated similarly in gralloc
  producer_usage = gralloc1_producer_usage_t(alloc_flags);
  consumer_usage = gralloc1_consumer_usage_t(alloc_flags);
  gralloc1::BufferInfo info(width, height, format, producer_usage, consumer_usage);
  GetBufferSizeAndDimensions(info, &buffer_size, &aligned_width, &aligned_height);
  return buffer_size;
}

int HWCBufferAllocator::SetBufferInfo(LayerBufferFormat format, int *target, uint64_t *flags) {
  switch (format) {
    case kFormatRGBA8888:
      *target = HAL_PIXEL_FORMAT_RGBA_8888;
      break;
    case kFormatRGBX8888:
      *target = HAL_PIXEL_FORMAT_RGBX_8888;
      break;
    case kFormatRGB888:
      *target = HAL_PIXEL_FORMAT_RGB_888;
      break;
    case kFormatRGB565:
      *target = HAL_PIXEL_FORMAT_RGB_565;
      break;
    case kFormatBGR565:
      *target = HAL_PIXEL_FORMAT_BGR_565;
      break;
    case kFormatBGR888:
      *target = HAL_PIXEL_FORMAT_BGR_888;
      break;
    case kFormatBGRA8888:
      *target = HAL_PIXEL_FORMAT_BGRA_8888;
      break;
    case kFormatYCrCb420PlanarStride16:
      *target = HAL_PIXEL_FORMAT_YV12;
      break;
    case kFormatYCrCb420SemiPlanar:
      *target = HAL_PIXEL_FORMAT_YCrCb_420_SP;
      break;
    case kFormatYCbCr420SemiPlanar:
      *target = HAL_PIXEL_FORMAT_YCbCr_420_SP;
      break;
    case kFormatYCbCr422H2V1Packed:
      *target = HAL_PIXEL_FORMAT_YCbCr_422_I;
      break;
    case kFormatCbYCrY422H2V1Packed:
      *target = HAL_PIXEL_FORMAT_CbYCrY_422_I;
      break;
    case kFormatYCbCr422H2V1SemiPlanar:
      *target = HAL_PIXEL_FORMAT_YCbCr_422_SP;
      break;
    case kFormatYCbCr420SemiPlanarVenus:
      *target = HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS;
      break;
    case kFormatYCrCb420SemiPlanarVenus:
      *target = HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS;
      break;
    case kFormatYCbCr420SPVenusUbwc:
      *target = HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC;
      *flags |= GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
      break;
    case kFormatRGBA5551:
      *target = HAL_PIXEL_FORMAT_RGBA_5551;
      break;
    case kFormatRGBA4444:
      *target = HAL_PIXEL_FORMAT_RGBA_4444;
      break;
    case kFormatRGBA1010102:
      *target = HAL_PIXEL_FORMAT_RGBA_1010102;
      break;
    case kFormatARGB2101010:
      *target = HAL_PIXEL_FORMAT_ARGB_2101010;
      break;
    case kFormatRGBX1010102:
      *target = HAL_PIXEL_FORMAT_RGBX_1010102;
      break;
    case kFormatXRGB2101010:
      *target = HAL_PIXEL_FORMAT_XRGB_2101010;
      break;
    case kFormatBGRA1010102:
      *target = HAL_PIXEL_FORMAT_BGRA_1010102;
      break;
    case kFormatABGR2101010:
      *target = HAL_PIXEL_FORMAT_ABGR_2101010;
      break;
    case kFormatBGRX1010102:
      *target = HAL_PIXEL_FORMAT_BGRX_1010102;
      break;
    case kFormatXBGR2101010:
      *target = HAL_PIXEL_FORMAT_XBGR_2101010;
      break;
    case kFormatYCbCr420P010:
      *target = HAL_PIXEL_FORMAT_YCbCr_420_P010;
      break;
    case kFormatYCbCr420TP10Ubwc:
      *target = HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC;
      *flags |= GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
      break;
    case kFormatYCbCr420P010Ubwc:
      *target = HAL_PIXEL_FORMAT_YCbCr_420_P010_UBWC;
      *flags |= GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
      break;
    case kFormatRGBA8888Ubwc:
      *target = HAL_PIXEL_FORMAT_RGBA_8888;
      *flags |= GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
      break;
    case kFormatRGBX8888Ubwc:
      *target = HAL_PIXEL_FORMAT_RGBX_8888;
      *flags |= GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
      break;
    case kFormatBGR565Ubwc:
      *target = HAL_PIXEL_FORMAT_BGR_565;
      *flags |= GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
      break;
    case kFormatRGBA1010102Ubwc:
      *target = HAL_PIXEL_FORMAT_RGBA_1010102;
      *flags |= GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
      break;
    case kFormatRGBX1010102Ubwc:
      *target = HAL_PIXEL_FORMAT_RGBX_1010102;
      *flags |= GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
      break;
    default:
      DLOGE("Unsupported format = 0x%x", format);
      return -EINVAL;
  }
  return 0;
}

DisplayError HWCBufferAllocator::GetAllocatedBufferInfo(
    const BufferConfig &buffer_config, AllocatedBufferInfo *allocated_buffer_info) {
  // TODO(user): This API should pass the buffer_info of the already allocated buffer
  // The private_data can then be typecast to the private_handle and used directly.
  uint64_t alloc_flags = GRALLOC_USAGE_PRIVATE_IOMMU_HEAP;

  int width = INT(buffer_config.width);
  int height = INT(buffer_config.height);
  int format;

  if (buffer_config.secure) {
    alloc_flags |= INT(GRALLOC_USAGE_PROTECTED);
  }

  if (!buffer_config.cache) {
    // Allocate uncached buffers
    alloc_flags |= GRALLOC_USAGE_PRIVATE_UNCACHED;
  }

  if (SetBufferInfo(buffer_config.format, &format, &alloc_flags) < 0) {
    return kErrorParameters;
  }

  uint32_t aligned_width = 0, aligned_height = 0, buffer_size = 0;
  gralloc1_producer_usage_t producer_usage = GRALLOC1_PRODUCER_USAGE_NONE;
  gralloc1_consumer_usage_t consumer_usage = GRALLOC1_CONSUMER_USAGE_NONE;
  // TODO(user): Currently both flags are treated similarly in gralloc
  producer_usage = gralloc1_producer_usage_t(alloc_flags);
  consumer_usage = gralloc1_consumer_usage_t(alloc_flags);
  gralloc1::BufferInfo info(width, height, format, producer_usage, consumer_usage);
  GetBufferSizeAndDimensions(info, &buffer_size, &aligned_width, &aligned_height);
  allocated_buffer_info->stride = UINT32(aligned_width);
  allocated_buffer_info->aligned_width = UINT32(aligned_width);
  allocated_buffer_info->aligned_height = UINT32(aligned_height);
  allocated_buffer_info->size = UINT32(buffer_size);

  return kErrorNone;
}

DisplayError HWCBufferAllocator::GetBufferLayout(const AllocatedBufferInfo &buf_info,
                                                 uint32_t stride[4], uint32_t offset[4],
                                                 uint32_t *num_planes) {
  // TODO(user): Transition APIs to not need a private handle
  private_handle_t hnd(-1, 0, 0, 0, 0, 0, 0);
  int format = HAL_PIXEL_FORMAT_RGBA_8888;
  uint64_t flags = 0;

  SetBufferInfo(buf_info.format, &format, &flags);
  // Setup only the required stuff, skip rest
  hnd.format = format;
  hnd.width = INT32(buf_info.aligned_width);
  hnd.height = INT32(buf_info.aligned_height);
  if (flags & GRALLOC_USAGE_PRIVATE_ALLOC_UBWC) {
    hnd.flags = private_handle_t::PRIV_FLAGS_UBWC_ALIGNED;
  }

  int ret = gralloc1::GetBufferLayout(&hnd, stride, offset, num_planes);
  if (ret < 0) {
    DLOGE("GetBufferLayout failed");
    return kErrorParameters;
  }

  return kErrorNone;
}

DisplayError HWCBufferAllocator::MapBuffer(const private_handle_t *handle, int acquire_fence) {
  void* buffer_ptr = NULL;
  const gralloc1_rect_t accessRegion = {
        .left = 0,
        .top = 0,
        .width = 0,
        .height = 0
  };
  Lock_(gralloc_device_, handle, GRALLOC1_PRODUCER_USAGE_CPU_READ, GRALLOC1_CONSUMER_USAGE_NONE,
        &accessRegion, &buffer_ptr, acquire_fence);
  if (!buffer_ptr) {
    return kErrorUndefined;
  }

  return kErrorNone;
}

DisplayError HWCBufferAllocator::UnmapBuffer(const private_handle_t *handle, int* release_fence) {
  return (DisplayError)(Unlock_(gralloc_device_, handle, release_fence));
}

}  // namespace sdm
