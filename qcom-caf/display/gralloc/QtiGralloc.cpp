/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
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

#include "QtiGralloc.h"

#include <log/log.h>
namespace qtigralloc {

using android::hardware::graphics::mapper::V4_0::IMapper;

static sp<IMapper> getInstance() {
  static sp<IMapper> mapper = IMapper::getService();
  return mapper;
}

Error decodeMetadataState(hidl_vec<uint8_t> &in, bool *out) {
  if (!in.size() || !out) {
    return Error::BAD_VALUE;
  }
  memcpy(out, in.data(), METADATA_SET_SIZE);
  return Error::NONE;
}

Error encodeMetadataState(bool *in, hidl_vec<uint8_t> *out) {
  if (!in || !out) {
    return Error::BAD_VALUE;
  }
  out->resize(sizeof(bool) * METADATA_SET_SIZE);
  memcpy(out->data(), in, sizeof(bool) * METADATA_SET_SIZE);
  return Error::NONE;
}

Error decodeColorMetadata(hidl_vec<uint8_t> &in, ColorMetaData *out) {
  if (!in.size() || !out) {
    return Error::BAD_VALUE;
  }
  memcpy(out, in.data(), sizeof(ColorMetaData));
  return Error::NONE;
}

Error encodeColorMetadata(ColorMetaData &in, hidl_vec<uint8_t> *out) {
  if (!out) {
    return Error::BAD_VALUE;
  }
  out->resize(sizeof(ColorMetaData));
  memcpy(out->data(), &in, sizeof(ColorMetaData));
  return Error::NONE;
}

// decode the raw graphics metadata from bytestream and store it in 'data' member of
// GraphicsMetadata struct during mapper->set call, 'size' member is unused.
Error decodeGraphicsMetadata(hidl_vec<uint8_t> &in, GraphicsMetadata *out) {
  if (!in.size() || !out) {
    return Error::BAD_VALUE;
  }
  memcpy(&(out->data), in.data(), GRAPHICS_METADATA_SIZE_IN_BYTES);
  return Error::NONE;
}

// encode only 'data' member of GraphicsMetadata struct for retrieval of
// graphics metadata during mapper->get call
Error encodeGraphicsMetadata(GraphicsMetadata &in, hidl_vec<uint8_t> *out) {
  if (!out) {
    return Error::BAD_VALUE;
  }
  out->resize(GRAPHICS_METADATA_SIZE_IN_BYTES);
  memcpy(out->data(), &(in.data), GRAPHICS_METADATA_SIZE_IN_BYTES);
  return Error::NONE;
}

// decode the raw graphics metadata from bytestream before presenting it to caller
Error decodeGraphicsMetadataRaw(hidl_vec<uint8_t> &in, void *out) {
  if (!in.size() || !out) {
    return Error::BAD_VALUE;
  }
  memcpy(out, in.data(), GRAPHICS_METADATA_SIZE_IN_BYTES);
  return Error::NONE;
}

// encode the raw graphics metadata in bytestream before calling mapper->set
Error encodeGraphicsMetadataRaw(void *in, hidl_vec<uint8_t> *out) {
  if (!in || !out) {
    return Error::BAD_VALUE;
  }
  out->resize(GRAPHICS_METADATA_SIZE_IN_BYTES);
  memcpy(out->data(), in, GRAPHICS_METADATA_SIZE_IN_BYTES);
  return Error::NONE;
}

Error decodeUBWCStats(hidl_vec<uint8_t> &in, UBWCStats *out) {
  if (!in.size() || !out) {
    return Error::BAD_VALUE;
  }
  memcpy(out, in.data(), UBWC_STATS_ARRAY_SIZE * sizeof(UBWCStats));
  return Error::NONE;
}

Error encodeUBWCStats(UBWCStats *in, hidl_vec<uint8_t> *out) {
  if (!in || !out) {
    return Error::BAD_VALUE;
  }
  out->resize(UBWC_STATS_ARRAY_SIZE * sizeof(UBWCStats));
  memcpy(out->data(), in, UBWC_STATS_ARRAY_SIZE * sizeof(UBWCStats));
  return Error::NONE;
}

Error decodeCVPMetadata(hidl_vec<uint8_t> &in, CVPMetadata *out) {
  if (!in.size() || !out) {
    return Error::BAD_VALUE;
  }
  memcpy(out, in.data(), sizeof(CVPMetadata));
  return Error::NONE;
}

Error encodeCVPMetadata(CVPMetadata &in, hidl_vec<uint8_t> *out) {
  if (!out) {
    return Error::BAD_VALUE;
  }
  out->resize(sizeof(CVPMetadata));
  memcpy(out->data(), &in, sizeof(CVPMetadata));
  return Error::NONE;
}

Error decodeVideoHistogramMetadata(hidl_vec<uint8_t> &in, VideoHistogramMetadata *out) {
  if (!in.size() || !out) {
    return Error::BAD_VALUE;
  }
  memcpy(out, in.data(), sizeof(VideoHistogramMetadata));
  return Error::NONE;
}

Error encodeVideoHistogramMetadata(VideoHistogramMetadata &in, hidl_vec<uint8_t> *out) {
  if (!out) {
    return Error::BAD_VALUE;
  }
  out->resize(sizeof(VideoHistogramMetadata));
  memcpy(out->data(), &in, sizeof(VideoHistogramMetadata));
  return Error::NONE;
}

Error decodeVideoTimestampInfo(hidl_vec<uint8_t> &in, VideoTimestampInfo *out) {
  if (!in.size() || !out) {
    return Error::BAD_VALUE;
  }
  memcpy(out, in.data(), sizeof(VideoTimestampInfo));
  return Error::NONE;
}

Error encodeVideoTimestampInfo(VideoTimestampInfo &in, hidl_vec<uint8_t> *out) {
  if (!out) {
    return Error::BAD_VALUE;
  }
  out->resize(sizeof(VideoTimestampInfo));
  memcpy(out->data(), &in, sizeof(VideoTimestampInfo));
  return Error::NONE;
}

Error decodeYUVPlaneInfoMetadata(hidl_vec<uint8_t> &in, qti_ycbcr *out) {
  if (!in.size() || !out) {
    return Error::BAD_VALUE;
  }
  //qti_ycbcr *p = reinterpret_cast<qti_ycbcr *>(in.data());
  memcpy(out, in.data(), (YCBCR_LAYOUT_ARRAY_SIZE * sizeof(qti_ycbcr)));
  return Error::NONE;
}

Error encodeYUVPlaneInfoMetadata(qti_ycbcr *in, hidl_vec<uint8_t> *out) {
  if (!out) {
    return Error::BAD_VALUE;
  }
  out->resize(YCBCR_LAYOUT_ARRAY_SIZE * sizeof(qti_ycbcr));
  memcpy(out->data(), in, YCBCR_LAYOUT_ARRAY_SIZE * sizeof(qti_ycbcr));
  return Error::NONE;
}

MetadataType getMetadataType(uint32_t in) {
  switch (in) {
    case QTI_VT_TIMESTAMP:
      return MetadataType_VTTimestamp;
    case QTI_VIDEO_PERF_MODE:
      return MetadataType_VideoPerfMode;
    case QTI_LINEAR_FORMAT:
      return MetadataType_LinearFormat;
    case QTI_SINGLE_BUFFER_MODE:
      return MetadataType_SingleBufferMode;
    case QTI_PP_PARAM_INTERLACED:
      return MetadataType_PPParamInterlaced;
    case QTI_MAP_SECURE_BUFFER:
      return MetadataType_MapSecureBuffer;
    case QTI_COLOR_METADATA:
      return MetadataType_ColorMetadata;
    case QTI_GRAPHICS_METADATA:
      return MetadataType_GraphicsMetadata;
    case QTI_UBWC_CR_STATS_INFO:
      return MetadataType_UBWCCRStatsInfo;
    case QTI_REFRESH_RATE:
      return MetadataType_RefreshRate;
    case QTI_CVP_METADATA:
      return MetadataType_CVPMetadata;
    case QTI_VIDEO_HISTOGRAM_STATS:
      return MetadataType_VideoHistogramStats;
    case QTI_VIDEO_TS_INFO:
      return MetadataType_VideoTimestampInfo;
    case QTI_FD:
      return MetadataType_FD;
    case QTI_PRIVATE_FLAGS:
      return MetadataType_PrivateFlags;
    case QTI_ALIGNED_WIDTH_IN_PIXELS:
      return MetadataType_AlignedWidthInPixels;
    case QTI_ALIGNED_HEIGHT_IN_PIXELS:
      return MetadataType_AlignedHeightInPixels;
    case QTI_STANDARD_METADATA_STATUS:
      return MetadataType_StandardMetadataStatus;
    case QTI_VENDOR_METADATA_STATUS:
      return MetadataType_VendorMetadataStatus;
    case QTI_BUFFER_TYPE:
      return MetadataType_BufferType;
    case QTI_CUSTOM_DIMENSIONS_STRIDE:
      return MetadataType_CustomDimensionsStride;
    case QTI_CUSTOM_DIMENSIONS_HEIGHT:
      return MetadataType_CustomDimensionsHeight;
    case QTI_RGB_DATA_ADDRESS:
      return MetadataType_RgbDataAddress;
    case QTI_COLORSPACE:
      return MetadataType_ColorSpace;
    case QTI_YUV_PLANE_INFO:
      return MetadataType_YuvPlaneInfo;
    default:
      return MetadataType_Invalid;
  }
}

Error get(void *buffer, uint32_t type, void *param) {
  hidl_vec<uint8_t> bytestream;
  sp<IMapper> mapper = getInstance();

  MetadataType metadata_type = getMetadataType(type);
  if (metadata_type == MetadataType_Invalid) {
    param = nullptr;
    return Error::UNSUPPORTED;
  }

  auto err = Error::UNSUPPORTED;
  mapper->get(buffer, metadata_type, [&](const auto &tmpError, const auto &tmpByteStream) {
    err = tmpError;
    bytestream = tmpByteStream;
  });

  if (err != Error::NONE) {
    return err;
  }

  switch (type) {
    case QTI_VT_TIMESTAMP:
      err = static_cast<Error>(android::gralloc4::decodeUint64(
          qtigralloc::MetadataType_VTTimestamp, bytestream, reinterpret_cast<uint64_t *>(param)));
      break;
    case QTI_VIDEO_PERF_MODE:
      err = static_cast<Error>(android::gralloc4::decodeUint32(
          qtigralloc::MetadataType_VideoPerfMode, bytestream, reinterpret_cast<uint32_t *>(param)));
      break;
    case QTI_LINEAR_FORMAT:
      err = static_cast<Error>(android::gralloc4::decodeUint32(
          qtigralloc::MetadataType_LinearFormat, bytestream, reinterpret_cast<uint32_t *>(param)));
      break;
    case QTI_SINGLE_BUFFER_MODE:
      err = static_cast<Error>(
          android::gralloc4::decodeUint32(qtigralloc::MetadataType_SingleBufferMode, bytestream,
                                          reinterpret_cast<uint32_t *>(param)));
      break;
    case QTI_PP_PARAM_INTERLACED:
      err = static_cast<Error>(
          android::gralloc4::decodeInt32(qtigralloc::MetadataType_PPParamInterlaced, bytestream,
                                         reinterpret_cast<int32_t *>(param)));
      break;
    case QTI_MAP_SECURE_BUFFER:
      err = static_cast<Error>(
          android::gralloc4::decodeInt32(qtigralloc::MetadataType_MapSecureBuffer, bytestream,
                                         reinterpret_cast<int32_t *>(param)));
      break;
    case QTI_COLOR_METADATA:
      err = decodeColorMetadata(bytestream, reinterpret_cast<ColorMetaData *>(param));
      break;
    case QTI_GRAPHICS_METADATA:
      err = decodeGraphicsMetadataRaw(bytestream, param);
      break;
    case QTI_UBWC_CR_STATS_INFO:
      err = decodeUBWCStats(bytestream, reinterpret_cast<UBWCStats *>(param));
      break;
    case QTI_REFRESH_RATE:
      err = static_cast<Error>(android::gralloc4::decodeFloat(
          qtigralloc::MetadataType_RefreshRate, bytestream, reinterpret_cast<float *>(param)));
      break;
    case QTI_CVP_METADATA:
      err = decodeCVPMetadata(bytestream, reinterpret_cast<CVPMetadata *>(param));
      break;
    case QTI_VIDEO_HISTOGRAM_STATS:
      err = decodeVideoHistogramMetadata(bytestream,
                                         reinterpret_cast<VideoHistogramMetadata *>(param));
      break;
    case QTI_VIDEO_TS_INFO:
      err = decodeVideoTimestampInfo(bytestream, reinterpret_cast<VideoTimestampInfo *>(param));
      break;
    case QTI_FD:
      err = static_cast<Error>(android::gralloc4::decodeInt32(
          qtigralloc::MetadataType_FD, bytestream, reinterpret_cast<int32_t *>(param)));
      break;
    case QTI_PRIVATE_FLAGS:
      err = static_cast<Error>(android::gralloc4::decodeInt32(
          qtigralloc::MetadataType_PrivateFlags, bytestream, reinterpret_cast<int32_t *>(param)));
      break;
    case QTI_ALIGNED_WIDTH_IN_PIXELS:
      err = static_cast<Error>(
          android::gralloc4::decodeUint32(qtigralloc::MetadataType_AlignedWidthInPixels, bytestream,
                                          reinterpret_cast<uint32_t *>(param)));
      break;
    case QTI_ALIGNED_HEIGHT_IN_PIXELS:
      err = static_cast<Error>(
          android::gralloc4::decodeUint32(qtigralloc::MetadataType_AlignedHeightInPixels,
                                          bytestream, reinterpret_cast<uint32_t *>(param)));
      break;
    case QTI_STANDARD_METADATA_STATUS:
    case QTI_VENDOR_METADATA_STATUS:
      err = decodeMetadataState(bytestream, reinterpret_cast<bool *>(param));
      break;
    case QTI_BUFFER_TYPE:
      err = static_cast<Error>(android::gralloc4::decodeUint32(
          qtigralloc::MetadataType_BufferType, bytestream, reinterpret_cast<uint32_t *>(param)));
      break;
    case QTI_CUSTOM_DIMENSIONS_STRIDE:
      err = static_cast<Error>(
          android::gralloc4::decodeUint32(qtigralloc::MetadataType_CustomDimensionsStride,
                                          bytestream, reinterpret_cast<uint32_t *>(param)));
      break;
    case QTI_CUSTOM_DIMENSIONS_HEIGHT:
      err = static_cast<Error>(
          android::gralloc4::decodeUint32(qtigralloc::MetadataType_CustomDimensionsHeight,
                                          bytestream, reinterpret_cast<uint32_t *>(param)));
      break;
    case QTI_RGB_DATA_ADDRESS:
      err = static_cast<Error>(
          android::gralloc4::decodeUint64(qtigralloc::MetadataType_RgbDataAddress, bytestream,
                                          reinterpret_cast<uint64_t *>(param)));
      break;
    case QTI_COLORSPACE:
      err = static_cast<Error>(android::gralloc4::decodeUint32(
          qtigralloc::MetadataType_ColorSpace, bytestream, reinterpret_cast<uint32_t *>(param)));
      break;
    case QTI_YUV_PLANE_INFO:
      err = decodeYUVPlaneInfoMetadata(bytestream, reinterpret_cast<qti_ycbcr *>(param));
      break;
    default:
      param = nullptr;
      return Error::UNSUPPORTED;
  }

  return err;
}

Error set(void *buffer, uint32_t type, void *param) {
  hidl_vec<uint8_t> bytestream;
  sp<IMapper> mapper = getInstance();

  Error err = Error::UNSUPPORTED;
  MetadataType metadata_type = getMetadataType(type);
  if (metadata_type == MetadataType_Invalid) {
    return err;
  }

  switch (type) {
    case QTI_VT_TIMESTAMP:
      err = static_cast<Error>(android::gralloc4::encodeUint64(
          qtigralloc::MetadataType_VTTimestamp, *reinterpret_cast<uint64_t *>(param), &bytestream));
      break;
    case QTI_VIDEO_PERF_MODE:
      err = static_cast<Error>(
          android::gralloc4::encodeUint32(qtigralloc::MetadataType_VideoPerfMode,
                                          *reinterpret_cast<uint32_t *>(param), &bytestream));
      break;
    case QTI_LINEAR_FORMAT:
      err = static_cast<Error>(
          android::gralloc4::encodeUint32(qtigralloc::MetadataType_LinearFormat,
                                          *reinterpret_cast<uint32_t *>(param), &bytestream));
      break;
    case QTI_SINGLE_BUFFER_MODE:
      err = static_cast<Error>(
          android::gralloc4::encodeUint32(qtigralloc::MetadataType_SingleBufferMode,
                                          *reinterpret_cast<uint32_t *>(param), &bytestream));
      break;
    case QTI_PP_PARAM_INTERLACED:
      err = static_cast<Error>(
          android::gralloc4::encodeInt32(qtigralloc::MetadataType_PPParamInterlaced,
                                         *reinterpret_cast<int32_t *>(param), &bytestream));
      break;
    case QTI_MAP_SECURE_BUFFER:
      err = static_cast<Error>(
          android::gralloc4::encodeInt32(qtigralloc::MetadataType_MapSecureBuffer,
                                         *reinterpret_cast<int32_t *>(param), &bytestream));
      break;
    case QTI_COLOR_METADATA:
      err = encodeColorMetadata(*reinterpret_cast<ColorMetaData *>(param), &bytestream);
      break;
    case QTI_GRAPHICS_METADATA:
      err = encodeGraphicsMetadataRaw(param, &bytestream);
      break;
    case QTI_UBWC_CR_STATS_INFO:
      err = encodeUBWCStats(reinterpret_cast<UBWCStats *>(param), &bytestream);
      break;
    case QTI_REFRESH_RATE:
      err = static_cast<Error>(android::gralloc4::encodeFloat(
          qtigralloc::MetadataType_RefreshRate, *reinterpret_cast<float *>(param), &bytestream));
      break;
    case QTI_CVP_METADATA:
      err = encodeCVPMetadata(*reinterpret_cast<CVPMetadata *>(param), &bytestream);
      break;
    case QTI_VIDEO_HISTOGRAM_STATS:
      err = encodeVideoHistogramMetadata(*reinterpret_cast<VideoHistogramMetadata *>(param),
                                         &bytestream);
      break;
    case QTI_VIDEO_TS_INFO:
      err = encodeVideoTimestampInfo(*reinterpret_cast<VideoTimestampInfo *>(param), &bytestream);
      break;
    default:
      param = nullptr;
      return Error::UNSUPPORTED;
  }

  if (err != Error::NONE) {
    return err;
  }

  return mapper->set(reinterpret_cast<void *>(buffer), metadata_type, bytestream);
}

int getMetadataState(void *buffer, uint32_t type) {
  bool metadata_set[METADATA_SET_SIZE];
  Error err;
  if (IS_VENDOR_METADATA_TYPE(type)) {
    err = get(buffer, QTI_VENDOR_METADATA_STATUS, &metadata_set);
  } else {
    err = get(buffer, QTI_STANDARD_METADATA_STATUS, &metadata_set);
  }

  if (err != Error::NONE) {
    ALOGE("Unable to get metadata state");
    return -1;
  }

  if (IS_VENDOR_METADATA_TYPE(type)) {
    return metadata_set[GET_VENDOR_METADATA_STATUS_INDEX(type)];
  } else if (GET_STANDARD_METADATA_STATUS_INDEX(type) < METADATA_SET_SIZE) {
    return metadata_set[GET_STANDARD_METADATA_STATUS_INDEX(type)];
  } else {
    return -1;
  }
}

}  // namespace qtigralloc
