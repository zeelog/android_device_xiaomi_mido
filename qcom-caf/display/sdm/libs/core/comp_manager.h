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

#ifndef __COMP_MANAGER_H__
#define __COMP_MANAGER_H__

#include <core/display_interface.h>
#include <private/extension_interface.h>
#include <utils/locker.h>
#include <bitset>

#include "strategy.h"
#include "resource_default.h"
#include "hw_interface.h"

namespace sdm {

class CompManager {
 public:
  DisplayError Init(const HWResourceInfo &hw_res_info_, ExtensionInterface *extension_intf,
                    BufferAllocator *buffer_allocator, BufferSyncHandler *buffer_sync_handler,
                    SocketHandler *socket_handler);
  DisplayError Deinit();
  DisplayError RegisterDisplay(DisplayType type, const HWDisplayAttributes &display_attributes,
                               const HWPanelInfo &hw_panel_info,
                               const HWMixerAttributes &mixer_attributes,
                               const DisplayConfigVariableInfo &fb_config, Handle *display_ctx);
  DisplayError UnregisterDisplay(Handle display_ctx);
  DisplayError ReconfigureDisplay(Handle display_ctx, const HWDisplayAttributes &display_attributes,
                                  const HWPanelInfo &hw_panel_info,
                                  const HWMixerAttributes &mixer_attributes,
                                  const DisplayConfigVariableInfo &fb_config);
  void PrePrepare(Handle display_ctx, HWLayers *hw_layers);
  DisplayError Prepare(Handle display_ctx, HWLayers *hw_layers);
  DisplayError Commit(Handle display_ctx, HWLayers *hw_layers);
  DisplayError PostPrepare(Handle display_ctx, HWLayers *hw_layers);
  DisplayError ReConfigure(Handle display_ctx, HWLayers *hw_layers);
  DisplayError PostCommit(Handle display_ctx, HWLayers *hw_layers);
  void Purge(Handle display_ctx);
  DisplayError SetIdleTimeoutMs(Handle display_ctx, uint32_t active_ms);
  void ProcessIdleTimeout(Handle display_ctx);
  void ProcessThermalEvent(Handle display_ctx, int64_t thermal_level);
  void ProcessIdlePowerCollapse(Handle display_ctx);
  DisplayError SetMaxMixerStages(Handle display_ctx, uint32_t max_mixer_stages);
  void ControlPartialUpdate(Handle display_ctx, bool enable);
  DisplayError ValidateScaling(const LayerRect &crop, const LayerRect &dst, bool rotate90);
  DisplayError ValidateCursorPosition(Handle display_ctx, HWLayers *hw_layers, int x, int y);
  bool SupportLayerAsCursor(Handle display_ctx, HWLayers *hw_layers);
  DisplayError ValidateAndSetCursorPosition(Handle display_ctx, HWLayers *hw_layers, int x, int y);
  bool SetDisplayState(Handle display_ctx, DisplayState state, DisplayType display_type);
  DisplayError SetMaxBandwidthMode(HWBwModes mode);
  DisplayError GetScaleLutConfig(HWScaleLutInfo *lut_info);
  DisplayError SetDetailEnhancerData(Handle display_ctx, const DisplayDetailEnhancerData &de_data);
  DisplayError SetCompositionState(Handle display_ctx, LayerComposition composition_type,
                                   bool enable);
  DisplayError ControlDpps(bool enable);

 private:
  static const int kMaxThermalLevel = 3;
  static const int kSafeModeThreshold = 4;

  void PrepareStrategyConstraints(Handle display_ctx, HWLayers *hw_layers);

  struct DisplayCompositionContext {
    Strategy *strategy = NULL;
    StrategyConstraints constraints;
    Handle display_resource_ctx = NULL;
    DisplayType display_type = kPrimary;
    uint32_t max_strategies = 0;
    uint32_t remaining_strategies = 0;
    bool idle_fallback = false;
    bool thermal_fallback_ = false;
    // Using primary panel flag of hw panel to configure Constraints. We do not need other hw
    // panel parameters for now.
    bool is_primary_panel = false;
    bool valid_cursor = false;
    PUConstraints pu_constraints = {};
    bool scaled_composition = false;
    DisplayConfigVariableInfo fb_config = {};
  };

  Locker locker_;
  ResourceInterface *resource_intf_ = NULL;
  std::bitset<kDisplayMax> registered_displays_;  // Bit mask of registered displays
  std::bitset<kDisplayMax> configured_displays_;  // Bit mask of sucessfully configured displays
  uint32_t display_state_[kDisplayMax] = {};
  bool safe_mode_ = false;              // Flag to notify all displays to be in resource crunch
                                        // mode, where strategy manager chooses the best strategy
                                        // that uses optimal number of pipes for each display
  bool secure_external_layer_ = false;
  bool secure_external_transition_ = false;
  HWResourceInfo hw_res_info_;
  BufferAllocator *buffer_allocator_ = NULL;
  ExtensionInterface *extension_intf_ = NULL;
  uint32_t max_layers_ = kMaxSDELayers;
  uint32_t max_sde_ext_layers_ = 0;
  DppsControlInterface *dpps_ctrl_intf_ = NULL;
};

}  // namespace sdm

#endif  // __COMP_MANAGER_H__

