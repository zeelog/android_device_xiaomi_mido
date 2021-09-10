/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2011 The Android Open Source Project *
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

#include <sys/cdefs.h>
#include <stdint.h>
#include <system/audio.h>

#ifndef QTI_AUDIO_HAL_DEFS_H
#define QTI_AUDIO_HAL_DEFS_H

__BEGIN_DECLS

/**************************************/

/**
 *  standard audio parameters that the HAL may need to handle
 */

/**
 *  audio device parameters
 */

/* BT SCO Noise Reduction + Echo Cancellation parameters */
#define QAHW_PARAMETER_KEY_BT_NREC "bt_headset_nrec"
#define QAHW_PARAMETER_VALUE_ON "on"
#define QAHW_PARAMETER_VALUE_OFF "off"

/* TTY mode selection */
#define QAHW_PARAMETER_KEY_TTY_MODE "tty_mode"
#define QAHW_PARAMETER_VALUE_TTY_OFF "tty_off"
#define QAHW_PARAMETER_VALUE_TTY_VCO "tty_vco"
#define QAHW_PARAMETER_VALUE_TTY_HCO "tty_hco"
#define QAHW_PARAMETER_VALUE_TTY_FULL "tty_full"

/* Hearing Aid Compatibility - Telecoil (HAC-T) mode on/off
   Strings must be in sync with CallFeaturesSetting.java */
#define QAHW_PARAMETER_KEY_HAC "HACSetting"
#define QAHW_PARAMETER_VALUE_HAC_ON "ON"
#define QAHW_PARAMETER_VALUE_HAC_OFF "OFF"

/* A2DP sink address set by framework */
#define QAHW_PARAMETER_A2DP_SINK_ADDRESS "a2dp_sink_address"

/* A2DP source address set by framework */
#define QAHW_PARAMETER_A2DP_SOURCE_ADDRESS "a2dp_source_address"

/* Screen state */
#define QAHW_PARAMETER_KEY_SCREEN_STATE "screen_state"

/* Bluetooth SCO wideband */
#define QAHW_PARAMETER_KEY_BT_SCO_WB "bt_wbs"

/* Get a new HW synchronization source identifier.
 * Return a valid source (positive integer) or AUDIO_HW_SYNC_INVALID if an error occurs
 * or no HW sync is available. */
#define QAHW_PARAMETER_HW_AV_SYNC "hw_av_sync"

/**
 *  audio stream parameters
 */

#define QAHW_PARAMETER_STREAM_ROUTING "routing"             /* audio_devices_t */
#define QAHW_PARAMETER_STREAM_FORMAT "format"               /* audio_format_t */
#define QAHW_PARAMETER_STREAM_CHANNELS "channels"           /* audio_channel_mask_t */
#define QAHW_PARAMETER_STREAM_FRAME_COUNT "frame_count"     /* size_t */
#define QAHW_PARAMETER_STREAM_INPUT_SOURCE "input_source"   /* audio_source_t */
#define QAHW_PARAMETER_STREAM_SAMPLING_RATE "sampling_rate" /* uint32_t */

#define QAHW_PARAMETER_DEVICE_CONNECT "connect"            /* audio_devices_t */
#define QAHW_PARAMETER_DEVICE_DISCONNECT "disconnect"      /* audio_devices_t */

/* Query supported formats. The response is a '|' separated list of strings from
 * audio_format_t enum e.g: "sup_formats=AUDIO_FORMAT_PCM_16_BIT" */
#define QAHW_PARAMETER_STREAM_SUP_FORMATS "sup_formats"

/* Query supported channel masks. The response is a '|' separated list of
 * strings from audio_channel_mask_t enum
 * e.g: "sup_channels=AUDIO_CHANNEL_OUT_STEREO|AUDIO_CHANNEL_OUT_MONO" */
#define QAHW_PARAMETER_STREAM_SUP_CHANNELS "sup_channels"

/* Query supported sampling rates. The response is a '|' separated list of
 * integer values e.g: "sup_sampling_rates=44100|48000" */
#define QAHW_PARAMETER_STREAM_SUP_SAMPLING_RATES "sup_sampling_rates"

/* Set the HW synchronization source for an output stream. */
#define QAHW_PARAMETER_STREAM_HW_AV_SYNC "hw_av_sync"

/* Enable mono audio playback if 1, else should be 0. */
#define QAHW_PARAMETER_MONO_OUTPUT "mono_output"

/**
 * audio codec parameters
 */

