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
#include <core/buffer_allocator.h>

#include "comp_manager.h"
#include "strategy.h"

#define __CLASS__ "CompManager"

namespace sdm {

static bool NeedsScaledComposition(const DisplayConfigVariableInfo &fb_config,
                                   const HWMixerAttributes &mixer_attributes) {
  return ((fb_config.x_pixels != mixer_attributes.width) ||
          (fb_config.y_pixels != mixer_attributes.height));
}

DisplayError CompManager::Init(const HWResourceInfo &hw_res_info,
                               ExtensionInterface *extension_intf,
                               BufferAllocator *buffer_allocator,
                               BufferSyncHandler *buffer_sync_handler,
                               SocketHandler *socket_handler) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  if (extension_intf) {
    error = extension_intf->CreateResourceExtn(hw_res_info, buffer_allocator, buffer_sync_handler,
                                               &resource_intf_);
    extension_intf->CreateDppsControlExtn(&dpps_ctrl_intf_, socket_handler);
  } else {
    error = ResourceDefault::CreateResourceDefault(hw_res_info, &resource_intf_);
  }

  if (error != kErrorNone) {
    if (extension_intf) {
      extension_intf->DestroyDppsControlExtn(dpps_ctrl_intf_);
    }
    return error;
  }

  hw_res_info_ = hw_res_info;
  buffer_allocator_ = buffer_allocator;
  extension_intf_ = extension_intf;

  return error;
}

DisplayError CompManager::Deinit() {
  SCOPE_LOCK(locker_);

  if (extension_intf_) {
    extension_intf_->DestroyResourceExtn(resource_intf_);
    extension_intf_->DestroyDppsControlExtn(dpps_ctrl_intf_);
  } else {
    ResourceDefault::DestroyResourceDefault(resource_intf_);
  }

  return kErrorNone;
}

DisplayError CompManager::RegisterDisplay(DisplayType type,
                                          const HWDisplayAttributes &display_attributes,
                                          const HWPanelInfo &hw_panel_info,
                                          const HWMixerAttributes &mixer_attributes,
                                          const DisplayConfigVariableInfo &fb_config,
                                          Handle *display_ctx) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;

  DisplayCompositionContext *display_comp_ctx = new DisplayCompositionContext();
  if (!display_comp_ctx) {
    return kErrorMemory;
  }

  Strategy *&strategy = display_comp_ctx->strategy;
  strategy = new Strategy(extension_intf_, buffer_allocator_, type, hw_res_info_, hw_panel_info,
                          mixer_attributes, display_attributes, fb_config);
  if (!strategy) {
    DLOGE("Unable to create strategy");
    delete display_comp_ctx;
    return kErrorMemory;
  }

  error = strategy->Init();
  if (error != kErrorNone) {
    delete strategy;
    delete display_comp_ctx;
    return error;
  }

  error = resource_intf_->RegisterDisplay(type, display_attributes, hw_panel_info, mixer_attributes,
                                          &display_comp_ctx->display_resource_ctx);
  if (error != kErrorNone) {
    strategy->Deinit();
    delete strategy;
    delete display_comp_ctx;
    display_comp_ctx = NULL;
    return error;
  }

  registered_displays_[type] = 1;
  display_comp_ctx->is_primary_panel = hw_panel_info.is_primary_panel;
  display_comp_ctx->display_type = type;
  display_comp_ctx->fb_config = fb_config;
  *display_ctx = display_comp_ctx;
  // New non-primary display device has been added, so move the composition mode to safe mode until
  // resources for the added display is configured properly.
  if (!display_comp_ctx->is_primary_panel) {
    safe_mode_ = true;
    max_sde_ext_layers_ = UINT32(Debug::GetExtMaxlayers());
  }

  display_comp_ctx->scaled_composition = NeedsScaledComposition(fb_config, mixer_attributes);
  DLOGV_IF(kTagCompManager, "registered display bit mask 0x%x, configured display bit mask 0x%x, " \
           "display type %d", registered_displays_.to_ulong(), configured_displays_.to_ulong(),
           display_comp_ctx->display_type);

  return kErrorNone;
}

