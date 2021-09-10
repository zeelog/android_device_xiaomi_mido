/*
* Copyright (c) 2017 - 2018, The Linux Foundation. All rights reserved.
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

#ifndef __DISPLAY_NULL_H__
#define __DISPLAY_NULL_H__

#include <core/display_interface.h>
#include <string>
#include <vector>

namespace sdm {

#define MAKE_NO_OP(virtual_method_name) \
      virtual DisplayError virtual_method_name { return kErrorNone; }

class DisplayNull : public DisplayInterface {
 public:
  virtual ~DisplayNull() { }
  virtual DisplayError Commit(LayerStack *layer_stack);
  virtual DisplayError GetDisplayState(DisplayState *state);
  virtual DisplayError SetDisplayState(DisplayState state);
  virtual DisplayError SetFrameBufferConfig(const DisplayConfigVariableInfo &variable_info);
  virtual DisplayError GetFrameBufferConfig(DisplayConfigVariableInfo *variable_info);
  virtual bool IsUnderscanSupported() { return true; }
  virtual void SetIdleTimeoutMs(uint32_t active_ms) { }
  virtual bool IsPrimaryDisplay() { return true; }

  void SetActive(bool active) {
    active_ = active;
  }

  bool IsActive() {
    return active_;
  }

  MAKE_NO_OP(Prepare(LayerStack *))
  MAKE_NO_OP(Flush(bool secure))
  MAKE_NO_OP(GetNumVariableInfoConfigs(uint32_t *))
  MAKE_NO_OP(GetConfig(uint32_t, DisplayConfigVariableInfo *))
  MAKE_NO_OP(GetConfig(DisplayConfigFixedInfo *))
  MAKE_NO_OP(GetActiveConfig(uint32_t *))
  MAKE_NO_OP(GetVSyncState(bool *))
  MAKE_NO_OP(SetActiveConfig(uint32_t))
  MAKE_NO_OP(SetActiveConfig(DisplayConfigVariableInfo *))
  MAKE_NO_OP(SetMaxMixerStages(uint32_t))
  MAKE_NO_OP(ControlPartialUpdate(bool, uint32_t *))
  MAKE_NO_OP(DisablePartialUpdateOneFrame())
  MAKE_NO_OP(SetDisplayMode(uint32_t))
  MAKE_NO_OP(SetPanelBrightness(int))
  MAKE_NO_OP(CachePanelBrightness(int))
  MAKE_NO_OP(OnMinHdcpEncryptionLevelChange(uint32_t))
  MAKE_NO_OP(ColorSVCRequestRoute(const PPDisplayAPIPayload &, PPDisplayAPIPayload *,
                                  PPPendingParams *))
  MAKE_NO_OP(GetColorModeCount(uint32_t *))
  MAKE_NO_OP(GetColorModes(uint32_t *, std::vector<std::string> *))
  MAKE_NO_OP(GetColorModeAttr(const std::string &, AttrVal *))
  MAKE_NO_OP(SetColorMode(const std::string &))
  MAKE_NO_OP(SetColorModeById(int32_t))
  MAKE_NO_OP(SetColorTransform(const uint32_t, const double *))
  MAKE_NO_OP(GetDefaultColorMode(std::string *))
  MAKE_NO_OP(ApplyDefaultDisplayMode())
  MAKE_NO_OP(SetCursorPosition(int, int))
  MAKE_NO_OP(GetRefreshRateRange(uint32_t *, uint32_t *))
  MAKE_NO_OP(SetRefreshRate(uint32_t))
  MAKE_NO_OP(GetPanelBrightness(int *))
  MAKE_NO_OP(SetVSyncState(bool))
  MAKE_NO_OP(SetMixerResolution(uint32_t, uint32_t))
  MAKE_NO_OP(GetMixerResolution(uint32_t *, uint32_t *))
  MAKE_NO_OP(SetDetailEnhancerData(const DisplayDetailEnhancerData &))
  MAKE_NO_OP(GetDisplayPort(DisplayPort *))
  MAKE_NO_OP(SetCompositionState(LayerComposition, bool))
  MAKE_NO_OP(GetClientTargetSupport(uint32_t, uint32_t, LayerBufferFormat,
                                    const ColorMetaData &))
  std::string Dump() { return ""; }
  MAKE_NO_OP(SetDynamicDSIClock(uint64_t bitclk))
  MAKE_NO_OP(GetDynamicDSIClock(uint64_t *bitclk))
  MAKE_NO_OP(GetSupportedDSIClock(std::vector<uint64_t> *bitclk_rates))

 private:
  bool active_ = false;
  DisplayState state_ = kStateOff;
  DisplayConfigVariableInfo fb_config_ = {};
};

}  // namespace sdm

#endif  // __DISPLAY_NULL_H__
