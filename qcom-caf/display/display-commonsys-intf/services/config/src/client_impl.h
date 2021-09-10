/*
* Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
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

#ifndef __CLIENT_IMPL_H__
#define __CLIENT_IMPL_H__

#define VALIDATE_CONFIG_SWITCH 1

#include <vendor/display/config/2.0/IDisplayConfig.h>
#include <hidl/HidlSupport.h>
#include <log/log.h>
#include <config/client_interface.h>
#include <string>
#include <vector>

#include "opcode_types.h"

namespace DisplayConfig {

using vendor::display::config::V2_0::IDisplayConfig;
using vendor::display::config::V2_0::IDisplayConfigCallback;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_vec;

typedef hidl_vec<uint8_t> ByteStream;
typedef hidl_vec<hidl_handle> HandleStream;

class ClientCallback: public IDisplayConfigCallback {
 public:
  ClientCallback(ConfigCallback *cb) {
    callback_ = cb;
  }

 private:
  virtual Return<void> perform(uint32_t op_code, const ByteStream &input_params,
                               const HandleStream &input_handles);
  void ParseNotifyCWBBufferDone(const ByteStream &input_params, const HandleStream &input_handles);
  void ParseNotifyQsyncChange(const ByteStream &input_params);
  void ParseNotifyIdleStatus(const ByteStream &input_params);
  ConfigCallback *callback_ = nullptr;
};

class ClientImpl : public ClientInterface {
 public:
  int Init(std::string client_name, ConfigCallback *callback);
  void DeInit();

  virtual int IsDisplayConnected(DisplayType dpy, bool *connected);
  virtual int SetDisplayStatus(DisplayType dpy, ExternalStatus status);
  virtual int ConfigureDynRefreshRate(DynRefreshRateOp op, uint32_t refresh_rate);
  virtual int GetConfigCount(DisplayType dpy, uint32_t *count);
  virtual int GetActiveConfig(DisplayType dpy, uint32_t *config);
  virtual int SetActiveConfig(DisplayType dpy, uint32_t config);
  virtual int GetDisplayAttributes(uint32_t config_index, DisplayType dpy, Attributes *attributes);
  virtual int SetPanelBrightness(uint32_t level);
  virtual int GetPanelBrightness(uint32_t *level);
  virtual int MinHdcpEncryptionLevelChanged(DisplayType dpy, uint32_t min_enc_level);
  virtual int RefreshScreen();
  virtual int ControlPartialUpdate(DisplayType dpy, bool enable);
  virtual int ToggleScreenUpdate(bool on);
  virtual int SetIdleTimeout(uint32_t value);
  virtual int GetHDRCapabilities(DisplayType dpy, HDRCapsParams *caps);
  virtual int SetCameraLaunchStatus(uint32_t on);
  virtual int DisplayBWTransactionPending(bool *status);
  virtual int SetDisplayAnimating(uint64_t display_id, bool animating);
  virtual int ControlIdlePowerCollapse(bool enable, bool synchronous);
  virtual int GetWriteBackCapabilities(bool *is_wb_ubwc_supported);
  virtual int SetDisplayDppsAdROI(uint32_t display_id, uint32_t h_start, uint32_t h_end,
                                  uint32_t v_start, uint32_t v_end, uint32_t factor_in,
                                  uint32_t factor_out);
  virtual int UpdateVSyncSourceOnPowerModeOff();
  virtual int UpdateVSyncSourceOnPowerModeDoze();
  virtual int SetPowerMode(uint32_t disp_id, PowerMode power_mode);
  virtual int IsPowerModeOverrideSupported(uint32_t disp_id, bool *supported);
  virtual int IsHDRSupported(uint32_t disp_id, bool *supported);
  virtual int IsWCGSupported(uint32_t disp_id, bool *supported);
  virtual int SetLayerAsMask(uint32_t disp_id, uint64_t layer_id);
  virtual int GetDebugProperty(const std::string prop_name, std::string *value);
  virtual int GetActiveBuiltinDisplayAttributes(Attributes *attr);
  virtual int SetPanelLuminanceAttributes(uint32_t disp_id, float min_lum, float max_lum);
  virtual int IsBuiltInDisplay(uint32_t disp_id, bool *is_builtin);
  virtual int IsAsyncVDSCreationSupported(bool *supported);
  virtual int CreateVirtualDisplay(uint32_t width, uint32_t height, int format);
  virtual int GetSupportedDSIBitClks(uint32_t disp_id, std::vector<uint64_t> *bit_clks);
  virtual int GetDSIClk(uint32_t disp_id, uint64_t *bit_clk);
  virtual int SetDSIClk(uint32_t disp_id, uint64_t bit_clk);
  virtual int SetCWBOutputBuffer(uint32_t disp_id, const Rect rect, bool post_processed,
                                 const native_handle_t *buffer);
  virtual int SetQsyncMode(uint32_t disp_id, QsyncMode mode);
  virtual int IsSmartPanelConfig(uint32_t disp_id, uint32_t config_id, bool *is_smart);
  virtual int IsRotatorSupportedFormat(int hal_format, bool ubwc, bool *supported);
  virtual int ControlQsyncCallback(bool enable);
  virtual int SendTUIEvent(DisplayType dpy, TUIEventType event_type);
  virtual int GetDisplayHwId(uint32_t disp_id, uint32_t *display_hw_id);
  virtual int GetSupportedDisplayRefreshRates(DisplayType dpy,
                                              std::vector<uint32_t> *supported_refresh_rates);
  virtual int IsRCSupported(uint32_t disp_id, bool *supported);
  virtual int ControlIdleStatusCallback(bool enable);
  virtual int IsSupportedConfigSwitch(uint32_t disp_id, uint32_t config, bool *supported);
  virtual int GetDisplayType(uint64_t physical_disp_id, DisplayType *disp_type);
  virtual int AllowIdleFallback();
  virtual int GetDisplayTileCount(uint64_t physical_disp_id, uint32_t *num_h_tiles,
                                  uint32_t *num_v_tiles);
  virtual int SetPowerModeTiled(uint64_t physical_disp_id, PowerMode power_mode,
                                uint32_t tile_h_loc, uint32_t tile_v_loc);
  virtual int SetPanelBrightnessTiled(uint64_t physical_disp_id, uint32_t level,
                                      uint32_t tile_h_loc, uint32_t tile_v_loc);
  virtual int SetWiderModePreference(uint64_t physical_disp_id, WiderModePref mode_pref);

 private:
  android::sp<IDisplayConfig> display_config_ = nullptr;
  uint64_t client_handle_ = 0;
};

}  // namespace DisplayConfig

#endif  // __CLIENT_IMPL_H__
