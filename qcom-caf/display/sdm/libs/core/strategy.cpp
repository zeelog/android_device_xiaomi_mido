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

#include "strategy.h"
#include "utils/rect.h"

#define __CLASS__ "Strategy"

namespace sdm {

Strategy::Strategy(ExtensionInterface *extension_intf, BufferAllocator *buffer_allocator,
                   DisplayType type,
                   const HWResourceInfo &hw_resource_info, const HWPanelInfo &hw_panel_info,
                   const HWMixerAttributes &mixer_attributes,
                   const HWDisplayAttributes &display_attributes,
                   const DisplayConfigVariableInfo &fb_config)
  : extension_intf_(extension_intf), display_type_(type), hw_resource_info_(hw_resource_info),
    hw_panel_info_(hw_panel_info), mixer_attributes_(mixer_attributes),
    display_attributes_(display_attributes), fb_config_(fb_config),
    buffer_allocator_(buffer_allocator) {
}

DisplayError Strategy::Init() {
  DisplayError error = kErrorNone;

  if (extension_intf_) {
    error = extension_intf_->CreateStrategyExtn(display_type_, buffer_allocator_, hw_resource_info_,
                                                hw_panel_info_, mixer_attributes_, fb_config_,
                                                &strategy_intf_);
    if (error != kErrorNone) {
      DLOGE("Failed to create strategy");
      return error;
    }

    error = extension_intf_->CreatePartialUpdate(display_type_, hw_resource_info_, hw_panel_info_,
                                                 mixer_attributes_, display_attributes_, fb_config_,
                                                 &partial_update_intf_);
  }

  return kErrorNone;
}

DisplayError Strategy::Deinit() {
  if (strategy_intf_) {
    if (partial_update_intf_) {
      extension_intf_->DestroyPartialUpdate(partial_update_intf_);
    }

    extension_intf_->DestroyStrategyExtn(strategy_intf_);
  }

  return kErrorNone;
}

DisplayError Strategy::Start(HWLayersInfo *hw_layers_info, uint32_t *max_attempts,
                             const PUConstraints &pu_constraints) {
  DisplayError error = kErrorNone;

  hw_layers_info_ = hw_layers_info;
  extn_start_success_ = false;

  if (!disable_gpu_comp_ && !hw_layers_info_->gpu_target_index) {
    DLOGE("GPU composition is enabled and GPU target buffer not provided.");
    return kErrorNotSupported;
  }

  if (partial_update_intf_) {
    partial_update_intf_->Start(pu_constraints);
  }
  GenerateROI();

  if (strategy_intf_) {
    error = strategy_intf_->Start(hw_layers_info_, max_attempts);
    if (error == kErrorNone) {
      extn_start_success_ = true;
      return kErrorNone;
    }
  }

  *max_attempts = 1;

  return kErrorNone;
}

DisplayError Strategy::Stop() {
  if (strategy_intf_) {
    return strategy_intf_->Stop();
  }

  return kErrorNone;
}

DisplayError Strategy::GetNextStrategy(StrategyConstraints *constraints) {
  if (extn_start_success_) {
    return strategy_intf_->GetNextStrategy(constraints);
  }

  // Do not fallback to GPU if GPU comp is disabled.
  if (disable_gpu_comp_) {
    return kErrorNotSupported;
  }

  // Mark all application layers for GPU composition. Find GPU target buffer and store its index for
  // programming the hardware.
  LayerStack *layer_stack = hw_layers_info_->stack;
  for (uint32_t i = 0; i < hw_layers_info_->app_layer_count; i++) {
    layer_stack->layers.at(i)->composition = kCompositionGPU;
    layer_stack->layers.at(i)->request.flags.request_flags = 0;  // Reset layer request
  }

  // When mixer resolution and panel resolutions are same (1600x2560) and FB resolution is
  // 1080x1920 FB_Target destination coordinates(mapped to FB resolution 1080x1920) need to
  // be mapped to destination coordinates of mixer resolution(1600x2560).
  Layer *gpu_target_layer = layer_stack->layers.at(hw_layers_info_->gpu_target_index);
  float layer_mixer_width = FLOAT(mixer_attributes_.width);
  float layer_mixer_height = FLOAT(mixer_attributes_.height);
  float fb_width = FLOAT(fb_config_.x_pixels);
  float fb_height = FLOAT(fb_config_.y_pixels);
  LayerRect src_domain = (LayerRect){0.0f, 0.0f, fb_width, fb_height};
  LayerRect dst_domain = (LayerRect){0.0f, 0.0f, layer_mixer_width, layer_mixer_height};

  Layer layer = *gpu_target_layer;
  hw_layers_info_->index[0] = hw_layers_info_->gpu_target_index;
  MapRect(src_domain, dst_domain, layer.dst_rect, &layer.dst_rect);
  hw_layers_info_->hw_layers.clear();
  hw_layers_info_->hw_layers.push_back(layer);

  return kErrorNone;
}

void Strategy::GenerateROI() {
  bool split_display = false;

  if (partial_update_intf_ && partial_update_intf_->GenerateROI(hw_layers_info_) == kErrorNone) {
    return;
  }

  float layer_mixer_width = mixer_attributes_.width;
  float layer_mixer_height = mixer_attributes_.height;

  if (!hw_resource_info_.is_src_split && display_attributes_.is_device_split) {
    split_display = true;
  }

  hw_layers_info_->left_frame_roi = {};
  hw_layers_info_->right_frame_roi = {};

  if (split_display) {
    float left_split = FLOAT(mixer_attributes_.split_left);
    hw_layers_info_->left_frame_roi.push_back(LayerRect(0.0f, 0.0f,
                                left_split, layer_mixer_height));
    hw_layers_info_->right_frame_roi.push_back(LayerRect(left_split,
                                0.0f, layer_mixer_width, layer_mixer_height));
  } else {
    hw_layers_info_->left_frame_roi.push_back(LayerRect(0.0f, 0.0f,
                                layer_mixer_width, layer_mixer_height));
    hw_layers_info_->right_frame_roi.push_back(LayerRect(0.0f, 0.0f, 0.0f, 0.0f));
  }
}

DisplayError Strategy::Reconfigure(const HWPanelInfo &hw_panel_info,
                         const HWDisplayAttributes &display_attributes,
                         const HWMixerAttributes &mixer_attributes,
                         const DisplayConfigVariableInfo &fb_config) {
  DisplayError error = kErrorNone;

  if (!extension_intf_) {
    return kErrorNone;
  }

  // TODO(user): PU Intf will not be created for video mode panels, hence re-evaluate if
  // reconfigure is needed.
  if (partial_update_intf_) {
    extension_intf_->DestroyPartialUpdate(partial_update_intf_);
    partial_update_intf_ = NULL;
  }

  extension_intf_->CreatePartialUpdate(display_type_, hw_resource_info_, hw_panel_info,
                                       mixer_attributes, display_attributes, fb_config,
                                       &partial_update_intf_);

  error = strategy_intf_->Reconfigure(hw_panel_info, hw_resource_info_, mixer_attributes,
                                      fb_config);
  if (error != kErrorNone) {
    return error;
  }

  hw_panel_info_ = hw_panel_info;
  display_attributes_ = display_attributes;
  mixer_attributes_ = mixer_attributes;
  fb_config_ = fb_config;

  return kErrorNone;
}

DisplayError Strategy::SetCompositionState(LayerComposition composition_type, bool enable) {
  DLOGI("composition type = %d, enable = %d", composition_type, enable);

  if (composition_type == kCompositionGPU) {
    disable_gpu_comp_ = !enable;
  }

  if (strategy_intf_) {
    return strategy_intf_->SetCompositionState(composition_type, enable);
  }

  return kErrorNone;
}

DisplayError Strategy::Purge() {
  if (strategy_intf_) {
    return strategy_intf_->Purge();
  }

  return kErrorNone;
}

DisplayError Strategy::SetIdleTimeoutMs(uint32_t active_ms) {
  if (strategy_intf_) {
    return strategy_intf_->SetIdleTimeoutMs(active_ms);
  }

  return kErrorNotSupported;
}

}  // namespace sdm
