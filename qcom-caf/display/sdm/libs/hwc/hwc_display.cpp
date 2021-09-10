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

#include <math.h>
#include <errno.h>
#include <gralloc_priv.h>
#include <gr.h>
#include <utils/constants.h>
#include <utils/formats.h>
#include <utils/rect.h>
#include <utils/debug.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sync/sync.h>
#include <cutils/properties.h>
#include <qd_utils.h>
#include <map>
#include <utility>
#include <vector>
#include <string>

#include "blit_engine_c2d.h"
#include "hwc_debugger.h"
#include "hwc_display.h"
#include "hwc_tonemapper.h"

#ifdef QTI_BSP
#include <hardware/display_defs.h>
#endif

#define __CLASS__ "HWCDisplay"

namespace sdm {

void HWCColorMode::Init() {
  int ret = PopulateColorModes();
  if (ret != 0) {
    DLOGW("Failed!!");
  }
  return;
}

int HWCColorMode::SetColorMode(const std::string &color_mode) {
  if (color_modes_.empty()) {
    DLOGW("No Color Modes supported");
    return -1;
  }

  std::vector<std::string>::iterator it = std::find(color_modes_.begin(), color_modes_.end(),
                                                    color_mode);
  if (it == color_modes_.end()) {
    DLOGE("Invalid colorMode request: %s", color_mode.c_str());
    return -1;
  }

  DisplayError error = display_intf_->SetColorMode(color_mode);
  if (error != kErrorNone) {
    DLOGE("Failed to set color_mode = %s", color_mode.c_str());
    return -1;
  }
  current_color_mode_ = color_mode;

  return 0;
}

const std::vector<std::string> &HWCColorMode::GetColorModes() {
  return color_modes_;
}

int HWCColorMode::SetColorTransform(uint32_t matrix_count, const float *matrix) {
  if (matrix_count > kColorTransformMatrixCount) {
    DLOGE("Transform matrix count = %d, exceeds max = %d", matrix_count,
          kColorTransformMatrixCount);
    return -1;
  }

  double color_matrix[kColorTransformMatrixCount] = {0};
  CopyColorTransformMatrix(matrix, color_matrix);
  DisplayError error = display_intf_->SetColorTransform(matrix_count, color_matrix);
  if (error != kErrorNone) {
    DLOGE("Failed!");
    return -1;
  }

  return 0;
}

int HWCColorMode::PopulateColorModes() {
  uint32_t color_mode_count = 0;
  DisplayError error = display_intf_->GetColorModeCount(&color_mode_count);
  if (error != kErrorNone || (color_mode_count == 0)) {
    return -1;
  }

  DLOGI("Color Mode count = %d", color_mode_count);

  color_modes_.resize(color_mode_count);

  // SDM returns modes which is string
  error = display_intf_->GetColorModes(&color_mode_count, &color_modes_);
  if (error != kErrorNone) {
    DLOGE("GetColorModes Failed for count = %d", color_mode_count);
    return -1;
  }

  return 0;
}

HWCDisplay::HWCDisplay(CoreInterface *core_intf, hwc_procs_t const **hwc_procs, DisplayType type,
                       int id, bool needs_blit, qService::QService *qservice,
                       DisplayClass display_class)
  : core_intf_(core_intf), hwc_procs_(hwc_procs), type_(type), id_(id), needs_blit_(needs_blit),
    qservice_(qservice), display_class_(display_class) {
}

int HWCDisplay::Init() {
  DisplayError error = core_intf_->CreateDisplay(type_, this, &display_intf_);
  if (error != kErrorNone) {
    DLOGE("Display create failed. Error = %d display_type %d event_handler %p disp_intf %p",
      error, type_, this, &display_intf_);
    return -EINVAL;
  }

  HWCDebugHandler::Get()->GetProperty("sys.hwc_disable_hdr", &disable_hdr_handling_);
  if (disable_hdr_handling_) {
    DLOGI("HDR Handling disabled");
  }

  int property_swap_interval = 1;
  HWCDebugHandler::Get()->GetProperty("debug.egl.swapinterval", &property_swap_interval);
  if (property_swap_interval == 0) {
    swap_interval_zero_ = true;
  }

  int blit_enabled = 0;
  HWCDebugHandler::Get()->GetProperty("persist.hwc.blit.comp", &blit_enabled);
  if (needs_blit_ && blit_enabled) {
    blit_engine_ = new BlitEngineC2d();
    if (!blit_engine_) {
      DLOGI("Create Blit Engine C2D failed");
    } else {
      if (blit_engine_->Init() < 0) {
        DLOGI("Blit Engine Init failed, Blit Composition will not be used!!");
        delete blit_engine_;
        blit_engine_ = NULL;
      }
    }
  }

  tone_mapper_ = new HWCToneMapper();

  display_intf_->GetRefreshRateRange(&min_refresh_rate_, &max_refresh_rate_);
  current_refresh_rate_ = max_refresh_rate_;

  s3d_format_hwc_to_sdm_.insert(std::pair<int, LayerBufferS3DFormat>(HAL_NO_3D, kS3dFormatNone));
  s3d_format_hwc_to_sdm_.insert(std::pair<int, LayerBufferS3DFormat>(HAL_3D_SIDE_BY_SIDE_L_R,
                                kS3dFormatLeftRight));
  s3d_format_hwc_to_sdm_.insert(std::pair<int, LayerBufferS3DFormat>(HAL_3D_SIDE_BY_SIDE_R_L,
                                kS3dFormatRightLeft));
  s3d_format_hwc_to_sdm_.insert(std::pair<int, LayerBufferS3DFormat>(HAL_3D_TOP_BOTTOM,
                                kS3dFormatTopBottom));

  disable_animation_ = Debug::IsExtAnimDisabled();

  return 0;
}

int HWCDisplay::Deinit() {
  DisplayError error = core_intf_->DestroyDisplay(display_intf_);
  if (error != kErrorNone) {
    DLOGE("Display destroy failed. Error = %d", error);
    return -EINVAL;
  }

  if (blit_engine_) {
    blit_engine_->DeInit();
    delete blit_engine_;
    blit_engine_ = NULL;
  }

  delete tone_mapper_;
  tone_mapper_ = NULL;

  return 0;
}

int HWCDisplay::EventControl(int event, int enable) {
  DisplayError error = kErrorNone;

  if (shutdown_pending_) {
    return 0;
  }

  switch (event) {
  case HWC_EVENT_VSYNC:
    error = display_intf_->SetVSyncState(enable);
    break;
  default:
    DLOGW("Unsupported event = %d", event);
  }

  if (error != kErrorNone) {
    if (error == kErrorShutDown) {
      shutdown_pending_ = true;
      return 0;
    }
    DLOGE("Failed. event = %d, enable = %d, error = %d", event, enable, error);
    return -EINVAL;
  }

  return 0;
}

int HWCDisplay::SetPowerMode(int mode) {
  DLOGI("display = %d, mode = %d", id_, mode);
  DisplayState state = kStateOff;
  bool flush_on_error = flush_on_error_;

  if (shutdown_pending_) {
    return 0;
  }

  switch (mode) {
  case HWC_POWER_MODE_OFF:
    // During power off, all of the buffers are released.
    // Do not flush until a buffer is successfully submitted again.
    flush_on_error = false;
    state = kStateOff;
    tone_mapper_->Terminate();
    break;

  case HWC_POWER_MODE_NORMAL:
    state = kStateOn;
    last_power_mode_ = HWC_POWER_MODE_NORMAL;
    break;

  case HWC_POWER_MODE_DOZE:
    state = kStateDoze;
    last_power_mode_ = HWC_POWER_MODE_DOZE;
    break;

  case HWC_POWER_MODE_DOZE_SUSPEND:
    state = kStateDozeSuspend;
    last_power_mode_ = HWC_POWER_MODE_DOZE_SUSPEND;
    break;

  default:
    return -EINVAL;
  }

  DisplayError error = display_intf_->SetDisplayState(state);
  if (error == kErrorNone) {
    flush_on_error_ = flush_on_error;
  } else {
    if (error == kErrorShutDown) {
      shutdown_pending_ = true;
      return 0;
    }
    DLOGE("Set state failed. Error = %d", error);
    return -EINVAL;
  }

  return 0;
}

int HWCDisplay::GetDisplayConfigs(uint32_t *configs, size_t *num_configs) {
  if (*num_configs > 0) {
    configs[0] = 0;
    *num_configs = 1;
  }

  return 0;
}

int HWCDisplay::GetDisplayAttributes(uint32_t config, const uint32_t *display_attributes,
                                     int32_t *values) {
  DisplayConfigVariableInfo variable_config;
  DisplayError error = display_intf_->GetFrameBufferConfig(&variable_config);
  if (error != kErrorNone) {
    DLOGV("Get variable config failed. Error = %d", error);
    return -EINVAL;
  }

  for (int i = 0; display_attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
    switch (display_attributes[i]) {
    case HWC_DISPLAY_VSYNC_PERIOD:
      values[i] = INT32(variable_config.vsync_period_ns);
      break;
    case HWC_DISPLAY_WIDTH:
      values[i] = INT32(variable_config.x_pixels);
      break;
    case HWC_DISPLAY_HEIGHT:
      values[i] = INT32(variable_config.y_pixels);
      break;
    case HWC_DISPLAY_DPI_X:
      values[i] = INT32(variable_config.x_dpi * 1000.0f);
      break;
    case HWC_DISPLAY_DPI_Y:
      values[i] = INT32(variable_config.y_dpi * 1000.0f);
      break;
    default:
      DLOGW("Spurious attribute type = %d", display_attributes[i]);
      return -EINVAL;
    }
  }

  return 0;
}

int HWCDisplay::GetActiveConfig() {
  return 0;
}

int HWCDisplay::SetActiveConfig(int index) {
  return -1;
}

DisplayError HWCDisplay::SetMixerResolution(uint32_t width, uint32_t height) {
  return kErrorNotSupported;
}

void HWCDisplay::SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type) {
  dump_frame_count_ = count;
  dump_frame_index_ = 0;
  dump_input_layers_ = ((bit_mask_layer_type & (1 << INPUT_LAYER_DUMP)) != 0);

  if (blit_engine_) {
    blit_engine_->SetFrameDumpConfig(count);
  }

  if (tone_mapper_) {
    tone_mapper_->SetFrameDumpConfig(count);
  }

  DLOGI("num_frame_dump %d, input_layer_dump_enable %d", dump_frame_count_, dump_input_layers_);
}

