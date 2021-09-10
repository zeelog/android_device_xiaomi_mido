/*
* Copyright (c) 2014 - 2016, The Linux Foundation. All rights reserved.
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
#include <gr.h>

#include "hwc_display_virtual.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCDisplayVirtual"

namespace sdm {

int HWCDisplayVirtual::Create(CoreInterface *core_intf, hwc_procs_t const **hwc_procs,
                              uint32_t primary_width, uint32_t primary_height,
                              hwc_display_contents_1_t *content_list,
                              HWCDisplay **hwc_display) {
  int status = 0;
  HWCDisplayVirtual *hwc_display_virtual = new HWCDisplayVirtual(core_intf, hwc_procs);
  uint32_t virtual_width = 0, virtual_height = 0;

  status = hwc_display_virtual->Init();
  if (status) {
    delete hwc_display_virtual;
    return status;
  }

  status = hwc_display_virtual->SetPowerMode(HWC_POWER_MODE_NORMAL);
  if (status) {
    Destroy(hwc_display_virtual);
    return status;
  }

  // TODO(user): Need to update resolution(and not aligned resolution) on writeback.
  status = hwc_display_virtual->SetOutputSliceFromMetadata(content_list);
  if (status) {
    Destroy(hwc_display_virtual);
    return status;
  }

  hwc_display_virtual->GetMixerResolution(&virtual_width, &virtual_height);

  if (content_list->numHwLayers < 1) {
    Destroy(hwc_display_virtual);
    return -1;
  }

  hwc_layer_1_t &fb_layer = content_list->hwLayers[content_list->numHwLayers-1];
  int fb_width = fb_layer.displayFrame.right - fb_layer.displayFrame.left;
  int fb_height = fb_layer.displayFrame.bottom - fb_layer.displayFrame.top;

  status = hwc_display_virtual->SetFrameBufferResolution(UINT32(fb_width), UINT32(fb_height));

  if (status) {
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

HWCDisplayVirtual::HWCDisplayVirtual(CoreInterface *core_intf, hwc_procs_t const **hwc_procs)
  : HWCDisplay(core_intf, hwc_procs, kVirtual, HWC_DISPLAY_VIRTUAL, false, NULL,
               DISPLAY_CLASS_VIRTUAL) {
}

int HWCDisplayVirtual::Init() {
  output_buffer_ = new LayerBuffer();
  if (!output_buffer_) {
    return -ENOMEM;
  }

  return HWCDisplay::Init();
}

int HWCDisplayVirtual::Deinit() {
  int status = 0;

  status = HWCDisplay::Deinit();
  if (status) {
    return status;
  }

  if (output_buffer_) {
    delete output_buffer_;
    output_buffer_ = NULL;
  }

  return status;
}

int HWCDisplayVirtual::Prepare(hwc_display_contents_1_t *content_list) {
  int status = 0;

  status = SetOutputSliceFromMetadata(content_list);
  if (status) {
    return status;
  }

  if (display_paused_) {
    MarkLayersForGPUBypass(content_list);
    return status;
  }

  status = AllocateLayerStack(content_list);
  if (status) {
    return status;
  }

  status = SetOutputBuffer(content_list);
  if (status) {
    return status;
  }

  status = PrePrepareLayerStack(content_list);
  if (status) {
    return status;
  }

  status = PrepareLayerStack(content_list);
  if (status) {
    return status;
  }

  return 0;
}

int HWCDisplayVirtual::Commit(hwc_display_contents_1_t *content_list) {
  int status = 0;
  if (display_paused_) {
    DisplayError error = display_intf_->Flush();
    if (error != kErrorNone) {
      DLOGE("Flush failed. Error = %d", error);
    }
    return status;
  }

  CommitOutputBufferParams(content_list);

  status = HWCDisplay::CommitLayerStack(content_list);
  if (status) {
    return status;
  }

  if (dump_frame_count_ && !flush_ && dump_output_layer_) {
    const private_handle_t *output_handle = (const private_handle_t *)(content_list->outbuf);
    if (output_handle && output_handle->base) {
      BufferInfo buffer_info;
      buffer_info.buffer_config.width = static_cast<uint32_t>(output_handle->width);
      buffer_info.buffer_config.height = static_cast<uint32_t>(output_handle->height);
      buffer_info.buffer_config.format = GetSDMFormat(output_handle->format, output_handle->flags);
      buffer_info.alloc_buffer_info.size = static_cast<uint32_t>(output_handle->size);
      DumpOutputBuffer(buffer_info, reinterpret_cast<void *>(output_handle->base),
                       layer_stack_.retire_fence_fd);
    }
  }

  status = HWCDisplay::PostCommitLayerStack(content_list);
  if (status) {
    return status;
  }

  return 0;
}

int HWCDisplayVirtual::SetOutputSliceFromMetadata(hwc_display_contents_1_t *content_list) {
  const private_handle_t *output_handle =
        static_cast<const private_handle_t *>(content_list->outbuf);
  DisplayError error = kErrorNone;
  int status = 0;

  if (output_handle) {
    int output_handle_format = output_handle->format;
    if (output_handle_format == HAL_PIXEL_FORMAT_RGBA_8888) {
      output_handle_format = HAL_PIXEL_FORMAT_RGBX_8888;
    }

    LayerBufferFormat format = GetSDMFormat(output_handle_format, output_handle->flags);
    if (format == kFormatInvalid) {
      return -EINVAL;
    }

    int active_width;
    int active_height;

    AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(output_handle, active_width,
                                                          active_height);

    if ((active_width != INT(output_buffer_->width)) ||
        (active_height!= INT(output_buffer_->height)) ||
        (format != output_buffer_->format)) {
      // Populate virtual display attributes based on displayFrame of FBT.
      // For DRC, use width and height populated in metadata (unaligned values)
      // for setting attributes of virtual display. This is needed because if
      // we use aligned width and height, scaling will be required for FBT layer.
      DisplayConfigVariableInfo variable_info;
      hwc_layer_1_t &fbt_layer = content_list->hwLayers[content_list->numHwLayers-1];
      hwc_rect_t &frame = fbt_layer.displayFrame;
      int fbt_width = frame.right - frame.left;
      int fbt_height = frame.bottom - frame.top;
      const MetaData_t *meta_data = reinterpret_cast<MetaData_t *>(output_handle->base_metadata);
      if (meta_data && meta_data->operation & UPDATE_BUFFER_GEOMETRY) {
        variable_info.x_pixels = UINT32(meta_data->bufferDim.sliceWidth);
        variable_info.y_pixels = UINT32(meta_data->bufferDim.sliceHeight);
      } else {
        variable_info.x_pixels = UINT32(fbt_width);
        variable_info.y_pixels = UINT32(fbt_height);
      }
      // TODO(user): Need to get the framerate of primary display and update it.
      variable_info.fps = 60;

      error = display_intf_->SetActiveConfig(&variable_info);
      if (error != kErrorNone) {
        return -EINVAL;
      }

      status = SetOutputBuffer(content_list);
      if (status) {
        return status;
      }
    }
  }

  return 0;
}

int HWCDisplayVirtual::SetOutputBuffer(hwc_display_contents_1_t *content_list) {
  const private_handle_t *output_handle =
        static_cast<const private_handle_t *>(content_list->outbuf);

  if (output_handle) {
    int output_handle_format = output_handle->format;

    if (output_handle_format == HAL_PIXEL_FORMAT_RGBA_8888) {
      output_handle_format = HAL_PIXEL_FORMAT_RGBX_8888;
    }

    output_buffer_->format = GetSDMFormat(output_handle_format, output_handle->flags);

    if (output_buffer_->format == kFormatInvalid) {
      return -EINVAL;
    }

    int aligned_width, aligned_height;
    int unaligned_width, unaligned_height;

    AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(output_handle, aligned_width,
                                                          aligned_height);
    AdrenoMemInfo::getInstance().getUnalignedWidthAndHeight(output_handle, unaligned_width,
                                                            unaligned_height);

    output_buffer_->width = UINT32(aligned_width);
    output_buffer_->height = UINT32(aligned_height);
    output_buffer_->unaligned_width = UINT32(unaligned_width);
    output_buffer_->unaligned_height = UINT32(unaligned_height);
    output_buffer_->flags.secure = 0;
    output_buffer_->flags.video = 0;

    const MetaData_t *meta_data = reinterpret_cast<MetaData_t *>(output_handle->base_metadata);
    if (meta_data && SetCSC(meta_data, &output_buffer_->color_metadata) != kErrorNone) {
      return kErrorNotSupported;
    }

    // TZ Protected Buffer - L1
    if (output_handle->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
      output_buffer_->flags.secure = 1;
    }
  }

  layer_stack_.output_buffer = output_buffer_;

  return 0;
}

void HWCDisplayVirtual::CommitOutputBufferParams(hwc_display_contents_1_t *content_list) {
  const private_handle_t *output_handle =
        static_cast<const private_handle_t *>(content_list->outbuf);

  // Fill output buffer parameters (width, height, format, plane information, fence)
  output_buffer_->acquire_fence_fd = content_list->outbufAcquireFenceFd;

  if (output_handle) {
    // ToDo: Need to extend for non-RGB formats
    output_buffer_->planes[0].fd = output_handle->fd;
    output_buffer_->planes[0].offset = output_handle->offset;
    output_buffer_->planes[0].stride = UINT32(output_handle->width);
  }
}

void HWCDisplayVirtual::SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type) {
  HWCDisplay::SetFrameDumpConfig(count, bit_mask_layer_type);
  dump_output_layer_ = ((bit_mask_layer_type & (1 << OUTPUT_LAYER_DUMP)) != 0);

  DLOGI("output_layer_dump_enable %d", dump_output_layer_);
}

}  // namespace sdm

