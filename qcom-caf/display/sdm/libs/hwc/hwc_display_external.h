/*
* Copyright (c) 2014, 2016, The Linux Foundation. All rights reserved.
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

#ifndef __HWC_DISPLAY_EXTERNAL_H__
#define __HWC_DISPLAY_EXTERNAL_H__

#include "hwc_display.h"

namespace sdm {

class HWCDisplayExternal : public HWCDisplay {
 public:
  static int Create(CoreInterface *core_intf, hwc_procs_t const **hwc_procs, uint32_t primary_width,
                    uint32_t primary_height, qService::QService *qservice, bool use_primary_res,
                    HWCDisplay **hwc_display);
  static int Create(CoreInterface *core_intf, hwc_procs_t const **hwc_procs,
                    qService::QService *qservice, HWCDisplay **hwc_display);
  static void Destroy(HWCDisplay *hwc_display);
  virtual int Prepare(hwc_display_contents_1_t *content_list);
  virtual int Commit(hwc_display_contents_1_t *content_list);
  virtual void SetSecureDisplay(bool secure_display_active, bool force_flush);
  virtual int Perform(uint32_t operation, ...);

 protected:
  virtual uint32_t RoundToStandardFPS(float fps);
  virtual void PrepareDynamicRefreshRate(Layer *layer);
  int drc_enabled_ = 0;
  int drc_reset_fps_enabled_ = 0;

 private:
  HWCDisplayExternal(CoreInterface *core_intf, hwc_procs_t const **hwc_procs,
                     qService::QService *qservice);
  void ApplyScanAdjustment(hwc_rect_t *display_frame);
  static void GetDownscaleResolution(uint32_t primary_width, uint32_t primary_height,
                                     uint32_t *virtual_width, uint32_t *virtual_height);
  void ForceRefreshRate(uint32_t refresh_rate);
  uint32_t GetOptimalRefreshRate(bool one_updating_layer);
};

}  // namespace sdm

#endif  // __HWC_DISPLAY_EXTERNAL_H__