uint32_t HWCDisplay::GetLastPowerMode() {
  return last_power_mode_;
}

DisplayError HWCDisplay::VSync(const DisplayEventVSync &vsync) {
  const hwc_procs_t *hwc_procs = *hwc_procs_;

  if (!hwc_procs) {
    return kErrorParameters;
  }

  hwc_procs->vsync(hwc_procs, id_, vsync.timestamp);

  return kErrorNone;
}

DisplayError HWCDisplay::Refresh() {
  return kErrorNotSupported;
}

DisplayError HWCDisplay::CECMessage(char *message) {
  if (qservice_) {
    qservice_->onCECMessageReceived(message, 0);
  } else {
    DLOGW("Qservice instance not available.");
  }

  return kErrorNone;
}

int HWCDisplay::AllocateLayerStack(hwc_display_contents_1_t *content_list) {
  if (!content_list || !content_list->numHwLayers) {
    DLOGW("Invalid content list");
    return -EINVAL;
  }

  size_t num_hw_layers = content_list->numHwLayers;
  uint32_t blit_target_count = 0;

  if (blit_engine_) {
    blit_target_count = kMaxBlitTargetLayers;
  }

  FreeLayerStack();

  for (size_t i = 0; i < num_hw_layers + blit_target_count; i++) {
    Layer *layer = new Layer();
    layer_stack_.layers.push_back(layer);
  }

  return 0;
}

void HWCDisplay::FreeLayerStack() {
  for (Layer *layer : layer_stack_.layers) {
    delete layer;
  }
  layer_stack_ = {};
}

int HWCDisplay::PrepareLayerParams(hwc_layer_1_t *hwc_layer, Layer* layer) {
  const private_handle_t *pvt_handle = static_cast<const private_handle_t *>(hwc_layer->handle);

  LayerBuffer &layer_buffer = layer->input_buffer;

  if (pvt_handle) {
    layer_buffer.planes[0].fd = pvt_handle->fd;
    layer_buffer.format = GetSDMFormat(pvt_handle->format, pvt_handle->flags);
    int aligned_width, aligned_height;
    int unaligned_width, unaligned_height;

    AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(pvt_handle, aligned_width,
                                                          aligned_height);
    AdrenoMemInfo::getInstance().getUnalignedWidthAndHeight(pvt_handle, unaligned_width,
                                                            unaligned_height);

    layer_buffer.width = UINT32(aligned_width);
    layer_buffer.height = UINT32(aligned_height);
    layer_buffer.unaligned_width = UINT32(unaligned_width);
    layer_buffer.unaligned_height = UINT32(unaligned_height);

    if (SetMetaData(pvt_handle, layer) != kErrorNone) {
      return -EINVAL;
    }

    if (pvt_handle->bufferType == BUFFER_TYPE_VIDEO) {
      layer_stack_.flags.video_present = true;
      layer_buffer.flags.video = true;
    }
    // TZ Protected Buffer - L1
    if (pvt_handle->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
      layer_stack_.flags.secure_present = true;
      layer_buffer.flags.secure = true;
      if (pvt_handle->flags & private_handle_t::PRIV_FLAGS_CAMERA_WRITE) {
        layer_buffer.flags.secure_camera = true;
      }
    }
    // Gralloc Usage Protected Buffer - L3 - which needs to be treated as Secure & avoid fallback
    if (pvt_handle->flags & private_handle_t::PRIV_FLAGS_PROTECTED_BUFFER) {
      layer_stack_.flags.secure_present = true;
    }
    if (pvt_handle->flags & private_handle_t::PRIV_FLAGS_SECURE_DISPLAY) {
      layer_buffer.flags.secure_display = true;
    }

    // check if this is special solid_fill layer without input_buffer.
    if (solid_fill_enable_ && pvt_handle->fd == -1) {
      layer->flags.solid_fill = true;
      layer->solid_fill_color = solid_fill_color_;
    }
  } else {
    // for FBT layer
    if (hwc_layer->compositionType == HWC_FRAMEBUFFER_TARGET) {
      uint32_t x_pixels;
      uint32_t y_pixels;
      int aligned_width;
      int aligned_height;
      int usage = GRALLOC_USAGE_HW_FB;
      int format = HAL_PIXEL_FORMAT_RGBA_8888;
      int ubwc_enabled = 0;
      int flags = 0;
      HWCDebugHandler::Get()->GetProperty("debug.gralloc.enable_fb_ubwc", &ubwc_enabled);
      bool linear = layer_stack_.output_buffer && !IsUBWCFormat(layer_stack_.output_buffer->format);
      if ((ubwc_enabled == 1) && !linear) {
        usage |= GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
        flags |= private_handle_t::PRIV_FLAGS_UBWC_ALIGNED;
      }

      GetFrameBufferResolution(&x_pixels, &y_pixels);

      AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(INT(x_pixels), INT(y_pixels), format,
                                                            usage, aligned_width, aligned_height);
      layer_buffer.width = UINT32(aligned_width);
      layer_buffer.height = UINT32(aligned_height);
      layer_buffer.unaligned_width = x_pixels;
      layer_buffer.unaligned_height = y_pixels;
      layer_buffer.format = GetSDMFormat(format, flags);
    }
  }

  return 0;
}

