/*
* Copyright (c) 2015-2020, The Linux Foundation. All rights reserved.
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
#define LOG_TAG "a2dp_offload"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0
#include <errno.h>
#include <log/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include "audio_extn.h"
#include <stdlib.h>
#include <cutils/str_parms.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>
#include <cutils/properties.h>

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_A2DP
#include <log_utils.h>
#endif

#define BT_IPC_SOURCE_LIB_NAME "btaudio_offload_if.so"
#define BT_IPC_SINK_LIB_NAME    "libbthost_if_sink.so"
#define MEDIA_FMT_NONE                                     0
#define MEDIA_FMT_AAC                                      0x00010DA6
#define MEDIA_FMT_APTX                                     0x000131ff
#define MEDIA_FMT_APTX_HD                                  0x00013200
#define MEDIA_FMT_APTX_AD                                  0x00013204
#define MEDIA_FMT_SBC                                      0x00010BF2
#define MEDIA_FMT_CELT                                     0x00013221
#define MEDIA_FMT_LDAC                                     0x00013224
#define MEDIA_FMT_MP3                                      0x00010BE9
#define MEDIA_FMT_APTX_ADAPTIVE                            0x00013204
#define MEDIA_FMT_APTX_AD_SPEECH                           0x00013208
#define MEDIA_FMT_AAC_AOT_LC                               2
#define MEDIA_FMT_AAC_AOT_SBR                              5
#define MEDIA_FMT_AAC_AOT_PS                               29
#define PCM_CHANNEL_L                                      1
#define PCM_CHANNEL_R                                      2
#define PCM_CHANNEL_C                                      3
#define MEDIA_FMT_SBC_CHANNEL_MODE_MONO                    1
#define MEDIA_FMT_SBC_CHANNEL_MODE_STEREO                  2
#define MEDIA_FMT_SBC_CHANNEL_MODE_DUAL_MONO               8
#define MEDIA_FMT_SBC_CHANNEL_MODE_JOINT_STEREO            9
#define MEDIA_FMT_SBC_ALLOCATION_METHOD_LOUDNESS           0
#define MEDIA_FMT_SBC_ALLOCATION_METHOD_SNR                1
#define MIXER_ENC_CONFIG_BLOCK            "SLIM_7_RX Encoder Config"
#define MIXER_ENC_APTX_AD_CONFIG_BLOCK    "SLIM_7_RX APTX_AD Enc Cfg"
#define MIXER_SOURCE_DEC_CONFIG_BLOCK     "SLIM_7_TX Decoder Config"
#define MIXER_SINK_DEC_CONFIG_BLOCK       "SLIM_9_TX Decoder Config"
#define MIXER_ENC_BIT_FORMAT       "AFE Input Bit Format"
#define MIXER_DEC_BIT_FORMAT       "AFE Output Bit Format"
#define MIXER_SCRAMBLER_MODE       "AFE Scrambler Mode"
#define MIXER_SAMPLE_RATE_RX       "BT SampleRate RX"
#define MIXER_SOURCE_SAMPLE_RATE_TX       "BT SampleRate TX"
#define MIXER_SAMPLE_RATE_DEFAULT  "BT SampleRate"
#define MIXER_AFE_IN_CHANNELS      "AFE Input Channels"
#define MIXER_ABR_TX_FEEDBACK_PATH "A2DP_SLIM7_UL_HL Switch"
#define MIXER_ABR_RX_FEEDBACK_PATH "SCO_SLIM7_DL_HL Switch"
#define MIXER_SET_FEEDBACK_CHANNEL "BT set feedback channel"
#define MIXER_SINK_SAMPLE_RATE     "BT_TX SampleRate"
#define MIXER_AFE_SINK_CHANNELS    "AFE Output Channels"
#define MIXER_FMT_TWS_CHANNEL_MODE "TWS Channel Mode"
#define ENCODER_LATENCY_SBC        10
#define ENCODER_LATENCY_APTX       40
#define ENCODER_LATENCY_APTX_HD    20
#define ENCODER_LATENCY_AAC        70
//To Do: Fine Tune Encoder CELT/LDAC latency.
#define ENCODER_LATENCY_CELT       40
#define ENCODER_LATENCY_LDAC       40
#define ENCODER_LATENCY_PCM        50
#define DEFAULT_SINK_LATENCY_SBC       140
#define DEFAULT_SINK_LATENCY_APTX      160
#define DEFAULT_SINK_LATENCY_APTX_HD   180
#define DEFAULT_SINK_LATENCY_AAC       180
//To Do: Fine Tune Default CELT/LDAC Latency.
#define DEFAULT_SINK_LATENCY_CELT      180
#define DEFAULT_SINK_LATENCY_LDAC      180
#define DEFAULT_SINK_LATENCY_PCM       140

#define SYSPROP_A2DP_OFFLOAD_SUPPORTED "ro.bluetooth.a2dp_offload.supported"
#define SYSPROP_A2DP_OFFLOAD_DISABLED  "persist.bluetooth.a2dp_offload.disabled"
#define SYSPROP_A2DP_CODEC_LATENCIES   "vendor.audio.a2dp.codec.latency"

// Default encoder bit width
#define DEFAULT_ENCODER_BIT_FORMAT 16

// Default encoder latency
#define DEFAULT_ENCODER_LATENCY    200

// Slimbus Tx sample rate for ABR feedback channel
#define ABR_TX_SAMPLE_RATE             "KHZ_8"

// Slimbus Tx sample rate for APTX AD SPEECH
#define SPEECH_TX_SAMPLE_RATE             "KHZ_96"

// Purpose ID for Inter Module Communication (IMC) in AFE
#define IMC_PURPOSE_ID_BT_INFO         0x000132E2

// Maximum quality levels for ABR
#define MAX_ABR_QUALITY_LEVELS             5

// Instance identifier for A2DP
#define MAX_INSTANCE_ID                (UINT32_MAX / 2)

// Instance identifier for SWB
#define APTX_AD_SPEECH_INSTANCE_ID                 37

#define SAMPLING_RATE_96K               96000
#define SAMPLING_RATE_48K               48000
#define SAMPLING_RATE_441K              44100
#define SAMPLING_RATE_32K               32000
#define CH_STEREO                       2
#define CH_MONO                         1
#define SOURCE 0
#define SINK   1
#define UNINITIALIZED -1

#ifdef __LP64__
#define VNDK_FWK_LIB_PATH "/vendor/lib64/libqti_vndfwk_detect.so"
#else
#define VNDK_FWK_LIB_PATH "/vendor/lib/libqti_vndfwk_detect.so"
#endif

static void *vndk_fwk_lib_handle = NULL;
static int is_running_with_enhanced_fwk = UNINITIALIZED;

typedef int (*vndk_fwk_isVendorEnhancedFwk_t)();
static vndk_fwk_isVendorEnhancedFwk_t vndk_fwk_isVendorEnhancedFwk;

/*
 * Below enum values are extended from audio_base.h to
 * to keep encoder and decoder type local to bthost_ipc
 * and audio_hal as these are intended only for handshake
 * between IPC lib and Audio HAL.
 */
typedef enum {
    CODEC_TYPE_INVALID = AUDIO_FORMAT_INVALID, // 0xFFFFFFFFUL
    CODEC_TYPE_AAC = AUDIO_FORMAT_AAC, // 0x04000000UL
    CODEC_TYPE_SBC = AUDIO_FORMAT_SBC, // 0x1F000000UL
    CODEC_TYPE_APTX = AUDIO_FORMAT_APTX, // 0x20000000UL
    CODEC_TYPE_APTX_HD = AUDIO_FORMAT_APTX_HD, // 0x21000000UL
#ifndef LINUX_ENABLED
    CODEC_TYPE_APTX_DUAL_MONO = 570425344u, // 0x22000000UL
#endif
    CODEC_TYPE_LDAC = AUDIO_FORMAT_LDAC, // 0x23000000UL
    CODEC_TYPE_CELT = 603979776u, // 0x24000000UL
    CODEC_TYPE_APTX_AD = 620756992u, // 0x25000000UL
    CODEC_TYPE_APTX_AD_SPEECH = 637534208u, //0x26000000UL
    CODEC_TYPE_PCM = AUDIO_FORMAT_PCM_16_BIT, // 0x1u
}codec_t;

/*
 * enums which describes the APTX Adaptive
 * channel mode, these values are used by encoder
 */
 typedef enum {
    APTX_AD_CHANNEL_UNCHANGED = -1,
    APTX_AD_CHANNEL_JOINT_STEREO = 0, // default
    APTX_AD_CHANNEL_MONO = 1,
    APTX_AD_CHANNEL_DUAL_MONO = 2,
    APTX_AD_CHANNEL_STEREO_TWS = 4,
    APTX_AD_CHANNEL_EARBUD = 8,
} enc_aptx_ad_channel_mode;

/*
 * enums which describes the APTX Adaptive
 * sampling frequency, these values are used
 * by encoder
 */
typedef enum {
    APTX_AD_SR_UNCHANGED = 0x0,
    APTX_AD_48 = 0x1,  // 48 KHz default
    APTX_AD_44_1 = 0x2, // 44.1kHz
    APTX_AD_96 = 0x3,  // 96KHz
} enc_aptx_ad_s_rate;

typedef void (*bt_audio_pre_init_t)(void);
typedef int (*audio_source_open_t)(void);
typedef int (*audio_source_close_t)(void);
typedef int (*audio_source_start_t)(void);
typedef int (*audio_source_stop_t)(void);
typedef int (*audio_source_suspend_t)(void);
typedef void (*audio_source_handoff_triggered_t)(void);
typedef void (*clear_source_a2dpsuspend_flag_t)(void);
typedef void * (*audio_get_enc_config_t)(uint8_t *multicast_status,
                                uint8_t *num_dev, codec_t *codec_type);
typedef int (*audio_source_check_a2dp_ready_t)(void);
typedef int (*audio_is_source_scrambling_enabled_t)(void);
typedef bool (*audio_is_tws_mono_mode_enable_t)(void);
typedef int (*audio_sink_start_t)(void);
typedef int (*audio_sink_stop_t)(void);
typedef void * (*audio_get_dec_config_t)(codec_t *codec_type);
typedef void * (*audio_sink_session_setup_complete_t)(uint64_t system_latency);
typedef int (*audio_sink_check_a2dp_ready_t)(void);
typedef uint16_t (*audio_sink_get_a2dp_latency_t)(void);

enum A2DP_STATE {
    A2DP_STATE_CONNECTED,
    A2DP_STATE_STARTED,
    A2DP_STATE_STOPPED,
    A2DP_STATE_DISCONNECTED,
};

typedef enum {
    IMC_TRANSMIT,
    IMC_RECEIVE,
} imc_direction_t;

typedef enum {
    IMC_DISABLE,
    IMC_ENABLE,
} imc_status_t;

typedef enum {
    SWAP_DISABLE,
    SWAP_ENABLE,
} swap_status_t;

typedef enum {
    MTU_SIZE,
    PEAK_BIT_RATE,
    BIT_RATE_MODE,
} frame_control_type_t;

// --- external function dependency ---
fp_platform_get_pcm_device_id_t fp_platform_get_pcm_device_id;
fp_check_a2dp_restore_t fp_check_a2dp_restore_l;

/* PCM config for ABR Feedback hostless front end */
static struct pcm_config pcm_config_abr = {
    .channels = 1,
    .rate = 8000,
    .period_size = 240,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

/* Adaptive bitrate config for A2DP codecs */
struct a2dp_abr_config {
    /* Flag to denote whether Adaptive bitrate is enabled for codec */
    bool is_abr_enabled;
    /* Flag to denote whether front end has been opened for ABR */
    bool abr_started;
    /* ABR Tx path pcm handle */
    struct pcm *abr_tx_handle;
    /* ABR Rx path pcm handle */
    struct pcm *abr_rx_handle;
    /* ABR Inter Module Communication (IMC) instance ID */
    uint32_t imc_instance;
};

static uint32_t instance_id = MAX_INSTANCE_ID;

/* structure used to  update a2dp state machine
 * to communicate IPC library
 * to store DSP encoder configuration information
 */
struct a2dp_data {
    struct audio_device *adev;
    void *bt_lib_source_handle;
    bt_audio_pre_init_t bt_audio_pre_init;
    audio_source_open_t audio_source_open;
    audio_source_close_t audio_source_close;
    audio_source_start_t audio_source_start;
    audio_source_stop_t audio_source_stop;
    audio_source_suspend_t audio_source_suspend;
    audio_source_handoff_triggered_t audio_source_handoff_triggered;
    clear_source_a2dpsuspend_flag_t clear_source_a2dpsuspend_flag;
    audio_get_enc_config_t audio_get_enc_config;
    audio_source_check_a2dp_ready_t audio_source_check_a2dp_ready;
    audio_is_tws_mono_mode_enable_t audio_is_tws_mono_mode_enable;
    audio_is_source_scrambling_enabled_t audio_is_source_scrambling_enabled;
    enum A2DP_STATE bt_state_source;
    codec_t bt_encoder_format;
    uint32_t enc_sampling_rate;
    uint32_t enc_channels;
    bool a2dp_source_started;
    bool a2dp_source_suspended;
    int  a2dp_source_total_active_session_requests;
    bool is_a2dp_offload_supported;
    bool is_handoff_in_progress;
    bool is_aptx_dual_mono_supported;
    /* Mono Mode support for TWS+ */
    bool is_tws_mono_mode_on;
    bool is_aptx_adaptive;
    /* Adaptive bitrate config for A2DP codecs */
    struct a2dp_abr_config abr_config;

