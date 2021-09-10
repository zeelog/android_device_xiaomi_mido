/*
 * Copyright (c) 2014-2018, The Linux Foundation. All rights reserved.
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

#ifndef __HWC_LAYERS_H__
#define __HWC_LAYERS_H__

/* This class translates HWC2 Layer functions to the SDM LayerStack
 */

#include <gralloc_priv.h>
#include <qdMetaData.h>
#include <core/layer_stack.h>
#define HWC2_INCLUDE_STRINGIFICATION
#define HWC2_USE_CPP11
#include <hardware/hwcomposer2.h>
#undef HWC2_INCLUDE_STRINGIFICATION
#undef HWC2_USE_CPP11
#include <map>
#include <queue>
#include <set>
#include "core/buffer_allocator.h"
#include "hwc_buffer_allocator.h"

namespace sdm {

DisplayError SetCSC(const private_handle_t *pvt_handle, ColorMetaData *color_metadata);
bool GetColorPrimary(const int32_t &dataspace, ColorPrimaries *color_primary);
bool GetTransfer(const int32_t &dataspace, GammaTransfer *gamma_transfer);
void GetRange(const int32_t &dataspace, ColorRange *color_range);
bool GetSDMColorSpace(const int32_t &dataspace, ColorMetaData *color_metadata);
bool IsBT2020(const ColorPrimaries &color_primary);
enum GeometryChanges {
  kNone         = 0x000,
  kBlendMode    = 0x001,
  kDataspace    = 0x002,
  kDisplayFrame = 0x004,
  kPlaneAlpha   = 0x008,
  kSourceCrop   = 0x010,
  kTransform    = 0x020,
  kZOrder       = 0x040,
  kAdded        = 0x080,
  kRemoved      = 0x100,
  kBufferGeometry = 0x200,
};

class HWCLayer {
 public:
  explicit HWCLayer(hwc2_display_t display_id, HWCBufferAllocator *buf_allocator);
  ~HWCLayer();
  uint32_t GetZ() const { return z_; }
  hwc2_layer_t GetId() const { return id_; }
  Layer *GetSDMLayer() { return layer_; }

  HWC2::Error SetLayerBlendMode(HWC2::BlendMode mode);
  HWC2::Error SetLayerBuffer(buffer_handle_t buffer, int32_t acquire_fence);
  HWC2::Error SetLayerColor(hwc_color_t color);
  HWC2::Error SetLayerCompositionType(HWC2::Composition type);
  HWC2::Error SetLayerDataspace(int32_t dataspace);
  HWC2::Error SetLayerDisplayFrame(hwc_rect_t frame);
  HWC2::Error SetCursorPosition(int32_t x, int32_t y);
  HWC2::Error SetLayerPlaneAlpha(float alpha);
  HWC2::Error SetLayerSourceCrop(hwc_frect_t crop);
  HWC2::Error SetLayerSurfaceDamage(hwc_region_t damage);
  HWC2::Error SetLayerTransform(HWC2::Transform transform);
  HWC2::Error SetLayerVisibleRegion(hwc_region_t visible);
  HWC2::Error SetLayerZOrder(uint32_t z);
  void SetComposition(const LayerComposition &sdm_composition);
  HWC2::Composition GetClientRequestedCompositionType() { return client_requested_; }
  void UpdateClientCompositionType(HWC2::Composition type) { client_requested_ = type; }
  HWC2::Composition GetDeviceSelectedCompositionType() { return device_selected_; }
  int32_t GetLayerDataspace() { return dataspace_; }
  uint32_t GetGeometryChanges() { return geometry_changes_; }
  void ResetGeometryChanges() { geometry_changes_ = GeometryChanges::kNone; }
  void PushReleaseFence(int32_t fence);
  int32_t PopReleaseFence(void);
  bool ValidateAndSetCSC();
  bool SupportLocalConversion(ColorPrimaries working_primaries);
  void ResetValidation() { needs_validate_ = false; }
  bool NeedsValidation() { return (needs_validate_ || geometry_changes_); }
  bool IsNonIntegralSourceCrop() { return non_integral_source_crop_; }

 private:
  Layer *layer_ = nullptr;
  uint32_t z_ = 0;
  const hwc2_layer_t id_;
  const hwc2_display_t display_id_;
  static std::atomic<hwc2_layer_t> next_id_;
  std::queue<int32_t> release_fences_;
  int ion_fd_ = -1;
  HWCBufferAllocator *buffer_allocator_ = NULL;
  int32_t dataspace_ =  HAL_DATASPACE_UNKNOWN;
  bool needs_validate_ = true;
  bool non_integral_source_crop_ = false;

  // Composition requested by client(SF)
  HWC2::Composition client_requested_ = HWC2::Composition::Device;
  // Composition selected by SDM
  HWC2::Composition device_selected_ = HWC2::Composition::Device;
  uint32_t geometry_changes_ = GeometryChanges::kNone;

  void SetRect(const hwc_rect_t &source, LayerRect *target);
  void SetRect(const hwc_frect_t &source, LayerRect *target);
  uint32_t GetUint32Color(const hwc_color_t &source);
  LayerBufferFormat GetSDMFormat(const int32_t &source, const int flags);
  LayerBufferS3DFormat GetS3DFormat(uint32_t s3d_format);
  DisplayError SetMetaData(const private_handle_t *pvt_handle, Layer *layer);
  uint32_t RoundToStandardFPS(float fps);
};

struct SortLayersByZ {
  bool operator()(const HWCLayer *lhs, const HWCLayer *rhs) const {
    return lhs->GetZ() < rhs->GetZ();
  }
};

}  // namespace sdm
#endif  // __HWC_LAYERS_H__
