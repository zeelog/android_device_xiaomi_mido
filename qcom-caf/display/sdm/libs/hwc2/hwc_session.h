/*
 * Copyright (c) 2014-2019, 2021, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __HWC_SESSION_H__
#define __HWC_SESSION_H__

#include <android/hardware/graphics/common/1.1/types.h>
#include <core/core_interface.h>
#include <utils/locker.h>
#include <cutils/native_handle.h>
#include <config/device_interface.h>
#include <string>

#include "hwc_callbacks.h"
#include "hwc_layers.h"
#include "hwc_display.h"
#include "hwc_display_primary.h"
#include "hwc_display_external.h"
#include "hwc_display_virtual.h"
#include "hwc_display_dummy.h"
#include "hwc_color_manager.h"
#include "hwc_socket_handler.h"

namespace sdm {

using ::android::hardware::Return;
using ::android::hardware::hidl_string;

typedef DisplayConfig::DisplayType DispType;

// Create a singleton uevent listener thread valid for life of hardware composer process.
// This thread blocks on uevents poll inside uevent library implementation. This poll exits
// only when there is a valid uevent, it can not be interrupted otherwise. Tieing life cycle
// of this thread with HWC session cause HWC deinitialization to wait infinitely for the
// thread to exit.
class HWCUEventListener {
 public:
  virtual ~HWCUEventListener() {}
  virtual void UEventHandler(const char *uevent_data, int length) = 0;
};

class HWCUEvent {
 public:
  HWCUEvent();
  static void UEventThread(HWCUEvent *hwc_event);
  void Register(HWCUEventListener *uevent_listener);
  inline bool InitDone() { return init_done_; }

 private:
  std::mutex mutex_;
  std::condition_variable caller_cv_;
  HWCUEventListener *uevent_listener_ = nullptr;
  bool init_done_ = false;
};

class HWCSession : hwc2_device_t, HWCUEventListener, public qClient::BnQClient,
                      public DisplayConfig::ClientContext {
 public:
  struct HWCModuleMethods : public hw_module_methods_t {
    HWCModuleMethods() { hw_module_methods_t::open = HWCSession::Open; }
  };

  explicit HWCSession(const hw_module_t *module);
  int Init();
  int Deinit();
  HWC2::Error CreateVirtualDisplayObject(uint32_t width, uint32_t height, int32_t *format);

  template <typename... Args>
  static int32_t CallDisplayFunction(hwc2_device_t *device, hwc2_display_t display,
                                     HWC2::Error (HWCDisplay::*member)(Args...), Args... args) {
    if (!device) {
      return HWC2_ERROR_BAD_PARAMETER;
    }

    if (display >= HWC_NUM_DISPLAY_TYPES) {
      return HWC2_ERROR_BAD_DISPLAY;
    }

    SCOPE_LOCK(locker_[display]);
    HWCSession *hwc_session = static_cast<HWCSession *>(device);
    auto status = HWC2::Error::BadDisplay;
    if (hwc_session->hwc_display_[display]) {
      auto hwc_display = hwc_session->hwc_display_[display];
      status = (hwc_display->*member)(std::forward<Args>(args)...);
    }
    return INT32(status);
  }

  template <typename... Args>
  static int32_t CallLayerFunction(hwc2_device_t *device, hwc2_display_t display,
                                   hwc2_layer_t layer, HWC2::Error (HWCLayer::*member)(Args...),
                                   Args... args) {
    if (!device) {
      return HWC2_ERROR_BAD_DISPLAY;
    }

    SCOPE_LOCK(locker_[display]);
    HWCSession *hwc_session = static_cast<HWCSession *>(device);
    auto status = HWC2::Error::BadDisplay;
    if (hwc_session->hwc_display_[display]) {
      status = HWC2::Error::BadLayer;
      auto hwc_layer = hwc_session->hwc_display_[display]->GetHWCLayer(layer);
      if (hwc_layer != nullptr) {
        status = (hwc_layer->*member)(std::forward<Args>(args)...);
        if (hwc_session->hwc_display_[display]->GetGeometryChanges()) {
          hwc_session->hwc_display_[display]->ResetValidation();
        }
      }
    }
    return INT32(status);
  }

  // HWC2 Functions that require a concrete implementation in hwc session
  // and hence need to be member functions
  static int32_t AcceptDisplayChanges(hwc2_device_t *device, hwc2_display_t display);
  static int32_t CreateLayer(hwc2_device_t *device, hwc2_display_t display,
                             hwc2_layer_t *out_layer_id);
  static int32_t CreateVirtualDisplay(hwc2_device_t *device, uint32_t width, uint32_t height,
                                      int32_t *format, hwc2_display_t *out_display_id);
  static int32_t DestroyLayer(hwc2_device_t *device, hwc2_display_t display, hwc2_layer_t layer);
  static int32_t DestroyVirtualDisplay(hwc2_device_t *device, hwc2_display_t display);
  static void Dump(hwc2_device_t *device, uint32_t *out_size, char *out_buffer);
  static int32_t PresentDisplay(hwc2_device_t *device, hwc2_display_t display,
                                int32_t *out_retire_fence);
  static int32_t RegisterCallback(hwc2_device_t *device, int32_t descriptor,
                                  hwc2_callback_data_t callback_data,
                                  hwc2_function_pointer_t pointer);
  static int32_t SetOutputBuffer(hwc2_device_t *device, hwc2_display_t display,
                                 buffer_handle_t buffer, int32_t releaseFence);
  static int32_t SetPowerMode(hwc2_device_t *device, hwc2_display_t display, int32_t int_mode);
  static int32_t ValidateDisplay(hwc2_device_t *device, hwc2_display_t display,
                                 uint32_t *out_num_types, uint32_t *out_num_requests);
  static int32_t SetColorMode(hwc2_device_t *device, hwc2_display_t display,
                              int32_t /*android_color_mode_t*/ int_mode);
  static int32_t SetColorTransform(hwc2_device_t *device, hwc2_display_t display,
                                   const float *matrix, int32_t /*android_color_transform_t*/ hint);
  virtual int RegisterClientContext(std::shared_ptr<DisplayConfig::ConfigCallback> callback,
                                    DisplayConfig::ConfigInterface **intf);
  virtual void UnRegisterClientContext(DisplayConfig::ConfigInterface *intf);

 private:

  class DisplayConfigImpl: public DisplayConfig::ConfigInterface {
   public:
    explicit DisplayConfigImpl(std::weak_ptr<DisplayConfig::ConfigCallback> callback,
                               HWCSession *hwc_session);

   private:
    virtual int IsDisplayConnected(DispType dpy, bool *connected);
    virtual int SetDisplayStatus(DispType dpy, DisplayConfig::ExternalStatus status);
    virtual int ConfigureDynRefreshRate(DisplayConfig::DynRefreshRateOp op, uint32_t refresh_rate);
    virtual int GetConfigCount(DispType dpy, uint32_t *count);
    virtual int GetActiveConfig(DispType dpy, uint32_t *config);
    virtual int SetActiveConfig(DispType dpy, uint32_t config);
    virtual int GetDisplayAttributes(uint32_t config_index, DispType dpy,
                                     DisplayConfig::Attributes *attributes);
    virtual int SetPanelBrightness(uint32_t level);
    virtual int GetPanelBrightness(uint32_t *level);
    virtual int MinHdcpEncryptionLevelChanged(DispType dpy, uint32_t min_enc_level);
    virtual int RefreshScreen();
    virtual int ControlPartialUpdate(DispType dpy, bool enable);
    virtual int ToggleScreenUpdate(bool on);
    virtual int SetIdleTimeout(uint32_t value);
    virtual int GetHDRCapabilities(DispType dpy, DisplayConfig::HDRCapsParams *caps);
    virtual int SetCameraLaunchStatus(uint32_t on);
    virtual int DisplayBWTransactionPending(bool *status);
    virtual int SetDisplayAnimating(uint64_t display_id, bool animating);
    virtual int IsPowerModeOverrideSupported(uint32_t disp_id, bool *supported);
    virtual int IsHDRSupported(uint32_t disp_id, bool *supported);
    virtual int IsWCGSupported(uint32_t disp_id, bool *supported);
    virtual int GetDebugProperty(const std::string prop_name, std::string value);
    virtual int IsBuiltInDisplay(uint32_t disp_id, bool *is_builtin);

    std::weak_ptr<DisplayConfig::ConfigCallback> callback_;
    HWCSession *hwc_session_ = nullptr;
  };

  static const int kExternalConnectionTimeoutMs = 500;
  static const int kPartialUpdateControlTimeoutMs = 100;
  static bool disable_skip_validate_;

  // hwc methods
  static int Open(const hw_module_t *module, const char *name, hw_device_t **device);
  static int Close(hw_device_t *device);
  static void GetCapabilities(struct hwc2_device *device, uint32_t *outCount,
                              int32_t *outCapabilities);
  static hwc2_function_pointer_t GetFunction(struct hwc2_device *device, int32_t descriptor);

  // Uevent handler
  virtual void UEventHandler(const char *uevent_data, int length);
  int GetEventValue(const char *uevent_data, int length, const char *event_info);
  int HotPlugHandler(bool connected);
  void ResetPanel();
  int32_t ConnectDisplay(int disp);
  int DisconnectDisplay(int disp);
  int GetVsyncPeriod(int disp);
  int GetConfigCount(int disp_id, uint32_t *count);
  int GetActiveConfigIndex(int disp_id, uint32_t *config);
  int SetActiveConfigIndex(int disp_id, uint32_t config);
  int ControlPartialUpdate(int dpy, bool enable);
  int DisplayBWTransactionPending(bool *status);
  int SetDisplayStatus(int disp_id, HWCDisplay::DisplayStatus status);
  int32_t GetPanelBrightness(int *level);
  int MinHdcpEncryptionLevelChanged(int disp_id, uint32_t min_enc_level);
  int32_t CreateExternalDisplay(int disp, uint32_t primary_width = 0,
                                 uint32_t primary_height = 0,
                                 bool use_primary_res  = false);
  int SetIdleTimeout(uint32_t value);
  int ToggleScreenUpdate(bool on);
  int SetCameraLaunchStatus(uint32_t on);

  int setPanelBrightness(uint32_t level);
  int refreshScreen();

  void CreateNullDisplay();
  // service methods
  void StartServices();

  // QClient methods
  virtual android::status_t notifyCallback(uint32_t command, const android::Parcel *input_parcel,
                                           android::Parcel *output_parcel);
  void DynamicDebug(const android::Parcel *input_parcel);
  void SetFrameDumpConfig(const android::Parcel *input_parcel);
  android::status_t SetMaxMixerStages(const android::Parcel *input_parcel);
  android::status_t SetDisplayMode(const android::Parcel *input_parcel);
  android::status_t ConfigureRefreshRate(const android::Parcel *input_parcel);
  android::status_t QdcmCMDHandler(const android::Parcel *input_parcel,
                                   android::Parcel *output_parcel);
  android::status_t HandleGetDisplayAttributesForConfig(const android::Parcel *input_parcel,
                                                        android::Parcel *output_parcel);
  android::status_t GetVisibleDisplayRect(const android::Parcel *input_parcel,
                                          android::Parcel *output_parcel);
  android::status_t SetMixerResolution(const android::Parcel *input_parcel);
  android::status_t SetColorModeOverride(const android::Parcel *input_parcel);
  android::status_t SetDsiClk(const android::Parcel *input_parcel);
  android::status_t GetDsiClk(const android::Parcel *input_parcel, android::Parcel *output_parcel);
  android::status_t GetSupportedDsiClk(const android::Parcel *input_parcel,
                                       android::Parcel *output_parcel);
  android::status_t getComposerStatus();
  android::status_t SetStandByMode(const android::Parcel *input_parcel);
  android::status_t GetPanelResolution(const android::Parcel *input_parcel,
                                       android::Parcel *output_parcel);


  void Refresh(hwc2_display_t display);
  void HotPlug(hwc2_display_t display, HWC2::Connection state);

  static Locker locker_[HWC_NUM_DISPLAY_TYPES];
  CoreInterface *core_intf_ = nullptr;
  HWCDisplay *hwc_display_[HWC_NUM_DISPLAY_TYPES] = {nullptr};
  HWCCallbacks callbacks_;
  HWCBufferAllocator buffer_allocator_;
  HWCBufferSyncHandler buffer_sync_handler_;
  HWCColorManager *color_mgr_ = nullptr;
  bool reset_panel_ = false;
  bool secure_display_active_ = false;
  bool external_pending_connect_ = false;
  bool external_pending_hotplug_ = false;
  bool first_commit_ = false;
  bool new_bw_mode_ = false;
  bool need_invalidate_ = false;
  int bw_mode_release_fd_ = -1;
  qService::QService *qservice_ = nullptr;
  HWCSocketHandler socket_handler_;
  bool hdmi_is_primary_ = false;
  bool is_composer_up_ = false;
  bool null_display_active_ = false;
  Locker callbacks_lock_;
  bool callback_reg_ = false;
};

}  // namespace sdm

#endif  // __HWC_SESSION_H__