#define QAHW_OFFLOAD_CODEC_PARAMS           "music_offload_codec_param"
#define QAHW_OFFLOAD_CODEC_BIT_PER_SAMPLE   "music_offload_bit_per_sample"
#define QAHW_OFFLOAD_CODEC_BIT_RATE         "music_offload_bit_rate"
#define QAHW_OFFLOAD_CODEC_AVG_BIT_RATE     "music_offload_avg_bit_rate"
#define QAHW_OFFLOAD_CODEC_ID               "music_offload_codec_id"
#define QAHW_OFFLOAD_CODEC_BLOCK_ALIGN      "music_offload_block_align"
#define QAHW_OFFLOAD_CODEC_SAMPLE_RATE      "music_offload_sample_rate"
#define QAHW_OFFLOAD_CODEC_ENCODE_OPTION    "music_offload_encode_option"
#define QAHW_OFFLOAD_CODEC_NUM_CHANNEL      "music_offload_num_channels"
#define QAHW_OFFLOAD_CODEC_DOWN_SAMPLING    "music_offload_down_sampling"
#define QAHW_OFFLOAD_CODEC_DELAY_SAMPLES    "delay_samples"
#define QAHW_OFFLOAD_CODEC_PADDING_SAMPLES  "padding_samples"

/**
 * extended audio codec parameters
 */

#define QAHW_OFFLOAD_CODEC_WMA_FORMAT_TAG "music_offload_wma_format_tag"
#define QAHW_OFFLOAD_CODEC_WMA_BLOCK_ALIGN "music_offload_wma_block_align"
#define QAHW_OFFLOAD_CODEC_WMA_BIT_PER_SAMPLE "music_offload_wma_bit_per_sample"
#define QAHW_OFFLOAD_CODEC_WMA_CHANNEL_MASK "music_offload_wma_channel_mask"
#define QAHW_OFFLOAD_CODEC_WMA_ENCODE_OPTION "music_offload_wma_encode_option"
#define QAHW_OFFLOAD_CODEC_WMA_ENCODE_OPTION1 "music_offload_wma_encode_option1"
#define QAHW_OFFLOAD_CODEC_WMA_ENCODE_OPTION2 "music_offload_wma_encode_option2"

#define QAHW_OFFLOAD_CODEC_FLAC_MIN_BLK_SIZE "music_offload_flac_min_blk_size"
#define QAHW_OFFLOAD_CODEC_FLAC_MAX_BLK_SIZE "music_offload_flac_max_blk_size"
#define QAHW_OFFLOAD_CODEC_FLAC_MIN_FRAME_SIZE "music_offload_flac_min_frame_size"
#define QAHW_OFFLOAD_CODEC_FLAC_MAX_FRAME_SIZE "music_offload_flac_max_frame_size"

#define QAHW_OFFLOAD_CODEC_ALAC_FRAME_LENGTH "music_offload_alac_frame_length"
#define QAHW_OFFLOAD_CODEC_ALAC_COMPATIBLE_VERSION "music_offload_alac_compatible_version"
#define QAHW_OFFLOAD_CODEC_ALAC_BIT_DEPTH "music_offload_alac_bit_depth"
#define QAHW_OFFLOAD_CODEC_ALAC_PB "music_offload_alac_pb"
#define QAHW_OFFLOAD_CODEC_ALAC_MB "music_offload_alac_mb"
#define QAHW_OFFLOAD_CODEC_ALAC_KB "music_offload_alac_kb"
#define QAHW_OFFLOAD_CODEC_ALAC_NUM_CHANNELS "music_offload_alac_num_channels"
#define QAHW_OFFLOAD_CODEC_ALAC_MAX_RUN "music_offload_alac_max_run"
#define QAHW_OFFLOAD_CODEC_ALAC_MAX_FRAME_BYTES "music_offload_alac_max_frame_bytes"
#define QAHW_OFFLOAD_CODEC_ALAC_AVG_BIT_RATE "music_offload_alac_avg_bit_rate"
#define QAHW_OFFLOAD_CODEC_ALAC_SAMPLING_RATE "music_offload_alac_sampling_rate"
#define QAHW_OFFLOAD_CODEC_ALAC_CHANNEL_LAYOUT_TAG "music_offload_alac_channel_layout_tag"

