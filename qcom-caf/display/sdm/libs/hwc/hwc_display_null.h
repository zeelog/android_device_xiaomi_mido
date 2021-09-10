/*
* Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#ifndef __HWC_DISPLAY_NULL_H__
#define __HWC_DISPLAY_NULL_H__

#include <qdMetaData.h>
#include <gralloc_priv.h>
#include "hwc_display.h"

namespace sdm {

class HWCDisplayNull : public HWCDisplay {
 public:
  static int Create(CoreInterface *core_intf, hwc_procs_t const **hwc_procs,
                    HWCDisplay **hwc_display);
  static void Destroy(HWCDisplay *hwc_display);
  virtual int Init();
  virtual int Deinit();
  virtual int Prepare(hwc_display_contents_1_t *content_list);
  virtual int Commit(hwc_display_contents_1_t *content_list);
  virtual int EventControl(int event, int enable) { return 0; }
  virtual int SetPowerMode(int mode) { return 0; }

  // Framebuffer configurations
  virtual int GetDisplayConfigs(uint32_t *configs, size_t *num_configs) {
    return HWCDisplay::GetDisplayConfigs(configs, num_configs);
  }

  virtual int GetDisplayAttributes(uint32_t config, const uint32_t *display_attributes,
                                   int32_t *values);
  virtual int GetActiveConfig() { return 0; }
  virtual int SetActiveConfig(int index) { return -1; }

  virtual void SetIdleTimeoutMs(uint32_t timeout_ms) { return; }
  virtual void SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type) { return; }
  virtual DisplayError SetMaxMixerStages(uint32_t max_mixer_stages) { return kErrorNone; }
  virtual DisplayError ControlPartialUpdate(bool enable, uint32_t *pending) { return kErrorNone; }
  virtual uint32_t GetLastPowerMode() { return 0; }
  virtual int SetFrameBufferResolution(uint32_t x_pixels, uint32_t y_pixels) { return 0; }

  virtual void GetFrameBufferResolution(uint32_t *x_pixels, uint32_t *y_pixels) {
    *x_pixels = x_res_;
    *y_pixels = y_res_;
  }

  virtual void GetPanelResolution(uint32_t *x_pixels, uint32_t *y_pixels) {
    *x_pixels = x_res_;
    *y_pixels = y_res_;
  }

  virtual int SetDisplayStatus(uint32_t display_status) { return 0; }
  virtual int OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) { return 0; }
  virtual int Perform(uint32_t operation, ...) { return 0; }
  virtual int SetCursorPosition(int x, int y) { return 0; }
  virtual void SetSecureDisplay(bool secure_display_active, bool force_flush) { return; }

  // Display Configurations
  virtual int SetActiveDisplayConfig(int config) { return 0; }
  virtual int GetActiveDisplayConfig(uint32_t *config) { return -1; }
  virtual int GetDisplayConfigCount(uint32_t *count) { return -1; }
  virtual int GetDisplayAttributesForConfig(int config,
                                            DisplayConfigVariableInfo *display_attributes) {
    return -1;
  }
  virtual bool IsValidContentList(hwc_display_contents_1_t *content_list) {
    return true;
  }

  void SetResolution(uint32_t x_res, uint32_t y_res) {
    x_res_ = x_res;
    y_res_ = y_res;
  }


 private:
  HWCDisplayNull(CoreInterface *core_intf, hwc_procs_t const **hwc_procs);
  uint32_t x_res_ = 1920;
  uint32_t y_res_ = 1080;
};

}  // namespace sdm

#endif  // __HWC_DISPLAY_NULL_H__

