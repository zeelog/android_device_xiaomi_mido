/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

//#define LOG_NDEBUG 0
#define LOG_TAG "audio_hw_hidl"

#include "audio_hidl.h"
#include <cutils/properties.h>
#include <log/log.h>
#include <hidl/HidlTransportSupport.h>
#include <hidl/LegacySupport.h>

#ifdef AHAL_EXT_ENABLED
#include <vendor/qti/hardware/audiohalext/1.0/IAudioHalExt.h>
using vendor::qti::hardware::audiohalext::V1_0::IAudioHalExt;
#endif

using namespace android::hardware;
using android::OK;

extern "C" {
int audio_extn_hidl_init() {

#ifdef AHAL_EXT_ENABLED
    if (!property_get_bool("vendor.audio.hal.ext.disabled", false)) {
        /* register audio HAL extension */
        bool fail = registerPassthroughServiceImplementation<IAudioHalExt>()!= OK;
        ALOGW_IF(fail, "Could not register AHAL extension");
    }
#endif

    /* to register other hidls */

    return 0;
}
}