#define QAHW_OFFLOAD_CODEC_APE_COMPATIBLE_VERSION "music_offload_ape_compatible_version"
#define QAHW_OFFLOAD_CODEC_APE_COMPRESSION_LEVEL "music_offload_ape_compression_level"
#define QAHW_OFFLOAD_CODEC_APE_FORMAT_FLAGS "music_offload_ape_format_flags"
#define QAHW_OFFLOAD_CODEC_APE_BLOCKS_PER_FRAME "music_offload_ape_blocks_per_frame"
#define QAHW_OFFLOAD_CODEC_APE_FINAL_FRAME_BLOCKS "music_offload_ape_final_frame_blocks"
#define QAHW_OFFLOAD_CODEC_APE_TOTAL_FRAMES "music_offload_ape_total_frames"
#define QAHW_OFFLOAD_CODEC_APE_BITS_PER_SAMPLE "music_offload_ape_bits_per_sample"
#define QAHW_OFFLOAD_CODEC_APE_NUM_CHANNELS "music_offload_ape_num_channels"
#define QAHW_OFFLOAD_CODEC_APE_SAMPLE_RATE "music_offload_ape_sample_rate"
#define QAHW_OFFLOAD_CODEC_APE_SEEK_TABLE_PRESENT "music_offload_seek_table_present"

#define QAHW_OFFLOAD_CODEC_VORBIS_BITSTREAM_FMT "music_offload_vorbis_bitstream_fmt"

/* Set or Query stream profile type */
#define QAHW_PARAMETER_STREAM_PROFILE "audio_stream_profile"

/* audio input flags for compress and timestamp mode.
 * check other input flags defined in audio.h for conflicts
 */
#define QAHW_INPUT_FLAG_TIMESTAMP 0x80000000
#define QAHW_INPUT_FLAG_COMPRESS  0x40000000
#define QAHW_INPUT_FLAG_PASSTHROUGH 0x20000000
#define QAHW_OUTPUT_FLAG_INCALL_MUSIC 0x80000000
#define QAHW_AUDIO_FLAG_HPCM_TX 0x00020000
#define QAHW_AUDIO_FLAG_HPCM_RX 0x00040000

/* Query fm volume */
#define QAHW_PARAMETER_KEY_FM_VOLUME "fm_volume"

/* Query if a2dp  is supported */
#define QAHW_PARAMETER_KEY_HANDLE_A2DP_DEVICE "isA2dpDeviceSupported"

#define MAX_OUT_CHANNELS 8
#define MAX_INP_CHANNELS 8

#define QAHW_PCM_CHANNEL_FL    1  /* Front left channel.                           */
#define QAHW_PCM_CHANNEL_FR    2  /* Front right channel.                          */
#define QAHW_PCM_CHANNEL_FC    3  /* Front center channel.                         */
#define QAHW_PCM_CHANNEL_LS    4  /* Left surround channel.                        */
#define QAHW_PCM_CHANNEL_RS    5  /* Right surround channel.                       */
#define QAHW_PCM_CHANNEL_LFE   6  /* Low frequency effect channel.                 */
#define QAHW_PCM_CHANNEL_CS    7  /* Center surround channel; Rear center channel. */
#define QAHW_PCM_CHANNEL_LB    8  /* Left back channel; Rear left channel.         */
#define QAHW_PCM_CHANNEL_RB    9  /* Right back channel; Rear right channel.       */
#define QAHW_PCM_CHANNEL_TS   10  /* Top surround channel.                         */
#define QAHW_PCM_CHANNEL_CVH  11  /* Center vertical height channel.               */
#define QAHW_PCM_CHANNEL_MS   12  /* Mono surround channel.                        */
#define QAHW_PCM_CHANNEL_FLC  13  /* Front left of center.                         */
#define QAHW_PCM_CHANNEL_FRC  14  /* Front right of center.                        */
#define QAHW_PCM_CHANNEL_RLC  15  /* Rear left of center.                          */
#define QAHW_PCM_CHANNEL_RRC  16  /* Rear right of center.                         */

/* type of asynchronous write callback events. Mutually exclusive */
typedef enum {
    QAHW_STREAM_CBK_EVENT_WRITE_READY, /* non blocking write completed */
    QAHW_STREAM_CBK_EVENT_DRAIN_READY,  /* drain completed */
    QAHW_STREAM_CBK_EVENT_ERROR,  /* stream hit some error */

    QAHW_STREAM_CBK_EVENT_ADSP = 0x100    /* callback event from ADSP PP,
                                           * corresponding payload will be
                                           * sent as is to the client
                                           */
} qahw_stream_callback_event_t;

typedef int qahw_stream_callback_t(qahw_stream_callback_event_t event,
                                   void *param,
                                   void *cookie);

