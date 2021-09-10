/*
* Copyright (c) 2017 - 2018, 2020, The Linux Foundation. All rights reserved.
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

#include <ctype.h>
#include <drm/drm_fourcc.h>
#include <drm_lib_loader.h>
#include <drm_master.h>
#include <drm_res_mgr.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/fb.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <utils/formats.h>
#include <utils/sys.h>
#include <private/color_params.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hw_device_drm.h"
#include "hw_info_interface.h"
#include "hw_color_manager_drm.h"

#define __CLASS__ "HWDeviceDRM"

#ifndef DRM_FORMAT_MOD_QCOM_COMPRESSED
#define DRM_FORMAT_MOD_QCOM_COMPRESSED fourcc_mod_code(QCOM, 1)
#endif
#ifndef DRM_FORMAT_MOD_QCOM_DX
#define DRM_FORMAT_MOD_QCOM_DX fourcc_mod_code(QCOM, 0x2)
#endif
#ifndef DRM_FORMAT_MOD_QCOM_TIGHT
#define DRM_FORMAT_MOD_QCOM_TIGHT fourcc_mod_code(QCOM, 0x4)
#endif

using std::string;
using std::to_string;
using std::fstream;
using std::unordered_map;
using drm_utils::DRMMaster;
using drm_utils::DRMResMgr;
using drm_utils::DRMLibLoader;
using drm_utils::DRMBuffer;
using sde_drm::GetDRMManager;
using sde_drm::DestroyDRMManager;
using sde_drm::DRMDisplayType;
using sde_drm::DRMDisplayToken;
using sde_drm::DRMConnectorInfo;
using sde_drm::DRMPPFeatureInfo;
using sde_drm::DRMRect;
using sde_drm::DRMRotation;
using sde_drm::DRMBlendType;
using sde_drm::DRMSrcConfig;
using sde_drm::DRMOps;
using sde_drm::DRMTopology;

namespace sdm {

static void GetDRMFormat(LayerBufferFormat format, uint32_t *drm_format,
                         uint64_t *drm_format_modifier) {
  switch (format) {
    case kFormatRGBA8888:
      *drm_format = DRM_FORMAT_ABGR8888;
      break;
    case kFormatRGBA8888Ubwc:
      *drm_format = DRM_FORMAT_ABGR8888;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
      break;
    case kFormatRGBA5551:
      *drm_format = DRM_FORMAT_ABGR1555;
      break;
    case kFormatRGBA4444:
      *drm_format = DRM_FORMAT_ABGR4444;
      break;
    case kFormatBGRA8888:
      *drm_format = DRM_FORMAT_ARGB8888;
      break;
    case kFormatRGBX8888:
      *drm_format = DRM_FORMAT_XBGR8888;
      break;
    case kFormatRGBX8888Ubwc:
      *drm_format = DRM_FORMAT_XBGR8888;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
      break;
    case kFormatBGRX8888:
      *drm_format = DRM_FORMAT_XRGB8888;
      break;
    case kFormatRGB888:
      *drm_format = DRM_FORMAT_BGR888;
      break;
    case kFormatRGB565:
      *drm_format = DRM_FORMAT_BGR565;
      break;
    case kFormatBGR565:
      *drm_format = DRM_FORMAT_RGB565;
      break;
    case kFormatBGR565Ubwc:
      *drm_format = DRM_FORMAT_BGR565;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
      break;
    case kFormatRGBA1010102:
      *drm_format = DRM_FORMAT_ABGR2101010;
      break;
    case kFormatRGBA1010102Ubwc:
      *drm_format = DRM_FORMAT_ABGR2101010;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
      break;
    case kFormatARGB2101010:
      *drm_format = DRM_FORMAT_BGRA1010102;
      break;
    case kFormatRGBX1010102:
      *drm_format = DRM_FORMAT_XBGR2101010;
      break;
    case kFormatRGBX1010102Ubwc:
      *drm_format = DRM_FORMAT_XBGR2101010;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
      break;
    case kFormatXRGB2101010:
      *drm_format = DRM_FORMAT_BGRX1010102;
      break;
    case kFormatBGRA1010102:
      *drm_format = DRM_FORMAT_ARGB2101010;
      break;
    case kFormatABGR2101010:
      *drm_format = DRM_FORMAT_RGBA1010102;
      break;
    case kFormatBGRX1010102:
      *drm_format = DRM_FORMAT_XRGB2101010;
      break;
    case kFormatXBGR2101010:
      *drm_format = DRM_FORMAT_RGBX1010102;
      break;
    case kFormatYCbCr420SemiPlanar:
      *drm_format = DRM_FORMAT_NV12;
      break;
    case kFormatYCbCr420SemiPlanarVenus:
      *drm_format = DRM_FORMAT_NV12;
      break;
    case kFormatYCbCr420SPVenusUbwc:
      *drm_format = DRM_FORMAT_NV12;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
      break;
    case kFormatYCrCb420SemiPlanar:
      *drm_format = DRM_FORMAT_NV21;
      break;
    case kFormatYCrCb420SemiPlanarVenus:
      *drm_format = DRM_FORMAT_NV21;
      break;
    case kFormatYCbCr420P010:
      *drm_format = DRM_FORMAT_NV12;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_DX;
      break;
    case kFormatYCbCr420P010Ubwc:
      *drm_format = DRM_FORMAT_NV12;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED |
        DRM_FORMAT_MOD_QCOM_DX;
      break;
    case kFormatYCbCr420TP10Ubwc:
      *drm_format = DRM_FORMAT_NV12;
      *drm_format_modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED |
        DRM_FORMAT_MOD_QCOM_DX | DRM_FORMAT_MOD_QCOM_TIGHT;
      break;
    case kFormatYCbCr422H2V1SemiPlanar:
      *drm_format = DRM_FORMAT_NV16;
      break;
    case kFormatYCrCb422H2V1SemiPlanar:
      *drm_format = DRM_FORMAT_NV61;
      break;
    case kFormatYCrCb420PlanarStride16:
      *drm_format = DRM_FORMAT_YVU420;
      break;
    default:
      DLOGW("Unsupported format %s", GetFormatString(format));
  }
}

void HWDeviceDRM::Registry::RegisterCurrent(HWLayers *hw_layers) {
  DRMMaster *master = nullptr;
  DRMMaster::GetInstance(&master);

  if (!master) {
    DLOGE("Failed to acquire DRM Master instance");
    return;
  }

  HWLayersInfo &hw_layer_info = hw_layers->info;
  uint32_t hw_layer_count = UINT32(hw_layer_info.hw_layers.size());

  for (uint32_t i = 0; i < hw_layer_count; i++) {
    Layer &layer = hw_layer_info.hw_layers.at(i);
    LayerBuffer *input_buffer = &layer.input_buffer;
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;
    HWRotateInfo *hw_rotate_info = &hw_rotator_session->hw_rotate_info[0];

    if (hw_rotate_info->valid) {
      input_buffer = &hw_rotator_session->output_buffer;
    }

    int fd = input_buffer->planes[0].fd;
    if (fd >= 0 && hashmap_[current_index_].find(fd) == hashmap_[current_index_].end()) {
      AllocatedBufferInfo buf_info {};
      DRMBuffer layout {};
      buf_info.fd = layout.fd = fd;
      buf_info.aligned_width = layout.width = input_buffer->width;
      buf_info.aligned_height = layout.height = input_buffer->height;
      buf_info.format = input_buffer->format;
      GetDRMFormat(buf_info.format, &layout.drm_format, &layout.drm_format_modifier);
      buffer_allocator_->GetBufferLayout(buf_info, layout.stride, layout.offset,
                                         &layout.num_planes);
      uint32_t fb_id = 0;
      int ret = master->CreateFbId(layout, &fb_id);
      if (ret < 0) {
        DLOGE("CreateFbId failed. width %d, height %d, format: %s, stride %u, error %d",
              layout.width, layout.height, GetFormatString(buf_info.format), layout.stride[0],
              errno);
      } else {
        hashmap_[current_index_][fd] = fb_id;
      }
    }
  }
}

void HWDeviceDRM::Registry::UnregisterNext() {
  DRMMaster *master = nullptr;
  DRMMaster::GetInstance(&master);

  if (!master) {
    DLOGE("Failed to acquire DRM Master instance");
    return;
  }

  current_index_ = (current_index_ + 1) % kCycleDelay;
  auto &curr_map = hashmap_[current_index_];
  for (auto &pair : curr_map) {
    uint32_t fb_id = pair.second;
    int ret = master->RemoveFbId(fb_id);
    if (ret < 0) {
      DLOGE("Removing fb_id %d failed with error %d", fb_id, errno);
    }
  }

  curr_map.clear();
}

void HWDeviceDRM::Registry::Clear() {
  for (int i = 0; i < kCycleDelay; i++) {
    UnregisterNext();
  }
  current_index_ = 0;
}

uint32_t HWDeviceDRM::Registry::GetFbId(int fd) {
  auto it = hashmap_[current_index_].find(fd);
  return (it == hashmap_[current_index_].end()) ? 0 : it->second;
}

HWDeviceDRM::HWDeviceDRM(BufferSyncHandler *buffer_sync_handler, BufferAllocator *buffer_allocator,
                         HWInfoInterface *hw_info_intf)
    : hw_info_intf_(hw_info_intf), buffer_sync_handler_(buffer_sync_handler),
      registry_(buffer_allocator) {
  device_type_ = kDevicePrimary;
  device_name_ = "Peripheral Display";
  hw_info_intf_ = hw_info_intf;
}

DisplayError HWDeviceDRM::Init() {
  DRMLibLoader *drm_lib = DRMLibLoader::GetInstance();
  if (drm_lib == nullptr) {
    DLOGE("Failed to load DRM Lib");
    return kErrorResources;
  }
  default_mode_ = (drm_lib->IsLoaded() == false);

  if (!default_mode_) {
    DRMMaster *drm_master = {};
    int dev_fd = -1;
    DRMMaster::GetInstance(&drm_master);
    drm_master->GetHandle(&dev_fd);
    drm_lib->FuncGetDRMManager()(dev_fd, &drm_mgr_intf_);
    if (drm_mgr_intf_->RegisterDisplay(DRMDisplayType::PERIPHERAL, &token_)) {
      DLOGE("RegisterDisplay failed");
      return kErrorResources;
    }

    drm_mgr_intf_->CreateAtomicReq(token_, &drm_atomic_intf_);
    drm_mgr_intf_->GetConnectorInfo(token_.conn_id, &connector_info_);
    InitializeConfigs();
    drm_atomic_intf_->Perform(DRMOps::CRTC_SET_MODE, token_.crtc_id, &current_mode_);

    drm_atomic_intf_->Perform(DRMOps::CRTC_SET_OUTPUT_FENCE_OFFSET, token_.crtc_id, 1);

    // TODO(user): Enable this and remove the one in SetupAtomic() onces underruns are fixed
    // drm_atomic_intf_->Perform(DRMOps::CRTC_SET_ACTIVE, token_.crtc_id, 1);
    // Commit to setup pipeline with mode, which then tells us the topology etc
    if (drm_atomic_intf_->Commit(true /* synchronous */)) {
      DLOGE("Setting up CRTC %d, Connector %d for %s failed", token_.crtc_id, token_.conn_id,
            device_name_);
      return kErrorResources;
    }

    // Reload connector info for updated info after 1st commit
    drm_mgr_intf_->GetConnectorInfo(token_.conn_id, &connector_info_);
    DLOGI("Setup CRTC %d, Connector %d for %s", token_.crtc_id, token_.conn_id, device_name_);
  }

  PopulateDisplayAttributes();
  PopulateHWPanelInfo();
  UpdateMixerAttributes();
  hw_info_intf_->GetHWResourceInfo(&hw_resource_);

  // TODO(user): In future, remove has_qseed3 member, add version and pass version to constructor
  if (hw_resource_.has_qseed3) {
    hw_scale_ = new HWScaleDRM(HWScaleDRM::Version::V2);
  }

  return kErrorNone;
}