DisplayError CompManager::UnregisterDisplay(Handle display_ctx) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  if (!display_comp_ctx) {
    return kErrorParameters;
  }

  resource_intf_->UnregisterDisplay(display_comp_ctx->display_resource_ctx);

  Strategy *&strategy = display_comp_ctx->strategy;
  strategy->Deinit();
  delete strategy;

  registered_displays_[display_comp_ctx->display_type] = 0;
  configured_displays_[display_comp_ctx->display_type] = 0;

  if (display_comp_ctx->display_type == kHDMI) {
    max_layers_ = kMaxSDELayers;
  }

  DLOGV_IF(kTagCompManager, "registered display bit mask 0x%x, configured display bit mask 0x%x, " \
           "display type %d", registered_displays_.to_ulong(), configured_displays_.to_ulong(),
           display_comp_ctx->display_type);

  delete display_comp_ctx;
  display_comp_ctx = NULL;
  return kErrorNone;
}

DisplayError CompManager::ReconfigureDisplay(Handle comp_handle,
                                             const HWDisplayAttributes &display_attributes,
                                             const HWPanelInfo &hw_panel_info,
                                             const HWMixerAttributes &mixer_attributes,
                                             const DisplayConfigVariableInfo &fb_config) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(comp_handle);

  error = resource_intf_->ReconfigureDisplay(display_comp_ctx->display_resource_ctx,
                                             display_attributes, hw_panel_info, mixer_attributes);
  if (error != kErrorNone) {
    return error;
  }

  if (display_comp_ctx->strategy) {
    error = display_comp_ctx->strategy->Reconfigure(hw_panel_info, display_attributes,
                                                    mixer_attributes, fb_config);
    if (error != kErrorNone) {
      DLOGE("Unable to Reconfigure strategy.");
      display_comp_ctx->strategy->Deinit();
      delete display_comp_ctx->strategy;
      display_comp_ctx->strategy = NULL;
      return error;
    }
  }

  // For HDMI S3D mode, set max_layers_ to 0 so that primary display would fall back
  // to GPU composition to release pipes for HDMI.
  if (display_comp_ctx->display_type == kHDMI) {
    if (hw_panel_info.s3d_mode != kS3DModeNone) {
      max_layers_ = 0;
    } else {
      max_layers_ = kMaxSDELayers;
    }
  }

  display_comp_ctx->scaled_composition = NeedsScaledComposition(fb_config, mixer_attributes);
  // Update new resolution.
  display_comp_ctx->fb_config = fb_config;

  return error;
}

void CompManager::PrepareStrategyConstraints(Handle comp_handle, HWLayers *hw_layers) {
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(comp_handle);
  StrategyConstraints *constraints = &display_comp_ctx->constraints;
  bool low_end_hw = ((hw_res_info_.num_vig_pipe + hw_res_info_.num_rgb_pipe +
                    hw_res_info_.num_dma_pipe) <= kSafeModeThreshold);

  constraints->safe_mode = safe_mode_;
  constraints->use_cursor = false;
  constraints->max_layers = max_layers_;

  // Limit 2 layer SDE Comp if its not a Primary Display.
  // Safe mode is the policy for External display on a low end device.
  if (!display_comp_ctx->is_primary_panel) {
    bool secure_ex_layer = false;
    constraints->max_layers = max_sde_ext_layers_;
    constraints->safe_mode = (low_end_hw && !hw_res_info_.separate_rotator) ? true : safe_mode_;
    if (hw_layers->info.stack->flags.secure_present) {
        secure_ex_layer = true;
    }
    else {
        secure_ex_layer = false;
    }
    if (secure_external_layer_ != secure_ex_layer) {
      secure_external_layer_ = secure_ex_layer;
      secure_external_transition_ = true;
    }
  }

  // When Secure layer is present on external, GPU composition should be policy
  // for Primary on low end devices. // Safe mode needs to be kicked in primary
  // during secure transition on external
  if(display_comp_ctx->is_primary_panel && (registered_displays_.count() > 1)
          && secure_external_layer_) {
    if (low_end_hw) {
      DLOGV_IF(kTagCompManager,"Secure layer present for LET. Fallingback to GPU");
      hw_layers->info.stack->flags.skip_present = 1;
      for(auto &layer : hw_layers->info.stack->layers) {
        if(layer->composition != kCompositionGPUTarget) {
          layer->flags.skip = 1;
        }
      }
    } else if (secure_external_transition_) {
      constraints->safe_mode = true;
      secure_external_transition_ = false;
    }
  }

  // If a strategy fails after successfully allocating resources, then set safe mode
  if (display_comp_ctx->remaining_strategies != display_comp_ctx->max_strategies) {
    constraints->safe_mode = true;
  }

  // Set use_cursor constraint to Strategy
  constraints->use_cursor = display_comp_ctx->valid_cursor;

  if (display_comp_ctx->idle_fallback || display_comp_ctx->thermal_fallback_) {
    // Handle the idle timeout by falling back
    constraints->safe_mode = true;
  }
}