/* type of drain requested to audio_stream_out->drain(). Mutually exclusive */
typedef enum {
    QAHW_DRAIN_ALL,            /* drain() returns when all data has been played */
    QAHW_DRAIN_EARLY_NOTIFY    /* drain() returns a short time before all data
                                  from the current track has been played to
                                  give time for gapless track switch */
} qahw_drain_type_t;

/* meta data flags */
/*TBD: Extend this based on stb requirement*/
typedef enum {
 QAHW_META_DATA_FLAGS_NONE = 0,
} qahw_meta_data_flags_t;

typedef struct {
    const void *buffer;    /* write buffer pointer */
    size_t bytes;          /* size of buffer */
    size_t offset;         /* offset in buffer from where valid byte starts */
    int64_t *timestamp;    /* timestmap */
    qahw_meta_data_flags_t flags; /* meta data flags */
    uint32_t reserved[64]; /*reserved for future */
} qahw_out_buffer_t;

typedef struct {
    void *buffer;          /* read buffer pointer */
    size_t bytes;          /* size of buffer */
    size_t offset;         /* offset in buffer from where valid byte starts */
    int64_t *timestamp;    /* timestmap */
    uint32_t reserved[64]; /*reserved for future */
} qahw_in_buffer_t;

typedef struct {
    void *buffer;    /* write buffer pointer */
    size_t size;          /* size of buffer */
    size_t offset;         /* offset in buffer from where valid byte starts */
    int64_t *timestamp;    /* timestmap */
    qahw_meta_data_flags_t flags; /* meta data flags */
} qahw_buffer_t;

#define MAX_SECTORS 8

struct qahw_source_tracking_param {
    uint8_t   vad[MAX_SECTORS];
    uint16_t  doa_speech;
    uint16_t  doa_noise[3];
    uint8_t   polar_activity[360];
} __attribute__((packed));

struct qahw_sound_focus_param {
    uint16_t  start_angle[MAX_SECTORS];
    uint8_t   enable[MAX_SECTORS];
    uint16_t  gain_step;
} __attribute__((packed));

struct aptx_dec_bt_addr {
    uint32_t nap;
    uint32_t uap;
    uint32_t lap;
};

struct qahw_aptx_dec_param {
   struct aptx_dec_bt_addr bt_addr;
};

struct qahw_avt_device_drift_param {
   /* Flag to indicate if resync is required on the client side for
    * drift correction. Flag is set to TRUE for the first get_param response
    * after device interface starts. This flag value can be used by client
    * to identify if device interface restart has happened and if any
    * re-sync is required at their end for drift correction.
    */
    uint32_t        resync_flag;
    /* Accumulated drift value in microseconds.
     * Positive drift value indicates AV timer is running faster than device.
     * Negative drift value indicates AV timer is running slower than device.
     */
    int32_t         avt_device_drift_value;
    /* 64-bit absolute timestamp of reference */
    uint64_t        ref_timer_abs_ts;
};

/*use these for setting infine window.i.e free run mode */
#define QAHW_MAX_RENDER_START_WINDOW 0x8000000000000000
#define QAHW_MAX_RENDER_END_WINDOW   0x7FFFFFFFFFFFFFFF

struct qahw_out_render_window_param {
   uint64_t        render_ws; /* render window start value microseconds*/
   uint64_t        render_we; /* render window end value microseconds*/
};

struct qahw_out_start_delay_param {
   uint64_t       start_delay; /* session start delay in microseconds*/
};

struct qahw_out_enable_drift_correction {
   bool        enable; /* enable drift correction*/
};

struct qahw_out_correct_drift {
    /*
     * adjust time in microseconds, a positive value
     * to advance the clock or a negative value to
     * delay the clock.
     */
    int64_t        adjust_time;
};

struct qahw_out_presentation_position_param {
    struct timespec timestamp;
    uint64_t frames;
    int32_t clock_id;
};

#define QAHW_MAX_ADSP_STREAM_CMD_PAYLOAD_LEN 512

typedef enum {
    QAHW_STREAM_PP_EVENT = 0,
    QAHW_STREAM_ENCDEC_EVENT = 1,
} qahw_event_id;

/* payload format for HAL parameter
 * QAHW_PARAM_ADSP_STREAM_CMD
 */
struct qahw_adsp_event {
    qahw_event_id event_type;      /* type of the event */
    uint32_t payload_length;       /* length in bytes of the payload */
    void *payload;                 /* the actual payload */
};

struct qahw_out_channel_map_param {
   uint8_t       channels;                               /* Input Channels */
   uint8_t       channel_map[AUDIO_CHANNEL_COUNT_MAX];   /* Input Channel Map */
};

