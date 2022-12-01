/*
 * Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
 * Not a contribution.
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

#ifndef QCOM_AUDIO_HW_H
#define QCOM_AUDIO_HW_H

#include <stdlib.h>
#include <cutils/str_parms.h>
#include <cutils/list.h>
#include <cutils/hashmap.h>
#include <hardware/audio.h>
#include <tinyalsa/asoundlib.h>
#include <tinycompress/tinycompress.h>

#include <audio_route/audio_route.h>
#ifndef LINUX_ENABLED
#include <audio_utils/ErrorLog.h>
#else
typedef int error_log_t;
#define error_log_dump(error_log, fd, prefix, lines, limit_ns)                 (0)
#define error_log_create(entries, aggregate_ns)                                (0)
#define error_log_destroy(error_log)                                           (0)
#endif
#ifndef LINUX_ENABLED
#include <audio_utils/Statistics.h>
#include <audio_utils/clock.h>
#endif
#include "audio_defs.h"
#include "voice.h"
#include "audio_hw_extn_api.h"
#include "device_utils.h"

#if LINUX_ENABLED
typedef struct {
   int64_t n;
   double min;
   double max;
   double last;
   double mean;
} simple_stats_t;
#define NANOS_PER_SECOND    1000000000LL
#endif

#if LINUX_ENABLED
#if defined(__LP64__)
#define VISUALIZER_LIBRARY_PATH "/usr/lib64/libqcomvisualizer.so"
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "/usr/lib64/libqcompostprocbundle.so"
#define ADM_LIBRARY_PATH "/usr/lib64/libadm.so"
#else
#define VISUALIZER_LIBRARY_PATH "/usr/lib/libqcomvisualizer.so"
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "/usr/lib/libqcompostprocbundle.so"
#define ADM_LIBRARY_PATH "/usr/lib/libadm.so"
#endif
#else
#define VISUALIZER_LIBRARY_PATH "/vendor/lib/soundfx/libqcomvisualizer.so"
#define OFFLOAD_EFFECTS_BUNDLE_LIBRARY_PATH "/vendor/lib/soundfx/libqcompostprocbundle.so"
#define ADM_LIBRARY_PATH "/vendor/lib/libadm.so"
#endif

/* Flags used to initialize acdb_settings variable that goes to ACDB library */
#define NONE_FLAG            0x00000000
#define ANC_FLAG	     0x00000001
#define DMIC_FLAG            0x00000002
#define QMIC_FLAG            0x00000004
/* Include TMIC Flag after existing QMIC flag to avoid backward compatibility
 * issues since they are bit masked */
#define TMIC_FLAG            0x00000008
#define TTY_MODE_OFF         0x00000010
#define TTY_MODE_FULL        0x00000020
#define TTY_MODE_VCO         0x00000040
#define TTY_MODE_HCO         0x00000080
#define TTY_MODE_CLEAR       0xFFFFFF0F
#define FLUENCE_MODE_CLEAR   0xFFFFFFF0

#define ACDB_DEV_TYPE_OUT 1
#define ACDB_DEV_TYPE_IN 2

/* SCO SWB codec mode */
#define SPEECH_MODE_INVALID  0xFFFF

/* support positional and index masks to 8ch */
#define MAX_SUPPORTED_CHANNEL_MASKS (2 * FCC_8)
#define MAX_SUPPORTED_FORMATS 15
#define MAX_SUPPORTED_SAMPLE_RATES 7
#define DEFAULT_HDMI_OUT_CHANNELS   2
#define DEFAULT_HDMI_OUT_SAMPLE_RATE 48000
#define DEFAULT_HDMI_OUT_FORMAT AUDIO_FORMAT_PCM_16_BIT

#define ERROR_LOG_ENTRIES 16

#define SND_CARD_STATE_OFFLINE 0
#define SND_CARD_STATE_ONLINE 1

#define STREAM_DIRECTION_IN 0
#define STREAM_DIRECTION_OUT 1

#define MAX_PERF_LOCK_OPTS 20

#define MAX_STREAM_PROFILE_STR_LEN 32
typedef enum {
    EFFECT_NONE = 0,
    EFFECT_AEC,
    EFFECT_NS,
    EFFECT_MAX
} effect_type_t;

struct audio_effect_config {
    uint32_t module_id;
    uint32_t instance_id;
    uint32_t param_id;
    uint32_t param_value;
};