DisplayError HWDeviceDRM::Deinit() {
  delete hw_scale_;
  registry_.Clear();
  drm_mgr_intf_->DestroyAtomicReq(drm_atomic_intf_);
  drm_atomic_intf_ = {};
  drm_mgr_intf_->UnregisterDisplay(token_);
  return kErrorNone;
}

void HWDeviceDRM::InitializeConfigs() {
  // TODO(user): Update modes
  current_mode_ = connector_info_.modes[0];
}

DisplayError HWDeviceDRM::PopulateDisplayAttributes() {
  drmModeModeInfo mode = {};
  uint32_t mm_width = 0;
  uint32_t mm_height = 0;
  DRMTopology topology = DRMTopology::SINGLE_LM;

  if (default_mode_) {
    DRMResMgr *res_mgr = nullptr;
    int ret = DRMResMgr::GetInstance(&res_mgr);
    if (ret < 0) {
      DLOGE("Failed to acquire DRMResMgr instance");
      return kErrorResources;
    }

    res_mgr->GetMode(&mode);
    res_mgr->GetDisplayDimInMM(&mm_width, &mm_height);
  } else {
    mode = current_mode_;
    mm_width = connector_info_.mmWidth;
    mm_height = connector_info_.mmHeight;
    topology = connector_info_.topology;
  }

  display_attributes_.x_pixels = mode.hdisplay;
  display_attributes_.y_pixels = mode.vdisplay;
  display_attributes_.fps = mode.vrefresh;
  display_attributes_.vsync_period_ns = UINT32(1000000000L / display_attributes_.fps);

  /*
              Active                 Front           Sync           Back
              Region                 Porch                          Porch
     <-----------------------><----------------><-------------><-------------->
     <----- [hv]display ----->
     <------------- [hv]sync_start ------------>
     <--------------------- [hv]sync_end --------------------->
     <-------------------------------- [hv]total ----------------------------->
   */

  display_attributes_.v_front_porch = mode.vsync_start - mode.vdisplay;
  display_attributes_.v_pulse_width = mode.vsync_end - mode.vsync_start;
  display_attributes_.v_back_porch = mode.vtotal - mode.vsync_end;
  display_attributes_.v_total = mode.vtotal;

  display_attributes_.h_total = mode.htotal;
  uint32_t h_blanking = mode.htotal - mode.hdisplay;
  display_attributes_.is_device_split =
      (topology == DRMTopology::DUAL_LM || topology == DRMTopology::DUAL_LM_MERGE);
  display_attributes_.h_total += display_attributes_.is_device_split ? h_blanking : 0;

  display_attributes_.x_dpi = (FLOAT(mode.hdisplay) * 25.4f) / FLOAT(mm_width);
  display_attributes_.y_dpi = (FLOAT(mode.vdisplay) * 25.4f) / FLOAT(mm_height);

  return kErrorNone;
}

