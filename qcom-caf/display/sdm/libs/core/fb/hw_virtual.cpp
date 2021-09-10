/*
* Copyright (c) 2015 - 2016, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
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

#include <utils/debug.h>
#include "hw_virtual.h"

#define __CLASS__ "HWVirtual"

namespace sdm {

HWVirtual::HWVirtual(BufferSyncHandler *buffer_sync_handler, HWInfoInterface *hw_info_intf)
  : HWDevice(buffer_sync_handler) {
  HWDevice::device_type_ = kDeviceVirtual;
  HWDevice::device_name_ = "Virtual Display Device";
  HWDevice::hw_info_intf_ = hw_info_intf;
}

DisplayError HWVirtual::Init() {
  return HWDevice::Init();
}

DisplayError HWVirtual::Validate(HWLayers *hw_layers) {
  HWDevice::ResetDisplayParams();
  return HWDevice::Validate(hw_layers);
}

DisplayError HWVirtual::GetMixerAttributes(HWMixerAttributes *mixer_attributes) {
  mixer_attributes->width = display_attributes_.x_pixels;
  mixer_attributes->height = display_attributes_.y_pixels;
  mixer_attributes_.split_left = display_attributes_.is_device_split ?
      (display_attributes_.x_pixels / 2) : mixer_attributes_.width;

  return kErrorNone;
}

DisplayError HWVirtual::SetDisplayAttributes(const HWDisplayAttributes &display_attributes) {
  if (display_attributes.x_pixels == 0 || display_attributes.y_pixels == 0) {
    return kErrorParameters;
  }

  display_attributes_ = display_attributes;

  if (display_attributes_.x_pixels > hw_resource_.max_mixer_width) {
    display_attributes_.is_device_split = true;
  }

  return kErrorNone;
}

DisplayError HWVirtual::GetDisplayAttributes(uint32_t index,
                                             HWDisplayAttributes *display_attributes) {
  display_attributes->fps = 60;
  // TODO(user): Need to update WB fps

  return kErrorNone;
}


DisplayError HWVirtual::SetActiveConfig(uint32_t active_config) {

  return kErrorNone;
}

DisplayError HWVirtual::GetHdmiMode(std::vector<uint32_t> &hdmi_modes) {
  return kErrorNone;
}

}  // namespace sdm

