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

#ifndef _QDMETADATA_H
#define _QDMETADATA_H

#include <QtiGrallocMetadata.h>
#include <color_metadata.h>

/* TODO: This conditional include is to prevent breaking video and camera test cases using
 * MetaData_t - camxchinodedewarp.cpp, vtest_EncoderFileSource.cpp
 */

#ifdef __cplusplus
#include <QtiGrallocPriv.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct MetaData_t;

enum ColorSpace_t {
  ITU_R_601,
  ITU_R_601_FR,
  ITU_R_709,
  ITU_R_709_FR,
  ITU_R_2020,
  ITU_R_2020_FR,
};

struct BufferDim_t {
  int32_t sliceWidth;
  int32_t sliceHeight;
};

enum DispParamType {
    SET_VT_TIMESTAMP           = 0x0001,
    COLOR_METADATA             = 0x0002,
    PP_PARAM_INTERLACED        = 0x0004,
    SET_VIDEO_PERF_MODE        = 0x0008,
    SET_GRAPHICS_METADATA      = 0x0010,
    SET_UNUSED                 = 0x0020,
    SET_UBWC_CR_STATS_INFO     = 0x0040,
    UPDATE_BUFFER_GEOMETRY     = 0x0080,
    UPDATE_REFRESH_RATE        = 0x0100,
    UPDATE_COLOR_SPACE         = 0x0200,
    MAP_SECURE_BUFFER          = 0x0400,
    S3D_FORMAT                 = 0x0800,
    LINEAR_FORMAT              = 0x1000,
    SET_IGC                    = 0x2000,
    SET_SINGLE_BUFFER_MODE     = 0x4000,
    SET_S3D_COMP               = 0x8000,
    SET_CVP_METADATA           = 0x00010000,
    SET_VIDEO_HISTOGRAM_STATS  = 0x00020000,
    SET_VIDEO_TS_INFO          = 0x00040000
};

enum DispFetchParamType {
    GET_VT_TIMESTAMP          = 0x0001,
    GET_COLOR_METADATA        = 0x0002,
    GET_PP_PARAM_INTERLACED   = 0x0004,
    GET_VIDEO_PERF_MODE       = 0x0008,
    GET_GRAPHICS_METADATA     = 0x0010,
    GET_UNUSED                = 0X0020,
    GET_UBWC_CR_STATS_INFO    = 0x0040,
    GET_BUFFER_GEOMETRY       = 0x0080,
    GET_REFRESH_RATE          = 0x0100,
    GET_COLOR_SPACE           = 0x0200,
    GET_MAP_SECURE_BUFFER     = 0x0400,
    GET_S3D_FORMAT            = 0x0800,
    GET_LINEAR_FORMAT         = 0x1000,
    GET_IGC                   = 0x2000,
    GET_SINGLE_BUFFER_MODE    = 0x4000,
    GET_S3D_COMP              = 0x8000,
    GET_CVP_METADATA          = 0x00010000,
    GET_VIDEO_HISTOGRAM_STATS = 0x00020000,
    GET_VIDEO_TS_INFO         = 0x00040000
};

/* Frame type bit mask */
#define QD_SYNC_FRAME (0x1 << 0)

struct private_handle_t;
int setMetaData(struct private_handle_t *handle, enum DispParamType paramType,
                void *param);
int setMetaDataVa(struct MetaData_t* data, enum DispParamType paramType,
                  void *param);

int getMetaData(struct private_handle_t *handle,
                enum DispFetchParamType paramType,
                void *param);
int getMetaDataVa(struct MetaData_t* data, enum DispFetchParamType paramType,
                  void *param);

int copyMetaData(struct private_handle_t *src, struct private_handle_t *dst);
int copyMetaDataVaToHandle(struct MetaData_t *src, struct private_handle_t *dst);
int copyMetaDataHandleToVa(struct private_handle_t* src, struct MetaData_t *dst);
int copyMetaDataVaToVa(struct MetaData_t *src, struct MetaData_t *dst);

int clearMetaData(struct private_handle_t *handle, enum DispParamType paramType);
int clearMetaDataVa(struct MetaData_t *data, enum DispParamType paramType);

unsigned long getMetaDataSize();

// Map, access metadata and unmap. Used by clients that do not import/free but
//  clone and delete native_handle
int setMetaDataAndUnmap(struct private_handle_t *handle, enum DispParamType paramType,
                        void *param);
int getMetaDataAndUnmap(struct private_handle_t *handle,
                        enum DispFetchParamType paramType,
                        void *param);

#ifdef __cplusplus
}
#endif

#endif /* _QDMETADATA_H */