void HWDeviceDRM::PopulateHWPanelInfo() {
  hw_panel_info_ = {};

  snprintf(hw_panel_info_.panel_name, sizeof(hw_panel_info_.panel_name), "%s",
           connector_info_.panel_name.c_str());
  hw_panel_info_.split_info.left_split = display_attributes_.x_pixels;
  if (display_attributes_.is_device_split) {
    hw_panel_info_.split_info.left_split = hw_panel_info_.split_info.right_split =
        display_attributes_.x_pixels / 2;
  }

  hw_panel_info_.partial_update = 0;
  hw_panel_info_.left_align = 0;
  hw_panel_info_.width_align = 0;
  hw_panel_info_.top_align = 0;
  hw_panel_info_.height_align = 0;
  hw_panel_info_.min_roi_width = 0;
  hw_panel_info_.min_roi_height = 0;
  hw_panel_info_.needs_roi_merge = 0;
  hw_panel_info_.dynamic_fps = connector_info_.dynamic_fps;
  hw_panel_info_.min_fps = 60;
  hw_panel_info_.max_fps = 60;
  hw_panel_info_.is_primary_panel = connector_info_.is_primary;
  hw_panel_info_.is_pluggable = 0;

  if (!default_mode_) {
    hw_panel_info_.needs_roi_merge = (connector_info_.topology == DRMTopology::DUAL_LM_MERGE);
  }

  GetHWDisplayPortAndMode();
  GetHWPanelMaxBrightness();

  DLOGI("%s, Panel Interface = %s, Panel Mode = %s, Is Primary = %d", device_name_,
        interface_str_.c_str(), hw_panel_info_.mode == kModeVideo ? "Video" : "Command",
        hw_panel_info_.is_primary_panel);
  DLOGI("Partial Update = %d, Dynamic FPS = %d", hw_panel_info_.partial_update,
        hw_panel_info_.dynamic_fps);
  DLOGI("Align: left = %d, width = %d, top = %d, height = %d", hw_panel_info_.left_align,
        hw_panel_info_.width_align, hw_panel_info_.top_align, hw_panel_info_.height_align);
  DLOGI("ROI: min_width = %d, min_height = %d, need_merge = %d", hw_panel_info_.min_roi_width,
        hw_panel_info_.min_roi_height, hw_panel_info_.needs_roi_merge);
  DLOGI("FPS: min = %d, max =%d", hw_panel_info_.min_fps, hw_panel_info_.max_fps);
  DLOGI("Left Split = %d, Right Split = %d", hw_panel_info_.split_info.left_split,
        hw_panel_info_.split_info.right_split);
}