    void *bt_lib_sink_handle;
    audio_sink_start_t audio_sink_start;
    audio_sink_stop_t audio_sink_stop;
    audio_get_dec_config_t audio_get_dec_config;
    audio_sink_session_setup_complete_t audio_sink_session_setup_complete;
    audio_sink_check_a2dp_ready_t audio_sink_check_a2dp_ready;
    audio_sink_get_a2dp_latency_t audio_sink_get_a2dp_latency;
    enum A2DP_STATE bt_state_sink;
    codec_t bt_decoder_format;
    uint32_t dec_sampling_rate;
    uint32_t dec_channels;
    bool a2dp_sink_started;
    int  a2dp_sink_total_active_session_requests;
    bool swb_configured;
};

struct a2dp_data a2dp;

/* Adaptive bitrate (ABR) is supported by certain Bluetooth codecs.
 * Structures sent to configure DSP for ABR are defined below.
 * This data helps DSP configure feedback path (BTSoC to LPASS)
 * for link quality levels and mapping quality levels to codec
 * specific bitrate.
 */

/* Key value pair for link quality level to bitrate mapping. */
struct bit_rate_level_map_t {
    uint32_t link_quality_level;
    uint32_t bitrate;
};

/* Link quality level to bitrate mapping info sent to DSP. */
struct quality_level_to_bitrate_info {
    /* Number of quality levels being mapped.
     * This will be equal to the size of mapping table.
     */
    uint32_t num_levels;
    /* Quality level to bitrate mapping table */
    struct bit_rate_level_map_t bit_rate_level_map[MAX_ABR_QUALITY_LEVELS];
};

/* Structure to set up Inter Module Communication (IMC) between
 * AFE Decoder and Encoder.
 */
struct imc_dec_enc_info {
    /* Decoder to encoder communication direction.
     * Transmit = 0 / Receive = 1
     */
    uint32_t direction;
    /* Enable / disable IMC between decoder and encoder */
    uint32_t enable;
    /* Purpose of IMC being set up between decoder and encoder.
     * IMC_PURPOSE_ID_BT_INFO defined for link quality feedback
     * is the default value to be sent as purpose.
     */
    uint32_t purpose;
    /* Unique communication instance ID.
     * purpose and comm_instance together form the actual key
     * used in IMC registration, which must be the same for
     * encoder and decoder for which IMC is being set up.
     */
    uint32_t comm_instance;
};

/* Structure to control frame size of AAC encoded frames. */
struct aac_frame_size_control_t {
    /* Type of frame size control: MTU_SIZE / PEAK_BIT_RATE*/
    uint32_t ctl_type;
    /* Control value
     * MTU_SIZE: MTU size in bytes
     * PEAK_BIT_RATE: Peak bitrate in bits per second.
     * BIT_RATE_MODE: Variable bitrate
     */
    uint32_t ctl_value;
};

/* Structure used for ABR config of AFE encoder and decoder. */
struct abr_enc_cfg_t {
    /* Link quality level to bitrate mapping info sent to DSP. */
    struct quality_level_to_bitrate_info mapping_info;
    /* Information to set up IMC between decoder and encoder */
    struct imc_dec_enc_info imc_info;
    /* Flag to indicate whether ABR is enabled */
    bool is_abr_enabled;
}  __attribute__ ((packed));

/* Structure to send configuration for decoder introduced
 * on AFE Tx path for ABR link quality feedback to BT encoder.
 */
struct abr_dec_cfg_t {
    /* Decoder media format */
    uint32_t dec_format;
    /* Information to set up IMC between decoder and encoder */
    struct imc_dec_enc_info imc_info;
} __attribute__ ((packed));

struct aptx_ad_speech_mode_cfg_t
{
    uint32_t mode;
    uint32_t swapping;
} __attribute__ ((packed));

/* Structure for SWB voice dec config */
struct aptx_ad_speech_dec_cfg_t {
    struct abr_dec_cfg_t abr_cfg;
    struct aptx_ad_speech_mode_cfg_t speech_mode;
} __attribute__ ((packed));

/* START of DSP configurable structures
 * These values should match with DSP interface defintion
 */

/* AAC encoder configuration structure. */
typedef struct aac_enc_cfg_t aac_enc_cfg_t;

/* supported enc_mode are AAC_LC, AAC_SBR, AAC_PS
 * supported aac_fmt_flag are ADTS/RAW
 * supported channel_cfg are Native mode, Mono , Stereo
 */
struct aac_enc_cfg_t {
    uint32_t      enc_format;
    uint32_t      bit_rate;
    uint32_t      enc_mode;
    uint16_t      aac_fmt_flag;
    uint16_t      channel_cfg;
    uint32_t      sample_rate;
} __attribute__ ((packed));

struct aac_enc_cfg_v2_t {
    struct aac_enc_cfg_t aac_enc_cfg;
    struct aac_frame_size_control_t frame_ctl;
} __attribute__ ((packed));

struct aac_enc_cfg_v3_t {
    struct aac_enc_cfg_t aac_enc_cfg;
    struct aac_frame_size_control_t frame_ctl;
    struct aac_frame_size_control_t aac_key_value_ctl;
} __attribute__ ((packed));

typedef struct audio_aac_decoder_config_t audio_aac_decoder_config_t;
struct audio_aac_decoder_config_t {
    uint16_t      aac_fmt_flag; /* LATM*/
    uint16_t      audio_object_type; /* LC */
    uint16_t      channels; /* Stereo */
    uint16_t      total_size_of_pce_bits; /* 0 - only for channel conf PCE */
    uint32_t      sampling_rate; /* 8k, 11.025k, 12k, 16k, 22.05k, 24k, 32k,
                                  44.1k, 48k, 64k, 88.2k, 96k */
} __attribute__ ((packed));

typedef struct audio_sbc_decoder_config_t audio_sbc_decoder_config_t;
struct audio_sbc_decoder_config_t {
    uint16_t      channels; /* Mono, Stereo */
    uint32_t      sampling_rate; /* 8k, 11.025k, 12k, 16k, 22.05k, 24k, 32k,
                                  44.1k, 48k, 64k, 88.2k, 96k */
} __attribute__ ((packed));

/* AAC decoder configuration structure. */
typedef struct aac_dec_cfg_t aac_dec_cfg_t;
struct aac_dec_cfg_t {
    uint32_t dec_format;
    audio_aac_decoder_config_t data;
} __attribute__ ((packed));

/* SBC decoder configuration structure. */
typedef struct sbc_dec_cfg_t sbc_dec_cfg_t;
struct sbc_dec_cfg_t {
    uint32_t dec_format;
    audio_sbc_decoder_config_t data;
} __attribute__ ((packed));

/* SBC encoder configuration structure. */
typedef struct sbc_enc_cfg_t sbc_enc_cfg_t;

/* supported num_subbands are 4/8
 * supported blk_len are 4, 8, 12, 16
 * supported channel_mode are MONO, STEREO, DUAL_MONO, JOINT_STEREO
 * supported alloc_method are LOUNDNESS/SNR
 * supported bit_rate for mono channel is max 320kbps
 * supported bit rate for stereo channel is max 512 kbps
 */
struct sbc_enc_cfg_t{
    uint32_t      enc_format;
    uint32_t      num_subbands;
    uint32_t      blk_len;
    uint32_t      channel_mode;
    uint32_t      alloc_method;
    uint32_t      bit_rate;
    uint32_t      sample_rate;
} __attribute__ ((packed));


/* supported num_channels are Mono/Stereo
 * supported channel_mapping for mono is CHANNEL_C
 * supported channel mapping for stereo is CHANNEL_L and CHANNEL_R
 * custom size and reserved are not used(for future enhancement)
 */
struct custom_enc_cfg_t
{
    uint32_t      enc_format;
    uint32_t      sample_rate;
    uint16_t      num_channels;
    uint16_t      reserved;
    uint8_t       channel_mapping[8];
    uint32_t      custom_size;
} __attribute__ ((packed));

struct celt_specific_enc_cfg_t
{
    uint32_t      bit_rate;
    uint16_t      frame_size;
    uint16_t      complexity;
    uint16_t      prediction_mode;
    uint16_t      vbr_flag;
} __attribute__ ((packed));

struct celt_enc_cfg_t
{
    struct custom_enc_cfg_t  custom_cfg;
    struct celt_specific_enc_cfg_t celt_cfg;
} __attribute__ ((packed));

/* sync_mode introduced with APTX V2 libraries
 * sync mode: 0x0 = stereo sync mode
 *            0x01 = dual mono sync mode
 *            0x02 = dual mono with no sync on either L or R codewords
 */
struct aptx_v2_enc_cfg_ext_t
{
    uint32_t       sync_mode;
} __attribute__ ((packed));

/* APTX struct for combining custom enc and V2 fields */
struct aptx_enc_cfg_t
{
    struct custom_enc_cfg_t  custom_cfg;
    struct aptx_v2_enc_cfg_ext_t aptx_v2_cfg;
} __attribute__ ((packed));

/* APTX AD structure */
struct aptx_ad_enc_cfg_ext_t
{
    uint32_t  sampling_freq;
    uint32_t  mtu;
    uint32_t  channel_mode;
    uint32_t  min_sink_modeA;
    uint32_t  max_sink_modeA;
    uint32_t  min_sink_modeB;
    uint32_t  max_sink_modeB;
    uint32_t  min_sink_modeC;
    uint32_t  max_sink_modeC;
    uint32_t  mode;
} __attribute__ ((packed));

struct aptx_ad_enc_cfg_t
{
    struct custom_enc_cfg_t  custom_cfg;
    struct aptx_ad_enc_cfg_ext_t aptx_ad_cfg;
    struct abr_enc_cfg_t abr_cfg;
} __attribute__ ((packed));

struct aptx_ad_enc_cfg_ext_r2_t
{
    uint32_t  sampling_freq;
    uint32_t  mtu;
    uint32_t  channel_mode;
    uint32_t  min_sink_modeA;
    uint32_t  max_sink_modeA;
    uint32_t  min_sink_modeB;
    uint32_t  max_sink_modeB;
    uint32_t  min_sink_modeC;
    uint32_t  max_sink_modeC;
    uint32_t  mode;
    uint32_t  input_mode;
    uint32_t  fade_duration;
    uint8_t   sink_cap[11];
} __attribute__ ((packed));

struct aptx_ad_enc_cfg_r2_t
{
    struct custom_enc_cfg_t  custom_cfg;
    struct aptx_ad_enc_cfg_ext_r2_t aptx_ad_cfg;
    struct abr_enc_cfg_t abr_cfg;
} __attribute__ ((packed));

/* APTX AD SPEECH structure */
struct aptx_ad_speech_enc_cfg_t
{
    struct custom_enc_cfg_t  custom_cfg;
    /* Information to set up IMC between decoder and encoder */
    struct imc_dec_enc_info imc_info;
    struct aptx_ad_speech_mode_cfg_t speech_mode;
} __attribute__ ((packed));

struct ldac_specific_enc_cfg_t
{
    uint32_t      bit_rate;
    uint16_t      channel_mode;
    uint16_t      mtu;
} __attribute__ ((packed));

struct ldac_enc_cfg_t
{
    struct custom_enc_cfg_t  custom_cfg;
    struct ldac_specific_enc_cfg_t ldac_cfg;
    struct abr_enc_cfg_t abr_cfg;
} __attribute__ ((packed));

/* In LE BT source code uses system/audio.h for below
 * structure definition. To avoid multiple definition
 * compilation error for audiohal in LE , masking structure
 * definition under "LINUX_ENABLED" which is defined only
 * in LE
 */
#ifndef LINUX_ENABLED
/* TODO: Define the following structures only for O using PLATFORM_VERSION */
/* Information about BT SBC encoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP encoder
 */
typedef struct {
    uint32_t subband;        /* 4, 8 */
    uint32_t blk_len;        /* 4, 8, 12, 16 */
    uint16_t sampling_rate;  /*44.1khz,48khz*/
    uint8_t  channels;       /*0(Mono),1(Dual_mono),2(Stereo),3(JS)*/
    uint8_t  alloc;          /*0(Loudness),1(SNR)*/
    uint8_t  min_bitpool;    /* 2 */
    uint8_t  max_bitpool;    /*53(44.1khz),51 (48khz) */
    uint32_t bitrate;        /* 320kbps to 512kbps */
    uint32_t bits_per_sample;
} audio_sbc_encoder_config;

/* Information about BT APTX encoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP encoder
 */
typedef struct {
    uint16_t sampling_rate;
    uint8_t  channels;
    uint32_t bitrate;
    uint32_t bits_per_sample;
} audio_aptx_default_config;

typedef struct {
    uint32_t sampling_rate;
    uint32_t mtu;
    int32_t  channel_mode;
    uint32_t min_sink_modeA;
    uint32_t max_sink_modeA;
    uint32_t min_sink_modeB;
    uint32_t max_sink_modeB;
    uint32_t min_sink_modeC;
    uint32_t max_sink_modeC;
    uint32_t encoder_mode;
    uint8_t  TTP_modeA_low;
    uint8_t  TTP_modeA_high;
    uint8_t  TTP_modeB_low;
    uint8_t  TTP_modeB_high;
    uint8_t  TTP_TWS_low;
    uint8_t  TTP_TWS_high;
    uint32_t bits_per_sample;
    uint32_t input_mode;
    uint32_t fade_duration;
    uint8_t  sink_cap[11];
} audio_aptx_ad_config;

typedef struct {
    uint16_t sampling_rate;
    uint8_t  channels;
    uint32_t bitrate;
    uint32_t sync_mode;
    uint32_t bits_per_sample;
} audio_aptx_dual_mono_config;

typedef union {
    audio_aptx_default_config *default_cfg;
    audio_aptx_dual_mono_config *dual_mono_cfg;
    audio_aptx_ad_config *ad_cfg;
} audio_aptx_encoder_config;

/* Information about BT AAC encoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP encoder
 */
typedef struct {
    uint32_t enc_mode; /* LC, SBR, PS */
    uint16_t format_flag; /* RAW, ADTS */
    uint16_t channels; /* 1-Mono, 2-Stereo */
    uint32_t sampling_rate;
    uint32_t bitrate;
    uint32_t bits_per_sample;
} audio_aac_encoder_config;
#endif

typedef struct {
    audio_aac_encoder_config audio_aac_enc_cfg;
    struct aac_frame_size_control_t frame_ctl;
} audio_aac_encoder_config_v2;

typedef struct {
    audio_aac_encoder_config audio_aac_enc_cfg;
    struct aac_frame_size_control_t frame_ctl;
    uint8_t size_control_struct;
    struct aac_frame_size_control_t* frame_ptr_ctl;
} audio_aac_encoder_config_v3;

/* Information about BT CELT encoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP encoder
 */
typedef struct {
    uint32_t sampling_rate; /* 32000 - 48000, 48000 */
    uint16_t channels; /* 1-Mono, 2-Stereo, 2*/
    uint16_t frame_size; /* 64-128-256-512, 512 */
    uint16_t complexity; /* 0-10, 1 */
    uint16_t prediction_mode; /* 0-1-2, 0 */
    uint16_t vbr_flag; /* 0-1, 0*/
    uint32_t bitrate; /*32000 - 1536000, 139500*/
    uint32_t bits_per_sample;
} audio_celt_encoder_config;

/* Information about BT LDAC encoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP encoder
 */
typedef struct {
    uint32_t sampling_rate; /*44100,48000,88200,96000*/
    uint32_t bit_rate; /*303000,606000,909000(in bits per second)*/
    uint16_t channel_mode; /* 0, 4, 2, 1*/
    uint16_t mtu; /*679*/
    uint32_t bits_per_sample;
    bool is_abr_enabled;
    struct quality_level_to_bitrate_info level_to_bitrate_map;
} audio_ldac_encoder_config;

/* Information about BT AAC decoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP decoder
 */
typedef struct {
    uint16_t      aac_fmt_flag; /* LATM*/
    uint16_t      audio_object_type; /* LC */
    uint16_t      channels; /* Stereo */
    uint16_t      total_size_of_pce_bits; /* 0 - only for channel conf PCE */
    uint32_t      sampling_rate; /* 8k, 11.025k, 12k, 16k, 22.05k, 24k, 32k,
                                  44.1k, 48k, 64k, 88.2k, 96k */
} audio_aac_dec_config_t;

/* Information about BT SBC decoder configuration
 * This data is used between audio HAL module and
 * BT IPC library to configure DSP decoder
 */
typedef struct {
    uint16_t      channels; /* Mono, Stereo */
    uint32_t      sampling_rate; /* 8k, 11.025k, 12k, 16k, 22.05k, 24k, 32k,
                                  44.1k, 48k, 64k, 88.2k, 96k */
}audio_sbc_dec_config_t;

/*********** END of DSP configurable structures ********************/

static void update_offload_codec_capabilities()
{

    a2dp.is_a2dp_offload_supported =
            property_get_bool(SYSPROP_A2DP_OFFLOAD_SUPPORTED, false) &&
            !property_get_bool(SYSPROP_A2DP_OFFLOAD_DISABLED, false);

    ALOGD("%s: A2DP offload supported = %d",__func__,
          a2dp.is_a2dp_offload_supported);
}

static int stop_abr()
{
    struct mixer_ctl *ctl_abr_tx_path = NULL;
    struct mixer_ctl *ctl_abr_rx_path = NULL;
    struct mixer_ctl *ctl_set_bt_feedback_channel = NULL;
    int ret = 0;

    /* This function can be used if !abr_started for clean up */
    ALOGV("%s: enter", __func__);

    // Close hostless front end
    if (a2dp.abr_config.abr_tx_handle != NULL) {
        pcm_close(a2dp.abr_config.abr_tx_handle);
        a2dp.abr_config.abr_tx_handle = NULL;
    }
    if (a2dp.abr_config.abr_rx_handle != NULL) {
        pcm_close(a2dp.abr_config.abr_rx_handle);
        a2dp.abr_config.abr_rx_handle = NULL;
    }
    a2dp.abr_config.abr_started = false;
    a2dp.abr_config.imc_instance = 0;

    // Reset BT driver mixer control for ABR usecase
    ctl_set_bt_feedback_channel = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_SET_FEEDBACK_CHANNEL);
    if (!ctl_set_bt_feedback_channel) {
        ALOGE("%s: ERROR Set usecase mixer control not identifed", __func__);
        ret = -ENOSYS;
    } else if (mixer_ctl_set_value(ctl_set_bt_feedback_channel, 0, 0) != 0) {
        ALOGE("%s: Failed to set BT usecase", __func__);
        ret = -ENOSYS;
    }

    // Reset ABR Tx feedback path
    ALOGV("%s: Disable ABR Tx feedback path", __func__);
    ctl_abr_tx_path = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_ABR_TX_FEEDBACK_PATH);
    if (!ctl_abr_tx_path) {
        ALOGE("%s: ERROR ABR Tx feedback path mixer control not identifed", __func__);
        ret = -ENOSYS;
    } else if (mixer_ctl_set_value(ctl_abr_tx_path, 0, 0) != 0) {
        ALOGE("%s: Failed to set ABR Tx feedback path", __func__);
        ret = -ENOSYS;
    }

    // Reset ABR Rx feedback path
    ALOGV("%s: Disable ABR Rx feedback path", __func__);
    ctl_abr_rx_path = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_ABR_RX_FEEDBACK_PATH);
    if (!ctl_abr_rx_path) {
        ALOGE("%s: ERROR ABR Rx feedback path mixer control not identifed", __func__);
        ret = -ENOSYS;
    } else if (mixer_ctl_set_value(ctl_abr_rx_path, 0, 0) != 0) {
        ALOGE("%s: Failed to set ABR Rx feedback path", __func__);
        ret = -ENOSYS;
    }

   return ret;
}

