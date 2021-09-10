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

#ifndef __QTIGRALLOCPRIV_H__
#define __QTIGRALLOCPRIV_H__

#include <color_metadata.h>
#include <cutils/native_handle.h>
#include <log/log.h>

#include <cinttypes>
#include <string>

#include "QtiGrallocMetadata.h"

/*
 *
 * DISCLAIMER:
 * INTERNAL GRALLOC USE ONLY - THIS FILE SHOULD NOT BE INCLUDED BY GRALLOC CLIENTS
 * The location will be changed and this file will not be exposed
 * once qdMetaData copy functions are deprecated
 *
 */

#define METADATA_V2

// TODO: MetaData_t should be in qtigralloc namespace
struct MetaData_t {
  int32_t operation;
  int32_t interlaced;
  float refreshrate;
  int32_t mapSecureBuffer;
  /* Deprecated */
  uint32_t s3dFormat;
  /* VENUS output buffer is linear for UBWC Interlaced video */
  uint32_t linearFormat;
  /* Set by graphics to indicate that this buffer will be written to but not
   * swapped out */
  uint32_t isSingleBufferMode;

  /* Set by camera to program the VT Timestamp */
  uint64_t vtTimeStamp;
  /* Color Aspects + HDR info */
  ColorMetaData color;
  /* Consumer should read this data as follows based on
   * Gralloc flag "interlaced" listed above.
   * [0] : If it is progressive.
   * [0] : Top field, if it is interlaced.
   * [1] : Do not read, if it is progressive.
   * [1] : Bottom field, if it is interlaced.
   */
  struct UBWCStats ubwcCRStats[UBWC_STATS_ARRAY_SIZE];
  /* Set by camera to indicate that this buffer will be used for a High
   * Performance Video Usecase */
  uint32_t isVideoPerfMode;
  /* Populated and used by adreno during buffer size calculation.
   * Set only for RGB formats. */
  GraphicsMetadata graphics_metadata;
  /* Video hisogram stats populated by video decoder */
  struct VideoHistogramMetadata video_histogram_stats;
  /*
   * Producer (camera) will set cvp metadata and consumer (video) will
   * use it. The format of metadata is known to producer and consumer.
   */
  CVPMetadata cvpMetadata;
  CropRectangle_t crop;
  int32_t blendMode;
  char name[MAX_NAME_LEN];
  ReservedRegion reservedRegion;
  bool isStandardMetadataSet[METADATA_SET_SIZE];
  bool isVendorMetadataSet[METADATA_SET_SIZE];
  uint64_t reservedSize;
  VideoTimestampInfo videoTsInfo;
};

namespace qtigralloc {
#define QTI_HANDLE_CONST(exp) static_cast<const private_handle_t *>(exp)

#pragma pack(push, 4)
struct private_handle_t : public native_handle_t {
  // file-descriptors dup'd over IPC
  int fd;
  int fd_metadata;

  // values sent over IPC
  int magic;
  int flags;
  int width;             // holds width of the actual buffer allocated
  int height;            // holds height of the  actual buffer allocated
  int unaligned_width;   // holds width client asked to allocate
  int unaligned_height;  // holds height client asked to allocate
  int format;
  int buffer_type;
  unsigned int layer_count;
  uint64_t id;
  uint64_t usage;

  unsigned int size;
  unsigned int offset;
  unsigned int offset_metadata;
  uint64_t base;
  uint64_t base_metadata;
  uint64_t gpuaddr;
  static const int kNumFds = 2;
  static const int kMagic = 'gmsm';

  static inline int NumInts() {
    return ((sizeof(private_handle_t) - sizeof(native_handle_t)) / sizeof(int)) - kNumFds;
  }

  private_handle_t(int fd, int meta_fd, int flags, int width, int height, int uw, int uh,
                   int format, int buf_type, unsigned int size, uint64_t usage = 0)
      : fd(fd),
        fd_metadata(meta_fd),
        magic(kMagic),
        flags(flags),
        width(width),
        height(height),
        unaligned_width(uw),
        unaligned_height(uh),
        format(format),
        buffer_type(buf_type),
        layer_count(1),
        id(0),
        usage(usage),
        size(size),
        offset(0),
        offset_metadata(0),
        base(0),
        base_metadata(0),
        gpuaddr(0) {
    version = static_cast<int>(sizeof(native_handle));
    numInts = NumInts();
    numFds = kNumFds;
  }

  ~private_handle_t() { magic = 0; }

  static int validate(const native_handle *h) {
    auto *hnd = static_cast<const private_handle_t *>(h);
    if (!h || h->version != sizeof(native_handle) || h->numInts != NumInts() ||
        h->numFds != kNumFds) {
      ALOGE("Invalid gralloc handle (at %p): ver(%d/%zu) ints(%d/%d) fds(%d/%d)", h,
            h ? h->version : -1, sizeof(native_handle), h ? h->numInts : -1, NumInts(),
            h ? h->numFds : -1, kNumFds);
      return -1;
    }
    if (hnd->magic != kMagic) {
      ALOGE("handle = %p  invalid  magic(%c%c%c%c/%c%c%c%c)", hnd,
            hnd ? (((hnd->magic >> 24) & 0xFF) ? ((hnd->magic >> 24) & 0xFF) : '-') : '?',
            hnd ? (((hnd->magic >> 16) & 0xFF) ? ((hnd->magic >> 16) & 0xFF) : '-') : '?',
            hnd ? (((hnd->magic >> 8) & 0xFF) ? ((hnd->magic >> 8) & 0xFF) : '-') : '?',
            hnd ? (((hnd->magic >> 0) & 0xFF) ? ((hnd->magic >> 0) & 0xFF) : '-') : '?',
            (kMagic >> 24) & 0xFF, (kMagic >> 16) & 0xFF, (kMagic >> 8) & 0xFF,
            (kMagic >> 0) & 0xFF);
      return -1;
    }

    return 0;
  }

  static void Dump(const private_handle_t *hnd) {
    ALOGD("handle id:%" PRIu64
          " wxh:%dx%d uwxuh:%dx%d size: %d fd:%d fd_meta:%d flags:0x%x "
          "usage:0x%" PRIx64 "  format:0x%x layer_count: %d",
          hnd->id, hnd->width, hnd->height, hnd->unaligned_width, hnd->unaligned_height, hnd->size,
          hnd->fd, hnd->fd_metadata, hnd->flags, hnd->usage, hnd->format, hnd->layer_count);
  }
};
#pragma pack(pop)

}  // namespace qtigralloc

#endif  //__QTIGRALLOCPRIV_H__
