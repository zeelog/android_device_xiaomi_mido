/*
* Copyright (c) 2014 - 2018, 2020, The Linux Foundation. All rights reserved.
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

#ifndef __HW_INTERFACE_H__
#define __HW_INTERFACE_H__

#include <core/buffer_allocator.h>
#include <core/buffer_sync_handler.h>
#include <core/display_interface.h>
#include <private/hw_info_types.h>
#include <private/color_interface.h>
#include <utils/constants.h>

#include "hw_info_interface.h"

namespace sdm {

enum HWScanSupport {
  kScanNotSupported,
  kScanAlwaysOverscanned,
  kScanAlwaysUnderscanned,
  kScanBoth,
};

struct HWScanInfo {
  HWScanSupport pt_scan_support;    // Scan support for preferred timing
  HWScanSupport it_scan_support;    // Scan support for digital monitor or industry timings
  HWScanSupport cea_scan_support;   // Scan support for CEA resolution timings

  HWScanInfo() : pt_scan_support(kScanNotSupported), it_scan_support(kScanNotSupported),
                 cea_scan_support(kScanNotSupported) { }
};

// HWEventHandler - Implemented in DisplayBase and HWInterface implementation
class HWEventHandler {
 public:
  virtual DisplayError VSync(int64_t timestamp) = 0;
  virtual DisplayError Blank(bool blank) = 0;
  virtual void IdleTimeout() = 0;
  virtual void ThermalEvent(int64_t thermal_level) = 0;
  virtual void IdlePowerCollapse() = 0;

 protected:
  virtual ~HWEventHandler() { }
};

class HWInterface {
 public:
  static DisplayError Create(DisplayType type, HWInfoInterface *hw_info_intf,
                             BufferSyncHandler *buffer_sync_handler,
                             BufferAllocator *buffer_allocator, HWInterface **intf);
  static DisplayError Destroy(HWInterface *intf);

  virtual DisplayError Init() = 0;
  virtual DisplayError Deinit() = 0;
  virtual DisplayError GetActiveConfig(uint32_t *active_config) = 0;
  virtual DisplayError SetActiveConfig(uint32_t active_config) = 0;
  virtual DisplayError GetNumDisplayAttributes(uint32_t *count) = 0;
  virtual DisplayError GetDisplayAttributes(uint32_t index,
                                            HWDisplayAttributes *display_attributes) = 0;
  virtual DisplayError GetHWPanelInfo(HWPanelInfo *panel_info) = 0;
  virtual DisplayError SetDisplayAttributes(uint32_t index) = 0;
  virtual DisplayError SetConfigAttributes(uint32_t index, uint32_t w, uint32_t h) = 0;
  virtual DisplayError SetDisplayAttributes(const HWDisplayAttributes &display_attributes) = 0;
  virtual DisplayError SetDisplayFormat(uint32_t index, DisplayInterfaceFormat fmt) = 0;
  virtual DisplayError GetConfigIndex(uint32_t mode, uint32_t *index) = 0;
  virtual DisplayError GetConfigIndex(uint32_t width, uint32_t height, uint32_t *index) = 0;
  virtual DisplayError PowerOn() = 0;
  virtual DisplayError PowerOff() = 0;
  virtual DisplayError Doze() = 0;
  virtual DisplayError DozeSuspend() = 0;
  virtual DisplayError Standby() = 0;
  virtual DisplayError Validate(HWLayers *hw_layers) = 0;
  virtual DisplayError Commit(HWLayers *hw_layers) = 0;
  virtual DisplayError Flush(bool secure) = 0;
  virtual DisplayError GetPPFeaturesVersion(PPFeatureVersion *vers) = 0;
  virtual DisplayError SetPPFeatures(PPFeaturesConfig *feature_list) = 0;
  virtual DisplayError SetVSyncState(bool enable) = 0;
  virtual void SetIdleTimeoutMs(uint32_t timeout_ms) = 0;
  virtual DisplayError SetDisplayMode(const HWDisplayMode hw_display_mode) = 0;
  virtual DisplayError SetRefreshRate(uint32_t refresh_rate) = 0;
  virtual DisplayError SetPanelBrightness(int level) = 0;
  virtual DisplayError GetHWScanInfo(HWScanInfo *scan_info) = 0;
  virtual DisplayError GetVideoFormat(uint32_t config_index, uint32_t *video_format) = 0;
  virtual DisplayError GetMaxCEAFormat(uint32_t *max_cea_format) = 0;
  virtual DisplayError SetCursorPosition(HWLayers *hw_layers, int x, int y) = 0;
  virtual DisplayError OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) = 0;
  virtual DisplayError GetPanelBrightness(int *level) = 0;
  virtual DisplayError SetAutoRefresh(bool enable) = 0;
  virtual DisplayError SetS3DMode(HWS3DMode s3d_mode) = 0;
  virtual DisplayError SetScaleLutConfig(HWScaleLutInfo *lut_info) = 0;
  virtual DisplayError SetMixerAttributes(HWMixerAttributes &mixer_attributes) = 0;
  virtual DisplayError GetMixerAttributes(HWMixerAttributes *mixer_attributes) = 0;
  virtual DisplayError SetDynamicDSIClock(uint64_t bitclk) = 0;
  virtual DisplayError GetDynamicDSIClock(uint64_t *bitclk) = 0;
  virtual DisplayError GetHdmiMode(std::vector<uint32_t> &hdmi_modes) = 0;

 protected:
  virtual ~HWInterface() { }
};

}  // namespace sdm

#endif  // __HW_INTERFACE_H__