static int start_abr()
{
    struct mixer_ctl *ctl_abr_tx_path = NULL;
    struct mixer_ctl *ctl_abr_rx_path = NULL;
    struct mixer_ctl *ctl_set_bt_feedback_channel = NULL;
    int abr_device_id;
    int ret = 0;

    if (!a2dp.abr_config.is_abr_enabled) {
        ALOGE("%s: Cannot start if ABR is not enabled", __func__);
        return -ENOSYS;
    }

    if (a2dp.abr_config.abr_started) {
        ALOGI("%s: ABR has already started", __func__);
        return ret;
    }

    // Enable Slimbus 7 Tx feedback path
    ALOGV("%s: Enable ABR Tx feedback path", __func__);
    ctl_abr_tx_path = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_ABR_TX_FEEDBACK_PATH);
    if (!ctl_abr_tx_path) {
        ALOGE("%s: ERROR ABR Tx feedback path mixer control not identifed", __func__);
        return -ENOSYS;
    }
    if (mixer_ctl_set_value(ctl_abr_tx_path, 0, 1) != 0) {
        ALOGE("%s: Failed to set ABR Tx feedback path", __func__);
        return -ENOSYS;
    }

    // Notify ABR usecase information to BT driver to distinguish
    // between SCO and feedback usecase
    ctl_set_bt_feedback_channel = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_SET_FEEDBACK_CHANNEL);
    if (!ctl_set_bt_feedback_channel) {
        ALOGE("%s: ERROR Set usecase mixer control not identifed", __func__);
        goto fail;
    }
    if (mixer_ctl_set_value(ctl_set_bt_feedback_channel, 0, 1) != 0) {
        ALOGE("%s: Failed to set BT usecase", __func__);
        goto fail;
    }

    // Open hostless front end and prepare ABR Tx path
    abr_device_id = fp_platform_get_pcm_device_id(USECASE_AUDIO_A2DP_ABR_FEEDBACK,
                                               PCM_CAPTURE);
    if (!a2dp.abr_config.abr_tx_handle) {
        a2dp.abr_config.abr_tx_handle = pcm_open(a2dp.adev->snd_card,
                                                 abr_device_id, PCM_IN,
                                                 &pcm_config_abr);
        if (a2dp.abr_config.abr_tx_handle == NULL) {
            ALOGE("%s: Can't open abr tx device", __func__);
            goto fail;
        }
        if (!(pcm_is_ready(a2dp.abr_config.abr_tx_handle) &&
              !pcm_start(a2dp.abr_config.abr_tx_handle))) {
            ALOGE("%s: tx: %s", __func__, pcm_get_error(a2dp.abr_config.abr_tx_handle));
            goto fail;
        }
    }

    // Enable Slimbus 7 Rx feedback path for HD Voice use case
    if (a2dp.bt_encoder_format == CODEC_TYPE_APTX_AD_SPEECH) {
        ALOGV("%s: Enable ABR Rx feedback path", __func__);
        ctl_abr_rx_path = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_ABR_RX_FEEDBACK_PATH);
        if (!ctl_abr_rx_path) {
            ALOGE("%s: ERROR ABR Rx feedback path mixer control not identifed", __func__);
            goto fail;
        }
        if (mixer_ctl_set_value(ctl_abr_rx_path, 0, 1) != 0) {
            ALOGE("%s: Failed to set ABR Rx feedback path", __func__);
            goto fail;
        }

        if (mixer_ctl_set_value(ctl_set_bt_feedback_channel, 0, 1) != 0) {
            ALOGE("%s: Failed to set BT usecase", __func__);
            goto fail;
        }

        // Open hostless front end and prepare ABR Rx path
        abr_device_id = fp_platform_get_pcm_device_id(USECASE_AUDIO_A2DP_ABR_FEEDBACK,
                                                   PCM_PLAYBACK);
        if (!a2dp.abr_config.abr_rx_handle) {
            a2dp.abr_config.abr_rx_handle = pcm_open(a2dp.adev->snd_card,
                                                     abr_device_id, PCM_OUT,
                                                     &pcm_config_abr);
            if (a2dp.abr_config.abr_rx_handle == NULL) {
                ALOGE("%s: Can't open abr rx device", __func__);
                goto fail;
            }
            if (!(pcm_is_ready(a2dp.abr_config.abr_rx_handle) &&
                  !pcm_start(a2dp.abr_config.abr_rx_handle))) {
                ALOGE("%s: rx: %s", __func__, pcm_get_error(a2dp.abr_config.abr_rx_handle));
                goto fail;
            }
        }
    }

    a2dp.abr_config.abr_started = true;

    return ret;

fail:
    stop_abr();
    return -ENOSYS;
}

static int check_if_enhanced_fwk() {

    int is_enhanced_fwk = 1;
    //dlopen lib
    vndk_fwk_lib_handle = dlopen(VNDK_FWK_LIB_PATH, RTLD_NOW);
    if (vndk_fwk_lib_handle != NULL) {
        vndk_fwk_isVendorEnhancedFwk = (vndk_fwk_isVendorEnhancedFwk_t)
                    dlsym(vndk_fwk_lib_handle, "isRunningWithVendorEnhancedFramework");
        if (vndk_fwk_isVendorEnhancedFwk == NULL) {
            ALOGW("%s: VNDK_FWK_LIB not found, defaulting to enhanced_fwk configuration",
                                                                            __func__);
            is_enhanced_fwk = 1;
        } else {
            is_enhanced_fwk = vndk_fwk_isVendorEnhancedFwk();
        }
    }
    ALOGV("%s: vndk_fwk_isVendorEnhancedFwk=%d", __func__, is_enhanced_fwk);
    return is_enhanced_fwk;
}

static void open_a2dp_source() {
    int ret = 0;

    ALOGD(" Open A2DP source start ");

    if (a2dp.bt_lib_source_handle && a2dp.audio_source_open) {
        if (a2dp.bt_state_source == A2DP_STATE_DISCONNECTED) {
            ALOGD("calling BT stream open");
            ret = a2dp.audio_source_open();
            if(ret != 0) {
                ALOGE("Failed to open source stream for a2dp: status %d", ret);
            }
            a2dp.bt_state_source = A2DP_STATE_CONNECTED;
        } else {
            ALOGD("Called a2dp open with improper state %d", a2dp.bt_state_source);
        }
    } else {
        ALOGE("a2dp handle is not identified, Ignoring open request");
        a2dp.bt_state_source = A2DP_STATE_DISCONNECTED;
    }
}
/* API to open BT IPC library to start IPC communication for BT Source*/
static void a2dp_source_init()
{
    ALOGD("a2dp_source_init START");
    if (a2dp.bt_lib_source_handle == NULL) {
        ALOGD("Requesting for BT lib handle");
        a2dp.bt_lib_source_handle = dlopen(BT_IPC_SOURCE_LIB_NAME, RTLD_NOW);
        if (a2dp.bt_lib_source_handle == NULL) {
            ALOGE("%s: dlopen failed for %s", __func__, BT_IPC_SOURCE_LIB_NAME);
            return;
        }
    }

    a2dp.bt_audio_pre_init = (bt_audio_pre_init_t)
                  dlsym(a2dp.bt_lib_source_handle, "bt_audio_pre_init");
    a2dp.audio_source_open = (audio_source_open_t)
                  dlsym(a2dp.bt_lib_source_handle, "audio_stream_open");
    a2dp.audio_source_start = (audio_source_start_t)
                  dlsym(a2dp.bt_lib_source_handle, "audio_start_stream");
    a2dp.audio_get_enc_config = (audio_get_enc_config_t)
                  dlsym(a2dp.bt_lib_source_handle, "audio_get_codec_config");
    a2dp.audio_source_suspend = (audio_source_suspend_t)
                  dlsym(a2dp.bt_lib_source_handle, "audio_suspend_stream");
    a2dp.audio_source_handoff_triggered = (audio_source_handoff_triggered_t)
                  dlsym(a2dp.bt_lib_source_handle, "audio_handoff_triggered");
    a2dp.clear_source_a2dpsuspend_flag = (clear_source_a2dpsuspend_flag_t)
                  dlsym(a2dp.bt_lib_source_handle, "clear_a2dpsuspend_flag");
    a2dp.audio_source_stop = (audio_source_stop_t)
                   dlsym(a2dp.bt_lib_source_handle, "audio_stop_stream");
    a2dp.audio_source_close = (audio_source_close_t)
                  dlsym(a2dp.bt_lib_source_handle, "audio_stream_close");
    a2dp.audio_source_check_a2dp_ready = (audio_source_check_a2dp_ready_t)
                  dlsym(a2dp.bt_lib_source_handle,"audio_check_a2dp_ready");
    a2dp.audio_sink_get_a2dp_latency = (audio_sink_get_a2dp_latency_t)
                  dlsym(a2dp.bt_lib_source_handle,"audio_sink_get_a2dp_latency");
    a2dp.audio_is_source_scrambling_enabled = (audio_is_source_scrambling_enabled_t)
                  dlsym(a2dp.bt_lib_source_handle,"audio_is_scrambling_enabled");
    a2dp.audio_is_tws_mono_mode_enable = (audio_is_tws_mono_mode_enable_t)
                   dlsym(a2dp.bt_lib_source_handle,"isTwsMonomodeEnable");

    if (a2dp.bt_lib_source_handle && a2dp.bt_audio_pre_init) {
        ALOGD("calling BT module preinit");
        // fwk related check's will be done in the BT layer
        a2dp.bt_audio_pre_init();
    }
}

/* API to open BT IPC library to start IPC communication for BT Sink*/
static void open_a2dp_sink()
{
    ALOGD(" Open A2DP input start ");
    if (a2dp.bt_lib_sink_handle == NULL){
        ALOGD(" Requesting for BT lib handle");
        a2dp.bt_lib_sink_handle = dlopen(BT_IPC_SINK_LIB_NAME, RTLD_NOW);

        if (a2dp.bt_lib_sink_handle == NULL) {
            ALOGE("%s: DLOPEN failed for %s", __func__, BT_IPC_SINK_LIB_NAME);
        } else {
            a2dp.audio_sink_start = (audio_sink_start_t)
                          dlsym(a2dp.bt_lib_sink_handle, "audio_sink_start_capture");
            a2dp.audio_get_dec_config = (audio_get_dec_config_t)
                          dlsym(a2dp.bt_lib_sink_handle, "audio_get_decoder_config");
            a2dp.audio_sink_stop = (audio_sink_stop_t)
                          dlsym(a2dp.bt_lib_sink_handle, "audio_sink_stop_capture");
            a2dp.audio_sink_check_a2dp_ready = (audio_sink_check_a2dp_ready_t)
                        dlsym(a2dp.bt_lib_sink_handle,"audio_sink_check_a2dp_ready");
            a2dp.audio_sink_session_setup_complete = (audio_sink_session_setup_complete_t)
                          dlsym(a2dp.bt_lib_sink_handle, "audio_sink_session_setup_complete");
        }
    }
}

static int close_a2dp_output()
{
    ALOGV("%s\n",__func__);

    if (!(a2dp.bt_lib_source_handle && a2dp.audio_source_close)) {
        ALOGE("a2dp source handle is not identified, Ignoring close request");
        return -ENOSYS;
    }

    if (a2dp.bt_state_source != A2DP_STATE_DISCONNECTED) {
        ALOGD("calling BT source stream close");
        if (a2dp.audio_source_close() == false)
            ALOGE("failed close a2dp source control path from BT library");
    }
    a2dp.a2dp_source_started = false;
    a2dp.a2dp_source_total_active_session_requests = 0;
    a2dp.a2dp_source_suspended = false;
    a2dp.bt_encoder_format = CODEC_TYPE_INVALID;
    a2dp.enc_sampling_rate = 48000;
    a2dp.enc_channels = 2;
    a2dp.bt_state_source = A2DP_STATE_DISCONNECTED;
    if (a2dp.abr_config.is_abr_enabled && a2dp.abr_config.abr_started)
        stop_abr();
    a2dp.abr_config.is_abr_enabled = false;
    a2dp.abr_config.abr_started = false;
    a2dp.abr_config.imc_instance = 0;
    a2dp.abr_config.abr_tx_handle = NULL;
    a2dp.abr_config.abr_rx_handle = NULL;
    a2dp.bt_state_source = A2DP_STATE_DISCONNECTED;

    return 0;
}

static int close_a2dp_input()
{
    ALOGV("%s\n",__func__);

    if (!(a2dp.bt_lib_sink_handle && a2dp.audio_source_close)) {
        ALOGE("a2dp sink handle is not identified, Ignoring close request");
        return -ENOSYS;
    }

    if (a2dp.bt_state_sink != A2DP_STATE_DISCONNECTED) {
        ALOGD("calling BT sink stream close");
        if (a2dp.audio_source_close() == false)
            ALOGE("failed close a2dp sink control path from BT library");
    }
    a2dp.a2dp_sink_started = false;
    a2dp.a2dp_sink_total_active_session_requests = 0;
    a2dp.bt_decoder_format = CODEC_TYPE_INVALID;
    a2dp.dec_sampling_rate = 48000;
    a2dp.dec_channels = 2;
    a2dp.bt_state_sink = A2DP_STATE_DISCONNECTED;

    return 0;
}

static void a2dp_check_and_set_scrambler()
{
    bool scrambler_mode = false;
    struct mixer_ctl *ctrl_scrambler_mode = NULL;
    if (a2dp.audio_is_source_scrambling_enabled && (a2dp.bt_state_source != A2DP_STATE_DISCONNECTED))
        scrambler_mode = a2dp.audio_is_source_scrambling_enabled();

    if (scrambler_mode) {
        //enable scrambler in dsp
        ctrl_scrambler_mode = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_SCRAMBLER_MODE);
        if (!ctrl_scrambler_mode) {
            ALOGE(" ERROR scrambler mode mixer control not identified");
            return;
        } else {
            if (mixer_ctl_set_value(ctrl_scrambler_mode, 0, true) != 0) {
                ALOGE("%s: Could not set scrambler mode", __func__);
                return;
            }
        }
    }
}

