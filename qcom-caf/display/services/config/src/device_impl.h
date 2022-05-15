/*
* Copyright (c) 2021 The Linux Foundation. All rights reserved.
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

#ifndef __DEVICE_IMPL_H__
#define __DEVICE_IMPL_H__

#include <vendor/display/config/2.0/IDisplayConfig.h>
#include <hidl/HidlSupport.h>
#include <log/log.h>
#include <config/device_interface.h>
#include <map>
#include <utility>
#include <string>
#include <vector>

#include "opcode_types.h"

namespace DisplayConfig {

using vendor::display::config::V2_0::IDisplayConfig;
using vendor::display::config::V2_0::IDisplayConfigCallback;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::sp;

typedef hidl_vec<uint8_t> ByteStream;
typedef hidl_vec<hidl_handle> HandleStream;

class DeviceImpl : public IDisplayConfig, public android::hardware::hidl_death_recipient {
 public:
  static int CreateInstance(ClientContext *intf);

 private:
  class DeviceClientContext : public ConfigCallback {
   public:
    explicit DeviceClientContext(const sp<IDisplayConfigCallback> callback);

    void SetDeviceConfigIntf(ConfigInterface *intf);
    ConfigInterface* GetDeviceConfigIntf();
    sp<IDisplayConfigCallback> GetDeviceConfigCallback();

    virtual void NotifyCWBBufferDone(int32_t error, const native_handle_t *buffer);
    virtual void NotifyQsyncChange(bool qsync_enabled, int32_t refresh_rate,
                                   int32_t qsync_refresh_rate);
    virtual void NotifyIdleStatus(bool is_idle);

    void ParseIsDisplayConnected(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseSetDisplayStatus(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseConfigureDynRefreshRate(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseGetConfigCount(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseGetActiveConfig(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseSetActiveConfig(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseGetDisplayAttributes(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseSetPanelBrightness(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseGetPanelBrightness(perform_cb _hidl_cb);
    void ParseMinHdcpEncryptionLevelChanged(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseRefreshScreen(perform_cb _hidl_cb);
    void ParseControlPartialUpdate(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseToggleScreenUpdate(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseSetIdleTimeout(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseGetHdrCapabilities(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseSetCameraLaunchStatus(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseDisplayBwTransactionPending(perform_cb _hidl_cb);
    void ParseSetDisplayAnimating(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseControlIdlePowerCollapse(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseGetWritebackCapabilities(perform_cb _hidl_cb);
    void ParseSetDisplayDppsAdRoi(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseUpdateVsyncSourceOnPowerModeOff(perform_cb _hidl_cb);
    void ParseUpdateVsyncSourceOnPowerModeDoze(perform_cb _hidl_cb);
    void ParseSetPowerMode(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseIsPowerModeOverrideSupported(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseIsHdrSupported(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseIsWcgSupported(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseSetLayerAsMask(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseGetDebugProperty(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseGetActiveBuiltinDisplayAttributes(perform_cb _hidl_cb);
    void ParseSetPanelLuminanceAttributes(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseIsBuiltinDisplay(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseSetCwbOutputBuffer(uint64_t clientHandle, const ByteStream &input_params,
                                 const HandleStream &inputHandles, perform_cb _hidl_cb);
    void ParseGetSupportedDsiBitclks(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseGetDsiClk(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseSetDsiClk(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseSetQsyncMode(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseIsSmartPanelConfig(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseIsAsyncVdsSupported(perform_cb _hidl_cb);
    void ParseCreateVirtualDisplay(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseIsRotatorSupportedFormat(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseControlQsyncCallback(uint64_t client_handle, const ByteStream &input_params,
                                   perform_cb _hidl_cb);
    void ParseSendTUIEvent(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseGetDisplayHwId(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseGetSupportedDisplayRefreshRates(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseIsRCSupported(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseControlIdleStatusCallback(uint64_t client_handle, const ByteStream &input_params,
                                        perform_cb _hidl_cb);
    void ParseIsSupportedConfigSwitch(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseGetDisplayType(const ByteStream &input_params, perform_cb _hidl_cb);
    void ParseAllowIdleFallback(perform_cb _hidl_cb);

   private:
    ConfigInterface *intf_ = nullptr;
    const sp<IDisplayConfigCallback> callback_;
  };

  Return<void> registerClient(const hidl_string &client_name, const sp<IDisplayConfigCallback>& cb,
                              registerClient_cb _hidl_cb) override;
  Return<void> perform(uint64_t client_handle, uint32_t op_code, const ByteStream &input_params,
                       const HandleStream &input_handles, perform_cb _hidl_cb) override;
  void serviceDied(uint64_t client_handle,
                   const android::wp<::android::hidl::base::V1_0::IBase>& callback);
  void ParseDestroy(uint64_t client_handle, perform_cb _hidl_cb);

  ClientContext *intf_ = nullptr;
  std::map<uint64_t, std::shared_ptr<DeviceClientContext>> display_config_map_;
  uint64_t client_id_ = 0;
  std::mutex death_service_mutex_;
  static DeviceImpl *device_obj_;
  static std::mutex device_lock_;
};

}  // namespace DisplayConfig

#endif  // __DEVICE_IMPL_H__