void HWCDisplay::CommitLayerParams(hwc_layer_1_t *hwc_layer, Layer *layer) {
  const private_handle_t *pvt_handle = static_cast<const private_handle_t *>(hwc_layer->handle);
  LayerBuffer &layer_buffer = layer->input_buffer;

  if (pvt_handle) {
    layer_buffer.planes[0].fd = pvt_handle->fd;
    layer_buffer.planes[0].offset = pvt_handle->offset;
    layer_buffer.planes[0].stride = UINT32(pvt_handle->width);
    layer_buffer.size = pvt_handle->size;
  }

  // if swapinterval property is set to 0 then close and reset the acquireFd
  if (swap_interval_zero_ && hwc_layer->acquireFenceFd >= 0) {
    close(hwc_layer->acquireFenceFd);
    hwc_layer->acquireFenceFd = -1;
  }
  layer_buffer.acquire_fence_fd = hwc_layer->acquireFenceFd;
}

int HWCDisplay::PrePrepareLayerStack(hwc_display_contents_1_t *content_list) {
  if (shutdown_pending_) {
    return 0;
  }

  size_t num_hw_layers = content_list->numHwLayers;

  use_blit_comp_ = false;
  metadata_refresh_rate_ = 0;
  display_rect_ = LayerRect();

  // Configure each layer
  for (size_t i = 0; i < num_hw_layers; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];

    const private_handle_t *pvt_handle = static_cast<const private_handle_t *>(hwc_layer.handle);
    Layer *layer = layer_stack_.layers.at(i);
    int ret = PrepareLayerParams(&content_list->hwLayers[i], layer);

    if (ret != kErrorNone) {
      return ret;
    }

    layer->flags.skip = ((hwc_layer.flags & HWC_SKIP_LAYER) > 0);
    layer->flags.solid_fill = (hwc_layer.flags & kDimLayer) || solid_fill_enable_;
    if (layer->flags.skip || layer->flags.solid_fill) {
      layer->dirty_regions.clear();
    }

    hwc_rect_t scaled_display_frame = hwc_layer.displayFrame;
    ApplyScanAdjustment(&scaled_display_frame);

    SetRect(scaled_display_frame, &layer->dst_rect);
    if (pvt_handle) {
        bool NonIntegralSourceCrop =  IsNonIntegralSourceCrop(hwc_layer.sourceCropf);
        bool secure = (pvt_handle->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) ||
                (pvt_handle->flags & private_handle_t::PRIV_FLAGS_PROTECTED_BUFFER) ||
                (pvt_handle->flags & private_handle_t::PRIV_FLAGS_SECURE_DISPLAY);
        if (NonIntegralSourceCrop && (!secure && pvt_handle->bufferType != BUFFER_TYPE_VIDEO)) {
            layer->flags.skip = true;
        }
    }
    SetRect(hwc_layer.sourceCropf, &layer->src_rect);

    uint32_t num_visible_rects = UINT32(hwc_layer.visibleRegionScreen.numRects);
    uint32_t num_dirty_rects = UINT32(hwc_layer.surfaceDamage.numRects);

    for (uint32_t j = 0; j < num_visible_rects; j++) {
      LayerRect visible_rect = {};
      SetRect(hwc_layer.visibleRegionScreen.rects[j], &visible_rect);
      layer->visible_regions.push_back(visible_rect);
    }

    for (uint32_t j = 0; j < num_dirty_rects; j++) {
      LayerRect dirty_rect = {};
      SetRect(hwc_layer.surfaceDamage.rects[j], &dirty_rect);
      layer->dirty_regions.push_back(dirty_rect);
    }

    if (blit_engine_) {
      for (uint32_t j = 0; j < kMaxBlitTargetLayers; j++) {
        LayerRect blit_rect = {};
        layer->blit_regions.push_back(blit_rect);
      }
    }

    SetComposition(hwc_layer.compositionType, &layer->composition);
    if (hwc_layer.compositionType != HWC_FRAMEBUFFER_TARGET) {
      display_rect_ = Union(display_rect_, layer->dst_rect);
    }

    // For dim layers, SurfaceFlinger
    //    - converts planeAlpha to per pixel alpha,
    //    - sets appropriate RGB color,
    //    - sets planeAlpha to 0xff,
    //    - blending to Premultiplied.
    // This can be achieved at hardware by
    //    - solid fill ARGB to appropriate value,
    //    - incoming planeAlpha,
    //    - blending to Coverage.
    if (hwc_layer.flags & kDimLayer) {
      layer->input_buffer.format = kFormatARGB8888;
      layer->solid_fill_color = 0xff000000;
#ifdef QTI_BSP
      // Get ARGB color from HWC Dim Layer color
      uint32_t a = UINT32(hwc_layer.color.a) << 24;
      uint32_t r = UINT32(hwc_layer.color.r) << 16;
      uint32_t g = UINT32(hwc_layer.color.g) << 8;
      uint32_t b = UINT32(hwc_layer.color.b);
      layer->solid_fill_color = a | r | g | b;
#endif
      SetBlending(HWC_BLENDING_COVERAGE, &layer->blending);
    } else {
      SetBlending(hwc_layer.blending, &layer->blending);
      LayerTransform &layer_transform = layer->transform;
      uint32_t &hwc_transform = hwc_layer.transform;
      layer_transform.flip_horizontal = ((hwc_transform & HWC_TRANSFORM_FLIP_H) > 0);
      layer_transform.flip_vertical = ((hwc_transform & HWC_TRANSFORM_FLIP_V) > 0);
      layer_transform.rotation = ((hwc_transform & HWC_TRANSFORM_ROT_90) ? 90.0f : 0.0f);
    }

    // TODO(user): Remove below block.
    // For solid fill, only dest rect need to be specified.
    if (layer->flags.solid_fill) {
      LayerBuffer &input_buffer = layer->input_buffer;
      input_buffer.width = UINT32(layer->dst_rect.right - layer->dst_rect.left);
      input_buffer.height = UINT32(layer->dst_rect.bottom - layer->dst_rect.top);
      input_buffer.unaligned_width = input_buffer.width;
      input_buffer.unaligned_height = input_buffer.height;
      layer->src_rect.left = 0;
      layer->src_rect.top = 0;
      layer->src_rect.right = input_buffer.width;
      layer->src_rect.bottom = input_buffer.height;
    }

    layer->plane_alpha = hwc_layer.planeAlpha;
    layer->flags.cursor = ((hwc_layer.flags & HWC_IS_CURSOR_LAYER) > 0);
    layer->flags.updating = true;

    if (num_hw_layers <= kMaxLayerCount) {
      layer->flags.updating = IsLayerUpdating(content_list, layer);
    }
#ifdef QTI_BSP
    if (hwc_layer.flags & HWC_SCREENSHOT_ANIMATOR_LAYER) {
      layer_stack_.flags.animating = true;
    }
#endif
    if (layer->flags.skip) {
      layer_stack_.flags.skip_present = true;
    }

    if (layer->flags.cursor) {
      layer_stack_.flags.cursor_present = true;
    }

    PrepareDynamicRefreshRate(layer);

    layer->input_buffer.buffer_id = reinterpret_cast<uint64_t>(hwc_layer.handle);
  }

  // Prepare the Blit Target
  if (blit_engine_) {
  // TODO(user): Fix this to enable BLIT
#if 0
    int ret = blit_engine_->Prepare(&layer_stack_);
    if (ret) {
      // Blit engine cannot handle this layer stack, hence set the layer stack
      // count to num_hw_layers
      layer_stack_.layer_count -= kMaxBlitTargetLayers;
    } else {
      use_blit_comp_ = true;
    }
#endif
  }

  // Configure layer stack
  layer_stack_.flags.geometry_changed = ((content_list->flags & HWC_GEOMETRY_CHANGED) > 0);

  return 0;
}