static bool a2dp_set_backend_cfg(uint8_t direction)
{
    char *rate_str = NULL, *channels = NULL;
    uint32_t sampling_rate;
    struct mixer_ctl *ctl_sample_rate = NULL, *ctrl_channels = NULL;
    bool is_configured = false;

    if (direction == SINK) {
        sampling_rate = a2dp.dec_sampling_rate;
    } else {
        sampling_rate = a2dp.enc_sampling_rate;
    }
    /*
     * For LDAC encoder and AAC decoder open slimbus port at
     * 96Khz for 48Khz input and 88.2Khz for 44.1Khz input.
     * For APTX AD encoder, open slimbus port at 96Khz for 48Khz input.
     */
    if (((a2dp.bt_encoder_format == CODEC_TYPE_LDAC) ||
         (a2dp.bt_decoder_format == CODEC_TYPE_SBC) ||
         (a2dp.bt_decoder_format == AUDIO_FORMAT_AAC) ||
         (a2dp.bt_encoder_format == CODEC_TYPE_APTX_AD)) &&
        (sampling_rate == 48000 || sampling_rate == 44100 )) {
        sampling_rate = sampling_rate *2;
    }

    // No need to configure backend for PCM format.
    if (a2dp.bt_encoder_format == CODEC_TYPE_PCM) {
        return 0;
    }

    //Configure backend sampling rate
    switch (sampling_rate) {
    case 44100:
        rate_str = "KHZ_44P1";
        break;
    case 88200:
        rate_str = "KHZ_88P2";
        break;
    case 96000:
        rate_str = "KHZ_96";
        break;
    case 48000:
    default:
        rate_str = "KHZ_48";
        break;
    }

    if (direction == SINK) {
        ALOGD("%s: set sink backend sample rate =%s", __func__, rate_str);
        ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_SINK_SAMPLE_RATE);
    } else {
        ALOGD("%s: set source backend sample rate =%s", __func__, rate_str);
        ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_SAMPLE_RATE_RX);
    }
    if (ctl_sample_rate) {

        if (mixer_ctl_set_enum_by_string(ctl_sample_rate, rate_str) != 0) {
            ALOGE("%s: Failed to set backend sample rate = %s", __func__, rate_str);
            is_configured = false;
            goto fail;
        }

        if (direction == SOURCE) {
            /* Set Tx backend sample rate */
            if (a2dp.abr_config.is_abr_enabled) {
                if (a2dp.bt_encoder_format == CODEC_TYPE_APTX_AD_SPEECH)
                    rate_str = SPEECH_TX_SAMPLE_RATE;
                else
                    rate_str = ABR_TX_SAMPLE_RATE;

                ALOGD("%s: set backend tx sample rate = %s", __func__, rate_str);
                ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                                MIXER_SOURCE_SAMPLE_RATE_TX);
                if (!ctl_sample_rate) {
                    ALOGE("%s: ERROR backend sample rate mixer control not identifed", __func__);
                    is_configured = false;
                    goto fail;
                }

                if (mixer_ctl_set_enum_by_string(ctl_sample_rate, rate_str) != 0) {
                    ALOGE("%s: Failed to set backend sample rate = %s", __func__, rate_str);
                    is_configured = false;
                    goto fail;
                }
            }
        }
    } else {
        /* Fallback to legacy approch if MIXER_SAMPLE_RATE_RX and
        MIXER_SAMPLE_RATE_TX is not supported */
        ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_SAMPLE_RATE_DEFAULT);
        if (!ctl_sample_rate) {
            ALOGE("%s: ERROR backend sample rate mixer control not identifed", __func__);
            is_configured = false;
            goto fail;
        }

        if (mixer_ctl_set_enum_by_string(ctl_sample_rate, rate_str) != 0) {
            ALOGE("%s: Failed to set backend sample rate = %s", __func__, rate_str);
            is_configured = false;
            goto fail;
        }
    }

    if (direction == SINK) {
        switch (a2dp.dec_channels) {
        case 1:
            channels = "One";
            break;
        case 2:
        default:
            channels = "Two";
            break;
        }

        ALOGD("%s: set afe dec channels =%s", __func__, channels);
        ctrl_channels = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_AFE_SINK_CHANNELS);
    } else {
        //Configure AFE enc channels
        switch (a2dp.enc_channels) {
        case 1:
            channels = "One";
            break;
        case 2:
        default:
            channels = "Two";
            break;
        }

        ALOGD("%s: set afe enc channels =%s", __func__, channels);
        ctrl_channels = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_AFE_IN_CHANNELS);
    }

    if (!ctrl_channels) {
        ALOGE(" ERROR AFE channels mixer control not identified");
    } else {
        if (mixer_ctl_set_enum_by_string(ctrl_channels, channels) != 0) {
            ALOGE("%s: Failed to set AFE channels =%s", __func__, channels);
            is_configured = false;
            goto fail;
        }
    }
    is_configured = true;
fail:
    return is_configured;
}

bool a2dp_set_source_backend_cfg()
{
    if (a2dp.a2dp_source_started && !a2dp.a2dp_source_suspended)
        return a2dp_set_backend_cfg(SOURCE);

    return false;
}

bool configure_aac_dec_format(audio_aac_dec_config_t *aac_bt_cfg)
{
    struct mixer_ctl *ctl_dec_data = NULL, *ctrl_bit_format = NULL;
    struct aac_dec_cfg_t aac_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if (aac_bt_cfg == NULL)
        return false;

    ctl_dec_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_SINK_DEC_CONFIG_BLOCK);
    if (!ctl_dec_data) {
        ALOGE(" ERROR  a2dp decoder CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }

    memset(&aac_dsp_cfg, 0x0, sizeof(struct aac_dec_cfg_t));
    aac_dsp_cfg.dec_format = MEDIA_FMT_AAC;
    aac_dsp_cfg.data.aac_fmt_flag = aac_bt_cfg->aac_fmt_flag;
    aac_dsp_cfg.data.channels = aac_bt_cfg->channels;
    switch(aac_bt_cfg->audio_object_type) {
    case 0:
        aac_dsp_cfg.data.audio_object_type = MEDIA_FMT_AAC_AOT_LC;
        break;
    case 2:
        aac_dsp_cfg.data.audio_object_type = MEDIA_FMT_AAC_AOT_PS;
        break;
    case 1:
    default:
        aac_dsp_cfg.data.audio_object_type = MEDIA_FMT_AAC_AOT_SBR;
        break;
    }
    aac_dsp_cfg.data.total_size_of_pce_bits = aac_bt_cfg->total_size_of_pce_bits;
    aac_dsp_cfg.data.sampling_rate = aac_bt_cfg->sampling_rate;
    ret = mixer_ctl_set_array(ctl_dec_data, (void *)&aac_dsp_cfg,
                              sizeof(struct aac_dec_cfg_t));
    if (ret != 0) {
        ALOGE("%s: failed to set AAC decoder config", __func__);
        is_configured = false;
        goto fail;
    }

    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_DEC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE(" ERROR Dec bit format mixer control not identified");
        is_configured = false;
        goto fail;
    }
    ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
    if (ret != 0) {
        ALOGE("%s: Failed to set bit format to decoder", __func__);
        is_configured = false;
        goto fail;
    }

    is_configured = true;
    a2dp.bt_decoder_format = CODEC_TYPE_AAC;
    a2dp.dec_channels = aac_dsp_cfg.data.channels;
    a2dp.dec_sampling_rate = aac_dsp_cfg.data.sampling_rate;
    ALOGV("Successfully updated AAC dec format with sampling_rate: %d channels:%d",
           aac_dsp_cfg.data.sampling_rate, aac_dsp_cfg.data.channels);
fail:
    return is_configured;
}

static int a2dp_set_bit_format(uint32_t enc_bit_format)
{
    const char *bit_format = NULL;
    struct mixer_ctl *ctrl_bit_format = NULL;

    // Configure AFE Input Bit Format
    switch (enc_bit_format) {
    case 32:
        bit_format = "S32_LE";
        break;
    case 24:
        bit_format = "S24_LE";
        break;
    case 16:
    default:
        bit_format = "S16_LE";
        break;
    }

    ALOGD("%s: set AFE input bit format = %d", __func__, enc_bit_format);
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_ENC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE("%s: ERROR AFE input bit format mixer control not identifed", __func__);
        return -ENOSYS;
    }
    if (mixer_ctl_set_enum_by_string(ctrl_bit_format, bit_format) != 0) {
        ALOGE("%s: Failed to set AFE input bit format = %d", __func__, enc_bit_format);
        return -ENOSYS;
    }
    return 0;
}

static int a2dp_reset_backend_cfg(uint8_t direction)
{
    const char *rate_str = "KHZ_8", *channels = "Zero";
    struct mixer_ctl *ctl_sample_rate = NULL, *ctl_sample_rate_tx = NULL;
    struct mixer_ctl *ctrl_channels = NULL;

    // Reset backend sampling rate
    if (direction == SINK) {
        ALOGD("%s: reset sink backend sample rate =%s", __func__, rate_str);
        ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                              MIXER_SINK_SAMPLE_RATE);
    } else {
        ALOGD("%s: reset source backend sample rate =%s", __func__, rate_str);
        ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                              MIXER_SAMPLE_RATE_RX);
    }
    if (ctl_sample_rate) {

        if (mixer_ctl_set_enum_by_string(ctl_sample_rate, rate_str) != 0) {
            ALOGE("%s: Failed to reset backend sample rate = %s", __func__, rate_str);
            return -ENOSYS;
        }
        if (a2dp.abr_config.is_abr_enabled) {
            ctl_sample_rate_tx = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_SOURCE_SAMPLE_RATE_TX);
            if (!ctl_sample_rate_tx) {
                ALOGE("%s: ERROR Tx backend sample rate mixer control not identifed", __func__);
                return -ENOSYS;
            }

            if (mixer_ctl_set_enum_by_string(ctl_sample_rate_tx, rate_str) != 0) {
                ALOGE("%s: Failed to reset Tx backend sample rate = %s", __func__, rate_str);
                return -ENOSYS;
            }
        }
    } else {

        ctl_sample_rate = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                        MIXER_SAMPLE_RATE_DEFAULT);
        if (!ctl_sample_rate) {
            ALOGE("%s: ERROR backend sample rate mixer control not identifed", __func__);
            return -ENOSYS;
        }

        if (mixer_ctl_set_enum_by_string(ctl_sample_rate, rate_str) != 0) {
            ALOGE("%s: Failed to reset backend sample rate = %s", __func__, rate_str);
            return -ENOSYS;
        }
    }

    // Reset AFE input channels
    if (direction == SINK) {
        ALOGD("%s: reset afe sink channels =%s", __func__, channels);
        ctrl_channels = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_AFE_SINK_CHANNELS);
    } else {
        ALOGD("%s: reset afe source channels =%s", __func__, channels);
        ctrl_channels = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_AFE_IN_CHANNELS);
    }
    if (!ctrl_channels) {
        ALOGE("%s: ERROR AFE input channels mixer control not identifed", __func__);
        return -ENOSYS;
    }
    if (mixer_ctl_set_enum_by_string(ctrl_channels, channels) != 0) {
        ALOGE("%s: Failed to reset AFE in channels = %d", __func__, a2dp.enc_channels);
        return -ENOSYS;
    }

    return 0;
}

/* API to configure AFE decoder in DSP */
static bool configure_a2dp_source_decoder_format(int dec_format)
{
    struct mixer_ctl *ctl_dec_data = NULL;
    struct abr_dec_cfg_t dec_cfg;
    int ret = 0;

    if (a2dp.abr_config.is_abr_enabled) {
        ctl_dec_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_SOURCE_DEC_CONFIG_BLOCK);
        if (!ctl_dec_data) {
            ALOGE("%s: ERROR A2DP codec config data mixer control not identifed", __func__);
            return false;
        }
        memset(&dec_cfg, 0x0, sizeof(dec_cfg));
        dec_cfg.dec_format = dec_format;
        dec_cfg.imc_info.direction = IMC_TRANSMIT;
        dec_cfg.imc_info.enable = IMC_ENABLE;
        dec_cfg.imc_info.purpose = IMC_PURPOSE_ID_BT_INFO;
        dec_cfg.imc_info.comm_instance = a2dp.abr_config.imc_instance;

        ret = mixer_ctl_set_array(ctl_dec_data, &dec_cfg,
                                  sizeof(dec_cfg));
        if (ret != 0) {
            ALOGE("%s: Failed to set decoder config", __func__);
            return false;
        }
     }

     return true;
}

bool configure_sbc_dec_format(audio_sbc_dec_config_t *sbc_bt_cfg)
{
    struct mixer_ctl *ctl_dec_data = NULL, *ctrl_bit_format = NULL;
    struct sbc_dec_cfg_t sbc_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if (sbc_bt_cfg == NULL)
        goto fail;

    ctl_dec_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_SINK_DEC_CONFIG_BLOCK);
    if (!ctl_dec_data) {
        ALOGE(" ERROR  a2dp decoder CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }

    memset(&sbc_dsp_cfg, 0x0, sizeof(struct sbc_dec_cfg_t));
    sbc_dsp_cfg.dec_format = MEDIA_FMT_SBC;
    sbc_dsp_cfg.data.channels = sbc_bt_cfg->channels;
    sbc_dsp_cfg.data.sampling_rate = sbc_bt_cfg->sampling_rate;
    ret = mixer_ctl_set_array(ctl_dec_data, (void *)&sbc_dsp_cfg,
                              sizeof(struct sbc_dec_cfg_t));

    if (ret != 0) {
        ALOGE("%s: failed to set SBC decoder config", __func__);
        is_configured = false;
        goto fail;
    }

    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_DEC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE(" ERROR Dec bit format mixer control not identified");
        is_configured = false;
        goto fail;
    }
    ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
    if (ret != 0) {
        ALOGE("%s: Failed to set bit format to decoder", __func__);
        is_configured = false;
        goto fail;
    }

    is_configured = true;
    a2dp.bt_decoder_format = CODEC_TYPE_SBC;
    if (sbc_dsp_cfg.data.channels == MEDIA_FMT_SBC_CHANNEL_MODE_MONO)
        a2dp.dec_channels = 1;
    else
        a2dp.dec_channels = 2;
    a2dp.dec_sampling_rate = sbc_dsp_cfg.data.sampling_rate;
    ALOGV("Successfully updated SBC dec format");
fail:
    return is_configured;
}

/* API to configure AFE decoder in DSP */
static bool configure_a2dp_sink_decoder_format()
{
    void *codec_info = NULL;
    codec_t codec_type = CODEC_TYPE_INVALID;
    bool is_configured = false;
    struct mixer_ctl *ctl_dec_data = NULL;

    if (!a2dp.audio_get_dec_config) {
        ALOGE(" a2dp handle is not identified, ignoring a2dp decoder config");
        return false;
    }

    ctl_dec_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_SINK_DEC_CONFIG_BLOCK);
    if (!ctl_dec_data) {
        ALOGE(" ERROR  a2dp decoder CONFIG data mixer control not identified");
        is_configured = false;
        return false;
    }
    codec_info = a2dp.audio_get_dec_config(&codec_type);
    switch(codec_type) {
        case CODEC_TYPE_SBC:
            ALOGD(" SBC decoder supported BT device");
            is_configured = configure_sbc_dec_format((audio_sbc_dec_config_t *)codec_info);
            break;
        case CODEC_TYPE_AAC:
            ALOGD(" AAC decoder supported BT device");
            is_configured =
              configure_aac_dec_format((audio_aac_dec_config_t *)codec_info);
            break;
        default:
            ALOGD(" Received Unsupported decoder format");
            is_configured = false;
            break;
    }
    return is_configured;
}

