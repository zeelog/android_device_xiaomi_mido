/*
 * Copyright (C) 2011-2015, 2018 The Linux Foundation. All rights reserved.
 * Copyright (c) 2011 The Android Open Source Project
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

#ifndef QCOM_OMX_METADATA_H_
#define QCOM_OMX_METADATA_H_

#include "cutils/native_handle.h"
#include <media/hardware/MetadataBufferType.h>

namespace android {

#ifdef USE_NATIVE_HANDLE_SOURCE
    typedef struct encoder_nativehandle_buffer_type {
        MetadataBufferType buffer_type;
        buffer_handle_t meta_handle;
    } encoder_nativehandle_buffer_type;
#endif

    typedef struct encoder_media_buffer_type {
        MetadataBufferType buffer_type;
        buffer_handle_t meta_handle;
    } encoder_media_buffer_type;

#ifdef ANDROID_JELLYBEAN_MR2
    // Meta data buffer layout used to transport output frames to the decoder for
    // dynamic buffer handling.
    struct VideoDecoderOutputMetaData {
        MetadataBufferType eType;
        buffer_handle_t pHandle;
    };
#endif
}

#endif
