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

#ifndef __CONFIG_DEFS_H__
#define __CONFIG_DEFS_H__

#include <vector>
#include <string>

#include <errno.h>
#include <cutils/native_handle.h>

// #defines specifying the API level supported
// Client can use these API level #ifdefs in their implementation to call the
// corresponding DisplayConfig API independent of the underlying DisplayConfig
// implementation being present. When this ifdef gets enabled in this header, the
// client code will automatically get compiled.
#define DISPLAY_CONFIG_API_LEVEL_0
#define DISPLAY_CONFIG_API_LEVEL_1
#define DISPLAY_CONFIG_API_LEVEL_2

#define DISPLAY_CONFIG_CAMERA_SMOOTH_APIs_1_0
#define DISPLAY_CONFIG_TILE_DISPLAY_APIS_1_0

namespace DisplayConfig {

// enum definitions
enum class DisplayType : int {
  kInvalid,
  kPrimary,
  kExternal,
  kVirtual,
  kBuiltIn2,
};

enum class ExternalStatus : int {
  kInvalid,
  kOffline,
  kOnline,
  kPause,
  kResume,
};

enum class DynRefreshRateOp : int {
  kInvalid,
  kDisableMetadata,
  kEnableMetadata,
  kSetBinder,
};

enum class DisplayPortType : int {
  kInvalid,
  kDefault,
  kDsi,
  kDtv,
  kWriteback,
  kLvds,
  kEdp,
  kDp,
};

enum class PowerMode : int {
  kOff,
  kDoze,
  kOn,
  kDozeSuspend,
};

enum class QsyncMode : int {
  kNone,
  kWaitForFencesOneFrame,
  kWaitForFencesEachFrame,
  kWaitForCommitEachFrame,
};

enum class TUIEventType : int {
  kNone,
  kPrepareTUITransition,
  kStartTUITransition,
  kEndTUITransition,
};

enum class CameraSmoothOp : int {
  kOff,
  kOn,
};

// Input and Output Params structures
struct Attributes {
  uint32_t vsync_period = 0;
  uint32_t x_res = 0;
  uint32_t y_res = 0;
  float x_dpi = 0;
  float y_dpi = 0;
  DisplayPortType panel_type = DisplayPortType::kDefault;
  bool is_yuv = 0;
};

struct HDRCapsParams {
  std::vector<int> supported_hdr_types = {};
  float max_luminance = 0;
  float max_avg_luminance = 0;
  float min_luminance = 0;
};

struct StatusParams {
  DisplayType dpy = DisplayType::kInvalid;
  ExternalStatus status = ExternalStatus::kInvalid;
};

struct DynRefreshRateParams {
  DynRefreshRateOp op = DynRefreshRateOp::kInvalid;
  uint32_t refresh_rate = 0;
};

struct ConfigParams {
  DisplayType dpy = DisplayType::kInvalid;
  uint32_t config = 0;
};

struct AttributesParams {
  uint32_t config_index = 0;
  DisplayType dpy = DisplayType::kInvalid;
};

struct MinHdcpEncLevelChangedParams {
  DisplayType dpy = DisplayType::kInvalid;
  uint32_t min_enc_level = 0;
};

struct PartialUpdateParams {
  DisplayType dpy = DisplayType::kInvalid;
  bool enable = 0;
};

struct AnimationParams {
  uint64_t display_id = 0;
  bool animating = 0;
};

struct IdlePcParams {
  bool enable = 0;
  bool synchronous = 0;
};

struct DppsAdRoiParams {
  uint32_t display_id = 0;
  uint32_t h_start = 0;
  uint32_t h_end = 0;
  uint32_t v_start = 0;
  uint32_t v_end = 0;
  uint32_t factor_in = 0;
  uint32_t factor_out = 0;
};

struct PowerModeParams {
  uint32_t disp_id = 0;
  PowerMode power_mode = PowerMode::kOff;
};

struct LayerMaskParams {
  uint32_t disp_id = 0;
  uint64_t layer_id = 0;
};

struct PanelLumAttrParams {
  uint32_t disp_id = 0;
  float min_lum = 0;
  float max_lum = 0;
};

struct Rect {
  uint32_t left = 0;
  uint32_t top = 0;
  uint32_t right = 0;
  uint32_t bottom = 0;
};

struct CwbBufferParams {
  uint32_t disp_id = 0;
  Rect rect;
  bool post_processed = 0;
};

struct DsiClkParams {
  uint32_t disp_id = 0;
  uint64_t bit_clk = 0;
};

struct QsyncModeParams {
  uint32_t disp_id = 0;
  QsyncMode mode = QsyncMode::kNone;
};

struct SmartPanelCfgParams {
  uint32_t disp_id = 0;
  uint32_t config_id = 0;
};

struct VdsParams {
  uint32_t width = 0;
  uint32_t height = 0;
  int format = 0;
};

struct RotatorFormatParams {
  int hal_format = 0;
  bool ubwc = 0;
};

struct QsyncCallbackParams {
  bool qsync_enabled = 0;
  int refresh_rate = 0;
  int qsync_refresh_rate = 0;
};

struct TUIEventParams {
  DisplayType dpy = DisplayType::kInvalid;
  TUIEventType tui_event_type = TUIEventType::kNone;
};

struct SupportedModesParams {
  uint32_t disp_id = 0;
  uint32_t mode = 0;
};

struct PowerModeTiledParams {
  uint64_t physical_disp_id = 0;
  PowerMode power_mode = PowerMode::kOn;
  uint32_t tile_h_loc = 0;
  uint32_t tile_v_loc = 0;
};

struct PanelBrightnessTiledParams {
  uint64_t physical_disp_id = 0;
  uint32_t level = 255;
  uint32_t tile_h_loc = 0;
  uint32_t tile_v_loc = 0;
};

enum class WiderModePref : int {
  kNoPreference,
  kWiderAsyncMode,
  kWiderSyncMode
};

struct WiderModePrefParams {
  uint64_t physical_disp_id = 0;
  WiderModePref mode_pref = WiderModePref::kNoPreference;
};

struct CameraSmoothInfo {
  CameraSmoothOp op = CameraSmoothOp::kOff;
  uint32_t fps = 0;
};

/* Callback Interface */
class ConfigCallback {
 public:
  virtual void NotifyCWBBufferDone(int /* error */, const native_handle_t* /* buffer */ ) { }
  virtual void NotifyQsyncChange(bool /* qsync_enabled */ , int /* refresh_rate */,
                                 int /* qsync_refresh_rate */) { }
  virtual void NotifyIdleStatus(bool /* is_idle */) { }
  virtual void NotifyCameraSmoothInfo(CameraSmoothOp /* op */, uint32_t /* fps */) { }