void HWDeviceDRM::GetHWDisplayPortAndMode() {
  hw_panel_info_.port = kPortDefault;
  hw_panel_info_.mode =
      (connector_info_.panel_mode == sde_drm::DRMPanelMode::VIDEO) ? kModeVideo : kModeCommand;

  if (default_mode_) {
    return;
  }

  switch (connector_info_.type) {
    case DRM_MODE_CONNECTOR_DSI:
      hw_panel_info_.port = kPortDSI;
      interface_str_ = "DSI";
      break;
    case DRM_MODE_CONNECTOR_LVDS:
      hw_panel_info_.port = kPortLVDS;
      interface_str_ = "LVDS";
      break;
    case DRM_MODE_CONNECTOR_eDP:
      hw_panel_info_.port = kPortEDP;
      interface_str_ = "EDP";
      break;
    case DRM_MODE_CONNECTOR_TV:
    case DRM_MODE_CONNECTOR_HDMIA:
    case DRM_MODE_CONNECTOR_HDMIB:
      hw_panel_info_.port = kPortDTV;
      interface_str_ = "HDMI";
      break;
    case DRM_MODE_CONNECTOR_VIRTUAL:
      hw_panel_info_.port = kPortWriteBack;
      interface_str_ = "Virtual";
      break;
    case DRM_MODE_CONNECTOR_DisplayPort:
      // TODO(user): Add when available
      interface_str_ = "DisplayPort";
      break;
  }

  return;
}

void HWDeviceDRM::GetHWPanelMaxBrightness() {
  char brightness[kMaxStringLength] = {0};
  string kMaxBrightnessNode = "/sys/class/backlight/panel0-backlight/max_brightness";

  hw_panel_info_.panel_max_brightness = 255;
  int fd = Sys::open_(kMaxBrightnessNode.c_str(), O_RDONLY);
  if (fd < 0) {
    DLOGW("Failed to open max brightness node = %s, error = %s", kMaxBrightnessNode.c_str(),
          strerror(errno));
    return;
  }

  if (Sys::pread_(fd, brightness, sizeof(brightness), 0) > 0) {
    hw_panel_info_.panel_max_brightness = atoi(brightness);
    DLOGI("Max brightness level = %d", hw_panel_info_.panel_max_brightness);
  } else {
    DLOGW("Failed to read max brightness level. error = %s", strerror(errno));
  }

  Sys::close_(fd);
}

DisplayError HWDeviceDRM::GetActiveConfig(uint32_t *active_config) {
  *active_config = 0;
  return kErrorNone;
}

DisplayError HWDeviceDRM::GetNumDisplayAttributes(uint32_t *count) {
  *count = 1;
  return kErrorNone;
}

