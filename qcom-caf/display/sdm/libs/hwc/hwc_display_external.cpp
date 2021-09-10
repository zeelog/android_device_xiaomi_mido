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

#include <cutils/properties.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <algorithm>

#include "hwc_display_external.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCDisplayExternal"

namespace sdm {

int HWCDisplayExternal::Create(CoreInterface *core_intf, hwc_procs_t const **hwc_procs,
                               qService::QService *qservice, HWCDisplay **hwc_display) {
  return Create(core_intf, hwc_procs, 0, 0, qservice, false, hwc_display);
}

int HWCDisplayExternal::Create(CoreInterface *core_intf, hwc_procs_t const **hwc_procs,
                               uint32_t primary_width, uint32_t primary_height,
                               qService::QService *qservice, bool use_primary_res,
                               HWCDisplay **hwc_display) {
  uint32_t external_width = 0;
  uint32_t external_height = 0;
  int drc_enabled = 0;
  int drc_reset_fps_enabled = 0;
  DisplayError error = kErrorNone;

  HWCDisplay *hwc_display_external = new HWCDisplayExternal(core_intf, hwc_procs, qservice);
  int status = hwc_display_external->Init();
  if (status) {
    delete hwc_display_external;
    return status;
  }

  error = hwc_display_external->GetMixerResolution(&external_width, &external_height);
  if (error != kErrorNone) {
    return -EINVAL;
  }

  if (primary_width && primary_height) {
    // use_primary_res means HWCDisplayExternal should directly set framebuffer resolution to the
    // provided primary_width and primary_height
    if (use_primary_res) {
      external_width = primary_width;
      external_height = primary_height;
    } else {
      int downscale_enabled = 0;
      HWCDebugHandler::Get()->GetProperty("sdm.debug.downscale_external", &downscale_enabled);
      if (downscale_enabled) {
        GetDownscaleResolution(primary_width, primary_height, &external_width, &external_height);
      }
    }
  }

  status = hwc_display_external->SetFrameBufferResolution(external_width, external_height);
  if (status) {
    Destroy(hwc_display_external);
    return status;
  }

  HWCDebugHandler::Get()->GetProperty("sdm.hdmi.drc_enabled", &(drc_enabled));
  reinterpret_cast<HWCDisplayExternal *>(hwc_display_external)->drc_enabled_ = drc_enabled;

  HWCDebugHandler::Get()->GetProperty("sdm.hdmi.drc_reset_fps", &(drc_reset_fps_enabled));
  reinterpret_cast<HWCDisplayExternal *>(hwc_display_external)->drc_reset_fps_enabled_ =
                                                                drc_reset_fps_enabled;

  *hwc_display = hwc_display_external;

  return status;
}

void HWCDisplayExternal::Destroy(HWCDisplay *hwc_display) {
  hwc_display->Deinit();
  delete hwc_display;
}

HWCDisplayExternal::HWCDisplayExternal(CoreInterface *core_intf, hwc_procs_t const **hwc_procs,
                                       qService::QService *qservice)
  : HWCDisplay(core_intf, hwc_procs, kHDMI, HWC_DISPLAY_EXTERNAL, false, qservice,
               DISPLAY_CLASS_EXTERNAL) {
}

int HWCDisplayExternal::Prepare(hwc_display_contents_1_t *content_list) {
  int status = 0;
  DisplayError error = kErrorNone;

  if (secure_display_active_) {
    MarkLayersForGPUBypass(content_list);
    return status;
  }

  status = AllocateLayerStack(content_list);
  if (status) {
    return status;
  }

  status = PrePrepareLayerStack(content_list);
  if (status) {
    return status;
  }

  if (content_list->numHwLayers <= 1) {
    flush_ = true;
    return 0;
  }

  bool one_video_updating_layer = SingleVideoLayerUpdating(UINT32(content_list->numHwLayers - 1));

  uint32_t refresh_rate = GetOptimalRefreshRate(one_video_updating_layer);
  if (current_refresh_rate_ != refresh_rate) {
    error = display_intf_->SetRefreshRate(refresh_rate);
    if (error == kErrorNone) {
      // On success, set current refresh rate to new refresh rate
      current_refresh_rate_ = refresh_rate;
    }
  }

  status = PrepareLayerStack(content_list);
  if (status) {
    return status;
  }

  return 0;
}

int HWCDisplayExternal::Commit(hwc_display_contents_1_t *content_list) {
  int status = 0;

  if (secure_display_active_) {
    return status;
  }

  status = HWCDisplay::CommitLayerStack(content_list);
  if (status) {
    return status;
  }

  status = HWCDisplay::PostCommitLayerStack(content_list);
  if (status) {
    return status;
  }

  return 0;
}

void HWCDisplayExternal::ApplyScanAdjustment(hwc_rect_t *display_frame) {
  if (display_intf_->IsUnderscanSupported()) {
    return;
  }

  // Read user defined width and height ratio
  int width = 0, height = 0;
  HWCDebugHandler::Get()->GetProperty("sdm.external_action_safe_width", &width);
  float width_ratio = FLOAT(width) / 100.0f;
  HWCDebugHandler::Get()->GetProperty("sdm.external_action_safe_height", &height);
  float height_ratio = FLOAT(height) / 100.0f;

  if (width_ratio == 0.0f ||  height_ratio == 0.0f) {
    return;
  }

  uint32_t mixer_width = 0;
  uint32_t mixer_height = 0;
  GetMixerResolution(&mixer_width, &mixer_height);

  if (mixer_width == 0 || mixer_height == 0) {
    DLOGV("Invalid mixer dimensions (%d, %d)", mixer_width, mixer_height);
    return;
  }

  uint32_t new_mixer_width = UINT32(mixer_width * FLOAT(1.0f - width_ratio));
  uint32_t new_mixer_height = UINT32(mixer_height * FLOAT(1.0f - height_ratio));

  int x_offset = INT((FLOAT(mixer_width) * width_ratio) / 2.0f);
  int y_offset = INT((FLOAT(mixer_height) * height_ratio) / 2.0f);

  display_frame->left = (display_frame->left * INT32(new_mixer_width) / INT32(mixer_width))
                        + x_offset;
  display_frame->top = (display_frame->top * INT32(new_mixer_height) / INT32(mixer_height)) +
                       y_offset;
  display_frame->right = ((display_frame->right * INT32(new_mixer_width)) / INT32(mixer_width)) +
                         x_offset;
  display_frame->bottom = ((display_frame->bottom * INT32(new_mixer_height)) / INT32(mixer_height))
                          + y_offset;
}

void HWCDisplayExternal::SetSecureDisplay(bool secure_display_active, bool force_flush) {
  if (secure_display_active_ != secure_display_active) {
    secure_display_active_ = secure_display_active;

    if (secure_display_active_) {
      DisplayError error = display_intf_->Flush();
      if (error != kErrorNone) {
        DLOGE("Flush failed. Error = %d", error);
      }
    }
  }
  return;
}

static void AdjustSourceResolution(uint32_t dst_width, uint32_t dst_height, uint32_t *src_width,
                                   uint32_t *src_height) {
  *src_height = (dst_width * (*src_height)) / (*src_width);
  *src_width = dst_width;
}

void HWCDisplayExternal::GetDownscaleResolution(uint32_t primary_width, uint32_t primary_height,
                                        uint32_t *non_primary_width, uint32_t *non_primary_height) {
  uint32_t primary_area = primary_width * primary_height;
  uint32_t non_primary_area = (*non_primary_width) * (*non_primary_height);

  if (primary_area > non_primary_area) {
    if (primary_height > primary_width) {
      std::swap(primary_height, primary_width);
    }
    AdjustSourceResolution(primary_width, primary_height, non_primary_width, non_primary_height);
  }
}

uint32_t HWCDisplayExternal::RoundToStandardFPS(float fps) {
  static const uint32_t standard_fps[] = {23976, 24000, 25000, 29970, 30000, 50000, 59940, 60000};
  static const uint32_t mapping_fps[] = {59940, 60000, 60000, 59940, 60000, 50000, 59940, 60000};
  uint32_t frame_rate = (uint32_t)(fps * 1000);

  // process non valid
  if (frame_rate == 0) {
    return current_refresh_rate_;
  }

  int count = INT(sizeof(standard_fps) / sizeof(standard_fps[0]));
  for (int i = 0; i < count; i++) {
    // Most likely used for video, the fps for frames should be stable from video side.
    if (standard_fps[i] > frame_rate) {
      if (i > 0) {
        if ((standard_fps[i] - frame_rate) > (frame_rate - standard_fps[i-1])) {
          return mapping_fps[i-1];
        } else {
          return mapping_fps[i];
        }
      } else {
        return mapping_fps[i];
      }
    }
  }

  return standard_fps[count - 1];
}

void HWCDisplayExternal::PrepareDynamicRefreshRate(Layer *layer) {
  if (layer->input_buffer.flags.video) {
    if (layer->frame_rate != 0) {
      metadata_refresh_rate_ = SanitizeRefreshRate(layer->frame_rate);
    } else {
      metadata_refresh_rate_ = current_refresh_rate_;
    }
    layer->frame_rate = current_refresh_rate_;
  } else if (!layer->frame_rate) {
    layer->frame_rate = current_refresh_rate_;
  }
}

void HWCDisplayExternal::ForceRefreshRate(uint32_t refresh_rate) {
  if ((refresh_rate && (refresh_rate < min_refresh_rate_ || refresh_rate > max_refresh_rate_)) ||
       force_refresh_rate_ == refresh_rate) {
    // Cannot honor force refresh rate, as its beyond the range or new request is same
    return;
  }

  force_refresh_rate_ = refresh_rate;
}

uint32_t HWCDisplayExternal::GetOptimalRefreshRate(bool one_updating_layer) {
  if (force_refresh_rate_) {
    return force_refresh_rate_;
  } else if (one_updating_layer && drc_enabled_) {
    return metadata_refresh_rate_;
  }

  if (drc_reset_fps_enabled_) {
    DisplayConfigVariableInfo fb_config;
    display_intf_->GetFrameBufferConfig(&fb_config);
    return (fb_config.fps * 1000);
  }

  return current_refresh_rate_;
}

int HWCDisplayExternal::Perform(uint32_t operation, ...) {
  va_list args;
  va_start(args, operation);
  int val = va_arg(args, int32_t);
  va_end(args);
  switch (operation) {
    case SET_BINDER_DYN_REFRESH_RATE:
      ForceRefreshRate(UINT32(val));
      break;
    default:
      DLOGW("Invalid operation %d", operation);
      return -EINVAL;
  }

  return 0;
}

}  // namespace sdm