 protected:
  virtual ~ConfigCallback() { }
};

#define DEFAULT_RET { return -EINVAL; }

/* Config Interface */
class ConfigInterface {
 public:
  virtual int IsDisplayConnected(DisplayType /* dpy */, bool* /* connected */) DEFAULT_RET
  virtual int SetDisplayStatus(DisplayType /* dpy */, ExternalStatus /* status */) DEFAULT_RET
  virtual int ConfigureDynRefreshRate(DynRefreshRateOp /* op */,
                                      uint32_t /* refresh_rate */) DEFAULT_RET
  virtual int GetConfigCount(DisplayType /* dpy */, uint32_t* /* count */) DEFAULT_RET
  virtual int GetActiveConfig(DisplayType /* dpy */, uint32_t* /* config */) DEFAULT_RET
  virtual int SetActiveConfig(DisplayType /* dpy */, uint32_t /* config */) DEFAULT_RET
  virtual int GetDisplayAttributes(uint32_t /* config_index */, DisplayType /* dpy */,
                                   Attributes* /* attributes */) DEFAULT_RET
  virtual int SetPanelBrightness(uint32_t /* level */) DEFAULT_RET
  virtual int GetPanelBrightness(uint32_t* /* level */) DEFAULT_RET
  virtual int MinHdcpEncryptionLevelChanged(DisplayType /* dpy */,
                                            uint32_t /* min_enc_level */) DEFAULT_RET
  virtual int RefreshScreen() DEFAULT_RET
  virtual int ControlPartialUpdate(DisplayType /* dpy */, bool /* enable */) DEFAULT_RET
  virtual int ToggleScreenUpdate(bool /* on */) DEFAULT_RET
  virtual int SetIdleTimeout(uint32_t /* value */) DEFAULT_RET
  virtual int GetHDRCapabilities(DisplayType /* dpy */, HDRCapsParams* /* caps */) DEFAULT_RET
  virtual int SetCameraLaunchStatus(uint32_t /* on */) DEFAULT_RET
  virtual int SetCameraSmoothInfo(CameraSmoothOp /* op */, uint32_t /* fps */) DEFAULT_RET
  virtual int DisplayBWTransactionPending(bool* /* status */) DEFAULT_RET
  virtual int SetDisplayAnimating(uint64_t /* display_id */, bool /* animating */) DEFAULT_RET
  virtual int ControlIdlePowerCollapse(bool /* enable */, bool /* synchronous */) DEFAULT_RET
  virtual int GetWriteBackCapabilities(bool* /* is_wb_ubwc_supported */) DEFAULT_RET
  virtual int SetDisplayDppsAdROI(uint32_t /* display_id */, uint32_t /* h_start */,
                                  uint32_t /* h_end */, uint32_t /* v_start */,
                                  uint32_t /* v_end */, uint32_t /* factor_in */,
                                  uint32_t /* factor_out */) DEFAULT_RET
  virtual int UpdateVSyncSourceOnPowerModeOff() DEFAULT_RET
  virtual int UpdateVSyncSourceOnPowerModeDoze() DEFAULT_RET
  virtual int SetPowerMode(uint32_t /* disp_id */, PowerMode /* power_mode */) DEFAULT_RET
  virtual int IsPowerModeOverrideSupported(uint32_t /* disp_id */,
                                           bool* /* supported */) DEFAULT_RET
  virtual int IsHDRSupported(uint32_t /* disp_id */, bool* /* supported */) DEFAULT_RET
  virtual int IsWCGSupported(uint32_t /* disp_id */, bool* /* supported */) DEFAULT_RET
  virtual int SetLayerAsMask(uint32_t /* disp_id */, uint64_t /* layer_id */) DEFAULT_RET
  virtual int GetDebugProperty(const std::string /* prop_name */,
                               std::string* /* value */) DEFAULT_RET
  virtual int GetActiveBuiltinDisplayAttributes(Attributes* /* attr */) DEFAULT_RET
  virtual int SetPanelLuminanceAttributes(uint32_t /* disp_id */, float /* min_lum */,
                                          float /* max_lum */) DEFAULT_RET
  virtual int IsBuiltInDisplay(uint32_t /* disp_id */, bool* /* is_builtin */) DEFAULT_RET
  virtual int IsAsyncVDSCreationSupported(bool* /* supported */) DEFAULT_RET
  virtual int CreateVirtualDisplay(uint32_t /* width */, uint32_t /* height */,
                                   int /* format */) DEFAULT_RET
  virtual int GetSupportedDSIBitClks(uint32_t /* disp_id */,
                                     std::vector<uint64_t>* /* bit_clks */) DEFAULT_RET
  virtual int GetDSIClk(uint32_t /* disp_id */, uint64_t* /* bit_clk */) DEFAULT_RET
  virtual int SetDSIClk(uint32_t /* disp_id */, uint64_t /* bit_clk */) DEFAULT_RET
  virtual int SetCWBOutputBuffer(uint32_t /* disp_id */,
                                 const Rect /* rect */, bool /* post_processed */,
                                 const native_handle_t* /* buffer */) DEFAULT_RET
  virtual int SetQsyncMode(uint32_t /* disp_id */, QsyncMode /* mode */) DEFAULT_RET
  virtual int IsSmartPanelConfig(uint32_t /* disp_id */, uint32_t /* config_id */,
                                 bool* /* is_smart */) DEFAULT_RET
  virtual int IsRotatorSupportedFormat(int /* hal_format */, bool /* ubwc */,
                                       bool* /* supported */) DEFAULT_RET
  virtual int ControlQsyncCallback(bool /* enable */) DEFAULT_RET
  virtual int SendTUIEvent(DisplayType /* dpy */, TUIEventType /* event_type */) DEFAULT_RET
  virtual int GetDisplayHwId(uint32_t /* disp_id */, uint32_t* /* display_hw_id */) DEFAULT_RET
  virtual int GetSupportedDisplayRefreshRates(DisplayType /* dpy */, std::vector<uint32_t>*
                                              /* supported_refresh_rates */) DEFAULT_RET
  virtual int IsRCSupported(uint32_t /* disp_id */, bool* /* supported */) DEFAULT_RET
  virtual int ControlIdleStatusCallback(bool /* enable */) DEFAULT_RET
  virtual int IsSupportedConfigSwitch(uint32_t /* disp_id */, uint32_t /* config */,
                                      bool* /* supported */) DEFAULT_RET
  virtual int GetDisplayType(uint64_t /* physical_disp_id */,
                             DisplayType* /* disp_type */) DEFAULT_RET
  virtual int AllowIdleFallback() DEFAULT_RET
  virtual int GetDisplayTileCount(uint64_t /* physical_disp_id */,
                                  uint32_t* /* Display h tiles count (Min. 1 tile) */,
                                  uint32_t* /* Display v tiles count (Min. 1 tile) */) DEFAULT_RET
  virtual int SetPowerModeTiled(uint64_t /* physical_disp_id */, PowerMode /* power_mode */,
                                uint32_t /* Display tile h location */,
                                uint32_t /* Display tile v location */) DEFAULT_RET
  virtual int SetPanelBrightnessTiled(uint64_t /* physical_disp_id */, uint32_t /* level */,
                                      uint32_t /* Display tile h location */,
                                      uint32_t /* Display tile v location */) DEFAULT_RET
  virtual int SetWiderModePreference(uint64_t /* physical_disp_id */,
                                     WiderModePref /* mode_pref */) DEFAULT_RET
  virtual int ControlCameraSmoothCallback(bool /* enable */) DEFAULT_RET
  virtual int DummyDisplayConfigAPI() DEFAULT_RET

  // deprecated APIs
  virtual int GetDebugProperty(const std::string /* prop_name */,
                               std::string /* value */) DEFAULT_RET
  virtual int GetSupportedDSIBitClks(uint32_t /* disp_id */,
                                     std::vector<uint64_t> /* bit_clks */) DEFAULT_RET

 protected:
  virtual ~ConfigInterface() { }
};

}  // namespace DisplayConfig

#endif  // __CONFIG_DEFS_H__