/* API to configure SBC DSP encoder */
bool configure_sbc_enc_format(audio_sbc_encoder_config *sbc_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL;
    struct sbc_enc_cfg_t sbc_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if (sbc_bt_cfg == NULL)
        return false;

   ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }
    memset(&sbc_dsp_cfg, 0x0, sizeof(struct sbc_enc_cfg_t));
    sbc_dsp_cfg.enc_format = MEDIA_FMT_SBC;
    sbc_dsp_cfg.num_subbands = sbc_bt_cfg->subband;
    sbc_dsp_cfg.blk_len = sbc_bt_cfg->blk_len;
    switch(sbc_bt_cfg->channels) {
        case 0:
            sbc_dsp_cfg.channel_mode = MEDIA_FMT_SBC_CHANNEL_MODE_MONO;
            break;
        case 1:
            sbc_dsp_cfg.channel_mode = MEDIA_FMT_SBC_CHANNEL_MODE_DUAL_MONO;
            break;
        case 3:
            sbc_dsp_cfg.channel_mode = MEDIA_FMT_SBC_CHANNEL_MODE_JOINT_STEREO;
            break;
        case 2:
        default:
            sbc_dsp_cfg.channel_mode = MEDIA_FMT_SBC_CHANNEL_MODE_STEREO;
            break;
    }
    if (sbc_bt_cfg->alloc)
        sbc_dsp_cfg.alloc_method = MEDIA_FMT_SBC_ALLOCATION_METHOD_LOUDNESS;
    else
        sbc_dsp_cfg.alloc_method = MEDIA_FMT_SBC_ALLOCATION_METHOD_SNR;
    sbc_dsp_cfg.bit_rate = sbc_bt_cfg->bitrate;
    sbc_dsp_cfg.sample_rate = sbc_bt_cfg->sampling_rate;
    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&sbc_dsp_cfg,
                                    sizeof(struct sbc_enc_cfg_t));
    if (ret != 0) {
        ALOGE("%s: failed to set SBC encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ret = a2dp_set_bit_format(sbc_bt_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = CODEC_TYPE_SBC;
    a2dp.enc_sampling_rate = sbc_bt_cfg->sampling_rate;

    if (sbc_dsp_cfg.channel_mode == MEDIA_FMT_SBC_CHANNEL_MODE_MONO)
        a2dp.enc_channels = 1;
    else
        a2dp.enc_channels = 2;

    ALOGV("Successfully updated SBC enc format with samplingrate: %d channelmode:%d",
           sbc_dsp_cfg.sample_rate, sbc_dsp_cfg.channel_mode);
fail:
    return is_configured;
}

#ifndef LINUX_ENABLED
static int update_aptx_ad_dsp_config(struct aptx_ad_enc_cfg_t *aptx_dsp_cfg,
                                     audio_aptx_encoder_config *aptx_bt_cfg)
{
    int ret = 0;

    if (aptx_dsp_cfg == NULL || aptx_bt_cfg == NULL) {
        ALOGE("Invalid param, aptx_dsp_cfg %p aptx_bt_cfg %p",
              aptx_dsp_cfg, aptx_bt_cfg);
        return -EINVAL;
    }

    memset(aptx_dsp_cfg, 0x0, sizeof(struct aptx_ad_enc_cfg_t));
    aptx_dsp_cfg->custom_cfg.enc_format = MEDIA_FMT_APTX_AD;


    aptx_dsp_cfg->aptx_ad_cfg.sampling_freq = aptx_bt_cfg->ad_cfg->sampling_rate;
    aptx_dsp_cfg->aptx_ad_cfg.mtu = aptx_bt_cfg->ad_cfg->mtu;
    aptx_dsp_cfg->aptx_ad_cfg.channel_mode = aptx_bt_cfg->ad_cfg->channel_mode;
    aptx_dsp_cfg->aptx_ad_cfg.min_sink_modeA = aptx_bt_cfg->ad_cfg->min_sink_modeA;
    aptx_dsp_cfg->aptx_ad_cfg.max_sink_modeA = aptx_bt_cfg->ad_cfg->max_sink_modeA;
    aptx_dsp_cfg->aptx_ad_cfg.min_sink_modeB = aptx_bt_cfg->ad_cfg->min_sink_modeB;
    aptx_dsp_cfg->aptx_ad_cfg.max_sink_modeB = aptx_bt_cfg->ad_cfg->max_sink_modeB;
    aptx_dsp_cfg->aptx_ad_cfg.min_sink_modeC = aptx_bt_cfg->ad_cfg->min_sink_modeC;
    aptx_dsp_cfg->aptx_ad_cfg.max_sink_modeC = aptx_bt_cfg->ad_cfg->max_sink_modeC;
    aptx_dsp_cfg->aptx_ad_cfg.mode = aptx_bt_cfg->ad_cfg->encoder_mode;
    aptx_dsp_cfg->abr_cfg.imc_info.direction = IMC_RECEIVE;
    aptx_dsp_cfg->abr_cfg.imc_info.enable = IMC_ENABLE;
    aptx_dsp_cfg->abr_cfg.imc_info.purpose = IMC_PURPOSE_ID_BT_INFO;
    aptx_dsp_cfg->abr_cfg.imc_info.comm_instance = a2dp.abr_config.imc_instance;


    switch(aptx_dsp_cfg->aptx_ad_cfg.channel_mode) {
        case APTX_AD_CHANNEL_UNCHANGED:
        case APTX_AD_CHANNEL_JOINT_STEREO:
        case APTX_AD_CHANNEL_DUAL_MONO:
        case APTX_AD_CHANNEL_STEREO_TWS:
        case APTX_AD_CHANNEL_EARBUD:
        default:
             a2dp.enc_channels = CH_STEREO;
             aptx_dsp_cfg->custom_cfg.num_channels = CH_STEREO;
             aptx_dsp_cfg->custom_cfg.channel_mapping[0] = PCM_CHANNEL_L;
             aptx_dsp_cfg->custom_cfg.channel_mapping[1] = PCM_CHANNEL_R;
             break;
        case APTX_AD_CHANNEL_MONO:
             a2dp.enc_channels = CH_MONO;
             aptx_dsp_cfg->custom_cfg.num_channels = CH_MONO;
             aptx_dsp_cfg->custom_cfg.channel_mapping[0] = PCM_CHANNEL_C;
             break;
    }
    switch(aptx_dsp_cfg->aptx_ad_cfg.sampling_freq) {
        case APTX_AD_SR_UNCHANGED:
        case APTX_AD_48:
        default:
            a2dp.enc_sampling_rate = SAMPLING_RATE_48K;
            aptx_dsp_cfg->custom_cfg.sample_rate = SAMPLING_RATE_48K;
            break;
        case APTX_AD_44_1:
            a2dp.enc_sampling_rate = SAMPLING_RATE_441K;
            aptx_dsp_cfg->custom_cfg.sample_rate = SAMPLING_RATE_441K;
            break;
    }
    ALOGV("Successfully updated APTX AD enc format with \
               samplingrate: %d channels:%d",
               aptx_dsp_cfg->custom_cfg.sample_rate,
               aptx_dsp_cfg->custom_cfg.num_channels);

    return ret;
}

static int update_aptx_ad_dsp_config_r2(struct aptx_ad_enc_cfg_r2_t *aptx_dsp_cfg,
                                     audio_aptx_encoder_config *aptx_bt_cfg)
{
    int ret = 0;

    if (aptx_dsp_cfg == NULL || aptx_bt_cfg == NULL) {
        ALOGE("Invalid param, aptx_dsp_cfg %pK aptx_bt_cfg %pK",
              aptx_dsp_cfg, aptx_bt_cfg);
        return -EINVAL;
    }

    memset(aptx_dsp_cfg, 0x0, sizeof(struct aptx_ad_enc_cfg_r2_t));
    aptx_dsp_cfg->custom_cfg.enc_format = MEDIA_FMT_APTX_AD;


    aptx_dsp_cfg->aptx_ad_cfg.sampling_freq = aptx_bt_cfg->ad_cfg->sampling_rate;
    aptx_dsp_cfg->aptx_ad_cfg.mtu = aptx_bt_cfg->ad_cfg->mtu;
    aptx_dsp_cfg->aptx_ad_cfg.channel_mode = aptx_bt_cfg->ad_cfg->channel_mode;
    aptx_dsp_cfg->aptx_ad_cfg.min_sink_modeA = aptx_bt_cfg->ad_cfg->min_sink_modeA;
    aptx_dsp_cfg->aptx_ad_cfg.max_sink_modeA = aptx_bt_cfg->ad_cfg->max_sink_modeA;
    aptx_dsp_cfg->aptx_ad_cfg.min_sink_modeB = aptx_bt_cfg->ad_cfg->min_sink_modeB;
    aptx_dsp_cfg->aptx_ad_cfg.max_sink_modeB = aptx_bt_cfg->ad_cfg->max_sink_modeB;
    aptx_dsp_cfg->aptx_ad_cfg.min_sink_modeC = aptx_bt_cfg->ad_cfg->min_sink_modeC;
    aptx_dsp_cfg->aptx_ad_cfg.max_sink_modeC = aptx_bt_cfg->ad_cfg->max_sink_modeC;
    aptx_dsp_cfg->aptx_ad_cfg.mode = aptx_bt_cfg->ad_cfg->encoder_mode;
    aptx_dsp_cfg->aptx_ad_cfg.input_mode = aptx_bt_cfg->ad_cfg->input_mode;
    aptx_dsp_cfg->aptx_ad_cfg.fade_duration = aptx_bt_cfg->ad_cfg->fade_duration;
    for (int i = 0; i < sizeof(aptx_dsp_cfg->aptx_ad_cfg.sink_cap); i ++)
        aptx_dsp_cfg->aptx_ad_cfg.sink_cap[i] = aptx_bt_cfg->ad_cfg->sink_cap[i];
    aptx_dsp_cfg->abr_cfg.imc_info.direction = IMC_RECEIVE;
    aptx_dsp_cfg->abr_cfg.imc_info.enable = IMC_ENABLE;
    aptx_dsp_cfg->abr_cfg.imc_info.purpose = IMC_PURPOSE_ID_BT_INFO;
    aptx_dsp_cfg->abr_cfg.imc_info.comm_instance = a2dp.abr_config.imc_instance;


    switch(aptx_dsp_cfg->aptx_ad_cfg.channel_mode) {
        case APTX_AD_CHANNEL_UNCHANGED:
        case APTX_AD_CHANNEL_JOINT_STEREO:
        case APTX_AD_CHANNEL_DUAL_MONO:
        case APTX_AD_CHANNEL_STEREO_TWS:
        case APTX_AD_CHANNEL_EARBUD:
        default:
             a2dp.enc_channels = CH_STEREO;
             aptx_dsp_cfg->custom_cfg.num_channels = CH_STEREO;
             aptx_dsp_cfg->custom_cfg.channel_mapping[0] = PCM_CHANNEL_L;
             aptx_dsp_cfg->custom_cfg.channel_mapping[1] = PCM_CHANNEL_R;
             break;
        case APTX_AD_CHANNEL_MONO:
             a2dp.enc_channels = CH_MONO;
             aptx_dsp_cfg->custom_cfg.num_channels = CH_MONO;
             aptx_dsp_cfg->custom_cfg.channel_mapping[0] = PCM_CHANNEL_C;
             break;
    }
    switch(aptx_dsp_cfg->aptx_ad_cfg.sampling_freq) {
        case APTX_AD_SR_UNCHANGED:
        case APTX_AD_48:
        default:
            a2dp.enc_sampling_rate = SAMPLING_RATE_48K;
            aptx_dsp_cfg->custom_cfg.sample_rate = SAMPLING_RATE_48K;
            break;
        case APTX_AD_44_1:
            a2dp.enc_sampling_rate = SAMPLING_RATE_441K;
            aptx_dsp_cfg->custom_cfg.sample_rate = SAMPLING_RATE_441K;
            break;
        case APTX_AD_96:
            a2dp.enc_sampling_rate = SAMPLING_RATE_96K;
            aptx_dsp_cfg->custom_cfg.sample_rate = SAMPLING_RATE_96K;
            break;
    }
    ALOGV("Successfully updated APTX AD enc format with \
               samplingrate: %d channels:%d",
               aptx_dsp_cfg->custom_cfg.sample_rate,
               aptx_dsp_cfg->custom_cfg.num_channels);

    return ret;
}

static void audio_a2dp_update_tws_channel_mode()
{
    char* channel_mode;
    struct mixer_ctl *ctl_channel_mode;

    ALOGD("Update tws for mono_mode on=%d",a2dp.is_tws_mono_mode_on);

    if (a2dp.is_tws_mono_mode_on)
       channel_mode = "One";
    else
       channel_mode = "Two";

    ctl_channel_mode = mixer_get_ctl_by_name(a2dp.adev->mixer,MIXER_FMT_TWS_CHANNEL_MODE);
    if (!ctl_channel_mode) {
         ALOGE("failed to get tws mixer ctl");
         return;
    }
    if (mixer_ctl_set_enum_by_string(ctl_channel_mode, channel_mode) != 0) {
         ALOGE("%s: Failed to set the channel mode = %s", __func__, channel_mode);
         return;
    }
}

static int update_aptx_dsp_config_v2(struct aptx_enc_cfg_t *aptx_dsp_cfg,
                                     audio_aptx_encoder_config *aptx_bt_cfg)
{
    int ret = 0;

    if (aptx_dsp_cfg == NULL || aptx_bt_cfg == NULL) {
        ALOGE("Invalid param, aptx_dsp_cfg %p aptx_bt_cfg %p",
              aptx_dsp_cfg, aptx_bt_cfg);
        return -EINVAL;
    }

    memset(aptx_dsp_cfg, 0x0, sizeof(struct aptx_enc_cfg_t));
    aptx_dsp_cfg->custom_cfg.enc_format = MEDIA_FMT_APTX;

    if (!a2dp.is_aptx_dual_mono_supported) {
        aptx_dsp_cfg->custom_cfg.sample_rate = aptx_bt_cfg->default_cfg->sampling_rate;
        aptx_dsp_cfg->custom_cfg.num_channels = aptx_bt_cfg->default_cfg->channels;
    } else {
        aptx_dsp_cfg->custom_cfg.sample_rate = aptx_bt_cfg->dual_mono_cfg->sampling_rate;
        aptx_dsp_cfg->custom_cfg.num_channels = aptx_bt_cfg->dual_mono_cfg->channels;
        aptx_dsp_cfg->aptx_v2_cfg.sync_mode = aptx_bt_cfg->dual_mono_cfg->sync_mode;
    }

    switch(aptx_dsp_cfg->custom_cfg.num_channels) {
        case 1:
            aptx_dsp_cfg->custom_cfg.channel_mapping[0] = PCM_CHANNEL_C;
            break;
        case 2:
        default:
               aptx_dsp_cfg->custom_cfg.channel_mapping[0] = PCM_CHANNEL_L;
               aptx_dsp_cfg->custom_cfg.channel_mapping[1] = PCM_CHANNEL_R;
            break;
    }
    a2dp.enc_channels = aptx_dsp_cfg->custom_cfg.num_channels;
    if (!a2dp.is_aptx_dual_mono_supported) {
        a2dp.enc_sampling_rate = aptx_bt_cfg->default_cfg->sampling_rate;
        ALOGV("Successfully updated APTX enc format with samplingrate: %d \
               channels:%d", aptx_dsp_cfg->custom_cfg.sample_rate,
               aptx_dsp_cfg->custom_cfg.num_channels);
    } else {
        a2dp.enc_sampling_rate = aptx_bt_cfg->dual_mono_cfg->sampling_rate;
        ALOGV("Successfully updated APTX dual mono enc format with \
               samplingrate: %d channels:%d syncmode %d",
               aptx_dsp_cfg->custom_cfg.sample_rate,
               aptx_dsp_cfg->custom_cfg.num_channels,
               aptx_dsp_cfg->aptx_v2_cfg.sync_mode);
    }
    return ret;
}
#else
static int update_aptx_dsp_config_v1(struct custom_enc_cfg_t *aptx_dsp_cfg,
                                     audio_aptx_encoder_config *aptx_bt_cfg)
{
    int ret = 0;

    if (aptx_dsp_cfg == NULL || aptx_bt_cfg == NULL) {
        ALOGE("Invalid param, aptx_dsp_cfg %p aptx_bt_cfg %p",
              aptx_dsp_cfg, aptx_bt_cfg);
        return -EINVAL;
    }

    memset(&aptx_dsp_cfg, 0x0, sizeof(struct custom_enc_cfg_t));
    aptx_dsp_cfg->enc_format = MEDIA_FMT_APTX;
    aptx_dsp_cfg->sample_rate = aptx_bt_cfg->sampling_rate;
    aptx_dsp_cfg->num_channels = aptx_bt_cfg->channels;
    switch(aptx_dsp_cfg->num_channels) {
        case 1:
            aptx_dsp_cfg->channel_mapping[0] = PCM_CHANNEL_C;
            break;
        case 2:
        default:
            aptx_dsp_cfg->channel_mapping[0] = PCM_CHANNEL_L;
            aptx_dsp_cfg->channel_mapping[1] = PCM_CHANNEL_R;
            break;
    }

    ALOGV("Updated APTX enc format with samplingrate: %d channels:%d",
            aptx_dsp_cfg->sample_rate, aptx_dsp_cfg->num_channels);

    return ret;
}
#endif

/* API to configure APTX DSP encoder */
bool configure_aptx_enc_format(audio_aptx_encoder_config *aptx_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL;
    struct mixer_ctl *aptx_ad_ctl = NULL;
    int mixer_size = 0;
    bool is_configured = false;
    int ret = 0;
    int sample_rate_backup = SAMPLING_RATE_48K;

    if (aptx_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR a2dp encoder CONFIG data mixer control not identifed");
        return false;
    }

#ifndef LINUX_ENABLED
    struct aptx_enc_cfg_t aptx_dsp_cfg;
    struct aptx_ad_enc_cfg_t aptx_ad_dsp_cfg;
    struct aptx_ad_enc_cfg_r2_t aptx_ad_dsp_cfg_r2;
    if (a2dp.is_aptx_adaptive) {
        aptx_ad_ctl = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                    MIXER_ENC_APTX_AD_CONFIG_BLOCK);
        if (aptx_ad_ctl)
            ret = update_aptx_ad_dsp_config_r2(&aptx_ad_dsp_cfg_r2, aptx_bt_cfg);
        else
            ret = update_aptx_ad_dsp_config(&aptx_ad_dsp_cfg, aptx_bt_cfg);
    } else
        ret = update_aptx_dsp_config_v2(&aptx_dsp_cfg, aptx_bt_cfg);

    if (ret) {
        is_configured = false;
        goto fail;
    }

    if (a2dp.is_aptx_adaptive) {
        if (aptx_ad_ctl)
            ret = mixer_ctl_set_array(aptx_ad_ctl, (void *)&aptx_ad_dsp_cfg_r2,
                              sizeof(struct aptx_ad_enc_cfg_r2_t));
        else
            ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aptx_ad_dsp_cfg,
                              sizeof(struct aptx_ad_enc_cfg_t));
    } else {
        ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aptx_dsp_cfg,
                              sizeof(struct aptx_enc_cfg_t));
    }
#else
    struct custom_enc_cfg_t aptx_dsp_cfg;
    mixer_size = sizeof(struct custom_enc_cfg_t);
    sample_rate_backup = aptx_bt_cfg->sampling_rate;
    ret = update_aptx_dsp_config_v1(&aptx_dsp_cfg, aptx_bt_cfg);
    if (ret) {
        is_configured = false;
        goto fail;
    }
    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aptx_dsp_cfg,
                          mixer_size);
