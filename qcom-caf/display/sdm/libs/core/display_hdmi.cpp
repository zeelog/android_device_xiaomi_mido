/*
* Copyright (c) 2014 - 2018, 2020 The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <utils/constants.h>
#include <utils/debug.h>
#include <map>
#include <utility>
#include <vector>
#include <cmath>

#include "display_hdmi.h"
#include "hw_interface.h"
#include "hw_info_interface.h"

#define __CLASS__ "DisplayHDMI"

#define STANDARD_VIC 127  // 1-127 are standard vic-ids

namespace sdm {

DisplayHDMI::DisplayHDMI(DisplayEventHandler *event_handler, HWInfoInterface *hw_info_intf,
                         BufferSyncHandler *buffer_sync_handler, BufferAllocator *buffer_allocator,
                         CompManager *comp_manager)
  : DisplayBase(kHDMI, event_handler, kDeviceHDMI, buffer_sync_handler, buffer_allocator,
                comp_manager, hw_info_intf) {
}

DisplayError DisplayHDMI::Init() {
  lock_guard<recursive_mutex> obj(recursive_mutex_);

  DisplayError error = HWInterface::Create(kHDMI, hw_info_intf_, buffer_sync_handler_,
                                           buffer_allocator_, &hw_intf_);
  if (error != kErrorNone) {
    return error;
  }

  uint32_t active_mode_index = 0;
  std::ifstream res_file;

  res_file.open("/vendor/resolutions.txt");
  if (res_file) {
    DLOGI("Getting best resolution from file");
    active_mode_index = GetBestConfigFromFile(res_file);
    res_file.close();
  } else {
    char value[64] = "0";
    DLOGI("Computing best resolution");
    Debug::GetProperty(HDMI_S3D_MODE_PROP, value);
    HWS3DMode mode = (HWS3DMode)atoi(value);
    if (mode > kS3DModeNone && mode < kS3DModeMax) {
      active_mode_index = GetBestConfig(mode);
    } else {
      active_mode_index = GetBestConfig(kS3DModeNone);
    }
  }

  error = hw_intf_->SetDisplayAttributes(active_mode_index);
  if (error != kErrorNone) {
    HWInterface::Destroy(hw_intf_);
  }
  panel_config_index_ = active_mode_index;
  error = DisplayBase::Init();
  if (error != kErrorNone) {
    DisplayBase::Deinit();
    HWInterface::Destroy(hw_intf_);
    return error;
  }

  HWDisplayAttributes display_attributes = {};
  hw_intf_->GetDisplayAttributes(active_mode_index, &display_attributes);

  uint32_t display_width = display_attributes.x_pixels;
  uint32_t display_height = display_attributes.y_pixels;
  uint32_t index = 0;
  bool dest_scale = false;
  HWDisplayInterfaceInfo hw_disp_info = {};
  hw_info_intf_->GetFirstDisplayInterfaceType(&hw_disp_info);
  if (hw_disp_info.type == kHDMI) {
    dest_scale = (mixer_attributes_.width != display_width ||
                  mixer_attributes_.height != display_height);
  }

  if (dest_scale) {
    // When DS is enabled SDM clients should see active config equal to mixer resolution.
    // active config = mixer config  // if DS is enabled
    error = hw_intf_->GetConfigIndex(mixer_attributes_.width, mixer_attributes_.height, &index);
    if (error !=kErrorNone) {
      // If mixer resolution does not match with any display resolution supported by TV,
      // then use second best config as mixer resolution.
      uint32_t closest_config_index = GetClosestConfig(mixer_attributes_.width,
                                                       mixer_attributes_.height);
      if (active_mode_index == closest_config_index) {
        dest_scale_enabled_ = false;
      } else {
        dest_scale_enabled_ = true;
      }
      hw_intf_->SetActiveConfig(closest_config_index);
      mixer_config_index_ = closest_config_index;
      HWDisplayAttributes display_attributes = {};
      hw_intf_->GetDisplayAttributes(closest_config_index, &display_attributes);
      mixer_attributes_.width = display_attributes.x_pixels;
      mixer_attributes_.height = display_attributes.y_pixels;
      hw_intf_->SetMixerAttributes(mixer_attributes_);
      hw_intf_->SetConfigAttributes(mixer_config_index_, mixer_attributes_.width,
                                    mixer_attributes_.height);
    } else {
      hw_intf_->SetActiveConfig(index);
      mixer_config_index_ = index;
      dest_scale_enabled_ = true;
    }
  }
  if (dest_scale_enabled_) {
    DLOGI("DS enabled. User config = %d",mixer_config_index_);
  }
  DLOGI("Mixer wxh = %dx%d Display wxh= %dx%d",mixer_attributes_.width, mixer_attributes_.height,
         display_width,display_height);

  GetScanSupport();
  underscan_supported_ = (scan_support_ == kScanAlwaysUnderscanned) || (scan_support_ == kScanBoth);

  s3d_format_to_mode_.insert(std::pair<LayerBufferS3DFormat, HWS3DMode>
                            (kS3dFormatNone, kS3DModeNone));
  s3d_format_to_mode_.insert(std::pair<LayerBufferS3DFormat, HWS3DMode>
                            (kS3dFormatLeftRight, kS3DModeLR));
  s3d_format_to_mode_.insert(std::pair<LayerBufferS3DFormat, HWS3DMode>
                            (kS3dFormatRightLeft, kS3DModeRL));
  s3d_format_to_mode_.insert(std::pair<LayerBufferS3DFormat, HWS3DMode>
                            (kS3dFormatTopBottom, kS3DModeTB));
  s3d_format_to_mode_.insert(std::pair<LayerBufferS3DFormat, HWS3DMode>
                            (kS3dFormatFramePacking, kS3DModeFP));
  if (hw_disp_info.type == kHDMI) {
    error = HWEventsInterface::Create(kPrimary, this, event_list_, &hw_events_intf_);
  } else {
    error = HWEventsInterface::Create(INT(display_type_), this, event_list_, &hw_events_intf_);
  }

  if (error != kErrorNone) {
    DisplayBase::Deinit();
    HWInterface::Destroy(hw_intf_);
    DLOGE("Failed to create hardware events interface. Error = %d", error);
  }

  return error;
}

DisplayError DisplayHDMI::Prepare(LayerStack *layer_stack) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  DisplayError error = kErrorNone;
  uint32_t new_mixer_width = 0;
  uint32_t new_mixer_height = 0;
  uint32_t display_width = display_attributes_.x_pixels;
  uint32_t display_height = display_attributes_.y_pixels;

  if (dest_scale_enabled_ && NeedsMixerReconfiguration(layer_stack, &new_mixer_width,
      &new_mixer_height)) {
    CheckMinMixerResolution(&new_mixer_width, &new_mixer_height);
    error = ReconfigureMixer(new_mixer_width, new_mixer_height);
    if (error != kErrorNone) {
      ReconfigureMixer(display_width, display_height);
    }
  }

  SetS3DMode(layer_stack);

  // Clean hw layers for reuse.
  hw_layers_ = HWLayers();

  return DisplayBase::Prepare(layer_stack);
}

DisplayError DisplayHDMI::GetRefreshRateRange(uint32_t *min_refresh_rate,
                                              uint32_t *max_refresh_rate) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  DisplayError error = kErrorNone;

  if (hw_panel_info_.min_fps && hw_panel_info_.max_fps) {
    *min_refresh_rate = hw_panel_info_.min_fps;
    *max_refresh_rate = hw_panel_info_.max_fps;
  } else {
    error = DisplayBase::GetRefreshRateRange(min_refresh_rate, max_refresh_rate);
  }

  return error;
}

DisplayError DisplayHDMI::SetRefreshRate(uint32_t refresh_rate) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);

  if (!active_) {
    return kErrorPermission;
  }

  DisplayError error = hw_intf_->SetRefreshRate(refresh_rate);
  if (error != kErrorNone) {
    return error;
  }

  return DisplayBase::ReconfigureDisplay();
}

bool DisplayHDMI::IsUnderscanSupported() {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  return underscan_supported_;
}

DisplayError DisplayHDMI::OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  return hw_intf_->OnMinHdcpEncryptionLevelChange(min_enc_level);
}

uint32_t DisplayHDMI::GetClosestConfig(uint32_t width, uint32_t height) {
  if ((UINT32_MAX / width < height) || (UINT32_MAX / height < width)) {
    //uint overflow
    return panel_config_index_;
  }
  uint32_t num_modes = 0, index = 0;
  hw_intf_->GetNumDisplayAttributes(&num_modes);
  uint32_t area = width * height;
  std::vector<uint32_t> area_modes(num_modes);
  // Get display attribute for each mode
  std::vector<HWDisplayAttributes> attrib(num_modes);
  for (index = 0; index < num_modes; index++) {
    hw_intf_->GetDisplayAttributes(index, &attrib[index]);
    area_modes[index] = attrib[index].y_pixels * attrib[index].x_pixels;
  }
  uint32_t least_area_diff = display_attributes_.x_pixels*display_attributes_.y_pixels;
  uint32_t least_diff_index = panel_config_index_;
  for (index = 0; index < num_modes; index++) {
    if (abs(INT(area_modes[index]) - INT(area)) < INT(least_area_diff)) {
      least_diff_index = index;
      least_area_diff = UINT32(abs(INT(area_modes[index]) - INT(area)));
    }
  }
  DLOGV("Closest config index = %d",least_diff_index);
  return least_diff_index;
}

uint32_t DisplayHDMI::GetBestConfigFromFile(std::ifstream &res_file) {
  DisplayError error = kErrorNone;
  string line;
  uint32_t num_modes = 0;
  uint32_t index = 0;
  std::map<std::string, DisplayInterfaceFormat> intf_format_to_str;
  intf_format_to_str[std::string("rgb")] = DisplayInterfaceFormat::kFormatRGB;
  intf_format_to_str[std::string("yuv422")] = DisplayInterfaceFormat::kFormatYUV;
  intf_format_to_str[std::string("yuv422d")] = DisplayInterfaceFormat::kFormatYUV;
  intf_format_to_str[std::string("yuv420")] = DisplayInterfaceFormat::kFormatYUV;
  intf_format_to_str[std::string("yuv420d")] = DisplayInterfaceFormat::kFormatYUV;
  intf_format_to_str[std::string("yuv444")] = DisplayInterfaceFormat::kFormatYUV;
  hw_intf_->GetNumDisplayAttributes(&num_modes);
  DLOGI("Num modes = %d", num_modes);
  // Get display attribute for each mode
  std::vector<HWDisplayAttributes> attrib(num_modes);
  std::vector<uint32_t> vics(num_modes);
  for (index = 0; index < num_modes; index++) {
    hw_intf_->GetDisplayAttributes(index, &attrib[index]);
    hw_intf_->GetVideoFormat(index, &vics[index]);
  }
  try {
    char cr = '\r';
    while (std::getline(res_file, line, cr)) {
      char hash = '#';
      std::size_t found = 0;
      found = line.find(hash);
      if (found != std::string::npos) {
        // # is found, ignore this line
        DLOGI("Hash found");
        continue;
      }
      char colon = ':';
      found = line.find(colon);
      if (found != std::string::npos) {
        DLOGI("Colon found at %d", found);
        std::string vic_str = line.substr(0, found);
        int vic = std::stoi(vic_str);
        if (vic > STANDARD_VIC) {
          DLOGE("Invalid svd %d", vic);
          continue;
        }
        std::string fmt_str = line.substr(found+1, line.size());
        std::map<std::string, DisplayInterfaceFormat>::iterator fmt_str_it =
                                            intf_format_to_str.find(fmt_str);
        if (fmt_str_it == intf_format_to_str.end()) {
          DLOGE("Invalid color token %s", fmt_str.c_str());
          continue;
        }
        DisplayInterfaceFormat fmt = fmt_str_it->second;
        DLOGI("Preferred format = %d",fmt);
        std::vector<uint32_t>::iterator vic_itr = std::find(vics.begin(), vics.end(), vic);
        if (vic_itr != vics.end())
        {
          uint32_t index = static_cast<uint32_t>(vic_itr - vics.begin());
          DLOGI("Display supports vic %d!.. index = %d", vic, index);
          if (fmt == DisplayInterfaceFormat::kFormatRGB) {
            if (attrib[index].pixel_formats & DisplayInterfaceFormat::kFormatRGB) {
              error = hw_intf_->SetDisplayFormat(index, fmt);
              if (error == kErrorNone) {
                DLOGI("RGB is supported by Display attributes[%d]", index);
                return index;
              }
            } else {
              DLOGI("RGB not supported by Display attributes[%d]", index);
            }
          } else if (fmt == DisplayInterfaceFormat::kFormatYUV) {
            if(attrib[index].pixel_formats & DisplayInterfaceFormat::kFormatYUV) {
              error = hw_intf_->SetDisplayFormat(index, fmt);
              if (error == kErrorNone) {
                DLOGI("YUV is supported by Display attributes[%d]", index);
                return index;
              }
            } else {
              DLOGI("YUV not supported by Display attributes[%d]", index);
            }
          } else {
            DLOGI("Invalid format %d", fmt);
          }
        } else {
          DLOGI("Display does not support vic %d ", vic);
        }
      } else {
        DLOGE("Delimiter : not found");
      }
    }
  } catch (const std::invalid_argument& ia) {
    DLOGE("Invalid argument exception %s", ia.what());
    return 0;
  }
  catch (const std::exception& e) {
    DLOGE("Exception occurred %s", e.what());
    return 0;
  } catch(...) {
      DLOGE("Exception occurred!");
      return 0;
  }
  // None of the resolutions are supported by TV.
  const int default_vic = 2;   // Default to 480p RGB.
  DisplayInterfaceFormat def_fmt = DisplayInterfaceFormat::kFormatRGB;
  std::vector<uint32_t>::iterator def_vic_itr = std::find(vics.begin(), vics.end(), default_vic);
  if (def_vic_itr != vics.end())
  {
    uint32_t def_index = static_cast<uint32_t>(def_vic_itr - vics.begin());
    error = hw_intf_->SetDisplayFormat(def_index, def_fmt);
    if (error != kErrorNone) {
      DLOGE("Unable to set RGB for 480p");
      return def_index;
    } else {
      DLOGI("Selected 480p RGB, Display attributes[%d]", def_index);
    }
  } else {
    // Even 480p is not supported.
    DLOGE("480p is not supported!");
    return 0;
  }
  return 0;
}

uint32_t DisplayHDMI::GetBestConfig(HWS3DMode s3d_mode) {
  uint32_t best_index = 0, index;
  uint32_t num_modes = 0;

  std::vector<uint32_t> hdmi_modes;

  hw_intf_->GetHdmiMode(hdmi_modes);

  for(uint32_t i =0;i < hdmi_modes.size();i++)
  {
    DLOGI("hdmi_modes val = %u", hdmi_modes[i]);
  }

  hw_intf_->GetNumDisplayAttributes(&num_modes);
  DLOGI("Number of modes = %d",num_modes);
  // Get display attribute for each mode
  std::vector<HWDisplayAttributes> attrib(num_modes);
  for (index = 0; index < num_modes; index++) {
    hw_intf_->GetDisplayAttributes(index, &attrib[index]);
    DLOGI("Index = %d. wxh = %dx%d",index, attrib[index].x_pixels, attrib[index].y_pixels);
  }

  // Select best config for s3d_mode. If s3d is not enabled, s3d_mode is kS3DModeNone
  for (index = 0; index < num_modes; index ++) {
    if (attrib[index].s3d_config[s3d_mode]) {
      break;
    }
  }
  if (index < num_modes) {
    best_index = UINT32(index);
    for (size_t index = best_index + 1; index < num_modes; index ++) {
      if (!attrib[index].s3d_config[s3d_mode])
        continue;

      // pixel_formats == kFormatYUV means only YUV bit is set.
      uint32_t best_clock_khz =
                (attrib[best_index].pixel_formats == DisplayInterfaceFormat::kFormatYUV) ?
                attrib[best_index].clock_khz/2 : attrib[best_index].clock_khz;
      uint32_t current_clock_khz =
                (attrib[index].pixel_formats == DisplayInterfaceFormat::kFormatYUV) ?
                attrib[index].clock_khz/2 : attrib[index].clock_khz;
      if (current_clock_khz > best_clock_khz) {
        DLOGI("Best index = %d .Best pixel clock = %d .Previous best was %d",
              index,current_clock_khz,best_clock_khz);
        best_index = UINT32(index);
      } else if (current_clock_khz == best_clock_khz) {
         DLOGI("Same pix clock. clock = %d . v1 = %d.. v2 = %d",
         current_clock_khz,hdmi_modes[best_index],hdmi_modes[index]);
        if ((hdmi_modes[index] > STANDARD_VIC && hdmi_modes[best_index] <= STANDARD_VIC)) {
          // we should not select the non-standard vic-id.
          DLOGI("Standard vic already selected");
          continue;
        } else if((hdmi_modes[index] <= STANDARD_VIC && hdmi_modes[best_index] > STANDARD_VIC)) {
          // select the standard vic-id
          best_index = UINT32(index);
          DLOGI("Selecting Standard vic now. Best index = %d", best_index);
          continue;
        }
        if (attrib[index].x_pixels > attrib[best_index].x_pixels) {
          DLOGI("Best index = %d .Best xpixel  = %d .Previous best was %d",
                index,attrib[index].x_pixels,attrib[best_index].x_pixels);
          best_index = UINT32(index);
        } else if (attrib[index].x_pixels == attrib[best_index].x_pixels) {
          if (attrib[index].y_pixels > attrib[best_index].y_pixels) {
            DLOGI("Best index = %d .Best ypixel  = %d .Previous best was %d",
                   index,attrib[index].y_pixels,attrib[best_index].y_pixels);
            best_index = UINT32(index);
          } else if (attrib[index].y_pixels == attrib[best_index].y_pixels) {
            if (attrib[index].vsync_period_ns < attrib[best_index].vsync_period_ns) {
              DLOGI("Best index = %d .Best vsync_period  = %d .Previous best was %d",
                    index,attrib[index].vsync_period_ns,attrib[best_index].vsync_period_ns);
              best_index = UINT32(index);
            }
          }
        }
      }
    }
  } else {
    DLOGW("%s, could not support S3D mode from EDID info. S3D mode is %d",
          __FUNCTION__, s3d_mode);
  }

  // Used for changing HDMI Resolution - override the best with user set config
  uint32_t user_config = UINT32(Debug::GetHDMIResolution());
  if (user_config) {
    uint32_t config_index = 0;
    // For the config, get the corresponding index
    DisplayError error = hw_intf_->GetConfigIndex(user_config, &config_index);
    if (error == kErrorNone)
      return config_index;
  }

  return best_index;
}

void DisplayHDMI::GetScanSupport() {
  DisplayError error = kErrorNone;
  uint32_t video_format = 0;
  uint32_t max_cea_format = 0;
  HWScanInfo scan_info = HWScanInfo();
  hw_intf_->GetHWScanInfo(&scan_info);

  uint32_t active_mode_index = 0;
  hw_intf_->GetActiveConfig(&active_mode_index);

  error = hw_intf_->GetVideoFormat(active_mode_index, &video_format);
  if (error != kErrorNone) {
    return;
  }

  error = hw_intf_->GetMaxCEAFormat(&max_cea_format);
  if (error != kErrorNone) {
    return;
  }

  // The scan support for a given HDMI TV must be read from scan info corresponding to
  // Preferred Timing if the preferred timing of the display is currently active, and if it is
  // valid. In all other cases, we must read the scan support from CEA scan info if
  // the resolution is a CEA resolution, or from IT scan info for all other resolutions.
  if (active_mode_index == 0 && scan_info.pt_scan_support != kScanNotSupported) {
    scan_support_ = scan_info.pt_scan_support;
  } else if (video_format < max_cea_format) {
    scan_support_ = scan_info.cea_scan_support;
  } else {
    scan_support_ = scan_info.it_scan_support;
  }
}

void DisplayHDMI::SetS3DMode(LayerStack *layer_stack) {
  uint32_t s3d_layer_count = 0;
  HWS3DMode s3d_mode = kS3DModeNone;
  uint32_t layer_count = UINT32(layer_stack->layers.size());

  // S3D mode is supported for the following scenarios:
  // 1. Layer stack containing only one s3d layer which is not skip
  // 2. Layer stack containing only one secure layer along with one s3d layer
  for (uint32_t i = 0; i < layer_count; i++) {
    Layer *layer = layer_stack->layers.at(i);
    LayerBuffer &layer_buffer = layer->input_buffer;

    if (layer_buffer.s3d_format != kS3dFormatNone) {
      s3d_layer_count++;
      if (s3d_layer_count > 1 || layer->flags.skip) {
        s3d_mode = kS3DModeNone;
        break;
      }

      std::map<LayerBufferS3DFormat, HWS3DMode>::iterator it =
                s3d_format_to_mode_.find(layer_buffer.s3d_format);
      if (it != s3d_format_to_mode_.end()) {
        s3d_mode = it->second;
      }
    } else if (layer_buffer.flags.secure && layer_count > 2) {
        s3d_mode = kS3DModeNone;
        break;
    }
  }

  if (hw_intf_->SetS3DMode(s3d_mode) != kErrorNone) {
    hw_intf_->SetS3DMode(kS3DModeNone);
    layer_stack->flags.s3d_mode_present = false;
  } else if (s3d_mode != kS3DModeNone) {
    layer_stack->flags.s3d_mode_present = true;
  }

  DisplayBase::ReconfigureDisplay();
}

DisplayError DisplayHDMI::VSync(int64_t timestamp) {
  if (vsync_enable_) {
    DisplayEventVSync vsync;
    vsync.timestamp = timestamp;
    event_handler_->VSync(vsync);
  }

  return kErrorNone;
}

}  // namespace sdm

