/*
* Copyright (c) 2014 - 2017, The Linux Foundation. All rights reserved.
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

#include <cutils/properties.h>
#include <sync/sync.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <stdarg.h>
#include <sys/mman.h>

#include <gr.h>
#include "hwc_display_primary.h"
#include "hwc_debugger.h"

#define __CLASS__ "HWCDisplayPrimary"

namespace sdm {

int HWCDisplayPrimary::Create(CoreInterface *core_intf, BufferAllocator *buffer_allocator,
                              hwc_procs_t const **hwc_procs, qService::QService *qservice,
                              HWCDisplay **hwc_display) {
  int status = 0;
  uint32_t primary_width = 0;
  uint32_t primary_height = 0;

  HWCDisplay *hwc_display_primary = new HWCDisplayPrimary(core_intf, buffer_allocator,
                                                          hwc_procs, qservice);
  status = hwc_display_primary->Init();
  if (status) {
    delete hwc_display_primary;
    return status;
  }

  hwc_display_primary->GetMixerResolution(&primary_width, &primary_height);
  int width = 0, height = 0;
  HWCDebugHandler::Get()->GetProperty("sdm.fb_size_width", &width);
  HWCDebugHandler::Get()->GetProperty("sdm.fb_size_height", &height);
  if (width > 0 && height > 0) {
    primary_width = UINT32(width);
    primary_height = UINT32(height);
  }

  status = hwc_display_primary->SetFrameBufferResolution(primary_width, primary_height);
  if (status) {
    Destroy(hwc_display_primary);
    return status;
  }

  *hwc_display = hwc_display_primary;

  return status;
}

void HWCDisplayPrimary::Destroy(HWCDisplay *hwc_display) {
  hwc_display->Deinit();
  delete hwc_display;
}

HWCDisplayPrimary::HWCDisplayPrimary(CoreInterface *core_intf,
                                     BufferAllocator *buffer_allocator,
                                     hwc_procs_t const **hwc_procs,
                                     qService::QService *qservice)
  : HWCDisplay(core_intf, hwc_procs, kPrimary, HWC_DISPLAY_PRIMARY, true, qservice,
               DISPLAY_CLASS_PRIMARY), buffer_allocator_(buffer_allocator) {
}

int HWCDisplayPrimary::Init() {
  cpu_hint_.Init(static_cast<HWCDebugHandler*>(HWCDebugHandler::Get()));

  use_metadata_refresh_rate_ = true;
  int disable_metadata_dynfps = 0;
  HWCDebugHandler::Get()->GetProperty("persist.metadata_dynfps.disable", &disable_metadata_dynfps);
  if (disable_metadata_dynfps) {
    use_metadata_refresh_rate_ = false;
  }

  int status = HWCDisplay::Init();
  if (status) {
    return status;
  }
  color_mode_ = new HWCColorMode(display_intf_);
  color_mode_->Init();

  return status;
}

int HWCDisplayPrimary::Deinit() {
  color_mode_->DeInit();
  delete color_mode_;
  color_mode_ = NULL;

  return HWCDisplay::Deinit();
}


void HWCDisplayPrimary::ProcessBootAnimCompleted(hwc_display_contents_1_t *list) {
  uint32_t numBootUpLayers = 0;

  numBootUpLayers = static_cast<uint32_t>(Debug::GetBootAnimLayerCount());

  if (numBootUpLayers == 0) {
    numBootUpLayers = 2;
  }
  /* All other checks namely "init.svc.bootanim" or
  * HWC_GEOMETRY_CHANGED fail in correctly identifying the
  * exact bootup transition to homescreen
  */
  char cryptoState[PROPERTY_VALUE_MAX];
  char voldDecryptState[PROPERTY_VALUE_MAX];
  bool isEncrypted = false;
  bool main_class_services_started = false;
  if (property_get("ro.crypto.state", cryptoState, "unencrypted")) {
    if (!strcmp(cryptoState, "encrypted")) {
      isEncrypted = true;
      if (property_get("vold.decrypt", voldDecryptState, "") &&
            !strcmp(voldDecryptState, "trigger_restart_framework"))
        main_class_services_started = true;
    }
  }
  if ((!isEncrypted ||(isEncrypted && main_class_services_started)) &&
    (list->numHwLayers > numBootUpLayers)) {
    boot_animation_completed_ = true;
    // Applying default mode after bootanimation is finished And
    // If Data is Encrypted, it is ready for access.
    if (display_intf_)
      display_intf_->ApplyDefaultDisplayMode();
  }
}

