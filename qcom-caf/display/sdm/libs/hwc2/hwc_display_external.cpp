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

#include <cutils/properties.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <algorithm>

#include "hwc_display_external.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCDisplayExternal"

namespace sdm {

int HWCDisplayExternal::Create(CoreInterface *core_intf, HWCBufferAllocator *buffer_allocator,
                               HWCCallbacks *callbacks, qService::QService *qservice,
                               HWCDisplay **hwc_display) {
  return Create(core_intf, buffer_allocator, callbacks, 0, 0, qservice, false, hwc_display);
}

int HWCDisplayExternal::Create(CoreInterface *core_intf, HWCBufferAllocator *buffer_allocator,
                               HWCCallbacks *callbacks,
                               uint32_t primary_width, uint32_t primary_height,
                               qService::QService *qservice, bool use_primary_res,
                               HWCDisplay **hwc_display) {
  uint32_t external_width = 0;
  uint32_t external_height = 0;
  DisplayError error = kErrorNone;

  HWCDisplay *hwc_display_external = new HWCDisplayExternal(core_intf, buffer_allocator, callbacks,
                                                            qservice);
  int status = hwc_display_external->Init();
  if (status) {
    delete hwc_display_external;
    return status;
  }

  error = hwc_display_external->GetMixerResolution(&external_width, &external_height);
  if (error != kErrorNone) {
    Destroy(hwc_display_external);
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
      HWCDebugHandler::Get()->GetProperty(ENABLE_EXTERNAL_DOWNSCALE_PROP, &downscale_enabled);
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

  *hwc_display = hwc_display_external;

  return status;
}

void HWCDisplayExternal::Destroy(HWCDisplay *hwc_display) {
  hwc_display->Deinit();
  delete hwc_display;
}

HWCDisplayExternal::HWCDisplayExternal(CoreInterface *core_intf,
                                       HWCBufferAllocator *buffer_allocator,
                                       HWCCallbacks *callbacks,
                                       qService::QService *qservice)
    : HWCDisplay(core_intf, callbacks, kHDMI, HWC_DISPLAY_EXTERNAL, false, qservice,
                 DISPLAY_CLASS_EXTERNAL, buffer_allocator) {
}

HWC2::Error HWCDisplayExternal::Validate(uint32_t *out_num_types, uint32_t *out_num_requests) {
  auto status = HWC2::Error::None;

  if (secure_display_active_) {
    MarkLayersForGPUBypass();
    return status;
  }

  BuildLayerStack();

  if (layer_set_.empty()) {
    flush_ = true;
    return status;
  }

  status = PrepareLayerStack(out_num_types, out_num_requests);
  return status;
}

HWC2::Error HWCDisplayExternal::Present(int32_t *out_retire_fence) {
  auto status = HWC2::Error::None;

  if (!secure_display_active_) {
    status = HWCDisplay::CommitLayerStack();
    if (status == HWC2::Error::None) {
      status = HWCDisplay::PostCommitLayerStack(out_retire_fence);
    }
  }
  CloseAcquireFds();
  return status;
}

void HWCDisplayExternal::ApplyScanAdjustment(hwc_rect_t *display_frame) {
  if ((underscan_width_ <= 0) || (underscan_height_ <= 0)) {
    return;
  }

  float width_ratio = FLOAT(underscan_width_) / 100.0f;
  float height_ratio = FLOAT(underscan_height_) / 100.0f;

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

void HWCDisplayExternal::SetSecureDisplay(bool secure_display_active) {
  if (secure_display_active_ != secure_display_active) {
    secure_display_active_ = secure_display_active;

    if (secure_display_active_) {
      DisplayError error = display_intf_->Flush(true);
      validated_.reset();
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
                                                uint32_t *non_primary_width,
                                                uint32_t *non_primary_height) {
  uint32_t primary_area = primary_width * primary_height;
  uint32_t non_primary_area = (*non_primary_width) * (*non_primary_height);

  if (primary_area > non_primary_area) {
    if (primary_height > primary_width) {
      std::swap(primary_height, primary_width);
    }
    AdjustSourceResolution(primary_width, primary_height, non_primary_width, non_primary_height);
  }
}

int HWCDisplayExternal::SetState(bool connected) {
  DisplayError error = kErrorNone;
  DisplayState state = kStateOff;
  DisplayConfigVariableInfo fb_config = {};

  if (connected) {
    if (display_null_.IsActive()) {
      error = core_intf_->CreateDisplay(type_, this, &display_intf_);
      if (error != kErrorNone) {
        DLOGE("Display create failed. Error = %d display_type %d event_handler %p disp_intf %p",
              error, type_, this, &display_intf_);
        return -EINVAL;
      }

      // Restore HDMI attributes when display is reconnected.
      // This is to ensure that surfaceflinger & sdm are in sync.
      display_null_.GetFrameBufferConfig(&fb_config);
      int status = SetFrameBufferResolution(fb_config.x_pixels, fb_config.y_pixels);
      if (status) {
        DLOGW("Set frame buffer config failed. Error = %d", error);
        return -1;
      }

      display_null_.GetDisplayState(&state);
      display_intf_->SetDisplayState(state);
      validated_.reset();

      SetVsyncEnabled(HWC2::Vsync::Enable);

      display_null_.SetActive(false);
      DLOGI("Display is connected successfully.");
    } else {
      DLOGI("Display is already connected.");
    }
  } else {
    if (!display_null_.IsActive()) {
      // Preserve required attributes of HDMI display that surfaceflinger sees.
      // Restore HDMI attributes when display is reconnected.
      display_intf_->GetDisplayState(&state);
      display_null_.SetDisplayState(state);

      error = display_intf_->GetFrameBufferConfig(&fb_config);
      if (error != kErrorNone) {
        DLOGW("Get frame buffer config failed. Error = %d", error);
        return -1;
      }
      display_null_.SetFrameBufferConfig(fb_config);

      SetVsyncEnabled(HWC2::Vsync::Disable);
      core_intf_->DestroyDisplay(display_intf_);
      display_intf_ = &display_null_;

      display_null_.SetActive(true);
      DLOGI("Display is disconnected successfully.");
    } else {
      DLOGI("Display is already disconnected.");
    }
  }

  return 0;
}

void HWCDisplayExternal::GetUnderScanConfig() {
  if (!display_intf_->IsUnderscanSupported()) {
    // Read user defined underscan width and height
    HWCDebugHandler::Get()->GetProperty(EXTERNAL_ACTION_SAFE_WIDTH_PROP, &underscan_width_);
    HWCDebugHandler::Get()->GetProperty(EXTERNAL_ACTION_SAFE_HEIGHT_PROP, &underscan_height_);
  }
}

DisplayError HWCDisplayExternal::SetMixerResolution(uint32_t width, uint32_t height) {
  DisplayError error = display_intf_->SetMixerResolution(width, height);
  validated_.reset();
  return error;
}

}  // namespace sdm
