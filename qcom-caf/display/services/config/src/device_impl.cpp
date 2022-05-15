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

#include <string>
#include <vector>

#include "device_impl.h"

namespace DisplayConfig {

DeviceImpl *DeviceImpl::device_obj_ = nullptr;
std::mutex DeviceImpl::device_lock_;

int DeviceImpl::CreateInstance(ClientContext *intf) {
  std::lock_guard<std::mutex> lock(device_lock_);
  if (!device_obj_) {
    device_obj_ = new DeviceImpl();
    if (!device_obj_) {
      return -1;
    }

    android::status_t status = device_obj_->IDisplayConfig::registerAsService();
    // Unable to start Display Config 2.0 service. Fail Init.
    if (status != android::OK) {
      delete device_obj_;
      device_obj_ = nullptr;
      return -1;
    }
    device_obj_->intf_ = intf;
  }

  return 0;
}

Return<void> DeviceImpl::registerClient(const hidl_string &client_name,
                                        const sp<IDisplayConfigCallback>& callback,
                                        registerClient_cb _hidl_cb) {
  ALOGI("Register client:%s", client_name.c_str());
  int32_t error = 0;
  std::string client_name_str = client_name.c_str();
  if (client_name_str.empty()) {
    error = -EINVAL;
    _hidl_cb(error, 0);
    return Void();
  }

  if (!callback) {
    ALOGW("IDisplayConfigCallback provided is null");
  }

  uint64_t client_handle = static_cast<uint64_t>(client_id_++);
  std::shared_ptr<DeviceClientContext> device_client(new DeviceClientContext(callback));
  if (callback) {
    callback->linkToDeath(this, client_handle);
  }

  if (!intf_) {
    ALOGW("ConfigInterface is invalid");
    _hidl_cb(error, 0);
    return Void();
  }

  if (!device_client) {
    ALOGW("Failed to initialize device client:%s", client_name.c_str());
    _hidl_cb(error, 0);
    return Void();
  }

  ConfigInterface *intf = nullptr;
  error = intf_->RegisterClientContext(device_client, &intf);

  if (error) {
    callback->unlinkToDeath(this);
    return Void();
  }

  device_client->SetDeviceConfigIntf(intf);

  display_config_map_.emplace(std::make_pair(client_handle, device_client));
  _hidl_cb(error, client_handle);
  return Void();
}

void DeviceImpl::serviceDied(uint64_t client_handle,
                             const android::wp<::android::hidl::base::V1_0::IBase>& callback) {
  std::lock_guard<std::mutex> lock(death_service_mutex_);
  auto itr = display_config_map_.find(client_handle);
  std::shared_ptr<DeviceClientContext> client = itr->second;
  if (client != NULL) {
    ConfigInterface *intf = client->GetDeviceConfigIntf();
    intf_->UnRegisterClientContext(intf);
    client.reset();
    display_config_map_.erase(itr);
  }
}

DeviceImpl::DeviceClientContext::DeviceClientContext(
            const sp<IDisplayConfigCallback> callback) : callback_(callback) { }

sp<IDisplayConfigCallback> DeviceImpl::DeviceClientContext::GetDeviceConfigCallback() {
  return callback_;
}

void DeviceImpl::DeviceClientContext::SetDeviceConfigIntf(ConfigInterface *intf) {
  intf_ = intf;
}

ConfigInterface* DeviceImpl::DeviceClientContext::GetDeviceConfigIntf() {
  return intf_;
}

void DeviceImpl::DeviceClientContext::NotifyCWBBufferDone(int32_t error,
                                                          const native_handle_t *buffer) {
  ByteStream output_params;
  HandleStream output_handles;
  std::vector<hidl_handle> handles;

  output_params.setToExternal(reinterpret_cast<uint8_t*>(&error), sizeof(int));
  handles.push_back(buffer);
  output_handles = handles;

  auto status = callback_->perform(kSetCwbOutputBuffer, output_params, output_handles);
  if (status.isDeadObject()) {
    return;
  }
}

void DeviceImpl::DeviceClientContext::NotifyQsyncChange(bool qsync_enabled, int32_t refresh_rate,
                                                        int32_t qsync_refresh_rate) {
  struct QsyncCallbackParams data = {qsync_enabled, refresh_rate, qsync_refresh_rate};
  ByteStream output_params;

  output_params.setToExternal(reinterpret_cast<uint8_t*>(&data), sizeof(data));

  auto status = callback_->perform(kControlQsyncCallback, output_params, {});
  if (status.isDeadObject()) {
    return;
  }
}

void DeviceImpl::DeviceClientContext::NotifyIdleStatus(bool is_idle) {
  bool data = {is_idle};
  ByteStream output_params;

  output_params.setToExternal(reinterpret_cast<uint8_t*>(&data), sizeof(data));

  auto status = callback_->perform(kControlIdleStatusCallback, output_params, {});
  if (status.isDeadObject()) {
    return;
  }
}

void DeviceImpl::DeviceClientContext::ParseIsDisplayConnected(const ByteStream &input_params,
                                                              perform_cb _hidl_cb) {
  const DisplayType *dpy;
  bool connected = false;
  ByteStream output_params;

  const uint8_t *data = input_params.data();
  dpy = reinterpret_cast<const DisplayType*>(data);
  int32_t error = intf_->IsDisplayConnected(*dpy, &connected);

  output_params.setToExternal(reinterpret_cast<uint8_t*>(&connected),
                              sizeof(connected));
  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseSetDisplayStatus(const ByteStream &input_params,
                                                            perform_cb _hidl_cb) {
  const struct StatusParams *display_status;
  const uint8_t *data = input_params.data();
  display_status = reinterpret_cast<const StatusParams*>(data);
  int32_t error = intf_->SetDisplayStatus(display_status->dpy,
                                          display_status->status);
  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseConfigureDynRefreshRate(const ByteStream &input_params,
                                                                   perform_cb _hidl_cb) {
  const struct DynRefreshRateParams *dyn_refresh_data;
  const uint8_t *data = input_params.data();
  dyn_refresh_data = reinterpret_cast<const DynRefreshRateParams*>(data);
  int32_t error = intf_->ConfigureDynRefreshRate(dyn_refresh_data->op,
                                                 dyn_refresh_data->refresh_rate);
  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseGetConfigCount(const ByteStream &input_params,
                                                          perform_cb _hidl_cb) {
  const DisplayType *dpy;
  uint32_t count = 0;
  ByteStream output_params;

  if (input_params.size() == 0) {
    _hidl_cb(-ENODATA, {}, {});
    return;
  }

  const uint8_t *data = input_params.data();
  dpy = reinterpret_cast<const DisplayType*>(data);
  int32_t error = intf_->GetConfigCount(*dpy, &count);
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&count),
                              sizeof(uint32_t));
  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseGetActiveConfig(const ByteStream &input_params,
                                                           perform_cb _hidl_cb) {
  const DisplayType *dpy;
  uint32_t config = 0;
  ByteStream output_params;

  const uint8_t *data = input_params.data();
  dpy = reinterpret_cast<const DisplayType*>(data);
  int32_t error = intf_->GetActiveConfig(*dpy, &config);
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&config),
                              sizeof(uint32_t));
  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseSetActiveConfig(const ByteStream &input_params,
                                                           perform_cb _hidl_cb) {
  const struct ConfigParams *set_active_cfg_data;

  const uint8_t *data = input_params.data();
  set_active_cfg_data = reinterpret_cast<const ConfigParams*>(data);
  int32_t error = intf_->SetActiveConfig(set_active_cfg_data->dpy,
                                         set_active_cfg_data->config);
  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseGetDisplayAttributes(const ByteStream &input_params,
                                                                perform_cb _hidl_cb) {
  const struct AttributesParams *get_disp_attr_data;
  struct Attributes attributes = {};
  ByteStream output_params;
  int32_t error = -EINVAL;

  const uint8_t *data = input_params.data();
  get_disp_attr_data = reinterpret_cast<const AttributesParams*>(data);
  error = intf_->GetDisplayAttributes(get_disp_attr_data->config_index, get_disp_attr_data->dpy,
                                      &attributes);
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&attributes),
                              sizeof(struct Attributes));
  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseSetPanelBrightness(const ByteStream &input_params,
                                                              perform_cb _hidl_cb) {
  const uint32_t *level;
  int32_t error = 0;

  const uint8_t *data = input_params.data();
  level = reinterpret_cast<const uint32_t*>(data);
  error = intf_->SetPanelBrightness(*level);

  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseGetPanelBrightness(perform_cb _hidl_cb) {
  uint32_t level = 0;
  ByteStream output_params;
  int32_t error = -EINVAL;

  error = intf_->GetPanelBrightness(&level);
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&level),
                              sizeof(uint32_t));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseMinHdcpEncryptionLevelChanged(
                                      const ByteStream &input_params,
                                      perform_cb _hidl_cb) {
  const struct MinHdcpEncLevelChangedParams *min_hdcp_enc_level_data;
  int32_t error = 0;

  const uint8_t *data = input_params.data();
  min_hdcp_enc_level_data = reinterpret_cast<const MinHdcpEncLevelChangedParams*>(data);
  error = intf_->MinHdcpEncryptionLevelChanged(min_hdcp_enc_level_data->dpy,
                                               min_hdcp_enc_level_data->min_enc_level);

  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseRefreshScreen(perform_cb _hidl_cb) {
  int32_t error = intf_->RefreshScreen();
  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseControlPartialUpdate(const ByteStream &input_params,
                                                                perform_cb _hidl_cb) {
  const struct PartialUpdateParams *partial_update_data;
  int32_t error = 0;

  const uint8_t *data = input_params.data();
  partial_update_data = reinterpret_cast<const PartialUpdateParams*>(data);
  error = intf_->ControlPartialUpdate(partial_update_data->dpy, partial_update_data->enable);

  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseToggleScreenUpdate(const ByteStream &input_params,
                                                              perform_cb _hidl_cb) {
  const bool *on;
  int32_t error = 0;

  const uint8_t *data = input_params.data();
  on = reinterpret_cast<const bool*>(data);
  error = intf_->ToggleScreenUpdate(on);

  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseSetIdleTimeout(const ByteStream &input_params,
                                                          perform_cb _hidl_cb) {
  const uint32_t *timeout_value;

  const uint8_t *data = input_params.data();
  timeout_value = reinterpret_cast<const uint32_t*>(data);
  int32_t error = intf_->SetIdleTimeout(*timeout_value);

  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseGetHdrCapabilities(const ByteStream &input_params,
                                                              perform_cb _hidl_cb) {
  const DisplayType *dpy;
  ByteStream output_params;
  struct HDRCapsParams hdr_caps;
  int32_t *data_output;
  int32_t error = -EINVAL;

  const uint8_t *data = input_params.data();
  dpy = reinterpret_cast<const DisplayType*>(data);
  error = intf_->GetHDRCapabilities(*dpy, &hdr_caps);

  data_output = reinterpret_cast<int32_t *>(malloc(sizeof(int32_t) *
                hdr_caps.supported_hdr_types.size() + 3 * sizeof(float)));
  if (data_output != NULL) {
    for (int i = 0; i < hdr_caps.supported_hdr_types.size(); i++) {
      data_output[i] = hdr_caps.supported_hdr_types[i];
    }
    float *lum = reinterpret_cast<float *>(&data_output[hdr_caps.supported_hdr_types.size()]);
    *lum = hdr_caps.max_luminance;
    lum++;
    *lum = hdr_caps.max_avg_luminance;
    lum++;
    *lum = hdr_caps.min_luminance;
    output_params.setToExternal(reinterpret_cast<uint8_t*>(data_output), sizeof(int32_t) *
                                hdr_caps.supported_hdr_types.size() + 3 * sizeof(float));
    _hidl_cb(error, output_params, {});
  }
  else {
    _hidl_cb(-EINVAL, {}, {});
  }
}

void DeviceImpl::DeviceClientContext::ParseSetCameraLaunchStatus(const ByteStream &input_params,
                                                                 perform_cb _hidl_cb) {
  const uint32_t *launch_status_data;

  const uint8_t *data = input_params.data();
  launch_status_data = reinterpret_cast<const uint32_t*>(data);

  int32_t error = intf_->SetCameraLaunchStatus(*launch_status_data);

  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseDisplayBwTransactionPending(perform_cb _hidl_cb) {
  bool status = true;
  ByteStream output_params;

  int32_t error = intf_->DisplayBWTransactionPending(&status);
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&status),
                              sizeof(bool));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseSetDisplayAnimating(const ByteStream &input_params,
                                                               perform_cb _hidl_cb) {
  const struct AnimationParams *display_animating_data;

  const uint8_t *data = input_params.data();
  display_animating_data = reinterpret_cast<const AnimationParams*>(data);
  int32_t error = intf_->SetDisplayAnimating(display_animating_data->display_id,
                                             display_animating_data->animating);

  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseControlIdlePowerCollapse(const ByteStream &input_params,
                                                                    perform_cb _hidl_cb) {
  const struct IdlePcParams *idle_pc_data;

  const uint8_t *data = input_params.data();
  idle_pc_data = reinterpret_cast<const IdlePcParams*>(data);
  int32_t error = intf_->ControlIdlePowerCollapse(idle_pc_data->enable, idle_pc_data->synchronous);

  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseGetWritebackCapabilities(perform_cb _hidl_cb) {
  bool is_wb_ubwc_supported = false;
  int32_t error = intf_->GetWriteBackCapabilities(&is_wb_ubwc_supported);
  ByteStream output_params;
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&is_wb_ubwc_supported),
                              sizeof(bool));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseSetDisplayDppsAdRoi(const ByteStream &input_params,
                                                               perform_cb _hidl_cb) {
  const struct DppsAdRoiParams *ad_roi_data;

  const uint8_t *data = input_params.data();
  ad_roi_data = reinterpret_cast<const DppsAdRoiParams*>(data);

  int32_t error = intf_->SetDisplayDppsAdROI(ad_roi_data->display_id, ad_roi_data->h_start,
                                             ad_roi_data->h_end, ad_roi_data->v_start,
                                             ad_roi_data->v_end, ad_roi_data->factor_in,
                                             ad_roi_data->factor_out);

  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseUpdateVsyncSourceOnPowerModeOff(perform_cb _hidl_cb) {
  int32_t error = intf_->UpdateVSyncSourceOnPowerModeOff();
  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseUpdateVsyncSourceOnPowerModeDoze(perform_cb _hidl_cb) {
  int32_t error = intf_->UpdateVSyncSourceOnPowerModeDoze();
  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseSetPowerMode(const ByteStream &input_params,
                                                        perform_cb _hidl_cb) {
  const struct PowerModeParams *set_power_mode_data;

  const uint8_t *data = input_params.data();
  set_power_mode_data = reinterpret_cast<const PowerModeParams*>(data);
  int32_t error = intf_->SetPowerMode(set_power_mode_data->disp_id,
                                      set_power_mode_data->power_mode);
  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseIsPowerModeOverrideSupported(
                                      const ByteStream &input_params,
                                      perform_cb _hidl_cb) {
  const uint8_t *data = input_params.data();
  const uint32_t *disp_id = reinterpret_cast<const uint32_t*>(data);
  bool supported = false;
  int32_t error = intf_->IsPowerModeOverrideSupported(*disp_id, &supported);
  ByteStream output_params;
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&supported),
                              sizeof(bool));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseIsHdrSupported(const ByteStream &input_params,
                                                          perform_cb _hidl_cb) {
  const uint8_t *data = input_params.data();
  const uint32_t *disp_id = reinterpret_cast<const uint32_t*>(data);
  bool supported = false;
  int32_t error = intf_->IsHDRSupported(*disp_id, &supported);
  ByteStream output_params;
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&supported),
                              sizeof(bool));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseIsWcgSupported(const ByteStream &input_params,
                                                          perform_cb _hidl_cb) {
  const uint8_t *data = input_params.data();
  const int32_t *disp_id = reinterpret_cast<const int32_t*>(data);
  bool supported = false;
  int32_t error = intf_->IsWCGSupported(*disp_id, &supported);
  ByteStream output_params;
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&supported),
                              sizeof(bool));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseSetLayerAsMask(const ByteStream &input_params,
                                                          perform_cb _hidl_cb) {
  const struct LayerMaskParams *layer_mask_data;

  const uint8_t *data = input_params.data();
  layer_mask_data = reinterpret_cast<const LayerMaskParams*>(data);
  int32_t error = intf_->SetLayerAsMask(layer_mask_data->disp_id, layer_mask_data->layer_id);

  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseGetDebugProperty(const ByteStream &input_params,
                                                            perform_cb _hidl_cb) {
  std::string value;
  ByteStream output_params;

  const uint8_t *data = input_params.data();
  const char *name = reinterpret_cast<const char *>(data);
  std::string prop_name(name);
  int32_t error = intf_->GetDebugProperty(prop_name, &value);
  value += '\0';
  uint8_t *data_output = reinterpret_cast<uint8_t*>(value.data());
  output_params.setToExternal(reinterpret_cast<uint8_t*>(data_output),
                              value.size() * sizeof(char));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseGetActiveBuiltinDisplayAttributes(perform_cb _hidl_cb) {
  struct Attributes attr;
  ByteStream output_params;

  int32_t error = intf_->GetActiveBuiltinDisplayAttributes(&attr);
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&attr), sizeof(Attributes));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseSetPanelLuminanceAttributes(
                                      const ByteStream &input_params,
                                      perform_cb _hidl_cb) {
  const struct PanelLumAttrParams *panel_lum_attr_data;

  const uint8_t *data = input_params.data();
  panel_lum_attr_data = reinterpret_cast<const PanelLumAttrParams*>(data);
  int32_t error = intf_->SetPanelLuminanceAttributes(panel_lum_attr_data->disp_id,
                                                     panel_lum_attr_data->min_lum,
                                                     panel_lum_attr_data->max_lum);

  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseIsBuiltinDisplay(const ByteStream &input_params,
                                                            perform_cb _hidl_cb) {
  const uint32_t *disp_id;
  bool is_builtin = false;
  ByteStream output_params;

  const uint8_t *data = input_params.data();
  disp_id = reinterpret_cast<const uint32_t*>(data);
  int32_t error = intf_->IsBuiltInDisplay(*disp_id, &is_builtin);
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&is_builtin),
                              sizeof(bool));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseSetCwbOutputBuffer(uint64_t clientHandle,
                                                              const ByteStream &input_params,
                                                              const HandleStream &input_handles,
                                                              perform_cb _hidl_cb) {
  const struct CwbBufferParams *cwb_buffer_data;

  const uint8_t *data = input_params.data();
  cwb_buffer_data = reinterpret_cast<const CwbBufferParams*>(data);
  hidl_handle buffer = input_handles[0];

  if (!callback_ || !buffer.getNativeHandle()) {
    _hidl_cb(-1, {}, {});
    return;
  }

  int32_t error = intf_->SetCWBOutputBuffer(cwb_buffer_data->disp_id , cwb_buffer_data->rect,
                                            cwb_buffer_data->post_processed,
                                            buffer.getNativeHandle());
  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseGetSupportedDsiBitclks(const ByteStream &input_params,
                                                                  perform_cb _hidl_cb) {
  const uint32_t *disp_id;
  ByteStream output_params;
  std::vector<uint64_t> bit_clks;
  uint64_t *bit_clks_data;

  const uint8_t *data = input_params.data();
  disp_id = reinterpret_cast<const uint32_t*>(data);
  int32_t error = intf_->GetSupportedDSIBitClks(*disp_id, &bit_clks);

  bit_clks_data = reinterpret_cast<uint64_t *>(malloc(sizeof(uint64_t) * bit_clks.size()));
  if (bit_clks_data == NULL) {
    _hidl_cb(-EINVAL, {}, {});
    return;
  }
  for (int i = 0; i < bit_clks.size(); i++) {
    bit_clks_data[i] = bit_clks[i];
  }
  output_params.setToExternal(reinterpret_cast<uint8_t*>(bit_clks_data),
                              sizeof(uint64_t) * bit_clks.size());
  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseGetDsiClk(const ByteStream &input_params,
                                                     perform_cb _hidl_cb) {
  const uint32_t *disp_id;
  uint64_t bit_clk = 0;
  ByteStream output_params;

  const uint8_t *data = input_params.data();
  disp_id = reinterpret_cast<const uint32_t*>(data);
  int32_t error = intf_->GetDSIClk(*disp_id, &bit_clk);
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&bit_clk),
                              sizeof(uint64_t));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseSetDsiClk(const ByteStream &input_params,
                                                     perform_cb _hidl_cb) {
  const struct DsiClkParams *set_dsi_clk_data;

  const uint8_t *data = input_params.data();
  set_dsi_clk_data = reinterpret_cast<const DsiClkParams*>(data);
  int32_t error = intf_->SetDSIClk(set_dsi_clk_data->disp_id, set_dsi_clk_data->bit_clk);
  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseSetQsyncMode(const ByteStream &input_params,
                                                        perform_cb _hidl_cb) {
  const struct QsyncModeParams *set_qsync_mode_data;

  const uint8_t *data = input_params.data();
  set_qsync_mode_data = reinterpret_cast<const QsyncModeParams*>(data);
  int32_t error = intf_->SetQsyncMode(set_qsync_mode_data->disp_id, set_qsync_mode_data->mode);
  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseIsSmartPanelConfig(const ByteStream &input_params,
                                                              perform_cb _hidl_cb) {
  const struct SmartPanelCfgParams *smart_panel_cfg_data;
  bool is_smart = false;
  ByteStream output_params;

  const uint8_t *data = input_params.data();
  smart_panel_cfg_data = reinterpret_cast<const SmartPanelCfgParams*>(data);
  int32_t error = intf_->IsSmartPanelConfig(smart_panel_cfg_data->disp_id,
                                            smart_panel_cfg_data->config_id, &is_smart);
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&is_smart),
                              sizeof(bool));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseIsAsyncVdsSupported(perform_cb _hidl_cb) {
  bool is_supported = false;
  int32_t error = intf_->IsAsyncVDSCreationSupported(&is_supported);
  ByteStream output_params;
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&is_supported),
                             sizeof(bool));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseCreateVirtualDisplay(const ByteStream &input_params,
                                                                perform_cb _hidl_cb) {
  const struct VdsParams *vds_input_data;

  const uint8_t *data = input_params.data();
  vds_input_data = reinterpret_cast<const VdsParams*>(data);

  int32_t error = intf_->CreateVirtualDisplay(vds_input_data->width, vds_input_data->height,
                                              vds_input_data->format);

  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseIsRotatorSupportedFormat(const ByteStream &input_params,
                                                                    perform_cb _hidl_cb) {
  const struct RotatorFormatParams *input_data;
  bool supported = false;
  ByteStream output_params;

  const uint8_t *data = input_params.data();
  input_data = reinterpret_cast<const RotatorFormatParams*>(data);
  int32_t error = intf_->IsRotatorSupportedFormat(input_data->hal_format, input_data->ubwc,
                                                  &supported);
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&supported),
                              sizeof(bool));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseControlQsyncCallback(uint64_t client_handle,
                                                                const ByteStream &input_params,
                                                                perform_cb _hidl_cb) {
  const bool *enable;

  const uint8_t *data = input_params.data();
  enable = reinterpret_cast<const bool*>(data);

  int32_t error = intf_->ControlQsyncCallback(*enable);

  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseControlIdleStatusCallback(uint64_t client_handle,
                                                                     const ByteStream &input_params,
                                                                     perform_cb _hidl_cb) {
  const bool *enable;

  const uint8_t *data = input_params.data();
  enable = reinterpret_cast<const bool*>(data);

  int32_t error = intf_->ControlIdleStatusCallback(*enable);

  _hidl_cb(error, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseSendTUIEvent(const ByteStream &input_params,
                                                        perform_cb _hidl_cb) {
  const struct TUIEventParams *input_data =
               reinterpret_cast<const TUIEventParams*>(input_params.data());

  int32_t error = intf_->SendTUIEvent(input_data->dpy, input_data->tui_event_type);

  _hidl_cb(error, {}, {});
}

void DeviceImpl::ParseDestroy(uint64_t client_handle, perform_cb _hidl_cb) {
  auto itr = display_config_map_.find(client_handle);
  if (itr == display_config_map_.end()) {
    _hidl_cb(-EINVAL, {}, {});
    return;
  }

  std::shared_ptr<DeviceClientContext> client = itr->second;
  if (client != NULL) {
    sp<IDisplayConfigCallback> callback = client->GetDeviceConfigCallback();
    callback->unlinkToDeath(this);
    ConfigInterface *intf = client->GetDeviceConfigIntf();
    intf_->UnRegisterClientContext(intf);
    client.reset();
    display_config_map_.erase(itr);
  }

  _hidl_cb(0, {}, {});
}

void DeviceImpl::DeviceClientContext::ParseGetDisplayHwId(const ByteStream &input_params,
                                                          perform_cb _hidl_cb) {
  uint32_t disp_hw_id = 0;
  ByteStream output_params;

  const uint8_t *data = input_params.data();
  const uint32_t *disp_id = reinterpret_cast<const uint32_t*>(data);
  int32_t error = intf_->GetDisplayHwId(*disp_id, &disp_hw_id);
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&disp_hw_id), sizeof(uint32_t));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseGetSupportedDisplayRefreshRates(
    const ByteStream &input_params, perform_cb _hidl_cb) {
  ByteStream output_params;
  std::vector<uint32_t> refresh_rates;

  const uint8_t *data = input_params.data();
  const DisplayType *dpy = reinterpret_cast<const DisplayType *>(data);
  int32_t error = intf_->GetSupportedDisplayRefreshRates(*dpy, &refresh_rates);

  uint32_t *refresh_rates_data =
      reinterpret_cast<uint32_t *>(malloc(sizeof(uint32_t) * refresh_rates.size()));
  if (refresh_rates_data) {
    for (int i = 0; i < refresh_rates.size(); i++) {
      refresh_rates_data[i] = refresh_rates[i];
    }
    output_params.setToExternal(reinterpret_cast<uint8_t *>(refresh_rates_data),
                                sizeof(uint32_t) * refresh_rates.size());
    _hidl_cb(error, output_params, {});
  } else {
    _hidl_cb(-EINVAL, {}, {});
  }
}

void DeviceImpl::DeviceClientContext::ParseIsRCSupported(const ByteStream &input_params,
                                                         perform_cb _hidl_cb) {
  const uint8_t *data = input_params.data();
  const uint32_t *disp_id = reinterpret_cast<const uint32_t*>(data);
  bool supported = false;
  int32_t error = intf_->IsRCSupported(*disp_id, &supported);
  ByteStream output_params;
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&supported), sizeof(bool));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseIsSupportedConfigSwitch(const ByteStream &input_params,
                                                                 perform_cb _hidl_cb) {
  if (!intf_) {
    _hidl_cb(-EINVAL, {}, {});
    return;
  }

  const struct SupportedModesParams *supported_modes_data;
  const uint8_t *data = input_params.data();
  bool supported = false;
  ByteStream output_params;
  supported_modes_data = reinterpret_cast<const SupportedModesParams*>(data);

  int32_t error = intf_->IsSupportedConfigSwitch(supported_modes_data->disp_id,
                                               supported_modes_data->mode, &supported);
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&supported), sizeof(bool));
  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseGetDisplayType(const ByteStream &input_params,
                                                          perform_cb _hidl_cb) {
  const uint8_t *data = input_params.data();
  const uint64_t *physical_disp_id = reinterpret_cast<const uint64_t*>(data);
  DisplayType disp_type = DisplayConfig::DisplayType::kInvalid;
  int32_t error = intf_->GetDisplayType(*physical_disp_id, &disp_type);
  ByteStream output_params;
  output_params.setToExternal(reinterpret_cast<uint8_t*>(&disp_type), sizeof(DisplayType));

  _hidl_cb(error, output_params, {});
}

void DeviceImpl::DeviceClientContext::ParseAllowIdleFallback(perform_cb _hidl_cb) {
  int32_t error = intf_->AllowIdleFallback();
  _hidl_cb(error, {}, {});
}

Return<void> DeviceImpl::perform(uint64_t client_handle, uint32_t op_code,
                                 const ByteStream &input_params, const HandleStream &input_handles,
                                 perform_cb _hidl_cb) {
  int32_t error = 0;
  auto itr = display_config_map_.find(client_handle);
  if (itr == display_config_map_.end()) {
    error = -EINVAL;
    _hidl_cb(error, {}, {});
    return Void();
  }

  std::shared_ptr<DeviceClientContext> client = itr->second;
  if (!client) {
    error = -EINVAL;
    _hidl_cb(error, {}, {});
     return Void();
  }
  switch (op_code) {
    case kIsDisplayConnected:
      client->ParseIsDisplayConnected(input_params, _hidl_cb);
      break;
    case kSetDisplayStatus:
      client->ParseSetDisplayStatus(input_params, _hidl_cb);
      break;
    case kConfigureDynRefreshRate:
      client->ParseConfigureDynRefreshRate(input_params, _hidl_cb);
      break;
    case kGetConfigCount:
      client->ParseGetConfigCount(input_params, _hidl_cb);
      break;
    case kGetActiveConfig:
      client->ParseGetActiveConfig(input_params, _hidl_cb);
      break;
    case kSetActiveConfig:
      client->ParseSetActiveConfig(input_params, _hidl_cb);
      break;
    case kGetDisplayAttributes:
      client->ParseGetDisplayAttributes(input_params, _hidl_cb);
      break;
    case kSetPanelBrightness:
      client->ParseSetPanelBrightness(input_params, _hidl_cb);
      break;
    case kGetPanelBrightness:
      client->ParseGetPanelBrightness(_hidl_cb);
      break;
    case kMinHdcpEncryptionLevelChanged:
      client->ParseMinHdcpEncryptionLevelChanged(input_params, _hidl_cb);
      break;
    case kRefreshScreen:
      client->ParseRefreshScreen(_hidl_cb);
      break;
    case kControlPartialUpdate:
      client->ParseControlPartialUpdate(input_params, _hidl_cb);
      break;
    case kToggleScreenUpdate:
      client->ParseToggleScreenUpdate(input_params, _hidl_cb);
      break;
    case kSetIdleTimeout:
      client->ParseSetIdleTimeout(input_params, _hidl_cb);
      break;
    case kGetHdrCapabilities:
      client->ParseGetHdrCapabilities(input_params, _hidl_cb);
      break;
    case kSetCameraLaunchStatus:
      client->ParseSetCameraLaunchStatus(input_params, _hidl_cb);
      break;
    case kDisplayBwTransactionPending:
      client->ParseDisplayBwTransactionPending(_hidl_cb);
      break;
    case kSetDisplayAnimating:
      client->ParseSetDisplayAnimating(input_params, _hidl_cb);
      break;
    case kControlIdlePowerCollapse:
      client->ParseControlIdlePowerCollapse(input_params, _hidl_cb);
      break;
    case kGetWritebackCapabilities:
      client->ParseGetWritebackCapabilities(_hidl_cb);
      break;
    case kSetDisplayDppsAdRoi:
      client->ParseSetDisplayDppsAdRoi(input_params, _hidl_cb);
      break;
    case kUpdateVsyncSourceOnPowerModeOff:
      client->ParseUpdateVsyncSourceOnPowerModeOff(_hidl_cb);
      break;
    case kUpdateVsyncSourceOnPowerModeDoze:
      client->ParseUpdateVsyncSourceOnPowerModeDoze(_hidl_cb);
      break;
    case kSetPowerMode:
      client->ParseSetPowerMode(input_params, _hidl_cb);
      break;
    case kIsPowerModeOverrideSupported:
      client->ParseIsPowerModeOverrideSupported(input_params, _hidl_cb);
      break;
    case kIsHdrSupported:
      client->ParseIsHdrSupported(input_params, _hidl_cb);
      break;
    case kIsWcgSupported:
      client->ParseIsWcgSupported(input_params, _hidl_cb);
      break;
    case kSetLayerAsMask:
      client->ParseSetLayerAsMask(input_params, _hidl_cb);
      break;
    case kGetDebugProperty:
      client->ParseGetDebugProperty(input_params, _hidl_cb);
      break;
    case kGetActiveBuiltinDisplayAttributes:
      client->ParseGetActiveBuiltinDisplayAttributes(_hidl_cb);
      break;
    case kSetPanelLuminanceAttributes:
      client->ParseSetPanelLuminanceAttributes(input_params, _hidl_cb);
      break;
    case kIsBuiltinDisplay:
      client->ParseIsBuiltinDisplay(input_params, _hidl_cb);
      break;
    case kSetCwbOutputBuffer:
      client->ParseSetCwbOutputBuffer(client_handle, input_params, input_handles, _hidl_cb);
      break;
    case kGetSupportedDsiBitclks:
      client->ParseGetSupportedDsiBitclks(input_params, _hidl_cb);
      break;
    case kGetDsiClk:
      client->ParseGetDsiClk(input_params, _hidl_cb);
      break;
    case kSetDsiClk:
      client->ParseSetDsiClk(input_params, _hidl_cb);
      break;
    case kSetQsyncMode:
      client->ParseSetQsyncMode(input_params, _hidl_cb);
      break;
    case kIsSmartPanelConfig:
      client->ParseIsSmartPanelConfig(input_params, _hidl_cb);
      break;
    case kIsAsyncVdsSupported:
      client->ParseIsAsyncVdsSupported(_hidl_cb);
      break;
    case kCreateVirtualDisplay:
      client->ParseCreateVirtualDisplay(input_params, _hidl_cb);
      break;
    case kIsRotatorSupportedFormat:
      client->ParseIsRotatorSupportedFormat(input_params, _hidl_cb);
      break;
    case kControlQsyncCallback:
      client->ParseControlQsyncCallback(client_handle, input_params, _hidl_cb);
      break;
    case kControlIdleStatusCallback:
      client->ParseControlIdleStatusCallback(client_handle, input_params, _hidl_cb);
      break;
    case kSendTUIEvent:
      client->ParseSendTUIEvent(input_params, _hidl_cb);
      break;
    case kDestroy:
      ParseDestroy(client_handle, _hidl_cb);
      break;
    case kGetDisplayHwId:
      client->ParseGetDisplayHwId(input_params, _hidl_cb);
      break;
    case kGetSupportedDisplayRefreshRates:
      client->ParseGetSupportedDisplayRefreshRates(input_params, _hidl_cb);
      break;
    case kIsRCSupported:
      client->ParseIsRCSupported(input_params, _hidl_cb);
      break;
    case kIsSupportedConfigSwitch:
      client->ParseIsSupportedConfigSwitch(input_params, _hidl_cb);
      break;
    case kGetDisplayType:
      client->ParseGetDisplayType(input_params, _hidl_cb);
      break;
    case kAllowIdleFallback:
      client->ParseAllowIdleFallback(_hidl_cb);
      break;
    case kDummyOpcode:
      _hidl_cb(-EINVAL, {}, {});
      break;
    default:
      _hidl_cb(-EINVAL, {}, {});
      break;
  }
  return Void();
}

}  // namespace DisplayConfig
