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

#include <utils/debug.h>
#include <utils/utils.h>

#include "hw_interface.h"
#include "fb/hw_device.h"
#include "fb/hw_primary.h"
#include "fb/hw_hdmi.h"
#include "fb/hw_virtual.h"
#ifdef COMPILE_DRM
#include "drm/hw_device_drm.h"
#endif

#define __CLASS__ "HWInterface"

namespace sdm {

DisplayError HWInterface::Create(DisplayType type, HWInfoInterface *hw_info_intf,
                                 BufferSyncHandler *buffer_sync_handler,
                                 BufferAllocator *buffer_allocator, HWInterface **intf) {
  DisplayError error = kErrorNone;
  HWInterface *hw = nullptr;
  DriverType driver_type = GetDriverType();

  switch (type) {
    case kPrimary:
      if (driver_type == DriverType::FB) {
        hw = new HWPrimary(buffer_sync_handler, hw_info_intf);
      } else {
#ifdef COMPILE_DRM
        hw = new HWDeviceDRM(buffer_sync_handler, buffer_allocator, hw_info_intf);
#endif
      }
      break;
    case kHDMI:
      if (driver_type == DriverType::FB) {
        hw = new HWHDMI(buffer_sync_handler, hw_info_intf);
      } else {
        return kErrorNotSupported;
      }
      break;
    case kVirtual:
      if (driver_type == DriverType::FB) {
        hw = new HWVirtual(buffer_sync_handler, hw_info_intf);
      } else {
        return kErrorNotSupported;
      }
      break;
    default:
      DLOGE("Undefined display type");
      return kErrorUndefined;
  }

  error = hw->Init();
  if (error != kErrorNone) {
    delete hw;
    DLOGE("Init on HW Intf type %d failed", type);
    return error;
  }
  *intf = hw;

  return error;
}

DisplayError HWInterface::Destroy(HWInterface *intf) {
  if (intf) {
    intf->Deinit();
    delete intf;
  }

  return kErrorNone;
}

}  // namespace sdm
