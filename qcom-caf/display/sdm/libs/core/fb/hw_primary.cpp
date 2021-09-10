/*
* Copyright (c) 2015 - 2018, The Linux Foundation. All rights reserved.
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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <utils/debug.h>
#include <utils/sys.h>
#include <core/display_interface.h>
#include <linux/msm_mdp_ext.h>
#include <utils/rect.h>

#include <string>

#include "hw_primary.h"
#include "hw_color_manager.h"

#define __CLASS__ "HWPrimary"

#ifndef MDP_COMMIT_CWB_EN
#define MDP_COMMIT_CWB_EN 0x800
#endif

#ifndef MDP_COMMIT_CWB_DSPP
#define MDP_COMMIT_CWB_DSPP 0x1000
#endif

#ifndef MDP_COMMIT_AVR_EN
#define MDP_COMMIT_AVR_EN 0x08
#endif

#ifndef MDP_COMMIT_AVR_ONE_SHOT_MODE
#define MDP_COMMIT_AVR_ONE_SHOT_MODE 0x10
#endif

#ifndef MDP_COMMIT_PARTIAL_UPDATE_DUAL_ROI
#define MDP_COMMIT_PARTIAL_UPDATE_DUAL_ROI  0x20
#endif

namespace sdm {

using std::string;
using std::to_string;
using std::fstream;

HWPrimary::HWPrimary(BufferSyncHandler *buffer_sync_handler, HWInfoInterface *hw_info_intf)
  : HWDevice(buffer_sync_handler) {
  HWDevice::device_type_ = kDevicePrimary;
  HWDevice::device_name_ = "Primary Display Device";
  HWDevice::hw_info_intf_ = hw_info_intf;
}

DisplayError HWPrimary::Init() {
  DisplayError error = kErrorNone;

  error = HWDevice::Init();
  if (error != kErrorNone) {
    return error;
  }

  mdp_dest_scalar_data_.resize(hw_resource_.hw_dest_scalar_info.count);

  error = PopulateDisplayAttributes();
  if (error != kErrorNone) {
    return error;
  }

  UpdateMixerAttributes();

  // Need to enable HPD, but toggle at start when HDMI is external
  // This helps for framework reboot or adb shell stop/start
  EnableHotPlugDetection(0);
  EnableHotPlugDetection(1);
  InitializeConfigs();

  avr_prop_disabled_ = Debug::IsAVRDisabled();

  return error;
}

bool HWPrimary::GetCurrentModeFromSysfs(size_t *curr_x_pixels, size_t *curr_y_pixels) {
  bool ret = false;
  string mode_path = fb_path_ + string("0/mode");

  Sys::fstream fs(mode_path, fstream::in);
  if (!fs.is_open()) {
    return false;
  }

  string line;
  if (Sys::getline_(fs, line)) {
    // String is of form "U:1600x2560p-0". Documentation/fb/modedb.txt in
    // kernel has more info on the format.
    size_t xpos = line.find(':');
    size_t ypos = line.find('x');

    if (xpos == string::npos || ypos == string::npos) {
      DLOGI("Resolution switch not supported");
    } else {
      *curr_x_pixels = static_cast<size_t>(atoi(line.c_str() + xpos + 1));
      *curr_y_pixels = static_cast<size_t>(atoi(line.c_str() + ypos + 1));
      DLOGI("Current Config: %u x %u", *curr_x_pixels, *curr_y_pixels);
      ret = true;
    }
  }

  return ret;
}

void HWPrimary::InitializeConfigs() {
  size_t curr_x_pixels = 0;
  size_t curr_y_pixels = 0;

  if (!GetCurrentModeFromSysfs(&curr_x_pixels, &curr_y_pixels)) {
    return;
  }

  string modes_path = string(fb_path_) + string("0/modes");

  Sys::fstream fs(modes_path, fstream::in);
  if (!fs.is_open()) {
    DLOGI("Unable to process modes");
    return;
  }

  string line;
  while (Sys::getline_(fs, line)) {
    DisplayConfigVariableInfo config;
    // std::getline (unlike ::getline) removes \n while driver expects it in mode, so add back
    line += '\n';
    size_t xpos = line.find(':');
    size_t ypos = line.find('x');

    if (xpos == string::npos || ypos == string::npos) {
      continue;
    }

    config.x_pixels = UINT32(atoi(line.c_str() + xpos + 1));
    config.y_pixels = UINT32(atoi(line.c_str() + ypos + 1));
    DLOGI("Found mode %d x %d", config.x_pixels, config.y_pixels);
    display_configs_.push_back(config);
    display_config_strings_.push_back(string(line.c_str()));

    if (curr_x_pixels == config.x_pixels && curr_y_pixels == config.y_pixels) {
      active_config_index_ = UINT32(display_configs_.size() - 1);
      DLOGI("Active config index %u", active_config_index_);
    }
  }
}

DisplayError HWPrimary::GetNumDisplayAttributes(uint32_t *count) {
  *count = IsResolutionSwitchEnabled() ? UINT32(display_configs_.size()) : 1;
  return kErrorNone;
}

DisplayError HWPrimary::GetActiveConfig(uint32_t *active_config_index) {
  *active_config_index = active_config_index_;
  return kErrorNone;
}

DisplayError HWPrimary::GetDisplayAttributes(uint32_t index,
                                             HWDisplayAttributes *display_attributes) {
  if (!display_attributes) {
    return kErrorParameters;
  }

  if (IsResolutionSwitchEnabled() && index >= display_configs_.size()) {
    return kErrorParameters;
  }

  *display_attributes = display_attributes_;
  if (IsResolutionSwitchEnabled()) {
    // Overwrite only the parent portion of object
    display_attributes->x_pixels = display_configs_.at(index).x_pixels;
    display_attributes->y_pixels = display_configs_.at(index).y_pixels;
  }

  return kErrorNone;
}

DisplayError HWPrimary::PopulateDisplayAttributes() {
  DTRACE_SCOPED();

  // Variable screen info
  fb_var_screeninfo var_screeninfo = {};

  if (Sys::ioctl_(device_fd_, FBIOGET_VSCREENINFO, &var_screeninfo) < 0) {
    IOCTL_LOGE(FBIOGET_VSCREENINFO, device_type_);
    return kErrorHardware;
  }

  // Frame rate
  msmfb_metadata meta_data = {};
  meta_data.op = metadata_op_frame_rate;
  if (Sys::ioctl_(device_fd_, MSMFB_METADATA_GET, &meta_data) < 0) {
    IOCTL_LOGE(MSMFB_METADATA_GET, device_type_);
    return kErrorHardware;
  }

  // If driver doesn't return width/height information, default to 320 dpi
  if (INT(var_screeninfo.width) <= 0 || INT(var_screeninfo.height) <= 0) {
    var_screeninfo.width  = UINT32(((FLOAT(var_screeninfo.xres) * 25.4f)/320.0f) + 0.5f);
    var_screeninfo.height = UINT32(((FLOAT(var_screeninfo.yres) * 25.4f)/320.0f) + 0.5f);
    DLOGW("Driver doesn't report panel physical width and height - defaulting to 320dpi");
  }

  display_attributes_.x_pixels = var_screeninfo.xres;
  display_attributes_.y_pixels = var_screeninfo.yres;
  display_attributes_.v_front_porch = var_screeninfo.lower_margin;
  display_attributes_.v_back_porch = var_screeninfo.upper_margin;
  display_attributes_.v_pulse_width = var_screeninfo.vsync_len;
  uint32_t h_blanking = var_screeninfo.right_margin + var_screeninfo.left_margin +
      var_screeninfo.hsync_len;
  display_attributes_.h_total = var_screeninfo.xres + h_blanking;
  display_attributes_.x_dpi =
      (FLOAT(var_screeninfo.xres) * 25.4f) / FLOAT(var_screeninfo.width);
  display_attributes_.y_dpi =
      (FLOAT(var_screeninfo.yres) * 25.4f) / FLOAT(var_screeninfo.height);
  display_attributes_.fps = meta_data.data.panel_frame_rate;
  display_attributes_.vsync_period_ns = UINT32(1000000000L / display_attributes_.fps);
  display_attributes_.is_device_split = (hw_panel_info_.split_info.right_split ||
      (var_screeninfo.xres > hw_resource_.max_mixer_width));
  display_attributes_.h_total += (display_attributes_.is_device_split ||
    hw_panel_info_.ping_pong_split)? h_blanking : 0;

  return kErrorNone;
}

DisplayError HWPrimary::SetDisplayAttributes(uint32_t index) {
  DisplayError ret = kErrorNone;

  if (!IsResolutionSwitchEnabled()) {
    return kErrorNotSupported;
  }

  if (index >= display_configs_.size()) {
    return kErrorParameters;
  }

  string mode_path = string(fb_path_) + string("0/mode");
  int fd = Sys::open_(mode_path.c_str(), O_WRONLY);

  if (fd < 0) {
    DLOGE("Opening mode failed");
    return kErrorNotSupported;
  }

  ssize_t written = Sys::pwrite_(fd, display_config_strings_.at(index).c_str(),
                                 display_config_strings_.at(index).length(), 0);
  if (written > 0) {
    DLOGI("Successfully set config %u", index);
    PopulateHWPanelInfo();
    PopulateDisplayAttributes();
    UpdateMixerAttributes();
    active_config_index_ = index;
  } else {
    DLOGE("Writing config index %u failed with error: %s", index, strerror(errno));
    ret = kErrorParameters;
  }

  Sys::close_(fd);

  return ret;
}

DisplayError HWPrimary::SetRefreshRate(uint32_t refresh_rate) {
  char node_path[kMaxStringLength] = {0};

  if (hw_resource_.has_avr && !avr_prop_disabled_) {
    return kErrorNotSupported;
  }

  snprintf(node_path, sizeof(node_path), "%s%d/dynamic_fps", fb_path_, fb_node_index_);

  int fd = Sys::open_(node_path, O_WRONLY);
  if (fd < 0) {
    DLOGE("Failed to open %s with error %s", node_path, strerror(errno));
    return kErrorFileDescriptor;
  }

  char refresh_rate_string[kMaxStringLength];
  snprintf(refresh_rate_string, sizeof(refresh_rate_string), "%d", refresh_rate);
  DLOGI_IF(kTagDriverConfig, "Setting refresh rate = %d", refresh_rate);
  ssize_t len = Sys::pwrite_(fd, refresh_rate_string, strlen(refresh_rate_string), 0);
  if (len < 0) {
    DLOGE("Failed to write %d with error %s", refresh_rate, strerror(errno));
    Sys::close_(fd);
    return kErrorUndefined;
  }
  Sys::close_(fd);

  DisplayError error = PopulateDisplayAttributes();
  if (error != kErrorNone) {
    return error;
  }

  return kErrorNone;
}

DisplayError HWPrimary::GetConfigIndex(uint32_t mode, uint32_t *index) {
  return HWDevice::GetConfigIndex(mode, index);
}

DisplayError HWPrimary::PowerOff() {
  if (Sys::ioctl_(device_fd_, FBIOBLANK, FB_BLANK_POWERDOWN) < 0) {
    IOCTL_LOGE(FB_BLANK_POWERDOWN, device_type_);
    return kErrorHardware;
  }

  auto_refresh_ = false;

  return kErrorNone;
}

DisplayError HWPrimary::Doze() {
  if (Sys::ioctl_(device_fd_, FBIOBLANK, FB_BLANK_NORMAL) < 0) {
    IOCTL_LOGE(FB_BLANK_NORMAL, device_type_);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWPrimary::DozeSuspend() {
  if (Sys::ioctl_(device_fd_, FBIOBLANK, FB_BLANK_VSYNC_SUSPEND) < 0) {
    IOCTL_LOGE(FB_BLANK_VSYNC_SUSPEND, device_type_);
    return kErrorHardware;
  }

  return kErrorNone;
}

DisplayError HWPrimary::Validate(HWLayers *hw_layers) {
  HWLayersInfo &hw_layer_info = hw_layers->info;
  LayerStack *stack = hw_layer_info.stack;

  HWDevice::ResetDisplayParams();

  mdp_layer_commit_v1 &mdp_commit = mdp_disp_commit_.commit_v1;

  LayerRect left_roi = hw_layer_info.left_frame_roi.at(0);
  LayerRect right_roi = hw_layer_info.right_frame_roi.at(0);

  mdp_commit.left_roi.x = UINT32(left_roi.left);
  mdp_commit.left_roi.y = UINT32(left_roi.top);
  mdp_commit.left_roi.w = UINT32(left_roi.right - left_roi.left);
  mdp_commit.left_roi.h = UINT32(left_roi.bottom - left_roi.top);

  // Update second roi information in right_roi
  if (hw_layer_info.left_frame_roi.size() == 2) {
    mdp_commit.flags |= MDP_COMMIT_PARTIAL_UPDATE_DUAL_ROI;
    right_roi = hw_layer_info.left_frame_roi.at(1);
  }

  // SDM treats ROI as one full coordinate system.
  // In case source split is disabled, However, Driver assumes Mixer to operate in
  // different co-ordinate system.
  if (IsValid(right_roi)) {
    mdp_commit.right_roi.x = UINT32(right_roi.left);
    if (!hw_resource_.is_src_split) {
      mdp_commit.right_roi.x = UINT32(right_roi.left) - mixer_attributes_.split_left;
    }
    mdp_commit.right_roi.y = UINT32(right_roi.top);
    mdp_commit.right_roi.w = UINT32(right_roi.right - right_roi.left);
    mdp_commit.right_roi.h = UINT32(right_roi.bottom - right_roi.top);
  }

  if (stack->output_buffer && hw_resource_.has_concurrent_writeback) {
    LayerBuffer *output_buffer = stack->output_buffer;
    mdp_out_layer_.writeback_ndx = hw_resource_.writeback_index;
    mdp_out_layer_.buffer.width = output_buffer->unaligned_width;
    mdp_out_layer_.buffer.height = output_buffer->unaligned_height;
    mdp_out_layer_.buffer.comp_ratio.denom = 1000;
    mdp_out_layer_.buffer.comp_ratio.numer = UINT32(hw_layers->output_compression * 1000);
    mdp_out_layer_.buffer.fence = -1;
#ifdef OUT_LAYER_COLOR_SPACE
    SetCSC(output_buffer->color_metadata, &mdp_out_layer_.color_space);
#endif
    SetFormat(output_buffer->format, &mdp_out_layer_.buffer.format);
    mdp_commit.flags |= MDP_COMMIT_CWB_EN;
    mdp_commit.flags |= (stack->flags.post_processed_output) ? MDP_COMMIT_CWB_DSPP : 0;
    DLOGI_IF(kTagDriverConfig, "****************** Conc WB Output buffer Info ******************");
    DLOGI_IF(kTagDriverConfig, "out_w %d, out_h %d, out_f %d, wb_id %d DSPP output %d",
             mdp_out_layer_.buffer.width, mdp_out_layer_.buffer.height,
             mdp_out_layer_.buffer.format, mdp_out_layer_.writeback_ndx,
             stack->flags.post_processed_output);
    DLOGI_IF(kTagDriverConfig, "****************************************************************");
  }

  if (hw_resource_.has_avr) {
    SetAVRFlags(hw_layers->hw_avr_info, &mdp_commit.flags);
  }

  return HWDevice::Validate(hw_layers);
}

DisplayError HWPrimary::Commit(HWLayers *hw_layers) {
  LayerBuffer *output_buffer = hw_layers->info.stack->output_buffer;

  if (hw_resource_.has_concurrent_writeback && output_buffer) {
    if (output_buffer->planes[0].fd >= 0) {
      mdp_out_layer_.buffer.planes[0].fd = output_buffer->planes[0].fd;
      mdp_out_layer_.buffer.planes[0].offset = output_buffer->planes[0].offset;
      SetStride(device_type_, output_buffer->format, output_buffer->planes[0].stride,
                &mdp_out_layer_.buffer.planes[0].stride);
      mdp_out_layer_.buffer.plane_count = 1;
      mdp_out_layer_.buffer.fence = -1;

      DLOGI_IF(kTagDriverConfig, "****************** Conc WB Output buffer Info ****************");
      DLOGI_IF(kTagDriverConfig, "out_fd %d, out_offset %d, out_stride %d",
               mdp_out_layer_.buffer.planes[0].fd, mdp_out_layer_.buffer.planes[0].offset,
               mdp_out_layer_.buffer.planes[0].stride);
      DLOGI_IF(kTagDriverConfig, "**************************************************************");
    } else {
      DLOGE("Invalid output buffer fd");
      return kErrorParameters;
    }
  }

  DisplayError ret = HWDevice::Commit(hw_layers);

  if (ret == kErrorNone && hw_resource_.has_concurrent_writeback && output_buffer) {
    output_buffer->release_fence_fd = mdp_out_layer_.buffer.fence;
  }

  return ret;
}

void HWPrimary::SetIdleTimeoutMs(uint32_t timeout_ms) {
  char node_path[kMaxStringLength] = {0};

  DLOGI_IF(kTagDriverConfig, "Setting idle timeout to = %d ms", timeout_ms);

  snprintf(node_path, sizeof(node_path), "%s%d/idle_time", fb_path_, fb_node_index_);

  // Open a sysfs node to send the timeout value to driver.
  int fd = Sys::open_(node_path, O_WRONLY);
  if (fd < 0) {
    DLOGE("Unable to open %s, node %s", node_path, strerror(errno));
    return;
  }

  char timeout_string[64];
  snprintf(timeout_string, sizeof(timeout_string), "%d", timeout_ms);

  // Notify driver about the timeout value
  ssize_t length = Sys::pwrite_(fd, timeout_string, strlen(timeout_string), 0);
  if (length <= 0) {
    DLOGE("Unable to write into %s, node %s", node_path, strerror(errno));
  }

  Sys::close_(fd);
}

DisplayError HWPrimary::SetVSyncState(bool enable) {
  DTRACE_SCOPED();
  return HWDevice::SetVSyncState(enable);
}

DisplayError HWPrimary::SetDisplayMode(const HWDisplayMode hw_display_mode) {
  uint32_t mode = kModeDefault;

  switch (hw_display_mode) {
  case kModeVideo:
    mode = kModeLPMVideo;
    break;
  case kModeCommand:
    mode = kModeLPMCommand;
    break;
  default:
    DLOGW("Failed to translate SDE display mode %d to a MSMFB_LPM_ENABLE mode",
          hw_display_mode);
    return kErrorParameters;
  }

  if (Sys::ioctl_(device_fd_, INT(MSMFB_LPM_ENABLE), &mode) < 0) {
    IOCTL_LOGE(MSMFB_LPM_ENABLE, device_type_);
    return kErrorHardware;
  }

  DLOGI("Triggering display mode change to %d on next commit.", hw_display_mode);
  synchronous_commit_ = true;

  return kErrorNone;
}

DisplayError HWPrimary::SetPanelBrightness(int level) {
  char buffer[kMaxSysfsCommandLength] = {0};

  DLOGV_IF(kTagDriverConfig, "Set brightness level to %d", level);
  int fd = Sys::open_(kBrightnessNode, O_RDWR);
  if (fd < 0) {
    DLOGV_IF(kTagDriverConfig, "Failed to open node = %s, error = %s ", kBrightnessNode,
             strerror(errno));
    return kErrorFileDescriptor;
  }

  int32_t bytes = snprintf(buffer, kMaxSysfsCommandLength, "%d\n", level);
  if (bytes < 0) {
    DLOGV_IF(kTagDriverConfig, "Failed to copy new brightness level = %d", level);
    Sys::close_(fd);
    return kErrorUndefined;
  }

  ssize_t ret = Sys::pwrite_(fd, buffer, static_cast<size_t>(bytes), 0);
  if (ret <= 0) {
    DLOGV_IF(kTagDriverConfig, "Failed to write to node = %s, error = %s ", kBrightnessNode,
             strerror(errno));
    Sys::close_(fd);
    return kErrorUndefined;
  }
  Sys::close_(fd);

  return kErrorNone;
}

DisplayError HWPrimary::GetPanelBrightness(int *level) {
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
  }
  Sys::close_(fd);

  return kErrorNone;
}

DisplayError HWPrimary::SetAutoRefresh(bool enable) {
  const int kWriteLength = 2;
  char buffer[kWriteLength] = {'\0'};
  ssize_t bytes = snprintf(buffer, kWriteLength, "%d", enable);

  if (enable == auto_refresh_) {
    return kErrorNone;
  }

  if (HWDevice::SysFsWrite(kAutoRefreshNode, buffer, bytes) <= 0) {  // Returns bytes written
    return kErrorUndefined;
  }

  auto_refresh_ = enable;

  return kErrorNone;
}

DisplayError HWPrimary::GetPPFeaturesVersion(PPFeatureVersion *vers) {
  mdp_pp_feature_version version = {};

#ifdef PA_DITHER
  uint32_t feature_id_mapping[kMaxNumPPFeatures] = { PCC, IGC, GC, GC, PA,
                                                     DITHER, GAMUT, PA_DITHER };
#else
  uint32_t feature_id_mapping[kMaxNumPPFeatures] = { PCC, IGC, GC, GC, PA, DITHER, GAMUT };
#endif

  if (hw_resource_.hw_version != kHWMdssVersion3) {
    // Do not query kGlobalColorFeatureCsc for kHWMdssVersion5
    for (int i(0); i < (kMaxNumPPFeatures - 1); i++) {
      version.pp_feature = feature_id_mapping[i];

      if (Sys::ioctl_(device_fd_,  INT(MSMFB_MDP_PP_GET_FEATURE_VERSION), &version) < 0) {
        IOCTL_LOGE(MSMFB_MDP_PP_GET_FEATURE_VERSION, device_type_);
        return kErrorHardware;
      }
      vers->version[i] = version.version_info;
    }
  } else {
    for (int i(0); i < kMaxNumPPFeatures; i++) {
      version.pp_feature = feature_id_mapping[i];
      vers->version[i] = mdp_pp_legacy;
    }
  }

  return kErrorNone;
}

// It was entered with PPFeaturesConfig::locker_ being hold.
DisplayError HWPrimary::SetPPFeatures(PPFeaturesConfig *feature_list) {
  msmfb_mdp_pp kernel_params = {};
  int ret = 0;
  PPFeatureInfo *feature = NULL;

  while (true) {
    ret = feature_list->RetrieveNextFeature(&feature);
    if (ret)
        break;

    if (feature) {
      DLOGV_IF(kTagDriverConfig, "feature_id = %d", feature->feature_id_);

      if ((feature->feature_id_ < kMaxNumPPFeatures)) {
        HWColorManager::SetFeature[feature->feature_id_](*feature, &kernel_params);
        if (Sys::ioctl_(device_fd_, INT(MSMFB_MDP_PP), &kernel_params) < 0) {
          IOCTL_LOGE(MSMFB_MDP_PP, device_type_);

          feature_list->Reset();
          return kErrorHardware;
        }
      }
    }
  }  // while(true)

  // Once all features were consumed, then destroy all feature instance from feature_list,
  // Then mark it as non-dirty of PPFeaturesConfig cache.
  feature_list->Reset();

  return kErrorNone;
}

DisplayError HWPrimary::SetMixerAttributes(HWMixerAttributes &mixer_attributes) {
  if (IsResolutionSwitchEnabled()) {
    return kErrorNotSupported;
  }

  return HWDevice::SetMixerAttributes(mixer_attributes);
}

void HWPrimary::UpdateMixerAttributes() {
  mixer_attributes_.width = display_attributes_.x_pixels;
  mixer_attributes_.height = display_attributes_.y_pixels;
  mixer_attributes_.split_left = display_attributes_.is_device_split ?
      hw_panel_info_.split_info.left_split : mixer_attributes_.width;
}

void HWPrimary::SetAVRFlags(const HWAVRInfo &hw_avr_info, uint32_t *avr_flags) {
  if (hw_avr_info.enable) {
    *avr_flags |= MDP_COMMIT_AVR_EN;
  }

  if (hw_avr_info.mode == kOneShotMode) {
    *avr_flags |= MDP_COMMIT_AVR_ONE_SHOT_MODE;
  }
}

DisplayError HWPrimary::SetDynamicDSIClock(uint64_t bitclk) {
  if (!hw_panel_info_.bitclk_update) {
    return kErrorNotSupported;
  }

  char node_path[kMaxStringLength] = {0};
  snprintf(node_path, sizeof(node_path), "%s%d/dynamic_bitclk", fb_path_, fb_node_index_);

  int fd = Sys::open_(node_path, O_WRONLY);
  if (fd < 0) {
    DLOGE("Failed to open %s with error %s", node_path, strerror(errno));
    return kErrorFileDescriptor;
  }

  char bitclk_string[kMaxStringLength];
  snprintf(bitclk_string, sizeof(bitclk_string), "%" PRIu64, bitclk);
  DLOGI_IF(kTagDriverConfig, "Setting bit clk to" "%" PRIu64, bitclk);
  ssize_t len = Sys::pwrite_(fd, bitclk_string, strlen(bitclk_string), 0);
  if (len < 0) {
    DLOGE("Failed to write %d with error %s", bitclk, strerror(errno));
    Sys::close_(fd);
    return kErrorUndefined;
  }
  Sys::close_(fd);

  return kErrorNone;
}

DisplayError HWPrimary::GetDynamicDSIClock(uint64_t *bitclk) {
  if (!hw_panel_info_.bitclk_update) {
    return kErrorNotSupported;
  }

  char node_path[kMaxStringLength] = {0};
  snprintf(node_path, sizeof(node_path), "%s%d/dynamic_bitclk", fb_path_, fb_node_index_);

  int fd = Sys::open_(node_path, O_RDONLY);
  if (fd < 0) {
    DLOGE("Failed to open %s with error %s", node_path, strerror(errno));
    return kErrorFileDescriptor;
  }

  char dsi_clk[64] = {0};
  ssize_t len = Sys::pread_(fd, dsi_clk, sizeof(dsi_clk), 0);
  if (len < 0) {
    DLOGE("Failed to read bit clk with error %s", strerror(errno));
    Sys::close_(fd);
    return kErrorUndefined;
  }

  *bitclk = UINT64(atoi(dsi_clk));
  Sys::close_(fd);
  return kErrorNone;
}

DisplayError HWPrimary::SetActiveConfig(uint32_t active_config) {
  active_config_index_ = active_config;
  return kErrorNone;
}

DisplayError HWPrimary::GetHdmiMode(std::vector<uint32_t> &hdmi_modes) {
  return kErrorNone;
}

}  // namespace sdm

