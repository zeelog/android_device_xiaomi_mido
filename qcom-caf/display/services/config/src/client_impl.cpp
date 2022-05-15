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

#include "client_impl.h"

namespace DisplayConfig {

int ClientImpl::Init(std::string client_name, ConfigCallback *callback) {
  display_config_ = IDisplayConfig::getService();
  // Unable to find Display Config 2.0 service. Fail Init.
  if (!display_config_) {
    return -1;
  }
  int32_t error = 0;
  uint64_t handle = 0;
  auto hidl_callback = [&error, &handle] (int32_t err, uint64_t client_handle) {
    error = err;
    handle = client_handle;
  };
  int pid = getpid();
  android::sp<ClientCallback> client_cb(new ClientCallback(callback));
  display_config_->registerClient(client_name + std::to_string(pid), client_cb,
                                  hidl_callback);
  client_handle_ = handle;

  return 0;
}

void ClientImpl::DeInit() {
  int32_t error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kDestroy, {}, {}, hidl_cb);
  display_config_.clear();
  display_config_ = nullptr;
}

int ClientImpl::IsDisplayConnected(DisplayType dpy, bool *connected) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&dpy), sizeof(DisplayType));
  const bool *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kIsDisplayConnected, input_params, {}, hidl_cb);
  const uint8_t *data = output_params.data();
  output = reinterpret_cast<const bool*>(data);

  if (!error) {
    *connected = *output;
  }

  return error;
}

int ClientImpl::SetDisplayStatus(DisplayType dpy, ExternalStatus status) {
  struct StatusParams input = {dpy, status};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input), sizeof(struct StatusParams));
  int error = 0;

  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kSetDisplayStatus, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::ConfigureDynRefreshRate(DynRefreshRateOp op, uint32_t refresh_rate) {
  struct DynRefreshRateParams input = {op, refresh_rate};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input),
                             sizeof(struct DynRefreshRateParams));
  int error = 0;

  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kConfigureDynRefreshRate, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::GetConfigCount(DisplayType dpy, uint32_t *count) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&dpy), sizeof(DisplayType));
  const uint32_t *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kGetConfigCount, input_params, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  output = reinterpret_cast<const uint32_t*>(data);
  *count = *output;

  return error;
}

int ClientImpl::GetActiveConfig(DisplayType dpy, uint32_t *config) {
  if (!config) {
    return -EINVAL;
  }

  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&dpy), sizeof(DisplayType));
  const uint32_t *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  if (display_config_) {
    display_config_->perform(client_handle_, kGetActiveConfig, input_params, {}, hidl_cb);
  }

  if (!error) {
    const uint8_t *data = output_params.data();
    output = reinterpret_cast<const uint32_t*>(data);
    *config = *output;
  }

  return error;
}

int ClientImpl::SetActiveConfig(DisplayType dpy, uint32_t config) {
  struct ConfigParams input = {dpy, config};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input), sizeof(struct ConfigParams));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kSetActiveConfig, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::GetDisplayAttributes(uint32_t config_index, DisplayType dpy,
                                     Attributes *attributes) {
  struct AttributesParams input = {config_index, dpy};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input), sizeof(struct AttributesParams));
  const struct Attributes *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kGetDisplayAttributes, input_params, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  output = reinterpret_cast<const Attributes*>(data);
  if (!error) {
    *attributes = *output;
  }

  return error;
}

int ClientImpl::SetPanelBrightness(uint32_t level) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&level), sizeof(uint32_t));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kSetPanelBrightness, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::GetPanelBrightness(uint32_t *level) {
  const uint32_t *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kGetPanelBrightness, {}, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  output = reinterpret_cast<const uint32_t*>(data);
  if (!error) {
    *level = *output;
  }

  return error;
}

