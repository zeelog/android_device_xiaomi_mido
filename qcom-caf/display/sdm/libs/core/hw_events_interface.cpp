/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
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

#include <utils/utils.h>
#include <vector>

#include "hw_events_interface.h"
#include "fb/hw_events.h"
#ifdef COMPILE_DRM
#include "drm/hw_events_drm.h"
#endif

#define __CLASS__ "HWEventsInterface"

namespace sdm {

DisplayError HWEventsInterface::Create(int display_type, HWEventHandler *event_handler,
                                       const std::vector<HWEvent> &event_list,
                                       HWEventsInterface **intf) {
  DisplayError error = kErrorNone;
  HWEventsInterface *hw_events = nullptr;
  if (GetDriverType() == DriverType::FB) {
    hw_events = new HWEvents();
  } else {
#ifdef COMPILE_DRM
    hw_events = new HWEventsDRM();
#endif
  }

  error = hw_events->Init(display_type, event_handler, event_list);
  if (error != kErrorNone) {
    delete hw_events;
  } else {
    *intf = hw_events;
  }

  return error;
}

DisplayError HWEventsInterface::Destroy(HWEventsInterface *intf) {
  if (intf) {
    intf->Deinit();
    delete intf;
  }

  return kErrorNone;
}

}  // namespace sdm
