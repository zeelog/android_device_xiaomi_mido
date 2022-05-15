/*
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
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

#include "qdMetaData.h"

#include <QtiGrallocPriv.h>
#include <errno.h>
#include <gralloc_priv.h>
#ifndef __QTI_NO_GRALLOC4__
#include <gralloctypes/Gralloc4.h>
#endif
#include <log/log.h>
#include <string.h>
#include <sys/mman.h>

#include <cinttypes>

static int colorMetaDataToColorSpace(ColorMetaData in, ColorSpace_t *out) {
  if (in.colorPrimaries == ColorPrimaries_BT601_6_525 ||
      in.colorPrimaries == ColorPrimaries_BT601_6_625) {
    if (in.range == Range_Full) {
      *out = ITU_R_601_FR;
    } else {
      *out = ITU_R_601;
    }
  } else if (in.colorPrimaries == ColorPrimaries_BT2020) {
    if (in.range == Range_Full) {
      *out = ITU_R_2020_FR;
    } else {
      *out = ITU_R_2020;
    }
  } else if (in.colorPrimaries == ColorPrimaries_BT709_5) {
    if (in.range == Range_Full) {
      *out = ITU_R_709_FR;
    } else {
      *out = ITU_R_709;
    }
  } else {
    ALOGE(
        "Cannot convert ColorMetaData to ColorSpace_t. "
        "Primaries = %d, Range = %d",
        in.colorPrimaries, in.range);
    return -1;
  }

  return 0;
}

static int colorSpaceToColorMetadata(ColorSpace_t in, ColorMetaData *out) {
  out->transfer = Transfer_sRGB;
  switch (in) {
    case ITU_R_601:
      out->colorPrimaries = ColorPrimaries_BT601_6_525;
      out->range = Range_Limited;
      break;
    case ITU_R_601_FR:
      out->colorPrimaries = ColorPrimaries_BT601_6_525;
      out->range = Range_Full;
      break;
    case ITU_R_709:
      out->colorPrimaries = ColorPrimaries_BT709_5;
      out->range = Range_Limited;
      break;
    case ITU_R_709_FR:
      out->colorPrimaries = ColorPrimaries_BT709_5;
      out->range = Range_Full;
      break;
    case ITU_R_2020:
      out->colorPrimaries = ColorPrimaries_BT2020;
      out->range = Range_Limited;
      break;
    case ITU_R_2020_FR:
      out->colorPrimaries = ColorPrimaries_BT2020;
      out->range = Range_Full;
      break;
    default:
      ALOGE("Cannot convert ColorSpace_t %d to ColorMetaData", in);
      return -1;
      break;
  }

  return 0;
}

#ifndef __QTI_NO_GRALLOC4__
static bool getGralloc4Array(MetaData_t *metadata, int32_t paramType) {
  switch (paramType) {
    case SET_VT_TIMESTAMP:
      return metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_VT_TIMESTAMP)];
    case COLOR_METADATA:
      return metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_COLOR_METADATA)];
    case PP_PARAM_INTERLACED:
      return metadata
          ->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_PP_PARAM_INTERLACED)];
    case SET_VIDEO_PERF_MODE:
      return metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_VIDEO_PERF_MODE)];
    case SET_GRAPHICS_METADATA:
      return metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_GRAPHICS_METADATA)];
    case SET_UBWC_CR_STATS_INFO:
      return metadata
          ->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_UBWC_CR_STATS_INFO)];
    case UPDATE_BUFFER_GEOMETRY:
      return metadata->isStandardMetadataSet[GET_STANDARD_METADATA_STATUS_INDEX(
          ::android::gralloc4::MetadataType_Crop.value)];
    case UPDATE_REFRESH_RATE:
      return metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_REFRESH_RATE)];
    case UPDATE_COLOR_SPACE:
      return metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_COLOR_METADATA)];
    case MAP_SECURE_BUFFER:
      return metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_MAP_SECURE_BUFFER)];
    case LINEAR_FORMAT:
      return metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_LINEAR_FORMAT)];
    case SET_SINGLE_BUFFER_MODE:
      return metadata
          ->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_SINGLE_BUFFER_MODE)];
    case SET_CVP_METADATA:
      return metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_CVP_METADATA)];
    case SET_VIDEO_HISTOGRAM_STATS:
      return metadata
          ->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_VIDEO_HISTOGRAM_STATS)];
    case SET_VIDEO_TS_INFO:
      return metadata
          ->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_VIDEO_TS_INFO)];
    case GET_S3D_FORMAT:
      return metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_S3D_FORMAT)];
    default:
      ALOGE("paramType %d not supported", paramType);
      return false;
  }
}

static void setGralloc4Array(MetaData_t *metadata, int32_t paramType, bool isSet) {
  switch (paramType) {
    case SET_VT_TIMESTAMP:
      metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_VT_TIMESTAMP)] = isSet;
      break;
    case COLOR_METADATA:
      metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_COLOR_METADATA)] = isSet;
      break;
    case PP_PARAM_INTERLACED:
      metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_PP_PARAM_INTERLACED)] =
          isSet;
      break;
    case SET_VIDEO_PERF_MODE:
      metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_VIDEO_PERF_MODE)] = isSet;
      break;
    case SET_GRAPHICS_METADATA:
      metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_GRAPHICS_METADATA)] =
          isSet;
      break;
    case SET_UBWC_CR_STATS_INFO:
      metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_UBWC_CR_STATS_INFO)] =
          isSet;
      break;
    case UPDATE_BUFFER_GEOMETRY:
      metadata->isStandardMetadataSet[GET_STANDARD_METADATA_STATUS_INDEX(
          ::android::gralloc4::MetadataType_Crop.value)] = isSet;
      break;
    case UPDATE_REFRESH_RATE:
      metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_REFRESH_RATE)] = isSet;
      break;
    case UPDATE_COLOR_SPACE:
      metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_COLOR_METADATA)] = isSet;
      break;
    case MAP_SECURE_BUFFER:
      metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_MAP_SECURE_BUFFER)] =
          isSet;
      break;
    case LINEAR_FORMAT:
      metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_LINEAR_FORMAT)] = isSet;
      break;
    case SET_SINGLE_BUFFER_MODE:
      metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_SINGLE_BUFFER_MODE)] =
          isSet;
      break;
    case SET_CVP_METADATA:
      metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_CVP_METADATA)] = isSet;
      break;
    case SET_VIDEO_HISTOGRAM_STATS:
      metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_VIDEO_HISTOGRAM_STATS)] =
          isSet;
      break;
    case SET_VIDEO_TS_INFO:
      metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_VIDEO_TS_INFO)] =
          isSet;
      break;
    case S3D_FORMAT:
      metadata->isVendorMetadataSet[GET_VENDOR_METADATA_STATUS_INDEX(QTI_S3D_FORMAT)] = isSet;
      break;
    default:
      ALOGE("paramType %d not supported in Gralloc4", paramType);
  }
}
#else
static bool getGralloc4Array(MetaData_t *metadata, int32_t paramType) {
  return true;
}

static void setGralloc4Array(MetaData_t *metadata, int32_t paramType, bool isSet) {
}
#endif


unsigned long getMetaDataSize() {
    return static_cast<unsigned long>(ROUND_UP_PAGESIZE(sizeof(MetaData_t)));
}

// Cannot add default argument to existing function
unsigned long getMetaDataSizeWithReservedRegion(uint64_t reserved_size) {
  return static_cast<unsigned long>(ROUND_UP_PAGESIZE(sizeof(MetaData_t) + reserved_size));
}

static int validateAndMap(private_handle_t* handle) {
    if (private_handle_t::validate(handle)) {
        ALOGE("%s: Private handle is invalid - handle:%p", __func__, handle);
        return -1;
    }
    if (handle->fd_metadata < 0) {
      // Metadata cannot be used
      return -1;
    }

    if (!handle->base_metadata) {
        auto size = getMetaDataSize();
        void *base = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED,
                handle->fd_metadata, 0);
        if (base == reinterpret_cast<void*>(MAP_FAILED)) {
            ALOGE("%s: metadata mmap failed - handle:%p fd: %d err: %s",
                __func__, handle, handle->fd_metadata, strerror(errno));
            return -1;
        }
        handle->base_metadata = (uintptr_t) base;
        auto metadata = reinterpret_cast<MetaData_t *>(handle->base_metadata);
        if (metadata->reservedSize) {
          auto reserved_size = metadata->reservedSize;
          munmap(reinterpret_cast<void *>(handle->base_metadata), getMetaDataSize());
          handle->base_metadata = 0;
          size = getMetaDataSizeWithReservedRegion(reserved_size);
          void *new_base =
              mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, handle->fd_metadata, 0);
          if (new_base == reinterpret_cast<void *>(MAP_FAILED)) {
            ALOGE("%s: metadata mmap failed - handle:%p fd: %d err: %s", __func__, handle,
                  handle->fd_metadata, strerror(errno));
            return -1;
          }
          handle->base_metadata = (uintptr_t)new_base;
        }
    }
    return 0;
}

static void unmapAndReset(private_handle_t *handle) {
    if (private_handle_t::validate(handle) == 0 && handle->base_metadata) {
      // If reservedSize is 0, the return value will be the same as getMetaDataSize
      auto metadata = reinterpret_cast<MetaData_t *>(handle->base_metadata);
      auto size = getMetaDataSizeWithReservedRegion(metadata->reservedSize);
      munmap(reinterpret_cast<void *>(handle->base_metadata), size);
      handle->base_metadata = 0;
    }
}

int setMetaData(private_handle_t *handle, DispParamType paramType,
                void *param) {
    auto err = validateAndMap(handle);
    if (err != 0)
        return err;
    return setMetaDataVa(reinterpret_cast<MetaData_t*>(handle->base_metadata),
                         paramType, param);
}

int setMetaDataVa(MetaData_t *data, DispParamType paramType,
                  void *param) {
    if (data == nullptr)
        return -EINVAL;
    // If parameter is NULL reset the specific MetaData Key
    if (!param) {
      setGralloc4Array(data, paramType, false);
      switch (paramType) {
        case SET_VIDEO_PERF_MODE:
          data->isVideoPerfMode = 0;
          break;
        case SET_CVP_METADATA:
          data->cvpMetadata.size = 0;
          break;
        case SET_VIDEO_HISTOGRAM_STATS:
          data->video_histogram_stats.stat_len = 0;
          break;
        default:
          ALOGE("Unknown paramType %d", paramType);
          break;
      }
       // param unset
       return 0;
    }

    setGralloc4Array(data, paramType, true);
    switch (paramType) {
        case PP_PARAM_INTERLACED:
            data->interlaced = *((int32_t *)param);
            break;
        case UPDATE_BUFFER_GEOMETRY: {
          BufferDim_t in = *((BufferDim_t *)param);
          data->crop = {0, 0, in.sliceWidth, in.sliceHeight};
          break;
        }
        case UPDATE_REFRESH_RATE:
            data->refreshrate = *((float *)param);
            break;
        case UPDATE_COLOR_SPACE: {
          ColorMetaData color = {};
          if (!colorSpaceToColorMetadata(*((ColorSpace_t *)param), &color)) {
            data->color = color;
            break;
          }
          return -EINVAL;
        }
        case MAP_SECURE_BUFFER:
            data->mapSecureBuffer = *((int32_t *)param);
            break;
        case S3D_FORMAT:
            data->s3dFormat = *((uint32_t *)param);
            break;
        case LINEAR_FORMAT:
            data->linearFormat = *((uint32_t *)param);
            break;
        case SET_SINGLE_BUFFER_MODE:
            data->isSingleBufferMode = *((uint32_t *)param);
            break;
        case SET_VT_TIMESTAMP:
            data->vtTimeStamp = *((uint64_t *)param);
            break;
        case COLOR_METADATA:
            data->color = *((ColorMetaData *)param);
            break;
        case SET_UBWC_CR_STATS_INFO: {
          struct UBWCStats *stats = (struct UBWCStats *)param;
          int numelems = sizeof(data->ubwcCRStats) / sizeof(struct UBWCStats);
          for (int i = 0; i < numelems; i++) {
            data->ubwcCRStats[i] = stats[i];
          }
          break;
          }
        case SET_VIDEO_PERF_MODE:
            data->isVideoPerfMode = *((uint32_t *)param);
            break;
        case SET_GRAPHICS_METADATA: {
             GraphicsMetadata payload = *((GraphicsMetadata*)(param));
             data->graphics_metadata.size = payload.size;
             memcpy(data->graphics_metadata.data, payload.data,
                    sizeof(data->graphics_metadata.data));
             break;
        }
        case SET_CVP_METADATA: {
             struct CVPMetadata *cvpMetadata = (struct CVPMetadata *)param;
             if (cvpMetadata->size <= CVP_METADATA_SIZE) {
                 data->cvpMetadata.size = cvpMetadata->size;
                 memcpy(data->cvpMetadata.payload, cvpMetadata->payload,
                        cvpMetadata->size);
                 data->cvpMetadata.capture_frame_rate = cvpMetadata->capture_frame_rate;
                 data->cvpMetadata.cvp_frame_rate = cvpMetadata->cvp_frame_rate;
                 data->cvpMetadata.flags = cvpMetadata->flags;
                 memcpy(data->cvpMetadata.reserved, cvpMetadata->reserved,
                        (8 * sizeof(uint32_t)));
             } else {
               setGralloc4Array(data, paramType, false);
               ALOGE("%s: cvp metadata length %d is more than max size %d", __func__,
                     cvpMetadata->size, CVP_METADATA_SIZE);
               return -EINVAL;
             }
             break;
        }
        case SET_VIDEO_HISTOGRAM_STATS: {
            struct VideoHistogramMetadata *vidstats = (struct VideoHistogramMetadata *)param;
            if (vidstats->stat_len <= VIDEO_HISTOGRAM_STATS_SIZE) {
                memcpy(data->video_histogram_stats.stats_info,
                    vidstats->stats_info, VIDEO_HISTOGRAM_STATS_SIZE);
                data->video_histogram_stats.stat_len = vidstats->stat_len;
                data->video_histogram_stats.frame_type = vidstats->frame_type;
                data->video_histogram_stats.display_width = vidstats->display_width;
                data->video_histogram_stats.display_height = vidstats->display_height;
                data->video_histogram_stats.decode_width = vidstats->decode_width;
                data->video_histogram_stats.decode_height = vidstats->decode_height;
            } else {
              setGralloc4Array(data, paramType, false);
              ALOGE("%s: video stats length %u is more than max size %u", __func__,
                    vidstats->stat_len, VIDEO_HISTOGRAM_STATS_SIZE);
              return -EINVAL;
            }
            break;
         }
        case SET_VIDEO_TS_INFO:
            data->videoTsInfo = *((VideoTimestampInfo *)param);
            break;
        default:
            ALOGE("Unknown paramType %d", paramType);
            break;
    }
    return 0;
}

int clearMetaData(private_handle_t *handle, DispParamType paramType) {
    auto err = validateAndMap(handle);
    if (err != 0)
        return err;
    return clearMetaDataVa(reinterpret_cast<MetaData_t *>(handle->base_metadata),
            paramType);
}

int clearMetaDataVa(MetaData_t *data, DispParamType paramType) {
    if (data == nullptr)
        return -EINVAL;
    data->operation &= ~paramType;
    switch (paramType) {
        case SET_VIDEO_PERF_MODE:
            data->isVideoPerfMode = 0;
            break;
        case SET_CVP_METADATA:
            data->cvpMetadata.size = 0;
            break;
        case SET_VIDEO_HISTOGRAM_STATS:
            data->video_histogram_stats.stat_len = 0;
            break;
        default:
            ALOGE("Unknown paramType %d", paramType);
            break;
    }
    return 0;
}

int getMetaData(private_handle_t *handle, DispFetchParamType paramType,
                                                    void *param) {
    int ret = validateAndMap(handle);
    if (ret != 0)
        return ret;
    return getMetaDataVa(reinterpret_cast<MetaData_t *>(handle->base_metadata),
                         paramType, param);
}

int getMetaDataVa(MetaData_t *data, DispFetchParamType paramType,
                  void *param) {
    // Make sure we send 0 only if the operation queried is present
    int ret = -EINVAL;
    if (data == nullptr)
        return ret;
    if (param == nullptr)
        return ret;

    if (!getGralloc4Array(data, paramType)) {
      return ret;
    }

    ret = 0;

    switch (paramType) {
        case GET_PP_PARAM_INTERLACED:
          *((int32_t *)param) = data->interlaced;
          break;
        case GET_BUFFER_GEOMETRY:
          *((BufferDim_t *)param) = {data->crop.right, data->crop.bottom};
          break;
        case GET_REFRESH_RATE:
          *((float *)param) = data->refreshrate;
          break;
        case GET_COLOR_SPACE: {
          ColorSpace_t color_space;
          if (!colorMetaDataToColorSpace(data->color, &color_space)) {
            *((ColorSpace_t *)param) = color_space;
          } else {
            ret = -EINVAL;
          }
          break;
        }
        case GET_MAP_SECURE_BUFFER:
          *((int32_t *)param) = data->mapSecureBuffer;
          break;
        case GET_S3D_FORMAT:
          *((uint32_t *)param) = data->s3dFormat;
          break;
        case GET_LINEAR_FORMAT:
          *((uint32_t *)param) = data->linearFormat;
          break;
        case GET_SINGLE_BUFFER_MODE:
          *((uint32_t *)param) = data->isSingleBufferMode;
          break;
        case GET_VT_TIMESTAMP:
          *((uint64_t *)param) = data->vtTimeStamp;
          break;
        case GET_COLOR_METADATA:
          *((ColorMetaData *)param) = data->color;
          break;
        case GET_UBWC_CR_STATS_INFO: {
          struct UBWCStats *stats = (struct UBWCStats *)param;
          int numelems = sizeof(data->ubwcCRStats) / sizeof(struct UBWCStats);
          for (int i = 0; i < numelems; i++) {
            stats[i] = data->ubwcCRStats[i];
          }
          break;
        }
        case GET_VIDEO_PERF_MODE:
          *((uint32_t *)param) = data->isVideoPerfMode;
          break;
        case GET_GRAPHICS_METADATA:
          memcpy(param, data->graphics_metadata.data, sizeof(data->graphics_metadata.data));
          break;
        case GET_CVP_METADATA: {
          struct CVPMetadata *cvpMetadata = (struct CVPMetadata *)param;
          cvpMetadata->size = 0;
          if (data->cvpMetadata.size <= CVP_METADATA_SIZE) {
            cvpMetadata->size = data->cvpMetadata.size;
            memcpy(cvpMetadata->payload, data->cvpMetadata.payload, data->cvpMetadata.size);
            cvpMetadata->capture_frame_rate = data->cvpMetadata.capture_frame_rate;
            cvpMetadata->cvp_frame_rate = data->cvpMetadata.cvp_frame_rate;
            cvpMetadata->flags = data->cvpMetadata.flags;
            memcpy(cvpMetadata->reserved, data->cvpMetadata.reserved, (8 * sizeof(uint32_t)));
          } else {
            ret = -EINVAL;
          }
          break;
        }
        case GET_VIDEO_HISTOGRAM_STATS: {
          struct VideoHistogramMetadata *vidstats = (struct VideoHistogramMetadata *)param;
          vidstats->stat_len = 0;
          if (data->video_histogram_stats.stat_len <= VIDEO_HISTOGRAM_STATS_SIZE) {
            memcpy(vidstats->stats_info, data->video_histogram_stats.stats_info,
                   VIDEO_HISTOGRAM_STATS_SIZE);
            vidstats->stat_len = data->video_histogram_stats.stat_len;
            vidstats->frame_type = data->video_histogram_stats.frame_type;
            vidstats->display_width = data->video_histogram_stats.display_width;
            vidstats->display_height = data->video_histogram_stats.display_height;
            vidstats->decode_width = data->video_histogram_stats.decode_width;
            vidstats->decode_height = data->video_histogram_stats.decode_height;
          } else {
            ret = -EINVAL;
          }
          break;
        }
        case GET_VIDEO_TS_INFO:
          *((VideoTimestampInfo *)param) = data->videoTsInfo;
          break;
        default:
            ALOGE("Unknown paramType %d", paramType);
            ret = -EINVAL;
            break;
    }
    return ret;
}

int copyMetaData(struct private_handle_t *src, struct private_handle_t *dst) {
    auto err = validateAndMap(src);
    if (err != 0)
        return err;

    err = validateAndMap(dst);
    if (err != 0)
        return err;

    MetaData_t *src_data = reinterpret_cast <MetaData_t *>(src->base_metadata);
    MetaData_t *dst_data = reinterpret_cast <MetaData_t *>(dst->base_metadata);
    *dst_data = *src_data;
    return 0;
}

int copyMetaDataVaToHandle(MetaData_t *src_data, struct private_handle_t *dst) {
    int err = -EINVAL;
    if (src_data == nullptr)
        return err;

    err = validateAndMap(dst);
    if (err != 0)
        return err;

    MetaData_t *dst_data = reinterpret_cast <MetaData_t *>(dst->base_metadata);
    *dst_data = *src_data;
    return 0;
}

int copyMetaDataHandleToVa(struct private_handle_t *src, MetaData_t *dst_data) {
    int err = -EINVAL;
    if (dst_data == nullptr)
        return err;

    err = validateAndMap(src);
    if (err != 0)
        return err;

    MetaData_t *src_data = reinterpret_cast <MetaData_t *>(src->base_metadata);
    *dst_data = *src_data;
    return 0;
}

int copyMetaDataVaToVa(MetaData_t *src_data, MetaData_t *dst_data) {
    int err = -EINVAL;
    if (src_data == nullptr)
        return err;

    if (dst_data == nullptr)
        return err;

    *dst_data = *src_data;
    return 0;
}

int setMetaDataAndUnmap(struct private_handle_t *handle, enum DispParamType paramType,
                        void *param) {
    auto ret = setMetaData(handle, paramType, param);
    unmapAndReset(handle);
    return ret;
}

int getMetaDataAndUnmap(struct private_handle_t *handle,
                        enum DispFetchParamType paramType,
                        void *param) {
    auto ret = getMetaData(handle, paramType, param);
    unmapAndReset(handle);
    return ret;
}
