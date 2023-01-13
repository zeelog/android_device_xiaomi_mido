/*
 * Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2013 The Android Open Source Project
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
 *
 * This file was modified by DTS, Inc. The portions of the
 * code modified by DTS, Inc are copyrighted and
 * licensed separately, as follows:
 *
 * (C) 2014 DTS, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_primary"
#define ATRACE_TAG (ATRACE_TAG_AUDIO|ATRACE_TAG_HAL)
/*#define LOG_NDEBUG 0*/
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif
#include <limits.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>
#include <math.h>
#include <dlfcn.h>
#include <sys/resource.h>
#include <sys/prctl.h>

#include <log/log.h>
#include <cutils/trace.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>
#include <cutils/atomic.h>
#include <cutils/sched_policy.h>
#include <hardware/audio_effect.h>
#include <hardware/audio_alsaops.h>
#include <system/thread_defs.h>
#include <tinyalsa/asoundlib.h>
#include <utils/Timers.h> // systemTime
#include <audio_effects/effect_aec.h>
#include <audio_effects/effect_ns.h>
#include <audio_utils/format.h>
#include "audio_hw.h"
#include "audio_perf.h"
#include "platform_api.h"
#include <platform.h>
#include "audio_extn.h"
#include "voice_extn.h"
#include "ip_hdlr_intf.h"

#include "sound/compress_params.h"

#ifdef AUDIO_GKI_ENABLED
#include "sound/audio_compressed_formats.h"
#endif

#include "sound/asound.h"

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_AUDIO_HW
#include <log_utils.h>
#endif

#define COMPRESS_OFFLOAD_NUM_FRAGMENTS 4
/*DIRECT PCM has same buffer sizes as DEEP Buffer*/
#define DIRECT_PCM_NUM_FRAGMENTS 2
#define COMPRESS_PLAYBACK_VOLUME_MAX 0x2000
#define VOIP_PLAYBACK_VOLUME_MAX 0x2000
#define MMAP_PLAYBACK_VOLUME_MAX 0x2000
#define PCM_PLAYBACK_VOLUME_MAX 0x2000
#define DSD_VOLUME_MIN_DB (-110)
#define INVALID_OUT_VOLUME -1
#define AUDIO_IO_PORTS_MAX 32

#define PLAYBACK_GAIN_MAX 1.0f
#define RECORD_GAIN_MIN 0.0f
#define RECORD_GAIN_MAX 1.0f
#define RECORD_VOLUME_CTL_MAX 0x2000

/* treat as unsigned Q1.13 */
#define APP_TYPE_GAIN_DEFAULT         0x2000

#define PROXY_OPEN_RETRY_COUNT           100
#define PROXY_OPEN_WAIT_TIME             20

#define GET_USECASE_AUDIO_PLAYBACK_PRIMARY(db) \
         (db)? USECASE_AUDIO_PLAYBACK_DEEP_BUFFER : \
               USECASE_AUDIO_PLAYBACK_LOW_LATENCY
#define GET_PCM_CONFIG_AUDIO_PLAYBACK_PRIMARY(db) \
         (db)? pcm_config_deep_buffer : pcm_config_low_latency

#define ULL_PERIOD_SIZE (DEFAULT_OUTPUT_SAMPLING_RATE/1000)
#define DEFAULT_VOIP_BUF_DURATION_MS 20
#define DEFAULT_VOIP_BIT_DEPTH_BYTE sizeof(int16_t)
#define DEFAULT_VOIP_SAMP_RATE 48000

#define VOIP_IO_BUF_SIZE(SR, DURATION_MS, BIT_DEPTH) (SR)/1000 * DURATION_MS * BIT_DEPTH

struct pcm_config default_pcm_config_voip_copp = {
    .channels = 1,
    .rate = DEFAULT_VOIP_SAMP_RATE, /* changed when the stream is opened */
    .period_size = VOIP_IO_BUF_SIZE(DEFAULT_VOIP_SAMP_RATE, DEFAULT_VOIP_BUF_DURATION_MS, DEFAULT_VOIP_BIT_DEPTH_BYTE)/2,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
    .avail_min = VOIP_IO_BUF_SIZE(DEFAULT_VOIP_SAMP_RATE, DEFAULT_VOIP_BUF_DURATION_MS, DEFAULT_VOIP_BIT_DEPTH_BYTE)/2,
    .stop_threshold = INT_MAX,
};

#define MIN_CHANNEL_COUNT                1
#define DEFAULT_CHANNEL_COUNT            2
#define MAX_HIFI_CHANNEL_COUNT           8

#ifndef MAX_TARGET_SPECIFIC_CHANNEL_CNT
#define MAX_CHANNEL_COUNT 1
#else
#define MAX_CHANNEL_COUNT atoi(XSTR(MAX_TARGET_SPECIFIC_CHANNEL_CNT))
#define XSTR(x) STR(x)
#define STR(x) #x
#endif

#ifdef LINUX_ENABLED
static inline int64_t audio_utils_ns_from_timespec(const struct timespec *ts)
{
	return ts->tv_sec * 1000000000LL + ts->tv_nsec;
}
#endif

static unsigned int configured_low_latency_capture_period_size =
        LOW_LATENCY_CAPTURE_PERIOD_SIZE;

#define MMAP_PERIOD_SIZE (DEFAULT_OUTPUT_SAMPLING_RATE/1000)
#define MMAP_PERIOD_COUNT_MIN 32
#define MMAP_PERIOD_COUNT_MAX 512
#define MMAP_PERIOD_COUNT_DEFAULT (MMAP_PERIOD_COUNT_MAX)

/* This constant enables extended precision handling.
 * TODO The flag is off until more testing is done.
 */
static const bool k_enable_extended_precision = false;
extern int AUDIO_DEVICE_IN_ALL_CODEC_BACKEND;

struct pcm_config pcm_config_deep_buffer = {
    .channels = 2,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = DEEP_BUFFER_OUTPUT_PERIOD_SIZE,
    .period_count = DEEP_BUFFER_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
};

struct pcm_config pcm_config_low_latency = {
    .channels = 2,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = LOW_LATENCY_OUTPUT_PERIOD_SIZE,
    .period_count = LOW_LATENCY_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
};

struct pcm_config pcm_config_haptics_audio = {
    .channels = 1,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = LOW_LATENCY_OUTPUT_PERIOD_SIZE,
    .period_count = LOW_LATENCY_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
};

struct pcm_config pcm_config_haptics = {
    .channels = 1,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = LOW_LATENCY_OUTPUT_PERIOD_SIZE,
    .period_count = LOW_LATENCY_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = LOW_LATENCY_OUTPUT_PERIOD_SIZE,
    .stop_threshold = INT_MAX,
    .avail_min = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
};

static int af_period_multiplier = 4;
struct pcm_config pcm_config_rt = {
    .channels = 2,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = ULL_PERIOD_SIZE, //1 ms
    .period_count = 512, //=> buffer size is 512ms
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = ULL_PERIOD_SIZE*8, //8ms
    .stop_threshold = INT_MAX,
    .silence_threshold = 0,
    .silence_size = 0,
    .avail_min = ULL_PERIOD_SIZE, //1 ms
};

struct pcm_config pcm_config_hdmi_multi = {
    .channels = HDMI_MULTI_DEFAULT_CHANNEL_COUNT, /* changed when the stream is opened */
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE, /* changed when the stream is opened */
    .period_size = HDMI_MULTI_PERIOD_SIZE,
    .period_count = HDMI_MULTI_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

struct pcm_config pcm_config_mmap_playback = {
    .channels = 2,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = MMAP_PERIOD_SIZE,
    .period_count = MMAP_PERIOD_COUNT_DEFAULT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = MMAP_PERIOD_SIZE*8,
    .stop_threshold = INT32_MAX,
    .silence_threshold = 0,
    .silence_size = 0,
    .avail_min = MMAP_PERIOD_SIZE, //1 ms
};

struct pcm_config pcm_config_hifi = {
    .channels = DEFAULT_CHANNEL_COUNT, /* changed when the stream is opened */
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE, /* changed when the stream is opened */
    .period_size = HIFI_BUFFER_OUTPUT_PERIOD_SIZE, /* change #define */
    .period_count = HIFI_BUFFER_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S24_3LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

struct pcm_config pcm_config_audio_capture = {
    .channels = 2,
    .period_count = AUDIO_CAPTURE_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

struct pcm_config pcm_config_mmap_capture = {
    .channels = 2,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = MMAP_PERIOD_SIZE,
    .period_count = MMAP_PERIOD_COUNT_DEFAULT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .silence_threshold = 0,
    .silence_size = 0,
    .avail_min = MMAP_PERIOD_SIZE, //1 ms
};

#define AFE_PROXY_CHANNEL_COUNT 2
#define AFE_PROXY_SAMPLING_RATE 48000

#define AFE_PROXY_PLAYBACK_PERIOD_SIZE  768
#define AFE_PROXY_PLAYBACK_PERIOD_COUNT 4

struct pcm_config pcm_config_afe_proxy_playback = {
    .channels = AFE_PROXY_CHANNEL_COUNT,
    .rate = AFE_PROXY_SAMPLING_RATE,
    .period_size = AFE_PROXY_PLAYBACK_PERIOD_SIZE,
    .period_count = AFE_PROXY_PLAYBACK_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = AFE_PROXY_PLAYBACK_PERIOD_SIZE,
    .stop_threshold = INT_MAX,
    .avail_min = AFE_PROXY_PLAYBACK_PERIOD_SIZE,
};

#define AFE_PROXY_RECORD_PERIOD_SIZE  768
#define AFE_PROXY_RECORD_PERIOD_COUNT 4

struct pcm_config pcm_config_audio_capture_rt = {
    .channels = 2,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = ULL_PERIOD_SIZE,
    .period_count = 512,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = AFE_PROXY_RECORD_PERIOD_SIZE * AFE_PROXY_RECORD_PERIOD_COUNT,
    .silence_threshold = 0,
    .silence_size = 0,
    .avail_min = ULL_PERIOD_SIZE, //1 ms
};

struct pcm_config pcm_config_afe_proxy_record = {
    .channels = AFE_PROXY_CHANNEL_COUNT,
    .rate = AFE_PROXY_SAMPLING_RATE,
    .period_size = AFE_PROXY_RECORD_PERIOD_SIZE,
    .period_count = AFE_PROXY_RECORD_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = AFE_PROXY_RECORD_PERIOD_SIZE,
    .stop_threshold = INT_MAX,
    .avail_min = AFE_PROXY_RECORD_PERIOD_SIZE,
};

#define AUDIO_MAX_PCM_FORMATS 7

const uint32_t format_to_bitwidth_table[AUDIO_MAX_PCM_FORMATS] = {
    [AUDIO_FORMAT_DEFAULT] = 0,
    [AUDIO_FORMAT_PCM_16_BIT] = sizeof(uint16_t),
    [AUDIO_FORMAT_PCM_8_BIT] = sizeof(uint8_t),
    [AUDIO_FORMAT_PCM_32_BIT] = sizeof(uint32_t),
    [AUDIO_FORMAT_PCM_8_24_BIT] = sizeof(uint32_t),
    [AUDIO_FORMAT_PCM_FLOAT] = sizeof(float),
    [AUDIO_FORMAT_PCM_24_BIT_PACKED] = sizeof(uint8_t) * 3,
};

const char * const use_case_table[AUDIO_USECASE_MAX] = {
    [USECASE_AUDIO_PLAYBACK_DEEP_BUFFER] = "deep-buffer-playback",
    [USECASE_AUDIO_PLAYBACK_LOW_LATENCY] = "low-latency-playback",
    [USECASE_AUDIO_PLAYBACK_WITH_HAPTICS] = "audio-with-haptics-playback",
    [USECASE_AUDIO_PLAYBACK_HAPTICS] = "haptics-playback",
    [USECASE_AUDIO_PLAYBACK_ULL]         = "audio-ull-playback",
    [USECASE_AUDIO_PLAYBACK_MULTI_CH]    = "multi-channel-playback",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD] = "compress-offload-playback",
    //Enabled for Direct_PCM
    [USECASE_AUDIO_PLAYBACK_OFFLOAD2] = "compress-offload-playback2",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD3] = "compress-offload-playback3",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD4] = "compress-offload-playback4",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD5] = "compress-offload-playback5",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD6] = "compress-offload-playback6",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD7] = "compress-offload-playback7",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD8] = "compress-offload-playback8",
    [USECASE_AUDIO_PLAYBACK_OFFLOAD9] = "compress-offload-playback9",
    [USECASE_AUDIO_PLAYBACK_FM] = "play-fm",
    [USECASE_AUDIO_PLAYBACK_MMAP] = "mmap-playback",
    [USECASE_AUDIO_PLAYBACK_HIFI] = "hifi-playback",
    [USECASE_AUDIO_PLAYBACK_TTS] = "audio-tts-playback",

    [USECASE_AUDIO_RECORD] = "audio-record",
    [USECASE_AUDIO_RECORD_COMPRESS] = "audio-record-compress",
    [USECASE_AUDIO_RECORD_COMPRESS2] = "audio-record-compress2",
    [USECASE_AUDIO_RECORD_COMPRESS3] = "audio-record-compress3",
    [USECASE_AUDIO_RECORD_COMPRESS4] = "audio-record-compress4",
    [USECASE_AUDIO_RECORD_COMPRESS5] = "audio-record-compress5",
    [USECASE_AUDIO_RECORD_COMPRESS6] = "audio-record-compress6",
    [USECASE_AUDIO_RECORD_LOW_LATENCY] = "low-latency-record",
    [USECASE_AUDIO_RECORD_FM_VIRTUAL] = "fm-virtual-record",
    [USECASE_AUDIO_RECORD_MMAP] = "mmap-record",
    [USECASE_AUDIO_RECORD_HIFI] = "hifi-record",

    [USECASE_AUDIO_HFP_SCO] = "hfp-sco",
    [USECASE_AUDIO_HFP_SCO_WB] = "hfp-sco-wb",
    [USECASE_AUDIO_HFP_SCO_DOWNLINK] = "hfp-sco-downlink",
    [USECASE_AUDIO_HFP_SCO_WB_DOWNLINK] = "hfp-sco-wb-downlink",

    [USECASE_VOICE_CALL] = "voice-call",
    [USECASE_VOICE2_CALL] = "voice2-call",
    [USECASE_VOLTE_CALL] = "volte-call",
    [USECASE_QCHAT_CALL] = "qchat-call",
    [USECASE_VOWLAN_CALL] = "vowlan-call",
    [USECASE_VOICEMMODE1_CALL] = "voicemmode1-call",
    [USECASE_VOICEMMODE2_CALL] = "voicemmode2-call",
    [USECASE_COMPRESS_VOIP_CALL] = "compress-voip-call",
    [USECASE_INCALL_REC_UPLINK] = "incall-rec-uplink",
    [USECASE_INCALL_REC_DOWNLINK] = "incall-rec-downlink",
    [USECASE_INCALL_REC_UPLINK_AND_DOWNLINK] = "incall-rec-uplink-and-downlink",
    [USECASE_INCALL_REC_UPLINK_COMPRESS] = "incall-rec-uplink-compress",
    [USECASE_INCALL_REC_DOWNLINK_COMPRESS] = "incall-rec-downlink-compress",
    [USECASE_INCALL_REC_UPLINK_AND_DOWNLINK_COMPRESS] = "incall-rec-uplink-and-downlink-compress",

    [USECASE_INCALL_MUSIC_UPLINK] = "incall_music_uplink",
    [USECASE_INCALL_MUSIC_UPLINK2] = "incall_music_uplink2",
    [USECASE_AUDIO_SPKR_CALIB_RX] = "spkr-rx-calib",
    [USECASE_AUDIO_SPKR_CALIB_TX] = "spkr-vi-record",

    [USECASE_AUDIO_PLAYBACK_AFE_PROXY] = "afe-proxy-playback",
    [USECASE_AUDIO_RECORD_AFE_PROXY] = "afe-proxy-record",
    [USECASE_AUDIO_RECORD_AFE_PROXY2] = "afe-proxy-record2",
    [USECASE_AUDIO_PLAYBACK_SILENCE] = "silence-playback",

    /* Transcode loopback cases */
    [USECASE_AUDIO_TRANSCODE_LOOPBACK_RX] = "audio-transcode-loopback-rx",
    [USECASE_AUDIO_TRANSCODE_LOOPBACK_TX] = "audio-transcode-loopback-tx",

    [USECASE_AUDIO_PLAYBACK_VOIP] = "audio-playback-voip",
    [USECASE_AUDIO_RECORD_VOIP] = "audio-record-voip",
    [USECASE_AUDIO_RECORD_VOIP_LOW_LATENCY] = "audio-record-voip-low-latency",
    /* For Interactive Audio Streams */
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM1] = "audio-interactive-stream1",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM2] = "audio-interactive-stream2",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM3] = "audio-interactive-stream3",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM4] = "audio-interactive-stream4",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM5] = "audio-interactive-stream5",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM6] = "audio-interactive-stream6",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM7] = "audio-interactive-stream7",
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM8] = "audio-interactive-stream8",

    [USECASE_AUDIO_EC_REF_LOOPBACK] = "ec-ref-audio-capture",

    [USECASE_AUDIO_A2DP_ABR_FEEDBACK] = "a2dp-abr-feedback",

    [USECASE_AUDIO_PLAYBACK_MEDIA] = "media-playback",
    [USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION] = "sys-notification-playback",
    [USECASE_AUDIO_PLAYBACK_NAV_GUIDANCE] = "nav-guidance-playback",
    [USECASE_AUDIO_PLAYBACK_PHONE] = "phone-playback",
    [USECASE_AUDIO_PLAYBACK_FRONT_PASSENGER] = "front-passenger-playback",
    [USECASE_AUDIO_PLAYBACK_REAR_SEAT] = "rear-seat-playback",
    [USECASE_AUDIO_FM_TUNER_EXT] = "fm-tuner-ext",
    [USECASE_ICC_CALL] = "icc-call",

    [USECASE_AUDIO_RECORD_BUS] = "audio-record",
    [USECASE_AUDIO_RECORD_BUS_FRONT_PASSENGER] = "front-passenger-record",
    [USECASE_AUDIO_RECORD_BUS_REAR_SEAT] = "rear-seat-record",
    [USECASE_AUDIO_PLAYBACK_SYNTHESIZER] = "synth-loopback",
    [USECASE_AUDIO_RECORD_ECHO_REF_EXT] = "echo-reference-external",
};

static const audio_usecase_t offload_usecases[] = {
    USECASE_AUDIO_PLAYBACK_OFFLOAD,
    USECASE_AUDIO_PLAYBACK_OFFLOAD2,
    USECASE_AUDIO_PLAYBACK_OFFLOAD3,
    USECASE_AUDIO_PLAYBACK_OFFLOAD4,
    USECASE_AUDIO_PLAYBACK_OFFLOAD5,
    USECASE_AUDIO_PLAYBACK_OFFLOAD6,
    USECASE_AUDIO_PLAYBACK_OFFLOAD7,
    USECASE_AUDIO_PLAYBACK_OFFLOAD8,
    USECASE_AUDIO_PLAYBACK_OFFLOAD9,
};

static const audio_usecase_t interactive_usecases[] = {
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM1,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM2,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM3,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM4,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM5,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM6,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM7,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM8,
};

#define STRING_TO_ENUM(string) { #string, string }

struct string_to_enum {
    const char *name;
    uint32_t value;
};

static const struct string_to_enum channels_name_to_enum_table[] = {
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_STEREO),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_2POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_QUAD),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_SURROUND),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_PENTA),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_5POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_6POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_7POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_IN_MONO),
    STRING_TO_ENUM(AUDIO_CHANNEL_IN_STEREO),
    STRING_TO_ENUM(AUDIO_CHANNEL_IN_FRONT_BACK),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_1),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_2),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_3),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_4),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_5),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_6),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_7),
    STRING_TO_ENUM(AUDIO_CHANNEL_INDEX_MASK_8),
};

static const struct string_to_enum formats_name_to_enum_table[] = {
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_16_BIT),
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_24_BIT_PACKED),
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_32_BIT),
    STRING_TO_ENUM(AUDIO_FORMAT_AC3),
    STRING_TO_ENUM(AUDIO_FORMAT_E_AC3),
    STRING_TO_ENUM(AUDIO_FORMAT_E_AC3_JOC),
    STRING_TO_ENUM(AUDIO_FORMAT_DOLBY_TRUEHD),
    STRING_TO_ENUM(AUDIO_FORMAT_DTS),
    STRING_TO_ENUM(AUDIO_FORMAT_DTS_HD),
    STRING_TO_ENUM(AUDIO_FORMAT_IEC61937)
};

//list of all supported sample rates by HDMI specification.
static const int out_hdmi_sample_rates[] = {
    32000, 44100, 48000, 88200, 96000, 176400, 192000,
};

static const struct string_to_enum out_sample_rates_name_to_enum_table[] = {
    STRING_TO_ENUM(32000),
    STRING_TO_ENUM(44100),
    STRING_TO_ENUM(48000),
    STRING_TO_ENUM(88200),
    STRING_TO_ENUM(96000),
    STRING_TO_ENUM(176400),
    STRING_TO_ENUM(192000),
    STRING_TO_ENUM(352800),
    STRING_TO_ENUM(384000),
};

struct in_effect_list {
    struct listnode list;
    effect_handle_t handle;
};

static struct audio_device *adev = NULL;
static pthread_mutex_t adev_init_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int audio_device_ref_count;
//cache last MBDRC cal step level
static int last_known_cal_step = -1 ;

static int out_set_compr_volume(struct audio_stream_out *stream, float left, float right);
static int out_set_mmap_volume(struct audio_stream_out *stream, float left, float right);
static int out_set_voip_volume(struct audio_stream_out *stream, float left, float right);
static int out_set_pcm_volume(struct audio_stream_out *stream, float left, float right);

static void adev_snd_mon_cb(void *cookie, struct str_parms *parms);
static void in_snd_mon_cb(void * stream, struct str_parms * parms);
static void out_snd_mon_cb(void * stream, struct str_parms * parms);

static int configure_btsco_sample_rate(snd_device_t snd_device);

#ifdef AUDIO_FEATURE_ENABLED_GCOV
extern void  __gcov_flush();
static void enable_gcov()
{
    __gcov_flush();
}
#else
static void enable_gcov()
{
}
#endif

static int in_set_microphone_direction(const struct audio_stream_in *stream,
                                           audio_microphone_direction_t dir);
static int in_set_microphone_field_dimension(const struct audio_stream_in *stream, float zoom);

static bool may_use_noirq_mode(struct audio_device *adev, audio_usecase_t uc_id,
                               int flags __unused)
{
    int dir = 0;
    switch (uc_id) {
        case USECASE_AUDIO_RECORD_LOW_LATENCY:
        case USECASE_AUDIO_RECORD_VOIP_LOW_LATENCY:
            dir = 1;
        case USECASE_AUDIO_PLAYBACK_ULL:
            break;
        default:
            return false;
    }

    int dev_id = platform_get_pcm_device_id(uc_id, dir == 0 ?
                                            PCM_PLAYBACK : PCM_CAPTURE);
    if (adev->adm_is_noirq_avail)
        return adev->adm_is_noirq_avail(adev->adm_data,
                                        adev->snd_card, dev_id, dir);
    return false;
}

static void register_out_stream(struct stream_out *out)
{
    struct audio_device *adev = out->dev;
    if (is_offload_usecase(out->usecase) ||
        !adev->adm_register_output_stream)
        return;

    // register stream first for backward compatibility
    adev->adm_register_output_stream(adev->adm_data,
                                     out->handle,
                                     out->flags);

    if (!adev->adm_set_config)
        return;

    if (out->realtime)
        adev->adm_set_config(adev->adm_data,
                             out->handle,
                             out->pcm, &out->config);
}

static void register_in_stream(struct stream_in *in)
{
    struct audio_device *adev = in->dev;
    if (!adev->adm_register_input_stream)
        return;

    adev->adm_register_input_stream(adev->adm_data,
                                    in->capture_handle,
                                    in->flags);

    if (!adev->adm_set_config)
        return;

    if (in->realtime)
        adev->adm_set_config(adev->adm_data,
                             in->capture_handle,
                             in->pcm,
                             &in->config);
}

static void request_out_focus(struct stream_out *out, long ns)
{
    struct audio_device *adev = out->dev;

    if (adev->adm_request_focus_v2)
        adev->adm_request_focus_v2(adev->adm_data, out->handle, ns);
    else if (adev->adm_request_focus)
        adev->adm_request_focus(adev->adm_data, out->handle);
}

static int request_in_focus(struct stream_in *in, long ns)
{
    struct audio_device *adev = in->dev;
    int ret = 0;

    if (adev->adm_request_focus_v2_1)
        ret = adev->adm_request_focus_v2_1(adev->adm_data, in->capture_handle, ns);
    else if (adev->adm_request_focus_v2)
        adev->adm_request_focus_v2(adev->adm_data, in->capture_handle, ns);
    else if (adev->adm_request_focus)
        adev->adm_request_focus(adev->adm_data, in->capture_handle);

    return ret;
}

static void release_out_focus(struct stream_out *out)
{
    struct audio_device *adev = out->dev;

    if (adev->adm_abandon_focus)
        adev->adm_abandon_focus(adev->adm_data, out->handle);
}

static void release_in_focus(struct stream_in *in)
{
    struct audio_device *adev = in->dev;
    if (adev->adm_abandon_focus)
        adev->adm_abandon_focus(adev->adm_data, in->capture_handle);
}

static int parse_snd_card_status(struct str_parms *parms, int *card,
                                 card_status_t *status)
{
    char value[32]={0};
    char state[32]={0};

    int  ret = str_parms_get_str(parms, "SND_CARD_STATUS", value, sizeof(value));
    if (ret < 0)
        return -1;

    // sscanf should be okay as value is of max length 32.
    // same as sizeof state.
    if (sscanf(value, "%d,%s", card, state) < 2)
        return -1;

    *status = !strcmp(state, "ONLINE") ? CARD_STATUS_ONLINE :
                                         CARD_STATUS_OFFLINE;
    return 0;
}

static inline void adjust_frames_for_device_delay(struct stream_out *out,
                                                  uint32_t *dsp_frames) {
    // Adjustment accounts for A2dp encoder latency with offload usecases
    // Note: Encoder latency is returned in ms.
    if (is_a2dp_out_device_type(&out->device_list)) {
        unsigned long offset =
                (audio_extn_a2dp_get_encoder_latency() * out->sample_rate / 1000);
        *dsp_frames = (*dsp_frames > offset) ? (*dsp_frames - offset) : 0;
    }
}

static inline bool free_entry(void *key  __unused,
                              void *value, void *context __unused)
{
    free(value);
    return true;
}

static inline void free_map(Hashmap *map)
{
    if (map) {
        hashmapForEach(map, free_entry, (void *) NULL);
        hashmapFree(map);
    }
}

static inline void patch_map_remove_l(struct audio_device *adev,
                                audio_patch_handle_t patch_handle)
{
    if (patch_handle == AUDIO_PATCH_HANDLE_NONE)
        return;

    struct audio_patch_info *p_info =
        hashmapGet(adev->patch_map, (void *) (intptr_t) patch_handle);
    if (p_info) {
        ALOGV("%s: Remove patch %d", __func__, patch_handle);
        hashmapRemove(adev->patch_map, (void *) (intptr_t) patch_handle);
        free(p_info->patch);
        free(p_info);
    }
}

static inline int io_streams_map_insert(struct audio_device *adev,
                                    struct audio_stream *stream,
                                    audio_io_handle_t handle,
                                    audio_patch_handle_t patch_handle)
{
    struct audio_stream_info *s_info =
            (struct audio_stream_info *) calloc(1, sizeof(struct audio_stream_info));

    if (s_info == NULL) {
        ALOGE("%s: Could not allocate stream info", __func__);
        return -ENOMEM;
    }
    s_info->stream = stream;
    s_info->patch_handle = patch_handle;

    pthread_mutex_lock(&adev->lock);
    struct audio_stream_info *stream_info =
            hashmapPut(adev->io_streams_map, (void *) (intptr_t) handle, (void *) s_info);
    if (stream_info != NULL)
        free(stream_info);
    pthread_mutex_unlock(&adev->lock);
    ALOGV("%s: Added stream in io_streams_map with handle %d", __func__, handle);
    return 0;
}

static inline void io_streams_map_remove(struct audio_device *adev,
                                     audio_io_handle_t handle)
{
    pthread_mutex_lock(&adev->lock);
    struct audio_stream_info *s_info =
            hashmapRemove(adev->io_streams_map, (void *) (intptr_t) handle);
    if (s_info == NULL)
        goto done;
    ALOGV("%s: Removed stream with handle %d", __func__, handle);
    patch_map_remove_l(adev, s_info->patch_handle);
    free(s_info);
done:
    pthread_mutex_unlock(&adev->lock);
    return;
}

static struct audio_patch_info* fetch_patch_info_l(struct audio_device *adev,
                                    audio_patch_handle_t handle)
{
    struct audio_patch_info *p_info = NULL;
    p_info = (struct audio_patch_info *)
                 hashmapGet(adev->patch_map, (void *) (intptr_t) handle);
    return p_info;
}

__attribute__ ((visibility ("default")))
bool audio_hw_send_gain_dep_calibration(int level) {
    bool ret_val = false;
    ALOGV("%s: called ...", __func__);

    pthread_mutex_lock(&adev_init_lock);

    if (adev != NULL && adev->platform != NULL) {
        pthread_mutex_lock(&adev->lock);
        ret_val = platform_send_gain_dep_cal(adev->platform, level);

        // cache level info for any of the use case which
        // was not started.
        last_known_cal_step = level;;

        pthread_mutex_unlock(&adev->lock);
    } else {
        ALOGE("%s: %s is NULL", __func__, adev == NULL ? "adev" : "adev->platform");
    }

    pthread_mutex_unlock(&adev_init_lock);

    return ret_val;
}

static int check_and_set_gapless_mode(struct audio_device *adev, bool enable_gapless)
{
    bool gapless_enabled = false;
    const char *mixer_ctl_name = "Compress Gapless Playback";
    struct mixer_ctl *ctl;

    ALOGV("%s:", __func__);
    gapless_enabled = property_get_bool("vendor.audio.offload.gapless.enabled", false);

    /*Disable gapless if its AV playback*/
    gapless_enabled = gapless_enabled && enable_gapless;

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
                               __func__, mixer_ctl_name);
        return -EINVAL;
    }

    if (mixer_ctl_set_value(ctl, 0, gapless_enabled) < 0) {
        ALOGE("%s: Could not set gapless mode %d",
                       __func__, gapless_enabled);
         return -EINVAL;
    }
    return 0;
}

__attribute__ ((visibility ("default")))
int audio_hw_get_gain_level_mapping(struct amp_db_and_gain_table *mapping_tbl,
                                    int table_size) {
     int ret_val = 0;
     ALOGV("%s: enter ... ", __func__);

     pthread_mutex_lock(&adev_init_lock);
     if (adev == NULL) {
         ALOGW("%s: adev is NULL .... ", __func__);
         goto done;
     }

     pthread_mutex_lock(&adev->lock);
     ret_val = platform_get_gain_level_mapping(mapping_tbl, table_size);
     pthread_mutex_unlock(&adev->lock);
done:
     pthread_mutex_unlock(&adev_init_lock);
     ALOGV("%s: exit ... ", __func__);
     return ret_val;
}

bool audio_hw_send_qdsp_parameter(int stream_type, float vol, bool active)
{
    bool ret = false;
    ALOGV("%s: enter ...", __func__);

    pthread_mutex_lock(&adev_init_lock);

    if (adev != NULL && adev->platform != NULL) {
        pthread_mutex_lock(&adev->lock);
        ret = audio_extn_qdsp_set_state(adev, stream_type, vol, active);
        pthread_mutex_unlock(&adev->lock);
    }

    pthread_mutex_unlock(&adev_init_lock);

    ALOGV("%s: exit with ret %d", __func__, ret);
    return ret;
}

static bool is_supported_format(audio_format_t format)
{
    if (format == AUDIO_FORMAT_MP3 ||
        format == AUDIO_FORMAT_MP2 ||
        format == AUDIO_FORMAT_AAC_LC ||
        format == AUDIO_FORMAT_AAC_HE_V1 ||
        format == AUDIO_FORMAT_AAC_HE_V2 ||
        format == AUDIO_FORMAT_AAC_ADTS_LC ||
        format == AUDIO_FORMAT_AAC_ADTS_HE_V1 ||
        format == AUDIO_FORMAT_AAC_ADTS_HE_V2 ||
        format == AUDIO_FORMAT_AAC_LATM_LC ||
        format == AUDIO_FORMAT_AAC_LATM_HE_V1 ||
        format == AUDIO_FORMAT_AAC_LATM_HE_V2 ||
        format == AUDIO_FORMAT_PCM_24_BIT_PACKED ||
        format == AUDIO_FORMAT_PCM_8_24_BIT ||
        format == AUDIO_FORMAT_PCM_FLOAT ||
        format == AUDIO_FORMAT_PCM_32_BIT ||
        format == AUDIO_FORMAT_PCM_16_BIT ||
        format == AUDIO_FORMAT_AC3 ||
        format == AUDIO_FORMAT_E_AC3 ||
        format == AUDIO_FORMAT_DOLBY_TRUEHD ||
        format == AUDIO_FORMAT_DTS ||
        format == AUDIO_FORMAT_DTS_HD ||
        format == AUDIO_FORMAT_FLAC ||
        format == AUDIO_FORMAT_ALAC ||
        format == AUDIO_FORMAT_APE ||
        format == AUDIO_FORMAT_DSD ||
        format == AUDIO_FORMAT_VORBIS ||
        format == AUDIO_FORMAT_WMA ||
        format == AUDIO_FORMAT_WMA_PRO ||
        format == AUDIO_FORMAT_APTX ||
        format == AUDIO_FORMAT_IEC61937)
           return true;

    return false;
}

static bool is_supported_conc_usecase_for_power_mode_call(struct audio_device *adev)
{
    struct listnode *node;
    struct audio_usecase *usecase;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->id == USECASE_AUDIO_PLAYBACK_FM) {
            ALOGD("%s: FM usecase is active, not setting power mode", __func__);
            return false;
        }
    }

    return true;
}
static inline bool is_mmap_usecase(audio_usecase_t uc_id)
{
    return (uc_id == USECASE_AUDIO_RECORD_AFE_PROXY) ||
           (uc_id == USECASE_AUDIO_RECORD_AFE_PROXY2) ||
           (uc_id == USECASE_AUDIO_PLAYBACK_AFE_PROXY);
}

static inline bool is_valid_volume(float left, float right)
{
    return ((left >= 0.0f && right >= 0.0f) ? true : false);
}

static void enable_asrc_mode(struct audio_device *adev)
{
    ALOGV("%s", __func__);
    audio_route_apply_and_update_path(adev->audio_route,
                                  "asrc-mode");
    adev->asrc_mode_enabled = true;
}

static void disable_asrc_mode(struct audio_device *adev)
{
    ALOGV("%s", __func__);
    audio_route_reset_and_update_path(adev->audio_route,
                                  "asrc-mode");
    adev->asrc_mode_enabled = false;
}

static void check_and_configure_headphone(struct audio_device *adev,
                                          struct audio_usecase *uc_info,
                                              snd_device_t snd_device)
{
    struct listnode *node;
    struct audio_usecase *usecase;
    int new_backend_idx, usecase_backend_idx;
    bool spkr_hph_single_be_native_concurrency;

    new_backend_idx = platform_get_backend_index(snd_device);
    spkr_hph_single_be_native_concurrency = platform_get_spkr_hph_single_be_native_concurrency_flag();
    if ((spkr_hph_single_be_native_concurrency && (new_backend_idx == DEFAULT_CODEC_BACKEND)) ||
            uc_info->id == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS) {
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if ((usecase->type != PCM_CAPTURE) && (usecase != uc_info)) {
                usecase_backend_idx = platform_get_backend_index(usecase->out_snd_device);
                if (((usecase_backend_idx == HEADPHONE_BACKEND) ||
                    (usecase_backend_idx == HEADPHONE_44_1_BACKEND)) &&
                    ((usecase->stream.out->sample_rate % OUTPUT_SAMPLING_RATE_44100) == 0)) {
                    disable_audio_route(adev, usecase);
                    disable_snd_device(adev, usecase->out_snd_device);
                    usecase->stream.out->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
                    platform_check_and_set_codec_backend_cfg(adev, usecase,
                                                            usecase->out_snd_device);
                    enable_snd_device(adev, usecase->out_snd_device);
                    enable_audio_route(adev, usecase);
                }
            }
            else if ((usecase->type != PCM_CAPTURE) && (usecase == uc_info)) {
                usecase_backend_idx = platform_get_backend_index(usecase->out_snd_device);
                if (((usecase_backend_idx == HEADPHONE_BACKEND) ||
                    (usecase_backend_idx == HEADPHONE_44_1_BACKEND)) &&
                    ((usecase->stream.out->sample_rate % OUTPUT_SAMPLING_RATE_44100) == 0)) {
                    usecase->stream.out->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
                    platform_check_and_set_codec_backend_cfg(adev, usecase,
                                                            usecase->out_snd_device);
                }
            }
        }
    }
}

/*
 * - Enable ASRC mode for incoming mix path use case(Headphone backend)if Headphone
 *   44.1 or Native DSD backends are enabled for any of current use case.
 *   e.g. 48-> + (Naitve DSD or Headphone 44.1)
 * - Disable current mix path use case(Headphone backend) and re-enable it with
 *   ASRC mode for incoming Headphone 44.1 or Native DSD use case.
 *   e.g. Naitve DSD or Headphone 44.1 -> + 48
 */
static void check_and_set_asrc_mode(struct audio_device *adev,
                                          struct audio_usecase *uc_info,
                                          snd_device_t snd_device)
{
    ALOGV("%s snd device %d", __func__, snd_device);
    int i, num_new_devices = 0;
    snd_device_t split_new_snd_devices[SND_DEVICE_OUT_END];
    /*
    *Split snd device for new combo use case
    *e.g. Headphopne 44.1-> + Ringtone (Headphone + Speaker)
    */
    if (platform_split_snd_device(adev->platform,
                                 snd_device,
                                 &num_new_devices,
                                 split_new_snd_devices) == 0) {
        for (i = 0; i < num_new_devices; i++)
            check_and_set_asrc_mode(adev, uc_info, split_new_snd_devices[i]);
    } else {
        int new_backend_idx = platform_get_backend_index(snd_device);
        if (((new_backend_idx == HEADPHONE_BACKEND) ||
                (new_backend_idx == HEADPHONE_44_1_BACKEND) ||
                (new_backend_idx == DSD_NATIVE_BACKEND)) &&
                !adev->asrc_mode_enabled) {
            struct listnode *node = NULL;
            struct audio_usecase *uc = NULL;
            struct stream_out *curr_out = NULL;
            int usecase_backend_idx = DEFAULT_CODEC_BACKEND;
            int i, num_devices, ret = 0;
            snd_device_t split_snd_devices[SND_DEVICE_OUT_END];

            list_for_each(node, &adev->usecase_list) {
                uc = node_to_item(node, struct audio_usecase, list);
                curr_out = (struct stream_out*) uc->stream.out;
                if (curr_out && PCM_PLAYBACK == uc->type && uc != uc_info) {
                    /*
                    *Split snd device for existing combo use case
                    *e.g. Ringtone (Headphone + Speaker) + Headphopne 44.1
                    */
                    ret = platform_split_snd_device(adev->platform,
                                             uc->out_snd_device,
                                             &num_devices,
                                             split_snd_devices);
                    if (ret < 0 || num_devices == 0) {
                        ALOGV("%s: Unable to split uc->out_snd_device: %d",__func__, uc->out_snd_device);
                        split_snd_devices[0] = uc->out_snd_device;
                        num_devices = 1;
                    }
                    for (i = 0; i < num_devices; i++) {
                        usecase_backend_idx = platform_get_backend_index(split_snd_devices[i]);
                        ALOGD("%s:snd_dev %d usecase_backend_idx %d",__func__, split_snd_devices[i],usecase_backend_idx);
                        if((new_backend_idx == HEADPHONE_BACKEND) &&
                               ((usecase_backend_idx == HEADPHONE_44_1_BACKEND) ||
                               (usecase_backend_idx == DSD_NATIVE_BACKEND))) {
                            ALOGV("%s:DSD or native stream detected enabling asrcmode in hardware",
                                  __func__);
                            enable_asrc_mode(adev);
                            break;
                        } else if(((new_backend_idx == HEADPHONE_44_1_BACKEND) ||
                                  (new_backend_idx == DSD_NATIVE_BACKEND)) &&
                                  (usecase_backend_idx == HEADPHONE_BACKEND)) {
                            ALOGV("%s: 48K stream detected, disabling and enabling it \
                                   with asrcmode in hardware", __func__);
                            disable_audio_route(adev, uc);
                            disable_snd_device(adev, uc->out_snd_device);
                            // Apply true-high-quality-mode if DSD or > 44.1KHz or >=24-bit
                            if (new_backend_idx == DSD_NATIVE_BACKEND)
                                audio_route_apply_and_update_path(adev->audio_route,
                                                        "hph-true-highquality-mode");
                            else if ((new_backend_idx == HEADPHONE_44_1_BACKEND) &&
                                     (curr_out->bit_width >= 24))
                                audio_route_apply_and_update_path(adev->audio_route,
                                                             "hph-highquality-mode");
                            enable_asrc_mode(adev);
                            enable_snd_device(adev, uc->out_snd_device);
                            enable_audio_route(adev, uc);
                            break;
                        }
                    }
                    // reset split devices count
                    num_devices = 0;
                }
                if (adev->asrc_mode_enabled)
                    break;
            }
        }
    }
}

static int send_effect_enable_disable_mixer_ctl(struct audio_device *adev,
                          struct audio_effect_config effect_config,
                          unsigned int param_value)
{
    char mixer_ctl_name[] = "Audio Effect";
    struct mixer_ctl *ctl;
    long set_values[6];
    struct stream_in *in = adev_get_active_input(adev);

    if (in == NULL) {
        ALOGE("%s: active input stream is NULL", __func__);
        return -EINVAL;
    }

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get mixer ctl - %s",
               __func__, mixer_ctl_name);
        return -EINVAL;
    }

    set_values[0] = 1; //0:Rx 1:Tx
    set_values[1] = in->app_type_cfg.app_type;
    set_values[2] = (long)effect_config.module_id;
    set_values[3] = (long)effect_config.instance_id;
    set_values[4] = (long)effect_config.param_id;
    set_values[5] = param_value;

    mixer_ctl_set_array(ctl, set_values, ARRAY_SIZE(set_values));

    return 0;

}

static int update_effect_param_ecns(struct audio_device *adev, unsigned int module_id,
                               int effect_type, unsigned int *param_value)
{
    int ret = 0;
    struct audio_effect_config other_effect_config;
    struct audio_usecase *usecase = NULL;
    struct stream_in *in = adev_get_active_input(adev);

    if (in == NULL) {
        ALOGE("%s: active input stream is NULL", __func__);
        return -EINVAL;
    }

    usecase = get_usecase_from_list(adev, in->usecase);
    if (!usecase)
        return -EINVAL;

    ret = platform_get_effect_config_data(usecase->in_snd_device, &other_effect_config,
                                            effect_type == EFFECT_AEC ? EFFECT_NS : EFFECT_AEC);
    if (ret < 0) {
        ALOGE("%s Failed to get effect params %d", __func__, ret);
        return ret;
    }

    if (module_id == other_effect_config.module_id) {
            //Same module id for AEC/NS. Values need to be combined
            if (((effect_type == EFFECT_AEC) && (in->enable_ns)) ||
                ((effect_type == EFFECT_NS) && (in->enable_aec))) {
                *param_value |= other_effect_config.param_value;
            }
    }

    return ret;
}

static int enable_disable_effect(struct audio_device *adev, int effect_type, bool enable)
{
    struct audio_effect_config effect_config;
    struct audio_usecase *usecase = NULL;
    int ret = 0;
    unsigned int param_value = 0;
    struct stream_in *in = adev_get_active_input(adev);

    if(!voice_extn_is_dynamic_ecns_enabled())
        return ENOSYS;

    if (!in) {
        ALOGE("%s: Invalid input stream", __func__);
        return -EINVAL;
    }

    ALOGD("%s: effect_type:%d enable:%d", __func__, effect_type, enable);

    usecase = get_usecase_from_list(adev, in->usecase);
    if (usecase == NULL) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
              __func__, in->usecase);
        return -EINVAL;
    }

    ret = platform_get_effect_config_data(usecase->in_snd_device, &effect_config, effect_type);
    if (ret < 0) {
        ALOGE("%s Failed to get module id %d", __func__, ret);
        return ret;
    }
    ALOGV("%s: %d %d usecase->id:%d usecase->in_snd_device:%d", __func__, effect_config.module_id,
           in->app_type_cfg.app_type, usecase->id, usecase->in_snd_device);

    if(enable)
        param_value = effect_config.param_value;

    /*Special handling for AEC & NS effects Param values need to be
      updated if module ids are same*/

    if ((effect_type == EFFECT_AEC) || (effect_type == EFFECT_NS)) {
        ret = update_effect_param_ecns(adev, effect_config.module_id, effect_type, &param_value);
        if (ret < 0)
            return ret;
    }

    ret = send_effect_enable_disable_mixer_ctl(adev, effect_config, param_value);

    return ret;
}

static void check_and_enable_effect(struct audio_device *adev)
{
    if(!voice_extn_is_dynamic_ecns_enabled())
        return;

    struct stream_in *in = adev_get_active_input(adev);

    if (in != NULL && !in->standby) {
        if (in->enable_aec)
            enable_disable_effect(adev, EFFECT_AEC, true);

        if (in->enable_ns &&
            in->source == AUDIO_SOURCE_VOICE_COMMUNICATION) {
            enable_disable_effect(adev, EFFECT_NS, true);
        }
    }
}

int pcm_ioctl(struct pcm *pcm, int request, ...)
{
    va_list ap;
    void * arg;
    int pcm_fd = *(int*)pcm;

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);

    return ioctl(pcm_fd, request, arg);
}

int enable_audio_route(struct audio_device *adev,
                       struct audio_usecase *usecase)
{
    snd_device_t snd_device;
    char mixer_path[MIXER_PATH_MAX_LENGTH];
    struct stream_out *out = NULL;
    struct stream_in *in = NULL;
    struct listnode out_devices;
    int ret = 0;

    if (usecase == NULL)
        return -EINVAL;

    ALOGV("%s: enter: usecase(%d)", __func__, usecase->id);

    if (usecase->type == PCM_CAPTURE) {
        struct stream_in *in = usecase->stream.in;
        struct audio_usecase *uinfo;
        snd_device = usecase->in_snd_device;

        if (in) {
            if (in->enable_aec || in->enable_ec_port) {
                list_init(&out_devices);
                update_device_list(&out_devices, AUDIO_DEVICE_OUT_SPEAKER, "", true);
                struct listnode *node;
                struct audio_usecase *voip_usecase = get_usecase_from_list(adev,
                                                           USECASE_AUDIO_PLAYBACK_VOIP);
                if (voip_usecase) {
                    assign_devices(&out_devices,
                                   &voip_usecase->stream.out->device_list);
                } else if (adev->primary_output &&
                              !adev->primary_output->standby) {
                    assign_devices(&out_devices,
                                   &adev->primary_output->device_list);
                } else {
                    list_for_each(node, &adev->usecase_list) {
                        uinfo = node_to_item(node, struct audio_usecase, list);
                        if (uinfo->type != PCM_CAPTURE) {
                            assign_devices(&out_devices,
                                           &uinfo->stream.out->device_list);
                            break;
                        }
                    }
                }

                platform_set_echo_reference(adev, true, &out_devices);
                in->ec_opened = true;
            }
        }
    } else if ((usecase->type == TRANSCODE_LOOPBACK_TX) || ((usecase->type == PCM_HFP_CALL) &&
        ((usecase->id == USECASE_AUDIO_HFP_SCO) || (usecase->id == USECASE_AUDIO_HFP_SCO_WB)) &&
        (usecase->in_snd_device == SND_DEVICE_IN_VOICE_SPEAKER_MIC_HFP_MMSECNS))) {
        snd_device = usecase->in_snd_device;
    } else {
        snd_device = usecase->out_snd_device;
    }

    if (usecase->type == PCM_CAPTURE) {
       if (platform_get_fluence_nn_state(adev->platform) == 0) {
           platform_set_fluence_nn_state(adev->platform, true);
           ALOGD("%s: set fluence nn capture state", __func__);
       }
    }

#ifdef DS1_DOLBY_DAP_ENABLED
    audio_extn_dolby_set_dmid(adev);
    audio_extn_dolby_set_endpoint(adev);
#endif
    audio_extn_dolby_ds2_set_endpoint(adev);
    audio_extn_sound_trigger_update_stream_status(usecase, ST_EVENT_STREAM_BUSY);
    audio_extn_listen_update_stream_status(usecase, LISTEN_EVENT_STREAM_BUSY);
    audio_extn_utils_send_app_type_cfg(adev, usecase);
    if (audio_extn_is_maxx_audio_enabled())
        audio_extn_ma_set_device(usecase);
    audio_extn_utils_send_audio_calibration(adev, usecase);
    if ((usecase->type == PCM_PLAYBACK) && is_offload_usecase(usecase->id)) {
        out = usecase->stream.out;
        if (out && out->compr)
            audio_extn_utils_compress_set_clk_rec_mode(usecase);
    }

    if (usecase->type == PCM_CAPTURE) {
         if (platform_get_fluence_nn_state(adev->platform) == 1 &&
             adev->fluence_nn_usecase_id == USECASE_INVALID ) {
             adev->fluence_nn_usecase_id = usecase->id;
             ALOGD("%s: assign fluence nn usecase %d", __func__, usecase->id);
         }
    }

    if (usecase->type == PCM_CAPTURE) {
        in = usecase->stream.in;
        if (in && is_loopback_input_device(get_device_types(&in->device_list))) {
            ALOGD("%s: set custom mtmx params v1", __func__);
            audio_extn_set_custom_mtmx_params_v1(adev, usecase, true);
        }
    } else {
        audio_extn_set_custom_mtmx_params_v2(adev, usecase, true);
    }

    // we shouldn't truncate mixer_path
    ALOGW_IF(strlcpy(mixer_path, use_case_table[usecase->id], sizeof(mixer_path))
            >= sizeof(mixer_path), "%s: truncation on mixer path", __func__);
    // this also appends to mixer_path
    platform_add_backend_name(mixer_path, snd_device, usecase);
    ALOGD("%s: apply mixer and update path: %s", __func__, mixer_path);
    ret = audio_route_apply_and_update_path(adev->audio_route, mixer_path);
    if (!ret && usecase->id == USECASE_AUDIO_PLAYBACK_FM) {
        struct str_parms *parms = str_parms_create_str("fm_restore_volume=1");
        if (parms) {
            audio_extn_fm_set_parameters(adev, parms);
            str_parms_destroy(parms);
        }
    }
    ALOGV("%s: exit", __func__);
    return 0;
}

int disable_audio_route(struct audio_device *adev,
                        struct audio_usecase *usecase)
{
    snd_device_t snd_device;
    char mixer_path[MIXER_PATH_MAX_LENGTH];
    struct stream_in *in = NULL;

    if (usecase == NULL || usecase->id == USECASE_INVALID)
        return -EINVAL;

    ALOGV("%s: enter: usecase(%d)", __func__, usecase->id);
    if (usecase->type == PCM_CAPTURE || usecase->type == TRANSCODE_LOOPBACK_TX)
        snd_device = usecase->in_snd_device;
    else
        snd_device = usecase->out_snd_device;

    /* disable island and power mode on supported device for voice call */
    if (usecase->type == VOICE_CALL) {
        if (usecase->in_snd_device != SND_DEVICE_NONE) {
            if (platform_get_island_cfg_on_device(adev->platform, usecase->in_snd_device) &&
                platform_get_power_mode_on_device(adev->platform, usecase->in_snd_device)) {
                platform_set_island_cfg_on_device(adev, usecase->in_snd_device, false);
                platform_set_power_mode_on_device(adev, usecase->in_snd_device, false);
                platform_reset_island_power_status(adev->platform, usecase->in_snd_device);
                if (voice_is_lte_call_active(adev))
                    platform_set_tx_lpi_mode(adev->platform, false);
                ALOGD("%s: disable island cfg and power mode in voice tx path",
                      __func__);
            }
        }
        if (usecase->out_snd_device != SND_DEVICE_NONE) {
            if (platform_get_island_cfg_on_device(adev->platform, usecase->out_snd_device) &&
                platform_get_power_mode_on_device(adev->platform, usecase->out_snd_device)) {
                platform_set_island_cfg_on_device(adev, usecase->out_snd_device, false);
                platform_set_power_mode_on_device(adev, usecase->out_snd_device, false);
                platform_reset_island_power_status(adev->platform, usecase->out_snd_device);
                ALOGD("%s: disable island cfg and power mode in voice rx path",
                       __func__);
            }
        }
    }

    // we shouldn't truncate mixer_path
    ALOGW_IF(strlcpy(mixer_path, use_case_table[usecase->id], sizeof(mixer_path))
            >= sizeof(mixer_path), "%s: truncation on mixer path", __func__);
    // this also appends to mixer_path
    platform_add_backend_name(mixer_path, snd_device, usecase);
    ALOGD("%s: reset and update mixer path: %s", __func__, mixer_path);
    audio_route_reset_and_update_path(adev->audio_route, mixer_path);
    if (usecase->type == PCM_CAPTURE) {
        struct stream_in *in = usecase->stream.in;
        if (in && in->ec_opened) {
            struct listnode out_devices;
            list_init(&out_devices);
            platform_set_echo_reference(in->dev, false, &out_devices);
            in->ec_opened = false;
        }
    }
    if (usecase->id == adev->fluence_nn_usecase_id) {
         platform_set_fluence_nn_state(adev->platform, false);
         adev->fluence_nn_usecase_id = USECASE_INVALID;
         ALOGD("%s: reset fluence nn capture state", __func__);
    }
    audio_extn_sound_trigger_update_stream_status(usecase, ST_EVENT_STREAM_FREE);
    audio_extn_listen_update_stream_status(usecase, LISTEN_EVENT_STREAM_FREE);

    if (usecase->type == PCM_CAPTURE) {
        in = usecase->stream.in;
        if (in && is_loopback_input_device(get_device_types(&in->device_list))) {
            ALOGD("%s: reset custom mtmx params v1", __func__);
            audio_extn_set_custom_mtmx_params_v1(adev, usecase, false);
        }
    } else {
        audio_extn_set_custom_mtmx_params_v2(adev, usecase, false);
    }

    if ((usecase->type == PCM_PLAYBACK) &&
            (usecase->stream.out != NULL))
        usecase->stream.out->pspd_coeff_sent = false;

    ALOGV("%s: exit", __func__);
    return 0;
}

int enable_snd_device(struct audio_device *adev,
                      snd_device_t snd_device)
{
    int i, num_devices = 0;
    snd_device_t new_snd_devices[SND_DEVICE_OUT_END];
    char device_name[DEVICE_NAME_MAX_SIZE] = {0};

    if (snd_device < SND_DEVICE_MIN ||
        snd_device >= SND_DEVICE_MAX) {
        ALOGE("%s: Invalid sound device %d", __func__, snd_device);
        return -EINVAL;
    }

    if (platform_get_snd_device_name_extn(adev->platform, snd_device, device_name) < 0) {
        ALOGE("%s: Invalid sound device returned", __func__);
        return -EINVAL;
    }

    adev->snd_dev_ref_cnt[snd_device]++;

    if ((adev->snd_dev_ref_cnt[snd_device] > 1) &&
            (platform_split_snd_device(adev->platform,
                                       snd_device,
                                       &num_devices,
                                       new_snd_devices) != 0)) {
        ALOGV("%s: snd_device(%d: %s) is already active",
              __func__, snd_device, device_name);
        /* Set backend config for A2DP to ensure slimbus configuration
           is correct if A2DP is already active and backend is closed
           and re-opened */
        if (snd_device == SND_DEVICE_OUT_BT_A2DP)
            audio_extn_a2dp_set_source_backend_cfg();
        return 0;
    }

    if (audio_extn_spkr_prot_is_enabled())
         audio_extn_spkr_prot_calib_cancel(adev);

    audio_extn_dsm_feedback_enable(adev, snd_device, true);

    if (platform_can_enable_spkr_prot_on_device(snd_device) &&
         audio_extn_spkr_prot_is_enabled()) {
        if (platform_get_spkr_prot_acdb_id(snd_device) < 0) {
            goto err;
        }
        audio_extn_dev_arbi_acquire(snd_device);
        if (audio_extn_spkr_prot_start_processing(snd_device)) {
            ALOGE("%s: spkr_start_processing failed", __func__);
            audio_extn_dev_arbi_release(snd_device);
            goto err;
        }
    } else if (platform_split_snd_device(adev->platform,
                                         snd_device,
                                         &num_devices,
                                         new_snd_devices) == 0) {
        for (i = 0; i < num_devices; i++) {
            enable_snd_device(adev, new_snd_devices[i]);
        }
        platform_set_speaker_gain_in_combo(adev, snd_device, true);
    } else {
        ALOGD("%s: snd_device(%d: %s)", __func__, snd_device, device_name);

        /* enable island and power mode on supported device */
        if (platform_get_island_cfg_on_device(adev->platform, snd_device) &&
            platform_get_power_mode_on_device(adev->platform, snd_device)) {
            platform_set_island_cfg_on_device(adev, snd_device, true);
            platform_set_power_mode_on_device(adev, snd_device, true);
            if (voice_is_lte_call_active(adev) &&
                (snd_device >= SND_DEVICE_IN_BEGIN &&
                 snd_device < SND_DEVICE_IN_END))
               platform_set_tx_lpi_mode(adev->platform, true);
            ALOGD("%s: enable island cfg and power mode on: %s",
                   __func__, device_name);
        }

        if (SND_DEVICE_OUT_BT_A2DP == snd_device) {
            if (audio_extn_a2dp_start_playback() < 0) {
                ALOGE(" fail to configure A2dp Source control path ");
                goto err;
            } else {
                adev->a2dp_started = true;
            }
        }

        if ((SND_DEVICE_IN_BT_A2DP == snd_device) &&
            (audio_extn_a2dp_start_capture() < 0)) {
            ALOGE(" fail to configure A2dp Sink control path ");
            goto err;
        }

        if ((SND_DEVICE_OUT_BT_SCO_SWB == snd_device) ||
            (SND_DEVICE_IN_BT_SCO_MIC_SWB_NREC == snd_device) ||
            (SND_DEVICE_IN_BT_SCO_MIC_SWB == snd_device)) {
            if (!adev->bt_sco_on || (audio_extn_sco_start_configuration() < 0)) {
                ALOGE(" fail to configure sco control path ");
                goto err;
            }
        }

        configure_btsco_sample_rate(snd_device);
        /* due to the possibility of calibration overwrite between listen
            and audio, notify listen hal before audio calibration is sent */
        audio_extn_sound_trigger_update_device_status(snd_device,
                                        ST_EVENT_SND_DEVICE_BUSY);
        audio_extn_listen_update_device_status(snd_device,
                                        LISTEN_EVENT_SND_DEVICE_BUSY);
        if (platform_get_snd_device_acdb_id(snd_device) < 0) {
            audio_extn_sound_trigger_update_device_status(snd_device,
                                            ST_EVENT_SND_DEVICE_FREE);
            audio_extn_listen_update_device_status(snd_device,
                                        LISTEN_EVENT_SND_DEVICE_FREE);
            goto err;
        }
        audio_extn_dev_arbi_acquire(snd_device);
        audio_route_apply_and_update_path(adev->audio_route, device_name);

        if (SND_DEVICE_OUT_HEADPHONES == snd_device &&
            !adev->native_playback_enabled &&
            audio_is_true_native_stream_active(adev)) {
            ALOGD("%s: %d: napb: enabling native mode in hardware",
                  __func__, __LINE__);
            audio_route_apply_and_update_path(adev->audio_route,
                                              "true-native-mode");
            adev->native_playback_enabled = true;
        }
        if (((snd_device == SND_DEVICE_IN_HANDSET_6MIC) ||
            (snd_device == SND_DEVICE_IN_HANDSET_QMIC)) &&
            (audio_extn_ffv_get_stream() == adev_get_active_input(adev))) {
            ALOGD("%s: init ec ref loopback", __func__);
            audio_extn_ffv_init_ec_ref_loopback(adev, snd_device);
        }
    }
    return 0;
err:
    adev->snd_dev_ref_cnt[snd_device]--;
    return -EINVAL;;
}

int disable_snd_device(struct audio_device *adev,
                       snd_device_t snd_device)
{
    int i, num_devices = 0;
    snd_device_t new_snd_devices[SND_DEVICE_OUT_END];
    char device_name[DEVICE_NAME_MAX_SIZE] = {0};

    if (snd_device < SND_DEVICE_MIN ||
        snd_device >= SND_DEVICE_MAX) {
        ALOGE("%s: Invalid sound device %d", __func__, snd_device);
        return -EINVAL;
    }

    if (platform_get_snd_device_name_extn(adev->platform, snd_device, device_name) < 0) {
        ALOGE("%s: Invalid sound device returned", __func__);
        return -EINVAL;
    }

    if (adev->snd_dev_ref_cnt[snd_device] <= 0) {
        ALOGE("%s: device ref cnt is already 0", __func__);
        return -EINVAL;
    }

    adev->snd_dev_ref_cnt[snd_device]--;


    if (adev->snd_dev_ref_cnt[snd_device] == 0) {
        ALOGD("%s: snd_device(%d: %s)", __func__, snd_device, device_name);

        audio_extn_dsm_feedback_enable(adev, snd_device, false);

        if (platform_can_enable_spkr_prot_on_device(snd_device) &&
             audio_extn_spkr_prot_is_enabled()) {
            audio_extn_spkr_prot_stop_processing(snd_device);

            // when speaker device is disabled, reset swap.
            // will be renabled on usecase start
            platform_set_swap_channels(adev, false);
        } else if (platform_split_snd_device(adev->platform,
                                             snd_device,
                                             &num_devices,
                                             new_snd_devices) == 0) {
            for (i = 0; i < num_devices; i++) {
                disable_snd_device(adev, new_snd_devices[i]);
            }
            platform_set_speaker_gain_in_combo(adev, snd_device, false);
        } else {
            audio_route_reset_and_update_path(adev->audio_route, device_name);
        }

        if (snd_device == SND_DEVICE_OUT_BT_A2DP) {
            audio_extn_a2dp_stop_playback();
            adev->a2dp_started = false;
        } else if (snd_device == SND_DEVICE_IN_BT_A2DP)
            audio_extn_a2dp_stop_capture();
        else if ((snd_device == SND_DEVICE_OUT_HDMI) ||
                (snd_device == SND_DEVICE_OUT_DISPLAY_PORT))
            adev->is_channel_status_set = false;
        else if ((snd_device == SND_DEVICE_OUT_HEADPHONES) &&
                 adev->native_playback_enabled) {
            ALOGD("%s: %d: napb: disabling native mode in hardware",
                  __func__, __LINE__);
            audio_route_reset_and_update_path(adev->audio_route,
                                              "true-native-mode");
            adev->native_playback_enabled = false;
        } else if ((snd_device == SND_DEVICE_OUT_HEADPHONES) &&
                 adev->asrc_mode_enabled) {
            ALOGD("%s: %d: disabling asrc mode in hardware", __func__, __LINE__);
            disable_asrc_mode(adev);
            audio_route_apply_and_update_path(adev->audio_route, "hph-lowpower-mode");
        } else if (((snd_device == SND_DEVICE_IN_HANDSET_6MIC) ||
            (snd_device == SND_DEVICE_IN_HANDSET_QMIC)) &&
            (audio_extn_ffv_get_stream() == adev_get_active_input(adev))) {
            ALOGD("%s: deinit ec ref loopback", __func__);
            audio_extn_ffv_deinit_ec_ref_loopback(adev, snd_device);
        }

        audio_extn_utils_release_snd_device(snd_device);
    } else {
        if (platform_split_snd_device(adev->platform,
                    snd_device,
                    &num_devices,
                    new_snd_devices) == 0) {
            for (i = 0; i < num_devices; i++) {
                adev->snd_dev_ref_cnt[new_snd_devices[i]]--;
            }
        }
    }

    return 0;
}

/*
  legend:
  uc - existing usecase
  new_uc - new usecase
  d1, d11, d2 - SND_DEVICE enums
  a1, a2 - corresponding ANDROID device enums
  B1, B2 - backend strings

case 1
  uc->dev  d1 (a1)               B1
  new_uc->dev d1 (a1), d2 (a2)   B1, B2

  resolution: disable and enable uc->dev on d1

case 2
  uc->dev d1 (a1)        B1
  new_uc->dev d11 (a1)   B1

  resolution: need to switch uc since d1 and d11 are related
  (e.g. speaker and voice-speaker)
  use ANDROID_DEVICE_OUT enums to match devices since SND_DEVICE enums may vary

case 3
  uc->dev d1 (a1)        B1
  new_uc->dev d2 (a2)    B2

  resolution: no need to switch uc

case 4
  uc->dev d1 (a1)      B1
  new_uc->dev d2 (a2)  B1

  resolution: disable enable uc-dev on d2 since backends match
  we cannot enable two streams on two different devices if they
  share the same backend. e.g. if offload is on speaker device using
  QUAD_MI2S backend and a low-latency stream is started on voice-handset
  using the same backend, offload must also be switched to voice-handset.

case 5
  uc->dev  d1 (a1)                  B1
  new_uc->dev d1 (a1), d2 (a2)      B1

  resolution: disable enable uc-dev on d2 since backends match
  we cannot enable two streams on two different devices if they
  share the same backend.

case 6
  uc->dev  d1 (a1)    B1
  new_uc->dev d2 (a1) B2

  resolution: no need to switch

case 7
  uc->dev d1 (a1), d2 (a2)       B1, B2
  new_uc->dev d1 (a1)            B1

  resolution: no need to switch

case 8
  uc->dev d1 (a1)                B1
  new_uc->dev d11 (a1), d2 (a2)  B1, B2
  resolution: compared to case 1, for this case, d1 and d11 are related
  then need to do the same as case 2 to siwtch to new uc

case 9
  uc->dev d1 (a1), d2(a2)        B1  B2
  new_uc->dev d1 (a1), d22 (a2)  B1, B2
  resolution: disable enable uc-dev on d2 since backends match
  we cannot enable two streams on two different devices if they
  share the same backend. This is special case for combo use case
  with a2dp and sco devices which uses same backend.
  e.g. speaker-a2dp and speaker-btsco
*/
static snd_device_t derive_playback_snd_device(void * platform,
                                               struct audio_usecase *uc,
                                               struct audio_usecase *new_uc,
                                               snd_device_t new_snd_device)
{
    struct listnode a1, a2;

    snd_device_t d1 = uc->out_snd_device;
    snd_device_t d2 = new_snd_device;

    list_init(&a1);
    list_init(&a2);

    switch (uc->type) {
        case TRANSCODE_LOOPBACK_RX :
            assign_devices(&a1, &uc->stream.inout->out_config.device_list);
            assign_devices(&a2, &new_uc->stream.inout->out_config.device_list);
            break;
        default :
            assign_devices(&a1, &uc->stream.out->device_list);
            assign_devices(&a2, &new_uc->stream.out->device_list);
            break;
    }

    // Treat as a special case when a1 and a2 are not disjoint
    if (!compare_devices(&a1, &a2) &&
         compare_devices_for_any_match(&a1 ,&a2)) {
        snd_device_t d3[2];
        int num_devices = 0;
        int ret = platform_split_snd_device(platform,
                                            list_length(&a1) > 1 ? d1 : d2,
                                            &num_devices,
                                            d3);
        if (ret < 0) {
            if (ret != -ENOSYS) {
                ALOGW("%s failed to split snd_device %d",
                      __func__,
                      list_length(&a1) > 1 ? d1 : d2);
            }
            goto end;
        }

        if (platform_check_backends_match(d3[0], d3[1])) {
            return d2; // case 5
        } else {
            if ((list_length(&a1) > 1) && (list_length(&a2) > 1) &&
                 platform_check_backends_match(d1, d2))
                return d2; //case 9
            if (list_length(&a1) > 1)
                return d1; //case 7
            // check if d1 is related to any of d3's
            if (d1 == d3[0] || d1 == d3[1])
                return d1; // case 1
            else
                return d3[1]; // case 8
        }
    } else {
        if (platform_check_backends_match(d1, d2)) {
            return d2; // case 2, 4
        } else {
            return d1; // case 6, 3
        }
    }

end:
    return d2; // return whatever was calculated before.
}

static void check_usecases_codec_backend(struct audio_device *adev,
                                              struct audio_usecase *uc_info,
                                              snd_device_t snd_device)
{
    struct listnode *node;
    struct audio_usecase *usecase;
    bool switch_device[AUDIO_USECASE_MAX];
    snd_device_t uc_derive_snd_device;
    snd_device_t derive_snd_device[AUDIO_USECASE_MAX];
    snd_device_t split_snd_devices[SND_DEVICE_OUT_END];
    int i, num_uc_to_switch = 0, num_devices = 0;
    int status = 0;
    bool force_restart_session = false;
    /*
     * This function is to make sure that all the usecases that are active on
     * the hardware codec backend are always routed to any one device that is
     * handled by the hardware codec.
     * For example, if low-latency and deep-buffer usecases are currently active
     * on speaker and out_set_parameters(headset) is received on low-latency
     * output, then we have to make sure deep-buffer is also switched to headset,
     * because of the limitation that both the devices cannot be enabled
     * at the same time as they share the same backend.
     */
    /*
     * This call is to check if we need to force routing for a particular stream
     * If there is a backend configuration change for the device when a
     * new stream starts, then ADM needs to be closed and re-opened with the new
     * configuraion. This call check if we need to re-route all the streams
     * associated with the backend. Touch tone + 24 bit + native playback.
     */
    bool force_routing = platform_check_and_set_codec_backend_cfg(adev, uc_info,
                         snd_device);
    /* For a2dp device reconfigure all active sessions
     * with new AFE encoder format based on a2dp state
     */
    if ((SND_DEVICE_OUT_BT_A2DP == snd_device ||
         SND_DEVICE_OUT_SPEAKER_AND_BT_A2DP == snd_device ||
         SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_A2DP == snd_device) &&
         audio_extn_a2dp_is_force_device_switch()) {
         force_routing = true;
         force_restart_session = true;
    }

    /*
     * Island cfg and power mode config needs to set before AFE port start.
     * Set force routing in case of voice device was enable before.
     */
    if (uc_info->type == VOICE_CALL &&
        voice_extn_is_voice_power_mode_supported() &&
        is_supported_conc_usecase_for_power_mode_call(adev) &&
        platform_check_and_update_island_power_status(adev->platform,
                                             uc_info,
                                             snd_device)) {
        force_routing = true;
        ALOGD("%s:becf: force routing %d for power mode supported device",
               __func__, force_routing);
    }
    ALOGD("%s:becf: force routing %d", __func__, force_routing);

    /* Disable all the usecases on the shared backend other than the
     * specified usecase.
     */
    for (i = 0; i < AUDIO_USECASE_MAX; i++)
        switch_device[i] = false;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);

        ALOGD("%s:becf: (%d) check_usecases curr device: %s, usecase device:%s "
            "backends match %d",__func__, i,
              platform_get_snd_device_name(snd_device),
              platform_get_snd_device_name(usecase->out_snd_device),
              platform_check_backends_match(snd_device, usecase->out_snd_device));
        if ((usecase->type != PCM_CAPTURE) && (usecase != uc_info) &&
                (usecase->type != PCM_PASSTHROUGH)) {
            uc_derive_snd_device = derive_playback_snd_device(adev->platform,
                                               usecase, uc_info, snd_device);
            if (((uc_derive_snd_device != usecase->out_snd_device) || force_routing) &&
                (is_codec_backend_out_device_type(&usecase->device_list) ||
                compare_device_type(&usecase->device_list, AUDIO_DEVICE_OUT_AUX_DIGITAL) ||
                compare_device_type(&usecase->device_list, AUDIO_DEVICE_OUT_USB_DEVICE) ||
                compare_device_type(&usecase->device_list, AUDIO_DEVICE_OUT_USB_HEADSET) ||
                is_a2dp_out_device_type(&usecase->device_list) ||
                is_sco_out_device_type(&usecase->device_list)) &&
                ((force_restart_session) ||
                (platform_check_backends_match(snd_device, usecase->out_snd_device)))) {
                ALOGD("%s:becf: check_usecases (%s) is active on (%s) - disabling ..",
                    __func__, use_case_table[usecase->id],
                      platform_get_snd_device_name(usecase->out_snd_device));
                disable_audio_route(adev, usecase);
                switch_device[usecase->id] = true;
                /* Enable existing usecase on derived playback device */
                derive_snd_device[usecase->id] = uc_derive_snd_device;
                num_uc_to_switch++;
            }
        }
    }

    ALOGD("%s:becf: check_usecases num.of Usecases to switch %d", __func__,
        num_uc_to_switch);

    if (num_uc_to_switch) {
        /* All streams have been de-routed. Disable the device */

        /* Make sure the previous devices to be disabled first and then enable the
           selected devices */
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (switch_device[usecase->id]) {
                /* Check if output sound device to be switched can be split and if any
                   of the split devices match with derived sound device */
                if (platform_split_snd_device(adev->platform, usecase->out_snd_device,
                                               &num_devices, split_snd_devices) == 0) {
                    adev->snd_dev_ref_cnt[usecase->out_snd_device]--;
                    for (i = 0; i < num_devices; i++) {
                        /* Disable devices that do not match with derived sound device */
                        if (split_snd_devices[i] != derive_snd_device[usecase->id])
                            disable_snd_device(adev, split_snd_devices[i]);
                     }
                } else {
                    disable_snd_device(adev, usecase->out_snd_device);
                }
            }
        }

        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (switch_device[usecase->id]) {
                if (platform_split_snd_device(adev->platform, usecase->out_snd_device,
                                               &num_devices, split_snd_devices) == 0) {
                        /* Enable derived sound device only if it does not match with
                           one of the split sound devices. This is because the matching
                           sound device was not disabled */
                        bool should_enable = true;
                        for (i = 0; i < num_devices; i++) {
                            if (derive_snd_device[usecase->id] == split_snd_devices[i]) {
                                 should_enable = false;
                                 break;
                            }
                        }
                        if (should_enable)
                            enable_snd_device(adev, derive_snd_device[usecase->id]);
                } else {
                    enable_snd_device(adev, derive_snd_device[usecase->id]);
                }
            }
        }

        /* Re-route all the usecases on the shared backend other than the
           specified usecase to new snd devices */
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            /* Update the out_snd_device only before enabling the audio route */
            if (switch_device[usecase->id]) {
                usecase->out_snd_device = derive_snd_device[usecase->id];
                ALOGD("%s:becf: enabling usecase (%s) on (%s)", __func__,
                     use_case_table[usecase->id],
                     platform_get_snd_device_name(usecase->out_snd_device));
                /* Update voc calibration before enabling Voice/VoIP route */
                if (usecase->type == VOICE_CALL || usecase->type == VOIP_CALL)
                    status = platform_switch_voice_call_device_post(adev->platform,
                                                       usecase->out_snd_device,
                                                       platform_get_input_snd_device(
                                                           adev->platform, NULL,
                                                           &uc_info->device_list,
                                                           usecase->type));
                enable_audio_route(adev, usecase);
                if (usecase->stream.out && usecase->id == USECASE_AUDIO_PLAYBACK_VOIP) {
                    out_set_voip_volume(&usecase->stream.out->stream,
                                        usecase->stream.out->volume_l,
                                        usecase->stream.out->volume_r);
                }
            }
        }
    }
}

static void check_usecases_capture_codec_backend(struct audio_device *adev,
                                             struct audio_usecase *uc_info,
                                             snd_device_t snd_device)
{
    struct listnode *node;
    struct audio_usecase *usecase;
    bool switch_device[AUDIO_USECASE_MAX];
    int i, num_uc_to_switch = 0;
    int backend_check_cond = is_codec_backend_out_device_type(&uc_info->device_list);
    int status = 0;

    bool force_routing = platform_check_and_set_capture_codec_backend_cfg(adev, uc_info,
                         snd_device);
    ALOGD("%s:becf: force routing %d", __func__, force_routing);

    /*
     * Make sure out devices is checked against out codec backend device and
     * also in devices against in codec backend. Checking out device against in
     * codec backend or vice versa causes issues.
     */
    if (uc_info->type == PCM_CAPTURE)
        backend_check_cond = is_codec_backend_in_device_type(&uc_info->device_list);

    /*
     * Island cfg and power mode config needs to set before AFE port start.
     * Set force routing in case of voice device was enable before.
     */

    if (uc_info->type == VOICE_CALL &&
        voice_extn_is_voice_power_mode_supported() &&
        is_supported_conc_usecase_for_power_mode_call(adev) &&
        platform_check_and_update_island_power_status(adev->platform,
                                             uc_info,
                                             snd_device)) {
        force_routing = true;
        ALOGD("%s:becf: force routing %d for power mode supported device",
               __func__, force_routing);
    }

    /*
     * This function is to make sure that all the active capture usecases
     * are always routed to the same input sound device.
     * For example, if audio-record and voice-call usecases are currently
     * active on speaker(rx) and speaker-mic (tx) and out_set_parameters(earpiece)
     * is received for voice call then we have to make sure that audio-record
     * usecase is also switched to earpiece i.e. voice-dmic-ef,
     * because of the limitation that two devices cannot be enabled
     * at the same time if they share the same backend.
     */
    for (i = 0; i < AUDIO_USECASE_MAX; i++)
        switch_device[i] = false;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        /*
         * TODO: Enhance below condition to handle BT sco/USB multi recording
         */

        bool capture_uc_needs_routing = usecase->type != PCM_PLAYBACK && (usecase != uc_info &&
                                       (usecase->in_snd_device != snd_device || force_routing));
        bool call_proxy_snd_device = platform_is_call_proxy_snd_device(snd_device) ||
                                platform_is_call_proxy_snd_device(usecase->in_snd_device);
        if (capture_uc_needs_routing && !call_proxy_snd_device &&
                ((backend_check_cond &&
                 (is_codec_backend_in_device_type(&usecase->device_list) ||
                  (usecase->type == VOIP_CALL))) ||
                ((uc_info->type == VOICE_CALL &&
                 is_single_device_type_equal(&usecase->device_list,
                                            AUDIO_DEVICE_IN_VOICE_CALL)) ||
                 platform_check_backends_match(snd_device,\
                                              usecase->in_snd_device))) &&
                (usecase->id != USECASE_AUDIO_SPKR_CALIB_TX)) {
            ALOGD("%s: Usecase (%s) is active on (%s) - disabling ..",
                  __func__, use_case_table[usecase->id],
                  platform_get_snd_device_name(usecase->in_snd_device));
            disable_audio_route(adev, usecase);
            switch_device[usecase->id] = true;
            num_uc_to_switch++;
        }
    }

    if (num_uc_to_switch) {
        /* All streams have been de-routed. Disable the device */

        /* Make sure the previous devices to be disabled first and then enable the
           selected devices */
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (switch_device[usecase->id]) {
                disable_snd_device(adev, usecase->in_snd_device);
            }
        }

        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (switch_device[usecase->id]) {
                enable_snd_device(adev, snd_device);
            }
        }

        /* Re-route all the usecases on the shared backend other than the
           specified usecase to new snd devices */
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            /* Update the in_snd_device only before enabling the audio route */
            if (switch_device[usecase->id] ) {
                usecase->in_snd_device = snd_device;
                    /* Update voc calibration before enabling Voice/VoIP route */
                if (usecase->type == VOICE_CALL || usecase->type == VOIP_CALL) {
                    snd_device_t voip_snd_device;
                    voip_snd_device = platform_get_output_snd_device(adev->platform,
                                                                     usecase->stream.out,
                                                                     usecase->type);
                    status = platform_switch_voice_call_device_post(adev->platform,
                                                                    voip_snd_device,
                                                                    usecase->in_snd_device);
                }
                enable_audio_route(adev, usecase);
            }
        }
    }
}

static void reset_hdmi_sink_caps(struct stream_out *out) {
    int i = 0;

    for (i = 0; i<= MAX_SUPPORTED_CHANNEL_MASKS; i++) {
        out->supported_channel_masks[i] = 0;
    }
    for (i = 0; i<= MAX_SUPPORTED_FORMATS; i++) {
        out->supported_formats[i] = 0;
    }
    for (i = 0; i<= MAX_SUPPORTED_SAMPLE_RATES; i++) {
        out->supported_sample_rates[i] = 0;
    }
}

/* must be called with hw device mutex locked */
static int read_hdmi_sink_caps(struct stream_out *out)
{
    int ret = 0, i = 0, j = 0;
    int channels = platform_edid_get_max_channels_v2(out->dev->platform,
                                                     out->extconn.cs.controller,
                                                     out->extconn.cs.stream);

    reset_hdmi_sink_caps(out);

    /* Cache ext disp type */
    ret = platform_get_ext_disp_type_v2(adev->platform,
                                      out->extconn.cs.controller,
                                      out->extconn.cs.stream);
    if(ret < 0) {
        ALOGE("%s: Failed to query disp type, ret:%d", __func__, ret);
        return -EINVAL;
    }

    switch (channels) {
    case 8:
        ALOGV("%s: HDMI supports 7.1 channels", __func__);
        out->supported_channel_masks[i++] = AUDIO_CHANNEL_OUT_7POINT1;
        out->supported_channel_masks[i++] = AUDIO_CHANNEL_OUT_6POINT1;
    case 6:
        ALOGV("%s: HDMI supports 5.1 channels", __func__);
        out->supported_channel_masks[i++] = AUDIO_CHANNEL_OUT_5POINT1;
        out->supported_channel_masks[i++] = AUDIO_CHANNEL_OUT_PENTA;
        out->supported_channel_masks[i++] = AUDIO_CHANNEL_OUT_QUAD;
        out->supported_channel_masks[i++] = AUDIO_CHANNEL_OUT_SURROUND;
        out->supported_channel_masks[i++] = AUDIO_CHANNEL_OUT_2POINT1;
        break;
    default:
        ALOGE("invalid/nonstandard channal count[%d]",channels);
        ret = -ENOSYS;
        break;
    }

    // check channel format caps
    i = 0;
    if (platform_is_edid_supported_format_v2(out->dev->platform, AUDIO_FORMAT_AC3,
                                             out->extconn.cs.controller,
                                             out->extconn.cs.stream)) {
        ALOGV(":%s HDMI supports AC3/EAC3 formats", __func__);
        out->supported_formats[i++] = AUDIO_FORMAT_AC3;
        //Adding EAC3/EAC3_JOC formats if AC3 is supported by the sink.
        //EAC3/EAC3_JOC will be converted to AC3 for decoding if needed
        out->supported_formats[i++] = AUDIO_FORMAT_E_AC3;
        out->supported_formats[i++] = AUDIO_FORMAT_E_AC3_JOC;
    }

    if (platform_is_edid_supported_format_v2(out->dev->platform, AUDIO_FORMAT_DOLBY_TRUEHD,
                                             out->extconn.cs.controller,
                                             out->extconn.cs.stream)) {
        ALOGV(":%s HDMI supports TRUE HD format", __func__);
        out->supported_formats[i++] = AUDIO_FORMAT_DOLBY_TRUEHD;
    }

    if (platform_is_edid_supported_format_v2(out->dev->platform, AUDIO_FORMAT_DTS,
                                             out->extconn.cs.controller,
                                             out->extconn.cs.stream)) {
        ALOGV(":%s HDMI supports DTS format", __func__);
        out->supported_formats[i++] = AUDIO_FORMAT_DTS;
    }

    if (platform_is_edid_supported_format_v2(out->dev->platform, AUDIO_FORMAT_DTS_HD,
                                             out->extconn.cs.controller,
                                             out->extconn.cs.stream)) {
        ALOGV(":%s HDMI supports DTS HD format", __func__);
        out->supported_formats[i++] = AUDIO_FORMAT_DTS_HD;
    }

    if (platform_is_edid_supported_format_v2(out->dev->platform, AUDIO_FORMAT_IEC61937,
                                             out->extconn.cs.controller,
                                             out->extconn.cs.stream)) {
        ALOGV(":%s HDMI supports IEC61937 format", __func__);
        out->supported_formats[i++] = AUDIO_FORMAT_IEC61937;
    }


    // check sample rate caps
    i = 0;
    for (j = 0; j < MAX_SUPPORTED_SAMPLE_RATES; j++) {
        if (platform_is_edid_supported_sample_rate_v2(out->dev->platform, out_hdmi_sample_rates[j],
                                                      out->extconn.cs.controller,
                                                      out->extconn.cs.stream)) {
            ALOGV(":%s HDMI supports sample rate:%d", __func__, out_hdmi_sample_rates[j]);
            out->supported_sample_rates[i++] = out_hdmi_sample_rates[j];
        }
    }

    return ret;
}

static inline ssize_t read_usb_sup_sample_rates(bool is_playback __unused,
                                         uint32_t *supported_sample_rates __unused,
                                         uint32_t max_rates __unused)
{
    ssize_t count = audio_extn_usb_get_sup_sample_rates(is_playback,
                                                        supported_sample_rates,
                                                        max_rates);
    ssize_t i = 0;

    for (i=0; i<count; i++) {
        ALOGV("%s %s %d", __func__, is_playback ? "P" : "C",
              supported_sample_rates[i]);
    }
    return count;
}

static inline int read_usb_sup_channel_masks(bool is_playback,
                                      audio_channel_mask_t *supported_channel_masks,
                                      uint32_t max_masks)
{
    int channels = audio_extn_usb_get_max_channels(is_playback);
    int channel_count;
    uint32_t num_masks = 0;
    if (channels > MAX_HIFI_CHANNEL_COUNT)
        channels = MAX_HIFI_CHANNEL_COUNT;

    if (is_playback) {
        // start from 2 channels as framework currently doesn't support mono.
        if (channels >= FCC_2) {
            supported_channel_masks[num_masks++] = audio_channel_out_mask_from_count(FCC_2);
        }
        for (channel_count = FCC_2;
                channel_count <= channels && num_masks < max_masks;
                ++channel_count) {
            supported_channel_masks[num_masks++] =
                    audio_channel_mask_for_index_assignment_from_count(channel_count);
        }
    } else {
        // For capture we report all supported channel masks from 1 channel up.
        channel_count = MIN_CHANNEL_COUNT;
        // audio_channel_in_mask_from_count() does the right conversion to either positional or
        // indexed mask
        for ( ; channel_count <= channels && num_masks < max_masks; channel_count++) {
            audio_channel_mask_t mask = AUDIO_CHANNEL_NONE;
            if (channel_count <= FCC_2) {
                mask = audio_channel_in_mask_from_count(channel_count);
                supported_channel_masks[num_masks++] = mask;
            }
            const audio_channel_mask_t index_mask =
                    audio_channel_mask_for_index_assignment_from_count(channel_count);
            if (mask != index_mask && num_masks < max_masks) { // ensure index mask added.
                supported_channel_masks[num_masks++] = index_mask;
            }
        }
    }

    for (size_t i = 0; i < num_masks; ++i) {
        ALOGV("%s: %s supported ch %d supported_channel_masks[%zu] %08x num_masks %d", __func__,
              is_playback ? "P" : "C", channels, i, supported_channel_masks[i], num_masks);
    }
    return num_masks;
}

static inline int read_usb_sup_formats(bool is_playback __unused,
                                audio_format_t *supported_formats,
                                uint32_t max_formats __unused)
{
    int bitwidth = audio_extn_usb_get_max_bit_width(is_playback);
    switch (bitwidth) {
        case 24:
            // XXX : usb.c returns 24 for s24 and s24_le?
            supported_formats[0] = AUDIO_FORMAT_PCM_24_BIT_PACKED;
            break;
        case 32:
            supported_formats[0] = AUDIO_FORMAT_PCM_32_BIT;
            break;
        case 16:
        default :
            supported_formats[0] = AUDIO_FORMAT_PCM_16_BIT;
            break;
    }
    ALOGV("%s: %s supported format %d", __func__,
          is_playback ? "P" : "C", bitwidth);
    return 1;
}

static inline int read_usb_sup_params_and_compare(bool is_playback,
                                           audio_format_t *format,
                                           audio_format_t *supported_formats,
                                           uint32_t max_formats,
                                           audio_channel_mask_t *mask,
                                           audio_channel_mask_t *supported_channel_masks,
                                           uint32_t max_masks,
                                           uint32_t *rate,
                                           uint32_t *supported_sample_rates,
                                           uint32_t max_rates) {
    int ret = 0;
    int num_formats;
    int num_masks;
    int num_rates;
    int i;

    num_formats = read_usb_sup_formats(is_playback, supported_formats,
                                       max_formats);
    num_masks = read_usb_sup_channel_masks(is_playback, supported_channel_masks,
                                           max_masks);

    num_rates = read_usb_sup_sample_rates(is_playback,
                                          supported_sample_rates, max_rates);

#define LUT(table, len, what, dflt)                  \
    for (i=0; i<len && (table[i] != what); i++);    \
    if (i==len) { ret |= (what == dflt ? 0 : -1); what=table[0]; }

    LUT(supported_formats, num_formats, *format, AUDIO_FORMAT_DEFAULT);
    LUT(supported_channel_masks, num_masks, *mask, AUDIO_CHANNEL_NONE);
    LUT(supported_sample_rates, num_rates, *rate, 0);

#undef LUT
    return ret < 0 ? -EINVAL : 0; // HACK TBD
}

audio_usecase_t get_usecase_id_from_usecase_type(const struct audio_device *adev,
                                                 usecase_type_t type)
{
    struct audio_usecase *usecase;
    struct listnode *node;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == type) {
            ALOGV("%s: usecase id %d", __func__, usecase->id);
            return usecase->id;
        }
    }
    return USECASE_INVALID;
}

struct audio_usecase *get_usecase_from_list(const struct audio_device *adev,
                                            audio_usecase_t uc_id)
{
    struct audio_usecase *usecase;
    struct listnode *node;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->id == uc_id)
            return usecase;
    }
    return NULL;
}

/*
 * is a true native playback active
 */
bool audio_is_true_native_stream_active(struct audio_device *adev)
{
    bool active = false;
    int i = 0;
    struct listnode *node;

    if (NATIVE_AUDIO_MODE_TRUE_44_1 != platform_get_native_support()) {
        ALOGV("%s:napb: not in true mode or non hdphones device",
               __func__);
        active = false;
        goto exit;
    }

    list_for_each(node, &adev->usecase_list) {
        struct audio_usecase *uc;
        uc = node_to_item(node, struct audio_usecase, list);
        struct stream_out *curr_out =
            (struct stream_out*) uc->stream.out;

        if (curr_out && PCM_PLAYBACK == uc->type) {
            ALOGD("%s:napb: (%d) (%s)id (%d) sr %d bw "
                  "(%d) device %s", __func__, i++, use_case_table[uc->id],
                  uc->id, curr_out->sample_rate,
                  curr_out->bit_width,
                  platform_get_snd_device_name(uc->out_snd_device));

            if (is_offload_usecase(uc->id) &&
                (curr_out->sample_rate == OUTPUT_SAMPLING_RATE_44100)) {
                active = true;
                ALOGD("%s:napb:native stream detected", __func__);
            }
        }
    }
exit:
    return active;
}

uint32_t adev_get_dsp_bit_width_enforce_mode()
{
    if (adev == NULL) {
        ALOGE("%s: adev is null. Disable DSP bit width enforce mode.\n", __func__);
        return 0;
    }
    return adev->dsp_bit_width_enforce_mode;
}

static uint32_t adev_init_dsp_bit_width_enforce_mode(struct mixer *mixer)
{
    char value[PROPERTY_VALUE_MAX];
    int trial;
    uint32_t dsp_bit_width_enforce_mode = 0;

    if (!mixer) {
        ALOGE("%s: adev mixer is null. cannot update DSP bitwidth.\n",
                __func__);
        return 0;
    }

    if (property_get("persist.vendor.audio_hal.dsp_bit_width_enforce_mode",
                        value, NULL) > 0) {
        trial = atoi(value);
        switch (trial) {
        case 16:
            dsp_bit_width_enforce_mode = 16;
            break;
        case 24:
            dsp_bit_width_enforce_mode = 24;
            break;
        case 32:
            dsp_bit_width_enforce_mode = 32;
            break;
       default:
            dsp_bit_width_enforce_mode = 0;
            ALOGD("%s Dynamic DSP bitwidth config is disabled.", __func__);
            break;
        }
    }

    return dsp_bit_width_enforce_mode;
}

static void audio_enable_asm_bit_width_enforce_mode(struct mixer *mixer,
                                                uint32_t enforce_mode,
                                                bool enable)
{
    struct mixer_ctl *ctl = NULL;
    const char *mixer_ctl_name = "ASM Bit Width";
    uint32_t asm_bit_width_mode = 0;

    if (enforce_mode == 0) {
        ALOGD("%s: DSP bitwidth feature is disabled.", __func__);
        return;
    }

    ctl = mixer_get_ctl_by_name(mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
                __func__, mixer_ctl_name);
        return;
    }

    if (enable)
        asm_bit_width_mode = enforce_mode;
    else
        asm_bit_width_mode = 0;

    ALOGV("%s DSP bit width feature status is %d width=%d",
        __func__, enable, asm_bit_width_mode);
    if (mixer_ctl_set_value(ctl, 0, asm_bit_width_mode) < 0)
        ALOGE("%s: Could not set ASM biwidth %d", __func__,
                asm_bit_width_mode);

    return;
}

/*
 * if native DSD playback active
 */
bool audio_is_dsd_native_stream_active(struct audio_device *adev)
{
    bool active = false;
    struct listnode *node = NULL;
    struct audio_usecase *uc = NULL;
    struct stream_out *curr_out = NULL;

    list_for_each(node, &adev->usecase_list) {
        uc = node_to_item(node, struct audio_usecase, list);
        curr_out = (struct stream_out*) uc->stream.out;

        if (curr_out && PCM_PLAYBACK == uc->type &&
               (DSD_NATIVE_BACKEND == platform_get_backend_index(uc->out_snd_device))) {
            active = true;
            ALOGV("%s:DSD playback is active", __func__);
            break;
        }
    }
    return active;
}

static bool force_device_switch(struct audio_usecase *usecase)
{
    bool ret = false;
    bool is_it_true_mode = false;

    if (usecase->type == PCM_CAPTURE ||
        usecase->type == TRANSCODE_LOOPBACK_RX ||
        usecase->type == TRANSCODE_LOOPBACK_TX) {
        return false;
    }

    if(usecase->stream.out == NULL) {
        ALOGE("%s: stream.out is NULL", __func__);
        return false;
    }

    if (is_offload_usecase(usecase->id) &&
        (usecase->stream.out->sample_rate == OUTPUT_SAMPLING_RATE_44100) &&
        (compare_device_type(&usecase->stream.out->device_list, AUDIO_DEVICE_OUT_WIRED_HEADSET) ||
         compare_device_type(&usecase->stream.out->device_list, AUDIO_DEVICE_OUT_WIRED_HEADPHONE))) {
        is_it_true_mode = (NATIVE_AUDIO_MODE_TRUE_44_1 == platform_get_native_support()? true : false);
         if ((is_it_true_mode && !adev->native_playback_enabled) ||
             (!is_it_true_mode && adev->native_playback_enabled)){
            ret = true;
            ALOGD("napb: time to toggle native mode");
        }
    }

    // Force all a2dp output devices to reconfigure for proper AFE encode format
    //Also handle a case where in earlier a2dp start failed as A2DP stream was
    //in suspended state, hence try to trigger a retry when we again get a routing request.
    if(is_a2dp_out_device_type(&usecase->stream.out->device_list) &&
        audio_extn_a2dp_is_force_device_switch()) {
         ALOGD("Force a2dp device switch to update new encoder config");
         ret = true;
    }

    if (usecase->stream.out->stream_config_changed) {
         ALOGD("Force stream_config_changed to update iec61937 transmission config");
         return true;
    }
    return ret;
}

static void stream_app_type_cfg_init(struct stream_app_type_cfg *cfg)
{
    cfg->gain[0] = cfg->gain[1] = APP_TYPE_GAIN_DEFAULT;
}

bool is_btsco_device(snd_device_t out_snd_device, snd_device_t in_snd_device)
{
   bool ret=false;
   if ((out_snd_device == SND_DEVICE_OUT_BT_SCO ||
        out_snd_device == SND_DEVICE_OUT_BT_SCO_WB ||
        out_snd_device == SND_DEVICE_OUT_BT_SCO_SWB) ||
        in_snd_device == SND_DEVICE_IN_BT_SCO_MIC_WB_NREC ||
        in_snd_device == SND_DEVICE_IN_BT_SCO_MIC_WB ||
        in_snd_device == SND_DEVICE_IN_BT_SCO_MIC_SWB ||
        in_snd_device == SND_DEVICE_IN_BT_SCO_MIC_NREC ||
        in_snd_device == SND_DEVICE_IN_BT_SCO_MIC ||
        in_snd_device == SND_DEVICE_IN_BT_SCO_MIC_SWB_NREC)
        ret = true;

   return ret;
}

bool is_a2dp_device(snd_device_t out_snd_device)
{
   bool ret=false;
   if (out_snd_device == SND_DEVICE_OUT_BT_A2DP)
        ret = true;

   return ret;
}

bool is_bt_soc_on(struct audio_device *adev)
{
    struct mixer_ctl *ctl;
    char *mixer_ctl_name = "BT SOC status";
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    bool bt_soc_status = true;
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        /*This is to ensure we dont break targets which dont have the kernel change*/
        return true;
    }
    bt_soc_status = mixer_ctl_get_value(ctl, 0);
    ALOGD("BT SOC status: %d",bt_soc_status);
    return bt_soc_status;
}

static int configure_btsco_sample_rate(snd_device_t snd_device)
{
    struct mixer_ctl *ctl = NULL;
    struct mixer_ctl *ctl_sr_rx = NULL, *ctl_sr_tx = NULL, *ctl_sr = NULL;
    char *rate_str = NULL;
    bool is_rx_dev = true;

    if (is_btsco_device(snd_device, snd_device)) {
        ctl_sr_tx = mixer_get_ctl_by_name(adev->mixer, "BT SampleRate TX");
        ctl_sr_rx = mixer_get_ctl_by_name(adev->mixer, "BT SampleRate RX");
        if (!ctl_sr_tx || !ctl_sr_rx) {
            ctl_sr = mixer_get_ctl_by_name(adev->mixer, "BT SampleRate");
            if (!ctl_sr)
                return -ENOSYS;
        }

        switch (snd_device) {
        case SND_DEVICE_OUT_BT_SCO:
            rate_str = "KHZ_8";
            break;
        case SND_DEVICE_IN_BT_SCO_MIC_NREC:
        case SND_DEVICE_IN_BT_SCO_MIC:
            rate_str = "KHZ_8";
            is_rx_dev = false;
            break;
        case SND_DEVICE_OUT_BT_SCO_WB:
            rate_str = "KHZ_16";
            break;
        case SND_DEVICE_IN_BT_SCO_MIC_WB_NREC:
        case SND_DEVICE_IN_BT_SCO_MIC_WB:
            rate_str = "KHZ_16";
            is_rx_dev = false;
            break;
        default:
            return 0;
        }

        ctl = (ctl_sr == NULL) ? (is_rx_dev ? ctl_sr_rx : ctl_sr_tx) : ctl_sr;
        if (mixer_ctl_set_enum_by_string(ctl, rate_str) != 0)
            return -ENOSYS;
    }
    return 0;
}

int out_standby_l(struct audio_stream *stream);

struct stream_in *adev_get_active_input(const struct audio_device *adev)
{
    struct listnode *node;
    struct stream_in *last_active_in = NULL;

    /* Get last added active input.
     * TODO: We may use a priority mechanism to pick highest priority active source */
    list_for_each(node, &adev->usecase_list)
    {
        struct audio_usecase *usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == PCM_CAPTURE && usecase->stream.in != NULL)
            last_active_in =  usecase->stream.in;
    }

    return last_active_in;
}

struct stream_in *get_voice_communication_input(const struct audio_device *adev)
{
    struct listnode *node;

    /* First check active inputs with voice communication source and then
     * any input if audio mode is in communication */
    list_for_each(node, &adev->usecase_list)
    {
        struct audio_usecase *usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == PCM_CAPTURE && usecase->stream.in != NULL &&
            usecase->stream.in->source == AUDIO_SOURCE_VOICE_COMMUNICATION)
            return usecase->stream.in;
    }
    if (adev->mode == AUDIO_MODE_IN_COMMUNICATION)
        return adev_get_active_input(adev);

    return NULL;
}

/*
 * Aligned with policy.h
 */
static inline int source_priority(int inputSource)
{
    switch (inputSource) {
    case AUDIO_SOURCE_VOICE_COMMUNICATION:
        return 9;
    case AUDIO_SOURCE_CAMCORDER:
        return 8;
    case AUDIO_SOURCE_VOICE_PERFORMANCE:
        return 7;
    case AUDIO_SOURCE_UNPROCESSED:
        return 6;
    case AUDIO_SOURCE_MIC:
        return 5;
    case AUDIO_SOURCE_ECHO_REFERENCE:
        return 4;
    case AUDIO_SOURCE_FM_TUNER:
        return 3;
    case AUDIO_SOURCE_VOICE_RECOGNITION:
        return 2;
    case AUDIO_SOURCE_HOTWORD:
        return 1;
    default:
        break;
    }
    return 0;
}

static struct stream_in *get_priority_input(struct audio_device *adev)
{
    struct listnode *node;
    struct audio_usecase *usecase;
    int last_priority = 0, priority;
    struct stream_in *priority_in = NULL;
    struct stream_in *in;

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->type == PCM_CAPTURE) {
            in = usecase->stream.in;
            if (!in)
                continue;
            priority = source_priority(in->source);

            if (priority > last_priority) {
                last_priority = priority;
                priority_in = in;
            }
        }
    }
    return priority_in;
}

int select_devices(struct audio_device *adev, audio_usecase_t uc_id)
{
    snd_device_t out_snd_device = SND_DEVICE_NONE;
    snd_device_t in_snd_device = SND_DEVICE_NONE;
    struct audio_usecase *usecase = NULL;
    struct audio_usecase *vc_usecase = NULL;
    struct audio_usecase *voip_usecase = NULL;
    struct audio_usecase *hfp_usecase = NULL;
    struct stream_out stream_out;
    audio_usecase_t hfp_ucid;
    int status = 0;

    ALOGD("%s for use case (%s)", __func__, use_case_table[uc_id]);

    usecase = get_usecase_from_list(adev, uc_id);
    if (usecase == NULL) {
        ALOGE("%s: Could not find the usecase(%d)", __func__, uc_id);
        return -EINVAL;
    }

    if ((usecase->type == VOICE_CALL) ||
        (usecase->type == VOIP_CALL)  ||
        (usecase->type == PCM_HFP_CALL)||
        (usecase->type == ICC_CALL) ||
        (usecase->type == SYNTH_LOOPBACK)) {
        if(usecase->stream.out == NULL) {
            ALOGE("%s: stream.out is NULL", __func__);
            return -EINVAL;
        }
        if (compare_device_type(&usecase->device_list, AUDIO_DEVICE_OUT_BUS)) {
            out_snd_device = audio_extn_auto_hal_get_output_snd_device(adev,
                                                                       uc_id);
            in_snd_device = audio_extn_auto_hal_get_input_snd_device(adev,
                                                                     uc_id);
        } else {
            out_snd_device = platform_get_output_snd_device(adev->platform,
                                                            usecase->stream.out, usecase->type);
            in_snd_device = platform_get_input_snd_device(adev->platform,
                                                          NULL,
                                                          &usecase->stream.out->device_list,
                                                          usecase->type);
        }
        assign_devices(&usecase->device_list, &usecase->stream.out->device_list);
    } else if (usecase->type == TRANSCODE_LOOPBACK_RX) {
        if (usecase->stream.inout == NULL) {
            ALOGE("%s: stream.inout is NULL", __func__);
            return -EINVAL;
        }
        assign_devices(&stream_out.device_list, &usecase->stream.inout->out_config.device_list);
        stream_out.sample_rate = usecase->stream.inout->out_config.sample_rate;
        stream_out.format = usecase->stream.inout->out_config.format;
        stream_out.channel_mask = usecase->stream.inout->out_config.channel_mask;
        out_snd_device = platform_get_output_snd_device(adev->platform, &stream_out, usecase->type);
        assign_devices(&usecase->device_list,
                       &usecase->stream.inout->out_config.device_list);
    } else if (usecase->type == TRANSCODE_LOOPBACK_TX ) {
        if (usecase->stream.inout == NULL) {
            ALOGE("%s: stream.inout is NULL", __func__);
            return -EINVAL;
        }
        struct listnode out_devices;
        list_init(&out_devices);
        in_snd_device = platform_get_input_snd_device(adev->platform, NULL,
                                                      &out_devices, usecase->type);
        assign_devices(&usecase->device_list,
                       &usecase->stream.inout->in_config.device_list);
    } else {
        /*
         * If the voice call is active, use the sound devices of voice call usecase
         * so that it would not result any device switch. All the usecases will
         * be switched to new device when select_devices() is called for voice call
         * usecase. This is to avoid switching devices for voice call when
         * check_usecases_codec_backend() is called below.
         * choose voice call device only if the use case device is
         * also using the codec backend
         */
        if (voice_is_in_call(adev) && adev->mode != AUDIO_MODE_NORMAL) {
            vc_usecase = get_usecase_from_list(adev,
                                               get_usecase_id_from_usecase_type(adev, VOICE_CALL));
            if ((vc_usecase) && ((is_codec_backend_out_device_type(&vc_usecase->device_list) &&
                                 is_codec_backend_out_device_type(&usecase->device_list)) ||
                                 (is_codec_backend_out_device_type(&vc_usecase->device_list) &&
                                 is_codec_backend_in_device_type(&usecase->device_list)) ||
                                 is_single_device_type_equal(&vc_usecase->device_list,
                                                        AUDIO_DEVICE_OUT_HEARING_AID) ||
                                 is_single_device_type_equal(&usecase->device_list,
                                                     AUDIO_DEVICE_IN_VOICE_CALL) ||
                                 (is_single_device_type_equal(&usecase->device_list,
                                                     AUDIO_DEVICE_IN_USB_HEADSET) &&
                                 is_single_device_type_equal(&vc_usecase->device_list,
                                                        AUDIO_DEVICE_OUT_USB_HEADSET))||
                                 (is_single_device_type_equal(&usecase->device_list,
                                                     AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) &&
                                 is_codec_backend_out_device_type(&vc_usecase->device_list)))) {
                in_snd_device = vc_usecase->in_snd_device;
                out_snd_device = vc_usecase->out_snd_device;
            }
        } else if (voice_extn_compress_voip_is_active(adev)) {
            bool out_snd_device_backend_match = true;
            voip_usecase = get_usecase_from_list(adev, USECASE_COMPRESS_VOIP_CALL);
            if ((voip_usecase != NULL) &&
                (usecase->type == PCM_PLAYBACK) &&
                (usecase->stream.out != NULL)) {
                out_snd_device_backend_match = platform_check_backends_match(
                                                   voip_usecase->out_snd_device,
                                                   platform_get_output_snd_device(
                                                       adev->platform,
                                                       usecase->stream.out, usecase->type));
            }
            if ((voip_usecase) && (is_codec_backend_out_device_type(&voip_usecase->device_list) &&
                (is_codec_backend_out_device_type(&usecase->device_list) ||
                 is_codec_backend_in_device_type(&usecase->device_list)) &&
                out_snd_device_backend_match &&
                 (voip_usecase->stream.out != adev->primary_output))) {
                    in_snd_device = voip_usecase->in_snd_device;
                    out_snd_device = voip_usecase->out_snd_device;
            }
        } else if (audio_extn_hfp_is_active(adev)) {
            hfp_ucid = audio_extn_hfp_get_usecase();
            hfp_usecase = get_usecase_from_list(adev, hfp_ucid);
            if ((hfp_usecase) && is_codec_backend_out_device_type(&hfp_usecase->device_list)) {
                   in_snd_device = hfp_usecase->in_snd_device;
                   out_snd_device = hfp_usecase->out_snd_device;
            }
        }
        if (usecase->type == PCM_PLAYBACK) {
            if (usecase->stream.out == NULL) {
                ALOGE("%s: stream.out is NULL", __func__);
                return -EINVAL;
            }
            assign_devices(&usecase->device_list, &usecase->stream.out->device_list);
            in_snd_device = SND_DEVICE_NONE;
            if (out_snd_device == SND_DEVICE_NONE) {
                struct stream_out *voip_out = adev->primary_output;
                struct stream_in *voip_in = get_voice_communication_input(adev);
                if (compare_device_type(&usecase->device_list, AUDIO_DEVICE_OUT_BUS))
                    out_snd_device = audio_extn_auto_hal_get_output_snd_device(adev, uc_id);
                else
                    out_snd_device = platform_get_output_snd_device(adev->platform,
                                                                    usecase->stream.out,
                                                                    usecase->type);
                voip_usecase = get_usecase_from_list(adev, USECASE_AUDIO_PLAYBACK_VOIP);

                if (voip_usecase)
                    voip_out = voip_usecase->stream.out;

                if (usecase->stream.out == voip_out && voip_in != NULL)
                    select_devices(adev, voip_in->usecase);
            }
        } else if (usecase->type == PCM_CAPTURE) {
            if (usecase->stream.in == NULL) {
                ALOGE("%s: stream.in is NULL", __func__);
                return -EINVAL;
            }
            assign_devices(&usecase->device_list, &usecase->stream.in->device_list);
            out_snd_device = SND_DEVICE_NONE;
            if (in_snd_device == SND_DEVICE_NONE) {
                struct listnode out_devices;
                struct stream_in *voip_in = get_voice_communication_input(adev);
                struct stream_in *priority_in = NULL;

                list_init(&out_devices);
                if (voip_in != NULL) {
                    struct audio_usecase *voip_usecase = get_usecase_from_list(adev,
                                                             USECASE_AUDIO_PLAYBACK_VOIP);

                    usecase->stream.in->enable_ec_port = false;

                    bool is_ha_usecase = adev->ha_proxy_enable ?
                        usecase->id == USECASE_AUDIO_RECORD_AFE_PROXY2 :
                        usecase->id == USECASE_AUDIO_RECORD_AFE_PROXY;
                    if (is_ha_usecase) {
                        reassign_device_list(&out_devices, AUDIO_DEVICE_OUT_TELEPHONY_TX, "");
                    } else if (voip_usecase) {
                        assign_devices(&out_devices, &voip_usecase->stream.out->device_list);
                    } else if (adev->primary_output &&
                                  !adev->primary_output->standby) {
                        assign_devices(&out_devices, &adev->primary_output->device_list);
                    } else {
                        /* forcing speaker o/p device to get matching i/p pair
                           in case o/p is not routed from same primary HAL */
                        reassign_device_list(&out_devices, AUDIO_DEVICE_OUT_SPEAKER, "");
                    }
                    priority_in = voip_in;
                } else {
                    /* get the input with the highest priority source*/
                    priority_in = get_priority_input(adev);

                    if (!priority_in ||
                            audio_extn_auto_hal_overwrite_priority_for_auto(usecase->stream.in))
                        priority_in = usecase->stream.in;
                }
                if (compare_device_type(&usecase->device_list, AUDIO_DEVICE_IN_BUS)){
                    in_snd_device = audio_extn_auto_hal_get_snd_device_for_car_audio_stream(priority_in->car_audio_stream);
                }
                else
                    in_snd_device = platform_get_input_snd_device(adev->platform,
                                                                  priority_in,
                                                                  &out_devices,
                                                                  usecase->type);
            }
        }
    }

    if (out_snd_device == usecase->out_snd_device &&
        in_snd_device == usecase->in_snd_device) {

        if (!force_device_switch(usecase))
            return 0;
    }

    if (!compare_device_type(&usecase->device_list, AUDIO_DEVICE_OUT_BUS) &&
        ((is_btsco_device(out_snd_device,in_snd_device) && !adev->bt_sco_on) ||
         (is_a2dp_device(out_snd_device) && !audio_extn_a2dp_source_is_ready()))) {
        ALOGD("SCO/A2DP is selected but they are not connected/ready hence dont route");
        return 0;
    }

    if (out_snd_device != SND_DEVICE_NONE &&
            out_snd_device != adev->last_logged_snd_device[uc_id][0]) {
        ALOGD("%s: changing use case %s output device from(%d: %s, acdb %d) to (%d: %s, acdb %d)",
              __func__,
              use_case_table[uc_id],
              adev->last_logged_snd_device[uc_id][0],
              platform_get_snd_device_name(adev->last_logged_snd_device[uc_id][0]),
              adev->last_logged_snd_device[uc_id][0] != SND_DEVICE_NONE ?
                      platform_get_snd_device_acdb_id(adev->last_logged_snd_device[uc_id][0]) :
                      -1,
              out_snd_device,
              platform_get_snd_device_name(out_snd_device),
              platform_get_snd_device_acdb_id(out_snd_device));
        adev->last_logged_snd_device[uc_id][0] = out_snd_device;
    }
    if (in_snd_device != SND_DEVICE_NONE &&
            in_snd_device != adev->last_logged_snd_device[uc_id][1]) {
        ALOGD("%s: changing use case %s input device from(%d: %s, acdb %d) to (%d: %s, acdb %d)",
              __func__,
              use_case_table[uc_id],
              adev->last_logged_snd_device[uc_id][1],
              platform_get_snd_device_name(adev->last_logged_snd_device[uc_id][1]),
              adev->last_logged_snd_device[uc_id][1] != SND_DEVICE_NONE ?
                      platform_get_snd_device_acdb_id(adev->last_logged_snd_device[uc_id][1]) :
                      -1,
              in_snd_device,
              platform_get_snd_device_name(in_snd_device),
              platform_get_snd_device_acdb_id(in_snd_device));
        adev->last_logged_snd_device[uc_id][1] = in_snd_device;
    }


    /*
     * Limitation: While in call, to do a device switch we need to disable
     * and enable both RX and TX devices though one of them is same as current
     * device.
     */
    if ((usecase->type == VOICE_CALL) &&
        (usecase->in_snd_device != SND_DEVICE_NONE) &&
        (usecase->out_snd_device != SND_DEVICE_NONE)) {
        status = platform_switch_voice_call_device_pre(adev->platform);
    }

    if (((usecase->type == VOICE_CALL) ||
         (usecase->type == VOIP_CALL)) &&
        (usecase->out_snd_device != SND_DEVICE_NONE)) {
        /* Disable sidetone only if voice/voip call already exists */
        if (voice_is_call_state_active_in_call(adev) ||
            voice_extn_compress_voip_is_started(adev))
            voice_set_sidetone(adev, usecase->out_snd_device, false);

        /* Disable aanc only if voice call exists */
        if (voice_is_call_state_active_in_call(adev))
            voice_check_and_update_aanc_path(adev, usecase->out_snd_device, false);
    }

    if ((out_snd_device == SND_DEVICE_OUT_SPEAKER_AND_BT_A2DP ||
         out_snd_device == SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_A2DP) &&
        (!audio_extn_a2dp_source_is_ready())) {
        ALOGW("%s: A2DP profile is not ready, routing to speaker only", __func__);
        if (out_snd_device == SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_A2DP)
            out_snd_device = SND_DEVICE_OUT_SPEAKER_SAFE;
        else
            out_snd_device = SND_DEVICE_OUT_SPEAKER;
    }

    /* Disable current sound devices */
    if (usecase->out_snd_device != SND_DEVICE_NONE) {
        disable_audio_route(adev, usecase);
        disable_snd_device(adev, usecase->out_snd_device);
    }

    if (usecase->in_snd_device != SND_DEVICE_NONE) {
        disable_audio_route(adev, usecase);
        disable_snd_device(adev, usecase->in_snd_device);
    }

    /* Applicable only on the targets that has external modem.
     * New device information should be sent to modem before enabling
     * the devices to reduce in-call device switch time.
     */
    if ((usecase->type == VOICE_CALL) &&
        (usecase->in_snd_device != SND_DEVICE_NONE) &&
        (usecase->out_snd_device != SND_DEVICE_NONE)) {
        status = platform_switch_voice_call_enable_device_config(adev->platform,
                                                                 out_snd_device,
                                                                 in_snd_device);
    }

    /* Enable new sound devices */
    if (out_snd_device != SND_DEVICE_NONE) {
        check_usecases_codec_backend(adev, usecase, out_snd_device);
        check_and_configure_headphone(adev, usecase, out_snd_device);
        if (platform_check_codec_asrc_support(adev->platform))
            check_and_set_asrc_mode(adev, usecase, out_snd_device);
        enable_snd_device(adev, out_snd_device);
        /* Enable haptics device for haptic usecase */
        if (usecase->id == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS)
            enable_snd_device(adev, SND_DEVICE_OUT_HAPTICS);
    }

    if (in_snd_device != SND_DEVICE_NONE) {
        check_usecases_capture_codec_backend(adev, usecase, in_snd_device);
        enable_snd_device(adev, in_snd_device);
    }

    if (usecase->type == VOICE_CALL || usecase->type == VOIP_CALL)
        status = platform_switch_voice_call_device_post(adev->platform,
                                                        out_snd_device,
                                                        in_snd_device);

    usecase->in_snd_device = in_snd_device;
    usecase->out_snd_device = out_snd_device;

    audio_extn_utils_update_stream_app_type_cfg_for_usecase(adev,
                                                            usecase);
    if (usecase->type == PCM_PLAYBACK) {
        if ((24 == usecase->stream.out->bit_width) &&
                compare_device_type(&usecase->stream.out->device_list, AUDIO_DEVICE_OUT_SPEAKER)) {
            usecase->stream.out->app_type_cfg.sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
        } else if ((out_snd_device == SND_DEVICE_OUT_HDMI ||
                    out_snd_device == SND_DEVICE_OUT_USB_HEADSET ||
                    out_snd_device == SND_DEVICE_OUT_DISPLAY_PORT) &&
                   (usecase->stream.out->sample_rate >= OUTPUT_SAMPLING_RATE_44100)) {
            /*
             * To best utlize DSP, check if the stream sample rate is supported/multiple of
             * configured device sample rate, if not update the COPP rate to be equal to the
             * device sample rate, else open COPP at stream sample rate
             */
            platform_check_and_update_copp_sample_rate(adev->platform, out_snd_device,
                    usecase->stream.out->sample_rate,
                    &usecase->stream.out->app_type_cfg.sample_rate);
        } else if (((out_snd_device != SND_DEVICE_OUT_HEADPHONES_44_1 &&
                     out_snd_device != SND_DEVICE_OUT_HEADPHONES &&
                     out_snd_device != SND_DEVICE_OUT_HEADPHONES_HIFI_FILTER &&
                     !audio_is_true_native_stream_active(adev)) &&
                    usecase->stream.out->sample_rate == OUTPUT_SAMPLING_RATE_44100) ||
                    (usecase->stream.out->sample_rate < OUTPUT_SAMPLING_RATE_44100)) {
            usecase->stream.out->app_type_cfg.sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
        }
    }
    enable_audio_route(adev, usecase);

    if (uc_id == USECASE_AUDIO_PLAYBACK_VOIP) {
        struct stream_in *voip_in = get_voice_communication_input(adev);
        struct audio_usecase *voip_in_usecase = NULL;
        voip_in_usecase = get_usecase_from_list(adev, USECASE_AUDIO_RECORD_VOIP);
        if (voip_in != NULL &&
            voip_in_usecase != NULL &&
            !(out_snd_device == AUDIO_DEVICE_OUT_SPEAKER ||
              out_snd_device == AUDIO_DEVICE_OUT_SPEAKER_SAFE) &&
            (voip_in_usecase->in_snd_device ==
            platform_get_input_snd_device(adev->platform, voip_in,
                    &usecase->stream.out->device_list,usecase->type))) {
            /*
             * if VOIP TX is enabled before VOIP RX, needs to re-route the TX path
             * for enabling echo-reference-voip with correct port
             */
            ALOGD("%s: VOIP TX is enabled before VOIP RX,needs to re-route the TX path",__func__);
            disable_audio_route(adev, voip_in_usecase);
            disable_snd_device(adev, voip_in_usecase->in_snd_device);
            enable_snd_device(adev, voip_in_usecase->in_snd_device);
            enable_audio_route(adev, voip_in_usecase);
        }
    }


    audio_extn_qdsp_set_device(usecase);

    /* If input stream is already running then effect needs to be
       applied on the new input device that's being enabled here.  */
    if (in_snd_device != SND_DEVICE_NONE)
        check_and_enable_effect(adev);

    if (usecase->type == VOICE_CALL || usecase->type == VOIP_CALL) {
        /* Enable aanc only if voice call exists */
        if (voice_is_call_state_active_in_call(adev))
            voice_check_and_update_aanc_path(adev, out_snd_device, true);

        /* Enable sidetone only if other voice/voip call already exists */
        if (voice_is_call_state_active_in_call(adev) ||
            voice_extn_compress_voip_is_started(adev))
            voice_set_sidetone(adev, out_snd_device, true);
    }

    /* Applicable only on the targets that has external modem.
     * Enable device command should be sent to modem only after
     * enabling voice call mixer controls
     */
    if (usecase->type == VOICE_CALL)
        status = platform_switch_voice_call_usecase_route_post(adev->platform,
                                                               out_snd_device,
                                                               in_snd_device);

    if (is_btsco_device(out_snd_device, in_snd_device) || is_a2dp_device(out_snd_device)) {
         struct stream_in *in = adev_get_active_input(adev);
         if (usecase->type == VOIP_CALL) {
             if (in != NULL && !in->standby) {
                 if (is_bt_soc_on(adev) == false){
                      ALOGD("BT SCO MIC disconnected while in connection");
                      if (in->pcm != NULL)
                          pcm_stop(in->pcm);
                 }
             }
             if ((usecase->stream.out != NULL) && (usecase->stream.out != adev->primary_output)
                  && usecase->stream.out->started) {
                  if (is_bt_soc_on(adev) == false) {
                      ALOGD("BT SCO/A2DP disconnected while in connection");
                      out_standby_l(&usecase->stream.out->stream.common);
                  }
             }
         } else if ((usecase->stream.out != NULL) &&
              !(usecase->stream.out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) &&
              (usecase->type != TRANSCODE_LOOPBACK_TX) &&
              (usecase->type != TRANSCODE_LOOPBACK_RX) &&
              (usecase->type != PCM_CAPTURE) &&
              usecase->stream.out->started) {
              if (is_bt_soc_on(adev) == false) {
                  ALOGD("BT SCO/A2dp disconnected while in connection");
                  out_standby_l(&usecase->stream.out->stream.common);
              }
         }
    }

    if (usecase->type != PCM_CAPTURE && usecase == voip_usecase) {
        struct stream_out *voip_out = voip_usecase->stream.out;
        audio_extn_utils_send_app_type_gain(adev,
                                            voip_out->app_type_cfg.app_type,
                                            &voip_out->app_type_cfg.gain[0]);
    }

    ALOGD("%s: done",__func__);

    return status;
}

static int stop_input_stream(struct stream_in *in)
{
    int ret = 0;
    struct audio_usecase *uc_info;

    if (in == NULL) {
        ALOGE("%s: stream_in ptr is NULL", __func__);
        return -EINVAL;
    }

    struct audio_device *adev = in->dev;
    struct stream_in *priority_in = NULL;

    ALOGV("%s: enter: usecase(%d: %s)", __func__,
          in->usecase, use_case_table[in->usecase]);
    uc_info = get_usecase_from_list(adev, in->usecase);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
              __func__, in->usecase);
        return -EINVAL;
    }

    priority_in = get_priority_input(adev);

    if (audio_extn_ext_hw_plugin_usecase_stop(adev->ext_hw_plugin, uc_info))
        ALOGE("%s: failed to stop ext hw plugin", __func__);

    /* Close in-call recording streams */
    voice_check_and_stop_incall_rec_usecase(adev, in);

    /* 1. Disable stream specific mixer controls */
    disable_audio_route(adev, uc_info);

    /* 2. Disable the tx device */
    disable_snd_device(adev, uc_info->in_snd_device);

    if (is_loopback_input_device(get_device_types(&in->device_list)))
        audio_extn_keep_alive_stop(KEEP_ALIVE_OUT_PRIMARY);

    list_remove(&uc_info->list);
    free(uc_info);

    if (priority_in == in) {
        priority_in = get_priority_input(adev);
        if (priority_in) {
            if (is_usb_in_device_type(&priority_in->device_list)) {
                if (audio_extn_usb_connected(NULL))
                    select_devices(adev, priority_in->usecase);
            } else {
                select_devices(adev, priority_in->usecase);
            }
        }
    }

    enable_gcov();
    ALOGV("%s: exit: status(%d)", __func__, ret);
    return ret;
}

int start_input_stream(struct stream_in *in)
{
    /* 1. Enable output device and stream routing controls */
    int ret = 0;
    struct audio_usecase *uc_info;

    if (in == NULL) {
        ALOGE("%s: stream_in ptr is NULL", __func__);
        return -EINVAL;
    }

    struct audio_device *adev = in->dev;
    struct pcm_config config = in->config;
    int usecase = platform_update_usecase_from_source(in->source,in->usecase);

    if (get_usecase_from_list(adev, usecase) == NULL)
        in->usecase = usecase;
    ALOGD("%s: enter: stream(%p)usecase(%d: %s)",
          __func__, &in->stream, in->usecase, use_case_table[in->usecase]);

    if (CARD_STATUS_OFFLINE == in->card_status||
        CARD_STATUS_OFFLINE == adev->card_status) {
        ALOGW("in->card_status or adev->card_status offline, try again");
        ret = -EIO;
        goto error_config;
    }

    if (is_sco_in_device_type(&in->device_list)) {
        if (!adev->bt_sco_on || audio_extn_a2dp_source_is_ready()) {
            ALOGE("%s: SCO profile is not ready, return error", __func__);
            ret = -EIO;
            goto error_config;
        }
    }

    /* Check if source matches incall recording usecase criteria */
    ret = voice_check_and_set_incall_rec_usecase(adev, in);
    if (ret)
        goto error_config;
    else
        ALOGV("%s: usecase(%d)", __func__, in->usecase);

    if (audio_extn_cin_attached_usecase(in))
        audio_extn_cin_acquire_usecase(in);

    if (get_usecase_from_list(adev, in->usecase) != NULL) {
        ALOGE("%s: use case assigned already in use, stream(%p)usecase(%d: %s)",
            __func__, &in->stream, in->usecase, use_case_table[in->usecase]);
        ret = -EINVAL;
        goto error_config;
    }

    in->pcm_device_id = platform_get_pcm_device_id(in->usecase, PCM_CAPTURE);
    if (in->pcm_device_id < 0) {
        ALOGE("%s: Could not find PCM device id for the usecase(%d)",
              __func__, in->usecase);
        ret = -EINVAL;
        goto error_config;
    }

    uc_info = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));

    if (!uc_info) {
        ret = -ENOMEM;
        goto error_config;
    }

    uc_info->id = in->usecase;
    uc_info->type = PCM_CAPTURE;
    uc_info->stream.in = in;
    list_init(&uc_info->device_list);
    assign_devices(&uc_info->device_list, &in->device_list);
    uc_info->in_snd_device = SND_DEVICE_NONE;
    uc_info->out_snd_device = SND_DEVICE_NONE;

    list_add_tail(&adev->usecase_list, &uc_info->list);
    audio_streaming_hint_start();
    audio_extn_perf_lock_acquire(&adev->perf_lock_handle, 0,
                                 adev->perf_lock_opts,
                                 adev->perf_lock_opts_size);
    select_devices(adev, in->usecase);

    if (audio_extn_ext_hw_plugin_usecase_start(adev->ext_hw_plugin, uc_info))
        ALOGE("%s: failed to start ext hw plugin", __func__);

    android_atomic_acquire_cas(true, false, &(in->capture_stopped));

    if (audio_extn_cin_attached_usecase(in)) {
       ret = audio_extn_cin_open_input_stream(in);
       if (ret)
           goto error_open;
       else
           goto done_open;
    }

    if (in->usecase == USECASE_AUDIO_RECORD_MMAP) {
        if (in->pcm == NULL || !pcm_is_ready(in->pcm)) {
            ALOGE("%s: pcm stream not ready", __func__);
            goto error_open;
        }
        ret = pcm_start(in->pcm);
        if (ret < 0) {
            ALOGE("%s: MMAP pcm_start failed ret %d", __func__, ret);
            goto error_open;
        }
    } else {
        unsigned int flags = PCM_IN | PCM_MONOTONIC;
        unsigned int pcm_open_retry_count = 0;

        if ((in->usecase == USECASE_AUDIO_RECORD_AFE_PROXY) ||
             (in->usecase == USECASE_AUDIO_RECORD_AFE_PROXY2)) {
            flags |= PCM_MMAP | PCM_NOIRQ;
            pcm_open_retry_count = PROXY_OPEN_RETRY_COUNT;
        } else if (in->realtime) {
            flags |= PCM_MMAP | PCM_NOIRQ;
        }

        if (audio_extn_ffv_get_stream() == in) {
           ALOGD("%s: ffv stream, update pcm config", __func__);
           audio_extn_ffv_update_pcm_config(&config);
        }
        ALOGV("%s: Opening PCM device card_id(%d) device_id(%d), channels %d",
              __func__, adev->snd_card, in->pcm_device_id, in->config.channels);

        while (1) {
            ATRACE_BEGIN("pcm_in_open");
            in->pcm = pcm_open(adev->snd_card, in->pcm_device_id,
                               flags, &config);
            ATRACE_END();
            if (errno == ENETRESET && !pcm_is_ready(in->pcm)) {
                ALOGE("%s: pcm_open failed errno:%d\n", __func__, errno);
                adev->card_status = CARD_STATUS_OFFLINE;
                in->card_status = CARD_STATUS_OFFLINE;
                ret = -EIO;
                goto error_open;
            }

            if (in->pcm == NULL || !pcm_is_ready(in->pcm)) {
                ALOGE("%s: %s", __func__, pcm_get_error(in->pcm));
                if (in->pcm != NULL) {
                    pcm_close(in->pcm);
                    in->pcm = NULL;
                }
                if (pcm_open_retry_count == 0) {
                    ret = -EIO;
                    goto error_open;
                }
                pcm_open_retry_count--;
                usleep(PROXY_OPEN_WAIT_TIME * 1000);
                continue;
            }
            break;
        }

        ALOGV("%s: pcm_prepare", __func__);
        ATRACE_BEGIN("pcm_in_prepare");
        ret = pcm_prepare(in->pcm);
        ATRACE_END();
        if (ret < 0) {
            ALOGE("%s: pcm_prepare returned %d", __func__, ret);
            pcm_close(in->pcm);
            in->pcm = NULL;
            goto error_open;
        }
        register_in_stream(in);
        if (in->realtime) {
            ATRACE_BEGIN("pcm_in_start");
            ret = pcm_start(in->pcm);
            ATRACE_END();
            if (ret < 0) {
                ALOGE("%s: RT pcm_start failed ret %d", __func__, ret);
                pcm_close(in->pcm);
                in->pcm = NULL;
                goto error_open;
            }
        }
    }

    check_and_enable_effect(adev);
    audio_extn_audiozoom_set_microphone_direction(in, in->zoom);
    audio_extn_audiozoom_set_microphone_field_dimension(in, in->direction);

    if (is_loopback_input_device(get_device_types(&in->device_list)))
        audio_extn_keep_alive_start(KEEP_ALIVE_OUT_PRIMARY);

done_open:
    audio_streaming_hint_end();
    audio_extn_perf_lock_release(&adev->perf_lock_handle);
    ALOGD("%s: exit", __func__);
    enable_gcov();
    return ret;

error_open:
    audio_streaming_hint_end();
    audio_extn_perf_lock_release(&adev->perf_lock_handle);
    stop_input_stream(in);

error_config:
    if (audio_extn_cin_attached_usecase(in))
        audio_extn_cin_close_input_stream(in);
    /*
     * sleep 50ms to allow sufficient time for kernel
     * drivers to recover incases like SSR.
     */
    usleep(50000);
    ALOGD("%s: exit: status(%d)", __func__, ret);
    enable_gcov();
    return ret;
}

void lock_input_stream(struct stream_in *in)
{
    pthread_mutex_lock(&in->pre_lock);
    pthread_mutex_lock(&in->lock);
    pthread_mutex_unlock(&in->pre_lock);
}

void lock_output_stream(struct stream_out *out)
{
    pthread_mutex_lock(&out->pre_lock);
    pthread_mutex_lock(&out->lock);
    pthread_mutex_unlock(&out->pre_lock);
}

/* must be called with out->lock locked */
static int send_offload_cmd_l(struct stream_out* out, int command)
{
    struct offload_cmd *cmd = (struct offload_cmd *)calloc(1, sizeof(struct offload_cmd));

    if (!cmd) {
        ALOGE("failed to allocate mem for command 0x%x", command);
        return -ENOMEM;
    }

    ALOGVV("%s %d", __func__, command);

    cmd->cmd = command;
    list_add_tail(&out->offload_cmd_list, &cmd->node);
    pthread_cond_signal(&out->offload_cond);
    return 0;
}

/* must be called with out->lock */
static void stop_compressed_output_l(struct stream_out *out)
{
    pthread_mutex_lock(&out->latch_lock);
    out->offload_state = OFFLOAD_STATE_IDLE;
    pthread_mutex_unlock(&out->latch_lock);

    out->playback_started = 0;
    out->send_new_metadata = 1;
    if (out->compr != NULL) {
        compress_stop(out->compr);
        while (out->offload_thread_blocked) {
            pthread_cond_wait(&out->cond, &out->lock);
        }
    }
}

bool is_interactive_usecase(audio_usecase_t uc_id)
{
    unsigned int i;
    for (i = 0; i < sizeof(interactive_usecases)/sizeof(interactive_usecases[0]); i++) {
        if (uc_id == interactive_usecases[i])
            return true;
    }
    return false;
}

static audio_usecase_t get_interactive_usecase(struct audio_device *adev)
{
    audio_usecase_t ret_uc = USECASE_INVALID;
    unsigned int intract_uc_index;
    unsigned int num_usecase = sizeof(interactive_usecases)/sizeof(interactive_usecases[0]);

    ALOGV("%s: num_usecase: %d", __func__, num_usecase);
    for (intract_uc_index = 0; intract_uc_index < num_usecase; intract_uc_index++) {
        if (!(adev->interactive_usecase_state & (0x1 << intract_uc_index))) {
            adev->interactive_usecase_state |= 0x1 << intract_uc_index;
            ret_uc = interactive_usecases[intract_uc_index];
            break;
        }
    }

    ALOGV("%s: Interactive usecase is %d", __func__, ret_uc);
    return ret_uc;
}

static void free_interactive_usecase(struct audio_device *adev,
                                 audio_usecase_t uc_id)
{
    unsigned int interact_uc_index;
    unsigned int num_usecase = sizeof(interactive_usecases)/sizeof(interactive_usecases[0]);

    for (interact_uc_index = 0; interact_uc_index < num_usecase; interact_uc_index++) {
        if (interactive_usecases[interact_uc_index] == uc_id) {
            adev->interactive_usecase_state &= ~(0x1 << interact_uc_index);
            break;
        }
    }
    ALOGV("%s: free Interactive usecase %d", __func__, uc_id);
}

bool is_offload_usecase(audio_usecase_t uc_id)
{
    unsigned int i;
    for (i = 0; i < sizeof(offload_usecases)/sizeof(offload_usecases[0]); i++) {
        if (uc_id == offload_usecases[i])
            return true;
    }
    return false;
}

static audio_usecase_t get_offload_usecase(struct audio_device *adev, bool is_compress)
{
    audio_usecase_t ret_uc = USECASE_INVALID;
    unsigned int offload_uc_index;
    unsigned int num_usecase = sizeof(offload_usecases)/sizeof(offload_usecases[0]);
    if (!adev->multi_offload_enable) {
        if (!is_compress)
            ret_uc = USECASE_AUDIO_PLAYBACK_OFFLOAD2;
        else
            ret_uc = USECASE_AUDIO_PLAYBACK_OFFLOAD;

        pthread_mutex_lock(&adev->lock);
        if (get_usecase_from_list(adev, ret_uc) != NULL)
           ret_uc = USECASE_INVALID;
        pthread_mutex_unlock(&adev->lock);

        return ret_uc;
    }

    ALOGV("%s: num_usecase: %d", __func__, num_usecase);
    for (offload_uc_index = 0; offload_uc_index < num_usecase; offload_uc_index++) {
        if (!(adev->offload_usecases_state & (0x1 << offload_uc_index))) {
            adev->offload_usecases_state |= 0x1 << offload_uc_index;
            ret_uc = offload_usecases[offload_uc_index];
            break;
        }
    }

    ALOGV("%s: offload usecase is %d", __func__, ret_uc);
    return ret_uc;
}

static void free_offload_usecase(struct audio_device *adev,
                                 audio_usecase_t uc_id)
{
    unsigned int offload_uc_index;
    unsigned int num_usecase = sizeof(offload_usecases)/sizeof(offload_usecases[0]);

    if (!adev->multi_offload_enable)
        return;

    for (offload_uc_index = 0; offload_uc_index < num_usecase; offload_uc_index++) {
        if (offload_usecases[offload_uc_index] == uc_id) {
            adev->offload_usecases_state &= ~(0x1 << offload_uc_index);
            break;
        }
    }
    ALOGV("%s: free offload usecase %d", __func__, uc_id);
}

static void *offload_thread_loop(void *context)
{
    struct stream_out *out = (struct stream_out *) context;
    struct listnode *item;
    int ret = 0;

    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_AUDIO);
    set_sched_policy(0, SP_FOREGROUND);
    prctl(PR_SET_NAME, (unsigned long)"Offload Callback", 0, 0, 0);

    ALOGV("%s", __func__);
    lock_output_stream(out);
    out->offload_state = OFFLOAD_STATE_IDLE;
    out->playback_started = 0;
    for (;;) {
        struct offload_cmd *cmd = NULL;
        stream_callback_event_t event;
        bool send_callback = false;

        ALOGVV("%s offload_cmd_list %d out->offload_state %d",
              __func__, list_empty(&out->offload_cmd_list),
              out->offload_state);
        if (list_empty(&out->offload_cmd_list)) {
            ALOGV("%s SLEEPING", __func__);
            pthread_cond_wait(&out->offload_cond, &out->lock);
            ALOGV("%s RUNNING", __func__);
            continue;
        }

        item = list_head(&out->offload_cmd_list);
        cmd = node_to_item(item, struct offload_cmd, node);
        list_remove(item);

        ALOGVV("%s STATE %d CMD %d out->compr %p",
               __func__, out->offload_state, cmd->cmd, out->compr);

        if (cmd->cmd == OFFLOAD_CMD_EXIT) {
            free(cmd);
            break;
        }

        // allow OFFLOAD_CMD_ERROR reporting during standby
        // this is needed to handle failures during compress_open
        // Note however that on a pause timeout, the stream is closed
        // and no offload usecase will be active. Therefore this
        // special case is needed for compress_open failures alone
        if (cmd->cmd != OFFLOAD_CMD_ERROR &&
            out->compr == NULL) {
            ALOGE("%s: Compress handle is NULL", __func__);
            free(cmd);
            pthread_cond_signal(&out->cond);
            continue;
        }
        out->offload_thread_blocked = true;
        pthread_mutex_unlock(&out->lock);
        send_callback = false;
        switch(cmd->cmd) {
        case OFFLOAD_CMD_WAIT_FOR_BUFFER:
            ALOGD("copl(%p):calling compress_wait", out);
            compress_wait(out->compr, -1);
            ALOGD("copl(%p):out of compress_wait", out);
            send_callback = true;
            event = STREAM_CBK_EVENT_WRITE_READY;
            break;
        case OFFLOAD_CMD_PARTIAL_DRAIN:
            ret = compress_next_track(out->compr);
            if(ret == 0) {
                ALOGD("copl(%p):calling compress_partial_drain", out);
                ret = compress_partial_drain(out->compr);
                ALOGD("copl(%p):out of compress_partial_drain", out);
                if (ret < 0)
                    ret = -errno;
            }
            else if (ret == -ETIMEDOUT)
                ret = compress_drain(out->compr);
            else
                ALOGE("%s: Next track returned error %d",__func__, ret);
            if (-ENETRESET != ret && !(-EINTR == ret &&
                        CARD_STATUS_OFFLINE == out->card_status)) {
                send_callback = true;
                pthread_mutex_lock(&out->lock);
                out->send_new_metadata = 1;
                out->send_next_track_params = true;
                pthread_mutex_unlock(&out->lock);
                event = STREAM_CBK_EVENT_DRAIN_READY;
                ALOGV("copl(%p):send drain callback, ret %d", out, ret);
            } else
                ALOGI("%s: Block drain ready event during SSR", __func__);
            break;
        case OFFLOAD_CMD_DRAIN:
            ALOGD("copl(%p):calling compress_drain", out);
            ret = compress_drain(out->compr);
            ALOGD("copl(%p):out of compress_drain", out);
            // EINTR check avoids drain interruption due to SSR
            if (-ENETRESET != ret && !(-EINTR == ret &&
                        CARD_STATUS_OFFLINE == out->card_status)) {
                send_callback = true;
                event = STREAM_CBK_EVENT_DRAIN_READY;
            } else
                ALOGI("%s: Block drain ready event during SSR", __func__);
            break;
        case OFFLOAD_CMD_ERROR:
            ALOGD("copl(%p): sending error callback to AF", out);
            send_callback = true;
            event = STREAM_CBK_EVENT_ERROR;
            break;
        default:
            ALOGE("%s unknown command received: %d", __func__, cmd->cmd);
            break;
        }
        lock_output_stream(out);
        out->offload_thread_blocked = false;
        pthread_cond_signal(&out->cond);
        if (send_callback && out->client_callback) {
            ALOGVV("%s: sending client_callback event %d", __func__, event);
            out->client_callback(event, NULL, out->client_cookie);
        }
        free(cmd);
    }

    pthread_cond_signal(&out->cond);
    while (!list_empty(&out->offload_cmd_list)) {
        item = list_head(&out->offload_cmd_list);
        list_remove(item);
        free(node_to_item(item, struct offload_cmd, node));
    }
    pthread_mutex_unlock(&out->lock);

    return NULL;
}

static int create_offload_callback_thread(struct stream_out *out)
{
    pthread_cond_init(&out->offload_cond, (const pthread_condattr_t *) NULL);
    list_init(&out->offload_cmd_list);
    pthread_create(&out->offload_thread, (const pthread_attr_t *) NULL,
                    offload_thread_loop, out);
    return 0;
}

static int destroy_offload_callback_thread(struct stream_out *out)
{
    lock_output_stream(out);
    stop_compressed_output_l(out);
    send_offload_cmd_l(out, OFFLOAD_CMD_EXIT);

    pthread_mutex_unlock(&out->lock);
    pthread_join(out->offload_thread, (void **) NULL);
    pthread_cond_destroy(&out->offload_cond);

    return 0;
}

static int stop_output_stream(struct stream_out *out)
{
    int ret = 0;
    struct audio_usecase *uc_info;
    struct audio_device *adev = out->dev;
    bool has_voip_usecase =
        get_usecase_from_list(adev, USECASE_AUDIO_PLAYBACK_VOIP) != NULL;

    ALOGV("%s: enter: usecase(%d: %s)", __func__,
          out->usecase, use_case_table[out->usecase]);
    uc_info = get_usecase_from_list(adev, out->usecase);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
              __func__, out->usecase);
        return -EINVAL;
    }

    out->a2dp_muted = false;

    if (audio_extn_ext_hw_plugin_usecase_stop(adev->ext_hw_plugin, uc_info))
        ALOGE("%s: failed to stop ext hw plugin", __func__);

    if (is_offload_usecase(out->usecase) &&
        !(audio_extn_passthru_is_passthrough_stream(out))) {
        if (adev->visualizer_stop_output != NULL)
            adev->visualizer_stop_output(out->handle, out->pcm_device_id);

        audio_extn_dts_remove_state_notifier_node(out->usecase);

        if (adev->offload_effects_stop_output != NULL)
            adev->offload_effects_stop_output(out->handle, out->pcm_device_id);
    } else if (out->usecase == USECASE_AUDIO_PLAYBACK_ULL ||
               out->usecase == USECASE_AUDIO_PLAYBACK_MMAP) {
        audio_low_latency_hint_end();
    }

    if (out->usecase == USECASE_INCALL_MUSIC_UPLINK ||
        out->usecase == USECASE_INCALL_MUSIC_UPLINK2) {
        voice_set_device_mute_flag(adev, false);
    }

    /* 1. Get and set stream specific mixer controls */
    disable_audio_route(adev, uc_info);

    /* 2. Disable the rx device */
    disable_snd_device(adev, uc_info->out_snd_device);
    if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS)
        disable_snd_device(adev, SND_DEVICE_OUT_HAPTICS);

    audio_extn_extspk_update(adev->extspk);

    if (is_offload_usecase(out->usecase)) {
        audio_enable_asm_bit_width_enforce_mode(adev->mixer,
                                                adev->dsp_bit_width_enforce_mode,
                                                false);
    }
    if (is_usb_out_device_type(&out->device_list)) {
        ret = audio_extn_usb_check_and_set_svc_int(uc_info,
                                                   false);

        if (ret != 0)
            check_usecases_codec_backend(adev, uc_info, uc_info->out_snd_device);
            /* default service interval was successfully updated,
            reopen USB backend with new service interval */
        ret = 0;
    }

    list_remove(&uc_info->list);
    out->started = 0;
    if (is_offload_usecase(out->usecase) &&
        (audio_extn_passthru_is_passthrough_stream(out))) {
        ALOGV("Disable passthrough , reset mixer to pcm");
        /* NO_PASSTHROUGH */
#ifdef AUDIO_GKI_ENABLED
        /* out->compr_config.codec->reserved[0] is for compr_passthr */
        out->compr_config.codec->reserved[0] = 0;
#else
        out->compr_config.codec->compr_passthr = 0;
#endif
        audio_extn_passthru_on_stop(out);
        audio_extn_dolby_set_dap_bypass(adev, DAP_STATE_ON);
    }

    /* Must be called after removing the usecase from list */
    if (compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_AUX_DIGITAL))
        audio_extn_keep_alive_start(KEEP_ALIVE_OUT_HDMI);

    if (out->ip_hdlr_handle) {
        ret = audio_extn_ip_hdlr_intf_close(out->ip_hdlr_handle, true, out);
        if (ret < 0)
            ALOGE("%s: audio_extn_ip_hdlr_intf_close failed %d",__func__, ret);
    }

    /* trigger voip input to reroute when voip output changes to hearing aid */
    if (has_voip_usecase ||
            compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_SPEAKER_SAFE)) {
        struct listnode *node;
        struct audio_usecase *usecase;
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (usecase->type == PCM_PLAYBACK || usecase == uc_info ||
                (usecase->type == PCM_CAPTURE &&
                     usecase->id != USECASE_AUDIO_RECORD_VOIP &&
                          usecase->id != USECASE_AUDIO_RECORD_VOIP_LOW_LATENCY))
                continue;

            ALOGD("%s: select_devices at usecase(%d: %s) after removing the usecase(%d: %s)",
                __func__, usecase->id, use_case_table[usecase->id],
                out->usecase, use_case_table[out->usecase]);
            select_devices(adev, usecase->id);
        }
    }

    free(uc_info);
    ALOGV("%s: exit: status(%d)", __func__, ret);
    return ret;
}

struct pcm* pcm_open_prepare_helper(unsigned int snd_card, unsigned int pcm_device_id,
                                   unsigned int flags, unsigned int pcm_open_retry_count,
                                   struct pcm_config *config)
{
    struct pcm* pcm = NULL;

    while (1) {
        pcm = pcm_open(snd_card, pcm_device_id, flags, config);
        if (pcm == NULL || !pcm_is_ready(pcm)) {
            ALOGE("%s: %s", __func__, pcm_get_error(pcm));
            if (pcm != NULL) {
                pcm_close(pcm);
                pcm = NULL;
            }
            if (pcm_open_retry_count == 0)
                return NULL;

            pcm_open_retry_count--;
            usleep(PROXY_OPEN_WAIT_TIME * 1000);
            continue;
        }
        break;
    }

    if (pcm_is_ready(pcm)) {
        int ret = pcm_prepare(pcm);
        if (ret < 0) {
            ALOGE("%s: pcm_prepare returned %d", __func__, ret);
            pcm_close(pcm);
            pcm = NULL;
        }
    }

    return pcm;
}

int start_output_stream(struct stream_out *out)
{
    int ret = 0;
    struct audio_usecase *uc_info;
    struct audio_device *adev = out->dev;
    char mixer_ctl_name[128];
    struct mixer_ctl *ctl = NULL;
    char* perf_mode[] = {"ULL", "ULL_PP", "LL"};
    bool a2dp_combo = false;
    bool is_haptic_usecase = (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS) ? true: false;

    ATRACE_BEGIN("start_output_stream");
    if ((out->usecase < 0) || (out->usecase >= AUDIO_USECASE_MAX)) {
        ret = -EINVAL;
        goto error_config;
    }

    ALOGD("%s: enter: stream(%p)usecase(%d: %s) devices(%#x) is_haptic_usecase(%d)",
          __func__, &out->stream, out->usecase, use_case_table[out->usecase],
          get_device_types(&out->device_list), is_haptic_usecase);

    bool is_speaker_active = compare_device_type(&out->device_list,
                                                 AUDIO_DEVICE_OUT_SPEAKER);
    bool is_speaker_safe_active = compare_device_type(&out->device_list,
                                                      AUDIO_DEVICE_OUT_SPEAKER_SAFE);

    if (CARD_STATUS_OFFLINE == out->card_status ||
        CARD_STATUS_OFFLINE == adev->card_status) {
        ALOGW("out->card_status or adev->card_status offline, try again");
        ret = -EIO;
        goto error_fatal;
    }

    //Update incall music usecase to reflect correct voice session
    if (out->flags & AUDIO_OUTPUT_FLAG_INCALL_MUSIC) {
        ret = voice_extn_check_and_set_incall_music_usecase(adev, out);
        if (ret != 0) {
            ALOGE("%s: Incall music delivery usecase cannot be set error:%d",
                __func__, ret);
            goto error_config;
        }
    }

    if (is_a2dp_out_device_type(&out->device_list)) {
        if (!audio_extn_a2dp_source_is_ready()) {
            if (is_speaker_active || is_speaker_safe_active) {
                a2dp_combo = true;
            } else {
                if (!is_offload_usecase(out->usecase)) {
                    ALOGE("%s: A2DP profile is not ready, return error", __func__);
                    ret = -EAGAIN;
                    goto error_config;
                }
            }
        }
    }
    if (is_sco_out_device_type(&out->device_list)) {
        if (!adev->bt_sco_on) {
            if (is_speaker_active) {
                //combo usecase just by pass a2dp
                ALOGW("%s: SCO is not connected, route it to speaker", __func__);
                reassign_device_list(&out->device_list, AUDIO_DEVICE_OUT_SPEAKER, "");
            } else {
                ALOGE("%s: SCO profile is not ready, return error", __func__);
                ret = -EAGAIN;
                goto error_config;
            }
        }
    }

    out->pcm_device_id = platform_get_pcm_device_id(out->usecase, PCM_PLAYBACK);
    if (out->pcm_device_id < 0) {
        ALOGE("%s: Invalid PCM device id(%d) for the usecase(%d)",
              __func__, out->pcm_device_id, out->usecase);
        ret = -EINVAL;
        goto error_open;
    }

    if (is_haptic_usecase) {
        adev->haptic_pcm_device_id = platform_get_pcm_device_id(
                     USECASE_AUDIO_PLAYBACK_HAPTICS, PCM_PLAYBACK);
        if (adev->haptic_pcm_device_id < 0) {
            ALOGE("%s: Invalid Haptics pcm device id(%d) for the usecase(%d)",
                  __func__, adev->haptic_pcm_device_id, out->usecase);
            ret = -EINVAL;
            goto error_config;
        }
    }

    uc_info = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));

    if (!uc_info) {
        ret = -ENOMEM;
        goto error_config;
    }

    uc_info->id = out->usecase;
    uc_info->type = PCM_PLAYBACK;
    uc_info->stream.out = out;
    list_init(&uc_info->device_list);
    assign_devices(&uc_info->device_list, &out->device_list);
    uc_info->in_snd_device = SND_DEVICE_NONE;
    uc_info->out_snd_device = SND_DEVICE_NONE;

    /* This must be called before adding this usecase to the list */
    if (is_usb_out_device_type(&out->device_list)) {
       audio_extn_usb_check_and_set_svc_int(uc_info, true);
       /* USB backend is not reopened immediately.
       This is eventually done as part of select_devices */
    }

    list_add_tail(&adev->usecase_list, &uc_info->list);

    audio_streaming_hint_start();
    audio_extn_perf_lock_acquire(&adev->perf_lock_handle, 0,
                                 adev->perf_lock_opts,
                                 adev->perf_lock_opts_size);

    if (compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
        audio_extn_keep_alive_stop(KEEP_ALIVE_OUT_HDMI);
        if (audio_extn_passthru_is_enabled() &&
            audio_extn_passthru_is_passthrough_stream(out)) {
            audio_extn_passthru_on_start(out);
        }
    }

    if (is_a2dp_out_device_type(&out->device_list) &&
        (!audio_extn_a2dp_source_is_ready())) {
        if (!a2dp_combo) {
            check_a2dp_restore_l(adev, out, false);
        } else {
            struct listnode dev;
            list_init(&dev);
            assign_devices(&dev, &out->device_list);
            if (compare_device_type(&dev, AUDIO_DEVICE_OUT_SPEAKER_SAFE))
                reassign_device_list(&out->device_list,
                                AUDIO_DEVICE_OUT_SPEAKER_SAFE, "");
            else
                reassign_device_list(&out->device_list,
                                AUDIO_DEVICE_OUT_SPEAKER, "");
            select_devices(adev, out->usecase);
            assign_devices(&out->device_list, &dev);
        }
    } else {
        select_devices(adev, out->usecase);
        if (is_a2dp_out_device_type(&out->device_list) &&
             !adev->a2dp_started) {
            if (is_speaker_active || is_speaker_safe_active) {
                struct listnode dev;
                list_init(&dev);
                assign_devices(&dev, &out->device_list);
                if (compare_device_type(&dev, AUDIO_DEVICE_OUT_SPEAKER_SAFE))
                    reassign_device_list(&out->device_list,
                                    AUDIO_DEVICE_OUT_SPEAKER_SAFE, "");
                else
                    reassign_device_list(&out->device_list,
                                    AUDIO_DEVICE_OUT_SPEAKER, "");
                select_devices(adev, out->usecase);
                assign_devices(&out->device_list, &dev);
            } else {
                ret = -EINVAL;
                goto error_open;
            }
        }
    }

    if (out->usecase == USECASE_INCALL_MUSIC_UPLINK ||
        out->usecase == USECASE_INCALL_MUSIC_UPLINK2) {
        voice_set_device_mute_flag(adev, true);
    }

    if (audio_extn_ext_hw_plugin_usecase_start(adev->ext_hw_plugin, uc_info))
        ALOGE("%s: failed to start ext hw plugin", __func__);

    ALOGV("%s: Opening PCM device card_id(%d) device_id(%d) format(%#x)",
          __func__, adev->snd_card, out->pcm_device_id, out->config.format);

    if (out->usecase == USECASE_AUDIO_PLAYBACK_MMAP) {
        ALOGD("%s: Starting MMAP stream", __func__);
        if (out->pcm == NULL || !pcm_is_ready(out->pcm)) {
            ALOGE("%s: pcm stream not ready", __func__);
            goto error_open;
        }
        ret = pcm_start(out->pcm);
        if (ret < 0) {
            ALOGE("%s: MMAP pcm_start failed ret %d", __func__, ret);
            goto error_open;
        }
        out_set_mmap_volume(&out->stream, out->volume_l, out->volume_r);
    } else if (!is_offload_usecase(out->usecase)) {
        unsigned int flags = PCM_OUT;
        unsigned int pcm_open_retry_count = 0;
        if (out->usecase == USECASE_AUDIO_PLAYBACK_AFE_PROXY) {
            flags |= PCM_MMAP | PCM_NOIRQ;
            pcm_open_retry_count = PROXY_OPEN_RETRY_COUNT;
        } else if (out->realtime) {
            flags |= PCM_MMAP | PCM_NOIRQ | PCM_MONOTONIC;
        } else
            flags |= PCM_MONOTONIC;

        if ((adev->vr_audio_mode_enabled) &&
            (out->flags & AUDIO_OUTPUT_FLAG_RAW)) {
            snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                    "PCM_Dev %d Topology", out->pcm_device_id);
            ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
            if (!ctl) {
                ALOGI("%s: Could not get ctl for mixer cmd might be ULL - %s",
                      __func__, mixer_ctl_name);
            } else {
                //if success use ULLPP
                ALOGI("%s: mixer ctrl %s succeeded setting up ULL for %d",
                    __func__, mixer_ctl_name, out->pcm_device_id);
                //There is a still a possibility that some sessions
                // that request for FAST|RAW when 3D audio is active
                //can go through ULLPP. Ideally we expects apps to
                //listen to audio focus and stop concurrent playback
                //Also, we will look for mode flag (voice_in_communication)
                //before enabling the realtime flag.
                mixer_ctl_set_enum_by_string(ctl, perf_mode[1]);
            }
        }

        platform_set_stream_channel_map(adev->platform, out->channel_mask,
               out->pcm_device_id, -1, &out->channel_map_param.channel_map[0]);

        out->pcm = pcm_open_prepare_helper(adev->snd_card, out->pcm_device_id,
                                       flags, pcm_open_retry_count,
                                       &(out->config));
        if (out->pcm == NULL) {
           ret = -EIO;
           goto error_open;
        }

        if (is_haptic_usecase) {
            adev->haptic_pcm = pcm_open_prepare_helper(adev->snd_card,
                                   adev->haptic_pcm_device_id,
                                   flags, pcm_open_retry_count,
                                   &(adev->haptics_config));
            // failure to open haptics pcm shouldnt stop audio,
            // so do not close audio pcm in case of error

            if (property_get_bool("vendor.audio.enable_haptic_audio_sync", false)) {
                ALOGD("%s: enable haptic audio synchronization", __func__);
                platform_set_qtime(adev->platform, out->pcm_device_id, adev->haptic_pcm_device_id);
            }
        }

        // apply volume for voip playback after path is set up
        if (out->usecase == USECASE_AUDIO_PLAYBACK_VOIP)
            out_set_voip_volume(&out->stream, out->volume_l, out->volume_r);
        else if ((out->usecase == USECASE_AUDIO_PLAYBACK_LOW_LATENCY || out->usecase == USECASE_AUDIO_PLAYBACK_DEEP_BUFFER ||
                  out->usecase == USECASE_AUDIO_PLAYBACK_ULL) && (out->apply_volume)) {
                 out_set_pcm_volume(&out->stream, out->volume_l, out->volume_r);
                 out->apply_volume = false;
        } else if (audio_extn_auto_hal_is_bus_device_usecase(out->usecase)) {
            out_set_pcm_volume(&out->stream, out->volume_l, out->volume_r);
        }
    } else {
        /*
         * set custom channel map if:
         *   1. neither mono nor stereo clips i.e. channels > 2 OR
         *   2. custom channel map has been set by client
         * else default channel map of FC/FR/FL can always be set to DSP
         */
        if (popcount(out->channel_mask) > 2 || out->channel_map_param.channel_map[0])
            platform_set_stream_channel_map(adev->platform, out->channel_mask,
                       out->pcm_device_id, -1, &out->channel_map_param.channel_map[0]);
        audio_enable_asm_bit_width_enforce_mode(adev->mixer,
                                                adev->dsp_bit_width_enforce_mode,
                                                true);
        out->pcm = NULL;
        ATRACE_BEGIN("compress_open");
        out->compr = compress_open(adev->snd_card,
                                   out->pcm_device_id,
                                   COMPRESS_IN, &out->compr_config);
        ATRACE_END();
        if (errno == ENETRESET && !is_compress_ready(out->compr)) {
                ALOGE("%s: compress_open failed errno:%d\n", __func__, errno);
                adev->card_status = CARD_STATUS_OFFLINE;
                out->card_status = CARD_STATUS_OFFLINE;
                ret = -EIO;
                goto error_open;
        }

        if (out->compr && !is_compress_ready(out->compr)) {
            ALOGE("%s: failed /w error %s", __func__, compress_get_error(out->compr));
            compress_close(out->compr);
            out->compr = NULL;
            ret = -EIO;
            goto error_open;
        }
        /* compress_open sends params of the track, so reset the flag here */
        out->is_compr_metadata_avail = false;

        if (out->client_callback)
            compress_nonblock(out->compr, out->non_blocking);

        /* Since small bufs uses blocking writes, a write will be blocked
           for the default max poll time (20s) in the event of an SSR.
           Reduce the poll time to observe and deal with SSR faster.
        */
        if (!out->non_blocking) {
            compress_set_max_poll_wait(out->compr, 1000);
        }

        audio_extn_utils_compress_set_render_mode(out);
        audio_extn_utils_compress_set_clk_rec_mode(uc_info);

        audio_extn_dts_create_state_notifier_node(out->usecase);
        audio_extn_dts_notify_playback_state(out->usecase, 0, out->sample_rate,
                                             popcount(out->channel_mask),
                                             out->playback_started);

#ifdef DS1_DOLBY_DDP_ENABLED
        if (audio_extn_utils_is_dolby_format(out->format))
            audio_extn_dolby_send_ddp_endp_params(adev);
#endif
        if (!(audio_extn_passthru_is_passthrough_stream(out)) &&
                (out->sample_rate != 176400 && out->sample_rate <= 192000)) {
            if (adev->visualizer_start_output != NULL)
                adev->visualizer_start_output(out->handle, out->pcm_device_id);
            if (adev->offload_effects_start_output != NULL)
                adev->offload_effects_start_output(out->handle, out->pcm_device_id, adev->mixer);
            audio_extn_check_and_set_dts_hpx_state(adev);
        }

        if (compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_BUS)) {
            /* Update cached volume from media to offload/direct stream */
            struct listnode *node = NULL;
            list_for_each(node, &adev->active_outputs_list) {
                streams_output_ctxt_t *out_ctxt = node_to_item(node,
                                                    streams_output_ctxt_t,
                                                    list);
                if (out_ctxt->output->usecase == USECASE_AUDIO_PLAYBACK_MEDIA) {
                    out->volume_l = out_ctxt->output->volume_l;
                    out->volume_r = out_ctxt->output->volume_r;
                }
            }
            out_set_compr_volume(&out->stream,
                                 out->volume_l, out->volume_r);
        }
    }

    if (ret == 0) {
        register_out_stream(out);
        if (out->realtime) {
            if (out->pcm == NULL || !pcm_is_ready(out->pcm)) {
                ALOGE("%s: pcm stream not ready", __func__);
                goto error_open;
            }
            ATRACE_BEGIN("pcm_start");
            ret = pcm_start(out->pcm);
            ATRACE_END();
            if (ret < 0)
                goto error_open;
        }
    }
    audio_streaming_hint_end();
    audio_extn_perf_lock_release(&adev->perf_lock_handle);
    ALOGD("%s: exit", __func__);

    if (out->usecase == USECASE_AUDIO_PLAYBACK_ULL ||
        out->usecase == USECASE_AUDIO_PLAYBACK_MMAP) {
        audio_low_latency_hint_start();
    }

    if (out->ip_hdlr_handle) {
        ret = audio_extn_ip_hdlr_intf_open(out->ip_hdlr_handle, true, out, out->usecase);
        if (ret < 0)
            ALOGE("%s: audio_extn_ip_hdlr_intf_open failed %d",__func__, ret);
    }

    // consider a scenario where on pause lower layers are tear down.
    // so on resume, swap mixer control need to be sent only when
    // backend is active, hence rather than sending from enable device
    // sending it from start of streamtream

    platform_set_swap_channels(adev, true);

    ATRACE_END();
    enable_gcov();
    return ret;
error_open:
    if (adev->haptic_pcm) {
        pcm_close(adev->haptic_pcm);
        adev->haptic_pcm = NULL;
    }
    audio_streaming_hint_end();
    audio_extn_perf_lock_release(&adev->perf_lock_handle);
    stop_output_stream(out);
error_fatal:
    /*
     * sleep 50ms to allow sufficient time for kernel
     * drivers to recover incases like SSR.
     */
    usleep(50000);
error_config:
    ATRACE_END();
    enable_gcov();
    return ret;
}

static int check_input_parameters(uint32_t sample_rate,
                                  audio_format_t format,
                                  int channel_count,
                                  bool is_usb_hifi)
{
    int ret = 0;

    if (((format != AUDIO_FORMAT_PCM_16_BIT) && (format != AUDIO_FORMAT_PCM_8_24_BIT) &&
        (format != AUDIO_FORMAT_PCM_24_BIT_PACKED) && (format != AUDIO_FORMAT_PCM_32_BIT) &&
        (format != AUDIO_FORMAT_PCM_FLOAT)) &&
        !voice_extn_compress_voip_is_format_supported(format) &&
        !audio_extn_compr_cap_format_supported(format) &&
        !audio_extn_cin_format_supported(format))
            ret = -EINVAL;

    int max_channel_count = is_usb_hifi ? MAX_HIFI_CHANNEL_COUNT : MAX_CHANNEL_COUNT;
    if ((channel_count < MIN_CHANNEL_COUNT) || (channel_count > max_channel_count)) {
        ALOGE("%s: unsupported channel count (%d) passed  Min / Max (%d / %d)", __func__,
               channel_count, MIN_CHANNEL_COUNT, max_channel_count);
        return -EINVAL;
    }

    switch (channel_count) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 6:
    case 8:
    case 10:
    case 12:
    case 14:
        break;
    default:
        ret = -EINVAL;
    }

    switch (sample_rate) {
    case 8000:
    case 11025:
    case 12000:
    case 16000:
    case 22050:
    case 24000:
    case 32000:
    case 44100:
    case 48000:
    case 88200:
    case 96000:
    case 176400:
    case 192000:
        break;
    default:
        ret = -EINVAL;
    }

    return ret;
}


/** Add a value in a list if not already present.
 * @return true if value was successfully inserted or already present,
 *         false if the list is full and does not contain the value.
 */
static bool register_uint(uint32_t value, uint32_t* list, size_t list_length) {
    for (size_t i = 0; i < list_length; i++) {
        if (list[i] == value) return true; // value is already present
        if (list[i] == 0) { // no values in this slot
            list[i] = value;
            return true; // value inserted
        }
    }
    return false; // could not insert value
}

/** Add channel_mask in supported_channel_masks if not already present.
 * @return true if channel_mask was successfully inserted or already present,
 *         false if supported_channel_masks is full and does not contain channel_mask.
 */
static void register_channel_mask(audio_channel_mask_t channel_mask,
            audio_channel_mask_t supported_channel_masks[static MAX_SUPPORTED_CHANNEL_MASKS]) {
    ALOGE_IF(!register_uint(channel_mask, supported_channel_masks, MAX_SUPPORTED_CHANNEL_MASKS),
        "%s: stream can not declare supporting its channel_mask %x", __func__, channel_mask);
}

/** Add format in supported_formats if not already present.
 * @return true if format was successfully inserted or already present,
 *         false if supported_formats is full and does not contain format.
 */
static void register_format(audio_format_t format,
            audio_format_t supported_formats[static MAX_SUPPORTED_FORMATS]) {
    ALOGE_IF(!register_uint(format, supported_formats, MAX_SUPPORTED_FORMATS),
             "%s: stream can not declare supporting its format %x", __func__, format);
}
/** Add sample_rate in supported_sample_rates if not already present.
 * @return true if sample_rate was successfully inserted or already present,
 *         false if supported_sample_rates is full and does not contain sample_rate.
 */
static void register_sample_rate(uint32_t sample_rate,
            uint32_t supported_sample_rates[static MAX_SUPPORTED_SAMPLE_RATES]) {
    ALOGE_IF(!register_uint(sample_rate, supported_sample_rates, MAX_SUPPORTED_SAMPLE_RATES),
             "%s: stream can not declare supporting its sample rate %x", __func__, sample_rate);
}

static inline uint32_t lcm(uint32_t num1, uint32_t num2)
{
    uint32_t high = num1, low = num2, temp = 0;

    if (!num1 || !num2)
        return 0;

    if (num1 < num2) {
         high = num2;
         low = num1;
    }

    while (low != 0) {
        temp = low;
        low = high % low;
        high = temp;
    }
    return (num1 * num2)/high;
}

static inline uint32_t nearest_multiple(uint32_t num, uint32_t multiplier)
{
    uint32_t remainder = 0;

    if (!multiplier)
        return num;

    remainder = num % multiplier;
    if (remainder)
        num += (multiplier - remainder);

    return num;
}

static size_t get_stream_buffer_size(size_t duration_ms,
                                     uint32_t sample_rate,
                                     audio_format_t format,
                                     int channel_count,
                                     bool is_low_latency)
{
    size_t size = 0;
    uint32_t bytes_per_period_sample = 0;

    size = (sample_rate * duration_ms) / 1000;
    if (is_low_latency)
        size = configured_low_latency_capture_period_size;

    bytes_per_period_sample = audio_bytes_per_sample(format) * channel_count;
    size *= audio_bytes_per_sample(format) * channel_count;

    /* make sure the size is multiple of 32 bytes and additionally multiple of
     * the frame_size (required for 24bit samples and non-power-of-2 channel counts)
     * At 48 kHz mono 16-bit PCM:
     *  5.000 ms = 240 frames = 15*16*1*2 = 480, a whole multiple of 32 (15)
     *  3.333 ms = 160 frames = 10*16*1*2 = 320, a whole multiple of 32 (10)
     * Also, make sure the size is multiple of bytes per period sample
     */
    size = nearest_multiple(size, lcm(32, bytes_per_period_sample));

    return size;
}

static size_t get_input_buffer_size(uint32_t sample_rate,
                                    audio_format_t format,
                                    int channel_count,
                                    bool is_low_latency)
{
    /* Don't know if USB HIFI in this context so use true to be conservative */
    if (check_input_parameters(sample_rate, format, channel_count,
                               true /*is_usb_hifi */) != 0)
        return 0;

    return get_stream_buffer_size(AUDIO_CAPTURE_PERIOD_DURATION_MSEC,
                                  sample_rate,
                                  format,
                                  channel_count,
                                  is_low_latency);
}

size_t get_output_period_size(uint32_t sample_rate,
                            audio_format_t format,
                            int channel_count,
                            int duration /*in millisecs*/)
{
    size_t size = 0;
    uint32_t bytes_per_sample = audio_bytes_per_sample(format);

    if ((duration == 0) || (sample_rate == 0) ||
        (bytes_per_sample == 0) || (channel_count == 0)) {
        ALOGW("Invalid config duration %d sr %d bps %d ch %d", duration, sample_rate,
               bytes_per_sample, channel_count);
        return -EINVAL;
    }

    size = (sample_rate *
            duration *
            bytes_per_sample *
            channel_count) / 1000;
    /*
     * To have same PCM samples for all channels, the buffer size requires to
     * be multiple of (number of channels * bytes per sample)
     * For writes to succeed, the buffer must be written at address which is multiple of 32
     */
    size = ALIGN(size, (bytes_per_sample * channel_count * 32));

    return (size/(channel_count * bytes_per_sample));
}

static uint64_t get_actual_pcm_frames_rendered(struct stream_out *out, struct timespec *timestamp)
{
    uint64_t actual_frames_rendered = 0;
    uint64_t written_frames = 0;
    uint64_t kernel_frames = 0;
    uint64_t dsp_frames = 0;
    uint64_t signed_frames = 0;
    size_t kernel_buffer_size = 0;

    /* This adjustment accounts for buffering after app processor.
     * It is based on estimated DSP latency per use case, rather than exact.
     */
    dsp_frames = platform_render_latency(out) *
        out->sample_rate / 1000000LL;

    pthread_mutex_lock(&out->position_query_lock);
    written_frames = out->written /
        (audio_bytes_per_sample(out->hal_ip_format) * popcount(out->channel_mask));

    /* not querying actual state of buffering in kernel as it would involve an ioctl call
     * which then needs protection, this causes delay in TS query for pcm_offload usecase
     * hence only estimate.
     */
    kernel_buffer_size = out->compr_config.fragment_size * out->compr_config.fragments;
    kernel_frames = kernel_buffer_size /
        (audio_bytes_per_sample(out->hal_op_format) * popcount(out->channel_mask));

    if (written_frames >= (kernel_frames + dsp_frames))
        signed_frames = written_frames - kernel_frames - dsp_frames;

    if (signed_frames > 0) {
        actual_frames_rendered = signed_frames;
        if (timestamp != NULL )
            *timestamp = out->writeAt;
    } else if (timestamp != NULL) {
        clock_gettime(CLOCK_MONOTONIC, timestamp);
    }
    pthread_mutex_unlock(&out->position_query_lock);

    ALOGVV("%s signed frames %lld written frames %lld kernel frames %lld dsp frames %lld",
            __func__, signed_frames, written_frames, kernel_frames, dsp_frames);

    return actual_frames_rendered;
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    return out->sample_rate;
}

static int out_set_sample_rate(struct audio_stream *stream __unused,
                               uint32_t rate __unused)
{
    return -ENOSYS;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    if (is_interactive_usecase(out->usecase)) {
        return out->config.period_size * out->config.period_count;
    } else if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
        if (out->flags & AUDIO_OUTPUT_FLAG_TIMESTAMP)
            return out->compr_config.fragment_size - sizeof(struct snd_codec_metadata);
        else
            return out->compr_config.fragment_size;
    } else if(out->usecase == USECASE_COMPRESS_VOIP_CALL)
        return voice_extn_compress_voip_out_get_buffer_size(out);
    else if (is_offload_usecase(out->usecase) &&
             out->flags == AUDIO_OUTPUT_FLAG_DIRECT)
        return out->hal_fragment_size;

    return out->config.period_size * out->af_period_multiplier *
                audio_stream_out_frame_size((const struct audio_stream_out *)stream);
}

static uint32_t out_get_channels(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    return out->channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;

    return out->format;
}

static int out_set_format(struct audio_stream *stream __unused,
                          audio_format_t format __unused)
{
    return -ENOSYS;
}

static int out_standby(struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    bool do_stop = true;

    ALOGD("%s: enter: stream (%p) usecase(%d: %s)", __func__,
          stream, out->usecase, use_case_table[out->usecase]);

    lock_output_stream(out);
    if (!out->standby) {
        if (adev->adm_deregister_stream)
            adev->adm_deregister_stream(adev->adm_data, out->handle);

        if (is_offload_usecase(out->usecase)) {
            stop_compressed_output_l(out);
        }

        pthread_mutex_lock(&adev->lock);
        out->standby = true;
        if (out->usecase == USECASE_COMPRESS_VOIP_CALL) {
            voice_extn_compress_voip_close_output_stream(stream);
            out->started = 0;
            pthread_mutex_unlock(&adev->lock);
            pthread_mutex_unlock(&out->lock);
            ALOGD("VOIP output entered standby");
            return 0;
        } else if (!is_offload_usecase(out->usecase)) {
            if (out->pcm) {
                pcm_close(out->pcm);
                out->pcm = NULL;
            }
            if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS) {
                if (adev->haptic_pcm) {
                    pcm_close(adev->haptic_pcm);
                    adev->haptic_pcm = NULL;
                }

                if (adev->haptic_buffer != NULL) {
                    free(adev->haptic_buffer);
                    adev->haptic_buffer = NULL;
                    adev->haptic_buffer_size = 0;
                }
                adev->haptic_pcm_device_id = 0;
            }

            if (out->usecase == USECASE_AUDIO_PLAYBACK_MMAP) {
                do_stop = out->playback_started;
                out->playback_started = false;

                if (out->mmap_shared_memory_fd >= 0) {
                    ALOGV("%s: closing mmap_shared_memory_fd = %d",
                          __func__, out->mmap_shared_memory_fd);
                    close(out->mmap_shared_memory_fd);
                    out->mmap_shared_memory_fd = -1;
                }
            }
        } else {
            ALOGD("copl(%p):standby", out);
            out->send_next_track_params = false;
            out->is_compr_metadata_avail = false;
            out->gapless_mdata.encoder_delay = 0;
            out->gapless_mdata.encoder_padding = 0;
            if (out->compr != NULL) {
                compress_close(out->compr);
                out->compr = NULL;
            }
        }
        if (do_stop) {
            stop_output_stream(out);
        }
        // if fm is active route on selected device in UI
        audio_extn_fm_route_on_selected_device(adev, &out->device_list);
        pthread_mutex_unlock(&adev->lock);
    }
    pthread_mutex_unlock(&out->lock);
    ALOGV("%s: exit", __func__);
    return 0;
}

static int out_on_error(struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = 0;

    lock_output_stream(out);
    // always send CMD_ERROR for offload streams, this
    // is needed e.g. when SSR happens within compress_open
    // since the stream is active, offload_callback_thread is also active.
    if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
        stop_compressed_output_l(out);
    }
    pthread_mutex_unlock(&out->lock);

    status = out_standby(&out->stream.common);

    lock_output_stream(out);
    if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
        send_offload_cmd_l(out, OFFLOAD_CMD_ERROR);
    }

    if (is_offload_usecase(out->usecase) && out->card_status == CARD_STATUS_OFFLINE) {
        ALOGD("Setting previous card status if offline");
        out->prev_card_status_offline = true;
    }

    pthread_mutex_unlock(&out->lock);

    return status;
}

/*
 * standby implementation without locks, assumes that the callee already
 * has taken adev and out lock.
 */
int out_standby_l(struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;

    ALOGD("%s: enter: stream (%p) usecase(%d: %s)", __func__,
          stream, out->usecase, use_case_table[out->usecase]);

    if (!out->standby) {
        ATRACE_BEGIN("out_standby_l");
        if (adev->adm_deregister_stream)
            adev->adm_deregister_stream(adev->adm_data, out->handle);

        if (is_offload_usecase(out->usecase)) {
            stop_compressed_output_l(out);
        }

        out->standby = true;
        if (out->usecase == USECASE_COMPRESS_VOIP_CALL) {
            voice_extn_compress_voip_close_output_stream(stream);
            out->started = 0;
            ALOGD("VOIP output entered standby");
            ATRACE_END();
            return 0;
        } else if (!is_offload_usecase(out->usecase)) {
            if (out->pcm) {
                pcm_close(out->pcm);
                out->pcm = NULL;
            }
            if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS) {
                if (adev->haptic_pcm) {
                    pcm_close(adev->haptic_pcm);
                    adev->haptic_pcm = NULL;
                }

                if (adev->haptic_buffer != NULL) {
                    free(adev->haptic_buffer);
                    adev->haptic_buffer = NULL;
                    adev->haptic_buffer_size = 0;
                }
                adev->haptic_pcm_device_id = 0;
            }
        } else {
            ALOGD("copl(%p):standby", out);
            out->send_next_track_params = false;
            out->is_compr_metadata_avail = false;
            out->gapless_mdata.encoder_delay = 0;
            out->gapless_mdata.encoder_padding = 0;
            if (out->compr != NULL) {
                compress_close(out->compr);
                out->compr = NULL;
            }
        }
        stop_output_stream(out);
        ATRACE_END();
    }
    ALOGV("%s: exit", __func__);
    return 0;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    struct stream_out *out = (struct stream_out *)stream;

    // We try to get the lock for consistency,
    // but it isn't necessary for these variables.
    // If we're not in standby, we may be blocked on a write.
    const bool locked = (pthread_mutex_trylock(&out->lock) == 0);
    dprintf(fd, "      Standby: %s\n", out->standby ? "yes" : "no");
    dprintf(fd, "      Frames written: %lld\n", (long long)out->written);
#ifndef LINUX_ENABLED
    char buffer[256]; // for statistics formatting
    if (!is_offload_usecase(out->usecase)) {
        simple_stats_to_string(&out->fifo_underruns, buffer, sizeof(buffer));
        dprintf(fd, "      Fifo frame underruns: %s\n", buffer);
    }

    if (out->start_latency_ms.n > 0) {
        simple_stats_to_string(&out->start_latency_ms, buffer, sizeof(buffer));
        dprintf(fd, "      Start latency ms: %s\n", buffer);
    }
#endif
    if (locked) {
        pthread_mutex_unlock(&out->lock);
    }

#ifndef LINUX_ENABLED
    // dump error info
    (void)error_log_dump(
            out->error_log, fd, "      " /* prefix */, 0 /* lines */, 0 /* limit_ns */);
#endif
    return 0;
}

static int parse_compress_metadata(struct stream_out *out, struct str_parms *parms)
{
    int ret = 0;
    char value[32];

    if (!out || !parms) {
        ALOGE("%s: return invalid ",__func__);
        return -EINVAL;
    }

    ret = audio_extn_parse_compress_metadata(out, parms);

    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_DELAY_SAMPLES, value, sizeof(value));
    if (ret >= 0) {
        out->gapless_mdata.encoder_delay = atoi(value); //whats a good limit check?
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_PADDING_SAMPLES, value, sizeof(value));
    if (ret >= 0) {
        out->gapless_mdata.encoder_padding = atoi(value);
    }

    ALOGV("%s new encoder delay %u and padding %u", __func__,
          out->gapless_mdata.encoder_delay, out->gapless_mdata.encoder_padding);

    return 0;
}

static bool output_drives_call(struct audio_device *adev, struct stream_out *out)
{
    return out == adev->primary_output || out == adev->voice_tx_output;
}

// note: this call is safe only if the stream_cb is
// removed first in close_output_stream (as is done now).
static void out_snd_mon_cb(void * stream, struct str_parms * parms)
{
    if (!stream || !parms)
        return;

    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;

    card_status_t status;
    int card;
    if (parse_snd_card_status(parms, &card, &status) < 0)
        return;

    pthread_mutex_lock(&adev->lock);
    bool valid_cb = (card == adev->snd_card);
    pthread_mutex_unlock(&adev->lock);

    if (!valid_cb)
        return;

    lock_output_stream(out);
    if (out->card_status != status)
        out->card_status = status;
    pthread_mutex_unlock(&out->lock);

    ALOGI("out_snd_mon_cb for card %d usecase %s, status %s", card,
          use_case_table[out->usecase],
          status == CARD_STATUS_OFFLINE ? "offline" : "online");

    if (status == CARD_STATUS_OFFLINE) {
        out_on_error(stream);
        if (voice_is_call_state_active(adev) &&
            out == adev->primary_output) {
            ALOGD("%s: SSR/PDR occurred, end all calls\n", __func__);
            pthread_mutex_lock(&adev->lock);
            voice_stop_call(adev);
            adev->mode = AUDIO_MODE_NORMAL;
            pthread_mutex_unlock(&adev->lock);
        }
    }
    return;
}

int route_output_stream(struct stream_out *out,
                        struct listnode *devices)
{
    struct audio_device *adev = out->dev;
    int ret = 0;
    struct listnode new_devices;
    bool bypass_a2dp = false;
    bool reconfig = false;
    unsigned long service_interval = 0;

    ALOGD("%s: enter: usecase(%d: %s) devices %x",
          __func__, out->usecase, use_case_table[out->usecase], get_device_types(devices));

    list_init(&new_devices);
    assign_devices(&new_devices, devices);

    lock_output_stream(out);
    pthread_mutex_lock(&adev->lock);

    /*
     * When HDMI cable is unplugged the music playback is paused and
     * the policy manager sends routing=0. But the audioflinger continues
     * to write data until standby time (3sec). As the HDMI core is
     * turned off, the write gets blocked.
     * Avoid this by routing audio to speaker until standby.
     */
    if (is_single_device_type_equal(&out->device_list,
                AUDIO_DEVICE_OUT_AUX_DIGITAL) &&
            list_empty(&new_devices) &&
            !audio_extn_passthru_is_passthrough_stream(out) &&
            (platform_get_edid_info(adev->platform) != 0) /* HDMI disconnected */) {
        reassign_device_list(&new_devices, AUDIO_DEVICE_OUT_SPEAKER, "");
    }
    /*
     * When A2DP is disconnected the
     * music playback is paused and the policy manager sends routing=0
     * But the audioflinger continues to write data until standby time
     * (3sec). As BT is turned off, the write gets blocked.
     * Avoid this by routing audio to speaker until standby.
     */
    if (is_a2dp_out_device_type(&out->device_list) &&
            list_empty(&new_devices) &&
            !audio_extn_a2dp_source_is_ready() &&
            !adev->bt_sco_on) {
        reassign_device_list(&new_devices, AUDIO_DEVICE_OUT_SPEAKER, "");
    }
    /*
     * When USB headset is disconnected the music playback paused
     * and the policy manager send routing=0. But if the USB is connected
     * back before the standby time, AFE is not closed and opened
     * when USB is connected back. So routing to speker will guarantee
     * AFE reconfiguration and AFE will be opend once USB is connected again
     */
    if (is_usb_out_device_type(&out->device_list) &&
            list_empty(&new_devices) &&
            !audio_extn_usb_connected(NULL)) {
        if (adev->mode == AUDIO_MODE_IN_CALL || adev->mode == AUDIO_MODE_IN_COMMUNICATION)
            reassign_device_list(&new_devices, AUDIO_DEVICE_OUT_EARPIECE, "");
        else
            reassign_device_list(&new_devices, AUDIO_DEVICE_OUT_SPEAKER, "");
    }
    /* To avoid a2dp to sco overlapping / BT device improper state
     * check with BT lib about a2dp streaming support before routing
     */
    if (is_a2dp_out_device_type(&new_devices)) {
        if (!audio_extn_a2dp_source_is_ready()) {
            if (compare_device_type(&new_devices, AUDIO_DEVICE_OUT_SPEAKER) ||
                compare_device_type(&new_devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE)) {
                //combo usecase just by pass a2dp
                ALOGW("%s: A2DP profile is not ready,routing to speaker only", __func__);
                bypass_a2dp = true;
            } else {
                ALOGE("%s: A2DP profile is not ready,ignoring routing request", __func__);
                /* update device to a2dp and don't route as BT returned error
                 * However it is still possible a2dp routing called because
                 * of current active device disconnection (like wired headset)
                 */
                assign_devices(&out->device_list, &new_devices);
                pthread_mutex_unlock(&adev->lock);
                pthread_mutex_unlock(&out->lock);
                goto error;
            }
        }
    }

    // Workaround: If routing to an non existing usb device, fail gracefully
    // The routing request will otherwise block during 10 second
    if (is_usb_out_device_type(&new_devices)) {
        struct str_parms *parms =
            str_parms_create_str(get_usb_device_address(&new_devices));
        if (!parms)
            goto error;
        if (!audio_extn_usb_connected(NULL)) {
            ALOGW("%s: ignoring rerouting to non existing USB card", __func__);
            pthread_mutex_unlock(&adev->lock);
            pthread_mutex_unlock(&out->lock);
            str_parms_destroy(parms);
            ret = -ENOSYS;
            goto error;
        }
        str_parms_destroy(parms);
    }

    // Workaround: If routing to an non existing hdmi device, fail gracefully
    if (compare_device_type(&new_devices, AUDIO_DEVICE_OUT_AUX_DIGITAL) &&
        (platform_get_edid_info_v2(adev->platform,
                                   out->extconn.cs.controller,
                                   out->extconn.cs.stream) != 0)) {
        ALOGW("out_set_parameters() ignoring rerouting to non existing HDMI/DP");
        pthread_mutex_unlock(&adev->lock);
        pthread_mutex_unlock(&out->lock);
        ret = -ENOSYS;
        goto error;
    }

    /*
     * select_devices() call below switches all the usecases on the same
     * backend to the new device. Refer to check_usecases_codec_backend() in
     * the select_devices(). But how do we undo this?
     *
     * For example, music playback is active on headset (deep-buffer usecase)
     * and if we go to ringtones and select a ringtone, low-latency usecase
     * will be started on headset+speaker. As we can't enable headset+speaker
     * and headset devices at the same time, select_devices() switches the music
     * playback to headset+speaker while starting low-lateny usecase for ringtone.
     * So when the ringtone playback is completed, how do we undo the same?
     *
     * We are relying on the out_set_parameters() call on deep-buffer output,
     * once the ringtone playback is ended.
     * NOTE: We should not check if the current devices are same as new devices.
     *       Because select_devices() must be called to switch back the music
     *       playback to headset.
     */
    if (!list_empty(&new_devices)) {
        bool same_dev = compare_devices(&out->device_list, &new_devices);
        assign_devices(&out->device_list, &new_devices);

        if (output_drives_call(adev, out)) {
            if (!voice_is_call_state_active(adev)) {
                if (adev->mode == AUDIO_MODE_IN_CALL) {
                    adev->current_call_output = out;
                    ret = voice_start_call(adev);
                }
            } else {
                platform_is_volume_boost_supported_device(adev->platform, &new_devices);
                adev->current_call_output = out;
                voice_update_devices_for_all_voice_usecases(adev);
            }
        }

        if (is_usb_out_device_type(&out->device_list)) {
             service_interval = audio_extn_usb_find_service_interval(false, true /*playback*/);
             audio_extn_usb_set_service_interval(true /*playback*/,
                                                 service_interval,
                                                 &reconfig);
             ALOGD("%s, svc_int(%ld),reconfig(%d)",__func__,service_interval, reconfig);
        }

        if (!out->standby) {
            if (!same_dev) {
                ALOGV("update routing change");
                audio_extn_perf_lock_acquire(&adev->perf_lock_handle, 0,
                                             adev->perf_lock_opts,
                                             adev->perf_lock_opts_size);
                if (adev->adm_on_routing_change)
                    adev->adm_on_routing_change(adev->adm_data,
                                                out->handle);
            }
            if (!bypass_a2dp) {
                select_devices(adev, out->usecase);
            } else {
                if (compare_device_type(&new_devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE))
                    reassign_device_list(&out->device_list, AUDIO_DEVICE_OUT_SPEAKER_SAFE, "");
                else
                    reassign_device_list(&out->device_list, AUDIO_DEVICE_OUT_SPEAKER, "");
                select_devices(adev, out->usecase);
                assign_devices(&out->device_list, &new_devices);
            }

            if (!same_dev) {
                // on device switch force swap, lower functions will make sure
                // to check if swap is allowed or not.
                platform_set_swap_channels(adev, true);
                audio_extn_perf_lock_release(&adev->perf_lock_handle);
            }
            pthread_mutex_lock(&out->latch_lock);
            if (!is_a2dp_out_device_type(&out->device_list) || audio_extn_a2dp_source_is_ready()) {
                if (out->a2dp_muted) {
                    out->a2dp_muted = false;
                    if (is_offload_usecase(out->usecase))
                        out_set_compr_volume(&out->stream, out->volume_l, out->volume_r);
                    else if (out->usecase != USECASE_AUDIO_PLAYBACK_VOIP)
                        out_set_pcm_volume(&out->stream, out->volume_l, out->volume_r);
                }
            }
            if (out->usecase == USECASE_AUDIO_PLAYBACK_VOIP && !out->a2dp_muted)
                out_set_voip_volume(&out->stream, out->volume_l, out->volume_r);
            pthread_mutex_unlock(&out->latch_lock);
        }
    }

    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&out->lock);

    /*handles device and call state changes*/
    audio_extn_extspk_update(adev->extspk);

error:
    ALOGV("%s: exit: code(%d)", __func__, ret);
    return ret;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    struct str_parms *parms;
    char value[32];
    int ret = 0, err;
    int ext_controller = -1;
    int ext_stream = -1;

    ALOGD("%s: enter: usecase(%d: %s) kvpairs: %s",
          __func__, out->usecase, use_case_table[out->usecase], kvpairs);
    parms = str_parms_create_str(kvpairs);
    if (!parms)
        goto error;

    err = platform_get_controller_stream_from_params(parms, &ext_controller,
                                                       &ext_stream);
    if (err == 0) {
        out->extconn.cs.controller = ext_controller;
        out->extconn.cs.stream = ext_stream;
        ALOGD("%s: usecase(%s) new controller/stream (%d/%d)", __func__,
              use_case_table[out->usecase], out->extconn.cs.controller,
              out->extconn.cs.stream);
    }

    if (out == adev->primary_output) {
        pthread_mutex_lock(&adev->lock);
        audio_extn_set_parameters(adev, parms);
        pthread_mutex_unlock(&adev->lock);
    }
    if (is_offload_usecase(out->usecase)) {
        lock_output_stream(out);
        parse_compress_metadata(out, parms);

        audio_extn_dts_create_state_notifier_node(out->usecase);
        audio_extn_dts_notify_playback_state(out->usecase, 0, out->sample_rate,
                                                 popcount(out->channel_mask),
                                                 out->playback_started);

        pthread_mutex_unlock(&out->lock);
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_DUAL_MONO, value,
                            sizeof(value));
    if (err >= 0) {
        if (!strncmp("true", value, sizeof("true")) || atoi(value))
            audio_extn_send_dual_mono_mixing_coefficients(out);
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_PROFILE, value, sizeof(value));
    if (err >= 0) {
        strlcpy(out->profile, value, sizeof(out->profile));
        ALOGV("updating stream profile with value '%s'", out->profile);
        lock_output_stream(out);
        audio_extn_utils_update_stream_output_app_type_cfg(adev->platform,
                                                          &adev->streams_output_cfg_list,
                                                          &out->device_list, out->flags,
                                                          out->hal_op_format,
                                                          out->sample_rate, out->bit_width,
                                                          out->channel_mask, out->profile,
                                                          &out->app_type_cfg);
        pthread_mutex_unlock(&out->lock);
    }

    //suspend, resume handling block
    //remove QOS only if vendor.audio.hal.dynamic.qos.config.supported is set to true
    // and vendor.audio.hal.output.suspend.supported is set to true
    if (out->hal_output_suspend_supported && out->dynamic_pm_qos_config_supported) {
        //check suspend parameter only for low latency and if the property
        //is enabled
        if (str_parms_get_str(parms, "suspend_playback", value, sizeof(value)) >= 0) {
            ALOGI("%s: got suspend_playback %s", __func__, value);
            lock_output_stream(out);
            if (!strncmp(value, "false", 5)) {
                //suspend_playback=false is supposed to set QOS value back to 75%
                //the mixer control sent with value Enable will achieve that
                ret = audio_route_apply_and_update_path(adev->audio_route, out->pm_qos_mixer_path);
            } else if (!strncmp (value, "true", 4)) {
                //suspend_playback=true is supposed to remove QOS value
                //resetting the mixer control will set the default value
                //for the mixer control which is Disable and this removes the QOS vote
                ret = audio_route_reset_and_update_path(adev->audio_route, out->pm_qos_mixer_path);
            } else {
                ALOGE("%s: Wrong value sent for suspend_playback, expected true/false,"
                       " got %s", __func__, value);
                ret = -1;
            }

            if (ret != 0) {
                ALOGE("%s: %s mixer ctl failed with %d, ignore suspend/resume setparams",
                        __func__, out->pm_qos_mixer_path, ret);
            }

            pthread_mutex_unlock(&out->lock);
        }
    }

    //end suspend, resume handling block
    str_parms_destroy(parms);
error:
    ALOGV("%s: exit: code(%d)", __func__, ret);
    return ret;
}

static int in_set_microphone_direction(const struct audio_stream_in *stream,
                                           audio_microphone_direction_t dir) {
    struct stream_in *in = (struct stream_in *)stream;

    ALOGVV("%s: standby %d source %d dir %d", __func__, in->standby, in->source, dir);

    in->direction = dir;

    if (in->standby)
        return 0;

    return audio_extn_audiozoom_set_microphone_direction(in, dir);
}

static int in_set_microphone_field_dimension(const struct audio_stream_in *stream, float zoom) {
    struct stream_in *in = (struct stream_in *)stream;

    ALOGVV("%s: standby %d source %d zoom %f", __func__, in->standby, in->source, zoom);

    if (zoom > 1.0 || zoom < -1.0)
        return -EINVAL;

    in->zoom = zoom;

    if (in->standby)
        return 0;

    return audio_extn_audiozoom_set_microphone_field_dimension(in, zoom);
}


static bool stream_get_parameter_channels(struct str_parms *query,
                                          struct str_parms *reply,
                                          audio_channel_mask_t *supported_channel_masks) {
    int ret = -1;
    char value[512];
    bool first = true;
    size_t i, j;

    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS)) {
        ret = 0;
        value[0] = '\0';
        i = 0;
        while (supported_channel_masks[i] != 0) {
            for (j = 0; j < ARRAY_SIZE(channels_name_to_enum_table); j++) {
                if (channels_name_to_enum_table[j].value == supported_channel_masks[i]) {
                    if (!first)
                        strlcat(value, "|", sizeof(value));

                    strlcat(value, channels_name_to_enum_table[j].name, sizeof(value));
                    first = false;
                    break;
                }
            }
            i++;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value);
    }
    return ret == 0;
}

static bool stream_get_parameter_formats(struct str_parms *query,
                                         struct str_parms *reply,
                                         audio_format_t *supported_formats) {
    int ret = -1;
    char value[256];
    size_t i, j;
    bool first = true;

    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
        ret = 0;
        value[0] = '\0';
        i = 0;
        while (supported_formats[i] != 0) {
            for (j = 0; j < ARRAY_SIZE(formats_name_to_enum_table); j++) {
                if (formats_name_to_enum_table[j].value == supported_formats[i]) {
                    if (!first) {
                        strlcat(value, "|", sizeof(value));
                    }
                    strlcat(value, formats_name_to_enum_table[j].name, sizeof(value));
                    first = false;
                    break;
                }
            }
            i++;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_FORMATS, value);
    }
    return ret == 0;
}

static bool stream_get_parameter_rates(struct str_parms *query,
                                       struct str_parms *reply,
                                       uint32_t *supported_sample_rates) {

    int i;
    char value[256];
    int ret = -1;
    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES)) {
        ret = 0;
        value[0] = '\0';
        i=0;
        int cursor = 0;
        while (supported_sample_rates[i]) {
            int avail = sizeof(value) - cursor;
            ret = snprintf(value + cursor, avail, "%s%d",
                           cursor > 0 ? "|" : "",
                           supported_sample_rates[i]);
            if (ret < 0 || ret >= avail) {
                // if cursor is at the last element of the array
                //    overwrite with \0 is duplicate work as
                //    snprintf already put a \0 in place.
                // else
                //    we had space to write the '|' at value[cursor]
                //    (which will be overwritten) or no space to fill
                //    the first element (=> cursor == 0)
                value[cursor] = '\0';
                break;
            }
            cursor += ret;
            ++i;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES,
                          value);
    }
    return ret >= 0;
}

static char* out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct str_parms *query = str_parms_create_str(keys);
    char *str = (char*) NULL;
    char value[256];
    struct str_parms *reply = str_parms_create();
    size_t i, j;
    int ret;
    bool first = true;

    if (!query || !reply) {
        if (reply) {
            str_parms_destroy(reply);
        }
        if (query) {
            str_parms_destroy(query);
        }
        ALOGE("out_get_parameters: failed to allocate mem for query or reply");
        return NULL;
    }

    ALOGV("%s: %s enter: keys - %s", __func__, use_case_table[out->usecase], keys);
    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';
        i = 0;
        while (out->supported_channel_masks[i] != 0) {
            for (j = 0; j < ARRAY_SIZE(channels_name_to_enum_table); j++) {
                if (channels_name_to_enum_table[j].value == out->supported_channel_masks[i]) {
                    if (!first) {
                        strlcat(value, "|", sizeof(value));
                    }
                    strlcat(value, channels_name_to_enum_table[j].name, sizeof(value));
                    first = false;
                    break;
                }
            }
            i++;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value);
        str = str_parms_to_str(reply);
    } else {
        voice_extn_out_get_parameters(out, query, reply);
        str = str_parms_to_str(reply);
    }


    ret = str_parms_get_str(query, "is_direct_pcm_track", value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';
        if (out->flags & AUDIO_OUTPUT_FLAG_DIRECT &&
            !(out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)) {
            ALOGV("in direct_pcm");
            strlcat(value, "true", sizeof(value));
        } else {
            ALOGV("not in direct_pcm");
            strlcat(value, "false", sizeof(value));
        }
        str_parms_add_str(reply, "is_direct_pcm_track", value);
        if (str)
            free(str);
        str = str_parms_to_str(reply);
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS, value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';
        i = 0;
        first = true;
        while (out->supported_formats[i] != 0) {
            for (j = 0; j < ARRAY_SIZE(formats_name_to_enum_table); j++) {
                if (formats_name_to_enum_table[j].value == out->supported_formats[i]) {
                    if (!first) {
                        strlcat(value, "|", sizeof(value));
                    }
                    strlcat(value, formats_name_to_enum_table[j].name, sizeof(value));
                    first = false;
                    break;
                }
            }
            i++;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_FORMATS, value);
        if (str)
            free(str);
        str = str_parms_to_str(reply);
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';
        i = 0;
        first = true;
        while (out->supported_sample_rates[i] != 0) {
            for (j = 0; j < ARRAY_SIZE(out_sample_rates_name_to_enum_table); j++) {
                if (out_sample_rates_name_to_enum_table[j].value == out->supported_sample_rates[i]) {
                    if (!first) {
                        strlcat(value, "|", sizeof(value));
                    }
                    strlcat(value, out_sample_rates_name_to_enum_table[j].name, sizeof(value));
                    first = false;
                    break;
                }
            }
            i++;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, value);
        if (str)
            free(str);
        str = str_parms_to_str(reply);
    }

    if (str_parms_get_str(query, "supports_hw_suspend", value, sizeof(value)) >= 0) {
        //only low latency track supports suspend_resume
        str_parms_add_int(reply, "supports_hw_suspend",
                (out->hal_output_suspend_supported));
        if (str)
            free(str);
        str = str_parms_to_str(reply);
    }


    str_parms_destroy(query);
    str_parms_destroy(reply);
    ALOGV("%s: exit: returns - %s", __func__, str);
    return str;
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    uint32_t period_ms;
    struct stream_out *out = (struct stream_out *)stream;
    uint32_t latency = 0;

    if (is_offload_usecase(out->usecase)) {
        lock_output_stream(out);
        latency = audio_extn_utils_compress_get_dsp_latency(out);
        pthread_mutex_unlock(&out->lock);
    } else if ((out->realtime) ||
            (out->usecase == USECASE_AUDIO_PLAYBACK_MMAP)) {
        // since the buffer won't be filled up faster than realtime,
        // return a smaller number
        if (out->config.rate)
            period_ms = (out->af_period_multiplier * out->config.period_size *
                         1000) / (out->config.rate);
        else
            period_ms = 0;
        latency = period_ms + platform_render_latency(out) / 1000;
    } else {
        latency = (out->config.period_count * out->config.period_size * 1000) /
                   (out->config.rate);
        if (out->usecase == USECASE_AUDIO_PLAYBACK_DEEP_BUFFER)
            latency += platform_render_latency(out)/1000;
    }

    if (!out->standby && is_a2dp_out_device_type(&out->device_list))
        latency += audio_extn_a2dp_get_encoder_latency();

    ALOGV("%s: Latency %d", __func__, latency);
    return latency;
}

static float AmpToDb(float amplification)
{
    float db = DSD_VOLUME_MIN_DB;
    if (amplification > 0) {
        db = 20 * log10(amplification);
        if(db < DSD_VOLUME_MIN_DB)
            return DSD_VOLUME_MIN_DB;
    }
    return db;
}

static int out_set_mmap_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    struct stream_out *out = (struct stream_out *)stream;
    long volume = 0;
    char mixer_ctl_name[128] = "";
    struct audio_device *adev = out->dev;
    struct mixer_ctl *ctl = NULL;
    int pcm_device_id = platform_get_pcm_device_id(out->usecase,
                                               PCM_PLAYBACK);

    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
             "Playback %d Volume", pcm_device_id);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }
    if (left != right)
        ALOGW("%s: Left and right channel volume mismatch:%f,%f",
                 __func__, left, right);
    volume = (long)(left * (MMAP_PLAYBACK_VOLUME_MAX*1.0));
    if (mixer_ctl_set_value(ctl, 0, volume) < 0){
        ALOGE("%s:ctl for mixer cmd - %s, volume %ld returned error",
           __func__, mixer_ctl_name, volume);
        return -EINVAL;
    }
    return 0;
}

static int out_set_compr_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    struct stream_out *out = (struct stream_out *)stream;
    long volume[2];
    char mixer_ctl_name[128];
    struct audio_device *adev = out->dev;
    struct mixer_ctl *ctl;
    int pcm_device_id = platform_get_pcm_device_id(out->usecase,
                                               PCM_PLAYBACK);

    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
             "Compress Playback %d Volume", pcm_device_id);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }
    ALOGE("%s:ctl for mixer cmd - %s, left %f, right %f",
           __func__, mixer_ctl_name, left, right);
    volume[0] = (int)(left * COMPRESS_PLAYBACK_VOLUME_MAX);
    volume[1] = (int)(right * COMPRESS_PLAYBACK_VOLUME_MAX);
    mixer_ctl_set_array(ctl, volume, sizeof(volume)/sizeof(volume[0]));

    return 0;
}

static int out_set_voip_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    struct stream_out *out = (struct stream_out *)stream;
    char mixer_ctl_name[] = "App Type Gain";
    struct audio_device *adev = out->dev;
    struct mixer_ctl *ctl;
    long set_values[4];

    if (!is_valid_volume(left, right)) {
        ALOGE("%s: Invalid stream volume for left=%f, right=%f",
                   __func__, left, right);
        return -EINVAL;
    }

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
               __func__, mixer_ctl_name);
        return -EINVAL;
    }

    set_values[0] = 0; //0: Rx Session 1:Tx Session
    set_values[1] = out->app_type_cfg.app_type;
    set_values[2] = (long)(left * VOIP_PLAYBACK_VOLUME_MAX);
    set_values[3] = (long)(right * VOIP_PLAYBACK_VOLUME_MAX);

    mixer_ctl_set_array(ctl, set_values, ARRAY_SIZE(set_values));
    return 0;
}

static int out_set_pcm_volume(struct audio_stream_out *stream, float left,
                              float right)
{
    struct stream_out *out = (struct stream_out *)stream;
    /* Volume control for pcm playback */
    if (left != right) {
        return -EINVAL;
    } else {
        char mixer_ctl_name[128];
        struct audio_device *adev = out->dev;
        struct mixer_ctl *ctl;
        int pcm_device_id = platform_get_pcm_device_id(out->usecase, PCM_PLAYBACK);
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "Playback %d Volume", pcm_device_id);
        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s : Could not get ctl for mixer cmd - %s", __func__, mixer_ctl_name);
            return -EINVAL;
        }

        int volume = (int) (left * PCM_PLAYBACK_VOLUME_MAX);
        int ret = mixer_ctl_set_value(ctl, 0, volume);
        if (ret < 0) {
            ALOGE("%s: Could not set ctl, error:%d ", __func__, ret);
            return -EINVAL;
        }

        ALOGV("%s : Pcm set volume value %d left %f", __func__, volume, left);

        return 0;
    }
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    struct stream_out *out = (struct stream_out *)stream;
    int volume[2];
    int ret = 0;

    ALOGD("%s: called with left_vol=%f, right_vol=%f", __func__, left, right);
    if (out->usecase == USECASE_AUDIO_PLAYBACK_MULTI_CH) {
        /* only take left channel into account: the API is for stereo anyway */
        out->muted = (left == 0.0f);
        return 0;
    } else if (is_offload_usecase(out->usecase)) {
        if (audio_extn_passthru_is_passthrough_stream(out)) {
            /*
             * Set mute or umute on HDMI passthrough stream.
             * Only take left channel into account.
             * Mute is 0 and unmute 1
             */
            audio_extn_passthru_set_volume(out, (left == 0.0f));
        } else if (out->format == AUDIO_FORMAT_DSD){
            char mixer_ctl_name[128] =  "DSD Volume";
            struct audio_device *adev = out->dev;
            struct mixer_ctl *ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);

            if (!ctl) {
                ALOGE("%s: Could not get ctl for mixer cmd - %s",
                      __func__, mixer_ctl_name);
                return -EINVAL;
            }
            volume[0] = (long)(AmpToDb(left));
            volume[1] = (long)(AmpToDb(right));
            mixer_ctl_set_array(ctl, volume, sizeof(volume)/sizeof(volume[0]));
            return 0;
        } else if (compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_BUS) &&
                (out->car_audio_stream == CAR_AUDIO_STREAM_MEDIA)) {
            ALOGD("%s: Overriding offload set volume for media bus stream", __func__);
            struct listnode *node = NULL;
            list_for_each(node, &adev->active_outputs_list) {
                streams_output_ctxt_t *out_ctxt = node_to_item(node,
                                                    streams_output_ctxt_t,
                                                    list);
                if (out_ctxt->output->usecase == USECASE_AUDIO_PLAYBACK_MEDIA) {
                    out->volume_l = out_ctxt->output->volume_l;
                    out->volume_r = out_ctxt->output->volume_r;
                }
            }
            pthread_mutex_lock(&out->latch_lock);
            if (!out->a2dp_muted) {
                ret = out_set_compr_volume(&out->stream, out->volume_l, out->volume_r);
            }
            pthread_mutex_unlock(&out->latch_lock);
            return ret;
        } else {
            pthread_mutex_lock(&out->latch_lock);
            ALOGV("%s: compress mute %d", __func__, out->a2dp_muted);
            if (!out->a2dp_muted)
                ret = out_set_compr_volume(stream, left, right);
            out->volume_l = left;
            out->volume_r = right;
            pthread_mutex_unlock(&out->latch_lock);
            return ret;
        }
    } else if (out->usecase == USECASE_AUDIO_PLAYBACK_VOIP) {
        out->app_type_cfg.gain[0] = (int)(left * VOIP_PLAYBACK_VOLUME_MAX);
        out->app_type_cfg.gain[1] = (int)(right * VOIP_PLAYBACK_VOLUME_MAX);
        pthread_mutex_lock(&out->latch_lock);
        if (!out->standby) {
            audio_extn_utils_send_app_type_gain(out->dev,
                                                out->app_type_cfg.app_type,
                                                &out->app_type_cfg.gain[0]);
            if (!out->a2dp_muted)
                ret = out_set_voip_volume(stream, left, right);
        }
        out->volume_l = left;
        out->volume_r = right;
        pthread_mutex_unlock(&out->latch_lock);
        return ret;
    } else if (out->usecase == USECASE_AUDIO_PLAYBACK_MMAP) {
        ALOGV("%s: MMAP set volume called", __func__);
        if (!out->standby)
            ret = out_set_mmap_volume(stream, left, right);
        out->volume_l = left;
        out->volume_r = right;
        return ret;
    } else if (out->usecase == USECASE_AUDIO_PLAYBACK_LOW_LATENCY ||
               out->usecase == USECASE_AUDIO_PLAYBACK_DEEP_BUFFER ||
               out->usecase == USECASE_AUDIO_PLAYBACK_ULL) {
        pthread_mutex_lock(&out->latch_lock);
        /* Volume control for pcm playback */
        if (!out->standby && !out->a2dp_muted)
            ret = out_set_pcm_volume(stream, left, right);
        else
            out->apply_volume = true;

        out->volume_l = left;
        out->volume_r = right;
        pthread_mutex_unlock(&out->latch_lock);
        return ret;
    } else if (audio_extn_auto_hal_is_bus_device_usecase(out->usecase)) {
        ALOGV("%s: bus device set volume called", __func__);
        pthread_mutex_lock(&out->latch_lock);
        if (!out->standby && !out->a2dp_muted)
            ret = out_set_pcm_volume(stream, left, right);
        out->volume_l = left;
        out->volume_r = right;
        pthread_mutex_unlock(&out->latch_lock);
        return ret;
    }

    return -ENOSYS;
}

static void update_frames_written(struct stream_out *out, size_t bytes)
{
    size_t bpf = 0;

    if (is_offload_usecase(out->usecase) && !out->non_blocking &&
        !(out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD))
        bpf = 1;
    else if (!is_offload_usecase(out->usecase))
        bpf = audio_bytes_per_sample(out->format) *
             audio_channel_count_from_out_mask(out->channel_mask);

    pthread_mutex_lock(&out->position_query_lock);
    if (bpf != 0) {
        out->written += bytes / bpf;
        clock_gettime(CLOCK_MONOTONIC, &out->writeAt);
    }
    pthread_mutex_unlock(&out->position_query_lock);
}

int split_and_write_audio_haptic_data(struct stream_out *out,
                 const void *buffer, size_t bytes_to_write)
{
    struct audio_device *adev = out->dev;

    int ret = 0;
    size_t channel_count = audio_channel_count_from_out_mask(out->channel_mask);
    size_t bytes_per_sample = audio_bytes_per_sample(out->format);
    size_t frame_size = channel_count * bytes_per_sample;
    size_t frame_count = bytes_to_write / frame_size;

    bool force_haptic_path =
         property_get_bool("vendor.audio.test_haptic", false);

    // extract Haptics data from Audio buffer
    bool   alloc_haptic_buffer = false;
    int    haptic_channel_count = adev->haptics_config.channels;
    size_t haptic_frame_size = bytes_per_sample * haptic_channel_count;
    size_t audio_frame_size = frame_size - haptic_frame_size;
    size_t total_haptic_buffer_size = frame_count * haptic_frame_size;

    if (adev->haptic_buffer == NULL) {
        alloc_haptic_buffer = true;
    } else if (adev->haptic_buffer_size < total_haptic_buffer_size) {
        free(adev->haptic_buffer);
        adev->haptic_buffer_size = 0;
        alloc_haptic_buffer = true;
    }

    if (alloc_haptic_buffer) {
        adev->haptic_buffer = (uint8_t *)calloc(1, total_haptic_buffer_size);
        if(adev->haptic_buffer == NULL) {
            ALOGE("%s: failed to allocate mem for dev->haptic_buffer", __func__);
            return -ENOMEM;
        }
        adev->haptic_buffer_size = total_haptic_buffer_size;
    }

    size_t src_index = 0, aud_index = 0, hap_index = 0;
    uint8_t *audio_buffer = (uint8_t *)buffer;
    uint8_t *haptic_buffer  = adev->haptic_buffer;

    // This is required for testing only. This works for stereo data only.
    // One channel is fed to audio stream and other to haptic stream for testing.
    if (force_haptic_path)
       audio_frame_size = haptic_frame_size = bytes_per_sample;

    for (size_t i = 0; i < frame_count; i++) {
        memcpy(audio_buffer + aud_index, audio_buffer + src_index,
               audio_frame_size);
        aud_index += audio_frame_size;
        src_index += audio_frame_size;

        if (adev->haptic_pcm)
            memcpy(haptic_buffer + hap_index, audio_buffer + src_index,
                   haptic_frame_size);
        hap_index += haptic_frame_size;
        src_index += haptic_frame_size;

        // This is required for testing only.
        // Discard haptic channel data.
        if (force_haptic_path)
            src_index += haptic_frame_size;
    }

    // write to audio pipeline
    ret = pcm_write(out->pcm, (void *)audio_buffer,
                    frame_count * audio_frame_size);

    // write to haptics pipeline
    if (adev->haptic_pcm)
        ret = pcm_write(adev->haptic_pcm, (void *)adev->haptic_buffer,
                        frame_count * haptic_frame_size);

    return ret;
}

#ifdef NO_AUDIO_OUT
static ssize_t out_write_for_no_output(struct audio_stream_out *stream,
                                       const void *buffer __unused, size_t bytes)
{
    struct stream_out *out = (struct stream_out *)stream;

    /* No Output device supported other than BT for playback.
     * Sleep for the amount of buffer duration
     */
    lock_output_stream(out);
    usleep(bytes * 1000000 / audio_stream_out_frame_size(
            (const struct audio_stream_out *)&out->stream) /
            out_get_sample_rate(&out->stream.common));
    pthread_mutex_unlock(&out->lock);
    return bytes;
}
#endif

static ssize_t out_write(struct audio_stream_out *stream, const void *buffer,
                         size_t bytes)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    ssize_t ret = 0;
    int channels = 0;
    const size_t frame_size = audio_stream_out_frame_size(stream);
    const size_t frames = (frame_size != 0) ? bytes / frame_size : bytes;
    struct audio_usecase *usecase = NULL;
    uint32_t compr_passthr = 0;

    ATRACE_BEGIN("out_write");
    lock_output_stream(out);

    if (CARD_STATUS_OFFLINE == out->card_status) {

        if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
            /*during SSR for compress usecase we should return error to flinger*/
            ALOGD(" copl %s: sound card is not active/SSR state", __func__);
            pthread_mutex_unlock(&out->lock);
            ATRACE_END();
            return -ENETRESET;
        } else {
            ALOGD(" %s: sound card is not active/SSR state", __func__);
            ret= -EIO;
            goto exit;
        }
    }

    if (audio_extn_passthru_should_drop_data(out)) {
        ALOGV(" %s : Drop data as compress passthrough session is going on", __func__);
        ret = -EIO;
        goto exit;
    }

    if (out->usecase == USECASE_AUDIO_PLAYBACK_MMAP) {
        ret = -EINVAL;
        goto exit;
    }

    if (compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_AUX_DIGITAL) &&
         !out->is_iec61937_info_available) {

        if (!audio_extn_passthru_is_passthrough_stream(out)) {
            out->is_iec61937_info_available = true;
        } else if (audio_extn_passthru_is_enabled()) {
            audio_extn_passthru_update_stream_configuration(adev, out, buffer, bytes);
            out->is_iec61937_info_available = true;

            if((out->format == AUDIO_FORMAT_DTS) ||
               (out->format == AUDIO_FORMAT_DTS_HD)) {
                ret = audio_extn_passthru_update_dts_stream_configuration(out,
                                                                buffer, bytes);
                if (ret) {
                    if (ret != -ENOSYS) {
                        out->is_iec61937_info_available = false;
                        ALOGD("iec61937 transmission info not yet updated retry");
                    }
                } else if (!out->standby) {
                    /* if stream has started and after that there is
                     * stream config change (iec transmission config)
                     * then trigger select_device to update backend configuration.
                     */
                    out->stream_config_changed = true;
                    pthread_mutex_lock(&adev->lock);
                    select_devices(adev, out->usecase);
                    if (!audio_extn_passthru_is_supported_backend_edid_cfg(adev, out)) {
                        pthread_mutex_unlock(&adev->lock);
                        ret = -EINVAL;
                        goto exit;
                    }
                    pthread_mutex_unlock(&adev->lock);
                    out->stream_config_changed = false;
                    out->is_iec61937_info_available = true;
                }
            }

#ifdef AUDIO_GKI_ENABLED
            /* out->compr_config.codec->reserved[0] is for compr_passthr */
            compr_passthr = out->compr_config.codec->reserved[0];
#else
            compr_passthr = out->compr_config.codec->compr_passthr;
#endif

            if ((channels < (int)audio_channel_count_from_out_mask(out->channel_mask)) &&
                (compr_passthr == PASSTHROUGH) &&
                (out->is_iec61937_info_available == true)) {
                    ALOGE("%s: ERROR: Unsupported channel config in passthrough mode", __func__);
                    ret = -EINVAL;
                    goto exit;
            }
        }
    }

    if (is_a2dp_out_device_type(&out->device_list) &&
        (audio_extn_a2dp_source_is_suspended())) {
        if (!(compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_SPEAKER) ||
              compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_SPEAKER_SAFE))) {
            if (!is_offload_usecase(out->usecase)) {
                ret = -EIO;
                goto exit;
            }
        }
    }

    if (is_usb_out_device_type(&out->device_list) &&
            !audio_extn_usb_connected(NULL)) {
        ret = -EIO;
        goto exit;
    }

    if (out->standby) {
        out->standby = false;
        const int64_t startNs = systemTime(SYSTEM_TIME_MONOTONIC);

        pthread_mutex_lock(&adev->lock);
        if (out->usecase == USECASE_COMPRESS_VOIP_CALL)
            ret = voice_extn_compress_voip_start_output_stream(out);
        else
            ret = start_output_stream(out);
        /* ToDo: If use case is compress offload should return 0 */
        if (ret != 0) {
            out->standby = true;
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
        out->started = 1;
        out->last_fifo_valid = false; // we're coming out of standby, last_fifo isn't valid.

        if ((last_known_cal_step != -1) && (adev->platform != NULL)) {
            ALOGD("%s: retry previous failed cal level set", __func__);
            platform_send_gain_dep_cal(adev->platform, last_known_cal_step);
            last_known_cal_step = -1;
        }
        pthread_mutex_unlock(&adev->lock);

        if ((out->is_iec61937_info_available == true) &&
            (audio_extn_passthru_is_passthrough_stream(out))&&
            (!audio_extn_passthru_is_supported_backend_edid_cfg(adev, out))) {
            ret = -EINVAL;
            goto exit;
        }
        if (out->set_dual_mono)
            audio_extn_send_dual_mono_mixing_coefficients(out);

#ifndef LINUX_ENABLED
        // log startup time in ms.
        simple_stats_log(
                &out->start_latency_ms, (systemTime(SYSTEM_TIME_MONOTONIC) - startNs) * 1e-6);
#endif
    }

    if (adev->is_channel_status_set == false &&
            compare_device_type(&out->device_list,
                                AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
        audio_utils_set_hdmi_channel_status(out, (void *)buffer, bytes);
        adev->is_channel_status_set = true;
    }

    if ((adev->use_old_pspd_mix_ctrl == true) &&
        (out->pspd_coeff_sent == false)) {
        /*
         * Need to resend pspd coefficients after stream started for
         * older kernel version as it does not save the coefficients
         * and also stream has to be started for coeff to apply.
         */
        usecase = get_usecase_from_list(adev, out->usecase);
        if (usecase != NULL) {
            audio_extn_set_custom_mtmx_params_v2(adev, usecase, true);
            out->pspd_coeff_sent = true;
        }
    }

    if (is_offload_usecase(out->usecase)) {
        ALOGVV("copl(%p): writing buffer (%zu bytes) to compress device", out, bytes);
        if (out->send_new_metadata) {
            ALOGD("copl(%p):send new gapless metadata", out);
            compress_set_gapless_metadata(out->compr, &out->gapless_mdata);
            out->send_new_metadata = 0;
            if (out->send_next_track_params && out->is_compr_metadata_avail) {
                ALOGD("copl(%p):send next track params in gapless", out);
                compress_set_next_track_param(out->compr, &(out->compr_config.codec->options));
                out->send_next_track_params = false;
                out->is_compr_metadata_avail = false;
            }
        }
        if (!(out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) &&
                      (out->convert_buffer) != NULL) {

            if ((bytes > out->hal_fragment_size)) {
                ALOGW("Error written bytes %zu > %d (fragment_size)",
                       bytes, out->hal_fragment_size);
                pthread_mutex_unlock(&out->lock);
                ATRACE_END();
                return -EINVAL;
            } else {
                audio_format_t dst_format = out->hal_op_format;
                audio_format_t src_format = out->hal_ip_format;

                /* prevent division-by-zero */
                uint32_t bitwidth_src = format_to_bitwidth_table[src_format];
                uint32_t bitwidth_dst = format_to_bitwidth_table[dst_format];
                if ((bitwidth_src == 0) || (bitwidth_dst == 0)) {
                    ALOGE("%s: Error bitwidth == 0", __func__);
                    pthread_mutex_unlock(&out->lock);
                    ATRACE_END();
                    return -EINVAL;
                }

                uint32_t frames = bytes / format_to_bitwidth_table[src_format];
                uint32_t bytes_to_write = frames * format_to_bitwidth_table[dst_format];

                memcpy_by_audio_format(out->convert_buffer,
                                       dst_format,
                                       buffer,
                                       src_format,
                                       frames);

                ret = compress_write(out->compr, out->convert_buffer,
                                     bytes_to_write);

                /*Convert written bytes in audio flinger format*/
                if (ret > 0)
                    ret = ((ret * format_to_bitwidth_table[out->format]) /
                           format_to_bitwidth_table[dst_format]);
            }
        } else
            ret = compress_write(out->compr, buffer, bytes);

        if ((ret < 0 || ret == (ssize_t)bytes) && !out->non_blocking)
            update_frames_written(out, bytes);

        if (ret < 0)
            ret = -errno;
        ALOGVV("%s: writing buffer (%zu bytes) to compress device returned %d", __func__, bytes, (int)ret);
        /*msg to cb thread only if non blocking write is enabled*/
        if (ret >= 0 && ret < (ssize_t)bytes && out->non_blocking) {
            ALOGD("No space available in compress driver, post msg to cb thread");
            send_offload_cmd_l(out, OFFLOAD_CMD_WAIT_FOR_BUFFER);
        } else if (-ENETRESET == ret) {
            ALOGE("copl %s: received sound card offline state on compress write", __func__);
            out->card_status = CARD_STATUS_OFFLINE;
            pthread_mutex_unlock(&out->lock);
            out_on_error(&out->stream.common);
            ATRACE_END();
            return ret;
        }

        /* Call compr start only when non-zero bytes of data is there to be rendered */
        if (!out->playback_started && ret > 0) {
            int status = compress_start(out->compr);
            if (status < 0) {
                ret = status;
                ALOGE("%s: compr start failed with err %d", __func__, errno);
                goto exit;
            }
            audio_extn_dts_eagle_fade(adev, true, out);
            out->playback_started = 1;
            pthread_mutex_lock(&out->latch_lock);
            out->offload_state = OFFLOAD_STATE_PLAYING;
            pthread_mutex_unlock(&out->latch_lock);

            audio_extn_dts_notify_playback_state(out->usecase, 0, out->sample_rate,
                                                     popcount(out->channel_mask),
                                                     out->playback_started);
        }
        pthread_mutex_unlock(&out->lock);
        ATRACE_END();
        return ret;
    } else {
        if (out->pcm) {
            size_t bytes_to_write = bytes;
            if (out->muted)
                memset((void *)buffer, 0, bytes);
            ALOGV("%s: frames=%zu, frame_size=%zu, bytes_to_write=%zu",
                     __func__, frames, frame_size, bytes_to_write);

            if (out->usecase == USECASE_INCALL_MUSIC_UPLINK ||
                out->usecase == USECASE_INCALL_MUSIC_UPLINK2 ||
                (out->usecase == USECASE_AUDIO_PLAYBACK_VOIP &&
                 !audio_extn_utils_is_vendor_enhanced_fwk())) {
                size_t channel_count = audio_channel_count_from_out_mask(out->channel_mask);
                int16_t *src = (int16_t *)buffer;
                int16_t *dst = (int16_t *)buffer;

                LOG_ALWAYS_FATAL_IF(channel_count > 2 ||
                                    out->format != AUDIO_FORMAT_PCM_16_BIT,
                                    "out_write called for %s use case with wrong properties",
                                    use_case_table[out->usecase]);

                /*
                 * FIXME: this can be removed once audio flinger mixer supports
                 * mono output
                 */

                /*
                 * Code below goes over each frame in the buffer and adds both
                 * L and R samples and then divides by 2 to convert to mono
                 */
                if (channel_count == 2) {
                    for (size_t i = 0; i < frames ; i++, dst++, src += 2) {
                        *dst = (int16_t)(((int32_t)src[0] + (int32_t)src[1]) >> 1);
                    }
                    bytes_to_write /= 2;
                }
            }

            // Note: since out_get_presentation_position() is called alternating with out_write()
            // by AudioFlinger, we can check underruns using the prior timestamp read.
            // (Alternately we could check if the buffer is empty using pcm_get_htimestamp().
            if (out->last_fifo_valid) {
                // compute drain to see if there is an underrun.
                const int64_t current_ns = systemTime(SYSTEM_TIME_MONOTONIC); // sys call
                int64_t time_diff_ns = current_ns - out->last_fifo_time_ns;
                int64_t frames_by_time =
                        ((time_diff_ns > 0) && (time_diff_ns < (INT64_MAX / out->config.rate))) ?
                                         (time_diff_ns * out->config.rate / NANOS_PER_SECOND) : 0;
                const int64_t underrun = frames_by_time - out->last_fifo_frames_remaining;

                if (underrun > 0) {
#ifndef LINUX_ENABLED
                    simple_stats_log(&out->fifo_underruns, underrun);
#endif

                    ALOGW("%s: underrun(%lld) "
                            "frames_by_time(%lld) > out->last_fifo_frames_remaining(%lld)",
                            __func__,
                            (long long)out->fifo_underruns.n,
                            (long long)frames_by_time,
                            (long long)out->last_fifo_frames_remaining);
                }
                out->last_fifo_valid = false;  // we're writing below, mark fifo info as stale.
            }

            ALOGVV("%s: writing buffer (%zu bytes) to pcm device", __func__, bytes);

            long ns = 0;

            if (out->config.rate)
                ns = pcm_bytes_to_frames(out->pcm, bytes)*1000000000LL/
                                                     out->config.rate;

            request_out_focus(out, ns);
            bool use_mmap = is_mmap_usecase(out->usecase) || out->realtime;

            if (use_mmap)
                ret = pcm_mmap_write(out->pcm, (void *)buffer, bytes_to_write);
            else if (out->hal_op_format != out->hal_ip_format &&
                       out->convert_buffer != NULL) {

                memcpy_by_audio_format(out->convert_buffer,
                                       out->hal_op_format,
                                       buffer,
                                       out->hal_ip_format,
                                       out->config.period_size * out->config.channels);

                ret = pcm_write(out->pcm, out->convert_buffer,
                                 (out->config.period_size *
                                 out->config.channels *
                                 format_to_bitwidth_table[out->hal_op_format]));
            } else {
                /*
                 * To avoid underrun in DSP when the application is not pumping
                 * data at required rate, check for the no. of bytes and ignore
                 * pcm_write if it is less than actual buffer size.
                 * It is a work around to a change in compress VOIP driver.
                 */
                if ((out->flags & AUDIO_OUTPUT_FLAG_VOIP_RX) &&
                    bytes < (out->config.period_size * out->config.channels *
                    audio_bytes_per_sample(out->format))) {
                    size_t voip_buf_size =
                        out->config.period_size * out->config.channels *
                        audio_bytes_per_sample(out->format);
                    ALOGE("%s:VOIP underrun: bytes received %zu, required:%zu\n",
                            __func__, bytes, voip_buf_size);
                    usleep(((uint64_t)voip_buf_size - bytes) *
                           1000000 / audio_stream_out_frame_size(stream) /
                           out_get_sample_rate(&out->stream.common));
                    ret = 0;
                } else {
                    if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS)
                        ret = split_and_write_audio_haptic_data(out, buffer, bytes);
                    else
                        ret = pcm_write(out->pcm, (void *)buffer, bytes_to_write);
                }
            }

            release_out_focus(out);

            if (ret < 0)
                ret = -errno;
            else if (ret > 0)
                ret = -EINVAL;
        }
    }

exit:
    update_frames_written(out, bytes);
    if (-ENETRESET == ret) {
        out->card_status = CARD_STATUS_OFFLINE;
    }
    pthread_mutex_unlock(&out->lock);

    if (ret != 0) {
        if (out->pcm)
            ALOGE("%s: error %d, %s", __func__, (int)ret, pcm_get_error(out->pcm));
        if (out->usecase == USECASE_COMPRESS_VOIP_CALL) {
            pthread_mutex_lock(&adev->lock);
            voice_extn_compress_voip_close_output_stream(&out->stream.common);
            out->started = 0;
            pthread_mutex_unlock(&adev->lock);
            out->standby = true;
        }
        out_on_error(&out->stream.common);
        if (!(out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)) {
            /* prevent division-by-zero */
            uint32_t stream_size = audio_stream_out_frame_size(stream);
            uint32_t srate = out_get_sample_rate(&out->stream.common);

            if ((stream_size == 0) || (srate == 0)) {
                ALOGE("%s: stream_size= %d, srate = %d", __func__, stream_size, srate);
                ATRACE_END();
                return -EINVAL;
             }
             usleep((uint64_t)bytes * 1000000 / stream_size / srate);
        }
        if (audio_extn_passthru_is_passthrough_stream(out)) {
                //ALOGE("%s: write error, ret = %zd", __func__, ret);
                ATRACE_END();
                return ret;
        }
    }
    ATRACE_END();
    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    struct stream_out *out = (struct stream_out *)stream;

    if (dsp_frames == NULL)
        return -EINVAL;

    *dsp_frames = 0;
    if (is_offload_usecase(out->usecase)) {
        ssize_t ret = 0;

        /* Below piece of code is not guarded against any lock beacuse audioFliner serializes
         * this operation and adev_close_output_stream(where out gets reset).
         */
        if (!out->non_blocking && !(out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)) {
            *dsp_frames = get_actual_pcm_frames_rendered(out, NULL);
             ALOGVV("dsp_frames %d sampleRate %d",(int)*dsp_frames,out->sample_rate);
             adjust_frames_for_device_delay(out, dsp_frames);
             return 0;
        }

        lock_output_stream(out);
        if (out->compr != NULL && out->non_blocking) {
            ret = compress_get_tstamp(out->compr, (unsigned long *)dsp_frames,
                    &out->sample_rate);
            if (ret < 0)
                ret = -errno;
            ALOGVV("%s rendered frames %d sample_rate %d",
                    __func__, *dsp_frames, out->sample_rate);
        }
        if (-ENETRESET == ret) {
            ALOGE(" ERROR: sound card not active Unable to get time stamp from compress driver");
            out->card_status = CARD_STATUS_OFFLINE;
            ret = -EINVAL;
        } else if(ret < 0) {
            ALOGE(" ERROR: Unable to get time stamp from compress driver");
            ret = -EINVAL;
        } else if (out->card_status == CARD_STATUS_OFFLINE) {
            /*
             * Handle corner case where compress session is closed during SSR
             * and timestamp is queried
             */
            ALOGE(" ERROR: sound card not active, return error");
            ret = -EINVAL;
        } else if (out->prev_card_status_offline) {
            ALOGE("ERROR: previously sound card was offline,return error");
            ret = -EINVAL;
        } else {
            ret = 0;
            adjust_frames_for_device_delay(out, dsp_frames);
        }
        pthread_mutex_unlock(&out->lock);
        return ret;
    } else if (audio_is_linear_pcm(out->format)) {
        *dsp_frames = out->written;
        adjust_frames_for_device_delay(out, dsp_frames);
        return 0;
    } else
        return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream __unused,
                                effect_handle_t effect __unused)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream __unused,
                                   effect_handle_t effect __unused)
{
    return 0;
}

static int out_get_next_write_timestamp(const struct audio_stream_out *stream __unused,
                                        int64_t *timestamp __unused)
{
    return -ENOSYS;
}

static int out_get_presentation_position(const struct audio_stream_out *stream,
                                   uint64_t *frames, struct timespec *timestamp)
{
    struct stream_out *out = (struct stream_out *)stream;
    int ret = -ENODATA;
    unsigned long dsp_frames;

    /* below piece of code is not guarded against any lock because audioFliner serializes
     * this operation and adev_close_output_stream( where out gets reset).
     */
    if (is_offload_usecase(out->usecase) && !out->non_blocking &&
        !(out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)) {
        *frames = get_actual_pcm_frames_rendered(out, timestamp);
        ALOGVV("frames %lld playedat %lld",(long long int)*frames,
             timestamp->tv_sec * 1000000LL + timestamp->tv_nsec / 1000);
        return 0;
    }

    lock_output_stream(out);

    if (is_offload_usecase(out->usecase) && out->compr != NULL && out->non_blocking) {
        ret = compress_get_tstamp(out->compr, &dsp_frames,
                 &out->sample_rate);
        // Adjustment accounts for A2dp encoder latency with offload usecases
        // Note: Encoder latency is returned in ms.
        if (is_a2dp_out_device_type(&out->device_list)) {
            unsigned long offset =
                        (audio_extn_a2dp_get_encoder_latency() * out->sample_rate / 1000);
            dsp_frames = (dsp_frames > offset) ? (dsp_frames - offset) : 0;
        }
        ALOGVV("%s rendered frames %ld sample_rate %d",
               __func__, dsp_frames, out->sample_rate);
        *frames = dsp_frames;
        if (ret < 0)
            ret = -errno;
        if (-ENETRESET == ret) {
            ALOGE(" ERROR: sound card not active Unable to get time stamp from compress driver");
            out->card_status = CARD_STATUS_OFFLINE;
            ret = -EINVAL;
        } else
            ret = 0;
         /* this is the best we can do */
        clock_gettime(CLOCK_MONOTONIC, timestamp);
    } else {
        if (out->pcm) {
            unsigned int avail;
            if (pcm_get_htimestamp(out->pcm, &avail, timestamp) == 0) {
                uint64_t signed_frames = 0;
                uint64_t frames_temp = 0;

                if (out->kernel_buffer_size > avail) {
                    frames_temp = out->last_fifo_frames_remaining = out->kernel_buffer_size - avail;
                } else {
                    ALOGW("%s: avail:%u > kernel_buffer_size:%zu clamping!",
                            __func__, avail, out->kernel_buffer_size);
                    avail = out->kernel_buffer_size;
                    frames_temp = out->last_fifo_frames_remaining = 0;
                }
                out->last_fifo_valid = true;
                out->last_fifo_time_ns = audio_utils_ns_from_timespec(timestamp);

                if (out->written >= frames_temp)
                    signed_frames = out->written - frames_temp;

                ALOGVV("%s: frames:%lld  avail:%u  kernel_buffer_size:%zu",
                        __func__, (long long)signed_frames, avail, out->kernel_buffer_size);

                // This adjustment accounts for buffering after app processor.
                // It is based on estimated DSP latency per use case, rather than exact.
                frames_temp = platform_render_latency(out) *
                              out->sample_rate / 1000000LL;
                if (signed_frames >= frames_temp)
                    signed_frames -= frames_temp;

                // Adjustment accounts for A2dp encoder latency with non offload usecases
                // Note: Encoder latency is returned in ms, while platform_render_latency in us.
                if (is_a2dp_out_device_type(&out->device_list)) {
                    frames_temp = audio_extn_a2dp_get_encoder_latency() * out->sample_rate / 1000;
                    if (signed_frames >= frames_temp)
                        signed_frames -= frames_temp;
                }

                // It would be unusual for this value to be negative, but check just in case ...
                *frames = signed_frames;
                ret = 0;
            }
        } else if (out->card_status == CARD_STATUS_OFFLINE ||
            // audioflinger still needs position updates when A2DP is suspended
            (is_a2dp_out_device_type(&out->device_list) && audio_extn_a2dp_source_is_suspended())) {
            *frames = out->written;
            clock_gettime(CLOCK_MONOTONIC, timestamp);
            if (is_offload_usecase(out->usecase))
                ret = -EINVAL;
            else
                ret = 0;
        }
    }
    pthread_mutex_unlock(&out->lock);
    return ret;
}

static int out_set_callback(struct audio_stream_out *stream,
            stream_callback_t callback, void *cookie)
{
    struct stream_out *out = (struct stream_out *)stream;
    int ret;

    ALOGV("%s", __func__);
    lock_output_stream(out);
    out->client_callback = callback;
    out->client_cookie = cookie;
    if (out->adsp_hdlr_stream_handle) {
        ret = audio_extn_adsp_hdlr_stream_set_callback(
                                out->adsp_hdlr_stream_handle,
                                callback,
                                cookie);
        if (ret)
            ALOGW("%s:adsp hdlr callback registration failed %d",
                   __func__, ret);
    }
    pthread_mutex_unlock(&out->lock);
    return 0;
}

static int out_pause(struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = -ENOSYS;
    ALOGV("%s", __func__);
    if (is_offload_usecase(out->usecase)) {
        ALOGD("copl(%p):pause compress driver", out);
        status = -ENODATA;
        lock_output_stream(out);
        pthread_mutex_lock(&out->latch_lock);
        if (out->compr != NULL && out->offload_state == OFFLOAD_STATE_PLAYING) {
            if (out->card_status != CARD_STATUS_OFFLINE)
                status = compress_pause(out->compr);

            out->offload_state = OFFLOAD_STATE_PAUSED;

            if (audio_extn_passthru_is_active()) {
                ALOGV("offload use case, pause passthru");
                audio_extn_passthru_on_pause(out);
            }

            audio_extn_dts_eagle_fade(adev, false, out);
            audio_extn_dts_notify_playback_state(out->usecase, 0,
                                                 out->sample_rate, popcount(out->channel_mask),
                                                 0);
        }
        pthread_mutex_unlock(&out->latch_lock);
        pthread_mutex_unlock(&out->lock);
    }
    return status;
}

static int out_resume(struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = -ENOSYS;
    ALOGV("%s", __func__);
    if (is_offload_usecase(out->usecase)) {
        ALOGD("copl(%p):resume compress driver", out);
        status = -ENODATA;
        lock_output_stream(out);
        pthread_mutex_lock(&out->latch_lock);
        if (out->compr != NULL && out->offload_state == OFFLOAD_STATE_PAUSED) {
            if (out->card_status != CARD_STATUS_OFFLINE) {
                status = compress_resume(out->compr);
            }
            if (!status) {
                out->offload_state = OFFLOAD_STATE_PLAYING;
            }
            audio_extn_dts_eagle_fade(adev, true, out);
            audio_extn_dts_notify_playback_state(out->usecase, 0, out->sample_rate,
                                                     popcount(out->channel_mask), 1);
        }
        pthread_mutex_unlock(&out->latch_lock);
        pthread_mutex_unlock(&out->lock);
    }
    return status;
}

static int out_drain(struct audio_stream_out* stream, audio_drain_type_t type )
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = -ENOSYS;
    ALOGV("%s", __func__);
    if (is_offload_usecase(out->usecase)) {
        lock_output_stream(out);
        if (type == AUDIO_DRAIN_EARLY_NOTIFY)
            status = send_offload_cmd_l(out, OFFLOAD_CMD_PARTIAL_DRAIN);
        else
            status = send_offload_cmd_l(out, OFFLOAD_CMD_DRAIN);
        pthread_mutex_unlock(&out->lock);
    }
    return status;
}

static int out_flush(struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    ALOGV("%s", __func__);
    if (is_offload_usecase(out->usecase)) {
        ALOGD("copl(%p):calling compress flush", out);
        lock_output_stream(out);
        pthread_mutex_lock(&out->latch_lock);
        if (out->offload_state == OFFLOAD_STATE_PAUSED) {
            pthread_mutex_unlock(&out->latch_lock);
            stop_compressed_output_l(out);
        } else {
            ALOGW("%s called in invalid state %d", __func__, out->offload_state);
            pthread_mutex_unlock(&out->latch_lock);
        }
        out->written = 0;
        pthread_mutex_unlock(&out->lock);
        ALOGD("copl(%p):out of compress flush", out);
        return 0;
    }
    return -ENOSYS;
}

static int out_stop(const struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    int ret = -ENOSYS;

    ALOGV("%s", __func__);
    pthread_mutex_lock(&adev->lock);
    if (out->usecase == USECASE_AUDIO_PLAYBACK_MMAP && !out->standby &&
            out->playback_started && out->pcm != NULL) {
        pcm_stop(out->pcm);
        ret = stop_output_stream(out);
        out->playback_started = false;
    }
    pthread_mutex_unlock(&adev->lock);
    return ret;
}

static int out_start(const struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    int ret = -ENOSYS;

    ALOGV("%s", __func__);
    pthread_mutex_lock(&adev->lock);
    if (out->usecase == USECASE_AUDIO_PLAYBACK_MMAP && !out->standby &&
            !out->playback_started && out->pcm != NULL) {
        ret = start_output_stream(out);
        if (ret == 0) {
            out->playback_started = true;
        }
    }
    pthread_mutex_unlock(&adev->lock);
    return ret;
}

/*
 * Modify config->period_count based on min_size_frames
 */
static void adjust_mmap_period_count(struct pcm_config *config, int32_t min_size_frames)
{
    int periodCountRequested = (min_size_frames + config->period_size - 1)
                               / config->period_size;
    int periodCount = MMAP_PERIOD_COUNT_MIN;

    ALOGV("%s original config.period_size = %d config.period_count = %d",
          __func__, config->period_size, config->period_count);

    while (periodCount < periodCountRequested && (periodCount * 2) < MMAP_PERIOD_COUNT_MAX) {
        periodCount *= 2;
    }
    config->period_count = periodCount;

    ALOGV("%s requested config.period_count = %d", __func__, config->period_count);
}

// Read offset for the positional timestamp from a persistent vendor property.
// This is to workaround apparent inaccuracies in the timing information that
// is used by the AAudio timing model. The inaccuracies can cause glitches.
static int64_t get_mmap_out_time_offset() {
    const int32_t kDefaultOffsetMicros = 0;
    int32_t mmap_time_offset_micros = property_get_int32(
        "persist.vendor.audio.out_mmap_delay_micros", kDefaultOffsetMicros);
    ALOGI("mmap_time_offset_micros = %d for output", mmap_time_offset_micros);
    return mmap_time_offset_micros * (int64_t)1000;
}

static int out_create_mmap_buffer(const struct audio_stream_out *stream,
                                  int32_t min_size_frames,
                                  struct audio_mmap_buffer_info *info)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    int ret = 0;
    unsigned int offset1 = 0;
    unsigned int frames1 = 0;
    const char *step = "";
    uint32_t mmap_size;
    uint32_t buffer_size;

    ALOGD("%s", __func__);
    lock_output_stream(out);
    pthread_mutex_lock(&adev->lock);

    if (CARD_STATUS_OFFLINE == out->card_status ||
        CARD_STATUS_OFFLINE == adev->card_status) {
        ALOGW("out->card_status or adev->card_status offline, try again");
        ret = -EIO;
        goto exit;
    }
    if (info == NULL || !(min_size_frames > 0 && min_size_frames < INT32_MAX)) {
        ALOGE("%s: info = %p, min_size_frames = %d", __func__, info, min_size_frames);
        ret = -EINVAL;
        goto exit;
    }
    if (out->usecase != USECASE_AUDIO_PLAYBACK_MMAP || !out->standby) {
        ALOGE("%s: usecase = %d, standby = %d", __func__, out->usecase, out->standby);
        ret = -ENOSYS;
        goto exit;
    }
    out->pcm_device_id = platform_get_pcm_device_id(out->usecase, PCM_PLAYBACK);
    if (out->pcm_device_id < 0) {
        ALOGE("%s: Invalid PCM device id(%d) for the usecase(%d)",
              __func__, out->pcm_device_id, out->usecase);
        ret = -EINVAL;
        goto exit;
    }

    adjust_mmap_period_count(&out->config, min_size_frames);

    ALOGD("%s: Opening PCM device card_id(%d) device_id(%d), channels %d",
          __func__, adev->snd_card, out->pcm_device_id, out->config.channels);
    out->pcm = pcm_open(adev->snd_card, out->pcm_device_id,
                        (PCM_OUT | PCM_MMAP | PCM_NOIRQ | PCM_MONOTONIC), &out->config);
    if (errno == ENETRESET && !pcm_is_ready(out->pcm)) {
        ALOGE("%s: pcm_open failed errno:%d\n", __func__, errno);
        out->card_status = CARD_STATUS_OFFLINE;
        adev->card_status = CARD_STATUS_OFFLINE;
        ret = -EIO;
        goto exit;
    }

    if (out->pcm == NULL || !pcm_is_ready(out->pcm)) {
        step = "open";
        ret = -ENODEV;
        goto exit;
    }
    ret = pcm_mmap_begin(out->pcm, &info->shared_memory_address, &offset1, &frames1);
    if (ret < 0)  {
        step = "begin";
        goto exit;
    }

    info->flags = 0;
    info->buffer_size_frames = pcm_get_buffer_size(out->pcm);
    buffer_size = pcm_frames_to_bytes(out->pcm, info->buffer_size_frames);
    info->burst_size_frames = out->config.period_size;
    ret = platform_get_mmap_data_fd(adev->platform,
                                    out->pcm_device_id, 0 /*playback*/,
                                    &info->shared_memory_fd,
                                    &mmap_size);
    if (ret < 0) {
        // Fall back to non exclusive mode
        info->shared_memory_fd = pcm_get_poll_fd(out->pcm);
    } else {
        out->mmap_shared_memory_fd = info->shared_memory_fd; // for closing later
        ALOGV("%s: opened mmap_shared_memory_fd = %d", __func__, out->mmap_shared_memory_fd);

        if (mmap_size < buffer_size) {
            step = "mmap";
            goto exit;
        }
        info->flags |= AUDIO_MMAP_APPLICATION_SHAREABLE;
    }
    memset(info->shared_memory_address, 0, pcm_frames_to_bytes(out->pcm,
                                                               info->buffer_size_frames));

    ret = pcm_mmap_commit(out->pcm, 0, MMAP_PERIOD_SIZE);
    if (ret < 0) {
        step = "commit";
        goto exit;
    }

    out->mmap_time_offset_nanos = get_mmap_out_time_offset();

    out->standby = false;
    ret = 0;

    ALOGD("%s: got mmap buffer address %p info->buffer_size_frames %d",
          __func__, info->shared_memory_address, info->buffer_size_frames);

exit:
    if (ret != 0) {
        if (out->pcm == NULL) {
            ALOGE("%s: %s - %d", __func__, step, ret);
        } else {
            ALOGE("%s: %s %s", __func__, step, pcm_get_error(out->pcm));
            pcm_close(out->pcm);
            out->pcm = NULL;
        }
    }
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&out->lock);
    return ret;
}

static int out_get_mmap_position(const struct audio_stream_out *stream,
                                  struct audio_mmap_position *position)
{
    struct stream_out *out = (struct stream_out *)stream;
    ALOGVV("%s", __func__);
    if (position == NULL) {
        return -EINVAL;
    }
    if (out->usecase != USECASE_AUDIO_PLAYBACK_MMAP) {
        ALOGE("%s: called on %s", __func__, use_case_table[out->usecase]);
        return -ENOSYS;
    }
    if (out->pcm == NULL) {
        return -ENOSYS;
    }

    struct timespec ts = { 0, 0 };
    int ret = pcm_mmap_get_hw_ptr(out->pcm, (unsigned int *)&position->position_frames, &ts);
    if (ret < 0) {
        ALOGE("%s: %s", __func__, pcm_get_error(out->pcm));
        return ret;
    }
    position->time_nanoseconds = ts.tv_sec*1000000000LL + ts.tv_nsec
            + out->mmap_time_offset_nanos;
    return 0;
}


/** audio_stream_in implementation **/
static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;

    return in->config.rate;
}

static int in_set_sample_rate(struct audio_stream *stream __unused,
                              uint32_t rate __unused)
{
    return -ENOSYS;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;

    if(in->usecase == USECASE_COMPRESS_VOIP_CALL)
        return voice_extn_compress_voip_in_get_buffer_size(in);
    else if(audio_extn_compr_cap_usecase_supported(in->usecase))
        return audio_extn_compr_cap_get_buffer_size(pcm_format_to_audio_format(in->config.format));
    else if(audio_extn_cin_attached_usecase(in))
        return audio_extn_cin_get_buffer_size(in);

    return in->config.period_size * in->af_period_multiplier *
        audio_stream_in_frame_size((const struct audio_stream_in *)stream);
}

static uint32_t in_get_channels(const struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;

    return in->channel_mask;
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;

    return in->format;
}

static int in_set_format(struct audio_stream *stream __unused,
                         audio_format_t format __unused)
{
    return -ENOSYS;
}

static int in_standby(struct audio_stream *stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    int status = 0;
    ALOGD("%s: enter: stream (%p) usecase(%d: %s)", __func__,
          stream, in->usecase, use_case_table[in->usecase]);
    bool do_stop = true;

    lock_input_stream(in);
    if (!in->standby && in->is_st_session) {
        ALOGD("%s: sound trigger pcm stop lab", __func__);
        audio_extn_sound_trigger_stop_lab(in);
        if (adev->num_va_sessions > 0)
            adev->num_va_sessions--;
        in->standby = 1;
    }

    if (!in->standby) {
        if (adev->adm_deregister_stream)
            adev->adm_deregister_stream(adev->adm_data, in->capture_handle);

        pthread_mutex_lock(&adev->lock);
        in->standby = true;
        if (in->usecase == USECASE_COMPRESS_VOIP_CALL) {
            do_stop = false;
            voice_extn_compress_voip_close_input_stream(stream);
            ALOGD("VOIP input entered standby");
        } else if (in->usecase == USECASE_AUDIO_RECORD_MMAP) {
            do_stop = in->capture_started;
            in->capture_started = false;
            if (in->mmap_shared_memory_fd >= 0) {
                ALOGV("%s: closing mmap_shared_memory_fd = %d",
                      __func__, in->mmap_shared_memory_fd);
                close(in->mmap_shared_memory_fd);
                in->mmap_shared_memory_fd = -1;
            }
        } else {
            if (audio_extn_cin_attached_usecase(in))
                audio_extn_cin_close_input_stream(in);
        }

        if (in->pcm) {
            ATRACE_BEGIN("pcm_in_close");
            pcm_close(in->pcm);
            ATRACE_END();
            in->pcm = NULL;
        }

        if (do_stop)
            status = stop_input_stream(in);

        if (in->source == AUDIO_SOURCE_VOICE_RECOGNITION) {
            if (adev->num_va_sessions > 0)
                adev->num_va_sessions--;
        }

        pthread_mutex_unlock(&adev->lock);
    }
    pthread_mutex_unlock(&in->lock);
    ALOGV("%s: exit:  status(%d)", __func__, status);
    return status;
}

static int in_dump(const struct audio_stream *stream,
                   int fd)
{
    struct stream_in *in = (struct stream_in *)stream;

    // We try to get the lock for consistency,
    // but it isn't necessary for these variables.
    // If we're not in standby, we may be blocked on a read.
    const bool locked = (pthread_mutex_trylock(&in->lock) == 0);
    dprintf(fd, "      Standby: %s\n", in->standby ? "yes" : "no");
    dprintf(fd, "      Frames read: %lld\n", (long long)in->frames_read);
    dprintf(fd, "      Frames muted: %lld\n", (long long)in->frames_muted);
#ifndef LINUX_ENABLED
    char buffer[256]; // for statistics formatting
    if (in->start_latency_ms.n > 0) {
        simple_stats_to_string(&in->start_latency_ms, buffer, sizeof(buffer));
        dprintf(fd, "      Start latency ms: %s\n", buffer);
    }
#endif
    if (locked) {
        pthread_mutex_unlock(&in->lock);
    }
#ifndef LINUX_ENABLED
    // dump error info
    (void)error_log_dump(
            in->error_log, fd, "      " /* prefix */, 0 /* lines */, 0 /* limit_ns */);
#endif
    return 0;
}

static void in_snd_mon_cb(void * stream, struct str_parms * parms)
{
    if (!stream || !parms)
        return;

    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;

    card_status_t status;
    int card;
    if (parse_snd_card_status(parms, &card, &status) < 0)
        return;

    pthread_mutex_lock(&adev->lock);
    bool valid_cb = (card == adev->snd_card);
    pthread_mutex_unlock(&adev->lock);

    if (!valid_cb)
        return;

    lock_input_stream(in);
    if (in->card_status != status)
        in->card_status = status;
    pthread_mutex_unlock(&in->lock);

    ALOGW("in_snd_mon_cb for card %d usecase %s, status %s", card,
          use_case_table[in->usecase],
          status == CARD_STATUS_OFFLINE ? "offline" : "online");

    // a better solution would be to report error back to AF and let
    // it put the stream to standby
    if (status == CARD_STATUS_OFFLINE)
        in_standby(&in->stream.common);

    return;
}

int route_input_stream(struct stream_in *in,
                       struct listnode *devices,
                       audio_source_t source)
{
    struct audio_device *adev = in->dev;
    int ret = 0;

    lock_input_stream(in);
    pthread_mutex_lock(&adev->lock);

    /* no audio source uses val == 0 */
    if ((in->source != source) && (source != AUDIO_SOURCE_DEFAULT)) {
        in->source = source;
        if ((in->source == AUDIO_SOURCE_VOICE_COMMUNICATION) &&
            (in->dev->mode == AUDIO_MODE_IN_COMMUNICATION) &&
            (voice_extn_compress_voip_is_format_supported(in->format)) &&
            (in->config.rate == 8000 || in->config.rate == 16000 ||
             in->config.rate == 32000 || in->config.rate == 48000 ) &&
            (audio_channel_count_from_in_mask(in->channel_mask) == 1)) {
            ret = voice_extn_compress_voip_open_input_stream(in);
            if (ret != 0) {
                ALOGE("%s: Compress voip input cannot be opened, error:%d",
                      __func__, ret);
            }
        }
    }

    if (!compare_devices(&in->device_list, devices) && !list_empty(devices) &&
          is_audio_in_device_type(devices)) {
        // Workaround: If routing to an non existing usb device, fail gracefully
        // The routing request will otherwise block during 10 second
        struct str_parms *usb_addr =
                str_parms_create_str(get_usb_device_address(devices));
        if (is_usb_in_device_type(devices) && usb_addr &&
             !audio_extn_usb_connected(NULL)) {
            ALOGW("%s: ignoring rerouting to non existing USB", __func__);
            ret = -ENOSYS;
        } else {
            /* If recording is in progress, change the tx device to new device */
            assign_devices(&in->device_list, devices);
            if (!in->standby && !in->is_st_session) {
                ALOGV("update input routing change");
                // inform adm before actual routing to prevent glitches.
                if (adev->adm_on_routing_change) {
                    adev->adm_on_routing_change(adev->adm_data,
                                                in->capture_handle);
                    ret = select_devices(adev, in->usecase);
                    if (in->usecase == USECASE_AUDIO_RECORD_LOW_LATENCY)
                        adev->adm_routing_changed = true;
                }
            }
        }
        if (usb_addr)
            str_parms_destroy(usb_addr);
    }
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&in->lock);

    ALOGV("%s: exit: status(%d)", __func__, ret);
    return ret;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    struct str_parms *parms;
    char value[32];
    int err = 0;

    ALOGD("%s: enter: kvpairs=%s", __func__, kvpairs);
    parms = str_parms_create_str(kvpairs);

    if (!parms)
        goto error;
    lock_input_stream(in);
    pthread_mutex_lock(&adev->lock);

    err = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_PROFILE, value, sizeof(value));
    if (err >= 0) {
        strlcpy(in->profile, value, sizeof(in->profile));
        ALOGV("updating stream profile with value '%s'", in->profile);
        audio_extn_utils_update_stream_input_app_type_cfg(adev->platform,
                                                          &adev->streams_input_cfg_list,
                                                          &in->device_list, in->flags, in->format,
                                                          in->sample_rate, in->bit_width,
                                                          in->profile, &in->app_type_cfg);
    }

    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&in->lock);

    str_parms_destroy(parms);
error:
    return 0;
}

static char* in_get_parameters(const struct audio_stream *stream,
                               const char *keys)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct str_parms *query = str_parms_create_str(keys);
    char *str;
    struct str_parms *reply = str_parms_create();

    if (!query || !reply) {
        if (reply) {
            str_parms_destroy(reply);
        }
        if (query) {
            str_parms_destroy(query);
        }
        ALOGE("in_get_parameters: failed to create query or reply");
        return NULL;
    }

    ALOGV("%s: enter: keys - %s %s ", __func__, use_case_table[in->usecase], keys);

    voice_extn_in_get_parameters(in, query, reply);

    stream_get_parameter_channels(query, reply,
                                  &in->supported_channel_masks[0]);
    stream_get_parameter_formats(query, reply,
                                 &in->supported_formats[0]);
    stream_get_parameter_rates(query, reply,
                               &in->supported_sample_rates[0]);
    str = str_parms_to_str(reply);
    str_parms_destroy(query);
    str_parms_destroy(reply);

    ALOGV("%s: exit: returns - %s", __func__, str);
    return str;
}

static int in_set_gain(struct audio_stream_in *stream,
                       float gain)
{
    struct stream_in *in = (struct stream_in *)stream;
    char mixer_ctl_name[128];
    struct mixer_ctl *ctl;
    int ctl_value;

    ALOGV("%s: gain %f", __func__, gain);

    if (stream == NULL)
        return -EINVAL;

    /* in_set_gain() only used to silence MMAP capture for now */
    if (in->usecase != USECASE_AUDIO_RECORD_MMAP)
        return -ENOSYS;

    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "Capture %d Volume", in->pcm_device_id);

    ctl = mixer_get_ctl_by_name(in->dev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGW("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -ENOSYS;
    }

    if (gain < RECORD_GAIN_MIN)
        gain  = RECORD_GAIN_MIN;
    else if (gain > RECORD_GAIN_MAX)
         gain = RECORD_GAIN_MAX;
    ctl_value = (int)(RECORD_VOLUME_CTL_MAX * gain);

    mixer_ctl_set_value(ctl, 0, ctl_value);

    return 0;
}

static ssize_t in_read(struct audio_stream_in *stream, void *buffer,
                       size_t bytes)
{
    struct stream_in *in = (struct stream_in *)stream;

    if (in == NULL) {
        ALOGE("%s: stream_in ptr is NULL", __func__);
        return -EINVAL;
    }

    struct audio_device *adev = in->dev;
    int ret = -1;
    size_t bytes_read = 0, frame_size = 0;

    lock_input_stream(in);

    if (in->is_st_session) {
        ALOGVV(" %s: reading on st session bytes=%zu", __func__, bytes);
        /* Read from sound trigger HAL */
        audio_extn_sound_trigger_read(in, buffer, bytes);
        if (in->standby) {
            if (adev->num_va_sessions < UINT_MAX)
                adev->num_va_sessions++;
            in->standby = 0;
        }
        pthread_mutex_unlock(&in->lock);
        return bytes;
    }

    if (in->usecase == USECASE_AUDIO_RECORD_MMAP) {
        ret = -ENOSYS;
        goto exit;
    }

    if (in->usecase == USECASE_AUDIO_RECORD_LOW_LATENCY &&
        !in->standby && adev->adm_routing_changed) {
        ret = -ENOSYS;
        goto exit;
    }

    if (in->standby) {
        const int64_t startNs = systemTime(SYSTEM_TIME_MONOTONIC);

        pthread_mutex_lock(&adev->lock);
        if (in->usecase == USECASE_COMPRESS_VOIP_CALL)
            ret = voice_extn_compress_voip_start_input_stream(in);
        else
            ret = start_input_stream(in);
        if (!ret && in->source == AUDIO_SOURCE_VOICE_RECOGNITION) {
            if (adev->num_va_sessions < UINT_MAX)
                adev->num_va_sessions++;
        }
        pthread_mutex_unlock(&adev->lock);
        if (ret != 0) {
            goto exit;
        }
        in->standby = 0;
#ifndef LINUX_ENABLED
        // log startup time in ms.
        simple_stats_log(
                &in->start_latency_ms, (systemTime(SYSTEM_TIME_MONOTONIC) - startNs) * 1e-6);
#endif
    }

    /* Avoid read if capture_stopped is set */
    if (android_atomic_acquire_load(&(in->capture_stopped)) > 0) {
        ALOGD("%s: force stopped catpure session, ignoring read request", __func__);
        ret = -EINVAL;
        goto exit;
    }

    // what's the duration requested by the client?
    long ns = 0;

    if (in->pcm && in->config.rate)
        ns = pcm_bytes_to_frames(in->pcm, bytes)*1000000000LL/
                                             in->config.rate;

    ret = request_in_focus(in, ns);
    if (ret != 0)
        goto exit;
    bool use_mmap = is_mmap_usecase(in->usecase) || in->realtime;

    if (audio_extn_cin_attached_usecase(in)) {
        ret = audio_extn_cin_read(in, buffer, bytes, &bytes_read);
    } else if (in->pcm) {
        if (audio_extn_ssr_get_stream() == in) {
            ret = audio_extn_ssr_read(stream, buffer, bytes);
        } else if (audio_extn_compr_cap_usecase_supported(in->usecase)) {
            ret = audio_extn_compr_cap_read(in, buffer, bytes);
        } else if (use_mmap) {
            ret = pcm_mmap_read(in->pcm, buffer, bytes);
        } else if (audio_extn_ffv_get_stream() == in) {
            ret = audio_extn_ffv_read(stream, buffer, bytes);
        } else {
            ret = pcm_read(in->pcm, buffer, bytes);
            /* data from DSP comes in 24_8 format, convert it to 8_24 */
            if (!ret && bytes > 0 && (in->format == AUDIO_FORMAT_PCM_8_24_BIT)) {
                if (audio_extn_utils_convert_format_24_8_to_8_24(buffer, bytes)
                    != bytes) {
                    ret = -EINVAL;
                    goto exit;
                }
            } else if (ret < 0) {
                ret = -errno;
            }
        }
        /* bytes read is always set to bytes for non compress usecases */
        bytes_read = bytes;
    }

    release_in_focus(in);

    /*
     * Instead of writing zeroes here, we could trust the hardware to always
     * provide zeroes when muted. This is also muted with voice recognition
     * usecases so that other clients do not have access to voice recognition
     * data.
     */
    if ((ret == 0 && voice_get_mic_mute(adev) &&
         !voice_is_in_call_rec_stream(in) &&
         (in->usecase != USECASE_AUDIO_RECORD_AFE_PROXY &&
          in->usecase != USECASE_AUDIO_RECORD_AFE_PROXY2)) ||
        (adev->num_va_sessions &&
         in->source != AUDIO_SOURCE_VOICE_RECOGNITION &&
         property_get_bool("persist.vendor.audio.va_concurrency_mute_enabled",
            false)))
        memset(buffer, 0, bytes);

exit:
    frame_size = audio_stream_in_frame_size(stream);
    if (frame_size > 0)
        in->frames_read += bytes_read/frame_size;

    if (-ENETRESET == ret)
        in->card_status = CARD_STATUS_OFFLINE;
    pthread_mutex_unlock(&in->lock);

    if (ret != 0) {
        if (in->usecase == USECASE_COMPRESS_VOIP_CALL) {
            pthread_mutex_lock(&adev->lock);
            voice_extn_compress_voip_close_input_stream(&in->stream.common);
            pthread_mutex_unlock(&adev->lock);
            in->standby = true;
        }
        if (!audio_extn_cin_attached_usecase(in)) {
            bytes_read = bytes;
            memset(buffer, 0, bytes);
        }
        in_standby(&in->stream.common);
        if (in->usecase == USECASE_AUDIO_RECORD_LOW_LATENCY)
            adev->adm_routing_changed = false;
        ALOGV("%s: read failed status %d- sleeping for buffer duration", __func__, ret);
        usleep((uint64_t)bytes * 1000000 / audio_stream_in_frame_size(stream) /
                                   in_get_sample_rate(&in->stream.common));
    }
    return bytes_read;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream __unused)
{
    return 0;
}

static int in_get_capture_position(const struct audio_stream_in *stream,
                                   int64_t *frames, int64_t *time)
{
    if (stream == NULL || frames == NULL || time == NULL) {
        return -EINVAL;
    }
    struct stream_in *in = (struct stream_in *)stream;
    int ret = -ENOSYS;

    lock_input_stream(in);
    // note: ST sessions do not close the alsa pcm driver synchronously
    // on standby. Therefore, we may return an error even though the
    // pcm stream is still opened.
    if (in->standby) {
        ALOGE_IF(in->pcm != NULL && !in->is_st_session,
                 "%s stream in standby but pcm not NULL for non ST session", __func__);
        goto exit;
    }
    if (in->pcm) {
        struct timespec timestamp;
        unsigned int avail;
        if (pcm_get_htimestamp(in->pcm, &avail, &timestamp) == 0) {
            *frames = in->frames_read + avail;
            *time = timestamp.tv_sec * 1000000000LL + timestamp.tv_nsec
                    - platform_capture_latency(in) * 1000LL;
             //Adjustment accounts for A2dp decoder latency for recording usecase
             // Note: decoder latency is returned in ms, while platform_capture_latency in ns.
            if (is_a2dp_in_device_type(&in->device_list))
                *time -= audio_extn_a2dp_get_decoder_latency() * 1000000LL;
            ret = 0;
        }
    }
exit:
    pthread_mutex_unlock(&in->lock);
    return ret;
}

static int in_update_effect_list(bool add, effect_handle_t effect,
                            struct listnode *head)
{
    struct listnode *node;
    struct in_effect_list *elist = NULL;
    struct in_effect_list *target = NULL;
    int ret = 0;

    if (!head)
        return ret;

    list_for_each(node, head) {
        elist = node_to_item(node, struct in_effect_list, list);
        if (elist->handle == effect) {
            target = elist;
            break;
        }
    }

    if (add) {
        if (target) {
            ALOGD("effect %p already exist", effect);
            return ret;
        }

        target = (struct in_effect_list *)
                     calloc(1, sizeof(struct in_effect_list));

        if (!target) {
            ALOGE("%s:fail to allocate memory", __func__);
            return -ENOMEM;
        }

        target->handle = effect;
        list_add_tail(head, &target->list);
    } else {
        if (target) {
            list_remove(&target->list);
            free(target);
        }
    }

    return ret;
}

static int add_remove_audio_effect(const struct audio_stream *stream,
                                   effect_handle_t effect,
                                   bool enable)
{
    struct stream_in *in = (struct stream_in *)stream;
    int status = 0;
    effect_descriptor_t desc;

    status = (*effect)->get_descriptor(effect, &desc);
    ALOGV("%s: status %d in->standby %d enable:%d", __func__, status, in->standby, enable);

    if (status != 0)
        return status;

    lock_input_stream(in);
    pthread_mutex_lock(&in->dev->lock);
    if ((in->source == AUDIO_SOURCE_VOICE_COMMUNICATION ||
            in->source == AUDIO_SOURCE_VOICE_RECOGNITION ||
            adev->mode == AUDIO_MODE_IN_COMMUNICATION) &&
            (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0)) {

        in_update_effect_list(enable, effect, &in->aec_list);
        enable = !list_empty(&in->aec_list);
        if (enable == in->enable_aec)
            goto exit;

        in->enable_aec = enable;
        ALOGD("AEC enable %d", enable);

        if (in->source == AUDIO_SOURCE_VOICE_COMMUNICATION ||
            in->dev->mode == AUDIO_MODE_IN_COMMUNICATION) {
            in->dev->enable_voicerx = enable;
            struct audio_usecase *usecase;
            struct listnode *node;
            list_for_each(node, &in->dev->usecase_list) {
                usecase = node_to_item(node, struct audio_usecase, list);
                if (usecase->type == PCM_PLAYBACK)
                    select_devices(in->dev, usecase->id);
            }
        }
        if (!in->standby) {
            if (enable_disable_effect(in->dev, EFFECT_AEC, enable) == ENOSYS)
                select_devices(in->dev, in->usecase);
        }

    }
    if (memcmp(&desc.type, FX_IID_NS, sizeof(effect_uuid_t)) == 0) {

        in_update_effect_list(enable, effect, &in->ns_list);
        enable = !list_empty(&in->ns_list);
        if (enable == in->enable_ns)
            goto exit;

        in->enable_ns = enable;
        ALOGD("NS enable %d", enable);
        if (!in->standby) {
            if (in->source == AUDIO_SOURCE_VOICE_COMMUNICATION ||
                in->dev->mode == AUDIO_MODE_IN_COMMUNICATION) {
                if (enable_disable_effect(in->dev, EFFECT_NS, enable) == ENOSYS)
                    select_devices(in->dev, in->usecase);
            } else
                select_devices(in->dev, in->usecase);
        }
    }
exit:
    pthread_mutex_unlock(&in->dev->lock);
    pthread_mutex_unlock(&in->lock);

    return 0;
}

static int in_add_audio_effect(const struct audio_stream *stream,
                               effect_handle_t effect)
{
    ALOGV("%s: effect %p", __func__, effect);
    return add_remove_audio_effect(stream, effect, true);
}

static int in_remove_audio_effect(const struct audio_stream *stream,
                                  effect_handle_t effect)
{
    ALOGV("%s: effect %p", __func__, effect);
    return add_remove_audio_effect(stream, effect, false);
}

static int in_stop(const struct audio_stream_in* stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;

    int ret = -ENOSYS;
    ALOGV("%s", __func__);
    pthread_mutex_lock(&adev->lock);
    if (in->usecase == USECASE_AUDIO_RECORD_MMAP && !in->standby &&
            in->capture_started && in->pcm != NULL) {
        pcm_stop(in->pcm);
        ret = stop_input_stream(in);
        in->capture_started = false;
    }
    pthread_mutex_unlock(&adev->lock);
    return ret;
}

static int in_start(const struct audio_stream_in* stream)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    int ret = -ENOSYS;

    ALOGV("%s in %p", __func__, in);
    pthread_mutex_lock(&adev->lock);
    if (in->usecase == USECASE_AUDIO_RECORD_MMAP && !in->standby &&
            !in->capture_started && in->pcm != NULL) {
        if (!in->capture_started) {
            ret = start_input_stream(in);
            if (ret == 0) {
                in->capture_started = true;
            }
        }
    }
    pthread_mutex_unlock(&adev->lock);
    return ret;
}

// Read offset for the positional timestamp from a persistent vendor property.
// This is to workaround apparent inaccuracies in the timing information that
// is used by the AAudio timing model. The inaccuracies can cause glitches.
static int64_t in_get_mmap_time_offset() {
    const int32_t kDefaultOffsetMicros = 0;
    int32_t mmap_time_offset_micros = property_get_int32(
            "persist.vendor.audio.in_mmap_delay_micros", kDefaultOffsetMicros);
    ALOGI("mmap_time_offset_micros = %d for input", mmap_time_offset_micros);
    return mmap_time_offset_micros * (int64_t)1000;
}

static int in_create_mmap_buffer(const struct audio_stream_in *stream,
                                  int32_t min_size_frames,
                                  struct audio_mmap_buffer_info *info)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    int ret = 0;
    unsigned int offset1 = 0;
    unsigned int frames1 = 0;
    const char *step = "";
    uint32_t mmap_size = 0;
    uint32_t buffer_size = 0;

    pthread_mutex_lock(&adev->lock);
    ALOGV("%s in %p", __func__, in);

    if (CARD_STATUS_OFFLINE == in->card_status||
        CARD_STATUS_OFFLINE == adev->card_status) {
        ALOGW("in->card_status or adev->card_status offline, try again");
        ret = -EIO;
        goto exit;
    }

    if (info == NULL || !(min_size_frames > 0 && min_size_frames < INT32_MAX)) {
        ALOGE("%s invalid argument info %p min_size_frames %d", __func__, info, min_size_frames);
        ret = -EINVAL;
        goto exit;
    }
    if (in->usecase != USECASE_AUDIO_RECORD_MMAP || !in->standby) {
        ALOGE("%s: usecase = %d, standby = %d", __func__, in->usecase, in->standby);
        ALOGV("%s in %p", __func__, in);
        ret = -ENOSYS;
        goto exit;
    }
    in->pcm_device_id = platform_get_pcm_device_id(in->usecase, PCM_CAPTURE);
    if (in->pcm_device_id < 0) {
        ALOGE("%s: Invalid PCM device id(%d) for the usecase(%d)",
              __func__, in->pcm_device_id, in->usecase);
        ret = -EINVAL;
        goto exit;
    }

    adjust_mmap_period_count(&in->config, min_size_frames);

    ALOGV("%s: Opening PCM device card_id(%d) device_id(%d), channels %d",
          __func__, adev->snd_card, in->pcm_device_id, in->config.channels);
    in->pcm = pcm_open(adev->snd_card, in->pcm_device_id,
                        (PCM_IN | PCM_MMAP | PCM_NOIRQ | PCM_MONOTONIC), &in->config);
    if (errno == ENETRESET && !pcm_is_ready(in->pcm)) {
        ALOGE("%s: pcm_open failed errno:%d\n", __func__, errno);
        in->card_status = CARD_STATUS_OFFLINE;
        adev->card_status = CARD_STATUS_OFFLINE;
        ret = -EIO;
        goto exit;
    }

    if (in->pcm == NULL || !pcm_is_ready(in->pcm)) {
        step = "open";
        ret = -ENODEV;
        goto exit;
    }

    ret = pcm_mmap_begin(in->pcm, &info->shared_memory_address, &offset1, &frames1);
    if (ret < 0)  {
        step = "begin";
        goto exit;
    }

    info->flags = 0;
    info->buffer_size_frames = pcm_get_buffer_size(in->pcm);
    buffer_size = pcm_frames_to_bytes(in->pcm, info->buffer_size_frames);
    info->burst_size_frames = in->config.period_size;
    ret = platform_get_mmap_data_fd(adev->platform,
                                    in->pcm_device_id, 1 /*capture*/,
                                    &info->shared_memory_fd,
                                    &mmap_size);
    if (ret < 0) {
        // Fall back to non exclusive mode
        info->shared_memory_fd = pcm_get_poll_fd(in->pcm);
    } else {
        in->mmap_shared_memory_fd = info->shared_memory_fd; // for closing later
        ALOGV("%s: opened mmap_shared_memory_fd = %d", __func__, in->mmap_shared_memory_fd);

        if (mmap_size < buffer_size) {
            step = "mmap";
            goto exit;
        }
        info->flags |= AUDIO_MMAP_APPLICATION_SHAREABLE;
    }

    memset(info->shared_memory_address, 0, buffer_size);

    ret = pcm_mmap_commit(in->pcm, 0, MMAP_PERIOD_SIZE);
    if (ret < 0) {
        step = "commit";
        goto exit;
    }

    in->mmap_time_offset_nanos = in_get_mmap_time_offset();

    in->standby = false;
    ret = 0;

    ALOGV("%s: got mmap buffer address %p info->buffer_size_frames %d",
          __func__, info->shared_memory_address, info->buffer_size_frames);

exit:
    if (ret != 0) {
        if (in->pcm == NULL) {
            ALOGE("%s: %s - %d", __func__, step, ret);
        } else {
            ALOGE("%s: %s %s", __func__, step, pcm_get_error(in->pcm));
            pcm_close(in->pcm);
            in->pcm = NULL;
        }
    }
    pthread_mutex_unlock(&adev->lock);
    return ret;
}

static int in_get_mmap_position(const struct audio_stream_in *stream,
                                  struct audio_mmap_position *position)
{
    struct stream_in *in = (struct stream_in *)stream;
    ALOGVV("%s", __func__);
    if (position == NULL) {
        return -EINVAL;
    }
    lock_input_stream(in);
    if (in->usecase != USECASE_AUDIO_RECORD_MMAP) {
        pthread_mutex_unlock(&in->lock);
        return -ENOSYS;
    }
    if (in->pcm == NULL) {
        pthread_mutex_unlock(&in->lock);
        return -ENOSYS;
    }
    struct timespec ts = { 0, 0 };
    int ret = pcm_mmap_get_hw_ptr(in->pcm, (unsigned int *)&position->position_frames, &ts);
    if (ret < 0) {
        ALOGE("%s: %s", __func__, pcm_get_error(in->pcm));
        pthread_mutex_unlock(&in->lock);
        return ret;
    }
    position->time_nanoseconds = ts.tv_sec*1000000000LL + ts.tv_nsec
            + in->mmap_time_offset_nanos;
    pthread_mutex_unlock(&in->lock);
    return 0;
}

static int in_get_active_microphones(const struct audio_stream_in *stream,
                                     struct audio_microphone_characteristic_t *mic_array,
                                     size_t *mic_count) {
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    ALOGVV("%s", __func__);

    lock_input_stream(in);
    pthread_mutex_lock(&adev->lock);
    int ret = platform_get_active_microphones(adev->platform,
                                              audio_channel_count_from_in_mask(in->channel_mask),
                                              in->usecase, mic_array, mic_count);
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&in->lock);

    return ret;
}

static int adev_get_microphones(const struct audio_hw_device *dev,
                                struct audio_microphone_characteristic_t *mic_array,
                                size_t *mic_count) {
    struct audio_device *adev = (struct audio_device *)dev;
    ALOGVV("%s", __func__);

    pthread_mutex_lock(&adev->lock);
    int ret = platform_get_microphones(adev->platform, mic_array, mic_count);
    pthread_mutex_unlock(&adev->lock);

    return ret;
}

static void in_update_sink_metadata(struct audio_stream_in *stream,
                                    const struct sink_metadata *sink_metadata) {

    if (stream == NULL
            || sink_metadata == NULL
            || sink_metadata->tracks == NULL) {
        return;
    }

    int error = 0;
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;
    struct listnode devices;
    bool is_ha_usecase = false;

    list_init(&devices);

    if (sink_metadata->track_count != 0)
        reassign_device_list(&devices, sink_metadata->tracks->dest_device, "");

    lock_input_stream(in);
    pthread_mutex_lock(&adev->lock);
    ALOGV("%s: in->usecase: %d, device: %x", __func__, in->usecase, get_device_types(&devices));

    is_ha_usecase = adev->ha_proxy_enable ?
        in->usecase == USECASE_AUDIO_RECORD_AFE_PROXY2 :
        in->usecase == USECASE_AUDIO_RECORD_AFE_PROXY;
    if (is_ha_usecase && !list_empty(&devices)
            && adev->voice_tx_output != NULL) {
        /* Use the rx device from afe-proxy record to route voice call because
           there is no routing if tx device is on primary hal and rx device
           is on other hal during voice call. */
        assign_devices(&adev->voice_tx_output->device_list, &devices);

        if (!voice_is_call_state_active(adev)) {
            if (adev->mode == AUDIO_MODE_IN_CALL) {
                adev->current_call_output = adev->voice_tx_output;
                error = voice_start_call(adev);
                if (error != 0)
                    ALOGE("%s: start voice call failed %d", __func__, error);
            }
        } else {
            adev->current_call_output = adev->voice_tx_output;
            voice_update_devices_for_all_voice_usecases(adev);
        }
    }

    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&in->lock);
}

int adev_open_output_stream(struct audio_hw_device *dev,
                            audio_io_handle_t handle,
                            audio_devices_t devices,
                            audio_output_flags_t flags,
                            struct audio_config *config,
                            struct audio_stream_out **stream_out,
                            const char *address)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_out *out;
    int ret = 0, ip_hdlr_stream = 0, ip_hdlr_dev = 0;
    audio_format_t format;
    struct adsp_hdlr_stream_cfg hdlr_stream_cfg;
    bool is_direct_passthough = false;
    bool is_hdmi = devices & AUDIO_DEVICE_OUT_AUX_DIGITAL;
    bool is_usb_dev = audio_is_usb_out_device(devices) &&
                      (devices != AUDIO_DEVICE_OUT_USB_ACCESSORY);
    bool direct_dev = is_hdmi || is_usb_dev;
    bool use_db_as_primary =
         property_get_bool("vendor.audio.feature.deepbuffer_as_primary.enable",
                            false);
    bool force_haptic_path =
            property_get_bool("vendor.audio.test_haptic", false);
    bool is_voip_rx = flags & AUDIO_OUTPUT_FLAG_VOIP_RX;
#ifdef AUDIO_GKI_ENABLED
    __s32 *generic_dec;
#endif
    pthread_mutexattr_t latch_attr;

    if (is_usb_dev && (!audio_extn_usb_connected(NULL))) {
        is_usb_dev = false;
        devices = AUDIO_DEVICE_OUT_SPEAKER;
        ALOGW("%s: ignore set device to non existing USB card, use output device(%#x)",
              __func__, devices);
        if (config->format == AUDIO_FORMAT_DEFAULT)
            config->format = AUDIO_FORMAT_PCM_16_BIT;
        if (config->sample_rate == 0)
            config->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
        if (config->channel_mask == AUDIO_CHANNEL_NONE)
            config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    }

    *stream_out = NULL;

    out = (struct stream_out *)calloc(1, sizeof(struct stream_out));

    ALOGD("%s: enter: format(%#x) sample_rate(%d) channel_mask(%#x) devices(%#x) flags(%#x)\
        stream_handle(%p) address(%s)", __func__, config->format, config->sample_rate, config->channel_mask,
        devices, flags, &out->stream, address);


    if (!out) {
        return -ENOMEM;
    }

    pthread_mutex_init(&out->lock, (const pthread_mutexattr_t *) NULL);
    pthread_mutex_init(&out->pre_lock, (const pthread_mutexattr_t *) NULL);
    pthread_mutexattr_init(&latch_attr);
    pthread_mutexattr_settype(&latch_attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&out->latch_lock, &latch_attr);
    pthread_mutexattr_destroy(&latch_attr);
    pthread_mutex_init(&out->position_query_lock, (const pthread_mutexattr_t *) NULL);
    pthread_cond_init(&out->cond, (const pthread_condattr_t *) NULL);

    if (devices == AUDIO_DEVICE_NONE)
        devices = AUDIO_DEVICE_OUT_SPEAKER;

    out->flags = flags;
    list_init(&out->device_list);
    update_device_list(&out->device_list, devices, address, true /* add devices */);
    out->dev = adev;
    out->hal_op_format = out->hal_ip_format = format = out->format = config->format;
    out->sample_rate = config->sample_rate;
    out->channel_mask = config->channel_mask;
    if (out->channel_mask == AUDIO_CHANNEL_NONE)
        out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_STEREO;
    else
        out->supported_channel_masks[0] = out->channel_mask;
    out->handle = handle;
    out->bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    out->non_blocking = 0;
    out->convert_buffer = NULL;
    out->started = 0;
    out->a2dp_muted = false;
    out->hal_output_suspend_supported = 0;
    out->dynamic_pm_qos_config_supported = 0;
    out->set_dual_mono = false;
    out->prev_card_status_offline = false;
    out->pspd_coeff_sent = false;
    out->mmap_shared_memory_fd = -1; // not open

    if ((flags & AUDIO_OUTPUT_FLAG_BD) &&
        (property_get_bool("vendor.audio.matrix.limiter.enable", false)))
        platform_set_device_params(out, DEVICE_PARAM_LIMITER_ID, 1);

    if (direct_dev &&
        (audio_is_linear_pcm(out->format) ||
         config->format == AUDIO_FORMAT_DEFAULT) &&
        out->flags == AUDIO_OUTPUT_FLAG_NONE) {
        audio_format_t req_format = config->format;
        audio_channel_mask_t req_channel_mask = config->channel_mask;
        uint32_t req_sample_rate = config->sample_rate;

        pthread_mutex_lock(&adev->lock);
        if (is_hdmi) {
            ALOGV("AUDIO_DEVICE_OUT_AUX_DIGITAL and DIRECT|OFFLOAD, check hdmi caps");
            ret = read_hdmi_sink_caps(out);
            if (config->sample_rate == 0)
                config->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
            if (config->channel_mask == AUDIO_CHANNEL_NONE)
                config->channel_mask = AUDIO_CHANNEL_OUT_5POINT1;
            if (config->format == AUDIO_FORMAT_DEFAULT)
                config->format = AUDIO_FORMAT_PCM_16_BIT;
        } else if (is_usb_dev) {
            ret = read_usb_sup_params_and_compare(true /*is_playback*/,
                                                  &config->format,
                                                  &out->supported_formats[0],
                                                  MAX_SUPPORTED_FORMATS,
                                                  &config->channel_mask,
                                                  &out->supported_channel_masks[0],
                                                  MAX_SUPPORTED_CHANNEL_MASKS,
                                                  &config->sample_rate,
                                                  &out->supported_sample_rates[0],
                                                  MAX_SUPPORTED_SAMPLE_RATES);
            ALOGV("plugged dev USB ret %d", ret);
       }

       pthread_mutex_unlock(&adev->lock);
       if (ret != 0) {
            if (ret == -ENOSYS) {
                /* ignore and go with default */
                ret = 0;
            }
            // For MMAP NO IRQ, allow conversions in ADSP
            else if (is_hdmi || (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) == 0)
                goto error_open;
            else {
                ALOGE("error reading direct dev sink caps");
                goto error_open;
            }

            if (req_sample_rate != 0 && config->sample_rate != req_sample_rate)
                config->sample_rate = req_sample_rate;
            if (req_channel_mask != AUDIO_CHANNEL_NONE && config->channel_mask != req_channel_mask)
                config->channel_mask = req_channel_mask;
            if (req_format != AUDIO_FORMAT_DEFAULT && config->format != req_format)
                config->format = req_format;
        }

        out->sample_rate = config->sample_rate;
        out->channel_mask = config->channel_mask;
        out->format = config->format;
        if (is_hdmi) {
            out->usecase = USECASE_AUDIO_PLAYBACK_HIFI;
            out->config = pcm_config_hdmi_multi;
        } else if (flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
            out->usecase = USECASE_AUDIO_PLAYBACK_MMAP;
            out->config = pcm_config_mmap_playback;
            out->stream.start = out_start;
            out->stream.stop = out_stop;
            out->stream.create_mmap_buffer = out_create_mmap_buffer;
            out->stream.get_mmap_position = out_get_mmap_position;
        } else {
            out->usecase = USECASE_AUDIO_PLAYBACK_HIFI;
            out->config = pcm_config_hifi;
        }

        out->config.rate = out->sample_rate;
        out->config.channels = audio_channel_count_from_out_mask(out->channel_mask);
        if (is_hdmi) {
            out->config.period_size = HDMI_MULTI_PERIOD_BYTES / (out->config.channels *
                                                         audio_bytes_per_sample(out->format));
        }
        out->config.format = pcm_format_from_audio_format(out->format);
    }

    /* validate bus device address */
    if (compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_BUS)) {
        /* extract car audio stream index */
        out->car_audio_stream =
            audio_extn_auto_hal_get_car_audio_stream_from_address(address);
        if (out->car_audio_stream < 0) {
            ALOGE("%s: invalid car audio stream %x",
                __func__, out->car_audio_stream);
            ret = -EINVAL;
            goto error_open;
        }
        ALOGV("%s: car_audio_stream %x", __func__, out->car_audio_stream);
    }

    /* Check for VOIP usecase */
    if (is_voip_rx) {
        if (!voice_extn_is_compress_voip_supported()) {
            if (out->sample_rate == 8000 || out->sample_rate == 16000 ||
             out->sample_rate == 32000 || out->sample_rate == 48000) {
                out->channel_mask = audio_extn_utils_is_vendor_enhanced_fwk() ?
                                        config->channel_mask : AUDIO_CHANNEL_OUT_STEREO;
                out->usecase = USECASE_AUDIO_PLAYBACK_VOIP;
                out->format = AUDIO_FORMAT_PCM_16_BIT;
                out->volume_l = INVALID_OUT_VOLUME;
                out->volume_r = INVALID_OUT_VOLUME;

                out->config = default_pcm_config_voip_copp;
                out->config.rate = out->sample_rate;
                uint32_t channel_count =
                        audio_channel_count_from_out_mask(out->channel_mask);
                out->config.channels = channel_count;

                uint32_t buffer_size = get_stream_buffer_size(DEFAULT_VOIP_BUF_DURATION_MS,
                                                              out->sample_rate, out->format,
                                                              channel_count, false);
                uint32_t frame_size = audio_bytes_per_sample(out->format) * channel_count;
                if (frame_size != 0)
                    out->config.period_size = buffer_size / frame_size;
                else
                    ALOGW("%s: frame size is 0 for format %#x", __func__, out->format);
            }
        } else {
                if ((out->dev->mode == AUDIO_MODE_IN_COMMUNICATION ||
                    voice_extn_compress_voip_is_active(out->dev)) &&
                       (voice_extn_compress_voip_is_config_supported(config))) {
                    ret = voice_extn_compress_voip_open_output_stream(out);
                    if (ret != 0) {
                        ALOGE("%s: Compress voip output cannot be opened, error:%d",
                              __func__, ret);
                        goto error_open;
                    }
                } else {
                    out->usecase = GET_USECASE_AUDIO_PLAYBACK_PRIMARY(use_db_as_primary);
                    out->config = GET_PCM_CONFIG_AUDIO_PLAYBACK_PRIMARY(use_db_as_primary);
                }
        }
    } else if (audio_is_linear_pcm(out->format) &&
        out->flags == AUDIO_OUTPUT_FLAG_NONE && is_usb_dev) {
        out->channel_mask = config->channel_mask;
        out->sample_rate = config->sample_rate;
        out->format = config->format;
        out->usecase = USECASE_AUDIO_PLAYBACK_HIFI;
        // does this change?
        out->config = is_hdmi ? pcm_config_hdmi_multi : pcm_config_hifi;
        out->config.rate = config->sample_rate;
        out->config.channels = audio_channel_count_from_out_mask(out->channel_mask);
        out->config.period_size = HDMI_MULTI_PERIOD_BYTES / (out->config.channels *
                                                         audio_bytes_per_sample(config->format));
        out->config.format = pcm_format_from_audio_format(out->format);
    } else if ((out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) ||
               (out->flags == AUDIO_OUTPUT_FLAG_DIRECT)) {
        pthread_mutex_lock(&adev->lock);
        bool offline = (adev->card_status == CARD_STATUS_OFFLINE);
        pthread_mutex_unlock(&adev->lock);

        // reject offload during card offline to allow
        // fallback to s/w paths
        if (offline) {
            ret = -ENODEV;
            goto error_open;
        }

        if (config->offload_info.version != AUDIO_INFO_INITIALIZER.version ||
            config->offload_info.size != AUDIO_INFO_INITIALIZER.size) {
            ALOGE("%s: Unsupported Offload information", __func__);
            ret = -EINVAL;
            goto error_open;
        }

        if (config->offload_info.format == 0)
            config->offload_info.format = config->format;
        if (config->offload_info.sample_rate == 0)
            config->offload_info.sample_rate = config->sample_rate;

        if (!is_supported_format(config->offload_info.format) &&
                !audio_extn_passthru_is_supported_format(config->offload_info.format)) {
            ALOGE("%s: Unsupported audio format %x " , __func__, config->offload_info.format);
            ret = -EINVAL;
            goto error_open;
        }

        /* TrueHD only supported for 48k multiples (48k, 96k, 192k) */
        if ((config->offload_info.format == AUDIO_FORMAT_DOLBY_TRUEHD) &&
                (audio_extn_passthru_is_passthrough_stream(out)) &&
                !((config->sample_rate == 48000) ||
                  (config->sample_rate == 96000) ||
                  (config->sample_rate == 192000))) {
            ALOGE("%s: Unsupported sample rate %d for audio format %x",
                    __func__, config->sample_rate, config->offload_info.format);
            ret = -EINVAL;
            goto error_open;
        }

        out->compr_config.codec = (struct snd_codec *)
                                    calloc(1, sizeof(struct snd_codec));

        if (!out->compr_config.codec) {
            ret = -ENOMEM;
            goto error_open;
        }

        out->stream.pause = out_pause;
        out->stream.resume = out_resume;
        out->stream.flush = out_flush;
        out->stream.set_callback = out_set_callback;
        if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
            out->stream.drain = out_drain;
            out->usecase = get_offload_usecase(adev, true /* is_compress */);
            ALOGV("Compress Offload usecase .. usecase selected %d", out->usecase);
        } else {
            out->usecase = get_offload_usecase(adev, false /* is_compress */);
            ALOGV("non-offload DIRECT_usecase ... usecase selected %d ", out->usecase);
        }

        if (out->flags & AUDIO_OUTPUT_FLAG_FAST) {
            ALOGD("%s: Setting latency mode to true", __func__);
#ifdef AUDIO_GKI_ENABLED
            /* out->compr_config.codec->reserved[1] is for flags */
            out->compr_config.codec->reserved[1] |= audio_extn_utils_get_perf_mode_flag();
#else
            out->compr_config.codec->flags |= audio_extn_utils_get_perf_mode_flag();
#endif
        }

        if (out->usecase == USECASE_INVALID) {
            if (compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_AUX_DIGITAL) &&
                    config->format == 0 && config->sample_rate == 0 &&
                    config->channel_mask == 0) {
                ALOGI("%s dummy open to query sink capability",__func__);
                out->usecase = USECASE_AUDIO_PLAYBACK_OFFLOAD;
            } else {
                ALOGE("%s, Max allowed OFFLOAD usecase reached ... ", __func__);
                ret = -EEXIST;
                goto error_open;
            }
        }

        if (config->offload_info.channel_mask)
            out->channel_mask = config->offload_info.channel_mask;
        else if (config->channel_mask) {
            out->channel_mask = config->channel_mask;
            config->offload_info.channel_mask = config->channel_mask;
        } else {
            ALOGE("out->channel_mask not set for OFFLOAD/DIRECT usecase");
            ret = -EINVAL;
            goto error_open;
        }

        format = out->format = config->offload_info.format;
        out->sample_rate = config->offload_info.sample_rate;

        out->bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;

        out->compr_config.codec->id = get_snd_codec_id(config->offload_info.format);
        if (audio_extn_utils_is_dolby_format(config->offload_info.format)) {
            audio_extn_dolby_send_ddp_endp_params(adev);
            audio_extn_dolby_set_dmid(adev);
        }

        out->compr_config.codec->sample_rate =
                    config->offload_info.sample_rate;
        out->compr_config.codec->bit_rate =
                    config->offload_info.bit_rate;
        out->compr_config.codec->ch_in =
                audio_channel_count_from_out_mask(out->channel_mask);
        out->compr_config.codec->ch_out = out->compr_config.codec->ch_in;
        /* Update bit width only for non passthrough usecases.
         * For passthrough usecases, the output will always be opened @16 bit
         */
        if (!audio_extn_passthru_is_passthrough_stream(out))
            out->bit_width = AUDIO_OUTPUT_BIT_WIDTH;

        if (out->flags & AUDIO_OUTPUT_FLAG_TIMESTAMP)
#ifdef AUDIO_GKI_ENABLED
            /* out->compr_config.codec->reserved[1] is for flags */
            out->compr_config.codec->reserved[1] |= COMPRESSED_TIMESTAMP_FLAG;
        ALOGVV("%s : out->compr_config.codec->flags -> (%#x) ", __func__, out->compr_config.codec->reserved[1]);
#else
            out->compr_config.codec->flags |= COMPRESSED_TIMESTAMP_FLAG;
        ALOGVV("%s : out->compr_config.codec->flags -> (%#x) ", __func__, out->compr_config.codec->flags);
#endif

        /*TODO: Do we need to change it for passthrough */
        out->compr_config.codec->format = SND_AUDIOSTREAMFORMAT_RAW;

        if ((config->offload_info.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC)
             out->compr_config.codec->format = SND_AUDIOSTREAMFORMAT_RAW;
        else if ((config->offload_info.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC_ADTS)
            out->compr_config.codec->format = SND_AUDIOSTREAMFORMAT_MP4ADTS;
        else if ((config->offload_info.format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AAC_LATM)
            out->compr_config.codec->format = SND_AUDIOSTREAMFORMAT_MP4LATM;

        if ((config->offload_info.format & AUDIO_FORMAT_MAIN_MASK) ==
             AUDIO_FORMAT_PCM) {

            /*Based on platform support, configure appropriate alsa format for corresponding
             *hal input format.
             */
            out->compr_config.codec->format = hal_format_to_alsa(
                                              config->offload_info.format);

            out->hal_op_format = alsa_format_to_hal(
                                                  out->compr_config.codec->format);
            out->hal_ip_format = out->format;

            /*for direct non-compress playback populate bit_width based on selected alsa format as
             *hal input format and alsa format might differ based on platform support.
             */
            out->bit_width = audio_bytes_per_sample(
                             out->hal_op_format) << 3;

            out->compr_config.fragments = DIRECT_PCM_NUM_FRAGMENTS;

            if (property_get_bool("vendor.audio.offload.buffer.duration.enabled", false)) {
                if ((config->offload_info.duration_us >= MIN_OFFLOAD_BUFFER_DURATION_MS * 1000) &&
                       (config->offload_info.duration_us <= MAX_OFFLOAD_BUFFER_DURATION_MS * 1000))
                    out->info.duration_us = (int64_t)config->offload_info.duration_us;
            }

            /* Check if alsa session is configured with the same format as HAL input format,
             * if not then derive correct fragment size needed to accomodate the
             * conversion of HAL input format to alsa format.
             */
            audio_extn_utils_update_direct_pcm_fragment_size(out);

            /*if hal input and output fragment size is different this indicates HAL input format is
             *not same as the alsa format
             */
            if (out->hal_fragment_size != out->compr_config.fragment_size) {
                /*Allocate a buffer to convert input data to the alsa configured format.
                 *size of convert buffer is equal to the size required to hold one fragment size
                 *worth of pcm data, this is because flinger does not write more than fragment_size
                 */
                out->convert_buffer = calloc(1,out->compr_config.fragment_size);
                if (out->convert_buffer == NULL){
                    ALOGE("Allocation failed for convert buffer for size %d", out->compr_config.fragment_size);
                    ret = -ENOMEM;
                    goto error_open;
                }
            }
        } else if (audio_extn_passthru_is_passthrough_stream(out)) {
            out->compr_config.fragment_size =
                   audio_extn_passthru_get_buffer_size(&config->offload_info);
            out->compr_config.fragments = COMPRESS_OFFLOAD_NUM_FRAGMENTS;
        } else {
            out->compr_config.fragment_size =
                  platform_get_compress_offload_buffer_size(&config->offload_info);
            out->compr_config.fragments = COMPRESS_OFFLOAD_NUM_FRAGMENTS;
        }

        if (out->flags & AUDIO_OUTPUT_FLAG_TIMESTAMP) {
            out->compr_config.fragment_size += sizeof(struct snd_codec_metadata);
        }
        if (config->offload_info.format == AUDIO_FORMAT_FLAC) {
#ifdef AUDIO_GKI_ENABLED
            generic_dec =
                &(out->compr_config.codec->options.generic.reserved[1]);
            ((struct snd_generic_dec_flac *)generic_dec)->sample_size =
                                                AUDIO_OUTPUT_BIT_WIDTH;
#else
            out->compr_config.codec->options.flac_dec.sample_size = AUDIO_OUTPUT_BIT_WIDTH;
#endif
        }

        if (config->offload_info.format == AUDIO_FORMAT_APTX) {
            audio_extn_send_aptx_dec_bt_addr_to_dsp(out);
        }

        if (flags & AUDIO_OUTPUT_FLAG_NON_BLOCKING)
            out->non_blocking = 1;

        if ((flags & AUDIO_OUTPUT_FLAG_TIMESTAMP) &&
            (flags & AUDIO_OUTPUT_FLAG_HW_AV_SYNC)) {
            out->render_mode = RENDER_MODE_AUDIO_STC_MASTER;
        } else if(flags & AUDIO_OUTPUT_FLAG_TIMESTAMP) {
            out->render_mode = RENDER_MODE_AUDIO_MASTER;
        } else {
            out->render_mode = RENDER_MODE_AUDIO_NO_TIMESTAMP;
        }

        memset(&out->channel_map_param, 0,
                sizeof(struct audio_out_channel_map_param));

        out->send_new_metadata = 1;
        out->send_next_track_params = false;
        out->is_compr_metadata_avail = false;
        out->offload_state = OFFLOAD_STATE_IDLE;
        out->playback_started = 0;
        out->writeAt.tv_sec = 0;
        out->writeAt.tv_nsec = 0;

        audio_extn_dts_create_state_notifier_node(out->usecase);

        ALOGV("%s: offloaded output offload_info version %04x bit rate %d",
                __func__, config->offload_info.version,
                config->offload_info.bit_rate);

        /* Check if DSD audio format is supported in codec
         * and there is no active native DSD use case
         */

        if ((config->format == AUDIO_FORMAT_DSD) &&
                (!platform_check_codec_dsd_support(adev->platform) ||
                audio_is_dsd_native_stream_active(adev))) {
            ret = -EINVAL;
            goto error_open;
        }

        /* Disable gapless if any of the following is true
         * passthrough playback
         * AV playback
         * non compressed Direct playback
         */
        if (audio_extn_passthru_is_passthrough_stream(out) ||
                (config->format == AUDIO_FORMAT_DSD) ||
                (config->format == AUDIO_FORMAT_IEC61937) ||
                config->offload_info.has_video ||
                !(out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)) {
            check_and_set_gapless_mode(adev, false);
        } else
            check_and_set_gapless_mode(adev, true);

        if (audio_extn_passthru_is_passthrough_stream(out)) {
            out->flags |= AUDIO_OUTPUT_FLAG_COMPRESS_PASSTHROUGH;
        }
        if (config->format == AUDIO_FORMAT_DSD) {
            out->flags |= AUDIO_OUTPUT_FLAG_COMPRESS_PASSTHROUGH;
#ifdef AUDIO_GKI_ENABLED
            /* out->compr_config.codec->reserved[0] is for compr_passthr */
            out->compr_config.codec->reserved[0] = PASSTHROUGH_DSD;
#else
            out->compr_config.codec->compr_passthr = PASSTHROUGH_DSD;
#endif
        }

        create_offload_callback_thread(out);

    } else if (out->flags & AUDIO_OUTPUT_FLAG_INCALL_MUSIC) {
          switch (config->sample_rate) {
            case 0:
                out->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
                break;
            case 8000:
            case 16000:
            case 48000:
                out->sample_rate = config->sample_rate;
                break;
            default:
                ALOGE("%s: Unsupported sampling rate %d for Incall Music", __func__,
                      config->sample_rate);
                config->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
                ret = -EINVAL;
                goto error_open;
        }
        //FIXME: add support for MONO stream configuration when audioflinger mixer supports it
        switch (config->channel_mask) {
            case AUDIO_CHANNEL_NONE:
            case AUDIO_CHANNEL_OUT_STEREO:
                out->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
                break;
            default:
                ALOGE("%s: Unsupported channel mask %#x for Incall Music", __func__,
                      config->channel_mask);
                config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
                ret = -EINVAL;
                goto error_open;
        }
        switch (config->format) {
            case AUDIO_FORMAT_DEFAULT:
            case AUDIO_FORMAT_PCM_16_BIT:
                out->format = AUDIO_FORMAT_PCM_16_BIT;
                break;
            default:
                ALOGE("%s: Unsupported format %#x for Incall Music", __func__,
                      config->format);
                config->format = AUDIO_FORMAT_PCM_16_BIT;
                ret = -EINVAL;
                goto error_open;
        }

        ret = voice_extn_check_and_set_incall_music_usecase(adev, out);
        if (ret != 0) {
            ALOGE("%s: Incall music delivery usecase cannot be set error:%d",
                __func__, ret);
            goto error_open;
        }
    } else if (is_single_device_type_equal(&out->device_list,
                                           AUDIO_DEVICE_OUT_TELEPHONY_TX)) {
        switch (config->sample_rate) {
            case 0:
                out->sample_rate = AFE_PROXY_SAMPLING_RATE;
                break;
            case 8000:
            case 16000:
            case 48000:
                out->sample_rate = config->sample_rate;
                break;
            default:
                ALOGE("%s: Unsupported sampling rate %d for Telephony TX", __func__,
                      config->sample_rate);
                config->sample_rate = AFE_PROXY_SAMPLING_RATE;
                ret = -EINVAL;
                break;
        }
        //FIXME: add support for MONO stream configuration when audioflinger mixer supports it
        switch (config->channel_mask) {
            case AUDIO_CHANNEL_NONE:
                out->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
                break;
            case AUDIO_CHANNEL_OUT_STEREO:
                out->channel_mask = config->channel_mask;
                break;
            default:
                ALOGE("%s: Unsupported channel mask %#x for Telephony TX", __func__,
                      config->channel_mask);
                config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
                ret = -EINVAL;
                break;
        }
        switch (config->format) {
            case AUDIO_FORMAT_DEFAULT:
                out->format = AUDIO_FORMAT_PCM_16_BIT;
                break;
            case AUDIO_FORMAT_PCM_16_BIT:
                out->format = config->format;
                break;
            default:
                ALOGE("%s: Unsupported format %#x for Telephony TX", __func__,
                      config->format);
                config->format = AUDIO_FORMAT_PCM_16_BIT;
                ret = -EINVAL;
                break;
        }
        if (ret != 0)
            goto error_open;

        out->usecase = USECASE_AUDIO_PLAYBACK_AFE_PROXY;
        out->config = pcm_config_afe_proxy_playback;
        out->config.rate = out->sample_rate;
        out->config.channels =
                audio_channel_count_from_out_mask(out->channel_mask);
        out->config.format = pcm_format_from_audio_format(out->format);
        adev->voice_tx_output = out;
    } else {
        unsigned int channels = 0;
        /*Update config params to default if not set by the caller*/
        if (config->sample_rate == 0)
            config->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
        if (config->channel_mask == AUDIO_CHANNEL_NONE)
            config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
        if (config->format == AUDIO_FORMAT_DEFAULT)
            config->format = AUDIO_FORMAT_PCM_16_BIT;

        channels = audio_channel_count_from_out_mask(out->channel_mask);

        if (out->flags & AUDIO_OUTPUT_FLAG_INTERACTIVE) {
            out->usecase = get_interactive_usecase(adev);
            out->config = pcm_config_low_latency;
        } else if (out->flags & AUDIO_OUTPUT_FLAG_RAW) {
            out->usecase = USECASE_AUDIO_PLAYBACK_ULL;
            out->realtime = may_use_noirq_mode(adev, USECASE_AUDIO_PLAYBACK_ULL,
                                               out->flags);
            out->config = out->realtime ? pcm_config_rt : pcm_config_low_latency;
        } else if (out->flags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
            out->usecase = USECASE_AUDIO_PLAYBACK_MMAP;
            out->config = pcm_config_mmap_playback;
            out->stream.start = out_start;
            out->stream.stop = out_stop;
            out->stream.create_mmap_buffer = out_create_mmap_buffer;
            out->stream.get_mmap_position = out_get_mmap_position;
        } else if (out->flags & AUDIO_OUTPUT_FLAG_FAST) {
            out->usecase = USECASE_AUDIO_PLAYBACK_LOW_LATENCY;
            out->hal_output_suspend_supported =
                property_get_bool("vendor.audio.hal.output.suspend.supported", false);
            out->dynamic_pm_qos_config_supported =
                property_get_bool("vendor.audio.hal.dynamic.qos.config.supported", false);
            if (!out->dynamic_pm_qos_config_supported) {
                ALOGI("%s: dynamic qos voting not enabled for platform", __func__);
            } else {
                ALOGI("%s: dynamic qos voting enabled for platform", __func__);
                //the mixer path will be a string similar to "low-latency-playback resume"
                strlcpy(out->pm_qos_mixer_path, use_case_table[out->usecase], MAX_MIXER_PATH_LEN);
                strlcat(out->pm_qos_mixer_path,
                            " resume", MAX_MIXER_PATH_LEN);
                ALOGI("%s: created %s pm_qos_mixer_path" , __func__,
                        out->pm_qos_mixer_path);
            }
            out->config = pcm_config_low_latency;
        } else if (out->flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) {
            out->usecase = USECASE_AUDIO_PLAYBACK_DEEP_BUFFER;
            out->config = pcm_config_deep_buffer;
            out->config.period_size = get_output_period_size(config->sample_rate, out->format,
                                                 channels, DEEP_BUFFER_OUTPUT_PERIOD_DURATION);
            if (out->config.period_size <= 0) {
                ALOGE("Invalid configuration period size is not valid");
                ret = -EINVAL;
                goto error_open;
            }
        } else if (flags & AUDIO_OUTPUT_FLAG_TTS) {
            out->usecase = USECASE_AUDIO_PLAYBACK_TTS;
            out->config = pcm_config_deep_buffer;
        } else if (config->channel_mask & AUDIO_CHANNEL_HAPTIC_ALL) {
            out->usecase = USECASE_AUDIO_PLAYBACK_WITH_HAPTICS;
            out->config = pcm_config_haptics_audio;
            if (force_haptic_path)
                adev->haptics_config = pcm_config_haptics_audio;
            else
                adev->haptics_config = pcm_config_haptics;

            channels =
                audio_channel_count_from_out_mask(out->channel_mask & ~AUDIO_CHANNEL_HAPTIC_ALL);

            if (force_haptic_path) {
                out->config.channels = 1;
                adev->haptics_config.channels = 1;
            } else
                adev->haptics_config.channels = audio_channel_count_from_out_mask(out->channel_mask & AUDIO_CHANNEL_HAPTIC_ALL);
        } else if (compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_BUS)) {
            ret = audio_extn_auto_hal_open_output_stream(out);
            if (ret) {
                ALOGE("%s: Failed to open output stream for bus device", __func__);
                ret = -EINVAL;
                goto error_open;
            }
        } else {
            /* primary path is the default path selected if no other outputs are available/suitable */
            out->usecase = GET_USECASE_AUDIO_PLAYBACK_PRIMARY(use_db_as_primary);
            out->config = GET_PCM_CONFIG_AUDIO_PLAYBACK_PRIMARY(use_db_as_primary);
        }
        out->hal_ip_format = format = out->format;
        out->config.format = hal_format_to_pcm(out->hal_ip_format);
        out->hal_op_format = pcm_format_to_hal(out->config.format);
        out->bit_width = format_to_bitwidth_table[out->hal_op_format] << 3;
        out->config.rate = config->sample_rate;
        out->sample_rate = out->config.rate;
        out->config.channels = channels;
        if (out->hal_ip_format != out->hal_op_format) {
            uint32_t buffer_size = out->config.period_size *
                                   format_to_bitwidth_table[out->hal_op_format] *
                                   out->config.channels;
            out->convert_buffer = calloc(1, buffer_size);
            if (out->convert_buffer == NULL){
                ALOGE("Allocation failed for convert buffer for size %d",
                       out->compr_config.fragment_size);
                ret = -ENOMEM;
                goto error_open;
            }
            ALOGD("Convert buffer allocated of size %d", buffer_size);
        }
    }

    ALOGV("%s devices:%d, format:%x, out->sample_rate:%d,out->bit_width:%d out->format:%d out->flags:%x, flags: %x usecase %d",
          __func__, devices, format, out->sample_rate, out->bit_width, out->format, out->flags, flags, out->usecase);

    /* TODO remove this hardcoding and check why width is zero*/
    if (out->bit_width == 0)
        out->bit_width = 16;
    audio_extn_utils_update_stream_output_app_type_cfg(adev->platform,
                                                &adev->streams_output_cfg_list,
                                                &out->device_list, out->flags,
                                                out->hal_op_format, out->sample_rate,
                                                out->bit_width, out->channel_mask, out->profile,
                                                &out->app_type_cfg);
    if ((out->usecase == (audio_usecase_t)(GET_USECASE_AUDIO_PLAYBACK_PRIMARY(use_db_as_primary))) ||
        (flags & AUDIO_OUTPUT_FLAG_PRIMARY)) {
        /* Ensure the default output is not selected twice */
        if(adev->primary_output == NULL)
            adev->primary_output = out;
        else {
            ALOGE("%s: Primary output is already opened", __func__);
            ret = -EEXIST;
            goto error_open;
        }
    }

    /* Check if this usecase is already existing */
    pthread_mutex_lock(&adev->lock);
    if ((get_usecase_from_list(adev, out->usecase) != NULL) &&
        (out->usecase != USECASE_COMPRESS_VOIP_CALL)) {
        ALOGE("%s: Usecase (%d) is already present", __func__, out->usecase);
        pthread_mutex_unlock(&adev->lock);
        ret = -EEXIST;
        goto error_open;
    }

    pthread_mutex_unlock(&adev->lock);

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
#ifdef NO_AUDIO_OUT
    out->stream.write = out_write_for_no_output;
#else
    out->stream.write = out_write;
#endif
    out->stream.get_render_position = out_get_render_position;
    out->stream.get_next_write_timestamp = out_get_next_write_timestamp;
    out->stream.get_presentation_position = out_get_presentation_position;

    if (out->realtime)
        out->af_period_multiplier = af_period_multiplier;
    else
        out->af_period_multiplier = 1;

    out->kernel_buffer_size = out->config.period_size * out->config.period_count;

    out->standby = 1;
    out->volume_l = PLAYBACK_GAIN_MAX;
    out->volume_r = PLAYBACK_GAIN_MAX;
    /* out->muted = false; by calloc() */
    /* out->written = 0; by calloc() */

    config->format = out->stream.common.get_format(&out->stream.common);
    config->channel_mask = out->stream.common.get_channels(&out->stream.common);
    config->sample_rate = out->stream.common.get_sample_rate(&out->stream.common);
    register_format(out->format, out->supported_formats);
    register_channel_mask(out->channel_mask, out->supported_channel_masks);
    register_sample_rate(out->sample_rate, out->supported_sample_rates);

#ifndef LINUX_ENABLED
    out->error_log = error_log_create(
            ERROR_LOG_ENTRIES,
            1000000000 /* aggregate consecutive identical errors within one second in ns */);
#endif
    /*
       By locking output stream before registering, we allow the callback
       to update stream's state only after stream's initial state is set to
       adev state.
    */
    lock_output_stream(out);
    audio_extn_snd_mon_register_listener(out, out_snd_mon_cb);
    pthread_mutex_lock(&adev->lock);
    out->card_status = adev->card_status;
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&out->lock);

    stream_app_type_cfg_init(&out->app_type_cfg);

    *stream_out = &out->stream;
    ALOGD("%s: Stream (%p) picks up usecase (%s)", __func__, &out->stream,
           use_case_table[out->usecase]);

    if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)
        audio_extn_dts_notify_playback_state(out->usecase, 0, out->sample_rate,
                                             popcount(out->channel_mask), out->playback_started);
    /* setup a channel for client <--> adsp communication for stream events */
    is_direct_passthough = audio_extn_passthru_is_direct_passthrough(out);
    if ((out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) ||
            (out->flags & AUDIO_OUTPUT_FLAG_DIRECT_PCM) ||
        audio_extn_ip_hdlr_intf_supported_for_copp(adev->platform) ||
        (audio_extn_ip_hdlr_intf_supported(config->format, is_direct_passthough, false))) {
        hdlr_stream_cfg.pcm_device_id = platform_get_pcm_device_id(
                out->usecase, PCM_PLAYBACK);
        hdlr_stream_cfg.flags = out->flags;
        hdlr_stream_cfg.type = PCM_PLAYBACK;
        ret = audio_extn_adsp_hdlr_stream_open(&out->adsp_hdlr_stream_handle,
                &hdlr_stream_cfg);
        if (ret) {
            ALOGE("%s: adsp_hdlr_stream_open failed %d",__func__, ret);
            out->adsp_hdlr_stream_handle = NULL;
        }
    }
    ip_hdlr_stream = audio_extn_ip_hdlr_intf_supported(config->format,
                                            is_direct_passthough, false);
    ip_hdlr_dev = audio_extn_ip_hdlr_intf_supported_for_copp(adev->platform);
    if (ip_hdlr_stream || ip_hdlr_dev ) {
        ret = audio_extn_ip_hdlr_intf_init(&out->ip_hdlr_handle, NULL, NULL, adev, out->usecase);
        if (ret < 0) {
            ALOGE("%s: audio_extn_ip_hdlr_intf_init failed %d",__func__, ret);
            out->ip_hdlr_handle = NULL;
        }
    }

    ret = io_streams_map_insert(adev, &out->stream.common,
                            out->handle, AUDIO_PATCH_HANDLE_NONE);
    if (ret != 0)
        goto error_open;

    out->out_ctxt.output = out;

    pthread_mutex_lock(&adev->lock);
    list_add_tail(&adev->active_outputs_list, &out->out_ctxt.list);
    pthread_mutex_unlock(&adev->lock);

    ALOGV("%s: exit", __func__);
    return 0;

error_open:
    if (out->convert_buffer)
        free(out->convert_buffer);
    free(out);
    *stream_out = NULL;
    ALOGD("%s: exit: ret %d", __func__, ret);
    return ret;
}

void adev_close_output_stream(struct audio_hw_device *dev __unused,
                                     struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    int ret = 0;

    ALOGD("%s: enter:stream_handle(%s)",__func__, use_case_table[out->usecase]);

    io_streams_map_remove(adev, out->handle);

    // remove out_ctxt early to prevent the stream
    // being opened in a race condition
    pthread_mutex_lock(&adev->lock);
    list_remove(&out->out_ctxt.list);
    pthread_mutex_unlock(&adev->lock);

    // must deregister from sndmonitor first to prevent races
    // between the callback and close_stream
    audio_extn_snd_mon_unregister_listener(out);

    /* close adsp hdrl session before standby */
    if (out->adsp_hdlr_stream_handle) {
        ret = audio_extn_adsp_hdlr_stream_close(out->adsp_hdlr_stream_handle);
        if (ret)
            ALOGE("%s: adsp_hdlr_stream_close failed %d",__func__, ret);
        out->adsp_hdlr_stream_handle = NULL;
    }

    if (out->ip_hdlr_handle) {
        audio_extn_ip_hdlr_intf_deinit(out->ip_hdlr_handle);
        out->ip_hdlr_handle = NULL;
    }

    if (out->usecase == USECASE_COMPRESS_VOIP_CALL) {
        pthread_mutex_lock(&adev->lock);
        ret = voice_extn_compress_voip_close_output_stream(&stream->common);
        out->started = 0;
        pthread_mutex_unlock(&adev->lock);
        if(ret != 0)
            ALOGE("%s: Compress voip output cannot be closed, error:%d",
                  __func__, ret);
    } else
        out_standby(&stream->common);

    if (is_offload_usecase(out->usecase)) {
        audio_extn_dts_remove_state_notifier_node(out->usecase);
        destroy_offload_callback_thread(out);
        free_offload_usecase(adev, out->usecase);
        if (out->compr_config.codec != NULL)
            free(out->compr_config.codec);
    }

    out->a2dp_muted = false;

    if (is_interactive_usecase(out->usecase))
        free_interactive_usecase(adev, out->usecase);

    if (out->convert_buffer != NULL) {
        free(out->convert_buffer);
        out->convert_buffer = NULL;
    }

    if (adev->voice_tx_output == out)
        adev->voice_tx_output = NULL;

#ifndef LINUX_ENABLED
    error_log_destroy(out->error_log);
    out->error_log = NULL;
#endif
    if (adev->primary_output == out)
        adev->primary_output = NULL;

    pthread_cond_destroy(&out->cond);
    pthread_mutex_destroy(&out->lock);
    pthread_mutex_destroy(&out->pre_lock);
    pthread_mutex_destroy(&out->latch_lock);
    pthread_mutex_destroy(&out->position_query_lock);

    pthread_mutex_lock(&adev->lock);
    free(stream);
    pthread_mutex_unlock(&adev->lock);
    ALOGV("%s: exit", __func__);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct str_parms *parms;
    char value[32];
    int val;
    int ret;
    int status = 0;
    bool a2dp_reconfig = false;
    struct listnode *node;
    int controller = -1, stream = -1;

    ALOGD("%s: enter: %s", __func__, kvpairs);
    parms = str_parms_create_str(kvpairs);

    if (!parms)
        goto error;

    /* notify adev and input/output streams on the snd card status */
    adev_snd_mon_cb((void *)adev, parms);

    ret = str_parms_get_str(parms, "SND_CARD_STATUS", value, sizeof(value));
    if (ret >= 0) {
        list_for_each(node, &adev->active_outputs_list) {
            streams_output_ctxt_t *out_ctxt = node_to_item(node,
                                                streams_output_ctxt_t,
                                                list);
            out_snd_mon_cb((void *)out_ctxt->output, parms);
        }

        list_for_each(node, &adev->active_inputs_list) {
            streams_input_ctxt_t *in_ctxt = node_to_item(node,
                                                streams_input_ctxt_t,
                                                list);
            in_snd_mon_cb((void *)in_ctxt->input, parms);
        }
    }

    pthread_mutex_lock(&adev->lock);
    ret = str_parms_get_str(parms, "BT_SCO", value, sizeof(value));
    if (ret >= 0) {
        /* When set to false, HAL should disable EC and NS */
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0){
            adev->bt_sco_on = true;
            /*
             * When ever BT_SCO=ON arrives, make sure to route
             * all use cases to SCO device, otherwise due to delay in
             * BT_SCO=ON and lack of synchronization with create audio patch
             * request for SCO device, some times use case not routed properly to
             * SCO device
             */
            struct audio_usecase *usecase;
            struct listnode *node;
            list_for_each(node, &adev->usecase_list) {
                usecase = node_to_item(node, struct audio_usecase, list);
                if (usecase->stream.in && (usecase->type == PCM_CAPTURE) &&
                    (!is_btsco_device(SND_DEVICE_NONE, usecase->in_snd_device)) && (is_sco_in_device_type(&usecase->stream.in->device_list))) {
                    ALOGD("BT_SCO ON, switch all in use case to it");
                    select_devices(adev, usecase->id);
                    }
                if (usecase->stream.out && (usecase->type == PCM_PLAYBACK ||
                                            usecase->type == VOICE_CALL) &&
                    (!is_btsco_device(usecase->out_snd_device, SND_DEVICE_NONE)) && (is_sco_out_device_type(&usecase->stream.out->device_list))) {
                     ALOGD("BT_SCO ON, switch all out use case to it");
                     select_devices(adev, usecase->id);
                    }
            }
         }
         else {
            adev->bt_sco_on = false;
            audio_extn_sco_reset_configuration();
        }
    }

    ret = str_parms_get_str(parms, "A2dpSuspended", value, sizeof(value));
    if (ret >= 0) {
        if (!strncmp(value, "false", 5) &&
            audio_extn_a2dp_source_is_suspended()) {
            struct audio_usecase *usecase;
            struct listnode *node;
            list_for_each(node, &adev->usecase_list) {
                usecase = node_to_item(node, struct audio_usecase, list);
                if (usecase->stream.in && (usecase->type == PCM_CAPTURE) &&
                    is_sco_in_device_type(&usecase->stream.in->device_list)) {
                    ALOGD("a2dp resumed, switch bt sco mic to handset mic");
                    reassign_device_list(&usecase->stream.in->device_list,
                                         AUDIO_DEVICE_IN_BUILTIN_MIC, "");
                    select_devices(adev, usecase->id);
                }
            }
        }
    }

    status = voice_set_parameters(adev, parms);
    if (status != 0)
        goto done;

    status = platform_set_parameters(adev->platform, parms);
    if (status != 0)
        goto done;

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_BT_NREC, value, sizeof(value));
    if (ret >= 0) {
        /* When set to false, HAL should disable EC and NS */
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            adev->bluetooth_nrec = true;
        else
            adev->bluetooth_nrec = false;
    }

    ret = str_parms_get_str(parms, "screen_state", value, sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            adev->screen_off = false;
        else
            adev->screen_off = true;
        audio_extn_sound_trigger_update_screen_status(adev->screen_off);
    }

    ret = str_parms_get_int(parms, "rotation", &val);
    if (ret >= 0) {
        bool reverse_speakers = false;
        int camera_rotation = CAMERA_ROTATION_LANDSCAPE;
        switch (val) {
        // FIXME: note that the code below assumes that the speakers are in the correct placement
        //   relative to the user when the device is rotated 90deg from its default rotation. This
        //   assumption is device-specific, not platform-specific like this code.
        case 270:
            reverse_speakers = true;
            camera_rotation = CAMERA_ROTATION_INVERT_LANDSCAPE;
            break;
        case 0:
        case 180:
            camera_rotation = CAMERA_ROTATION_PORTRAIT;
            break;
        case 90:
            camera_rotation = CAMERA_ROTATION_LANDSCAPE;
            break;
        default:
            ALOGE("%s: unexpected rotation of %d", __func__, val);
            status = -EINVAL;
        }
        if (status == 0) {
            // check and set swap
            //   - check if orientation changed and speaker active
            //   - set rotation and cache the rotation value
            adev->camera_orientation =
                (adev->camera_orientation & ~CAMERA_ROTATION_MASK) | camera_rotation;
            if (!audio_extn_is_maxx_audio_enabled())
                platform_check_and_set_swap_lr_channels(adev, reverse_speakers);
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_BT_SCO_WB, value, sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            adev->bt_wb_speech_enabled = true;
        else
            adev->bt_wb_speech_enabled = false;
    }

    ret = str_parms_get_str(parms, "bt_swb", value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        adev->swb_speech_mode = val;
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        audio_devices_t device = (audio_devices_t) val;
        if (audio_is_output_device(val) &&
            (val & AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
            ALOGV("cache new ext disp type and edid");
            platform_get_controller_stream_from_params(parms, &controller, &stream);
            platform_set_ext_display_device_v2(adev->platform, controller, stream);
            ret = platform_get_ext_disp_type_v2(adev->platform, controller, stream);
            if (ret < 0) {
                ALOGE("%s: Failed to query disp type, ret:%d", __func__, ret);
            } else {
                platform_cache_edid_v2(adev->platform, controller, stream);
            }
        } else if (audio_is_usb_out_device(device) || audio_is_usb_in_device(device)) {
            /*
             * Do not allow AFE proxy port usage by WFD source when USB headset is connected.
             * Per AudioPolicyManager, USB device is higher priority than WFD.
             * For Voice call over USB headset, voice call audio is routed to AFE proxy ports.
             * If WFD use case occupies AFE proxy, it may result unintended behavior while
             * starting voice call on USB
             */
            ret = str_parms_get_str(parms, "card", value, sizeof(value));
            if (ret >= 0)
                audio_extn_usb_add_device(device, atoi(value));

            if (!audio_extn_usb_is_tunnel_supported()) {
                ALOGV("detected USB connect .. disable proxy");
                adev->allow_afe_proxy_usage = false;
            }
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        audio_devices_t device = (audio_devices_t) val;
        /*
         * The HDMI / Displayport disconnect handling has been moved to
         * audio extension to ensure that its parameters are not
         * invalidated prior to updating sysfs of the disconnect event
         * Invalidate will be handled by audio_extn_ext_disp_set_parameters()
         */
        if (audio_is_usb_out_device(device) || audio_is_usb_in_device(device)) {
            ret = str_parms_get_str(parms, "card", value, sizeof(value));
            if (ret >= 0)
                audio_extn_usb_remove_device(device, atoi(value));

            if (!audio_extn_usb_is_tunnel_supported()) {
                ALOGV("detected USB disconnect .. enable proxy");
                adev->allow_afe_proxy_usage = true;
            }
        }
    }

    audio_extn_qdsp_set_parameters(adev, parms);

    status = audio_extn_a2dp_set_parameters(parms, &a2dp_reconfig);
    if (status >= 0 && a2dp_reconfig) {
        struct audio_usecase *usecase;
        struct listnode *node;
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if ((usecase->stream.out == NULL) || (usecase->type != PCM_PLAYBACK))
                continue;

            if (is_a2dp_out_device_type(&usecase->device_list)) {
                ALOGD("reconfigure a2dp... forcing device switch");
                audio_extn_a2dp_set_handoff_mode(true);
                ALOGD("Switching to speaker and muting the stream before select_devices");
                check_a2dp_restore_l(adev, usecase->stream.out, false);
                //force device switch to re configure encoder
                select_devices(adev, usecase->id);
                ALOGD("Unmuting the stream after select_devices");
                check_a2dp_restore_l(adev, usecase->stream.out, true);
                audio_extn_a2dp_set_handoff_mode(false);
                break;
            } else if (is_offload_usecase(usecase->stream.out->usecase)) {
                pthread_mutex_lock(&usecase->stream.out->latch_lock);
                if (usecase->stream.out->a2dp_muted) {
                    pthread_mutex_unlock(&usecase->stream.out->latch_lock);
                    reassign_device_list(&usecase->stream.out->device_list,
                                         AUDIO_DEVICE_OUT_BLUETOOTH_A2DP, "");
                    check_a2dp_restore_l(adev, usecase->stream.out, true);
                    break;
                }
                pthread_mutex_unlock(&usecase->stream.out->latch_lock);
            }
        }
    }

    //handle vr audio setparam
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_VR_AUDIO_MODE,
        value, sizeof(value));
    if (ret >= 0) {
        ALOGI("Setting vr mode to be %s", value);
        if (!strncmp(value, "true", 4)) {
            adev->vr_audio_mode_enabled = true;
            ALOGI("Setting vr mode to true");
        } else if (!strncmp(value, "false", 5)) {
            adev->vr_audio_mode_enabled = false;
            ALOGI("Setting vr mode to false");
        } else {
            ALOGI("wrong vr mode set");
        }
    }

    //FIXME: to be replaced by proper video capture properties API
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_CAMERA_FACING, value, sizeof(value));
    if (ret >= 0) {
        int camera_facing = CAMERA_FACING_BACK;
        if (strcmp(value, AUDIO_PARAMETER_VALUE_FRONT) == 0)
            camera_facing = CAMERA_FACING_FRONT;
        else if (strcmp(value, AUDIO_PARAMETER_VALUE_BACK) == 0)
            camera_facing = CAMERA_FACING_BACK;
        else {
            ALOGW("%s: invalid camera facing value: %s", __func__, value);
            goto done;
        }
        adev->camera_orientation =
                       (adev->camera_orientation & ~CAMERA_FACING_MASK) | camera_facing;
        struct audio_usecase *usecase;
        struct listnode *node;
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            struct stream_in *in = usecase->stream.in;
            if (usecase->type == PCM_CAPTURE && in != NULL &&
                    in->source == AUDIO_SOURCE_CAMCORDER && !in->standby) {
                select_devices(adev, in->usecase);
            }
        }
    }

    audio_extn_set_parameters(adev, parms);
done:
    str_parms_destroy(parms);
    pthread_mutex_unlock(&adev->lock);
error:
    ALOGV("%s: exit with code(%d)", __func__, status);
    return status;
}

static char* adev_get_parameters(const struct audio_hw_device *dev,
                                 const char *keys)
{
    ALOGD("%s:%s", __func__, keys);

    struct audio_device *adev = (struct audio_device *)dev;
    struct str_parms *reply = str_parms_create();
    struct str_parms *query = str_parms_create_str(keys);
    char *str;
    char value[256] = {0};
    int ret = 0;

    if (!query || !reply) {
        if (reply) {
            str_parms_destroy(reply);
        }
        if (query) {
            str_parms_destroy(query);
        }
        ALOGE("adev_get_parameters: failed to create query or reply");
        return NULL;
    }

    //handle vr audio getparam

    ret = str_parms_get_str(query,
        AUDIO_PARAMETER_KEY_VR_AUDIO_MODE,
        value, sizeof(value));

    if (ret >= 0) {
        bool vr_audio_enabled = false;
        pthread_mutex_lock(&adev->lock);
        vr_audio_enabled = adev->vr_audio_mode_enabled;
        pthread_mutex_unlock(&adev->lock);

        ALOGV("getting vr mode to %d", vr_audio_enabled);

        if (vr_audio_enabled) {
            str_parms_add_str(reply, AUDIO_PARAMETER_KEY_VR_AUDIO_MODE,
                "true");
            goto exit;
        } else {
            str_parms_add_str(reply, AUDIO_PARAMETER_KEY_VR_AUDIO_MODE,
                "false");
            goto exit;
        }
    }

    pthread_mutex_lock(&adev->lock);
    audio_extn_get_parameters(adev, query, reply);
    voice_get_parameters(adev, query, reply);
    audio_extn_a2dp_get_parameters(query, reply);
    platform_get_parameters(adev->platform, query, reply);
    audio_extn_ma_get_parameters(adev, query, reply);
    pthread_mutex_unlock(&adev->lock);

exit:
    str = str_parms_to_str(reply);
    str_parms_destroy(query);
    str_parms_destroy(reply);

    ALOGV("%s: exit: returns - %s", __func__, str);
    return str;
}

static int adev_init_check(const struct audio_hw_device *dev __unused)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    int ret;
    struct audio_device *adev = (struct audio_device *)dev;

    audio_extn_extspk_set_voice_vol(adev->extspk, volume);

    pthread_mutex_lock(&adev->lock);
    /* cache volume */
    ret = voice_set_volume(adev, volume);
    pthread_mutex_unlock(&adev->lock);
    return ret;
}

static int adev_set_master_volume(struct audio_hw_device *dev __unused,
                                  float volume __unused)
{
    return -ENOSYS;
}

static int adev_get_master_volume(struct audio_hw_device *dev __unused,
                                  float *volume __unused)
{
    return -ENOSYS;
}

static int adev_set_master_mute(struct audio_hw_device *dev __unused,
                                bool muted __unused)
{
    return -ENOSYS;
}

static int adev_get_master_mute(struct audio_hw_device *dev __unused,
                                bool *muted __unused)
{
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct listnode *node;
    struct audio_usecase *usecase = NULL;
    int ret = 0;

    pthread_mutex_lock(&adev->lock);
    if (adev->mode != mode) {
        ALOGD("%s: mode %d , prev_mode %d \n", __func__, mode , adev->mode);
        adev->prev_mode = adev->mode; /* prev_mode is kept to handle voip concurrency*/
        adev->mode = mode;
        if (mode == AUDIO_MODE_CALL_SCREEN) {
            adev->current_call_output = adev->primary_output;
            voice_start_call(adev);
        } else if (voice_is_in_call_or_call_screen(adev) &&
            (mode == AUDIO_MODE_NORMAL ||
             (mode == AUDIO_MODE_IN_COMMUNICATION && !voice_is_call_state_active(adev)))) {
            list_for_each(node, &adev->usecase_list) {
                usecase = node_to_item(node, struct audio_usecase, list);
                if (usecase->type == VOICE_CALL)
                    break;
            }
            if (usecase &&
                audio_is_usb_out_device(usecase->out_snd_device & AUDIO_DEVICE_OUT_ALL_USB)) {
                ret = audio_extn_usb_check_and_set_svc_int(usecase,
                                                           true);
                if (ret != 0) {
                    /* default service interval was successfully updated,
                       reopen USB backend with new service interval */
                    check_usecases_codec_backend(adev,
                                                 usecase,
                                                 usecase->out_snd_device);
                }
            }

            voice_stop_call(adev);
            platform_set_gsm_mode(adev->platform, false);
            adev->current_call_output = NULL;
            // restore device for other active usecases after stop call
            list_for_each(node, &adev->usecase_list) {
                usecase = node_to_item(node, struct audio_usecase, list);
                select_devices(adev, usecase->id);
            }
        }
    }
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    int ret;
    struct audio_device *adev = (struct audio_device *)dev;

    pthread_mutex_lock(&adev->lock);
    ALOGD("%s state %d\n", __func__, state);
    ret = voice_set_mic_mute((struct audio_device *)dev, state);

    if (adev->ext_hw_plugin)
        ret = audio_extn_ext_hw_plugin_set_mic_mute(adev->ext_hw_plugin, state);

    adev->mic_muted = state;
    pthread_mutex_unlock(&adev->lock);

    return ret;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    *state = voice_get_mic_mute((struct audio_device *)dev);
    return 0;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev __unused,
                                         const struct audio_config *config)
{
    int channel_count = audio_channel_count_from_in_mask(config->channel_mask);

    /* Don't know if USB HIFI in this context so use true to be conservative */
    if (check_input_parameters(config->sample_rate, config->format, channel_count,
                               true /*is_usb_hifi */) != 0)
        return 0;

    return get_input_buffer_size(config->sample_rate, config->format, channel_count,
            false /* is_low_latency: since we don't know, be conservative */);
}

static bool adev_input_allow_hifi_record(struct audio_device *adev,
                                         audio_devices_t devices,
                                         audio_input_flags_t flags,
                                         audio_source_t source) {
    const bool allowed = true;

    if (!audio_is_usb_in_device(devices))
        return !allowed;

    switch (flags) {
        case AUDIO_INPUT_FLAG_NONE:
            break;
        case AUDIO_INPUT_FLAG_FAST: // disallow hifi record for FAST as
                                    // it affects RTD numbers over USB
        default:
            return !allowed;
    }

    switch (source) {
        case AUDIO_SOURCE_DEFAULT:
        case AUDIO_SOURCE_MIC:
        case AUDIO_SOURCE_UNPROCESSED:
            break;
        default:
            return !allowed;
    }

    switch (adev->mode) {
        case 0:
            break;
        default:
            return !allowed;
    }

    return allowed;
}

static int adev_update_voice_comm_input_stream(struct stream_in *in,
                                               struct audio_config *config)
{
    bool valid_rate = (config->sample_rate == 8000 ||
                       config->sample_rate == 16000 ||
                       config->sample_rate == 32000 ||
                       config->sample_rate == 48000);
    bool valid_ch = audio_channel_count_from_in_mask(in->channel_mask) == 1;

    if(!voice_extn_is_compress_voip_supported()) {
        if (valid_rate && valid_ch) {
        in->usecase = USECASE_AUDIO_RECORD_VOIP;
        in->config = default_pcm_config_voip_copp;
        in->config.period_size = VOIP_IO_BUF_SIZE(in->sample_rate,
                                                  DEFAULT_VOIP_BUF_DURATION_MS,
                                                  DEFAULT_VOIP_BIT_DEPTH_BYTE)/2;
        } else {
            ALOGW("%s No valid input in voip, use defaults"
                   "sample rate %u, channel mask 0x%X",
                   __func__, config->sample_rate, in->channel_mask);
        }
        in->config.rate = config->sample_rate;
        in->sample_rate = config->sample_rate;
    } else {
        //XXX needed for voice_extn_compress_voip_open_input_stream
        in->config.rate = config->sample_rate;
        if ((in->dev->mode == AUDIO_MODE_IN_COMMUNICATION ||
             in->source == AUDIO_SOURCE_VOICE_COMMUNICATION ||
             voice_extn_compress_voip_is_active(in->dev)) &&
            (voice_extn_compress_voip_is_format_supported(in->format)) &&
            valid_rate && valid_ch) {
            voice_extn_compress_voip_open_input_stream(in);
            // update rate entries to match config from AF
            in->config.rate = config->sample_rate;
            in->sample_rate = config->sample_rate;
        } else {
            ALOGW("%s compress voip not active, use defaults", __func__);
        }
    }
    return 0;
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in,
                                  audio_input_flags_t flags,
                                  const char *address,
                                  audio_source_t source)
{
    struct audio_device *adev = (struct audio_device *)dev;
    struct stream_in *in;
    int ret = 0, buffer_size, frame_size;
    int channel_count = audio_channel_count_from_in_mask(config->channel_mask);
    bool is_low_latency = false;
    bool channel_mask_updated = false;
    bool is_usb_dev = audio_is_usb_in_device(devices);
    bool may_use_hifi_record = adev_input_allow_hifi_record(adev,
                                                            devices,
                                                            flags,
                                                            source);
    ALOGV("%s: enter: flags %#x, is_usb_dev %d, may_use_hifi_record %d,"
            " sample_rate %u, channel_mask %#x, format %#x",
            __func__, flags, is_usb_dev, may_use_hifi_record,
            config->sample_rate, config->channel_mask, config->format);

    if (is_usb_dev && (!audio_extn_usb_connected(NULL))) {
        is_usb_dev = false;
        devices = AUDIO_DEVICE_IN_BUILTIN_MIC;
        ALOGW("%s: ignore set device to non existing USB card, use input device(%#x)",
              __func__, devices);
    }

    *stream_in = NULL;

    if (!(is_usb_dev && may_use_hifi_record)) {
        if (config->sample_rate == 0)
            config->sample_rate = 48000;
        if (config->channel_mask == AUDIO_CHANNEL_NONE)
            config->channel_mask = AUDIO_CHANNEL_IN_MONO;
        if (config->format == AUDIO_FORMAT_DEFAULT)
            config->format = AUDIO_FORMAT_PCM_16_BIT;

        channel_count = audio_channel_count_from_in_mask(config->channel_mask);

        if (check_input_parameters(config->sample_rate, config->format, channel_count,
                                   false) != 0)
            return -EINVAL;
    }

    in = (struct stream_in *)calloc(1, sizeof(struct stream_in));

    if (!in) {
        ALOGE("failed to allocate input stream");
        return -ENOMEM;
    }

    ALOGD("%s: enter: sample_rate(%d) channel_mask(%#x) devices(%#x)\
        stream_handle(%p) io_handle(%d) source(%d) format %x",__func__, config->sample_rate,
        config->channel_mask, devices, &in->stream, handle, source, config->format);
    pthread_mutex_init(&in->lock, (const pthread_mutexattr_t *) NULL);
    pthread_mutex_init(&in->pre_lock, (const pthread_mutexattr_t *) NULL);

    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;
    in->stream.get_capture_position = in_get_capture_position;
    in->stream.get_active_microphones = in_get_active_microphones;
    in->stream.set_microphone_direction = in_set_microphone_direction;
    in->stream.set_microphone_field_dimension = in_set_microphone_field_dimension;
    in->stream.update_sink_metadata = in_update_sink_metadata;

    list_init(&in->device_list);
    update_device_list(&in->device_list, devices, address, true);
    in->source = source;
    in->dev = adev;
    in->standby = 1;
    in->capture_handle = handle;
    in->flags = flags;
    in->bit_width = 16;
    in->af_period_multiplier = 1;
    in->direction = MIC_DIRECTION_UNSPECIFIED;
    in->zoom = 0;
    list_init(&in->aec_list);
    list_init(&in->ns_list);
    in->mmap_shared_memory_fd = -1; // not open

    ALOGV("%s: source %d, config->channel_mask %#x", __func__, source, config->channel_mask);
    if (source == AUDIO_SOURCE_VOICE_UPLINK ||
        source == AUDIO_SOURCE_VOICE_DOWNLINK) {
        /* Force channel config requested to mono if incall
           record is being requested for only uplink/downlink */
        if (config->channel_mask != AUDIO_CHANNEL_IN_MONO) {
            config->channel_mask = AUDIO_CHANNEL_IN_MONO;
            ret = -EINVAL;
            goto err_open;
        }
    }

    if (is_usb_dev && may_use_hifi_record) {
        /* HiFi record selects an appropriate format, channel, rate combo
           depending on sink capabilities*/
        ret = read_usb_sup_params_and_compare(false /*is_playback*/,
                                              &config->format,
                                              &in->supported_formats[0],
                                              MAX_SUPPORTED_FORMATS,
                                              &config->channel_mask,
                                              &in->supported_channel_masks[0],
                                              MAX_SUPPORTED_CHANNEL_MASKS,
                                              &config->sample_rate,
                                              &in->supported_sample_rates[0],
                                              MAX_SUPPORTED_SAMPLE_RATES);
        if (ret != 0) {
            ret = -EINVAL;
            goto err_open;
        }
        channel_count = audio_channel_count_from_in_mask(config->channel_mask);
    } else if (config->format == AUDIO_FORMAT_DEFAULT) {
        config->format = AUDIO_FORMAT_PCM_16_BIT;
    } else if (property_get_bool("vendor.audio.capture.pcm.32bit.enable", false)
                                 && config->format == AUDIO_FORMAT_PCM_32_BIT) {
            in->config.format = PCM_FORMAT_S32_LE;
            in->bit_width = 32;
    } else if ((config->format == AUDIO_FORMAT_PCM_FLOAT) ||
               (config->format == AUDIO_FORMAT_PCM_32_BIT) ||
               (config->format == AUDIO_FORMAT_PCM_24_BIT_PACKED) ||
               (config->format == AUDIO_FORMAT_PCM_8_24_BIT)) {
        bool ret_error = false;
        in->bit_width = 24;
        /* 24 bit is restricted to UNPROCESSED source only,also format supported
           from HAL is 24_packed and 8_24
         *> In case of UNPROCESSED source, for 24 bit, if format requested is other than
            24_packed return error indicating supported format is 24_packed
         *> In case of any other source requesting 24 bit or float return error
            indicating format supported is 16 bit only.

            on error flinger will retry with supported format passed
         */
        if ((source != AUDIO_SOURCE_UNPROCESSED) &&
            (source != AUDIO_SOURCE_CAMCORDER)) {
            config->format = AUDIO_FORMAT_PCM_16_BIT;
            if (config->sample_rate > 48000)
                config->sample_rate = 48000;
            ret_error = true;
        } else if (!(config->format == AUDIO_FORMAT_PCM_24_BIT_PACKED ||
                     config->format == AUDIO_FORMAT_PCM_8_24_BIT)) {
            config->format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
            ret_error = true;
        }

        if (ret_error) {
            ret = -EINVAL;
            goto err_open;
        }
    }

    in->channel_mask = config->channel_mask;
    in->format = config->format;

    in->usecase = USECASE_AUDIO_RECORD;

    /* validate bus device address */
    if (compare_device_type(&in->device_list, AUDIO_DEVICE_IN_BUS)) {
        /* extract car audio stream index */
        in->car_audio_stream =
            audio_extn_auto_hal_get_car_audio_stream_from_address(address);
        if (in->car_audio_stream < 0) {
            ALOGE("%s: invalid car audio stream %x",
                __func__, in->car_audio_stream);
            ret = -EINVAL;
            goto err_open;
        }
        ALOGV("%s: car_audio_stream 0x%x", __func__, in->car_audio_stream);
        ret = audio_extn_auto_hal_open_input_stream(in);
        if (ret) {
            ALOGE("%s: Failed to open input stream for bus device", __func__);
            ret = -EINVAL;
            goto err_open;
        }
    }

    /* reassign use case for echo reference stream on automotive platforms */
    if (in->source == AUDIO_SOURCE_ECHO_REFERENCE) {
        ret = audio_extn_auto_hal_open_echo_reference_stream(in);
    }

    if (in->source == AUDIO_SOURCE_FM_TUNER) {
        if(!get_usecase_from_list(adev, USECASE_AUDIO_RECORD_FM_VIRTUAL))
            in->usecase = USECASE_AUDIO_RECORD_FM_VIRTUAL;
        else {
            ret = -EINVAL;
            goto err_open;
        }
    }

    if (config->sample_rate == LOW_LATENCY_CAPTURE_SAMPLE_RATE &&
            (flags & AUDIO_INPUT_FLAG_TIMESTAMP) == 0 &&
            (flags & AUDIO_INPUT_FLAG_COMPRESS) == 0 &&
            (flags & AUDIO_INPUT_FLAG_FAST) != 0) {
        is_low_latency = true;
#if LOW_LATENCY_CAPTURE_USE_CASE
        if ((flags & AUDIO_INPUT_FLAG_VOIP_TX) != 0)
            in->usecase = USECASE_AUDIO_RECORD_VOIP_LOW_LATENCY;
        else
            in->usecase = USECASE_AUDIO_RECORD_LOW_LATENCY;
#endif
        in->realtime = may_use_noirq_mode(adev, in->usecase, in->flags);
        if (!in->realtime) {
            in->config = pcm_config_audio_capture;
            frame_size = audio_stream_in_frame_size(&in->stream);
            buffer_size = get_input_buffer_size(config->sample_rate,
                                                config->format,
                                                channel_count,
                                                is_low_latency);
            in->config.period_size = buffer_size / frame_size;
            in->config.rate = config->sample_rate;
            in->af_period_multiplier = 1;
        } else {
            // period size is left untouched for rt mode playback
            in->config = pcm_config_audio_capture_rt;
            in->af_period_multiplier = af_period_multiplier;
        }
    }

    if ((config->sample_rate == LOW_LATENCY_CAPTURE_SAMPLE_RATE) &&
               ((in->flags & AUDIO_INPUT_FLAG_MMAP_NOIRQ) != 0)) {
        in->realtime = 0;
        in->usecase = USECASE_AUDIO_RECORD_MMAP;
        in->config = pcm_config_mmap_capture;
        in->config.format = pcm_format_from_audio_format(config->format);
        in->stream.start = in_start;
        in->stream.stop = in_stop;
        in->stream.create_mmap_buffer = in_create_mmap_buffer;
        in->stream.get_mmap_position = in_get_mmap_position;
        ALOGV("%s: USECASE_AUDIO_RECORD_MMAP", __func__);
    } else if (is_usb_dev && may_use_hifi_record) {
        in->usecase = USECASE_AUDIO_RECORD_HIFI;
        in->config = pcm_config_audio_capture;
        frame_size = audio_stream_in_frame_size(&in->stream);
        buffer_size = get_input_buffer_size(config->sample_rate,
                                            config->format,
                                            channel_count,
                                            false /*is_low_latency*/);
        in->config.period_size = buffer_size / frame_size;
        in->config.rate = config->sample_rate;
        in->config.format = pcm_format_from_audio_format(config->format);
        switch (config->format) {
        case AUDIO_FORMAT_PCM_32_BIT:
            in->bit_width = 32;
            break;
        case AUDIO_FORMAT_PCM_24_BIT_PACKED:
        case AUDIO_FORMAT_PCM_8_24_BIT:
            in->bit_width = 24;
            break;
        default:
            in->bit_width = 16;
        }
    } else if (is_single_device_type_equal(&in->device_list,
                                           AUDIO_DEVICE_IN_TELEPHONY_RX) ||
               is_single_device_type_equal(&in->device_list,
                                           AUDIO_DEVICE_IN_PROXY)) {
        if (config->sample_rate == 0)
            config->sample_rate = AFE_PROXY_SAMPLING_RATE;
        if (config->sample_rate != 48000 && config->sample_rate != 16000 &&
                config->sample_rate != 8000) {
            config->sample_rate = AFE_PROXY_SAMPLING_RATE;
            ret = -EINVAL;
            goto err_open;
        }
        if (config->format == AUDIO_FORMAT_DEFAULT)
            config->format = AUDIO_FORMAT_PCM_16_BIT;
        if (config->format != AUDIO_FORMAT_PCM_16_BIT) {
            config->format = AUDIO_FORMAT_PCM_16_BIT;
            ret = -EINVAL;
            goto err_open;
        }

        in->usecase = USECASE_AUDIO_RECORD_AFE_PROXY;
        if (adev->ha_proxy_enable &&
            is_single_device_type_equal(&in->device_list,
                                        AUDIO_DEVICE_IN_TELEPHONY_RX))
            in->usecase = USECASE_AUDIO_RECORD_AFE_PROXY2;
        in->config = pcm_config_afe_proxy_record;
        in->config.rate = config->sample_rate;
        in->af_period_multiplier = 1;
    } else if (in->source == AUDIO_SOURCE_VOICE_COMMUNICATION &&
               (!voice_extn_is_compress_voip_supported()) &&
               in->flags & AUDIO_INPUT_FLAG_VOIP_TX &&
               (config->sample_rate == 8000 ||
                config->sample_rate == 16000 ||
                config->sample_rate == 32000 ||
                config->sample_rate == 48000) &&
               channel_count == 1) {
        in->usecase = USECASE_AUDIO_RECORD_VOIP;
        in->config = pcm_config_audio_capture;
        frame_size = audio_stream_in_frame_size(&in->stream);
        buffer_size = get_stream_buffer_size(VOIP_CAPTURE_PERIOD_DURATION_MSEC,
                                             config->sample_rate,
                                             config->format,
                                             channel_count, false /*is_low_latency*/);
        in->config.period_size = buffer_size / frame_size;
        in->config.period_count = VOIP_CAPTURE_PERIOD_COUNT;
        in->config.rate = config->sample_rate;
        in->af_period_multiplier = 1;
    } else if (in->realtime) {
        in->config = pcm_config_audio_capture_rt;
        in->config.format = pcm_format_from_audio_format(config->format);
        in->af_period_multiplier = af_period_multiplier;
    } else {
        int ret_val;
        pthread_mutex_lock(&adev->lock);
        ret_val = audio_extn_check_and_set_multichannel_usecase(adev,
               in, config, &channel_mask_updated);
        pthread_mutex_unlock(&adev->lock);

        if (!ret_val) {
           if (channel_mask_updated == true) {
               ALOGD("%s: return error to retry with updated channel mask (%#x)",
                   __func__, config->channel_mask);
               ret = -EINVAL;
               goto err_open;
           }
           ALOGD("%s: created multi-channel session succesfully",__func__);
        } else if (audio_extn_compr_cap_enabled() &&
                   audio_extn_compr_cap_format_supported(config->format) &&
                   (in->dev->mode != AUDIO_MODE_IN_COMMUNICATION)) {
            audio_extn_compr_cap_init(in);
        } else if (audio_extn_cin_applicable_stream(in)) {
            ret = audio_extn_cin_configure_input_stream(in, config);
            if (ret)
                goto err_open;
        } else {
            in->config = pcm_config_audio_capture;
            in->config.rate = config->sample_rate;
            in->config.format = pcm_format_from_audio_format(config->format);
            in->format = config->format;
            frame_size = audio_stream_in_frame_size(&in->stream);
            buffer_size = get_input_buffer_size(config->sample_rate,
                                            config->format,
                                            channel_count,
                                            is_low_latency);
            /* prevent division-by-zero */
            if (frame_size == 0) {
                ALOGE("%s: Error frame_size==0", __func__);
                ret = -EINVAL;
                goto err_open;
            }

            in->config.period_size = buffer_size / frame_size;
            in->af_period_multiplier = 1;

            if (in->source == AUDIO_SOURCE_VOICE_COMMUNICATION) {
                /* optionally use VOIP usecase depending on config(s) */
                ret = adev_update_voice_comm_input_stream(in, config);
            }

            if (ret) {
                ALOGE("%s AUDIO_SOURCE_VOICE_COMMUNICATION invalid args", __func__);
                goto err_open;
            }
        }

        /* assign concurrent capture usecase if record has to caried out from
         * actual hardware input source */
        if (audio_extn_is_concurrent_capture_enabled() &&
            !audio_is_virtual_input_source(in->source)) {
            /* Acquire lock to avoid two concurrent use cases initialized to
               same pcm record use case */

            if (in->usecase == USECASE_AUDIO_RECORD) {
                pthread_mutex_lock(&adev->lock);
                if (!(adev->pcm_record_uc_state)) {
                    ALOGV("%s: using USECASE_AUDIO_RECORD",__func__);
                    adev->pcm_record_uc_state = 1;
                    pthread_mutex_unlock(&adev->lock);
                } else {
                    pthread_mutex_unlock(&adev->lock);
                    /* Assign compress record use case for second record */
                    in->usecase = USECASE_AUDIO_RECORD_COMPRESS2;
                    in->flags |= AUDIO_INPUT_FLAG_COMPRESS;
                    ALOGV("%s: overriding usecase with USECASE_AUDIO_RECORD_COMPRESS2 and appending compress flag", __func__);
                    if (audio_extn_cin_applicable_stream(in)) {
                        in->sample_rate = config->sample_rate;
                        ret = audio_extn_cin_configure_input_stream(in, config);
                        if (ret)
                            goto err_open;
                    }
                }
            }
        }
    }
    if (audio_extn_ssr_get_stream() != in)
        in->config.channels = channel_count;

    in->sample_rate  = in->config.rate;

    audio_extn_utils_update_stream_input_app_type_cfg(adev->platform,
                                                &adev->streams_input_cfg_list,
                                                &in->device_list, flags, in->format,
                                                in->sample_rate, in->bit_width,
                                                in->profile, &in->app_type_cfg);
    register_format(in->format, in->supported_formats);
    register_channel_mask(in->channel_mask, in->supported_channel_masks);
    register_sample_rate(in->sample_rate, in->supported_sample_rates);

#ifndef LINUX_ENABLED
    in->error_log = error_log_create(
            ERROR_LOG_ENTRIES,
            1000000000 /* aggregate consecutive identical errors within one second */);
#endif

    /* This stream could be for sound trigger lab,
       get sound trigger pcm if present */
    audio_extn_sound_trigger_check_and_get_session(in);

    lock_input_stream(in);
    audio_extn_snd_mon_register_listener(in, in_snd_mon_cb);
    pthread_mutex_lock(&adev->lock);
    in->card_status = adev->card_status;
    pthread_mutex_unlock(&adev->lock);
    pthread_mutex_unlock(&in->lock);

    stream_app_type_cfg_init(&in->app_type_cfg);

    *stream_in = &in->stream;

    ret = io_streams_map_insert(adev, &in->stream.common,
                            handle, AUDIO_PATCH_HANDLE_NONE);
    if (ret != 0)
        goto err_open;

    in->in_ctxt.input = in;

    pthread_mutex_lock(&adev->lock);
    list_add_tail(&adev->active_inputs_list, &in->in_ctxt.list);
    pthread_mutex_unlock(&adev->lock);

    ALOGV("%s: exit", __func__);
    return ret;

err_open:
    if (in->usecase == USECASE_AUDIO_RECORD) {
        pthread_mutex_lock(&adev->lock);
        adev->pcm_record_uc_state = 0;
        pthread_mutex_unlock(&adev->lock);
    }
    free(in);
    *stream_in = NULL;
    return ret;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                    struct audio_stream_in *stream)
{
    int ret;
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = (struct audio_device *)dev;

    ALOGD("%s: enter:stream_handle(%p)",__func__, in);

    if (in == NULL) {
        ALOGE("%s: audio_stream_in ptr is NULL", __func__);
        return;
    }
    io_streams_map_remove(adev, in->capture_handle);

    // remove out_ctxt early to prevent the stream
    // being opened in a race condition
    pthread_mutex_lock(&adev->lock);
    list_remove(&in->in_ctxt.list);
    pthread_mutex_unlock(&adev->lock);

    /* must deregister from sndmonitor first to prevent races
     * between the callback and close_stream
     */
    audio_extn_snd_mon_unregister_listener(stream);

    /* Disable echo reference if there are no active input, hfp call
     * and sound trigger while closing input stream
     */
    if (adev_get_active_input(adev) == NULL &&
        !audio_extn_hfp_is_active(adev) &&
        !audio_extn_sound_trigger_check_ec_ref_enable()) {
        struct listnode out_devices;
        list_init(&out_devices);
        platform_set_echo_reference(adev, false, &out_devices);
    } else
        audio_extn_sound_trigger_update_ec_ref_status(false);

#ifndef LINUX_ENABLED
    error_log_destroy(in->error_log);
    in->error_log = NULL;
#endif

    if (in->usecase == USECASE_COMPRESS_VOIP_CALL) {
        pthread_mutex_lock(&adev->lock);
        ret = voice_extn_compress_voip_close_input_stream(&stream->common);
        pthread_mutex_unlock(&adev->lock);
        if (ret != 0)
            ALOGE("%s: Compress voip input cannot be closed, error:%d",
                  __func__, ret);
    } else
        in_standby(&stream->common);

    pthread_mutex_destroy(&in->lock);
    pthread_mutex_destroy(&in->pre_lock);

    pthread_mutex_lock(&adev->lock);
    if (in->usecase == USECASE_AUDIO_RECORD) {
        adev->pcm_record_uc_state = 0;
    }

    if (in->source == AUDIO_SOURCE_VOICE_COMMUNICATION) {
        adev->enable_voicerx = false;
    }

    if (audio_extn_ssr_get_stream() == in) {
        audio_extn_ssr_deinit();
    }

    if (audio_extn_ffv_get_stream() == in) {
        audio_extn_ffv_stream_deinit();
    }

    if (audio_extn_compr_cap_enabled() &&
            audio_extn_compr_cap_format_supported(pcm_format_to_audio_format((in->config).format)))
        audio_extn_compr_cap_deinit();

    if (audio_extn_cin_attached_usecase(in))
        audio_extn_cin_free_input_stream_resources(in);

    if (in->is_st_session) {
        ALOGV("%s: sound trigger pcm stop lab", __func__);
        audio_extn_sound_trigger_stop_lab(in);
    }
    free(stream);
    pthread_mutex_unlock(&adev->lock);
    return;
}

/* verifies input and output devices and their capabilities.
 *
 * This verification is required when enabling extended bit-depth or
 * sampling rates, as not all qcom products support it.
 *
 * Suitable for calling only on initialization such as adev_open().
 * It fills the audio_device use_case_table[] array.
 *
 * Has a side-effect that it needs to configure audio routing / devices
 * in order to power up the devices and read the device parameters.
 * It does not acquire any hw device lock. Should restore the devices
 * back to "normal state" upon completion.
 */
static int adev_verify_devices(struct audio_device *adev)
{
    /* enumeration is a bit difficult because one really wants to pull
     * the use_case, device id, etc from the hidden pcm_device_table[].
     * In this case there are the following use cases and device ids.
     *
     * [USECASE_AUDIO_PLAYBACK_DEEP_BUFFER] = {0, 0},
     * [USECASE_AUDIO_PLAYBACK_LOW_LATENCY] = {15, 15},
     * [USECASE_AUDIO_PLAYBACK_HIFI] = {1, 1},
     * [USECASE_AUDIO_PLAYBACK_OFFLOAD] = {9, 9},
     * [USECASE_AUDIO_RECORD] = {0, 0},
     * [USECASE_AUDIO_RECORD_LOW_LATENCY] = {15, 15},
     * [USECASE_VOICE_CALL] = {2, 2},
     *
     * USECASE_AUDIO_PLAYBACK_OFFLOAD, USECASE_AUDIO_PLAYBACK_HIFI omitted.
     * USECASE_VOICE_CALL omitted, but possible for either input or output.
     */

    /* should be the usecases enabled in adev_open_input_stream() */
    static const int test_in_usecases[] = {
             USECASE_AUDIO_RECORD,
             USECASE_AUDIO_RECORD_LOW_LATENCY, /* does not appear to be used */
    };
    /* should be the usecases enabled in adev_open_output_stream()*/
    static const int test_out_usecases[] = {
            USECASE_AUDIO_PLAYBACK_DEEP_BUFFER,
            USECASE_AUDIO_PLAYBACK_LOW_LATENCY,
    };
    static const usecase_type_t usecase_type_by_dir[] = {
            PCM_PLAYBACK,
            PCM_CAPTURE,
    };
    static const unsigned flags_by_dir[] = {
            PCM_OUT,
            PCM_IN,
    };

    size_t i;
    unsigned dir;
    const unsigned card_id = adev->snd_card;

    for (dir = 0; dir < 2; ++dir) {
        const usecase_type_t usecase_type = usecase_type_by_dir[dir];
        const unsigned flags_dir = flags_by_dir[dir];
        const size_t testsize =
                dir ? ARRAY_SIZE(test_in_usecases) : ARRAY_SIZE(test_out_usecases);
        const int *testcases =
                dir ? test_in_usecases : test_out_usecases;
        const audio_devices_t audio_device =
                dir ? AUDIO_DEVICE_IN_BUILTIN_MIC : AUDIO_DEVICE_OUT_SPEAKER;

        for (i = 0; i < testsize; ++i) {
            const audio_usecase_t audio_usecase = testcases[i];
            int device_id;
            struct pcm_params **pparams;
            struct stream_out out;
            struct stream_in in;
            struct audio_usecase uc_info;
            int retval;

            pparams = &adev->use_case_table[audio_usecase];
            pcm_params_free(*pparams); /* can accept null input */
            *pparams = NULL;

            /* find the device ID for the use case (signed, for error) */
            device_id = platform_get_pcm_device_id(audio_usecase, usecase_type);
            if (device_id < 0)
                continue;

            /* prepare structures for device probing */
            memset(&uc_info, 0, sizeof(uc_info));
            uc_info.id = audio_usecase;
            uc_info.type = usecase_type;
            list_init(&uc_info.device_list);
            if (dir) {
                memset(&in, 0, sizeof(in));
                list_init(&in.device_list);
                update_device_list(&in.device_list, audio_device, "", true);
                in.source = AUDIO_SOURCE_VOICE_COMMUNICATION;
                uc_info.stream.in = &in;
            }
            memset(&out, 0, sizeof(out));
            list_init(&out.device_list);
            update_device_list(&out.device_list, audio_device, "", true);
            uc_info.stream.out = &out;
            update_device_list(&uc_info.device_list, audio_device, "", true);
            uc_info.in_snd_device = SND_DEVICE_NONE;
            uc_info.out_snd_device = SND_DEVICE_NONE;
            list_add_tail(&adev->usecase_list, &uc_info.list);

            /* select device - similar to start_(in/out)put_stream() */
            retval = select_devices(adev, audio_usecase);
            if (retval >= 0) {
                *pparams = pcm_params_get(card_id, device_id, flags_dir);
#if LOG_NDEBUG == 0
                char info[512]; /* for possible debug info */
                if (*pparams) {
                    ALOGV("%s: (%s) card %d  device %d", __func__,
                            dir ? "input" : "output", card_id, device_id);
                    pcm_params_to_string(*pparams, info, ARRAY_SIZE(info));
                } else {
                    ALOGV("%s: cannot locate card %d  device %d", __func__, card_id, device_id);
                }
#endif
            }

            /* deselect device - similar to stop_(in/out)put_stream() */
            /* 1. Get and set stream specific mixer controls */
            retval = disable_audio_route(adev, &uc_info);
            /* 2. Disable the rx device */
            retval = disable_snd_device(adev,
                    dir ? uc_info.in_snd_device : uc_info.out_snd_device);
            list_remove(&uc_info.list);
        }
    }
    return 0;
}

int update_patch(unsigned int num_sources,
                 const struct audio_port_config *sources,
                 unsigned int num_sinks,
                 const struct audio_port_config *sinks,
                 audio_patch_handle_t handle,
                 struct audio_patch_info *p_info,
                 patch_type_t patch_type, bool new_patch)
{
    ALOGV("%s: enter", __func__);

    if (p_info == NULL) {
        ALOGE("%s: Invalid patch pointer", __func__);
        return -EINVAL;
    }

    if (new_patch) {
        p_info->patch = (struct audio_patch *) calloc(1, sizeof(struct audio_patch));
        if (p_info->patch == NULL) {
            ALOGE("%s: Could not allocate patch", __func__);
            return -ENOMEM;
        }
    }

    p_info->patch->id = handle;
    p_info->patch->num_sources = num_sources;
    p_info->patch->num_sinks = num_sinks;

    for (int i = 0; i < num_sources; i++)
        p_info->patch->sources[i] = sources[i];
    for (int i = 0; i < num_sinks; i++)
        p_info->patch->sinks[i] = sinks[i];

    p_info->patch_type = patch_type;
    return 0;
}

audio_patch_handle_t generate_patch_handle()
{
    static audio_patch_handle_t patch_handle = AUDIO_PATCH_HANDLE_NONE;
    if (++patch_handle < 0)
        patch_handle = AUDIO_PATCH_HANDLE_NONE + 1;
    return patch_handle;
}

int adev_create_audio_patch(struct audio_hw_device *dev,
                            unsigned int num_sources,
                            const struct audio_port_config *sources,
                            unsigned int num_sinks,
                            const struct audio_port_config *sinks,
                            audio_patch_handle_t *handle)
{
    int ret = 0;
    struct audio_device *adev = (struct audio_device *)dev;
    struct audio_patch_info *p_info = NULL;
    patch_type_t patch_type = PATCH_NONE;
    audio_io_handle_t io_handle = AUDIO_IO_HANDLE_NONE;
    audio_source_t input_source = AUDIO_SOURCE_DEFAULT;
    struct audio_stream_info *s_info = NULL;
    struct audio_stream *stream = NULL;
    struct listnode devices;
    audio_devices_t device_type = AUDIO_DEVICE_NONE;
    bool new_patch = false;
    char addr[AUDIO_DEVICE_MAX_ADDRESS_LEN];

    ALOGD("%s: enter: num sources %d, num_sinks %d, handle %d", __func__,
           num_sources, num_sinks, *handle);

    if (num_sources == 0 || num_sources > AUDIO_PATCH_PORTS_MAX ||
        num_sinks == 0 || num_sinks > AUDIO_PATCH_PORTS_MAX) {
        ALOGE("%s: Invalid patch arguments", __func__);
        ret = -EINVAL;
        goto done;
    }

    if (num_sources > 1) {
        ALOGE("%s: Multiple sources are not supported", __func__);
        ret = -EINVAL;
        goto done;
    }

    if (sources == NULL || sinks == NULL) {
        ALOGE("%s: Invalid sources or sinks port config", __func__);
        ret = -EINVAL;
        goto done;
    }

    ALOGV("%s: source role %d, source type %d", __func__,
           sources[0].type, sources[0].role);
    list_init(&devices);

    // Populate source/sink information and fetch stream info
    switch (sources[0].type) {
        case AUDIO_PORT_TYPE_DEVICE: // Patch for audio capture or loopback
            device_type = sources[0].ext.device.type;
            strlcpy(&addr[0], &sources[0].ext.device.address[0], AUDIO_DEVICE_MAX_ADDRESS_LEN);
            update_device_list(&devices, device_type, &addr[0], true);
            if (sinks[0].type == AUDIO_PORT_TYPE_MIX) {
                patch_type = PATCH_CAPTURE;
                io_handle = sinks[0].ext.mix.handle;
                input_source = sinks[0].ext.mix.usecase.source;
                ALOGD("%s: Capture patch from device %x to mix %d",
                       __func__, device_type, io_handle);
            } else {
                // Device to device patch is not implemented.
                // This space will need changes if audio HAL
                // handles device to device patches in the future.
                patch_type = PATCH_DEVICE_LOOPBACK;
            }
            break;
        case AUDIO_PORT_TYPE_MIX: // Patch for audio playback
            io_handle = sources[0].ext.mix.handle;
            for (int i = 0; i < num_sinks; i++) {
                device_type = sinks[i].ext.device.type;
                strlcpy(&addr[0], &sinks[i].ext.device.address[0], AUDIO_DEVICE_MAX_ADDRESS_LEN);
                update_device_list(&devices, device_type, &addr[0], true);
            }
            patch_type = PATCH_PLAYBACK;
            ALOGD("%s: Playback patch from mix handle %d to device %x",
                   __func__, io_handle, get_device_types(&devices));
            break;
        case AUDIO_PORT_TYPE_SESSION:
        case AUDIO_PORT_TYPE_NONE:
            ALOGE("%s: Unsupported source type %d", __func__, sources[0].type);
            ret = -EINVAL;
            goto done;
    }

    pthread_mutex_lock(&adev->lock);

    // Generate patch info and update patch
    if (*handle == AUDIO_PATCH_HANDLE_NONE) {
        *handle = generate_patch_handle();
        p_info = (struct audio_patch_info *)
                      calloc(1, sizeof(struct audio_patch_info));
        if (p_info == NULL) {
            ALOGE("%s: Failed to allocate memory", __func__);
            pthread_mutex_unlock(&adev->lock);
            ret = -ENOMEM;
            goto done;
        }
        new_patch = true;
    } else {
        p_info = fetch_patch_info_l(adev, *handle);
        if (p_info == NULL) {
            ALOGE("%s: Unable to fetch patch for received patch handle %d",
                  __func__, *handle);
            pthread_mutex_unlock(&adev->lock);
            ret = -EINVAL;
            goto done;
        }
    }
    update_patch(num_sources, sources, num_sinks, sinks,
             *handle, p_info, patch_type, new_patch);

    // Fetch stream info of associated mix for playback or capture patches
    if (p_info->patch_type == PATCH_PLAYBACK ||
            p_info->patch_type == PATCH_CAPTURE) {
        s_info = hashmapGet(adev->io_streams_map, (void *) (intptr_t) io_handle);
        if (s_info == NULL) {
            ALOGE("%s: Failed to obtain stream info", __func__);
            if (new_patch)
                free(p_info);
            pthread_mutex_unlock(&adev->lock);
            ret = -EINVAL;
            goto done;
        }
        ALOGV("%s: Fetched stream info with io_handle %d", __func__, io_handle);
        s_info->patch_handle = *handle;
        stream = s_info->stream;
    }
    pthread_mutex_unlock(&adev->lock);

    // Update routing for stream
    if (stream != NULL) {
        if (p_info->patch_type == PATCH_PLAYBACK)
            ret = route_output_stream((struct stream_out *) stream, &devices);
        else if (p_info->patch_type == PATCH_CAPTURE)
            ret = route_input_stream((struct stream_in *) stream, &devices, input_source);
        if (ret < 0) {
            pthread_mutex_lock(&adev->lock);
            s_info->patch_handle = AUDIO_PATCH_HANDLE_NONE;
            if (new_patch)
                free(p_info);
            pthread_mutex_unlock(&adev->lock);
            ALOGE("%s: Stream routing failed for io_handle %d", __func__, io_handle);
            goto done;
        }
    }

    // Add new patch to patch map
    if (!ret && new_patch) {
        pthread_mutex_lock(&adev->lock);
        hashmapPut(adev->patch_map, (void *) (intptr_t) *handle, (void *) p_info);
        ALOGD("%s: Added a new patch with handle %d", __func__, *handle);
        pthread_mutex_unlock(&adev->lock);
    }

done:
    audio_extn_hw_loopback_create_audio_patch(dev,
                                        num_sources,
                                        sources,
                                        num_sinks,
                                        sinks,
                                        handle);
    audio_extn_auto_hal_create_audio_patch(dev,
                                        num_sources,
                                        sources,
                                        num_sinks,
                                        sinks,
                                        handle);
    return ret;
}

int adev_release_audio_patch(struct audio_hw_device *dev,
                           audio_patch_handle_t handle)
{
    struct audio_device *adev = (struct audio_device *) dev;
    int ret = 0;
    audio_source_t input_source = AUDIO_SOURCE_DEFAULT;
    struct audio_stream *stream = NULL;

    if (handle == AUDIO_PATCH_HANDLE_NONE) {
        ALOGE("%s: Invalid patch handle %d", __func__, handle);
        ret = -EINVAL;
        goto done;
    }

    ALOGD("%s: Remove patch with handle %d", __func__, handle);
    pthread_mutex_lock(&adev->lock);
    struct audio_patch_info *p_info = fetch_patch_info_l(adev, handle);
    if (p_info == NULL) {
        ALOGE("%s: Patch info not found with handle %d", __func__, handle);
        pthread_mutex_unlock(&adev->lock);
        ret = -EINVAL;
        goto done;
    }
    struct audio_patch *patch = p_info->patch;
    if (patch == NULL) {
        ALOGE("%s: Patch not found for handle %d", __func__, handle);
        pthread_mutex_unlock(&adev->lock);
        ret = -EINVAL;
        goto done;
    }
    audio_io_handle_t io_handle = AUDIO_IO_HANDLE_NONE;
    switch (patch->sources[0].type) {
        case AUDIO_PORT_TYPE_MIX:
            io_handle = patch->sources[0].ext.mix.handle;
            break;
        case AUDIO_PORT_TYPE_DEVICE:
            if (p_info->patch_type == PATCH_CAPTURE)
                io_handle = patch->sinks[0].ext.mix.handle;
            break;
        case AUDIO_PORT_TYPE_SESSION:
        case AUDIO_PORT_TYPE_NONE:
            pthread_mutex_unlock(&adev->lock);
            ret = -EINVAL;
            goto done;
    }

    // Remove patch and reset patch handle in stream info
    patch_type_t patch_type = p_info->patch_type;
    patch_map_remove_l(adev, handle);
    if (patch_type == PATCH_PLAYBACK ||
        patch_type == PATCH_CAPTURE) {
        struct audio_stream_info *s_info =
            hashmapGet(adev->io_streams_map, (void *) (intptr_t) io_handle);
        if (s_info == NULL) {
            ALOGE("%s: stream for io_handle %d is not available", __func__, io_handle);
            pthread_mutex_unlock(&adev->lock);
            goto done;
        }
        s_info->patch_handle = AUDIO_PATCH_HANDLE_NONE;
        stream = s_info->stream;
    }
    pthread_mutex_unlock(&adev->lock);

    if (stream != NULL) {
        struct listnode devices;
        list_init(&devices);
        if (patch_type == PATCH_PLAYBACK)
            ret = route_output_stream((struct stream_out *) stream, &devices);
        else if (patch_type == PATCH_CAPTURE)
            ret = route_input_stream((struct stream_in *) stream, &devices, input_source);
    }

    if (ret < 0)
        ALOGW("%s: Stream routing failed for io_handle %d", __func__, io_handle);

done:
    audio_extn_hw_loopback_release_audio_patch(dev, handle);
    audio_extn_auto_hal_release_audio_patch(dev, handle);

    ALOGV("%s: Successfully released patch %d", __func__, handle);
    return ret;
}

int adev_get_audio_port(struct audio_hw_device *dev, struct audio_port *config)
{
    int ret = 0;

    ret = audio_extn_hw_loopback_get_audio_port(dev, config);
    ret |= audio_extn_auto_hal_get_audio_port(dev, config);
    return ret;
}

int adev_set_audio_port_config(struct audio_hw_device *dev,
                        const struct audio_port_config *config)
{
    int ret = 0;

    ret = audio_extn_hw_loopback_set_audio_port_config(dev, config);
    ret |= audio_extn_auto_hal_set_audio_port_config(dev, config);
    return ret;
}

static int adev_dump(const audio_hw_device_t *device __unused,
                     int fd __unused)
{
    return 0;
}

static int adev_close(hw_device_t *device)
{
    size_t i;
    struct audio_device *adev_temp = (struct audio_device *)device;

    if (!adev_temp)
        return 0;

    pthread_mutex_lock(&adev_init_lock);

    if ((--audio_device_ref_count) == 0) {
         if (audio_extn_spkr_prot_is_enabled())
             audio_extn_spkr_prot_deinit();
        audio_extn_battery_properties_listener_deinit();
        audio_extn_snd_mon_unregister_listener(adev);
        audio_extn_sound_trigger_deinit(adev);
        audio_extn_listen_deinit(adev);
        audio_extn_qdsp_deinit();
        audio_extn_extspk_deinit(adev->extspk);
        audio_extn_utils_release_streams_cfg_lists(
                      &adev->streams_output_cfg_list,
                      &adev->streams_input_cfg_list);
        if (audio_extn_qap_is_enabled())
            audio_extn_qap_deinit();
        if (audio_extn_qaf_is_enabled())
            audio_extn_qaf_deinit();
        audio_route_free(adev->audio_route);
        audio_extn_gef_deinit(adev);
        free(adev->snd_dev_ref_cnt);
        platform_deinit(adev->platform);
        for (i = 0; i < ARRAY_SIZE(adev->use_case_table); ++i) {
            pcm_params_free(adev->use_case_table[i]);
        }
        if (adev->adm_deinit)
            adev->adm_deinit(adev->adm_data);
        qahwi_deinit(device);
        audio_extn_adsp_hdlr_deinit();
        audio_extn_snd_mon_deinit();
        audio_extn_hw_loopback_deinit(adev);
        audio_extn_ffv_deinit();
        if (adev->device_cfg_params) {
            free(adev->device_cfg_params);
            adev->device_cfg_params = NULL;
        }
        if(adev->ext_hw_plugin)
            audio_extn_ext_hw_plugin_deinit(adev->ext_hw_plugin);
        audio_extn_auto_hal_deinit();
        free_map(adev->patch_map);
        free_map(adev->io_streams_map);
        free(device);
        adev = NULL;
    }
    pthread_mutex_unlock(&adev_init_lock);
    enable_gcov();
    return 0;
}

/* This returns 1 if the input parameter looks at all plausible as a low latency period size,
 * or 0 otherwise.  A return value of 1 doesn't mean the value is guaranteed to work,
 * just that it _might_ work.
 */
static int period_size_is_plausible_for_low_latency(int period_size)
{
    switch (period_size) {
    case 160:
    case 192:
    case 240:
    case 320:
    case 480:
        return 1;
    default:
        return 0;
    }
}

static void adev_snd_mon_cb(void *cookie, struct str_parms *parms)
{
    bool is_snd_card_status = false;
    bool is_ext_device_status = false;
    char value[32];
    int card = -1;
    card_status_t status;

    if (cookie != adev || !parms)
        return;

    if (!parse_snd_card_status(parms, &card, &status)) {
        is_snd_card_status = true;
    } else if (0 < str_parms_get_str(parms, "ext_audio_device", value, sizeof(value))) {
        is_ext_device_status = true;
    } else {
        // not a valid event
        return;
    }

    pthread_mutex_lock(&adev->lock);
    if (card == adev->snd_card || is_ext_device_status) {
        if (is_snd_card_status && adev->card_status != status) {
            ALOGD("%s card_status %d", __func__, status);
            adev->card_status = status;
            platform_snd_card_update(adev->platform, status);
            audio_extn_fm_set_parameters(adev, parms);
            audio_extn_auto_hal_set_parameters(adev, parms);
            if (status == CARD_STATUS_OFFLINE)
                audio_extn_sco_reset_configuration();
        } else if (is_ext_device_status) {
            platform_set_parameters(adev->platform, parms);
        }
    }
    pthread_mutex_unlock(&adev->lock);
    return;
}

/* adev lock held */
int check_a2dp_restore_l(struct audio_device *adev, struct stream_out *out, bool restore)
{
    struct audio_usecase *uc_info;
    struct audio_usecase *usecase;
    struct listnode devices;
    struct listnode *node;

    uc_info = get_usecase_from_list(adev, out->usecase);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
              __func__, out->usecase);
        return -EINVAL;
    }
    list_init(&devices);

    ALOGD("%s: enter: usecase(%d: %s), a2dp muted %d", __func__,
          out->usecase, use_case_table[out->usecase], out->a2dp_muted);

    if (restore) {
        pthread_mutex_lock(&out->latch_lock);
        // restore A2DP device for active usecases and unmute if required
        if (is_a2dp_out_device_type(&out->device_list)) {
            ALOGD("%s: restoring A2dp and unmuting stream", __func__);
            if (uc_info->out_snd_device != SND_DEVICE_OUT_BT_A2DP)
                select_devices(adev, uc_info->id);

            if (is_offload_usecase(out->usecase)) {
                if (uc_info->out_snd_device == SND_DEVICE_OUT_BT_A2DP)
                    out_set_compr_volume(&out->stream, out->volume_l, out->volume_r);
            } else if (out->usecase == USECASE_AUDIO_PLAYBACK_VOIP) {
                out_set_voip_volume(&out->stream, out->volume_l, out->volume_r);
            } else {
                out_set_pcm_volume(&out->stream, out->volume_l, out->volume_r);
            }
            out->a2dp_muted = false;
        }
        pthread_mutex_unlock(&out->latch_lock);
    } else {
        pthread_mutex_lock(&out->latch_lock);
        // mute stream and switch to speaker if suspended
        if (!out->a2dp_muted && !out->standby) {
            assign_devices(&devices, &out->device_list);
            reassign_device_list(&out->device_list, AUDIO_DEVICE_OUT_SPEAKER, "");
            list_for_each(node, &adev->usecase_list) {
                usecase = node_to_item(node, struct audio_usecase, list);
                if ((usecase->type != PCM_CAPTURE) && (usecase != uc_info) &&
                    !is_a2dp_out_device_type(&usecase->stream.out->device_list) &&
                    !is_sco_out_device_type(&usecase->stream.out->device_list) &&
                    platform_check_backends_match(SND_DEVICE_OUT_SPEAKER,
                                                  usecase->out_snd_device)) {
                    assign_devices(&out->device_list, &usecase->stream.out->device_list);
                    break;
                }
            }
            if ((is_a2dp_out_device_type(&devices) && list_length(&devices) == 1) ||
                (uc_info->out_snd_device == SND_DEVICE_OUT_BT_A2DP)) {
                out->a2dp_muted = true;
                if (is_offload_usecase(out->usecase)) {
                    if (out->offload_state == OFFLOAD_STATE_PLAYING)
                        compress_pause(out->compr);
                    out_set_compr_volume(&out->stream, (float)0, (float)0);
                } else {
                    if (out->usecase == USECASE_AUDIO_PLAYBACK_VOIP)
                        out_set_voip_volume(&out->stream, (float)0, (float)0);
                    else
                        out_set_pcm_volume(&out->stream, (float)0, (float)0);

                    /* wait for stale pcm drained before switching to speaker */
                    uint32_t latency =
                        (out->config.period_count * out->config.period_size * 1000) /
                        (out->config.rate);
                    usleep(latency * 1000);
                }
            }
            select_devices(adev, out->usecase);
            ALOGD("%s: switched to device:%s and stream muted:%d", __func__,
                platform_get_snd_device_name(uc_info->out_snd_device), out->a2dp_muted);
            if (is_offload_usecase(out->usecase)) {
                if (out->offload_state == OFFLOAD_STATE_PLAYING)
                    compress_resume(out->compr);
            }
            assign_devices(&out->device_list, &devices);
        }
        pthread_mutex_unlock(&out->latch_lock);
    }
    ALOGV("%s: exit", __func__);
    return 0;
}

void adev_on_battery_status_changed(bool charging)
{
    pthread_mutex_lock(&adev->lock);
    ALOGI("%s: battery status changed to %scharging", __func__, charging ? "" : "not ");
    adev->is_charging = charging;
    audio_extn_sound_trigger_update_battery_status(charging);
    pthread_mutex_unlock(&adev->lock);
}

static int adev_open(const hw_module_t *module, const char *name,
                     hw_device_t **device)
{
    int ret;
    char value[PROPERTY_VALUE_MAX] = {0};
    char mixer_ctl_name[128] = {0};
    struct mixer_ctl *ctl = NULL;

    ALOGD("%s: enter", __func__);
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0) return -EINVAL;

    pthread_mutex_lock(&adev_init_lock);
    if (audio_device_ref_count != 0){
            *device = &adev->device.common;
            audio_device_ref_count++;
            ALOGD("%s: returning existing instance of adev", __func__);
            ALOGD("%s: exit", __func__);
            pthread_mutex_unlock(&adev_init_lock);
            return 0;
    }

    adev = calloc(1, sizeof(struct audio_device));

    if (!adev) {
        pthread_mutex_unlock(&adev_init_lock);
        return -ENOMEM;
    }

    pthread_mutex_init(&adev->lock, (const pthread_mutexattr_t *) NULL);

    // register audio ext hidl at the earliest
    audio_extn_hidl_init();
#ifdef DYNAMIC_LOG_ENABLED
    register_for_dynamic_logging("hal");
#endif

    /* default audio HAL major version */
    uint32_t maj_version = 3;
    if(property_get("vendor.audio.hal.maj.version", value, NULL))
        maj_version = atoi(value);

    adev->device.common.tag = HARDWARE_DEVICE_TAG;
    adev->device.common.version = HARDWARE_DEVICE_API_VERSION(maj_version, 0);
    adev->device.common.module = (struct hw_module_t *)module;
    adev->device.common.close = adev_close;

    adev->device.init_check = adev_init_check;
    adev->device.set_voice_volume = adev_set_voice_volume;
    adev->device.set_master_volume = adev_set_master_volume;
    adev->device.get_master_volume = adev_get_master_volume;
    adev->device.set_master_mute = adev_set_master_mute;
    adev->device.get_master_mute = adev_get_master_mute;
    adev->device.set_mode = adev_set_mode;
    adev->device.set_mic_mute = adev_set_mic_mute;
    adev->device.get_mic_mute = adev_get_mic_mute;
    adev->device.set_parameters = adev_set_parameters;
    adev->device.get_parameters = adev_get_parameters;
    adev->device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->device.open_output_stream = adev_open_output_stream;
    adev->device.close_output_stream = adev_close_output_stream;
    adev->device.open_input_stream = adev_open_input_stream;
    adev->device.close_input_stream = adev_close_input_stream;
    adev->device.create_audio_patch = adev_create_audio_patch;
    adev->device.release_audio_patch = adev_release_audio_patch;
    adev->device.get_audio_port = adev_get_audio_port;
    adev->device.set_audio_port_config = adev_set_audio_port_config;
    adev->device.dump = adev_dump;
    adev->device.get_microphones = adev_get_microphones;

    /* Set the default route before the PCM stream is opened */
    adev->mode = AUDIO_MODE_NORMAL;
    adev->primary_output = NULL;
    adev->out_device = AUDIO_DEVICE_NONE;
    adev->bluetooth_nrec = true;
    adev->acdb_settings = TTY_MODE_OFF;
    adev->allow_afe_proxy_usage = true;
    adev->bt_sco_on = false;
    /* adev->cur_hdmi_channels = 0;  by calloc() */
    adev->snd_dev_ref_cnt = calloc(SND_DEVICE_MAX, sizeof(int));
    /* Init audio and voice feature */
    audio_extn_feature_init();
    voice_extn_feature_init();
    voice_init(adev);
    list_init(&adev->usecase_list);
    list_init(&adev->active_inputs_list);
    list_init(&adev->active_outputs_list);
    list_init(&adev->audio_patch_record_list);
    adev->io_streams_map = hashmapCreate(AUDIO_IO_PORTS_MAX, audio_extn_utils_hash_fn,
                                         audio_extn_utils_hash_eq);
    if (!adev->io_streams_map) {
        ALOGE("%s: Could not create io streams map", __func__);
        ret = -ENOMEM;
        goto adev_open_err;
    }
    adev->patch_map = hashmapCreate(AUDIO_PATCH_PORTS_MAX, audio_extn_utils_hash_fn,
                                    audio_extn_utils_hash_eq);
    if (!adev->patch_map) {
        ALOGE("%s: Could not create audio patch map", __func__);
        ret = -ENOMEM;
        goto adev_open_err;
    }
    adev->cur_wfd_channels = 2;
    adev->offload_usecases_state = 0;
    adev->pcm_record_uc_state = 0;
    adev->is_channel_status_set = false;
    adev->perf_lock_opts[0] = 0x101;
    adev->perf_lock_opts[1] = 0x20E;
    adev->perf_lock_opts_size = 2;
    adev->dsp_bit_width_enforce_mode = 0;
    adev->enable_hfp = false;
    adev->use_old_pspd_mix_ctrl = false;
    adev->adm_routing_changed = false;
    adev->a2dp_started = false;

    audio_extn_perf_lock_init();

    /* Loads platform specific libraries dynamically */
    adev->platform = platform_init(adev);
    if (!adev->platform) {
        ALOGE("%s: Failed to init platform data, aborting.", __func__);
        ret = -EINVAL;
        goto adev_open_err;
    }

    adev->extspk = audio_extn_extspk_init(adev);
    if (audio_extn_qap_is_enabled()) {
        ret = audio_extn_qap_init(adev);
        if (ret < 0) {
            ALOGE("%s: Failed to init platform data, aborting.", __func__);
            goto adev_open_err;
        }
        adev->device.open_output_stream = audio_extn_qap_open_output_stream;
        adev->device.close_output_stream = audio_extn_qap_close_output_stream;
    }

    if (audio_extn_qaf_is_enabled()) {
        ret = audio_extn_qaf_init(adev);
        if (ret < 0) {
            ALOGE("%s: Failed to init platform data, aborting.", __func__);
            goto adev_open_err;
        }

        adev->device.open_output_stream = audio_extn_qaf_open_output_stream;
        adev->device.close_output_stream = audio_extn_qaf_close_output_stream;
    }

    audio_extn_auto_hal_init(adev);
    adev->ext_hw_plugin = audio_extn_ext_hw_plugin_init(adev);

    if (access(VISUALIZER_LIBRARY_PATH, R_OK) == 0) {
        adev->visualizer_lib = dlopen(VISUALIZER_LIBRARY_PATH, RTLD_NOW);
        if (adev->visualizer_lib == NULL) {
            ALOGE("%s: DLOPEN failed for %s", __func__, VISUALIZER_LIBRARY_PATH);
        } else {
            ALOGV("%s: DLOPEN successful for %s", __func__, VISUALIZER_LIBRARY_PATH);
            adev->visualizer_start_output =
                        (int (*)(audio_io_handle_t, int))dlsym(adev->visualizer_lib,
                                                        "visualizer_hal_start_output");
            adev->visualizer_stop_output =
                        (int (*)(audio_io_handle_t, int))dlsym(adev->visualizer_lib,
                                                        "visualizer_hal_stop_output");
        }
    }
    audio_extn_init(adev);
    voice_extn_init(adev);
    audio_extn_listen_init(adev, adev->snd_card);
    audio_extn_gef_init(adev);
    audio_extn_hw_loopback_init(adev);
    audio_extn_ffv_init(adev);

    if (access(OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH, R_OK) == 0) {
        adev->offload_effects_lib = dlopen(OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH, RTLD_NOW);
        if (adev->offload_effects_lib == NULL) {
            ALOGE("%s: DLOPEN failed for %s", __func__,
                  OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH);
        } else {
            ALOGV("%s: DLOPEN successful for %s", __func__,
                  OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH);
            adev->offload_effects_start_output =
                        (int (*)(audio_io_handle_t, int, struct mixer *))dlsym(adev->offload_effects_lib,
                                         "offload_effects_bundle_hal_start_output");
            adev->offload_effects_stop_output =
                        (int (*)(audio_io_handle_t, int))dlsym(adev->offload_effects_lib,
                                         "offload_effects_bundle_hal_stop_output");
            adev->offload_effects_set_hpx_state =
                        (int (*)(bool))dlsym(adev->offload_effects_lib,
                                         "offload_effects_bundle_set_hpx_state");
            adev->offload_effects_get_parameters =
                        (void (*)(struct str_parms *, struct str_parms *))
                                         dlsym(adev->offload_effects_lib,
                                         "offload_effects_bundle_get_parameters");
            adev->offload_effects_set_parameters =
                        (void (*)(struct str_parms *))dlsym(adev->offload_effects_lib,
                                         "offload_effects_bundle_set_parameters");
        }
    }

    if (access(ADM_LIBRARY_PATH, R_OK) == 0) {
        adev->adm_lib = dlopen(ADM_LIBRARY_PATH, RTLD_NOW);
        if (adev->adm_lib == NULL) {
            ALOGE("%s: DLOPEN failed for %s", __func__, ADM_LIBRARY_PATH);
        } else {
            ALOGV("%s: DLOPEN successful for %s", __func__, ADM_LIBRARY_PATH);
            adev->adm_init = (adm_init_t)
                                    dlsym(adev->adm_lib, "adm_init");
            adev->adm_deinit = (adm_deinit_t)
                                    dlsym(adev->adm_lib, "adm_deinit");
            adev->adm_register_input_stream = (adm_register_input_stream_t)
                                    dlsym(adev->adm_lib, "adm_register_input_stream");
            adev->adm_register_output_stream = (adm_register_output_stream_t)
                                    dlsym(adev->adm_lib, "adm_register_output_stream");
            adev->adm_deregister_stream = (adm_deregister_stream_t)
                                    dlsym(adev->adm_lib, "adm_deregister_stream");
            adev->adm_request_focus = (adm_request_focus_t)
                                    dlsym(adev->adm_lib, "adm_request_focus");
            adev->adm_abandon_focus = (adm_abandon_focus_t)
                                    dlsym(adev->adm_lib, "adm_abandon_focus");
            adev->adm_set_config = (adm_set_config_t)
                                    dlsym(adev->adm_lib, "adm_set_config");
            adev->adm_request_focus_v2 = (adm_request_focus_v2_t)
                                    dlsym(adev->adm_lib, "adm_request_focus_v2");
            adev->adm_is_noirq_avail = (adm_is_noirq_avail_t)
                                    dlsym(adev->adm_lib, "adm_is_noirq_avail");
            adev->adm_on_routing_change = (adm_on_routing_change_t)
                                    dlsym(adev->adm_lib, "adm_on_routing_change");
            adev->adm_request_focus_v2_1 = (adm_request_focus_v2_1_t)
                                    dlsym(adev->adm_lib, "adm_request_focus_v2_1");
        }
    }

    adev->enable_voicerx = false;
    adev->bt_wb_speech_enabled = false;
    adev->swb_speech_mode = SPEECH_MODE_INVALID;
    adev->fluence_nn_usecase_id = USECASE_INVALID;
    //initialize this to false for now,
    //this will be set to true through set param
    adev->vr_audio_mode_enabled = false;

    audio_extn_ds2_enable(adev);
    *device = &adev->device.common;

    if (k_enable_extended_precision)
        adev_verify_devices(adev);

    adev->dsp_bit_width_enforce_mode =
        adev_init_dsp_bit_width_enforce_mode(adev->mixer);

    audio_extn_utils_update_streams_cfg_lists(adev->platform, adev->mixer,
                                             &adev->streams_output_cfg_list,
                                             &adev->streams_input_cfg_list);

    audio_device_ref_count++;

    int trial;
    if (property_get("vendor.audio_hal.period_size", value, NULL) > 0) {
        trial = atoi(value);
        if (period_size_is_plausible_for_low_latency(trial)) {
            pcm_config_low_latency.period_size = trial;
            pcm_config_low_latency.start_threshold = trial / 4;
            pcm_config_low_latency.avail_min = trial / 4;
            configured_low_latency_capture_period_size = trial;
        }
    }
    if (property_get("vendor.audio_hal.in_period_size", value, NULL) > 0) {
        trial = atoi(value);
        if (period_size_is_plausible_for_low_latency(trial)) {
            configured_low_latency_capture_period_size = trial;
        }
    }

    adev->mic_break_enabled = property_get_bool("vendor.audio.mic_break", false);

    adev->camera_orientation = CAMERA_DEFAULT;

    if (property_get("vendor.audio_hal.period_multiplier",value,NULL) > 0) {
        af_period_multiplier = atoi(value);
        if (af_period_multiplier < 0)
            af_period_multiplier = 2;
        else if (af_period_multiplier > 4)
            af_period_multiplier = 4;

        ALOGV("new period_multiplier = %d", af_period_multiplier);
    }

    audio_extn_qdsp_init(adev->platform);

    adev->multi_offload_enable = property_get_bool("vendor.audio.offload.multiple.enabled", false);
    adev->ha_proxy_enable = property_get_bool("persist.vendor.audio.ha_proxy.enabled", false);
    pthread_mutex_unlock(&adev_init_lock);

    if (adev->adm_init)
        adev->adm_data = adev->adm_init();

    qahwi_init(*device);
    audio_extn_adsp_hdlr_init(adev->mixer);

    audio_extn_snd_mon_init();
    pthread_mutex_lock(&adev->lock);
    audio_extn_snd_mon_register_listener(adev, adev_snd_mon_cb);
    adev->card_status = CARD_STATUS_ONLINE;
    audio_extn_battery_properties_listener_init(adev_on_battery_status_changed);
    /*
     * if the battery state callback happens before charging can be queried,
     * it will be guarded with the adev->lock held in the cb function and so
     * the callback value will reflect the latest state
     */
    adev->is_charging = audio_extn_battery_properties_is_charging();
    audio_extn_sound_trigger_init(adev); /* dependent on snd_mon_init() */
    audio_extn_sound_trigger_update_battery_status(adev->is_charging);
    audio_extn_audiozoom_init();
    pthread_mutex_unlock(&adev->lock);
    /* Allocate memory for Device config params */
    adev->device_cfg_params = (struct audio_device_config_param*)
                                  calloc(platform_get_max_codec_backend(),
                                  sizeof(struct audio_device_config_param));
    if (adev->device_cfg_params == NULL)
        ALOGE("%s: Memory allocation failed for Device config params", __func__);

    /*
     * Check if new PSPD matrix mixer control is supported. If not
     * supported, then set flag so that old mixer ctrl is sent while
     * sending pspd coefficients on older kernel version. Query mixer
     * control for default pcm id and channel value one.
     */
    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
            "AudStr %d ChMixer Weight Ch %d", 0, 1);

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        adev->use_old_pspd_mix_ctrl = true;
    }

    ALOGD("%s: exit", __func__);
    return 0;

adev_open_err:
    free_map(adev->patch_map);
    free_map(adev->io_streams_map);
    free(adev->snd_dev_ref_cnt);
    pthread_mutex_destroy(&adev->lock);
    free(adev);
    adev = NULL;
    *device = NULL;
    pthread_mutex_unlock(&adev_init_lock);
    return ret;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "QCOM Audio HAL",
        .author = "The Linux Foundation",
        .methods = &hal_module_methods,
    },
};