struct audio_fluence_mmsecns_config {
    uint32_t topology_id;
    uint32_t module_id;
    uint32_t instance_id;
    uint32_t param_id;
};

#define MAX_MIXER_PATH_LEN 64

typedef enum card_status_t {
    CARD_STATUS_OFFLINE,
    CARD_STATUS_ONLINE
} card_status_t;

/* These are the supported use cases by the hardware.
 * Each usecase is mapped to a specific PCM device.
 * Refer to pcm_device_table[].
 */
enum {
    USECASE_INVALID = -1,
    /* Playback usecases */
    USECASE_AUDIO_PLAYBACK_DEEP_BUFFER = 0,
    USECASE_AUDIO_PLAYBACK_LOW_LATENCY,
    USECASE_AUDIO_PLAYBACK_MULTI_CH,
    USECASE_AUDIO_PLAYBACK_OFFLOAD,
    USECASE_AUDIO_PLAYBACK_OFFLOAD2,
    USECASE_AUDIO_PLAYBACK_OFFLOAD3,
    USECASE_AUDIO_PLAYBACK_OFFLOAD4,
    USECASE_AUDIO_PLAYBACK_OFFLOAD5,
    USECASE_AUDIO_PLAYBACK_OFFLOAD6,
    USECASE_AUDIO_PLAYBACK_OFFLOAD7,
    USECASE_AUDIO_PLAYBACK_OFFLOAD8,
    USECASE_AUDIO_PLAYBACK_OFFLOAD9,
    USECASE_AUDIO_PLAYBACK_ULL,
    USECASE_AUDIO_PLAYBACK_MMAP,
    USECASE_AUDIO_PLAYBACK_WITH_HAPTICS,
    USECASE_AUDIO_PLAYBACK_HAPTICS,
    USECASE_AUDIO_PLAYBACK_HIFI,
    USECASE_AUDIO_PLAYBACK_TTS,

    /* FM usecase */
    USECASE_AUDIO_PLAYBACK_FM,

    /* HFP Use case*/
    USECASE_AUDIO_HFP_SCO,
    USECASE_AUDIO_HFP_SCO_WB,
    USECASE_AUDIO_HFP_SCO_DOWNLINK,
    USECASE_AUDIO_HFP_SCO_WB_DOWNLINK,

    /* Capture usecases */
    USECASE_AUDIO_RECORD,
    USECASE_AUDIO_RECORD_COMPRESS,
    USECASE_AUDIO_RECORD_COMPRESS2,
    USECASE_AUDIO_RECORD_COMPRESS3,
    USECASE_AUDIO_RECORD_COMPRESS4,
    USECASE_AUDIO_RECORD_COMPRESS5,
    USECASE_AUDIO_RECORD_COMPRESS6,
    USECASE_AUDIO_RECORD_LOW_LATENCY,
    USECASE_AUDIO_RECORD_FM_VIRTUAL,
    USECASE_AUDIO_RECORD_HIFI,

    USECASE_AUDIO_PLAYBACK_VOIP,
    USECASE_AUDIO_RECORD_VOIP,
    /* Voice usecase */
    USECASE_VOICE_CALL,
    USECASE_AUDIO_RECORD_MMAP,

    /* Voice extension usecases */
    USECASE_VOICE2_CALL,
    USECASE_VOLTE_CALL,
    USECASE_QCHAT_CALL,
    USECASE_VOWLAN_CALL,
    USECASE_VOICEMMODE1_CALL,
    USECASE_VOICEMMODE2_CALL,
    USECASE_COMPRESS_VOIP_CALL,

    USECASE_INCALL_REC_UPLINK,
    USECASE_INCALL_REC_DOWNLINK,
    USECASE_INCALL_REC_UPLINK_AND_DOWNLINK,
    USECASE_INCALL_REC_UPLINK_COMPRESS,
    USECASE_INCALL_REC_DOWNLINK_COMPRESS,
    USECASE_INCALL_REC_UPLINK_AND_DOWNLINK_COMPRESS,

    USECASE_INCALL_MUSIC_UPLINK,
    USECASE_INCALL_MUSIC_UPLINK2,

    USECASE_AUDIO_SPKR_CALIB_RX,
    USECASE_AUDIO_SPKR_CALIB_TX,

