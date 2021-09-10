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

#ifndef AUDIO_FEATURE_MANAGER_H
#define AUDIO_FEATURE_MANAGER_H

#include <ahal_config_helper.h>

enum audio_ext_feature_t {
    // Start Audio feature flags
    SND_MONITOR = 0,
    COMPRESS_CAPTURE,
    SOURCE_TRACK,
    SSREC,
    AUDIOSPHERE,
    AFE_PROXY,
    USE_DEEP_BUFFER_AS_PRIMARY_OUTPUT,
    HDMI_EDID,
    KEEP_ALIVE,
    HIFI_AUDIO,
    RECEIVER_AIDED_STEREO,
    KPI_OPTIMIZE,
    DISPLAY_PORT,
    FLUENCE,
    CUSTOM_STEREO,
    ANC_HEADSET,
    DSM_FEEDBACK,
    USB_OFFLOAD,
    USB_OFFLOAD_BURST_MODE,
    USB_OFFLOAD_SIDETONE_VOLM,
    A2DP_OFFLOAD,
    HFP,
    VBAT,
    SPKR_PROT,
    FM_POWER_OPT_FEATURE,
    EXTERNAL_QDSP,
    EXTERNAL_SPEAKER,
    EXTERNAL_SPEAKER_TFA,
    HWDEP_CAL,
    WSA,
    EXT_HW_PLUGIN,
    RECORD_PLAY_CONCURRENCY,
    HDMI_PASSTHROUGH,
    CONCURRENT_CAPTURE,
    COMPRESS_IN_CAPTURE,
    BATTERY_LISTENER,
    COMPRESS_METADATA_NEEDED,
    MAXX_AUDIO,
    AUDIO_ZOOM,
    AUTO_HAL,
    // End Audio feature flags
    // Start Voice feature flags
    COMPRESS_VOIP,
    VOICE_START = COMPRESS_VOIP,
    DYNAMIC_ECNS,
    INCALL_MUSIC,
    // End Voice feature flags
    MAX_SUPPORTED_FEATURE
};

typedef enum audio_ext_feature_t audio_ext_feature;

void audio_feature_manager_init();
bool audio_feature_manager_is_feature_enabled(audio_ext_feature feature);

#endif /* AUDIO_FEATURE_MANAGER_H */