void HWCDisplay::SetLayerS3DMode(const LayerBufferS3DFormat &source, uint32_t *target) {
#ifdef QTI_BSP
    switch (source) {
    case kS3dFormatNone: *target = HWC_S3DMODE_NONE; break;
    case kS3dFormatLeftRight: *target = HWC_S3DMODE_LR; break;
    case kS3dFormatRightLeft: *target = HWC_S3DMODE_RL; break;
    case kS3dFormatTopBottom: *target = HWC_S3DMODE_TB; break;
    case kS3dFormatFramePacking: *target = HWC_S3DMODE_FP; break;
    default: *target = HWC_S3DMODE_MAX; break;
    }
#endif
}

int HWCDisplay::PrepareLayerStack(hwc_display_contents_1_t *content_list) {
  if (shutdown_pending_) {
    return 0;
  }

  size_t num_hw_layers = content_list->numHwLayers;

  if (!skip_prepare_cnt) {
    DisplayError error = display_intf_->Prepare(&layer_stack_);
    if (error != kErrorNone) {
      if (error == kErrorShutDown) {
        shutdown_pending_ = true;
      } else if ((error != kErrorPermission) && (error != kErrorNoAppLayers)) {
        DLOGE("Prepare failed. Error = %d", error);
        // To prevent surfaceflinger infinite wait, flush the previous frame during Commit()
        // so that previous buffer and fences are released, and override the error.
        flush_ = true;
      } else {
        DLOGV("Prepare failed for Display = %d Error = %d", type_, error);
      }
      return 0;
    }
  } else {
    // Skip is not set
    MarkLayersForGPUBypass(content_list);
    skip_prepare_cnt = skip_prepare_cnt - 1;
    DLOGI("SecureDisplay %s, Skip Prepare/Commit and Flush", secure_display_active_ ? "Starting" :
          "Stopping");
    flush_ = true;
  }

  for (size_t i = 0; i < num_hw_layers; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    Layer *layer = layer_stack_.layers.at(i);
    LayerComposition composition = layer->composition;
    private_handle_t* pvt_handle  = static_cast<private_handle_t*>
      (const_cast<native_handle_t*>(hwc_layer.handle));
    MetaData_t *meta_data = pvt_handle ?
      reinterpret_cast<MetaData_t *>(pvt_handle->base_metadata) : NULL;

    if ((composition == kCompositionSDE) || (composition == kCompositionHybrid) ||
        (composition == kCompositionBlit)) {
      hwc_layer.hints |= HWC_HINT_CLEAR_FB;
    }
    SetComposition(composition, &hwc_layer.compositionType);

    if (meta_data != NULL) {
      if (composition == kCompositionGPUS3D) {
        // Align HWC and client's dispaly ID in case of HDMI as primary
        meta_data->s3dComp.displayId =
          display_intf_->IsPrimaryDisplay() ? HWC_DISPLAY_PRIMARY: id_;
        SetLayerS3DMode(layer->input_buffer.s3d_format,
            &meta_data->s3dComp.s3dMode);
      }
    }
  }

  return 0;
}

int HWCDisplay::CommitLayerStack(hwc_display_contents_1_t *content_list) {
  if (!content_list || !content_list->numHwLayers) {
    DLOGW("Invalid content list");
    return -EINVAL;
  }

  if (shutdown_pending_) {
    return 0;
  }

  int status = 0;

  size_t num_hw_layers = content_list->numHwLayers;

  DumpInputBuffers(content_list);

  if (!flush_) {
    for (size_t i = 0; i < num_hw_layers; i++) {
      CommitLayerParams(&content_list->hwLayers[i], layer_stack_.layers.at(i));
    }

    if (use_blit_comp_) {
      status = blit_engine_->PreCommit(content_list, &layer_stack_);
      if (status == 0) {
        status = blit_engine_->Commit(content_list, &layer_stack_);
        if (status != 0) {
          DLOGE("Blit Comp Failed!");
        }
      }
    }

    if (layer_stack_.flags.hdr_present) {
      status = tone_mapper_->HandleToneMap(content_list, &layer_stack_);
      if (status != 0) {
        DLOGE("Error handling HDR in ToneMapper");
      }
    } else {
      tone_mapper_->Terminate();
    }

    DisplayError error = kErrorUndefined;
    if (status == 0) {
      error = display_intf_->Commit(&layer_stack_);
      status = 0;
    }

    if (error == kErrorNone) {
      // A commit is successfully submitted, start flushing on failure now onwards.
      flush_on_error_ = true;
    } else {
      if (error == kErrorShutDown) {
        shutdown_pending_ = true;
        return status;
      } else if (error != kErrorPermission) {
        DLOGE("Commit failed. Error = %d", error);
        // To prevent surfaceflinger infinite wait, flush the previous frame during Commit()
        // so that previous buffer and fences are released, and override the error.
        flush_ = true;
      } else {
        DLOGI("Commit failed for Display = %d Error = %d", type_, error);
      }
    }
  }

  return status;
}