#endif
    if (ret != 0) {
        ALOGE("%s: Failed to set APTX encoder config", __func__);
        is_configured = false;
        goto fail;
    }
#ifndef LINUX_ENABLED //Temporarily disabled for LE, need to take care while doing VT FR
    if (a2dp.is_aptx_adaptive)
        ret = a2dp_set_bit_format(aptx_bt_cfg->ad_cfg->bits_per_sample);
    else if (a2dp.is_aptx_dual_mono_supported)
        ret = a2dp_set_bit_format(aptx_bt_cfg->dual_mono_cfg->bits_per_sample);
    else
        ret = a2dp_set_bit_format(aptx_bt_cfg->default_cfg->bits_per_sample);
#endif
    if (ret != 0) {
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    if (a2dp.is_aptx_adaptive)
        a2dp.bt_encoder_format = CODEC_TYPE_APTX_AD;
    else
        a2dp.bt_encoder_format = CODEC_TYPE_APTX;
fail:
    /*restore sample rate */
    if (!is_configured)
        a2dp.enc_sampling_rate = sample_rate_backup;
    return is_configured;
}

/* API to configure APTX HD DSP encoder
 */
#ifndef LINUX_ENABLED
bool configure_aptx_hd_enc_format(audio_aptx_default_config *aptx_bt_cfg)
#else
bool configure_aptx_hd_enc_format(audio_aptx_encoder_config *aptx_bt_cfg)
#endif
{
    struct mixer_ctl *ctl_enc_data = NULL;
    struct custom_enc_cfg_t aptx_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if (aptx_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }

    memset(&aptx_dsp_cfg, 0x0, sizeof(struct custom_enc_cfg_t));
    aptx_dsp_cfg.enc_format = MEDIA_FMT_APTX_HD;
    aptx_dsp_cfg.sample_rate = aptx_bt_cfg->sampling_rate;
    aptx_dsp_cfg.num_channels = aptx_bt_cfg->channels;
    switch(aptx_dsp_cfg.num_channels) {
        case 1:
            aptx_dsp_cfg.channel_mapping[0] = PCM_CHANNEL_C;
            break;
        case 2:
        default:
            aptx_dsp_cfg.channel_mapping[0] = PCM_CHANNEL_L;
            aptx_dsp_cfg.channel_mapping[1] = PCM_CHANNEL_R;
            break;
    }
    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aptx_dsp_cfg,
                              sizeof(struct custom_enc_cfg_t));
    if (ret != 0) {
        ALOGE("%s: Failed to set APTX HD encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ret = a2dp_set_bit_format(aptx_bt_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = CODEC_TYPE_APTX_HD;
    a2dp.enc_sampling_rate = aptx_bt_cfg->sampling_rate;
    a2dp.enc_channels = aptx_bt_cfg->channels;
    ALOGV("Successfully updated APTX HD encformat with samplingrate: %d channels:%d",
           aptx_dsp_cfg.sample_rate, aptx_dsp_cfg.num_channels);
fail:
    return is_configured;
}

/* API to configure AAC DSP encoder */
bool configure_aac_enc_format(audio_aac_encoder_config *aac_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL;
    struct aac_enc_cfg_t aac_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if (aac_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }
    memset(&aac_dsp_cfg, 0x0, sizeof(struct aac_enc_cfg_t));
    aac_dsp_cfg.enc_format = MEDIA_FMT_AAC;
    aac_dsp_cfg.bit_rate = aac_bt_cfg->bitrate;
    aac_dsp_cfg.sample_rate = aac_bt_cfg->sampling_rate;
    switch (aac_bt_cfg->enc_mode) {
        case 0:
            aac_dsp_cfg.enc_mode = MEDIA_FMT_AAC_AOT_LC;
            break;
        case 2:
            aac_dsp_cfg.enc_mode = MEDIA_FMT_AAC_AOT_PS;
            break;
        case 1:
        default:
            aac_dsp_cfg.enc_mode = MEDIA_FMT_AAC_AOT_SBR;
            break;
    }
    aac_dsp_cfg.aac_fmt_flag = aac_bt_cfg->format_flag;
    aac_dsp_cfg.channel_cfg = aac_bt_cfg->channels;

    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aac_dsp_cfg,
                              sizeof(struct aac_enc_cfg_t));
    if (ret != 0) {
        ALOGE("%s: Failed to set AAC encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ret = a2dp_set_bit_format(aac_bt_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = CODEC_TYPE_AAC;
    a2dp.enc_sampling_rate = aac_bt_cfg->sampling_rate;
    a2dp.enc_channels = aac_bt_cfg->channels;
    ALOGV("%s: Successfully updated AAC enc format with sampling rate: %d channels:%d",
           __func__, aac_dsp_cfg.sample_rate, aac_dsp_cfg.channel_cfg);
fail:
    return is_configured;
}

bool configure_aac_enc_format_v2(audio_aac_encoder_config_v2 *aac_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL;
    struct aac_enc_cfg_v2_t aac_dsp_cfg;
    bool is_configured = false;
    int ret = 0;

    if (aac_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identifed");
        is_configured = false;
        goto fail;
    }
    memset(&aac_dsp_cfg, 0x0, sizeof(struct aac_enc_cfg_v2_t));
    aac_dsp_cfg.aac_enc_cfg.enc_format = MEDIA_FMT_AAC;
    aac_dsp_cfg.aac_enc_cfg.bit_rate = aac_bt_cfg->audio_aac_enc_cfg.bitrate;
    aac_dsp_cfg.aac_enc_cfg.sample_rate = aac_bt_cfg->audio_aac_enc_cfg.sampling_rate;
    switch (aac_bt_cfg->audio_aac_enc_cfg.enc_mode) {
        case 0:
            aac_dsp_cfg.aac_enc_cfg.enc_mode = MEDIA_FMT_AAC_AOT_LC;
            break;
        case 2:
            aac_dsp_cfg.aac_enc_cfg.enc_mode = MEDIA_FMT_AAC_AOT_PS;
            break;
        case 1:
        default:
            aac_dsp_cfg.aac_enc_cfg.enc_mode = MEDIA_FMT_AAC_AOT_SBR;
            break;
    }
    aac_dsp_cfg.aac_enc_cfg.aac_fmt_flag = aac_bt_cfg->audio_aac_enc_cfg.format_flag;
    aac_dsp_cfg.aac_enc_cfg.channel_cfg = aac_bt_cfg->audio_aac_enc_cfg.channels;
    aac_dsp_cfg.frame_ctl.ctl_type = aac_bt_cfg->frame_ctl.ctl_type;
    aac_dsp_cfg.frame_ctl.ctl_value = aac_bt_cfg->frame_ctl.ctl_value;

    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aac_dsp_cfg,
                              sizeof(struct aac_enc_cfg_v2_t));
    if (ret != 0) {
        ALOGE("%s: Failed to set AAC encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ret = a2dp_set_bit_format(aac_bt_cfg->audio_aac_enc_cfg.bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = CODEC_TYPE_AAC;
    a2dp.enc_sampling_rate = aac_bt_cfg->audio_aac_enc_cfg.sampling_rate;
    a2dp.enc_channels = aac_bt_cfg->audio_aac_enc_cfg.channels;
    ALOGV("%s: Successfully updated AAC enc format with sampling rate: %d channels:%d",
           __func__, aac_dsp_cfg.aac_enc_cfg.sample_rate, aac_dsp_cfg.aac_enc_cfg.channel_cfg);
fail:
    return is_configured;
}

bool configure_aac_enc_format_v3(audio_aac_encoder_config_v3 *aac_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL;
    struct aac_enc_cfg_v3_t aac_dsp_cfg;
    struct aac_frame_size_control_t* frame_vbr_ctl = NULL;
    bool is_configured = false;
    int ret = 0;

    if (aac_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identifed");
        is_configured = false;
        goto fail;
    }
    memset(&aac_dsp_cfg, 0x0, sizeof(struct aac_enc_cfg_v3_t));
    aac_dsp_cfg.aac_enc_cfg.enc_format = MEDIA_FMT_AAC;
    aac_dsp_cfg.aac_enc_cfg.bit_rate = aac_bt_cfg->audio_aac_enc_cfg.bitrate;
    aac_dsp_cfg.aac_enc_cfg.sample_rate = aac_bt_cfg->audio_aac_enc_cfg.sampling_rate;
    switch (aac_bt_cfg->audio_aac_enc_cfg.enc_mode) {
        case 0:
            aac_dsp_cfg.aac_enc_cfg.enc_mode = MEDIA_FMT_AAC_AOT_LC;
            break;
        case 2:
            aac_dsp_cfg.aac_enc_cfg.enc_mode = MEDIA_FMT_AAC_AOT_PS;
            break;
        case 1:
        default:
            aac_dsp_cfg.aac_enc_cfg.enc_mode = MEDIA_FMT_AAC_AOT_SBR;
            break;
    }
    aac_dsp_cfg.aac_enc_cfg.aac_fmt_flag = aac_bt_cfg->audio_aac_enc_cfg.format_flag;
    aac_dsp_cfg.aac_enc_cfg.channel_cfg = aac_bt_cfg->audio_aac_enc_cfg.channels;
    aac_dsp_cfg.frame_ctl.ctl_type = aac_bt_cfg->frame_ctl.ctl_type;
    aac_dsp_cfg.frame_ctl.ctl_value = aac_bt_cfg->frame_ctl.ctl_value;
    frame_vbr_ctl =  aac_bt_cfg->frame_ptr_ctl;

    if (frame_vbr_ctl != NULL) {
        aac_dsp_cfg.aac_key_value_ctl.ctl_type = frame_vbr_ctl->ctl_type;
        aac_dsp_cfg.aac_key_value_ctl.ctl_value = frame_vbr_ctl->ctl_value;
    } else {
        ALOGE("%s: VBR cannot be enabled, fall back to default",__func__);
        aac_dsp_cfg.aac_key_value_ctl.ctl_type = 0;
        aac_dsp_cfg.aac_key_value_ctl.ctl_value = 0;
    }

    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aac_dsp_cfg,
                              sizeof(struct aac_enc_cfg_v3_t));
    if (ret != 0) {
        ALOGE("%s: Failed to set AAC encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ret = a2dp_set_bit_format(aac_bt_cfg->audio_aac_enc_cfg.bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = CODEC_TYPE_AAC;
    a2dp.enc_sampling_rate = aac_bt_cfg->audio_aac_enc_cfg.sampling_rate;
    a2dp.enc_channels = aac_bt_cfg->audio_aac_enc_cfg.channels;
    ALOGV("%s: Successfully updated AAC enc format with sampling rate: %d channels:%d",
           __func__, aac_dsp_cfg.aac_enc_cfg.sample_rate, aac_dsp_cfg.aac_enc_cfg.channel_cfg);
fail:
    return is_configured;
}

bool configure_celt_enc_format(audio_celt_encoder_config *celt_bt_cfg)
{
    struct mixer_ctl *ctl_enc_data = NULL;
    struct celt_enc_cfg_t celt_dsp_cfg;
    bool is_configured = false;
    int ret = 0;
    if (celt_bt_cfg == NULL)
        return false;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }
    memset(&celt_dsp_cfg, 0x0, sizeof(struct celt_enc_cfg_t));

    celt_dsp_cfg.custom_cfg.enc_format = MEDIA_FMT_CELT;
    celt_dsp_cfg.custom_cfg.sample_rate = celt_bt_cfg->sampling_rate;
    celt_dsp_cfg.custom_cfg.num_channels = celt_bt_cfg->channels;
    switch(celt_dsp_cfg.custom_cfg.num_channels) {
        case 1:
            celt_dsp_cfg.custom_cfg.channel_mapping[0] = PCM_CHANNEL_C;
            break;
        case 2:
        default:
            celt_dsp_cfg.custom_cfg.channel_mapping[0] = PCM_CHANNEL_L;
            celt_dsp_cfg.custom_cfg.channel_mapping[1] = PCM_CHANNEL_R;
            break;
    }

    celt_dsp_cfg.custom_cfg.custom_size = sizeof(struct celt_enc_cfg_t);

    celt_dsp_cfg.celt_cfg.frame_size = celt_bt_cfg->frame_size;
    celt_dsp_cfg.celt_cfg.complexity = celt_bt_cfg->complexity;
    celt_dsp_cfg.celt_cfg.prediction_mode = celt_bt_cfg->prediction_mode;
    celt_dsp_cfg.celt_cfg.vbr_flag = celt_bt_cfg->vbr_flag;
    celt_dsp_cfg.celt_cfg.bit_rate = celt_bt_cfg->bitrate;

    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&celt_dsp_cfg,
                              sizeof(struct celt_enc_cfg_t));
    if (ret != 0) {
        ALOGE("%s: Failed to set CELT encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ret = a2dp_set_bit_format(celt_bt_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = CODEC_TYPE_CELT;
    a2dp.enc_sampling_rate = celt_bt_cfg->sampling_rate;
    a2dp.enc_channels = celt_bt_cfg->channels;
    ALOGV("Successfully updated CELT encformat with samplingrate: %d channels:%d",
           celt_dsp_cfg.custom_cfg.sample_rate, celt_dsp_cfg.custom_cfg.num_channels);
fail:
    return is_configured;
}

bool configure_ldac_enc_format(audio_ldac_encoder_config *ldac_bt_cfg)
{
    struct mixer_ctl *ldac_enc_data = NULL;
    struct ldac_enc_cfg_t ldac_dsp_cfg;
    bool is_configured = false;
    int ret = 0;
    if (ldac_bt_cfg == NULL)
        return false;

    ldac_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ldac_enc_data) {
        ALOGE(" ERROR  a2dp encoder CONFIG data mixer control not identified");
        is_configured = false;
        goto fail;
    }
    memset(&ldac_dsp_cfg, 0x0, sizeof(struct ldac_enc_cfg_t));

    ldac_dsp_cfg.custom_cfg.enc_format = MEDIA_FMT_LDAC;
    ldac_dsp_cfg.custom_cfg.sample_rate = ldac_bt_cfg->sampling_rate;
    ldac_dsp_cfg.ldac_cfg.channel_mode = ldac_bt_cfg->channel_mode;
    switch(ldac_dsp_cfg.ldac_cfg.channel_mode) {
        case 4:
            ldac_dsp_cfg.custom_cfg.channel_mapping[0] = PCM_CHANNEL_C;
            ldac_dsp_cfg.custom_cfg.num_channels = 1;
            break;
        case 2:
        case 1:
        default:
            ldac_dsp_cfg.custom_cfg.channel_mapping[0] = PCM_CHANNEL_L;
            ldac_dsp_cfg.custom_cfg.channel_mapping[1] = PCM_CHANNEL_R;
            ldac_dsp_cfg.custom_cfg.num_channels = 2;
            break;
    }

    ldac_dsp_cfg.custom_cfg.custom_size = sizeof(struct ldac_enc_cfg_t);
    ldac_dsp_cfg.ldac_cfg.mtu = ldac_bt_cfg->mtu;
    ldac_dsp_cfg.ldac_cfg.bit_rate = ldac_bt_cfg->bit_rate;
    if (ldac_bt_cfg->is_abr_enabled) {
        ldac_dsp_cfg.abr_cfg.mapping_info = ldac_bt_cfg->level_to_bitrate_map;
        ldac_dsp_cfg.abr_cfg.imc_info.direction = IMC_RECEIVE;
        ldac_dsp_cfg.abr_cfg.imc_info.enable = IMC_ENABLE;
        ldac_dsp_cfg.abr_cfg.imc_info.purpose = IMC_PURPOSE_ID_BT_INFO;
        ldac_dsp_cfg.abr_cfg.imc_info.comm_instance = a2dp.abr_config.imc_instance;
        ldac_dsp_cfg.abr_cfg.is_abr_enabled = ldac_bt_cfg->is_abr_enabled;
    }

    ret = mixer_ctl_set_array(ldac_enc_data, (void *)&ldac_dsp_cfg,
                              sizeof(struct ldac_enc_cfg_t));
    if (ret != 0) {
        ALOGE("%s: Failed to set LDAC encoder config", __func__);
        is_configured = false;
        goto fail;
    }
    ret = a2dp_set_bit_format(ldac_bt_cfg->bits_per_sample);
    if (ret != 0) {
        is_configured = false;
        goto fail;
    }
    is_configured = true;
    a2dp.bt_encoder_format = CODEC_TYPE_LDAC;
    a2dp.enc_sampling_rate = ldac_bt_cfg->sampling_rate;
    a2dp.enc_channels = ldac_dsp_cfg.custom_cfg.num_channels;
    a2dp.abr_config.is_abr_enabled = ldac_bt_cfg->is_abr_enabled;
    ALOGV("Successfully updated LDAC encformat with samplingrate: %d channels:%d",
           ldac_dsp_cfg.custom_cfg.sample_rate, ldac_dsp_cfg.custom_cfg.num_channels);
fail:
    return is_configured;
}

bool configure_a2dp_encoder_format()
{
    void *codec_info = NULL;
    uint8_t multi_cast = 0, num_dev = 1;
    codec_t codec_type = CODEC_TYPE_INVALID;
    bool is_configured = false;
    audio_aptx_encoder_config aptx_encoder_cfg;

    if (!a2dp.audio_get_enc_config) {
        ALOGE(" a2dp handle is not identified, ignoring a2dp encoder config");
        return false;
    }
    ALOGD("configure_a2dp_encoder_format start");
    codec_info = a2dp.audio_get_enc_config(&multi_cast, &num_dev,
                               &codec_type);

    // ABR disabled by default for all codecs
    a2dp.abr_config.is_abr_enabled = false;
    a2dp.is_aptx_adaptive = false;

    switch(codec_type) {
        case CODEC_TYPE_SBC:
            ALOGD(" Received SBC encoder supported BT device");
            is_configured =
              configure_sbc_enc_format((audio_sbc_encoder_config *)codec_info);
            break;
        case CODEC_TYPE_APTX:
            ALOGD(" Received APTX encoder supported BT device");
#ifndef LINUX_ENABLED
            a2dp.is_aptx_dual_mono_supported = false;
            aptx_encoder_cfg.default_cfg = (audio_aptx_default_config *)codec_info;
#endif
            is_configured =
              configure_aptx_enc_format(&aptx_encoder_cfg);
            break;
        case CODEC_TYPE_APTX_HD:
            ALOGD(" Received APTX HD encoder supported BT device");
#ifndef LINUX_ENABLED
            is_configured =
              configure_aptx_hd_enc_format((audio_aptx_default_config *)codec_info);
#else
            is_configured =
              configure_aptx_hd_enc_format((audio_aptx_encoder_config *)codec_info);
#endif
            break;
#ifndef LINUX_ENABLED
        case CODEC_TYPE_APTX_DUAL_MONO:
            ALOGD(" Received APTX dual mono encoder supported BT device");
            a2dp.is_aptx_dual_mono_supported = true;
            if (a2dp.audio_is_tws_mono_mode_enable != NULL)
                a2dp.is_tws_mono_mode_on = a2dp.audio_is_tws_mono_mode_enable();
            aptx_encoder_cfg.dual_mono_cfg = (audio_aptx_dual_mono_config *)codec_info;
            is_configured =
              configure_aptx_enc_format(&aptx_encoder_cfg);
            break;
#endif
        case CODEC_TYPE_AAC:
            ALOGD(" Received AAC encoder supported BT device");
            bool is_aac_vbr_enabled =
                    property_get_bool("persist.vendor.bt.aac_vbr_frm_ctl.enabled", false);
            bool is_aac_frame_ctl_enabled =
                    property_get_bool("persist.vendor.bt.aac_frm_ctl.enabled", false);
            if (is_aac_vbr_enabled)
                is_configured = configure_aac_enc_format_v3((audio_aac_encoder_config_v3 *) codec_info);
            else
                is_configured = is_aac_frame_ctl_enabled ?
                                configure_aac_enc_format_v2((audio_aac_encoder_config_v2 *) codec_info) :
                                configure_aac_enc_format((audio_aac_encoder_config *) codec_info);
            break;
        case CODEC_TYPE_CELT:
            ALOGD(" Received CELT encoder supported BT device");
            is_configured =
              configure_celt_enc_format((audio_celt_encoder_config *)codec_info);
            break;
        case CODEC_TYPE_LDAC:
            ALOGD(" Received LDAC encoder supported BT device");
            if (!instance_id || instance_id > MAX_INSTANCE_ID)
                instance_id = MAX_INSTANCE_ID;
            a2dp.abr_config.imc_instance = instance_id--;
            is_configured =
                (configure_ldac_enc_format((audio_ldac_encoder_config *)codec_info) &&
                 configure_a2dp_source_decoder_format(CODEC_TYPE_LDAC));
            break;
#ifndef LINUX_ENABLED //Temporarily disabled for LE, need to take care while doing VT FR
         case CODEC_TYPE_APTX_AD:
             ALOGD(" Received APTX AD encoder supported BT device");
             if (!instance_id || instance_id > MAX_INSTANCE_ID)
                 instance_id = MAX_INSTANCE_ID;
              a2dp.abr_config.imc_instance = instance_id--;
              a2dp.abr_config.is_abr_enabled = true; // for APTX Adaptive ABR is Always on
              a2dp.is_aptx_adaptive = true;
              aptx_encoder_cfg.ad_cfg = (audio_aptx_ad_config *)codec_info;
              is_configured =
                (configure_aptx_enc_format(&aptx_encoder_cfg) &&
                 configure_a2dp_source_decoder_format(MEDIA_FMT_APTX_AD));
            break;
#endif
        case CODEC_TYPE_PCM:
            ALOGD("Received PCM format for BT device");
            a2dp.bt_encoder_format = CODEC_TYPE_PCM;
            is_configured = true;
            break;
        default:
            ALOGD(" Received Unsupported encoder formar");
            is_configured = false;
            break;
    }
    return is_configured;
}

int a2dp_start_playback()
{
    int ret = 0;

    ALOGD("a2dp_start_playback start");

    if (!(a2dp.bt_lib_source_handle && a2dp.audio_source_start
       && a2dp.audio_get_enc_config)) {
        ALOGE("a2dp handle is not identified, Ignoring start playback request");
        return -ENOSYS;
    }

    if (a2dp.a2dp_source_suspended || a2dp.swb_configured) {
        //session will be restarted after suspend completion
        ALOGD("a2dp start requested during suspend state");
        return -ENOSYS;
    }

    if (!a2dp.a2dp_source_started && !a2dp.a2dp_source_total_active_session_requests) {
        ALOGD("calling BT module stream start");
        /* This call indicates BT IPC lib to start playback */
        ret =  a2dp.audio_source_start();
        ALOGE("BT controller start return = %d",ret);
        if (ret != 0 ) {
           ALOGE("BT controller start failed");
           a2dp.a2dp_source_started = false;
           ret = -ETIMEDOUT;
        } else {
           if (configure_a2dp_encoder_format() == true) {
                a2dp.a2dp_source_started = true;
                ret = 0;
                ALOGD("Start playback successful to BT library");
           } else {
                ALOGD(" unable to configure DSP encoder");
                a2dp.a2dp_source_started = false;
                ret = -ETIMEDOUT;
           }
        }
    }

    if (a2dp.a2dp_source_started) {
        a2dp.a2dp_source_total_active_session_requests++;
        a2dp_check_and_set_scrambler();
        audio_a2dp_update_tws_channel_mode();
        a2dp_set_backend_cfg(SOURCE);
        if (a2dp.abr_config.is_abr_enabled)
            start_abr();
    }

    ALOGD("start A2DP playback total active sessions :%d",
          a2dp.a2dp_source_total_active_session_requests);
    return ret;
}

uint64_t a2dp_get_decoder_latency()
{
    uint32_t latency = 0;

    switch(a2dp.bt_decoder_format) {
        case CODEC_TYPE_SBC:
            latency = DEFAULT_SINK_LATENCY_SBC;
            break;
        case CODEC_TYPE_AAC:
            latency = DEFAULT_SINK_LATENCY_AAC;
            break;
        default:
            latency = 200;
            ALOGD("No valid decoder defined, setting latency to %dms", latency);
            break;
    }
    return (uint64_t)latency;
}

bool a2dp_send_sink_setup_complete(void) {
    uint64_t system_latency = 0;
    bool is_complete = false;

    system_latency = a2dp_get_decoder_latency();

    if (a2dp.audio_sink_session_setup_complete(system_latency) == 0) {
        is_complete = true;
    }
    return is_complete;
}

bool a2dp_sink_is_ready()
{
    bool ret = false;

    if ((a2dp.bt_state_sink != A2DP_STATE_DISCONNECTED) &&
        (a2dp.is_a2dp_offload_supported) &&
        (a2dp.audio_sink_check_a2dp_ready))
           ret = a2dp.audio_sink_check_a2dp_ready();
    return ret;
}

int a2dp_start_capture()
{
    int ret = 0;

    ALOGD("a2dp_start_capture start");

    if (!(a2dp.bt_lib_sink_handle && a2dp.audio_sink_start
       && a2dp.audio_get_dec_config)) {
        ALOGE("a2dp handle is not identified, Ignoring start capture request");
        return -ENOSYS;
    }

    if (!a2dp.a2dp_sink_started && !a2dp.a2dp_sink_total_active_session_requests) {
        ALOGD("calling BT module stream start");
        /* This call indicates BT IPC lib to start capture */
        ret =  a2dp.audio_sink_start();
        ALOGE("BT controller start capture return = %d",ret);
        if (ret != 0 ) {
           ALOGE("BT controller start capture failed");
           a2dp.a2dp_sink_started = false;
        } else {

           if (!a2dp_sink_is_ready()) {
                ALOGD("Wait for capture ready not successful");
                ret = -ETIMEDOUT;
           }

           if (configure_a2dp_sink_decoder_format() == true) {
                a2dp.a2dp_sink_started = true;
                ret = 0;
                ALOGD("Start capture successful to BT library");
           } else {
                ALOGD(" unable to configure DSP decoder");
                a2dp.a2dp_sink_started = false;
                ret = -ETIMEDOUT;
           }

           if (!a2dp_send_sink_setup_complete()) {
               ALOGD("sink_setup_complete not successful");
               ret = -ETIMEDOUT;
           }
        }
    }

    if (a2dp.a2dp_sink_started) {
        if (a2dp_set_backend_cfg(SINK) == true) {
            a2dp.a2dp_sink_total_active_session_requests++;
        }
    }

    ALOGD("start A2DP sink total active sessions :%d",
          a2dp.a2dp_sink_total_active_session_requests);
    return ret;
}

static void reset_a2dp_enc_config_params()
{
    int ret =0;

    struct mixer_ctl *ctl_enc_config, *ctl_channel_mode;
    struct sbc_enc_cfg_t dummy_reset_config;
    char* channel_mode;

    memset(&dummy_reset_config, 0x0, sizeof(struct sbc_enc_cfg_t));
    ctl_enc_config = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                           MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_config) {
        ALOGE(" ERROR  a2dp encoder format mixer control not identified");
    } else {
        ret = mixer_ctl_set_array(ctl_enc_config, (void *)&dummy_reset_config,
                                        sizeof(struct sbc_enc_cfg_t));
         a2dp.bt_encoder_format = MEDIA_FMT_NONE;
    }

    a2dp_set_bit_format(DEFAULT_ENCODER_BIT_FORMAT);

    ctl_channel_mode = mixer_get_ctl_by_name(a2dp.adev->mixer,MIXER_FMT_TWS_CHANNEL_MODE);

    if (!ctl_channel_mode) {
        ALOGE("failed to get tws mixer ctl");
    } else {
        channel_mode = "Two";
        if (mixer_ctl_set_enum_by_string(ctl_channel_mode, channel_mode) != 0) {
            ALOGE("%s: Failed to set the channel mode = %s", __func__, channel_mode);
        }
        a2dp.is_tws_mono_mode_on = false;
    }
}

static int reset_a2dp_source_dec_config_params()
{
    struct mixer_ctl *ctl_dec_data = NULL;
    struct abr_dec_cfg_t dummy_reset_cfg;
    int ret = 0;

    ctl_dec_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_SOURCE_DEC_CONFIG_BLOCK);
    if (!ctl_dec_data) {
        ALOGE("%s: ERROR A2DP decoder config mixer control not identifed", __func__);
        return -EINVAL;
    }
    memset(&dummy_reset_cfg, 0x0, sizeof(dummy_reset_cfg));
    ret = mixer_ctl_set_array(ctl_dec_data, (void *)&dummy_reset_cfg,
                              sizeof(dummy_reset_cfg));
    if (ret != 0) {
        ALOGE("%s: Failed to set dummy decoder config", __func__);
        return ret;
    }

    return ret;
}

static void reset_a2dp_sink_dec_config_params()
{
    int ret =0;

    struct mixer_ctl *ctl_dec_config, *ctrl_bit_format;
    struct aac_dec_cfg_t dummy_reset_config;

    memset(&dummy_reset_config, 0x0, sizeof(struct aac_dec_cfg_t));
    ctl_dec_config = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                           MIXER_SINK_DEC_CONFIG_BLOCK);
    if (!ctl_dec_config) {
        ALOGE(" ERROR  a2dp decoder format mixer control not identified");
    } else {
        ret = mixer_ctl_set_array(ctl_dec_config, (void *)&dummy_reset_config,
                                        sizeof(struct aac_dec_cfg_t));
         a2dp.bt_decoder_format = MEDIA_FMT_NONE;
    }
    ctrl_bit_format = mixer_get_ctl_by_name(a2dp.adev->mixer,
                                            MIXER_DEC_BIT_FORMAT);
    if (!ctrl_bit_format) {
        ALOGE(" ERROR  bit format CONFIG data mixer control not identified");
    } else {
        ret = mixer_ctl_set_enum_by_string(ctrl_bit_format, "S16_LE");
        if (ret != 0) {
            ALOGE("%s: Failed to set bit format to decoder", __func__);
        }
    }
}