int ClientImpl::MinHdcpEncryptionLevelChanged(DisplayType dpy, uint32_t min_enc_level) {
  struct MinHdcpEncLevelChangedParams input = {dpy, min_enc_level};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input),
                             sizeof(struct MinHdcpEncLevelChangedParams));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kMinHdcpEncryptionLevelChanged,
                           input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::RefreshScreen() {
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kRefreshScreen, {}, {}, hidl_cb);

  return error;
}

int ClientImpl::ControlPartialUpdate(DisplayType dpy, bool enable) {
  struct PartialUpdateParams input = {dpy, enable};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input),
                             sizeof(struct PartialUpdateParams));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kControlPartialUpdate, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::ToggleScreenUpdate(bool on) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&on), sizeof(bool));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kToggleScreenUpdate, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::SetIdleTimeout(uint32_t value) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&value), sizeof(uint32_t));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kSetIdleTimeout, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::GetHDRCapabilities(DisplayType dpy, HDRCapsParams *caps) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&dpy), sizeof(DisplayType));
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kGetHdrCapabilities, input_params, {}, hidl_cb);

  const uint8_t *data = output_params.data();

  if (!error) {
    const int32_t *hdr_caps;
    const float *lum;
    size_t size = output_params.size();
    size_t hdr_caps_size = size - 3 * sizeof(float);
    hdr_caps_size /= sizeof(int32_t);
    hdr_caps = reinterpret_cast<const int32_t*>(data);
    for (size_t i = 0; i < hdr_caps_size; i++) {
      caps->supported_hdr_types.push_back(*hdr_caps);
      hdr_caps++;
    }
    lum = reinterpret_cast<const float *>(hdr_caps);
    caps->max_luminance = lum[0];
    caps->max_avg_luminance = lum[1];
    caps->min_luminance = lum[2];
  }

  return error;
}

int ClientImpl::SetCameraLaunchStatus(uint32_t on) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&on), sizeof(uint32_t));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kSetCameraLaunchStatus, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::DisplayBWTransactionPending(bool *status) {
  const bool *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kDisplayBwTransactionPending, {}, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  output = reinterpret_cast<const bool*>(data);
  if (!error) {
    *status = *output;
  }

  return error;
}

int ClientImpl::SetDisplayAnimating(uint64_t display_id, bool animating) {
  struct AnimationParams input = {display_id, animating};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input), sizeof(struct AnimationParams));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kSetDisplayAnimating, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::ControlIdlePowerCollapse(bool enable, bool synchronous) {
  struct IdlePcParams input = {enable, synchronous};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input), sizeof(struct IdlePcParams));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kControlIdlePowerCollapse, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::GetWriteBackCapabilities(bool *is_wb_ubwc_supported) {
  const bool *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kGetWritebackCapabilities, {}, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  output = reinterpret_cast<const bool*>(data);
  if (!error) {
    *is_wb_ubwc_supported = *output;
  }

  return error;
}

int ClientImpl::SetDisplayDppsAdROI(uint32_t display_id, uint32_t h_start,
                                    uint32_t h_end, uint32_t v_start, uint32_t v_end,
                                    uint32_t factor_in, uint32_t factor_out) {
  struct DppsAdRoiParams input = {display_id, h_start, h_end, v_start, v_end,
                                     factor_in, factor_out};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input), sizeof(struct DppsAdRoiParams));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kSetDisplayDppsAdRoi, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::UpdateVSyncSourceOnPowerModeOff() {
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kUpdateVsyncSourceOnPowerModeOff, {}, {}, hidl_cb);

  return error;
}

int ClientImpl::UpdateVSyncSourceOnPowerModeDoze() {
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kUpdateVsyncSourceOnPowerModeDoze, {}, {}, hidl_cb);

  return error;
}

int ClientImpl::SetPowerMode(uint32_t disp_id, PowerMode power_mode) {
  struct PowerModeParams input = {disp_id, power_mode};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input), sizeof(struct PowerModeParams));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kSetPowerMode, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::IsPowerModeOverrideSupported(uint32_t disp_id, bool *supported) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&disp_id), sizeof(uint32_t));
  const bool *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kIsPowerModeOverrideSupported,
                           input_params, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  output = reinterpret_cast<const bool*>(data);

  if (!error) {
    *supported = *output;
  }

  return error;
}

