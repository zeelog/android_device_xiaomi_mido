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

#include <hardware/hwcomposer_defs.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include "hwc_display_null.h"

#define __CLASS__ "HWCDisplayNull"

namespace sdm {

int HWCDisplayNull::Create(CoreInterface *core_intf, hwc_procs_t const **hwc_procs,
                           HWCDisplay **hwc_display) {
  int status;

  DLOGI("Null display is being created");
  HWCDisplayNull *hwc_display_null = new HWCDisplayNull(core_intf, hwc_procs);

  status = hwc_display_null->Init();
  if (status) {
    delete hwc_display_null;
    return status;
  }

  *hwc_display = hwc_display_null;

  return 0;
}

void HWCDisplayNull::Destroy(HWCDisplay *hwc_display) {
  DLOGI("Null display is being destroyed");
  hwc_display->Deinit();
  delete hwc_display;
}

// We pass the display type as HWC_DISPLAY_PRIMARY to HWCDisplay, but since we override
// and don't chain to HWCDisplay::Init(), that type does not actually get used.
HWCDisplayNull::HWCDisplayNull(CoreInterface *core_intf, hwc_procs_t const **hwc_procs)
  : HWCDisplay(core_intf, hwc_procs, kPrimary, HWC_DISPLAY_PRIMARY, false, NULL,
               DISPLAY_CLASS_NULL) {
}

int HWCDisplayNull::Init() {
  // Don't call HWCDisplay::Init() for null display, we don't want the chain of
  // DisplayPrimary / HWPrimary etc objects to be created.
  return 0;
}

int HWCDisplayNull::Deinit() {
  return 0;
}

int HWCDisplayNull::Prepare(hwc_display_contents_1_t *content_list) {
  for (size_t i = 0; i < content_list->numHwLayers; i++) {
    if (content_list->hwLayers[i].compositionType == HWC_FRAMEBUFFER_TARGET ||
        content_list->hwLayers[i].compositionType == HWC_BACKGROUND) {
      continue;
    }

    content_list->hwLayers[i].compositionType = HWC_OVERLAY;
  }

  return 0;
}

int HWCDisplayNull::Commit(hwc_display_contents_1_t *content_list) {
  // HWCSession::Commit (from where this is called) already closes all the acquire
  // fences once we return from here. So no need to close acquire fences here.
  for (size_t i = 0; i < content_list->numHwLayers; i++) {
    content_list->hwLayers[i].releaseFenceFd = -1;
  }

  return 0;
}

#define NULL_DISPLAY_FPS 60

int HWCDisplayNull::GetDisplayAttributes(uint32_t config, const uint32_t *display_attributes,
                                         int32_t *values) {
  for (int i = 0; display_attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
    // We fake display resolution as 1080P by default, though it can be overriden through a call to
    // SetResolution(), and DPI as 160, though what the DPI value does is not clear
    switch (display_attributes[i]) {
    case HWC_DISPLAY_VSYNC_PERIOD:
      values[i] = INT32(1000000000L / NULL_DISPLAY_FPS);
      break;
    case HWC_DISPLAY_WIDTH:
      values[i] = static_cast<int32_t>(x_res_);
      break;
    case HWC_DISPLAY_HEIGHT:
      values[i] = static_cast<int32_t>(y_res_);
      break;
    }
  }
  return 0;
}

}  // namespace sdm