int HWCDisplayPrimary::Prepare(hwc_display_contents_1_t *content_list) {
  int status = 0;
  DisplayError error = kErrorNone;

  if (!boot_animation_completed_)
    ProcessBootAnimCompleted(content_list);

  if (display_paused_) {
    MarkLayersForGPUBypass(content_list);
    return status;
  }

  status = AllocateLayerStack(content_list);
  if (status) {
    return status;
  }

  status = PrePrepareLayerStack(content_list);
  if (status) {
    return status;
  }

  bool pending_output_dump = dump_frame_count_ && dump_output_to_file_;

  if (frame_capture_buffer_queued_ || pending_output_dump) {
    // RHS values were set in FrameCaptureAsync() called from a binder thread. They are picked up
    // here in a subsequent draw round.
    layer_stack_.output_buffer = &output_buffer_;
    layer_stack_.flags.post_processed_output = post_processed_output_;
  }

  uint32_t num_updating_layers = GetUpdatingLayersCount(UINT32(content_list->numHwLayers - 1));
  bool one_updating_layer = (num_updating_layers == 1);

  if (num_updating_layers != 0) {
    ToggleCPUHint(one_updating_layer);
  }

  uint32_t refresh_rate = GetOptimalRefreshRate(one_updating_layer);
  // TODO(user): Need to read current refresh rate to avoid
  // redundant calls to set refresh rate during idle fall back.
  if ((current_refresh_rate_ != refresh_rate) || (handle_idle_timeout_)) {
    error = display_intf_->SetRefreshRate(refresh_rate);
  }

  if (error == kErrorNone) {
    // On success, set current refresh rate to new refresh rate
    current_refresh_rate_ = refresh_rate;
  }

  if (handle_idle_timeout_) {
    handle_idle_timeout_ = false;
  }

  if (content_list->numHwLayers <= 1) {
    flush_ = true;
  }

  status = PrepareLayerStack(content_list);
  if (status) {
    return status;
  }

  return 0;
}

int HWCDisplayPrimary::Commit(hwc_display_contents_1_t *content_list) {
  int status = 0;

  DisplayConfigFixedInfo display_config;
  display_intf_->GetConfig(&display_config);
  if (content_list->numHwLayers <= 1 && display_config.is_cmdmode) {
    DLOGV("Skipping null commit on cmd mode panel");
    flush_ = false;
    return 0;
  }

  if (display_paused_) {
    if (content_list->outbufAcquireFenceFd >= 0) {
      // If we do not handle the frame set retireFenceFd to outbufAcquireFenceFd,
      // which will make sure the framework waits on it and closes it.
      content_list->retireFenceFd = dup(content_list->outbufAcquireFenceFd);
      close(content_list->outbufAcquireFenceFd);
      content_list->outbufAcquireFenceFd = -1;
    }

    DisplayError error = display_intf_->Flush();
    if (error != kErrorNone) {
      DLOGE("Flush failed. Error = %d", error);
    }
    return status;
  }

  status = HWCDisplay::CommitLayerStack(content_list);
  if (status) {
    return status;
  }

  HandleFrameOutput();

  status = HWCDisplay::PostCommitLayerStack(content_list);
  if (status) {
    return status;
  }

  return 0;
}

int HWCDisplayPrimary::Perform(uint32_t operation, ...) {
  va_list args;
  va_start(args, operation);
  int val = va_arg(args, int32_t);
  va_end(args);
  switch (operation) {
    case SET_METADATA_DYN_REFRESH_RATE:
      SetMetaDataRefreshRateFlag(val);
      break;
    case SET_BINDER_DYN_REFRESH_RATE:
      ForceRefreshRate(UINT32(val));
      break;
    case SET_DISPLAY_MODE:
      SetDisplayMode(UINT32(val));
      break;
    case SET_QDCM_SOLID_FILL_INFO:
      SetQDCMSolidFillInfo(true, UINT32(val));
      break;
    case UNSET_QDCM_SOLID_FILL_INFO:
      SetQDCMSolidFillInfo(false, UINT32(val));
      break;
    default:
      DLOGW("Invalid operation %d", operation);
      return -EINVAL;
  }

  return 0;
}

DisplayError HWCDisplayPrimary::SetDisplayMode(uint32_t mode) {
  DisplayError error = kErrorNone;

  if (display_intf_) {
    error = display_intf_->SetDisplayMode(mode);
  }

  return error;
}

void HWCDisplayPrimary::SetMetaDataRefreshRateFlag(bool enable) {
  int disable_metadata_dynfps = 0;

  HWCDebugHandler::Get()->GetProperty("persist.metadata_dynfps.disable", &disable_metadata_dynfps);
  if (disable_metadata_dynfps) {
    return;
  }
  use_metadata_refresh_rate_ = enable;
}

void HWCDisplayPrimary::SetQDCMSolidFillInfo(bool enable, uint32_t color) {
  solid_fill_enable_ = enable;
  solid_fill_color_  = color;
}

