/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
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
#include <utils/constants.h>
#include <utils/debug.h>
#include "hwc_callbacks.h"

#define __CLASS__ "HWCCallbacks"

namespace sdm {

HWC2::Error HWCCallbacks::Hotplug(hwc2_display_t display, HWC2::Connection state) {
  if (!hotplug_) {
    return HWC2::Error::NoResources;
  }
  hotplug_(hotplug_data_, display, INT32(state));
  return HWC2::Error::None;
}

HWC2::Error HWCCallbacks::Refresh(hwc2_display_t display) {
  if (!refresh_) {
    return HWC2::Error::NoResources;
  }
  refresh_(refresh_data_, display);
  return HWC2::Error::None;
}

HWC2::Error HWCCallbacks::Vsync(hwc2_display_t display, int64_t timestamp) {
  if (!vsync_) {
    return HWC2::Error::NoResources;
  }
  vsync_(vsync_data_, display, timestamp);
  return HWC2::Error::None;
}

HWC2::Error HWCCallbacks::Register(HWC2::Callback descriptor, hwc2_callback_data_t callback_data,
                                   hwc2_function_pointer_t pointer) {
  switch (descriptor) {
    case HWC2::Callback::Hotplug:
      hotplug_data_ = callback_data;
      hotplug_ = reinterpret_cast<HWC2_PFN_HOTPLUG>(pointer);
      break;
    case HWC2::Callback::Refresh:
      refresh_data_ = callback_data;
      refresh_ = reinterpret_cast<HWC2_PFN_REFRESH>(pointer);
      break;
    case HWC2::Callback::Vsync:
      vsync_data_ = callback_data;
      vsync_ = reinterpret_cast<HWC2_PFN_VSYNC>(pointer);
      break;
    default:
      return HWC2::Error::BadParameter;
  }
  return HWC2::Error::None;
}

}  // namespace sdm