int HWCDisplay::PostCommitLayerStack(hwc_display_contents_1_t *content_list) {
  size_t num_hw_layers = content_list->numHwLayers;
  int status = 0;

  // Do no call flush on errors, if a successful buffer is never submitted.
  if (flush_ && flush_on_error_) {
    display_intf_->Flush();
  }


  if (tone_mapper_ && tone_mapper_->IsActive()) {
     tone_mapper_->PostCommit(&layer_stack_);
  }

  // Set the release fence fd to the blit engine
  if (use_blit_comp_ && blit_engine_->BlitActive()) {
    blit_engine_->PostCommit(&layer_stack_);
  }

  for (size_t i = 0; i < num_hw_layers; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    Layer *layer = layer_stack_.layers.at(i);
    LayerBuffer &layer_buffer = layer->input_buffer;

    if (!flush_) {
      // If swapinterval property is set to 0 or for single buffer layers, do not update f/w
      // release fences and discard fences from driver
      if (swap_interval_zero_ || layer->flags.single_buffer) {
        hwc_layer.releaseFenceFd = -1;
        close(layer_buffer.release_fence_fd);
        layer_buffer.release_fence_fd = -1;
      } else if (layer->composition != kCompositionGPU) {
        hwc_layer.releaseFenceFd = layer_buffer.release_fence_fd;
      }

      // During animation on external/virtual display, SDM will use the cached
      // framebuffer layer throughout animation and do not allow framework to do eglswapbuffer on
      // framebuffer target. So graphics doesn't close the release fence fd of framebuffer target,
      // Hence close the release fencefd of framebuffer target here.
      if (disable_animation_) {
        if (layer->composition == kCompositionGPUTarget && animating_) {
          close(hwc_layer.releaseFenceFd);
          hwc_layer.releaseFenceFd = -1;
        }
      }
    }

    if (hwc_layer.acquireFenceFd >= 0) {
      close(hwc_layer.acquireFenceFd);
      hwc_layer.acquireFenceFd = -1;
    }
  }

  if (!flush_) {
    animating_ = layer_stack_.flags.animating;
    // if swapinterval property is set to 0 then close and reset the list retire fence
    if (swap_interval_zero_) {
      close(layer_stack_.retire_fence_fd);
      layer_stack_.retire_fence_fd = -1;
    }
    content_list->retireFenceFd = layer_stack_.retire_fence_fd;

    if (dump_frame_count_) {
      dump_frame_count_--;
      dump_frame_index_++;
    }
  }

  flush_ = false;

  return status;
}

bool HWCDisplay::IsLayerUpdating(hwc_display_contents_1_t *content_list, const Layer *layer) {
  // Layer should be considered updating if
  //   a) layer is in single buffer mode, or
  //   b) valid dirty_regions(android specific hint for updating status), or
  //   c) layer stack geometry has changed
  return (layer->flags.single_buffer || IsSurfaceUpdated(layer->dirty_regions) ||
         (layer_stack_.flags.geometry_changed));
}

bool HWCDisplay::IsNonIntegralSourceCrop(const hwc_frect_t &source) {
     if ((source.left != roundf(source.left)) ||
         (source.top != roundf(source.top)) ||
         (source.right != roundf(source.right)) ||
         (source.bottom != roundf(source.bottom))) {
         return true;
     } else {
         return false;
     }
}

void HWCDisplay::SetRect(const hwc_rect_t &source, LayerRect *target) {
  target->left = FLOAT(source.left);
  target->top = FLOAT(source.top);
  target->right = FLOAT(source.right);
  target->bottom = FLOAT(source.bottom);
}

void HWCDisplay::SetRect(const hwc_frect_t &source, LayerRect *target) {
  target->left = floorf(source.left);
  target->top = floorf(source.top);
  target->right = ceilf(source.right);
  target->bottom = ceilf(source.bottom);
}

void HWCDisplay::SetComposition(const int32_t &source, LayerComposition *target) {
  switch (source) {
  case HWC_FRAMEBUFFER_TARGET:  *target = kCompositionGPUTarget;  break;
  default:                      *target = kCompositionGPU;        break;
  }
}

void HWCDisplay::SetComposition(const LayerComposition &source, int32_t *target) {
  switch (source) {
  case kCompositionGPUTarget:   *target = HWC_FRAMEBUFFER_TARGET; break;
  case kCompositionGPU:         *target = HWC_FRAMEBUFFER;        break;
  case kCompositionGPUS3D:      *target = HWC_FRAMEBUFFER;        break;
  case kCompositionHWCursor:    *target = HWC_CURSOR_OVERLAY;     break;
  default:                      *target = HWC_OVERLAY;            break;
  }
}

void HWCDisplay::SetBlending(const int32_t &source, LayerBlending *target) {
  switch (source) {
  case HWC_BLENDING_PREMULT:    *target = kBlendingPremultiplied;   break;
  case HWC_BLENDING_COVERAGE:   *target = kBlendingCoverage;        break;
  default:                      *target = kBlendingOpaque;          break;
  }
}

void HWCDisplay::SetIdleTimeoutMs(uint32_t timeout_ms) {
  return;
}

DisplayError HWCDisplay::SetMaxMixerStages(uint32_t max_mixer_stages) {
  DisplayError error = kErrorNone;

  if (display_intf_) {
    error = display_intf_->SetMaxMixerStages(max_mixer_stages);
  }

  return error;
}