void HWCDisplayPrimary::ToggleCPUHint(bool set) {
  if (set) {
    cpu_hint_.Set();
  } else {
    cpu_hint_.Reset();
  }
}

void HWCDisplayPrimary::SetSecureDisplay(bool secure_display_active, bool force_flush) {
  if (secure_display_active_ != secure_display_active) {
    // Skip Prepare and call Flush for null commit
    DLOGI("SecureDisplay state changed from %d to %d Needs Flush!!", secure_display_active_,
           secure_display_active);
    secure_display_active_ = secure_display_active;
    skip_prepare_cnt = 1;

    // Issue two null commits for command mode panels when external displays are connected.
    // Two null commits are required to handle non secure to secure transitions at 30fps.
    // TODO(user): Need two null commits on video mode also to handle transition cases of
    // primary at higher fps (ex60) and external at lower fps.

    // Avoid flush for command mode panels when no external displays are connected.
    // This is to avoid flicker/blink on primary during transitions.
    DisplayConfigFixedInfo display_config;
    display_intf_->GetConfig(&display_config);
    if (display_config.is_cmdmode) {
      if (force_flush) {
        DLOGI("Issue two null commits for command mode panels");
        skip_prepare_cnt = 2;
      } else {
        DLOGI("Avoid flush for command mode panel when no external displays are connected");
        skip_prepare_cnt = 0;
      }
    }
  }
}

void HWCDisplayPrimary::ForceRefreshRate(uint32_t refresh_rate) {
  if ((refresh_rate && (refresh_rate < min_refresh_rate_ || refresh_rate > max_refresh_rate_)) ||
       force_refresh_rate_ == refresh_rate) {
    // Cannot honor force refresh rate, as its beyond the range or new request is same
    return;
  }

  const hwc_procs_t *hwc_procs = *hwc_procs_;
  force_refresh_rate_ = refresh_rate;

  hwc_procs->invalidate(hwc_procs);

  return;
}

uint32_t HWCDisplayPrimary::GetOptimalRefreshRate(bool one_updating_layer) {
  if (force_refresh_rate_) {
    return force_refresh_rate_;
  } else if (handle_idle_timeout_) {
    return min_refresh_rate_;
  } else if (use_metadata_refresh_rate_ && one_updating_layer && metadata_refresh_rate_) {
    return metadata_refresh_rate_;
  }

  return max_refresh_rate_;
}

DisplayError HWCDisplayPrimary::Refresh() {
  const hwc_procs_t *hwc_procs = *hwc_procs_;
  DisplayError error = kErrorNone;

  if (!hwc_procs) {
    return kErrorParameters;
  }

  hwc_procs->invalidate(hwc_procs);
  handle_idle_timeout_ = true;

  return error;
}

void HWCDisplayPrimary::SetIdleTimeoutMs(uint32_t timeout_ms) {
  display_intf_->SetIdleTimeoutMs(timeout_ms);
}

static void SetLayerBuffer(const BufferInfo& output_buffer_info, LayerBuffer *output_buffer) {
  const BufferConfig& buffer_config = output_buffer_info.buffer_config;
  const AllocatedBufferInfo &alloc_buffer_info = output_buffer_info.alloc_buffer_info;

  output_buffer->width = alloc_buffer_info.aligned_width;
  output_buffer->height = alloc_buffer_info.aligned_height;
  output_buffer->unaligned_width = buffer_config.width;
  output_buffer->unaligned_height = buffer_config.height;
  output_buffer->format = buffer_config.format;
  output_buffer->planes[0].fd = alloc_buffer_info.fd;
  output_buffer->planes[0].stride = alloc_buffer_info.stride;
}

void HWCDisplayPrimary::HandleFrameOutput() {
  if (frame_capture_buffer_queued_) {
    HandleFrameCapture();
  } else if (dump_output_to_file_) {
    HandleFrameDump();
  }
}

void HWCDisplayPrimary::HandleFrameCapture() {
  if (output_buffer_.release_fence_fd >= 0) {
    frame_capture_status_ = sync_wait(output_buffer_.release_fence_fd, 1000);
    ::close(output_buffer_.release_fence_fd);
    output_buffer_.release_fence_fd = -1;
  }

  frame_capture_buffer_queued_ = false;
  post_processed_output_ = false;
  output_buffer_ = {};
}

