/*
* Copyright (c) 2015 - 2017, The Linux Foundation. All rights reserved.
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

#ifndef __RESOURCE_INTERFACE_H__
#define __RESOURCE_INTERFACE_H__

#include <core/display_interface.h>
#include "hw_info_types.h"

namespace sdm {

class ResourceInterface {
 public:
  enum ResourceCmd {
    kCmdResetScalarLUT,
    kCmdMax,
  };

  virtual DisplayError RegisterDisplay(DisplayType type,
                                       const HWDisplayAttributes &display_attributes,
                                       const HWPanelInfo &hw_panel_info,
                                       const HWMixerAttributes &mixer_attributes,
                                       Handle *display_ctx) = 0;
  virtual DisplayError UnregisterDisplay(Handle display_ctx) = 0;
  virtual DisplayError ReconfigureDisplay(Handle display_ctx,
                                          const HWDisplayAttributes &display_attributes,
                                          const HWPanelInfo &hw_panel_info,
                                          const HWMixerAttributes &mixer_attributes) = 0;
  virtual DisplayError Start(Handle display_ctx) = 0;
  virtual DisplayError Stop(Handle display_ctx, HWLayers *hw_layers) = 0;
  virtual DisplayError Prepare(Handle display_ctx, HWLayers *hw_layers) = 0;
  virtual DisplayError PostPrepare(Handle display_ctx, HWLayers *hw_layers) = 0;
  virtual DisplayError Commit(Handle display_ctx, HWLayers *hw_layers) = 0;
  virtual DisplayError PostCommit(Handle display_ctx, HWLayers *hw_layers) = 0;
  virtual void Purge(Handle display_ctx) = 0;
  virtual DisplayError SetMaxMixerStages(Handle display_ctx, uint32_t max_mixer_stages) = 0;
  virtual DisplayError ValidateScaling(const LayerRect &crop, const LayerRect &dst,
                                       bool rotate90, BufferLayout layout,
                                       bool use_rotator_downscale) = 0;
  virtual DisplayError ValidateCursorConfig(Handle display_ctx, const Layer *layer,
                                            bool is_top) = 0;
  virtual DisplayError ValidateAndSetCursorPosition(Handle display_ctx, HWLayers *hw_layers,
                                                    int x, int y,
                                                    DisplayConfigVariableInfo *fb_config) = 0;
  virtual DisplayError SetMaxBandwidthMode(HWBwModes mode) = 0;
  virtual DisplayError GetScaleLutConfig(HWScaleLutInfo *lut_info) = 0;
  virtual DisplayError SetDetailEnhancerData(Handle display_ctx,
                                             const DisplayDetailEnhancerData &de_data) = 0;
  virtual DisplayError Perform(int cmd, ...) = 0;
  virtual ~ResourceInterface() { }
};

}  // namespace sdm

#endif  // __RESOURCE_INTERFACE_H__