    USECASE_AUDIO_PLAYBACK_AFE_PROXY,
    USECASE_AUDIO_RECORD_AFE_PROXY,
    USECASE_AUDIO_RECORD_AFE_PROXY2,
    USECASE_AUDIO_DSM_FEEDBACK,

    USECASE_AUDIO_PLAYBACK_SILENCE,

    USECASE_AUDIO_TRANSCODE_LOOPBACK_RX,
    USECASE_AUDIO_TRANSCODE_LOOPBACK_TX,

    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM1,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM2,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM3,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM4,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM5,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM6,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM7,
    USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM8,

    USECASE_AUDIO_EC_REF_LOOPBACK,

    USECASE_AUDIO_A2DP_ABR_FEEDBACK,

    /* car streams usecases */
    USECASE_AUDIO_PLAYBACK_MEDIA,
    USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION,
    USECASE_AUDIO_PLAYBACK_NAV_GUIDANCE,
    USECASE_AUDIO_PLAYBACK_PHONE,
    USECASE_AUDIO_PLAYBACK_FRONT_PASSENGER,
    USECASE_AUDIO_PLAYBACK_REAR_SEAT,
    USECASE_AUDIO_RECORD_BUS,
    USECASE_AUDIO_RECORD_BUS_FRONT_PASSENGER,
    USECASE_AUDIO_RECORD_BUS_REAR_SEAT,

    USECASE_AUDIO_PLAYBACK_SYNTHESIZER,

    /* Echo reference capture usecases */
    USECASE_AUDIO_RECORD_ECHO_REF_EXT,

    /*Audio FM Tuner usecase*/
    USECASE_AUDIO_FM_TUNER_EXT,
    /*voip usecase with low latency path*/
    USECASE_AUDIO_RECORD_VOIP_LOW_LATENCY,

    /*In Car Communication Usecase*/
    USECASE_ICC_CALL,
    AUDIO_USECASE_MAX
};

const char * const use_case_table[AUDIO_USECASE_MAX];

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/*
 * tinyAlsa library interprets period size as number of frames
 * one frame = channel_count * sizeof (pcm sample)
 * so if format = 16-bit PCM and channels = Stereo, frame size = 2 ch * 2 = 4 bytes
 * DEEP_BUFFER_OUTPUT_PERIOD_SIZE = 1024 means 1024 * 4 = 4096 bytes
 * We should take care of returning proper size when AudioFlinger queries for
 * the buffer size of an input/output stream
 */
enum {
    OFFLOAD_CMD_EXIT,               /* exit compress offload thread loop*/
    OFFLOAD_CMD_DRAIN,              /* send a full drain request to DSP */
    OFFLOAD_CMD_PARTIAL_DRAIN,      /* send a partial drain request to DSP */
    OFFLOAD_CMD_WAIT_FOR_BUFFER,    /* wait for buffer released by DSP */
    OFFLOAD_CMD_ERROR,              /* offload playback hit some error */
};

/*
 * Camera selection indicated via set_parameters "cameraFacing=front|back and
 * "rotation=0|90|180|270""
 */
enum {
  CAMERA_FACING_BACK = 0x0,
  CAMERA_FACING_FRONT = 0x1,
  CAMERA_FACING_MASK = 0x0F,
  CAMERA_ROTATION_LANDSCAPE = 0x0,
  CAMERA_ROTATION_INVERT_LANDSCAPE = 0x10,
  CAMERA_ROTATION_PORTRAIT = 0x20,
  CAMERA_ROTATION_MASK = 0xF0,
  CAMERA_BACK_LANDSCAPE = (CAMERA_FACING_BACK|CAMERA_ROTATION_LANDSCAPE),
  CAMERA_BACK_INVERT_LANDSCAPE = (CAMERA_FACING_BACK|CAMERA_ROTATION_INVERT_LANDSCAPE),
  CAMERA_BACK_PORTRAIT = (CAMERA_FACING_BACK|CAMERA_ROTATION_PORTRAIT),
  CAMERA_FRONT_LANDSCAPE = (CAMERA_FACING_FRONT|CAMERA_ROTATION_LANDSCAPE),
  CAMERA_FRONT_INVERT_LANDSCAPE = (CAMERA_FACING_FRONT|CAMERA_ROTATION_INVERT_LANDSCAPE),
  CAMERA_FRONT_PORTRAIT = (CAMERA_FACING_FRONT|CAMERA_ROTATION_PORTRAIT),

