/* Copyright (c) 2015,2018 The Linux Foundataion. All rights reserved.
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
*
*/

#include <cutils/properties.h>
#include <dlfcn.h>
#include <utils/debug.h>

#include "cpuhint.h"
#include "hwc_debugger.h"

#define __CLASS__ "CPUHint"

namespace sdm {

DisplayError CPUHint::Init(HWCDebugHandler *debug_handler) {
  char path[PROPERTY_VALUE_MAX];
  if (debug_handler->GetProperty("ro.vendor.extension_library", path) != kErrorNone) {
    DLOGI("Vendor Extension Library not enabled");
    return kErrorNotSupported;
  }

  int pre_enable_window = -1;
  debug_handler->GetProperty(PERF_HINT_WINDOW_PROP, &pre_enable_window);
  if (pre_enable_window <= 0) {
    DLOGI("Invalid CPU Hint Pre-enable Window %d", pre_enable_window);
    return kErrorNotSupported;
  }

  DLOGI("CPU Hint Pre-enable Window %d", pre_enable_window);
  pre_enable_window_ = pre_enable_window;

  if (vendor_ext_lib_.Open(path)) {
    if (!vendor_ext_lib_.Sym("perf_lock_acq", reinterpret_cast<void **>(&fn_lock_acquire_)) ||
        !vendor_ext_lib_.Sym("perf_lock_rel", reinterpret_cast<void **>(&fn_lock_release_))) {
      DLOGW("Failed to load symbols for Vendor Extension Library");
      return kErrorNotSupported;
    }
    DLOGI("Successfully Loaded Vendor Extension Library symbols");
    enabled_ = true;
  } else {
    DLOGW("Failed to open %s : %s", path, vendor_ext_lib_.Error());
  }

  return kErrorNone;
}

void CPUHint::Set() {
  if (!enabled_) {
    return;
  }
  if (lock_acquired_) {
    return;
  }
  if (frame_countdown_) {
    --frame_countdown_;
    return;
  }

  int hint = HINT;
  lock_handle_ = fn_lock_acquire_(0 /*handle*/, 0/*duration*/,
                                  &hint, sizeof(hint) / sizeof(int));
  if (lock_handle_ >= 0) {
    lock_acquired_ = true;
  }
}

void CPUHint::Reset() {
  if (!enabled_) {
    return;
  }

  frame_countdown_ = pre_enable_window_;

  if (!lock_acquired_) {
    return;
  }

  fn_lock_release_(lock_handle_);
  lock_acquired_ = false;
}

}  // namespace sdm
