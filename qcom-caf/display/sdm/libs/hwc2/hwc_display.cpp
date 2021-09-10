/*
 * Copyright (c) 2014-2020, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cutils/properties.h>
#include <errno.h>
#include <math.h>
#include <sync/sync.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <utils/formats.h>
#include <utils/rect.h>
#include <qd_utils.h>

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "hwc_display.h"
#include "hwc_debugger.h"
#include "blit_engine_c2d.h"
#include "hwc_tonemapper.h"

#ifndef USE_GRALLOC1
#include <gr.h>
#endif

#ifdef QTI_BSP
#include <hardware/display_defs.h>
#endif

#define __CLASS__ "HWCDisplay"

namespace sdm {

std::bitset<kDisplayMax> HWCDisplay::validated_ = 0;

// This weight function is needed because the color primaries are not sorted by gamut size
static ColorPrimaries WidestPrimaries(ColorPrimaries p1, ColorPrimaries p2) {
  int weight = 10;
  int lp1 = p1, lp2 = p2;
  // TODO(user) add weight to other wide gamut primaries
  if (lp1 == ColorPrimaries_BT2020) {
    lp1 *= weight;
  }
  if (lp1 == ColorPrimaries_BT2020) {
    lp2 *= weight;
  }
  if (lp1 >= lp2) {
    return p1;
  } else {
    return p2;
  }
}

HWCColorMode::HWCColorMode(DisplayInterface *display_intf) : display_intf_(display_intf) {}

HWC2::Error HWCColorMode::Init() {
  PopulateColorModes();
  return ApplyDefaultColorMode();
}

HWC2::Error HWCColorMode::DeInit() {
  color_mode_transform_map_.clear();
  return HWC2::Error::None;
}

uint32_t HWCColorMode::GetColorModeCount() {
  uint32_t count = UINT32(color_mode_transform_map_.size());
  DLOGI("Supported color mode count = %d", count);
#ifdef EXCLUDE_DISPLAY_PP
  return count;
#else
  return std::max(1U, count);
#endif
}

HWC2::Error HWCColorMode::GetColorModes(uint32_t *out_num_modes,
                                        android_color_mode_t *out_modes) {
  auto it = color_mode_transform_map_.begin();
  for (auto i = 0; it != color_mode_transform_map_.end(); it++, i++) {
    out_modes[i] = it->first;
    DLOGI("Supports color mode[%d] = %d", i, it->first);
  }
  *out_num_modes = UINT32(color_mode_transform_map_.size());
  return HWC2::Error::None;
}

HWC2::Error HWCColorMode::SetColorMode(android_color_mode_t mode) {
  // first mode in 2D matrix is the mode (identity)
  if (mode < HAL_COLOR_MODE_NATIVE || mode > HAL_COLOR_MODE_DISPLAY_P3) {
    DLOGE("Could not find mode: %d", mode);
    return HWC2::Error::BadParameter;
  }
  if (color_mode_transform_map_.find(mode) == color_mode_transform_map_.end()) {
    return HWC2::Error::Unsupported;
  }

  auto status = HandleColorModeTransform(mode, current_color_transform_, color_matrix_);
  if (status != HWC2::Error::None) {
    DLOGE("failed for mode = %d", mode);
  }

  DLOGV_IF(kTagClient, "Color mode %d successfully set.", mode);
  return status;
}

HWC2::Error HWCColorMode::RestoreColorTransform() {
  DisplayError error = display_intf_->SetColorTransform(kColorTransformMatrixCount, color_matrix_);
  if (error != kErrorNone) {
    DLOGI_IF(kTagClient,"Failed to set Color Transform");
    return HWC2::Error::BadParameter;
  }

  return HWC2::Error::None;
}

HWC2::Error HWCColorMode::SetColorTransform(const float *matrix, android_color_transform_t hint) {
  DTRACE_SCOPED();
  double color_matrix[kColorTransformMatrixCount] = {0};
  CopyColorTransformMatrix(matrix, color_matrix);

  auto status = HandleColorModeTransform(current_color_mode_, hint, color_matrix);
  if (status != HWC2::Error::None) {
    DLOGE("failed for hint = %d", hint);
  }

  return status;
}

HWC2::Error HWCColorMode::HandleColorModeTransform(android_color_mode_t mode,
                                                   android_color_transform_t hint,
                                                   const double *matrix) {
  android_color_transform_t transform_hint = hint;
  std::string color_mode_transform;
  bool use_matrix = false;
  if (hint != HAL_COLOR_TRANSFORM_ARBITRARY_MATRIX) {
    // if the mode + transfrom request from HWC matches one mode in SDM, set that
    if (color_mode_transform.empty()) {
      transform_hint = HAL_COLOR_TRANSFORM_IDENTITY;
      use_matrix = true;
    } else {
      color_mode_transform = color_mode_transform_map_[mode][hint];
    }
  } else {
    use_matrix = true;
    transform_hint = HAL_COLOR_TRANSFORM_IDENTITY;
  }

  // if the mode count is 1, then only native mode is supported, so just apply matrix w/o
  // setting mode
  if (color_mode_transform_map_.size() > 1U && current_color_mode_ != mode) {
    color_mode_transform = color_mode_transform_map_[mode][transform_hint];
    DisplayError error = display_intf_->SetColorMode(color_mode_transform);
    if (error != kErrorNone) {
      DLOGE("Failed to set color_mode  = %d transform_hint = %d", mode, hint);
      // failure to force client composition
      return HWC2::Error::Unsupported;
    }
    DLOGI("Setting Color Mode = %d Transform Hint = %d Success", mode, hint);
  }

  if (use_matrix) {
    DisplayError error = display_intf_->SetColorTransform(kColorTransformMatrixCount, matrix);
    if (error != kErrorNone) {
      DLOGE("Failed to set Color Transform Matrix");
      // failure to force client composition
      return HWC2::Error::Unsupported;
    }
  }

  current_color_mode_ = mode;
  current_color_transform_ = hint;
  CopyColorTransformMatrix(matrix, color_matrix_);

  return HWC2::Error::None;
}

void HWCColorMode::PopulateColorModes() {
  uint32_t color_mode_count = 0;
  // SDM returns modes which is string combination of mode + transform.
  DisplayError error = display_intf_->GetColorModeCount(&color_mode_count);
  if (error != kErrorNone || (color_mode_count == 0)) {
#ifndef EXCLUDE_DISPLAY_PP
    DLOGW("GetColorModeCount failed, use native color mode");
    PopulateTransform(HAL_COLOR_MODE_NATIVE, "native", "identity");
#endif
    return;
  }

  DLOGV_IF(kTagClient, "Color Modes supported count = %d", color_mode_count);

  const std::string color_transform = "identity";
  std::vector<std::string> color_modes(color_mode_count);
  error = display_intf_->GetColorModes(&color_mode_count, &color_modes);
  for (uint32_t i = 0; i < color_mode_count; i++) {
    std::string &mode_string = color_modes.at(i);
    DLOGV_IF(kTagClient, "Color Mode[%d] = %s", i, mode_string.c_str());
    AttrVal attr;
    error = display_intf_->GetColorModeAttr(mode_string, &attr);
    std::string color_gamut, dynamic_range, pic_quality;
    if (!attr.empty()) {
      for (auto &it : attr) {
        if (it.first.find(kColorGamutAttribute) != std::string::npos) {
          color_gamut = it.second;
        } else if (it.first.find(kDynamicRangeAttribute) != std::string::npos) {
          dynamic_range = it.second;
        } else if (it.first.find(kPictureQualityAttribute) != std::string::npos) {
          pic_quality = it.second;
        }
      }

      DLOGV_IF(kTagClient, "color_gamut : %s, dynamic_range : %s, pic_quality : %s",
               color_gamut.c_str(), dynamic_range.c_str(), pic_quality.c_str());

      if (dynamic_range == kHdr) {
        continue;
      }
      if ((color_gamut == kNative) &&
          (pic_quality.empty() || pic_quality == kStandard)) {
        PopulateTransform(HAL_COLOR_MODE_NATIVE, mode_string, color_transform);
      } else if ((color_gamut == kSrgb) &&
                 (pic_quality.empty() || pic_quality == kStandard)) {
        PopulateTransform(HAL_COLOR_MODE_SRGB, mode_string, color_transform);
      } else if ((color_gamut == kDcip3) &&
                 (pic_quality.empty() || pic_quality == kStandard)) {
        PopulateTransform(HAL_COLOR_MODE_DISPLAY_P3, mode_string, color_transform);
      } else if ((color_gamut == kDisplayP3) &&
                 (pic_quality.empty() || pic_quality == kStandard)) {
        PopulateTransform(HAL_COLOR_MODE_DISPLAY_P3, mode_string, color_transform);
      }
    }

    // Look at the mode name, if no color gamut is found
    if (color_gamut.empty()) {
      if (mode_string.find("hal_native") != std::string::npos) {
        PopulateTransform(HAL_COLOR_MODE_NATIVE, mode_string, mode_string);
      } else if (mode_string.find("hal_srgb") != std::string::npos) {
        PopulateTransform(HAL_COLOR_MODE_SRGB, mode_string, mode_string);
      } else if (mode_string.find("hal_adobe") != std::string::npos) {
        PopulateTransform(HAL_COLOR_MODE_ADOBE_RGB, mode_string, mode_string);
      } else if (mode_string.find("hal_dci_p3") != std::string::npos) {
        PopulateTransform(HAL_COLOR_MODE_DCI_P3, mode_string, mode_string);
      } else if (mode_string.find("hal_display_p3") != std::string::npos) {
        PopulateTransform(HAL_COLOR_MODE_DISPLAY_P3, mode_string, mode_string);
      }
    }
  }
}

void HWCColorMode::PopulateTransform(const android_color_mode_t &mode,
                                     const std::string &color_mode,
                                     const std::string &color_transform) {
  // TODO(user): Check the substring from QDCM
  if (color_transform.find("identity") != std::string::npos) {
    color_mode_transform_map_[mode][HAL_COLOR_TRANSFORM_IDENTITY] = color_mode;
  } else if (color_transform.find("arbitrary") != std::string::npos) {
    // no color mode for arbitrary
  } else if (color_transform.find("inverse") != std::string::npos) {
    color_mode_transform_map_[mode][HAL_COLOR_TRANSFORM_VALUE_INVERSE] = color_mode;
  } else if (color_transform.find("grayscale") != std::string::npos) {
    color_mode_transform_map_[mode][HAL_COLOR_TRANSFORM_GRAYSCALE] = color_mode;
  } else if (color_transform.find("correct_protonopia") != std::string::npos) {
    color_mode_transform_map_[mode][HAL_COLOR_TRANSFORM_CORRECT_PROTANOPIA] = color_mode;
  } else if (color_transform.find("correct_deuteranopia") != std::string::npos) {
    color_mode_transform_map_[mode][HAL_COLOR_TRANSFORM_CORRECT_DEUTERANOPIA] = color_mode;
  } else if (color_transform.find("correct_tritanopia") != std::string::npos) {
    color_mode_transform_map_[mode][HAL_COLOR_TRANSFORM_CORRECT_TRITANOPIA] = color_mode;
  } else {
    color_mode_transform_map_[mode][HAL_COLOR_TRANSFORM_IDENTITY] = color_mode;
  }
}

HWC2::Error HWCColorMode::ApplyDefaultColorMode() {
  android_color_mode_t color_mode = HAL_COLOR_MODE_NATIVE;
  if (color_mode_transform_map_.size() == 1U) {
    color_mode = color_mode_transform_map_.begin()->first;
  } else if (color_mode_transform_map_.size() > 1U) {
    std::string default_color_mode;
    bool found = false;
    DisplayError error = display_intf_->GetDefaultColorMode(&default_color_mode);
    if (error == kErrorNone) {
      // get the default mode corresponding android_color_mode_t
      for (auto &it_mode : color_mode_transform_map_) {
        for (auto &it : it_mode.second) {
          if (it.second == default_color_mode) {
            found = true;
            break;
          }
        }
        if (found) {
          color_mode = it_mode.first;
          break;
        }
      }
    }

    // return the first andrid_color_mode_t when we encouter if not found
    if (!found) {
      color_mode = color_mode_transform_map_.begin()->first;
    }
  }
  return SetColorMode(color_mode);
}

void HWCColorMode::Dump(std::ostringstream* os) {
  *os << "color modes supported: ";
  for (auto it : color_mode_transform_map_) {
    *os << it.first <<" ";
  }
  *os << "current mode: " << current_color_mode_ << std::endl;
  *os << "current transform: ";
  for (uint32_t i = 0; i < kColorTransformMatrixCount; i++) {
    if (i % 4 == 0) {
     *os << std::endl;
    }
    *os << std::fixed << std::setprecision(2) << std::setw(6) << std::setfill(' ')
        << color_matrix_[i] << " ";
  }
  *os << std::endl;
}

HWCDisplay::HWCDisplay(CoreInterface *core_intf, HWCCallbacks *callbacks, DisplayType type,
                       hwc2_display_t id, bool needs_blit, qService::QService *qservice,
                       DisplayClass display_class, BufferAllocator *buffer_allocator)
    : core_intf_(core_intf),
      callbacks_(callbacks),
      type_(type),
      id_(id),
      needs_blit_(needs_blit),
      qservice_(qservice),
      display_class_(display_class) {
  buffer_allocator_ = static_cast<HWCBufferAllocator *>(buffer_allocator);
}

int HWCDisplay::Init() {
  DisplayError error = core_intf_->CreateDisplay(type_, this, &display_intf_);
  if (error != kErrorNone) {
    DLOGE("Display create failed. Error = %d display_type %d event_handler %p disp_intf %p", error,
          type_, this, &display_intf_);
    return -EINVAL;
  }

  validated_.reset();
  HWCDebugHandler::Get()->GetProperty(DISABLE_HDR, &disable_hdr_handling_);
  if (disable_hdr_handling_) {
    DLOGI("HDR Handling disabled");
  }

  int property_swap_interval = 1;
  HWCDebugHandler::Get()->GetProperty(ZERO_SWAP_INTERVAL, &property_swap_interval);
  if (property_swap_interval == 0) {
    swap_interval_zero_ = true;
  }

  client_target_ = new HWCLayer(id_, buffer_allocator_);

  fbt_valid_ = false;

  int blit_enabled = 0;
  HWCDebugHandler::Get()->GetProperty(DISABLE_BLIT_COMPOSITION_PROP, &blit_enabled);
  if (needs_blit_ && blit_enabled) {
    // TODO(user): Add blit engine when needed
  }

  error = display_intf_->GetNumVariableInfoConfigs(&num_configs_);
  if (error != kErrorNone) {
    DLOGE("Getting config count failed. Error = %d", error);
    return -EINVAL;
  }

  tone_mapper_ = new HWCToneMapper(buffer_allocator_);

  display_intf_->GetRefreshRateRange(&min_refresh_rate_, &max_refresh_rate_);
  current_refresh_rate_ = max_refresh_rate_;

  GetUnderScanConfig();
  DLOGI("Display created with id: %d", id_);

  return 0;
}

int HWCDisplay::Deinit() {
  DisplayError error = core_intf_->DestroyDisplay(display_intf_);
  if (error != kErrorNone) {
    DLOGE("Display destroy failed. Error = %d", error);
    return -EINVAL;
  }

  delete client_target_;
  for (auto hwc_layer : layer_set_) {
    delete hwc_layer;
  }

  if (color_mode_) {
    color_mode_->DeInit();
    delete color_mode_;
  }

  delete tone_mapper_;
  tone_mapper_ = nullptr;

  return 0;
}

// LayerStack operations
HWC2::Error HWCDisplay::CreateLayer(hwc2_layer_t *out_layer_id) {
  HWCLayer *layer = *layer_set_.emplace(new HWCLayer(id_, buffer_allocator_));
  layer_map_.emplace(std::make_pair(layer->GetId(), layer));
  *out_layer_id = layer->GetId();
  geometry_changes_ |= GeometryChanges::kAdded;
  validated_.reset();
  layer_stack_invalid_ = true;

  return HWC2::Error::None;
}

HWCLayer *HWCDisplay::GetHWCLayer(hwc2_layer_t layer_id) {
  const auto map_layer = layer_map_.find(layer_id);
  if (map_layer == layer_map_.end()) {
    DLOGE("[%" PRIu64 "] GetLayer(%" PRIu64 ") failed: no such layer", id_, layer_id);
    return nullptr;
  } else {
    return map_layer->second;
  }
}

HWC2::Error HWCDisplay::DestroyLayer(hwc2_layer_t layer_id) {
  const auto map_layer = layer_map_.find(layer_id);
  if (map_layer == layer_map_.end()) {
    DLOGE("[%" PRIu64 "] destroyLayer(%" PRIu64 ") failed: no such layer", id_, layer_id);
    return HWC2::Error::BadLayer;
  }
  const auto layer = map_layer->second;
  layer_map_.erase(map_layer);
  const auto z_range = layer_set_.equal_range(layer);
  for (auto current = z_range.first; current != z_range.second; ++current) {
    if (*current == layer) {
      current = layer_set_.erase(current);
      delete layer;
      break;
    }
  }

  geometry_changes_ |= GeometryChanges::kRemoved;
  validated_.reset();
  layer_stack_invalid_ = true;

  return HWC2::Error::None;
}


void HWCDisplay::BuildLayerStack() {
  layer_stack_ = LayerStack();
  display_rect_ = LayerRect();
  metadata_refresh_rate_ = 0;
  auto working_primaries = ColorPrimaries_BT709_5;
  bool secure_display_active = false;
  layer_stack_.flags.animating = animating_;

  // Add one layer for fb target
  // TODO(user): Add blit target layers
  for (auto hwc_layer : layer_set_) {
    Layer *layer = hwc_layer->GetSDMLayer();
    layer->flags = {};   // Reset earlier flags
    if (hwc_layer->GetClientRequestedCompositionType() == HWC2::Composition::Client) {
      layer->flags.skip = true;
    } else if (hwc_layer->GetClientRequestedCompositionType() == HWC2::Composition::SolidColor) {
      layer->flags.solid_fill = true;
    }

    if (!hwc_layer->ValidateAndSetCSC()) {
#ifdef FEATURE_WIDE_COLOR
      layer->flags.skip = true;
#endif
    }

    working_primaries = WidestPrimaries(working_primaries,
                                        layer->input_buffer.color_metadata.colorPrimaries);

    // set default composition as GPU for SDM
    layer->composition = kCompositionGPU;

    if (swap_interval_zero_) {
      if (layer->input_buffer.acquire_fence_fd >= 0) {
        close(layer->input_buffer.acquire_fence_fd);
        layer->input_buffer.acquire_fence_fd = -1;
      }
    }

    bool is_secure = false;
    const private_handle_t *handle =
        reinterpret_cast<const private_handle_t *>(layer->input_buffer.buffer_id);
    if (handle) {
#ifdef USE_GRALLOC1
      if (handle->buffer_type == BUFFER_TYPE_VIDEO) {
#else
      if (handle->bufferType == BUFFER_TYPE_VIDEO) {
#endif
        layer_stack_.flags.video_present = true;
      }
      // TZ Protected Buffer - L1
      // Gralloc Usage Protected Buffer - L3 - which needs to be treated as Secure & avoid fallback
      if (handle->flags & private_handle_t::PRIV_FLAGS_PROTECTED_BUFFER ||
          handle->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
        layer_stack_.flags.secure_present = true;
        is_secure = true;
      }
    }

    if (layer->input_buffer.flags.secure_display) {
      secure_display_active = true;
      is_secure = true;
    }

    if (hwc_layer->GetClientRequestedCompositionType() == HWC2::Composition::Cursor) {
      // Currently we support only one HWCursor & only at top most z-order
      if ((*layer_set_.rbegin())->GetId() == hwc_layer->GetId()) {
        layer->flags.cursor = true;
        layer_stack_.flags.cursor_present = true;
      }
    }

    bool hdr_layer = layer->input_buffer.color_metadata.colorPrimaries == ColorPrimaries_BT2020 &&
                     (layer->input_buffer.color_metadata.transfer == Transfer_SMPTE_ST2084 ||
                     layer->input_buffer.color_metadata.transfer == Transfer_HLG);
    if (hdr_layer && !disable_hdr_handling_) {
      // dont honor HDR when its handling is disabled
      layer->input_buffer.flags.hdr = true;
      layer_stack_.flags.hdr_present = true;
    }

    if (hwc_layer->IsNonIntegralSourceCrop() && !is_secure && !hdr_layer &&
        !layer->flags.single_buffer && !layer->flags.solid_fill) {
      layer->flags.skip = true;
    }

    if (layer->flags.skip) {
      layer_stack_.flags.skip_present = true;
    }

    // TODO(user): Move to a getter if this is needed at other places
    hwc_rect_t scaled_display_frame = {INT(layer->dst_rect.left), INT(layer->dst_rect.top),
                                       INT(layer->dst_rect.right), INT(layer->dst_rect.bottom)};
    if (hwc_layer->GetGeometryChanges() & kDisplayFrame) {
      ApplyScanAdjustment(&scaled_display_frame);
    }
    hwc_layer->SetLayerDisplayFrame(scaled_display_frame);
    // SDM requires these details even for solid fill
    if (layer->flags.solid_fill) {
      LayerBuffer *layer_buffer = &layer->input_buffer;
      layer_buffer->width = UINT32(layer->dst_rect.right - layer->dst_rect.left);
      layer_buffer->height = UINT32(layer->dst_rect.bottom - layer->dst_rect.top);
      layer_buffer->unaligned_width = layer_buffer->width;
      layer_buffer->unaligned_height = layer_buffer->height;
      layer_buffer->acquire_fence_fd = -1;
      layer_buffer->release_fence_fd = -1;
      layer->src_rect.left = 0;
      layer->src_rect.top = 0;
      layer->src_rect.right = layer_buffer->width;
      layer->src_rect.bottom = layer_buffer->height;
    }

    if (layer->frame_rate > metadata_refresh_rate_) {
      metadata_refresh_rate_ = SanitizeRefreshRate(layer->frame_rate);
    } else {
      layer->frame_rate = current_refresh_rate_;
    }
    display_rect_ = Union(display_rect_, layer->dst_rect);
    geometry_changes_ |= hwc_layer->GetGeometryChanges();

    layer->flags.updating = true;
    if (layer_set_.size() <= kMaxLayerCount) {
      layer->flags.updating = IsLayerUpdating(layer);
    }

    layer_stack_.layers.push_back(layer);
  }


#ifdef FEATURE_WIDE_COLOR
  for (auto hwc_layer : layer_set_) {
    auto layer = hwc_layer->GetSDMLayer();
    if (layer->input_buffer.color_metadata.colorPrimaries != working_primaries &&
        !hwc_layer->SupportLocalConversion(working_primaries)) {
      layer->flags.skip = true;
    }
    if (layer->flags.skip) {
      layer_stack_.flags.skip_present = true;
    }
  }
#endif

  layer_stack_.flags.fbt_valid = fbt_valid_;
  // TODO(user): Set correctly when SDM supports geometry_changes as bitmask
  layer_stack_.flags.geometry_changed = UINT32(geometry_changes_ > 0);
  // Append client target to the layer stack

  Layer *sdm_client_target = client_target_->GetSDMLayer();
  layer_stack_.layers.push_back(sdm_client_target);
  // fall back frame composition to GPU when client target is 10bit
  // TODO(user): clarify the behaviour from Client(SF) and SDM Extn -
  // when handling 10bit FBT, as it would affect blending
  if (Is10BitFormat(sdm_client_target->input_buffer.format)) {
    // Must fall back to client composition
    MarkLayersForClientComposition();
  }

  // set secure display
  SetSecureDisplay(secure_display_active);

  layer_stack_invalid_ = false;
}

void HWCDisplay::BuildSolidFillStack() {
  layer_stack_ = LayerStack();
  display_rect_ = LayerRect();

  layer_stack_.layers.push_back(solid_fill_layer_);
  layer_stack_.flags.geometry_changed = 1U;
  // Append client target to the layer stack
  layer_stack_.layers.push_back(client_target_->GetSDMLayer());
}

HWC2::Error HWCDisplay::SetLayerZOrder(hwc2_layer_t layer_id, uint32_t z) {
  const auto map_layer = layer_map_.find(layer_id);
  if (map_layer == layer_map_.end()) {
    DLOGE("[%" PRIu64 "] updateLayerZ failed to find layer", id_);
    return HWC2::Error::BadLayer;
  }

  const auto layer = map_layer->second;
  const auto z_range = layer_set_.equal_range(layer);
  bool layer_on_display = false;
  for (auto current = z_range.first; current != z_range.second; ++current) {
    if (*current == layer) {
      if ((*current)->GetZ() == z) {
        // Don't change anything if the Z hasn't changed
        return HWC2::Error::None;
      }
      current = layer_set_.erase(current);
      layer_on_display = true;
      break;
    }
  }

  if (!layer_on_display) {
    DLOGE("[%" PRIu64 "] updateLayerZ failed to find layer on display", id_);
    return HWC2::Error::BadLayer;
  }

  layer->SetLayerZOrder(z);
  layer_set_.emplace(layer);
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::SetVsyncEnabled(HWC2::Vsync enabled) {
  DLOGV("Display ID: %d enabled: %s", id_, to_string(enabled).c_str());
  DisplayError error = kErrorNone;

  if (shutdown_pending_ || !callbacks_->VsyncCallbackRegistered()) {
    return HWC2::Error::None;
  }

  bool state;
  if (enabled == HWC2::Vsync::Enable)
    state = true;
  else if (enabled == HWC2::Vsync::Disable)
    state = false;
  else
    return HWC2::Error::BadParameter;

  error = display_intf_->SetVSyncState(state);

  if (error != kErrorNone) {
    if (error == kErrorShutDown) {
      shutdown_pending_ = true;
      return HWC2::Error::None;
    }
    DLOGE("Failed. enabled = %s, error = %d", to_string(enabled).c_str(), error);
    return HWC2::Error::BadDisplay;
  }

  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::SetPowerMode(HWC2::PowerMode mode) {
  DLOGV("display = %d, mode = %s", id_, to_string(mode).c_str());
  DisplayState state = kStateOff;
  bool flush_on_error = flush_on_error_;

  if (shutdown_pending_) {
    return HWC2::Error::None;
  }

  switch (mode) {
    case HWC2::PowerMode::Off:
      // During power off, all of the buffers are released.
      // Do not flush until a buffer is successfully submitted again.
      flush_on_error = false;
      state = kStateOff;
      if (tone_mapper_) {
        tone_mapper_->Terminate();
      }
      break;
    case HWC2::PowerMode::On:
      state = kStateOn;
      last_power_mode_ = HWC2::PowerMode::On;
      break;
    case HWC2::PowerMode::Doze:
      state = kStateDoze;
      last_power_mode_ = HWC2::PowerMode::Doze;
      break;
    case HWC2::PowerMode::DozeSuspend:
      state = kStateDozeSuspend;
      last_power_mode_ = HWC2::PowerMode::DozeSuspend;
      break;
    default:
      return HWC2::Error::BadParameter;
  }

  DisplayError error = display_intf_->SetDisplayState(state);
  validated_.reset();

  if (error == kErrorNone) {
    flush_on_error_ = flush_on_error;
  } else {
    if (error == kErrorShutDown) {
      shutdown_pending_ = true;
      return HWC2::Error::None;
    }
    DLOGE("Set state failed. Error = %d", error);
    return HWC2::Error::BadParameter;
  }

  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetClientTargetSupport(uint32_t width, uint32_t height, int32_t format,
                                               int32_t dataspace) {
  ColorMetaData color_metadata = {};
  if (dataspace != HAL_DATASPACE_UNKNOWN) {
    GetColorPrimary(dataspace, &(color_metadata.colorPrimaries));
    GetTransfer(dataspace, &(color_metadata.transfer));
    GetRange(dataspace, &(color_metadata.range));
  }

  LayerBufferFormat sdm_format = GetSDMFormat(format, 0);
  if (display_intf_->GetClientTargetSupport(width, height, sdm_format,
                                            color_metadata) != kErrorNone) {
    return HWC2::Error::Unsupported;
  }

  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetColorModes(uint32_t *out_num_modes, android_color_mode_t *out_modes) {
  if (out_modes) {
    out_modes[0] = HAL_COLOR_MODE_NATIVE;
  }
  *out_num_modes = 1;

  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetDisplayConfigs(uint32_t *out_num_configs, hwc2_config_t *out_configs) {
  if (out_configs == nullptr) {
    *out_num_configs = num_configs_;
    return HWC2::Error::None;
  }

  *out_num_configs = num_configs_;

  for (uint32_t i = 0; i < num_configs_; i++) {
    out_configs[i] = i;
  }

  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetDisplayAttribute(hwc2_config_t config, HWC2::Attribute attribute,
                                            int32_t *out_value) {
  DisplayConfigVariableInfo variable_config;
  // Get display attributes from config index only if resolution switch is supported.
  // Otherwise always send mixer attributes. This is to support destination scaler.
  if (num_configs_ > 1) {
    if (GetDisplayAttributesForConfig(INT(config), &variable_config) != kErrorNone) {
      DLOGE("Get variable config failed");
      return HWC2::Error::BadDisplay;
    }
  } else {
    if (display_intf_->GetFrameBufferConfig(&variable_config) != kErrorNone) {
      DLOGV("Get variable config failed");
      return HWC2::Error::BadDisplay;
    }
  }

  switch (attribute) {
    case HWC2::Attribute::VsyncPeriod:
      *out_value = INT32(variable_config.vsync_period_ns);
      break;
    case HWC2::Attribute::Width:
      *out_value = INT32(variable_config.x_pixels);
      break;
    case HWC2::Attribute::Height:
      *out_value = INT32(variable_config.y_pixels);
      break;
    case HWC2::Attribute::DpiX:
      *out_value = INT32(variable_config.x_dpi * 1000.0f);
      break;
    case HWC2::Attribute::DpiY:
      *out_value = INT32(variable_config.y_dpi * 1000.0f);
      break;
    default:
      DLOGW("Spurious attribute type = %s", to_string(attribute).c_str());
      *out_value = -1;
      return HWC2::Error::BadConfig;
  }

  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetDisplayName(uint32_t *out_size, char *out_name) {
  // TODO(user): Get panel name and EDID name and populate it here
  if (out_name == nullptr) {
    *out_size = 32;
  } else {
    std::string name;
    switch (id_) {
      case HWC_DISPLAY_PRIMARY:
        name = "Primary Display";
        break;
      case HWC_DISPLAY_EXTERNAL:
        name = "External Display";
        break;
      case HWC_DISPLAY_VIRTUAL:
        name = "Virtual Display";
        break;
      default:
        name = "Unknown";
        break;
    }
    strlcpy(out_name, name.c_str(), name.size());
    *out_size = UINT32(name.size());
  }
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetDisplayType(int32_t *out_type) {
  if (out_type != nullptr) {
    if (id_ == HWC_DISPLAY_VIRTUAL) {
      *out_type = HWC2_DISPLAY_TYPE_VIRTUAL;
    } else {
      *out_type = HWC2_DISPLAY_TYPE_PHYSICAL;
    }
    return HWC2::Error::None;
  } else {
    return HWC2::Error::BadParameter;
  }
}

HWC2::Error HWCDisplay::GetActiveConfig(hwc2_config_t *out_config) {
  if (out_config == nullptr) {
    return HWC2::Error::BadDisplay;
  }

  uint32_t active_index = 0;
  if (GetActiveDisplayConfig(&active_index) != kErrorNone) {
    return HWC2::Error::BadConfig;
  }

  *out_config = active_index;

  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::SetClientTarget(buffer_handle_t target, int32_t acquire_fence,
                                        int32_t dataspace, hwc_region_t damage) {
  // TODO(user): SurfaceFlinger gives us a null pointer here when doing full SDE composition
  // The error is problematic for layer caching as it would overwrite our cached client target.
  // Reported bug 28569722 to resolve this.
  // For now, continue to use the last valid buffer reported to us for layer caching.
  if (target == nullptr) {
    return HWC2::Error::None;
  }

  if (acquire_fence == 0) {
    DLOGV_IF(kTagClient, "Re-using cached buffer");
  }

  client_target_->SetLayerBuffer(target, acquire_fence);
  client_target_->SetLayerSurfaceDamage(damage);
  if (client_target_->GetLayerDataspace() != dataspace) {
    client_target_->SetLayerDataspace(dataspace);
    Layer *sdm_layer = client_target_->GetSDMLayer();
    // Data space would be validated at GetClientTargetSupport, so just use here.
    sdm::GetSDMColorSpace(dataspace, &sdm_layer->input_buffer.color_metadata);
  }
  fbt_valid_ = true;
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::SetActiveConfig(hwc2_config_t config) {
  if (SetActiveDisplayConfig(config) != kErrorNone) {
    return HWC2::Error::BadConfig;
  }

  validated_.reset();
  return HWC2::Error::None;
}

DisplayError HWCDisplay::SetMixerResolution(uint32_t width, uint32_t height) {
  return kErrorNotSupported;
}

void HWCDisplay::SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type) {
  dump_frame_count_ = count;
  dump_frame_index_ = 0;
  dump_input_layers_ = ((bit_mask_layer_type & (1 << INPUT_LAYER_DUMP)) != 0);

  if (tone_mapper_) {
    tone_mapper_->SetFrameDumpConfig(count);
  }

  DLOGI("num_frame_dump %d, input_layer_dump_enable %d", dump_frame_count_, dump_input_layers_);
  validated_.reset();
}

HWC2::PowerMode HWCDisplay::GetLastPowerMode() {
  return last_power_mode_;
}

DisplayError HWCDisplay::VSync(const DisplayEventVSync &vsync) {
  if (is_primary_) {
    callbacks_->Vsync(HWC_DISPLAY_PRIMARY, vsync.timestamp);
    return kErrorNone;
  }
  callbacks_->Vsync(id_, vsync.timestamp);
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

DisplayError HWCDisplay::HandleEvent(DisplayEvent event) {
  switch (event) {
    case kIdleTimeout:
      break;
    case kThermalEvent:
      validated_.reset();
      break;
    default:
      DLOGW("Unknown event: %d", event);
      break;
  }

  return kErrorNone;
}

HWC2::Error HWCDisplay::PrepareLayerStack(uint32_t *out_num_types, uint32_t *out_num_requests) {
  layer_changes_.clear();
  layer_requests_.clear();
  if (shutdown_pending_) {
    return HWC2::Error::BadDisplay;
  }

  if (!skip_prepare_) {
    DisplayError error = display_intf_->Prepare(&layer_stack_);
    if (error != kErrorNone) {
      if (error == kErrorShutDown) {
        shutdown_pending_ = true;
      } else if (error != kErrorPermission) {
        DLOGE("Prepare failed. Error = %d", error);
        // To prevent surfaceflinger infinite wait, flush the previous frame during Commit()
        // so that previous buffer and fences are released, and override the error.
        flush_ = true;
      }
      return HWC2::Error::BadDisplay;
    } else {
      validated_.set(type_);
    }
  } else {
    // Skip is not set
    MarkLayersForGPUBypass();
    skip_prepare_ = false;
    DLOGI("SecureDisplay %s, Skip Prepare/Commit and Flush",
          secure_display_active_ ? "Starting" : "Stopping");
    flush_ = true;
  }

  for (auto hwc_layer : layer_set_) {
    Layer *layer = hwc_layer->GetSDMLayer();
    LayerComposition &composition = layer->composition;

    if ((composition == kCompositionSDE) || (composition == kCompositionHybrid) ||
        (composition == kCompositionBlit)) {
      layer_requests_[hwc_layer->GetId()] = HWC2::LayerRequest::ClearClientTarget;
    }

    HWC2::Composition requested_composition = hwc_layer->GetClientRequestedCompositionType();
    // Set SDM composition to HWC2 type in HWCLayer
    hwc_layer->SetComposition(composition);
    HWC2::Composition device_composition  = hwc_layer->GetDeviceSelectedCompositionType();
    // Update the changes list only if the requested composition is different from SDM comp type
    // TODO(user): Take Care of other comptypes(BLIT)
    if (requested_composition != device_composition) {
      layer_changes_[hwc_layer->GetId()] = device_composition;
    }
    hwc_layer->ResetValidation();
  }
  client_target_->ResetValidation();
  *out_num_types = UINT32(layer_changes_.size());
  *out_num_requests = UINT32(layer_requests_.size());
  skip_validate_ = false;
  if (*out_num_types > 0) {
    return HWC2::Error::HasChanges;
  } else {
    return HWC2::Error::None;
  }
}

HWC2::Error HWCDisplay::AcceptDisplayChanges() {
  if (layer_set_.empty()) {
    return HWC2::Error::None;
  }

  if (!validated_.test(type_)) {
    return HWC2::Error::NotValidated;
  }

  for (const auto& change : layer_changes_) {
    auto hwc_layer = layer_map_[change.first];
    auto composition = change.second;
    if (hwc_layer != nullptr) {
      hwc_layer->UpdateClientCompositionType(composition);
    } else {
      DLOGW("Invalid layer: %" PRIu64, change.first);
    }
  }
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetChangedCompositionTypes(uint32_t *out_num_elements,
                                                   hwc2_layer_t *out_layers, int32_t *out_types) {
  if (layer_set_.empty()) {
    return HWC2::Error::None;
  }

  if (!validated_.test(type_)) {
    DLOGW("Display is not validated");
    return HWC2::Error::NotValidated;
  }

  *out_num_elements = UINT32(layer_changes_.size());
  if (out_layers != nullptr && out_types != nullptr) {
    int i = 0;
    for (auto change : layer_changes_) {
      out_layers[i] = change.first;
      out_types[i] = INT32(change.second);
      i++;
    }
  }
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetReleaseFences(uint32_t *out_num_elements, hwc2_layer_t *out_layers,
                                         int32_t *out_fences) {
  if (out_layers != nullptr && out_fences != nullptr) {
    int i = 0;
    for (auto hwc_layer : layer_set_) {
      out_layers[i] = hwc_layer->GetId();
      out_fences[i] = hwc_layer->PopReleaseFence();
      i++;
    }
  }
  *out_num_elements = UINT32(layer_set_.size());
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetDisplayRequests(int32_t *out_display_requests,
                                           uint32_t *out_num_elements, hwc2_layer_t *out_layers,
                                           int32_t *out_layer_requests) {
  if (layer_set_.empty()) {
    return HWC2::Error::None;
  }

  // No display requests for now
  // Use for sharing blit buffers and
  // writing wfd buffer directly to output if there is full GPU composition
  // and no color conversion needed
  if (!validated_.test(type_)) {
    DLOGW("Display is not validated");
    return HWC2::Error::NotValidated;
  }

  *out_display_requests = 0;
  *out_num_elements = UINT32(layer_requests_.size());
  if (out_layers != nullptr && out_layer_requests != nullptr) {
    int i = 0;
    for (auto &request : layer_requests_) {
      out_layers[i] = request.first;
      out_layer_requests[i] = INT32(request.second);
      i++;
    }
  }

  auto client_target_layer = client_target_->GetSDMLayer();
  if (client_target_layer->request.flags.flip_buffer) {
    *out_display_requests = INT32(HWC2::DisplayRequest::FlipClientTarget);
  }

  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::GetHdrCapabilities(uint32_t *out_num_types, int32_t *out_types,
                                           float *out_max_luminance,
                                           float *out_max_average_luminance,
                                           float *out_min_luminance) {
  DisplayConfigFixedInfo fixed_info = {};
  display_intf_->GetConfig(&fixed_info);

  if (!fixed_info.hdr_supported) {
    *out_num_types = 0;
    DLOGI("HDR is not supported");
    return HWC2::Error::None;
  }

  if (out_types == nullptr) {
    // 1(now) - because we support only HDR10, change when HLG & DOLBY vision are supported
    *out_num_types  = 1;
  } else {
    // Only HDR10 supported
    *out_types = HAL_HDR_HDR10;
    static const float kLuminanceFactor = 10000.0;
    // luminance is expressed in the unit of 0.0001 cd/m2, convert it to 1cd/m2.
    *out_max_luminance = FLOAT(fixed_info.max_luminance)/kLuminanceFactor;
    *out_max_average_luminance = FLOAT(fixed_info.average_luminance)/kLuminanceFactor;
    *out_min_luminance = FLOAT(fixed_info.min_luminance)/kLuminanceFactor;
  }

  return HWC2::Error::None;
}


HWC2::Error HWCDisplay::CommitLayerStack(void) {
  if (flush_) {
     return HWC2::Error::None;
  }

  if (skip_validate_ && !CanSkipValidate()) {
    validated_.reset(type_);
  }

  if (!validated_.test(type_)) {
    DLOGV_IF(kTagClient, "Display %d is not validated", id_);
    return HWC2::Error::NotValidated;
  }

  if (shutdown_pending_ || layer_set_.empty()) {
    return HWC2::Error::None;
  }

  DumpInputBuffers();

  DisplayError error = kErrorUndefined;
  int status = 0;
  if (tone_mapper_) {
    if (layer_stack_.flags.hdr_present) {
      status = tone_mapper_->HandleToneMap(&layer_stack_);
      if (status != 0) {
        DLOGE("Error handling HDR in ToneMapper");
      }
    } else {
      tone_mapper_->Terminate();
    }
  }

  error = display_intf_->Commit(&layer_stack_);

  if (error == kErrorNone) {
    // A commit is successfully submitted, start flushing on failure now onwards.
    flush_on_error_ = true;
  } else {
    if (error == kErrorShutDown) {
      shutdown_pending_ = true;
      return HWC2::Error::Unsupported;
    } else if (error == kErrorNotValidated) {
      validated_.reset(type_);
      return HWC2::Error::NotValidated;
    } else if (error != kErrorPermission) {
      DLOGE("Commit failed. Error = %d", error);
      // To prevent surfaceflinger infinite wait, flush the previous frame during Commit()
      // so that previous buffer and fences are released, and override the error.
      flush_ = true;
    }
  }

  skip_validate_ = true;
  return HWC2::Error::None;
}

HWC2::Error HWCDisplay::PostCommitLayerStack(int32_t *out_retire_fence) {
  auto status = HWC2::Error::None;

  // Do no call flush on errors, if a successful buffer is never submitted.
  if (flush_ && flush_on_error_) {
    display_intf_->Flush(secure_display_transition_);
    secure_display_transition_ = false;
    validated_.reset(type_);
    flush_on_error_ = false;
  }

  if (tone_mapper_ && tone_mapper_->IsActive()) {
     tone_mapper_->PostCommit(&layer_stack_);
  }

  // TODO(user): No way to set the client target release fence on SF
  int32_t &client_target_release_fence =
      client_target_->GetSDMLayer()->input_buffer.release_fence_fd;
  if (client_target_release_fence >= 0) {
    close(client_target_release_fence);
    client_target_release_fence = -1;
  }
  client_target_->ResetGeometryChanges();

  for (auto hwc_layer : layer_set_) {
    hwc_layer->ResetGeometryChanges();
    Layer *layer = hwc_layer->GetSDMLayer();
    LayerBuffer *layer_buffer = &layer->input_buffer;

    if (!flush_) {
      // If swapinterval property is set to 0 or for single buffer layers, do not update f/w
      // release fences and discard fences from driver
      if (swap_interval_zero_ || layer->flags.single_buffer) {
        close(layer_buffer->release_fence_fd);
      } else if (layer->composition != kCompositionGPU) {
        hwc_layer->PushReleaseFence(layer_buffer->release_fence_fd);
      } else {
        hwc_layer->PushReleaseFence(-1);
      }
    } else {
      // In case of flush, we don't return an error to f/w, so it will get a release fence out of
      // the hwc_layer's release fence queue. We should push a -1 to preserve release fence
      // circulation semantics.
      hwc_layer->PushReleaseFence(-1);
    }

    layer_buffer->release_fence_fd = -1;
    if (layer_buffer->acquire_fence_fd >= 0) {
      close(layer_buffer->acquire_fence_fd);
      layer_buffer->acquire_fence_fd = -1;
    }

    layer->request.flags = {};
  }

  client_target_->GetSDMLayer()->request.flags = {};
  *out_retire_fence = -1;
  if (!flush_) {
    // if swapinterval property is set to 0 then close and reset the list retire fence
    if (swap_interval_zero_) {
      close(layer_stack_.retire_fence_fd);
      layer_stack_.retire_fence_fd = -1;
    }
    *out_retire_fence = layer_stack_.retire_fence_fd;
    layer_stack_.retire_fence_fd = -1;

    if (dump_frame_count_) {
      dump_frame_count_--;
      dump_frame_index_++;
    }
  }

  geometry_changes_ = GeometryChanges::kNone;
  flush_ = false;

  return status;
}

void HWCDisplay::SetIdleTimeoutMs(uint32_t timeout_ms) {
  return;
}

DisplayError HWCDisplay::SetMaxMixerStages(uint32_t max_mixer_stages) {
  DisplayError error = kErrorNone;

  if (display_intf_) {
    error = display_intf_->SetMaxMixerStages(max_mixer_stages);
    validated_.reset();
  }

  return error;
}

LayerBufferFormat HWCDisplay::GetSDMFormat(const int32_t &source, const int flags) {
  LayerBufferFormat format = kFormatInvalid;
  if (flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) {
    switch (source) {
      case HAL_PIXEL_FORMAT_RGBA_8888:
        format = kFormatRGBA8888Ubwc;
        break;
      case HAL_PIXEL_FORMAT_RGBX_8888:
        format = kFormatRGBX8888Ubwc;
        break;
      case HAL_PIXEL_FORMAT_BGR_565:
        format = kFormatBGR565Ubwc;
        break;
      case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
      case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
      case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
        format = kFormatYCbCr420SPVenusUbwc;
        break;
      case HAL_PIXEL_FORMAT_RGBA_1010102:
        format = kFormatRGBA1010102Ubwc;
        break;
      case HAL_PIXEL_FORMAT_RGBX_1010102:
        format = kFormatRGBX1010102Ubwc;
        break;
      case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
        format = kFormatYCbCr420TP10Ubwc;
        break;
      case HAL_PIXEL_FORMAT_YCbCr_420_P010_UBWC:
        format = kFormatYCbCr420P010Ubwc;
        break;
      default:
        DLOGE("Unsupported format type for UBWC %d", source);
        return kFormatInvalid;
    }
    return format;
  }

  switch (source) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
      format = kFormatRGBA8888;
      break;
    case HAL_PIXEL_FORMAT_RGBA_5551:
      format = kFormatRGBA5551;
      break;
    case HAL_PIXEL_FORMAT_RGBA_4444:
      format = kFormatRGBA4444;
      break;
    case HAL_PIXEL_FORMAT_BGRA_8888:
      format = kFormatBGRA8888;
      break;
    case HAL_PIXEL_FORMAT_RGBX_8888:
      format = kFormatRGBX8888;
      break;
    case HAL_PIXEL_FORMAT_BGRX_8888:
      format = kFormatBGRX8888;
      break;
    case HAL_PIXEL_FORMAT_RGB_888:
      format = kFormatRGB888;
      break;
    case HAL_PIXEL_FORMAT_RGB_565:
      format = kFormatRGB565;
      break;
    case HAL_PIXEL_FORMAT_BGR_565:
      format = kFormatBGR565;
      break;
    case HAL_PIXEL_FORMAT_BGR_888:
      format = kFormatBGR888;
      break;
    case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
      format = kFormatYCbCr420SemiPlanarVenus;
      break;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS:
      format = kFormatYCrCb420SemiPlanarVenus;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
      format = kFormatYCbCr420SPVenusUbwc;
      break;
    case HAL_PIXEL_FORMAT_YV12:
      format = kFormatYCrCb420PlanarStride16;
      break;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
      format = kFormatYCrCb420SemiPlanar;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
      format = kFormatYCbCr420SemiPlanar;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
      format = kFormatYCbCr422H2V1SemiPlanar;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
      format = kFormatYCbCr422H2V1Packed;
      break;
    case HAL_PIXEL_FORMAT_CbYCrY_422_I:
      format = kFormatCbYCrY422H2V1Packed;
      break;
    case HAL_PIXEL_FORMAT_RGBA_1010102:
      format = kFormatRGBA1010102;
      break;
    case HAL_PIXEL_FORMAT_ARGB_2101010:
      format = kFormatARGB2101010;
      break;
    case HAL_PIXEL_FORMAT_RGBX_1010102:
      format = kFormatRGBX1010102;
      break;
    case HAL_PIXEL_FORMAT_XRGB_2101010:
      format = kFormatXRGB2101010;
      break;
    case HAL_PIXEL_FORMAT_BGRA_1010102:
      format = kFormatBGRA1010102;
      break;
    case HAL_PIXEL_FORMAT_ABGR_2101010:
      format = kFormatABGR2101010;
      break;
    case HAL_PIXEL_FORMAT_BGRX_1010102:
      format = kFormatBGRX1010102;
      break;
    case HAL_PIXEL_FORMAT_XBGR_2101010:
      format = kFormatXBGR2101010;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_P010:
      format = kFormatYCbCr420P010;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
      format = kFormatYCbCr420TP10Ubwc;
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_P010_UBWC:
      format = kFormatYCbCr420P010Ubwc;
      break;
    default:
      DLOGW("Unsupported format type = %d", source);
      return kFormatInvalid;
  }

  return format;
}

void HWCDisplay::DumpInputBuffers() {
  char dir_path[PATH_MAX];

  if (!dump_frame_count_ || flush_ || !dump_input_layers_) {
    return;
  }

  DLOGI("dump_frame_count %d dump_input_layers %d", dump_frame_count_, dump_input_layers_);
  snprintf(dir_path, sizeof(dir_path), "%s/frame_dump_%s", HWCDebugHandler::DumpDir(),
           GetDisplayString());

  if (mkdir(dir_path, 0777) != 0 && errno != EEXIST) {
    DLOGW("Failed to create %s directory errno = %d, desc = %s", dir_path, errno, strerror(errno));
    return;
  }

  // if directory exists already, need to explicitly change the permission.
  if (errno == EEXIST && chmod(dir_path, 0777) != 0) {
    DLOGW("Failed to change permissions on %s directory", dir_path);
    return;
  }

  for (uint32_t i = 0; i < layer_stack_.layers.size(); i++) {
    auto layer = layer_stack_.layers.at(i);
    const private_handle_t *pvt_handle =
        reinterpret_cast<const private_handle_t *>(layer->input_buffer.buffer_id);
    auto &acquire_fence_fd = layer->input_buffer.acquire_fence_fd;

    if (acquire_fence_fd >= 0) {
      DisplayError error = buffer_allocator_->MapBuffer(pvt_handle, acquire_fence_fd);
      if (error != kErrorNone) {
        continue;
      }
      acquire_fence_fd = -1;
    }

    DLOGI("Dump layer[%d] of %d pvt_handle %x pvt_handle->base %x", i, layer_stack_.layers.size(),
          pvt_handle, pvt_handle? pvt_handle->base : 0);

    if (!pvt_handle) {
      DLOGE("Buffer handle is null");
      return;
    }

    if (!pvt_handle->base) {
      DisplayError error = buffer_allocator_->MapBuffer(pvt_handle, -1);
      if (error != kErrorNone) {
        DLOGE("Failed to map buffer, error = %d", error);
        return;
      }
    }

    char dump_file_name[PATH_MAX];
    size_t result = 0;

    snprintf(dump_file_name, sizeof(dump_file_name), "%s/input_layer%d_%dx%d_%s_frame%d.raw",
             dir_path, i, pvt_handle->width, pvt_handle->height,
             qdutils::GetHALPixelFormatString(pvt_handle->format), dump_frame_index_);

    FILE *fp = fopen(dump_file_name, "w+");
    if (fp) {
      result = fwrite(reinterpret_cast<void *>(pvt_handle->base), pvt_handle->size, 1, fp);
      fclose(fp);
    }

    int release_fence = -1;
    DisplayError error = buffer_allocator_->UnmapBuffer(pvt_handle, &release_fence);
    if (error != kErrorNone) {
      DLOGE("Failed to unmap buffer, error = %d", error);
      return;
    }

    DLOGI("Frame Dump %s: is %s", dump_file_name, result ? "Successful" : "Failed");
  }
}

void HWCDisplay::DumpOutputBuffer(const BufferInfo &buffer_info, void *base, int fence) {
  char dir_path[PATH_MAX];

  snprintf(dir_path, sizeof(dir_path), "%s/frame_dump_%s", HWCDebugHandler::DumpDir(),
           GetDisplayString());

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
        DLOGW("sync_wait error errno = %d, desc = %s", errno, strerror(errno));
        return;
      }
    }

    snprintf(dump_file_name, sizeof(dump_file_name), "%s/output_layer_%dx%d_%s_frame%d.raw",
             dir_path, buffer_info.buffer_config.width, buffer_info.buffer_config.height,
             GetFormatString(buffer_info.buffer_config.format), dump_frame_index_);

    FILE *fp = fopen(dump_file_name, "w+");
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
  if (x_pixels <= 0 || y_pixels <= 0) {
    DLOGW("Unsupported config: x_pixels=%d, y_pixels=%d", x_pixels, y_pixels);
    return -EINVAL;
  }

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

  // Create rects to represent the new source and destination crops
  LayerRect crop = LayerRect(0, 0, FLOAT(x_pixels), FLOAT(y_pixels));
  hwc_rect_t scaled_display_frame = {0, 0, INT(x_pixels), INT(y_pixels)};
  ApplyScanAdjustment(&scaled_display_frame);
  client_target_->SetLayerDisplayFrame(scaled_display_frame);

  auto client_target_layer = client_target_->GetSDMLayer();
  client_target_layer->src_rect = crop;

  int aligned_width;
  int aligned_height;
  uint32_t usage = GRALLOC_USAGE_HW_FB;
  int format = HAL_PIXEL_FORMAT_RGBA_8888;
  int ubwc_disabled = 0;
  int flags = 0;

  // By default UBWC is enabled and below property is global enable/disable for all
  // buffers allocated through gralloc , including framebuffer targets.
  HWCDebugHandler::Get()->GetProperty(DISABLE_UBWC_PROP, &ubwc_disabled);
  if (!ubwc_disabled) {
    usage |= GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
    flags |= private_handle_t::PRIV_FLAGS_UBWC_ALIGNED;
  }

#ifdef USE_GRALLOC1
  buffer_allocator_->GetAlignedWidthAndHeight(INT(x_pixels), INT(y_pixels), format, usage,
                                              &aligned_width, &aligned_height);
#else
  AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(INT(x_pixels), INT(y_pixels), format,
                                                        INT(usage), aligned_width, aligned_height);
#endif

  // TODO(user): How does the dirty region get set on the client target? File bug on Google
  client_target_layer->composition = kCompositionGPUTarget;
  client_target_layer->input_buffer.format = GetSDMFormat(format, flags);
  client_target_layer->input_buffer.width = UINT32(aligned_width);
  client_target_layer->input_buffer.height = UINT32(aligned_height);
  client_target_layer->input_buffer.unaligned_width = x_pixels;
  client_target_layer->input_buffer.unaligned_height = y_pixels;
  client_target_layer->plane_alpha = 255;

  DLOGI("New framebuffer resolution (%dx%d)", fb_config.x_pixels, fb_config.y_pixels);

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

int HWCDisplay::SetDisplayStatus(DisplayStatus display_status) {
  int status = 0;

  switch (display_status) {
    case kDisplayStatusResume:
      display_paused_ = false;
      fbt_valid_ = false;
      status = INT32(SetPowerMode(HWC2::PowerMode::On));
      break;
    case kDisplayStatusOnline:
      status = INT32(SetPowerMode(HWC2::PowerMode::On));
      break;
    case kDisplayStatusPause:
      display_paused_ = true;
      status = INT32(SetPowerMode(HWC2::PowerMode::Off));
      break;
    case kDisplayStatusOffline:
      status = INT32(SetPowerMode(HWC2::PowerMode::Off));
      break;
    default:
      DLOGW("Invalid display status %d", display_status);
      return -EINVAL;
  }

  if (display_status == kDisplayStatusResume || display_status == kDisplayStatusPause) {
    callbacks_->Refresh(HWC_DISPLAY_PRIMARY);
    validated_.reset();
  }

  return status;
}

HWC2::Error HWCDisplay::SetCursorPosition(hwc2_layer_t layer, int x, int y) {
  if (shutdown_pending_) {
    return HWC2::Error::None;
  }

  if (!layer_stack_.flags.cursor_present) {
    DLOGW("Cursor layer not present");
    return HWC2::Error::BadLayer;
  }

  HWCLayer *hwc_layer = GetHWCLayer(layer);
  if (hwc_layer == nullptr) {
    return HWC2::Error::BadLayer;
  }
  if (hwc_layer->GetDeviceSelectedCompositionType() != HWC2::Composition::Cursor) {
    return HWC2::Error::None;
  }
  if (!skip_validate_ && validated_.test(type_)) {
    // the device is currently in the middle of the validate/present sequence,
    // cannot set the Position(as per HWC2 spec)
    return HWC2::Error::NotValidated;
  }

  DisplayState state;
  if (display_intf_->GetDisplayState(&state) == kErrorNone) {
    if (state != kStateOn) {
      return HWC2::Error::None;
    }
  }

  // TODO(user): HWC1.5 was not letting SetCursorPosition before validateDisplay,
  // but HWC2.0 doesn't let setting cursor position after validate before present.
  // Need to revisit.

  auto error = display_intf_->SetCursorPosition(x, y);
  if (error != kErrorNone) {
    if (error == kErrorShutDown) {
      shutdown_pending_ = true;
      return HWC2::Error::None;
    }

    DLOGE("Failed for x = %d y = %d, Error = %d", x, y, error);
    return HWC2::Error::BadDisplay;
  }

  return HWC2::Error::None;
}

int HWCDisplay::OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) {
  if (min_enc_level_ == min_enc_level) {
    DLOGI("Min hdcp level not changed!");
    return 0;
  }
  DisplayError error = display_intf_->OnMinHdcpEncryptionLevelChange(min_enc_level);
  if (error != kErrorNone) {
    DLOGE("Failed. Error = %d", error);
    return -1;
  }

  validated_.reset();
  min_enc_level_ = min_enc_level;
  return 0;
}

void HWCDisplay::MarkLayersForGPUBypass() {
  for (auto hwc_layer : layer_set_) {
    auto layer = hwc_layer->GetSDMLayer();
    layer->composition = kCompositionSDE;
  }
  validated_.set(type_);
}

void HWCDisplay::MarkLayersForClientComposition() {
  // ClientComposition - GPU comp, to achieve this, set skip flag so that
  // SDM does not handle this layer and hwc_layer composition will be
  // set correctly at the end of Prepare.
  DLOGV_IF(kTagClient, "HWC Layers marked for GPU comp");
  for (auto hwc_layer : layer_set_) {
    Layer *layer = hwc_layer->GetSDMLayer();
    layer->flags.skip = true;
  }
  layer_stack_.flags.skip_present = true;
}

void HWCDisplay::ApplyScanAdjustment(hwc_rect_t *display_frame) {
}

int HWCDisplay::SetPanelBrightness(int level) {
  int ret = 0;
  if (display_intf_) {
    ret = display_intf_->SetPanelBrightness(level);
    validated_.reset();
  } else {
    ret = -EINVAL;
  }

  return ret;
}

int HWCDisplay::GetPanelBrightness(int *level) {
  return display_intf_->GetPanelBrightness(level);
}

int HWCDisplay::ToggleScreenUpdates(bool enable) {
  display_paused_ = enable ? false : true;
  callbacks_->Refresh(HWC_DISPLAY_PRIMARY);
  validated_.reset();
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

void HWCDisplay::SolidFillPrepare() {
  if (solid_fill_enable_) {
    if (solid_fill_layer_ == NULL) {
      // Create a dummy layer here
      solid_fill_layer_ = new Layer();
    }
    uint32_t primary_width = 0, primary_height = 0;
    GetMixerResolution(&primary_width, &primary_height);

    LayerBuffer *layer_buffer = &solid_fill_layer_->input_buffer;
    layer_buffer->width = primary_width;
    layer_buffer->height = primary_height;
    layer_buffer->unaligned_width = primary_width;
    layer_buffer->unaligned_height = primary_height;
    layer_buffer->acquire_fence_fd = -1;
    layer_buffer->release_fence_fd = -1;

    LayerRect rect;
    rect.top = 0; rect.left = 0;
    rect.right = primary_width;
    rect.bottom = primary_height;

    solid_fill_layer_->composition = kCompositionGPU;
    solid_fill_layer_->src_rect = rect;
    solid_fill_layer_->dst_rect = rect;

    solid_fill_layer_->blending = kBlendingPremultiplied;
    solid_fill_layer_->solid_fill_color = solid_fill_color_;
    solid_fill_layer_->frame_rate = 60;
    solid_fill_layer_->visible_regions.push_back(solid_fill_layer_->dst_rect);
    solid_fill_layer_->flags.updating = 1;
    solid_fill_layer_->flags.solid_fill = true;
  } else {
    // delete the dummy layer
    delete solid_fill_layer_;
    solid_fill_layer_ = NULL;
  }

  if (solid_fill_enable_ && solid_fill_layer_) {
    BuildSolidFillStack();
    MarkLayersForGPUBypass();
  }

  return;
}

void HWCDisplay::SolidFillCommit() {
  if (solid_fill_enable_ && solid_fill_layer_) {
    LayerBuffer *layer_buffer = &solid_fill_layer_->input_buffer;
    if (layer_buffer->release_fence_fd > 0) {
      close(layer_buffer->release_fence_fd);
      layer_buffer->release_fence_fd = -1;
    }
    if (layer_stack_.retire_fence_fd > 0) {
      close(layer_stack_.retire_fence_fd);
      layer_stack_.retire_fence_fd = -1;
    }
  }
}

int HWCDisplay::GetVisibleDisplayRect(hwc_rect_t *visible_rect) {
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

void HWCDisplay::SetSecureDisplay(bool secure_display_active) {
  if (secure_display_active_ != secure_display_active) {
    DLOGI("SecureDisplay state changed from %d to %d Needs Flush!!", secure_display_active_,
          secure_display_active);
    secure_display_active_ = secure_display_active;
    secure_display_transition_ = true;
    skip_prepare_ = true;
  }
  return;
}

int HWCDisplay::SetActiveDisplayConfig(uint32_t config) {
  int status = (display_intf_->SetActiveConfig(config) == kErrorNone) ? 0 : -1;
  validated_.reset();
  return status;
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

uint32_t HWCDisplay::GetUpdatingLayersCount(void) {
  uint32_t updating_count = 0;

  for (uint i = 0; i < layer_stack_.layers.size(); i++) {
    auto layer = layer_stack_.layers.at(i);
    if (layer->flags.updating) {
      updating_count++;
    }
  }

  return updating_count;
}

bool HWCDisplay::IsLayerUpdating(const Layer *layer) {
  // Layer should be considered updating if
  //   a) layer is in single buffer mode, or
  //   b) valid dirty_regions(android specific hint for updating status), or
  //   c) layer stack geometry has changed (TODO(user): Remove when SDM accepts
  //      geometry_changed as bit fields).
  return (layer->flags.single_buffer || IsSurfaceUpdated(layer->dirty_regions) ||
          geometry_changes_);
}

bool HWCDisplay::IsSurfaceUpdated(const std::vector<LayerRect> &dirty_regions) {
  // based on dirty_regions determine if its updating
  // dirty_rect count = 0 - whole layer - updating.
  // dirty_rect count = 1 or more valid rects - updating.
  // dirty_rect count = 1 with (0,0,0,0) - not updating.
  return (dirty_regions.empty() || IsValid(dirty_regions.at(0)));
}

uint32_t HWCDisplay::SanitizeRefreshRate(uint32_t req_refresh_rate) {
  uint32_t refresh_rate = req_refresh_rate;

  if (refresh_rate < min_refresh_rate_) {
    // Pick the next multiple of request which is within the range
    refresh_rate =
        (((min_refresh_rate_ / refresh_rate) + ((min_refresh_rate_ % refresh_rate) ? 1 : 0)) *
         refresh_rate);
  }

  if (refresh_rate > max_refresh_rate_) {
    refresh_rate = max_refresh_rate_;
  }

  return refresh_rate;
}

DisplayClass HWCDisplay::GetDisplayClass() {
  return display_class_;
}

void HWCDisplay::CloseAcquireFds() {
  for (auto hwc_layer : layer_set_) {
    auto layer = hwc_layer->GetSDMLayer();
    if (layer->input_buffer.acquire_fence_fd >= 0) {
      close(layer->input_buffer.acquire_fence_fd);
      layer->input_buffer.acquire_fence_fd = -1;
    }
  }
  int32_t &client_target_acquire_fence =
      client_target_->GetSDMLayer()->input_buffer.acquire_fence_fd;
  if (client_target_acquire_fence >= 0) {
    close(client_target_acquire_fence);
    client_target_acquire_fence = -1;
  }
}

std::string HWCDisplay::Dump() {
  std::ostringstream os;
  os << "\n------------HWC----------------\n";
  os << "HWC2 display_id: " << id_ << std::endl;
  for (auto layer : layer_set_) {
    auto sdm_layer = layer->GetSDMLayer();
    auto transform = sdm_layer->transform;
    os << "layer: " << std::setw(4) << layer->GetId();
    os << " z: " << layer->GetZ();
    os << " compositon: " <<
          to_string(layer->GetClientRequestedCompositionType()).c_str();
    os << "/" <<
          to_string(layer->GetDeviceSelectedCompositionType()).c_str();
    os << " alpha: " << std::to_string(sdm_layer->plane_alpha).c_str();
    os << " format: " << std::setw(22) << GetFormatString(sdm_layer->input_buffer.format);
    os << " dataspace:" << std::hex << "0x" << std::setw(8) << std::setfill('0')
       << layer->GetLayerDataspace() << std::dec << std::setfill(' ');
    os << " transform: " << transform.rotation << "/" << transform.flip_horizontal <<
          "/"<< transform.flip_vertical;
    os << " buffer_id: " << std::hex << "0x" << sdm_layer->input_buffer.buffer_id << std::dec
       << std::endl;
  }

  if (layer_stack_invalid_) {
    os << "\n Layers added or removed but not reflected to SDM's layer stack yet\n";
    return os.str();
  }

  if (color_mode_) {
    os << "\n----------Color Modes---------\n";
    color_mode_->Dump(&os);
  }

  if (display_intf_) {
    os << "\n------------SDM----------------\n";
    os << display_intf_->Dump();
  }

  os << "\n";

  return os.str();
}

bool HWCDisplay::CanSkipValidate() {
  // Layer Stack checks
  if (layer_stack_.flags.hdr_present && (tone_mapper_ && tone_mapper_->IsActive())) {
    DLOGV_IF(kTagClient, "HDR content present with tone mapping enabled. Returning false.");
    return false;
  }

  if (client_target_->NeedsValidation()) {
    DLOGV_IF(kTagClient, "Framebuffer target needs validation. Returning false.");
    return false;
  }

  for (auto hwc_layer : layer_set_) {
    if (hwc_layer->NeedsValidation()) {
      DLOGV_IF(kTagClient, "hwc_layer[%d] needs validation. Returning false.",
               hwc_layer->GetId());
      return false;
    }

    // Do not allow Skip Validate, if any layer needs GPU Composition.
    if (hwc_layer->GetDeviceSelectedCompositionType() == HWC2::Composition::Client) {
      DLOGV_IF(kTagClient, "hwc_layer[%d] is GPU composed. Returning false.",
               hwc_layer->GetId());
      return false;
    }
  }

  return true;
}

}  // namespace sdm