  CAMERA_DEFAULT = CAMERA_BACK_LANDSCAPE,
};

//FIXME: to be replaced by proper video capture properties API
#define AUDIO_PARAMETER_KEY_CAMERA_FACING "cameraFacing"
#define AUDIO_PARAMETER_VALUE_FRONT "front"
#define AUDIO_PARAMETER_VALUE_BACK "back"

enum {
    OFFLOAD_STATE_IDLE,
    OFFLOAD_STATE_PLAYING,
    OFFLOAD_STATE_PAUSED,
};

struct offload_cmd {
    struct listnode node;
    int cmd;
    int data[];
};

typedef enum render_mode {
    RENDER_MODE_AUDIO_NO_TIMESTAMP = 0,
    RENDER_MODE_AUDIO_MASTER,
    RENDER_MODE_AUDIO_STC_MASTER,
} render_mode_t;

/* This defines the physical car audio streams supported in
 * audio HAL, limited by the available frontend PCM devices.
 * Max number of physical streams supported is 32 and is
 * represented by stream bit flag.
 *     Primary zone: bit 0 - 7
 *     Front passenger zone: bit 8 - 15
 *     Rear seat zone: bit 16 - 23
 */
#define MAX_CAR_AUDIO_STREAMS    32
enum {
    CAR_AUDIO_STREAM_MEDIA              = 0x1,
    CAR_AUDIO_STREAM_SYS_NOTIFICATION   = 0x2,
    CAR_AUDIO_STREAM_NAV_GUIDANCE       = 0x4,
    CAR_AUDIO_STREAM_PHONE              = 0x8,
    CAR_AUDIO_STREAM_IN_PRIMARY         = 0x10,
    CAR_AUDIO_STREAM_FRONT_PASSENGER    = 0x100,
    CAR_AUDIO_STREAM_IN_FRONT_PASSENGER = 0x200,
    CAR_AUDIO_STREAM_REAR_SEAT          = 0x10000,
    CAR_AUDIO_STREAM_IN_REAR_SEAT       = 0x20000,
};

struct stream_app_type_cfg {
    int sample_rate;
    uint32_t bit_width;
    int app_type;
    int gain[2];
};

struct stream_config {
    unsigned int sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    struct listnode device_list;
    unsigned int bit_width;
};

typedef struct streams_input_ctxt {
    struct listnode list;
    struct stream_in *input;
} streams_input_ctxt_t;

typedef struct streams_output_ctxt {
    struct listnode list;
    struct stream_out *output;
} streams_output_ctxt_t;

struct stream_inout {
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    pthread_mutex_t pre_lock; /* acquire before lock to avoid DOS by playback thread */
    pthread_cond_t  cond;
    struct stream_config in_config;
    struct stream_config out_config;
    struct stream_app_type_cfg out_app_type_cfg;
    char profile[MAX_STREAM_PROFILE_STR_LEN];
    struct audio_device *dev;
    void *adsp_hdlr_stream_handle;
    void *ip_hdlr_handle;
    stream_callback_t client_callback;
    void *client_cookie;
};

struct stream_out {
    struct audio_stream_out stream;
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    pthread_mutex_t pre_lock; /* acquire before lock to avoid DOS by playback thread */
    pthread_cond_t  cond;
    /* stream_out->lock is of large granularity, and can only be held before device lock
     * latch is a supplemetary lock to protect certain fields of out stream (such as
     * offload_state, a2dp_muted, to add any stream member that needs to be accessed
     * with device lock held) and it can be held after device lock
     */
    pthread_mutex_t latch_lock;
    pthread_mutex_t position_query_lock;
    struct pcm_config config;
    struct compr_config compr_config;
    struct pcm *pcm;
    struct compress *compr;
    int standby;
    int pcm_device_id;
    unsigned int sample_rate;
    audio_channel_mask_t channel_mask;
    audio_format_t format;
    struct listnode device_list;
    audio_output_flags_t flags;
    char profile[MAX_STREAM_PROFILE_STR_LEN];
    audio_usecase_t usecase;
    /* Array of supported channel mask configurations. +1 so that the last entry is always 0 */
    audio_channel_mask_t supported_channel_masks[MAX_SUPPORTED_CHANNEL_MASKS + 1];
    audio_format_t supported_formats[MAX_SUPPORTED_FORMATS+1];
    uint32_t supported_sample_rates[MAX_SUPPORTED_SAMPLE_RATES+1];
    bool muted;
    uint64_t written; /* total frames written, not cleared when entering standby */
    int64_t mmap_time_offset_nanos; /* fudge factor to correct inaccuracies in DSP */
    int     mmap_shared_memory_fd; /* file descriptor associated with MMAP NOIRQ shared memory */
    audio_io_handle_t handle;
    streams_output_ctxt_t out_ctxt;
    struct stream_app_type_cfg app_type_cfg;

