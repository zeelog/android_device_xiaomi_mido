/*
 * Copyright (c) 2014-2016, 2018, 2021, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

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

#ifndef __HWC_DISPLAY_PRIMARY_H__
#define __HWC_DISPLAY_PRIMARY_H__

#include <string>

#include "cpuhint.h"
#include "hwc_display.h"
#include "display_null.h"

namespace sdm {

class HWCDisplayPrimary : public HWCDisplay {
 public:
  enum {
    SET_METADATA_DYN_REFRESH_RATE,
    SET_BINDER_DYN_REFRESH_RATE,
    SET_DISPLAY_MODE,
    SET_QDCM_SOLID_FILL_INFO,
    UNSET_QDCM_SOLID_FILL_INFO,
    SET_QDCM_SOLID_FILL_RECT,
  };

  static int Create(CoreInterface *core_intf, BufferAllocator *buffer_allocator,
                    HWCCallbacks *callbacks, qService::QService *qservice,
                    HWCDisplay **hwc_display);
  static void Destroy(HWCDisplay *hwc_display);
  virtual int Init();
  virtual HWC2::Error Validate(uint32_t *out_num_types, uint32_t *out_num_requests);
  virtual HWC2::Error Present(int32_t *out_retire_fence);
  virtual HWC2::Error GetColorModes(uint32_t *out_num_modes, android_color_mode_t *out_modes);
  virtual HWC2::Error SetColorMode(android_color_mode_t mode);
  virtual HWC2::Error SetColorTransform(const float *matrix, android_color_transform_t hint);
  virtual HWC2::Error RestoreColorTransform();
  virtual int Perform(uint32_t operation, ...);
  virtual void SetSecureDisplay(bool secure_display_active);
  virtual DisplayError Refresh();
  virtual void SetIdleTimeoutMs(uint32_t timeout_ms);
  virtual void SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type);
  virtual int FrameCaptureAsync(const BufferInfo &output_buffer_info, bool post_processed);
  virtual int GetFrameCaptureStatus() { return frame_capture_status_; }
  virtual DisplayError SetDetailEnhancerConfig(const DisplayDetailEnhancerData &de_data);
  virtual DisplayError ControlPartialUpdate(bool enable, uint32_t *pending);
  virtual DisplayError SetDynamicDSIClock(uint64_t bitclk);
  virtual DisplayError GetDynamicDSIClock(uint64_t *bitclk);
  virtual DisplayError SetStandByMode(bool enable);

 private:
  HWCDisplayPrimary(CoreInterface *core_intf, BufferAllocator *buffer_allocator,
                    HWCCallbacks *callbacks, qService::QService *qservice);
  void SetMetaDataRefreshRateFlag(bool enable);
  virtual DisplayError SetDisplayMode(uint32_t mode);
  virtual DisplayError DisablePartialUpdateOneFrame();
  void ProcessBootAnimCompleted(void);
  void SetQDCMSolidFillInfo(bool enable, uint32_t color);
  void ToggleCPUHint(bool set);
  void ForceRefreshRate(uint32_t refresh_rate);
  uint32_t GetOptimalRefreshRate(bool one_updating_layer);
  void HandleFrameOutput();
  void HandleFrameCapture();
  void HandleFrameDump();
  DisplayError SetMixerResolution(uint32_t width, uint32_t height);
  DisplayError GetMixerResolution(uint32_t *width, uint32_t *height);

  BufferAllocator *buffer_allocator_ = nullptr;
  CPUHint *cpu_hint_ = nullptr;
  bool handle_idle_timeout_ = false;

  // Primary output buffer configuration
  LayerBuffer output_buffer_ = {};
  bool post_processed_output_ = false;

  // Members for 1 frame capture in a client provided buffer
  bool frame_capture_buffer_queued_ = false;
  int frame_capture_status_ = -EAGAIN;

  // Members for N frame output dump to file
  bool dump_output_to_file_ = false;
  BufferInfo output_buffer_info_ = {};
  void *output_buffer_base_ = nullptr;
  int default_mode_status_ = 0;

  //Null display
  DisplayNull display_null_;
  DisplayInterface *stored_display_intf_ = NULL;

};

}  // namespace sdm

#endif  // __HWC_DISPLAY_PRIMARY_H__
