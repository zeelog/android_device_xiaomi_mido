/*
* Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
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

#include <core/buffer_allocator.h>
#include <utils/debug.h>
#include <sync/sync.h>
#include <profiler.h>
#include <errno.h>
#include <math.h>

#include "hwc_buffer_sync_handler.h"
#include "hwc_session.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCSession"

namespace sdm {

void HWCSession::StartServices() {
  int error = DisplayConfig::DeviceInterface::RegisterDevice(this);
  if (error) {
  ALOGW("%s::%s: Could not register IDisplayConfig as service (%d).",
          __CLASS__, __FUNCTION__, error);

  } else {
    ALOGI("%s::%s: IDisplayConfig service registration completed.", __CLASS__, __FUNCTION__);
  }
}

int MapDisplayType(DispType dpy) {
  switch (dpy) {
    case DispType::kPrimary:
      return HWC_DISPLAY_PRIMARY;

    case DispType::kExternal:
      return HWC_DISPLAY_EXTERNAL;

    case DispType::kVirtual:
      return HWC_DISPLAY_VIRTUAL;

    default:
      break;
  }

  return -EINVAL;
}

HWCDisplay::DisplayStatus MapExternalStatus(DisplayConfig::ExternalStatus status) {
  switch (status) {
    case DisplayConfig::ExternalStatus::kOffline:
      return HWCDisplay::kDisplayStatusOffline;

    case DisplayConfig::ExternalStatus::kOnline:
      return HWCDisplay::kDisplayStatusOnline;

    case DisplayConfig::ExternalStatus::kPause:
      return HWCDisplay::kDisplayStatusPause;

    case DisplayConfig::ExternalStatus::kResume:
      return HWCDisplay::kDisplayStatusResume;

    default:
      break;
  }

  return HWCDisplay::kDisplayStatusInvalid;
}

int HWCSession::RegisterClientContext(std::shared_ptr<DisplayConfig::ConfigCallback> callback,
                                      DisplayConfig::ConfigInterface **intf) {
  if (!intf) {
    DLOGE("Invalid DisplayConfigIntf location");
    return -EINVAL;
  }

  std::weak_ptr<DisplayConfig::ConfigCallback> wp_callback = callback;
  DisplayConfigImpl *impl = new DisplayConfigImpl(wp_callback, this);
  *intf = impl;

  return 0;
}

void HWCSession::UnRegisterClientContext(DisplayConfig::ConfigInterface *intf) {
  delete static_cast<DisplayConfigImpl *>(intf);
}

HWCSession::DisplayConfigImpl::DisplayConfigImpl(
                               std::weak_ptr<DisplayConfig::ConfigCallback> callback,
                               HWCSession *hwc_session) {
  callback_ = callback;
  hwc_session_ = hwc_session;
}

int HWCSession::DisplayConfigImpl::IsDisplayConnected(DispType dpy, bool *connected) {
  int disp_id = MapDisplayType(dpy);

  if (disp_id < HWC_DISPLAY_PRIMARY || disp_id >= HWC_NUM_DISPLAY_TYPES) {
    return -EINVAL;
  }

  SEQUENCE_WAIT_SCOPE_LOCK(hwc_session_->locker_[disp_id]);

  if (disp_id >= 0) {
    *connected = hwc_session_->hwc_display_[disp_id];
  }

  return 0;
}

int HWCSession::SetDisplayStatus(int disp_id, HWCDisplay::DisplayStatus status) {
  if (disp_id < HWC_DISPLAY_PRIMARY || disp_id >= HWC_NUM_DISPLAY_TYPES) {
    DLOGE("Invalid display = %d", disp_id);
    return -EINVAL;
  }

  SEQUENCE_WAIT_SCOPE_LOCK(locker_[disp_id]);
  DLOGI("Display = %d, Status = %d", disp_id, status);

  if (disp_id == HWC_DISPLAY_PRIMARY) {
    DLOGE("Not supported for this display");
  } else if (!hwc_display_[disp_id]) {
    DLOGW("Display is not connected");
  } else {
    return hwc_display_[disp_id]->SetDisplayStatus(status);
  }

  return -EINVAL;
}

int HWCSession::DisplayConfigImpl::SetDisplayStatus(DispType dpy,
                                                    DisplayConfig::ExternalStatus status) {
  return hwc_session_->SetDisplayStatus(MapDisplayType(dpy), MapExternalStatus(status));
}

int HWCSession::DisplayConfigImpl::ConfigureDynRefreshRate(DisplayConfig::DynRefreshRateOp op,
                                                           uint32_t refresh_rate) {
  SEQUENCE_WAIT_SCOPE_LOCK(hwc_session_->locker_[HWC_DISPLAY_PRIMARY]);
  HWCDisplay *hwc_display = hwc_session_->hwc_display_[HWC_DISPLAY_PRIMARY];

  switch (op) {
    case DisplayConfig::DynRefreshRateOp::kDisableMetadata:
      return hwc_display->Perform(HWCDisplayPrimary::SET_METADATA_DYN_REFRESH_RATE, false);

    case DisplayConfig::DynRefreshRateOp::kEnableMetadata:
      return hwc_display->Perform(HWCDisplayPrimary::SET_METADATA_DYN_REFRESH_RATE, true);

    case DisplayConfig::DynRefreshRateOp::kSetBinder:
      return hwc_display->Perform(HWCDisplayPrimary::SET_BINDER_DYN_REFRESH_RATE, refresh_rate);

    default:
      DLOGW("Invalid operation %d", op);
      return -EINVAL;
  }

  return 0;
}

int HWCSession::GetConfigCount(int disp_id, uint32_t *count) {
  if (disp_id < HWC_DISPLAY_PRIMARY || disp_id >= HWC_NUM_DISPLAY_TYPES) {
    DLOGE("Invalid display = %d", disp_id);
    return -EINVAL;
  }

  SEQUENCE_WAIT_SCOPE_LOCK(locker_[disp_id]);

  if (hwc_display_[disp_id]) {
    return hwc_display_[disp_id]->GetDisplayConfigCount(count);
  }

  return -EINVAL;
}

int HWCSession::DisplayConfigImpl::GetConfigCount(DispType dpy, uint32_t *count) {
  return hwc_session_->GetConfigCount(MapDisplayType(dpy), count);
}

int HWCSession::GetActiveConfigIndex(int disp_id, uint32_t *config) {
  if (disp_id < HWC_DISPLAY_PRIMARY || disp_id >= HWC_NUM_DISPLAY_TYPES) {
    DLOGE("Invalid display = %d", disp_id);
    return -EINVAL;
  }

  SEQUENCE_WAIT_SCOPE_LOCK(locker_[disp_id]);

  if (hwc_display_[disp_id]) {
    return hwc_display_[disp_id]->GetActiveDisplayConfig(config);
  }

  return -EINVAL;
}

int HWCSession::DisplayConfigImpl::GetActiveConfig(DispType dpy, uint32_t *config) {
  return hwc_session_->GetActiveConfigIndex(MapDisplayType(dpy), config);
}

int HWCSession::SetActiveConfigIndex(int disp_id, uint32_t config) {
  if (disp_id < HWC_DISPLAY_PRIMARY || disp_id >= HWC_NUM_DISPLAY_TYPES) {
    DLOGE("Invalid display = %d", disp_id);
    return -EINVAL;
  }

  SEQUENCE_WAIT_SCOPE_LOCK(locker_[disp_id]);
  int error = -EINVAL;
  if (hwc_display_[disp_id]) {
    error = hwc_display_[disp_id]->SetActiveDisplayConfig(config);
    if (!error) {
      Refresh(0);
    }
  }

  return error;
}

int HWCSession::DisplayConfigImpl::SetActiveConfig(DispType dpy, uint32_t config) {
  return hwc_session_->SetActiveConfigIndex(MapDisplayType(dpy), config);
}

int HWCSession::DisplayConfigImpl::GetDisplayAttributes(uint32_t config_index, DispType dpy,
                                                        DisplayConfig::Attributes *attributes) {
  int error = -EINVAL;

  int disp_id = MapDisplayType(dpy);

  if (disp_id >= HWC_DISPLAY_PRIMARY && disp_id < HWC_NUM_DISPLAY_TYPES) {
    SEQUENCE_WAIT_SCOPE_LOCK(hwc_session_->locker_[disp_id]);
    if (hwc_session_->hwc_display_[disp_id]) {
      DisplayConfigVariableInfo hwc_display_attributes;
      error = hwc_session_->hwc_display_[disp_id]->GetDisplayAttributesForConfig(static_cast<int>(config_index),
                                                                 &hwc_display_attributes);
      if (!error) {
        attributes->vsync_period = hwc_display_attributes.vsync_period_ns;
        attributes->x_res = hwc_display_attributes.x_pixels;
        attributes->y_res = hwc_display_attributes.y_pixels;
        attributes->x_dpi = hwc_display_attributes.x_dpi;
        attributes->y_dpi = hwc_display_attributes.y_dpi;
        attributes->panel_type = DisplayConfig::DisplayPortType::kDefault;
        attributes->is_yuv = hwc_display_attributes.is_yuv;
      }
    }
  }

  return error;
}

int HWCSession::setPanelBrightness(uint32_t level) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_[HWC_DISPLAY_PRIMARY]);
  int32_t error = -EINVAL;

  if (hwc_display_[HWC_DISPLAY_PRIMARY]) {
    error = hwc_display_[HWC_DISPLAY_PRIMARY]->SetPanelBrightness(static_cast<int>(level));
    if (error) {
      DLOGE("Failed to set the panel brightness = %d. Error = %d", level, error);
    }
  }

  return error;
}

int HWCSession::DisplayConfigImpl::SetPanelBrightness(uint32_t level) {
  if (!(0 <= level && level <= 255)) {
    return -EINVAL;
  }

  return hwc_session_->setPanelBrightness(level);
}

int HWCSession::GetPanelBrightness(int *level) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_[HWC_DISPLAY_PRIMARY]);
  int32_t error = -EINVAL;

  if (hwc_display_[HWC_DISPLAY_PRIMARY]) {
    error = hwc_display_[HWC_DISPLAY_PRIMARY]->GetPanelBrightness(level);
    if (error) {
      DLOGE("Failed to get the panel brightness. Error = %d", error);
    }
  }

  return error;
}

int HWCSession::DisplayConfigImpl::GetPanelBrightness(uint32_t *level) {
  int value = 0;
  int32_t error = hwc_session_->GetPanelBrightness(&value);

  *level = static_cast<uint32_t>(value);

  return error;
}

int HWCSession::MinHdcpEncryptionLevelChanged(int disp_id, uint32_t min_enc_level) {
  DLOGI("Display %d", disp_id);

  if (disp_id < HWC_DISPLAY_PRIMARY || disp_id >= HWC_NUM_DISPLAY_TYPES) {
    DLOGE("Invalid display = %d", disp_id);
    return -EINVAL;
  }
  if (hdmi_is_primary_) {
    disp_id = HWC_DISPLAY_PRIMARY;
  }
  SEQUENCE_WAIT_SCOPE_LOCK(locker_[disp_id]);
  if (!hdmi_is_primary_ && disp_id != HWC_DISPLAY_EXTERNAL) {
    DLOGE("Not supported for display");
  } else if (!hwc_display_[disp_id] && !hdmi_is_primary_) {
    DLOGW("Display is not connected");
  } else {
    return hwc_display_[disp_id]->OnMinHdcpEncryptionLevelChange(min_enc_level);
  }

  return -EINVAL;
}

int HWCSession::DisplayConfigImpl::MinHdcpEncryptionLevelChanged(DispType dpy,
                                                                 uint32_t min_enc_level) {
  return hwc_session_->MinHdcpEncryptionLevelChanged(MapDisplayType(dpy), min_enc_level);
}

int HWCSession::refreshScreen() {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_[HWC_DISPLAY_PRIMARY]);
  Refresh(HWC_DISPLAY_PRIMARY);

  return 0;
}

int HWCSession::DisplayConfigImpl::RefreshScreen() {
  return hwc_session_->refreshScreen();
}

int HWCSession::ControlPartialUpdate(int disp_id, bool enable) {
  if (disp_id < HWC_DISPLAY_PRIMARY || disp_id >= HWC_NUM_DISPLAY_TYPES) {
    DLOGE("Invalid display = %d", disp_id);
    return -EINVAL;
  }

  if (disp_id != HWC_DISPLAY_PRIMARY) {
    DLOGW("CONTROL_PARTIAL_UPDATE is not applicable for display = %d", disp_id);
    return -EINVAL;
  }

  SEQUENCE_WAIT_SCOPE_LOCK(locker_[disp_id]);
  HWCDisplay *hwc_display = hwc_display_[HWC_DISPLAY_PRIMARY];
  if (!hwc_display) {
    DLOGE("primary display object is not instantiated");
    return -EINVAL;
  }

  uint32_t pending = 0;
  DisplayError hwc_error = hwc_display->ControlPartialUpdate(enable, &pending);

  if (hwc_error == kErrorNone) {
    if (!pending) {
      return 0;
    }
  } else if (hwc_error == kErrorNotSupported) {
    return 0;
  } else {
    return -EINVAL;
  }

  // Todo(user): Unlock it before sending events to client. It may cause deadlocks in future.
  Refresh(HWC_DISPLAY_PRIMARY);

  // Wait until partial update control is complete
  int error = locker_[disp_id].WaitFinite(kPartialUpdateControlTimeoutMs);

  return error;
}

int HWCSession::DisplayConfigImpl::ControlPartialUpdate(DispType dpy, bool enable) {
  return hwc_session_->ControlPartialUpdate(MapDisplayType(dpy), enable);
}

int HWCSession::ToggleScreenUpdate(bool on) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_[HWC_DISPLAY_PRIMARY]);

  int32_t error = -EINVAL;
  if (hwc_display_[HWC_DISPLAY_PRIMARY]) {
    error = hwc_display_[HWC_DISPLAY_PRIMARY]->ToggleScreenUpdates(on);
    if (error) {
      DLOGE("Failed to toggle screen updates = %d. Error = %d", on, error);
    }
  }

  return error;
}

int HWCSession::DisplayConfigImpl::ToggleScreenUpdate(bool on) {
  return hwc_session_->ToggleScreenUpdate(on);
}

int HWCSession::SetIdleTimeout(uint32_t value) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_[HWC_DISPLAY_PRIMARY]);

  if (hwc_display_[HWC_DISPLAY_PRIMARY]) {
    hwc_display_[HWC_DISPLAY_PRIMARY]->SetIdleTimeoutMs(value);
    return 0;
  }

  return -EINVAL;
}

int HWCSession::DisplayConfigImpl::SetIdleTimeout(uint32_t value) {
  return hwc_session_->SetIdleTimeout(value);
}

int HWCSession::DisplayConfigImpl::GetHDRCapabilities(DispType dpy,
                                                      DisplayConfig::HDRCapsParams *caps) {
  int error = -EINVAL;

  do {
      int disp_id = MapDisplayType(dpy);
      if ((disp_id < 0) || (disp_id >= HWC_NUM_DISPLAY_TYPES)) {
      DLOGE("Invalid display = %d", disp_id);
      break;
    }

     SEQUENCE_WAIT_SCOPE_LOCK(locker_[disp_id]);    
     HWCDisplay *hwc_display = hwc_session_->hwc_display_[disp_id];
    if (!hwc_display) {
      DLOGE("Display = %d is not connected.", disp_id);
      break;
    }

    // query number of hdr types
    uint32_t out_num_types = 0;
    if (hwc_display->GetHdrCapabilities(&out_num_types, nullptr, nullptr, nullptr, nullptr)
        != HWC2::Error::None) {
      break;
    }

    if (!out_num_types) {
      error = 0;
      break;
    }

    // query hdr caps
    caps->supported_hdr_types.resize(out_num_types);

    float out_max_luminance = 0.0f;
    float out_max_average_luminance = 0.0f;
    float out_min_luminance = 0.0f;
    if (hwc_display->GetHdrCapabilities(&out_num_types, caps->supported_hdr_types.data(),
                                        &out_max_luminance, &out_max_average_luminance,
                                        &out_min_luminance)
        == HWC2::Error::None) {
      error = 0;
    }
  } while (false);
    return error;
}

int HWCSession::SetCameraLaunchStatus(uint32_t on) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_[HWC_DISPLAY_PRIMARY]);

  HWBwModes mode = on > 0 ? kBwCamera : kBwDefault;

  // trigger invalidate to apply new bw caps.
  if (callback_reg_) {
    Refresh(HWC_DISPLAY_PRIMARY);
  }

  if (core_intf_->SetMaxBandwidthMode(mode) != kErrorNone) {
    return -EINVAL;
  }
  new_bw_mode_ = true;
  need_invalidate_ = true;
  hwc_display_[HWC_DISPLAY_PRIMARY]->ResetValidation();
  return 0;
}

int HWCSession::DisplayConfigImpl::SetCameraLaunchStatus(uint32_t on) {
  return hwc_session_->SetCameraLaunchStatus(on);
}

int HWCSession::DisplayBWTransactionPending(bool *status) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_[HWC_DISPLAY_PRIMARY]);

  if (hwc_display_[HWC_DISPLAY_PRIMARY]) {
    if (sync_wait(bw_mode_release_fd_, 0) < 0) {
      DLOGI("bw_transaction_release_fd is not yet signaled: err= %s", strerror(errno));
      *status = false;
    }
    return 0;
  }

  DLOGW("Display = %d is not connected.", HWC_DISPLAY_PRIMARY);
  return -ENODEV;
}

int HWCSession::DisplayConfigImpl::DisplayBWTransactionPending(bool *status) {
  return hwc_session_->DisplayBWTransactionPending(status);
}

int HWCSession::DisplayConfigImpl::SetDisplayAnimating(uint64_t display_id, bool animating ) {
  return hwc_session_->CallDisplayFunction(static_cast<hwc2_device_t *>(hwc_session_), display_id,
                             &HWCDisplay::SetDisplayAnimating, animating);
}

int HWCSession::DisplayConfigImpl::IsPowerModeOverrideSupported(uint32_t disp_id,
                                                                bool *supported) {
    *supported = false;
    return 0;
}

int HWCSession::DisplayConfigImpl::IsHDRSupported(uint32_t disp_id, bool *supported) {

  if (disp_id < HWC_DISPLAY_PRIMARY || disp_id >= HWC_NUM_DISPLAY_TYPES) {
    DLOGW("Not valid display");
    return -EINVAL;
  }
  SEQUENCE_WAIT_SCOPE_LOCK(hwc_session_->locker_[disp_id]);
  HWCDisplay *hwc_display = hwc_session_->hwc_display_[disp_id];

  if (!hwc_display) {
    DLOGW("Display = %d is not connected.", disp_id);
    *supported = false;
    return 0;
  }

  // query number of hdr types
  uint32_t out_num_types = 0;
  if (hwc_display->GetHdrCapabilities(&out_num_types, nullptr, nullptr, nullptr, nullptr)
      != HWC2::Error::None) {
    *supported = false;
    return 0;
  }

  if (!out_num_types) {
    *supported = false;
    return 0;
  }

  *supported = true;
  return 0;
}

int HWCSession::DisplayConfigImpl::IsWCGSupported(uint32_t disp_id, bool *supported) {
  // todo(user): Query wcg from sdm. For now assume them same.
  return IsHDRSupported(disp_id, supported);
}

int HWCSession::DisplayConfigImpl::GetDebugProperty(const std::string prop_name,
                                                    std::string value) {
  std::string vendor_prop_name = DISP_PROP_PREFIX;
  int error = -EINVAL;
  char val[64] = {};

  vendor_prop_name += prop_name.c_str();
  if (HWCDebugHandler::Get()->GetProperty(vendor_prop_name.c_str(), val) != kErrorNone) {
    value = val;
    error = 0;
  }

  return error;
}

int HWCSession::DisplayConfigImpl::IsBuiltInDisplay(uint32_t disp_id, bool *is_builtin) {
#ifdef EXCLUDES_MULTI_DISPLAY
  if (HWC_DISPLAY_PRIMARY == disp_id) {
    *is_builtin = true;
  }
#else
  if ((HWC_DISPLAY_PRIMARY == disp_id) || (HWC_DISPLAY_BUILTIN_2 == disp_id) ||
      (HWC_DISPLAY_BUILTIN_3 == disp_id) || (HWC_DISPLAY_BUILTIN_4 == disp_id)) {
   *is_builtin = true;
  }
#endif
  else {
   *is_builtin = false;
  }
  return 0;
}

}  // namespace sdm