    int non_blocking;
    int playback_started;
    int offload_state; /* guarded by latch_lock */
    pthread_cond_t offload_cond;
    pthread_t offload_thread;
    struct listnode offload_cmd_list;
    bool offload_thread_blocked;
    struct timespec writeAt;

    void *adsp_hdlr_stream_handle;
    void *ip_hdlr_handle;

    stream_callback_t client_callback;
    void *client_cookie;
    struct compr_gapless_mdata gapless_mdata;
    int send_new_metadata;
    bool send_next_track_params;
    bool is_compr_metadata_avail;
    unsigned int bit_width;
    uint32_t hal_fragment_size;
    audio_format_t hal_ip_format;
    audio_format_t hal_op_format;
    void *convert_buffer;

    bool realtime;
    int af_period_multiplier;
    struct audio_device *dev;
    card_status_t card_status;

    void* qaf_stream_handle;
    void* qap_stream_handle;
    pthread_cond_t qaf_offload_cond;
    pthread_t qaf_offload_thread;
    struct listnode qaf_offload_cmd_list;
    uint32_t platform_latency;
    render_mode_t render_mode;
    bool drift_correction_enabled;

    struct audio_out_channel_map_param channel_map_param; /* input channel map */
    audio_offload_info_t info;
    int started;
    qahwi_stream_out_t qahwi_out;

    bool is_iec61937_info_available;
    bool a2dp_muted; /* guarded by latch_lock */
    float volume_l;
    float volume_r;
    bool apply_volume;

    char pm_qos_mixer_path[MAX_MIXER_PATH_LEN];
    int hal_output_suspend_supported;
    int dynamic_pm_qos_config_supported;
    bool stream_config_changed;
    mix_matrix_params_t pan_scale_params;
    mix_matrix_params_t downmix_params;
    bool set_dual_mono;
    bool prev_card_status_offline;
#ifndef LINUX_ENABLED
    error_log_t *error_log;
#endif
    bool pspd_coeff_sent;

    int car_audio_stream; /* handle for car_audio_stream */

    union {
        char *addr;
        struct {
            int controller;
            int stream;
        } cs;
    } extconn;

    size_t kernel_buffer_size;  // cached value of the alsa buffer size, const after open().

    // last out_get_presentation_position() cached info.
    bool         last_fifo_valid;
    unsigned int last_fifo_frames_remaining;
    int64_t      last_fifo_time_ns;

    simple_stats_t fifo_underruns;  // TODO: keep a list of the last N fifo underrun times.
    simple_stats_t start_latency_ms;
};

struct stream_in {
    struct audio_stream_in stream;
    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    pthread_mutex_t pre_lock; /* acquire before lock to avoid DOS by playback thread */
    struct pcm_config config;
    struct pcm *pcm;
    int standby;
    int source;
    int pcm_device_id;
    struct listnode device_list;
    audio_channel_mask_t channel_mask;
    audio_usecase_t usecase;
    bool enable_aec;
    bool enable_ns;
    audio_format_t format;
    bool enable_ec_port;
    bool ec_opened;
    struct listnode aec_list;
    struct listnode ns_list;
    int64_t mmap_time_offset_nanos; /* fudge factor to correct inaccuracies in DSP */
    int     mmap_shared_memory_fd; /* file descriptor associated with MMAP NOIRQ shared memory */
    audio_io_handle_t capture_handle;
    streams_input_ctxt_t in_ctxt;
    audio_input_flags_t flags;
    char profile[MAX_STREAM_PROFILE_STR_LEN];
    bool is_st_session;
    bool is_st_session_active;
    unsigned int sample_rate;
    unsigned int bit_width;
    bool realtime;
    int af_period_multiplier;
    struct stream_app_type_cfg app_type_cfg;
    void *cin_extn;
    qahwi_stream_in_t qahwi_in;

