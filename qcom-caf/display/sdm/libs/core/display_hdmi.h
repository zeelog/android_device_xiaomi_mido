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

#ifndef __DISPLAY_HDMI_H__
#define __DISPLAY_HDMI_H__

#include <vector>
#include <map>

#include "display_base.h"
#include "hw_events_interface.h"

namespace sdm {

class HWHDMIInterface;

class DisplayHDMI : public DisplayBase, HWEventHandler {
 public:
  DisplayHDMI(DisplayEventHandler *event_handler, HWInfoInterface *hw_info_intf,
              BufferSyncHandler *buffer_sync_handler, BufferAllocator *buffer_allocator,
              CompManager *comp_manager);
  virtual DisplayError Init();
  virtual DisplayError Prepare(LayerStack *layer_stack);
  virtual DisplayError GetRefreshRateRange(uint32_t *min_refresh_rate, uint32_t *max_refresh_rate);
  virtual DisplayError SetRefreshRate(uint32_t refresh_rate);
  virtual bool IsUnderscanSupported();
  virtual DisplayError OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level);

  // Implement the HWEventHandlers
  virtual DisplayError VSync(int64_t timestamp);
  virtual DisplayError Blank(bool blank) { return kErrorNone; }
  virtual void IdleTimeout() { }
  virtual void ThermalEvent(int64_t thermal_level) { }
  virtual void IdlePowerCollapse() { }

 private:
  uint32_t GetBestConfig(HWS3DMode s3d_mode);
  uint32_t GetBestConfigFromFile(std::ifstream &res_file);
  uint32_t GetClosestConfig(uint32_t width, uint32_t height);
  void GetScanSupport();
  void SetS3DMode(LayerStack *layer_stack);

  bool underscan_supported_ = false;
  HWScanSupport scan_support_;
  std::map<LayerBufferS3DFormat, HWS3DMode> s3d_format_to_mode_;
  std::vector<HWEvent> event_list_ = { HWEvent::VSYNC, HWEvent::IDLE_NOTIFY, HWEvent::EXIT
 };
};

}  // namespace sdm

#endif  // __DISPLAY_HDMI_H__
