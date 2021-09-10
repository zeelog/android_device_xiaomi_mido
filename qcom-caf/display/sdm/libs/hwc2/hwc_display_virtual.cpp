/*
* Copyright (c) 2014-2018, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
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

#include <utils/constants.h>
#include <utils/debug.h>
#include <sync/sync.h>
#include <stdarg.h>
#ifndef USE_GRALLOC1
#include <gr.h>
#endif

#include "hwc_display_virtual.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCDisplayVirtual"

namespace sdm {

int HWCDisplayVirtual::Create(CoreInterface *core_intf, HWCBufferAllocator *buffer_allocator,
                              HWCCallbacks *callbacks, uint32_t width,
                              uint32_t height, int32_t *format, HWCDisplay **hwc_display) {
  int status = 0;
  HWCDisplayVirtual *hwc_display_virtual = new HWCDisplayVirtual(core_intf, buffer_allocator,
                                                                 callbacks);

  // TODO(user): Populate format correctly
  DLOGI("Creating virtual display: w: %d h:%d format:0x%x", width, height, *format);

  status = hwc_display_virtual->Init();
  if (status) {
    DLOGW("Failed to initialize virtual display");
    delete hwc_display_virtual;
    return status;
  }

  status = INT32(hwc_display_virtual->SetPowerMode(HWC2::PowerMode::On));
  if (status) {
    DLOGW("Failed to set power mode on virtual display");
    Destroy(hwc_display_virtual);
    return status;
  }

  status = hwc_display_virtual->SetConfig(width, height);
  if (status) {
    Destroy(hwc_display_virtual);
    return status;
  }

  // TODO(user): Validate that we support this width/height
  status = hwc_display_virtual->SetFrameBufferResolution(width, height);

  if (status) {
    DLOGW("Failed to set virtual display FB resolution");
    Destroy(hwc_display_virtual);
    return status;
  }

  *hwc_display = static_cast<HWCDisplay *>(hwc_display_virtual);

  return 0;
}

void HWCDisplayVirtual::Destroy(HWCDisplay *hwc_display) {
  hwc_display->Deinit();
  delete hwc_display;
}

HWCDisplayVirtual::HWCDisplayVirtual(CoreInterface *core_intf, HWCBufferAllocator *buffer_allocator,
                                     HWCCallbacks *callbacks)
    : HWCDisplay(core_intf, callbacks, kVirtual, HWC_DISPLAY_VIRTUAL, false, NULL,
                 DISPLAY_CLASS_VIRTUAL, buffer_allocator) {
}

int HWCDisplayVirtual::Init() {
  output_buffer_ = new LayerBuffer();
  return HWCDisplay::Init();
}

int HWCDisplayVirtual::Deinit() {
  int status = 0;
  if (output_buffer_) {
    if (output_buffer_->acquire_fence_fd >= 0) {
      close(output_buffer_->acquire_fence_fd);
      output_buffer_->acquire_fence_fd = -1;
    }
    delete output_buffer_;
    output_buffer_ = nullptr;
  }
  status = HWCDisplay::Deinit();

  return status;
}

HWC2::Error HWCDisplayVirtual::Validate(uint32_t *out_num_types, uint32_t *out_num_requests) {
  auto status = HWC2::Error::None;

  if (display_paused_) {
    MarkLayersForGPUBypass();
    return status;
  }

  BuildLayerStack();
  layer_stack_.output_buffer = output_buffer_;

  if (layer_set_.empty()) {
    DLOGI("Skipping Validate and Commit");
    return status;
  }
  status = PrepareLayerStack(out_num_types, out_num_requests);
  return status;
}

HWC2::Error HWCDisplayVirtual::Present(int32_t *out_retire_fence) {
  auto status = HWC2::Error::None;
  if (display_paused_) {
    DisplayError error = display_intf_->Flush(false);
    if (error != kErrorNone) {
      DLOGE("Flush failed. Error = %d", error);
    }
  } else {
    status = HWCDisplay::CommitLayerStack();
    if (status == HWC2::Error::None) {
      if (dump_frame_count_ && !flush_ && dump_output_layer_) {
        if (output_handle_) {
          BufferInfo buffer_info;
          const private_handle_t *output_handle =
              reinterpret_cast<const private_handle_t *>(output_buffer_->buffer_id);
          DisplayError error = kErrorNone;
          if (!output_handle->base) {
            error = buffer_allocator_->MapBuffer(output_handle, -1);
            if (error != kErrorNone) {
              DLOGE("Failed to map output buffer, error = %d", error);
              return HWC2::Error::BadParameter;
            }
          }
          buffer_info.buffer_config.width = static_cast<uint32_t>(output_handle->width);
          buffer_info.buffer_config.height = static_cast<uint32_t>(output_handle->height);
          buffer_info.buffer_config.format =
              GetSDMFormat(output_handle->format, output_handle->flags);
          buffer_info.alloc_buffer_info.size = static_cast<uint32_t>(output_handle->size);
          DumpOutputBuffer(buffer_info, reinterpret_cast<void *>(output_handle->base),
                           layer_stack_.retire_fence_fd);

          int release_fence = -1;
          error = buffer_allocator_->UnmapBuffer(output_handle, &release_fence);
          if (error != kErrorNone) {
            DLOGE("Failed to unmap buffer, error = %d", error);
            return HWC2::Error::BadParameter;
          }
        }
      }

      status = HWCDisplay::PostCommitLayerStack(out_retire_fence);
    }
  }
  CloseAcquireFds();
  if (output_buffer_->acquire_fence_fd >= 0) {
    close(output_buffer_->acquire_fence_fd);
    output_buffer_->acquire_fence_fd = -1;
  }
  return status;
}

int HWCDisplayVirtual::SetConfig(uint32_t width, uint32_t height) {
  DisplayConfigVariableInfo variable_info;
  variable_info.x_pixels = width;
  variable_info.y_pixels = height;
  // TODO(user): Need to get the framerate of primary display and update it.
  variable_info.fps = 60;
  DisplayError err = display_intf_->SetActiveConfig(&variable_info);
  if (err != kErrorNone) {
    return -EINVAL;
  }
  return 0;
}

HWC2::Error HWCDisplayVirtual::SetOutputBuffer(buffer_handle_t buf, int32_t release_fence) {
  if (buf == nullptr || release_fence == 0) {
    return HWC2::Error::BadParameter;
  }
  const private_handle_t *output_handle = static_cast<const private_handle_t *>(buf);

  // Fill output buffer parameters (width, height, format, plane information, fence)
  output_buffer_->acquire_fence_fd = dup(release_fence);

  if (output_handle) {
    int output_handle_format = output_handle->format;
    int active_aligned_w, active_aligned_h;
    int new_width, new_height;
    int new_aligned_w, new_aligned_h;
    uint32_t active_width, active_height;
    ColorMetaData color_metadata = {};

    if (output_handle_format == HAL_PIXEL_FORMAT_RGBA_8888) {
      output_handle_format = HAL_PIXEL_FORMAT_RGBX_8888;
    }

    LayerBufferFormat new_sdm_format = GetSDMFormat(output_handle_format, output_handle->flags);
    if (new_sdm_format == kFormatInvalid) {
      return HWC2::Error::BadParameter;
    }

    if (sdm::SetCSC(output_handle, &color_metadata) != kErrorNone) {
      return HWC2::Error::BadParameter;
    }

    GetMixerResolution(&active_width, &active_height);
    buffer_allocator_->GetCustomWidthAndHeight(output_handle, &new_width, &new_height);
    buffer_allocator_->GetAlignedWidthAndHeight(INT(new_width), INT(new_height),
                                                output_handle_format, 0, &new_aligned_w,
                                                &new_aligned_h);
    buffer_allocator_->GetAlignedWidthAndHeight(INT(active_width), INT(active_height),
                                                output_handle_format, 0, &active_aligned_w,
                                                &active_aligned_h);
    if (new_aligned_w != active_aligned_w  || new_aligned_h != active_aligned_h) {
      int status = SetConfig(UINT32(new_width), UINT32(new_height));
      if (status) {
        DLOGE("SetConfig failed custom WxH %dx%d", new_width, new_height);
        return HWC2::Error::BadParameter;
      }
      validated_ = false;
    }

    output_buffer_->width = UINT32(new_aligned_w);
    output_buffer_->height = UINT32(new_aligned_h);
    output_buffer_->unaligned_width = UINT32(new_width);
    output_buffer_->unaligned_height = UINT32(new_height);
    output_buffer_->flags.secure = 0;
    output_buffer_->flags.video = 0;
    output_buffer_->buffer_id = reinterpret_cast<uint64_t>(output_handle);
    output_buffer_->format = new_sdm_format;
    output_buffer_->color_metadata = color_metadata;
    output_handle_ = output_handle;

    // TZ Protected Buffer - L1
    if (output_handle->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
      output_buffer_->flags.secure = 1;
    }

    // ToDo: Need to extend for non-RGB formats
    output_buffer_->planes[0].fd = output_handle->fd;
    output_buffer_->planes[0].offset = output_handle->offset;
    output_buffer_->planes[0].stride = UINT32(output_handle->width);
  }

  return HWC2::Error::None;
}

void HWCDisplayVirtual::SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type) {
  HWCDisplay::SetFrameDumpConfig(count, bit_mask_layer_type);
  dump_output_layer_ = ((bit_mask_layer_type & (1 << OUTPUT_LAYER_DUMP)) != 0);

  DLOGI("output_layer_dump_enable %d", dump_output_layer_);
}

}  // namespace sdm