void CompManager::PrePrepare(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  display_comp_ctx->valid_cursor = SupportLayerAsCursor(display_comp_ctx, hw_layers);

  // pu constraints
  display_comp_ctx->pu_constraints.enable_cursor_pu = display_comp_ctx->valid_cursor;

  display_comp_ctx->strategy->Start(&hw_layers->info, &display_comp_ctx->max_strategies,
                                    display_comp_ctx->pu_constraints);
  display_comp_ctx->remaining_strategies = display_comp_ctx->max_strategies;
}

DisplayError CompManager::Prepare(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  Handle &display_resource_ctx = display_comp_ctx->display_resource_ctx;

  DisplayError error = kErrorUndefined;

  PrepareStrategyConstraints(display_ctx, hw_layers);

  // Select a composition strategy, and try to allocate resources for it.
  resource_intf_->Start(display_resource_ctx);

  bool exit = false;
  uint32_t &count = display_comp_ctx->remaining_strategies;
  for (; !exit && count > 0; count--) {
    error = display_comp_ctx->strategy->GetNextStrategy(&display_comp_ctx->constraints);
    if (error != kErrorNone) {
      // Composition strategies exhausted. Resource Manager could not allocate resources even for
      // GPU composition. This will never happen.
      exit = true;
    }

    if (!exit) {
      error = resource_intf_->Prepare(display_resource_ctx, hw_layers);
      // Exit if successfully prepared resource, else try next strategy.
      exit = (error == kErrorNone);
    }
  }

  if (error != kErrorNone) {
    resource_intf_->Stop(display_resource_ctx, hw_layers);
    DLOGE("Composition strategies exhausted for display = %d", display_comp_ctx->display_type);
    return error;
  }

  error = resource_intf_->Stop(display_resource_ctx, hw_layers);

  return error;
}

DisplayError CompManager::PostPrepare(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  Handle &display_resource_ctx = display_comp_ctx->display_resource_ctx;

  DisplayError error = kErrorNone;
  error = resource_intf_->PostPrepare(display_resource_ctx, hw_layers);
  if (error != kErrorNone) {
    return error;
  }

  display_comp_ctx->strategy->Stop();

  return kErrorNone;
}

DisplayError CompManager::Commit(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  return resource_intf_->Commit(display_comp_ctx->display_resource_ctx, hw_layers);
}

DisplayError CompManager::ReConfigure(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  Handle &display_resource_ctx = display_comp_ctx->display_resource_ctx;

  DisplayError error = kErrorUndefined;
  resource_intf_->Start(display_resource_ctx);
  error = resource_intf_->Prepare(display_resource_ctx, hw_layers);

  if (error != kErrorNone) {
    DLOGE("Reconfigure failed for display = %d", display_comp_ctx->display_type);
  }

  resource_intf_->Stop(display_resource_ctx, hw_layers);
  if (error != kErrorNone) {
      error = resource_intf_->PostPrepare(display_resource_ctx, hw_layers);
  }

  return error;
}

DisplayError CompManager::PostCommit(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  configured_displays_[display_comp_ctx->display_type] = 1;
  if (configured_displays_ == registered_displays_) {
    safe_mode_ = false;
  }

  error = resource_intf_->PostCommit(display_comp_ctx->display_resource_ctx, hw_layers);
  if (error != kErrorNone) {
    return error;
  }

  display_comp_ctx->idle_fallback = false;

  DLOGV_IF(kTagCompManager, "registered display bit mask 0x%x, configured display bit mask 0x%x, " \
           "display type %d", registered_displays_, configured_displays_,
           display_comp_ctx->display_type);

  return kErrorNone;
}

void CompManager::Purge(Handle display_ctx) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  resource_intf_->Purge(display_comp_ctx->display_resource_ctx);

  display_comp_ctx->strategy->Purge();
}

DisplayError CompManager::SetIdleTimeoutMs(Handle display_ctx, uint32_t active_ms) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  return display_comp_ctx->strategy->SetIdleTimeoutMs(active_ms);
}

void CompManager::ProcessIdleTimeout(Handle display_ctx) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  if (!display_comp_ctx) {
    return;
  }

  display_comp_ctx->idle_fallback = true;
}

void CompManager::ProcessThermalEvent(Handle display_ctx, int64_t thermal_level) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
          reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  if (thermal_level >= kMaxThermalLevel) {
    display_comp_ctx->thermal_fallback_ = true;
  } else {
    display_comp_ctx->thermal_fallback_ = false;
  }
}