int ClientImpl::IsHDRSupported(uint32_t disp_id, bool *supported) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&disp_id), sizeof(uint32_t));
  const bool *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kIsHdrSupported, input_params, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  output = reinterpret_cast<const bool*>(data);

  if (!error) {
    *supported = *output;
  }

  return error;
}

int ClientImpl::IsWCGSupported(uint32_t disp_id, bool *supported) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&disp_id), sizeof(int32_t));
  const bool *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kIsWcgSupported, input_params, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  output = reinterpret_cast<const bool*>(data);

  if (!error) {
    *supported = *output;
  }

  return error;
}

int ClientImpl::SetLayerAsMask(uint32_t disp_id, uint64_t layer_id) {
  struct LayerMaskParams input = {disp_id, layer_id};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input), sizeof(struct LayerMaskParams));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kSetLayerAsMask, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::GetDebugProperty(const std::string prop_name, std::string *value) {
  ByteStream input_params;
  std::string prop(prop_name);
  prop += '\0';
  uint8_t *data_input = reinterpret_cast<uint8_t*>(const_cast<char*>(prop.data()));
  input_params.setToExternal(reinterpret_cast<uint8_t*>(data_input),
                             prop.size() * sizeof(char));
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kGetDebugProperty, input_params, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  const char *name = reinterpret_cast<const char *>(data);
  if (!error) {
    std::string output(name);
    *value = output;
  }

  return error;
}

int ClientImpl::GetActiveBuiltinDisplayAttributes(Attributes *attr) {
  const struct Attributes *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kGetActiveBuiltinDisplayAttributes, {}, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  output = reinterpret_cast<const Attributes*>(data);
  if (!error) {
    *attr = *output;
  }

  return error;
}

int ClientImpl::SetPanelLuminanceAttributes(uint32_t disp_id, float min_lum, float max_lum) {
  struct PanelLumAttrParams input = {disp_id, min_lum, max_lum};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input), sizeof(struct PanelLumAttrParams));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kSetPanelLuminanceAttributes, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::IsBuiltInDisplay(uint32_t disp_id, bool *is_builtin) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&disp_id), sizeof(uint32_t));
  const bool *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kIsBuiltinDisplay, input_params, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  output = reinterpret_cast<const bool*>(data);

  if (!error) {
    *is_builtin = *output;
  }

  return error;
}

int ClientImpl::SetCWBOutputBuffer(uint32_t disp_id, const Rect rect, bool post_processed,
                                   const native_handle_t *buffer) {
  struct CwbBufferParams input = {disp_id, rect, post_processed};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input), sizeof(struct CwbBufferParams));

  hidl_handle handle = buffer;
  std::vector<hidl_handle> handle_vector;
  handle_vector.push_back(buffer);
  HandleStream input_handles = handle_vector;

  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kSetCwbOutputBuffer, input_params,
                           input_handles, hidl_cb);

  return error;
}

int ClientImpl::GetSupportedDSIBitClks(uint32_t disp_id, std::vector<uint64_t> *bit_clks) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&disp_id), sizeof(uint32_t));
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kGetSupportedDsiBitclks, input_params, {}, hidl_cb);

  if (!error) {
    const uint8_t *data = output_params.data();
    const uint64_t *bit_clks_data = reinterpret_cast<const uint64_t *>(data);
    int num_bit_clks = static_cast<int>(output_params.size() / sizeof(uint64_t));
    for (int i = 0; i < num_bit_clks; i++) {
      bit_clks->push_back(bit_clks_data[i]);
    }
  }

  return error;
}

int ClientImpl::GetDSIClk(uint32_t disp_id, uint64_t *bit_clk) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&disp_id), sizeof(uint32_t));
  const uint64_t *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kGetDsiClk, input_params, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  output = reinterpret_cast<const uint64_t*>(data);

  if (!error) {
    *bit_clk = *output;
  }

  return error;
}