struct qahw_device_cfg_param {
   uint32_t   sample_rate;
   uint32_t   channels;
   uint32_t   bit_width;
   audio_format_t format;
   audio_devices_t device;
   uint8_t    channel_map[AUDIO_CHANNEL_COUNT_MAX];
   uint16_t   channel_allocation;
};

typedef struct qahw_mix_matrix_params {
    uint16_t num_output_channels;
    uint16_t num_input_channels;
    uint8_t has_output_channel_map;
    uint16_t output_channel_map[AUDIO_CHANNEL_COUNT_MAX];
    uint8_t has_input_channel_map;
    uint16_t input_channel_map[AUDIO_CHANNEL_COUNT_MAX];
    uint8_t has_mixer_coeffs;
    float mixer_coeffs[AUDIO_CHANNEL_COUNT_MAX][AUDIO_CHANNEL_COUNT_MAX];
} qahw_mix_matrix_params_t;


#define QAHW_LICENCE_STR_MAX_LENGTH (64)
#define QAHW_PRODUCT_STR_MAX_LENGTH (64)
typedef struct qahw_license_params {
    char product[QAHW_PRODUCT_STR_MAX_LENGTH + 1];
    int key;
    char license[QAHW_LICENCE_STR_MAX_LENGTH + 1];
} qahw_license_params_t;

typedef struct qahw_dtmf_gen_params {
   bool enable;
   uint16_t low_freq;
   uint16_t high_freq;
   uint16_t gain;
} qahw_dtmf_gen_params_t;

enum {
    QAHW_TTY_MODE_OFF,
    QAHW_TTY_MODE_FULL,
    QAHW_TTY_MODE_VCO,
    QAHW_TTY_MODE_HCO,
    QAHW_TTY_MODE_MAX,
};

typedef struct qahw_tty_params {
   uint32_t mode;
} qahw_tty_params_t;

typedef enum {
    QAHW_HPCM_TAP_POINT_RX = 1,
    QAHW_HPCM_TAP_POINT_TX = 2,
    QAHW_HPCM_TAP_POINT_RX_TX = 3,
} qahw_hpcm_tap_point;

typedef enum {
    QAHW_HPCM_DIRECTION_OUT,
    QAHW_HPCM_DIRECTION_IN,
    QAHW_HPCM_DIRECTION_OUT_IN,
} qahw_hpcm_direction;

typedef struct qahw_hpcm_params {
   qahw_hpcm_tap_point tap_point;
   qahw_hpcm_direction direction;
} qahw_hpcm_params_t;

typedef union {
    struct qahw_source_tracking_param st_params;
    struct qahw_sound_focus_param sf_params;
    struct qahw_aptx_dec_param aptx_params;
    struct qahw_avt_device_drift_param drift_params;
    struct qahw_out_render_window_param render_window_params;
    struct qahw_out_start_delay_param start_delay;
    struct qahw_out_enable_drift_correction drift_enable_param;
    struct qahw_out_correct_drift drift_correction_param;
    struct qahw_adsp_event adsp_event_params;
    struct qahw_out_channel_map_param channel_map_params;
    struct qahw_device_cfg_param device_cfg_params;
    struct qahw_mix_matrix_params mix_matrix_params;
    struct qahw_license_params license_params;
    struct qahw_out_presentation_position_param pos_param;
    struct qahw_dtmf_gen_params dtmf_gen_params;
    struct qahw_tty_params tty_mode_params;
    struct qahw_hpcm_params hpcm_params;
} qahw_param_payload;

typedef enum {
    QAHW_PARAM_SOURCE_TRACK,
    QAHW_PARAM_SOUND_FOCUS,
    QAHW_PARAM_APTX_DEC,
    QAHW_PARAM_AVT_DEVICE_DRIFT,  /* PARAM to query AV timer vs device drift */
    QAHW_PARAM_OUT_RENDER_WINDOW, /* PARAM to set render window */
    QAHW_PARAM_OUT_START_DELAY, /* PARAM to set session start delay*/
    /* enable adsp drift correction this must be called before out_write */
    QAHW_PARAM_OUT_ENABLE_DRIFT_CORRECTION,
    /* param to set drift value to be adjusted by dsp */
    QAHW_PARAM_OUT_CORRECT_DRIFT,
    QAHW_PARAM_ADSP_STREAM_CMD,
    QAHW_PARAM_OUT_CHANNEL_MAP,    /* PARAM to set i/p channel map */
    QAHW_PARAM_DEVICE_CONFIG,      /* PARAM to set device config */
    QAHW_PARAM_OUT_MIX_MATRIX_PARAMS,
    QAHW_PARAM_CH_MIX_MATRIX_PARAMS,
    QAHW_PARAM_LICENSE_PARAMS,
    QAHW_PARAM_OUT_PRESENTATION_POSITION,
    QAHW_PARAM_DTMF_GEN,
    QAHW_PARAM_TTY_MODE,
    QAHW_PARAM_HPCM,
} qahw_param_id;