    struct audio_device *dev;
    card_status_t card_status;
    int capture_started;
    float zoom;
    audio_microphone_direction_t direction;

    volatile int32_t capture_stopped;

    /* Array of supported channel mask configurations. +1 so that the last entry is always 0 */
    audio_channel_mask_t supported_channel_masks[MAX_SUPPORTED_CHANNEL_MASKS + 1];
    audio_format_t supported_formats[MAX_SUPPORTED_FORMATS + 1];
    uint32_t supported_sample_rates[MAX_SUPPORTED_SAMPLE_RATES + 1];

    int64_t frames_read; /* total frames read, not cleared when entering standby */
    int64_t frames_muted; /* total frames muted, not cleared when entering standby */

#ifndef LINUX_ENABLED
    error_log_t *error_log;
#endif
    simple_stats_t start_latency_ms;

    int car_audio_stream; /* handle for car_audio_stream*/
};

typedef enum {
    PCM_PLAYBACK,
    PCM_CAPTURE,
    VOICE_CALL,
    VOIP_CALL,
    PCM_HFP_CALL,
    TRANSCODE_LOOPBACK_RX,
    TRANSCODE_LOOPBACK_TX,
    PCM_PASSTHROUGH,
    ICC_CALL,
    SYNTH_LOOPBACK,
    USECASE_TYPE_MAX
} usecase_type_t;

typedef enum {
    PATCH_NONE = -1,
    PATCH_PLAYBACK,
    PATCH_CAPTURE,
    PATCH_DEVICE_LOOPBACK
} patch_type_t;

struct audio_patch_info {
    struct audio_patch *patch;
    patch_type_t patch_type;
};

struct audio_stream_info {
    struct audio_stream *stream;
    audio_patch_handle_t patch_handle;
};

union stream_ptr {
    struct stream_in *in;
    struct stream_out *out;
    struct stream_inout *inout;
};

struct audio_usecase {
    struct listnode list;
    audio_usecase_t id;
    usecase_type_t  type;
    struct listnode device_list;
    snd_device_t out_snd_device;
    snd_device_t in_snd_device;
    struct stream_app_type_cfg out_app_type_cfg;
    struct stream_app_type_cfg in_app_type_cfg;
    union stream_ptr stream;
};

struct stream_format {
    struct listnode list;
    audio_format_t format;
};

struct stream_sample_rate {
    struct listnode list;
    uint32_t sample_rate;
};

typedef union {
    audio_output_flags_t out_flags;
    audio_input_flags_t in_flags;
} audio_io_flags_t;

struct streams_io_cfg {
    struct listnode list;
    audio_io_flags_t flags;
    char profile[MAX_STREAM_PROFILE_STR_LEN];
    struct listnode format_list;
    struct listnode sample_rate_list;
    struct stream_app_type_cfg app_type_cfg;
};

typedef void* (*adm_init_t)();
typedef void (*adm_deinit_t)(void *);
typedef void (*adm_register_output_stream_t)(void *, audio_io_handle_t, audio_output_flags_t);
typedef void (*adm_register_input_stream_t)(void *, audio_io_handle_t, audio_input_flags_t);
typedef void (*adm_deregister_stream_t)(void *, audio_io_handle_t);
typedef void (*adm_request_focus_t)(void *, audio_io_handle_t);
typedef void (*adm_abandon_focus_t)(void *, audio_io_handle_t);
typedef void (*adm_set_config_t)(void *, audio_io_handle_t,
                                         struct pcm *,
                                         struct pcm_config *);
typedef void (*adm_request_focus_v2_t)(void *, audio_io_handle_t, long);
typedef bool (*adm_is_noirq_avail_t)(void *, int, int, int);
typedef void (*adm_on_routing_change_t)(void *, audio_io_handle_t);
typedef int (*adm_request_focus_v2_1_t)(void *, audio_io_handle_t, long);

struct audio_device {
    struct audio_hw_device device;