int ClientImpl::SetDSIClk(uint32_t disp_id, uint64_t bit_clk) {
  struct DsiClkParams input = {disp_id, bit_clk};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input), sizeof(struct DsiClkParams));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kSetDsiClk, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::SetQsyncMode(uint32_t disp_id, QsyncMode mode) {
  struct QsyncModeParams input = {disp_id, mode};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input), sizeof(struct QsyncModeParams));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kSetQsyncMode, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::IsSmartPanelConfig(uint32_t disp_id, uint32_t config_id, bool *is_smart) {
  struct SmartPanelCfgParams input = {disp_id, config_id};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input),
                             sizeof(struct SmartPanelCfgParams));
  const bool *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kIsSmartPanelConfig, input_params, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  output = reinterpret_cast<const bool*>(data);

  if (!error) {
    *is_smart = *output;
  }

  return error;
}

int ClientImpl::IsAsyncVDSCreationSupported(bool *supported) {
  const bool *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kIsAsyncVdsSupported, {}, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  output = reinterpret_cast<const bool*>(data);

  if (!error) {
    *supported = *output;
  }

  return error;
}

int ClientImpl::CreateVirtualDisplay(uint32_t width, uint32_t height, int32_t format) {
  struct VdsParams input = {width, height, format};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input), sizeof(struct VdsParams));
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kCreateVirtualDisplay, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::IsRotatorSupportedFormat(int hal_format, bool ubwc, bool *supported) {
  struct RotatorFormatParams input = {hal_format, ubwc};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input),
                             sizeof(struct RotatorFormatParams));
  const bool *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kIsRotatorSupportedFormat, input_params, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  output = reinterpret_cast<const bool*>(data);

  if (!error) {
    *supported = *output;
  }

  return error;
}

int ClientImpl::ControlQsyncCallback(bool enable) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&enable), sizeof(bool));
  int32_t error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kControlQsyncCallback, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::ControlIdleStatusCallback(bool enable) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&enable), sizeof(bool));
  int32_t error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kControlIdleStatusCallback, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::SendTUIEvent(DisplayType dpy, TUIEventType event_type) {
  struct TUIEventParams input = {dpy, event_type};
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input), sizeof(struct TUIEventParams));
  int32_t error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };

  display_config_->perform(client_handle_, kSendTUIEvent, input_params, {}, hidl_cb);

  return error;
}

int ClientImpl::GetDisplayHwId(uint32_t disp_id, uint32_t *display_hw_id) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&disp_id), sizeof(uint32_t));
  ByteStream output_params;

  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kGetDisplayHwId, input_params, {}, hidl_cb);

  const uint8_t *data = output_params.data();
  const uint32_t *output = reinterpret_cast<const uint32_t*>(data);

  if (!error) {
    *display_hw_id = *output;
  }

  return error;
}

int ClientImpl::GetSupportedDisplayRefreshRates(DisplayType dpy,
                                                std::vector<uint32_t> *supported_refresh_rates) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t *>(&dpy), sizeof(DisplayType));
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params](int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kGetSupportedDisplayRefreshRates, input_params, {},
                           hidl_cb);

  if (!error) {
    const uint8_t *data = output_params.data();
    const uint32_t *refresh_rates_data = reinterpret_cast<const uint32_t *>(data);
    int num_refresh_rates = static_cast<int>(output_params.size() / sizeof(uint32_t));
    for (int i = 0; i < num_refresh_rates; i++) {
      supported_refresh_rates->push_back(refresh_rates_data[i]);
    }
  }

  return error;
}

int ClientImpl::IsRCSupported(uint32_t disp_id, bool *supported) {
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&disp_id), sizeof(uint32_t));
  const bool *output;
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };

  display_config_->perform(client_handle_, kIsRCSupported, input_params, {}, hidl_cb);

  if (!error) {
    const uint8_t *data = output_params.data();
    output = reinterpret_cast<const bool*>(data);
    *supported = *output;
  }

  return error;
}

