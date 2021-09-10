/*
* Copyright (c) 2014 - 2017, The Linux Foundation. All rights reserved.
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
#include <algorithm>
#include "display_virtual.h"
#include "hw_interface.h"
#include "hw_info_interface.h"

#define __CLASS__ "DisplayVirtual"

namespace sdm {

DisplayVirtual::DisplayVirtual(DisplayEventHandler *event_handler, HWInfoInterface *hw_info_intf,
                               BufferSyncHandler *buffer_sync_handler,
                               BufferAllocator *buffer_allocator, CompManager *comp_manager)
  : DisplayBase(kVirtual, event_handler, kDeviceVirtual, buffer_sync_handler, buffer_allocator,
                comp_manager, hw_info_intf) {
}

DisplayError DisplayVirtual::Init() {
  lock_guard<recursive_mutex> obj(recursive_mutex_);

  DisplayError error = HWInterface::Create(kVirtual, hw_info_intf_, buffer_sync_handler_,
                                           buffer_allocator_, &hw_intf_);
  if (error != kErrorNone) {
    return error;
  }

  HWScaleLutInfo lut_info = {};
  error = comp_manager_->GetScaleLutConfig(&lut_info);
  if (error == kErrorNone) {
    error = hw_intf_->SetScaleLutConfig(&lut_info);
    if (error != kErrorNone) {
      HWInterface::Destroy(hw_intf_);
      return error;
    }
  }

  if (hw_info_intf_) {
    HWResourceInfo hw_resource_info = HWResourceInfo();
    hw_info_intf_->GetHWResourceInfo(&hw_resource_info);
    auto max_mixer_stages = hw_resource_info.num_blending_stages;
    int property_value = Debug::GetMaxPipesPerMixer(display_type_);
    if (property_value >= 0) {
      max_mixer_stages = std::min(UINT32(property_value), hw_resource_info.num_blending_stages);
    }
    DisplayBase::SetMaxMixerStages(max_mixer_stages);
  }

  return error;
}

DisplayError DisplayVirtual::GetNumVariableInfoConfigs(uint32_t *count) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  *count = 1;
  return kErrorNone;
}

DisplayError DisplayVirtual::GetConfig(uint32_t index, DisplayConfigVariableInfo *variable_info) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  *variable_info = display_attributes_;
  return kErrorNone;
}

DisplayError DisplayVirtual::GetActiveConfig(uint32_t *index) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);
  *index = 0;
  return kErrorNone;
}

DisplayError DisplayVirtual::SetActiveConfig(DisplayConfigVariableInfo *variable_info) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);

  if (!variable_info) {
    return kErrorParameters;
  }

  DisplayError error = kErrorNone;
  HWDisplayAttributes display_attributes;
  HWMixerAttributes mixer_attributes;
  HWPanelInfo hw_panel_info = {};
  DisplayConfigVariableInfo fb_config = fb_config_;

  display_attributes.x_pixels = variable_info->x_pixels;
  display_attributes.y_pixels = variable_info->y_pixels;
  display_attributes.fps = variable_info->fps;

  if (display_attributes == display_attributes_) {
    return kErrorNone;
  }

  error = hw_intf_->SetDisplayAttributes(display_attributes);
  if (error != kErrorNone) {
    return error;
  }

  hw_intf_->GetHWPanelInfo(&hw_panel_info);

  error = hw_intf_->GetMixerAttributes(&mixer_attributes);
  if (error != kErrorNone) {
    return error;
  }

  // fb_config will be updated only once after creation of virtual display
  if (fb_config.x_pixels == 0 || fb_config.y_pixels == 0) {
    fb_config = display_attributes;
  }

  // if display is already connected, unregister display from composition manager and register
  // the display with new configuration.
  if (display_comp_ctx_) {
    comp_manager_->UnregisterDisplay(display_comp_ctx_);
  }

  error = comp_manager_->RegisterDisplay(display_type_, display_attributes, hw_panel_info,
                                         mixer_attributes, fb_config, &display_comp_ctx_);
  if (error != kErrorNone) {
    return error;
  }

  display_attributes_ = display_attributes;
  mixer_attributes_ = mixer_attributes;
  hw_panel_info_ = hw_panel_info;
  fb_config_ = fb_config;

  DLOGI("Virtual display resolution changed to[%dx%d]", display_attributes_.x_pixels,
        display_attributes_.y_pixels);

  return kErrorNone;
}

DisplayError DisplayVirtual::Prepare(LayerStack *layer_stack) {
  lock_guard<recursive_mutex> obj(recursive_mutex_);

  // Clean hw layers for reuse.
  hw_layers_ = HWLayers();

  return DisplayBase::Prepare(layer_stack);
}


}  // namespace sdm

