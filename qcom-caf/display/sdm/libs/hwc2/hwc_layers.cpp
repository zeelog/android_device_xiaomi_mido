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

#include <stdint.h>
#include <qdMetaData.h>

#include "hwc_layers.h"
#ifndef USE_GRALLOC1
#include <gr.h>
#endif
#include <utils/debug.h>
#include <utils/utils.h>
#include <cmath>

#define __CLASS__ "HWCLayer"

namespace sdm {

std::atomic<hwc2_layer_t> HWCLayer::next_id_(1);

DisplayError SetCSC(const private_handle_t *pvt_handle, ColorMetaData *color_metadata) {
  if (getMetaData(const_cast<private_handle_t *>(pvt_handle), GET_COLOR_METADATA,
                  color_metadata) != 0) {
    ColorSpace_t csc = ITU_R_601;
    if (getMetaData(const_cast<private_handle_t *>(pvt_handle),  GET_COLOR_SPACE,
                    &csc) == 0) {
      if (csc == ITU_R_601_FR || csc == ITU_R_2020_FR) {
        color_metadata->range = Range_Full;
      }

      switch (csc) {
      case ITU_R_601:
      case ITU_R_601_FR:
        // video and display driver uses 601_525
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
    } else {
      return kErrorNotSupported;
    }
  }

  return kErrorNone;
}

// Returns true when color primary is supported
bool GetColorPrimary(const int32_t &dataspace, ColorPrimaries *color_primary) {
  auto standard = dataspace & HAL_DATASPACE_STANDARD_MASK;
  bool supported_csc = true;
  switch (standard) {
    case  HAL_DATASPACE_STANDARD_BT709:
      *color_primary = ColorPrimaries_BT709_5;
      break;
    case HAL_DATASPACE_STANDARD_BT601_525:
    case HAL_DATASPACE_STANDARD_BT601_525_UNADJUSTED:
      *color_primary = ColorPrimaries_BT601_6_525;
      break;
    case HAL_DATASPACE_STANDARD_BT601_625:
    case HAL_DATASPACE_STANDARD_BT601_625_UNADJUSTED:
      *color_primary = ColorPrimaries_BT601_6_625;
      break;
    case HAL_DATASPACE_STANDARD_DCI_P3:
      *color_primary = ColorPrimaries_DCIP3;
      break;
    case HAL_DATASPACE_STANDARD_BT2020:
      *color_primary = ColorPrimaries_BT2020;
      break;
    default:
      DLOGV_IF(kTagClient, "Unsupported Standard Request = %d", standard);
      supported_csc = false;
  }
  return supported_csc;
}

bool GetTransfer(const int32_t &dataspace, GammaTransfer *gamma_transfer) {
  auto transfer = dataspace & HAL_DATASPACE_TRANSFER_MASK;
  bool supported_transfer = true;
  switch (transfer) {
    case HAL_DATASPACE_TRANSFER_SRGB:
      *gamma_transfer = Transfer_sRGB;
      break;
    case HAL_DATASPACE_TRANSFER_SMPTE_170M:
      *gamma_transfer = Transfer_SMPTE_170M;
      break;
    case HAL_DATASPACE_TRANSFER_ST2084:
      *gamma_transfer = Transfer_SMPTE_ST2084;
      break;
    case HAL_DATASPACE_TRANSFER_HLG:
      *gamma_transfer = Transfer_HLG;
      break;
    case HAL_DATASPACE_TRANSFER_LINEAR:
      *gamma_transfer = Transfer_Linear;
      break;
    case HAL_DATASPACE_TRANSFER_GAMMA2_2:
      *gamma_transfer = Transfer_Gamma2_2;
      break;
    default:
      DLOGV_IF(kTagClient, "Unsupported Transfer Request = %d", transfer);
      supported_transfer = false;
  }
  return supported_transfer;
}

void GetRange(const int32_t &dataspace, ColorRange *color_range) {
  auto range = dataspace & HAL_DATASPACE_RANGE_MASK;
  switch (range) {
    case HAL_DATASPACE_RANGE_FULL:
      *color_range = Range_Full;
      break;
    case HAL_DATASPACE_RANGE_LIMITED:
      *color_range = Range_Limited;
      break;
    default:
      DLOGV_IF(kTagClient, "Unsupported Range Request = %d", range);
      break;
  }
}

bool IsBT2020(const ColorPrimaries &color_primary) {
  switch (color_primary) {
  case ColorPrimaries_BT2020:
    return true;
    break;
  default:
    return false;
  }
}

// Retrieve ColorMetaData from android_data_space_t (STANDARD|TRANSFER|RANGE)
bool GetSDMColorSpace(const int32_t &dataspace, ColorMetaData *color_metadata) {
  bool valid = false;
  valid = GetColorPrimary(dataspace, &(color_metadata->colorPrimaries));
  if (!valid) {
    return valid;
  }
  valid = GetTransfer(dataspace, &(color_metadata->transfer));
  if (!valid) {
    return valid;
  }
  GetRange(dataspace, &(color_metadata->range));

  return true;
}

// Layer operations
HWCLayer::HWCLayer(hwc2_display_t display_id, HWCBufferAllocator *buf_allocator)
  : id_(next_id_++), display_id_(display_id), buffer_allocator_(buf_allocator) {
  layer_ = new Layer();
  // Fences are deferred, so the first time this layer is presented, return -1
  // TODO(user): Verify that fences are properly obtained on suspend/resume
  release_fences_.push(-1);
}

HWCLayer::~HWCLayer() {
  // Close any fences left for this layer
  while (!release_fences_.empty()) {
    close(release_fences_.front());
    release_fences_.pop();
  }
  close(ion_fd_);
  if (layer_) {
    delete layer_;
  }
}

HWC2::Error HWCLayer::SetLayerBuffer(buffer_handle_t buffer, int32_t acquire_fence) {
  if (!buffer) {
    DLOGE("Invalid buffer handle: %p on layer: %d", buffer, id_);
    return HWC2::Error::BadParameter;
  }

  if (acquire_fence == 0) {
    DLOGE("acquire_fence is zero");
    return HWC2::Error::BadParameter;
  }

  const private_handle_t *handle = static_cast<const private_handle_t *>(buffer);

  // Validate and dup ion fd from surfaceflinger
  // This works around bug 30281222
  if (handle->fd < 0) {
    return HWC2::Error::BadParameter;
  } else {
    close(ion_fd_);
    ion_fd_ = dup(handle->fd);
  }

  LayerBuffer *layer_buffer = &layer_->input_buffer;
  int aligned_width, aligned_height;
#ifdef USE_GRALLOC1
  buffer_allocator_->GetCustomWidthAndHeight(handle, &aligned_width, &aligned_height);
#else
  AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(handle, aligned_width, aligned_height);
#endif

  LayerBufferFormat format = GetSDMFormat(handle->format, handle->flags);
  if ((format != layer_buffer->format) || (UINT32(aligned_width) != layer_buffer->width) ||
      (UINT32(aligned_height) != layer_buffer->height)) {
    // Layer buffer geometry has changed.
    geometry_changes_ |= kBufferGeometry;
  }

  layer_buffer->format = format;
  layer_buffer->width = UINT32(aligned_width);
  layer_buffer->height = UINT32(aligned_height);
  layer_buffer->unaligned_width = UINT32(handle->unaligned_width);
  layer_buffer->unaligned_height = UINT32(handle->unaligned_height);

  if (SetMetaData(const_cast<private_handle_t *>(handle), layer_) != kErrorNone) {
    return HWC2::Error::BadLayer;
  }

  layer_buffer->flags.video = (handle->buffer_type == BUFFER_TYPE_VIDEO) ? true : false;

  // TZ Protected Buffer - L1
  bool secure = (handle->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER);
  bool secure_camera = secure && (handle->flags & private_handle_t::PRIV_FLAGS_CAMERA_WRITE);
  bool secure_display = (handle->flags & private_handle_t::PRIV_FLAGS_SECURE_DISPLAY);
  if (secure != layer_buffer->flags.secure || secure_camera != layer_buffer->flags.secure_camera ||
      secure_display != layer_buffer->flags.secure_display) {
    // Secure attribute of layer buffer has changed.
    needs_validate_ = true;
  }

  layer_buffer->flags.secure = secure;
  layer_buffer->flags.secure_camera = secure_camera;
  layer_buffer->flags.secure_display = secure_display;

  layer_buffer->planes[0].fd = ion_fd_;
  layer_buffer->planes[0].offset = handle->offset;
  layer_buffer->planes[0].stride = UINT32(handle->width);
  CloseFd(&layer_buffer->acquire_fence_fd);
  layer_buffer->acquire_fence_fd = acquire_fence;
  layer_buffer->size = handle->size;
  layer_buffer->buffer_id = reinterpret_cast<uint64_t>(handle);

  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerSurfaceDamage(hwc_region_t damage) {
  // Check if there is an update in SurfaceDamage rects
  if (layer_->dirty_regions.size() != damage.numRects) {
    needs_validate_ = true;
  } else {
    for (uint32_t j = 0; j < damage.numRects; j++) {
      LayerRect damage_rect;
      SetRect(damage.rects[j], &damage_rect);
      if (damage_rect != layer_->dirty_regions.at(j)) {
        needs_validate_ = true;
        break;
      }
    }
  }

  layer_->dirty_regions.clear();
  for (uint32_t i = 0; i < damage.numRects; i++) {
    LayerRect rect;
    SetRect(damage.rects[i], &rect);
    layer_->dirty_regions.push_back(rect);
  }
  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerBlendMode(HWC2::BlendMode mode) {
  LayerBlending blending = kBlendingPremultiplied;
  switch (mode) {
    case HWC2::BlendMode::Coverage:
      blending = kBlendingCoverage;
      break;
    case HWC2::BlendMode::Premultiplied:
      blending = kBlendingPremultiplied;
      break;
    case HWC2::BlendMode::None:
      blending = kBlendingOpaque;
      break;
    default:
      return HWC2::Error::BadParameter;
  }

  if (layer_->blending != blending) {
    geometry_changes_ |= kBlendMode;
    layer_->blending = blending;
  }
  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerColor(hwc_color_t color) {
  if (client_requested_ != HWC2::Composition::SolidColor) {
    return HWC2::Error::None;
  }
  layer_->solid_fill_color = GetUint32Color(color);
  layer_->input_buffer.format = kFormatARGB8888;
  DLOGV_IF(kTagClient, "[%" PRIu64 "][%" PRIu64 "] Layer color set to %x", display_id_, id_,
           layer_->solid_fill_color);
  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerCompositionType(HWC2::Composition type) {
  client_requested_ = type;
  switch (type) {
    case HWC2::Composition::Client:
      break;
    case HWC2::Composition::Device:
      // We try and default to this in SDM
      break;
    case HWC2::Composition::SolidColor:
      break;
    case HWC2::Composition::Cursor:
      break;
    case HWC2::Composition::Invalid:
      return HWC2::Error::BadParameter;
    default:
      return HWC2::Error::Unsupported;
  }

  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerDataspace(int32_t dataspace) {
  // Map deprecated dataspace values to appropriate
  // new enums
  if (dataspace & 0xffff) {
    switch (dataspace & 0xffff) {
      case HAL_DATASPACE_SRGB:
        dataspace = HAL_DATASPACE_V0_SRGB;
        break;
      case HAL_DATASPACE_JFIF:
        dataspace = HAL_DATASPACE_V0_JFIF;
        break;
      case HAL_DATASPACE_SRGB_LINEAR:
        dataspace = HAL_DATASPACE_V0_SRGB_LINEAR;
        break;
      case HAL_DATASPACE_BT601_625:
        dataspace = HAL_DATASPACE_V0_BT601_625;
        break;
      case HAL_DATASPACE_BT601_525:
        dataspace = HAL_DATASPACE_V0_BT601_525;
        break;
      case HAL_DATASPACE_BT709:
        dataspace = HAL_DATASPACE_V0_BT709;
        break;
      default:
        // unknown legacy dataspace
        DLOGW_IF(kTagClient, "Unsupported dataspace type %d", dataspace);
    }
  }

  // cache the dataspace, to be used later to update SDM ColorMetaData
  if (dataspace_ != dataspace) {
    geometry_changes_ |= kDataspace;
    dataspace_ = dataspace;
  }
  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerDisplayFrame(hwc_rect_t frame) {
  LayerRect dst_rect = {};
  SetRect(frame, &dst_rect);
  if (layer_->dst_rect != dst_rect) {
    geometry_changes_ |= kDisplayFrame;
    layer_->dst_rect = dst_rect;
  }
  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetCursorPosition(int32_t x, int32_t y) {
  hwc_rect_t frame = {};
  frame.left = x;
  frame.top = y;
  frame.right = x + INT(layer_->dst_rect.right - layer_->dst_rect.left);
  frame.bottom = y + INT(layer_->dst_rect.bottom - layer_->dst_rect.top);
  SetLayerDisplayFrame(frame);

  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerPlaneAlpha(float alpha) {
  // Conversion of float alpha in range 0.0 to 1.0 similar to the HWC Adapter
  uint8_t plane_alpha = static_cast<uint8_t>(std::round(255.0f * alpha));
  if (layer_->plane_alpha != plane_alpha) {
    geometry_changes_ |= kPlaneAlpha;
    layer_->plane_alpha = plane_alpha;
  }

  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerSourceCrop(hwc_frect_t crop) {
  LayerRect src_rect = {};
  SetRect(crop, &src_rect);
  non_integral_source_crop_ = ((crop.left != roundf(crop.left)) ||
                              (crop.top != roundf(crop.top)) ||
                              (crop.right != roundf(crop.right)) ||
                              (crop.bottom != roundf(crop.bottom)));
  if (non_integral_source_crop_) {
    DLOGV_IF(kTagClient, "Crop: LRTB %f %f %f %f", crop.left, crop.top, crop.right, crop.bottom);
  }
  if (layer_->src_rect != src_rect) {
    geometry_changes_ |= kSourceCrop;
    layer_->src_rect = src_rect;
  }

  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerTransform(HWC2::Transform transform) {
  LayerTransform layer_transform = {};
  switch (transform) {
    case HWC2::Transform::FlipH:
      layer_transform.flip_horizontal = true;
      break;
    case HWC2::Transform::FlipV:
      layer_transform.flip_vertical = true;
      break;
    case HWC2::Transform::Rotate90:
      layer_transform.rotation = 90.0f;
      break;
    case HWC2::Transform::Rotate180:
      layer_transform.flip_horizontal = true;
      layer_transform.flip_vertical = true;
      break;
    case HWC2::Transform::Rotate270:
      layer_transform.rotation = 90.0f;
      layer_transform.flip_horizontal = true;
      layer_transform.flip_vertical = true;
      break;
    case HWC2::Transform::FlipHRotate90:
      layer_transform.rotation = 90.0f;
      layer_transform.flip_horizontal = true;
      break;
    case HWC2::Transform::FlipVRotate90:
      layer_transform.rotation = 90.0f;
      layer_transform.flip_vertical = true;
      break;
    case HWC2::Transform::None:
      // do nothing
      break;
  }

  if (layer_->transform != layer_transform) {
    geometry_changes_ |= kTransform;
    layer_->transform = layer_transform;
  }
  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerVisibleRegion(hwc_region_t visible) {
  layer_->visible_regions.clear();
  for (uint32_t i = 0; i < visible.numRects; i++) {
    LayerRect rect;
    SetRect(visible.rects[i], &rect);
    layer_->visible_regions.push_back(rect);
  }

  return HWC2::Error::None;
}

HWC2::Error HWCLayer::SetLayerZOrder(uint32_t z) {
  if (z_ != z) {
    geometry_changes_ |= kZOrder;
    z_ = z;
  }
  return HWC2::Error::None;
}

void HWCLayer::SetRect(const hwc_rect_t &source, LayerRect *target) {
  target->left = FLOAT(source.left);
  target->top = FLOAT(source.top);
  target->right = FLOAT(source.right);
  target->bottom = FLOAT(source.bottom);
}

void HWCLayer::SetRect(const hwc_frect_t &source, LayerRect *target) {
  // Recommended way of rounding as in hwcomposer2.h - SetLayerSourceCrop
  target->left = std::ceil(source.left);
  target->top = std::ceil(source.top);
  target->right = std::floor(source.right);
  target->bottom = std::floor(source.bottom);
}

uint32_t HWCLayer::GetUint32Color(const hwc_color_t &source) {
  // Returns 32 bit ARGB
  uint32_t a = UINT32(source.a) << 24;
  uint32_t r = UINT32(source.r) << 16;
  uint32_t g = UINT32(source.g) << 8;
  uint32_t b = UINT32(source.b);
  uint32_t color = a | r | g | b;
  return color;
}

LayerBufferFormat HWCLayer::GetSDMFormat(const int32_t &source, const int flags) {
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
    case HAL_PIXEL_FORMAT_BGR_888:
      format = kFormatBGR888;
      break;
    case HAL_PIXEL_FORMAT_RGB_565:
      format = kFormatRGB565;
      break;
    case HAL_PIXEL_FORMAT_BGR_565:
      format = kFormatBGR565;
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

LayerBufferS3DFormat HWCLayer::GetS3DFormat(uint32_t s3d_format) {
  LayerBufferS3DFormat sdm_s3d_format = kS3dFormatNone;
  switch (s3d_format) {
    case HAL_NO_3D:
      sdm_s3d_format = kS3dFormatNone;
      break;
    case HAL_3D_SIDE_BY_SIDE_L_R:
      sdm_s3d_format = kS3dFormatLeftRight;
      break;
    case HAL_3D_SIDE_BY_SIDE_R_L:
      sdm_s3d_format = kS3dFormatRightLeft;
      break;
    case HAL_3D_TOP_BOTTOM:
      sdm_s3d_format = kS3dFormatTopBottom;
      break;
    default:
      DLOGW("Invalid S3D format %d", s3d_format);
  }
  return sdm_s3d_format;
}

DisplayError HWCLayer::SetMetaData(const private_handle_t *pvt_handle, Layer *layer) {
  LayerBuffer *layer_buffer = &layer->input_buffer;
  private_handle_t *handle = const_cast<private_handle_t *>(pvt_handle);

  float fps = 0;
  uint32_t frame_rate = layer->frame_rate;
  if (getMetaData(handle, GET_REFRESH_RATE, &fps) == 0) {
    frame_rate = RoundToStandardFPS(fps);
  }

  int32_t interlaced = 0;
  getMetaData(handle, GET_PP_PARAM_INTERLACED, &interlaced);
  bool interlace = interlaced ? true : false;

  uint32_t linear_format = 0;
  if (getMetaData(handle, GET_LINEAR_FORMAT, &linear_format) == 0) {
    layer_buffer->format = GetSDMFormat(INT32(linear_format), 0);
  }

  uint32_t s3d = 0;
  LayerBufferS3DFormat s3d_format = layer_buffer->s3d_format;
  if (getMetaData(handle, GET_S3D_FORMAT, &s3d) == 0) {
    s3d_format = GetS3DFormat(s3d);
  }

  if ((interlace != layer_buffer->flags.interlace) ||
      (frame_rate != layer->frame_rate) || (s3d_format != layer_buffer->s3d_format)) {
    // Layer buffer metadata has changed.
    needs_validate_ = true;
    layer->frame_rate = frame_rate;
    layer_buffer->s3d_format = s3d_format;
    layer_buffer->flags.interlace = interlace;
  }

  return kErrorNone;
}


bool HWCLayer::SupportLocalConversion(ColorPrimaries working_primaries) {
  if (layer_->input_buffer.color_metadata.colorPrimaries <= ColorPrimaries_BT601_6_525 &&
      working_primaries <= ColorPrimaries_BT601_6_525) {
    return true;
  }
  return false;
}

bool HWCLayer::ValidateAndSetCSC() {
  if (client_requested_ != HWC2::Composition::Device &&
      client_requested_ != HWC2::Composition::Cursor) {
    // Check the layers which are configured to Device
    return true;
  }

  LayerBuffer *layer_buffer = &layer_->input_buffer;
  bool use_color_metadata = true;
#ifdef FEATURE_WIDE_COLOR
  ColorMetaData csc = {};
  if (dataspace_ != HAL_DATASPACE_UNKNOWN) {
    use_color_metadata = false;
    bool valid_csc = GetSDMColorSpace(dataspace_, &csc);
    if (!valid_csc) {
      return false;
    }
    // if we are here here, update the sdm layer csc.
    layer_buffer->color_metadata.transfer = csc.transfer;
    layer_buffer->color_metadata.colorPrimaries = csc.colorPrimaries;
    layer_buffer->color_metadata.range = csc.range;
  }
#endif

  if (IsBT2020(layer_buffer->color_metadata.colorPrimaries)) {
     // android_dataspace_t doesnt support mastering display and light levels
     // so retrieve it from metadata for BT2020(HDR)
     use_color_metadata = true;
  }

  if (use_color_metadata) {
    const private_handle_t *handle =
      reinterpret_cast<const private_handle_t *>(layer_buffer->buffer_id);
    if (sdm::SetCSC(handle, &layer_buffer->color_metadata) != kErrorNone) {
      return false;
    }
  }

  return true;
}


uint32_t HWCLayer::RoundToStandardFPS(float fps) {
  static const uint32_t standard_fps[4] = {24, 30, 48, 60};
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

void HWCLayer::SetComposition(const LayerComposition &sdm_composition) {
  auto hwc_composition = HWC2::Composition::Invalid;
  switch (sdm_composition) {
    case kCompositionGPU:
      hwc_composition = HWC2::Composition::Client;
      break;
    case kCompositionHWCursor:
      hwc_composition = HWC2::Composition::Cursor;
      break;
    default:
      hwc_composition = HWC2::Composition::Device;
      break;
  }
  // Update solid fill composition
  if (sdm_composition == kCompositionSDE && layer_->flags.solid_fill != 0) {
    hwc_composition = HWC2::Composition::SolidColor;
  }
  device_selected_ = hwc_composition;

  return;
}
void HWCLayer::PushReleaseFence(int32_t fence) {
  release_fences_.push(fence);
}
int32_t HWCLayer::PopReleaseFence(void) {
  if (release_fences_.empty())
    return -1;
  auto fence = release_fences_.front();
  release_fences_.pop();
  return fence;
}

}  // namespace sdm
