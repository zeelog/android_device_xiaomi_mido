/*
* Copyright (c) 2014 - 2018, 2020, The Linux Foundation. All rights reserved.
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

#define __STDC_FORMAT_MACROS

#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <utils/sys.h>
#include <vector>
#include <algorithm>
#include <string>

#include "hw_device.h"
#include "hw_primary.h"
#include "hw_hdmi.h"
#include "hw_virtual.h"
#include "hw_info_interface.h"

#define __CLASS__ "HWDevice"

using std::string;
using std::to_string;
using std::fstream;

namespace sdm {

HWDevice::HWDevice(BufferSyncHandler *buffer_sync_handler)
  : fb_node_index_(-1), fb_path_("/sys/devices/virtual/graphics/fb"),
    buffer_sync_handler_(buffer_sync_handler), synchronous_commit_(false) {
}

DisplayError HWDevice::Init() {
  // Read the fb node index
  fb_node_index_ = GetFBNodeIndex(device_type_);
  if (fb_node_index_ == -1) {
    DLOGE("device type = %d should be present", device_type_);
    return kErrorHardware;
  }

  const char *dev_name = NULL;
  vector<string> dev_paths = {"/dev/graphics/fb", "/dev/fb"};
  for (size_t i = 0; i < dev_paths.size(); i++) {
    dev_paths[i] += to_string(fb_node_index_);
    if (Sys::access_(dev_paths[i].c_str(), F_OK) >= 0) {
      dev_name = dev_paths[i].c_str();
      DLOGI("access(%s) successful", dev_name);
      break;
    }

    DLOGI("access(%s), errno = %d, error = %s", dev_paths[i].c_str(), errno, strerror(errno));
  }

  if (!dev_name) {
    DLOGE("access() failed for all possible paths");
    return kErrorHardware;
  }

  // Populate Panel Info (Used for Partial Update)
  PopulateHWPanelInfo();
  // Populate Bit clk levels.
  PopulateBitClkRates();
  // Populate HW Capabilities
  hw_resource_ = HWResourceInfo();
  hw_info_intf_->GetHWResourceInfo(&hw_resource_);

  device_fd_ = Sys::open_(dev_name, O_RDWR);
  if (device_fd_ < 0) {
    DLOGE("open %s failed errno = %d, error = %s", dev_name, errno, strerror(errno));
    return kErrorResources;
  }

  return HWScale::Create(&hw_scale_, hw_resource_.has_qseed3);
}

DisplayError HWDevice::Deinit() {
  HWScale::Destroy(hw_scale_);

  if (device_fd_ >= 0) {
    Sys::close_(device_fd_);
    device_fd_ = -1;
  }

  if (stored_retire_fence >= 0) {
    Sys::close_(stored_retire_fence);
    stored_retire_fence = -1;
  }
  return kErrorNone;
}

DisplayError HWDevice::GetActiveConfig(uint32_t *active_config) {
  *active_config = 0;
  return kErrorNone;
}

DisplayError HWDevice::GetNumDisplayAttributes(uint32_t *count) {
  *count = 1;
  return kErrorNone;
}

DisplayError HWDevice::GetDisplayAttributes(uint32_t index,
                                            HWDisplayAttributes *display_attributes) {
  return kErrorNone;
}

DisplayError HWDevice::GetHWPanelInfo(HWPanelInfo *panel_info) {
  *panel_info = hw_panel_info_;
  return kErrorNone;
}

DisplayError HWDevice::SetDisplayAttributes(uint32_t index) {
  return kErrorNone;
}

DisplayError HWDevice::SetDisplayAttributes(const HWDisplayAttributes &display_attributes) {
  return kErrorNotSupported;
}

DisplayError HWDevice::GetConfigIndex(uint32_t mode, uint32_t *index) {
  return kErrorNone;
}

DisplayError HWDevice::GetConfigIndex(uint32_t width, uint32_t height, uint32_t *index) {
  return kErrorNone;
}

DisplayError HWDevice::PowerOn() {
  DTRACE_SCOPED();

  if (Sys::ioctl_(device_fd_, FBIOBLANK, FB_BLANK_UNBLANK) < 0) {
    if (errno == ESHUTDOWN) {
      DLOGI_IF(kTagDriverConfig, "Driver is processing shutdown sequence");
      return kErrorShutDown;
    }
    IOCTL_LOGE(FB_BLANK_UNBLANK, device_type_);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWDevice::PowerOff() {
  return kErrorNone;
}

DisplayError HWDevice::Doze() {
  return kErrorNone;
}

DisplayError HWDevice::DozeSuspend() {
  return kErrorNone;
}

DisplayError HWDevice::Standby() {
  return kErrorNone;
}

DisplayError HWDevice::Validate(HWLayers *hw_layers) {
  DTRACE_SCOPED();

  DisplayError error = kErrorNone;

  HWLayersInfo &hw_layer_info = hw_layers->info;
  uint32_t hw_layer_count = UINT32(hw_layer_info.hw_layers.size());

  DLOGD_IF(kTagDriverConfig, "************************** %s Validate Input ***********************",
           device_name_);
  DLOGD_IF(kTagDriverConfig, "SDE layer count is %d", hw_layer_count);

  mdp_layer_commit_v1 &mdp_commit = mdp_disp_commit_.commit_v1;
  uint32_t &mdp_layer_count = mdp_commit.input_layer_cnt;

  DLOGI_IF(kTagDriverConfig, "left_roi: x = %d, y = %d, w = %d, h = %d", mdp_commit.left_roi.x,
    mdp_commit.left_roi.y, mdp_commit.left_roi.w, mdp_commit.left_roi.h);
  DLOGI_IF(kTagDriverConfig, "right_roi: x = %d, y = %d, w = %d, h = %d", mdp_commit.right_roi.x,
    mdp_commit.right_roi.y, mdp_commit.right_roi.w, mdp_commit.right_roi.h);

  for (uint32_t i = 0; i < hw_layer_count; i++) {
    const Layer &layer = hw_layer_info.hw_layers.at(i);
    LayerBuffer input_buffer = layer.input_buffer;
    HWPipeInfo *left_pipe = &hw_layers->config[i].left_pipe;
    HWPipeInfo *right_pipe = &hw_layers->config[i].right_pipe;
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;
    bool is_rotator_used = (hw_rotator_session->hw_block_count != 0);
    bool is_cursor_pipe_used = (hw_layer_info.use_hw_cursor & layer.flags.cursor);

    for (uint32_t count = 0; count < 2; count++) {
      HWPipeInfo *pipe_info = (count == 0) ? left_pipe : right_pipe;
      HWRotateInfo *hw_rotate_info = &hw_rotator_session->hw_rotate_info[count];

      if (hw_rotate_info->valid) {
        input_buffer = hw_rotator_session->output_buffer;
      }

      if (pipe_info->valid) {
        mdp_input_layer &mdp_layer = mdp_in_layers_[mdp_layer_count];
        mdp_layer_buffer &mdp_buffer = mdp_layer.buffer;

        mdp_buffer.width = input_buffer.width;
        mdp_buffer.height = input_buffer.height;
        mdp_buffer.comp_ratio.denom = 1000;
        mdp_buffer.comp_ratio.numer = UINT32(hw_layers->config[i].compression * 1000);

        if (layer.flags.solid_fill) {
          mdp_buffer.format = MDP_ARGB_8888;
        } else {
          error = SetFormat(input_buffer.format, &mdp_buffer.format);
          if (error != kErrorNone) {
            return error;
          }
        }
        mdp_layer.alpha = layer.plane_alpha;
        mdp_layer.z_order = UINT16(pipe_info->z_order);
        mdp_layer.transp_mask = 0xffffffff;
        SetBlending(layer.blending, &mdp_layer.blend_op);
        mdp_layer.pipe_ndx = pipe_info->pipe_id;
        mdp_layer.horz_deci = pipe_info->horizontal_decimation;
        mdp_layer.vert_deci = pipe_info->vertical_decimation;
#ifdef MDP_COMMIT_RECT_NUM
        mdp_layer.rect_num = pipe_info->rect;
#endif
        SetRect(pipe_info->src_roi, &mdp_layer.src_rect);
        SetRect(pipe_info->dst_roi, &mdp_layer.dst_rect);
        SetMDPFlags(&layer, is_rotator_used, is_cursor_pipe_used, &mdp_layer.flags);
        SetCSC(layer.input_buffer.color_metadata, &mdp_layer.color_space);
        if (pipe_info->flags & kIGC) {
          SetIGC(&layer.input_buffer, mdp_layer_count);
        }
        if (pipe_info->flags & kMultiRect) {
          mdp_layer.flags |= MDP_LAYER_MULTIRECT_ENABLE;
          if (pipe_info->flags & kMultiRectParallelMode) {
            mdp_layer.flags |= MDP_LAYER_MULTIRECT_PARALLEL_MODE;
          }
        }
        mdp_layer.bg_color = layer.solid_fill_color;

        // HWScaleData to MDP driver
        hw_scale_->SetHWScaleData(pipe_info->scale_data, mdp_layer_count, &mdp_commit,
                                  pipe_info->sub_block_type);
        mdp_layer.scale = hw_scale_->GetScaleDataRef(mdp_layer_count, pipe_info->sub_block_type);

        mdp_layer_count++;

        DLOGD_IF(kTagDriverConfig, "******************* Layer[%d] %s pipe Input ******************",
                 i, count ? "Right" : "Left");
        DLOGD_IF(kTagDriverConfig, "in_w %d, in_h %d, in_f %d", mdp_buffer.width, mdp_buffer.height,
                 mdp_buffer.format);
        DLOGD_IF(kTagDriverConfig, "plane_alpha %d, zorder %d, blending %d, horz_deci %d, "
                 "vert_deci %d, pipe_id = 0x%x, mdp_flags 0x%x", mdp_layer.alpha, mdp_layer.z_order,
                 mdp_layer.blend_op, mdp_layer.horz_deci, mdp_layer.vert_deci, mdp_layer.pipe_ndx,
                 mdp_layer.flags);
        DLOGV_IF(kTagDriverConfig, "src_rect [%d, %d, %d, %d]", mdp_layer.src_rect.x,
                 mdp_layer.src_rect.y, mdp_layer.src_rect.w, mdp_layer.src_rect.h);
        DLOGV_IF(kTagDriverConfig, "dst_rect [%d, %d, %d, %d]", mdp_layer.dst_rect.x,
                 mdp_layer.dst_rect.y, mdp_layer.dst_rect.w, mdp_layer.dst_rect.h);
        hw_scale_->DumpScaleData(mdp_layer.scale);
        DLOGD_IF(kTagDriverConfig, "*************************************************************");
      }
    }
  }

  // TODO(user): This block should move to the derived class
  if (device_type_ == kDeviceVirtual) {
    LayerBuffer *output_buffer = hw_layers->info.stack->output_buffer;
    mdp_out_layer_.writeback_ndx = hw_resource_.writeback_index;
    mdp_out_layer_.buffer.width = output_buffer->width;
    mdp_out_layer_.buffer.height = output_buffer->height;
    if (output_buffer->flags.secure) {
      mdp_out_layer_.flags |= MDP_LAYER_SECURE_SESSION;
    }
    mdp_out_layer_.buffer.comp_ratio.denom = 1000;
    mdp_out_layer_.buffer.comp_ratio.numer = UINT32(hw_layers->output_compression * 1000);
#ifdef OUT_LAYER_COLOR_SPACE
    SetCSC(output_buffer->color_metadata, &mdp_out_layer_.color_space);
#endif
    SetFormat(output_buffer->format, &mdp_out_layer_.buffer.format);

    DLOGI_IF(kTagDriverConfig, "********************* Output buffer Info ************************");
    DLOGI_IF(kTagDriverConfig, "out_w %d, out_h %d, out_f %d, wb_id %d",
             mdp_out_layer_.buffer.width, mdp_out_layer_.buffer.height,
             mdp_out_layer_.buffer.format, mdp_out_layer_.writeback_ndx);
    DLOGI_IF(kTagDriverConfig, "*****************************************************************");
  }

  uint32_t index = 0;
  for (uint32_t i = 0; i < hw_resource_.hw_dest_scalar_info.count; i++) {
    DestScaleInfoMap::iterator it = hw_layer_info.dest_scale_info_map.find(i);

    if (it == hw_layer_info.dest_scale_info_map.end()) {
      continue;
    }

    HWDestScaleInfo *dest_scale_info = it->second;

    mdp_destination_scaler_data *dest_scalar_data = &mdp_dest_scalar_data_[index];
    hw_scale_->SetHWScaleData(dest_scale_info->scale_data, index, &mdp_commit,
                              kHWDestinationScalar);

    if (dest_scale_info->scale_update) {
      dest_scalar_data->flags |= MDP_DESTSCALER_SCALE_UPDATE;
    }

    dest_scalar_data->dest_scaler_ndx = i;
    dest_scalar_data->lm_width = dest_scale_info->mixer_width;
    dest_scalar_data->lm_height = dest_scale_info->mixer_height;
#ifdef MDP_DESTSCALER_ROI_ENABLE
    SetRect(dest_scale_info->panel_roi, &dest_scalar_data->panel_roi);
    dest_scalar_data->flags |= MDP_DESTSCALER_ROI_ENABLE;
#endif
    dest_scalar_data->scale = reinterpret_cast <uint64_t>
      (hw_scale_->GetScaleDataRef(index, kHWDestinationScalar));

    index++;

    DLOGD_IF(kTagDriverConfig, "************************ DestScalar[%d] **************************",
             dest_scalar_data->dest_scaler_ndx);
    DLOGD_IF(kTagDriverConfig, "Mixer WxH %dx%d flags %x", dest_scalar_data->lm_width,
             dest_scalar_data->lm_height, dest_scalar_data->flags);
#ifdef MDP_DESTSCALER_ROI_ENABLE
    DLOGD_IF(kTagDriverConfig, "Panel ROI [%d, %d, %d, %d]", dest_scalar_data->panel_roi.x,
             dest_scalar_data->panel_roi.y, dest_scalar_data->panel_roi.w,
             dest_scalar_data->panel_roi.h);
#endif
    DLOGD_IF(kTagDriverConfig, "*****************************************************************");
  }
  mdp_commit.dest_scaler_cnt = UINT32(hw_layer_info.dest_scale_info_map.size());

  mdp_commit.flags |= MDP_VALIDATE_LAYER;
#ifdef MDP_COMMIT_RECT_NUM
  mdp_commit.flags |= MDP_COMMIT_RECT_NUM;
#endif
  if (Sys::ioctl_(device_fd_, INT(MSMFB_ATOMIC_COMMIT), &mdp_disp_commit_) < 0) {
    if (errno == ESHUTDOWN) {
      DLOGI_IF(kTagDriverConfig, "Driver is processing shutdown sequence");
      return kErrorShutDown;
    }
    IOCTL_LOGE(MSMFB_ATOMIC_COMMIT, device_type_);
    DumpLayerCommit(mdp_disp_commit_);
    return kErrorHardware;
  }

  return kErrorNone;
}

void HWDevice::DumpLayerCommit(const mdp_layer_commit &layer_commit) {
  const mdp_layer_commit_v1 &mdp_commit = layer_commit.commit_v1;
  const mdp_input_layer *mdp_layers = mdp_commit.input_layers;
  const mdp_rect &l_roi = mdp_commit.left_roi;
  const mdp_rect &r_roi = mdp_commit.right_roi;

  DLOGI("mdp_commit: flags = %x, release fence = %x", mdp_commit.flags, mdp_commit.release_fence);
  DLOGI("left_roi: x = %d, y = %d, w = %d, h = %d", l_roi.x, l_roi.y, l_roi.w, l_roi.h);
  DLOGI("right_roi: x = %d, y = %d, w = %d, h = %d", r_roi.x, r_roi.y, r_roi.w, r_roi.h);
  for (uint32_t i = 0; i < mdp_commit.dest_scaler_cnt; i++) {
    mdp_destination_scaler_data *dest_scalar_data = &mdp_dest_scalar_data_[i];
    mdp_scale_data_v2 *mdp_scale = reinterpret_cast<mdp_scale_data_v2 *>(dest_scalar_data->scale);

    DLOGI("Dest scalar index %d Mixer WxH %dx%d", dest_scalar_data->dest_scaler_ndx,
          dest_scalar_data->lm_width, dest_scalar_data->lm_height);
#ifdef MDP_DESTSCALER_ROI_ENABLE
    DLOGI("Panel ROI [%d, %d, %d, %d]", dest_scalar_data->panel_roi.x,
           dest_scalar_data->panel_roi.y, dest_scalar_data->panel_roi.w,
           dest_scalar_data->panel_roi.h);
#endif
    DLOGI("Dest scalar Dst WxH %dx%d", mdp_scale->dst_width, mdp_scale->dst_height);
  }
  for (uint32_t i = 0; i < mdp_commit.input_layer_cnt; i++) {
    const mdp_input_layer &layer = mdp_layers[i];
    const mdp_rect &src_rect = layer.src_rect;
    const mdp_rect &dst_rect = layer.dst_rect;
    DLOGI("layer = %d, pipe_ndx = %x, z = %d, flags = %x",
      i, layer.pipe_ndx, layer.z_order, layer.flags);
    DLOGI("src_width = %d, src_height = %d, src_format = %d",
      layer.buffer.width, layer.buffer.height, layer.buffer.format);
    DLOGI("src_rect: x = %d, y = %d, w = %d, h = %d",
      src_rect.x, src_rect.y, src_rect.w, src_rect.h);
    DLOGI("dst_rect: x = %d, y = %d, w = %d, h = %d",
      dst_rect.x, dst_rect.y, dst_rect.w, dst_rect.h);
  }
}

DisplayError HWDevice::Commit(HWLayers *hw_layers) {
  DTRACE_SCOPED();

  HWLayersInfo &hw_layer_info = hw_layers->info;
  uint32_t hw_layer_count = UINT32(hw_layer_info.hw_layers.size());

  DLOGD_IF(kTagDriverConfig, "*************************** %s Commit Input ************************",
           device_name_);
  DLOGD_IF(kTagDriverConfig, "SDE layer count is %d", hw_layer_count);

  mdp_layer_commit_v1 &mdp_commit = mdp_disp_commit_.commit_v1;
  uint32_t mdp_layer_index = 0;

  for (uint32_t i = 0; i < hw_layer_count; i++) {
    const Layer &layer = hw_layer_info.hw_layers.at(i);
    LayerBuffer *input_buffer = const_cast<LayerBuffer *>(&layer.input_buffer);
    HWPipeInfo *left_pipe = &hw_layers->config[i].left_pipe;
    HWPipeInfo *right_pipe = &hw_layers->config[i].right_pipe;
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;

    for (uint32_t count = 0; count < 2; count++) {
      HWPipeInfo *pipe_info = (count == 0) ? left_pipe : right_pipe;
      HWRotateInfo *hw_rotate_info = &hw_rotator_session->hw_rotate_info[count];

      if (hw_rotate_info->valid) {
        input_buffer = &hw_rotator_session->output_buffer;
      }

      if (pipe_info->valid) {
        mdp_layer_buffer &mdp_buffer = mdp_in_layers_[mdp_layer_index].buffer;
        mdp_input_layer &mdp_layer = mdp_in_layers_[mdp_layer_index];
        if (input_buffer->planes[0].fd >= 0) {
          mdp_buffer.plane_count = 1;
          mdp_buffer.planes[0].fd = input_buffer->planes[0].fd;
          mdp_buffer.planes[0].offset = input_buffer->planes[0].offset;
          SetStride(device_type_, input_buffer->format, input_buffer->planes[0].stride,
                    &mdp_buffer.planes[0].stride);
        } else {
          mdp_buffer.plane_count = 0;
        }

        mdp_buffer.fence = input_buffer->acquire_fence_fd;
        mdp_layer_index++;

        DLOGD_IF(kTagDriverConfig, "****************** Layer[%d] %s pipe Input *******************",
                 i, count ? "Right" : "Left");
        DLOGD_IF(kTagDriverConfig, "in_w %d, in_h %d, in_f %d, horz_deci %d, vert_deci %d",
                 mdp_buffer.width, mdp_buffer.height, mdp_buffer.format, mdp_layer.horz_deci,
                 mdp_layer.vert_deci);
        DLOGV_IF(kTagDriverConfig, "in_buf_fd %d, in_buf_offset %d, in_buf_stride %d, " \
                 "in_plane_count %d, in_fence %d, layer count %d", mdp_buffer.planes[0].fd,
                 mdp_buffer.planes[0].offset, mdp_buffer.planes[0].stride, mdp_buffer.plane_count,
                 mdp_buffer.fence, mdp_commit.input_layer_cnt);
        DLOGD_IF(kTagDriverConfig, "*************************************************************");
      }
    }
  }

  // TODO(user): Move to derived class
  if (device_type_ == kDeviceVirtual) {
    LayerBuffer *output_buffer = hw_layers->info.stack->output_buffer;

    if (output_buffer->planes[0].fd >= 0) {
      mdp_out_layer_.buffer.planes[0].fd = output_buffer->planes[0].fd;
      mdp_out_layer_.buffer.planes[0].offset = output_buffer->planes[0].offset;
      SetStride(device_type_, output_buffer->format, output_buffer->planes[0].stride,
                &mdp_out_layer_.buffer.planes[0].stride);
      mdp_out_layer_.buffer.plane_count = 1;
    } else {
      DLOGE("Invalid output buffer fd");
      return kErrorParameters;
    }

    mdp_out_layer_.buffer.fence = output_buffer->acquire_fence_fd;

    DLOGI_IF(kTagDriverConfig, "********************** Output buffer Info ***********************");
    DLOGI_IF(kTagDriverConfig, "out_fd %d, out_offset %d, out_stride %d, acquire_fence %d",
             mdp_out_layer_.buffer.planes[0].fd, mdp_out_layer_.buffer.planes[0].offset,
             mdp_out_layer_.buffer.planes[0].stride,  mdp_out_layer_.buffer.fence);
    DLOGI_IF(kTagDriverConfig, "*****************************************************************");
  }

  mdp_commit.release_fence = -1;
  mdp_commit.flags &= UINT32(~MDP_VALIDATE_LAYER);
  if (synchronous_commit_) {
    mdp_commit.flags |= MDP_COMMIT_WAIT_FOR_FINISH;
  }
  if (Sys::ioctl_(device_fd_, INT(MSMFB_ATOMIC_COMMIT), &mdp_disp_commit_) < 0) {
    if (errno == ESHUTDOWN) {
      DLOGI_IF(kTagDriverConfig, "Driver is processing shutdown sequence");
      return kErrorShutDown;
    }
    IOCTL_LOGE(MSMFB_ATOMIC_COMMIT, device_type_);
    DumpLayerCommit(mdp_disp_commit_);
    synchronous_commit_ = false;
    return kErrorHardware;
  }

  LayerStack *stack = hw_layer_info.stack;
  stack->retire_fence_fd = mdp_commit.retire_fence;
#ifdef VIDEO_MODE_DEFER_RETIRE_FENCE
  if (hw_panel_info_.mode == kModeVideo) {
    stack->retire_fence_fd = stored_retire_fence;
    stored_retire_fence =  mdp_commit.retire_fence;
  }
#endif
  // MDP returns only one release fence for the entire layer stack. Duplicate this fence into all
  // layers being composed by MDP.

  for (uint32_t i = 0; i < hw_layer_count; i++) {
    const Layer &layer = hw_layer_info.hw_layers.at(i);
    LayerBuffer *input_buffer = const_cast<LayerBuffer *>(&layer.input_buffer);
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;

    if (hw_rotator_session->hw_block_count) {
      input_buffer = &hw_rotator_session->output_buffer;
      input_buffer->release_fence_fd = Sys::dup_(mdp_commit.release_fence);
      continue;
    }

    input_buffer->release_fence_fd = Sys::dup_(mdp_commit.release_fence);
  }

  hw_layer_info.sync_handle = Sys::dup_(mdp_commit.release_fence);

  DLOGI_IF(kTagDriverConfig, "*************************** %s Commit Input ************************",
           device_name_);
  DLOGI_IF(kTagDriverConfig, "retire_fence_fd %d", stack->retire_fence_fd);
  DLOGI_IF(kTagDriverConfig, "*******************************************************************");

  if (mdp_commit.release_fence >= 0) {
    Sys::close_(mdp_commit.release_fence);
  }

  if (synchronous_commit_) {
    // A synchronous commit can be requested when changing the display mode so we need to update
    // panel info.
    PopulateHWPanelInfo();
    synchronous_commit_ = false;
  }

  return kErrorNone;
}

DisplayError HWDevice::Flush(bool secure) {
  if (hw_resource_.has_ppp && !secure) {
    DLOGI_IF(kTagDriverConfig, "Avoid flush for non-secure use cases");
    return kErrorNone;
  }

  ResetDisplayParams();
  mdp_layer_commit_v1 &mdp_commit = mdp_disp_commit_.commit_v1;
  mdp_commit.input_layer_cnt = 0;
  mdp_commit.output_layer = NULL;

  mdp_commit.flags &= UINT32(~MDP_VALIDATE_LAYER);
  if (Sys::ioctl_(device_fd_, INT(MSMFB_ATOMIC_COMMIT), &mdp_disp_commit_) < 0) {
    if (errno == ESHUTDOWN) {
      DLOGI_IF(kTagDriverConfig, "Driver is processing shutdown sequence");
      return kErrorShutDown;
    }
    IOCTL_LOGE(MSMFB_ATOMIC_COMMIT, device_type_);
    DumpLayerCommit(mdp_disp_commit_);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWDevice::SetFormat(const LayerBufferFormat &source, uint32_t *target) {
  switch (source) {
  case kFormatARGB8888:                 *target = MDP_ARGB_8888;         break;
  case kFormatRGBA8888:                 *target = MDP_RGBA_8888;         break;
  case kFormatBGRA8888:                 *target = MDP_BGRA_8888;         break;
  case kFormatRGBX8888:                 *target = MDP_RGBX_8888;         break;
  case kFormatBGRX8888:                 *target = MDP_BGRX_8888;         break;
  case kFormatRGBA5551:                 *target = MDP_RGBA_5551;         break;
  case kFormatRGBA4444:                 *target = MDP_RGBA_4444;         break;
  case kFormatRGB888:                   *target = MDP_RGB_888;           break;
  case kFormatBGR888:                   *target = MDP_BGR_888;           break;
  case kFormatRGB565:                   *target = MDP_RGB_565;           break;
  case kFormatBGR565:                   *target = MDP_BGR_565;           break;
  case kFormatYCbCr420Planar:           *target = MDP_Y_CB_CR_H2V2;      break;
  case kFormatYCrCb420Planar:           *target = MDP_Y_CR_CB_H2V2;      break;
  case kFormatYCrCb420PlanarStride16:   *target = MDP_Y_CR_CB_GH2V2;     break;
  case kFormatYCbCr420SemiPlanar:       *target = MDP_Y_CBCR_H2V2;       break;
  case kFormatYCrCb420SemiPlanar:       *target = MDP_Y_CRCB_H2V2;       break;
  case kFormatYCbCr422H1V2SemiPlanar:   *target = MDP_Y_CBCR_H1V2;       break;
  case kFormatYCrCb422H1V2SemiPlanar:   *target = MDP_Y_CRCB_H1V2;       break;
  case kFormatYCbCr422H2V1SemiPlanar:   *target = MDP_Y_CBCR_H2V1;       break;
  case kFormatYCrCb422H2V1SemiPlanar:   *target = MDP_Y_CRCB_H2V1;       break;
  case kFormatYCbCr422H2V1Packed:       *target = MDP_YCBYCR_H2V1;       break;
  case kFormatYCbCr420SemiPlanarVenus:  *target = MDP_Y_CBCR_H2V2_VENUS; break;
  case kFormatRGBA8888Ubwc:             *target = MDP_RGBA_8888_UBWC;    break;
  case kFormatRGBX8888Ubwc:             *target = MDP_RGBX_8888_UBWC;    break;
  case kFormatBGR565Ubwc:               *target = MDP_RGB_565_UBWC;      break;
  case kFormatYCbCr420SPVenusUbwc:      *target = MDP_Y_CBCR_H2V2_UBWC;  break;
  case kFormatCbYCrY422H2V1Packed:      *target = MDP_CBYCRY_H2V1;       break;
  case kFormatRGBA1010102:              *target = MDP_RGBA_1010102;      break;
  case kFormatARGB2101010:              *target = MDP_ARGB_2101010;      break;
  case kFormatRGBX1010102:              *target = MDP_RGBX_1010102;      break;
  case kFormatXRGB2101010:              *target = MDP_XRGB_2101010;      break;
  case kFormatBGRA1010102:              *target = MDP_BGRA_1010102;      break;
  case kFormatABGR2101010:              *target = MDP_ABGR_2101010;      break;
  case kFormatBGRX1010102:              *target = MDP_BGRX_1010102;      break;
  case kFormatXBGR2101010:              *target = MDP_XBGR_2101010;      break;
  case kFormatRGBA1010102Ubwc:          *target = MDP_RGBA_1010102_UBWC; break;
  case kFormatRGBX1010102Ubwc:          *target = MDP_RGBX_1010102_UBWC; break;
  case kFormatYCbCr420P010:             *target = MDP_Y_CBCR_H2V2_P010;  break;
  case kFormatYCbCr420TP10Ubwc:         *target = MDP_Y_CBCR_H2V2_TP10_UBWC; break;
  default:
    DLOGE("Unsupported format type %d", source);
    return kErrorParameters;
  }

  return kErrorNone;
}

DisplayError HWDevice::SetStride(HWDeviceType device_type, LayerBufferFormat format,
                                      uint32_t width, uint32_t *target) {
  // TODO(user): This SetStride function is a workaround to satisfy the driver expectation for
  // rotator and virtual devices. Eventually this will be taken care in the driver.
  if (device_type != kDeviceRotator && device_type != kDeviceVirtual) {
    *target = width;
    return kErrorNone;
  }

  switch (format) {
  case kFormatARGB8888:
  case kFormatRGBA8888:
  case kFormatBGRA8888:
  case kFormatRGBX8888:
  case kFormatBGRX8888:
  case kFormatRGBA8888Ubwc:
  case kFormatRGBX8888Ubwc:
  case kFormatRGBA1010102:
  case kFormatARGB2101010:
  case kFormatRGBX1010102:
  case kFormatXRGB2101010:
  case kFormatBGRA1010102:
  case kFormatABGR2101010:
  case kFormatBGRX1010102:
  case kFormatXBGR2101010:
  case kFormatRGBA1010102Ubwc:
  case kFormatRGBX1010102Ubwc:
    *target = width * 4;
    break;
  case kFormatRGB888:
  case kFormatBGR888:
    *target = width * 3;
    break;
  case kFormatRGB565:
  case kFormatBGR565:
  case kFormatBGR565Ubwc:
    *target = width * 2;
    break;
  case kFormatYCbCr420SemiPlanarVenus:
  case kFormatYCbCr420SPVenusUbwc:
  case kFormatYCbCr420Planar:
  case kFormatYCrCb420Planar:
  case kFormatYCrCb420PlanarStride16:
  case kFormatYCbCr420SemiPlanar:
  case kFormatYCrCb420SemiPlanar:
  case kFormatYCbCr420TP10Ubwc:
    *target = width;
    break;
  case kFormatYCbCr422H2V1Packed:
  case kFormatCbYCrY422H2V1Packed:
  case kFormatYCrCb422H2V1SemiPlanar:
  case kFormatYCrCb422H1V2SemiPlanar:
  case kFormatYCbCr422H2V1SemiPlanar:
  case kFormatYCbCr422H1V2SemiPlanar:
  case kFormatYCbCr420P010:
  case kFormatRGBA5551:
  case kFormatRGBA4444:
    *target = width * 2;
    break;
  default:
    DLOGE("Unsupported format type %d", format);
    return kErrorParameters;
  }

  return kErrorNone;
}

void HWDevice::SetBlending(const LayerBlending &source, mdss_mdp_blend_op *target) {
  switch (source) {
  case kBlendingPremultiplied:  *target = BLEND_OP_PREMULTIPLIED;   break;
  case kBlendingOpaque:         *target = BLEND_OP_OPAQUE;          break;
  case kBlendingCoverage:       *target = BLEND_OP_COVERAGE;        break;
  default:                      *target = BLEND_OP_NOT_DEFINED;     break;
  }
}

void HWDevice::SetRect(const LayerRect &source, mdp_rect *target) {
  target->x = UINT32(source.left);
  target->y = UINT32(source.top);
  target->w = UINT32(source.right) - target->x;
  target->h = UINT32(source.bottom) - target->y;
}

void HWDevice::SetMDPFlags(const Layer *layer, const bool &is_rotator_used,
                           bool is_cursor_pipe_used, uint32_t *mdp_flags) {
  const LayerBuffer &input_buffer = layer->input_buffer;

  // Flips will be taken care by rotator, if layer uses rotator for downscale/rotation. So ignore
  // flip flags for MDP.
  if (!is_rotator_used) {
    if (layer->transform.flip_vertical) {
      *mdp_flags |= MDP_LAYER_FLIP_UD;
    }

    if (layer->transform.flip_horizontal) {
      *mdp_flags |= MDP_LAYER_FLIP_LR;
    }

    if (input_buffer.flags.interlace) {
      *mdp_flags |= MDP_LAYER_DEINTERLACE;
    }
  }

  if (input_buffer.flags.secure_camera) {
    *mdp_flags |= MDP_LAYER_SECURE_CAMERA_SESSION;
  } else if (input_buffer.flags.secure) {
    *mdp_flags |= MDP_LAYER_SECURE_SESSION;
  }

  if (input_buffer.flags.secure_display) {
    *mdp_flags |= MDP_LAYER_SECURE_DISPLAY_SESSION;
  }

  if (layer->flags.solid_fill) {
    *mdp_flags |= MDP_LAYER_SOLID_FILL;
  }

  if (hw_panel_info_.mode != kModeCommand && layer->flags.cursor && is_cursor_pipe_used) {
    // command mode panels does not support async position update
    *mdp_flags |= MDP_LAYER_ASYNC;
  }
}

int HWDevice::GetFBNodeIndex(HWDeviceType device_type) {
  for (int i = 0; i < kFBNodeMax; i++) {
    HWPanelInfo panel_info;
    GetHWPanelInfoByNode(i, &panel_info);
    switch (device_type) {
    case kDevicePrimary:
      if (panel_info.is_primary_panel) {
        return i;
      }
      break;
    case kDeviceHDMI:
      if (panel_info.is_pluggable == true) {
        if (IsFBNodeConnected(i)) {
          return i;
        }
      }
      break;
    case kDeviceVirtual:
      if (panel_info.port == kPortWriteBack) {
        return i;
      }
      break;
    default:
      break;
    }
  }
  return -1;
}

void HWDevice::PopulateHWPanelInfo() {
  hw_panel_info_ = HWPanelInfo();
  GetHWPanelInfoByNode(fb_node_index_, &hw_panel_info_);
  DLOGI("Device type = %d, Display Port = %d, Display Mode = %d, Device Node = %d, Is Primary = %d",
        device_type_, hw_panel_info_.port, hw_panel_info_.mode, fb_node_index_,
        hw_panel_info_.is_primary_panel);
  DLOGI("Partial Update = %d, supported roi_count =%d, Dynamic FPS = %d",
        hw_panel_info_.partial_update, hw_panel_info_.left_roi_count, hw_panel_info_.dynamic_fps);
  DLOGI("Align: left = %d, width = %d, top = %d, height = %d",
        hw_panel_info_.left_align, hw_panel_info_.width_align,
        hw_panel_info_.top_align, hw_panel_info_.height_align);
  DLOGI("ROI: min_width = %d, min_height = %d, need_merge = %d",
        hw_panel_info_.min_roi_width, hw_panel_info_.min_roi_height,
        hw_panel_info_.needs_roi_merge);
  DLOGI("FPS: min = %d, max =%d", hw_panel_info_.min_fps, hw_panel_info_.max_fps);
  DLOGI("Ping Pong Split = %d",  hw_panel_info_.ping_pong_split);
  DLOGI("Left Split = %d, Right Split = %d", hw_panel_info_.split_info.left_split,
        hw_panel_info_.split_info.right_split);
}

void HWDevice::PopulateBitClkRates() {
  if (!hw_panel_info_.bitclk_update) {
    return;
  }

  char bitclk_str[kMaxStringLength] = {'\0'};
  char bitclk_path[kMaxStringLength] = {'\0'};
  snprintf(bitclk_path, sizeof(bitclk_path), "%s%d/supported_bitclk", fb_path_, fb_node_index_);
  int fd = Sys::open_(bitclk_path, O_RDONLY);
  if (fd < 0) {
    DLOGE("BitClk file open failed.");
    return;
  }

  ssize_t length = Sys::pread_(fd, bitclk_str, sizeof(bitclk_str) - 1, 0);
  if (length <= 0) {
    DLOGE("%s: bitclk_modes file empty");
    Sys::close_(fd);
    return;
  }
  Sys::close_(fd);

  DLOGI("Bit Clk string: %s", bitclk_str);
  bitclk_str[length] = '\0';
  while (length > 1 && isspace(bitclk_str[length - 1])) {
     --length;
  }
  bitclk_str[length] = '\0';

  if (length > 0) {
    // Parse supported clk. levels.
    const uint32_t max_levels = 32;
    char *ptr = bitclk_str;
    char *tokens[max_levels] = { NULL };
    const char *delim = ",\n";
    uint32_t clk_levels = 0;

    ParseLine(ptr, delim, tokens, max_levels, &clk_levels);

    for (uint32_t i = 0; i < clk_levels; i++) {
      hw_panel_info_.bitclk_rates.push_back(UINT64(atoi(tokens[i])));
    }
  }

}

void HWDevice::GetHWPanelNameByNode(int device_node, HWPanelInfo *panel_info) {
  string file_name = fb_path_ + to_string(device_node) + "/msm_fb_panel_info";

  Sys::fstream fs(file_name, fstream::in);
  if (!fs.is_open()) {
    DLOGW("Failed to open msm_fb_panel_info node device node %d", device_node);
    return;
  }

  string line;
  while (Sys::getline_(fs, line)) {
    uint32_t token_count = 0;
    const uint32_t max_count = 10;
    char *tokens[max_count] = { NULL };
    if (!ParseLine(line.c_str(), "=\n", tokens, max_count, &token_count)) {
      if (!strncmp(tokens[0], "panel_name", strlen("panel_name"))) {
        snprintf(panel_info->panel_name, sizeof(panel_info->panel_name), "%s", tokens[1]);
        break;
      }
    }
  }
}

void HWDevice::GetHWPanelInfoByNode(int device_node, HWPanelInfo *panel_info) {
  string file_name = fb_path_ + to_string(device_node) + "/msm_fb_panel_info";

  Sys::fstream fs(file_name, fstream::in);
  if (!fs.is_open()) {
    DLOGW("Failed to open msm_fb_panel_info node device node %d", device_node);
    return;
  }

  string line;
  while (Sys::getline_(fs, line)) {
    uint32_t token_count = 0;
    const uint32_t max_count = 10;
    char *tokens[max_count] = { NULL };
    if (!ParseLine(line.c_str(), tokens, max_count, &token_count)) {
      if (!strncmp(tokens[0], "pu_en", strlen("pu_en"))) {
        panel_info->partial_update = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "xstart", strlen("xstart"))) {
        panel_info->left_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "walign", strlen("walign"))) {
        panel_info->width_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "ystart", strlen("ystart"))) {
        panel_info->top_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "halign", strlen("halign"))) {
        panel_info->height_align = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "min_w", strlen("min_w"))) {
        panel_info->min_roi_width = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "min_h", strlen("min_h"))) {
        panel_info->min_roi_height = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "roi_merge", strlen("roi_merge"))) {
        panel_info->needs_roi_merge = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "dyn_fps_en", strlen("dyn_fps_en"))) {
        panel_info->dynamic_fps = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "dfps_porch_mode", strlen("dfps_porch_mode"))) {
        panel_info->dfps_porch_mode = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "is_pingpong_split", strlen("is_pingpong_split"))) {
        panel_info->ping_pong_split = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "min_fps", strlen("min_fps"))) {
        panel_info->min_fps = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "max_fps", strlen("max_fps"))) {
        panel_info->max_fps = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "primary_panel", strlen("primary_panel"))) {
        panel_info->is_primary_panel = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "is_pluggable", strlen("is_pluggable"))) {
        panel_info->is_pluggable = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "pu_roi_cnt", strlen("pu_roi_cnt"))) {
        panel_info->left_roi_count = UINT32(atoi(tokens[1]));
        panel_info->right_roi_count = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "is_hdr_enabled", strlen("is_hdr_enabled"))) {
        panel_info->hdr_enabled = atoi(tokens[1]);
      } else if (!strncmp(tokens[0], "peak_brightness", strlen("peak_brightness"))) {
        panel_info->peak_luminance = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "average_brightness", strlen("average_brightness"))) {
        panel_info->average_luminance = UINT32(panel_info->peak_luminance +
                                            panel_info->blackness_level) / 2;
      } else if (!strncmp(tokens[0], "blackness_level", strlen("blackness_level"))) {
        panel_info->blackness_level = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "white_chromaticity_x", strlen("white_chromaticity_x"))) {
        panel_info->primaries.white_point[0] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "white_chromaticity_y", strlen("white_chromaticity_y"))) {
        panel_info->primaries.white_point[1] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "red_chromaticity_x", strlen("red_chromaticity_x"))) {
        panel_info->primaries.red[0] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "red_chromaticity_y", strlen("red_chromaticity_y"))) {
        panel_info->primaries.red[1] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "green_chromaticity_x", strlen("green_chromaticity_x"))) {
        panel_info->primaries.green[0] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "green_chromaticity_y", strlen("green_chromaticity_y"))) {
        panel_info->primaries.green[1] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "blue_chromaticity_x", strlen("blue_chromaticity_x"))) {
        panel_info->primaries.blue[0] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "blue_chromaticity_y", strlen("blue_chromaticity_y"))) {
        panel_info->primaries.blue[1] = UINT32(atoi(tokens[1]));
      } else if (!strncmp(tokens[0], "panel_orientation", strlen("panel_orientation"))) {
        int32_t panel_orient = atoi(tokens[1]);
        panel_info->panel_orientation.flip_horizontal = ((panel_orient & MDP_FLIP_LR) > 0);
        panel_info->panel_orientation.flip_vertical = ((panel_orient & MDP_FLIP_UD) > 0);
        panel_info->panel_orientation.rotation = ((panel_orient & MDP_ROT_90) > 0);
      } else if (!strncmp(tokens[0], "dyn_bitclk_en", strlen("dyn_bitclk_en"))) {
        panel_info->bitclk_update = atoi(tokens[1]);
      }
    }
  }

  GetHWDisplayPortAndMode(device_node, panel_info);
  GetSplitInfo(device_node, panel_info);
  GetHWPanelNameByNode(device_node, panel_info);
  GetHWPanelMaxBrightnessFromNode(panel_info);
}

void HWDevice::GetHWDisplayPortAndMode(int device_node, HWPanelInfo *panel_info) {
  DisplayPort *port = &panel_info->port;
  HWDisplayMode *mode = &panel_info->mode;

  *port = kPortDefault;
  *mode = kModeDefault;

  string file_name = fb_path_ + to_string(device_node) + "/msm_fb_type";

  Sys::fstream fs(file_name, fstream::in);
  if (!fs.is_open()) {
    DLOGW("File not found %s", file_name.c_str());
    return;
  }

  string line;
  if (!Sys::getline_(fs, line)) {
    return;
  }

  if ((strncmp(line.c_str(), "mipi dsi cmd panel", strlen("mipi dsi cmd panel")) == 0)) {
    *port = kPortDSI;
    *mode = kModeCommand;
  } else if ((strncmp(line.c_str(), "mipi dsi video panel", strlen("mipi dsi video panel")) == 0)) {
    *port = kPortDSI;
    *mode = kModeVideo;
  } else if ((strncmp(line.c_str(), "lvds panel", strlen("lvds panel")) == 0)) {
    *port = kPortLVDS;
    *mode = kModeVideo;
  } else if ((strncmp(line.c_str(), "edp panel", strlen("edp panel")) == 0)) {
    *port = kPortEDP;
    *mode = kModeVideo;
  } else if ((strncmp(line.c_str(), "dtv panel", strlen("dtv panel")) == 0)) {
    *port = kPortDTV;
    *mode = kModeVideo;
  } else if ((strncmp(line.c_str(), "writeback panel", strlen("writeback panel")) == 0)) {
    *port = kPortWriteBack;
    *mode = kModeCommand;
  } else if ((strncmp(line.c_str(), "dp panel", strlen("dp panel")) == 0)) {
    *port = kPortDP;
    *mode = kModeVideo;
  }

  return;
}

void HWDevice::GetSplitInfo(int device_node, HWPanelInfo *panel_info) {
  // Split info - for MDSS Version 5 - No need to check version here
  string file_name = fb_path_ + to_string(device_node) + "/msm_fb_split";

  Sys::fstream fs(file_name, fstream::in);
  if (!fs.is_open()) {
    DLOGW("File not found %s", file_name.c_str());
    return;
  }

  // Format "left right" space as delimiter
  uint32_t token_count = 0;
  const uint32_t max_count = 10;
  char *tokens[max_count] = { NULL };
  string line;
  if (Sys::getline_(fs, line)) {
    if (!ParseLine(line.c_str(), tokens, max_count, &token_count)) {
      panel_info->split_info.left_split = UINT32(atoi(tokens[0]));
      panel_info->split_info.right_split = UINT32(atoi(tokens[1]));
    }
  }
}

void HWDevice::GetHWPanelMaxBrightnessFromNode(HWPanelInfo *panel_info) {
  char brightness[kMaxStringLength] = { 0 };
  char kMaxBrightnessNode[64] = { 0 };

  snprintf(kMaxBrightnessNode, sizeof(kMaxBrightnessNode), "%s",
           "/sys/class/leds/lcd-backlight/max_brightness");

  panel_info->panel_max_brightness = 0;
  int fd = Sys::open_(kMaxBrightnessNode, O_RDONLY);
  if (fd < 0) {
    DLOGW("Failed to open max brightness node = %s, error = %s", kMaxBrightnessNode,
          strerror(errno));
    return;
  }

  if (Sys::pread_(fd, brightness, sizeof(brightness), 0) > 0) {
    panel_info->panel_max_brightness = atoi(brightness);
    DLOGI("Max brightness level = %d", panel_info->panel_max_brightness);
  } else {
    DLOGW("Failed to read max brightness level. error = %s", strerror(errno));
  }
  Sys::close_(fd);
}

int HWDevice::ParseLine(const char *input, char *tokens[], const uint32_t max_token,
                        uint32_t *count) {
  char *tmp_token = NULL;
  char *temp_ptr;
  uint32_t index = 0;
  const char *delim = ", =\n";
  if (!input) {
    return -1;
  }
  tmp_token = strtok_r(const_cast<char *>(input), delim, &temp_ptr);
  while (tmp_token && index < max_token) {
    tokens[index++] = tmp_token;
    tmp_token = strtok_r(NULL, delim, &temp_ptr);
  }
  *count = index;

  return 0;
}

int HWDevice::ParseLine(const char *input, const char *delim, char *tokens[],
                        const uint32_t max_token, uint32_t *count) {
  char *tmp_token = NULL;
  char *temp_ptr;
  uint32_t index = 0;
  if (!input) {
    return -1;
  }
  tmp_token = strtok_r(const_cast<char *>(input), delim, &temp_ptr);
  while (tmp_token && index < max_token) {
    tokens[index++] = tmp_token;
    tmp_token = strtok_r(NULL, delim, &temp_ptr);
  }
  *count = index;

  return 0;
}

bool HWDevice::EnableHotPlugDetection(int enable) {
  char hpdpath[kMaxStringLength];
  const char *value = enable ? "1" : "0";

  // Enable HPD for all pluggable devices.
  for (int i = 0; i < kFBNodeMax; i++) {
    HWPanelInfo panel_info;
    GetHWPanelInfoByNode(i, &panel_info);
    if (panel_info.is_pluggable == true) {
      snprintf(hpdpath , sizeof(hpdpath), "%s%d/hpd", fb_path_, i);

      ssize_t length = SysFsWrite(hpdpath, value, 1);
      if (length <= 0) {
        return false;
      }
    }
  }

  return true;
}

void HWDevice::ResetDisplayParams() {
  memset(&mdp_disp_commit_, 0, sizeof(mdp_disp_commit_));
  memset(&mdp_in_layers_, 0, sizeof(mdp_in_layers_));
  memset(&mdp_out_layer_, 0, sizeof(mdp_out_layer_));
  mdp_out_layer_.buffer.fence = -1;
  hw_scale_->ResetScaleParams();
  memset(&pp_params_, 0, sizeof(pp_params_));
  memset(&igc_lut_data_, 0, sizeof(igc_lut_data_));

  for (size_t i = 0; i < mdp_dest_scalar_data_.size(); i++) {
    mdp_dest_scalar_data_[i] = {};
  }

  for (uint32_t i = 0; i < kMaxSDELayers * 2; i++) {
    mdp_in_layers_[i].buffer.fence = -1;
  }

  mdp_disp_commit_.version = MDP_COMMIT_VERSION_1_0;
  mdp_disp_commit_.commit_v1.input_layers = mdp_in_layers_;
  mdp_disp_commit_.commit_v1.output_layer = &mdp_out_layer_;
  mdp_disp_commit_.commit_v1.release_fence = -1;
  mdp_disp_commit_.commit_v1.retire_fence = -1;
  mdp_disp_commit_.commit_v1.dest_scaler = mdp_dest_scalar_data_.data();
}

void HWDevice::SetCSC(const ColorMetaData &color_metadata, mdp_color_space *color_space) {
  switch (color_metadata.colorPrimaries) {
  case ColorPrimaries_BT601_6_525:
  case ColorPrimaries_BT601_6_625:
    *color_space = ((color_metadata.range == Range_Full) ? MDP_CSC_ITU_R_601_FR :
                                                           MDP_CSC_ITU_R_601);
    break;
  case ColorPrimaries_BT709_5:
    *color_space = MDP_CSC_ITU_R_709;
    break;
#if defined MDP_CSC_ITU_R_2020 && defined MDP_CSC_ITU_R_2020_FR
  case ColorPrimaries_BT2020:
    *color_space = static_cast<mdp_color_space>((color_metadata.range == Range_Full) ?
                                                 MDP_CSC_ITU_R_2020_FR : MDP_CSC_ITU_R_2020);
    break;
#endif
  default:
    break;
  }
}

void HWDevice::SetIGC(const LayerBuffer *layer_buffer, uint32_t index) {
  mdp_input_layer &mdp_layer = mdp_in_layers_[index];
  mdp_overlay_pp_params &pp_params = pp_params_[index];
  mdp_igc_lut_data_v1_7 &igc_lut_data = igc_lut_data_[index];

  switch (layer_buffer->igc) {
  case kIGCsRGB:
    igc_lut_data.table_fmt = mdp_igc_srgb;
    pp_params.igc_cfg.ops = MDP_PP_OPS_WRITE | MDP_PP_OPS_ENABLE;
    break;

  default:
    pp_params.igc_cfg.ops = MDP_PP_OPS_DISABLE;
    break;
  }

  pp_params.config_ops = MDP_OVERLAY_PP_IGC_CFG;
  pp_params.igc_cfg.version = mdp_igc_v1_7;
  pp_params.igc_cfg.cfg_payload = &igc_lut_data;

  mdp_layer.pp_info = &pp_params;
  mdp_layer.flags |= MDP_LAYER_PP;
}

DisplayError HWDevice::SetCursorPosition(HWLayers *hw_layers, int x, int y) {
  DTRACE_SCOPED();

  HWLayersInfo &hw_layer_info = hw_layers->info;
  uint32_t count = UINT32(hw_layer_info.hw_layers.size());
  uint32_t cursor_index = count - 1;
  HWPipeInfo *left_pipe = &hw_layers->config[cursor_index].left_pipe;

  mdp_async_layer async_layer = {};
  async_layer.flags = MDP_LAYER_ASYNC;
  async_layer.pipe_ndx = left_pipe->pipe_id;
  async_layer.src.x = UINT32(left_pipe->src_roi.left);
  async_layer.src.y = UINT32(left_pipe->src_roi.top);
  async_layer.dst.x = UINT32(left_pipe->dst_roi.left);
  async_layer.dst.y = UINT32(left_pipe->dst_roi.top);

  mdp_position_update pos_update = {};
  pos_update.input_layer_cnt = 1;
  pos_update.input_layers = &async_layer;
  if (Sys::ioctl_(device_fd_, INT(MSMFB_ASYNC_POSITION_UPDATE), &pos_update) < 0) {
    if (errno == ESHUTDOWN) {
      DLOGI_IF(kTagDriverConfig, "Driver is processing shutdown sequence");
      return kErrorShutDown;
    }
    IOCTL_LOGE(MSMFB_ASYNC_POSITION_UPDATE, device_type_);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWDevice::GetPPFeaturesVersion(PPFeatureVersion *vers) {
  return kErrorNotSupported;
}

DisplayError HWDevice::SetPPFeatures(PPFeaturesConfig *feature_list) {
  return kErrorNotSupported;
}

DisplayError HWDevice::SetVSyncState(bool enable) {
  int vsync_on = enable ? 1 : 0;
  if (Sys::ioctl_(device_fd_, MSMFB_OVERLAY_VSYNC_CTRL, &vsync_on) < 0) {
    if (errno == ESHUTDOWN) {
      DLOGI_IF(kTagDriverConfig, "Driver is processing shutdown sequence");
      return kErrorShutDown;
    }
    IOCTL_LOGE(MSMFB_OVERLAY_VSYNC_CTRL, device_type_);
    return kErrorHardware;
  }
  return kErrorNone;
}

void HWDevice::SetIdleTimeoutMs(uint32_t timeout_ms) {
}

DisplayError HWDevice::SetDisplayMode(const HWDisplayMode hw_display_mode) {
  return kErrorNotSupported;
}

DisplayError HWDevice::SetRefreshRate(uint32_t refresh_rate) {
  return kErrorNotSupported;
}

DisplayError HWDevice::SetPanelBrightness(int level) {
  return kErrorNotSupported;
}

DisplayError HWDevice::GetHWScanInfo(HWScanInfo *scan_info) {
  return kErrorNotSupported;
}

DisplayError HWDevice::GetVideoFormat(uint32_t config_index, uint32_t *video_format) {
  return kErrorNotSupported;
}

DisplayError HWDevice::GetMaxCEAFormat(uint32_t *max_cea_format) {
  return kErrorNotSupported;
}

DisplayError HWDevice::OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) {
  return kErrorNotSupported;
}

DisplayError HWDevice::GetPanelBrightness(int *level) {
  return kErrorNotSupported;
}

ssize_t HWDevice::SysFsWrite(const char* file_node, const char* value, ssize_t length) {
  int fd = Sys::open_(file_node, O_RDWR, 0);
  if (fd < 0) {
    DLOGW("Open failed = %s", file_node);
    return -1;
  }
  ssize_t len = Sys::pwrite_(fd, value, static_cast<size_t>(length), 0);
  if (len <= 0) {
    DLOGE("Write failed for path %s with value %s", file_node, value);
  }
  Sys::close_(fd);

  return len;
}

bool HWDevice::IsFBNodeConnected(int fb_node) {
  string file_name = fb_path_ + to_string(fb_node) + "/connected";

  Sys::fstream fs(file_name, fstream::in);
  if (!fs.is_open()) {
    DLOGW("File not found %s", file_name.c_str());
    return false;
  }

  string line;
  if (!Sys::getline_(fs, line)) {
    return false;
  }

  return atoi(line.c_str());
}

DisplayError HWDevice::SetS3DMode(HWS3DMode s3d_mode) {
  return kErrorNotSupported;
}

DisplayError HWDevice::SetScaleLutConfig(HWScaleLutInfo *lut_info) {
  mdp_scale_luts_info mdp_lut_info = {};
  mdp_set_cfg cfg = {};

  if (!hw_resource_.has_qseed3) {
    DLOGV_IF(kTagDriverConfig, "No support for QSEED3 luts");
    return kErrorNone;
  }

  if (!lut_info->dir_lut_size && !lut_info->dir_lut && !lut_info->cir_lut_size &&
      !lut_info->cir_lut && !lut_info->sep_lut_size && !lut_info->sep_lut) {
      // HWSupports QSEED3, but LutInfo is invalid as scalar is disabled by property or
      // its loading failed. Driver will use default settings/filter
      return kErrorNone;
  }

  mdp_lut_info.dir_lut_size = lut_info->dir_lut_size;
  mdp_lut_info.dir_lut = lut_info->dir_lut;
  mdp_lut_info.cir_lut_size = lut_info->cir_lut_size;
  mdp_lut_info.cir_lut = lut_info->cir_lut;
  mdp_lut_info.sep_lut_size = lut_info->sep_lut_size;
  mdp_lut_info.sep_lut = lut_info->sep_lut;

  cfg.flags = MDP_QSEED3_LUT_CFG;
  cfg.len = sizeof(mdp_scale_luts_info);
  cfg.payload = reinterpret_cast<uint64_t>(&mdp_lut_info);

  if (Sys::ioctl_(device_fd_, MSMFB_MDP_SET_CFG, &cfg) < 0) {
    if (errno == ESHUTDOWN) {
      DLOGI_IF(kTagDriverConfig, "Driver is processing shutdown sequence");
      return kErrorShutDown;
    }
    IOCTL_LOGE(MSMFB_MDP_SET_CFG, device_type_);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWDevice::SetMixerAttributes(HWMixerAttributes &mixer_attributes) {

  if (!hw_resource_.hw_dest_scalar_info.count) {
    return kErrorNotSupported;
  }
  uint32_t align_x = display_attributes_.is_device_split ? 4 : 2;
  uint32_t align_y = 2;

  float scale_x = FLOAT(display_attributes_.x_pixels) / FLOAT(mixer_attributes.width);
  float scale_y = FLOAT(display_attributes_.y_pixels) / FLOAT(mixer_attributes.height);
  float max_scale_up = hw_resource_.hw_dest_scalar_info.max_scale_up;

  if (scale_x > max_scale_up) {
    DLOGW_IF(kTagDriverConfig, "Up scaling ratio exceeds for destination scalar upscale " \
             "limit scale_x %f  max_scale_up %f", scale_x, max_scale_up);
    mixer_attributes.width = UINT32(FLOAT(display_attributes_.x_pixels) / max_scale_up);
  }
  if (scale_y > max_scale_up) {
    DLOGW_IF(kTagDriverConfig, "Up scaling ratio exceeds for destination scalar upscale " \
             "limit scale_y %f  max_scale_up %f", scale_y, max_scale_up);
    mixer_attributes.height = UINT32(FLOAT(display_attributes_.y_pixels) / max_scale_up);
  }
  if (mixer_attributes.width > display_attributes_.x_pixels) {
    mixer_attributes.width = display_attributes_.x_pixels;
    DLOGW_IF(kTagDriverConfig, "Input mixer width exceeds display width! input: res %d "\
             "display: res %d", mixer_attributes.width, display_attributes_.x_pixels);
  }
  if(mixer_attributes.height > display_attributes_.y_pixels) {
    mixer_attributes.height = display_attributes_.y_pixels;
    DLOGW_IF(kTagDriverConfig, "Input mixer height exceeds display height! input: res %d "\
             "display: res %d", mixer_attributes.height, display_attributes_.y_pixels);
  }

  uint32_t max_input_width = hw_resource_.hw_dest_scalar_info.max_input_width;
  if (display_attributes_.is_device_split) {
    max_input_width *= 2;
  }

  if (mixer_attributes.width > max_input_width) {
    DLOGW_IF(kTagDriverConfig, "Input width exceeds width limit! input_width %d width_limit %d",
             mixer_attributes.width, max_input_width);
    mixer_attributes.width = max_input_width;
  }

  float mixer_aspect_ratio = FLOAT(mixer_attributes.width) / FLOAT(mixer_attributes.height);
  float display_aspect_ratio =
    FLOAT(display_attributes_.x_pixels) / FLOAT(display_attributes_.y_pixels);

  if (display_aspect_ratio != mixer_aspect_ratio) {
    DLOGW_IF(kTagDriverConfig, "Aspect ratio mismatch! input: res %dx%d display: res %dx%d",
             mixer_attributes.width, mixer_attributes.height, display_attributes_.x_pixels,
             display_attributes_.y_pixels);
    uint32_t new_mixer_width = FloorToMultipleOf(UINT32((display_aspect_ratio) *
                                                 mixer_attributes.height), align_x);

    if (new_mixer_width > max_input_width || new_mixer_width > display_attributes_.x_pixels) {
      uint32_t new_mixer_height = FloorToMultipleOf(UINT32(FLOAT(mixer_attributes.width) /
                                  display_aspect_ratio), align_y);
      if (new_mixer_height > display_attributes_.y_pixels) {
        mixer_attributes.width = display_attributes_.x_pixels;
        mixer_attributes.height = display_attributes_.y_pixels;
      } else {
        mixer_attributes.height = new_mixer_height;
        mixer_aspect_ratio = FLOAT(mixer_attributes.width) / FLOAT(mixer_attributes.height);
        if (display_aspect_ratio != mixer_aspect_ratio) {
          uint32_t new_mixer_width = FloorToMultipleOf(UINT32((display_aspect_ratio) *
                                                      mixer_attributes.height), align_x);
          if (new_mixer_width > max_input_width || new_mixer_width > display_attributes_.x_pixels) {
            mixer_attributes.width = display_attributes_.x_pixels;
            mixer_attributes.height = display_attributes_.y_pixels;
          } else {
            mixer_attributes.width = new_mixer_width;
          }
        }
      }
    } else {
      mixer_attributes.width = new_mixer_width;
      mixer_aspect_ratio = FLOAT(mixer_attributes.width) / FLOAT(mixer_attributes.height);
      if (display_aspect_ratio != mixer_aspect_ratio) {
        uint32_t new_mixer_height = FloorToMultipleOf(UINT32(FLOAT(mixer_attributes.width) /
                                                      display_aspect_ratio), align_y);
        if (new_mixer_height > display_attributes_.y_pixels) {
          mixer_attributes.width = display_attributes_.x_pixels;
          mixer_attributes.height = display_attributes_.y_pixels;
        } else {
           mixer_attributes.height = new_mixer_height;
        }
      }
    }
  }

  float mixer_split_ratio = FLOAT(mixer_attributes_.split_left) / FLOAT(mixer_attributes_.width);

  mixer_attributes_ = mixer_attributes;
  mixer_attributes_.split_left = mixer_attributes_.width;
  if (display_attributes_.is_device_split) {
    mixer_attributes_.split_left = UINT32(FLOAT(mixer_attributes.width) * mixer_split_ratio);
  }

  DLOGV_IF(kTagDriverConfig, "New mixer wxh = %dx%d", mixer_attributes_.width, mixer_attributes_.height);
  return kErrorNone;
}

DisplayError HWDevice::GetMixerAttributes(HWMixerAttributes *mixer_attributes) {
  if (!mixer_attributes) {
    return kErrorParameters;
  }

  *mixer_attributes = mixer_attributes_;

  return kErrorNone;
}

DisplayError HWDevice::SetDynamicDSIClock(uint64_t bitclk) {
  return kErrorNotSupported;
}

DisplayError HWDevice::SetConfigAttributes(uint32_t index, uint32_t width, uint32_t height) {
  return kErrorNone;
}

DisplayError HWDevice::GetDynamicDSIClock(uint64_t *bitclk) {
  return kErrorNotSupported;
}

DisplayError HWDevice::SetDisplayFormat(uint32_t index, DisplayInterfaceFormat fmt) {
  return kErrorNotSupported;
}

}  // namespace sdm