LayerBufferFormat HWCDisplay::GetSDMFormat(const int32_t &source, const int flags) {
  LayerBufferFormat format = kFormatInvalid;
  if (flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) {
    switch (source) {
    case HAL_PIXEL_FORMAT_RGBA_8888:           format = kFormatRGBA8888Ubwc;            break;
    case HAL_PIXEL_FORMAT_RGBX_8888:           format = kFormatRGBX8888Ubwc;            break;
    case HAL_PIXEL_FORMAT_BGR_565:             format = kFormatBGR565Ubwc;              break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
    case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:     format = kFormatYCbCr420SPVenusUbwc;     break;
    case HAL_PIXEL_FORMAT_RGBA_1010102:        format = kFormatRGBA1010102Ubwc;         break;
    case HAL_PIXEL_FORMAT_RGBX_1010102:        format = kFormatRGBX1010102Ubwc;         break;
    case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC: format = kFormatYCbCr420TP10Ubwc;        break;
    case HAL_PIXEL_FORMAT_YCbCr_420_P010_UBWC: format = kFormatYCbCr420P010Ubwc;        break;
    default:
      DLOGE("Unsupported format type for UBWC %d", source);
      return kFormatInvalid;
    }
    return format;
  }

  switch (source) {
  case HAL_PIXEL_FORMAT_RGBA_8888:                format = kFormatRGBA8888;                 break;
  case HAL_PIXEL_FORMAT_RGBA_5551:                format = kFormatRGBA5551;                 break;
  case HAL_PIXEL_FORMAT_RGBA_4444:                format = kFormatRGBA4444;                 break;
  case HAL_PIXEL_FORMAT_BGRA_8888:                format = kFormatBGRA8888;                 break;
  case HAL_PIXEL_FORMAT_RGBX_8888:                format = kFormatRGBX8888;                 break;
  case HAL_PIXEL_FORMAT_BGRX_8888:                format = kFormatBGRX8888;                 break;
  case HAL_PIXEL_FORMAT_RGB_888:                  format = kFormatRGB888;                   break;
  case HAL_PIXEL_FORMAT_RGB_565:                  format = kFormatRGB565;                   break;
  case HAL_PIXEL_FORMAT_BGR_565:                  format = kFormatBGR565;                   break;
  case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
  case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:       format = kFormatYCbCr420SemiPlanarVenus;  break;
  case HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS:       format = kFormatYCrCb420SemiPlanarVenus;  break;
  case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:  format = kFormatYCbCr420SPVenusUbwc;      break;
  case HAL_PIXEL_FORMAT_YV12:                     format = kFormatYCrCb420PlanarStride16;   break;
  case HAL_PIXEL_FORMAT_YCrCb_420_SP:             format = kFormatYCrCb420SemiPlanar;       break;
  case HAL_PIXEL_FORMAT_YCbCr_420_SP:             format = kFormatYCbCr420SemiPlanar;       break;
  case HAL_PIXEL_FORMAT_YCbCr_422_SP:             format = kFormatYCbCr422H2V1SemiPlanar;   break;
  case HAL_PIXEL_FORMAT_YCbCr_422_I:              format = kFormatYCbCr422H2V1Packed;       break;
  case HAL_PIXEL_FORMAT_CbYCrY_422_I:             format = kFormatCbYCrY422H2V1Packed;      break;
  case HAL_PIXEL_FORMAT_RGBA_1010102:             format = kFormatRGBA1010102;              break;
  case HAL_PIXEL_FORMAT_ARGB_2101010:             format = kFormatARGB2101010;              break;
  case HAL_PIXEL_FORMAT_RGBX_1010102:             format = kFormatRGBX1010102;              break;
  case HAL_PIXEL_FORMAT_XRGB_2101010:             format = kFormatXRGB2101010;              break;
  case HAL_PIXEL_FORMAT_BGRA_1010102:             format = kFormatBGRA1010102;              break;
  case HAL_PIXEL_FORMAT_ABGR_2101010:             format = kFormatABGR2101010;              break;
  case HAL_PIXEL_FORMAT_BGRX_1010102:             format = kFormatBGRX1010102;              break;
  case HAL_PIXEL_FORMAT_XBGR_2101010:             format = kFormatXBGR2101010;              break;
  case HAL_PIXEL_FORMAT_YCbCr_420_P010:           format = kFormatYCbCr420P010;             break;
  case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:      format = kFormatYCbCr420TP10Ubwc;         break;
  case HAL_PIXEL_FORMAT_YCbCr_420_P010_UBWC:      format = kFormatYCbCr420P010Ubwc;         break;
  default:
    DLOGW("Unsupported format type = %d", source);
    return kFormatInvalid;
  }

  return format;
}

void HWCDisplay::DumpInputBuffers(hwc_display_contents_1_t *content_list) {
  size_t num_hw_layers = content_list->numHwLayers;
  char dir_path[PATH_MAX];

  if (!dump_frame_count_ || flush_ || !dump_input_layers_) {
    return;
  }

  snprintf(dir_path, sizeof(dir_path), "/data/misc/display/frame_dump_%s", GetDisplayString());

  if (mkdir(dir_path, 0777) != 0 && errno != EEXIST) {
    DLOGW("Failed to create %s directory errno = %d, desc = %s", dir_path, errno, strerror(errno));
    return;
  }

  // if directory exists already, need to explicitly change the permission.
  if (errno == EEXIST && chmod(dir_path, 0777) != 0) {
    DLOGW("Failed to change permissions on %s directory", dir_path);
    return;
  }

  for (uint32_t i = 0; i < num_hw_layers; i++) {
    hwc_layer_1_t &hwc_layer = content_list->hwLayers[i];
    const private_handle_t *pvt_handle = static_cast<const private_handle_t *>(hwc_layer.handle);

    if (hwc_layer.acquireFenceFd >= 0) {
      int error = sync_wait(hwc_layer.acquireFenceFd, 1000);
      if (error < 0) {
        DLOGW("sync_wait error errno = %d, desc = %s", errno, strerror(errno));
        return;
      }
    }

    if (pvt_handle && pvt_handle->base) {
      char dump_file_name[PATH_MAX];
      size_t result = 0;

      snprintf(dump_file_name, sizeof(dump_file_name), "%s/input_layer%d_%dx%d_%s_frame%d.raw",
               dir_path, i, pvt_handle->width, pvt_handle->height,
               qdutils::GetHALPixelFormatString(pvt_handle->format), dump_frame_index_);

      FILE* fp = fopen(dump_file_name, "w+");
      if (fp) {
        result = fwrite(reinterpret_cast<void *>(pvt_handle->base), pvt_handle->size, 1, fp);
        fclose(fp);
      }

      DLOGI("Frame Dump %s: is %s", dump_file_name, result ? "Successful" : "Failed");
    }
  }
}

void HWCDisplay::DumpOutputBuffer(const BufferInfo& buffer_info, void *base, int fence) {
  char dir_path[PATH_MAX];

  snprintf(dir_path, sizeof(dir_path), "/data/misc/display/frame_dump_%s", GetDisplayString());

  if (mkdir(dir_path, 777) != 0 && errno != EEXIST) {
    DLOGW("Failed to create %s directory errno = %d, desc = %s", dir_path, errno, strerror(errno));
    return;
  }

  // if directory exists already, need to explicitly change the permission.
  if (errno == EEXIST && chmod(dir_path, 0777) != 0) {
    DLOGW("Failed to change permissions on %s directory", dir_path);
    return;
  }

  if (base) {
    char dump_file_name[PATH_MAX];
    size_t result = 0;

    if (fence >= 0) {
      int error = sync_wait(fence, 1000);
      if (error < 0) {
        DLOGW("sync_wait error errno = %d, desc = %s", errno,  strerror(errno));
        return;
      }
    }

    snprintf(dump_file_name, sizeof(dump_file_name), "%s/output_layer_%dx%d_%s_frame%d.raw",
             dir_path, buffer_info.alloc_buffer_info.aligned_width,
             buffer_info.alloc_buffer_info.aligned_height,
             GetFormatString(buffer_info.buffer_config.format), dump_frame_index_);

    FILE* fp = fopen(dump_file_name, "w+");
    if (fp) {
      result = fwrite(base, buffer_info.alloc_buffer_info.size, 1, fp);
      fclose(fp);
    }

    DLOGI("Frame Dump of %s is %s", dump_file_name, result ? "Successful" : "Failed");
  }
}

const char *HWCDisplay::GetDisplayString() {
  switch (type_) {
  case kPrimary:
    return "primary";
  case kHDMI:
    return "hdmi";
  case kVirtual:
    return "virtual";
  default:
    return "invalid";
  }
}