    pthread_mutex_t lock; /* see note below on mutex acquisition order */
    pthread_mutex_t cal_lock;
    struct mixer *mixer;
    audio_mode_t mode;
    audio_mode_t prev_mode;
    audio_devices_t out_device;
    struct stream_out *primary_output;
    struct stream_out *voice_tx_output;
    struct stream_out *current_call_output;
    bool bluetooth_nrec;
    bool screen_off;
    int *snd_dev_ref_cnt;
    struct listnode usecase_list;
    struct listnode streams_output_cfg_list;
    struct listnode streams_input_cfg_list;
    struct audio_route *audio_route;
    int acdb_settings;
    bool speaker_lr_swap;
    struct voice voice;
    unsigned int cur_hdmi_channels;
    audio_format_t cur_hdmi_format;
    unsigned int cur_hdmi_sample_rate;
    unsigned int cur_hdmi_bit_width;
    unsigned int cur_wfd_channels;
    bool bt_wb_speech_enabled;
    unsigned int swb_speech_mode;
    bool allow_afe_proxy_usage;
    bool is_charging; // from battery listener
    bool mic_break_enabled;
    bool enable_hfp;
    bool mic_muted;
    bool enable_voicerx;
    unsigned int num_va_sessions;

    int snd_card;
    card_status_t card_status;
    unsigned int cur_codec_backend_samplerate;
    unsigned int cur_codec_backend_bit_width;
    bool is_channel_status_set;
    void *platform;
    void *extspk;
    unsigned int offload_usecases_state;
    unsigned int pcm_record_uc_state;
    void *visualizer_lib;
    int (*visualizer_start_output)(audio_io_handle_t, int);
    int (*visualizer_stop_output)(audio_io_handle_t, int);
    void *offload_effects_lib;
    int (*offload_effects_start_output)(audio_io_handle_t, int, struct mixer *);
    int (*offload_effects_stop_output)(audio_io_handle_t, int);

    int (*offload_effects_set_hpx_state)(bool);

    void *adm_data;
    void *adm_lib;
    adm_init_t adm_init;
    adm_deinit_t adm_deinit;
    adm_register_input_stream_t adm_register_input_stream;
    adm_register_output_stream_t adm_register_output_stream;
    adm_deregister_stream_t adm_deregister_stream;
    adm_request_focus_t adm_request_focus;
    adm_abandon_focus_t adm_abandon_focus;
    adm_set_config_t adm_set_config;
    adm_request_focus_v2_t adm_request_focus_v2;
    adm_is_noirq_avail_t adm_is_noirq_avail;
    adm_on_routing_change_t adm_on_routing_change;
    adm_request_focus_v2_1_t adm_request_focus_v2_1;

    void (*offload_effects_get_parameters)(struct str_parms *,
                                           struct str_parms *);
    void (*offload_effects_set_parameters)(struct str_parms *);

    bool multi_offload_enable;
    int perf_lock_handle;
    int perf_lock_opts[MAX_PERF_LOCK_OPTS];
    int perf_lock_opts_size;
    bool native_playback_enabled;
    bool asrc_mode_enabled;
    qahwi_device_t qahwi_dev;
    bool vr_audio_mode_enabled;
    uint32_t dsp_bit_width_enforce_mode;
    bool bt_sco_on;
    struct audio_device_config_param *device_cfg_params;
    unsigned int interactive_usecase_state;
    bool dp_allowed_for_voice;
    void *ext_hw_plugin;

    struct pcm_config haptics_config;
    struct pcm *haptic_pcm;
    int    haptic_pcm_device_id;
    uint8_t *haptic_buffer;
    size_t haptic_buffer_size;
    int fluence_nn_usecase_id;

    /* logging */
    snd_device_t last_logged_snd_device[AUDIO_USECASE_MAX][2]; /* [out, in] */

    /* The pcm_params use_case_table is loaded by adev_verify_devices() upon
     * calling adev_open().
     *
     * If an entry is not NULL, it can be used to determine if extended precision
     * or other capabilities are present for the device corresponding to that usecase.
     */
    struct pcm_params *use_case_table[AUDIO_USECASE_MAX];
    struct listnode active_inputs_list;
    struct listnode active_outputs_list;
    bool use_old_pspd_mix_ctrl;
    int camera_orientation; /* CAMERA_BACK_LANDSCAPE ... CAMERA_FRONT_PORTRAIT */
    bool adm_routing_changed;
    struct listnode audio_patch_record_list;
    Hashmap *patch_map;
    Hashmap *io_streams_map;
    bool a2dp_started;
    bool ha_proxy_enable;
};