void HWCDisplayPrimary::HandleFrameDump() {
  if (dump_frame_count_ && output_buffer_.release_fence_fd >= 0) {
    int ret = sync_wait(output_buffer_.release_fence_fd, 1000);
    ::close(output_buffer_.release_fence_fd);
    output_buffer_.release_fence_fd = -1;
    if (ret < 0) {
      DLOGE("sync_wait error errno = %d, desc = %s", errno,  strerror(errno));
    } else {
      DumpOutputBuffer(output_buffer_info_, output_buffer_base_, layer_stack_.retire_fence_fd);
    }
  }

  if (0 == dump_frame_count_) {
    dump_output_to_file_ = false;
    // Unmap and Free buffer
    if (munmap(output_buffer_base_, output_buffer_info_.alloc_buffer_info.size) != 0) {
      DLOGE("unmap failed with err %d", errno);
    }
    if (buffer_allocator_->FreeBuffer(&output_buffer_info_) != 0) {
      DLOGE("FreeBuffer failed");
    }

    post_processed_output_ = false;
    output_buffer_ = {};
    output_buffer_info_ = {};
    output_buffer_base_ = nullptr;
  }
}

void HWCDisplayPrimary::SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type) {
  HWCDisplay::SetFrameDumpConfig(count, bit_mask_layer_type);
  dump_output_to_file_ = bit_mask_layer_type & (1 << OUTPUT_LAYER_DUMP);
  DLOGI("output_layer_dump_enable %d", dump_output_to_file_);

  if (!count || !dump_output_to_file_) {
    return;
  }

  // Allocate and map output buffer
  output_buffer_info_ = {};
  // Since we dump DSPP output use Panel resolution.
  GetPanelResolution(&output_buffer_info_.buffer_config.width,
                     &output_buffer_info_.buffer_config.height);
  output_buffer_info_.buffer_config.format = kFormatRGB888;
  output_buffer_info_.buffer_config.buffer_count = 1;
  if (buffer_allocator_->AllocateBuffer(&output_buffer_info_) != 0) {
    DLOGE("Buffer allocation failed");
    output_buffer_info_ = {};
    return;
  }

  void *buffer = mmap(NULL, output_buffer_info_.alloc_buffer_info.size,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED, output_buffer_info_.alloc_buffer_info.fd, 0);

  if (buffer == MAP_FAILED) {
    DLOGE("mmap failed with err %d", errno);
    buffer_allocator_->FreeBuffer(&output_buffer_info_);
    output_buffer_info_ = {};
    return;
  }

  output_buffer_base_ = buffer;
  post_processed_output_ = true;
  DisablePartialUpdateOneFrame();
}

int HWCDisplayPrimary::FrameCaptureAsync(const BufferInfo& output_buffer_info,
                                         bool post_processed_output) {
  // Note: This function is called in context of a binder thread and a lock is already held
  if (output_buffer_info.alloc_buffer_info.fd < 0) {
    DLOGE("Invalid fd %d", output_buffer_info.alloc_buffer_info.fd);
    return -1;
  }

  auto panel_width = 0u;
  auto panel_height = 0u;
  auto fb_width = 0u;
  auto fb_height = 0u;

  GetPanelResolution(&panel_width, &panel_height);
  GetFrameBufferResolution(&fb_width, &fb_height);

  if (post_processed_output && (output_buffer_info.buffer_config.width < panel_width ||
                                output_buffer_info.buffer_config.height < panel_height)) {
    DLOGE("Buffer dimensions should not be less than panel resolution");
    return -1;
  } else if (!post_processed_output && (output_buffer_info.buffer_config.width < fb_width ||
                                        output_buffer_info.buffer_config.height < fb_height)) {
    DLOGE("Buffer dimensions should not be less than FB resolution");
    return -1;
  }

  SetLayerBuffer(output_buffer_info, &output_buffer_);
  post_processed_output_ = post_processed_output;
  frame_capture_buffer_queued_ = true;
  // Status is only cleared on a new call to dump and remains valid otherwise
  frame_capture_status_ = -EAGAIN;
  DisablePartialUpdateOneFrame();

  return 0;
}

DisplayError HWCDisplayPrimary::SetDetailEnhancerConfig(
                                    const DisplayDetailEnhancerData &de_data) {
  DisplayError error = kErrorNotSupported;
  if (display_intf_) {
    error = display_intf_->SetDetailEnhancerData(de_data);
  }
  return error;
}

DisplayError HWCDisplayPrimary::ControlPartialUpdate(bool enable, uint32_t *pending) {
  DisplayError error = kErrorNone;

  if (display_intf_) {
    error = display_intf_->ControlPartialUpdate(enable, pending);
  }

  return error;
}

DisplayError HWCDisplayPrimary::DisablePartialUpdateOneFrame() {
  DisplayError error = kErrorNone;

  if (display_intf_) {
    error = display_intf_->DisablePartialUpdateOneFrame();
  }

  return error;
}

DisplayError HWCDisplayPrimary::SetMixerResolution(uint32_t width, uint32_t height) {
  return display_intf_->SetMixerResolution(width, height);
}

DisplayError HWCDisplayPrimary::GetMixerResolution(uint32_t *width, uint32_t *height) {
  return display_intf_->GetMixerResolution(width, height);
}

}  // namespace sdm

