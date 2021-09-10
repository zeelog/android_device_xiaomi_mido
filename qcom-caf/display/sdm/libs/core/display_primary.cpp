/*
* Copyright (c) 2014 - 2018, The Linux Foundation. All rights reserved.
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
#include <utils/rect.h>
#include <map>
#include <algorithm>
#include <functional>
#include <vector>

#include "display_primary.h"
#include "hw_interface.h"
#include "hw_info_interface.h"

#define __CLASS__ "DisplayPrimary"

namespace sdm {

DisplayPrimary::DisplayPrimary(DisplayEventHandler *event_handler, HWInfoInterface *hw_info_intf,
                               BufferSyncHandler *buffer_sync_handler,
                               BufferAllocator *buffer_allocator, CompManager *comp_manager)
  : DisplayBase(kPrimary, event_handler, kDevicePrimary, buffer_sync_handler, buffer_allocator,
                comp_manager, hw_info_intf) {
}

DisplayError DisplayPrimary::Init() {
  lock_guard<recursive_mutex> obj(recursive_mutex_);

  DisplayError error = HWInterface::Create(kPrimary, hw_info_intf_, buffer_sync_handler_,
                                           buffer_allocator_, &hw_intf_);
  if (error != kErrorNone) {
    return error;
  }

  error = DisplayBase::Init();
  if (error != kErrorNone) {
    HWInterface::Destroy(hw_intf_);
    return error;
  }

  if (hw_panel_info_.mode == kModeCommand && Debug::IsVideoModeEnabled()) {
    error = hw_intf_->SetDisplayMode(kModeVideo);
    if (error != kErrorNone) {
      DLOGW("Retaining current display mode. Current = %d, Requested = %d", hw_panel_info_.mode,
            kModeVideo);
    }
  }

  avr_prop_disabled_ = Debug::IsAVRDisabled();

  error = HWEventsInterface::Create(INT(display_type_), this, event_list_, &hw_events_intf_);
  if (error != kErrorNone) {
    DLOGE("Failed to create hardware events interface. Error = %d", error);
    DisplayBase::Deinit();
    HWInterface::Destroy(hw_intf_);
  }

  return error;
}

DisplayError DisplayPrimary::Prepare(LayerStack *layer_stack) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  DisplayError error = kErrorNone;
  uint32_t new_mixer_width = 0;
  uint32_t new_mixer_height = 0;
  uint32_t display_width = display_attributes_.x_pixels;
  uint32_t display_height = display_attributes_.y_pixels;

  if (NeedsMixerReconfiguration(layer_stack, &new_mixer_width, &new_mixer_height)) {
    error = ReconfigureMixer(new_mixer_width, new_mixer_height);
    if (error != kErrorNone) {
      ReconfigureMixer(display_width, display_height);
    }
  }

  // Clean hw layers for reuse.
  hw_layers_ = HWLayers();
  hw_layers_.hw_avr_info.enable = NeedsAVREnable();

  return DisplayBase::Prepare(layer_stack);
}

DisplayError DisplayPrimary::Commit(LayerStack *layer_stack) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  DisplayError error = kErrorNone;
  uint32_t app_layer_count = hw_layers_.info.app_layer_count;

  // Enabling auto refresh is async and needs to happen before commit ioctl
  if (hw_panel_info_.mode == kModeCommand) {
    bool enable = (app_layer_count == 1) && layer_stack->flags.single_buffered_layer_present;
    bool need_refresh = layer_stack->flags.single_buffered_layer_present && (app_layer_count > 1);

    hw_intf_->SetAutoRefresh(enable);
    if (need_refresh) {
      event_handler_->Refresh();
    }
  }

  error = DisplayBase::Commit(layer_stack);
  if (error != kErrorNone) {
    return error;
  }

  DisplayBase::ReconfigureDisplay();

  int idle_time_ms = hw_layers_.info.set_idle_time_ms;
  if (idle_time_ms >= 0) {
    hw_intf_->SetIdleTimeoutMs(UINT32(idle_time_ms));
  }

  if (switch_to_cmd_) {
    uint32_t pending;
    switch_to_cmd_ = false;
    ControlPartialUpdate(true /* enable */, &pending);
  }

  return error;
}

DisplayError DisplayPrimary::SetDisplayState(DisplayState state) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  DisplayError error = kErrorNone;
  error = DisplayBase::SetDisplayState(state);
  if (error != kErrorNone) {
    return error;
  }

  // Set vsync enable state to false, as driver disables vsync during display power off.
  if (state == kStateOff) {
    vsync_enable_ = false;
  }

  return kErrorNone;
}

void DisplayPrimary::SetIdleTimeoutMs(uint32_t active_ms) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);

  if (comp_manager_->SetIdleTimeoutMs(display_comp_ctx_, active_ms) == kErrorNone) {
    hw_intf_->SetIdleTimeoutMs(active_ms);
  }
}