struct audio_patch_record {
    struct listnode list;
    audio_patch_handle_t handle;
    audio_usecase_t usecase;
    struct audio_patch patch;
};

int select_devices(struct audio_device *adev,
                          audio_usecase_t uc_id);
int disable_audio_route(struct audio_device *adev,
                        struct audio_usecase *usecase);
int disable_snd_device(struct audio_device *adev,
                       snd_device_t snd_device);
int enable_snd_device(struct audio_device *adev,
                      snd_device_t snd_device);

int enable_audio_route(struct audio_device *adev,
                       struct audio_usecase *usecase);

struct audio_usecase *get_usecase_from_list(const struct audio_device *adev,
                                                   audio_usecase_t uc_id);

bool is_offload_usecase(audio_usecase_t uc_id);

bool audio_is_true_native_stream_active(struct audio_device *adev);

bool audio_is_dsd_native_stream_active(struct audio_device *adev);

uint32_t adev_get_dsp_bit_width_enforce_mode();

int pcm_ioctl(struct pcm *pcm, int request, ...);

audio_usecase_t get_usecase_id_from_usecase_type(const struct audio_device *adev,
                                                 usecase_type_t type);

/* adev lock held */
int check_a2dp_restore_l(struct audio_device *adev, struct stream_out *out, bool restore);

int adev_open_output_stream(struct audio_hw_device *dev,
                            audio_io_handle_t handle,
                            audio_devices_t devices,
                            audio_output_flags_t flags,
                            struct audio_config *config,
                            struct audio_stream_out **stream_out,
                            const char *address);
void adev_close_output_stream(struct audio_hw_device *dev __unused,
                              struct audio_stream_out *stream);

bool is_interactive_usecase(audio_usecase_t uc_id);

size_t get_output_period_size(uint32_t sample_rate,
                            audio_format_t format,
                            int channel_count,
                            int duration /*in millisecs*/);

#define LITERAL_TO_STRING(x) #x
#define CHECK(condition) LOG_ALWAYS_FATAL_IF(!(condition), "%s",\
            __FILE__ ":" LITERAL_TO_STRING(__LINE__)\
            " ASSERT_FATAL(" #condition ") failed.")

static inline bool is_loopback_input_device(audio_devices_t device) {
    if (!audio_is_output_device(device) &&
         ((device & AUDIO_DEVICE_IN_LOOPBACK) == AUDIO_DEVICE_IN_LOOPBACK))
        return true;
    else
        return false;
}

static inline bool audio_is_virtual_input_source(audio_source_t source) {
    bool result = false;
    switch(source) {
        case AUDIO_SOURCE_VOICE_UPLINK :
        case AUDIO_SOURCE_VOICE_DOWNLINK :
        case AUDIO_SOURCE_VOICE_CALL :
        case AUDIO_SOURCE_FM_TUNER :
            result = true;
            break;
        default:
            break;
    }
    return result;
}

int route_output_stream(struct stream_out *stream,
                        struct listnode *devices);
int route_input_stream(struct stream_in *stream,
                       struct listnode *devices,
                       audio_source_t source);

audio_patch_handle_t generate_patch_handle();

/*
 * NOTE: when multiple mutexes have to be acquired, always take the
 * stream_in or stream_out mutex first, followed by the audio_device mutex
 * and latch at last.
 */

static inline audio_format_t pcm_format_to_audio_format(const enum pcm_format format)
{
   audio_format_t ret = AUDIO_FORMAT_INVALID;
   switch(format) {
        case PCM_FORMAT_S16_LE:
            ret = (audio_format_t)AUDIO_FORMAT_PCM_SUB_16_BIT;
            break;
        case PCM_FORMAT_S32_LE:
           ret = (audio_format_t)AUDIO_FORMAT_PCM_SUB_32_BIT;
           break;
        case PCM_FORMAT_S8:
           ret = (audio_format_t)AUDIO_FORMAT_PCM_SUB_8_BIT;
           break;
        case PCM_FORMAT_S24_LE:
           ret = (audio_format_t)AUDIO_FORMAT_PCM_SUB_8_24_BIT;
           break;
        case PCM_FORMAT_S24_3LE:
           ret = (audio_format_t)AUDIO_FORMAT_PCM_SUB_24_BIT_PACKED;
           break;
        default:
           break;
      }
      return ret;
}

#endif // QCOM_AUDIO_HW_H