int HWCDisplay::SetFrameBufferResolution(uint32_t x_pixels, uint32_t y_pixels) {
  DisplayConfigVariableInfo fb_config;
  DisplayError error = display_intf_->GetFrameBufferConfig(&fb_config);
  if (error != kErrorNone) {
    DLOGV("Get frame buffer config failed. Error = %d", error);
    return -EINVAL;
  }

  fb_config.x_pixels = x_pixels;
  fb_config.y_pixels = y_pixels;

  error = display_intf_->SetFrameBufferConfig(fb_config);
  if (error != kErrorNone) {
    DLOGV("Set frame buffer config failed. Error = %d", error);
    return -EINVAL;
  }

  DLOGI("New framebuffer resolution (%dx%d)", x_pixels, y_pixels);

  return 0;
}

void HWCDisplay::GetFrameBufferResolution(uint32_t *x_pixels, uint32_t *y_pixels) {
  DisplayConfigVariableInfo fb_config;
  display_intf_->GetFrameBufferConfig(&fb_config);

  *x_pixels = fb_config.x_pixels;
  *y_pixels = fb_config.y_pixels;
}

DisplayError HWCDisplay::GetMixerResolution(uint32_t *x_pixels, uint32_t *y_pixels) {
  return display_intf_->GetMixerResolution(x_pixels, y_pixels);
}


void HWCDisplay::GetPanelResolution(uint32_t *x_pixels, uint32_t *y_pixels) {
  DisplayConfigVariableInfo display_config;
  uint32_t active_index = 0;

  display_intf_->GetActiveConfig(&active_index);
  display_intf_->GetConfig(active_index, &display_config);

  *x_pixels = display_config.x_pixels;
  *y_pixels = display_config.y_pixels;
}

int HWCDisplay::SetDisplayStatus(uint32_t display_status) {
  int status = 0;
  const hwc_procs_t *hwc_procs = *hwc_procs_;

  switch (display_status) {
  case kDisplayStatusResume:
    display_paused_ = false;
  case kDisplayStatusOnline:
    status = SetPowerMode(HWC_POWER_MODE_NORMAL);
    break;
  case kDisplayStatusPause:
    display_paused_ = true;
  case kDisplayStatusOffline:
    status = SetPowerMode(HWC_POWER_MODE_OFF);
    break;
  default:
    DLOGW("Invalid display status %d", display_status);
    return -EINVAL;
  }

  if (display_status == kDisplayStatusResume ||
      display_status == kDisplayStatusPause) {
    hwc_procs->invalidate(hwc_procs);
  }

  return status;
}

int HWCDisplay::SetCursorPosition(int x, int y) {
  DisplayError error = kErrorNone;

  if (shutdown_pending_) {
    return 0;
  }

  error = display_intf_->SetCursorPosition(x, y);
  if (error != kErrorNone) {
    if (error == kErrorShutDown) {
      shutdown_pending_ = true;
      return 0;
    }
    DLOGE("Failed for x = %d y = %d, Error = %d", x, y, error);
    return -1;
  }

  return 0;
}

int HWCDisplay::OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) {
  DisplayError error = display_intf_->OnMinHdcpEncryptionLevelChange(min_enc_level);
  if (error != kErrorNone) {
    DLOGE("Failed. Error = %d", error);
    return -1;
  }

  return 0;
}

void HWCDisplay::MarkLayersForGPUBypass(hwc_display_contents_1_t *content_list) {
  for (size_t i = 0 ; i < (content_list->numHwLayers - 1); i++) {
    hwc_layer_1_t *layer = &content_list->hwLayers[i];
    layer->compositionType = HWC_OVERLAY;
  }
}

void HWCDisplay::ApplyScanAdjustment(hwc_rect_t *display_frame) {
}

DisplayError HWCDisplay::SetCSC(const MetaData_t *meta_data, ColorMetaData *color_metadata) {
  if (meta_data->operation & COLOR_METADATA) {
#ifdef USE_COLOR_METADATA
    *color_metadata = meta_data->color;
#endif
  } else if (meta_data->operation & UPDATE_COLOR_SPACE) {
    ColorSpace_t csc = meta_data->colorSpace;
    color_metadata->range = Range_Limited;

    if (csc == ITU_R_601_FR || csc == ITU_R_2020_FR) {
      color_metadata->range = Range_Full;
    }

    switch (csc) {
    case ITU_R_601:
    case ITU_R_601_FR:
      // display driver uses 601 irrespective of 525 or 625
      color_metadata->colorPrimaries = ColorPrimaries_BT601_6_525;
      break;
    case ITU_R_709:
      color_metadata->colorPrimaries = ColorPrimaries_BT709_5;
      break;
    case ITU_R_2020:
    case ITU_R_2020_FR:
      color_metadata->colorPrimaries = ColorPrimaries_BT2020;
      break;
    default:
      DLOGE("Unsupported CSC: %d", csc);
      return kErrorNotSupported;
    }
  }

  return kErrorNone;
}

DisplayError HWCDisplay::SetIGC(IGC_t source, LayerIGC *target) {
  switch (source) {
  case IGC_NotSpecified:    *target = kIGCNotSpecified; break;
  case IGC_sRGB:            *target = kIGCsRGB;   break;
  default:
    DLOGE("Unsupported IGC: %d", source);
    return kErrorNotSupported;
  }

  return kErrorNone;
}

DisplayError HWCDisplay::SetMetaData(const private_handle_t *pvt_handle, Layer *layer) {
  const MetaData_t *meta_data = reinterpret_cast<MetaData_t *>(pvt_handle->base_metadata);
  LayerBuffer &layer_buffer = layer->input_buffer;

  if (!meta_data) {
    return kErrorNone;
  }

  if (SetCSC(meta_data, &layer_buffer.color_metadata) != kErrorNone) {
    return kErrorNotSupported;
  }

  bool hdr_layer = layer_buffer.color_metadata.colorPrimaries == ColorPrimaries_BT2020 &&
                   (layer_buffer.color_metadata.transfer == Transfer_SMPTE_ST2084 ||
                   layer_buffer.color_metadata.transfer == Transfer_HLG);
  if (hdr_layer && !disable_hdr_handling_) {
    // dont honor HDR when its handling is disabled
    layer_buffer.flags.hdr = true;
    layer_stack_.flags.hdr_present = true;
  }

  if (meta_data->operation & SET_IGC) {
    if (SetIGC(meta_data->igc, &layer_buffer.igc) != kErrorNone) {
      return kErrorNotSupported;
    }
  }

  if (meta_data->operation & UPDATE_REFRESH_RATE) {
    layer->frame_rate = RoundToStandardFPS(meta_data->refreshrate);
  }

  if ((meta_data->operation & PP_PARAM_INTERLACED) && meta_data->interlaced) {
    layer_buffer.flags.interlace = true;
  }

  if (meta_data->operation & LINEAR_FORMAT) {
    layer_buffer.format = GetSDMFormat(INT32(meta_data->linearFormat), 0);
  }

  if (meta_data->operation & SET_SINGLE_BUFFER_MODE) {
    layer->flags.single_buffer = meta_data->isSingleBufferMode;
    // Graphics can set this operation on all types of layers including FB and set the actual value
    // to 0. To protect against SET operations of 0 value, we need to do a logical OR.
    layer_stack_.flags.single_buffered_layer_present |= meta_data->isSingleBufferMode;
  }

  if (meta_data->operation & S3D_FORMAT) {
    std::map<int, LayerBufferS3DFormat>::iterator it =
        s3d_format_hwc_to_sdm_.find(INT32(meta_data->s3dFormat));
    if (it != s3d_format_hwc_to_sdm_.end()) {
      layer->input_buffer.s3d_format = it->second;
    } else {
      DLOGW("Invalid S3D format %d", meta_data->s3dFormat);
    }
  }

  return kErrorNone;
}