DisplayError HWDeviceDRM::GetDisplayAttributes(uint32_t index,
                                            HWDisplayAttributes *display_attributes) {
  *display_attributes = display_attributes_;
  return kErrorNone;
}

DisplayError HWDeviceDRM::GetHWPanelInfo(HWPanelInfo *panel_info) {
  *panel_info = hw_panel_info_;
  return kErrorNone;
}

DisplayError HWDeviceDRM::SetDisplayAttributes(uint32_t index) {
  return kErrorNone;
}

DisplayError HWDeviceDRM::SetDisplayAttributes(const HWDisplayAttributes &display_attributes) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::GetConfigIndex(uint32_t mode, uint32_t *index) {
  return kErrorNone;
}

DisplayError HWDeviceDRM::PowerOn() {
  DTRACE_SCOPED();
  return kErrorNone;
}

DisplayError HWDeviceDRM::PowerOff() {
  return kErrorNone;
}

DisplayError HWDeviceDRM::Doze() {
  return kErrorNone;
}

DisplayError HWDeviceDRM::DozeSuspend() {
  return kErrorNone;
}

DisplayError HWDeviceDRM::Standby() {
  return kErrorNone;
}

void HWDeviceDRM::SetupAtomic(HWLayers *hw_layers, bool validate) {
  if (default_mode_) {
    return;
  }

  HWLayersInfo &hw_layer_info = hw_layers->info;
  uint32_t hw_layer_count = UINT32(hw_layer_info.hw_layers.size());

  for (uint32_t i = 0; i < hw_layer_count; i++) {
    Layer &layer = hw_layer_info.hw_layers.at(i);
    LayerBuffer *input_buffer = &layer.input_buffer;
    HWPipeInfo *left_pipe = &hw_layers->config[i].left_pipe;
    HWPipeInfo *right_pipe = &hw_layers->config[i].right_pipe;
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;
    bool needs_rotation = false;

    for (uint32_t count = 0; count < 2; count++) {
      HWPipeInfo *pipe_info = (count == 0) ? left_pipe : right_pipe;
      HWRotateInfo *hw_rotate_info = &hw_rotator_session->hw_rotate_info[count];

      if (hw_rotate_info->valid) {
        input_buffer = &hw_rotator_session->output_buffer;
        needs_rotation = true;
      }

      uint32_t fb_id = registry_.GetFbId(input_buffer->planes[0].fd);
      if (pipe_info->valid && fb_id) {
        uint32_t pipe_id = pipe_info->pipe_id;
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_ALPHA, pipe_id, layer.plane_alpha);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_ZORDER, pipe_id, pipe_info->z_order);
        DRMBlendType blending = {};
        SetBlending(layer.blending, &blending);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_BLEND_TYPE, pipe_id, blending);
        DRMRect src = {};
        SetRect(pipe_info->src_roi, &src);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_SRC_RECT, pipe_id, src);
        DRMRect dst = {};
        SetRect(pipe_info->dst_roi, &dst);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_DST_RECT, pipe_id, dst);

        uint32_t rot_bit_mask = 0;
        // In case of rotation, rotator handles flips
        if (!needs_rotation) {
          if (layer.transform.flip_horizontal) {
            rot_bit_mask |= UINT32(DRMRotation::FLIP_H);
          }
          if (layer.transform.flip_vertical) {
            rot_bit_mask |= UINT32(DRMRotation::FLIP_V);
          }
        }

        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_ROTATION, pipe_id, rot_bit_mask);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_H_DECIMATION, pipe_id,
                                  pipe_info->horizontal_decimation);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_V_DECIMATION, pipe_id,
                                  pipe_info->vertical_decimation);
        uint32_t config = 0;
        SetSrcConfig(layer.input_buffer, &config);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_SRC_CONFIG, pipe_id, config);;
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_FB_ID, pipe_id, fb_id);
        drm_atomic_intf_->Perform(DRMOps::PLANE_SET_CRTC, pipe_id, token_.crtc_id);
        if (!validate && input_buffer->acquire_fence_fd >= 0) {
          drm_atomic_intf_->Perform(DRMOps::PLANE_SET_INPUT_FENCE, pipe_id,
                                    input_buffer->acquire_fence_fd);
        }
        if (hw_scale_) {
          SDEScaler scaler_output = {};
          hw_scale_->SetPlaneScaler(pipe_info->scale_data, &scaler_output);
          // TODO(user): Remove qseed3 and add version check, then send appropriate scaler object
          if (hw_resource_.has_qseed3) {
            drm_atomic_intf_->Perform(DRMOps::PLANE_SET_SCALER_CONFIG, pipe_id,
                                      reinterpret_cast<uint64_t>(&scaler_output.scaler_v2));
          }
        }
      }
    }

    // TODO(user): Remove this and enable the one in Init() onces underruns are fixed
    drm_atomic_intf_->Perform(DRMOps::CRTC_SET_ACTIVE, token_.crtc_id, 1);
  }
}

