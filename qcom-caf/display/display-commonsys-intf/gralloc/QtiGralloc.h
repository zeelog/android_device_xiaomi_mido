/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *    * Neither the name of The Linux Foundation. nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
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

#ifndef __QTIGRALLOC_H__
#define __QTIGRALLOC_H__

#include <android/hardware/graphics/mapper/4.0/IMapper.h>
#include <gralloctypes/Gralloc4.h>
#include <hidl/HidlSupport.h>
#include <stdint.h>

#include <string>

#include "QtiGrallocDefs.h"
#include "QtiGrallocMetadata.h"

namespace qtigralloc {
using android::sp;
using android::hardware::hidl_vec;
using MetadataType = android::hardware::graphics::mapper::V4_0::IMapper::MetadataType;
using android::hardware::graphics::mapper::V4_0::Error;
// from gr_priv_handle.h
enum {
  PRIV_FLAGS_USES_ION = 0x00000008,
  PRIV_FLAGS_NEEDS_FLUSH = 0x00000020,
  PRIV_FLAGS_NON_CPU_WRITER = 0x00000080,
  PRIV_FLAGS_CACHED = 0x00000200,
  PRIV_FLAGS_SECURE_BUFFER = 0x00000400,
  PRIV_FLAGS_VIDEO_ENCODER = 0x00010000,
  PRIV_FLAGS_CAMERA_WRITE = 0x00020000,
  PRIV_FLAGS_CAMERA_READ = 0x00040000,
  PRIV_FLAGS_HW_TEXTURE = 0x00100000,
  PRIV_FLAGS_SECURE_DISPLAY = 0x01000000,
  PRIV_FLAGS_TILE_RENDERED = 0x02000000,
  PRIV_FLAGS_UBWC_ALIGNED = 0x08000000,
  PRIV_FLAGS_UBWC_ALIGNED_PI = 0x40000000,  // PI format
};

// Metadata
static const std::string VENDOR_QTI = "QTI";

Error get(void *buffer, uint32_t type, void *param);
Error set(void *buffer, uint32_t type, void *param);
MetadataType getMetadataType(uint32_t in);
int getMetadataState(void *buffer, uint32_t type);

static const MetadataType MetadataType_VTTimestamp = {VENDOR_QTI, QTI_VT_TIMESTAMP};

static const MetadataType MetadataType_ColorMetadata = {VENDOR_QTI, QTI_COLOR_METADATA};

static const MetadataType MetadataType_PPParamInterlaced = {VENDOR_QTI, QTI_PP_PARAM_INTERLACED};

static const MetadataType MetadataType_VideoPerfMode = {VENDOR_QTI, QTI_VIDEO_PERF_MODE};

static const MetadataType MetadataType_GraphicsMetadata = {VENDOR_QTI, QTI_GRAPHICS_METADATA};

static const MetadataType MetadataType_UBWCCRStatsInfo = {VENDOR_QTI, QTI_UBWC_CR_STATS_INFO};

static const MetadataType MetadataType_RefreshRate = {VENDOR_QTI, QTI_REFRESH_RATE};
static const MetadataType MetadataType_MapSecureBuffer = {VENDOR_QTI, QTI_MAP_SECURE_BUFFER};

static const MetadataType MetadataType_LinearFormat = {VENDOR_QTI, QTI_LINEAR_FORMAT};

static const MetadataType MetadataType_SingleBufferMode = {VENDOR_QTI, QTI_SINGLE_BUFFER_MODE};

static const MetadataType MetadataType_CVPMetadata = {VENDOR_QTI, QTI_CVP_METADATA};

static const MetadataType MetadataType_VideoHistogramStats = {VENDOR_QTI,
                                                              QTI_VIDEO_HISTOGRAM_STATS};

static const MetadataType MetadataType_VideoTimestampInfo = {VENDOR_QTI, QTI_VIDEO_TS_INFO};

static const MetadataType MetadataType_FD = {VENDOR_QTI, QTI_FD};

static const MetadataType MetadataType_PrivateFlags = {VENDOR_QTI, QTI_PRIVATE_FLAGS};

static const MetadataType MetadataType_AlignedWidthInPixels = {VENDOR_QTI,
                                                               QTI_ALIGNED_WIDTH_IN_PIXELS};

static const MetadataType MetadataType_AlignedHeightInPixels = {VENDOR_QTI,
                                                                QTI_ALIGNED_HEIGHT_IN_PIXELS};

static const MetadataType MetadataType_StandardMetadataStatus = {VENDOR_QTI,
                                                                 QTI_STANDARD_METADATA_STATUS};

static const MetadataType MetadataType_VendorMetadataStatus = {VENDOR_QTI,
                                                               QTI_VENDOR_METADATA_STATUS};

static const MetadataType MetadataType_BufferType = {VENDOR_QTI,
                                                     QTI_BUFFER_TYPE};

static const MetadataType MetadataType_CustomDimensionsStride = {VENDOR_QTI,
                                                                 QTI_CUSTOM_DIMENSIONS_STRIDE};

static const MetadataType MetadataType_CustomDimensionsHeight = {VENDOR_QTI,
                                                                 QTI_CUSTOM_DIMENSIONS_HEIGHT};

static const MetadataType MetadataType_RgbDataAddress = {VENDOR_QTI, QTI_RGB_DATA_ADDRESS};

static const MetadataType MetadataType_ColorSpace = {VENDOR_QTI, QTI_COLORSPACE};
static const MetadataType MetadataType_YuvPlaneInfo = {VENDOR_QTI, QTI_YUV_PLANE_INFO};
// 0 is also used as invalid value in standard metadata
static const MetadataType MetadataType_Invalid = {VENDOR_QTI, 0};

static const aidl::android::hardware::graphics::common::ExtendableType Compression_QtiUBWC = {
    VENDOR_QTI, COMPRESSION_QTI_UBWC};
static const aidl::android::hardware::graphics::common::ExtendableType Interlaced_Qti = {
    VENDOR_QTI, INTERLACED_QTI};

static const aidl::android::hardware::graphics::common::ExtendableType
    PlaneLayoutComponentType_Raw = {VENDOR_QTI, PLANE_COMPONENT_TYPE_RAW};
static const aidl::android::hardware::graphics::common::ExtendableType
    PlaneLayoutComponentType_Meta = {VENDOR_QTI, PLANE_COMPONENT_TYPE_META};

Error decodeMetadataState(hidl_vec<uint8_t> &in, bool *out);
Error encodeMetadataState(bool *in, hidl_vec<uint8_t> *out);
Error decodeColorMetadata(hidl_vec<uint8_t> &in, ColorMetaData *out);
Error encodeColorMetadata(ColorMetaData &in, hidl_vec<uint8_t> *out);
Error decodeGraphicsMetadata(hidl_vec<uint8_t> &in, GraphicsMetadata *out);
Error encodeGraphicsMetadata(GraphicsMetadata &in, hidl_vec<uint8_t> *out);
Error decodeGraphicsMetadataRaw(hidl_vec<uint8_t> &in, void *out);
Error encodeGraphicsMetadataRaw(void *in, hidl_vec<uint8_t> *out);
Error decodeUBWCStats(hidl_vec<uint8_t> &in, UBWCStats *out);
Error encodeUBWCStats(UBWCStats *in, hidl_vec<uint8_t> *out);
Error decodeCVPMetadata(hidl_vec<uint8_t> &in, CVPMetadata *out);
Error encodeCVPMetadata(CVPMetadata &in, hidl_vec<uint8_t> *out);
Error decodeVideoHistogramMetadata(hidl_vec<uint8_t> &in, VideoHistogramMetadata *out);
Error encodeVideoHistogramMetadata(VideoHistogramMetadata &in, hidl_vec<uint8_t> *out);
Error decodeVideoTimestampInfo(hidl_vec<uint8_t> &in, VideoTimestampInfo *out);
Error encodeVideoTimestampInfo(VideoTimestampInfo &in, hidl_vec<uint8_t> *out);
Error decodeYUVPlaneInfoMetadata(hidl_vec<uint8_t> &in, qti_ycbcr *out);
Error encodeYUVPlaneInfoMetadata(qti_ycbcr *in, hidl_vec<uint8_t> *out);
}  // namespace qtigralloc

#endif  //__QTIGRALLOC_H__
