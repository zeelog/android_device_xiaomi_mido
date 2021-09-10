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

#ifndef __STRATEGY_INTERFACE_H__
#define __STRATEGY_INTERFACE_H__

#include <core/sdm_types.h>
#include <core/display_interface.h>
#include "hw_info_types.h"

namespace sdm {

struct StrategyConstraints {
  bool safe_mode = false;   //!< In this mode, strategy manager chooses the composition strategy
                            //!< that requires minimum number of pipe for the current frame. i.e.,
                            //!< video only composition, secure only composition or GPU composition

  bool use_cursor = false;  //!< If this is set, strategy manager will configure cursor layer in the
                            //!< layer stack as hw cursor else it will be treated as a normal layer

  uint32_t max_layers = kMaxSDELayers;  //!< Maximum number of layers that shall be programmed
                                        //!< on hardware for the given layer stack.
};

class StrategyInterface {
 public:
  virtual DisplayError Start(HWLayersInfo *hw_layers_info, uint32_t *max_attempts) = 0;
  virtual DisplayError GetNextStrategy(StrategyConstraints *constraints) = 0;
  virtual DisplayError Stop() = 0;
  virtual DisplayError Reconfigure(const HWPanelInfo &hw_panel_info,
                                   const HWResourceInfo &hw_res_info,
                                   const HWMixerAttributes &mixer_attributes,
                                   const DisplayConfigVariableInfo &fb_config) = 0;
  virtual DisplayError SetCompositionState(LayerComposition composition_type, bool enable) = 0;
  virtual DisplayError Purge() = 0;
  virtual DisplayError SetIdleTimeoutMs(uint32_t active_ms) = 0;

  virtual ~StrategyInterface() { }
};

}  // namespace sdm

#endif  // __STRATEGY_INTERFACE_H__