DisplayError DisplayPrimary::SetDisplayMode(uint32_t mode) {
  DisplayError error = kErrorNone;

  // Limit scope of mutex to this block
  {
    lock_guard<recursive_mutex> obj(recursive_mutex_);
    HWDisplayMode hw_display_mode = static_cast<HWDisplayMode>(mode);
    uint32_t pending = 0;

    if (!active_) {
      DLOGW("Invalid display state = %d. Panel must be on.", state_);
      return kErrorNotSupported;
    }

    if (hw_display_mode != kModeCommand && hw_display_mode != kModeVideo) {
      DLOGW("Invalid panel mode parameters. Requested = %d", hw_display_mode);
      return kErrorParameters;
    }

    if (hw_display_mode == hw_panel_info_.mode) {
      DLOGW("Same display mode requested. Current = %d, Requested = %d", hw_panel_info_.mode,
            hw_display_mode);
      return kErrorNone;
    }

    error = hw_intf_->SetDisplayMode(hw_display_mode);
    if (error != kErrorNone) {
      DLOGW("Retaining current display mode. Current = %d, Requested = %d", hw_panel_info_.mode,
            hw_display_mode);
      return error;
    }

    if (mode == kModeVideo) {
      ControlPartialUpdate(false /* enable */, &pending);
    } else if (mode == kModeCommand) {
      // Flush idle timeout value currently set.
      hw_intf_->SetIdleTimeoutMs(0);
      switch_to_cmd_ = true;
    }
  }

  // Request for a new draw cycle. New display mode will get applied on next draw cycle.
  // New idle time will get configured as part of this.
  event_handler_->Refresh();

  return error;
}

DisplayError DisplayPrimary::SetPanelBrightness(int level) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  return hw_intf_->SetPanelBrightness(level);
}

DisplayError DisplayPrimary::GetRefreshRateRange(uint32_t *min_refresh_rate,
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

DisplayError DisplayPrimary::SetRefreshRate(uint32_t refresh_rate) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);

  if (!active_ || !hw_panel_info_.dynamic_fps) {
    return kErrorNotSupported;
  }

  if (refresh_rate < hw_panel_info_.min_fps || refresh_rate > hw_panel_info_.max_fps) {
    DLOGE("Invalid Fps = %d request", refresh_rate);
    return kErrorParameters;
  }

  DisplayError error = hw_intf_->SetRefreshRate(refresh_rate);
  if (error != kErrorNone) {
    return error;
  }

  return DisplayBase::ReconfigureDisplay();
}

DisplayError DisplayPrimary::VSync(int64_t timestamp) {
  if (vsync_enable_) {
    DisplayEventVSync vsync;
    vsync.timestamp = timestamp;
    event_handler_->VSync(vsync);
  }

  return kErrorNone;
}

void DisplayPrimary::IdleTimeout() {
  event_handler_->Refresh();
  comp_manager_->ProcessIdleTimeout(display_comp_ctx_);
  event_handler_->HandleEvent(kIdleTimeout);
}

void DisplayPrimary::ThermalEvent(int64_t thermal_level) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  comp_manager_->ProcessThermalEvent(display_comp_ctx_, thermal_level);
  event_handler_->HandleEvent(kThermalEvent);
}

void DisplayPrimary::IdlePowerCollapse() {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  comp_manager_->ProcessIdlePowerCollapse(display_comp_ctx_);
}

DisplayError DisplayPrimary::GetPanelBrightness(int *level) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  return hw_intf_->GetPanelBrightness(level);
}

DisplayError DisplayPrimary::ControlPartialUpdate(bool enable, uint32_t *pending) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  if (!pending) {
    return kErrorParameters;
  }

  if (!hw_panel_info_.partial_update) {
    // Nothing to be done.
    DLOGI("partial update is not applicable for display=%d", display_type_);
    return kErrorNotSupported;
  }

  *pending = 0;
  if (enable == partial_update_control_) {
    DLOGI("Same state transition is requested.");
    return kErrorNone;
  }

  partial_update_control_ = enable;

  if (!enable) {
    // If the request is to turn off feature, new draw call is required to have
    // the new setting into effect.
    *pending = 1;
  }

  return kErrorNone;
}

DisplayError DisplayPrimary::DisablePartialUpdateOneFrame() {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  disable_pu_one_frame_ = true;

  return kErrorNone;
}

bool DisplayPrimary::NeedsAVREnable() {
  if (avr_prop_disabled_) {
    return false;
  }

  return (hw_panel_info_.mode == kModeVideo && ((hw_panel_info_.dynamic_fps &&
          hw_panel_info_.dfps_porch_mode) || (!hw_panel_info_.dynamic_fps &&
          hw_panel_info_.min_fps != hw_panel_info_.max_fps)));
}

DisplayError DisplayPrimary::SetDynamicDSIClock(uint64_t bitclk) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  if (!hw_panel_info_.bitclk_update) {
    DLOGI("BitClk update is not supported for display=%d", display_type_);
    return kErrorNotSupported;
  }

  std::vector<uint64_t> &bitclk_rates = hw_panel_info_.bitclk_rates;
  bool valid = std::find(bitclk_rates.begin(), bitclk_rates.end(), bitclk) != bitclk_rates.end();
  if (bitclk == bitclk_ || !valid) {
    DLOGI("Invalid setting %d, Clk. already set %d", (bitclk == bitclk_), !valid);
    return kErrorNone;
  }

  DisplayError error = hw_intf_->SetDynamicDSIClock(bitclk);
  if (error != kErrorNone) {
    return error;
  }

  bitclk_ = bitclk;
  return kErrorNone;
}

DisplayError DisplayPrimary::GetDynamicDSIClock(uint64_t *bitclk) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);

  return hw_intf_->GetDynamicDSIClock(bitclk);
}

DisplayError DisplayPrimary::GetSupportedDSIClock(std::vector<uint64_t> *bitclk_rates) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);

  *bitclk_rates = hw_panel_info_.bitclk_rates;
  return kErrorNone;
}

}  // namespace sdm