static void reset_codec_config()
{
    reset_a2dp_enc_config_params();
    reset_a2dp_source_dec_config_params();
    a2dp_reset_backend_cfg(SOURCE);
    if (a2dp.abr_config.is_abr_enabled && a2dp.abr_config.abr_started)
        stop_abr();
    a2dp.abr_config.is_abr_enabled = false;
}

int a2dp_stop_playback()
{
    int ret =0;

    ALOGV("a2dp_stop_playback start");
    if (!(a2dp.bt_lib_source_handle && a2dp.audio_source_stop)) {
        ALOGE("a2dp handle is not identified, Ignoring stop request");
        return -ENOSYS;
    }

    if (a2dp.a2dp_source_total_active_session_requests > 0)
        a2dp.a2dp_source_total_active_session_requests--;
    else
        ALOGE("%s: No active playback session requests on A2DP", __func__);

    if ( a2dp.a2dp_source_started && !a2dp.a2dp_source_total_active_session_requests) {
        ALOGV("calling BT module stream stop");
        ret = a2dp.audio_source_stop();
        if (ret < 0)
            ALOGE("stop stream to BT IPC lib failed");
        else
            ALOGV("stop steam to BT IPC lib successful");
        if (!a2dp.a2dp_source_suspended && !a2dp.swb_configured)
            reset_codec_config();
        a2dp.a2dp_source_started = false;
    }
    if (!a2dp.a2dp_source_total_active_session_requests)
       a2dp.a2dp_source_started = false;
    ALOGD("Stop A2DP playback, total active sessions :%d",
          a2dp.a2dp_source_total_active_session_requests);
    return 0;
}

int a2dp_stop_capture()
{
    int ret =0;

    ALOGV("a2dp_stop_capture start");
    if (!(a2dp.bt_lib_sink_handle && a2dp.audio_sink_stop)) {
        ALOGE("a2dp handle is not identified, Ignoring stop request");
        return -ENOSYS;
    }

    if (a2dp.a2dp_sink_total_active_session_requests > 0)
        a2dp.a2dp_sink_total_active_session_requests--;

    if ( a2dp.a2dp_sink_started && !a2dp.a2dp_sink_total_active_session_requests) {
        ALOGV("calling BT module stream stop");
        ret = a2dp.audio_sink_stop();
        if (ret < 0)
            ALOGE("stop stream to BT IPC lib failed");
        else
            ALOGV("stop steam to BT IPC lib successful");
        reset_a2dp_sink_dec_config_params();
        a2dp_reset_backend_cfg(SINK);
    }
    if (!a2dp.a2dp_sink_total_active_session_requests)
       a2dp.a2dp_source_started = false;
    ALOGD("Stop A2DP capture, total active sessions :%d",
          a2dp.a2dp_sink_total_active_session_requests);
    return 0;
}