int ClientImpl::DummyDisplayConfigAPI() {
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };
  display_config_->perform(client_handle_, kDummyOpcode, {}, {}, hidl_cb);
  if (error) {
    return -EINVAL;
  }
  return error;
}

int ClientImpl::IsSupportedConfigSwitch(uint32_t disp_id, uint32_t config, bool *supported) {
  struct SupportedModesParams input = {disp_id, config};
  ByteStream input_params;
  ByteStream output_params;
  const bool *output;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&input),
                             sizeof(struct SupportedModesParams));
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };
  if (display_config_) {
    display_config_->perform(client_handle_, kIsSupportedConfigSwitch, input_params, {}, hidl_cb);
  }

  if (!error) {
    const uint8_t *data = output_params.data();
    output = reinterpret_cast<const bool *>(data);
    *supported = *output;
  }

  return error;
}

int ClientImpl::GetDisplayType(uint64_t physical_disp_id, DisplayType *disp_type) {
  if (!disp_type) {
    return -EINVAL;
  }
  ByteStream input_params;
  input_params.setToExternal(reinterpret_cast<uint8_t*>(&physical_disp_id), sizeof(uint64_t));
  ByteStream output_params;
  int error = 0;
  auto hidl_cb = [&error, &output_params] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
    output_params = params;
  };
  if (display_config_) {
    display_config_->perform(client_handle_, kGetDisplayType, input_params, {}, hidl_cb);
  }

  if (!error) {
    const uint8_t *data = output_params.data();
    const DisplayType *output = reinterpret_cast<const DisplayType*>(data);
    *disp_type = *output;
  }
  return error;
}

int ClientImpl::AllowIdleFallback() {
  int error = 0;
  auto hidl_cb = [&error] (int32_t err, ByteStream params, HandleStream handles) {
    error = err;
  };
  if (display_config_) {
    display_config_->perform(client_handle_, kAllowIdleFallback, {}, {}, hidl_cb);
  }
  return error;
}

void ClientCallback::ParseNotifyCWBBufferDone(const ByteStream &input_params,
                                              const HandleStream &input_handles) {
  const int *error;

  if (callback_ == nullptr || input_params.size() == 0 || input_handles.size() == 0) {
    return;
  }

  const uint8_t *data = input_params.data();
  error = reinterpret_cast<const int*>(data);
  hidl_handle buffer = input_handles[0];
  callback_->NotifyCWBBufferDone(*error, buffer.getNativeHandle());
}

void ClientCallback::ParseNotifyQsyncChange(const ByteStream &input_params) {
  const struct QsyncCallbackParams *qsync_data;

  if (callback_ == nullptr || input_params.size() == 0) {
    return;
  }

  const uint8_t *data = input_params.data();
  qsync_data = reinterpret_cast<const QsyncCallbackParams*>(data);
  callback_->NotifyQsyncChange(qsync_data->qsync_enabled, qsync_data->refresh_rate,
                               qsync_data->qsync_refresh_rate);
}

void ClientCallback::ParseNotifyIdleStatus(const ByteStream &input_params) {
  const bool *is_idle;
  if (callback_ == nullptr || input_params.size() == 0) {
    return;
  }

  const uint8_t *data = input_params.data();
  is_idle = reinterpret_cast<const bool*>(data);
  callback_->NotifyIdleStatus(*is_idle);
}

Return<void> ClientCallback::perform(uint32_t op_code, const ByteStream &input_params,
                                     const HandleStream &input_handles) {
  switch (op_code) {
    case kSetCwbOutputBuffer:
      ParseNotifyCWBBufferDone(input_params, input_handles);
      break;
    case kControlQsyncCallback:
      ParseNotifyQsyncChange(input_params);
      break;
    case kControlIdleStatusCallback:
      ParseNotifyIdleStatus(input_params);
      break;
    default:
      break;
  }

  return Void();
}

}  // namespace DisplayConfig