int HWCDisplay::SetPanelBrightness(int level) {
  int ret = 0;
  if (display_intf_)
    ret = display_intf_->SetPanelBrightness(level);
  else
    ret = -EINVAL;

  return ret;
}

int HWCDisplay::GetPanelBrightness(int *level) {
  return display_intf_->GetPanelBrightness(level);
}

int HWCDisplay::CachePanelBrightness(int level) {
  int ret = 0;
  if (display_intf_)
    ret = display_intf_->CachePanelBrightness(level);
  else
    ret = -EINVAL;

  return ret;
}

int HWCDisplay::ToggleScreenUpdates(bool enable) {
  const hwc_procs_t *hwc_procs = *hwc_procs_;
  display_paused_ = enable ? false : true;
  hwc_procs->invalidate(hwc_procs);
  return 0;
}

int HWCDisplay::ColorSVCRequestRoute(const PPDisplayAPIPayload &in_payload,
                                     PPDisplayAPIPayload *out_payload,
                                     PPPendingParams *pending_action) {
  int ret = 0;

  if (display_intf_)
    ret = display_intf_->ColorSVCRequestRoute(in_payload, out_payload, pending_action);
  else
    ret = -EINVAL;

  return ret;
}

int HWCDisplay::GetVisibleDisplayRect(hwc_rect_t* visible_rect) {
  if (!IsValid(display_rect_)) {
    return -EINVAL;
  }

  visible_rect->left = INT(display_rect_.left);
  visible_rect->top = INT(display_rect_.top);
  visible_rect->right = INT(display_rect_.right);
  visible_rect->bottom = INT(display_rect_.bottom);
  DLOGI("Dpy = %d Visible Display Rect(%d %d %d %d)", visible_rect->left, visible_rect->top,
        visible_rect->right, visible_rect->bottom);

  return 0;
}

void HWCDisplay::SetSecureDisplay(bool secure_display_active, bool force_flush) {
  secure_display_active_ = secure_display_active;
  return;
}

int HWCDisplay::SetActiveDisplayConfig(int config) {
  return display_intf_->SetActiveConfig(UINT32(config)) == kErrorNone ? 0 : -1;
}

int HWCDisplay::GetActiveDisplayConfig(uint32_t *config) {
  return display_intf_->GetActiveConfig(config) == kErrorNone ? 0 : -1;
}

int HWCDisplay::GetDisplayConfigCount(uint32_t *count) {
  return display_intf_->GetNumVariableInfoConfigs(count) == kErrorNone ? 0 : -1;
}

int HWCDisplay::GetDisplayAttributesForConfig(int config,
                                            DisplayConfigVariableInfo *display_attributes) {
  return display_intf_->GetConfig(UINT32(config), display_attributes) == kErrorNone ? 0 : -1;
}

int HWCDisplay::GetDisplayFixedConfig(DisplayConfigFixedInfo *fixed_info) {
  return display_intf_->GetConfig(fixed_info) == kErrorNone ? 0 : -1;
}

// TODO(user): HWC needs to know updating for dyn_fps, cpu hint features,
// once the features are moved to SDM, the two functions below can be removed.
uint32_t HWCDisplay::GetUpdatingLayersCount(uint32_t app_layer_count) {
  uint32_t updating_count = 0;

  for (uint i = 0; i < app_layer_count; i++) {
    Layer *layer = layer_stack_.layers.at(i);
    if (layer->flags.updating) {
      updating_count++;
    }
  }

  return updating_count;
}

bool HWCDisplay::SingleVideoLayerUpdating(uint32_t app_layer_count) {
  uint32_t updating_count = 0;

  for (uint i = 0; i < app_layer_count; i++) {
    Layer *layer = layer_stack_.layers[i];
    // TODO(user): disable DRC feature in S3D playbacl case.S3D video
    // need play in dedicate resolution and fps, if DRC switch the
    // mode to an non S3D supported mode, it would break S3D playback.
    // Need figure out a way to make S3D and DRC co-exist.
    if (layer->flags.updating && (layer->input_buffer.flags.video == true) &&
       (layer->input_buffer.s3d_format == kS3dFormatNone)) {
      updating_count++;
    }
  }

  return (updating_count == 1);
}

uint32_t HWCDisplay::RoundToStandardFPS(float fps) {
  static const uint32_t standard_fps[4] = {30, 24, 48, 60};
  uint32_t frame_rate = (uint32_t)(fps);

  int count = INT(sizeof(standard_fps) / sizeof(standard_fps[0]));
  for (int i = 0; i < count; i++) {
    if ((standard_fps[i] - frame_rate) < 2) {
      // Most likely used for video, the fps can fluctuate
      // Ex: b/w 29 and 30 for 30 fps clip
      return standard_fps[i];
    }
  }

  return frame_rate;
}

uint32_t HWCDisplay::SanitizeRefreshRate(uint32_t req_refresh_rate) {
  uint32_t refresh_rate = req_refresh_rate;

  if (refresh_rate < min_refresh_rate_) {
    // Pick the next multiple of request which is within the range
    refresh_rate = (((min_refresh_rate_ / refresh_rate) +
                     ((min_refresh_rate_ % refresh_rate) ? 1 : 0)) * refresh_rate);
  }

  if (refresh_rate > max_refresh_rate_) {
    refresh_rate = max_refresh_rate_;
  }

  return refresh_rate;
}

DisplayClass HWCDisplay::GetDisplayClass() {
  return display_class_;
}

void HWCDisplay::PrepareDynamicRefreshRate(Layer *layer) {
  if (layer->frame_rate > metadata_refresh_rate_) {
    metadata_refresh_rate_ = SanitizeRefreshRate(layer->frame_rate);
  } else {
    layer->frame_rate = current_refresh_rate_;
  }
}

bool HWCDisplay::IsSurfaceUpdated(const std::vector<LayerRect> &dirty_regions) {
  // based on dirty_regions determine if its updating
  // dirty_rect count = 0 - whole layer - updating.
  // dirty_rect count = 1 or more valid rects - updating.
  // dirty_rect count = 1 with (0,0,0,0) - not updating.
  return (dirty_regions.empty() || IsValid(dirty_regions.at(0)));
}

int HWCDisplay::GetDisplayPort(DisplayPort *port) {
  return display_intf_->GetDisplayPort(port) == kErrorNone ? 0 : -1;
}


}  // namespace sdm