DisplayError HWDeviceDRM::Validate(HWLayers *hw_layers) {
  DTRACE_SCOPED();

  registry_.RegisterCurrent(hw_layers);
  SetupAtomic(hw_layers, true /* validate */);

  int ret = drm_atomic_intf_->Validate();
  if (ret) {
    DLOGE("failed with error %d for %s", ret, device_name_);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWDeviceDRM::Commit(HWLayers *hw_layers) {
  DTRACE_SCOPED();

  DisplayError err = kErrorNone;
  registry_.RegisterCurrent(hw_layers);

  if (default_mode_) {
    err = DefaultCommit(hw_layers);
  } else {
    err = AtomicCommit(hw_layers);
  }

  registry_.UnregisterNext();

  return err;
}

DisplayError HWDeviceDRM::DefaultCommit(HWLayers *hw_layers) {
  DTRACE_SCOPED();

  HWLayersInfo &hw_layer_info = hw_layers->info;
  LayerStack *stack = hw_layer_info.stack;

  stack->retire_fence_fd = -1;
  for (Layer &layer : hw_layer_info.hw_layers) {
    layer.input_buffer.release_fence_fd = -1;
  }

  DRMMaster *master = nullptr;
  int ret = DRMMaster::GetInstance(&master);
  if (ret < 0) {
    DLOGE("Failed to acquire DRMMaster instance");
    return kErrorResources;
  }

  DRMResMgr *res_mgr = nullptr;
  ret = DRMResMgr::GetInstance(&res_mgr);
  if (ret < 0) {
    DLOGE("Failed to acquire DRMResMgr instance");
    return kErrorResources;
  }

  int dev_fd = -1;
  master->GetHandle(&dev_fd);

  uint32_t connector_id = 0;
  res_mgr->GetConnectorId(&connector_id);

  uint32_t crtc_id = 0;
  res_mgr->GetCrtcId(&crtc_id);

  drmModeModeInfo mode;
  res_mgr->GetMode(&mode);

  uint32_t fb_id = registry_.GetFbId(hw_layer_info.hw_layers.at(0).input_buffer.planes[0].fd);
  ret = drmModeSetCrtc(dev_fd, crtc_id, fb_id, 0 /* x */, 0 /* y */, &connector_id,
                       1 /* num_connectors */, &mode);
  if (ret < 0) {
    DLOGE("drmModeSetCrtc failed dev fd %d, fb_id %d, crtc id %d, connector id %d, %s", dev_fd,
          fb_id, crtc_id, connector_id, strerror(errno));
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWDeviceDRM::AtomicCommit(HWLayers *hw_layers) {
  DTRACE_SCOPED();
  SetupAtomic(hw_layers, false /* validate */);

  int ret = drm_atomic_intf_->Commit(false /* synchronous */);
  if (ret) {
    DLOGE("%s failed with error %d", __FUNCTION__, ret);
    return kErrorHardware;
  }

  int release_fence = -1;
  int retire_fence = -1;

  drm_atomic_intf_->Perform(DRMOps::CRTC_GET_RELEASE_FENCE, token_.crtc_id, &release_fence);
  drm_atomic_intf_->Perform(DRMOps::CONNECTOR_GET_RETIRE_FENCE, token_.conn_id, &retire_fence);

  HWLayersInfo &hw_layer_info = hw_layers->info;
  LayerStack *stack = hw_layer_info.stack;
  stack->retire_fence_fd = retire_fence;

  for (uint32_t i = 0; i < hw_layer_info.hw_layers.size(); i++) {
    Layer &layer = hw_layer_info.hw_layers.at(i);
    HWRotatorSession *hw_rotator_session = &hw_layers->config[i].hw_rotator_session;
    if (hw_rotator_session->hw_block_count) {
      hw_rotator_session->output_buffer.release_fence_fd = Sys::dup_(release_fence);
    } else {
      layer.input_buffer.release_fence_fd = Sys::dup_(release_fence);
    }
  }

  hw_layer_info.sync_handle = release_fence;

  return kErrorNone;
}

DisplayError HWDeviceDRM::Flush(bool secure) {
  return kErrorNone;
}

void HWDeviceDRM::SetBlending(const LayerBlending &source, DRMBlendType *target) {
  switch (source) {
    case kBlendingPremultiplied:
      *target = DRMBlendType::PREMULTIPLIED;
      break;
    case kBlendingOpaque:
      *target = DRMBlendType::OPAQUE;
      break;
    case kBlendingCoverage:
      *target = DRMBlendType::COVERAGE;
      break;
    default:
      *target = DRMBlendType::UNDEFINED;
  }
}


void HWDeviceDRM::SetSrcConfig(const LayerBuffer &input_buffer, uint32_t *config) {
  if (input_buffer.flags.interlace) {
    *config |= (0x01 << UINT32(DRMSrcConfig::DEINTERLACE));
  }
}

void HWDeviceDRM::SetRect(const LayerRect &source, DRMRect *target) {
  target->left = UINT32(source.left);
  target->top = UINT32(source.top);
  target->right = UINT32(source.right);
  target->bottom = UINT32(source.bottom);
}

bool HWDeviceDRM::EnableHotPlugDetection(int enable) {
  return true;
}

DisplayError HWDeviceDRM::SetCursorPosition(HWLayers *hw_layers, int x, int y) {
  DTRACE_SCOPED();
  return kErrorNone;
}

DisplayError HWDeviceDRM::GetPPFeaturesVersion(PPFeatureVersion *vers) {
  struct DRMPPFeatureInfo info = {};

  for (uint32_t i = 0; i < kMaxNumPPFeatures; i++) {
    memset(&info, 0, sizeof(struct DRMPPFeatureInfo));
    info.id = HWColorManagerDrm::ToDrmFeatureId(i);
    if (info.id >= sde_drm::kPPFeaturesMax)
      continue;
    // use crtc_id_ = 0 since PP features are same across all CRTCs
    drm_mgr_intf_->GetCrtcPPInfo(0, info);
    vers->version[i] = HWColorManagerDrm::GetFeatureVersion(info);
  }
  return kErrorNone;
}

DisplayError HWDeviceDRM::SetPPFeatures(PPFeaturesConfig *feature_list) {
  int ret = 0;
  PPFeatureInfo *feature = NULL;
  DRMPPFeatureInfo kernel_params = {};

  while (true) {
    ret = feature_list->RetrieveNextFeature(&feature);
    if (ret)
      break;

    if (feature) {
      DLOGV_IF(kTagDriverConfig, "feature_id = %d", feature->feature_id_);
      if (!HWColorManagerDrm::GetDrmFeature[feature->feature_id_]) {
        DLOGE("GetDrmFeature is not valid for feature %d", feature->feature_id_);
        continue;
      }
      ret = HWColorManagerDrm::GetDrmFeature[feature->feature_id_](*feature, &kernel_params);
      if (!ret)
        drm_atomic_intf_->Perform(DRMOps::CRTC_SET_POST_PROC, token_.crtc_id, &kernel_params);
      HWColorManagerDrm::FreeDrmFeatureData(&kernel_params);
    }
  }

  // Once all features were consumed, then destroy all feature instance from feature_list,
  feature_list->Reset();

  return kErrorNone;
}

DisplayError HWDeviceDRM::SetVSyncState(bool enable) {
  return kErrorNone;
}

void HWDeviceDRM::SetIdleTimeoutMs(uint32_t timeout_ms) {}

DisplayError HWDeviceDRM::SetDisplayMode(const HWDisplayMode hw_display_mode) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::SetRefreshRate(uint32_t refresh_rate) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::SetPanelBrightness(int level) {
  DisplayError err = kErrorNone;
  char buffer[kMaxSysfsCommandLength] = {0};

  DLOGV_IF(kTagDriverConfig, "Set brightness level to %d", level);
  int fd = Sys::open_(kBrightnessNode, O_RDWR);
  if (fd < 0) {
    DLOGV_IF(kTagDriverConfig, "Failed to open node = %s, error = %s ", kBrightnessNode,
             strerror(errno));
    return kErrorFileDescriptor;
  }

  int32_t bytes = snprintf(buffer, kMaxSysfsCommandLength, "%d\n", level);
  ssize_t ret = Sys::pwrite_(fd, buffer, static_cast<size_t>(bytes), 0);
  if (ret <= 0) {
    DLOGV_IF(kTagDriverConfig, "Failed to write to node = %s, error = %s ", kBrightnessNode,
             strerror(errno));
    err = kErrorHardware;
  }

  Sys::close_(fd);

  return err;
}

DisplayError HWDeviceDRM::GetPanelBrightness(int *level) {
  DisplayError err = kErrorNone;
  char brightness[kMaxStringLength] = {0};

  if (!level) {
    DLOGV_IF(kTagDriverConfig, "Invalid input, null pointer.");
    return kErrorParameters;
  }

  int fd = Sys::open_(kBrightnessNode, O_RDWR);
  if (fd < 0) {
    DLOGV_IF(kTagDriverConfig, "Failed to open brightness node = %s, error = %s", kBrightnessNode,
             strerror(errno));
    return kErrorFileDescriptor;
  }

  if (Sys::pread_(fd, brightness, sizeof(brightness), 0) > 0) {
    *level = atoi(brightness);
    DLOGV_IF(kTagDriverConfig, "Brightness level = %d", *level);
  } else {
    DLOGV_IF(kTagDriverConfig, "Failed to read panel brightness");
    err = kErrorHardware;
  }

  Sys::close_(fd);

  return err;
}

DisplayError HWDeviceDRM::GetHWScanInfo(HWScanInfo *scan_info) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::GetVideoFormat(uint32_t config_index, uint32_t *video_format) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::GetMaxCEAFormat(uint32_t *max_cea_format) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::SetS3DMode(HWS3DMode s3d_mode) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::SetScaleLutConfig(HWScaleLutInfo *lut_info) {
  sde_drm::DRMScalerLUTInfo drm_lut_info = {};
  drm_lut_info.cir_lut = lut_info->cir_lut;
  drm_lut_info.dir_lut = lut_info->dir_lut;
  drm_lut_info.sep_lut = lut_info->sep_lut;
  drm_lut_info.cir_lut_size = lut_info->cir_lut_size;
  drm_lut_info.dir_lut_size = lut_info->dir_lut_size;
  drm_lut_info.sep_lut_size = lut_info->sep_lut_size;
  drm_mgr_intf_->SetScalerLUT(drm_lut_info);

  return kErrorNone;
}

DisplayError HWDeviceDRM::SetMixerAttributes(HWMixerAttributes &mixer_attributes) {
  if (!hw_resource_.hw_dest_scalar_info.count) {
    return kErrorNotSupported;
  }

  if (mixer_attributes.width > display_attributes_.x_pixels ||
      mixer_attributes.height > display_attributes_.y_pixels) {
    DLOGW("Input resolution exceeds display resolution! input: res %dx%d display: res %dx%d",
          mixer_attributes.width, mixer_attributes.height, display_attributes_.x_pixels,
          display_attributes_.y_pixels);
    return kErrorNotSupported;
  }

  uint32_t max_input_width = hw_resource_.hw_dest_scalar_info.max_input_width;
  if (display_attributes_.is_device_split) {
    max_input_width *= 2;
  }

  if (mixer_attributes.width > max_input_width) {
    DLOGW("Input width exceeds width limit! input_width %d width_limit %d", mixer_attributes.width,
          max_input_width);
    return kErrorNotSupported;
  }

  float mixer_aspect_ratio = FLOAT(mixer_attributes.width) / FLOAT(mixer_attributes.height);
  float display_aspect_ratio =
      FLOAT(display_attributes_.x_pixels) / FLOAT(display_attributes_.y_pixels);

  if (display_aspect_ratio != mixer_aspect_ratio) {
    DLOGW("Aspect ratio mismatch! input: res %dx%d display: res %dx%d", mixer_attributes.width,
          mixer_attributes.height, display_attributes_.x_pixels, display_attributes_.y_pixels);
    return kErrorNotSupported;
  }

  float scale_x = FLOAT(display_attributes_.x_pixels) / FLOAT(mixer_attributes.width);
  float scale_y = FLOAT(display_attributes_.y_pixels) / FLOAT(mixer_attributes.height);
  float max_scale_up = hw_resource_.hw_dest_scalar_info.max_scale_up;
  if (scale_x > max_scale_up || scale_y > max_scale_up) {
    DLOGW(
        "Up scaling ratio exceeds for destination scalar upscale limit scale_x %f scale_y %f "
        "max_scale_up %f",
        scale_x, scale_y, max_scale_up);
    return kErrorNotSupported;
  }

  float mixer_split_ratio = FLOAT(mixer_attributes_.split_left) / FLOAT(mixer_attributes_.width);

  mixer_attributes_ = mixer_attributes;
  mixer_attributes_.split_left = mixer_attributes_.width;
  if (display_attributes_.is_device_split) {
    mixer_attributes_.split_left = UINT32(FLOAT(mixer_attributes.width) * mixer_split_ratio);
  }

  return kErrorNone;
}

DisplayError HWDeviceDRM::GetMixerAttributes(HWMixerAttributes *mixer_attributes) {
  if (!mixer_attributes) {
    return kErrorParameters;
  }

  mixer_attributes_.width = display_attributes_.x_pixels;
  mixer_attributes_.height = display_attributes_.y_pixels;
  mixer_attributes_.split_left = display_attributes_.is_device_split
                                     ? hw_panel_info_.split_info.left_split
                                     : mixer_attributes_.width;
  *mixer_attributes = mixer_attributes_;

  return kErrorNone;
}

void HWDeviceDRM::UpdateMixerAttributes() {
  mixer_attributes_.width = display_attributes_.x_pixels;
  mixer_attributes_.height = display_attributes_.y_pixels;
  mixer_attributes_.split_left = display_attributes_.is_device_split
                                     ? hw_panel_info_.split_info.left_split
                                     : mixer_attributes_.width;
  DLOGI("Mixer WxH %dx%d for %s", mixer_attributes_.width, mixer_attributes_.height, device_name_);
}

DisplayError HWDeviceDRM::SetDynamicDSIClock(uint64_t bitclk) {
  return kErrorNotSupported;
}

DisplayError HWDeviceDRM::GetDynamicDSIClock(uint64_t *bitclk) {
  return kErrorNotSupported;
}


DisplayError HWDeviceDRM::SetActiveConfig(uint32_t active_config) {
  return kErrorNone;
}

DisplayError HWDeviceDRM::GetConfigIndex(uint32_t width, uint32_t height, uint32_t *index) {
  return kErrorNone;
}


DisplayError HWDeviceDRM::SetConfigAttributes(uint32_t index, uint32_t width, uint32_t height) {
  return kErrorNone;
}

DisplayError HWDeviceDRM::GetHdmiMode(std::vector<uint32_t> &hdmi_modes) {
  return kErrorNone;
}

DisplayError HWDeviceDRM::SetDisplayFormat(uint32_t index, DisplayInterfaceFormat fmt) {
  return kErrorNotSupported;
}

}  // namespace sdm
