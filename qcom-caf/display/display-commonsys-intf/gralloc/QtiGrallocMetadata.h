/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
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

#ifndef __QTIGRALLOCMETADATA_H__
#define __QTIGRALLOCMETADATA_H__

#include <color_metadata.h>

#define QTI_VT_TIMESTAMP 10000
#define QTI_COLOR_METADATA 10001
#define QTI_PP_PARAM_INTERLACED 10002
#define QTI_VIDEO_PERF_MODE 10003
#define QTI_GRAPHICS_METADATA 10004
#define QTI_UBWC_CR_STATS_INFO 10005
#define QTI_REFRESH_RATE 10006
#define QTI_MAP_SECURE_BUFFER 10007
#define QTI_LINEAR_FORMAT 10008
#define QTI_SINGLE_BUFFER_MODE 10009
#define QTI_CVP_METADATA 10010
#define QTI_VIDEO_HISTOGRAM_STATS 10011
// File descriptor for allocated buffer
#define QTI_FD 10012
// Flags from the private handle of the allocated buffer
#define QTI_PRIVATE_FLAGS 10013
// Width of the allocated buffer in pixels
#define QTI_ALIGNED_WIDTH_IN_PIXELS 10014
// Height of the allocated buffer in pixels
#define QTI_ALIGNED_HEIGHT_IN_PIXELS 10015
// Indicates whether metadata is using default value or has been set
#define QTI_STANDARD_METADATA_STATUS 10016
#define QTI_VENDOR_METADATA_STATUS 10017
#define QTI_BUFFER_TYPE 10018
#define QTI_VIDEO_TS_INFO 10019
// This is legacy format
#define QTI_S3D_FORMAT 10020
#define QTI_CUSTOM_DIMENSIONS_STRIDE 10021
#define QTI_CUSTOM_DIMENSIONS_HEIGHT 10022
#define QTI_RGB_DATA_ADDRESS 10023
#define QTI_COLORSPACE 10024
#define QTI_YUV_PLANE_INFO 10025

// Used to indicate to framework that internal definitions are used instead
#define COMPRESSION_QTI_UBWC 20001
#define INTERLACED_QTI 20002

#define PLANE_COMPONENT_TYPE_RAW 20003
#define PLANE_COMPONENT_TYPE_META 20004

#define MAX_NAME_LEN 256

// GRAPHICS_METADATA
#define GRAPHICS_METADATA_SIZE 4096
#define GRAPHICS_METADATA_SIZE_IN_BYTES (GRAPHICS_METADATA_SIZE * sizeof(uint32_t))
typedef struct GraphicsMetadata {
  uint32_t size;  //unused in Gralloc4, in Gralloc3 it was never returned on Get()
  uint32_t data[GRAPHICS_METADATA_SIZE]; //Clients must set only raw data with Gralloc4
} GraphicsMetadata;

// UBWC_CR_STATS_INFO
#define MAX_UBWC_STATS_LENGTH 32
enum UBWC_Version {
  UBWC_UNUSED = 0,
  UBWC_1_0 = 0x1,
  UBWC_2_0 = 0x2,
  UBWC_3_0 = 0x3,
  UBWC_4_0 = 0x4,
  UBWC_MAX_VERSION = 0xFF,
};

struct UBWC_2_0_Stats {
  uint32_t nCRStatsTile32;  /**< UBWC Stats info for  32 Byte Tile */
  uint32_t nCRStatsTile64;  /**< UBWC Stats info for  64 Byte Tile */
  uint32_t nCRStatsTile96;  /**< UBWC Stats info for  96 Byte Tile */
  uint32_t nCRStatsTile128; /**< UBWC Stats info for 128 Byte Tile */
  uint32_t nCRStatsTile160; /**< UBWC Stats info for 160 Byte Tile */
  uint32_t nCRStatsTile192; /**< UBWC Stats info for 192 Byte Tile */
  uint32_t nCRStatsTile256; /**< UBWC Stats info for 256 Byte Tile */
};

struct UBWCStats {
  enum UBWC_Version version; /* Union depends on this version. */
  uint8_t bDataValid;        /* If [non-zero], CR Stats data is valid.
                              * Consumers may use stats data.
                              * If [zero], CR Stats data is invalid.
                              * Consumers *Shall* not use stats data */
  union {
    struct UBWC_2_0_Stats ubwc_stats;
    uint32_t reserved[MAX_UBWC_STATS_LENGTH]; /* This is for future */
  };
};

#define UBWC_STATS_ARRAY_SIZE 2
struct CropRectangle_t {
  int32_t left;
  int32_t top;
  int32_t right;
  int32_t bottom;
};

// CVP_METADATA
#define CVP_METADATA_SIZE 1024
enum CVPMetadataFlags {
  /* bit wise flags */
  CVP_METADATA_FLAG_NONE = 0x00000000,
  CVP_METADATA_FLAG_REPEAT = 0x00000001,
};

typedef struct CVPMetadata {
  uint32_t size; /* payload size in bytes */
  uint8_t payload[CVP_METADATA_SIZE];
  uint32_t capture_frame_rate;
  /* Frame rate in Q16 format.
          Eg: fps = 7.5, then
          capture_frame_rate = 7 << 16 --> Upper 16 bits to represent 7
          capture_frame_rate |= 5 -------> Lower 16 bits to represent 5

     If size > 0, framerate is valid
     If size = 0, invalid data, so ignore all parameters */
  uint32_t cvp_frame_rate;
  enum CVPMetadataFlags flags;
  uint32_t reserved[8];
} CVPMetadata;

// VIDEO_HISTOGRAM_STATS

#define VIDEO_HISTOGRAM_STATS_SIZE (4 * 1024)
struct VideoHistogramMetadata {
  uint32_t stats_info[1024]; /* video stats payload */
  uint32_t stat_len;         /* Payload size in bytes */
  uint32_t frame_type;       /* bit mask to indicate frame type */
  uint32_t display_width;
  uint32_t display_height;
  uint32_t decode_width;
  uint32_t decode_height;
  uint32_t reserved[12];
};

#define VIDEO_TIMESTAMP_INFO_SIZE 16
struct VideoTimestampInfo {
  uint32_t enable;               /* Enable video timestamp info */
  uint32_t frame_number;         /* Frame number/counter */
  int64_t frame_timestamp_us;    /* Frame timestamp in us */
};

#define RESERVED_REGION_SIZE 4096
typedef struct ReservedRegion {
  uint32_t size;
  uint8_t data[RESERVED_REGION_SIZE];
} ReservedRegion;

#define YCBCR_LAYOUT_ARRAY_SIZE 2
struct qti_ycbcr {
  void *y;
  void *cb;
  void *cr;
  uint32_t yStride;
  uint32_t cStride;
  uint32_t chromaStep;
};

/* Color Space Macros */
#define HAL_CSC_ITU_R_601 0
#define HAL_CSC_ITU_R_601_FR 1
#define HAL_CSC_ITU_R_709 2
#define HAL_CSC_ITU_R_709_FR 3
#define HAL_CSC_ITU_R_2020 4
#define HAL_CSC_ITU_R_2020_FR 5

#define METADATA_SET_SIZE 512

#define IS_VENDOR_METADATA_TYPE(x) (x >= QTI_VT_TIMESTAMP)

#define GET_STANDARD_METADATA_STATUS_INDEX(x) x
#define GET_VENDOR_METADATA_STATUS_INDEX(x) x - QTI_VT_TIMESTAMP

#endif  //__QTIGRALLOCMETADATA_H__
