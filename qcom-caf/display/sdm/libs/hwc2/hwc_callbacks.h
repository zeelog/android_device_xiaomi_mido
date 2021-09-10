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

#ifndef __HWC_CALLBACKS_H__
#define __HWC_CALLBACKS_H__

#define HWC2_INCLUDE_STRINGIFICATION
#define HWC2_USE_CPP11
#include <hardware/hwcomposer2.h>
#undef HWC2_INCLUDE_STRINGIFICATION
#undef HWC2_USE_CPP11

namespace sdm {

class HWCCallbacks {
 public:
  HWC2::Error Hotplug(hwc2_display_t display, HWC2::Connection state);
  HWC2::Error Refresh(hwc2_display_t display);
  HWC2::Error Vsync(hwc2_display_t display, int64_t timestamp);
  HWC2::Error Register(HWC2::Callback, hwc2_callback_data_t callback_data,
                       hwc2_function_pointer_t pointer);

  bool VsyncCallbackRegistered() { return (vsync_ != nullptr && vsync_data_ != nullptr); }

 private:
  hwc2_callback_data_t hotplug_data_ = nullptr;
  hwc2_callback_data_t refresh_data_ = nullptr;
  hwc2_callback_data_t vsync_data_ = nullptr;

  HWC2_PFN_HOTPLUG hotplug_ = nullptr;
  HWC2_PFN_REFRESH refresh_ = nullptr;
  HWC2_PFN_VSYNC vsync_ = nullptr;
};

}  // namespace sdm

#endif  // __HWC_CALLBACKS_H__
