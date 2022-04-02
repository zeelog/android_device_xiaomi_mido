/*
* Copyright (c) 2014 - 2018, The Linux Foundation. All rights reserved.
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

#include <core/dump_interface.h>
#include <core/buffer_allocator.h>
#include <private/color_params.h>
#include <utils/constants.h>
#include <utils/String16.h>
#include <cutils/properties.h>
#include <hardware_legacy/uevent.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <binder/Parcel.h>
#include <QService.h>
#include <gr.h>
#include <gralloc_priv.h>
#include <display_config.h>
#include <utils/debug.h>
#include <sync/sync.h>
#include <profiler.h>
#include <bitset>
#include <vector>

#include "hwc_buffer_allocator.h"
#include "hwc_buffer_sync_handler.h"
#include "hwc_session.h"
#include "hwc_debugger.h"
#include "hwc_display_null.h"
#include "hwc_display_primary.h"
#include "hwc_display_virtual.h"
#include "hwc_display_external_test.h"
#include "qd_utils.h"

#define __CLASS__ "HWCSession"

#define HWC_UEVENT_SWITCH_HDMI "change@/devices/virtual/switch/hdmi"
#define HWC_UEVENT_GRAPHICS_FB0 "change@/devices/virtual/graphics/fb0"

static sdm::HWCSession::HWCModuleMethods g_hwc_module_methods;

hwc_module_t HAL_MODULE_INFO_SYM = {
  .common = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 2,
    .version_minor = 0,
    .id = HWC_HARDWARE_MODULE_ID,
    .name = "QTI Hardware Composer Module",
    .author = "CodeAurora Forum",
    .methods = &g_hwc_module_methods,
    .dso = 0,
    .reserved = {0},
  }
};

namespace sdm {

Locker HWCSession::locker_;

static void Invalidate(const struct hwc_procs *procs) {
}

static void VSync(const struct hwc_procs* procs, int disp, int64_t timestamp) {
}

static void Hotplug(const struct hwc_procs* procs, int disp, int connected) {
}

HWCSession::HWCSession(const hw_module_t *module) {
  // By default, drop any events. Calls will be routed to SurfaceFlinger after registerProcs.
  hwc_procs_default_.invalidate = Invalidate;
  hwc_procs_default_.vsync = VSync;
  hwc_procs_default_.hotplug = Hotplug;

  hwc_composer_device_1_t::common.tag = HARDWARE_DEVICE_TAG;
  hwc_composer_device_1_t::common.version = HWC_DEVICE_API_VERSION_1_5;
  hwc_composer_device_1_t::common.module = const_cast<hw_module_t*>(module);
  hwc_composer_device_1_t::common.close = Close;
  hwc_composer_device_1_t::prepare = Prepare;
  hwc_composer_device_1_t::set = Set;
  hwc_composer_device_1_t::eventControl = EventControl;
  hwc_composer_device_1_t::setPowerMode = SetPowerMode;
  hwc_composer_device_1_t::query = Query;
  hwc_composer_device_1_t::registerProcs = RegisterProcs;
  hwc_composer_device_1_t::dump = Dump;
  hwc_composer_device_1_t::getDisplayConfigs = GetDisplayConfigs;
  hwc_composer_device_1_t::getDisplayAttributes = GetDisplayAttributes;
  hwc_composer_device_1_t::getActiveConfig = GetActiveConfig;
  hwc_composer_device_1_t::setActiveConfig = SetActiveConfig;
  hwc_composer_device_1_t::setCursorPositionAsync = SetCursorPositionAsync;
}

int HWCSession::Init() {
  int status = -EINVAL;
  const char *qservice_name = "display.qservice";

  // Start QService and connect to it.
  qService::QService::init();
  android::sp<qService::IQService> iqservice = android::interface_cast<qService::IQService>(
                android::defaultServiceManager()->getService(android::String16(qservice_name)));

  if (iqservice.get()) {
    iqservice->connect(android::sp<qClient::IQClient>(this));
    qservice_ = reinterpret_cast<qService::QService* >(iqservice.get());
  } else {
    DLOGE("Failed to acquire %s", qservice_name);
    return -EINVAL;
  }

  DisplayError error = CoreInterface::CreateCore(HWCDebugHandler::Get(), &buffer_allocator_,
                                                 &buffer_sync_handler_, &socket_handler_,
                                                 &core_intf_);
  if (error != kErrorNone) {
    DLOGE("Display core initialization failed. Error = %d", error);
    return -EINVAL;
  }

  SCOPE_LOCK(uevent_locker_);

  if (pthread_create(&uevent_thread_, NULL, &HWCUeventThread, this) < 0) {
    DLOGE("Failed to start = %s, error = %s", uevent_thread_name_, strerror(errno));
    CoreInterface::DestroyCore();
    return -errno;
  }

  // Wait for uevent_init() to happen and let the uevent thread wait for uevents, so that hdmi
  // connect/disconnect events won't be missed
  uevent_locker_.Wait();

  // Read which display is first, and create it and store it in primary slot
  HWDisplayInterfaceInfo hw_disp_info;
  error = core_intf_->GetFirstDisplayInterfaceType(&hw_disp_info);
  if (error == kErrorNone) {
    if (hw_disp_info.type == kHDMI) {
      // HDMI is primary display. If already connected, then create it and store in
      // primary display slot. If not connected, create a NULL display for now.
      HWCDebugHandler::Get()->SetProperty("persist.sys.is_hdmi_primary", "1");
      is_hdmi_primary_ = true;
      if (hw_disp_info.is_connected) {
        status = CreateExternalDisplay(HWC_DISPLAY_PRIMARY, 0, 0, false);
        is_hdmi_yuv_ = IsDisplayYUV(HWC_DISPLAY_PRIMARY);
      } else {
        // NullDisplay simply closes all its fences, and advertizes a standard
        // resolution to SurfaceFlinger
        status = HWCDisplayNull::Create(core_intf_, &hwc_procs_,
                                        &hwc_display_[HWC_DISPLAY_PRIMARY]);
      }
    } else {
      // Create and power on primary display
      status = HWCDisplayPrimary::Create(core_intf_, &buffer_allocator_, &hwc_procs_, qservice_,
                                         &hwc_display_[HWC_DISPLAY_PRIMARY]);
    }
  } else {
    // Create and power on primary display
    status = HWCDisplayPrimary::Create(core_intf_, &buffer_allocator_, &hwc_procs_, qservice_,
                                       &hwc_display_[HWC_DISPLAY_PRIMARY]);
  }

  if (status) {
    CoreInterface::DestroyCore();
    uevent_thread_exit_ = true;
    pthread_join(uevent_thread_, NULL);
    return status;
  }

  color_mgr_ = HWCColorManager::CreateColorManager();
  if (!color_mgr_) {
    DLOGW("Failed to load HWCColorManager.");
  }

  connected_displays_[HWC_DISPLAY_PRIMARY] = 1;
  struct rlimit fd_limit = {};
  getrlimit(RLIMIT_NOFILE, &fd_limit);
  fd_limit.rlim_cur = fd_limit.rlim_cur * 2;
  auto err = setrlimit(RLIMIT_NOFILE, &fd_limit);
  if (err) {
    DLOGW("Unable to increase fd limit -  err: %d, %s", errno, strerror(errno));
  }
  return 0;
}

int HWCSession::Deinit() {
  HWCDisplayPrimary::Destroy(hwc_display_[HWC_DISPLAY_PRIMARY]);
  hwc_display_[HWC_DISPLAY_PRIMARY] = 0;
  if (color_mgr_) {
    color_mgr_->DestroyColorManager();
  }
  uevent_thread_exit_ = true;
  pthread_join(uevent_thread_, NULL);

  DisplayError error = CoreInterface::DestroyCore();
  if (error != kErrorNone) {
    DLOGE("Display core de-initialization failed. Error = %d", error);
  }

  connected_displays_[HWC_DISPLAY_PRIMARY] = 0;
  return 0;
}

int HWCSession::Open(const hw_module_t *module, const char *name, hw_device_t **device) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!module || !name || !device) {
    DLOGE("Invalid parameters.");
    return -EINVAL;
  }

  if (!strcmp(name, HWC_HARDWARE_COMPOSER)) {
    HWCSession *hwc_session = new HWCSession(module);
    if (!hwc_session) {
      return -ENOMEM;
    }

    int status = hwc_session->Init();
    if (status != 0) {
      delete hwc_session;
      return status;
    }

    hwc_composer_device_1_t *composer_device = hwc_session;
    *device = reinterpret_cast<hw_device_t *>(composer_device);
  }

  return 0;
}

int HWCSession::Close(hw_device_t *device) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device) {
    return -EINVAL;
  }

  hwc_composer_device_1_t *composer_device = reinterpret_cast<hwc_composer_device_1_t *>(device);
  HWCSession *hwc_session = static_cast<HWCSession *>(composer_device);

  hwc_session->Deinit();
  delete hwc_session;

  return 0;
}

int HWCSession::Prepare(hwc_composer_device_1 *device, size_t num_displays,
                        hwc_display_contents_1_t **displays) {
  DTRACE_SCOPED();

  if (!device || !displays || num_displays > HWC_NUM_DISPLAY_TYPES) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  hwc_procs_t const *hwc_procs = NULL;
  bool hotplug_connect = false;

  // Hold mutex only in this scope.
  {
    SEQUENCE_ENTRY_SCOPE_LOCK(locker_);

    hwc_procs = hwc_session->hwc_procs_;

    if (hwc_session->reset_panel_) {
      DLOGW("panel is in bad state, resetting the panel");
      hwc_session->ResetPanel();
    }

    if (hwc_session->need_invalidate_) {
      hwc_procs->invalidate(hwc_procs);
      hwc_session->need_invalidate_ = false;
    }

    hwc_session->HandleSecureDisplaySession(displays);

    if (hwc_session->color_mgr_) {
      HWCDisplay *primary_display = hwc_session->hwc_display_[HWC_DISPLAY_PRIMARY];
      if (primary_display && !hwc_session->is_hdmi_primary_) {
        int ret = hwc_session->color_mgr_->SolidFillLayersPrepare(displays, primary_display);
        if (ret)
          return 0;
      }
    }

    for (ssize_t dpy = static_cast<ssize_t>(num_displays - 1); dpy >= 0; dpy--) {
      hwc_display_contents_1_t *content_list = displays[dpy];
      // If external display is connected, ignore virtual display content list.
      // If virtual display content list is valid, connect virtual display if not connected.
      // If virtual display content list is invalid, disconnect virtual display if connected.
      // If external display connection is pending, connect external display when virtual
      // display is destroyed.
      // If HDMI is primary and the output format is YUV then ignore the virtual display
      // content list.
      if (dpy == HWC_DISPLAY_VIRTUAL) {
        if (hwc_session->hwc_display_[HWC_DISPLAY_EXTERNAL] ||
                (hwc_session->is_hdmi_primary_ && hwc_session->is_hdmi_yuv_)) {
          continue;
        }

        bool valid_content = HWCDisplayVirtual::IsValidContentList(content_list);
        bool connected = (hwc_session->hwc_display_[HWC_DISPLAY_VIRTUAL] != NULL);

        if (valid_content && !connected) {
          hwc_session->ConnectDisplay(HWC_DISPLAY_VIRTUAL, content_list);
        } else if (!valid_content && connected) {
          hwc_session->DisconnectDisplay(HWC_DISPLAY_VIRTUAL);

          if (hwc_session->external_pending_connect_) {
            DLOGI("Process pending external display connection");
            hwc_session->ConnectDisplay(HWC_DISPLAY_EXTERNAL, NULL);
            hwc_session->external_pending_connect_ = false;
            hotplug_connect = true;
          }
        }
      }

      if (hwc_session->hwc_display_[dpy]) {
        if (!content_list) {
          DLOGI("Display[%d] connected. content_list is null", dpy);
        } else if (!content_list->numHwLayers) {
          DLOGE("Display[%d] connected. numHwLayers is zero", dpy);
        } else {
          hwc_session->hwc_display_[dpy]->Prepare(content_list);
        }
      }
    }
  }

  if (hotplug_connect) {
    // notify client
    hwc_procs->hotplug(hwc_procs, HWC_DISPLAY_EXTERNAL, true);
  }
  // Return 0, else client will go into bad state
  return 0;
}

int HWCSession::GetVsyncPeriod(int disp) {
  SCOPE_LOCK(locker_);
  // default value
  int32_t vsync_period = 1000000000l / 60;
  const uint32_t attribute = HWC_DISPLAY_VSYNC_PERIOD;

  if (hwc_display_[disp]) {
    hwc_display_[disp]->GetDisplayAttributes(0, &attribute, &vsync_period);
  }

  return vsync_period;
}

int HWCSession::Set(hwc_composer_device_1 *device, size_t num_displays,
                    hwc_display_contents_1_t **displays) {
  DTRACE_SCOPED();

  SEQUENCE_EXIT_SCOPE_LOCK(locker_);

  if (!device || !displays || num_displays > HWC_NUM_DISPLAY_TYPES) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);

  if (hwc_session->color_mgr_) {
    HWCDisplay *primary_display = hwc_session->hwc_display_[HWC_DISPLAY_PRIMARY];
    if (primary_display) {
      int ret = hwc_session->color_mgr_->SolidFillLayersSet(displays, primary_display);
      if (ret)
        return 0;
      hwc_session->color_mgr_->SetColorModeDetailEnhancer(primary_display);
    }
  }

  for (size_t dpy = 0; dpy < num_displays; dpy++) {
    hwc_display_contents_1_t *content_list = displays[dpy];

    // Drop virtual display composition if virtual display object could not be created
    // due to HDMI concurrency.
    if (dpy == HWC_DISPLAY_VIRTUAL && !hwc_session->hwc_display_[HWC_DISPLAY_VIRTUAL]) {
      CloseAcquireFds(content_list);
      if (content_list) {
        content_list->retireFenceFd = -1;
      }

      continue;
    }

    if (hwc_session->hwc_display_[dpy]) {
      hwc_session->hwc_display_[dpy]->Commit(content_list);
    }
    CloseAcquireFds(content_list);
  }

  if (hwc_session->new_bw_mode_) {
    hwc_display_contents_1_t *content_list = displays[HWC_DISPLAY_PRIMARY];
    hwc_session->new_bw_mode_ = false;
    if (hwc_session->bw_mode_release_fd_ >= 0) {
      close(hwc_session->bw_mode_release_fd_);
    }
    hwc_session->bw_mode_release_fd_ = dup(content_list->retireFenceFd);
  }

  // This is only indicative of how many times SurfaceFlinger posts
  // frames to the display.
  CALC_FPS();

  // Return 0, else client will go into bad state
  return 0;
}

void HWCSession::CloseAcquireFds(hwc_display_contents_1_t *content_list) {
  if (content_list) {
    for (size_t i = 0; i < content_list->numHwLayers; i++) {
      int &acquireFenceFd = content_list->hwLayers[i].acquireFenceFd;
      if (acquireFenceFd >= 0) {
        close(acquireFenceFd);
        acquireFenceFd = -1;
      }
    }

    int &outbufAcquireFenceFd = content_list->outbufAcquireFenceFd;
    if (outbufAcquireFenceFd >= 0) {
      close(outbufAcquireFenceFd);
      outbufAcquireFenceFd = -1;
    }
  }
}

bool HWCSession::IsDisplayYUV(int disp) {
  int error = -EINVAL;
  bool is_yuv = false;
  DisplayConfigVariableInfo attributes = {};

  if (disp < 0 || disp >= HWC_NUM_DISPLAY_TYPES || !hwc_display_[disp]) {
    DLOGE("Invalid input parameters. Display = %d", disp);
    return is_yuv;
  }

  uint32_t active_config = 0;
  error = hwc_display_[disp]->GetActiveDisplayConfig(&active_config);
  if (!error) {
    error = hwc_display_[disp]->GetDisplayAttributesForConfig(INT(active_config), &attributes);
    if (error == 0) {
      is_yuv = attributes.is_yuv;
    } else {
      DLOGW("Error querying display attributes. Display = %d, Config = %d", disp, active_config);
    }
  }

  return is_yuv;
}

int HWCSession::EventControl(hwc_composer_device_1 *device, int disp, int event, int enable) {
  SCOPE_LOCK(locker_);

  if (!device) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;
  if (hwc_session->hwc_display_[disp]) {
    status = hwc_session->hwc_display_[disp]->EventControl(event, enable);
  }

  return status;
}

int HWCSession::SetPowerMode(hwc_composer_device_1 *device, int disp, int mode) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;
  if (hwc_session->hwc_display_[disp]) {
    status = hwc_session->hwc_display_[disp]->SetPowerMode(mode);
  }

  return status;
}

int HWCSession::Query(hwc_composer_device_1 *device, int param, int *value) {
  SCOPE_LOCK(locker_);

  if (!device || !value) {
    return -EINVAL;
  }

  int status = 0;

  switch (param) {
  case HWC_BACKGROUND_LAYER_SUPPORTED:
    value[0] = 1;
    break;

  default:
    status = -EINVAL;
  }

  return status;
}

void HWCSession::RegisterProcs(hwc_composer_device_1 *device, hwc_procs_t const *procs) {
  SCOPE_LOCK(locker_);

  if (!device || !procs) {
    return;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  hwc_session->hwc_procs_ = procs;
}

void HWCSession::Dump(hwc_composer_device_1 *device, char *buffer, int length) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device || !buffer || !length) {
    return;
  }

  DumpInterface::GetDump(buffer, UINT32(length));
}

int HWCSession::GetDisplayConfigs(hwc_composer_device_1 *device, int disp, uint32_t *configs,
                                  size_t *num_configs) {
  SCOPE_LOCK(locker_);

  if (!device || !configs || !num_configs) {
    return -EINVAL;
  }

  if (disp < HWC_DISPLAY_PRIMARY || disp >  HWC_NUM_PHYSICAL_DISPLAY_TYPES) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;
  if (hwc_session->hwc_display_[disp]) {
    status = hwc_session->hwc_display_[disp]->GetDisplayConfigs(configs, num_configs);
  }

  return status;
}

int HWCSession::GetDisplayAttributes(hwc_composer_device_1 *device, int disp, uint32_t config,
                                     const uint32_t *display_attributes, int32_t *values) {
  SCOPE_LOCK(locker_);

  if (!device || !display_attributes || !values) {
    return -EINVAL;
  }

  if (disp < HWC_DISPLAY_PRIMARY || disp >  HWC_NUM_PHYSICAL_DISPLAY_TYPES) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;
  if (hwc_session->hwc_display_[disp]) {
    status = hwc_session->hwc_display_[disp]->GetDisplayAttributes(config, display_attributes,
                                                                   values);
  }

  return status;
}

int HWCSession::GetActiveConfig(hwc_composer_device_1 *device, int disp) {
  SCOPE_LOCK(locker_);

  if (!device) {
    return -EINVAL;
  }

  if (disp < HWC_DISPLAY_PRIMARY || disp >  HWC_NUM_PHYSICAL_DISPLAY_TYPES) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int active_config = -1;
  if (hwc_session->hwc_display_[disp]) {
    active_config = hwc_session->hwc_display_[disp]->GetActiveConfig();
  }

  return active_config;
}

int HWCSession::SetActiveConfig(hwc_composer_device_1 *device, int disp, int index) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  if (!device) {
    return -EINVAL;
  }

  if (disp < HWC_DISPLAY_PRIMARY || disp >  HWC_NUM_PHYSICAL_DISPLAY_TYPES) {
    return -EINVAL;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int status = -EINVAL;

  if (hwc_session->hwc_display_[disp]) {
    status = hwc_session->hwc_display_[disp]->SetActiveConfig(index);
  }

  return status;
}

int HWCSession::SetCursorPositionAsync(hwc_composer_device_1 *device, int disp, int x, int y) {
  DTRACE_SCOPED();

  SCOPE_LOCK(locker_);

  if (!device || (disp < HWC_DISPLAY_PRIMARY) || (disp > HWC_DISPLAY_VIRTUAL)) {
    return -EINVAL;
  }

  int status = -EINVAL;
  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  if (hwc_session->hwc_display_[disp]) {
    status = hwc_session->hwc_display_[disp]->SetCursorPosition(x, y);
  }

  return status;
}

int HWCSession::ConnectDisplay(int disp, hwc_display_contents_1_t *content_list) {
  DLOGI("Display = %d", disp);

  int status = 0;
  uint32_t primary_width = 0;
  uint32_t primary_height = 0;

  hwc_display_[HWC_DISPLAY_PRIMARY]->GetFrameBufferResolution(&primary_width, &primary_height);

  if (disp == HWC_DISPLAY_EXTERNAL) {
    status = CreateExternalDisplay(disp, primary_width, primary_height, false);
    connected_displays_[HWC_DISPLAY_EXTERNAL] = 1;
  } else if (disp == HWC_DISPLAY_VIRTUAL) {
    status = HWCDisplayVirtual::Create(core_intf_, &hwc_procs_, primary_width, primary_height,
                                       content_list, &hwc_display_[disp]);
    connected_displays_[HWC_DISPLAY_VIRTUAL] = 1;
  } else {
    DLOGE("Invalid display type");
    return -1;
  }

  if (!status) {
    hwc_display_[disp]->SetSecureDisplay(secure_display_active_, true);
  }

  return status;
}

int HWCSession::DisconnectDisplay(int disp) {
  DLOGI("Display = %d", disp);

  if (disp == HWC_DISPLAY_EXTERNAL) {
    HWCDisplayExternal::Destroy(hwc_display_[disp]);
    connected_displays_[HWC_DISPLAY_EXTERNAL] = 0;
  } else if (disp == HWC_DISPLAY_VIRTUAL) {
    HWCDisplayVirtual::Destroy(hwc_display_[disp]);
    connected_displays_[HWC_DISPLAY_VIRTUAL] = 0;
  } else {
    DLOGE("Invalid display type");
    return -1;
  }

  hwc_display_[disp] = NULL;

  return 0;
}

android::status_t HWCSession::notifyCallback(uint32_t command, const android::Parcel *input_parcel,
                                             android::Parcel *output_parcel) {
  SEQUENCE_WAIT_SCOPE_LOCK(locker_);

  android::status_t status = 0;

  switch (command) {
  case qService::IQService::DYNAMIC_DEBUG:
    DynamicDebug(input_parcel);
    break;

  case qService::IQService::SCREEN_REFRESH:
    hwc_procs_->invalidate(hwc_procs_);
    break;

  case qService::IQService::SET_IDLE_TIMEOUT:
    if (hwc_display_[HWC_DISPLAY_PRIMARY]) {
      uint32_t timeout = UINT32(input_parcel->readInt32());
      hwc_display_[HWC_DISPLAY_PRIMARY]->SetIdleTimeoutMs(timeout);
    }
    break;

  case qService::IQService::SET_FRAME_DUMP_CONFIG:
    SetFrameDumpConfig(input_parcel);
    break;

  case qService::IQService::SET_MAX_PIPES_PER_MIXER:
    status = SetMaxMixerStages(input_parcel);
    break;

  case qService::IQService::SET_DISPLAY_MODE:
    status = SetDisplayMode(input_parcel);
    break;

  case qService::IQService::SET_SECONDARY_DISPLAY_STATUS:
    status = SetSecondaryDisplayStatus(input_parcel, output_parcel);
    break;

  case qService::IQService::CONFIGURE_DYN_REFRESH_RATE:
    status = ConfigureRefreshRate(input_parcel);
    break;

  case qService::IQService::SET_VIEW_FRAME:
    break;

  case qService::IQService::TOGGLE_SCREEN_UPDATES:
    status = ToggleScreenUpdates(input_parcel, output_parcel);
    break;

  case qService::IQService::QDCM_SVC_CMDS:
    status = QdcmCMDHandler(input_parcel, output_parcel);
    break;

  case qService::IQService::MIN_HDCP_ENCRYPTION_LEVEL_CHANGED:
    status = OnMinHdcpEncryptionLevelChange(input_parcel, output_parcel);
    break;

  case qService::IQService::CONTROL_PARTIAL_UPDATE:
    status = ControlPartialUpdate(input_parcel, output_parcel);
    break;

  case qService::IQService::SET_ACTIVE_CONFIG:
    status = HandleSetActiveDisplayConfig(input_parcel, output_parcel);
    break;

  case qService::IQService::GET_ACTIVE_CONFIG:
    status = HandleGetActiveDisplayConfig(input_parcel, output_parcel);
    break;

  case qService::IQService::GET_CONFIG_COUNT:
    status = HandleGetDisplayConfigCount(input_parcel, output_parcel);
    break;

  case qService::IQService::GET_DISPLAY_ATTRIBUTES_FOR_CONFIG:
    status = HandleGetDisplayAttributesForConfig(input_parcel, output_parcel);
    break;

  case qService::IQService::GET_PANEL_BRIGHTNESS:
    status = GetPanelBrightness(input_parcel, output_parcel);
    break;

  case qService::IQService::SET_PANEL_BRIGHTNESS:
    status = SetPanelBrightness(input_parcel, output_parcel);
    break;

  case qService::IQService::GET_DISPLAY_VISIBLE_REGION:
    status = GetVisibleDisplayRect(input_parcel, output_parcel);
    break;

  case qService::IQService::SET_CAMERA_STATUS:
    status = SetDynamicBWForCamera(input_parcel, output_parcel);
    break;

  case qService::IQService::GET_BW_TRANSACTION_STATUS:
    status = GetBWTransactionStatus(input_parcel, output_parcel);
    break;

  case qService::IQService::SET_LAYER_MIXER_RESOLUTION:
    status = SetMixerResolution(input_parcel);
    break;

  case qService::IQService::GET_HDR_CAPABILITIES:
    status = GetHdrCapabilities(input_parcel, output_parcel);
    break;

  default:
    DLOGW("QService command = %d is not supported", command);
    return -EINVAL;
  }

  return status;
}

android::status_t HWCSession::ToggleScreenUpdates(const android::Parcel *input_parcel,
                                                  android::Parcel *output_parcel) {
  int input = input_parcel->readInt32();
  int error = android::BAD_VALUE;

  if (hwc_display_[HWC_DISPLAY_PRIMARY] && (input <= 1) && (input >= 0)) {
    error = hwc_display_[HWC_DISPLAY_PRIMARY]->ToggleScreenUpdates(input == 1);
    if (error != 0) {
      DLOGE("Failed to toggle screen updates = %d. Error = %d", input, error);
    }
  }
  output_parcel->writeInt32(error);

  return error;
}

android::status_t HWCSession::SetPanelBrightness(const android::Parcel *input_parcel,
                                                 android::Parcel *output_parcel) {
  int level = input_parcel->readInt32();
  int error = android::BAD_VALUE;

  if (hwc_display_[HWC_DISPLAY_PRIMARY]) {
    error = hwc_display_[HWC_DISPLAY_PRIMARY]->SetPanelBrightness(level);
    if (error != 0) {
      DLOGE("Failed to set the panel brightness = %d. Error = %d", level, error);
    }
  }
  output_parcel->writeInt32(error);

  return error;
}

android::status_t HWCSession::GetPanelBrightness(const android::Parcel *input_parcel,
                                                 android::Parcel *output_parcel) {
  int error = android::BAD_VALUE;
  int ret = error;

  if (hwc_display_[HWC_DISPLAY_PRIMARY]) {
    error = hwc_display_[HWC_DISPLAY_PRIMARY]->GetPanelBrightness(&ret);
    if (error != 0) {
      ret = error;
      DLOGE("Failed to get the panel brightness. Error = %d", error);
    }
  }
  output_parcel->writeInt32(ret);

  return error;
}

android::status_t HWCSession::ControlPartialUpdate(const android::Parcel *input_parcel,
                                                   android::Parcel *out) {
  DisplayError error = kErrorNone;
  int ret = 0;
  uint32_t disp_id = UINT32(input_parcel->readInt32());
  uint32_t enable = UINT32(input_parcel->readInt32());

  if (disp_id != HWC_DISPLAY_PRIMARY) {
    DLOGW("CONTROL_PARTIAL_UPDATE is not applicable for display = %d", disp_id);
    ret = -EINVAL;
    out->writeInt32(ret);
    return ret;
  }

  if (!hwc_display_[HWC_DISPLAY_PRIMARY]) {
    DLOGE("primary display object is not instantiated");
    ret = -EINVAL;
    out->writeInt32(ret);
    return ret;
  }

  uint32_t pending = 0;
  error = hwc_display_[HWC_DISPLAY_PRIMARY]->ControlPartialUpdate(enable, &pending);

  if (error == kErrorNone) {
    if (!pending) {
      out->writeInt32(ret);
      return ret;
    }
  } else if (error == kErrorNotSupported) {
    out->writeInt32(ret);
    return ret;
  } else {
    ret = -EINVAL;
    out->writeInt32(ret);
    return ret;
  }

  // Todo(user): Unlock it before sending events to client. It may cause deadlocks in future.
  hwc_procs_->invalidate(hwc_procs_);

  // Wait until partial update control is complete
  ret = locker_.WaitFinite(kPartialUpdateControlTimeoutMs);

  out->writeInt32(ret);

  return ret;
}

android::status_t HWCSession::HandleSetActiveDisplayConfig(const android::Parcel *input_parcel,
                                                     android::Parcel *output_parcel) {
  int config = input_parcel->readInt32();
  int dpy = input_parcel->readInt32();
  int error = android::BAD_VALUE;

  if (dpy < HWC_DISPLAY_PRIMARY || dpy > HWC_DISPLAY_VIRTUAL) {
    return android::BAD_VALUE;
  }

  if (hwc_display_[dpy]) {
    error = hwc_display_[dpy]->SetActiveDisplayConfig(config);
    if (error == 0) {
      hwc_procs_->invalidate(hwc_procs_);
    }
  }

  return error;
}

android::status_t HWCSession::HandleGetActiveDisplayConfig(const android::Parcel *input_parcel,
                                                           android::Parcel *output_parcel) {
  int dpy = input_parcel->readInt32();
  int error = android::BAD_VALUE;

  if (dpy < HWC_DISPLAY_PRIMARY || dpy > HWC_DISPLAY_VIRTUAL) {
    return android::BAD_VALUE;
  }

  if (hwc_display_[dpy]) {
    uint32_t config = 0;
    error = hwc_display_[dpy]->GetActiveDisplayConfig(&config);
    if (error == 0) {
      output_parcel->writeInt32(INT(config));
    }
  }

  return error;
}

android::status_t HWCSession::HandleGetDisplayConfigCount(const android::Parcel *input_parcel,
                                                          android::Parcel *output_parcel) {
  int dpy = input_parcel->readInt32();
  int error = android::BAD_VALUE;

  if (dpy < HWC_DISPLAY_PRIMARY || dpy > HWC_DISPLAY_VIRTUAL) {
    return android::BAD_VALUE;
  }

  uint32_t count = 0;
  if (hwc_display_[dpy]) {
    error = hwc_display_[dpy]->GetDisplayConfigCount(&count);
    if (error == 0) {
      output_parcel->writeInt32(INT(count));
    }
  }

  return error;
}

android::status_t HWCSession::SetDisplayPort(DisplayPort sdm_disp_port, int *hwc_disp_port) {
  if (!hwc_disp_port) {
    return -EINVAL;
  }

  switch (sdm_disp_port) {
    case kPortDSI:
      *hwc_disp_port = qdutils::DISPLAY_PORT_DSI;
      break;
    case kPortDTV:
      *hwc_disp_port = qdutils::DISPLAY_PORT_DTV;
      break;
    case kPortLVDS:
      *hwc_disp_port = qdutils::DISPLAY_PORT_LVDS;
      break;
    case kPortEDP:
      *hwc_disp_port = qdutils::DISPLAY_PORT_EDP;
      break;
    case kPortWriteBack:
      *hwc_disp_port = qdutils::DISPLAY_PORT_WRITEBACK;
      break;
    case kPortDP:
      *hwc_disp_port = qdutils::DISPLAY_PORT_DP;
      break;
    case kPortDefault:
      *hwc_disp_port = qdutils::DISPLAY_PORT_DEFAULT;
      break;
    default:
      DLOGE("Invalid sdm display port %d", sdm_disp_port);
      return -EINVAL;
  }

  return 0;
}

android::status_t HWCSession::HandleGetDisplayAttributesForConfig(const android::Parcel
                                                                  *input_parcel,
                                                                  android::Parcel *output_parcel) {
  int config = input_parcel->readInt32();
  int dpy = input_parcel->readInt32();
  int error = android::BAD_VALUE;
  DisplayConfigVariableInfo display_attributes;
  DisplayPort sdm_disp_port = kPortDefault;
  int hwc_disp_port = qdutils::DISPLAY_PORT_DEFAULT;

  if (dpy < HWC_DISPLAY_PRIMARY || dpy >= HWC_NUM_DISPLAY_TYPES || config < 0) {
    return android::BAD_VALUE;
  }

  if (hwc_display_[dpy]) {
    error = hwc_display_[dpy]->GetDisplayAttributesForConfig(config, &display_attributes);
    if (error == 0) {
      hwc_display_[dpy]->GetDisplayPort(&sdm_disp_port);

      SetDisplayPort(sdm_disp_port, &hwc_disp_port);

      output_parcel->writeInt32(INT(display_attributes.vsync_period_ns));
      output_parcel->writeInt32(INT(display_attributes.x_pixels));
      output_parcel->writeInt32(INT(display_attributes.y_pixels));
      output_parcel->writeFloat(display_attributes.x_dpi);
      output_parcel->writeFloat(display_attributes.y_dpi);
      output_parcel->writeInt32(hwc_disp_port);
      output_parcel->writeInt32(display_attributes.is_yuv);
    }
  }

  return error;
}

android::status_t HWCSession::SetSecondaryDisplayStatus(const android::Parcel *input_parcel,
                                                        android::Parcel *output_parcel) {
  int ret = -EINVAL;

  uint32_t display_id = UINT32(input_parcel->readInt32());
  uint32_t display_status = UINT32(input_parcel->readInt32());

  DLOGI("Display = %d, Status = %d", display_id, display_status);

  if (display_id >= HWC_NUM_DISPLAY_TYPES) {
    DLOGE("Invalid display_id");
  } else if (display_id == HWC_DISPLAY_PRIMARY) {
    DLOGE("Not supported for this display");
  } else if (!hwc_display_[display_id]) {
    DLOGW("Display is not connected");
  } else {
    ret = hwc_display_[display_id]->SetDisplayStatus(display_status);
  }

  output_parcel->writeInt32(ret);

  return ret;
}

android::status_t HWCSession::ConfigureRefreshRate(const android::Parcel *input_parcel) {
  uint32_t operation = UINT32(input_parcel->readInt32());
  switch (operation) {
    case qdutils::DISABLE_METADATA_DYN_REFRESH_RATE:
      return hwc_display_[HWC_DISPLAY_PRIMARY]->Perform(
          HWCDisplayPrimary::SET_METADATA_DYN_REFRESH_RATE, false);
    case qdutils::ENABLE_METADATA_DYN_REFRESH_RATE:
      return hwc_display_[HWC_DISPLAY_PRIMARY]->Perform(
          HWCDisplayPrimary::SET_METADATA_DYN_REFRESH_RATE, true);
    case qdutils::SET_BINDER_DYN_REFRESH_RATE:
      {
        uint32_t refresh_rate = UINT32(input_parcel->readInt32());
        return hwc_display_[HWC_DISPLAY_PRIMARY]->Perform(
            HWCDisplayPrimary::SET_BINDER_DYN_REFRESH_RATE,
            refresh_rate);
      }
    default:
      DLOGW("Invalid operation %d", operation);
      return -EINVAL;
  }

  return 0;
}

android::status_t HWCSession::SetDisplayMode(const android::Parcel *input_parcel) {
  uint32_t mode = UINT32(input_parcel->readInt32());
  return hwc_display_[HWC_DISPLAY_PRIMARY]->Perform(HWCDisplayPrimary::SET_DISPLAY_MODE, mode);
}

android::status_t HWCSession::SetMaxMixerStages(const android::Parcel *input_parcel) {
  DisplayError error = kErrorNone;
  std::bitset<32> bit_mask_display_type = UINT32(input_parcel->readInt32());
  uint32_t max_mixer_stages = UINT32(input_parcel->readInt32());

  if (bit_mask_display_type[HWC_DISPLAY_PRIMARY]) {
    if (hwc_display_[HWC_DISPLAY_PRIMARY]) {
      error = hwc_display_[HWC_DISPLAY_PRIMARY]->SetMaxMixerStages(max_mixer_stages);
      if (error != kErrorNone) {
        return -EINVAL;
      }
    }
  }

  if (bit_mask_display_type[HWC_DISPLAY_EXTERNAL]) {
    if (hwc_display_[HWC_DISPLAY_EXTERNAL]) {
      error = hwc_display_[HWC_DISPLAY_EXTERNAL]->SetMaxMixerStages(max_mixer_stages);
      if (error != kErrorNone) {
        return -EINVAL;
      }
    }
  }

  if (bit_mask_display_type[HWC_DISPLAY_VIRTUAL]) {
    if (hwc_display_[HWC_DISPLAY_VIRTUAL]) {
      error = hwc_display_[HWC_DISPLAY_VIRTUAL]->SetMaxMixerStages(max_mixer_stages);
      if (error != kErrorNone) {
        return -EINVAL;
      }
    }
  }

  return 0;
}

android::status_t HWCSession::SetDynamicBWForCamera(const android::Parcel *input_parcel,
                                                    android::Parcel *output_parcel) {
  DisplayError error = kErrorNone;
  uint32_t camera_status = UINT32(input_parcel->readInt32());
  HWBwModes mode = camera_status > 0 ? kBwCamera : kBwDefault;

  // trigger invalidate to apply new bw caps.
  hwc_procs_->invalidate(hwc_procs_);

    error = core_intf_->SetMaxBandwidthMode(mode);
  if (error != kErrorNone) {
      return -EINVAL;
  }

  new_bw_mode_ = true;
  need_invalidate_ = true;

  return 0;
}

android::status_t HWCSession::GetBWTransactionStatus(const android::Parcel *input_parcel,
                                                     android::Parcel *output_parcel)  {
  bool state = true;

  if (hwc_display_[HWC_DISPLAY_PRIMARY]) {
    if (sync_wait(bw_mode_release_fd_, 0) < 0) {
      DLOGI("bw_transaction_release_fd is not yet signalled: err= %s", strerror(errno));
      state = false;
    }
    output_parcel->writeInt32(state);
  }

  return 0;
}

void HWCSession::SetFrameDumpConfig(const android::Parcel *input_parcel) {
  uint32_t frame_dump_count = UINT32(input_parcel->readInt32());
  std::bitset<32> bit_mask_display_type = UINT32(input_parcel->readInt32());
  uint32_t bit_mask_layer_type = UINT32(input_parcel->readInt32());

  if (bit_mask_display_type[HWC_DISPLAY_PRIMARY]) {
    if (hwc_display_[HWC_DISPLAY_PRIMARY]) {
      hwc_display_[HWC_DISPLAY_PRIMARY]->SetFrameDumpConfig(frame_dump_count, bit_mask_layer_type);
    }
  }

  if (bit_mask_display_type[HWC_DISPLAY_EXTERNAL]) {
    if (hwc_display_[HWC_DISPLAY_EXTERNAL]) {
      hwc_display_[HWC_DISPLAY_EXTERNAL]->SetFrameDumpConfig(frame_dump_count, bit_mask_layer_type);
    }
  }

  if (bit_mask_display_type[HWC_DISPLAY_VIRTUAL]) {
    if (hwc_display_[HWC_DISPLAY_VIRTUAL]) {
      hwc_display_[HWC_DISPLAY_VIRTUAL]->SetFrameDumpConfig(frame_dump_count, bit_mask_layer_type);
    }
  }
}

android::status_t HWCSession::SetMixerResolution(const android::Parcel *input_parcel) {
  DisplayError error = kErrorNone;
  uint32_t dpy = UINT32(input_parcel->readInt32());

  if (dpy != HWC_DISPLAY_PRIMARY) {
    DLOGI("Resoulution change not supported for this display %d", dpy);
    return -EINVAL;
  }

  if (!hwc_display_[HWC_DISPLAY_PRIMARY]) {
    DLOGI("Primary display is not initialized");
    return -EINVAL;
  }

  uint32_t width = UINT32(input_parcel->readInt32());
  uint32_t height = UINT32(input_parcel->readInt32());

  error = hwc_display_[HWC_DISPLAY_PRIMARY]->SetMixerResolution(width, height);
  if (error != kErrorNone) {
    return -EINVAL;
  }

  return 0;
}

android::status_t HWCSession::GetHdrCapabilities(const android::Parcel *input_parcel,
                                                 android::Parcel *output_parcel) {
  uint32_t display_id = UINT32(input_parcel->readInt32());
  if (display_id >= HWC_NUM_DISPLAY_TYPES) {
    DLOGE("Invalid display id = %d", display_id);
    return -EINVAL;
  }

  if (hwc_display_[display_id] == NULL) {
    DLOGW("Display = %d not initialized", display_id);
    return -EINVAL;
  }

  DisplayConfigFixedInfo fixed_info = {};
  int ret = hwc_display_[display_id]->GetDisplayFixedConfig(&fixed_info);
  if (ret) {
    DLOGE("Failed");
    return ret;
  }

  if (!fixed_info.hdr_supported) {
    DLOGI("HDR is not supported");
    return 0;
  }

  std::vector<int32_t> supported_hdr_types;
  // Only HDR10 supported now, in future add other supported HDR formats(HLG, DolbyVision)
  supported_hdr_types.push_back(HAL_HDR_HDR10);

  static const float kLuminanceFactor = 10000.0;
  // luminance is expressed in the unit of 0.0001 cd/m2, convert it to 1cd/m2.
  float max_luminance = FLOAT(fixed_info.max_luminance)/kLuminanceFactor;
  float max_average_luminance = FLOAT(fixed_info.average_luminance)/kLuminanceFactor;
  float min_luminance = FLOAT(fixed_info.min_luminance)/kLuminanceFactor;

  if (output_parcel != nullptr) {
    output_parcel->writeInt32Vector(supported_hdr_types);
    output_parcel->writeFloat(max_luminance);
    output_parcel->writeFloat(max_average_luminance);
    output_parcel->writeFloat(min_luminance);
  }

  return 0;
}

void HWCSession::DynamicDebug(const android::Parcel *input_parcel) {
  int type = input_parcel->readInt32();
  bool enable = (input_parcel->readInt32() > 0);
  DLOGI("type = %d enable = %d", type, enable);
  int verbose_level = input_parcel->readInt32();

  switch (type) {
  case qService::IQService::DEBUG_ALL:
    HWCDebugHandler::DebugAll(enable, verbose_level);
    break;

  case qService::IQService::DEBUG_MDPCOMP:
    HWCDebugHandler::DebugStrategy(enable, verbose_level);
    HWCDebugHandler::DebugCompManager(enable, verbose_level);
    break;

  case qService::IQService::DEBUG_PIPE_LIFECYCLE:
    HWCDebugHandler::DebugResources(enable, verbose_level);
    break;

  case qService::IQService::DEBUG_DRIVER_CONFIG:
    HWCDebugHandler::DebugDriverConfig(enable, verbose_level);
    break;

  case qService::IQService::DEBUG_ROTATOR:
    HWCDebugHandler::DebugResources(enable, verbose_level);
    HWCDebugHandler::DebugDriverConfig(enable, verbose_level);
    HWCDebugHandler::DebugRotator(enable, verbose_level);
    break;

  case qService::IQService::DEBUG_QDCM:
    HWCDebugHandler::DebugQdcm(enable, verbose_level);
    break;

  default:
    DLOGW("type = %d is not supported", type);
  }
}

android::status_t HWCSession::QdcmCMDHandler(const android::Parcel *input_parcel,
                                             android::Parcel *output_parcel) {
  int ret = 0;
  int32_t *brightness_value = NULL;
  uint32_t display_id(0);
  PPPendingParams pending_action;
  PPDisplayAPIPayload resp_payload, req_payload;

  if (!color_mgr_) {
    return -1;
  }

  pending_action.action = kNoAction;
  pending_action.params = NULL;

  // Read display_id, payload_size and payload from in_parcel.
  ret = HWCColorManager::CreatePayloadFromParcel(*input_parcel, &display_id, &req_payload);
  if (!ret) {
    if (HWC_DISPLAY_PRIMARY == display_id && hwc_display_[HWC_DISPLAY_PRIMARY])
      ret = hwc_display_[HWC_DISPLAY_PRIMARY]->ColorSVCRequestRoute(req_payload,
                                                                  &resp_payload, &pending_action);

    if (HWC_DISPLAY_EXTERNAL == display_id && hwc_display_[HWC_DISPLAY_EXTERNAL])
      ret = hwc_display_[HWC_DISPLAY_EXTERNAL]->ColorSVCRequestRoute(req_payload, &resp_payload,
                                                                  &pending_action);
  }

  if (ret) {
    output_parcel->writeInt32(ret);  // first field in out parcel indicates return code.
    req_payload.DestroyPayload();
    resp_payload.DestroyPayload();
    return ret;
  }

  switch (pending_action.action) {
    case kInvalidating:
      hwc_procs_->invalidate(hwc_procs_);
      break;
    case kEnterQDCMMode:
      ret = color_mgr_->EnableQDCMMode(true, hwc_display_[HWC_DISPLAY_PRIMARY]);
      break;
    case kExitQDCMMode:
      ret = color_mgr_->EnableQDCMMode(false, hwc_display_[HWC_DISPLAY_PRIMARY]);
      break;
    case kApplySolidFill:
      ret = color_mgr_->SetSolidFill(pending_action.params,
                                     true, hwc_display_[HWC_DISPLAY_PRIMARY]);
      hwc_procs_->invalidate(hwc_procs_);
      break;
    case kDisableSolidFill:
      ret = color_mgr_->SetSolidFill(pending_action.params,
                                     false, hwc_display_[HWC_DISPLAY_PRIMARY]);
      hwc_procs_->invalidate(hwc_procs_);
      break;
    case kSetPanelBrightness:
      brightness_value = reinterpret_cast<int32_t*>(resp_payload.payload);
      if (brightness_value == NULL) {
        DLOGE("Brightness value is Null");
        return -EINVAL;
      }
      if (HWC_DISPLAY_PRIMARY == display_id)
        ret = hwc_display_[HWC_DISPLAY_PRIMARY]->SetPanelBrightness(*brightness_value);
      break;
    case kEnableFrameCapture:
      ret = color_mgr_->SetFrameCapture(pending_action.params,
                                        true, hwc_display_[HWC_DISPLAY_PRIMARY]);
      hwc_procs_->invalidate(hwc_procs_);
      break;
    case kDisableFrameCapture:
      ret = color_mgr_->SetFrameCapture(pending_action.params,
                                        false, hwc_display_[HWC_DISPLAY_PRIMARY]);
      break;
    case kConfigureDetailedEnhancer:
      ret = color_mgr_->SetDetailedEnhancer(pending_action.params,
                                            hwc_display_[HWC_DISPLAY_PRIMARY]);
      hwc_procs_->invalidate(hwc_procs_);
      break;
    case kInvalidatingAndkSetPanelBrightness:
      brightness_value = reinterpret_cast<int32_t*>(resp_payload.payload);
      if (brightness_value == NULL) {
        DLOGE("Brightness value is Null");
        return -EINVAL;
      }
      if (HWC_DISPLAY_PRIMARY == display_id)
        ret = hwc_display_[HWC_DISPLAY_PRIMARY]->CachePanelBrightness(*brightness_value);
      hwc_procs_->invalidate(hwc_procs_);
      break;
    case kNoAction:
      break;
    default:
      DLOGW("Invalid pending action = %d!", pending_action.action);
      break;
  }

  // for display API getter case, marshall returned params into out_parcel.
  output_parcel->writeInt32(ret);
  HWCColorManager::MarshallStructIntoParcel(resp_payload, output_parcel);
  req_payload.DestroyPayload();
  resp_payload.DestroyPayload();

  return (ret? -EINVAL : 0);
}

android::status_t HWCSession::OnMinHdcpEncryptionLevelChange(const android::Parcel *input_parcel,
                                                             android::Parcel *output_parcel) {
  int ret = -EINVAL;
  uint32_t display_id = UINT32(input_parcel->readInt32());
  uint32_t min_enc_level = UINT32(input_parcel->readInt32());

  DLOGI("Display %d", display_id);

  if (display_id >= HWC_NUM_DISPLAY_TYPES) {
    DLOGE("Invalid display_id");
  } else if (display_id != HWC_DISPLAY_EXTERNAL) {
    DLOGE("Not supported for display");
  } else if (!hwc_display_[display_id]) {
    DLOGW("Display is not connected");
  } else {
    ret = hwc_display_[display_id]->OnMinHdcpEncryptionLevelChange(min_enc_level);
  }

  output_parcel->writeInt32(ret);

  return ret;
}

void* HWCSession::HWCUeventThread(void *context) {
  if (context) {
    return reinterpret_cast<HWCSession *>(context)->HWCUeventThreadHandler();
  }

  return NULL;
}

void* HWCSession::HWCUeventThreadHandler() {
  static char uevent_data[PAGE_SIZE];
  int length = 0;

  uevent_locker_.Lock();
  prctl(PR_SET_NAME, uevent_thread_name_, 0, 0, 0);
  setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);
  if (!uevent_init()) {
    DLOGE("Failed to init uevent");
    pthread_exit(0);
    uevent_locker_.Signal();
    uevent_locker_.Unlock();
    return NULL;
  }

  uevent_locker_.Signal();
  uevent_locker_.Unlock();

  while (!uevent_thread_exit_) {
    // keep last 2 zeroes to ensure double 0 termination
    length = uevent_next_event(uevent_data, INT32(sizeof(uevent_data)) - 2);

    if (strcasestr(HWC_UEVENT_SWITCH_HDMI, uevent_data)) {
      DLOGI("Uevent HDMI = %s", uevent_data);
      int connected = GetEventValue(uevent_data, length, "SWITCH_STATE=");
      if (connected >= 0) {
        DLOGI("HDMI = %s", connected ? "connected" : "disconnected");
        if (HotPlugHandler(connected) == -1) {
          DLOGE("Failed handling Hotplug = %s", connected ? "connected" : "disconnected");
        }
      }
    } else if (strcasestr(HWC_UEVENT_GRAPHICS_FB0, uevent_data)) {
      DLOGI("Uevent FB0 = %s", uevent_data);
      int panel_reset = GetEventValue(uevent_data, length, "PANEL_ALIVE=");
      if (panel_reset == 0) {
        if (hwc_procs_) {
          reset_panel_ = true;
          hwc_procs_->invalidate(hwc_procs_);
        } else {
          DLOGW("Ignore resetpanel - hwc_proc not registered");
        }
      }
    }
  }
  pthread_exit(0);

  return NULL;
}

int HWCSession::GetEventValue(const char *uevent_data, int length, const char *event_info) {
  const char *iterator_str = uevent_data;
  while (((iterator_str - uevent_data) <= length) && (*iterator_str)) {
    const char *pstr = strstr(iterator_str, event_info);
    if (pstr != NULL) {
      return (atoi(iterator_str + strlen(event_info)));
    }
    iterator_str += strlen(iterator_str) + 1;
  }

  return -1;
}

void HWCSession::ResetPanel() {
  int status = -EINVAL;

  DLOGI("Powering off primary");
  status = hwc_display_[HWC_DISPLAY_PRIMARY]->SetPowerMode(HWC_POWER_MODE_OFF);
  if (status) {
    DLOGE("power-off on primary failed with error = %d", status);
  }

  DLOGI("Restoring power mode on primary");
  int32_t mode = INT(hwc_display_[HWC_DISPLAY_PRIMARY]->GetLastPowerMode());
  status = hwc_display_[HWC_DISPLAY_PRIMARY]->SetPowerMode(mode);
  if (status) {
    DLOGE("Setting power mode = %d on primary failed with error = %d", mode, status);
  }

  status = hwc_display_[HWC_DISPLAY_PRIMARY]->EventControl(HWC_EVENT_VSYNC, 1);
  if (status) {
    DLOGE("enabling vsync failed for primary with error = %d", status);
  }

  reset_panel_ = false;
}

int HWCSession::HotPlugHandler(bool connected) {
  int status = 0;
  bool notify_hotplug = false;
  bool refresh_screen = false;

  // To prevent sending events to client while a lock is held, acquire scope locks only within
  // below scope so that those get automatically unlocked after the scope ends.
  {
    SEQUENCE_WAIT_SCOPE_LOCK(locker_);

    if (!hwc_display_[HWC_DISPLAY_PRIMARY]) {
      DLOGE("Primay display is not connected.");
      return -1;
    }


    HWCDisplay *primary_display = hwc_display_[HWC_DISPLAY_PRIMARY];
    HWCDisplay *external_display = NULL;
    HWCDisplay *null_display = NULL;

    if (primary_display->GetDisplayClass() == DISPLAY_CLASS_EXTERNAL) {
      external_display = static_cast<HWCDisplayExternal *>(hwc_display_[HWC_DISPLAY_PRIMARY]);
    } else if (primary_display->GetDisplayClass() == DISPLAY_CLASS_NULL) {
      null_display = static_cast<HWCDisplayNull *>(hwc_display_[HWC_DISPLAY_PRIMARY]);
    }

    // If primary display connected is a NULL display, then replace it with the external display
    if (connected) {
      // If we are in HDMI as primary and the primary display just got plugged in
      if (is_hdmi_primary_ && null_display) {
        uint32_t primary_width, primary_height;
        int status = 0;
        null_display->GetFrameBufferResolution(&primary_width, &primary_height);
        delete null_display;
        hwc_display_[HWC_DISPLAY_PRIMARY] = NULL;

        // Create external display with a forced framebuffer resolution to that of what the NULL
        // display had. This is necessary because SurfaceFlinger does not dynamically update
        // framebuffer resolution once it reads it at bootup. So we always have to have the NULL
        // display/external display both at the bootup resolution.
        status = CreateExternalDisplay(HWC_DISPLAY_PRIMARY, primary_width, primary_height, true);
        if (status) {
          DLOGE("Could not create external display");
          return -1;
        }

        status = hwc_display_[HWC_DISPLAY_PRIMARY]->SetPowerMode(HWC_POWER_MODE_NORMAL);
        if (status) {
          DLOGE("power-on on primary failed with error = %d", status);
        }

        is_hdmi_yuv_ = IsDisplayYUV(HWC_DISPLAY_PRIMARY);

        // Next, go ahead and enable vsync on external display. This is expliclity required
        // because in HDMI as primary case, SurfaceFlinger may not be aware of underlying
        // changing display. and thus may not explicitly enable vsync

        status = hwc_display_[HWC_DISPLAY_PRIMARY]->EventControl(HWC_EVENT_VSYNC, true);
        if (status) {
          DLOGE("Error enabling vsync for HDMI as primary case");
        }
        // Don't do hotplug notification for HDMI as primary case for now
        notify_hotplug = false;
        refresh_screen = true;
      } else {
        if (hwc_display_[HWC_DISPLAY_EXTERNAL]) {
          DLOGE("HDMI is already connected");
          return -1;
        }

        // Connect external display if virtual display is not connected.
        // Else, defer external display connection and process it when virtual display
        // tears down; Do not notify SurfaceFlinger since connection is deferred now.
        if (!hwc_display_[HWC_DISPLAY_VIRTUAL]) {
          status = ConnectDisplay(HWC_DISPLAY_EXTERNAL, NULL);
          if (status) {
            return status;
          }
          notify_hotplug = true;
        } else {
          DLOGI("Virtual display is connected, pending connection");
          external_pending_connect_ = true;
        }
      }
    } else {
      // Do not return error if external display is not in connected status.
      // Due to virtual display concurrency, external display connection might be still pending
      // but hdmi got disconnected before pending connection could be processed.

      if (is_hdmi_primary_ && external_display) {
        uint32_t x_res, y_res;
        external_display->GetFrameBufferResolution(&x_res, &y_res);
        // Need to manually disable VSYNC as SF is not aware of connect/disconnect cases
        // for HDMI as primary
        external_display->EventControl(HWC_EVENT_VSYNC, false);
        HWCDisplayExternal::Destroy(external_display);

        HWCDisplayNull *null_display;

        int status = HWCDisplayNull::Create(core_intf_, &hwc_procs_,
                                            reinterpret_cast<HWCDisplay **>(&null_display));

        if (status) {
          DLOGE("Could not create Null display when primary got disconnected");
          return -1;
        }

        null_display->SetResolution(x_res, y_res);
        hwc_display_[HWC_DISPLAY_PRIMARY] = null_display;

        // Don't do hotplug notification for HDMI as primary case for now
        notify_hotplug = false;
      } else {
        if (hwc_display_[HWC_DISPLAY_EXTERNAL]) {
          status = DisconnectDisplay(HWC_DISPLAY_EXTERNAL);
          notify_hotplug = true;
        }
        external_pending_connect_ = false;
      }
    }
  }

  if (connected && (notify_hotplug || refresh_screen)) {
    // trigger screen refresh to ensure sufficient resources are available to process new
    // new display connection.
    hwc_procs_->invalidate(hwc_procs_);
    uint32_t vsync_period = UINT32(GetVsyncPeriod(HWC_DISPLAY_PRIMARY));
    usleep(vsync_period * 2 / 1000);
  }
  // notify client
  if (notify_hotplug) {
    hwc_procs_->hotplug(hwc_procs_, HWC_DISPLAY_EXTERNAL, connected);
  }

  qservice_->onHdmiHotplug(INT(connected));

  return 0;
}

void HWCSession::HandleSecureDisplaySession(hwc_display_contents_1_t **displays) {
  secure_display_active_ = false;
  if (!*displays) {
    DLOGW("Invalid display contents");
    return;
  }

  hwc_display_contents_1_t *content_list = displays[HWC_DISPLAY_PRIMARY];
  if (!content_list) {
    DLOGW("Invalid primary content list");
    return;
  }
  size_t num_hw_layers = content_list->numHwLayers;

  for (size_t i = 0; i < num_hw_layers - 1; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    const private_handle_t *pvt_handle = static_cast<const private_handle_t *>(hwc_layer.handle);
    if (pvt_handle && pvt_handle->flags & private_handle_t::PRIV_FLAGS_SECURE_DISPLAY) {
      secure_display_active_ = true;
    }
  }

  // Force flush on primary during transitions(secure<->non secure)
  // when external displays are connected.
  bool force_flush = false;
  if ((connected_displays_[HWC_DISPLAY_PRIMARY] == 1) &&
     ((connected_displays_[HWC_DISPLAY_EXTERNAL] == 1) ||
      (connected_displays_[HWC_DISPLAY_VIRTUAL] == 1))) {
    force_flush = true;
  }

  for (ssize_t dpy = static_cast<ssize_t>(HWC_NUM_DISPLAY_TYPES - 1); dpy >= 0; dpy--) {
    if (hwc_display_[dpy]) {
      hwc_display_[dpy]->SetSecureDisplay(secure_display_active_, force_flush);
    }
  }
}

android::status_t HWCSession::GetVisibleDisplayRect(const android::Parcel *input_parcel,
                                                    android::Parcel *output_parcel) {
  int dpy = input_parcel->readInt32();

  if (dpy < HWC_DISPLAY_PRIMARY || dpy >= HWC_NUM_DISPLAY_TYPES) {
    return android::BAD_VALUE;;
  }

  if (!hwc_display_[dpy]) {
    return android::NO_INIT;
  }

  hwc_rect_t visible_rect = {0, 0, 0, 0};
  int error = hwc_display_[dpy]->GetVisibleDisplayRect(&visible_rect);
  if (error < 0) {
    return error;
  }

  output_parcel->writeInt32(visible_rect.left);
  output_parcel->writeInt32(visible_rect.top);
  output_parcel->writeInt32(visible_rect.right);
  output_parcel->writeInt32(visible_rect.bottom);

  return android::NO_ERROR;
}

int HWCSession::CreateExternalDisplay(int disp, uint32_t primary_width, uint32_t primary_height,
                                      bool use_primary_res) {
  uint32_t panel_bpp = 0;
  uint32_t pattern_type = 0;

  if (qdutils::isDPConnected()) {
    qdutils::getDPTestConfig(&panel_bpp, &pattern_type);
  }

  if (panel_bpp && pattern_type) {
    return HWCDisplayExternalTest::Create(core_intf_, &hwc_procs_, qservice_, panel_bpp,
                                          pattern_type, &hwc_display_[disp]);
  }

  return HWCDisplayExternal::Create(core_intf_, &hwc_procs_, primary_width, primary_height,
                                    qservice_, use_primary_res, &hwc_display_[disp]);
}

}  // namespace sdm

