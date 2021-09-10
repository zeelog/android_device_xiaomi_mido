/*
* Copyright (c) 2015 - 2017, The Linux Foundation. All rights reserved.
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

#include <dlfcn.h>
#include <cutils/sockets.h>
#include <cutils/native_handle.h>
#include <utils/String16.h>
#include <binder/Parcel.h>
#include <gralloc_priv.h>
#include <hardware/hwcomposer.h>
#include <hardware/hwcomposer_defs.h>
#include <QService.h>

#include <utils/constants.h>
#include <utils/debug.h>
#include <core/buffer_allocator.h>
#include <private/color_params.h>
#include "hwc_buffer_allocator.h"
#include "hwc_buffer_sync_handler.h"
#include "hwc_session.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCColorManager"

namespace sdm {

uint32_t HWCColorManager::Get8BitsARGBColorValue(const PPColorFillParams &params) {
  uint32_t argb_color = ((params.color.r << 16) & 0xff0000) | ((params.color.g << 8) & 0xff00) |
                        ((params.color.b) & 0xff);
  return argb_color;
}

int HWCColorManager::CreatePayloadFromParcel(const android::Parcel &in, uint32_t *disp_id,
                                             PPDisplayAPIPayload *sink) {
  int ret = 0;
  uint32_t id(0);
  uint32_t size(0);

  id = UINT32(in.readInt32());
  size = UINT32(in.readInt32());
  if (size > 0 && size == in.dataAvail()) {
    const void *data = in.readInplace(size);
    const uint8_t *temp = reinterpret_cast<const uint8_t *>(data);

    sink->size = size;
    sink->payload = const_cast<uint8_t *>(temp);
    *disp_id = id;
  } else {
    DLOGW("Failing size checking, size = %d", size);
    ret = -EINVAL;
  }

  return ret;
}

void HWCColorManager::MarshallStructIntoParcel(const PPDisplayAPIPayload &data,
                                               android::Parcel *out_parcel) {
  out_parcel->writeInt32(INT32(data.size));
  if (data.payload)
    out_parcel->write(data.payload, data.size);
}

HWCColorManager *HWCColorManager::CreateColorManager(HWCBufferAllocator * buffer_allocator) {
  HWCColorManager *color_mgr = new HWCColorManager(buffer_allocator);

  if (color_mgr) {
    // Load display API interface library. And retrieve color API function tables.
    DynLib &color_apis_lib = color_mgr->color_apis_lib_;
    if (color_apis_lib.Open(DISPLAY_API_INTERFACE_LIBRARY_NAME)) {
      if (!color_apis_lib.Sym(DISPLAY_API_FUNC_TABLES, &color_mgr->color_apis_)) {
        DLOGE("Fail to retrieve = %s from %s", DISPLAY_API_FUNC_TABLES,
              DISPLAY_API_INTERFACE_LIBRARY_NAME);
        delete color_mgr;
        return NULL;
      }
    } else {
      DLOGW("Unable to load = %s", DISPLAY_API_INTERFACE_LIBRARY_NAME);
      delete color_mgr;
      return NULL;
    }
    DLOGI("Successfully loaded %s", DISPLAY_API_INTERFACE_LIBRARY_NAME);

    // Load diagclient library and invokes its entry point to pass in display APIs.
    DynLib &diag_client_lib = color_mgr->diag_client_lib_;
    if (diag_client_lib.Open(QDCM_DIAG_CLIENT_LIBRARY_NAME)) {
      if (!diag_client_lib.Sym(INIT_QDCM_DIAG_CLIENT_NAME,
                               reinterpret_cast<void **>(&color_mgr->qdcm_diag_init_)) ||
        !diag_client_lib.Sym(DEINIT_QDCM_DIAG_CLIENT_NAME,
                               reinterpret_cast<void **>(&color_mgr->qdcm_diag_deinit_))) {
        DLOGE("Fail to retrieve = %s from %s", INIT_QDCM_DIAG_CLIENT_NAME,
              QDCM_DIAG_CLIENT_LIBRARY_NAME);
      } else {
        // invoke Diag Client entry point to initialize.
        color_mgr->qdcm_diag_init_(color_mgr->color_apis_);
        DLOGI("Successfully loaded %s and %s and diag_init'ed", DISPLAY_API_INTERFACE_LIBRARY_NAME,
              QDCM_DIAG_CLIENT_LIBRARY_NAME);
      }
    } else {
      DLOGW("Unable to load = %s", QDCM_DIAG_CLIENT_LIBRARY_NAME);
      // only QDCM Diag client failed to be loaded and system still should function.
    }
  } else {
    DLOGE("Unable to create HWCColorManager");
    return NULL;
  }

  return color_mgr;
}

HWCColorManager::HWCColorManager(HWCBufferAllocator *buffer_allocator) :
    buffer_allocator_(buffer_allocator) {
}

HWCColorManager::~HWCColorManager() {
}

void HWCColorManager::DestroyColorManager() {
  if (qdcm_mode_mgr_) {
    delete qdcm_mode_mgr_;
  }
  if (qdcm_diag_deinit_) {
    qdcm_diag_deinit_();
  }
  delete this;
}

int HWCColorManager::EnableQDCMMode(bool enable, HWCDisplay *hwc_display) {
  int ret = 0;

  if (!qdcm_mode_mgr_) {
    qdcm_mode_mgr_ = HWCQDCMModeManager::CreateQDCMModeMgr();
    if (!qdcm_mode_mgr_) {
      DLOGE("Unable to create QDCM operating mode manager.");
      ret = -EFAULT;
    }
  }

  if (qdcm_mode_mgr_) {
    ret = qdcm_mode_mgr_->EnableQDCMMode(enable, hwc_display);
  }

  return ret;
}

int HWCColorManager::SetSolidFill(const void *params, bool enable, HWCDisplay *hwc_display) {
  SCOPE_LOCK(locker_);

  if (params) {
    solid_fill_params_ = *reinterpret_cast<const PPColorFillParams *>(params);
  } else {
    solid_fill_params_ = PPColorFillParams();
  }

  uint32_t solid_fill_color = Get8BitsARGBColorValue(solid_fill_params_);
  if (enable) {
    LayerRect solid_fill_rect = {
      FLOAT(solid_fill_params_.rect.x), FLOAT(solid_fill_params_.rect.y),
      FLOAT(solid_fill_params_.rect.x) + FLOAT(solid_fill_params_.rect.width),
      FLOAT(solid_fill_params_.rect.y) + FLOAT(solid_fill_params_.rect.height),
    };

    hwc_display->Perform(HWCDisplayPrimary::SET_QDCM_SOLID_FILL_INFO, solid_fill_color);
    hwc_display->Perform(HWCDisplayPrimary::SET_QDCM_SOLID_FILL_RECT, &solid_fill_rect);
  } else {
    hwc_display->Perform(HWCDisplayPrimary::UNSET_QDCM_SOLID_FILL_INFO, 0);
  }

  return 0;
}

int HWCColorManager::SetFrameCapture(void *params, bool enable, HWCDisplay *hwc_display) {
  SCOPE_LOCK(locker_);
  int ret = 0;

  PPFrameCaptureData *frame_capture_data = reinterpret_cast<PPFrameCaptureData *>(params);

  if (enable) {
    std::memset(&buffer_info, 0x00, sizeof(buffer_info));
    hwc_display->GetPanelResolution(&buffer_info.buffer_config.width,
                                    &buffer_info.buffer_config.height);
    if (frame_capture_data->input_params.out_pix_format == PP_PIXEL_FORMAT_RGB_888) {
      buffer_info.buffer_config.format = kFormatRGB888;
    } else if (frame_capture_data->input_params.out_pix_format == PP_PIXEL_FORMAT_RGB_2101010) {
      buffer_info.buffer_config.format = kFormatRGBA1010102;
    } else {
      DLOGE("Pixel-format: %d NOT support.", frame_capture_data->input_params.out_pix_format);
      return -EFAULT;
    }

    buffer_info.buffer_config.buffer_count = 1;
    buffer_info.alloc_buffer_info.fd = -1;
    buffer_info.alloc_buffer_info.stride = 0;
    buffer_info.alloc_buffer_info.size = 0;

    ret = buffer_allocator_->AllocateBuffer(&buffer_info);
    if (ret != 0) {
      DLOGE("Buffer allocation failed. ret: %d", ret);
      return -ENOMEM;
    } else {
      void *buffer = mmap(NULL, buffer_info.alloc_buffer_info.size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, buffer_info.alloc_buffer_info.fd, 0);

      if (buffer == MAP_FAILED) {
        DLOGE("mmap failed. err = %d", errno);
        frame_capture_data->buffer = NULL;
        ret = buffer_allocator_->FreeBuffer(&buffer_info);
        return -EFAULT;
      } else {
        frame_capture_data->buffer = reinterpret_cast<uint8_t *>(buffer);
        frame_capture_data->buffer_stride = buffer_info.alloc_buffer_info.stride;
        frame_capture_data->buffer_size = buffer_info.alloc_buffer_info.size;
      }
      ret = hwc_display->FrameCaptureAsync(buffer_info, 1);
      if (ret < 0) {
        DLOGE("FrameCaptureAsync failed. ret = %d", ret);
      }
    }
  } else {
    ret = hwc_display->GetFrameCaptureStatus();
    if (!ret) {
      if (frame_capture_data->buffer != NULL) {
        if (munmap(frame_capture_data->buffer, buffer_info.alloc_buffer_info.size) != 0) {
          DLOGE("munmap failed. err = %d", errno);
        }
      }
      if (buffer_allocator_ != NULL) {
        std::memset(frame_capture_data, 0x00, sizeof(PPFrameCaptureData));
        ret = buffer_allocator_->FreeBuffer(&buffer_info);
        if (ret != 0) {
          DLOGE("FreeBuffer failed. ret = %d", ret);
        }
      }
    } else {
      DLOGE("GetFrameCaptureStatus failed. ret = %d", ret);
    }
  }
  return ret;
}

int HWCColorManager::SetHWDetailedEnhancerConfig(void *params, HWCDisplay *hwc_display) {
  int err = -1;
  DisplayDetailEnhancerData de_data;

  PPDETuningCfgData *de_tuning_cfg_data = reinterpret_cast<PPDETuningCfgData*>(params);
  if (de_tuning_cfg_data->cfg_pending == true) {
    if (!de_tuning_cfg_data->cfg_en) {
      de_data.enable = 0;
    } else {
      de_data.override_flags = kOverrideDEEnable;
      de_data.enable = 1;

      if (de_tuning_cfg_data->params.flags & kDeTuningFlagSharpFactor) {
        de_data.override_flags |= kOverrideDESharpen1;
        de_data.sharp_factor = de_tuning_cfg_data->params.sharp_factor;
      }

      if (de_tuning_cfg_data->params.flags & kDeTuningFlagClip) {
        de_data.override_flags |= kOverrideDEClip;
        de_data.clip = de_tuning_cfg_data->params.clip;
      }

      if (de_tuning_cfg_data->params.flags & kDeTuningFlagThrQuiet) {
        de_data.override_flags |= kOverrideDEThrQuiet;
        de_data.thr_quiet = de_tuning_cfg_data->params.thr_quiet;
      }

      if (de_tuning_cfg_data->params.flags & kDeTuningFlagThrDieout) {
        de_data.override_flags |= kOverrideDEThrDieout;
        de_data.thr_dieout = de_tuning_cfg_data->params.thr_dieout;
      }

      if (de_tuning_cfg_data->params.flags & kDeTuningFlagThrLow) {
        de_data.override_flags |= kOverrideDEThrLow;
        de_data.thr_low = de_tuning_cfg_data->params.thr_low;
      }

      if (de_tuning_cfg_data->params.flags & kDeTuningFlagThrHigh) {
        de_data.override_flags |= kOverrideDEThrHigh;
        de_data.thr_high = de_tuning_cfg_data->params.thr_high;
      }

      if (de_tuning_cfg_data->params.flags & kDeTuningFlagContentQualLevel) {
        switch (de_tuning_cfg_data->params.quality) {
          case kDeContentQualLow:
            de_data.quality_level = kContentQualityLow;
            break;
          case kDeContentQualMedium:
            de_data.quality_level = kContentQualityMedium;
            break;
          case kDeContentQualHigh:
            de_data.quality_level = kContentQualityHigh;
            break;
          case kDeContentQualUnknown:
          default:
            de_data.quality_level = kContentQualityUnknown;
            break;
        }
      }
    }
    err = hwc_display->SetDetailEnhancerConfig(de_data);
    if (err) {
      DLOGW("SetDetailEnhancerConfig failed. err = %d", err);
    }
    de_tuning_cfg_data->cfg_pending = false;
  }
  return err;
}

void HWCColorManager::SetColorModeDetailEnhancer(HWCDisplay *hwc_display) {
  SCOPE_LOCK(locker_);
  int err = -1;
  PPPendingParams pending_action;
  PPDisplayAPIPayload req_payload;

  pending_action.action = kGetDetailedEnhancerData;
  pending_action.params = NULL;

  if (hwc_display) {
    err = hwc_display->ColorSVCRequestRoute(req_payload, NULL, &pending_action);
    if (!err && pending_action.action == kConfigureDetailedEnhancer) {
      err = SetHWDetailedEnhancerConfig(pending_action.params, hwc_display);
    }
  }
  return;
}

int HWCColorManager::SetDetailedEnhancer(void *params, HWCDisplay *hwc_display) {
  SCOPE_LOCK(locker_);
  int err = -1;
  err = SetHWDetailedEnhancerConfig(params, hwc_display);
  return err;
}

const HWCQDCMModeManager::ActiveFeatureCMD HWCQDCMModeManager::kActiveFeatureCMD[] = {
    HWCQDCMModeManager::ActiveFeatureCMD("cabl:on", "cabl:off", "cabl:status", "running"),
    HWCQDCMModeManager::ActiveFeatureCMD("ad:on", "ad:off", "ad:query:status", "running"),
    HWCQDCMModeManager::ActiveFeatureCMD("svi:on", "svi:off", "svi:status", "running"),
};

const char *const HWCQDCMModeManager::kSocketName = "pps";
const char *const HWCQDCMModeManager::kTagName = "surfaceflinger";
const char *const HWCQDCMModeManager::kPackageName = "colormanager";

HWCQDCMModeManager *HWCQDCMModeManager::CreateQDCMModeMgr() {
  HWCQDCMModeManager *mode_mgr = new HWCQDCMModeManager();

  if (!mode_mgr) {
    DLOGW("No memory to create HWCQDCMModeManager.");
    return NULL;
  } else {
    mode_mgr->socket_fd_ =
        ::socket_local_client(kSocketName, ANDROID_SOCKET_NAMESPACE_RESERVED, SOCK_STREAM);
    if (mode_mgr->socket_fd_ < 0) {
      // it should not be disastrous and we still can grab wakelock in QDCM mode.
      DLOGW("Unable to connect to dpps socket!");
    }

    // retrieve system GPU idle timeout value for later to recover.
    mode_mgr->entry_timeout_ = UINT32(HWCDebugHandler::GetIdleTimeoutMs());
  }

  return mode_mgr;
}

HWCQDCMModeManager::~HWCQDCMModeManager() {
  if (socket_fd_ >= 0)
    ::close(socket_fd_);
}

int HWCQDCMModeManager::EnableActiveFeatures(bool enable,
                                             const HWCQDCMModeManager::ActiveFeatureCMD &cmds,
                                             bool *was_running) {
  int ret = 0;
  ssize_t size = 0;
  char response[kSocketCMDMaxLength] = {
      0,
  };

  if (socket_fd_ < 0) {
    DLOGW("No socket connection available!");
    return -EFAULT;
  }

  if (!enable) {  // if client requesting to disable it.
    // query CABL status, if off, no action. keep the status.
    size = ::write(socket_fd_, cmds.cmd_query_status, strlen(cmds.cmd_query_status));
    if (size < 0) {
      DLOGW("Unable to send data over socket %s", ::strerror(errno));
      ret = -EFAULT;
    } else {
      size = ::read(socket_fd_, response, kSocketCMDMaxLength);
      if (size < 0) {
        DLOGW("Unable to read data over socket %s", ::strerror(errno));
        ret = -EFAULT;
      } else if (!strncmp(response, cmds.running, strlen(cmds.running))) {
        *was_running = true;
      }
    }

    if (*was_running) {  // if was running, it's requested to disable it.
      size = ::write(socket_fd_, cmds.cmd_off, strlen(cmds.cmd_off));
      if (size < 0) {
        DLOGW("Unable to send data over socket %s", ::strerror(errno));
        ret = -EFAULT;
      }
    }
  } else {  // if was running, need enable it back.
    if (*was_running) {
      size = ::write(socket_fd_, cmds.cmd_on, strlen(cmds.cmd_on));
      if (size < 0) {
        DLOGW("Unable to send data over socket %s", ::strerror(errno));
        ret = -EFAULT;
      }
    }
  }

  return ret;
}

int HWCQDCMModeManager::EnableQDCMMode(bool enable, HWCDisplay *hwc_display) {
  int ret = 0;

  ret = EnableActiveFeatures((enable ? false : true), kActiveFeatureCMD[kCABLFeature],
                             &cabl_was_running_);

  // if enter QDCM mode, disable GPU fallback idle timeout.
  if (hwc_display) {
    uint32_t timeout = enable ? 0 : entry_timeout_;
    hwc_display->SetIdleTimeoutMs(timeout);
  }

  return ret;
}

} // namespace sdm