int a2dp_set_parameters(struct str_parms *parms, bool *reconfig)
{
    int ret = 0, val, status = 0;
    char value[32] = {0};
    struct audio_usecase *uc_info;
    struct listnode *node;

    if (a2dp.is_a2dp_offload_supported == false) {
        ALOGV("no supported encoders identified,ignoring a2dp setparam");
        status = -EINVAL;
        goto param_handled;
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT, value,
                            sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        if (audio_is_a2dp_out_device(val)) {
            ALOGV("Received device connect request for A2DP source");
            open_a2dp_source();
        }
        goto param_handled;
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value,
                            sizeof(value));

    if (ret >= 0) {
        val = atoi(value);
        if (audio_is_a2dp_out_device(val)) {
            ALOGV("Received source device dis- connect request");
            close_a2dp_output();
            reset_a2dp_enc_config_params();
            reset_a2dp_source_dec_config_params();
            a2dp_reset_backend_cfg(SOURCE);
        } else if (audio_is_a2dp_in_device(val)) {
            ALOGV("Received sink device dis- connect request");
            close_a2dp_input();
            reset_a2dp_sink_dec_config_params();
            a2dp_reset_backend_cfg(SINK);
        }
        goto param_handled;
    }
#ifndef LINUX_ENABLED
    ret = str_parms_get_str(parms, "TwsChannelConfig", value, sizeof(value));
    if (ret >= 0) {
        ALOGD("Setting tws channel mode to %s",value);
        if (!(strncmp(value, "mono", strlen(value))))
            a2dp.is_tws_mono_mode_on = true;
        else if (!(strncmp(value, "dual-mono", strlen(value))))
            a2dp.is_tws_mono_mode_on = false;
        audio_a2dp_update_tws_channel_mode();
        goto param_handled;
    }
#endif
    ret = str_parms_get_str(parms, "A2dpSuspended", value, sizeof(value));
    if (ret >= 0) {
        if (a2dp.bt_lib_source_handle == NULL)
            goto param_handled;

        if ((!strncmp(value, "true", sizeof(value)))) {
            if (a2dp.a2dp_source_suspended) {
                ALOGD("%s: A2DP is already suspended", __func__);
                goto param_handled;
            }
            ALOGD("Setting a2dp to suspend state");
            a2dp.a2dp_source_suspended = true;
            if (a2dp.bt_state_source == A2DP_STATE_DISCONNECTED)
                goto param_handled;
            list_for_each(node, &a2dp.adev->usecase_list) {
                uc_info = node_to_item(node, struct audio_usecase, list);
                if (uc_info->type == PCM_PLAYBACK &&
                    (uc_info->out_snd_device == SND_DEVICE_OUT_BT_A2DP ||
                     uc_info->out_snd_device == SND_DEVICE_OUT_SPEAKER_AND_BT_A2DP ||
                     uc_info->out_snd_device == SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_A2DP)) {
                    fp_check_a2dp_restore_l(a2dp.adev, uc_info->stream.out, false);
                }
            }
            if (!a2dp.swb_configured)
                reset_codec_config();
            if (a2dp.audio_source_suspend)
                a2dp.audio_source_suspend();
        } else if (a2dp.a2dp_source_suspended == true) {
            ALOGD("Resetting a2dp suspend state");
            struct audio_usecase *uc_info;
            struct listnode *node;
            if (a2dp.clear_source_a2dpsuspend_flag)
                a2dp.clear_source_a2dpsuspend_flag();
            a2dp.a2dp_source_suspended = false;
            /*
             * It is possible that before suspend,a2dp sessions can be active
             * for example during music + voice activation concurrency
             * a2dp suspend will be called & BT will change to sco mode
             * though music is paused as a part of voice activation
             * compress session close happens only after pause timeout(10secs)
             * so if resume request comes before pause timeout as a2dp session
             * is already active IPC start will not be called from APM/audio_hw
             * Fix is to call a2dp start for IPC library post suspend
             * based on number of active session count
             */
            if (a2dp.a2dp_source_total_active_session_requests > 0) {
                ALOGD(" Calling IPC lib start post suspend state");
                if (a2dp.audio_source_start) {
                    ret =  a2dp.audio_source_start();
                    if (ret != 0) {
                        ALOGE("BT controller start failed");
                        a2dp.a2dp_source_started = false;
                    }
                }
            }
            list_for_each(node, &a2dp.adev->usecase_list) {
                uc_info = node_to_item(node, struct audio_usecase, list);
                if (uc_info->stream.out && uc_info->type == PCM_PLAYBACK &&
                    is_a2dp_out_device_type(&uc_info->stream.out->device_list)) {
                    fp_check_a2dp_restore_l(a2dp.adev, uc_info->stream.out, true);
                }
            }
        }
        goto param_handled;
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_RECONFIG_A2DP, value,
                            sizeof(value));
    if (ret >= 0) {
        if (a2dp.is_a2dp_offload_supported &&
            a2dp.bt_state_source != A2DP_STATE_DISCONNECTED) {
            *reconfig = true;
        }
        goto param_handled;
    }

param_handled:
    ALOGV("end of a2dp setparam");
    return status;
}

void a2dp_set_handoff_mode(bool is_on)
{
    a2dp.is_handoff_in_progress = is_on;
}

bool a2dp_is_force_device_switch()
{
    //During encoder reconfiguration mode, force a2dp device switch
    // Or if a2dp device is selected but earlier start failed ( as a2dp
    // was suspended, force retry.
    return a2dp.is_handoff_in_progress || !a2dp.a2dp_source_started;
}

void a2dp_get_enc_sample_rate(int *sample_rate)
{
    *sample_rate = a2dp.enc_sampling_rate;
}

void a2dp_get_dec_sample_rate(int *sample_rate)
{
    *sample_rate = a2dp.dec_sampling_rate;
}

bool a2dp_source_is_ready()
{
    bool ret = false;

    if (a2dp.a2dp_source_suspended)
        return ret;

    if ((a2dp.bt_state_source != A2DP_STATE_DISCONNECTED) &&
        (a2dp.is_a2dp_offload_supported) &&
        (a2dp.audio_source_check_a2dp_ready))
           ret = a2dp.audio_source_check_a2dp_ready();
    return ret;
}

bool a2dp_source_is_suspended()
{
    return a2dp.a2dp_source_suspended;
}

void a2dp_init(void *adev,
               a2dp_offload_init_config_t init_config)
{
  a2dp.adev = (struct audio_device*)adev;
  a2dp.bt_lib_source_handle = NULL;
  a2dp.a2dp_source_started = false;
  a2dp.bt_state_source = A2DP_STATE_DISCONNECTED;
  a2dp.a2dp_source_total_active_session_requests = 0;
  a2dp.a2dp_source_suspended = false;
  a2dp.bt_encoder_format = CODEC_TYPE_INVALID;
  a2dp.enc_sampling_rate = 48000;
  a2dp.is_handoff_in_progress = false;
  a2dp.is_aptx_dual_mono_supported = false;
  a2dp.is_aptx_adaptive = false;
  a2dp.abr_config.is_abr_enabled = false;
  a2dp.abr_config.abr_started = false;
  a2dp.abr_config.imc_instance = 0;
  a2dp.abr_config.abr_tx_handle = NULL;
  a2dp.abr_config.abr_rx_handle = NULL;
  a2dp.is_tws_mono_mode_on = false;
  a2dp_source_init();
  a2dp.swb_configured = false;

  // init function pointers
  fp_platform_get_pcm_device_id =
              init_config.fp_platform_get_pcm_device_id;
  fp_check_a2dp_restore_l = init_config.fp_check_a2dp_restore_l;

  reset_a2dp_enc_config_params();
  reset_a2dp_source_dec_config_params();
  reset_a2dp_sink_dec_config_params();

  a2dp.bt_lib_sink_handle = NULL;
  a2dp.a2dp_sink_started = false;
  a2dp.bt_state_sink = A2DP_STATE_DISCONNECTED;
  a2dp.a2dp_sink_total_active_session_requests = 0;

  if (is_running_with_enhanced_fwk == UNINITIALIZED)
      is_running_with_enhanced_fwk = check_if_enhanced_fwk();
  if (is_running_with_enhanced_fwk)
      open_a2dp_sink();

  a2dp.is_a2dp_offload_supported = false;
  update_offload_codec_capabilities();
}

uint32_t a2dp_get_encoder_latency()
{
    uint32_t latency = 0;
    int avsync_runtime_prop = 0;
    int sbc_offset = 0, aptx_offset = 0, aptxhd_offset = 0,
        aac_offset = 0, celt_offset = 0, ldac_offset = 0;
    char value[PROPERTY_VALUE_MAX];

    memset(value, '\0', sizeof(char)*PROPERTY_VALUE_MAX);
    avsync_runtime_prop = property_get(SYSPROP_A2DP_CODEC_LATENCIES, value, NULL);
    if (avsync_runtime_prop > 0) {
        if (sscanf(value, "%d/%d/%d/%d/%d%d",
                  &sbc_offset, &aptx_offset, &aptxhd_offset, &aac_offset, &celt_offset, &ldac_offset) != 6) {
            ALOGI("Failed to parse avsync offset params from '%s'.", value);
            avsync_runtime_prop = 0;
        }
    }

    uint32_t slatency = 0;
    if (a2dp.audio_sink_get_a2dp_latency && a2dp.bt_state_source != A2DP_STATE_DISCONNECTED) {
        slatency = a2dp.audio_sink_get_a2dp_latency();
    }

    switch(a2dp.bt_encoder_format) {
        case CODEC_TYPE_SBC:
            latency = (avsync_runtime_prop > 0) ? sbc_offset : ENCODER_LATENCY_SBC;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_SBC : slatency;
            break;
        case CODEC_TYPE_APTX:
            latency = (avsync_runtime_prop > 0) ? aptx_offset : ENCODER_LATENCY_APTX;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_APTX : slatency;
            break;
        case CODEC_TYPE_APTX_HD:
            latency = (avsync_runtime_prop > 0) ? aptxhd_offset : ENCODER_LATENCY_APTX_HD;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_APTX_HD : slatency;
            break;
        case CODEC_TYPE_AAC:
            latency = (avsync_runtime_prop > 0) ? aac_offset : ENCODER_LATENCY_AAC;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_AAC : slatency;
            break;
        case CODEC_TYPE_CELT:
            latency = (avsync_runtime_prop > 0) ? celt_offset : ENCODER_LATENCY_CELT;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_CELT : slatency;
            break;
        case CODEC_TYPE_LDAC:
            latency = (avsync_runtime_prop > 0) ? ldac_offset : ENCODER_LATENCY_LDAC;
            latency += (slatency <= 0) ? DEFAULT_SINK_LATENCY_LDAC : slatency;
            break;
        case CODEC_TYPE_APTX_AD: // for aptx adaptive the latency depends on the mode (HQ/LL) and
            latency = slatency;      // BT IPC will take care of accomodating the mode factor and return latency
            break;
        case CODEC_TYPE_PCM:
            latency = ENCODER_LATENCY_PCM;
            latency += DEFAULT_SINK_LATENCY_PCM;
            break;
        default:
            latency = 200;
            break;
    }
    return latency;
}

int a2dp_get_parameters(struct str_parms *query,
                                   struct str_parms *reply)
{
    int ret, val = 0;
    char value[32]={0};

    ret = str_parms_get_str(query, AUDIO_PARAMETER_A2DP_RECONFIG_SUPPORTED,
                            value, sizeof(value));
    if (ret >= 0) {
        val = a2dp.is_a2dp_offload_supported;
        str_parms_add_int(reply, AUDIO_PARAMETER_A2DP_RECONFIG_SUPPORTED, val);
        ALOGV("%s: called ... isReconfigA2dpSupported %d", __func__, val);
    }

    return 0;
}


bool configure_aptx_ad_speech_enc_fmt() {
    struct mixer_ctl *ctl_enc_data = NULL;
    int mixer_size = 0;
    int ret = 0;
    struct aptx_ad_speech_enc_cfg_t aptx_dsp_cfg;

    ctl_enc_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_ENC_CONFIG_BLOCK);
    if (!ctl_enc_data) {
        ALOGE(" ERROR a2dp encoder CONFIG data mixer control not identifed");
        return false;
    }

    /* Initialize dsp configuration params */
    memset(&aptx_dsp_cfg, 0x0, sizeof(struct aptx_ad_speech_enc_cfg_t));
    aptx_dsp_cfg.custom_cfg.enc_format = MEDIA_FMT_APTX_AD_SPEECH;
    aptx_dsp_cfg.custom_cfg.sample_rate = SAMPLING_RATE_32K;
    aptx_dsp_cfg.custom_cfg.num_channels = CH_MONO;
    aptx_dsp_cfg.custom_cfg.channel_mapping[0] = PCM_CHANNEL_L;
    aptx_dsp_cfg.imc_info.direction = IMC_RECEIVE;
    aptx_dsp_cfg.imc_info.enable = IMC_ENABLE;
    aptx_dsp_cfg.imc_info.purpose = IMC_PURPOSE_ID_BT_INFO;
    aptx_dsp_cfg.imc_info.comm_instance = APTX_AD_SPEECH_INSTANCE_ID;
    aptx_dsp_cfg.speech_mode.mode = a2dp.adev->swb_speech_mode;
    aptx_dsp_cfg.speech_mode.swapping = SWAP_ENABLE;

    /* Configure AFE DSP configuration */
    mixer_size = sizeof(struct aptx_ad_speech_enc_cfg_t);
    ret = mixer_ctl_set_array(ctl_enc_data, (void *)&aptx_dsp_cfg,
                  mixer_size);
    if (ret != 0) {
        ALOGE("%s: Failed to set SWB encoder config", __func__);
        return false;
    }

    /* Configure AFE Input Bit Format as PCM_16 */
    ret = a2dp_set_bit_format(DEFAULT_ENCODER_BIT_FORMAT);
    if (ret != 0) {
        ALOGE("%s: Failed to set SWB bit format", __func__);
        return false;
    }

    return true;
}

bool configure_aptx_ad_speech_dec_fmt()
{
    struct mixer_ctl *ctl_dec_data = NULL;
    struct aptx_ad_speech_dec_cfg_t dec_cfg;
    int ret = 0;

    ctl_dec_data = mixer_get_ctl_by_name(a2dp.adev->mixer, MIXER_SOURCE_DEC_CONFIG_BLOCK);
    if (!ctl_dec_data) {
        ALOGE("%s: ERROR codec config data mixer control not identifed", __func__);
        return false;
    }
    memset(&dec_cfg, 0x0, sizeof(dec_cfg));
    dec_cfg.abr_cfg.dec_format = MEDIA_FMT_APTX_AD_SPEECH;
    dec_cfg.abr_cfg.imc_info.direction = IMC_TRANSMIT;
    dec_cfg.abr_cfg.imc_info.enable = IMC_ENABLE;
    dec_cfg.abr_cfg.imc_info.purpose = IMC_PURPOSE_ID_BT_INFO;
    dec_cfg.abr_cfg.imc_info.comm_instance = APTX_AD_SPEECH_INSTANCE_ID;
    dec_cfg.speech_mode.mode = a2dp.adev->swb_speech_mode;
    dec_cfg.speech_mode.swapping = SWAP_ENABLE;

    ret = mixer_ctl_set_array(ctl_dec_data, &dec_cfg,
                              sizeof(dec_cfg));
    if (ret != 0) {
        ALOGE("%s: Failed to set decoder config", __func__);
        return false;
    }
      return true;
}

int sco_start_configuration()
{
    ALOGD("sco_start_configuration start");

    if (!a2dp.swb_configured) {
        a2dp.bt_encoder_format = CODEC_TYPE_APTX_AD_SPEECH;
        /* Configure AFE codec*/
        if (configure_aptx_ad_speech_enc_fmt() &&
            configure_aptx_ad_speech_dec_fmt()) {
            ALOGD("%s: SCO enc/dec configured successfully", __func__);
        } else {
            ALOGE("%s: failed to send SCO configuration", __func__);
            return -ETIMEDOUT;
        }
        /* Configure backend*/
        a2dp.enc_sampling_rate = SAMPLING_RATE_96K;
        a2dp.enc_channels = CH_MONO;
        a2dp.abr_config.is_abr_enabled = true;
        a2dp_set_backend_cfg(SOURCE);
        /* Start abr*/
        start_abr();
        a2dp.swb_configured = true;
    }
    return 0;
}

void sco_reset_configuration()
{
    if (a2dp.swb_configured) {
        ALOGD("sco_reset_configuration start");

        reset_codec_config();
        a2dp.bt_encoder_format = CODEC_TYPE_INVALID;
        a2dp.swb_configured = false;
    }
}