typedef union {
    struct qahw_out_render_window_param render_window_params;
} qahw_loopback_param_payload;

typedef enum {
    QAHW_PARAM_LOOPBACK_RENDER_WINDOW /* PARAM to set render window */
} qahw_loopback_param_id;

/** stream direction enumeration */
typedef enum {
    QAHW_STREAM_INPUT,
    QAHW_STREAM_OUTPUT,
    QAHW_STREAM_INPUT_OUTPUT,
} qahw_stream_direction;

/** stream types */
typedef enum {
    QAHW_STREAM_TYPE_INVALID,
    QAHW_AUDIO_PLAYBACK_LOW_LATENCY,      /**< low latency, higher power*/
    QAHW_AUDIO_PLAYBACK_DEEP_BUFFER,          /**< low power, higher latency*/
    QAHW_AUDIO_PLAYBACK_COMPRESSED,           /**< compresssed audio*/
    QAHW_AUDIO_PLAYBACK_VOIP,                 /**< pcm voip audio*/
    QAHW_AUDIO_PLAYBACK_VOICE_CALL_MUSIC,     /**< pcm voip audio*/

    QAHW_AUDIO_CAPTURE_LOW_LATENCY,           /**< low latency, higher power*/
    QAHW_AUDIO_CAPTURE_DEEP_BUFFER,           /**< low power, higher latency*/
    QAHW_AUDIO_CAPTURE_COMPRESSED,            /**< compresssed audio*/
    QAHW_AUDIO_CAPTURE_RAW,                   /**< pcm no post processing*/
    QAHW_AUDIO_CAPTURE_VOIP,                  /**< pcm voip audio*/
    QAHW_AUDIO_CAPTURE_VOICE_ACTIVATION,      /**< voice activation*/
    QAHW_AUDIO_CAPTURE_VOICE_CALL_RX,         /**< incall record, downlink */
    QAHW_AUDIO_CAPTURE_VOICE_CALL_TX,         /**< incall record, uplink */
    QAHW_AUDIO_CAPTURE_VOICE_CALL_RX_TX,      /**< incall record, uplink & Downlink */

    QAHW_VOICE_CALL,                          /**< voice call */

    QAHW_AUDIO_TRANSCODE,                     /**< audio transcode */
    QAHW_AUDIO_HOST_PCM_TX,
    QAHW_AUDIO_HOST_PCM_RX,
    QAHW_AUDIO_HOST_PCM_TX_RX,
    QAHW_AUDIO_STREAM_TYPE_MAX,
} qahw_audio_stream_type;

typedef uint32_t qahw_device_t;

/**< Key value pair to identify the topology of a usecase from default  */
struct qahw_modifier_kv  {
    uint32_t key;
    uint32_t value;
};

struct qahw_shared_attributes{
     audio_config_t config;
};
struct qahw_voice_attributes{
     audio_config_t config;
     const char *vsid;
};

struct qahw_audio_attributes{
    audio_config_t config;
};

typedef union {
    struct qahw_shared_attributes shared;
    struct qahw_voice_attributes voice;
    struct qahw_audio_attributes audio;
} qahw_stream_attributes_config;

struct qahw_stream_attributes {
     qahw_audio_stream_type type;
     qahw_stream_direction direction;
     qahw_stream_attributes_config attr;
};

typedef enum {
    QAHW_CHANNEL_L = 0, /*left channel*/
    QAHW_CHANNEL_R = 1, /*right channel*/
    QAHW_CHANNELS_MAX = 2, /*max number of supported streams*/
} qahw_channel_map;

struct qahw_channel_vol {
    qahw_channel_map channel;
    float vol;
};

struct qahw_volume_data {
    uint32_t num_of_channels;
    struct qahw_channel_vol *vol_pair;
};

struct qahw_mute_data {
    bool enable;
    qahw_stream_direction direction;
};

__END_DECLS

#endif  // QTI_AUDIO_HAL_DEFS_H