void CompManager::ProcessIdlePowerCollapse(Handle display_ctx) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
          reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  if (display_comp_ctx) {
    resource_intf_->Perform(ResourceInterface::kCmdResetScalarLUT,
                            display_comp_ctx->display_resource_ctx);
  }
}

DisplayError CompManager::SetMaxMixerStages(Handle display_ctx, uint32_t max_mixer_stages) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  if (display_comp_ctx) {
    error = resource_intf_->SetMaxMixerStages(display_comp_ctx->display_resource_ctx,
                                              max_mixer_stages);
  }

  return error;
}

void CompManager::ControlPartialUpdate(Handle display_ctx, bool enable) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  display_comp_ctx->pu_constraints.enable = enable;
}

DisplayError CompManager::ValidateScaling(const LayerRect &crop, const LayerRect &dst,
                                          bool rotate90) {
  BufferLayout layout = Debug::IsUbwcTiledFrameBuffer() ? kUBWC : kLinear;
  return resource_intf_->ValidateScaling(crop, dst, rotate90, layout, true);
}

DisplayError CompManager::ValidateAndSetCursorPosition(Handle display_ctx, HWLayers *hw_layers,
                                                 int x, int y) {
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);
  Handle &display_resource_ctx = display_comp_ctx->display_resource_ctx;
  return resource_intf_->ValidateAndSetCursorPosition(display_resource_ctx, hw_layers, x, y,
                                                      &display_comp_ctx->fb_config);
}

bool CompManager::SupportLayerAsCursor(Handle comp_handle, HWLayers *hw_layers) {
  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(comp_handle);
  Handle &display_resource_ctx = display_comp_ctx->display_resource_ctx;
  LayerStack *layer_stack = hw_layers->info.stack;
  bool supported = false;
  int32_t gpu_index = -1;

  // HW Cursor cannot be used, if Display configuration needs scaled composition.
  if (display_comp_ctx->scaled_composition || !layer_stack->flags.cursor_present) {
    return supported;
  }

  for (int32_t i = INT32(layer_stack->layers.size() - 1); i >= 0; i--) {
    Layer *layer = layer_stack->layers.at(UINT32(i));
    if (layer->composition == kCompositionGPUTarget) {
      gpu_index = i;
      break;
    }
  }
  if (gpu_index <= 0) {
    return supported;
  }
  Layer *cursor_layer = layer_stack->layers.at(UINT32(gpu_index) - 1);
  if (cursor_layer->flags.cursor && !cursor_layer->flags.skip &&
      resource_intf_->ValidateCursorConfig(display_resource_ctx,
                                           cursor_layer, true) == kErrorNone) {
    supported = true;
  }

  return supported;
}

DisplayError CompManager::SetMaxBandwidthMode(HWBwModes mode) {
  if ((hw_res_info_.has_dyn_bw_support == false) || (mode >= kBwModeMax)) {
    return kErrorNotSupported;
  }

  return resource_intf_->SetMaxBandwidthMode(mode);
}

DisplayError CompManager::GetScaleLutConfig(HWScaleLutInfo *lut_info) {
  return resource_intf_->GetScaleLutConfig(lut_info);
}

DisplayError CompManager::SetDetailEnhancerData(Handle display_ctx,
                                                const DisplayDetailEnhancerData &de_data) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  return resource_intf_->SetDetailEnhancerData(display_comp_ctx->display_resource_ctx, de_data);
}

DisplayError CompManager::SetCompositionState(Handle display_ctx,
                                              LayerComposition composition_type, bool enable) {
  SCOPE_LOCK(locker_);

  DisplayCompositionContext *display_comp_ctx =
                             reinterpret_cast<DisplayCompositionContext *>(display_ctx);

  return display_comp_ctx->strategy->SetCompositionState(composition_type, enable);
}

DisplayError CompManager::ControlDpps(bool enable) {
  if (dpps_ctrl_intf_) {
    return enable ? dpps_ctrl_intf_->On() : dpps_ctrl_intf_->Off();
  }

  return kErrorNone;
}

bool CompManager::SetDisplayState(Handle display_ctx,
                                  DisplayState state, DisplayType display_type) {
  display_state_[display_type] = state;

  switch (state) {
  case kStateOff:
    Purge(display_ctx);
    configured_displays_.reset(display_type);
    DLOGV_IF(kTagCompManager, "configured_displays_ = 0x%x", configured_displays_);
    break;

  case kStateOn:
    if (registered_displays_.count() > 1) {
      safe_mode_ = true;
      DLOGV_IF(kTagCompManager, "safe_mode = %d", safe_mode_);
    }
    break;

  default:
    break;
  }

  return true;
}

}  // namespace sdm
