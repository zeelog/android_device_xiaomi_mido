/*
 * Copyright (c) 2013-2020, The Linux Foundation. All rights reserved.
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
 */

#define LOG_TAG "msm8974_platform"
//#define LOG_NDEBUG 0
#define LOG_NDDEBUG 0
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#include <stdlib.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <log/log.h>
#include <cutils/properties.h>
#include <cutils/str_parms.h>
#include <audio_hw.h>
#include <platform_api.h>
#include <pthread.h>
#include <unistd.h>
#include "platform.h"
#include "audio_extn.h"
#include "acdb.h"
#include "voice_extn.h"
#include "edid.h"
#include "sound/compress_params.h"
#include "sound/msmcal-hwdep.h"
#include <dirent.h>

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_PLATFORM
#include <log_utils.h>
#endif

#define SOUND_TRIGGER_DEVICE_HANDSET_MONO_LOW_POWER_ACDB_ID (100)
#define MIXER_FILE_DELIMITER "_"
#define MIXER_FILE_EXT ".xml"

#define MIXER_XML_BASE_STRING "mixer_paths"
#define MIXER_XML_DEFAULT_PATH "mixer_paths.xml"

#define PLATFORM_INFO_XML_PATH_INTCODEC_NAME  "audio_platform_info_intcodec.xml"
#define PLATFORM_INFO_XML_PATH_SKUSH_NAME  "audio_platform_info_skush.xml"
#define PLATFORM_INFO_XML_PATH_SKUW_NAME  "audio_platform_info_skuw.xml"
#define PLATFORM_INFO_XML_PATH_QRD_NAME  "audio_platform_info_qrd.xml"
#define PLATFORM_INFO_XML_PATH_LAGOON_QRD_NAME  "audio_platform_info_lagoon_qrd.xml"
#define PLATFORM_INFO_XML_PATH_NAME "audio_platform_info.xml"
#define MIXER_XML_PATH_AUXPCM_NAME "mixer_paths_auxpcm.xml"
#define MIXER_XML_PATH_I2S_NAME "mixer_paths_i2s.xml"
#define PLATFORM_INFO_XML_PATH_I2S_NAME "audio_platform_info_extcodec.xml"
#define PLATFORM_INFO_XML_PATH_WSA_NAME  "audio_platform_info_wsa.xml"
#define PLATFORM_INFO_XML_PATH_TDM_NAME  "audio_platform_info_tdm.xml"

#include <linux/msm_audio.h>
#if defined (PLATFORM_MSM8998) || (PLATFORM_SDM845) || (PLATFORM_SDM710) || \
    defined (PLATFORM_QCS605) || defined (PLATFORM_MSMNILE) || \
    defined (PLATFORM_KONA) || defined (PLATFORM_MSMSTEPPE) || \
    defined (PLATFORM_QCS405) || defined (PLATFORM_TRINKET) || \
    defined (PLATFORM_LITO) || defined (PLATFORM_MSMFALCON) || \
    defined (PLATFORM_ATOLL) || defined (PLATFORM_BENGAL) || \
    defined (PLATFORM_HOLI)

#include <sound/devdep_params.h>
#endif

#include <resolv.h>

#define QTIME_FREQ_KHZ  19200
#define IPC_ERROR_DELAY 10000

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

#define TOSTRING_(x) #x
#define TOSTRING(x) TOSTRING_(x)

#define LIB_ACDB_LOADER "libacdbloader.so"
#define CVD_VERSION_MIXER_CTL "CVD Version"

#define FLAC_COMPRESS_OFFLOAD_FRAGMENT_SIZE (256 * 1024)
#define MAX_COMPRESS_OFFLOAD_FRAGMENT_SIZE (2 * 1024 * 1024)
#define MIN_COMPRESS_OFFLOAD_FRAGMENT_SIZE (2 * 1024)
#define COMPRESS_OFFLOAD_FRAGMENT_SIZE_FOR_AV_STREAMING (2 * 1024)
#define COMPRESS_OFFLOAD_FRAGMENT_SIZE (32 * 1024)


/*
 * This file will have a maximum of 38 bytes:
 *
 * 4 bytes: number of audio blocks
 * 4 bytes: total length of Short Audio Descriptor (SAD) blocks
 * Maximum 10 * 3 bytes: SAD blocks
 */
#define MAX_SAD_BLOCKS      10
#define SAD_BLOCK_SIZE      3

#define MAX_CVD_VERSION_STRING_SIZE    100
#define MAX_SND_CARD_STRING_SIZE    100

/* EDID format ID for LPCM audio */
#define EDID_FORMAT_LPCM    1

/* fallback app type if the default app type from acdb loader fails */
#define DEFAULT_APP_TYPE_RX_PATH  69936
#define DEFAULT_APP_TYPE_TX_PATH  69938

#define SAMPLE_RATE_8KHZ  8000
#define SAMPLE_RATE_16KHZ 16000

#define MAX_SET_CAL_BYTE_SIZE 65536

/* Mixer path names */
#define AFE_SIDETONE_MIXER_PATH "afe-sidetone"

#define AUDIO_PARAMETER_KEY_SLOWTALK      "st_enable"
#define AUDIO_PARAMETER_KEY_HD_VOICE      "hd_voice"
#define AUDIO_PARAMETER_KEY_VOLUME_BOOST  "volume_boost"
#define AUDIO_PARAMETER_KEY_AUD_CALDATA   "cal_data"
#define AUDIO_PARAMETER_KEY_AUD_CALRESULT "cal_result"
#define AUDIO_PARAMETER_KEY_EC_CAR_STATE  "ec_car_state"

#define AUDIO_PARAMETER_KEY_MONO_SPEAKER "mono_speaker"

#define AUDIO_PARAMETER_KEY_FLUENCE_TYPE        "fluence_type"
#define AUDIO_PARAMETER_KEY_FLUENCE_VOICE_CALL  "fluence_voice"
#define AUDIO_PARAMETER_KEY_FLUENCE_VOICE_REC   "fluence_voice_rec"
#define AUDIO_PARAMETER_KEY_FLUENCE_AUDIO_REC   "fluence_audio_rec"
#define AUDIO_PARAMETER_KEY_FLUENCE_SPEAKER     "fluence_speaker"
#define AUDIO_PARAMETER_KEY_FLUENCE_MODE        "fluence_mode"
#define AUDIO_PARAMETER_KEY_FLUENCE_HFPCALL     "fluence_hfp"
#define AUDIO_PARAMETER_KEY_FLUENCE_TRI_MIC     "fluence_tri_mic"

#define AUDIO_PARAMETER_KEY_PERF_LOCK_OPTS "perf_lock_opts"

/* Reload ACDB files from specified path */
#define AUDIO_PARAMETER_KEY_RELOAD_ACDB "reload_acdb"

/* Query external audio device connection status */
#define AUDIO_PARAMETER_KEY_EXT_AUDIO_DEVICE "ext_audio_device"

/* Query whether it is ok to select display-port as output
 * device for voice usecase
 */
#define AUDIO_PARAMETER_KEY_DP_FOR_VOICE_USECASE "dp_for_voice"
#define AUDIO_PARAMETER_KEY_DP_CHANNEL_MASK "dp_channel_mask"
#define AUDIO_PARAMETER_KEY_SPKR_DEVICE_CHMAP "spkr_device_chmap"
#define AUDIO_PARAMETER_KEY_HFP_ZONE "hfp_zone"

#define EVENT_EXTERNAL_SPK_1 "qc_ext_spk_1"
#define EVENT_EXTERNAL_SPK_2 "qc_ext_spk_2"
#define EVENT_EXTERNAL_MIC   "qc_ext_mic"
#define MAX_CAL_NAME 20
#define MAX_MIME_TYPE_LENGTH 30
#define MAX_SND_CARD_NAME_LENGTH 100

#define GET_IN_DEVICE_INDEX(SND_DEVICE) ((SND_DEVICE) - (SND_DEVICE_IN_BEGIN))

#define is_usb_in_snd_dev(x) \
    (((x) == SND_DEVICE_IN_USB_HEADSET_MIC) ||                           \
    ((x) == SND_DEVICE_IN_USB_HEADSET_MIC_AEC) ||                        \
    ((x) == SND_DEVICE_IN_VOICE_USB_HEADSET_MIC) ||                      \
    ((x) == SND_DEVICE_IN_UNPROCESSED_USB_HEADSET_MIC) ||                \
    ((x) == SND_DEVICE_IN_UNPROCESSED_USB_HEADSET_MIC) ||                \
    ((x) == SND_DEVICE_IN_USB_HEADSET_MULTI_CHANNEL_MIC) ||              \
    ((x) == SND_DEVICE_IN_USB_HEADSET_MULTI_CHANNEL_MIC_AEC) ||          \
    ((x) == SND_DEVICE_IN_UNPROCESSED_USB_HEADSET_MULTI_CHANNEL_MIC) ||  \
    ((x) == SND_DEVICE_IN_VOICE_RECOG_USB_HEADSET_MULTI_CHANNEL_MIC))

#define is_usb_out_snd_dev(x) \
    (((x) == SND_DEVICE_OUT_USB_HEADSET) ||         \
    ((x) == SND_DEVICE_OUT_USB_HEADPHONES) ||       \
    ((x) == SND_DEVICE_OUT_VOICE_USB_HEADPHONES) || \
    ((x) == SND_DEVICE_OUT_VOICE_USB_HEADSET) ||    \
    ((x) == SND_DEVICE_OUT_VOICE_TTY_FULL_USB) ||   \
    ((x) == SND_DEVICE_OUT_VOICE_TTY_VCO_USB))

#ifdef DYNAMIC_LOG_ENABLED
extern void log_utils_init(void);
extern void log_utils_deinit(void);
#endif

char cal_name_info[WCD9XXX_MAX_CAL][MAX_CAL_NAME] = {
        [WCD9XXX_ANC_CAL] = "anc_cal",
        [WCD9XXX_MBHC_CAL] = "mbhc_cal",
        [WCD9XXX_VBAT_CAL] = "vbat_cal",
};
static char *default_rx_backend = NULL;

#define  AUDIO_PARAMETER_IS_HW_DECODER_SESSION_ALLOWED  "is_hw_dec_session_allowed"

char dsp_only_decoders_mime[][MAX_MIME_TYPE_LENGTH] = {
    "audio/x-ms-wma" /* wma*/ ,
    "audio/x-ms-wma-lossless" /* wma lossless */ ,
    "audio/x-ms-wma-pro" /* wma prop */ ,
    "audio/amr-wb-plus" /* amr wb plus */ ,
    "audio/alac"  /*alac */ ,
    "audio/x-ape" /*ape */,
};

enum {
    VOICE_FEATURE_SET_DEFAULT,
    VOICE_FEATURE_SET_VOLUME_BOOST
};

struct audio_block_header
{
    int reserved;
    int length;
};

enum {
    CAL_MODE_SEND           = 0x1,
    CAL_MODE_PERSIST        = 0x2,
    CAL_MODE_RTAC           = 0x4
};

#define PLATFORM_CONFIG_KEY_OPERATOR_INFO "operator_info"
char vendor_config_path[VENDOR_CONFIG_PATH_MAX_LENGTH];
char xml_file_path[VENDOR_CONFIG_FILE_MAX_LENGTH];

struct operator_info {
    struct listnode list;
    char *name;
    char *mccmnc;
};

struct operator_specific_device {
    struct listnode list;
    char *operator;
    char *mixer_path;
    int acdb_id;
};

struct external_specific_device {
    struct listnode list;
    char *usbid;
    int acdb_id;
};

static struct listnode operator_info_list;
static struct listnode *operator_specific_device_table[SND_DEVICE_MAX];

acdb_loader_get_calibration_t acdb_loader_get_calibration;

typedef struct codec_backend_cfg {
    uint32_t sample_rate;
    uint32_t bit_width;
    uint32_t channels;
    uint32_t format;
    char     *bitwidth_mixer_ctl;
    char     *samplerate_mixer_ctl;
    char     *channels_mixer_ctl;
    int      controller;
    int      stream;
} codec_backend_cfg_t;

static native_audio_prop na_props = {0, 0, NATIVE_AUDIO_MODE_INVALID};
static bool supports_true_32_bit = false;

static int max_be_dai_names = 0;
static const struct be_dai_name_struct *be_dai_name_table;

struct snd_device_to_mic_map {
    struct mic_info microphones[AUDIO_MICROPHONE_MAX_COUNT];
    size_t mic_count;
};

static struct listnode *external_specific_device_table[SND_DEVICE_MAX];

struct platform_data {
    struct audio_device *adev;
    bool fluence_in_spkr_mode;
    bool fluence_in_voice_call;
    bool fluence_in_voice_comm;
    bool fluence_in_voice_rec;
    bool fluence_in_audio_rec;
    bool fluence_in_hfp_call;
    bool external_spk_1;
    bool external_spk_2;
    bool external_mic;
    bool speaker_lr_swap;
    bool fluence_sb_enabled;
    bool fluence_nn_enabled;
    int  fluence_type;
    int  fluence_mode;
    char fluence_cap[PROPERTY_VALUE_MAX];
    bool ambisonic_capture;
    bool ambisonic_profile;
    bool slowtalk;
    bool hd_voice;
    bool ec_ref_enabled;
    bool is_wsa_speaker;
    bool hifi_audio;
    bool is_cls_ab_only_supported;
    bool is_i2s_ext_modem;
    bool is_acdb_initialized;
    bool ec_car_state;
    /* Vbat monitor related flags */
    bool is_vbat_speaker;
    bool is_bcl_speaker;
    bool gsm_mode_enabled;
    bool is_slimbus_interface;
    bool is_internal_codec;
    bool is_default_be_config;
    int mono_speaker;
    bool voice_speaker_stereo;
    /* Audio calibration related functions */
    void                       *acdb_handle;
    int                        voice_feature_set;
    acdb_init_t                acdb_init;
    acdb_init_v3_t             acdb_init_v3;
    acdb_init_v4_t             acdb_init_v4;
    acdb_deallocate_t          acdb_deallocate;
    acdb_send_audio_cal_t      acdb_send_audio_cal;
    acdb_send_audio_cal_v3_t   acdb_send_audio_cal_v3;
    acdb_send_audio_cal_v4_t   acdb_send_audio_cal_v4;
    acdb_set_audio_cal_t       acdb_set_audio_cal;
    acdb_get_audio_cal_t       acdb_get_audio_cal;
    acdb_send_voice_cal_t      acdb_send_voice_cal;
    acdb_reload_vocvoltable_t  acdb_reload_vocvoltable;
    acdb_get_default_app_type_t acdb_get_default_app_type;
    acdb_send_common_top_t     acdb_send_common_top;
    acdb_set_codec_data_t      acdb_set_codec_data;
    acdb_reload_t              acdb_reload;
    acdb_reload_v2_t           acdb_reload_v2;
    void *hw_info;
    acdb_send_gain_dep_cal_t   acdb_send_gain_dep_cal;
    struct csd_data *csd;
    struct ext_disp_state {
        void *edid_info;
        bool valid;
        int type;
    } ext_disp[MAX_CONTROLLERS][MAX_STREAMS_PER_CONTROLLER];
    char ec_ref_mixer_path[MIXER_PATH_MAX_LENGTH];
    codec_backend_cfg_t current_backend_cfg[MAX_CODEC_BACKENDS];
    char codec_version[CODEC_VERSION_MAX_LENGTH];
    char codec_variant[CODEC_VARIANT_MAX_LENGTH];
    int hw_dep_fd;
    char cvd_version[MAX_CVD_VERSION_STRING_SIZE];
    char snd_card_name[MAX_SND_CARD_STRING_SIZE];
    int max_vol_index;
    int source_mic_type;
    int max_mic_count;
    bool is_dsd_supported;
    bool is_asrc_supported;
    struct listnode acdb_meta_key_list;
    bool use_generic_handset;
    struct acdb_init_data_v4 acdb_init_data;
    uint32_t declared_mic_count;
    struct audio_microphone_characteristic_t microphones[AUDIO_MICROPHONE_MAX_COUNT];
    struct snd_device_to_mic_map mic_map[SND_DEVICE_MAX];
    struct  spkr_device_chmap *spkr_ch_map;
    bool use_sprk_default_sample_rate;
    bool is_multiple_sample_rate_combo_supported;
    struct listnode custom_mtmx_params_list;
    struct listnode custom_mtmx_in_params_list;
    struct power_mode_cfg power_mode_cfg[SND_DEVICE_MAX];
    struct island_cfg island_cfg[SND_DEVICE_MAX];
};

struct  spkr_device_chmap {
    int num_ch;
    char chmap[AUDIO_CHANNEL_COUNT_MAX];
};

static int pcm_device_table[AUDIO_USECASE_MAX][2] = {
    [USECASE_AUDIO_PLAYBACK_DEEP_BUFFER] = {DEEP_BUFFER_PCM_DEVICE,
                                            DEEP_BUFFER_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_WITH_HAPTICS] = {AUDIO_HAPTICS_PCM_DEVICE,
                                             AUDIO_HAPTICS_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_HAPTICS] = {HAPTICS_PCM_DEVICE,
                                             HAPTICS_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_LOW_LATENCY] = {LOWLATENCY_PCM_DEVICE,
                                           LOWLATENCY_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_ULL]         = {MULTIMEDIA3_PCM_DEVICE,
                                            MULTIMEDIA3_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_MULTI_CH] = {MULTIMEDIA2_PCM_DEVICE,
                                         MULTIMEDIA2_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_HIFI] = {MULTIMEDIA2_PCM_DEVICE,
                                     MULTIMEDIA2_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_TTS] = {MULTIMEDIA2_PCM_DEVICE,
                                        MULTIMEDIA2_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_OFFLOAD] =
                     {PLAYBACK_OFFLOAD_DEVICE, PLAYBACK_OFFLOAD_DEVICE},
    [USECASE_AUDIO_PLAYBACK_OFFLOAD2] =
                     {PLAYBACK_OFFLOAD_DEVICE2, PLAYBACK_OFFLOAD_DEVICE2},
    [USECASE_AUDIO_PLAYBACK_OFFLOAD3] =
                     {PLAYBACK_OFFLOAD_DEVICE3, PLAYBACK_OFFLOAD_DEVICE3},
    [USECASE_AUDIO_PLAYBACK_OFFLOAD4] =
                     {PLAYBACK_OFFLOAD_DEVICE4, PLAYBACK_OFFLOAD_DEVICE4},
    [USECASE_AUDIO_PLAYBACK_OFFLOAD5] =
                     {PLAYBACK_OFFLOAD_DEVICE5, PLAYBACK_OFFLOAD_DEVICE5},
    [USECASE_AUDIO_PLAYBACK_OFFLOAD6] =
                     {PLAYBACK_OFFLOAD_DEVICE6, PLAYBACK_OFFLOAD_DEVICE6},
    [USECASE_AUDIO_PLAYBACK_OFFLOAD7] =
                     {PLAYBACK_OFFLOAD_DEVICE7, PLAYBACK_OFFLOAD_DEVICE7},
    [USECASE_AUDIO_PLAYBACK_OFFLOAD8] =
                     {PLAYBACK_OFFLOAD_DEVICE8, PLAYBACK_OFFLOAD_DEVICE8},
    [USECASE_AUDIO_PLAYBACK_OFFLOAD9] =
                     {PLAYBACK_OFFLOAD_DEVICE9, PLAYBACK_OFFLOAD_DEVICE9},


    [USECASE_AUDIO_RECORD] = {AUDIO_RECORD_PCM_DEVICE, AUDIO_RECORD_PCM_DEVICE},
    [USECASE_AUDIO_RECORD_COMPRESS] = {COMPRESS_CAPTURE_DEVICE, COMPRESS_CAPTURE_DEVICE},
    [USECASE_AUDIO_RECORD_COMPRESS2] = {-1, -1},
    [USECASE_AUDIO_RECORD_COMPRESS3] = {-1, -1},
    [USECASE_AUDIO_RECORD_COMPRESS4] = {-1, -1},
    [USECASE_AUDIO_RECORD_COMPRESS5] = {-1, -1},
    [USECASE_AUDIO_RECORD_COMPRESS6] = {-1, -1},
    [USECASE_AUDIO_RECORD_LOW_LATENCY] = {LOWLATENCY_PCM_DEVICE,
                                          LOWLATENCY_PCM_DEVICE},
    [USECASE_AUDIO_RECORD_FM_VIRTUAL] = {MULTIMEDIA2_PCM_DEVICE,
                                  MULTIMEDIA2_PCM_DEVICE},
    [USECASE_AUDIO_RECORD_HIFI] = {MULTIMEDIA2_PCM_DEVICE,
                                   MULTIMEDIA2_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_FM] = {FM_PLAYBACK_PCM_DEVICE, FM_CAPTURE_PCM_DEVICE},
    [USECASE_AUDIO_HFP_SCO] = {HFP_PCM_RX, HFP_SCO_RX},
    [USECASE_AUDIO_HFP_SCO_WB] = {HFP_PCM_RX, HFP_SCO_RX},
    [USECASE_AUDIO_HFP_SCO_DOWNLINK] = {HFP_ASM_RX_TX, HFP_ASM_RX_TX},
    [USECASE_AUDIO_HFP_SCO_WB_DOWNLINK] = {HFP_ASM_RX_TX, HFP_ASM_RX_TX},
    [USECASE_VOICE_CALL] = {VOICE_CALL_PCM_DEVICE, VOICE_CALL_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_MMAP] = {MMAP_PLAYBACK_PCM_DEVICE,
            MMAP_PLAYBACK_PCM_DEVICE},
    [USECASE_AUDIO_RECORD_MMAP] = {MMAP_RECORD_PCM_DEVICE,
            MMAP_RECORD_PCM_DEVICE},
    [USECASE_VOICE2_CALL] = {VOICE2_CALL_PCM_DEVICE, VOICE2_CALL_PCM_DEVICE},
    [USECASE_VOLTE_CALL] = {VOLTE_CALL_PCM_DEVICE, VOLTE_CALL_PCM_DEVICE},
    [USECASE_QCHAT_CALL] = {QCHAT_CALL_PCM_DEVICE, QCHAT_CALL_PCM_DEVICE},
    [USECASE_VOWLAN_CALL] = {VOWLAN_CALL_PCM_DEVICE, VOWLAN_CALL_PCM_DEVICE},
    [USECASE_VOICEMMODE1_CALL] = {VOICEMMODE1_CALL_PCM_DEVICE,
                                  VOICEMMODE1_CALL_PCM_DEVICE},
    [USECASE_VOICEMMODE2_CALL] = {VOICEMMODE2_CALL_PCM_DEVICE,
                                  VOICEMMODE2_CALL_PCM_DEVICE},
    [USECASE_COMPRESS_VOIP_CALL] = {COMPRESS_VOIP_CALL_PCM_DEVICE, COMPRESS_VOIP_CALL_PCM_DEVICE},
    [USECASE_INCALL_REC_UPLINK] = {AUDIO_RECORD_PCM_DEVICE,
                                   AUDIO_RECORD_PCM_DEVICE},
    [USECASE_INCALL_REC_DOWNLINK] = {AUDIO_RECORD_PCM_DEVICE,
                                     AUDIO_RECORD_PCM_DEVICE},
    [USECASE_INCALL_REC_UPLINK_AND_DOWNLINK] = {AUDIO_RECORD_PCM_DEVICE,
                                                AUDIO_RECORD_PCM_DEVICE},
    [USECASE_INCALL_REC_UPLINK_COMPRESS] = {COMPRESS_CAPTURE_DEVICE,
                                            COMPRESS_CAPTURE_DEVICE},
    [USECASE_INCALL_REC_DOWNLINK_COMPRESS] = {COMPRESS_CAPTURE_DEVICE,
                                              COMPRESS_CAPTURE_DEVICE},
    [USECASE_INCALL_REC_UPLINK_AND_DOWNLINK_COMPRESS] = {COMPRESS_CAPTURE_DEVICE,
                                                         COMPRESS_CAPTURE_DEVICE},
    [USECASE_INCALL_MUSIC_UPLINK] = {INCALL_MUSIC_UPLINK_PCM_DEVICE,
                                     INCALL_MUSIC_UPLINK_PCM_DEVICE},
    [USECASE_INCALL_MUSIC_UPLINK2] = {INCALL_MUSIC_UPLINK2_PCM_DEVICE,
                                      INCALL_MUSIC_UPLINK2_PCM_DEVICE},
    [USECASE_AUDIO_SPKR_CALIB_RX] = {SPKR_PROT_CALIB_RX_PCM_DEVICE, -1},
    [USECASE_AUDIO_SPKR_CALIB_TX] = {-1, SPKR_PROT_CALIB_TX_PCM_DEVICE},

    [USECASE_AUDIO_PLAYBACK_AFE_PROXY] = {AFE_PROXY_PLAYBACK_PCM_DEVICE,
                                          AFE_PROXY_RECORD_PCM_DEVICE},
    [USECASE_AUDIO_RECORD_AFE_PROXY] = {AFE_PROXY_PLAYBACK_PCM_DEVICE,
                                        AFE_PROXY_RECORD_PCM_DEVICE},
    [USECASE_AUDIO_RECORD_AFE_PROXY2] = {AFE_PROXY_RECORD2_PCM_DEVICE,
                                        AFE_PROXY_RECORD2_PCM_DEVICE},
    [USECASE_AUDIO_DSM_FEEDBACK] = {QUAT_MI2S_PCM_DEVICE, QUAT_MI2S_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_SILENCE] = {MULTIMEDIA9_PCM_DEVICE, -1},
    [USECASE_AUDIO_TRANSCODE_LOOPBACK_RX] = {TRANSCODE_LOOPBACK_RX_DEV_ID, -1},
    [USECASE_AUDIO_TRANSCODE_LOOPBACK_TX] = {-1, TRANSCODE_LOOPBACK_TX_DEV_ID},

    [USECASE_AUDIO_PLAYBACK_VOIP] = {AUDIO_PLAYBACK_VOIP_PCM_DEVICE, AUDIO_PLAYBACK_VOIP_PCM_DEVICE},
    [USECASE_AUDIO_RECORD_VOIP] = {AUDIO_RECORD_VOIP_PCM_DEVICE, AUDIO_RECORD_VOIP_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM1] =
                     {PLAYBACK_INTERACTIVE_STRM_DEVICE1, PLAYBACK_INTERACTIVE_STRM_DEVICE1},
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM2] =
                     {PLAYBACK_INTERACTIVE_STRM_DEVICE2, PLAYBACK_INTERACTIVE_STRM_DEVICE2},
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM3] =
                     {PLAYBACK_INTERACTIVE_STRM_DEVICE3, PLAYBACK_INTERACTIVE_STRM_DEVICE3},
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM4] =
                     {PLAYBACK_INTERACTIVE_STRM_DEVICE4, PLAYBACK_INTERACTIVE_STRM_DEVICE4},
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM5] =
                     {PLAYBACK_INTERACTIVE_STRM_DEVICE5, PLAYBACK_INTERACTIVE_STRM_DEVICE5},
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM6] =
                     {PLAYBACK_INTERACTIVE_STRM_DEVICE6, PLAYBACK_INTERACTIVE_STRM_DEVICE6},
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM7] =
                     {PLAYBACK_INTERACTIVE_STRM_DEVICE7, PLAYBACK_INTERACTIVE_STRM_DEVICE7},
    [USECASE_AUDIO_PLAYBACK_INTERACTIVE_STREAM8] =
                     {PLAYBACK_INTERACTIVE_STRM_DEVICE8, PLAYBACK_INTERACTIVE_STRM_DEVICE8},
    [USECASE_AUDIO_EC_REF_LOOPBACK] = {-1, -1}, /* pcm id updated from platform info file */
    [USECASE_AUDIO_PLAYBACK_MEDIA] = {MEDIA_PCM_DEVICE,
                                      MEDIA_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION] = {SYS_NOTIFICATION_PCM_DEVICE,
                                                 SYS_NOTIFICATION_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_NAV_GUIDANCE] = {NAV_GUIDANCE_PCM_DEVICE,
                                             NAV_GUIDANCE_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_PHONE] = {PHONE_PCM_DEVICE,
                                      PHONE_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_FRONT_PASSENGER] = {FRONT_PASSENGER_PCM_DEVICE,
                                                FRONT_PASSENGER_PCM_DEVICE},
    [USECASE_AUDIO_PLAYBACK_REAR_SEAT] = {REAR_SEAT_PCM_DEVICE,
                                          REAR_SEAT_PCM_DEVICE},
    [USECASE_AUDIO_FM_TUNER_EXT] = {-1, -1},
};

/* Array to store sound devices */
static const char * device_table[SND_DEVICE_MAX] = {
    [SND_DEVICE_NONE] = "none",
    /* Playback sound devices */
    [SND_DEVICE_OUT_HANDSET] = "handset",
    [SND_DEVICE_OUT_SPEAKER] = "speaker",
    [SND_DEVICE_OUT_SPEAKER_EXTERNAL_1] = "speaker-ext-1",
    [SND_DEVICE_OUT_SPEAKER_EXTERNAL_2] = "speaker-ext-2",
    [SND_DEVICE_OUT_SPEAKER_WSA] = "wsa-speaker",
    [SND_DEVICE_OUT_SPEAKER_VBAT] = "speaker-vbat",
    [SND_DEVICE_OUT_SPEAKER_REVERSE] = "speaker-reverse",
    [SND_DEVICE_OUT_SPEAKER_SAFE] = "speaker-safe",
    [SND_DEVICE_OUT_HEADPHONES] = "headphones",
    [SND_DEVICE_OUT_HEADPHONES_DSD] = "headphones-dsd",
    [SND_DEVICE_OUT_HEADPHONES_HIFI_FILTER] = "headphones-hifi-filter",
    [SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_HIFI_FILTER] = "speaker-and-headphones-hifi-filter",
    [SND_DEVICE_OUT_HEADPHONES_44_1] = "headphones-44.1",
    [SND_DEVICE_OUT_LINE] = "line",
    [SND_DEVICE_OUT_SPEAKER_AND_HAPTICS] = "speaker-and-haptics",
    [SND_DEVICE_OUT_HEADPHONES_AND_HAPTICS] = "headphones-and-haptics",
    [SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES] = "speaker-and-headphones",
    [SND_DEVICE_OUT_SPEAKER_SAFE_AND_HEADPHONES] = "speaker-safe-and-headphones",
    [SND_DEVICE_OUT_SPEAKER_AND_LINE] = "speaker-and-line",
    [SND_DEVICE_OUT_SPEAKER_SAFE_AND_LINE] = "speaker-safe-and-line",
    [SND_DEVICE_OUT_SPEAKER_HEADPHONES_AND_HAPTICS] = "speaker-headphones-and-haptics",
    [SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_1] = "speaker-and-headphones-ext-1",
    [SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_2] = "speaker-and-headphones-ext-2",
    [SND_DEVICE_OUT_VOICE_HANDSET] = "voice-handset",
    [SND_DEVICE_OUT_VOICE_HAC_HANDSET] = "voice-hac-handset",
    [SND_DEVICE_OUT_VOICE_SPEAKER] = "voice-speaker",
    [SND_DEVICE_OUT_VOICE_SPEAKER_STEREO] = "voice-speaker-stereo",
    [SND_DEVICE_OUT_VOICE_SPEAKER_WSA] = "wsa-voice-speaker",
    [SND_DEVICE_OUT_VOICE_SPEAKER_VBAT] = "voice-speaker-vbat",
    [SND_DEVICE_OUT_VOICE_SPEAKER_2] = "voice-speaker-2",
    [SND_DEVICE_OUT_VOICE_SPEAKER_2_WSA] = "wsa-voice-speaker-2",
    [SND_DEVICE_OUT_VOICE_SPEAKER_2_VBAT] = "voice-speaker-2-vbat",
    [SND_DEVICE_OUT_VOICE_HEADPHONES] = "voice-headphones",
    [SND_DEVICE_OUT_VOICE_HEADSET] = "voice-headset",
    [SND_DEVICE_OUT_VOICE_LINE] = "voice-line",
    [SND_DEVICE_OUT_HDMI] = "hdmi",
    [SND_DEVICE_OUT_SPEAKER_AND_HDMI] = "speaker-and-hdmi",
    [SND_DEVICE_OUT_DISPLAY_PORT] = "display-port",
    [SND_DEVICE_OUT_DISPLAY_PORT1] = "display-port1",
    [SND_DEVICE_OUT_SPEAKER_AND_DISPLAY_PORT] = "speaker-and-display-port",
    [SND_DEVICE_OUT_SPEAKER_AND_DISPLAY_PORT1] = "speaker-and-display-port1",
    [SND_DEVICE_OUT_BT_SCO] = "bt-sco-headset",
    [SND_DEVICE_OUT_BT_SCO_WB] = "bt-sco-headset-wb",
    [SND_DEVICE_OUT_BT_SCO_SWB] = "bt-sco-headset-swb",
    [SND_DEVICE_OUT_BT_A2DP] = "bt-a2dp",
    [SND_DEVICE_OUT_SPEAKER_AND_BT_A2DP] = "speaker-and-bt-a2dp",
    [SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_A2DP] = "speaker-safe-and-bt-a2dp",
    [SND_DEVICE_OUT_SPEAKER_BT_A2DP_AND_HAPTICS] = "speaker-bt-a2dp-and-haptics",
    [SND_DEVICE_OUT_SPEAKER_SAFE_BT_A2DP_AND_HAPTICS] = "speaker-safe-bt-a2dp-and-haptics",
    [SND_DEVICE_OUT_VOICE_HANDSET_TMUS] = "voice-handset-tmus",
    [SND_DEVICE_OUT_VOICE_TTY_FULL_HEADPHONES] = "voice-tty-full-headphones",
    [SND_DEVICE_OUT_VOICE_TTY_FULL_HEADSET] = "voice-tty-full-headset",
    [SND_DEVICE_OUT_VOICE_TTY_VCO_HEADPHONES] = "voice-tty-vco-headphones",
    [SND_DEVICE_OUT_VOICE_TTY_HCO_HANDSET] = "voice-tty-hco-handset",
    [SND_DEVICE_OUT_VOICE_TTY_FULL_USB] = "voice-tty-full-usb",
    [SND_DEVICE_OUT_VOICE_TTY_VCO_USB] = "voice-tty-vco-usb",
    [SND_DEVICE_OUT_VOICE_TX] = "voice-tx",
    [SND_DEVICE_OUT_VOICE_MUSIC_TX] = "voice-music-tx",
    [SND_DEVICE_OUT_AFE_PROXY] = "afe-proxy",
    [SND_DEVICE_OUT_USB_HEADSET] = "usb-headset",
    [SND_DEVICE_OUT_VOICE_USB_HEADSET] = "usb-headset",
    [SND_DEVICE_OUT_USB_HEADPHONES] = "usb-headphones",
    [SND_DEVICE_OUT_USB_HEADSET_SPEC] = "usb-headset",
    [SND_DEVICE_OUT_VOICE_USB_HEADPHONES] = "usb-headphones",
    [SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET] = "speaker-and-usb-headphones",
    [SND_DEVICE_OUT_SPEAKER_SAFE_AND_USB_HEADSET] = "speaker-safe-and-usb-headphones",
    [SND_DEVICE_OUT_SPEAKER_USB_HEADSET_AND_HAPTICS] = "speaker-usb-headphones-and-haptics",
    [SND_DEVICE_OUT_SPEAKER_SAFE_USB_HEADSET_AND_HAPTICS] = "speaker-safe-usb-headphones-and-haptics",
    [SND_DEVICE_OUT_TRANSMISSION_FM] = "transmission-fm",
    [SND_DEVICE_OUT_ANC_HEADSET] = "anc-headphones",
    [SND_DEVICE_OUT_ANC_FB_HEADSET] = "anc-fb-headphones",
    [SND_DEVICE_OUT_VOICE_ANC_HEADSET] = "voice-anc-headphones",
    [SND_DEVICE_OUT_VOICE_ANC_FB_HEADSET] = "voice-anc-fb-headphones",
    [SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_HEADPHONES] = "voice-speaker-and-voice-headphones",
    [SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_ANC_HEADSET] = "voice-speaker-and-voice-anc-headphones",
    [SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_ANC_FB_HEADSET] = "voice-speaker-and-voice-anc-fb-headphones",
    [SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_AND_VOICE_HEADPHONES] = "voice-speaker-stereo-and-voice-headphones",
    [SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_AND_VOICE_ANC_HEADSET] = "voice-speaker-stereo-and-voice-anc-headphones",
    [SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_AND_VOICE_ANC_FB_HEADSET] = "voice-speaker-stereo-and-voice-anc-fb-headphones",
    [SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET] = "speaker-and-anc-headphones",
    [SND_DEVICE_OUT_SPEAKER_AND_ANC_FB_HEADSET] = "speaker-and-anc-fb-headphones",
    [SND_DEVICE_OUT_ANC_HANDSET] = "anc-handset",
    [SND_DEVICE_OUT_SPEAKER_PROTECTED] = "speaker-protected",
    [SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED] = "voice-speaker-protected",
    [SND_DEVICE_OUT_VOICE_SPEAKER_HFP] = "voice-speaker-hfp",
    [SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_PROTECTED] = "voice-speaker-stereo-protected",
    [SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED] = "voice-speaker-2-protected",
    [SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT] = "speaker-protected-vbat",
    [SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED_VBAT] = "voice-speaker-protected-vbat",
    [SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED_VBAT] = "voice-speaker-2-protected-vbat",
    [SND_DEVICE_OUT_SPEAKER_PROTECTED_RAS] = "speaker-protected",
    [SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT_RAS] = "speaker-protected-vbat",
    [SND_DEVICE_OUT_SPEAKER_AND_BT_SCO] = "speaker-and-bt-sco",
    [SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO] = "speaker-safe-and-bt-sco",
    [SND_DEVICE_OUT_SPEAKER_AND_BT_SCO_WB] = "speaker-and-bt-sco-wb",
    [SND_DEVICE_OUT_SPEAKER_AND_BT_SCO_SWB] = "speaker-and-bt-sco-swb",
    [SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO_WB] = "speaker-safe-and-bt-sco-wb",
    [SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO_SWB] = "speaker-safe-and-bt-sco-swb",
    [SND_DEVICE_OUT_SPEAKER_BT_SCO_AND_HAPTICS] = "speaker-bt-sco-and-haptics",
    [SND_DEVICE_OUT_SPEAKER_SAFE_BT_SCO_AND_HAPTICS] = "speaker-safe-bt-sco-and-haptics",
    [SND_DEVICE_OUT_SPEAKER_BT_SCO_WB_AND_HAPTICS] = "speaker-bt-sco-wb-and-haptics",
    [SND_DEVICE_OUT_SPEAKER_BT_SCO_SWB_AND_HAPTICS] = "speaker-bt-sco-swb-and-haptics",
    [SND_DEVICE_OUT_SPEAKER_SAFE_BT_SCO_WB_AND_HAPTICS] = "speaker-safe-bt-sco-wb-and-haptics",
    [SND_DEVICE_OUT_SPEAKER_SAFE_BT_SCO_SWB_AND_HAPTICS] = "speaker-safe-bt-sco-swb-and-haptics",
    [SND_DEVICE_OUT_SPEAKER_WSA_AND_BT_SCO] = "wsa-speaker-and-bt-sco",
    [SND_DEVICE_OUT_SPEAKER_WSA_AND_BT_SCO_WB] = "wsa-speaker-and-bt-sco-wb",
    [SND_DEVICE_OUT_SPEAKER_WSA_AND_BT_SCO_SWB] = "wsa-speaker-and-bt-sco-wb",
    [SND_DEVICE_OUT_VOICE_HEARING_AID] = "hearing-aid",
    [SND_DEVICE_OUT_BUS_MEDIA] = "bus-speaker",
    [SND_DEVICE_OUT_BUS_SYS] = "bus-speaker",
    [SND_DEVICE_OUT_BUS_NAV] = "bus-speaker",
    [SND_DEVICE_OUT_BUS_PHN] = "bus-speaker",
    [SND_DEVICE_OUT_BUS_PAX] = "bus-speaker",
    [SND_DEVICE_OUT_BUS_RSE] = "bus-speaker",
    [SND_DEVICE_OUT_CALL_PROXY] = "call-proxy",

    /* Capture sound devices */
    [SND_DEVICE_IN_HANDSET_MIC] = "handset-mic",
    [SND_DEVICE_IN_HANDSET_MIC_SB] = "handset-mic",
    [SND_DEVICE_IN_HANDSET_MIC_NN] = "handset-mic",
    [SND_DEVICE_IN_HANDSET_MIC_EXTERNAL] = "handset-mic-ext",
    [SND_DEVICE_IN_HANDSET_MIC_AEC] = "handset-mic",
    [SND_DEVICE_IN_HANDSET_MIC_AEC_SB] = "handset-mic",
    [SND_DEVICE_IN_HANDSET_MIC_AEC_NN] = "handset-mic",
    [SND_DEVICE_IN_HANDSET_MIC_NS] = "handset-mic",
    [SND_DEVICE_IN_HANDSET_MIC_NS_SB] = "handset-mic",
    [SND_DEVICE_IN_HANDSET_MIC_NS_NN] = "handset-mic",
    [SND_DEVICE_IN_HANDSET_MIC_AEC_NS] = "handset-mic",
    [SND_DEVICE_IN_HANDSET_MIC_AEC_NS_SB] = "handset-mic",
    [SND_DEVICE_IN_HANDSET_MIC_AEC_NS_NN] = "handset-mic",
    [SND_DEVICE_IN_HANDSET_DMIC] = "dmic-endfire",
    [SND_DEVICE_IN_HANDSET_DMIC_AEC] = "dmic-endfire",
    [SND_DEVICE_IN_HANDSET_DMIC_AEC_SB] = "dmic-endfire",
    [SND_DEVICE_IN_HANDSET_DMIC_NS] = "dmic-endfire",
    [SND_DEVICE_IN_HANDSET_DMIC_NS_SB] = "dmic-endfire",
    [SND_DEVICE_IN_HANDSET_DMIC_AEC_NS] = "dmic-endfire",
    [SND_DEVICE_IN_HANDSET_DMIC_AEC_NS_SB] = "dmic-endfire",
    [SND_DEVICE_IN_SPEAKER_MIC] = "speaker-mic",
    [SND_DEVICE_IN_SPEAKER_MIC_SB] = "speaker-mic",
    [SND_DEVICE_IN_SPEAKER_MIC_NN] = "speaker-mic",
    [SND_DEVICE_IN_SPEAKER_MIC_AEC] = "speaker-mic",
    [SND_DEVICE_IN_SPEAKER_MIC_AEC_SB] = "speaker-mic",
    [SND_DEVICE_IN_SPEAKER_MIC_AEC_NN] = "speaker-mic",
    [SND_DEVICE_IN_SPEAKER_MIC_NS] = "speaker-mic",
    [SND_DEVICE_IN_SPEAKER_MIC_NS_SB] = "speaker-mic",
    [SND_DEVICE_IN_SPEAKER_MIC_NS_NN] = "speaker-mic",
    [SND_DEVICE_IN_SPEAKER_MIC_AEC_NS] = "speaker-mic",
    [SND_DEVICE_IN_SPEAKER_MIC_AEC_NS_SB] = "speaker-mic",
    [SND_DEVICE_IN_SPEAKER_MIC_AEC_NS_NN] = "speaker-mic",
    [SND_DEVICE_IN_SPEAKER_DMIC] = "speaker-dmic-endfire",
    [SND_DEVICE_IN_SPEAKER_DMIC_AEC] = "speaker-dmic-endfire",
    [SND_DEVICE_IN_SPEAKER_DMIC_AEC_SB] = "speaker-dmic-endfire",
    [SND_DEVICE_IN_SPEAKER_DMIC_NS] = "speaker-dmic-endfire",
    [SND_DEVICE_IN_SPEAKER_DMIC_NS_SB] = "speaker-dmic-endfire",
    [SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS] = "speaker-dmic-endfire",
    [SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_SB] = "speaker-dmic-endfire",
    [SND_DEVICE_IN_HEADSET_MIC] = "headset-mic",
    [SND_DEVICE_IN_HEADSET_MIC_AEC] = "headset-mic",
    [SND_DEVICE_IN_HEADSET_MIC_FLUENCE] = "headset-mic",
    [SND_DEVICE_IN_VOICE_SPEAKER_MIC] = "voice-speaker-mic",
    [SND_DEVICE_IN_VOICE_SPEAKER_MIC_SB] = "voice-speaker-mic",
    [SND_DEVICE_IN_VOICE_SPEAKER_MIC_NN] = "voice-speaker-mic",
    [SND_DEVICE_IN_VOICE_HEADSET_MIC] = "voice-headset-mic",
    [SND_DEVICE_IN_SPDIF] = "spdif-in",
    [SND_DEVICE_IN_HDMI_MIC] = "hdmi-in",
    [SND_DEVICE_IN_HDMI_ARC] = "hdmi-arc-in",
    [SND_DEVICE_IN_BT_SCO_MIC] = "bt-sco-mic",
    [SND_DEVICE_IN_BT_SCO_MIC_NREC] = "bt-sco-mic",
    [SND_DEVICE_IN_BT_SCO_MIC_WB] = "bt-sco-mic-wb",
    [SND_DEVICE_IN_BT_SCO_MIC_WB_NREC] = "bt-sco-mic-wb",
    [SND_DEVICE_IN_BT_SCO_MIC_SWB] = "bt-sco-mic-swb",
    [SND_DEVICE_IN_BT_SCO_MIC_SWB_NREC] = "bt-sco-mic-swb",
    [SND_DEVICE_IN_BT_A2DP] = "bt-a2dp-cap",
    [SND_DEVICE_IN_CAMCORDER_LANDSCAPE] = "camcorder-mic",
    [SND_DEVICE_IN_VOICE_DMIC] = "voice-dmic-ef",
    [SND_DEVICE_IN_VOICE_DMIC_SB] = "voice-dmic-ef",
    [SND_DEVICE_IN_VOICE_DMIC_TMUS] = "voice-dmic-ef-tmus",
    [SND_DEVICE_IN_VOICE_SPEAKER_DMIC] = "voice-speaker-dmic-ef",
    [SND_DEVICE_IN_VOICE_SPEAKER_DMIC_SB] = "voice-speaker-dmic-ef",
    [SND_DEVICE_IN_VOICE_SPEAKER_TMIC] = "voice-speaker-tmic",
    [SND_DEVICE_IN_VOICE_SPEAKER_QMIC] = "voice-speaker-qmic",
    [SND_DEVICE_IN_VOICE_SPEAKER_MIC_HFP] = "voice-speaker-mic-hfp",
    [SND_DEVICE_IN_VOICE_SPEAKER_MIC_HFP_MMSECNS] = "voice-speaker-mic-hfp-mmsecns",
    [SND_DEVICE_IN_VOICE_TTY_FULL_HEADSET_MIC] = "voice-tty-full-headset-mic",
    [SND_DEVICE_IN_VOICE_TTY_VCO_HANDSET_MIC] = "voice-tty-vco-handset-mic",
    [SND_DEVICE_IN_VOICE_TTY_HCO_HEADSET_MIC] = "voice-tty-hco-headset-mic",
    [SND_DEVICE_IN_VOICE_TTY_FULL_USB_MIC] = "voice-tty-full-usb-mic",
    [SND_DEVICE_IN_VOICE_TTY_HCO_USB_MIC] = "voice-tty-hco-usb-mic",
    [SND_DEVICE_IN_VOICE_RX] = "voice-rx",

    [SND_DEVICE_IN_VOICE_REC_MIC] = "voice-rec-mic",
    [SND_DEVICE_IN_VOICE_REC_MIC_NS] = "voice-rec-mic",
    [SND_DEVICE_IN_VOICE_REC_MIC_AEC] = "voice-rec-mic",
    [SND_DEVICE_IN_VOICE_REC_MIC_AEC_NS] = "voice-rec-mic",
    [SND_DEVICE_IN_VOICE_REC_DMIC_STEREO] = "voice-rec-dmic-ef",
    [SND_DEVICE_IN_VOICE_REC_DMIC_FLUENCE] = "voice-rec-dmic-ef-fluence",
    [SND_DEVICE_IN_USB_HEADSET_MIC] = "usb-headset-mic",
    [SND_DEVICE_IN_VOICE_USB_HEADSET_MIC] ="usb-headset-mic",
    [SND_DEVICE_IN_USB_HEADSET_MIC_AEC] = "usb-headset-mic",
    [SND_DEVICE_IN_UNPROCESSED_USB_HEADSET_MIC] = "usb-headset-mic",
    [SND_DEVICE_IN_VOICE_RECOG_USB_HEADSET_MIC] = "usb-headset-mic",
    [SND_DEVICE_IN_VOICE_REC_HEADSET_MIC] = "headset-mic",
    [SND_DEVICE_IN_USB_HEADSET_MULTI_CHANNEL_MIC] = "usb-headset-mic",
    [SND_DEVICE_IN_VOICE_RECOG_USB_HEADSET_MULTI_CHANNEL_MIC] = "usb-headset-mic",
    [SND_DEVICE_IN_USB_HEADSET_MULTI_CHANNEL_MIC_AEC] = "usb-headset-mic",
    [SND_DEVICE_IN_UNPROCESSED_USB_HEADSET_MULTI_CHANNEL_MIC] = "usb-headset-mic",
    [SND_DEVICE_IN_CAPTURE_FM] = "capture-fm",
    [SND_DEVICE_IN_AANC_HANDSET_MIC] = "aanc-handset-mic",
    [SND_DEVICE_IN_VOICE_FLUENCE_DMIC_AANC] = "aanc-handset-mic",
    [SND_DEVICE_IN_QUAD_MIC] = "quad-mic",
    [SND_DEVICE_IN_HANDSET_DMIC_STEREO] = "handset-stereo-dmic-ef",
    [SND_DEVICE_IN_SPEAKER_DMIC_STEREO] = "speaker-stereo-dmic-ef",
    [SND_DEVICE_IN_CAPTURE_VI_FEEDBACK] = "vi-feedback",
    [SND_DEVICE_IN_CAPTURE_VI_FEEDBACK_MONO_1] = "vi-feedback-mono-1",
    [SND_DEVICE_IN_CAPTURE_VI_FEEDBACK_MONO_2] = "vi-feedback-mono-2",
    [SND_DEVICE_IN_VOICE_SPEAKER_DMIC_BROADSIDE] = "voice-speaker-dmic-broadside",
    [SND_DEVICE_IN_SPEAKER_DMIC_BROADSIDE] = "speaker-dmic-broadside",
    [SND_DEVICE_IN_SPEAKER_DMIC_AEC_BROADSIDE] = "speaker-dmic-broadside",
    [SND_DEVICE_IN_SPEAKER_DMIC_NS_BROADSIDE] = "speaker-dmic-broadside",
    [SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_BROADSIDE] = "speaker-dmic-broadside",
    [SND_DEVICE_IN_HANDSET_QMIC] = "quad-mic",
    [SND_DEVICE_IN_SPEAKER_QMIC_AEC] = "quad-mic",
    [SND_DEVICE_IN_SPEAKER_QMIC_NS] = "quad-mic",
    [SND_DEVICE_IN_SPEAKER_QMIC_AEC_NS] = "quad-mic",
    [SND_DEVICE_IN_HANDSET_QMIC_AEC] = "quad-mic",
    [SND_DEVICE_IN_VOICE_REC_QMIC_FLUENCE] = "quad-mic",
    [SND_DEVICE_IN_THREE_MIC] = "three-mic",
    [SND_DEVICE_IN_HANDSET_TMIC_FLUENCE_PRO] = "three-mic",
    [SND_DEVICE_IN_HANDSET_TMIC] = "three-mic",
    [SND_DEVICE_IN_HANDSET_TMIC_AEC] = "three-mic",
    [SND_DEVICE_IN_HANDSET_TMIC_NS] = "three-mic",
    [SND_DEVICE_IN_HANDSET_TMIC_AEC_NS] = "three-mic",
    [SND_DEVICE_IN_SPEAKER_TMIC_AEC] = "speaker-tmic",
    [SND_DEVICE_IN_SPEAKER_TMIC_NS] = "speaker-tmic",
    [SND_DEVICE_IN_SPEAKER_TMIC_AEC_NS] = "speaker-tmic",
    [SND_DEVICE_IN_VOICE_REC_TMIC] = "three-mic",
    [SND_DEVICE_IN_UNPROCESSED_MIC] = "unprocessed-mic",
    [SND_DEVICE_IN_UNPROCESSED_STEREO_MIC] = "unprocessed-stereo-mic",
    [SND_DEVICE_IN_UNPROCESSED_THREE_MIC] = "unprocessed-three-mic",
    [SND_DEVICE_IN_UNPROCESSED_QUAD_MIC] = "unprocessed-quad-mic",
    [SND_DEVICE_IN_UNPROCESSED_HEADSET_MIC] = "unprocessed-headset-mic",
    [SND_DEVICE_IN_HANDSET_GENERIC_QMIC] = "quad-mic",
    [SND_DEVICE_IN_INCALL_REC_RX] = "incall-rec-rx",
    [SND_DEVICE_IN_INCALL_REC_TX] = "incall-rec-tx",
    [SND_DEVICE_IN_INCALL_REC_RX_TX] = "incall-rec-rx-tx",
    [SND_DEVICE_IN_LINE] = "line-in",
    [SND_DEVICE_IN_HANDSET_6MIC] = "handset-6mic",
    [SND_DEVICE_IN_HANDSET_8MIC] = "handset-8mic",
    [SND_DEVICE_IN_CAMCORDER_INVERT_LANDSCAPE] = "camcorder-mic",
    [SND_DEVICE_IN_CAMCORDER_PORTRAIT] = "camcorder-mic",
    [SND_DEVICE_IN_CAMCORDER_SELFIE_LANDSCAPE] = "camcorder-mic",
    [SND_DEVICE_IN_CAMCORDER_SELFIE_INVERT_LANDSCAPE] = "camcorder-mic",
    [SND_DEVICE_IN_CAMCORDER_SELFIE_PORTRAIT] = "camcorder-mic",
    [SND_DEVICE_OUT_VOIP_HANDSET] = "voip-handset",
    [SND_DEVICE_OUT_VOIP_SPEAKER] = "voip-speaker",
    [SND_DEVICE_OUT_VOIP_HEADPHONES] = "voip-headphones",
    [SND_DEVICE_IN_VOICE_HEARING_AID] = "hearing-aid-mic",
    [SND_DEVICE_IN_BUS] = "bus-mic",
    [SND_DEVICE_IN_EC_REF_LOOPBACK] = "ec-ref-loopback",
    [SND_DEVICE_IN_HANDSET_DMIC_AND_EC_REF_LOOPBACK] = "handset-dmic-and-ec-ref-loopback",
    [SND_DEVICE_IN_HANDSET_QMIC_AND_EC_REF_LOOPBACK] = "handset-qmic-and-ec-ref-loopback",
    [SND_DEVICE_IN_HANDSET_6MIC_AND_EC_REF_LOOPBACK] = "handset-6mic-and-ec-ref-loopback",
    [SND_DEVICE_IN_HANDSET_8MIC_AND_EC_REF_LOOPBACK] = "handset-8mic-and-ec-ref-loopback",
    [SND_DEVICE_IN_CALL_PROXY] = "call-proxy-in",
};

// Platform specific backend bit width table
static int backend_bit_width_table[SND_DEVICE_MAX] = {0};

static struct audio_effect_config effect_config_table[GET_IN_DEVICE_INDEX(SND_DEVICE_MAX)][EFFECT_MAX] = {
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_QMIC_AEC_NS)][EFFECT_AEC] = {TX_VOICE_FLUENCE_PROV2, 0x0, 0x10EAF, 0x01},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_QMIC_AEC_NS)][EFFECT_NS] = {TX_VOICE_FLUENCE_PROV2,  0x0, 0x10EAF, 0x02},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_TMIC_AEC_NS)][EFFECT_AEC] = {TX_VOICE_TM_FLUENCE_PRO_VC, 0x0, 0x10EAF, 0x01},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_TMIC_AEC_NS)][EFFECT_NS] = {TX_VOICE_TM_FLUENCE_PRO_VC,  0x0, 0x10EAF, 0x02},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_BROADSIDE)][EFFECT_AEC] = {TX_VOICE_DM_FV5_BROADSIDE, 0x0,
                                                                 0x10EAF, 0x01},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_BROADSIDE)][EFFECT_NS] = {TX_VOICE_DM_FV5_BROADSIDE, 0x0,
                                                                0x10EAF, 0x02},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS)][EFFECT_AEC] = {TX_VOICE_FV5ECNS_DM, 0x0, 0x10EAF, 0x01},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS)][EFFECT_NS] = {TX_VOICE_FV5ECNS_DM, 0x0, 0x10EAF, 0x02},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_MIC)][EFFECT_AEC] = {TX_VOICE_SMECNS_V2, 0x0, 0x10EAF, 0x01},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_MIC)][EFFECT_NS] = {TX_VOICE_SMECNS_V2, 0x0, 0x10EAF, 0x02},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_HANDSET_TMIC_AEC_NS)][EFFECT_AEC] = {TX_VOICE_TM_FLUENCE_EF, 0x8000, 0x10EAF, 0x01},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_HANDSET_TMIC_AEC_NS)][EFFECT_NS] = {TX_VOICE_TM_FLUENCE_EF, 0x8000, 0x10EAF, 0x02},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_HANDSET_DMIC_AEC_NS)][EFFECT_AEC] = {TX_VOICE_FV5ECNS_DM, 0x0, 0x10EAF, 0x01},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_HANDSET_DMIC_AEC_NS)][EFFECT_NS] = {TX_VOICE_FV5ECNS_DM, 0x0, 0x10EAF, 0x02},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_HANDSET_MIC)][EFFECT_AEC] = {TX_VOICE_SMECNS_V2, 0x0, 0x10EAF, 0x01},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_HANDSET_MIC)][EFFECT_NS] = {TX_VOICE_SMECNS_V2, 0x0, 0x10EAF, 0x02},

    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_SB)][EFFECT_AEC] = {TX_VOICE_FLUENCE_SM_SB, 0x8000, 0x10EAF, 0x01},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_SB)][EFFECT_NS] = {TX_VOICE_FLUENCE_SM_SB, 0x8000, 0x10EAF, 0x02},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_MIC_SB)][EFFECT_AEC] = {TX_VOICE_FLUENCE_SM_SB, 0x8000, 0x10EAF, 0x01},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_MIC_SB)][EFFECT_NS] = {TX_VOICE_FLUENCE_SM_SB, 0x8000, 0x10EAF, 0x02},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_HANDSET_DMIC_AEC_NS_SB)][EFFECT_AEC] = {TX_VOICE_FLUENCE_SM_SB, 0x8000, 0x10EAF, 0x01},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_HANDSET_DMIC_AEC_NS_SB)][EFFECT_NS] = {TX_VOICE_FLUENCE_MM_SB, 0x8000, 0x10EAF, 0x02},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_HANDSET_MIC_SB)][EFFECT_AEC] = {TX_VOICE_FLUENCE_SM_SB, 0x8000, 0x10EAF, 0x01},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_HANDSET_MIC_SB)][EFFECT_NS] = {TX_VOICE_FLUENCE_SM_SB, 0x8000, 0x10EAF, 0x02},

    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_MIC_NN)][EFFECT_AEC] = {TX_VOICE_FLUENCE_SM_NN, 0x8000, 0x10EAF, 0x01},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_SPEAKER_MIC_NN)][EFFECT_NS] = {TX_VOICE_FLUENCE_SM_NN, 0x8000, 0x10EAF, 0x02},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_HANDSET_MIC_NN)][EFFECT_AEC] = {TX_VOICE_FLUENCE_SM_NN, 0x8000, 0x10EAF, 0x01},
    [GET_IN_DEVICE_INDEX(SND_DEVICE_IN_HANDSET_MIC_NN)][EFFECT_NS] = {TX_VOICE_FLUENCE_SM_NN, 0x8000, 0x10EAF, 0x02},
};

static struct audio_fluence_mmsecns_config fluence_mmsecns_table = {TOPOLOGY_ID_MM_HFP_ECNS, MODULE_ID_MM_HFP_ECNS,
                                                                    INSTANCE_ID_MM_HFP_ECNS, PARAM_ID_MM_HFP_ZONE};

/* ACDB IDs (audio DSP path configuration IDs) for each sound device */
static int acdb_device_table[SND_DEVICE_MAX] = {
    [SND_DEVICE_NONE] = -1,
    [SND_DEVICE_OUT_HANDSET] = 7,
    [SND_DEVICE_OUT_SPEAKER] = 14,
    [SND_DEVICE_OUT_SPEAKER_EXTERNAL_1] = 130,
    [SND_DEVICE_OUT_SPEAKER_EXTERNAL_2] = 130,
    [SND_DEVICE_OUT_SPEAKER_WSA] = 135,
    [SND_DEVICE_OUT_SPEAKER_VBAT] = 14,
    [SND_DEVICE_OUT_SPEAKER_REVERSE] = 14,
    [SND_DEVICE_OUT_SPEAKER_SAFE] = 14,
    [SND_DEVICE_OUT_LINE] = 10,
    [SND_DEVICE_OUT_HEADPHONES] = 10,
    [SND_DEVICE_OUT_HEADPHONES_DSD] = 10,
    [SND_DEVICE_OUT_HEADPHONES_44_1] = 10,
    [SND_DEVICE_OUT_HEADPHONES_AND_HAPTICS] = 10,
    [SND_DEVICE_OUT_SPEAKER_AND_HAPTICS] = 10,
    [SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES] = 10,
    [SND_DEVICE_OUT_SPEAKER_SAFE_AND_HEADPHONES] = 10,
    [SND_DEVICE_OUT_SPEAKER_HEADPHONES_AND_HAPTICS] = 10,
    [SND_DEVICE_OUT_SPEAKER_SAFE_HEADPHONES_AND_HAPTICS] = 10,
    [SND_DEVICE_OUT_SPEAKER_AND_LINE] = 10,
    [SND_DEVICE_OUT_SPEAKER_SAFE_AND_LINE] = 10,
    [SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_1] = 130,
    [SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_2] = 130,
    [SND_DEVICE_OUT_VOICE_HANDSET] = 7,
    [SND_DEVICE_OUT_VOICE_SPEAKER] = 14,
    [SND_DEVICE_OUT_VOICE_SPEAKER_STEREO] = 15,
    [SND_DEVICE_OUT_VOICE_SPEAKER_VBAT] = 14,
    [SND_DEVICE_OUT_VOICE_SPEAKER_2] = 14,
    [SND_DEVICE_OUT_VOICE_SPEAKER_WSA] = 135,
    [SND_DEVICE_OUT_VOICE_SPEAKER_2_WSA] = 135,
    [SND_DEVICE_OUT_VOICE_SPEAKER_2_VBAT] = 14,
    [SND_DEVICE_OUT_VOICE_HAC_HANDSET] = 53,
    [SND_DEVICE_OUT_VOICE_HEADPHONES] = 10,
    [SND_DEVICE_OUT_VOICE_HEADSET] = 10,
    [SND_DEVICE_OUT_VOICE_LINE] = 10,
    [SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_HEADPHONES] = 10,
    [SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_ANC_HEADSET] = 10,
    [SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_ANC_FB_HEADSET] = 10,
    [SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_AND_VOICE_HEADPHONES] = 10,
    [SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_AND_VOICE_ANC_HEADSET] = 10,
    [SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_AND_VOICE_ANC_FB_HEADSET] = 10,
    [SND_DEVICE_OUT_HDMI] = 18,
    [SND_DEVICE_OUT_SPEAKER_AND_HDMI] = 14,
    [SND_DEVICE_OUT_DISPLAY_PORT] = 18,
    [SND_DEVICE_OUT_DISPLAY_PORT1] = 18,
    [SND_DEVICE_OUT_SPEAKER_AND_DISPLAY_PORT] = 14,
    [SND_DEVICE_OUT_SPEAKER_AND_DISPLAY_PORT1] = 14,
    [SND_DEVICE_OUT_SPEAKER_DISPLAY_PORT_AND_HAPTICS] = 14,
    [SND_DEVICE_OUT_SPEAKER_DISPLAY_PORT1_AND_HAPTICS] = 14,
    [SND_DEVICE_OUT_BT_SCO] = 22,
    [SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO] = 14,
    [SND_DEVICE_OUT_SPEAKER_SAFE_BT_SCO_AND_HAPTICS] = 14,
    [SND_DEVICE_OUT_BT_SCO_WB] = 39,
    [SND_DEVICE_OUT_BT_SCO_SWB] = 39,
    [SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO_WB] = 14,
    [SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO_SWB] = 14,
    [SND_DEVICE_OUT_SPEAKER_SAFE_BT_SCO_WB_AND_HAPTICS] = 14,
    [SND_DEVICE_OUT_SPEAKER_SAFE_BT_SCO_SWB_AND_HAPTICS] = 14,
    [SND_DEVICE_OUT_BT_A2DP] = 20,
    [SND_DEVICE_OUT_SPEAKER_AND_BT_A2DP] = 14,
    [SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_A2DP] = 14,
    [SND_DEVICE_OUT_SPEAKER_BT_A2DP_AND_HAPTICS] = 14,
    [SND_DEVICE_OUT_SPEAKER_SAFE_BT_A2DP_AND_HAPTICS] = 14,
    [SND_DEVICE_OUT_VOICE_HANDSET_TMUS] = 88,
    [SND_DEVICE_OUT_VOICE_TTY_FULL_HEADPHONES] = 17,
    [SND_DEVICE_OUT_VOICE_TTY_FULL_HEADSET] = 17,
    [SND_DEVICE_OUT_VOICE_TTY_VCO_HEADPHONES] = 17,
    [SND_DEVICE_OUT_VOICE_TTY_HCO_HANDSET] = 37,
    [SND_DEVICE_OUT_VOICE_TTY_FULL_USB] = 17,
    [SND_DEVICE_OUT_VOICE_TTY_VCO_USB] = 17,
    [SND_DEVICE_OUT_VOICE_TX] = 45,
    [SND_DEVICE_OUT_VOICE_MUSIC_TX] = 3,
    [SND_DEVICE_OUT_AFE_PROXY] = 0,
    [SND_DEVICE_OUT_USB_HEADSET] = 45,
    [SND_DEVICE_OUT_VOICE_USB_HEADSET] = 45,
    [SND_DEVICE_OUT_USB_HEADPHONES] = 45,
    [SND_DEVICE_OUT_USB_HEADSET_SPEC] = 45,
    [SND_DEVICE_OUT_VOICE_USB_HEADPHONES] = 45,
    [SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET] = 14,
    [SND_DEVICE_OUT_SPEAKER_SAFE_AND_USB_HEADSET] = 14,
    [SND_DEVICE_OUT_SPEAKER_USB_HEADSET_AND_HAPTICS] = 14,
    [SND_DEVICE_OUT_SPEAKER_SAFE_USB_HEADSET_AND_HAPTICS] = 14,
    [SND_DEVICE_OUT_TRANSMISSION_FM] = 0,
    [SND_DEVICE_OUT_ANC_HEADSET] = 26,
    [SND_DEVICE_OUT_ANC_FB_HEADSET] = 27,
    [SND_DEVICE_OUT_HEADPHONES_HIFI_FILTER] = 188,
    [SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_HIFI_FILTER] = 188,
    [SND_DEVICE_OUT_VOICE_ANC_HEADSET] = 26,
    [SND_DEVICE_OUT_VOICE_ANC_FB_HEADSET] = 27,
    [SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET] = 26,
    [SND_DEVICE_OUT_SPEAKER_AND_ANC_FB_HEADSET] = 27,
    [SND_DEVICE_OUT_ANC_HANDSET] = 103,
    [SND_DEVICE_OUT_SPEAKER_PROTECTED] = 124,
    [SND_DEVICE_OUT_VOICE_SPEAKER_HFP] = 14,
    [SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED] = 101,
    [SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED] = 101,
    [SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT] = 124,
    [SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED_VBAT] = 101,
    [SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED_VBAT] = 101,
    [SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_PROTECTED] = 124,
    [SND_DEVICE_OUT_SPEAKER_PROTECTED_RAS] = 134,
    [SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT_RAS] = 134,
    [SND_DEVICE_OUT_VOICE_HEARING_AID] = 45,
    [SND_DEVICE_OUT_BUS_MEDIA] = 60,
    [SND_DEVICE_OUT_BUS_SYS] = 60,
    [SND_DEVICE_OUT_BUS_NAV] = 14,
    [SND_DEVICE_OUT_BUS_PHN] = 94,
    [SND_DEVICE_OUT_BUS_PAX] = 60,
    [SND_DEVICE_OUT_BUS_RSE] = 60,
    [SND_DEVICE_OUT_CALL_PROXY] = 32,
    [SND_DEVICE_IN_HANDSET_MIC] = 4,
    [SND_DEVICE_IN_HANDSET_MIC_SB] = 163,
    [SND_DEVICE_IN_HANDSET_MIC_NN] = 183,
    [SND_DEVICE_IN_HANDSET_MIC_EXTERNAL] = 4,
    [SND_DEVICE_IN_HANDSET_MIC_AEC] = 106,
    [SND_DEVICE_IN_HANDSET_MIC_AEC_SB] = 164,
    [SND_DEVICE_IN_HANDSET_MIC_AEC_NN] = 184,
    [SND_DEVICE_IN_HANDSET_MIC_NS] = 107,
    [SND_DEVICE_IN_HANDSET_MIC_NS_SB] = 165,
    [SND_DEVICE_IN_HANDSET_MIC_NS_NN] = 185,
    [SND_DEVICE_IN_HANDSET_MIC_AEC_NS] = 108,
    [SND_DEVICE_IN_HANDSET_MIC_AEC_NS_SB] = 166,
    [SND_DEVICE_IN_HANDSET_MIC_AEC_NS_NN] = 186,
    [SND_DEVICE_IN_HANDSET_DMIC] = 41,
    [SND_DEVICE_IN_HANDSET_DMIC_AEC] = 109,
    [SND_DEVICE_IN_HANDSET_DMIC_AEC_SB] = 168,
    [SND_DEVICE_IN_HANDSET_DMIC_NS] = 110,
    [SND_DEVICE_IN_HANDSET_DMIC_NS_SB] = 169,
    [SND_DEVICE_IN_HANDSET_DMIC_AEC_NS] = 111,
    [SND_DEVICE_IN_HANDSET_DMIC_AEC_NS_SB] = 170,
    [SND_DEVICE_IN_SPEAKER_MIC] = 11,
    [SND_DEVICE_IN_SPEAKER_MIC_SB] = 171,
    [SND_DEVICE_IN_SPEAKER_MIC_NN] = 187,
    [SND_DEVICE_IN_SPEAKER_MIC_AEC] = 112,
    [SND_DEVICE_IN_SPEAKER_MIC_AEC_SB] = 172,
    [SND_DEVICE_IN_SPEAKER_MIC_AEC_NN] = 189,
    [SND_DEVICE_IN_SPEAKER_MIC_NS] = 113,
    [SND_DEVICE_IN_SPEAKER_MIC_NS_SB] = 173,
    [SND_DEVICE_IN_SPEAKER_MIC_NS_NN] = 190,
    [SND_DEVICE_IN_SPEAKER_MIC_AEC_NS] = 114,
    [SND_DEVICE_IN_SPEAKER_MIC_AEC_NS_SB] = 174,
    [SND_DEVICE_IN_SPEAKER_MIC_AEC_NS_NN] = 192,
    [SND_DEVICE_IN_SPEAKER_DMIC] = 43,
    [SND_DEVICE_IN_SPEAKER_DMIC_AEC] = 115,
    [SND_DEVICE_IN_SPEAKER_DMIC_AEC_SB] = 176,
    [SND_DEVICE_IN_SPEAKER_DMIC_NS] = 116,
    [SND_DEVICE_IN_SPEAKER_DMIC_NS_SB] = 177,
    [SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS] = 117,
    [SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_SB] = 178,
    [SND_DEVICE_IN_HEADSET_MIC] = 8,
    [SND_DEVICE_IN_HEADSET_MIC_AEC] = 8,
    [SND_DEVICE_IN_HEADSET_MIC_FLUENCE] = 47,
    [SND_DEVICE_IN_VOICE_SPEAKER_MIC] = 11,
    [SND_DEVICE_IN_VOICE_SPEAKER_MIC_SB] = 171,
    [SND_DEVICE_IN_VOICE_SPEAKER_MIC_NN] = 187,
    [SND_DEVICE_IN_VOICE_SPEAKER_MIC_HFP] = 11,
    [SND_DEVICE_IN_VOICE_SPEAKER_MIC_HFP_MMSECNS] = 131,
    [SND_DEVICE_IN_VOICE_HEADSET_MIC] = 8,
    [SND_DEVICE_IN_SPDIF] = 143,
    [SND_DEVICE_IN_HDMI_MIC] = 143,
    [SND_DEVICE_IN_HDMI_ARC] = 143,
    [SND_DEVICE_IN_BT_SCO_MIC] = 21,
    [SND_DEVICE_IN_BT_SCO_MIC_NREC] = 122,
    [SND_DEVICE_IN_BT_SCO_MIC_WB] = 38,
    [SND_DEVICE_IN_BT_SCO_MIC_WB_NREC] = 123,
    [SND_DEVICE_IN_BT_SCO_MIC_SWB] = 38,
    [SND_DEVICE_IN_BT_SCO_MIC_SWB_NREC] = 123,
    [SND_DEVICE_IN_BT_A2DP] = 21,
    [SND_DEVICE_IN_CAMCORDER_LANDSCAPE] = 4,
    [SND_DEVICE_IN_VOICE_DMIC] = 41,
    [SND_DEVICE_IN_VOICE_DMIC_SB] = 167,
    [SND_DEVICE_IN_VOICE_DMIC_TMUS] = 89,
    [SND_DEVICE_IN_VOICE_SPEAKER_DMIC] = 43,
    [SND_DEVICE_IN_VOICE_SPEAKER_DMIC_SB] = 175,
    [SND_DEVICE_IN_VOICE_SPEAKER_TMIC] = 161,
    [SND_DEVICE_IN_VOICE_SPEAKER_QMIC] = 19,
    [SND_DEVICE_IN_VOICE_TTY_FULL_HEADSET_MIC] = 16,
    [SND_DEVICE_IN_VOICE_TTY_VCO_HANDSET_MIC] = 36,
    [SND_DEVICE_IN_VOICE_TTY_HCO_HEADSET_MIC] = 16,
    [SND_DEVICE_IN_VOICE_TTY_FULL_USB_MIC] = 16,
    [SND_DEVICE_IN_VOICE_TTY_HCO_USB_MIC] = 16,
    [SND_DEVICE_IN_VOICE_RX] = 44,

    [SND_DEVICE_IN_VOICE_REC_MIC] = 4,
    [SND_DEVICE_IN_VOICE_REC_MIC_NS] = 107,
    [SND_DEVICE_IN_VOICE_REC_MIC_AEC] = 112,
    [SND_DEVICE_IN_VOICE_REC_MIC_AEC_NS] = 114,
    [SND_DEVICE_IN_VOICE_REC_DMIC_STEREO] = 34,
    [SND_DEVICE_IN_VOICE_REC_DMIC_FLUENCE] = 41,
    [SND_DEVICE_IN_VOICE_REC_HEADSET_MIC] = 8,
    [SND_DEVICE_IN_USB_HEADSET_MIC] = 44,
    [SND_DEVICE_IN_VOICE_USB_HEADSET_MIC] = 44,
    [SND_DEVICE_IN_UNPROCESSED_USB_HEADSET_MIC] = 44,
    [SND_DEVICE_IN_VOICE_RECOG_USB_HEADSET_MIC] = 44,
    [SND_DEVICE_IN_USB_HEADSET_MIC_AEC] = 44,
    [SND_DEVICE_IN_USB_HEADSET_MULTI_CHANNEL_MIC] = 162,
    [SND_DEVICE_IN_VOICE_RECOG_USB_HEADSET_MULTI_CHANNEL_MIC] = 162,
    [SND_DEVICE_IN_USB_HEADSET_MULTI_CHANNEL_MIC_AEC] = 162,
    [SND_DEVICE_IN_UNPROCESSED_USB_HEADSET_MULTI_CHANNEL_MIC] = 162,
    [SND_DEVICE_IN_CAPTURE_FM] = 0,
    [SND_DEVICE_IN_AANC_HANDSET_MIC] = 104,
    [SND_DEVICE_IN_VOICE_FLUENCE_DMIC_AANC] = 105,
    [SND_DEVICE_IN_QUAD_MIC] = 46,
    [SND_DEVICE_IN_HANDSET_DMIC_STEREO] = 34,
    [SND_DEVICE_IN_SPEAKER_DMIC_STEREO] = 35,
    [SND_DEVICE_IN_CAPTURE_VI_FEEDBACK] = 102,
    [SND_DEVICE_IN_CAPTURE_VI_FEEDBACK_MONO_1] = 102,
    [SND_DEVICE_IN_CAPTURE_VI_FEEDBACK_MONO_2] = 102,
    [SND_DEVICE_IN_VOICE_SPEAKER_DMIC_BROADSIDE] = 12,
    [SND_DEVICE_IN_SPEAKER_DMIC_BROADSIDE] = 12,
    [SND_DEVICE_IN_SPEAKER_DMIC_AEC_BROADSIDE] = 119,
    [SND_DEVICE_IN_SPEAKER_DMIC_NS_BROADSIDE] = 121,
    [SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_BROADSIDE] = 120,
    [SND_DEVICE_IN_HANDSET_QMIC] = 125,
    [SND_DEVICE_IN_HANDSET_QMIC_AEC] = 125,
    [SND_DEVICE_IN_SPEAKER_QMIC_AEC] = 126,
    [SND_DEVICE_IN_SPEAKER_QMIC_NS] = 127,
    [SND_DEVICE_IN_SPEAKER_QMIC_AEC_NS] = 129,
    [SND_DEVICE_IN_VOICE_REC_QMIC_FLUENCE] = 125,
    [SND_DEVICE_IN_THREE_MIC] = 46, /* for APSS Surround Sound Recording */
    [SND_DEVICE_IN_HANDSET_TMIC_FLUENCE_PRO] = 138,
    [SND_DEVICE_IN_HANDSET_TMIC] = 153,
    [SND_DEVICE_IN_HANDSET_TMIC_AEC] = 154,
    [SND_DEVICE_IN_HANDSET_TMIC_NS] = 155,
    [SND_DEVICE_IN_HANDSET_TMIC_AEC_NS] = 156,
    [SND_DEVICE_IN_SPEAKER_TMIC_AEC] = 158,
    [SND_DEVICE_IN_SPEAKER_TMIC_NS] = 159,
    [SND_DEVICE_IN_SPEAKER_TMIC_AEC_NS] = 160,
    [SND_DEVICE_IN_VOICE_REC_TMIC] = 125,
    [SND_DEVICE_IN_UNPROCESSED_MIC] = 143,
    [SND_DEVICE_IN_UNPROCESSED_STEREO_MIC] = 144,
    [SND_DEVICE_IN_UNPROCESSED_THREE_MIC] = 145,
    [SND_DEVICE_IN_UNPROCESSED_QUAD_MIC] = 146,
    [SND_DEVICE_IN_UNPROCESSED_HEADSET_MIC] = 147,
    [SND_DEVICE_IN_HANDSET_GENERIC_QMIC] = 150,
    [SND_DEVICE_IN_LINE] = 4,
    [SND_DEVICE_IN_HANDSET_6MIC] = 4,
    [SND_DEVICE_IN_HANDSET_8MIC] = 4,
    [SND_DEVICE_OUT_VOIP_HANDSET] = 133,
    [SND_DEVICE_OUT_VOIP_SPEAKER] = 132,
    [SND_DEVICE_OUT_VOIP_HEADPHONES] = 134,
    [SND_DEVICE_IN_CAMCORDER_INVERT_LANDSCAPE] = 4,
    [SND_DEVICE_IN_CAMCORDER_PORTRAIT] = 4,
    [SND_DEVICE_IN_CAMCORDER_SELFIE_LANDSCAPE] = 4,
    [SND_DEVICE_IN_CAMCORDER_SELFIE_INVERT_LANDSCAPE] = 4,
    [SND_DEVICE_IN_CAMCORDER_SELFIE_PORTRAIT] = 4,
    [SND_DEVICE_IN_VOICE_HEARING_AID] = 44,
    [SND_DEVICE_IN_BUS] = 11,
    [SND_DEVICE_IN_CALL_PROXY] = 33,
};

struct name_to_index {
    char name[100];
    unsigned int index;
};

#define TO_NAME_INDEX(X)   #X, X

/* Used to get index from parsed string */
static struct name_to_index snd_device_name_index[SND_DEVICE_MAX] = {
    {TO_NAME_INDEX(SND_DEVICE_OUT_HANDSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_EXTERNAL_1)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_EXTERNAL_2)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_WSA)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_VBAT)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_REVERSE)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_SAFE)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_HEADPHONES)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_HEADPHONES_DSD)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_HEADPHONES_HIFI_FILTER)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_HIFI_FILTER)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_HEADPHONES_44_1)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_LINE)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_AND_HAPTICS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_HEADPHONES_AND_HAPTICS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_SAFE_AND_HEADPHONES)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_HEADPHONES_AND_HAPTICS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_SAFE_HEADPHONES_AND_HAPTICS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_AND_LINE)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_SAFE_AND_LINE)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_1)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_2)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_HANDSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_SPEAKER)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_SPEAKER_STEREO)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_SPEAKER_WSA)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_SPEAKER_HFP)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_SPEAKER_VBAT)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_SPEAKER_2)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_SPEAKER_2_WSA)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_SPEAKER_2_VBAT)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_HEADPHONES)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_HEADSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_LINE)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_HDMI)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_AND_HDMI)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_DISPLAY_PORT)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_DISPLAY_PORT1)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_AND_DISPLAY_PORT)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_DISPLAY_PORT_AND_HAPTICS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_BT_SCO)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_SAFE_BT_SCO_AND_HAPTICS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_BT_SCO_WB)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_BT_SCO_SWB)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO_WB)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_SAFE_BT_SCO_WB_AND_HAPTICS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_SAFE_BT_SCO_SWB_AND_HAPTICS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_BT_A2DP)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_AND_BT_A2DP)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_A2DP)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_BT_A2DP_AND_HAPTICS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_SAFE_BT_A2DP_AND_HAPTICS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_HANDSET_TMUS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_HAC_HANDSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_TTY_FULL_HEADPHONES)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_TTY_FULL_HEADSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_TTY_VCO_HEADPHONES)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_TTY_HCO_HANDSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_AND_BT_SCO)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_AND_BT_SCO_WB)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_AND_BT_SCO_SWB)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_WSA_AND_BT_SCO)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_WSA_AND_BT_SCO_WB)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_WSA_AND_BT_SCO_SWB)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_BT_SCO_AND_HAPTICS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_BT_SCO_WB_AND_HAPTICS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_BT_SCO_SWB_AND_HAPTICS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_TTY_FULL_USB)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_TTY_VCO_USB)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_TX)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_AFE_PROXY)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_USB_HEADSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_USB_HEADSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_USB_HEADPHONES)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_USB_HEADPHONES)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_SAFE_AND_USB_HEADSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_USB_HEADSET_AND_HAPTICS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_SAFE_USB_HEADSET_AND_HAPTICS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_USB_HEADSET_SPEC)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_TRANSMISSION_FM)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_ANC_HEADSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_ANC_FB_HEADSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_ANC_HEADSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_ANC_FB_HEADSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_AND_ANC_FB_HEADSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_ANC_HANDSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_PROTECTED)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_PROTECTED)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED_VBAT)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED_VBAT)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_PROTECTED_RAS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT_RAS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOIP_HANDSET)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOIP_SPEAKER)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOIP_HEADPHONES)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_VOICE_HEARING_AID)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_BUS_MEDIA)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_BUS_SYS)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_BUS_NAV)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_BUS_PHN)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_BUS_PAX)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_BUS_RSE)},
    {TO_NAME_INDEX(SND_DEVICE_OUT_CALL_PROXY)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_MIC_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_MIC_NN)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_MIC_EXTERNAL)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_MIC_AEC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_MIC_AEC_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_MIC_AEC_NN)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_MIC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_MIC_NS_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_MIC_NS_NN)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_MIC_AEC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_MIC_AEC_NS_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_MIC_AEC_NS_NN)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_DMIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_DMIC_AEC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_DMIC_AEC_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_DMIC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_DMIC_NS_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_DMIC_AEC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_DMIC_AEC_NS_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_MIC_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_MIC_NN)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_MIC_AEC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_MIC_AEC_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_MIC_AEC_NN)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_MIC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_MIC_NS_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_MIC_NS_NN)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_MIC_AEC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_MIC_AEC_NS_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_MIC_AEC_NS_NN)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_DMIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_AEC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_AEC_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_NS_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_STEREO)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HEADSET_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HEADSET_MIC_AEC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HEADSET_MIC_FLUENCE)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_SPEAKER_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_SPEAKER_MIC_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_SPEAKER_MIC_NN)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_SPEAKER_MIC_HFP)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_SPEAKER_MIC_HFP_MMSECNS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_HEADSET_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPDIF)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HDMI_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HDMI_ARC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_BT_SCO_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_BT_SCO_MIC_NREC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_BT_SCO_MIC_WB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_BT_SCO_MIC_WB_NREC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_BT_SCO_MIC_SWB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_BT_SCO_MIC_SWB_NREC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_BT_A2DP)},
    {TO_NAME_INDEX(SND_DEVICE_IN_CAMCORDER_LANDSCAPE)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_DMIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_DMIC_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_DMIC_TMUS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_SPEAKER_DMIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_SPEAKER_DMIC_SB)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_SPEAKER_TMIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_SPEAKER_QMIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_TTY_FULL_HEADSET_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_TTY_VCO_HANDSET_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_TTY_HCO_HEADSET_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_TTY_FULL_USB_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_TTY_HCO_USB_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_REC_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_REC_MIC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_REC_MIC_AEC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_REC_MIC_AEC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_REC_DMIC_STEREO)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_REC_DMIC_FLUENCE)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_REC_HEADSET_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_RX)},
    {TO_NAME_INDEX(SND_DEVICE_IN_USB_HEADSET_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_USB_HEADSET_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_UNPROCESSED_USB_HEADSET_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_RECOG_USB_HEADSET_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_USB_HEADSET_MIC_AEC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_USB_HEADSET_MULTI_CHANNEL_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_UNPROCESSED_USB_HEADSET_MULTI_CHANNEL_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_RECOG_USB_HEADSET_MULTI_CHANNEL_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_USB_HEADSET_MULTI_CHANNEL_MIC_AEC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_CAPTURE_FM)},
    {TO_NAME_INDEX(SND_DEVICE_IN_AANC_HANDSET_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_FLUENCE_DMIC_AANC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_QUAD_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_DMIC_STEREO)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_STEREO)},
    {TO_NAME_INDEX(SND_DEVICE_IN_CAPTURE_VI_FEEDBACK)},
    {TO_NAME_INDEX(SND_DEVICE_IN_CAPTURE_VI_FEEDBACK_MONO_1)},
    {TO_NAME_INDEX(SND_DEVICE_IN_CAPTURE_VI_FEEDBACK_MONO_2)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_SPEAKER_DMIC_BROADSIDE)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_BROADSIDE)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_AEC_BROADSIDE)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_NS_BROADSIDE)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_BROADSIDE)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_QMIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_QMIC_AEC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_QMIC_AEC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_HEARING_AID)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_QMIC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_QMIC_AEC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_REC_QMIC_FLUENCE)},
    {TO_NAME_INDEX(SND_DEVICE_IN_THREE_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_TMIC_FLUENCE_PRO)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_TMIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_TMIC_AEC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_TMIC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_TMIC_AEC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_TMIC_AEC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_TMIC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_SPEAKER_TMIC_AEC_NS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_VOICE_REC_TMIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_UNPROCESSED_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_UNPROCESSED_STEREO_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_UNPROCESSED_THREE_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_UNPROCESSED_QUAD_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_UNPROCESSED_HEADSET_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_6MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_8MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_GENERIC_QMIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_INCALL_REC_RX)},
    {TO_NAME_INDEX(SND_DEVICE_IN_INCALL_REC_TX)},
    {TO_NAME_INDEX(SND_DEVICE_IN_INCALL_REC_RX_TX)},
    {TO_NAME_INDEX(SND_DEVICE_IN_LINE)},
    {TO_NAME_INDEX(SND_DEVICE_IN_CAMCORDER_INVERT_LANDSCAPE)},
    {TO_NAME_INDEX(SND_DEVICE_IN_CAMCORDER_PORTRAIT)},
    {TO_NAME_INDEX(SND_DEVICE_IN_CAMCORDER_SELFIE_LANDSCAPE)},
    {TO_NAME_INDEX(SND_DEVICE_IN_CAMCORDER_SELFIE_INVERT_LANDSCAPE)},
    {TO_NAME_INDEX(SND_DEVICE_IN_CAMCORDER_SELFIE_PORTRAIT)},
    /* For legacy xml file parsing */
    {TO_NAME_INDEX(SND_DEVICE_IN_CAMCORDER_MIC)},
    {TO_NAME_INDEX(SND_DEVICE_IN_BUS)},
    {TO_NAME_INDEX(SND_DEVICE_IN_EC_REF_LOOPBACK)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_DMIC_AND_EC_REF_LOOPBACK)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_QMIC_AND_EC_REF_LOOPBACK)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_6MIC_AND_EC_REF_LOOPBACK)},
    {TO_NAME_INDEX(SND_DEVICE_IN_HANDSET_8MIC_AND_EC_REF_LOOPBACK)},
    {TO_NAME_INDEX(SND_DEVICE_IN_CALL_PROXY)},
};

static char * backend_tag_table[SND_DEVICE_MAX] = {0};
static char * hw_interface_table[SND_DEVICE_MAX] = {0};

static struct name_to_index usecase_name_index[AUDIO_USECASE_MAX] = {
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_DEEP_BUFFER)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_WITH_HAPTICS)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_HAPTICS)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_LOW_LATENCY)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_HIFI)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_TTS)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_ULL)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_MULTI_CH)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_OFFLOAD)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_OFFLOAD2)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_OFFLOAD3)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_OFFLOAD4)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_OFFLOAD5)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_OFFLOAD6)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_OFFLOAD7)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_OFFLOAD8)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_OFFLOAD9)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_MMAP)},
    {TO_NAME_INDEX(USECASE_AUDIO_RECORD)},
    {TO_NAME_INDEX(USECASE_AUDIO_RECORD_COMPRESS)},
    {TO_NAME_INDEX(USECASE_AUDIO_RECORD_COMPRESS2)},
    {TO_NAME_INDEX(USECASE_AUDIO_RECORD_COMPRESS3)},
    {TO_NAME_INDEX(USECASE_AUDIO_RECORD_COMPRESS4)},
    {TO_NAME_INDEX(USECASE_AUDIO_RECORD_COMPRESS5)},
    {TO_NAME_INDEX(USECASE_AUDIO_RECORD_COMPRESS6)},
    {TO_NAME_INDEX(USECASE_AUDIO_RECORD_LOW_LATENCY)},
    {TO_NAME_INDEX(USECASE_AUDIO_RECORD_MMAP)},
    {TO_NAME_INDEX(USECASE_AUDIO_RECORD_HIFI)},
    {TO_NAME_INDEX(USECASE_VOICE_CALL)},
    {TO_NAME_INDEX(USECASE_VOICE2_CALL)},
    {TO_NAME_INDEX(USECASE_VOLTE_CALL)},
    {TO_NAME_INDEX(USECASE_QCHAT_CALL)},
    {TO_NAME_INDEX(USECASE_VOWLAN_CALL)},
    {TO_NAME_INDEX(USECASE_VOICEMMODE1_CALL)},
    {TO_NAME_INDEX(USECASE_VOICEMMODE2_CALL)},
    {TO_NAME_INDEX(USECASE_INCALL_REC_UPLINK)},
    {TO_NAME_INDEX(USECASE_INCALL_REC_DOWNLINK)},
    {TO_NAME_INDEX(USECASE_INCALL_REC_UPLINK_AND_DOWNLINK)},
    {TO_NAME_INDEX(USECASE_AUDIO_HFP_SCO)},
    {TO_NAME_INDEX(USECASE_AUDIO_HFP_SCO_WB)},
    {TO_NAME_INDEX(USECASE_AUDIO_HFP_SCO_DOWNLINK)},
    {TO_NAME_INDEX(USECASE_AUDIO_HFP_SCO_WB_DOWNLINK)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_FM)},
    {TO_NAME_INDEX(USECASE_AUDIO_RECORD_FM_VIRTUAL)},
    {TO_NAME_INDEX(USECASE_AUDIO_SPKR_CALIB_RX)},
    {TO_NAME_INDEX(USECASE_AUDIO_SPKR_CALIB_TX)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_AFE_PROXY)},
    {TO_NAME_INDEX(USECASE_AUDIO_RECORD_AFE_PROXY)},
    {TO_NAME_INDEX(USECASE_AUDIO_RECORD_AFE_PROXY2)},
    {TO_NAME_INDEX(USECASE_AUDIO_DSM_FEEDBACK)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_SILENCE)},
    {TO_NAME_INDEX(USECASE_AUDIO_EC_REF_LOOPBACK)},
    {TO_NAME_INDEX(USECASE_INCALL_MUSIC_UPLINK)},
    {TO_NAME_INDEX(USECASE_INCALL_MUSIC_UPLINK2)},
    {TO_NAME_INDEX(USECASE_AUDIO_A2DP_ABR_FEEDBACK)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_VOIP)},
    {TO_NAME_INDEX(USECASE_AUDIO_RECORD_VOIP)},
    {TO_NAME_INDEX(USECASE_AUDIO_TRANSCODE_LOOPBACK_RX)},
    {TO_NAME_INDEX(USECASE_AUDIO_TRANSCODE_LOOPBACK_TX)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_MEDIA)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_NAV_GUIDANCE)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_PHONE)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_FRONT_PASSENGER)},
    {TO_NAME_INDEX(USECASE_AUDIO_PLAYBACK_REAR_SEAT)},
};

static const struct name_to_index usecase_type_index[USECASE_TYPE_MAX] = {
    {TO_NAME_INDEX(PCM_PLAYBACK)},
    {TO_NAME_INDEX(PCM_CAPTURE)},
    {TO_NAME_INDEX(VOICE_CALL)},
    {TO_NAME_INDEX(PCM_HFP_CALL)},
};

struct app_type_entry {
    int uc_type;
    int bit_width;
    int app_type;
    int max_rate;
    char *mode;
    struct listnode node; // membership in app_type_entry_list;
};

static struct listnode app_type_entry_list;

#define NO_COLS 2
#ifdef PLATFORM_APQ8084
static int msm_device_to_be_id [][NO_COLS] = {
       {AUDIO_DEVICE_OUT_EARPIECE                       ,       2},
       {AUDIO_DEVICE_OUT_SPEAKER                        ,       2},
       {AUDIO_DEVICE_OUT_WIRED_HEADSET                  ,       2},
       {AUDIO_DEVICE_OUT_WIRED_HEADPHONE                ,       2},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO                  ,       11},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET          ,       11},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT           ,       11},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP                 ,       -1},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES      ,       -1},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER         ,       -1},
       {AUDIO_DEVICE_OUT_AUX_DIGITAL                    ,       4},
       {AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET              ,       9},
       {AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET              ,       9},
       {AUDIO_DEVICE_OUT_USB_ACCESSORY                  ,       -1},
       {AUDIO_DEVICE_OUT_USB_DEVICE                     ,       -1},
       {AUDIO_DEVICE_OUT_USB_HEADSET                    ,       -1},
       {AUDIO_DEVICE_OUT_REMOTE_SUBMIX                  ,       9},
       {AUDIO_DEVICE_OUT_PROXY                          ,       9},
       {AUDIO_DEVICE_OUT_FM                             ,       7},
       {AUDIO_DEVICE_OUT_ALL                            ,      -1},
       {AUDIO_DEVICE_NONE                               ,      -1},
       {AUDIO_DEVICE_OUT_DEFAULT                        ,      -1},
};
#elif PLATFORM_MSM8994
static int msm_device_to_be_id [][NO_COLS] = {
       {AUDIO_DEVICE_OUT_EARPIECE                       ,       2},
       {AUDIO_DEVICE_OUT_SPEAKER                        ,       2},
       {AUDIO_DEVICE_OUT_WIRED_HEADSET                  ,       2},
       {AUDIO_DEVICE_OUT_WIRED_HEADPHONE                ,       2},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO                  ,       38},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET          ,       38},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT           ,       38},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP                 ,       -1},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES      ,       -1},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER         ,       -1},
       {AUDIO_DEVICE_OUT_AUX_DIGITAL                    ,       4},
       {AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET              ,       9},
       {AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET              ,       9},
       {AUDIO_DEVICE_OUT_USB_ACCESSORY                  ,       -1},
       {AUDIO_DEVICE_OUT_USB_DEVICE                     ,       -1},
       {AUDIO_DEVICE_OUT_USB_HEADSET                    ,       -1},
       {AUDIO_DEVICE_OUT_REMOTE_SUBMIX                  ,       9},
       {AUDIO_DEVICE_OUT_PROXY                          ,       9},
/* Add the correct be ids */
       {AUDIO_DEVICE_OUT_FM                             ,       7},
       {AUDIO_DEVICE_OUT_ALL                            ,      -1},
       {AUDIO_DEVICE_NONE                               ,      -1},
       {AUDIO_DEVICE_OUT_DEFAULT                        ,      -1},
};
#elif PLATFORM_MSM8996
static int msm_device_to_be_id [][NO_COLS] = {
       {AUDIO_DEVICE_OUT_EARPIECE                       ,       2},
       {AUDIO_DEVICE_OUT_SPEAKER                        ,       2},
       {AUDIO_DEVICE_OUT_WIRED_HEADSET                  ,       41},
       {AUDIO_DEVICE_OUT_WIRED_HEADPHONE                ,       41},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO                  ,       11},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET          ,       11},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT           ,       11},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP                 ,       -1},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES      ,       -1},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER         ,       -1},
       {AUDIO_DEVICE_OUT_AUX_DIGITAL                    ,       4},
       {AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET              ,       9},
       {AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET              ,       9},
       {AUDIO_DEVICE_OUT_USB_ACCESSORY                  ,       -1},
       {AUDIO_DEVICE_OUT_USB_DEVICE                     ,       -1},
       {AUDIO_DEVICE_OUT_USB_HEADSET                    ,       -1},
       {AUDIO_DEVICE_OUT_REMOTE_SUBMIX                  ,       9},
       {AUDIO_DEVICE_OUT_PROXY                          ,       9},
/* Add the correct be ids */
       {AUDIO_DEVICE_OUT_FM                             ,       7},
       {AUDIO_DEVICE_OUT_ALL                            ,      -1},
       {AUDIO_DEVICE_NONE                               ,      -1},
       {AUDIO_DEVICE_OUT_DEFAULT                        ,      -1},
};
#elif PLATFORM_MSM8998
static int msm_device_to_be_id [][NO_COLS] = {
       {AUDIO_DEVICE_OUT_EARPIECE                       ,       2},
       {AUDIO_DEVICE_OUT_SPEAKER                        ,       2},
       {AUDIO_DEVICE_OUT_WIRED_HEADSET                  ,       41},
       {AUDIO_DEVICE_OUT_WIRED_HEADPHONE                ,       41},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO                  ,       11},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET          ,       11},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT           ,       11},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP                 ,       -1},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES      ,       -1},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER         ,       -1},
       {AUDIO_DEVICE_OUT_AUX_DIGITAL                    ,       4},
       {AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET              ,       9},
       {AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET              ,       9},
       {AUDIO_DEVICE_OUT_USB_ACCESSORY                  ,       -1},
       {AUDIO_DEVICE_OUT_USB_DEVICE                     ,       -1},
       {AUDIO_DEVICE_OUT_USB_HEADSET                    ,       -1},
       {AUDIO_DEVICE_OUT_REMOTE_SUBMIX                  ,       9},
       {AUDIO_DEVICE_OUT_PROXY                          ,       9},
/* Add the correct be ids */
       {AUDIO_DEVICE_OUT_FM                             ,       7},
       {AUDIO_DEVICE_OUT_ALL                            ,      -1},
       {AUDIO_DEVICE_NONE                               ,      -1},
       {AUDIO_DEVICE_OUT_DEFAULT                        ,      -1},
};
#elif defined (PLATFORM_MSMFALCON) || defined (PLATFORM_MSM8937) || \
      defined (PLATFORM_MSM8953)
static int (*msm_device_to_be_id)[];
#else
static int msm_device_to_be_id [][NO_COLS] = {
    {AUDIO_DEVICE_NONE, -1},
};
#endif

#if defined (PLATFORM_MSMFALCON)  || defined (PLATFORM_MSM8937) || \
    defined (PLATFORM_MSM8953)
static int msm_device_to_be_id_internal_codec [][NO_COLS] = {
       {AUDIO_DEVICE_OUT_EARPIECE                       ,       34},
       {AUDIO_DEVICE_OUT_SPEAKER                        ,       34},
       {AUDIO_DEVICE_OUT_WIRED_HEADSET                  ,       34},
       {AUDIO_DEVICE_OUT_WIRED_HEADPHONE                ,       34},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO                  ,       11},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET          ,       11},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT           ,       11},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP                 ,       -1},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES      ,       -1},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER         ,       -1},
       {AUDIO_DEVICE_OUT_AUX_DIGITAL                    ,       4},
       {AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET              ,       9},
       {AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET              ,       9},
       {AUDIO_DEVICE_OUT_USB_ACCESSORY                  ,       -1},
       {AUDIO_DEVICE_OUT_USB_DEVICE                     ,       -1},
       {AUDIO_DEVICE_OUT_USB_HEADSET                    ,       -1},
       {AUDIO_DEVICE_OUT_REMOTE_SUBMIX                  ,       9},
       {AUDIO_DEVICE_OUT_PROXY                          ,       9},
       {AUDIO_DEVICE_OUT_FM                             ,       7},
       {AUDIO_DEVICE_OUT_ALL                            ,      -1},
       {AUDIO_DEVICE_NONE                               ,      -1},
       {AUDIO_DEVICE_OUT_DEFAULT                        ,      -1},
};

static int msm_device_to_be_id_external_codec [][NO_COLS] = {
       {AUDIO_DEVICE_OUT_EARPIECE                       ,       2},
       {AUDIO_DEVICE_OUT_SPEAKER                        ,       2},
       {AUDIO_DEVICE_OUT_WIRED_HEADSET                  ,       41},
       {AUDIO_DEVICE_OUT_WIRED_HEADPHONE                ,       41},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO                  ,       11},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET          ,       11},
       {AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT           ,       11},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP                 ,       -1},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES      ,       -1},
       {AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER         ,       -1},
       {AUDIO_DEVICE_OUT_AUX_DIGITAL                    ,       4},
       {AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET              ,       9},
       {AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET              ,       9},
       {AUDIO_DEVICE_OUT_USB_ACCESSORY                  ,       -1},
       {AUDIO_DEVICE_OUT_USB_DEVICE                     ,       -1},
       {AUDIO_DEVICE_OUT_USB_HEADSET                    ,       -1},
       {AUDIO_DEVICE_OUT_REMOTE_SUBMIX                  ,       9},
       {AUDIO_DEVICE_OUT_PROXY                          ,       9},
       {AUDIO_DEVICE_OUT_FM                             ,       7},
       {AUDIO_DEVICE_OUT_ALL                            ,      -1},
       {AUDIO_DEVICE_NONE                               ,      -1},
       {AUDIO_DEVICE_OUT_DEFAULT                        ,      -1},
};
#endif

#if defined (PLATFORM_MSMFALCON)  || defined (PLATFORM_MSM8937) || \
    defined (PLATFORM_MSM8953)
static int msm_be_id_array_len;
#else
static int msm_be_id_array_len  =
    sizeof(msm_device_to_be_id) / sizeof(msm_device_to_be_id[0]);
#endif

#define DEEP_BUFFER_PLATFORM_DELAY (29*1000LL)
#define PCM_OFFLOAD_PLATFORM_DELAY (30*1000LL)
#define LOW_LATENCY_PLATFORM_DELAY (13*1000LL)
#define ULL_PLATFORM_DELAY         (3*1000LL)
#define MMAP_PLATFORM_DELAY        (3*1000LL)

static int audio_source_delay_ms[AUDIO_SOURCE_CNT] = {0};

static struct name_to_index audio_source_index[AUDIO_SOURCE_CNT] = {
    {TO_NAME_INDEX(AUDIO_SOURCE_DEFAULT)},
    {TO_NAME_INDEX(AUDIO_SOURCE_MIC)},
    {TO_NAME_INDEX(AUDIO_SOURCE_VOICE_UPLINK)},
    {TO_NAME_INDEX(AUDIO_SOURCE_VOICE_DOWNLINK)},
    {TO_NAME_INDEX(AUDIO_SOURCE_VOICE_CALL)},
    {TO_NAME_INDEX(AUDIO_SOURCE_CAMCORDER)},
    {TO_NAME_INDEX(AUDIO_SOURCE_VOICE_RECOGNITION)},
    {TO_NAME_INDEX(AUDIO_SOURCE_VOICE_COMMUNICATION)},
    {TO_NAME_INDEX(AUDIO_SOURCE_REMOTE_SUBMIX)},
    {TO_NAME_INDEX(AUDIO_SOURCE_UNPROCESSED)},
    {TO_NAME_INDEX(AUDIO_SOURCE_VOICE_PERFORMANCE)},
};

static bool is_usb_snd_dev(snd_device_t snd_device)
{
    return is_usb_in_snd_dev(snd_device) || is_usb_out_snd_dev(snd_device);
}

bool is_operator_tmus()
{
    char value[PROPERTY_VALUE_MAX];
    int mccmnc;
    property_get("gsm.sim.operator.numeric",value,"0");
    mccmnc = atoi(value);
    ALOGD("%s: tmus mccmnc %d", __func__, mccmnc);
    switch(mccmnc) {
    /* TMUS MCC(310), MNC(490, 260, 026) */
    case 310490:
    case 310260:
    case 310026:
    /* Add new TMUS MNC(800, 660, 580, 310, 270, 250, 240, 230, 220, 210, 200, 160) */
    case 310800:
    case 310660:
    case 310580:
    case 310310:
    case 310270:
    case 310250:
    case 310240:
    case 310230:
    case 310220:
    case 310210:
    case 310200:
    case 310160:
        return true;
    default:
        return false;
    }
}

const char* get_xml_file_path(const char* file_name)
{
    audio_get_vendor_config_path(vendor_config_path, sizeof(vendor_config_path));
    snprintf(xml_file_path, sizeof(xml_file_path),
        "%s/%s", vendor_config_path, file_name);
    return xml_file_path;
}

static char *get_current_operator()
{
    struct listnode *node;
    struct operator_info *info_item;
    char mccmnc[PROPERTY_VALUE_MAX];
    char *ret = NULL;

    property_get("gsm.sim.operator.numeric",mccmnc,"00000");

    list_for_each(node, &operator_info_list) {
        info_item = node_to_item(node, struct operator_info, list);
        if (strstr(info_item->mccmnc, mccmnc) != NULL) {
            ret = info_item->name;
        }
    }

    return ret;
}

static struct operator_specific_device *get_operator_specific_device(snd_device_t snd_device)
{
    struct listnode *node;
    struct operator_specific_device *ret = NULL;
    struct operator_specific_device *device_item;
    char *operator_name;

    operator_name = get_current_operator();
    if (operator_name == NULL)
        return ret;

    list_for_each(node, operator_specific_device_table[snd_device]) {
        device_item = node_to_item(node, struct operator_specific_device, list);
        if (strcmp(operator_name, device_item->operator) == 0) {
            ret = device_item;
        }
    }

    return ret;
}

static int get_operator_specific_device_acdb_id(snd_device_t snd_device)
{
    struct operator_specific_device *device;
    int ret = acdb_device_table[snd_device];

    device = get_operator_specific_device(snd_device);
    if (device != NULL)
        ret = device->acdb_id;

    return ret;
}

static int get_external_specific_device_acdb_id(snd_device_t snd_device)
{
    struct external_specific_device *ext_dev;
    int ret = acdb_device_table[snd_device];
    char *usbid = NULL;
    struct listnode *node;

    if (is_usb_snd_dev(snd_device))
        usbid = audio_extn_usb_usbid();

    if (usbid) {
        list_for_each(node, external_specific_device_table[snd_device]) {
            ext_dev = node_to_item(node, struct external_specific_device, list);
            if (ext_dev->usbid && !strcmp(usbid, ext_dev->usbid)) {
                ret = ext_dev->acdb_id;
                break;
            }
        }

        free(usbid);
    }
    return ret;
}

static const char *get_operator_specific_device_mixer_path(snd_device_t snd_device)
{
    struct operator_specific_device *device;
    const char *ret = device_table[snd_device];

    device = get_operator_specific_device(snd_device);
    if (device != NULL)
        ret = device->mixer_path;

    return ret;
}

static void update_codec_type_and_interface(struct platform_data * my_data,
     const char *snd_card_name)
{
     if (!strncmp(snd_card_name, "sdm670-skuw-snd-card",
                  sizeof("sdm670-skuw-snd-card")) ||
         !strncmp(snd_card_name, "sdm660-snd-card",
                  sizeof("sdm660-snd-card")) ||
         !strncmp(snd_card_name, "sdm660-snd-card-skush",
                  sizeof("sdm660-snd-card-skush")) ||
         !strncmp(snd_card_name, "sdm670-360cam-snd-card",
                  sizeof("sdm670-360cam-snd-card")) ||
         !strncmp(snd_card_name, "sdm660-snd-card-mtp",
                  sizeof("sdm660-snd-card-mtp")) ||
         !strncmp(snd_card_name, "sdm670-mtp-snd-card",
                   sizeof("sdm670-mtp-snd-card")) ||
         !strncmp(snd_card_name, "sm6150-qrd-snd-card",
                   sizeof("sm6150-qrd-snd-card")) ||
         !strncmp(snd_card_name, "sm6150-idp-snd-card",
                   sizeof("sm6150-idp-snd-card")) ||
         !strncmp(snd_card_name, "qcs605-lc-snd-card",
                   sizeof("qcs605-lc-snd-card")) ||
         !strncmp(snd_card_name, "lahaina-mtp-snd-card",
                   sizeof("lahaina-mtp-snd-card")) ||
         !strncmp(snd_card_name, "lahaina-qrd-snd-card",
                   sizeof("lahaina-qrd-snd-card")) ||
         !strncmp(snd_card_name, "lahaina-cdp-snd-card",
                   sizeof("lahaina-cdp-snd-card")) ||
         !strncmp(snd_card_name, "kona-mtp-snd-card",
                   sizeof("kona-mtp-snd-card")) ||
         !strncmp(snd_card_name, "kona-qrd-snd-card",
                   sizeof("kona-qrd-snd-card")) ||
         !strncmp(snd_card_name, "lito-mtp-snd-card",
                   sizeof("lito-mtp-snd-card")) ||
         !strncmp(snd_card_name, "lito-qrd-snd-card",
                   sizeof("lito-qrd-snd-card")) ||
         !strncmp(snd_card_name, "trinket-idp-snd-card",
                   sizeof("trinket-idp-snd-card")) ||
         !strncmp(snd_card_name, "sdm439-sku1-snd-card",
                   sizeof("sdm439-sku1-snd-card")) ||
         !strncmp(snd_card_name, "sdm439-snd-card-mtp",
                   sizeof("sdm439-snd-card-mtp")) ||
         !strncmp(snd_card_name, "sm6150-wcd9375qrd-snd-card",
                   sizeof("sm6150-wcd9375qrd-snd-card")) ||
         !strncmp(snd_card_name, "sm6150-wcd9375-snd-card",
                   sizeof("sm6150-wcd9375-snd-card")) ||
         !strncmp(snd_card_name, "sm8150-tavil-qrd-snd-card",
                   sizeof("sm8150-tavil-qrd-snd-card")) ||
         !strncmp(snd_card_name, "atoll-wcd937x-snd-card",
                   sizeof("atoll-wcd937x-snd-card")) ||
         !strncmp(snd_card_name, "atoll-idp-snd-card",
                   sizeof("atoll-idp-snd-card")) ||
         !strncmp(snd_card_name, "atoll-qrd-snd-card",
                   sizeof("atoll-qrd-snd-card")) ||
         !strncmp(snd_card_name, "bengal-idp-snd-card",
                   sizeof("bengal-idp-snd-card")) ||
         !strncmp(snd_card_name, "bengal-scubaidp-snd-card",
                   sizeof("bengal-scubaidp-snd-card")) ||
         !strncmp(snd_card_name, "bengal-qrd-snd-card",
                   sizeof("bengal-qrd-snd-card")) ||
         !strncmp(snd_card_name, "holi-mtp-snd-card",
                   sizeof("holi-mtp-snd-card")) ||
         !strncmp(snd_card_name, "holi-qrd-snd-card",
                   sizeof("holi-qrd-snd-card")) ||
         !strncmp(snd_card_name, "msm8937-snd-card-mtp",
                   sizeof("msm8937-snd-card-mtp")) ||
         !strncmp(snd_card_name, "msm8953-snd-card-mtp",
                   sizeof("msm8953-snd-card-mtp")) ||
         !strncmp(snd_card_name, "msm8953-sku4-snd-card",
                   sizeof("msm8953-sku4-snd-card")) ||
         !strncmp(snd_card_name, "lito-lagoonmtp-snd-card",
                   sizeof("lito-lagoonmtp-snd-card")) ||
         !strncmp(snd_card_name, "lito-lagoonqrd-snd-card",
                   sizeof("lito-lagoonqrd-snd-card")) ||
         !strncmp(snd_card_name, "lito-orchidmtp-snd-card",
                   sizeof("lito-orchidmtp-snd-card"))) {
         ALOGI("%s: snd_card_name: %s",__func__,snd_card_name);
         my_data->is_internal_codec = true;
         my_data->is_slimbus_interface = false;
     }

    if (!strncmp(snd_card_name, "sdm670", strlen("sdm670")) ||
        !strncmp(snd_card_name, "sdm660", strlen("sdm660")) ||
        !strncmp(snd_card_name, "qcs605", strlen("qcs605")) ||
        !strncmp(snd_card_name, "sdm439", strlen("sdm439"))) {
        my_data->is_default_be_config = true;
    }
}

static bool can_enable_mbdrc_on_device(snd_device_t snd_device)
{
    bool ret = false;

    if (snd_device == SND_DEVICE_OUT_SPEAKER ||
        snd_device == SND_DEVICE_OUT_SPEAKER_WSA ||
        snd_device == SND_DEVICE_OUT_SPEAKER_VBAT ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_VBAT ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_2_VBAT ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_2 ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_STEREO ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_WSA ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_2_WSA) {
        ret = true;
    }
    return ret;
}

static snd_device_t fixup_usb_headset_mic_snd_device(void *platform,
                                                     snd_device_t base,
                                                     snd_device_t other)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    struct stream_in *in = adev_get_active_input(adev);
    if (in == NULL)
        return base;
    unsigned int sr = in->sample_rate;
    unsigned int ch = popcount(in->channel_mask);
    unsigned int bit_width = in->bit_width;
    if (audio_extn_usb_is_config_supported(&bit_width, &sr, &ch, false)
                                           && ((ch == 6) || (ch == 8))) {
        return other;
    } else {
        return base;
    }
}

static const char *platform_get_mixer_control(struct mixer_ctl *);

static void platform_reset_edid_info(void *platform) {
    ALOGV("%s:", __func__);
    struct platform_data *my_data = (struct platform_data *)platform;
    int i = 0, j = 0;
    for (i = 0; i < MAX_CONTROLLERS; ++i) {
        for (j = 0; j < MAX_STREAMS_PER_CONTROLLER; ++j) {
            struct ext_disp_state *state = &my_data->ext_disp[i][j];
            state->type = EXT_DISPLAY_TYPE_NONE;
            if (state->edid_info) {
                free(state->edid_info);
                state->edid_info = NULL;
            }
            state->valid = false;
        }
    }
}

bool platform_send_gain_dep_cal(void *platform, int level) {
    bool ret_val = false;
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    int acdb_dev_id, app_type;
    int acdb_dev_type = MSM_SNDDEV_CAP_RX;
    int mode = CAL_MODE_RTAC;
    struct listnode *node;
    struct audio_usecase *usecase;

    if (my_data->acdb_send_gain_dep_cal == NULL) {
        ALOGE("%s: dlsym error for acdb_send_gain_dep_cal", __func__);
        return ret_val;
    }

    if (!voice_is_in_call(adev)) {
        ALOGV("%s: Not Voice call usecase, apply new cal for level %d",
               __func__, level);

        // find the current active sound device
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);

            if (usecase != NULL && usecase->stream.out &&
                                   usecase->type == PCM_PLAYBACK) {
                int new_snd_device[2] = {0};
                int i, num_devices = 1;

                ALOGV("%s: out device is %d", __func__,  usecase->out_snd_device);
                app_type = usecase->stream.out->app_type_cfg.app_type;
                acdb_dev_id = platform_get_snd_device_acdb_id(usecase->out_snd_device);

                if (platform_split_snd_device(my_data, usecase->out_snd_device,
                                              &num_devices, new_snd_device) < 0)
                    new_snd_device[0] = usecase->out_snd_device;

                for (i = 0; i < num_devices; i++)
                    if (can_enable_mbdrc_on_device(new_snd_device[i])) {
                        if (audio_extn_spkr_prot_is_enabled())
                            acdb_dev_id = platform_get_spkr_prot_acdb_id(new_snd_device[i]);
                        else
                            acdb_dev_id = acdb_device_table[new_snd_device[i]];
                    }

                if (!my_data->acdb_send_gain_dep_cal(acdb_dev_id, app_type,
                                                     acdb_dev_type, mode, level)) {
                    // set ret_val true if at least one calibration is set successfully
                    ret_val = true;
                } else {
                    ALOGE("%s: my_data->acdb_send_gain_dep_cal failed ", __func__);
                }
            } else {
                ALOGW("%s: Usecase list is empty", __func__);
            }
        }
    } else {
        ALOGW("%s: Voice call in progress .. ignore setting new cal",
              __func__);
    }
    return ret_val;
}

void platform_set_gsm_mode(void *platform, bool enable)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;

    if (my_data->gsm_mode_enabled) {
        my_data->gsm_mode_enabled = false;
        ALOGV("%s: disabling gsm mode", __func__);
        audio_route_reset_and_update_path(adev->audio_route, "gsm-mode");
    }

    if (enable) {
         my_data->gsm_mode_enabled = true;
         ALOGD("%s: enabling gsm mode", __func__);
         audio_route_apply_and_update_path(adev->audio_route, "gsm-mode");
    }
}

void platform_set_echo_reference(struct audio_device *adev, bool enable,
                                 struct listnode *out_devices)
{
    struct platform_data *my_data = (struct platform_data *)adev->platform;
    char ec_ref_mixer_path[MIXER_PATH_MAX_LENGTH] = "echo-reference";

    audio_extn_sound_trigger_update_ec_ref_status(enable);

    if (strcmp(my_data->ec_ref_mixer_path, "")) {
        ALOGV("%s: disabling %s", __func__, my_data->ec_ref_mixer_path);
        audio_route_reset_and_update_path(adev->audio_route,
                                          my_data->ec_ref_mixer_path);
    }

    if (enable) {
        if (!voice_extn_is_compress_voip_supported()) {
            if (adev->mode == AUDIO_MODE_IN_COMMUNICATION)
                strlcat(ec_ref_mixer_path, "-voip", MIXER_PATH_MAX_LENGTH);
        }
        strlcpy(my_data->ec_ref_mixer_path, ec_ref_mixer_path,
                    MIXER_PATH_MAX_LENGTH);
        /*
         * If native audio device reference count > 0, then apply codec EC otherwise
         * apply EC based on output device.
         */
        if (adev->snd_dev_ref_cnt[SND_DEVICE_OUT_HEADPHONES_44_1] > 0)
            strlcat(ec_ref_mixer_path, " headphones-44.1",
                    MIXER_PATH_MAX_LENGTH);
        else if (adev->snd_dev_ref_cnt[SND_DEVICE_OUT_SPEAKER_VBAT] > 0)
            strlcat(ec_ref_mixer_path, " speaker-vbat",
                    MIXER_PATH_MAX_LENGTH);
        else if (adev->snd_dev_ref_cnt[SND_DEVICE_OUT_DISPLAY_PORT] > 0)
            strlcat(ec_ref_mixer_path, " display-port",
                    MIXER_PATH_MAX_LENGTH);
        else if (adev->snd_dev_ref_cnt[SND_DEVICE_OUT_DISPLAY_PORT1] > 0)
            strlcat(ec_ref_mixer_path, " display-port1",
                    MIXER_PATH_MAX_LENGTH);
        else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_EARPIECE))
            strlcat(ec_ref_mixer_path, " handset",
                    MIXER_PATH_MAX_LENGTH);
        else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) ||
                 compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADSET))
            strlcat(ec_ref_mixer_path, " headphones",
                    MIXER_PATH_MAX_LENGTH);
        else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_USB_HEADSET))
            strlcat(ec_ref_mixer_path, " usb-headphones",
                    MIXER_PATH_MAX_LENGTH);
        else if (adev->snd_dev_ref_cnt[SND_DEVICE_OUT_BT_SCO_WB] > 0)
            strlcat(ec_ref_mixer_path, " bt-sco-wb",
                    MIXER_PATH_MAX_LENGTH);
        else if (adev->snd_dev_ref_cnt[SND_DEVICE_OUT_BT_SCO_SWB] > 0)
            strlcat(ec_ref_mixer_path, " bt-sco-swb",
                    MIXER_PATH_MAX_LENGTH);
        else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_BLUETOOTH_SCO))
            strlcat(ec_ref_mixer_path, " bt-sco",
                    MIXER_PATH_MAX_LENGTH);

        if (audio_route_apply_and_update_path(adev->audio_route,
                                              ec_ref_mixer_path) == 0)
            strlcpy(my_data->ec_ref_mixer_path, ec_ref_mixer_path,
                    MIXER_PATH_MAX_LENGTH);
        else
            audio_route_apply_and_update_path(adev->audio_route, my_data->ec_ref_mixer_path);

        ALOGD("%s: enabling %s", __func__, my_data->ec_ref_mixer_path);
    }
}

static struct csd_data *open_csd_client(bool i2s_ext_modem)
{
    struct csd_data *csd = calloc(1, sizeof(struct csd_data));

    if (!csd) {
        ALOGE("failed to allocate csd_data mem");
        return NULL;
    }

    csd->csd_client = dlopen(LIB_CSD_CLIENT, RTLD_NOW);
    if (csd->csd_client == NULL) {
        ALOGE("%s: DLOPEN failed for %s", __func__, LIB_CSD_CLIENT);
        goto error;
    } else {
        ALOGV("%s: DLOPEN successful for %s", __func__, LIB_CSD_CLIENT);

        csd->deinit = (deinit_t)dlsym(csd->csd_client,
                                             "csd_client_deinit");
        if (csd->deinit == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_deinit", __func__,
                  dlerror());
            goto error;
        }
        csd->disable_device = (disable_device_t)dlsym(csd->csd_client,
                                             "csd_client_disable_device");
        if (csd->disable_device == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_disable_device",
                  __func__, dlerror());
            goto error;
        }
        csd->enable_device_config = (enable_device_config_t)dlsym(csd->csd_client,
                                               "csd_client_enable_device_config");
        if (csd->enable_device_config == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_enable_device_config",
                  __func__, dlerror());
            goto error;
        }
        csd->enable_device = (enable_device_t)dlsym(csd->csd_client,
                                             "csd_client_enable_device");
        if (csd->enable_device == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_enable_device",
                  __func__, dlerror());
            goto error;
        }
        csd->start_voice = (start_voice_t)dlsym(csd->csd_client,
                                             "csd_client_start_voice");
        if (csd->start_voice == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_start_voice",
                  __func__, dlerror());
            goto error;
        }
        csd->stop_voice = (stop_voice_t)dlsym(csd->csd_client,
                                             "csd_client_stop_voice");
        if (csd->stop_voice == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_stop_voice",
                  __func__, dlerror());
            goto error;
        }
        csd->volume = (volume_t)dlsym(csd->csd_client,
                                             "csd_client_volume");
        if (csd->volume == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_volume",
                  __func__, dlerror());
            goto error;
        }
        csd->mic_mute = (mic_mute_t)dlsym(csd->csd_client,
                                             "csd_client_mic_mute");
        if (csd->mic_mute == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_mic_mute",
                  __func__, dlerror());
            goto error;
        }
        csd->slow_talk = (slow_talk_t)dlsym(csd->csd_client,
                                             "csd_client_slow_talk");
        if (csd->slow_talk == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_slow_talk",
                  __func__, dlerror());
            goto error;
        }
        csd->start_playback = (start_playback_t)dlsym(csd->csd_client,
                                             "csd_client_start_playback");
        if (csd->start_playback == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_start_playback",
                  __func__, dlerror());
            goto error;
        }
        csd->stop_playback = (stop_playback_t)dlsym(csd->csd_client,
                                             "csd_client_stop_playback");
        if (csd->stop_playback == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_stop_playback",
                  __func__, dlerror());
            goto error;
        }
        csd->set_lch = (set_lch_t)dlsym(csd->csd_client, "csd_client_set_lch");
        if (csd->set_lch == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_set_lch",
                  __func__, dlerror());
            /* Ignore the error as this is not mandatory function for
             * basic voice call to work.
             */
        }
        csd->start_record = (start_record_t)dlsym(csd->csd_client,
                                             "csd_client_start_record");
        if (csd->start_record == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_start_record",
                  __func__, dlerror());
            goto error;
        }
        csd->stop_record = (stop_record_t)dlsym(csd->csd_client,
                                             "csd_client_stop_record");
        if (csd->stop_record == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_stop_record",
                  __func__, dlerror());
            goto error;
        }

        csd->get_sample_rate = (get_sample_rate_t)dlsym(csd->csd_client,
                                             "csd_client_get_sample_rate");
        if (csd->get_sample_rate == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_get_sample_rate",
                  __func__, dlerror());

            goto error;
        }

        csd->init = (init_t)dlsym(csd->csd_client, "csd_client_init");

        if (csd->init == NULL) {
            ALOGE("%s: dlsym error %s for csd_client_init",
                  __func__, dlerror());
            goto error;
        } else {
            csd->init(i2s_ext_modem);
        }
    }
    return csd;

error:
    free(csd);
    csd = NULL;
    return csd;
}

void close_csd_client(struct csd_data *csd)
{
    if (csd != NULL) {
        csd->deinit();
        dlclose(csd->csd_client);
        free(csd);
        csd = NULL;
    }
}

static bool platform_is_i2s_ext_modem(const char *snd_card_name,
                                      struct platform_data *plat_data)
{
    plat_data->is_i2s_ext_modem = false;

    if (!strncmp(snd_card_name, "apq8084-taiko-i2s-mtp-snd-card",
                 sizeof("apq8084-taiko-i2s-mtp-snd-card")) ||
        !strncmp(snd_card_name, "apq8084-taiko-i2s-cdp-snd-card",
                 sizeof("apq8084-taiko-i2s-cdp-snd-card")) ||
        !strncmp(snd_card_name, "apq8096-tasha-i2c-snd-card",
                 sizeof("apq8096-tasha-i2c-snd-card")) ||
        !strncmp(snd_card_name, "sdx-tavil-i2s-snd-card",
                 sizeof("sdx-tavil-i2s-snd-card")) ||
        !strncmp(snd_card_name, "sda845-tavil-i2s-snd-card",
                 sizeof("sda845-tavil-i2s-snd-card")) ||
        !strncmp(snd_card_name, "sa6155-adp-star-snd-card",
                 sizeof("sa6155-adp-star-snd-card"))) {
        plat_data->is_i2s_ext_modem = true;
    }
    ALOGV("%s, is_i2s_ext_modem:%d soundcard name is %s",__func__,
           plat_data->is_i2s_ext_modem, snd_card_name);

    return plat_data->is_i2s_ext_modem;
}

static bool is_auto_snd_card(const char *snd_card_name)
{
    bool is_auto_snd_card = false;

    if (!strncmp(snd_card_name, "sa6155-adp-star-snd-card",
                 sizeof("sa6155-adp-star-snd-card"))) {
        is_auto_snd_card = true;
        ALOGV("%s : Auto snd card detected: soundcard name is %s",__func__,
               snd_card_name);
    }

    return is_auto_snd_card;
}

static void set_platform_defaults(struct platform_data * my_data)
{
    int32_t dev;
    unsigned int count = 0;
    const char *MEDIA_MIMETYPE_AUDIO_ALAC = "audio/alac";
    const char *MEDIA_MIMETYPE_AUDIO_APE = "audio/x-ape";

    for (dev = 0; dev < SND_DEVICE_MAX; dev++) {
        backend_tag_table[dev] = NULL;
        hw_interface_table[dev] = NULL;
        operator_specific_device_table[dev] = NULL;
        external_specific_device_table[dev] = NULL;
        /* Init island cfg and power mode */
        my_data->island_cfg[dev].mixer_ctl = NULL;
        my_data->power_mode_cfg[dev].mixer_ctl = NULL;
        my_data->island_cfg[dev].enable = false;
        my_data->power_mode_cfg[dev].enable = false;
    }
    for (dev = 0; dev < SND_DEVICE_MAX; dev++) {
        backend_bit_width_table[dev] = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    }

    // To overwrite these go to the audio_platform_info.xml file.
    backend_tag_table[SND_DEVICE_IN_BT_SCO_MIC] = strdup("bt-sco");
    backend_tag_table[SND_DEVICE_IN_BT_SCO_MIC_WB] = strdup("bt-sco-wb");
    backend_tag_table[SND_DEVICE_IN_BT_SCO_MIC_NREC] = strdup("bt-sco");
    backend_tag_table[SND_DEVICE_IN_BT_SCO_MIC_WB_NREC] = strdup("bt-sco-wb");
    backend_tag_table[SND_DEVICE_IN_BT_SCO_MIC_SWB] = strdup("bt-sco-swb");
    backend_tag_table[SND_DEVICE_IN_BT_SCO_MIC_SWB_NREC] = strdup("bt-sco-swb");
    backend_tag_table[SND_DEVICE_IN_SPDIF] = strdup("spdif-in");
    backend_tag_table[SND_DEVICE_IN_HDMI_MIC] = strdup("hdmi-in");
    backend_tag_table[SND_DEVICE_IN_HDMI_ARC] = strdup("hdmi-arc-in");
    backend_tag_table[SND_DEVICE_OUT_BT_SCO] = strdup("bt-sco");
    backend_tag_table[SND_DEVICE_OUT_BT_SCO_WB] = strdup("bt-sco-wb");
    backend_tag_table[SND_DEVICE_OUT_BT_SCO_SWB] = strdup("bt-sco-swb");
    backend_tag_table[SND_DEVICE_OUT_HDMI] = strdup("hdmi");
    backend_tag_table[SND_DEVICE_OUT_SPEAKER_AND_HDMI] = strdup("speaker-and-hdmi");
    backend_tag_table[SND_DEVICE_OUT_DISPLAY_PORT] = strdup("display-port");
    backend_tag_table[SND_DEVICE_OUT_DISPLAY_PORT1] = strdup("display-port1");
    backend_tag_table[SND_DEVICE_OUT_SPEAKER_AND_DISPLAY_PORT] = strdup("speaker-and-display-port");
    backend_tag_table[SND_DEVICE_OUT_SPEAKER_AND_DISPLAY_PORT1] = strdup("speaker-and-display-port1");
    backend_tag_table[SND_DEVICE_OUT_VOICE_TX] = strdup("afe-proxy");
    backend_tag_table[SND_DEVICE_IN_VOICE_RX] = strdup("afe-proxy");
    backend_tag_table[SND_DEVICE_OUT_AFE_PROXY] = strdup("afe-proxy");
    backend_tag_table[SND_DEVICE_OUT_USB_HEADSET] = strdup("usb-headset");
    backend_tag_table[SND_DEVICE_OUT_VOICE_USB_HEADSET] = strdup("usb-headset");
    backend_tag_table[SND_DEVICE_OUT_USB_HEADPHONES] = strdup("usb-headphones");
    backend_tag_table[SND_DEVICE_OUT_VOICE_TTY_FULL_USB] = strdup("usb-headset");
    backend_tag_table[SND_DEVICE_OUT_VOICE_TTY_VCO_USB] = strdup("usb-headphones");
    backend_tag_table[SND_DEVICE_OUT_VOICE_USB_HEADPHONES] = strdup("usb-headphones");
    backend_tag_table[SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET] =
        strdup("speaker-and-usb-headphones");
    backend_tag_table[SND_DEVICE_OUT_SPEAKER_SAFE_AND_USB_HEADSET] =
        strdup("speaker-safe-and-usb-headphones");
    backend_tag_table[SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO] =
        strdup("speaker-safe-and-bt-sco");
    backend_tag_table[SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO_WB] =
        strdup("speaker-safe-and-bt-sco-wb");
    backend_tag_table[SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO_SWB] =
        strdup("speaker-safe-and-bt-sco-swb");
    backend_tag_table[SND_DEVICE_IN_VOICE_TTY_FULL_USB_MIC] = strdup("usb-headset-mic");
    backend_tag_table[SND_DEVICE_IN_VOICE_TTY_HCO_USB_MIC] = strdup("usb-headset-mic");
    backend_tag_table[SND_DEVICE_IN_USB_HEADSET_MIC] = strdup("usb-headset-mic");
    backend_tag_table[SND_DEVICE_IN_VOICE_USB_HEADSET_MIC] = strdup("usb-headset-mic");
    backend_tag_table[SND_DEVICE_IN_UNPROCESSED_USB_HEADSET_MIC] = strdup("usb-headset-mic");
    backend_tag_table[SND_DEVICE_IN_VOICE_RECOG_USB_HEADSET_MIC] = strdup("usb-headset-mic");
    backend_tag_table[SND_DEVICE_IN_USB_HEADSET_MIC_AEC] = strdup("usb-headset-mic");
    backend_tag_table[SND_DEVICE_IN_USB_HEADSET_MULTI_CHANNEL_MIC] = strdup("usb-headset-mic");
    backend_tag_table[SND_DEVICE_IN_UNPROCESSED_USB_HEADSET_MULTI_CHANNEL_MIC] = strdup("usb-headset-mic");
    backend_tag_table[SND_DEVICE_IN_VOICE_RECOG_USB_HEADSET_MULTI_CHANNEL_MIC] = strdup("usb-headset-mic");
    backend_tag_table[SND_DEVICE_IN_USB_HEADSET_MULTI_CHANNEL_MIC_AEC] = strdup("usb-headset-mic");
    backend_tag_table[SND_DEVICE_IN_CAPTURE_FM] = strdup("capture-fm");
    backend_tag_table[SND_DEVICE_OUT_TRANSMISSION_FM] = strdup("transmission-fm");
    backend_tag_table[SND_DEVICE_OUT_HEADPHONES_DSD] = strdup("headphones-dsd");
    backend_tag_table[SND_DEVICE_OUT_HEADPHONES_HIFI_FILTER] = strdup("headphones-hifi-filter");
    backend_tag_table[SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_HIFI_FILTER] =
        strdup("speaker-and-headphones-hifi-filter");
    backend_tag_table[SND_DEVICE_OUT_SPEAKER_AND_HAPTICS] =
        strdup("speaker-and-haptics");
    backend_tag_table[SND_DEVICE_OUT_HEADPHONES_AND_HAPTICS] =
        strdup("headphones-and-haptics");
    backend_tag_table[SND_DEVICE_OUT_HEADPHONES_44_1] = strdup("headphones-44.1");
    backend_tag_table[SND_DEVICE_OUT_VOICE_SPEAKER_VBAT] = strdup("voice-speaker-vbat");
    backend_tag_table[SND_DEVICE_OUT_VOICE_SPEAKER_2_VBAT] = strdup("voice-speaker-2-vbat");
    backend_tag_table[SND_DEVICE_OUT_BT_A2DP] = strdup("bt-a2dp");
    backend_tag_table[SND_DEVICE_IN_BT_A2DP] = strdup("bt-a2dp-cap");
    backend_tag_table[SND_DEVICE_OUT_SPEAKER_AND_BT_A2DP] = strdup("speaker-and-bt-a2dp");
    backend_tag_table[SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_A2DP] = strdup("speaker-safe-and-bt-a2dp");
    backend_tag_table[SND_DEVICE_OUT_SPEAKER_SAFE_AND_HEADPHONES] = strdup("speaker-safe-and-headphones");
    backend_tag_table[SND_DEVICE_OUT_SPEAKER_SAFE_AND_LINE] = strdup("speaker-safe-and-line");
    backend_tag_table[SND_DEVICE_OUT_USB_HEADSET_SPEC] = strdup("usb-headset");
    backend_tag_table[SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_HEADPHONES] = strdup("speaker-and-headphones");
    backend_tag_table[SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_ANC_HEADSET] = strdup("speaker-and-headphones");
    backend_tag_table[SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_ANC_FB_HEADSET] = strdup("speaker-and-headphones");
    backend_tag_table[SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_AND_VOICE_HEADPHONES] = strdup("speaker-and-headphones");
    backend_tag_table[SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_AND_VOICE_ANC_HEADSET] = strdup("speaker-and-headphones");
    backend_tag_table[SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_AND_VOICE_ANC_FB_HEADSET] = strdup("speaker-and-headphones");
    backend_tag_table[SND_DEVICE_OUT_VOICE_HEARING_AID] = strdup("hearing-aid");
    backend_tag_table[SND_DEVICE_IN_VOICE_SPEAKER_MIC_HFP_MMSECNS] = strdup("bt-sco-mmsecns");
    backend_tag_table[SND_DEVICE_OUT_CALL_PROXY] = strdup("call-proxy");
    backend_tag_table[SND_DEVICE_IN_CALL_PROXY] = strdup("call-proxy-in");

    hw_interface_table[SND_DEVICE_OUT_HANDSET] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_EXTERNAL_1] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_EXTERNAL_2] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_REVERSE] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_SAFE] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_VBAT] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_LINE] = strdup("SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_HEADPHONES] = strdup("SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_HEADPHONES_DSD] = strdup("SLIMBUS_2_RX");
    hw_interface_table[SND_DEVICE_OUT_HEADPHONES_44_1] = strdup("SLIMBUS_5_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES] = strdup("SLIMBUS_0_RX-and-SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_SAFE_AND_HEADPHONES] = strdup("SLIMBUS_0_RX-and-SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_HEADPHONES] = strdup("SLIMBUS_0_RX-and-SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_ANC_HEADSET] = strdup("SLIMBUS_0_RX-and-SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_ANC_FB_HEADSET] = strdup("SLIMBUS_0_RX-and-SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_AND_VOICE_HEADPHONES] = strdup("SLIMBUS_0_RX-and-SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_AND_VOICE_ANC_HEADSET] = strdup("SLIMBUS_0_RX-and-SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_AND_VOICE_ANC_FB_HEADSET] = strdup("SLIMBUS_0_RX-and-SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_AND_LINE] = strdup("SLIMBUS_0_RX-and-SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_SAFE_AND_LINE] = strdup("SLIMBUS_0_RX-and-SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_1] = strdup("SLIMBUS_0_RX-and-SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_2] = strdup("SLIMBUS_0_RX-and-SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_HANDSET] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_HAC_HANDSET] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_STEREO] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_VBAT] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_2] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_2_VBAT] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_HEADPHONES] = strdup("SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_HEADSET] = strdup("SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_MUSIC_TX] = strdup("VOICE_PLAYBACK_TX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_LINE] = strdup("SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_HDMI] = strdup("HDMI");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_AND_HDMI] = strdup("SLIMBUS_0_RX-and-HDMI");
    hw_interface_table[SND_DEVICE_OUT_DISPLAY_PORT] = strdup("DISPLAY_PORT");
    hw_interface_table[SND_DEVICE_OUT_DISPLAY_PORT1] = strdup("DISPLAY_PORT1");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_AND_DISPLAY_PORT] = strdup("SLIMBUS_0_RX-and-DISPLAY_PORT");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_AND_DISPLAY_PORT1] = strdup("SLIMBUS_0_RX-and-DISPLAY_PORT1");
    hw_interface_table[SND_DEVICE_OUT_BT_SCO] = strdup("SLIMBUS_7_RX");
    hw_interface_table[SND_DEVICE_OUT_BT_SCO_WB] = strdup("SLIMBUS_7_RX");
    hw_interface_table[SND_DEVICE_OUT_BT_SCO_SWB] = strdup("SLIMBUS_7_RX");
    hw_interface_table[SND_DEVICE_OUT_BT_A2DP] = strdup("SLIMBUS_7_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_AND_BT_A2DP] = strdup("SLIMBUS_0_RX-and-SLIMBUS_7_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_A2DP] =
        strdup("SLIMBUS_0_RX-and-SLIMBUS_7_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_HANDSET_TMUS] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_TTY_FULL_HEADPHONES] = strdup("SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_TTY_FULL_HEADSET] = strdup("SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_TTY_VCO_HEADPHONES] = strdup("SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_TTY_HCO_HANDSET] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_TTY_FULL_USB] = strdup("USB_AUDIO_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_TTY_VCO_USB] = strdup("USB_AUDIO_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_TX] = strdup("RT_PROXY_DAI_001_RX");
    hw_interface_table[SND_DEVICE_OUT_AFE_PROXY] = strdup("RT_PROXY_DAI_001_RX");
    hw_interface_table[SND_DEVICE_OUT_USB_HEADSET] = strdup("USB_AUDIO_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_USB_HEADSET] = strdup("USB_AUDIO_RX");
    hw_interface_table[SND_DEVICE_OUT_USB_HEADPHONES] = strdup("USB_AUDIO_RX");
    hw_interface_table[SND_DEVICE_OUT_USB_HEADSET_SPEC] = strdup("USB_AUDIO_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_SAFE_AND_USB_HEADSET] =
        strdup("SLIMBUS_0_RX-and-USB_AUDIO_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_USB_HEADPHONES] = strdup("USB_AUDIO_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET] = strdup("SLIMBUS_0_RX-and-USB_AUDIO_RX");
    hw_interface_table[SND_DEVICE_OUT_TRANSMISSION_FM] = strdup("SLIMBUS_8_TX");
    hw_interface_table[SND_DEVICE_OUT_ANC_HEADSET] = strdup("SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_ANC_FB_HEADSET] = strdup("SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_ANC_HEADSET] = strdup("SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_ANC_FB_HEADSET] = strdup("SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET] = strdup("SLIMBUS_0_RX-and-SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_AND_ANC_FB_HEADSET] = strdup("SLIMBUS_0_RX-and-SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_OUT_ANC_HANDSET] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_PROTECTED] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_HFP] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_AND_BT_SCO] = strdup("SLIMBUS_0_RX-and-SEC_AUX_PCM_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_AND_BT_SCO_WB] = strdup("SLIMBUS_0_RX-and-SEC_AUX_PCM_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_AND_BT_SCO_SWB] = strdup("SLIMBUS_0_RX-and-SEC_AUX_PCM_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO] = strdup("QUAT_TDM_RX_0-and-SLIMBUS_7_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO_WB] = strdup("QUAT_TDM_RX_0-and-SLIMBUS_7_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO_SWB] = strdup("QUAT_TDM_RX_0-and-SLIMBUS_7_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_PROTECTED] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED_VBAT] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED_VBAT] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_SPEAKER_WSA] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_WSA] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOICE_SPEAKER_2_WSA] = strdup("SLIMBUS_0_RX");
    /* So far, primary hal doesn't support hearing aid device.
       Need snd_device to route voice call and use specific acdb tuning.
       Also, BT_RX is a virtual port to indicate bluetooth hearing aid. */
    hw_interface_table[SND_DEVICE_OUT_VOICE_HEARING_AID] = strdup("BT_RX"),
    hw_interface_table[SND_DEVICE_OUT_BUS_MEDIA] = strdup("TERT_TDM_RX_0");
    hw_interface_table[SND_DEVICE_OUT_BUS_SYS] = strdup("TERT_TDM_RX_0");
    hw_interface_table[SND_DEVICE_OUT_BUS_NAV] = strdup("TERT_TDM_RX_1");
    hw_interface_table[SND_DEVICE_OUT_BUS_PHN] = strdup("TERT_TDM_RX_2");
    hw_interface_table[SND_DEVICE_OUT_BUS_PAX] = strdup("QUAT_TDM_RX_0");
    hw_interface_table[SND_DEVICE_OUT_BUS_RSE] = strdup("QUIN_TDM_RX_0");
    hw_interface_table[SND_DEVICE_OUT_CALL_PROXY] = strdup("CALL_PROXY_RX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_MIC_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_MIC_NN] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_MIC_EXTERNAL] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_MIC_AEC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_MIC_AEC_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_MIC_AEC_NN] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_MIC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_MIC_NS_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_MIC_NS_NN] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_MIC_AEC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_MIC_AEC_NS_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_MIC_AEC_NS_NN] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_DMIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_DMIC_AEC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_DMIC_AEC_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_DMIC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_DMIC_NS_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_DMIC_AEC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_DMIC_AEC_NS_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_DMIC_STEREO] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HEADSET_MIC_AEC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_CAMCORDER_LANDSCAPE] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_MIC_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_MIC_NN] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_MIC_AEC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_MIC_AEC_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_MIC_AEC_NN] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_MIC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_MIC_NS_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_MIC_NS_NN] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_MIC_AEC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_MIC_AEC_NS_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_MIC_AEC_NS_NN] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_DMIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_DMIC_AEC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_DMIC_AEC_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_DMIC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_DMIC_NS_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_DMIC_STEREO] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HEADSET_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HEADSET_MIC_FLUENCE] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_SPEAKER_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_SPEAKER_MIC_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_SPEAKER_MIC_NN] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_HEADSET_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPDIF] = strdup("PRI_SPDIF_TX");
    hw_interface_table[SND_DEVICE_IN_HDMI_MIC] = strdup("SEC_MI2S_TX");
    hw_interface_table[SND_DEVICE_IN_HDMI_ARC] = strdup("SEC_SPDIF_TX");
    hw_interface_table[SND_DEVICE_IN_BT_SCO_MIC] = strdup("SLIMBUS_7_TX");
    hw_interface_table[SND_DEVICE_IN_BT_SCO_MIC_NREC] = strdup("SLIMBUS_7_TX");
    hw_interface_table[SND_DEVICE_IN_BT_SCO_MIC_WB] = strdup("SLIMBUS_7_TX");
    hw_interface_table[SND_DEVICE_IN_BT_SCO_MIC_WB_NREC] = strdup("SLIMBUS_7_TX");
    hw_interface_table[SND_DEVICE_IN_BT_SCO_MIC_SWB] = strdup("SLIMBUS_7_TX");
    hw_interface_table[SND_DEVICE_IN_BT_SCO_MIC_SWB_NREC] = strdup("SLIMBUS_7_TX");
    hw_interface_table[SND_DEVICE_IN_BT_A2DP] = strdup("SLIMBUS_7_TX");
    hw_interface_table[SND_DEVICE_IN_CAMCORDER_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_DMIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_DMIC_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_DMIC_TMUS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_SPEAKER_DMIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_SPEAKER_DMIC_SB] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_SPEAKER_MIC_HFP] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_SPEAKER_MIC_HFP_MMSECNS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_SPEAKER_TMIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_SPEAKER_QMIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_TTY_FULL_HEADSET_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_TTY_VCO_HANDSET_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_TTY_HCO_HEADSET_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_REC_HEADSET_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_TTY_FULL_USB_MIC] = strdup("USB_AUDIO_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_TTY_HCO_USB_MIC] = strdup("USB_AUDIO_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_REC_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_REC_MIC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_REC_MIC_AEC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_REC_MIC_AEC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_REC_DMIC_STEREO] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_REC_DMIC_FLUENCE] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_RX] = strdup("RT_PROXY_DAI_002_TX");
    hw_interface_table[SND_DEVICE_IN_USB_HEADSET_MIC] = strdup("USB_AUDIO_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_USB_HEADSET_MIC] = strdup("USB_AUDIO_TX");
    hw_interface_table[SND_DEVICE_IN_USB_HEADSET_MIC_AEC] =  strdup("USB_AUDIO_TX");
    hw_interface_table[SND_DEVICE_IN_UNPROCESSED_USB_HEADSET_MIC] = strdup("USB_AUDIO_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_RECOG_USB_HEADSET_MIC] = strdup("USB_AUDIO_TX");
    hw_interface_table[SND_DEVICE_IN_USB_HEADSET_MULTI_CHANNEL_MIC] = strdup("USB_AUDIO_TX");
    hw_interface_table[SND_DEVICE_IN_CAPTURE_FM] = strdup("SLIMBUS_8_TX");
    hw_interface_table[SND_DEVICE_IN_AANC_HANDSET_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_QUAD_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_DMIC_STEREO] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_DMIC_STEREO] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_CAPTURE_VI_FEEDBACK] = strdup("SLIMBUS_4_TX");
    hw_interface_table[SND_DEVICE_IN_CAPTURE_VI_FEEDBACK_MONO_1] = strdup("SLIMBUS_4_TX");
    hw_interface_table[SND_DEVICE_IN_CAPTURE_VI_FEEDBACK_MONO_2] = strdup("SLIMBUS_4_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_SPEAKER_DMIC_BROADSIDE] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_DMIC_BROADSIDE] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_DMIC_AEC_BROADSIDE] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_DMIC_NS_BROADSIDE] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_BROADSIDE] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_FLUENCE_DMIC_AANC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_QMIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_QMIC_AEC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_QMIC_AEC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_QMIC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_QMIC_AEC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_REC_QMIC_FLUENCE] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_THREE_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_TMIC_FLUENCE_PRO] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_TMIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_TMIC_AEC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_TMIC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_TMIC_AEC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_TMIC_AEC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_TMIC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_SPEAKER_TMIC_AEC_NS] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_REC_TMIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_UNPROCESSED_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_UNPROCESSED_STEREO_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_UNPROCESSED_THREE_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_UNPROCESSED_QUAD_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_UNPROCESSED_HEADSET_MIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_HANDSET_GENERIC_QMIC] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_INCALL_REC_RX] = strdup("INCALL_RECORD_RX");
    hw_interface_table[SND_DEVICE_IN_INCALL_REC_TX] = strdup("INCALL_RECORD_TX");
    hw_interface_table[SND_DEVICE_IN_LINE] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_OUT_VOIP_HANDSET] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOIP_SPEAKER] = strdup("SLIMBUS_0_RX");
    hw_interface_table[SND_DEVICE_OUT_VOIP_HEADPHONES] = strdup("SLIMBUS_6_RX");
    hw_interface_table[SND_DEVICE_IN_CAMCORDER_INVERT_LANDSCAPE] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_CAMCORDER_PORTRAIT] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_CAMCORDER_SELFIE_LANDSCAPE] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_CAMCORDER_SELFIE_INVERT_LANDSCAPE] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_CAMCORDER_SELFIE_PORTRAIT] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_VOICE_HEARING_AID] = strdup("SLIMBUS_0_TX");
    hw_interface_table[SND_DEVICE_IN_BUS] = strdup("TERT_TDM_TX_0");
    hw_interface_table[SND_DEVICE_IN_CALL_PROXY] = strdup("CALL_PROXY_TX");
    my_data->max_mic_count = PLATFORM_DEFAULT_MIC_COUNT;

     /*remove ALAC & APE from DSP decoder list based on software decoder availability*/
     for (count = 0; count < (int32_t)(sizeof(dsp_only_decoders_mime)/sizeof(dsp_only_decoders_mime[0]));
            count++) {

         if (!strncmp(MEDIA_MIMETYPE_AUDIO_ALAC, dsp_only_decoders_mime[count],
              strlen(dsp_only_decoders_mime[count]))) {

             if(property_get_bool("vendor.audio.use.sw.alac.decoder", false)) {
                 ALOGD("Alac software decoder is available...removing alac from DSP decoder list");
                 strlcpy(dsp_only_decoders_mime[count],"none",5);
             }
         } else if (!strncmp(MEDIA_MIMETYPE_AUDIO_APE, dsp_only_decoders_mime[count],
              strlen(dsp_only_decoders_mime[count]))) {

             if(property_get_bool("vendor.audio.use.sw.ape.decoder", false)) {
                 ALOGD("APE software decoder is available...removing ape from DSP decoder list");
                 strlcpy(dsp_only_decoders_mime[count],"none",5);
             }
         }
     }
}

void get_cvd_version(char *cvd_version, struct audio_device *adev)
{
    struct mixer_ctl *ctl;
    int count;
    int ret = 0;

    ctl = mixer_get_ctl_by_name(adev->mixer, CVD_VERSION_MIXER_CTL);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",  __func__, CVD_VERSION_MIXER_CTL);
        goto done;
    }
    mixer_ctl_update(ctl);

    count = mixer_ctl_get_num_values(ctl);
    if (count > MAX_CVD_VERSION_STRING_SIZE)
        count = MAX_CVD_VERSION_STRING_SIZE - 1;

    ret = mixer_ctl_get_array(ctl, cvd_version, count);
    if (ret != 0) {
        ALOGE("%s: ERROR! mixer_ctl_get_array() failed to get CVD Version", __func__);
        goto done;
    }

done:
    return;
}

static int hw_util_open(int card_no)
{
    int fd = -1;
    char dev_name[256];

    snprintf(dev_name, sizeof(dev_name), "/dev/snd/hwC%uD%u",
                               card_no, WCD9XXX_CODEC_HWDEP_NODE);
    ALOGD("%s Opening device %s\n", __func__, dev_name);
    fd = open(dev_name, O_WRONLY);
    if (fd < 0) {
        ALOGE("%s: cannot open device '%s'\n", __func__, dev_name);
        return fd;
    }
    ALOGD("%s success", __func__);
    return fd;
}

struct param_data {
    int    use_case;
    int    acdb_id;
    int    get_size;
    int    buff_size;
    int    data_size;
    void   *buff;
};

static int send_vbat_adc_data_to_acdb(struct platform_data *plat_data, char *cal_type)
{
    int ret = 0;
    struct mixer_ctl *ctl;
    uint16_t vbat_adc_data[2];
    struct platform_data *my_data = plat_data;
    struct audio_device *adev = my_data->adev;

    const char *mixer_ctl_name = "Vbat ADC data";

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer ctl name - %s",
               __func__, mixer_ctl_name);
        ret = -EINVAL;
        goto done;
    }

    vbat_adc_data[0] = mixer_ctl_get_value(ctl, 0);
    vbat_adc_data[1] = mixer_ctl_get_value(ctl, 1);

    ALOGD("%s: Vbat ADC output values: Dcp1: %d , Dcp2: %d",
           __func__, vbat_adc_data[0], vbat_adc_data[1]);

    ret = my_data->acdb_set_codec_data(&vbat_adc_data[0], cal_type);

done:
    return ret;
}

static void send_codec_cal(acdb_loader_get_calibration_t acdb_loader_get_calibration,
                           struct platform_data *plat_data, int fd)
{
    int type;

    for (type = WCD9XXX_ANC_CAL; type < WCD9XXX_MAX_CAL; type++) {
        struct wcdcal_ioctl_buffer codec_buffer;
        struct param_data calib;
        int ret;

        /* MAD calibration is handled by sound trigger HAL, skip here */
        if (type == WCD9XXX_MAD_CAL)
            continue;

        ret = 0;

        if ((plat_data->is_vbat_speaker) && (WCD9XXX_VBAT_CAL == type)) {
           /* VBAT BCL speaker does not need tuning data */
           if (!plat_data->is_bcl_speaker)
               return;
           ret = send_vbat_adc_data_to_acdb(plat_data, cal_name_info[type]);
           if (ret < 0)
               ALOGE("%s error in sending vbat adc data to acdb", __func__);
        }

        calib.get_size = 1;
        ret = acdb_loader_get_calibration(cal_name_info[type],
                                          sizeof(struct param_data),
                                          &calib);
        if (ret < 0) {
            ALOGE("%s: %s get_calibration size failed, err = %d\n",
                  __func__, cal_name_info[type], ret);
            continue;
        }

        calib.get_size = 0;
        calib.buff = malloc(calib.buff_size);
        if (!calib.buff) {
            ALOGE("%s: %s: No Memory for size = %d\n",
                  __func__, cal_name_info[type], calib.buff_size);
            continue;
        }

        ret = acdb_loader_get_calibration(cal_name_info[type],
                              sizeof(struct param_data), &calib);
        if (ret < 0) {
            ALOGE("%s: %s get_calibration failed, err = %d\n",
                  __func__, cal_name_info[type], ret);
            free(calib.buff);
            continue;
        }

        codec_buffer.buffer = calib.buff;
        codec_buffer.size = calib.data_size;
        codec_buffer.cal_type = type;
        if (ioctl(fd, SNDRV_CTL_IOCTL_HWDEP_CAL_TYPE, &codec_buffer) < 0)
            ALOGE("%s: %s Failed to call ioctl, err=%d",
                  __func__, cal_name_info[type], errno);
        else
            ALOGD("%s: %s cal sent successfully\n",
              __func__, cal_name_info[type]);

        free(calib.buff);
    }
}

static void audio_hwdep_send_cal(struct platform_data *plat_data)
{
    int fd = plat_data->hw_dep_fd;

    if (fd < 0)
        fd = hw_util_open(plat_data->adev->snd_card);
    if (fd == -1) {
        ALOGE("%s error open\n", __func__);
        return;
    }

    acdb_loader_get_calibration = (acdb_loader_get_calibration_t)
          dlsym(plat_data->acdb_handle, "acdb_loader_get_calibration");

    if (acdb_loader_get_calibration == NULL) {
        ALOGE("%s: ERROR. dlsym Error:%s acdb_loader_get_calibration", __func__,
           dlerror());
        if (fd >= 0) {
            close(fd);
            plat_data->hw_dep_fd = -1;
        }
        return;
    }

    send_codec_cal(acdb_loader_get_calibration, plat_data, fd);
    plat_data->hw_dep_fd = fd;
}

const char * platform_get_snd_card_name_for_acdb_loader(const char *snd_card_name)
{
    const char *acdb_card_name = NULL;
    char *substring = NULL;
    char string[MAX_SND_CARD_NAME_LENGTH] = {0};
    int length = 0;

    if (snd_card_name == NULL)
        return NULL;

    /* Both tasha & tasha-lite uses tasha ACDB files
       simulate sound card name for tasha lite, so that
       ACDB module loads tasha ACDB files for tasha lite */
    if ((substring = strstr(snd_card_name, "tashalite")) ||
        (substring = strstr(snd_card_name, "tasha9326"))) {
        ALOGD("%s: using tasha ACDB files for tasha-lite", __func__);
        length = substring - snd_card_name + 1;
        snprintf(string, length, "%s", snd_card_name);
        strlcat(string, "tasha-snd-card", sizeof(string));
        acdb_card_name = strdup(string);
        return acdb_card_name;
    }
    acdb_card_name = strdup(snd_card_name);
    return acdb_card_name;
}

static int platform_acdb_init(void *platform)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    char *cvd_version = NULL;
    const char *snd_card_name;
    int result = -1;
    struct listnode *node;
    struct meta_key_list *key_info;
    int key = 0;

    cvd_version = calloc(1, MAX_CVD_VERSION_STRING_SIZE);
    if (!cvd_version) {
        ALOGE("Failed to allocate cvd version");
        return -1;
    } else {
        get_cvd_version(cvd_version, my_data->adev);
    }

    snd_card_name = mixer_get_name(my_data->adev->mixer);
    snd_card_name = platform_get_snd_card_name_for_acdb_loader(snd_card_name);
    if (!snd_card_name) {
        ALOGE("Failed to get snd_card_name");
        goto cleanup;
    }

    my_data->acdb_init_data.cvd_version = cvd_version;
    my_data->acdb_init_data.snd_card_name = strdup(snd_card_name);
    my_data->acdb_init_data.meta_key_list = &my_data->acdb_meta_key_list;
    if (my_data->acdb_init_v4) {
        result = my_data->acdb_init_v4(&my_data->acdb_init_data, ACDB_LOADER_INIT_V4);
    } else if (my_data->acdb_init_v3) {
        result = my_data->acdb_init_v3(snd_card_name, cvd_version,
                                           &my_data->acdb_meta_key_list);
    } else if (my_data->acdb_init) {
        node = list_head(&my_data->acdb_meta_key_list);
        key_info = node_to_item(node, struct meta_key_list, list);
        key = key_info->cal_info.nKey;
        result = my_data->acdb_init(snd_card_name, cvd_version, key);
    }

    /* Save these variables in platform_data. These will be used
       while reloading ACDB files during run time. */
    strlcpy(my_data->cvd_version, cvd_version, MAX_CVD_VERSION_STRING_SIZE);
    strlcpy(my_data->snd_card_name, snd_card_name,
                                               MAX_SND_CARD_STRING_SIZE);

cleanup:
    if (cvd_version)
        free(cvd_version);
    if (!result) {
        my_data->is_acdb_initialized = true;
        ALOGD("ACDB initialized");
        audio_hwdep_send_cal(my_data);
    } else {
        my_data->is_acdb_initialized = false;
        ALOGD("ACDB initialization failed");
    }
    return result;
}

#define MAX_PATH             (256)
#define THERMAL_SYSFS "/sys/class/thermal"
#define TZ_TYPE "/sys/class/thermal/thermal_zone%d/type"
#define TZ_WSA "/sys/class/thermal/thermal_zone%d/temp"

static bool check_and_get_wsa_info(char *snd_card_name, int *wsaCount,
                                   bool *is_wsa_combo_supported)
{
    DIR *tdir = NULL;
    struct dirent *tdirent = NULL;
    int tzn = 0;
    char name[MAX_PATH] = {0};
    char cwd[MAX_PATH] = {0};
    char file[10] = "wsa";
    bool found = false;
    int wsa_count = 0;

    if (!getcwd(cwd, sizeof(cwd)))
        return false;

    chdir(THERMAL_SYSFS); /* Change dir to read the entries. Doesnt work
                             otherwise */
    tdir = opendir(THERMAL_SYSFS);
    if (!tdir) {
        ALOGE("Unable to open %s\n", THERMAL_SYSFS);
        return false;
    }

    while ((tdirent = readdir(tdir))) {
        char buf[50];
        struct dirent *tzdirent;
        DIR *tzdir = NULL;

        tzdir = opendir(tdirent->d_name);
        if (!tzdir)
            continue;
        while ((tzdirent = readdir(tzdir))) {
            if (strcmp(tzdirent->d_name, "type"))
                continue;
            snprintf(name, MAX_PATH, TZ_TYPE, tzn);
            ALOGD("Opening %s\n", name);
            read_line_from_file(name, buf, sizeof(buf));
            if (strstr(buf, file)) {
                if (property_get_bool("vendor.audio.read.wsatz.type", false)) {
                    struct str_parms *parms = NULL;
                    buf[strlen(buf) - 1] = '\0';
                    audio_extn_spkr_prot_set_parameters(parms, buf, 0);
                }
                wsa_count++;
                /*We support max only two WSA speakers*/
                if (wsa_count == 2)
                    break;
            }
            tzn++;
        }
        closedir(tzdir);
    }
    if (wsa_count > 0){
         ALOGD("Found %d WSA present on the platform", wsa_count);
         found = true;
         *wsaCount = wsa_count;

        /* update wsa combo supported flag based on sound card name */
        /* wsa combo flag needs to be set to true only for hardware
           combinations which has support for both wsa and non-wsa speaker */
        *is_wsa_combo_supported = false;
        if(snd_card_name) {
                if (((!strncmp(snd_card_name, "sdm439-sku1-snd-card",
                    sizeof("sdm439-sku1-snd-card"))) ||
                (!strncmp(snd_card_name, "sdm439-snd-card-mtp",
                    sizeof("sdm439-snd-card-mtp"))) ||
                (!strncmp(snd_card_name, "msm8953-snd-card-mtp",
                    sizeof("msm8953-snd-card-mtp"))) ||
                (!strncmp(snd_card_name, "msm8953-sku4-snd-card",
                    sizeof("msm8953-sku4-snd-card"))))) {
                *is_wsa_combo_supported = true;
            }
        }
    }
    closedir(tdir);
    chdir(cwd); /* Restore current working dir */
    return found;
}

static void get_source_mic_type(struct platform_data * my_data)
{
    // support max to mono, example if max count is 3, usecase supports Three, dual and mono mic
    switch (my_data->max_mic_count) {
        case 6:
            my_data->source_mic_type |= SOURCE_HEX_MIC;
        case 4:
            my_data->source_mic_type |= SOURCE_QUAD_MIC;
        case 3:
            my_data->source_mic_type |= SOURCE_THREE_MIC;
        case 2:
            my_data->source_mic_type |= SOURCE_DUAL_MIC;
        case 1:
            my_data->source_mic_type |= SOURCE_MONO_MIC;
            break;
        default:
            ALOGE("%s: max_mic_count (%d), is not supported, setting to default",
                   __func__, my_data->max_mic_count);
            my_data->source_mic_type = SOURCE_MONO_MIC | SOURCE_DUAL_MIC;
            break;
    }
}

/*
 * Retrieves the be_dai_name_table from kernel to enable a mapping
 * between sound device hw interfaces and backend IDs. This allows HAL to
 * specify the backend a specific calibration is needed for.
 */
static int init_be_dai_name_table(struct audio_device *adev)
{
    const char *mixer_ctl_name = "Backend DAI Name Table";
    struct mixer_ctl *ctl;
    int i, j, ret, size;
    bool valid_hw_interface;

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer name %s\n",
               __func__, mixer_ctl_name);
        ret = -EINVAL;
        goto done;
    }

    mixer_ctl_update(ctl);

    size = mixer_ctl_get_num_values(ctl);
    if (size <= 0){
        ALOGE("%s: Failed to get %s size %d\n",
               __func__, mixer_ctl_name, size);
        ret = -EFAULT;
        goto done;
    }

    be_dai_name_table =
        (const struct be_dai_name_struct *)calloc(1, size);
    if (be_dai_name_table == NULL) {
        ALOGE("%s: Failed to allocate memory for %s\n",
               __func__, mixer_ctl_name);
        ret = -ENOMEM;
        goto freeMem;
    }

    ret = mixer_ctl_get_array(ctl, (void *)be_dai_name_table, size);
    if (ret) {
        ALOGE("%s: Failed to get %s, ret %d\n",
               __func__, mixer_ctl_name, ret);
        ret = -EFAULT;
        goto freeMem;
    }

    if (be_dai_name_table != NULL) {
        max_be_dai_names = size / sizeof(struct be_dai_name_struct);
        ALOGV("%s: Successfully got %s, number of be dais is %d\n",
              __func__, mixer_ctl_name, max_be_dai_names);
        ret = 0;
    } else {
        ALOGE("%s: Failed to get %s\n", __func__, mixer_ctl_name);
        ret = -EFAULT;
        goto freeMem;
    }

    /*
     * Validate all sound devices have a valid backend set to catch
     * errors for uncommon sound devices
     */
    for (i = 0; i < SND_DEVICE_MAX; i++) {
        valid_hw_interface = false;

        if (hw_interface_table[i] == NULL) {
            ALOGW("%s: sound device %s has no hw interface set\n",
                  __func__, platform_get_snd_device_name(i));
            continue;
        }

        for (j = 0; j < max_be_dai_names; j++) {
            if (strcmp(hw_interface_table[i], be_dai_name_table[j].be_name)
                == 0) {
                valid_hw_interface = true;
                break;
            }
        }
        if (!valid_hw_interface)
            ALOGD("%s: sound device %s does not have a valid hw interface set (disregard for combo devices) %s\n",
                   __func__, platform_get_snd_device_name(i), hw_interface_table[i]);
    }

    goto done;

freeMem:
    if (be_dai_name_table) {
        free((void *)be_dai_name_table);
        be_dai_name_table = NULL;
    }

done:
    return ret;
}

#ifdef FLICKER_SENSOR_INPUT
static void configure_flicker_sensor_input(struct mixer *mixer)
{
    struct mixer_ctl *ctl;
    const char* ctl1 = "AIF3_CAP Mixer SLIM TX2";
    int setting1 = 1;
    const char* ctl2 = "CDC_IF TX2 MUX";
    const char* setting2 = "DEC2";
    const char* ctl3 = "SLIM_1_TX Channels";
    const char* setting3 = "One";
    const char* ctl4 = "ADC MUX2";
    const char* setting4 = "AMIC";
    const char* ctl5 = "AMIC MUX2";
    const char* setting5 = "ADC1";
    const char* ctl6 = "DEC2 Volume";
    int setting6 = 84;
    const char* ctl7 = "MultiMedia9 Mixer SLIM_1_TX";
    int setting7 = 1;
    const char* ctl8 = "SLIM_1_TX SampleRate";
    const char* setting8 = "KHZ_8";

    ctl = mixer_get_ctl_by_name(mixer, ctl1);
    mixer_ctl_set_value(ctl, 0, setting1);
    ctl = mixer_get_ctl_by_name(mixer, ctl2);
    mixer_ctl_set_enum_by_string(ctl, setting2);
    ctl = mixer_get_ctl_by_name(mixer, ctl3);
    mixer_ctl_set_enum_by_string(ctl, setting3);
    ctl = mixer_get_ctl_by_name(mixer, ctl4);
    mixer_ctl_set_enum_by_string(ctl, setting4);
    ctl = mixer_get_ctl_by_name(mixer, ctl5);
    mixer_ctl_set_enum_by_string(ctl, setting5);
    ctl = mixer_get_ctl_by_name(mixer, ctl6);
    mixer_ctl_set_value(ctl, 0, setting6);
    ctl = mixer_get_ctl_by_name(mixer, ctl7);
    mixer_ctl_set_value(ctl, 0, setting7);
    ctl = mixer_get_ctl_by_name(mixer, ctl8);
    mixer_ctl_set_enum_by_string(ctl, setting8);
}
#endif

void *platform_init(struct audio_device *adev)
{
    char platform[PROPERTY_VALUE_MAX];
    char baseband[PROPERTY_VALUE_MAX];
    char value[PROPERTY_VALUE_MAX];
    struct platform_data *my_data = NULL;
    char *snd_card_name = NULL;
    char mixer_xml_file[MIXER_PATH_MAX_LENGTH]= {0};
    char platform_info_file[MIXER_PATH_MAX_LENGTH]= {0};
    char ffspEnable[PROPERTY_VALUE_MAX];
    int idx;
    int wsaCount =0;
    bool is_wsa_combo_supported = false;
    struct mixer_ctl *ctl = NULL;
    const char *id_string = NULL;
    int cfg_value = -1;
    bool dual_mic_config = false;
    struct snd_card_split *snd_split_handle = NULL;

    list_init(&operator_info_list);
    list_init(&app_type_entry_list);

    adev->snd_card = audio_extn_utils_open_snd_mixer(&adev->mixer);
    if (adev->snd_card < 0) {
        ALOGE("%s: Unable to find correct sound card", __func__);
        return NULL;
    }
    ALOGD("%s: Opened sound card:%d", __func__, adev->snd_card);

    snd_card_name = strdup(mixer_get_name(adev->mixer));
    if (!snd_card_name) {
        ALOGE("failed to allocate memory for snd_card_name\n");
        audio_extn_utils_close_snd_mixer(adev->mixer);
        return NULL;
    }

    audio_extn_set_snd_card_split(snd_card_name);
    snd_split_handle = audio_extn_get_snd_card_split();

    my_data = calloc(1, sizeof(struct platform_data));
    if (!my_data) {
        ALOGE("failed to allocate platform data");
        if (snd_card_name)
            free(snd_card_name);
        audio_extn_utils_close_snd_mixer(adev->mixer);
        return NULL;
    }

    my_data->is_slimbus_interface = true;
    my_data->is_internal_codec = false;
    my_data->is_default_be_config = false;

    my_data->hw_info = hw_info_init(snd_card_name);
    if (!my_data->hw_info) {
        ALOGE("failed to init hw_info");
        audio_extn_utils_close_snd_mixer(adev->mixer);
        if (my_data)
            free(my_data);

        if (snd_card_name)
            free(snd_card_name);
        return NULL;
    }

    update_codec_type_and_interface(my_data, snd_card_name);

    adev->dp_allowed_for_voice =
        property_get_bool("vendor.audio.enable.dp.for.voice", false);

    my_data->adev = adev;
    my_data->fluence_in_spkr_mode = false;
    my_data->fluence_in_voice_call = false;
    my_data->fluence_in_voice_rec = false;
    my_data->fluence_in_audio_rec = false;
    my_data->fluence_in_hfp_call = false;
    my_data->external_spk_1 = false;
    my_data->external_spk_2 = false;
    my_data->external_mic = false;
    my_data->fluence_sb_enabled = false;
    my_data->fluence_nn_enabled = false;
    my_data->fluence_type = FLUENCE_NONE;
    my_data->fluence_mode = FLUENCE_ENDFIRE;
    my_data->ambisonic_capture = false;
    my_data->ambisonic_profile = false;
    my_data->slowtalk = false;
    my_data->hd_voice = false;
    my_data->is_wsa_speaker = false;
    my_data->hw_dep_fd = -1;
    my_data->mono_speaker = SPKR_1;
    my_data->speaker_lr_swap = false;
    my_data->voice_speaker_stereo = false;
    my_data->declared_mic_count = 0;
    my_data->spkr_ch_map = NULL;
    my_data->use_sprk_default_sample_rate = true;
    my_data->fluence_in_voice_comm = false;
    my_data->ec_car_state = false;
    my_data->is_multiple_sample_rate_combo_supported = true;
    platform_reset_edid_info(my_data);

    //set max volume step for voice call
    property_get("ro.config.vc_call_vol_steps", value, TOSTRING(MAX_VOL_INDEX));
    my_data->max_vol_index = atoi(value);

    be_dai_name_table = NULL;

    property_get("persist.vendor.audio.dualmic.config",value,"");
    if (!strcmp("endfire", value)) {
        dual_mic_config = true;
    }

    /* Check for Fluence Sub Band Enablement */
    if (property_get_bool("ro.vendor.audio.sdk.fluence.subband.enabled",false))
        my_data->fluence_sb_enabled = true;

    /* Check for Fluence NN Enablement */
    if (property_get_bool("ro.vendor.audio.sdk.fluence.nn.enabled",false))
        my_data->fluence_nn_enabled = true;

    my_data->fluence_type = FLUENCE_NONE;
    if (property_get("ro.vendor.audio.sdk.fluencetype",
                      my_data->fluence_cap, NULL) > 0) {
        if (!strncmp("fluencepro", my_data->fluence_cap, sizeof("fluencepro"))) {
            my_data->fluence_type = FLUENCE_QUAD_MIC | FLUENCE_DUAL_MIC;

            if (property_get_bool("persist.vendor.audio.fluence.tmic.enabled",false)) {
                my_data->fluence_type |= FLUENCE_TRI_MIC;
            }
        } else if (!strncmp("fluence", my_data->fluence_cap, sizeof("fluence")) ||
                   dual_mic_config) {
            my_data->fluence_type = FLUENCE_DUAL_MIC;

            if (property_get_bool("persist.vendor.audio.fluence.tmic.enabled",false)) {
                my_data->fluence_type |= FLUENCE_TRI_MIC;
            }
        }
    }

    if (my_data->fluence_type != FLUENCE_NONE) {
        if (property_get("persist.vendor.audio.fluence.voicecall",
                          value,NULL) > 0) {
            if (!strncmp("true", value, sizeof("true")))
                my_data->fluence_in_voice_call = true;
        }

        if (property_get("persist.vendor.audio.fluence.voicerec",
                          value,NULL) > 0) {
            if (!strncmp("true", value, sizeof("true")))
                my_data->fluence_in_voice_rec = true;
        }

        property_get("persist.vendor.audio.fluence.voicecomm",value,"");
        if (!strncmp("true", value, sizeof("true"))) {
            my_data->fluence_in_voice_comm = true;
        }

        property_get("persist.vendor.audio.fluence.audiorec",value,"");
        if (!strncmp("true", value, sizeof("true"))) {
            my_data->fluence_in_audio_rec = true;
        }

        if (property_get("persist.vendor.audio.fluence.speaker",
                          value,NULL) > 0) {
            if (!strncmp("true", value, sizeof("true"))) {
                my_data->fluence_in_spkr_mode = true;
            }
        }

        property_get("persist.vendor.audio.fluence.mode",value,"");
        if (!strncmp("broadside", value, sizeof("broadside"))) {
            my_data->fluence_mode = FLUENCE_BROADSIDE;
        }

        property_get("persist.vendor.audio.fluence.hfpcall",value,"");
        if (!strncmp("true", value, sizeof("true"))) {
            my_data->fluence_in_hfp_call = true;
        }
    }
    /* Check for Ambisonic Capture Enablement */
    if (property_get_bool("persist.vendor.audio.ambisonic.capture",false))
        my_data->ambisonic_capture = true;

    /* Check for Ambisonic Profile Assignment*/
    if (property_get_bool("persist.vendor.audio.ambisonic.auto.profile",false))
        my_data->ambisonic_profile = true;

    if (audio_extn_is_wsa_enabled()
        && check_and_get_wsa_info((char *)snd_card_name, &wsaCount, &is_wsa_combo_supported)) {
        /*Set ACDB ID of Stereo speaker if two WSAs are present*/
        /*Default ACDB ID for wsa speaker is that for mono*/
        if (wsaCount == 2) {
            platform_set_snd_device_acdb_id(SND_DEVICE_OUT_SPEAKER_WSA, 15);
            platform_set_snd_device_acdb_id(SND_DEVICE_OUT_SPEAKER_VBAT, 15);
        }

        my_data->is_wsa_speaker = true;

        if (is_wsa_combo_supported)
            hw_info_enable_wsa_combo_usecase_support(my_data->hw_info);

    }
    my_data->voice_speaker_stereo =
        property_get_bool("persist.vendor.audio.voicecall.speaker.stereo", false);

    property_get("persist.vendor.audio.FFSP.enable", ffspEnable, "");
    if (!strncmp("true", ffspEnable, sizeof("true"))) {
        acdb_device_table[SND_DEVICE_OUT_SPEAKER] = 131;
        acdb_device_table[SND_DEVICE_OUT_SPEAKER_WSA] = 131;
        acdb_device_table[SND_DEVICE_OUT_SPEAKER_REVERSE] = 131;
        acdb_device_table[SND_DEVICE_OUT_SPEAKER_AND_HDMI] = 131;
        acdb_device_table[SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET] = 131;
    }

    /* Check if Vbat speaker enabled property is set, this should be done before acdb init */
    bool ret = false;
    ret = audio_extn_can_use_vbat();
    if (ret)
        my_data->is_vbat_speaker = true;

    ret = audio_extn_can_use_bcl();
    if (ret)
        my_data->is_bcl_speaker = true;

    list_init(&my_data->acdb_meta_key_list);
    list_init(&my_data->custom_mtmx_params_list);
    list_init(&my_data->custom_mtmx_in_params_list);

    ret = audio_extn_is_hifi_audio_supported();
    if (ret || !my_data->is_internal_codec)
        my_data->hifi_audio = true;
    set_platform_defaults(my_data);
    /* Initialize ACDB ID's */
    if (my_data->is_i2s_ext_modem && !is_auto_snd_card(snd_card_name)) {
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_I2S_NAME),
            my_data, PLATFORM);
    } else if (!strncmp(snd_card_name, "sdm660-snd-card-skush",
               sizeof("sdm660-snd-card-skush"))) {
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_SKUSH_NAME),
            my_data, PLATFORM);
    } else if (!strncmp(snd_card_name, "sdm670-skuw-snd-card",
               sizeof("sdm670-skuw-snd-card"))) {
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_SKUW_NAME),
            my_data, PLATFORM);
    } else if (!strncmp(snd_card_name, "sm6150-qrd-snd-card",
               sizeof("sm6150-qrd-snd-card"))) {
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_QRD_NAME),
            my_data, PLATFORM);
    } else if (!strncmp(snd_card_name, "sm6150-wcd9375qrd-snd-card",
               sizeof("sm6150-wcd9375qrd-snd-card"))) {
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_QRD_NAME),
            my_data, PLATFORM);
    } else if (!strncmp(snd_card_name, "lahaina-qrd-snd-card",
               sizeof("lahaina-qrd-snd-card"))) {
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_QRD_NAME),
            my_data, PLATFORM);
    } else if (!strncmp(snd_card_name, "kona-qrd-snd-card",
               sizeof("kona-qrd-snd-card"))) {
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_QRD_NAME),
            my_data, PLATFORM);
    } else if (!strncmp(snd_card_name, "sm8150-tavil-qrd-snd-card",
               sizeof("sm8150-tavil-qrd-snd-card"))) {
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_QRD_NAME),
            my_data, PLATFORM);
    } else if (!strncmp(snd_card_name, "lito-qrd-snd-card",
               sizeof("lito-qrd-snd-card"))) {
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_QRD_NAME),
            my_data, PLATFORM);
    } else if (!strncmp(snd_card_name, "lito-lagoonqrd-snd-card",
               sizeof("lito-lagoonqrd-snd-card"))) {
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_LAGOON_QRD_NAME),
            my_data, PLATFORM);
    } else if (!strncmp(snd_card_name, "atoll-qrd-snd-card",
               sizeof("atoll-qrd-snd-card"))){
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_QRD_NAME),
            my_data, PLATFORM);
    } else if (!strncmp(snd_card_name, "bengal-qrd-snd-card",
               sizeof("bengal-qrd-snd-card"))) {
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_QRD_NAME),
            my_data, PLATFORM);
    } else if (!strncmp(snd_card_name, "holi-qrd-snd-card",
               sizeof("holi-qrd-snd-card"))) {
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_QRD_NAME),
            my_data, PLATFORM);
    } else if (!strncmp(snd_card_name, "qcs405-wsa-snd-card",
               sizeof("qcs405-wsa-snd-card"))) {
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_WSA_NAME),
            my_data, PLATFORM);
    } else if (!strncmp(snd_card_name, "qcs405-tdm-snd-card",
               sizeof("qcs405-tdm-snd-card"))) {
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_TDM_NAME),
            my_data, PLATFORM);
    } else if (my_data->is_internal_codec) {
        platform_info_init(get_xml_file_path(PLATFORM_INFO_XML_PATH_INTCODEC_NAME),
            my_data, PLATFORM);
    }
    else {
        // Try to load pixel or default
        audio_extn_utils_get_platform_info(snd_card_name, platform_info_file);
        platform_info_init(platform_info_file, my_data, PLATFORM);
    }

    // acquire perf lock to reduce the time for audio route init
    audio_extn_perf_lock_acquire(&adev->perf_lock_handle, 0,
                                 adev->perf_lock_opts,
                                 adev->perf_lock_opts_size);
    if (platform_is_i2s_ext_modem(snd_card_name, my_data) &&
        !is_auto_snd_card(snd_card_name)) {
        ALOGD("%s: Call MIXER_XML_PATH_I2S", __func__);
        adev->audio_route = audio_route_init(adev->snd_card,
            get_xml_file_path(MIXER_XML_PATH_I2S_NAME));
    } else {
        /* Get the codec internal name from the sound card name
         * and form the mixer paths file name dynamically. This
         * is generic way of picking any codec name based mixer
         * files in future with no code change. This code
         * assumes mixer files are formed with format as
         * mixer_paths_internalcodecname.xml

         * If this dynamically read mixer files fails to open then it
         * falls back to default mixer file i.e mixer_paths.xml. This is
         * done to preserve backward compatibility but not mandatory as
         * long as the mixer files are named as per above assumption.
        */
        snprintf(mixer_xml_file, sizeof(mixer_xml_file), "%s_%s_%s.xml",
                         MIXER_XML_BASE_STRING, snd_split_handle->snd_card,
                         snd_split_handle->form_factor);
        if (!audio_extn_utils_resolve_config_file(mixer_xml_file)) {
            memset(mixer_xml_file, 0, sizeof(mixer_xml_file));
            snprintf(mixer_xml_file, sizeof(mixer_xml_file), "%s_%s.xml",
                         MIXER_XML_BASE_STRING, snd_split_handle->variant);

            if (!audio_extn_utils_resolve_config_file(mixer_xml_file)) {
                memset(mixer_xml_file, 0, sizeof(mixer_xml_file));
                snprintf(mixer_xml_file, sizeof(mixer_xml_file), "%s_%s.xml",
                             MIXER_XML_BASE_STRING, snd_split_handle->snd_card);

                if (!audio_extn_utils_resolve_config_file(mixer_xml_file)) {
                    memset(mixer_xml_file, 0, sizeof(mixer_xml_file));
                    strlcpy(mixer_xml_file, MIXER_XML_DEFAULT_PATH, MIXER_PATH_MAX_LENGTH);
                    audio_extn_utils_resolve_config_file(mixer_xml_file);
                }
            }
        }

        ALOGD("%s: Loading mixer file: %s", __func__, mixer_xml_file);
        if (audio_extn_read_xml(adev, adev->snd_card, mixer_xml_file,
            get_xml_file_path(MIXER_XML_PATH_AUXPCM_NAME)) == -ENOSYS) {
            adev->audio_route = audio_route_init(adev->snd_card, mixer_xml_file);
        }
    }
    audio_extn_perf_lock_release(&adev->perf_lock_handle);
    if (!adev->audio_route) {
        ALOGE("%s: Failed to init audio route controls, aborting.",
               __func__);
        if (my_data)
            free(my_data);
        if (snd_card_name)
            free(snd_card_name);
        audio_extn_utils_close_snd_mixer(adev->mixer);
        return NULL;
    }

#if defined (PLATFORM_MSMFALCON) || defined (PLATFORM_MSM8937) || \
    defined (PLATFORM_MSM8953)
         if (my_data->is_internal_codec == true) {
            msm_device_to_be_id = msm_device_to_be_id_internal_codec;
            msm_be_id_array_len  =
                sizeof(msm_device_to_be_id_internal_codec) /
                sizeof(msm_device_to_be_id_internal_codec[0]);
         } else {
            msm_device_to_be_id = msm_device_to_be_id_external_codec;
            msm_be_id_array_len  =
                sizeof(msm_device_to_be_id_external_codec) /
                sizeof(msm_device_to_be_id_external_codec[0]);
         }
#endif

    /* CSRA devices support multiple sample rates via I2S at spkr out */
    if (!strncmp(snd_card_name, "qcs405-csra", strlen("qcs405-csra"))) {
        ALOGE("%s: soundcard: %s supports multiple sample rates", __func__, snd_card_name);
        my_data->use_sprk_default_sample_rate = false;
    } else {
        my_data->use_sprk_default_sample_rate = true;
        ALOGE("%s: soundcard: %s supports only default sample rate", __func__, snd_card_name);
    }

    my_data->voice_feature_set = VOICE_FEATURE_SET_DEFAULT;
    my_data->acdb_handle = dlopen(LIB_ACDB_LOADER, RTLD_NOW);
    if (my_data->acdb_handle == NULL) {
        ALOGE("%s: DLOPEN failed for %s", __func__, LIB_ACDB_LOADER);
    } else {
        ALOGV("%s: DLOPEN successful for %s", __func__, LIB_ACDB_LOADER);
        my_data->acdb_deallocate = (acdb_deallocate_t)dlsym(my_data->acdb_handle,
                                                    "acdb_loader_deallocate_ACDB");
        if (!my_data->acdb_deallocate)
            ALOGE("%s: Could not find the symbol acdb_loader_deallocate_ACDB from %s",
                  __func__, LIB_ACDB_LOADER);

        my_data->acdb_send_audio_cal = (acdb_send_audio_cal_t)dlsym(my_data->acdb_handle,
                                                    "acdb_loader_send_audio_cal_v2");
        if (!my_data->acdb_send_audio_cal)
            ALOGE("%s: Could not find the symbol acdb_send_audio_cal_v2 from %s",
                  __func__, LIB_ACDB_LOADER);

        my_data->acdb_send_audio_cal_v3 = (acdb_send_audio_cal_v3_t)dlsym(my_data->acdb_handle,
                                                    "acdb_loader_send_audio_cal_v3");
        if (!my_data->acdb_send_audio_cal_v3)
            ALOGE("%s: Could not find the symbol acdb_send_audio_cal_v3 from %s",
                  __func__, LIB_ACDB_LOADER);

        my_data->acdb_send_audio_cal_v4 = (acdb_send_audio_cal_v4_t)dlsym(my_data->acdb_handle,
                                                    "acdb_loader_send_audio_cal_v4");
        if (!my_data->acdb_send_audio_cal_v4)
            ALOGE("%s: Could not find the symbol acdb_send_audio_cal_v4 from %s",
                  __func__, LIB_ACDB_LOADER);

        my_data->acdb_set_audio_cal = (acdb_set_audio_cal_t)dlsym(my_data->acdb_handle,
                                                    "acdb_loader_set_audio_cal_v2");
        if (!my_data->acdb_set_audio_cal)
            ALOGE("%s: Could not find the symbol acdb_set_audio_cal_v2 from %s",
                  __func__, LIB_ACDB_LOADER);

        my_data->acdb_get_audio_cal = (acdb_get_audio_cal_t)dlsym(my_data->acdb_handle,
                                                    "acdb_loader_get_audio_cal_v2");
        if (!my_data->acdb_get_audio_cal)
            ALOGE("%s: Could not find the symbol acdb_get_audio_cal_v2 from %s",
                  __func__, LIB_ACDB_LOADER);

        my_data->acdb_send_voice_cal = (acdb_send_voice_cal_t)dlsym(my_data->acdb_handle,
                                                    "acdb_loader_send_voice_cal");
        if (!my_data->acdb_send_voice_cal)
            ALOGE("%s: Could not find the symbol acdb_loader_send_voice_cal from %s",
                  __func__, LIB_ACDB_LOADER);

        my_data->acdb_reload_vocvoltable = (acdb_reload_vocvoltable_t)dlsym(my_data->acdb_handle,
                                                    "acdb_loader_reload_vocvoltable");
        if (!my_data->acdb_reload_vocvoltable)
            ALOGE("%s: Could not find the symbol acdb_loader_reload_vocvoltable from %s",
                  __func__, LIB_ACDB_LOADER);

        my_data->acdb_get_default_app_type = (acdb_get_default_app_type_t)dlsym(
                                                    my_data->acdb_handle,
                                                    "acdb_loader_get_default_app_type");
        if (!my_data->acdb_get_default_app_type)
            ALOGE("%s: Could not find the symbol acdb_get_default_app_type from %s",
                  __func__, LIB_ACDB_LOADER);

        my_data->acdb_send_gain_dep_cal = (acdb_send_gain_dep_cal_t)dlsym(my_data->acdb_handle,
                                                    "acdb_loader_send_gain_dep_cal");
        if (!my_data->acdb_send_gain_dep_cal)
            ALOGV("%s: Could not find the symbol acdb_loader_send_gain_dep_cal from %s",
                  __func__, LIB_ACDB_LOADER);

        my_data->acdb_send_common_top = (acdb_send_common_top_t)dlsym(
                                                    my_data->acdb_handle,
                                                    "acdb_loader_send_common_custom_topology");
        if (!my_data->acdb_send_common_top)
            ALOGE("%s: Could not find the symbol acdb_get_default_app_type from %s",
                  __func__, LIB_ACDB_LOADER);

        my_data->acdb_set_codec_data = (acdb_set_codec_data_t)dlsym(
                                                    my_data->acdb_handle,
                                                    "acdb_loader_set_codec_data");
        if (!my_data->acdb_set_codec_data)
            ALOGE("%s: Could not find the symbol acdb_get_default_app_type from %s",
                  __func__, LIB_ACDB_LOADER);


        my_data->acdb_init_v4 = (acdb_init_v4_t)dlsym(my_data->acdb_handle,
                                                     "acdb_loader_init_v4");
        if (my_data->acdb_init_v4 == NULL) {
            ALOGE("%s: dlsym error %s for acdb_loader_init_v4", __func__, dlerror());
        }

        my_data->acdb_init_v3 = (acdb_init_v3_t)dlsym(my_data->acdb_handle,
                                                     "acdb_loader_init_v3");
        if (my_data->acdb_init_v3 == NULL) {
            ALOGE("%s: dlsym error %s for acdb_loader_init_v3", __func__, dlerror());
        }

        my_data->acdb_init = (acdb_init_t)dlsym(my_data->acdb_handle,
                                                     "acdb_loader_init_v3");
        if (my_data->acdb_init == NULL) {
            ALOGE("%s: dlsym error %s for acdb_loader_init_v3", __func__, dlerror());
            goto acdb_init_fail;
        }

        my_data->acdb_reload_v2 = (acdb_reload_v2_t)dlsym(my_data->acdb_handle,
                                                    "acdb_loader_reload_acdb_files_v2");
        if (my_data->acdb_reload_v2 == NULL) {
            ALOGE("%s: dlsym error %s for acdb_loader_reload_acdb_files_v2", __func__, dlerror());
        }

        my_data->acdb_reload = (acdb_reload_t)dlsym(my_data->acdb_handle,
                                                    "acdb_loader_reload_acdb_files");
        if (my_data->acdb_reload == NULL) {
            ALOGE("%s: dlsym error %s for acdb_loader_reload_acdb_files", __func__, dlerror());
            goto acdb_init_fail;
        }

        int result = acdb_init_v2(adev->mixer);
        if (!result) {
            my_data->is_acdb_initialized = true;
            ALOGD("ACDB initialized");
            audio_hwdep_send_cal(my_data);
        } else {
            my_data->is_acdb_initialized = false;
            ALOGD("ACDB initialization failed");
            if (my_data->acdb_deallocate)
                my_data->acdb_deallocate();
        }
    }
    /* init keep-alive for compress passthru */
    audio_extn_keep_alive_init(adev);

#ifdef FLICKER_SENSOR_INPUT
    configure_flicker_sensor_input(adev->mixer);
#endif

#ifdef DYNAMIC_LOG_ENABLED
    log_utils_init();
#endif
acdb_init_fail:


    /*
     * Get the be_dai_name_table from kernel which provides a mapping
     * between a backend string name and a backend ID
     */
    init_be_dai_name_table(adev);

    if (audio_extn_can_use_ras()) {
        if (property_get_bool("persist.vendor.audio.speaker.prot.enable", false)) {
            platform_set_snd_device_acdb_id(SND_DEVICE_OUT_SPEAKER_PROTECTED,
                           acdb_device_table[SND_DEVICE_OUT_SPEAKER_PROTECTED_RAS]);
            platform_set_snd_device_acdb_id(SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT,
                           acdb_device_table[SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT_RAS]);
        } else {
            ALOGD("%s: RAS Feature should be enabled with Speaker Protection", __func__);
        }
    }

    /* If platform is apq8084 and baseband is MDM, load CSD Client specific
     * symbols. Voice call is handled by MDM and apps processor talks to
     * MDM through CSD Client
     */
    property_get("ro.board.platform", platform, "");
    property_get("ro.baseband", baseband, "");
    if ((!strncmp("apq8084", platform, sizeof("apq8084")) ||
        !strncmp("msm8996", platform, sizeof("msm8996")) ||
        !strncmp("sm6150", platform, sizeof("sm6150")) ||
        !strncmp("sdx", platform, sizeof("sdx")) ||
        !strncmp("sdm845", platform, sizeof("sdm845"))) &&
        ( !strncmp("mdm", baseband, (sizeof("mdm")-1)) ||
          !strncmp("sdx", baseband, (sizeof("sdx")-1)))) {
         my_data->csd = open_csd_client(my_data->is_i2s_ext_modem);
    } else {
         my_data->csd = NULL;
    }

    /* obtain source mic type from max mic count*/
    get_source_mic_type(my_data);
    ALOGD("%s: Fluence_Type(%d) max_mic_count(%d) mic_type(0x%x) fluence_in_voice_call(%d)"
          " fluence_in_voice_rec(%d) fluence_in_spkr_mode(%d) fluence_in_hfp_call(%d) fluence_sb_enabled(%d)"
          "fluence_nn_enabled(%d) ", __func__, my_data->fluence_type, my_data->max_mic_count,
          my_data->source_mic_type, my_data->fluence_in_voice_call,
          my_data->fluence_in_voice_rec, my_data->fluence_in_spkr_mode,
          my_data->fluence_in_hfp_call, my_data->fluence_sb_enabled, my_data->fluence_nn_enabled);

    /* init usb */
    audio_extn_usb_init(adev);

    /*init a2dp*/
    audio_extn_a2dp_init(adev);

    /* init dap hal */
    audio_extn_dap_hal_init(adev->snd_card);

    /* Read one time ssr property */
    audio_extn_ssr_update_enabled();
    audio_extn_spkr_prot_init(adev);

    audio_extn_hwdep_cal_send(adev->snd_card, my_data->acdb_handle);

    /* init audio device arbitration */
    audio_extn_dev_arbi_init();

    default_rx_backend = strdup("SLIMBUS_0_RX");

    /* initialize backend config */
    for (idx = 0; idx < MAX_CODEC_BACKENDS; idx++) {
        my_data->current_backend_cfg[idx].sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
        if (idx == HEADPHONE_44_1_BACKEND)
            my_data->current_backend_cfg[idx].sample_rate = OUTPUT_SAMPLING_RATE_44100;
        my_data->current_backend_cfg[idx].bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
        my_data->current_backend_cfg[idx].channels = CODEC_BACKEND_DEFAULT_CHANNELS;
        if (idx > MAX_RX_CODEC_BACKENDS)
            my_data->current_backend_cfg[idx].channels = CODEC_BACKEND_DEFAULT_TX_CHANNELS;
        my_data->current_backend_cfg[idx].format = AUDIO_FORMAT_PCM;
        my_data->current_backend_cfg[idx].bitwidth_mixer_ctl = NULL;
        my_data->current_backend_cfg[idx].samplerate_mixer_ctl = NULL;
        my_data->current_backend_cfg[idx].channels_mixer_ctl = NULL;
    }

    my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].bitwidth_mixer_ctl =
        strdup("SLIM_0_RX Format");
    my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].samplerate_mixer_ctl =
        strdup("SLIM_0_RX SampleRate");

    my_data->current_backend_cfg[DSD_NATIVE_BACKEND].bitwidth_mixer_ctl =
        strdup("SLIM_2_RX Format");
    my_data->current_backend_cfg[DSD_NATIVE_BACKEND].samplerate_mixer_ctl =
        strdup("SLIM_2_RX SampleRate");

    my_data->current_backend_cfg[HEADPHONE_44_1_BACKEND].bitwidth_mixer_ctl =
        strdup("SLIM_5_RX Format");
    my_data->current_backend_cfg[HEADPHONE_44_1_BACKEND].samplerate_mixer_ctl =
        strdup("SLIM_5_RX SampleRate");

    if (!my_data->is_slimbus_interface) {
        //TODO:: make generic interfaceface to check Slimbus/I2S/CDC_DMA
        if (!strncmp(snd_card_name, "sm6150", strlen("sm6150")) ||
            !strncmp(snd_card_name, "kona", strlen("kona")) ||
            !strncmp(snd_card_name, "lahaina", strlen("lahaina")) ||
            !strncmp(snd_card_name, "lito", strlen("lito")) ||
            !strncmp(snd_card_name, "atoll", strlen("atoll")) ||
            !strncmp(snd_card_name, "trinket", strlen("trinket"))||
            !strncmp(snd_card_name, "holi", strlen("holi"))||
            !strncmp(snd_card_name, "bengal", strlen("bengal"))) {
            my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].bitwidth_mixer_ctl =
                strdup("WSA_CDC_DMA_RX_0 Format");
            my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].samplerate_mixer_ctl =
                strdup("WSA_CDC_DMA_RX_0 SampleRate");
            my_data->current_backend_cfg[DEFAULT_CODEC_TX_BACKEND].bitwidth_mixer_ctl =
                strdup("TX_CDC_DMA_TX_3 Format");
            my_data->current_backend_cfg[DEFAULT_CODEC_TX_BACKEND].samplerate_mixer_ctl =
                strdup("TX_CDC_DMA_TX_3 SampleRate");
            my_data->current_backend_cfg[HEADPHONE_BACKEND].bitwidth_mixer_ctl =
                strdup("RX_CDC_DMA_RX_0 Format");
            my_data->current_backend_cfg[HEADPHONE_BACKEND].samplerate_mixer_ctl =
                strdup("RX_CDC_DMA_RX_0 SampleRate");
            /*
             * TODO: enable CONCURRENT_CAPTURE_ENABLED flag only if separate backend
             * is defined for headset-mic. This is to capture separate data from
             * headset-mic and handset-mic.
             */
            if(audio_extn_is_concurrent_capture_enabled()) {
                my_data->current_backend_cfg[HEADSET_TX_BACKEND].bitwidth_mixer_ctl =
                                                    strdup("TX_CDC_DMA_TX_4 Format");
                my_data->current_backend_cfg[HEADSET_TX_BACKEND].samplerate_mixer_ctl =
                                                    strdup("TX_CDC_DMA_TX_4 SampleRate");
            }

            if (default_rx_backend)
                free(default_rx_backend);
            default_rx_backend = strdup("WSA_CDC_DMA_RX_0");
            if((!strncmp(snd_card_name, "bengal", strlen("bengal")) &&
               strncmp(snd_card_name, "bengal-scuba", strlen("bengal-scuba"))) ||
               !strncmp(snd_card_name, "holi", strlen("holi"))) {
                my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].bitwidth_mixer_ctl =
                        strdup("RX_CDC_DMA_RX_1 Format");
                my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].samplerate_mixer_ctl =
                        strdup("RX_CDC_DMA_RX_1 SampleRate");
                default_rx_backend = strdup("RX_CDC_DMA_RX_1");
                my_data->is_multiple_sample_rate_combo_supported = false;
            } else if (!strncmp(snd_card_name, "bengal-scuba", strlen("bengal-scuba"))) {
                my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].bitwidth_mixer_ctl =
                        strdup("RX_CDC_DMA_RX_0 Format");
                my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].samplerate_mixer_ctl =
                        strdup("RX_CDC_DMA_RX_0 SampleRate");
                default_rx_backend = strdup("RX_CDC_DMA_RX_0");
                my_data->is_multiple_sample_rate_combo_supported = false;
            }
        } else if (!strncmp(snd_card_name, "sdm660", strlen("sdm660")) ||
               !strncmp(snd_card_name, "sdm670", strlen("sdm670")) ||
               !strncmp(snd_card_name, "qcs605", strlen("qcs605"))) {
            my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].bitwidth_mixer_ctl =
                strdup("INT4_MI2S_RX Format");
            my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].samplerate_mixer_ctl =
                strdup("INT4_MI2S_RX SampleRate");
            my_data->current_backend_cfg[DEFAULT_CODEC_TX_BACKEND].bitwidth_mixer_ctl =
                strdup("INT3_MI2S_TX Format");
            my_data->current_backend_cfg[DEFAULT_CODEC_TX_BACKEND].samplerate_mixer_ctl =
                strdup("INT3_MI2S_TX SampleRate");
            my_data->current_backend_cfg[HEADPHONE_BACKEND].bitwidth_mixer_ctl =
                strdup("INT0_MI2S_RX Format");
            my_data->current_backend_cfg[HEADPHONE_BACKEND].samplerate_mixer_ctl =
                strdup("INT0_MI2S_RX SampleRate");
            if (default_rx_backend)
                free(default_rx_backend);
            default_rx_backend = strdup("INT4_MI2S_RX");
        } else {
            my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].bitwidth_mixer_ctl =
                strdup("MI2S_RX Format");
            my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].samplerate_mixer_ctl =
                strdup("MI2S_RX SampleRate");
            my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].channels_mixer_ctl =
                strdup("MI2S_RX Channels");
            my_data->current_backend_cfg[DEFAULT_CODEC_TX_BACKEND].bitwidth_mixer_ctl =
                strdup("MI2S_TX Format");
            my_data->current_backend_cfg[DEFAULT_CODEC_TX_BACKEND].samplerate_mixer_ctl =
                strdup("MI2S_TX SampleRate");
            my_data->current_backend_cfg[HEADPHONE_BACKEND].bitwidth_mixer_ctl =
                strdup("INT0_MI2S_RX Format");
            my_data->current_backend_cfg[HEADPHONE_BACKEND].samplerate_mixer_ctl =
                strdup("INT0_MI2S_RX SampleRate");
        }
    } else {
        if (!strncmp(snd_card_name, "qcs405", strlen("qcs405"))) {

            if (!strncmp(snd_card_name, "qcs405-csra", strlen("qcs405-csra"))) {
               my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].bitwidth_mixer_ctl =
                   strdup("PRIM_MI2S_RX Format");
               my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].samplerate_mixer_ctl =
                   strdup("PRIM_MI2S_RX SampleRate");
            } else {
               my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].bitwidth_mixer_ctl =
                   strdup("WSA_CDC_DMA_RX_0 Format");
               my_data->current_backend_cfg[DEFAULT_CODEC_BACKEND].samplerate_mixer_ctl =
                   strdup("WSA_CDC_DMA_RX_0 SampleRate");
            }
            my_data->current_backend_cfg[DEFAULT_CODEC_TX_BACKEND].bitwidth_mixer_ctl =
                strdup("VA_CDC_DMA_TX_0 Format");
            my_data->current_backend_cfg[DEFAULT_CODEC_TX_BACKEND].samplerate_mixer_ctl =
                strdup("VA_CDC_DMA_TX_0 SampleRate");
        } else {
            my_data->current_backend_cfg[DEFAULT_CODEC_TX_BACKEND].bitwidth_mixer_ctl =
                strdup("SLIM_0_TX Format");
            my_data->current_backend_cfg[DEFAULT_CODEC_TX_BACKEND].samplerate_mixer_ctl =
                strdup("SLIM_0_TX SampleRate");
        }

        if (strstr(snd_card_name, "intcodec")) {
            my_data->current_backend_cfg[HEADPHONE_BACKEND].bitwidth_mixer_ctl =
                strdup("INT0_MI2S_RX Format");
            my_data->current_backend_cfg[HEADPHONE_BACKEND].samplerate_mixer_ctl =
                strdup("INT0_MI2S_RX SampleRate");
        } else {
            my_data->current_backend_cfg[HEADPHONE_BACKEND].bitwidth_mixer_ctl =
                strdup("SLIM_6_RX Format");
            my_data->current_backend_cfg[HEADPHONE_BACKEND].samplerate_mixer_ctl =
                strdup("SLIM_6_RX SampleRate");
        }

        //NOTE: enable CONCURRENT_CAPTURE_ENABLED flag only if separate backend is defined
        //for headset-mic. This is to capture separate data from headset-mic and handset-mic.
        if(audio_extn_is_concurrent_capture_enabled()) {
            my_data->current_backend_cfg[HEADSET_TX_BACKEND].bitwidth_mixer_ctl =
                strdup("SLIM_1_TX Format");
            my_data->current_backend_cfg[HEADSET_TX_BACKEND].samplerate_mixer_ctl =
                strdup("SLIM_1_TX SampleRate");
        }
    }

    my_data->current_backend_cfg[USB_AUDIO_TX_BACKEND].bitwidth_mixer_ctl =
        strdup("USB_AUDIO_TX Format");
    my_data->current_backend_cfg[USB_AUDIO_TX_BACKEND].samplerate_mixer_ctl =
        strdup("USB_AUDIO_TX SampleRate");
    my_data->current_backend_cfg[USB_AUDIO_TX_BACKEND].channels_mixer_ctl =
        strdup("USB_AUDIO_TX Channels");

    if (!strncmp(platform_get_snd_device_backend_interface(SND_DEVICE_IN_HDMI_MIC),
        "SEC_MI2S_TX", sizeof("SEC_MI2S_TX"))) {
        my_data->current_backend_cfg[HDMI_TX_BACKEND].bitwidth_mixer_ctl =
            strdup("SEC_MI2S_TX Format");
        my_data->current_backend_cfg[HDMI_TX_BACKEND].samplerate_mixer_ctl =
            strdup("SEC_MI2S_TX SampleRate");
        my_data->current_backend_cfg[HDMI_TX_BACKEND].channels_mixer_ctl =
            strdup("SEC_MI2S_TX Channels");
    } else {
        my_data->current_backend_cfg[HDMI_TX_BACKEND].bitwidth_mixer_ctl =
            strdup("QUAT_MI2S_TX Format");
        my_data->current_backend_cfg[HDMI_TX_BACKEND].samplerate_mixer_ctl =
            strdup("QUAT_MI2S_TX SampleRate");
        my_data->current_backend_cfg[HDMI_TX_BACKEND].channels_mixer_ctl =
            strdup("QUAT_MI2S_TX Channels");
    }

    my_data->current_backend_cfg[SPDIF_TX_BACKEND].bitwidth_mixer_ctl =
        strdup("PRIM_SPDIF_TX Format");
    my_data->current_backend_cfg[SPDIF_TX_BACKEND].samplerate_mixer_ctl =
        strdup("PRIM_SPDIF_TX SampleRate");
    my_data->current_backend_cfg[SPDIF_TX_BACKEND].channels_mixer_ctl =
        strdup("PRIM_SPDIF_TX Channels");

    my_data->current_backend_cfg[HDMI_ARC_TX_BACKEND].bitwidth_mixer_ctl =
        strdup("SEC_SPDIF_TX Format");
    my_data->current_backend_cfg[HDMI_ARC_TX_BACKEND].samplerate_mixer_ctl =
        strdup("SEC_SPDIF_TX SampleRate");
    my_data->current_backend_cfg[HDMI_ARC_TX_BACKEND].channels_mixer_ctl =
        strdup("SEC_SPDIF_TX Channels");

    my_data->current_backend_cfg[HDMI_RX_BACKEND].bitwidth_mixer_ctl =
        strdup("HDMI_RX Bit Format");
    my_data->current_backend_cfg[HDMI_RX_BACKEND].samplerate_mixer_ctl =
        strdup("HDMI_RX SampleRate");
    my_data->current_backend_cfg[HDMI_RX_BACKEND].channels_mixer_ctl =
        strdup("HDMI_RX Channels");
    my_data->current_backend_cfg[DISP_PORT_RX_BACKEND].bitwidth_mixer_ctl =
        strdup("Display Port RX Bit Format");
    my_data->current_backend_cfg[DISP_PORT_RX_BACKEND].samplerate_mixer_ctl =
        strdup("Display Port RX SampleRate");
    my_data->current_backend_cfg[DISP_PORT_RX_BACKEND].channels_mixer_ctl =
        strdup("Display Port RX Channels");
    my_data->current_backend_cfg[DISP_PORT1_RX_BACKEND].bitwidth_mixer_ctl =
        strdup("Display Port1 RX Bit Format");
    my_data->current_backend_cfg[DISP_PORT1_RX_BACKEND].samplerate_mixer_ctl =
        strdup("Display Port1 RX SampleRate");
    my_data->current_backend_cfg[DISP_PORT1_RX_BACKEND].channels_mixer_ctl =
        strdup("Display Port1 RX Channels");

    my_data->current_backend_cfg[USB_AUDIO_RX_BACKEND].bitwidth_mixer_ctl =
        strdup("USB_AUDIO_RX Format");
    my_data->current_backend_cfg[USB_AUDIO_RX_BACKEND].samplerate_mixer_ctl =
        strdup("USB_AUDIO_RX SampleRate");
    my_data->current_backend_cfg[USB_AUDIO_RX_BACKEND].channels_mixer_ctl =
        strdup("USB_AUDIO_RX Channels");

    for (idx = 0; idx < MAX_CODEC_BACKENDS; idx++) {
        if (my_data->current_backend_cfg[idx].bitwidth_mixer_ctl) {
            ctl = mixer_get_ctl_by_name(adev->mixer,
                         my_data->current_backend_cfg[idx].bitwidth_mixer_ctl);
            id_string = platform_get_mixer_control(ctl);
            if (id_string) {
                cfg_value = audio_extn_utils_get_bit_width_from_string(id_string);
                if (cfg_value > 0)
                    my_data->current_backend_cfg[idx].bit_width = cfg_value;
            }
        }

        if (my_data->current_backend_cfg[idx].samplerate_mixer_ctl) {
            ctl = mixer_get_ctl_by_name(adev->mixer,
                         my_data->current_backend_cfg[idx].samplerate_mixer_ctl);
            id_string = platform_get_mixer_control(ctl);
            if (id_string) {
                cfg_value = audio_extn_utils_get_sample_rate_from_string(id_string);
                if (cfg_value > 0)
                    my_data->current_backend_cfg[idx].sample_rate = cfg_value;
            }
        }

        if (my_data->current_backend_cfg[idx].channels_mixer_ctl) {
            ctl = mixer_get_ctl_by_name(adev->mixer,
                         my_data->current_backend_cfg[idx].channels_mixer_ctl);
            id_string = platform_get_mixer_control(ctl);
            if (id_string) {
                cfg_value = audio_extn_utils_get_channels_from_string(id_string);
                if (cfg_value > 0)
                    my_data->current_backend_cfg[idx].channels = cfg_value;
            }
        }
    }

    ret = audio_extn_utils_get_codec_variant(my_data->adev->snd_card,
                                             my_data->codec_variant);
    ret = audio_extn_utils_get_codec_version(snd_card_name,
                                             my_data->adev->snd_card,
                                             my_data->codec_version);

    /* WCD9370 codec variant only supports Class AB power mode */
    if (strstr(my_data->codec_variant, "WCD9370")) {
        my_data->is_cls_ab_only_supported = true;
    }

    if (NATIVE_AUDIO_MODE_INVALID != platform_get_native_support()) {
        /*
         * Native playback is enabled from the UI.
         */
        if(strstr(snd_card_name, "tasha")) {
            if (strstr(my_data->codec_version, "WCD9335_1_0") ||
                strstr(my_data->codec_version, "WCD9335_1_1")) {
                ALOGD("%s:napb: TASHA 1.0 or 1.1 only SRC mode is supported",
                      __func__);
                platform_set_native_support(NATIVE_AUDIO_MODE_SRC);
            }
        }
        if (strstr(snd_card_name, "tavil")) {
            ALOGD("%s:DSD playback is supported", __func__);
            my_data->is_dsd_supported = true;
            my_data->is_asrc_supported = true;
            platform_set_native_support(NATIVE_AUDIO_MODE_MULTIPLE_MIX_IN_CODEC);
        }
    }

    if (property_get_bool("persist.vendor.audio.apptype.multirec.enabled", false))
        my_data->use_generic_handset = true;

    /* Initialize keep alive for HDMI/loopback silence */
    audio_extn_keep_alive_init(adev);

    platform_reset_edid_info(my_data);
    free(snd_card_name);
    return my_data;
}

struct audio_custom_mtmx_params *
    platform_get_custom_mtmx_params(void *platform,
                                    struct audio_custom_mtmx_params_info *info,
                                    uint32_t *idx)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct listnode *node = NULL;
    struct audio_custom_mtmx_params *params = NULL;
    int i = 0;

    if (!info || !idx) {
        ALOGE("%s: Invalid params", __func__);
        return NULL;
    }

    list_for_each(node, &my_data->custom_mtmx_params_list) {
        params = node_to_item(node, struct audio_custom_mtmx_params, list);
        if (params &&
            params->info.id == info->id &&
            params->info.ip_channels == info->ip_channels &&
            params->info.op_channels == info->op_channels &&
            params->info.snd_device == info->snd_device) {
            while (params->info.usecase_id[i] != 0) {
                if (params->info.usecase_id[i] == info->usecase_id[0]) {
                    ALOGV("%s: found params with ip_ch %d op_ch %d uc_id %d snd_dev %d",
                           __func__, info->ip_channels, info->op_channels,
                           info->usecase_id[0], info->snd_device);
                    *idx = i;
                    return params;
                }
                i++;
            }
        }
    }
    ALOGI("%s: no matching param with id %d ip_ch %d op_ch %d uc_id %d snd_dev %d",
          __func__, info->id, info->ip_channels, info->op_channels,
          info->usecase_id[0], info->snd_device);
    return NULL;
}

int platform_add_custom_mtmx_params(void *platform,
                                    struct audio_custom_mtmx_params_info *info)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_custom_mtmx_params *params = NULL;
    uint32_t size = sizeof(*params);
    int i = 0;

    if (!info) {
        ALOGE("%s: Invalid params", __func__);
        return -EINVAL;
    }

    if (info->ip_channels > AUDIO_CHANNEL_COUNT_MAX ||
        info->op_channels > AUDIO_CHANNEL_COUNT_MAX) {
        ALOGE("%s: unusupported channels in %d, out %d",
              __func__, info->ip_channels, info->op_channels);
        return -EINVAL;
    }

    size += sizeof(params->coeffs[0]) * info->ip_channels * info->op_channels;
    params = (struct audio_custom_mtmx_params *) calloc(1, size);
    if (!params) {
        ALOGE("%s: failed to add custom mtmx params", __func__);
        return -ENOMEM;
    }

    ALOGI("%s: adding mtmx params with id %d ip_ch %d op_ch %d snd_dev %d",
          __func__, info->id, info->ip_channels, info->op_channels,
          info->snd_device);
    while (info->usecase_id[i] != 0) {
        ALOGI("%s: supported usecase ids for added mtmx params %d",
              __func__, info->usecase_id[i]);
        i++;
    }

    params->info = *info;
    list_add_tail(&my_data->custom_mtmx_params_list, &params->list);
    return 0;
}

static void platform_release_custom_mtmx_params(void *platform)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct listnode *node = NULL, *tempnode = NULL;

    list_for_each_safe(node, tempnode, &my_data->custom_mtmx_params_list) {
        list_remove(node);
        free(node_to_item(node, struct audio_custom_mtmx_params, list));
    }
}

struct audio_custom_mtmx_in_params *platform_get_custom_mtmx_in_params(void *platform,
                                    struct audio_custom_mtmx_in_params_info *info)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct listnode *node = NULL;
    struct audio_custom_mtmx_in_params *params = NULL;
    int i = 0;

    if (!info) {
        ALOGE("%s: Invalid params", __func__);
        return NULL;
    }

    list_for_each(node, &my_data->custom_mtmx_in_params_list) {
        params = node_to_item(node, struct audio_custom_mtmx_in_params, list);
        if (params &&
            params->in_info.op_channels == info->op_channels) {
            while (params->in_info.usecase_id[i] != 0) {
                if (params->in_info.usecase_id[i] == info->usecase_id[0]) {
                    ALOGV("%s: found params with op_ch %d uc_id %d",
                          __func__, info->op_channels, info->usecase_id[0]);
                    return params;
                }
                i++;
            }
        }
    }

    ALOGI("%s: no matching param with op_ch %d uc_id %d",
           __func__, info->op_channels, info->usecase_id[0]);
    return NULL;
}

int platform_add_custom_mtmx_in_params(void *platform,
                                    struct audio_custom_mtmx_in_params_info *info)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_custom_mtmx_in_params *params = NULL;
    uint32_t size = sizeof(*params);
    int i = 0;

    if (!info) {
        ALOGE("%s: Invalid params", __func__);
        return -EINVAL;
    }

    if (info->op_channels > AUDIO_CHANNEL_COUNT_MAX) {
        ALOGE("%s: unusupported channels in %d", __func__, info->op_channels);
        return -EINVAL;
    }

    params = (struct audio_custom_mtmx_in_params *)calloc(1, size);
    if (!params) {
        ALOGE("%s: failed to add custom mtmx in params", __func__);
        return -ENOMEM;
    }

    ALOGI("%s: adding mtmx in params with op_ch %d",
          __func__, info->op_channels);

    while (info->usecase_id[i] != 0) {
        ALOGI("%s: supported usecase ids for added mtmx in params %d",
              __func__, info->usecase_id[i]);
        i++;
    }

    params->in_info = *info;
    list_add_tail(&my_data->custom_mtmx_in_params_list, &params->list);
    return 0;
}

static void platform_release_custom_mtmx_in_params(void *platform)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct listnode *node = NULL, *tempnode = NULL;

    list_for_each_safe(node, tempnode, &my_data->custom_mtmx_in_params_list) {
        list_remove(node);
        free(node_to_item(node, struct audio_custom_mtmx_in_params, list));
    }
}

void platform_release_acdb_metainfo_key(void *platform)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct listnode *node, *tempnode;

    list_for_each_safe(node, tempnode, &my_data->acdb_meta_key_list) {
        list_remove(node);
        free(node_to_item(node, struct meta_key_list, list));
    }
}

void platform_deinit(void *platform)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct operator_info *info_item;
    struct operator_specific_device *device_item;
    struct external_specific_device *ext_dev;
    struct app_type_entry *ap;
    struct listnode *node;

    audio_extn_keep_alive_deinit();
    platform_reset_edid_info(my_data);

    if (be_dai_name_table) {
        free((void *)be_dai_name_table);
        be_dai_name_table = NULL;
    }

    if (my_data->hw_dep_fd >= 0) {
        close(my_data->hw_dep_fd);
        my_data->hw_dep_fd = -1;
    }
    if (default_rx_backend)
        free(default_rx_backend);

    hw_info_deinit(my_data->hw_info);
    close_csd_client(my_data->csd);

    int32_t dev;
    for (dev = 0; dev < SND_DEVICE_MAX; dev++) {
        if (backend_tag_table[dev]) {
            free(backend_tag_table[dev]);
            backend_tag_table[dev]= NULL;
        }

        if (hw_interface_table[dev]) {
            free(hw_interface_table[dev]);
            hw_interface_table[dev] = NULL;
        }

        if (operator_specific_device_table[dev]) {
            while (!list_empty(operator_specific_device_table[dev])) {
                node = list_head(operator_specific_device_table[dev]);
                list_remove(node);
                device_item = node_to_item(node,
                               struct operator_specific_device, list);
                free(device_item->operator);
                device_item->operator = NULL;
                free(device_item->mixer_path);
                device_item->mixer_path = NULL;
                free(device_item);
                device_item = NULL;
            }
            free(operator_specific_device_table[dev]);
            operator_specific_device_table[dev] = NULL;
        }

        if (external_specific_device_table[dev]) {
            while (!list_empty(external_specific_device_table[dev])) {
                node = list_head(external_specific_device_table[dev]);
                list_remove(node);
                ext_dev = node_to_item(node, struct external_specific_device, list);
                free(ext_dev->usbid);
                free(ext_dev);
            }
            free(external_specific_device_table[dev]);
        }
    }

    while (!list_empty(&operator_info_list)) {
        node = list_head(&operator_info_list);
        list_remove(node);
        info_item = node_to_item(node, struct operator_info, list);
        free(info_item->name);
        info_item->name = NULL;
        free(info_item->mccmnc);
        info_item->mccmnc = NULL;
        free(info_item);
        info_item = NULL;
    }

    while (!list_empty(&app_type_entry_list)) {
        node = list_head(&app_type_entry_list);
        list_remove(node);
        ap = node_to_item(node, struct app_type_entry, node);
        if (ap->mode) {
            free(ap->mode);
            ap->mode = NULL;
        }
        free(ap);
        ap = NULL;
     }

    /* deinit audio device arbitration */
    audio_extn_dev_arbi_deinit();
    platform_reset_edid_info(my_data);

    if (my_data->adev->mixer) {
        audio_extn_utils_close_snd_mixer(my_data->adev->mixer);
        my_data->adev->mixer = NULL;
    }



    if (my_data->spkr_ch_map) {
        free(my_data->spkr_ch_map);
        my_data->spkr_ch_map = NULL;
    }

    int32_t idx;

    for (idx = 0; idx < MAX_CODEC_BACKENDS; idx++) {
         if (my_data->current_backend_cfg[idx].bitwidth_mixer_ctl) {
             free(my_data->current_backend_cfg[idx].bitwidth_mixer_ctl);
             my_data->current_backend_cfg[idx].bitwidth_mixer_ctl = NULL;
         }

         if (my_data->current_backend_cfg[idx].samplerate_mixer_ctl) {
             free(my_data->current_backend_cfg[idx].samplerate_mixer_ctl);
             my_data->current_backend_cfg[idx].samplerate_mixer_ctl = NULL;
         }

         if (my_data->current_backend_cfg[idx].channels_mixer_ctl) {
             free(my_data->current_backend_cfg[idx].channels_mixer_ctl);
             my_data->current_backend_cfg[idx].channels_mixer_ctl = NULL;
         }
    }

    /* free acdb_meta_key_list */
    platform_release_acdb_metainfo_key(platform);
    platform_release_custom_mtmx_params(platform);
    platform_release_custom_mtmx_in_params(platform);

    if (my_data->acdb_deallocate)
        my_data->acdb_deallocate();

    free(platform);
    /* deinit usb */
    audio_extn_usb_deinit();
    audio_extn_dap_hal_deinit();
#ifdef DYNAMIC_LOG_ENABLED
    log_utils_deinit();
#endif
}

static int platform_is_acdb_initialized(void *platform)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    ALOGD("%s: acdb initialized %d\n", __func__, my_data->is_acdb_initialized);
    return my_data->is_acdb_initialized;
}

void platform_snd_card_update(void *platform, card_status_t card_status)
{
    struct platform_data *my_data = (struct platform_data *)platform;

    if (card_status == CARD_STATUS_ONLINE) {
        if (!platform_is_acdb_initialized(my_data)) {
            if(platform_acdb_init(my_data))
                ALOGE("%s: acdb initialization is failed", __func__);
        } else if (my_data->acdb_send_common_top() < 0) {
                ALOGD("%s: acdb did not set common topology", __func__);
        }
    }
}

const char *platform_get_snd_device_name(snd_device_t snd_device)
{
    if (snd_device >= SND_DEVICE_MIN && snd_device < SND_DEVICE_MAX) {
        if (operator_specific_device_table[snd_device] != NULL) {
            return get_operator_specific_device_mixer_path(snd_device);
        }
        return device_table[snd_device];
    } else
        return "";
}

int platform_get_snd_device_name_extn(void *platform, snd_device_t snd_device,
                                      char *device_name)
{
    struct platform_data *my_data = (struct platform_data *)platform;

    if (snd_device >= SND_DEVICE_MIN && snd_device < SND_DEVICE_MAX) {
        if (operator_specific_device_table[snd_device] != NULL) {
            strlcpy(device_name, get_operator_specific_device_mixer_path(snd_device),
                    DEVICE_NAME_MAX_SIZE);
        } else
            strlcpy(device_name, device_table[snd_device], DEVICE_NAME_MAX_SIZE);
        hw_info_append_hw_type(my_data->hw_info, snd_device, device_name);
    } else {
        strlcpy(device_name, "", DEVICE_NAME_MAX_SIZE);
        return -EINVAL;
    }

    return 0;
}

void platform_add_backend_name(char *mixer_path, snd_device_t snd_device,
                               struct audio_usecase *usecase)
{
    if ((snd_device < SND_DEVICE_MIN) || (snd_device >= SND_DEVICE_MAX)) {
        ALOGE("%s: Invalid snd_device = %d", __func__, snd_device);
        return;
    }

    if ((snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_VBAT ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_2_VBAT) &&
        !(usecase->type == VOICE_CALL || usecase->type == VOIP_CALL)) {
        ALOGI("%s: Not adding vbat speaker device to non voice use cases", __func__);
        return;
    }

    const char * suffix = backend_tag_table[snd_device];

    if (suffix != NULL) {
        strlcat(mixer_path, " ", MIXER_PATH_MAX_LENGTH);
        strlcat(mixer_path, suffix, MIXER_PATH_MAX_LENGTH);

        /* if we can use display-port for voice call and usb mic
         * is connected, choose dp_rx, usb_tx audio route
         */
        if (usecase->type == VOICE_CALL) {
            struct audio_device *adev = usecase->stream.out->dev;
            if (((snd_device == SND_DEVICE_OUT_DISPLAY_PORT) ||
                (snd_device == SND_DEVICE_OUT_DISPLAY_PORT1)) &&
                adev->dp_allowed_for_voice &&
                (usecase->in_snd_device == SND_DEVICE_IN_VOICE_USB_HEADSET_MIC)) {
                strlcat(mixer_path, "-and-usb-headset-mic", MIXER_PATH_MAX_LENGTH);
            }
        }
    }
}

bool platform_check_backends_match(snd_device_t snd_device1, snd_device_t snd_device2)
{
    bool result = true;

    ALOGV("%s: snd_device1 = %s, snd_device2 = %s", __func__,
                platform_get_snd_device_name(snd_device1),
                platform_get_snd_device_name(snd_device2));

    if ((snd_device1 < SND_DEVICE_MIN) || (snd_device1 >= SND_DEVICE_OUT_END)) {
        ALOGE("%s: Invalid snd_device = %s", __func__,
                platform_get_snd_device_name(snd_device1));
        return false;
    }
    if ((snd_device2 < SND_DEVICE_MIN) || (snd_device2 >= SND_DEVICE_OUT_END)) {
        ALOGE("%s: Invalid snd_device = %s", __func__,
                platform_get_snd_device_name(snd_device2));
        return false;
    }
    const char * be_itf1 = hw_interface_table[snd_device1];
    const char * be_itf2 = hw_interface_table[snd_device2];

    if (NULL != be_itf1 && NULL != be_itf2) {
        if ((NULL == strstr(be_itf2, be_itf1)) && (NULL == strstr(be_itf1, be_itf2)))
            result = false;
    } else if (NULL == be_itf1 && NULL != be_itf2 && (NULL == strstr(be_itf2, default_rx_backend))) {
            result = false;
    } else if (NULL != be_itf1 && NULL == be_itf2 && (NULL == strstr(be_itf1, default_rx_backend))) {
            result = false;
    }

    ALOGV("%s: be_itf1 = %s, be_itf2 = %s, match %d", __func__, be_itf1, be_itf2, result);
    return result;
}

bool platform_check_all_backends_match(snd_device_t snd_device1, snd_device_t snd_device2)
{
    bool result = true;

    if ((snd_device1 < SND_DEVICE_MIN) || (snd_device1 >= SND_DEVICE_MAX)) {
        ALOGE("%s: Invalid snd_device = %s", __func__,
                platform_get_snd_device_name(snd_device1));
        return false;
    }

    if ((snd_device2 < SND_DEVICE_MIN) || (snd_device2 >= SND_DEVICE_MAX)) {
        ALOGE("%s: Invalid snd_device = %s", __func__,
                platform_get_snd_device_name(snd_device2));
        return false;
    }

    const char * be_itf1 = hw_interface_table[snd_device1];
    const char * be_itf2 = hw_interface_table[snd_device2];

    if (snd_device1 < SND_DEVICE_OUT_END && snd_device2 < SND_DEVICE_OUT_END) {
        return platform_check_backends_match(snd_device1, snd_device2);
    } else if (snd_device1 >= SND_DEVICE_IN_BEGIN && snd_device2 >= SND_DEVICE_IN_BEGIN) {
        if (NULL != be_itf1 && NULL != be_itf2) {
            if (strcmp(be_itf2, be_itf1))
                result = false;
        }
    } else {
        result = false;
    }

    ALOGV("%s: be_itf1 = %s, be_itf2 = %s, match %d", __func__, be_itf1, be_itf2, result);
    return result;
}

int platform_get_pcm_device_id(audio_usecase_t usecase, int device_type)
{
    int device_id = -1;

    if ((usecase >= AUDIO_USECASE_MAX) || (usecase <= USECASE_INVALID)) {
        ALOGE("%s: invalid usecase case idx %d", __func__, usecase);
        return device_id;
    }
    if (device_type == PCM_PLAYBACK)
        device_id = pcm_device_table[usecase][0];
    else
        device_id = pcm_device_table[usecase][1];
    return device_id;
}

uint64_t getQtime()
{
    uint64_t qTimerCount = 0;

#if __aarch64__
    asm volatile("mrs %0, cntvct_el0" : "=r" (qTimerCount));
#else
    asm volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r" (qTimerCount));
#endif

    return qTimerCount;
}

int platform_get_delay(void *platform, int pcm_device_id)
{
    int ctl_len = 0;
    struct audio_device *adev = NULL;
    struct mixer_ctl *ctl = NULL;
    const char *mixer_ctl_name = "ADSP Path Latency";
    const char *deviceNo = "NN";
    char *mixer_str = NULL;
    int path_delay = 0;

    if (NULL == platform) {
        ALOGE("%s: platform is NULL", __func__);
        return -EINVAL;
    }
    if (pcm_device_id <= 0) {
        ALOGE("%s: invalid pcm device id: %d", __func__, pcm_device_id);
        return -EINVAL;
    }

    adev = ((struct platform_data *)platform)->adev;

    // Mixer control format: "ADSP Path Latency NN"
    ctl_len = strlen(mixer_ctl_name) + 1 + strlen(deviceNo) + 1;

    mixer_str = (char*) calloc(ctl_len, sizeof(char));
    if (!mixer_str) {
        ALOGE("%s: Could not allocate memory", __func__);
        return -ENOMEM;
    }

    snprintf(mixer_str, ctl_len, "%s %d", mixer_ctl_name, pcm_device_id);

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_str);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s", __func__, mixer_str);
        free(mixer_str);
        return -EINVAL;
    }

    path_delay = mixer_ctl_get_value(ctl, 0);
    if (path_delay < 0) {
        ALOGE("%s: Could not get val for mixer cmd - %s", __func__, mixer_str);
    }
    ALOGD("%s: Path Delay: %d", __func__, path_delay);

    free(mixer_str);
    return path_delay;
}

int send_qtime(void *platform, uint64_t qtime_value, int pcm_device_id)
{
    int ret = 0;
    int ctl_len = 0;
    struct audio_device *adev = ((struct platform_data *)platform)->adev;
    struct mixer_ctl *ctl = NULL;
    const char *mixer_ctl_name = "QTimer";
    const char *deviceNo = "NN";
    char *mixer_str = NULL;
    uint32_t set_values[2];

    set_values[0] = (uint32_t)qtime_value;
    set_values[1] = (uint32_t)((qtime_value >> 16) >> 16);
    ALOGD("%s: Send qtime msw: %u, lsw: %u", __func__, set_values[1],
          set_values[0]);

    // Mixer control format: "Qtimer NN"
    ctl_len = strlen(mixer_ctl_name) + 1 + strlen(deviceNo) + 1;

    mixer_str = (char*) calloc(ctl_len, sizeof(char));
    if (!mixer_str) {
        ALOGE("%s: Could not allocate memory", __func__);
        return -ENOMEM;
    }

    snprintf(mixer_str, ctl_len, "%s %d", mixer_ctl_name, pcm_device_id);

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_str);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_str);
        free(mixer_str);
        return -EINVAL;
    }

    ret = mixer_ctl_set_array(ctl, set_values, ARRAY_SIZE(set_values));
    if (ret < 0) {
        ALOGE("%s: Could not set array for mixer cmd - %s",
              __func__, mixer_str);
    }
    free(mixer_str);

    return ret;
}

int platform_set_qtime(void *platform, int audio_pcm_device_id,
                        int haptic_pcm_device_id)
{
    int ret = 0;
    uint64_t qtime_count = 0;
    uint64_t qtime_value = 0;
    uint32_t qtime_remainder = 0;
    int32_t audio_path_latency = 0;
    int32_t haptic_path_latency = 0;

    if (NULL == platform) {
        ALOGE("%s: platform is NULL", __func__);
        return -EINVAL;
    }
    if (audio_pcm_device_id <= 0 || haptic_pcm_device_id <= 0) {
        ALOGE("%s: Invalid pcm device id - %d", __func__,
              audio_pcm_device_id <= 0 ? audio_pcm_device_id
              : haptic_pcm_device_id);
        return -EINVAL;
    }

    audio_path_latency = platform_get_delay(platform, audio_pcm_device_id);
    if (audio_path_latency <= 0) {
        ALOGE("%s: error getting audio path latency: %d", __func__,
              audio_path_latency);
        return -EINVAL;
    }
    ALOGD("%s: Audio Path Latency: %d", __func__, audio_path_latency);

    haptic_path_latency = platform_get_delay(platform, haptic_pcm_device_id);
    if (haptic_path_latency <= 0) {
        ALOGE("%s: error getting haptic path latency: %d", __func__,
              haptic_path_latency);
        return -EINVAL;
    }
    ALOGD("%s: Haptic Path Latency: %d", __func__, haptic_path_latency);

    qtime_count = getQtime();

    // Qtime count / Qtime freq (KHZ) = Qtime in milliseconds
    qtime_value = (uint64_t) (qtime_count / QTIME_FREQ_KHZ);

    // Convert Qtime to microseconds
    qtime_value *= 1000;

    // Adding max(path_latency)
    qtime_value += (uint32_t) max(audio_path_latency, haptic_path_latency);

    // Adding IPC delay + error correction ~10ms
    qtime_value += IPC_ERROR_DELAY;

    // Calculate remainder in microseconds
    qtime_remainder = ((qtime_count % QTIME_FREQ_KHZ) * 1000) / QTIME_FREQ_KHZ;

    // Add the remainder to qtime
    qtime_value += qtime_remainder;
    ALOGD("%s: Set qtime: %llu microsecs\n", __func__,
          (unsigned long long int)qtime_value);

    ret = send_qtime(platform, qtime_value, haptic_pcm_device_id);
    if (ret < 0) {
        ALOGE("%s: Could not send qtime for haptic session - %d",
              __func__, ret);
        return ret;
    }

    ret = send_qtime(platform, qtime_value, audio_pcm_device_id);
    if (ret < 0) {
        ALOGE("%s: Could not send qtime for audio session - %d",
              __func__, ret);
    }

    return ret;
}

static int find_index(struct name_to_index * table, int32_t len, const char * name)
{
    int ret = 0;
    int32_t i;

    if (table == NULL) {
        ALOGE("%s: table is NULL", __func__);
        ret = -ENODEV;
        goto done;
    }

    if (name == NULL) {
        ALOGE("null key");
        ret = -ENODEV;
        goto done;
    }

    for (i=0; i < len; i++) {
        const char* tn = table[i].name;
        size_t len = strlen(tn);
        if (strncmp(tn, name, len) == 0) {
            if (strlen(name) != len) {
                continue; // substring
            }
            ret = table[i].index;
            goto done;
        }
    }
    ALOGE("%s: Could not find index for name = %s",
            __func__, name);
    ret = -ENODEV;
done:
    return ret;
}

int platform_set_fluence_type(void *platform, char *value)
{
    int ret = 0;
    int fluence_type = FLUENCE_NONE;
    int fluence_flag = NONE_FLAG;
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;

    ALOGV("%s: fluence type:%d", __func__, my_data->fluence_type);

    /* only dual mic turn on and off is supported as of now through setparameters */
    if (!strncmp(AUDIO_PARAMETER_VALUE_DUALMIC,value, sizeof(AUDIO_PARAMETER_VALUE_DUALMIC))) {
        if (!strncmp("fluencepro", my_data->fluence_cap, sizeof("fluencepro")) ||
            !strncmp("fluence", my_data->fluence_cap, sizeof("fluence"))) {
            ALOGV("fluence dualmic feature enabled \n");
            fluence_type = FLUENCE_DUAL_MIC;
            fluence_flag = DMIC_FLAG;
        } else {
            ALOGE("%s: Failed to set DUALMIC", __func__);
            ret = -1;
            goto done;
        }
    } else if (!strncmp(AUDIO_PARAMETER_KEY_NO_FLUENCE, value, sizeof(AUDIO_PARAMETER_KEY_NO_FLUENCE))) {
        ALOGV("fluence disabled");
        fluence_type = FLUENCE_NONE;
    } else {
        ALOGE("Invalid fluence value : %s",value);
        ret = -1;
        goto done;
    }

    if (fluence_type != my_data->fluence_type) {
        ALOGV("%s: Updating fluence_type to :%d", __func__, fluence_type);
        my_data->fluence_type = fluence_type;
        adev->acdb_settings = (adev->acdb_settings & FLUENCE_MODE_CLEAR) | fluence_flag;
    }
done:
    return ret;
}

int platform_get_fluence_type(void *platform, char *value, uint32_t len)
{
    int ret = 0;
    struct platform_data *my_data = (struct platform_data *)platform;

    if (my_data->fluence_type == FLUENCE_QUAD_MIC) {
        strlcpy(value, "quadmic", len);
    } else if (my_data->fluence_type == FLUENCE_TRI_MIC) {
        strlcpy(value, "trimic", len);
    } else if (my_data->fluence_type == FLUENCE_DUAL_MIC) {
        strlcpy(value, "dualmic", len);
    } else if (my_data->fluence_type == FLUENCE_NONE) {
        strlcpy(value, "none", len);
    } else
        ret = -1;

    return ret;
}

int platform_get_snd_device_index(char *device_name)
{
    return find_index(snd_device_name_index, SND_DEVICE_MAX, device_name);
}

int platform_get_usecase_index(const char *usecase_name)
{
    return find_index(usecase_name_index, AUDIO_USECASE_MAX, usecase_name);
}

int platform_get_audio_source_index(const char *audio_source_name)
{
    return find_index(audio_source_index, AUDIO_SOURCE_CNT, audio_source_name);
}

void platform_add_operator_specific_device(snd_device_t snd_device,
                                           const char *operator,
                                           const char *mixer_path,
                                           unsigned int acdb_id)
{
    struct operator_specific_device *device;

    if (operator_specific_device_table[snd_device] == NULL) {
        operator_specific_device_table[snd_device] =
            (struct listnode *)calloc(1, sizeof(struct listnode));
        list_init(operator_specific_device_table[snd_device]);
    }

    device = (struct operator_specific_device *)calloc(1, sizeof(struct operator_specific_device));
    if (device == NULL) {
        ALOGE("%s: memory allocation failed", __func__);
        return;
    }

    device->operator = strdup(operator);
    device->mixer_path = strdup(mixer_path);
    device->acdb_id = acdb_id;

    list_add_tail(operator_specific_device_table[snd_device], &device->list);

    ALOGD("%s: device[%s] -> operator[%s] mixer_path[%s] acdb_id[%d]", __func__,
            platform_get_snd_device_name(snd_device), operator, mixer_path, acdb_id);

}

int platform_get_effect_config_data(snd_device_t snd_device,
                                      struct audio_effect_config *effect_config,
                                      effect_type_t effect_type)
{
    int ret = 0;

    if ((snd_device < SND_DEVICE_IN_BEGIN) || (snd_device >= SND_DEVICE_MAX) ||
        (effect_type <= EFFECT_NONE) || (effect_type >= EFFECT_MAX)) {
        ALOGE("%s: Invalid snd_device = %d",
            __func__, snd_device);
        ret = -EINVAL;
        goto done;
    }

    if (effect_config == NULL) {
        ALOGE("%s: Invalid effect_config", __func__);
        ret = -EINVAL;
        goto done;
    }

    ALOGV("%s: snd_device = %d module_id = %d",
            __func__, snd_device, effect_config_table[GET_IN_DEVICE_INDEX(snd_device)][effect_type].module_id);
    memcpy(effect_config, &effect_config_table[GET_IN_DEVICE_INDEX(snd_device)][effect_type],
           sizeof(struct audio_effect_config));

done:
    return ret;
}

void platform_add_external_specific_device(snd_device_t snd_device,
                                           const char *usbid,
                                           unsigned int acdb_id)
{
    struct external_specific_device *device;

    if (external_specific_device_table[snd_device] == NULL) {
        external_specific_device_table[snd_device] =
            (struct listnode *)calloc(1, sizeof(struct listnode));
        list_init(external_specific_device_table[snd_device]);
    }

    device = (struct external_specific_device *)calloc(1, sizeof(struct external_specific_device));
    if (device == NULL) {
        ALOGE("%s: memory allocation failed", __func__);
        return;
    }

    device->usbid = strdup(usbid);
    device->acdb_id = acdb_id;

    list_add_tail(external_specific_device_table[snd_device], &device->list);

    ALOGD("%s: device[%s] usbid[%s] -> acdb_id[%d]", __func__,
            platform_get_snd_device_name(snd_device), usbid, acdb_id);
}

static int platform_get_fluence_mmsecns_config_data(struct audio_fluence_mmsecns_config *fluence_mmsecns_config)
{
    int ret = 0;

    if (fluence_mmsecns_config == NULL) {
        ALOGE("%s: Invalid fluence_mmsecns_config", __func__);
        ret = -EINVAL;
        goto done;
    }

    ALOGV("%s: topology_id = 0x%x, module_id = 0x%x, instance_id = 0x%x, param_id = 0x%x",
           __func__, fluence_mmsecns_table.topology_id, fluence_mmsecns_table.module_id,
           fluence_mmsecns_table.instance_id, fluence_mmsecns_table.param_id);
    memcpy(fluence_mmsecns_config, &fluence_mmsecns_table,
           sizeof(struct audio_fluence_mmsecns_config));

done:
    return ret;
}

int platform_set_snd_device_acdb_id(snd_device_t snd_device, unsigned int acdb_id)
{
    int ret = 0;

    if ((snd_device < SND_DEVICE_MIN) || (snd_device >= SND_DEVICE_MAX)) {
        ALOGE("%s: Invalid snd_device = %d",
            __func__, snd_device);
        ret = -EINVAL;
        goto done;
    }

    ALOGV("%s: acdb_device_table[%s]: old = %d new = %d", __func__,
          platform_get_snd_device_name(snd_device), acdb_device_table[snd_device], acdb_id);
    acdb_device_table[snd_device] = acdb_id;
done:
    return ret;
}

int platform_set_effect_config_data(snd_device_t snd_device,
                                      struct audio_effect_config effect_config,
                                      effect_type_t effect_type)
{
    int ret = 0;

    if ((snd_device < SND_DEVICE_IN_BEGIN) || (snd_device >= SND_DEVICE_MAX) ||
        (effect_type <= EFFECT_NONE) || (effect_type >= EFFECT_MAX)) {
        ALOGE("%s: Invalid snd_device = %d",
            __func__, snd_device);
        ret = -EINVAL;
        goto done;
    }

    ALOGV("%s 0x%x 0x%x 0x%x 0x%x", __func__, effect_config.module_id,
           effect_config.instance_id, effect_config.param_id,
           effect_config.param_value);
    effect_config_table[GET_IN_DEVICE_INDEX(snd_device)][effect_type] = effect_config;

done:
    return ret;
}

int platform_set_fluence_mmsecns_config(struct audio_fluence_mmsecns_config fluence_mmsecns_config)
{
    int ret = 0;

    ALOGV("%s: topology_id = 0x%x, module_id = 0x%x, instance_id = 0x%x, param_id = 0x%x",
           __func__, fluence_mmsecns_config.topology_id, fluence_mmsecns_config.module_id,
           fluence_mmsecns_config.instance_id, fluence_mmsecns_config.param_id);
    fluence_mmsecns_table = fluence_mmsecns_config;

    return ret;
}

int platform_set_acdb_metainfo_key(void *platform, char *name, int key)
{
    struct meta_key_list *key_info;
    struct platform_data *pdata = (struct platform_data *)platform;

    key_info = (struct meta_key_list *)calloc(1, sizeof(struct meta_key_list));
    if (!key_info) {
        ALOGE("%s: Could not allocate memory for key %d", __func__, key);
        return -ENOMEM;
    }

    key_info->cal_info.nKey = key;
    strlcpy(key_info->name, name, sizeof(key_info->name));
    list_add_tail(&pdata->acdb_meta_key_list, &key_info->list);

    ALOGD("%s: successfully added module %s and key %d to the list", __func__,
               key_info->name, key_info->cal_info.nKey);
    return 0;
}

int platform_get_meta_info_key_from_list(void *platform, char *mod_name)
{
    struct listnode *node;
    struct meta_key_list *key_info;
    struct platform_data *pdata = (struct platform_data *)platform;
    int key = 0;

    ALOGV("%s: for module %s", __func__, mod_name);

    list_for_each(node, &pdata->acdb_meta_key_list) {
        key_info = node_to_item(node, struct meta_key_list, list);
        if (strcmp(key_info->name, mod_name) == 0) {
            key = key_info->cal_info.nKey;
            ALOGD("%s: Found key %d for module %s", __func__, key, mod_name);
            break;
        }
    }
    return key;
}

int platform_get_default_app_type(void *platform)
{
    struct platform_data *my_data = (struct platform_data *)platform;

    if (my_data->acdb_get_default_app_type)
        return my_data->acdb_get_default_app_type();
    else
        return DEFAULT_APP_TYPE_RX_PATH;
}

int platform_get_default_app_type_v2(void *platform, usecase_type_t  type)
{
    ALOGV("%s: Platform: %p, type: %d", __func__, platform, type);
    if(type == PCM_CAPTURE)
        return DEFAULT_APP_TYPE_TX_PATH;
    else
        return DEFAULT_APP_TYPE_RX_PATH;
}

int platform_get_snd_device_acdb_id(snd_device_t snd_device)
{
    if ((snd_device < SND_DEVICE_MIN) || (snd_device >= SND_DEVICE_MAX)) {
        ALOGE("%s: Invalid snd_device = %d", __func__, snd_device);
        return -EINVAL;
    }

    /*
     * If speaker protection is enabled, function returns supported
     * sound device for speaker. Else same sound device is returned.
     */
    snd_device = platform_get_spkr_prot_snd_device(snd_device);

    if (operator_specific_device_table[snd_device] != NULL)
        return get_operator_specific_device_acdb_id(snd_device);
    else if (external_specific_device_table[snd_device] != NULL)
        return get_external_specific_device_acdb_id(snd_device);
    else
        return acdb_device_table[snd_device];
}

int platform_set_snd_device_bit_width(snd_device_t snd_device, unsigned int bit_width)
{
    int ret = 0;

    if ((snd_device < SND_DEVICE_MIN) || (snd_device >= SND_DEVICE_MAX)) {
        ALOGE("%s: Invalid snd_device = %d",
            __func__, snd_device);
        ret = -EINVAL;
        goto done;
    }

    backend_bit_width_table[snd_device] = bit_width;
done:
    return ret;
}

int platform_get_snd_device_bit_width(snd_device_t snd_device)
{
    if ((snd_device < SND_DEVICE_MIN) || (snd_device >= SND_DEVICE_MAX)) {
        ALOGE("%s: Invalid snd_device = %d", __func__, snd_device);
        return CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    }
    return backend_bit_width_table[snd_device];
}

int platform_set_native_support(int na_mode)
{
    if (NATIVE_AUDIO_MODE_SRC == na_mode || NATIVE_AUDIO_MODE_TRUE_44_1 == na_mode
        || NATIVE_AUDIO_MODE_MULTIPLE_MIX_IN_CODEC == na_mode
        || NATIVE_AUDIO_MODE_MULTIPLE_MIX_IN_DSP == na_mode) {
        na_props.platform_na_prop_enabled = na_props.ui_na_prop_enabled = true;
        na_props.na_mode = na_mode;
        ALOGD("%s:napb: native audio playback enabled in (%s) mode", __func__,
              ((na_mode == NATIVE_AUDIO_MODE_SRC)?"SRC":
               (na_mode == NATIVE_AUDIO_MODE_TRUE_44_1)?"True":
               (na_mode == NATIVE_AUDIO_MODE_MULTIPLE_MIX_IN_CODEC)?"Multiple_Mix_Codec":"Multiple_Mix_DSP"));
    }
    else {
        na_props.platform_na_prop_enabled = false;
        na_props.na_mode = NATIVE_AUDIO_MODE_INVALID;
        ALOGD("%s:napb: native audio playback disabled", __func__);
    }

    return 0;
}

bool platform_check_codec_dsd_support(void *platform)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    return my_data->is_dsd_supported;
}

bool platform_check_codec_asrc_support(void *platform)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    return my_data->is_asrc_supported;
}

int platform_get_native_support()
{
    int ret = NATIVE_AUDIO_MODE_INVALID;
    if (na_props.platform_na_prop_enabled &&
        na_props.ui_na_prop_enabled) {
        ret = na_props.na_mode;
    }
    ALOGV("%s:napb: ui Prop enabled(%d) version(%d)", __func__,
           na_props.ui_na_prop_enabled, na_props.na_mode);
    return ret;
}

void native_audio_get_params(struct str_parms *query,
                             struct str_parms *reply,
                             char *value, int len)
{
    int ret;
    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_NATIVE_AUDIO,
                            value, len);
    if (ret >= 0) {
        if (na_props.platform_na_prop_enabled) {
            str_parms_add_str(reply, AUDIO_PARAMETER_KEY_NATIVE_AUDIO,
                          na_props.ui_na_prop_enabled ? "true" : "false");
            ALOGV("%s:napb: na_props.ui_na_prop_enabled: %d", __func__,
                  na_props.ui_na_prop_enabled);
        } else {
            str_parms_add_str(reply, AUDIO_PARAMETER_KEY_NATIVE_AUDIO,
                              "false");
            ALOGV("%s:napb: native audio not supported: %d", __func__,
                  na_props.platform_na_prop_enabled);
        }
    }
}

int native_audio_set_params(struct platform_data *platform,
                            struct str_parms *parms, char *value, int len)
{
    int ret = -1;
    struct audio_usecase *usecase;
    struct listnode *node;
    int mode = NATIVE_AUDIO_MODE_INVALID;

    if (!value || !parms)
        return ret;

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_NATIVE_AUDIO_MODE,
                             value, len);
    if (ret >= 0) {
        if (value && !strncmp(value, "src", sizeof("src")))
            mode = NATIVE_AUDIO_MODE_SRC;
        else if (value && !strncmp(value, "true", sizeof("true")))
            mode = NATIVE_AUDIO_MODE_TRUE_44_1;
        else if (value && !strncmp(value, "multiple_mix_codec", sizeof("multiple_mix_codec")))
            mode = NATIVE_AUDIO_MODE_MULTIPLE_MIX_IN_CODEC;
        else if (value && !strncmp(value, "multiple_mix_dsp", sizeof("multiple_mix_dsp")))
            mode = NATIVE_AUDIO_MODE_MULTIPLE_MIX_IN_DSP;
        else {
            mode = NATIVE_AUDIO_MODE_INVALID;
            ALOGE("%s:napb:native_audio_mode in platform info xml,invalid mode string",
                  __func__);
        }
        ALOGD("%s:napb updating mode (%d) from XML",__func__, mode);
        platform_set_native_support(mode);
    }



    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_NATIVE_AUDIO,
                             value, len);
    if (ret >= 0) {
        if (na_props.platform_na_prop_enabled) {
            if (!strncmp("true", value, sizeof("true"))) {
                na_props.ui_na_prop_enabled = true;
                ALOGD("%s:napb: native audio feature enabled from UI",
                    __func__);
            } else {
                na_props.ui_na_prop_enabled = false;
                ALOGD("%s:napb: native audio feature disabled from UI",
                      __func__);
            }

            str_parms_del(parms, AUDIO_PARAMETER_KEY_NATIVE_AUDIO);

            /*
             * Iterate through the usecase list and trigger device switch for
             * all the appropriate usecases
             */
            list_for_each(node, &(platform->adev)->usecase_list) {
                 usecase = node_to_item(node, struct audio_usecase, list);

                 if (usecase->stream.out && is_offload_usecase(usecase->id) &&
                     (compare_device_type(&usecase->stream.out->device_list,
                                        AUDIO_DEVICE_OUT_WIRED_HEADPHONE) ||
                     compare_device_type(&usecase->stream.out->device_list,
                                         AUDIO_DEVICE_OUT_WIRED_HEADSET)) &&
                     OUTPUT_SAMPLING_RATE_44100 == usecase->stream.out->sample_rate) {
                         ALOGD("%s:napb: triggering dynamic device switch for usecase %d, %s"
                               " stream %p, device (%u)", __func__, usecase->id,
                               use_case_table[usecase->id],
                               (void*) usecase->stream.out,
                               get_device_types(&usecase->stream.out->device_list));
                         select_devices(platform->adev, usecase->id);
                 }
            }
        } else
              ALOGD("%s:napb: native audio cannot be enabled from UI",
                    __func__);
    }
    return ret;
}

int check_hdset_combo_device(snd_device_t snd_device)
{
    int ret = false;

    if (SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES == snd_device ||
        SND_DEVICE_OUT_SPEAKER_AND_LINE == snd_device ||
        SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_1 == snd_device ||
        SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_2 == snd_device ||
        SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET == snd_device ||
        SND_DEVICE_OUT_SPEAKER_AND_ANC_FB_HEADSET == snd_device)
        ret = true;

    return ret;
}

int codec_device_supports_native_playback(struct listnode *out_devices)
{
    int ret = false;

    if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) ||
        compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADSET) ||
        compare_device_type(out_devices, AUDIO_DEVICE_OUT_LINE) ||
        compare_device_type(out_devices, AUDIO_DEVICE_OUT_USB_HEADSET))
        ret = true;

    return ret;
}

int is_hdset_combo_device(struct listnode *out_devices)
{
     int ret = false;

     if ((compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) &&
          compare_device_type(out_devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE)) ||
         (compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADSET) &&
          compare_device_type(out_devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE)) ||
         (compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) &&
          compare_device_type(out_devices, AUDIO_DEVICE_OUT_SPEAKER)) ||
         (compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADSET) &&
          compare_device_type(out_devices, AUDIO_DEVICE_OUT_SPEAKER)))
         ret = true;

     return ret;
}

int platform_get_backend_index(snd_device_t snd_device)
{
    int32_t port = DEFAULT_CODEC_BACKEND;

    if (snd_device >= SND_DEVICE_OUT_BEGIN && snd_device < SND_DEVICE_OUT_END) {
        if (backend_tag_table[snd_device] != NULL) {
                if (strncmp(backend_tag_table[snd_device], "headphones-44.1",
                            sizeof("headphones-44.1")) == 0)
                        port = HEADPHONE_44_1_BACKEND;
                else if (strncmp(backend_tag_table[snd_device], "headphones-hifi-filter",
                            sizeof("headphones-hifi-filter")) == 0)
                        port = HEADPHONE_BACKEND;
                else if (strncmp(backend_tag_table[snd_device], "headphones-dsd",
                            sizeof("headphones-dsd")) == 0)
                        port = DSD_NATIVE_BACKEND;
                else if (strncmp(backend_tag_table[snd_device], "headphones",
                            sizeof("headphones")) == 0)
                        port = HEADPHONE_BACKEND;
                else if (strncmp(backend_tag_table[snd_device], "headset",
                            sizeof("headset")) == 0)
                        port = HEADPHONE_BACKEND;
                else if (strcmp(backend_tag_table[snd_device], "hdmi") == 0)
                        port = HDMI_RX_BACKEND;
                else if (strcmp(backend_tag_table[snd_device], "display-port") == 0)
                        port = DISP_PORT_RX_BACKEND;
                else if (strcmp(backend_tag_table[snd_device], "display-port1") == 0)
                        port = DISP_PORT1_RX_BACKEND;
                else if ((strcmp(backend_tag_table[snd_device], "usb-headphones") == 0) ||
                           (strcmp(backend_tag_table[snd_device], "usb-headset") == 0))
                        port = USB_AUDIO_RX_BACKEND;
                else if (strcmp(backend_tag_table[snd_device], "call-proxy") == 0)
                        port = CALL_PROXY_RX_BACKEND;
                else if (strncmp(backend_tag_table[snd_device], "headphones-and-haptics",
                            sizeof("headphones-and-haptics")) == 0)
                        port = HEADPHONE_BACKEND;
        }
    } else if (snd_device >= SND_DEVICE_IN_BEGIN && snd_device < SND_DEVICE_IN_END) {
        port = DEFAULT_CODEC_TX_BACKEND;
        if (backend_tag_table[snd_device] != NULL) {
                if (strcmp(backend_tag_table[snd_device], "usb-headset-mic") == 0)
                        port = USB_AUDIO_TX_BACKEND;
                else if (strstr(backend_tag_table[snd_device], "bt-sco") != NULL)
                        port = BT_SCO_TX_BACKEND;
                else if (strcmp(backend_tag_table[snd_device], "spdif-in") == 0)
                        port = SPDIF_TX_BACKEND;
                else if (strcmp(backend_tag_table[snd_device], "hdmi-in") == 0)
                        port = HDMI_TX_BACKEND;
                else if (strcmp(backend_tag_table[snd_device], "hdmi-arc-in") == 0)
                        port = HDMI_ARC_TX_BACKEND;
                else if (strcmp(backend_tag_table[snd_device], "headset-mic") == 0)
                        port = HEADSET_TX_BACKEND;
                else if (strcmp(backend_tag_table[snd_device], "call-proxy-in") == 0)
                        port = CALL_PROXY_TX_BACKEND;
        }
    } else {
        ALOGW("%s:napb: Invalid device - %d ", __func__, snd_device);
    }

    ALOGV("%s:napb: backend port - %d device - %d ", __func__, port, snd_device);
    return port;
}

int platform_send_audio_calibration(void *platform, struct audio_usecase *usecase,
                                    int app_type)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    int acdb_dev_id, acdb_dev_type;
    int snd_device = SND_DEVICE_OUT_SPEAKER;
    int new_snd_device[SND_DEVICE_OUT_END] = {0};
    int i, num_devices = 1;
    bool is_incall_rec_usecase = false;
    snd_device_t incall_rec_device;
    int sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
    struct audio_backend_cfg backend_cfg = {0};
    bool is_bus_dev_usecase = false;

    if (voice_is_in_call_or_call_screen(my_data->adev))
        is_incall_rec_usecase = voice_is_in_call_rec_stream(usecase->stream.in);

    if (compare_device_type(&usecase->device_list, AUDIO_DEVICE_OUT_BUS))
        is_bus_dev_usecase = true;

    if (usecase->type == PCM_PLAYBACK)
        snd_device = usecase->out_snd_device;
    else if ((usecase->type == PCM_CAPTURE) && is_incall_rec_usecase)
        snd_device = voice_get_incall_rec_snd_device(usecase->in_snd_device);
    else if ((usecase->type == PCM_HFP_CALL) || (usecase->type == PCM_CAPTURE))
        snd_device = usecase->in_snd_device;
    else if (usecase->type == TRANSCODE_LOOPBACK_RX)
        snd_device = usecase->out_snd_device;

    acdb_dev_id = acdb_device_table[platform_get_spkr_prot_snd_device(snd_device)];
    if (acdb_dev_id < 0) {
        ALOGE("%s: Could not find acdb id for device(%d)",
              __func__, snd_device);
        return -EINVAL;
    }

    if (!is_incall_rec_usecase) {
        if (platform_split_snd_device(my_data, snd_device,
                                      &num_devices, new_snd_device) < 0) {
            new_snd_device[0] = snd_device;
        }
    } else {
        incall_rec_device = voice_get_incall_rec_backend_device(usecase->stream.in);
        if (platform_split_snd_device(my_data, incall_rec_device,
                                      &num_devices, new_snd_device) < 0) {
            new_snd_device[0] = snd_device;
        }
    }
    if ((usecase->type == PCM_HFP_CALL) && is_bus_dev_usecase) {
        num_devices = 2;
        new_snd_device[0] = usecase->in_snd_device;
        new_snd_device[1] = usecase->out_snd_device;
    }

    for (i = 0; i < num_devices; i++) {
        if (!is_incall_rec_usecase) {
            acdb_dev_id = acdb_device_table[platform_get_spkr_prot_snd_device(new_snd_device[i])];
            sample_rate = audio_extn_utils_get_app_sample_rate_for_device(my_data->adev, usecase,
                                                          new_snd_device[i]);
            platform_get_codec_backend_cfg(my_data->adev, new_snd_device[i], &backend_cfg);
        } else {
            // Use in_call_rec snd_device to extract the ACDB device ID instead of split snd devices
            acdb_dev_id = acdb_device_table[platform_get_spkr_prot_snd_device(snd_device)];
            sample_rate = audio_extn_utils_get_app_sample_rate_for_device(my_data->adev, usecase,
                                                          snd_device);
            platform_get_codec_backend_cfg(my_data->adev, snd_device, &backend_cfg);
        }

        // Do not use Rx path default app type for TX path
        if ((usecase->type == PCM_CAPTURE) && (app_type == DEFAULT_APP_TYPE_RX_PATH)) {
            ALOGD("Resetting app type for Tx path to default");
            app_type  = DEFAULT_APP_TYPE_TX_PATH;
        } else if ((usecase->type == PCM_HFP_CALL) && is_bus_dev_usecase) {
            if (new_snd_device[i] >= SND_DEVICE_OUT_BEGIN &&
                new_snd_device[i] < SND_DEVICE_OUT_END) {
                app_type  = usecase->out_app_type_cfg.app_type;
                sample_rate = usecase->out_app_type_cfg.sample_rate;
            } else {
                app_type  = usecase->in_app_type_cfg.app_type;
                sample_rate = usecase->in_app_type_cfg.sample_rate;
            }
            ALOGD("%s: Updating to app type (%d) and sample rate (%d)",
                  __func__, app_type, sample_rate);
        }

        /* Override backend cfg sample rate in calibration for vi feedback usecase */
        if (usecase->id == USECASE_AUDIO_SPKR_CALIB_TX) {
            ALOGV("Reset backend cfg sample rate to 8KHz for spkr calib Tx use case");
            backend_cfg.sample_rate = sample_rate;
        }

        if (acdb_dev_id < 0) {
            ALOGE("%s: Could not find acdb id for device(%d)",
                  __func__, new_snd_device[i]);
            return -EINVAL;
        }

        /* Notify device change info to effect clients registered */
        if (usecase->type == PCM_PLAYBACK) {
            audio_extn_gef_notify_device_config(
                    &usecase->stream.out->device_list,
                    usecase->stream.out->channel_mask,
                    sample_rate,
                    acdb_dev_id,
                    usecase->stream.out->app_type_cfg.app_type);
        }

        ALOGV("%s: sending audio calibration for snd_device(%d) acdb_id(%d)",
              __func__, new_snd_device[i], acdb_dev_id);
        if (new_snd_device[i] >= SND_DEVICE_OUT_BEGIN &&
                new_snd_device[i] < SND_DEVICE_OUT_END)
            acdb_dev_type = ACDB_DEV_TYPE_OUT;
        else
            acdb_dev_type = ACDB_DEV_TYPE_IN;

        if (my_data->acdb_send_audio_cal_v4) {
            my_data->acdb_send_audio_cal_v4(acdb_dev_id, acdb_dev_type,
                                            app_type, sample_rate, i,
                                            backend_cfg.sample_rate);
        } else if (my_data->acdb_send_audio_cal_v3) {
            my_data->acdb_send_audio_cal_v3(acdb_dev_id, acdb_dev_type,
                                            app_type, sample_rate, i);
        } else if (my_data->acdb_send_audio_cal) {
            my_data->acdb_send_audio_cal(acdb_dev_id, acdb_dev_type, app_type,
                                         sample_rate);
        }
    }

    return 0;
}

int platform_switch_voice_call_device_pre(void *platform)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    int ret = 0;

    if (my_data->csd != NULL &&
        voice_is_in_call_or_call_screen(my_data->adev)) {
        /* This must be called before disabling mixer controls on APQ side */
        ret = my_data->csd->disable_device();
        if (ret < 0) {
            ALOGE("%s: csd_client_disable_device, failed, error %d",
                  __func__, ret);
        }
    }
    return ret;
}

int platform_switch_voice_call_enable_device_config(void *platform,
                                                    snd_device_t out_snd_device,
                                                    snd_device_t in_snd_device)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    int acdb_rx_id, acdb_tx_id;
    int ret = 0;

    if (my_data->csd == NULL)
        return ret;

    if ((out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER ||
         out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_STEREO ||
         out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_2 ||
         out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_VBAT ||
         out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_2_VBAT) &&
         audio_extn_spkr_prot_is_enabled()) {
        if (my_data->is_vbat_speaker || my_data->is_bcl_speaker)
            acdb_rx_id = acdb_device_table[SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT];
        else
            acdb_rx_id = acdb_device_table[SND_DEVICE_OUT_SPEAKER_PROTECTED];
    } else
        acdb_rx_id = platform_get_snd_device_acdb_id(out_snd_device);

    acdb_tx_id = platform_get_snd_device_acdb_id(in_snd_device);

    if (acdb_rx_id > 0 && acdb_tx_id > 0) {
        ret = my_data->csd->enable_device_config(acdb_rx_id, acdb_tx_id);
        if (ret < 0) {
            ALOGE("%s: csd_enable_device_config, failed, error %d",
                  __func__, ret);
        }
    } else {
        ALOGE("%s: Incorrect ACDB IDs (rx: %d tx: %d)", __func__,
              acdb_rx_id, acdb_tx_id);
    }

    return ret;
}

int platform_switch_voice_call_device_post(void *platform,
                                           snd_device_t out_snd_device,
                                           snd_device_t in_snd_device)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    int acdb_rx_id, acdb_tx_id;

    if (my_data->acdb_send_voice_cal == NULL) {
        ALOGE("%s: dlsym error for acdb_send_voice_call", __func__);
    } else {
        if (audio_extn_spkr_prot_is_enabled()) {
            if (out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER ||
                out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_WSA)
                out_snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED;
            else if (out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_STEREO)
                out_snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_PROTECTED;
            else if (out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_VBAT)
                out_snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED_VBAT;
            else if (out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_2 ||
                out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_2_WSA)
                out_snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED;
            else if (out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_2_VBAT)
                out_snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED_VBAT;
        }

        acdb_rx_id = platform_get_snd_device_acdb_id(out_snd_device);
        acdb_tx_id = platform_get_snd_device_acdb_id(in_snd_device);

        if (acdb_rx_id > 0 && acdb_tx_id > 0)
            my_data->acdb_send_voice_cal(acdb_rx_id, acdb_tx_id);
        else
            ALOGE("%s: Incorrect ACDB IDs (rx: %d tx: %d)", __func__,
                  acdb_rx_id, acdb_tx_id);
    }

    return 0;
}

int platform_switch_voice_call_usecase_route_post(void *platform,
                                                  snd_device_t out_snd_device,
                                                  snd_device_t in_snd_device)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    int acdb_rx_id, acdb_tx_id;
    int ret = 0;

    if (my_data->csd == NULL)
        return ret;

    if ((out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER ||
         out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_2 ||
         out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_STEREO ||
         out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_VBAT ||
         out_snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_2_VBAT) &&
         audio_extn_spkr_prot_is_enabled()) {
        if (my_data->is_vbat_speaker || my_data->is_bcl_speaker)
            acdb_rx_id = acdb_device_table[SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT];
         else
            acdb_rx_id = acdb_device_table[SND_DEVICE_OUT_SPEAKER_PROTECTED];
    } else
        acdb_rx_id = platform_get_snd_device_acdb_id(out_snd_device);

    acdb_tx_id = platform_get_snd_device_acdb_id(in_snd_device);

    if (acdb_rx_id > 0 && acdb_tx_id > 0) {
        ret = my_data->csd->enable_device(acdb_rx_id, acdb_tx_id,
                                          my_data->adev->acdb_settings);
        if (ret < 0) {
            ALOGE("%s: csd_enable_device, failed, error %d", __func__, ret);
        }
    } else {
        ALOGE("%s: Incorrect ACDB IDs (rx: %d tx: %d)", __func__,
              acdb_rx_id, acdb_tx_id);
    }

    return ret;
}

int platform_start_voice_call(void *platform, uint32_t vsid)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    int ret = 0;

    if (my_data->csd != NULL) {
        ret = my_data->csd->start_voice(vsid);
        if (ret < 0) {
            ALOGE("%s: csd_start_voice error %d\n", __func__, ret);
        }
    }
    return ret;
}

int platform_stop_voice_call(void *platform, uint32_t vsid)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    int ret = 0;

    if (my_data->csd != NULL) {
        ret = my_data->csd->stop_voice(vsid);
        if (ret < 0) {
            ALOGE("%s: csd_stop_voice error %d\n", __func__, ret);
        }
    }
    return ret;
}

int platform_set_mic_break_det(void *platform, bool enable)
{
    int ret = 0;
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    const char *mixer_ctl_name = "Voice Mic Break Enable";
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }

    ret = mixer_ctl_set_value(ctl, 0, enable);
    if(ret)
        ALOGE("%s: Failed to set mixer ctl: %s", __func__, mixer_ctl_name);

    return ret;
}

int platform_get_sample_rate(void *platform, uint32_t *rate)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    int ret = 0;

    if ((my_data->csd != NULL) && my_data->is_i2s_ext_modem) {
        ret = my_data->csd->get_sample_rate(rate);
        if (ret < 0) {
            ALOGE("%s: csd_get_sample_rate error %d\n", __func__, ret);
        }
    }
    return ret;
}

void platform_set_speaker_gain_in_combo(struct audio_device *adev,
                                        snd_device_t snd_device,
                                        bool enable)
{
    const char* name;
    switch (snd_device) {
        case SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES:
            if (enable)
                name = "spkr-gain-in-headphone-combo";
            else
                name = "speaker-gain-default";
            break;
        case SND_DEVICE_OUT_SPEAKER_AND_LINE:
            if (enable)
                name = "spkr-gain-in-line-combo";
            else
                name = "speaker-gain-default";
            break;
        case SND_DEVICE_OUT_SPEAKER_SAFE_AND_HEADPHONES:
            if (enable)
                name = "spkr-safe-gain-in-headphone-combo";
            else
                name = "speaker-safe-gain-default";
            break;
        case SND_DEVICE_OUT_SPEAKER_SAFE_AND_LINE:
            if (enable)
                name = "spkr-safe-gain-in-line-combo";
            else
                name = "speaker-safe-gain-default";
            break;
        default:
            return;
    }

    audio_route_apply_and_update_path(adev->audio_route, name);
}

int platform_set_voice_volume(void *platform, int volume)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name = "Voice Rx Gain";
    const char *mute_mixer_ctl_name = "Voice Rx Device Mute";
    int vol_index = 0, ret = 0;
    long set_values[ ] = {0,
                          ALL_SESSION_VSID,
                          DEFAULT_VOLUME_RAMP_DURATION_MS};

    // Voice volume levels are mapped to adsp volume levels as follows.
    // 100 -> 5, 80 -> 4, 60 -> 3, 40 -> 2, 20 -> 1  0 -> 0
    // But this values don't changed in kernel. So, below change is need.
    vol_index = (int)percent_to_index(volume, MIN_VOL_INDEX, my_data->max_vol_index);
    set_values[0] = vol_index;

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }
    ALOGV("%s: Setting voice volume index: %ld", __func__, set_values[0]);
    mixer_ctl_set_array(ctl, set_values, ARRAY_SIZE(set_values));

    // Send mute command in case volume index is max since indexes are inverted
    // for mixer controls.
    if (vol_index == my_data->max_vol_index)
        set_values[0] = 1;
    else
        set_values[0] = 0;

    ctl = mixer_get_ctl_by_name(adev->mixer, mute_mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mute_mixer_ctl_name);
        return -EINVAL;
    }
    ALOGV("%s: Setting RX Device Mute to: %ld", __func__, set_values[0]);
    mixer_ctl_set_array(ctl, set_values, ARRAY_SIZE(set_values));

    if (my_data->csd != NULL) {
        ret = my_data->csd->volume(ALL_SESSION_VSID, volume,
                                   DEFAULT_VOLUME_RAMP_DURATION_MS);
        if (ret < 0) {
            ALOGE("%s: csd_volume error %d", __func__, ret);
        }
    }
    return ret;
}

int platform_set_mic_mute(void *platform, bool state)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name = "Voice Tx Mute";
    int ret = 0;
    long set_values[ ] = {0,
                          ALL_SESSION_VSID,
                          DEFAULT_MUTE_RAMP_DURATION_MS};

    if (adev->mode != AUDIO_MODE_IN_CALL &&
        adev->mode != AUDIO_MODE_IN_COMMUNICATION &&
        adev->mode != AUDIO_MODE_CALL_SCREEN)
        return 0;

    if (adev->enable_hfp)
        mixer_ctl_name = "HFP Tx Mute";

    set_values[0] = state;
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        ret = -EINVAL;
    } else {
        ALOGV("%s Setting voice mute state: %d", __func__, state);
        mixer_ctl_set_array(ctl, set_values, ARRAY_SIZE(set_values));
    }

    if (my_data->csd != NULL) {
        ret = my_data->csd->mic_mute(ALL_SESSION_VSID, state,
                                     DEFAULT_MUTE_RAMP_DURATION_MS);
        if (ret < 0) {
            ALOGE("%s: csd_mic_mute error %d", __func__, ret);
        }
    }
    return ret;
}

int platform_set_device_mute(void *platform, bool state, char *dir)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    struct mixer_ctl *ctl;
    char *mixer_ctl_name = NULL;
    int ret = 0;
    long set_values[ ] = {0,
                          ALL_SESSION_VSID,
                          0};
    if(dir == NULL) {
        ALOGE("%s: Invalid direction:%s", __func__, dir);
        return -EINVAL;
    }

    if (!strncmp("rx", dir, sizeof("rx"))) {
        mixer_ctl_name = "Voice Rx Device Mute";
    } else if (!strncmp("tx", dir, sizeof("tx"))) {
        mixer_ctl_name = "Voice Tx Device Mute";
    } else {
        return -EINVAL;
    }

    set_values[0] = state;
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }

    ALOGV("%s: Setting device mute state: %d, mixer ctrl:%s",
          __func__,state, mixer_ctl_name);
    mixer_ctl_set_array(ctl, set_values, ARRAY_SIZE(set_values));

    return ret;
}

int platform_split_snd_device(void *platform,
                              snd_device_t snd_device,
                              int *num_devices,
                              snd_device_t *new_snd_devices)
{
    int ret = -EINVAL;
    struct platform_data *my_data = (struct platform_data *)platform;

    if ( NULL == num_devices || NULL == new_snd_devices || NULL == my_data) {
        ALOGE("%s: NULL pointer ..", __func__);
        return -EINVAL;
    }

    /*
     * If wired headset/headphones/line devices share the same backend
     * with speaker/earpiece this routine returns -EINVAL.
     */
    if (snd_device == SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES &&
        !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_HEADPHONES)) {
        *num_devices = 2;
         if (my_data->is_vbat_speaker)
             new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_VBAT;
         else if (my_data->is_wsa_speaker)
             new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_WSA;
         else
             new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_HEADPHONES;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_HEADPHONES_AND_HAPTICS &&
        !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_HEADPHONES)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_AND_HAPTICS;
        new_snd_devices[1] = SND_DEVICE_OUT_HEADPHONES;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_AND_LINE &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_LINE)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_LINE;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_SAFE_AND_HEADPHONES &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER_SAFE, SND_DEVICE_OUT_HEADPHONES)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_SAFE;
        new_snd_devices[1] = SND_DEVICE_OUT_HEADPHONES;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_SAFE_HEADPHONES_AND_HAPTICS &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER_SAFE, SND_DEVICE_OUT_HEADPHONES)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_SAFE_AND_HAPTICS;
        new_snd_devices[1] = SND_DEVICE_OUT_HEADPHONES;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_SAFE_AND_LINE &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER_SAFE, SND_DEVICE_OUT_LINE)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_SAFE;
        new_snd_devices[1] = SND_DEVICE_OUT_LINE;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_ANC_HEADSET)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_HEADPHONES;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_AND_ANC_FB_HEADSET &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_ANC_FB_HEADSET)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_HEADPHONES;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_HEADPHONES &&
               !platform_check_backends_match(SND_DEVICE_OUT_VOICE_SPEAKER, SND_DEVICE_OUT_VOICE_HEADPHONES)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_VOICE_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_VOICE_HEADPHONES;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_ANC_HEADSET &&
               !platform_check_backends_match(SND_DEVICE_OUT_VOICE_SPEAKER, SND_DEVICE_OUT_VOICE_ANC_HEADSET)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_VOICE_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_VOICE_ANC_HEADSET;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_ANC_FB_HEADSET &&
               !platform_check_backends_match(SND_DEVICE_OUT_VOICE_SPEAKER, SND_DEVICE_OUT_VOICE_ANC_FB_HEADSET)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_VOICE_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_VOICE_ANC_FB_HEADSET;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_AND_VOICE_HEADPHONES &&
               !platform_check_backends_match(SND_DEVICE_OUT_VOICE_SPEAKER_STEREO, SND_DEVICE_OUT_VOICE_HEADPHONES)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_VOICE_SPEAKER_STEREO;
        new_snd_devices[1] = SND_DEVICE_OUT_VOICE_HEADPHONES;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_AND_VOICE_ANC_HEADSET &&
               !platform_check_backends_match(SND_DEVICE_OUT_VOICE_SPEAKER_STEREO, SND_DEVICE_OUT_VOICE_ANC_HEADSET)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_VOICE_SPEAKER_STEREO;
        new_snd_devices[1] = SND_DEVICE_OUT_VOICE_ANC_HEADSET;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_AND_VOICE_ANC_FB_HEADSET &&
               !platform_check_backends_match(SND_DEVICE_OUT_VOICE_SPEAKER_STEREO, SND_DEVICE_OUT_VOICE_ANC_FB_HEADSET)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_VOICE_SPEAKER_STEREO;
        new_snd_devices[1] = SND_DEVICE_OUT_VOICE_ANC_FB_HEADSET;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_AND_HDMI &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_HDMI)) {
        *num_devices = 2;

        if (my_data->is_vbat_speaker)
            new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_VBAT;
        else if (my_data->is_wsa_speaker)
            new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_WSA;
        else
            new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_HDMI;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_AND_DISPLAY_PORT &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_DISPLAY_PORT)) {
        *num_devices = 2;
        if (my_data->is_vbat_speaker)
            new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_VBAT;
        else if (my_data->is_wsa_speaker)
            new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_WSA;
        else
            new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_DISPLAY_PORT;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_AND_DISPLAY_PORT1 &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_DISPLAY_PORT1)) {
        *num_devices = 2;
        if (my_data->is_vbat_speaker)
            new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_VBAT;
        else if (my_data->is_wsa_speaker)
            new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_WSA;
        else
            new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_DISPLAY_PORT1;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_USB_HEADSET)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_USB_HEADSET;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_USB_HEADSET_AND_HAPTICS &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_USB_HEADSET)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_AND_HAPTICS;
        new_snd_devices[1] = SND_DEVICE_OUT_USB_HEADSET;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_HIFI_FILTER &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_HEADPHONES_HIFI_FILTER)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_HEADPHONES_HIFI_FILTER;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_AND_BT_SCO &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_BT_SCO)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_SCO;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_BT_SCO_AND_HAPTICS &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_BT_SCO)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_AND_HAPTICS;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_SCO;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER_SAFE,
                                              SND_DEVICE_OUT_BT_SCO)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_SAFE;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_SCO;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_SAFE_BT_SCO_AND_HAPTICS &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER_SAFE,
                                              SND_DEVICE_OUT_BT_SCO)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_SAFE_AND_HAPTICS;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_SCO;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_BT_SCO_WB_AND_HAPTICS &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_BT_SCO_WB)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_AND_HAPTICS;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_SCO_WB;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_AND_BT_SCO_WB &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_BT_SCO_WB)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_SCO_WB;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_BT_SCO_WB_AND_HAPTICS &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_BT_SCO_WB)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_AND_HAPTICS;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_SCO_WB;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO_WB &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER_SAFE,
                                              SND_DEVICE_OUT_BT_SCO_WB)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_SAFE;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_SCO_WB;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_AND_BT_SCO_SWB &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_BT_SCO_SWB)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_SCO_SWB;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_BT_SCO_SWB_AND_HAPTICS &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, SND_DEVICE_OUT_BT_SCO_SWB)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_AND_HAPTICS;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_SCO_SWB;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO_SWB &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER_SAFE,
                                              SND_DEVICE_OUT_BT_SCO_SWB)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_SAFE;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_SCO_SWB;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_SAFE_BT_SCO_SWB_AND_HAPTICS &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER_SAFE,
                                              SND_DEVICE_OUT_BT_SCO_SWB)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_SAFE_AND_HAPTICS;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_SCO_SWB;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_SAFE_AND_USB_HEADSET &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER_SAFE, SND_DEVICE_OUT_USB_HEADSET)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_SAFE;
        new_snd_devices[1] = SND_DEVICE_OUT_USB_HEADSET;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_SAFE_USB_HEADSET_AND_HAPTICS &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER_SAFE, SND_DEVICE_OUT_USB_HEADSET)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_SAFE_AND_HAPTICS;
        new_snd_devices[1] = SND_DEVICE_OUT_USB_HEADSET;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_WSA_AND_BT_SCO &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER_WSA, SND_DEVICE_OUT_BT_SCO)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_WSA;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_SCO;
        ret = 0;
    } else if (snd_device == SND_DEVICE_OUT_SPEAKER_WSA_AND_BT_SCO_WB &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER_WSA, SND_DEVICE_OUT_BT_SCO_WB)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_WSA;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_SCO_WB;
        ret = 0;
    } else if (SND_DEVICE_OUT_SPEAKER_AND_BT_A2DP == snd_device) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_A2DP;
        ret = 0;
    } else if (SND_DEVICE_OUT_SPEAKER_BT_A2DP_AND_HAPTICS == snd_device) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_AND_HAPTICS;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_A2DP;
        ret = 0;
    } else if (SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_A2DP == snd_device &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER_SAFE,
                                              SND_DEVICE_OUT_BT_A2DP)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_SAFE;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_A2DP;
        ret = 0;
    } else if (SND_DEVICE_OUT_SPEAKER_SAFE_BT_A2DP_AND_HAPTICS == snd_device &&
               !platform_check_backends_match(SND_DEVICE_OUT_SPEAKER_SAFE,
                                              SND_DEVICE_OUT_BT_A2DP)) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_OUT_SPEAKER_SAFE_AND_HAPTICS;
        new_snd_devices[1] = SND_DEVICE_OUT_BT_A2DP;
        ret = 0;
    } else if (SND_DEVICE_IN_INCALL_REC_RX_TX == snd_device) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_IN_INCALL_REC_RX;
        new_snd_devices[1] = SND_DEVICE_IN_INCALL_REC_TX;
        ret = 0;
    } else if (SND_DEVICE_IN_HANDSET_DMIC_AND_EC_REF_LOOPBACK == snd_device) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_IN_HANDSET_DMIC;
        new_snd_devices[1] = SND_DEVICE_IN_EC_REF_LOOPBACK;
        ret = 0;
    } else if (SND_DEVICE_IN_HANDSET_QMIC_AND_EC_REF_LOOPBACK == snd_device) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_IN_UNPROCESSED_QUAD_MIC;
        new_snd_devices[1] = SND_DEVICE_IN_EC_REF_LOOPBACK;
        ret = 0;
    } else if (SND_DEVICE_IN_HANDSET_6MIC_AND_EC_REF_LOOPBACK == snd_device) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_IN_HANDSET_6MIC;
        new_snd_devices[1] = SND_DEVICE_IN_EC_REF_LOOPBACK;
        ret = 0;
    } else if (SND_DEVICE_IN_HANDSET_8MIC_AND_EC_REF_LOOPBACK == snd_device) {
        *num_devices = 2;
        new_snd_devices[0] = SND_DEVICE_IN_HANDSET_8MIC;
        new_snd_devices[1] = SND_DEVICE_IN_EC_REF_LOOPBACK;
        ret = 0;
    }


    ALOGD("%s: snd_device(%d) num devices(%d) new_snd_devices(%d)", __func__,
        snd_device, *num_devices, *new_snd_devices);

    return ret;
}

/*
 * returns index for mixer controls
 *
 * example: max controllers = 2, max streams = 4
 * controller = 0, stream = 0 => Index 0
 * ...
 * controller = 0, stream = 3 => Index 3
 * controller = 1, stream = 0 => Index 4
 * ...
 * controller = 1, stream = 3 => Index 7
 */
int platform_get_display_port_ctl_index(int controller, int stream) {

    if (controller < 0 || controller >= MAX_CONTROLLERS ||
            stream < 0 || stream >= MAX_STREAMS_PER_CONTROLLER) {
        ALOGE("%s: Invalid controller/stream - %d/%d",
              __func__, controller, stream);
        return -EINVAL;
    }

    return ((controller % MAX_CONTROLLERS) * MAX_STREAMS_PER_CONTROLLER) +
            (stream % MAX_STREAMS_PER_CONTROLLER);
}

int platform_set_ext_display_device_v2(void *platform, int controller, int stream)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    struct mixer_ctl *ctl = NULL;
    int ctl_index = 0;
    const char *ctl_name_prefix = "External Display";
    const char *ctl_name_suffix = "Audio Device";
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};
    int device_values[2] = {-1, -1};

    if (!audio_extn_is_display_port_enabled()) {
        ALOGE("%s: display port is not supported", __func__);
        return -EINVAL;
    }

    ctl_index = platform_get_display_port_ctl_index(controller, stream);
    if (-EINVAL == ctl_index) {
        ALOGE("%s: Unknown controller/stream %d/%d",
              __func__, controller, stream);
        return -EINVAL;
    }

    if (0 == ctl_index)
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                 "%s %s", ctl_name_prefix, ctl_name_suffix);
    else
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                 "%s%d %s", ctl_name_prefix, ctl_index, ctl_name_suffix);

    device_values[0] = controller;
    device_values[1] = stream;

    ALOGV("%s: mixer ctl name: %s", __func__, mixer_ctl_name);

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }

    ALOGV("%s: controller/stream: %d/%d", __func__, device_values[0],
          device_values[1]);

    return mixer_ctl_set_array(ctl, device_values, ARRAY_SIZE(device_values));
}

int platform_get_ext_disp_type(void *platform) {
    return platform_get_ext_disp_type_v2(platform, 0, 0);
}

int platform_get_ext_disp_type_v2(void *platform, int controller, int stream)
{
    int disp_type = EXT_DISPLAY_TYPE_NONE;
    int ctl_index = 0;
    struct platform_data *my_data = (struct platform_data *)platform;
    struct ext_disp_state *disp = NULL;

    ctl_index = platform_get_display_port_ctl_index(controller, stream);
    if (-EINVAL == ctl_index) {
        ALOGE("%s: Unknown controller/stream %d/%d",
              __func__, controller, stream);
        return -EINVAL;
    }

    disp = &my_data->ext_disp[controller][stream];
    if (disp->type > EXT_DISPLAY_TYPE_NONE) {
         ALOGD("%s: Returning cached ext disp type:%s",
               __func__, (disp->type == EXT_DISPLAY_TYPE_DP) ? "DisplayPort" : "HDMI");
         return disp->type;
    }

    if (audio_extn_is_display_port_enabled()) {
        struct audio_device *adev = my_data->adev;
        struct mixer_ctl *ctl = NULL;
        const char *ctl_name_prefix = "External Display";
        const char *ctl_name_suffix = "Type";
        char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};

        if (0 == ctl_index)
            snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                     "%s %s", ctl_name_prefix, ctl_name_suffix);
        else
            snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                     "%s%d %s", ctl_name_prefix, ctl_index, ctl_name_suffix);

        ALOGV("%s: mixer ctl name: %s", __func__, mixer_ctl_name);

        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s: Could not get ctl for mixer cmd - %s",
                  __func__, mixer_ctl_name);
            return -EINVAL;
        }

        disp_type = mixer_ctl_get_value(ctl, 0);
        if (disp_type <= EXT_DISPLAY_TYPE_NONE) {
             ALOGE("%s: Invalid external display type: %d", __func__, disp_type);
             return -EINVAL;
        }
    } else {
        disp_type = EXT_DISPLAY_TYPE_HDMI;
    }

    disp->type = disp_type;

    ALOGD("%s: ext disp type: %s", __func__,
          (disp_type == EXT_DISPLAY_TYPE_DP) ? "DisplayPort" : "HDMI");

    return disp_type;
}

snd_device_t platform_get_output_snd_device(void *platform, struct stream_out *out,
                                            usecase_type_t uc_type)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    audio_mode_t mode = adev->mode;
    snd_device_t snd_device = SND_DEVICE_NONE;
    struct listnode devices;
    unsigned int sample_rate = out->sample_rate;
    int na_mode = platform_get_native_support();
    struct stream_in *in = adev_get_active_input(adev);

    audio_channel_mask_t channel_mask = (in == NULL) ?
                                AUDIO_CHANNEL_IN_MONO : in->channel_mask;
    int channel_count = popcount(channel_mask);
    int controller = -1;
    int stream = -1;

    list_init(&devices);
    assign_devices(&devices, &out->device_list);

    ALOGV("%s: enter: output devices(%#x)", __func__, get_device_types(&devices));
    if (list_empty(&devices) ||
        is_audio_in_device_type(&devices)) {
        ALOGV("%s: Invalid output devices (%#x)", __func__, get_device_types(&devices));
        goto exit;
    }

    if (compare_device_type(&devices, AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
        controller = out->extconn.cs.controller;
        stream = out->extconn.cs.stream;

        if (controller < 0 || controller >= MAX_CONTROLLERS ||
                stream < 0 || stream >= MAX_STREAMS_PER_CONTROLLER) {
            ALOGE("%s: Invalid controller/stream - %d/%d",
                  __func__, controller, stream);
            goto exit;
        }
    }

    if (mode == AUDIO_MODE_CALL_SCREEN) {
        if (uc_type == VOICE_CALL)
            snd_device = SND_DEVICE_OUT_CALL_PROXY;
        if (compare_device_type(&devices, AUDIO_DEVICE_OUT_TELEPHONY_TX))
            snd_device = SND_DEVICE_OUT_VOICE_TX;
        if (snd_device != SND_DEVICE_NONE)
            goto exit;
    }

    if (list_length(&devices) == 2) {
        bool is_active_voice_call = false;

        /*
        * This is special case handling for combo device use case during
        * voice call. APM route use case to combo device if stream type is
        * enforced audible (e.g. Camera shutter sound).
        */
        if ((mode == AUDIO_MODE_IN_CALL) ||
            voice_check_voicecall_usecases_active(adev) ||
            voice_extn_compress_voip_is_active(adev))
                is_active_voice_call = true;

        if (compare_device_type(&devices, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) &&
            compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER)) {
            if (my_data->external_spk_1)
                snd_device = SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_1;
            else if (my_data->external_spk_2)
                snd_device = SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_2;
            else if (is_active_voice_call)
                snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_HEADPHONES;
            else if  (audio_extn_is_hifi_filter_enabled(adev, out, snd_device,
               my_data->codec_variant, channel_count, 1))
                snd_device = SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_HIFI_FILTER;
            else if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS)
                snd_device = SND_DEVICE_OUT_SPEAKER_HEADPHONES_AND_HAPTICS;
            else
                snd_device = SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES;
        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_LINE) &&
                   compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER)) {
                snd_device = SND_DEVICE_OUT_SPEAKER_AND_LINE;
        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_WIRED_HEADSET) &&
                   compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER)) {
            if (audio_extn_get_anc_enabled()) {
                if (audio_extn_should_use_fb_anc()) {
                    if (is_active_voice_call)
                        snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_ANC_FB_HEADSET;
                    else
                        snd_device = SND_DEVICE_OUT_SPEAKER_AND_ANC_FB_HEADSET;
                } else {
                    if (is_active_voice_call)
                        snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_ANC_HEADSET;
                    else
                        snd_device = SND_DEVICE_OUT_SPEAKER_AND_ANC_HEADSET;
                }
            } else if (my_data->external_spk_1)
                snd_device = SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_1;
            else if (my_data->external_spk_2)
                snd_device = SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_EXTERNAL_2;
            else if  (audio_extn_is_hifi_filter_enabled(adev, out, snd_device,
               my_data->codec_variant, channel_count, 1))
                snd_device = SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES_HIFI_FILTER;
            else {
                if (is_active_voice_call)
                    snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_AND_VOICE_HEADPHONES;
                else if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS)
                    snd_device = SND_DEVICE_OUT_SPEAKER_HEADPHONES_AND_HAPTICS;
                else
                    snd_device = SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES;
            }
        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_LINE) &&
                   compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER)) {
            snd_device = SND_DEVICE_OUT_SPEAKER_AND_LINE;
        } else if ((compare_device_type(&devices, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) &&
                    compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE)) ||
                   (compare_device_type(&devices, AUDIO_DEVICE_OUT_WIRED_HEADSET) &&
                    compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE))) {
            snd_device = SND_DEVICE_OUT_SPEAKER_SAFE_AND_HEADPHONES;
        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_LINE) &&
                   compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE)) {
            snd_device = SND_DEVICE_OUT_SPEAKER_SAFE_AND_LINE;
        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_AUX_DIGITAL) &&
                   compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER)) {
            switch(my_data->ext_disp[controller][stream].type) {
                case EXT_DISPLAY_TYPE_HDMI:
                    snd_device = SND_DEVICE_OUT_SPEAKER_AND_HDMI;
                    break;
                case EXT_DISPLAY_TYPE_DP:
                    snd_device = SND_DEVICE_OUT_SPEAKER_AND_DISPLAY_PORT +
                        ((controller * MAX_STREAMS_PER_CONTROLLER) + stream);
                    break;
                default:
                    ALOGE("%s: Invalid disp_type %d", __func__,
                          my_data->ext_disp[controller][stream].type);
                    goto exit;
            }
        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET) &&
                   compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER)) {
            if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS)
                snd_device = SND_DEVICE_OUT_SPEAKER_USB_HEADSET_AND_HAPTICS;
            else
                snd_device = SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET;
        } else if ((compare_device_type(&devices, AUDIO_DEVICE_OUT_USB_DEVICE) &&
                    compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER)) ||
                   (compare_device_type(&devices, AUDIO_DEVICE_OUT_USB_HEADSET) &&
                    compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER))) {
            if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS)
                snd_device = SND_DEVICE_OUT_SPEAKER_USB_HEADSET_AND_HAPTICS;
            else
                snd_device = SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET;
        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER) &&
                   is_a2dp_out_device_type(&devices)) {
            if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS)
                snd_device = SND_DEVICE_OUT_SPEAKER_BT_A2DP_AND_HAPTICS;
            else
                snd_device = SND_DEVICE_OUT_SPEAKER_AND_BT_A2DP;
        }  else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE) &&
                   is_a2dp_out_device_type(&devices)) {
            if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS)
                snd_device = SND_DEVICE_OUT_SPEAKER_SAFE_BT_A2DP_AND_HAPTICS;
            else
                snd_device = SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_A2DP;
        } else if (is_sco_out_device_type(&devices) &&
                   compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER)) {
            if (my_data->is_wsa_speaker) {
                if (adev->swb_speech_mode != SPEECH_MODE_INVALID)
                    snd_device = SND_DEVICE_OUT_SPEAKER_WSA_AND_BT_SCO_SWB;
                else
                    snd_device = adev->bt_wb_speech_enabled ?
                            SND_DEVICE_OUT_SPEAKER_WSA_AND_BT_SCO_WB :
                            SND_DEVICE_OUT_SPEAKER_WSA_AND_BT_SCO;
            } else {
                if (adev->swb_speech_mode != SPEECH_MODE_INVALID)
                    snd_device = SND_DEVICE_OUT_SPEAKER_AND_BT_SCO_SWB;
                else if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS)
                    snd_device = adev->bt_wb_speech_enabled ?
                            SND_DEVICE_OUT_SPEAKER_BT_SCO_SWB_AND_HAPTICS :
                            SND_DEVICE_OUT_SPEAKER_BT_SCO_AND_HAPTICS;
                else
                    snd_device = adev->bt_wb_speech_enabled ?
                            SND_DEVICE_OUT_SPEAKER_AND_BT_SCO_WB :
                            SND_DEVICE_OUT_SPEAKER_AND_BT_SCO;
            }
        } else if (is_sco_out_device_type(&devices) &&
                         compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE)) {
            if (adev->swb_speech_mode != SPEECH_MODE_INVALID)
                snd_device = SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO_SWB;
            else if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS)
                snd_device = adev->bt_wb_speech_enabled ?
                        SND_DEVICE_OUT_SPEAKER_SAFE_BT_SCO_SWB_AND_HAPTICS :
                        SND_DEVICE_OUT_SPEAKER_SAFE_BT_SCO_AND_HAPTICS;
            else
                snd_device = adev->bt_wb_speech_enabled ?
                        SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO_WB :
                        SND_DEVICE_OUT_SPEAKER_SAFE_AND_BT_SCO;
        } else if ((compare_device_type(&devices, AUDIO_DEVICE_OUT_USB_DEVICE) &&
                    compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE)) ||
                   (compare_device_type(&devices, AUDIO_DEVICE_OUT_USB_HEADSET) &&
                    compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE))) {
            if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS)
                snd_device = SND_DEVICE_OUT_SPEAKER_SAFE_USB_HEADSET_AND_HAPTICS;
            else
                snd_device = SND_DEVICE_OUT_SPEAKER_SAFE_AND_USB_HEADSET;
        } else {
            ALOGE("%s: Invalid combo device(%#x)", __func__, get_device_types(&devices));
            goto exit;
        }
        if (snd_device != SND_DEVICE_NONE) {
            goto exit;
        }
    }

    if (list_length(&devices) != 1) {
        ALOGE("%s: Invalid output devices(%#x)", __func__, get_device_types(&devices));
        goto exit;
    }

    if ((mode == AUDIO_MODE_IN_CALL) ||
        voice_check_voicecall_usecases_active(adev) ||
        voice_extn_compress_voip_is_active(adev) ||
        adev->enable_voicerx ||
        audio_extn_hfp_is_active(adev)) {
        if (compare_device_type(&devices, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) ||
            compare_device_type(&devices, AUDIO_DEVICE_OUT_WIRED_HEADSET) ||
            compare_device_type(&devices, AUDIO_DEVICE_OUT_LINE)) {
            if ((adev->voice.tty_mode != TTY_MODE_OFF) &&
                !voice_extn_compress_voip_is_active(adev)) {
                switch (adev->voice.tty_mode) {
                case TTY_MODE_FULL:
                    if (audio_extn_is_concurrent_capture_enabled() &&
                         compare_device_type(&devices, AUDIO_DEVICE_OUT_WIRED_HEADSET)) {
                        //Separate backend is added for headset-mic as part of concurrent capture
                        snd_device = SND_DEVICE_OUT_VOICE_TTY_FULL_HEADSET;
                    } else {
                        snd_device = SND_DEVICE_OUT_VOICE_TTY_FULL_HEADPHONES;
                    }
                    break;
                case TTY_MODE_VCO:
                    snd_device = SND_DEVICE_OUT_VOICE_TTY_VCO_HEADPHONES;
                    break;
                case TTY_MODE_HCO:
                    snd_device = SND_DEVICE_OUT_VOICE_TTY_HCO_HANDSET;
                    break;
                default:
                    ALOGE("%s: Invalid TTY mode (%#x)",
                          __func__, adev->voice.tty_mode);
                }
            } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_LINE)) {
                snd_device = SND_DEVICE_OUT_VOICE_LINE;
            } else if (audio_extn_get_anc_enabled()) {
                if (audio_extn_should_use_fb_anc())
                    snd_device = SND_DEVICE_OUT_VOICE_ANC_FB_HEADSET;
                else
                    snd_device = SND_DEVICE_OUT_VOICE_ANC_HEADSET;
            } else if (!platform_check_all_backends_match(SND_DEVICE_IN_VOICE_HEADSET_MIC,
                       SND_DEVICE_IN_SPEAKER_MIC) && compare_device_type(&devices,
                       AUDIO_DEVICE_OUT_WIRED_HEADSET)) {
                //Separate backend is added for headset-mic as part of concurrent capture
                snd_device = SND_DEVICE_OUT_VOICE_HEADSET;
            } else {
                snd_device = SND_DEVICE_OUT_VOICE_HEADPHONES;
            }
        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_USB_DEVICE) ||
                   compare_device_type(&devices, AUDIO_DEVICE_OUT_USB_HEADSET)) {
            if (voice_is_in_call(adev)) {
                switch (adev->voice.tty_mode) {
                    case TTY_MODE_FULL:
                        snd_device = SND_DEVICE_OUT_VOICE_TTY_FULL_USB;
                        break;
                    case TTY_MODE_VCO:
                       snd_device = SND_DEVICE_OUT_VOICE_TTY_VCO_USB;
                        break;
                    case TTY_MODE_HCO:
                        // since Hearing will be on handset\speaker, use existing device
                        snd_device = SND_DEVICE_OUT_VOICE_TTY_HCO_HANDSET;
                        break;
                    case TTY_MODE_OFF:
                        break;
                    default:
                        ALOGE("%s: Invalid TTY mode (%#x)",
                              __func__, adev->voice.tty_mode);
                }
            }
            if (snd_device == SND_DEVICE_NONE) {
                    snd_device = audio_extn_usb_is_capture_supported() ?
                                 SND_DEVICE_OUT_VOICE_USB_HEADSET :
                                 SND_DEVICE_OUT_VOICE_USB_HEADPHONES;
            }
        } else if (is_sco_out_device_type(&devices)) {
            if (adev->swb_speech_mode != SPEECH_MODE_INVALID)
                snd_device = SND_DEVICE_OUT_BT_SCO_SWB;
            else if (adev->bt_wb_speech_enabled)
                snd_device = SND_DEVICE_OUT_BT_SCO_WB;
            else
                snd_device = SND_DEVICE_OUT_BT_SCO;
        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER) ||
                   compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE)) {
            if (my_data->is_vbat_speaker || my_data->is_bcl_speaker) {
                if (hw_info_is_stereo_spkr(my_data->hw_info)) {
                    if (my_data->mono_speaker == SPKR_1)
                        snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_VBAT;
                    else
                        snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_2_VBAT;
                } else
                    snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_VBAT;
            } else if (my_data->is_wsa_speaker) {
                    if (my_data->mono_speaker == SPKR_1)
                        snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_WSA;
                    else
                        snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_2_WSA;
            } else {
                if (hw_info_is_stereo_spkr(my_data->hw_info)) {
                    if (my_data->voice_speaker_stereo)
                        snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_STEREO;
                    else if (adev->enable_hfp)
                        snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_HFP;
                    else {
                        if (my_data->mono_speaker == SPKR_1)
                            snd_device = SND_DEVICE_OUT_VOICE_SPEAKER;
                        else
                            snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_2;
                    }
                } else if (adev->enable_hfp)
                    snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_HFP;
                else
                    snd_device = SND_DEVICE_OUT_VOICE_SPEAKER;
            }
        } else if (is_a2dp_out_device_type(&devices)) {
            snd_device = SND_DEVICE_OUT_BT_A2DP;
        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET) ||
                   compare_device_type(&devices, AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET)) {
            snd_device = SND_DEVICE_OUT_USB_HEADSET;
        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_AUX_DIGITAL) &&
                   adev->dp_allowed_for_voice) {
            switch(my_data->ext_disp[controller][stream].type) {
                case EXT_DISPLAY_TYPE_DP:
                    snd_device = SND_DEVICE_OUT_DISPLAY_PORT +
                        ((controller * MAX_STREAMS_PER_CONTROLLER) + stream);
                    break;
                default:
                    ALOGE("%s: Invalid disp_type %d", __func__,
                           my_data->ext_disp[controller][stream].type);
                    goto exit;
            }
        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_EARPIECE)) {
            if(adev->voice.hac)
                snd_device = SND_DEVICE_OUT_VOICE_HAC_HANDSET;
            else if (is_operator_tmus())
                snd_device = SND_DEVICE_OUT_VOICE_HANDSET_TMUS;
            else if (audio_extn_should_use_handset_anc(channel_count))
                snd_device = SND_DEVICE_OUT_ANC_HANDSET;
            else
                snd_device = SND_DEVICE_OUT_VOICE_HANDSET;
        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_TELEPHONY_TX)) {
            snd_device = SND_DEVICE_OUT_VOICE_TX;
        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_HEARING_AID)) {
            snd_device = SND_DEVICE_OUT_VOICE_HEARING_AID;
        }

        if (snd_device != SND_DEVICE_NONE) {
            goto exit;
        }
    }

    if (compare_device_type(&devices, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) ||
        compare_device_type(&devices, AUDIO_DEVICE_OUT_WIRED_HEADSET) ||
        compare_device_type(&devices, AUDIO_DEVICE_OUT_LINE)) {
        if (OUTPUT_SAMPLING_RATE_44100 == sample_rate &&
            NATIVE_AUDIO_MODE_SRC == na_mode &&
            !audio_extn_get_anc_enabled()) {

            snd_device = SND_DEVICE_OUT_HEADPHONES_44_1;

        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_WIRED_HEADSET)
            && audio_extn_get_anc_enabled()) {
                if (audio_extn_should_use_fb_anc())
                    snd_device = SND_DEVICE_OUT_ANC_FB_HEADSET;
                else
                    snd_device = SND_DEVICE_OUT_ANC_HEADSET;
        } else if (NATIVE_AUDIO_MODE_SRC == na_mode &&
                   OUTPUT_SAMPLING_RATE_44100 == sample_rate) {
                snd_device = SND_DEVICE_OUT_HEADPHONES_44_1;
        } else if (NATIVE_AUDIO_MODE_MULTIPLE_MIX_IN_CODEC == na_mode &&
                   (sample_rate % OUTPUT_SAMPLING_RATE_44100 == 0) &&
                   (out->format != AUDIO_FORMAT_DSD)) {
                snd_device = SND_DEVICE_OUT_HEADPHONES_44_1;
        } else if (out->format == AUDIO_FORMAT_DSD) {
                snd_device = SND_DEVICE_OUT_HEADPHONES_DSD;
        } else if (audio_extn_is_hifi_filter_enabled(adev, out, snd_device,
             my_data->codec_variant, channel_count, 1)) {
                snd_device = SND_DEVICE_OUT_HEADPHONES_HIFI_FILTER;
        } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_LINE)) {
                snd_device = SND_DEVICE_OUT_LINE;
        } else {
                if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS)
                        snd_device = SND_DEVICE_OUT_HEADPHONES_AND_HAPTICS;
                else
                        snd_device = SND_DEVICE_OUT_HEADPHONES;
        }
    } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_LINE)) {
        snd_device = SND_DEVICE_OUT_LINE;
    } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE)) {
        snd_device = SND_DEVICE_OUT_SPEAKER_SAFE;
    } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_SPEAKER)) {
        if (my_data->external_spk_1)
            snd_device = SND_DEVICE_OUT_SPEAKER_EXTERNAL_1;
        else if (my_data->external_spk_2)
            snd_device = SND_DEVICE_OUT_SPEAKER_EXTERNAL_2;
        else if (adev->speaker_lr_swap) {
            /*
             * Perform device switch only if acdb tuning is
             * different between SPEAKER & SPEAKER_REVERSE,
             * Or there will be a small pause while performing
             * device switch.
            */
            if (acdb_device_table[SND_DEVICE_OUT_SPEAKER] !=
                acdb_device_table[SND_DEVICE_OUT_SPEAKER_REVERSE])
                snd_device = SND_DEVICE_OUT_SPEAKER_REVERSE;
            else
                snd_device = SND_DEVICE_OUT_SPEAKER;
        } else if (my_data->is_vbat_speaker || my_data->is_bcl_speaker)
            snd_device = SND_DEVICE_OUT_SPEAKER_VBAT;
          else if (my_data->is_wsa_speaker)
            snd_device = SND_DEVICE_OUT_SPEAKER_WSA;
          else {
            if (out->usecase == USECASE_AUDIO_PLAYBACK_WITH_HAPTICS)
                    snd_device = SND_DEVICE_OUT_SPEAKER_AND_HAPTICS;
            else
                    snd_device = SND_DEVICE_OUT_SPEAKER;
          }
    } else if (is_sco_out_device_type(&devices)) {
        if (adev->swb_speech_mode != SPEECH_MODE_INVALID)
                snd_device = SND_DEVICE_OUT_BT_SCO_SWB;
        else if (adev->bt_wb_speech_enabled)
            snd_device = SND_DEVICE_OUT_BT_SCO_WB;
        else
            snd_device = SND_DEVICE_OUT_BT_SCO;
    } else if (is_a2dp_out_device_type(&devices)) {
        snd_device = SND_DEVICE_OUT_BT_A2DP;
    } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
            switch(my_data->ext_disp[controller][stream].type) {
                case EXT_DISPLAY_TYPE_HDMI:
                    snd_device = SND_DEVICE_OUT_HDMI;
                    break;
                case EXT_DISPLAY_TYPE_DP:
                    snd_device = SND_DEVICE_OUT_DISPLAY_PORT +
                        ((controller * MAX_STREAMS_PER_CONTROLLER) + stream);
                    break;
                default:
                    ALOGE("%s: Invalid disp_type %d", __func__,
                          my_data->ext_disp[controller][stream].type);
                    goto exit;
            }
    } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET) ||
               compare_device_type(&devices, AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET)) {
        ALOGD("%s: setting USB hadset channel capability(2) for Proxy", __func__);
        snd_device = SND_DEVICE_OUT_USB_HEADSET;
        audio_extn_set_afe_proxy_channel_mixer(adev, 2, snd_device);
    } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_USB_DEVICE) ||
               compare_device_type(&devices, AUDIO_DEVICE_OUT_USB_HEADSET)) {
        if (audio_extn_qdsp_supported_usb())
            snd_device = SND_DEVICE_OUT_USB_HEADSET_SPEC;
        else if (audio_extn_usb_is_capture_supported())
            snd_device = SND_DEVICE_OUT_USB_HEADSET;
        else
            snd_device = SND_DEVICE_OUT_USB_HEADPHONES;
    } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_EARPIECE)) {
        /*HAC support for voice-ish audio (eg visual voicemail)*/
        if(adev->voice.hac)
            snd_device = SND_DEVICE_OUT_VOICE_HAC_HANDSET;
        else
            snd_device = SND_DEVICE_OUT_HANDSET;
    } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_PROXY)) {
        channel_count = audio_extn_get_afe_proxy_channel_count();
        ALOGD("%s: setting sink capability(%d) for Proxy", __func__, channel_count);
        snd_device = SND_DEVICE_OUT_AFE_PROXY;
        audio_extn_set_afe_proxy_channel_mixer(adev, channel_count, snd_device);
    } else if (compare_device_type(&devices, AUDIO_DEVICE_OUT_BUS)) {
        snd_device = audio_extn_auto_hal_get_output_snd_device(adev, out->usecase);
    } else {
        ALOGE("%s: Unknown device(s) %#x", __func__, get_device_types(&devices));
    }
exit:
    ALOGV("%s: exit: snd_device(%s)", __func__, device_table[snd_device]);
    return snd_device;
}

static snd_device_t get_snd_device_for_voice_comm_ecns_enabled(struct platform_data *my_data,
                                                  struct stream_in *in,
                                                  struct listnode *out_devices __unused,
                                                  struct listnode *in_devices)
{
    struct audio_device *adev = my_data->adev;
    snd_device_t snd_device = SND_DEVICE_NONE;

    if (compare_device_type(in_devices, AUDIO_DEVICE_IN_BACK_MIC)) {
        if (my_data->fluence_in_spkr_mode) {
            if ((my_data->fluence_type & FLUENCE_QUAD_MIC) &&
                (my_data->source_mic_type & SOURCE_QUAD_MIC)) {
                snd_device = SND_DEVICE_IN_SPEAKER_QMIC_AEC_NS;
            } else if ((my_data->fluence_type & FLUENCE_TRI_MIC) &&
                       (my_data->source_mic_type & SOURCE_THREE_MIC)) {
                    snd_device = SND_DEVICE_IN_SPEAKER_TMIC_AEC_NS;
            } else if ((my_data->fluence_type & FLUENCE_DUAL_MIC) &&
                       (my_data->source_mic_type & SOURCE_DUAL_MIC)) {
                if (my_data->fluence_mode == FLUENCE_BROADSIDE)
                    snd_device = SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_BROADSIDE;
                else
                    snd_device = my_data->fluence_sb_enabled ?
                                     SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_SB
                                     : SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS;
            }
            adev->acdb_settings |= DMIC_FLAG;
        } else
            snd_device = my_data->fluence_sb_enabled ?
                             SND_DEVICE_IN_SPEAKER_MIC_SB
                             : (my_data->fluence_nn_enabled ?
                                 SND_DEVICE_IN_SPEAKER_MIC_NN
                                 : SND_DEVICE_IN_SPEAKER_MIC);
    } else if (compare_device_type(in_devices, AUDIO_DEVICE_IN_BUILTIN_MIC)) {
        if ((my_data->fluence_type & FLUENCE_TRI_MIC) &&
            (my_data->source_mic_type & SOURCE_THREE_MIC)) {
            snd_device = SND_DEVICE_IN_HANDSET_TMIC_AEC_NS;
            adev->acdb_settings |= TMIC_FLAG;
        } else if ((my_data->fluence_type & FLUENCE_DUAL_MIC) &&
            (my_data->source_mic_type & SOURCE_DUAL_MIC)) {
            snd_device = my_data->fluence_sb_enabled ?
                             SND_DEVICE_IN_HANDSET_DMIC_AEC_NS_SB
                             : SND_DEVICE_IN_HANDSET_DMIC_AEC_NS;
            adev->acdb_settings |= DMIC_FLAG;
        } else
            snd_device = my_data->fluence_sb_enabled ?
                             SND_DEVICE_IN_HANDSET_MIC_SB
                             : (my_data->fluence_nn_enabled ?
                                 SND_DEVICE_IN_HANDSET_MIC_NN
                                 : SND_DEVICE_IN_HANDSET_MIC);
    } else if (compare_device_type(in_devices, AUDIO_DEVICE_IN_WIRED_HEADSET)) {
        snd_device = SND_DEVICE_IN_HEADSET_MIC;
    }
    in->enable_ec_port = true;

    return snd_device;
}

static snd_device_t get_snd_device_for_voice_comm_ecns_disabled(struct platform_data *my_data,
                                                  struct stream_in *in,
                                                  struct listnode *out_devices,
                                                  struct listnode *in_devices)
{
    struct audio_device *adev = my_data->adev;
    snd_device_t snd_device = SND_DEVICE_NONE;

    if (in != NULL && in->enable_aec && in->enable_ns) {
        if (compare_device_type(in_devices, AUDIO_DEVICE_IN_BACK_MIC)) {
            if (my_data->fluence_in_spkr_mode) {
                if ((my_data->fluence_type & FLUENCE_QUAD_MIC) &&
                    (my_data->source_mic_type & SOURCE_QUAD_MIC)) {
                    snd_device = SND_DEVICE_IN_SPEAKER_QMIC_AEC_NS;
                } else if ((my_data->fluence_type & FLUENCE_TRI_MIC) &&
                           (my_data->source_mic_type & SOURCE_THREE_MIC)) {
                        snd_device = SND_DEVICE_IN_SPEAKER_TMIC_AEC_NS;
                } else if ((my_data->fluence_type & FLUENCE_DUAL_MIC) &&
                           (my_data->source_mic_type & SOURCE_DUAL_MIC) &&
                           my_data->fluence_in_voice_comm) {
                    if (my_data->fluence_mode == FLUENCE_BROADSIDE)
                        snd_device = SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_BROADSIDE;
                    else
                        snd_device = my_data->fluence_sb_enabled ?
                                         SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS_SB
                                         : SND_DEVICE_IN_SPEAKER_DMIC_AEC_NS;
                }
                adev->acdb_settings |= DMIC_FLAG;
            } else
                snd_device = my_data->fluence_sb_enabled ?
                                 SND_DEVICE_IN_SPEAKER_MIC_AEC_NS_SB
                                 : (my_data->fluence_nn_enabled?
                                      SND_DEVICE_IN_SPEAKER_MIC_AEC_NS_NN
                                      : SND_DEVICE_IN_SPEAKER_MIC_AEC_NS);
        } else if (compare_device_type(in_devices, AUDIO_DEVICE_IN_BUILTIN_MIC)) {
            if ((my_data->fluence_type & FLUENCE_TRI_MIC) &&
                (my_data->source_mic_type & SOURCE_THREE_MIC)) {
                snd_device = SND_DEVICE_IN_HANDSET_TMIC_AEC_NS;
                adev->acdb_settings |= TMIC_FLAG;
            } else if ((my_data->fluence_type & FLUENCE_DUAL_MIC) &&
                (my_data->source_mic_type & SOURCE_DUAL_MIC) &&
                my_data->fluence_in_voice_comm) {
                snd_device = my_data->fluence_sb_enabled ?
                                 SND_DEVICE_IN_HANDSET_DMIC_AEC_NS_SB
                                 : SND_DEVICE_IN_HANDSET_DMIC_AEC_NS;
                adev->acdb_settings |= DMIC_FLAG;
            } else
                snd_device = my_data->fluence_sb_enabled ?
                                 SND_DEVICE_IN_HANDSET_MIC_AEC_NS_SB
                                 : (my_data->fluence_nn_enabled ?
                                       SND_DEVICE_IN_HANDSET_MIC_AEC_NS_NN
                                       : SND_DEVICE_IN_HANDSET_MIC_AEC_NS);
        } else if (compare_device_type(in_devices, AUDIO_DEVICE_IN_WIRED_HEADSET)) {
            snd_device = SND_DEVICE_IN_HEADSET_MIC_FLUENCE;
        } else if (audio_extn_usb_connected(NULL) &&
                   is_usb_in_device_type(in_devices)) {
            snd_device = SND_DEVICE_IN_USB_HEADSET_MIC_AEC;
        }
    } else if (in != NULL && in->enable_aec) {
        if (compare_device_type(in_devices, AUDIO_DEVICE_IN_BACK_MIC)) {
            if (my_data->fluence_in_spkr_mode) {
                if ((my_data->fluence_type & FLUENCE_QUAD_MIC) &&
                    (my_data->source_mic_type & SOURCE_QUAD_MIC)) {
                    snd_device = SND_DEVICE_IN_SPEAKER_QMIC_AEC;
                } else if ((my_data->fluence_type & FLUENCE_TRI_MIC) &&
                           (my_data->source_mic_type & SOURCE_THREE_MIC)) {
                        snd_device = SND_DEVICE_IN_SPEAKER_TMIC_AEC;
                } else if ((my_data->fluence_type & FLUENCE_DUAL_MIC) &&
                           (my_data->source_mic_type & SOURCE_DUAL_MIC) &&
                           my_data->fluence_in_voice_comm) {
                    if (my_data->fluence_mode == FLUENCE_BROADSIDE)
                        snd_device = SND_DEVICE_IN_SPEAKER_DMIC_AEC_BROADSIDE;
                    else
                        snd_device = my_data->fluence_sb_enabled ?
                                         SND_DEVICE_IN_SPEAKER_DMIC_AEC_SB
                                         : SND_DEVICE_IN_SPEAKER_DMIC_AEC;
                }
                adev->acdb_settings |= DMIC_FLAG;
            } else
                snd_device = my_data->fluence_sb_enabled ?
                                 SND_DEVICE_IN_SPEAKER_MIC_AEC_SB
                                 : (my_data->fluence_nn_enabled ?
                                       SND_DEVICE_IN_SPEAKER_MIC_AEC_NN
                                       : SND_DEVICE_IN_SPEAKER_MIC_AEC);
        } else if (compare_device_type(in_devices, AUDIO_DEVICE_IN_BUILTIN_MIC)) {
            if ((my_data->fluence_type & FLUENCE_TRI_MIC) &&
                (my_data->source_mic_type & SOURCE_THREE_MIC)) {
                snd_device = SND_DEVICE_IN_HANDSET_TMIC_AEC;
                adev->acdb_settings |= TMIC_FLAG;
            } else if ((my_data->fluence_type & FLUENCE_DUAL_MIC) &&
                (my_data->source_mic_type & SOURCE_DUAL_MIC) &&
                my_data->fluence_in_voice_comm) {
                snd_device = my_data->fluence_sb_enabled ?
                                 SND_DEVICE_IN_HANDSET_DMIC_AEC_SB
                                 : SND_DEVICE_IN_HANDSET_DMIC_AEC;
                adev->acdb_settings |= DMIC_FLAG;
            } else
                snd_device = SND_DEVICE_IN_HANDSET_MIC_AEC;
        } else if (compare_device_type(in_devices, AUDIO_DEVICE_IN_WIRED_HEADSET)) {
            snd_device = SND_DEVICE_IN_HEADSET_MIC_FLUENCE;
        } else if (audio_extn_usb_connected(NULL) && is_usb_in_device_type(in_devices)) {
            snd_device = SND_DEVICE_IN_USB_HEADSET_MIC_AEC;
        }
    } else if (in != NULL && in->enable_ns) {
        if (compare_device_type(in_devices, AUDIO_DEVICE_IN_BACK_MIC)) {
            if (my_data->fluence_in_spkr_mode) {
                if ((my_data->fluence_type & FLUENCE_QUAD_MIC) &&
                    (my_data->source_mic_type & SOURCE_QUAD_MIC)) {
                    snd_device = SND_DEVICE_IN_SPEAKER_QMIC_NS;
                } else if ((my_data->fluence_type & FLUENCE_TRI_MIC) &&
                           (my_data->source_mic_type & SOURCE_THREE_MIC)) {
                        snd_device = SND_DEVICE_IN_SPEAKER_TMIC_NS;
                } else if ((my_data->fluence_type & FLUENCE_DUAL_MIC) &&
                           (my_data->source_mic_type & SOURCE_DUAL_MIC) &&
                           my_data->fluence_in_voice_comm) {
                    if (my_data->fluence_mode == FLUENCE_BROADSIDE)
                        snd_device = SND_DEVICE_IN_SPEAKER_DMIC_NS_BROADSIDE;
                    else
                        snd_device = my_data->fluence_sb_enabled ?
                                         SND_DEVICE_IN_SPEAKER_DMIC_NS_SB
                                         : SND_DEVICE_IN_SPEAKER_DMIC_NS;
                }
                adev->acdb_settings |= DMIC_FLAG;
            } else
                snd_device = my_data->fluence_sb_enabled ?
                                 SND_DEVICE_IN_SPEAKER_MIC_NS_SB
                                 : (my_data->fluence_nn_enabled ?
                                       SND_DEVICE_IN_SPEAKER_MIC_NS_NN
                                       : SND_DEVICE_IN_SPEAKER_MIC_NS);
        } else if (compare_device_type(in_devices, AUDIO_DEVICE_IN_BUILTIN_MIC)) {
            if ((my_data->fluence_type & FLUENCE_TRI_MIC) &&
                (my_data->source_mic_type & SOURCE_THREE_MIC)) {
                snd_device = SND_DEVICE_IN_HANDSET_TMIC_NS;
                adev->acdb_settings |= TMIC_FLAG;
            } else if ((my_data->fluence_type & FLUENCE_DUAL_MIC) &&
                (my_data->source_mic_type & SOURCE_DUAL_MIC) &&
                my_data->fluence_in_voice_comm) {
                snd_device = my_data->fluence_sb_enabled ?
                                 SND_DEVICE_IN_HANDSET_DMIC_NS_SB
                                 : SND_DEVICE_IN_HANDSET_DMIC_NS;
                adev->acdb_settings |= DMIC_FLAG;
            } else
                snd_device = SND_DEVICE_IN_HANDSET_MIC_NS;
        } else if (compare_device_type(in_devices, AUDIO_DEVICE_IN_WIRED_HEADSET)) {
            snd_device = SND_DEVICE_IN_HEADSET_MIC_FLUENCE;
        }
        platform_set_echo_reference(adev, false, out_devices);
    } else
        platform_set_echo_reference(adev, false, out_devices);

    return snd_device;
}

static snd_device_t get_snd_device_for_voice_comm(struct platform_data *my_data,
                                                  struct stream_in *in,
                                                  struct listnode *out_devices,
                                                  struct listnode *in_devices)
{
    if(voice_extn_is_dynamic_ecns_enabled())
        return get_snd_device_for_voice_comm_ecns_enabled(my_data, in, out_devices, in_devices);
    else
        return get_snd_device_for_voice_comm_ecns_disabled(my_data, in, out_devices, in_devices);
}

snd_device_t platform_get_input_snd_device(void *platform,
                                           struct stream_in *in,
                                           struct listnode *out_devices,
                                           usecase_type_t uc_type)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    audio_mode_t mode = adev->mode;
    snd_device_t snd_device = SND_DEVICE_NONE;
    struct listnode in_devices;
    char *address = "";

    if (in == NULL)
        in = adev_get_active_input(adev);

    audio_source_t source = (in == NULL) ? AUDIO_SOURCE_DEFAULT : in->source;
    list_init(&in_devices);
    if (in != NULL)
        assign_devices(&in_devices, &in->device_list);
    audio_channel_mask_t channel_mask = (in == NULL) ? AUDIO_CHANNEL_IN_MONO : in->channel_mask;
    int channel_count = audio_channel_count_from_in_mask(channel_mask);
    int str_bitwidth = (in == NULL) ? CODEC_BACKEND_DEFAULT_BIT_WIDTH : in->bit_width;
    int sample_rate = (in == NULL) ? 8000 : in->sample_rate;
    struct audio_usecase *usecase = NULL;
    audio_usecase_t uc_id = (in == NULL) ? USECASE_AUDIO_RECORD : in->usecase;

    ALOGV("%s: enter: out_device(%#x) in_device(%#x) channel_count (%d) channel_mask (0x%x)",
          __func__, get_device_types(out_devices), get_device_types(&in_devices),
          channel_count, channel_mask);
    if (my_data->external_mic) {
        if (!list_empty(out_devices) && ((mode == AUDIO_MODE_IN_CALL) ||
            voice_check_voicecall_usecases_active(adev) ||
            voice_extn_compress_voip_is_active(adev) ||
            audio_extn_hfp_is_active(adev))) {
            if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) ||
               compare_device_type(out_devices, AUDIO_DEVICE_OUT_EARPIECE) ||
               compare_device_type(out_devices, AUDIO_DEVICE_OUT_SPEAKER))
                snd_device = SND_DEVICE_IN_HANDSET_MIC_EXTERNAL;
        } else if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_BUILTIN_MIC) ||
                   compare_device_type(&in_devices, AUDIO_DEVICE_IN_BACK_MIC)) {
            snd_device = SND_DEVICE_IN_HANDSET_MIC_EXTERNAL;
        }
    }

    if (mode == AUDIO_MODE_CALL_SCREEN) {
        if (uc_type == VOICE_CALL)
            snd_device = SND_DEVICE_IN_CALL_PROXY;
        if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_TELEPHONY_TX))
            snd_device = SND_DEVICE_IN_VOICE_RX;
    }

    if (snd_device != AUDIO_DEVICE_NONE)
        goto exit;

    if (!list_empty(out_devices) && ((mode == AUDIO_MODE_IN_CALL) ||
        voice_check_voicecall_usecases_active(adev) ||
        voice_extn_compress_voip_is_active(adev) ||
        audio_extn_hfp_is_active(adev))) {
        if ((adev->voice.tty_mode != TTY_MODE_OFF) &&
            !voice_extn_compress_voip_is_active(adev)) {
            if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) ||
                compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADSET) ||
                compare_device_type(out_devices, AUDIO_DEVICE_OUT_LINE)) {
                switch (adev->voice.tty_mode) {
                    case TTY_MODE_FULL:
                        snd_device = SND_DEVICE_IN_VOICE_TTY_FULL_HEADSET_MIC;
                        break;
                    case TTY_MODE_VCO:
                        snd_device = SND_DEVICE_IN_VOICE_TTY_VCO_HANDSET_MIC;
                        break;
                    case TTY_MODE_HCO:
                        snd_device = SND_DEVICE_IN_VOICE_TTY_HCO_HEADSET_MIC;
                        break;
                    default:
                        ALOGE("%s: Invalid TTY mode (%#x)", __func__, adev->voice.tty_mode);
                }
                goto exit;
            } else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_USB_DEVICE) ||
                       compare_device_type(out_devices, AUDIO_DEVICE_OUT_USB_HEADSET)) {
                switch (adev->voice.tty_mode) {
                    case TTY_MODE_FULL:
                        snd_device = SND_DEVICE_IN_VOICE_TTY_FULL_USB_MIC;
                        break;
                    case TTY_MODE_VCO:
                        // since voice will be captured from handset mic, use existing device
                        snd_device = SND_DEVICE_IN_VOICE_TTY_VCO_HANDSET_MIC;
                        break;
                    case TTY_MODE_HCO:
                        snd_device = SND_DEVICE_IN_VOICE_TTY_HCO_USB_MIC;
                        break;
                    default:
                        ALOGE("%s: Invalid TTY mode (%#x)", __func__, adev->voice.tty_mode);
                }
                goto exit;
            }
        }
        if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_EARPIECE)) {
            if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_EARPIECE) &&
                audio_extn_should_use_handset_anc(channel_count)) {
                if ((my_data->fluence_type != FLUENCE_NONE) &&
                    (my_data->source_mic_type & SOURCE_DUAL_MIC)) {
                    snd_device = SND_DEVICE_IN_VOICE_FLUENCE_DMIC_AANC;
                    adev->acdb_settings |= DMIC_FLAG;
                } else {
                    snd_device = SND_DEVICE_IN_AANC_HANDSET_MIC;
                }
                adev->acdb_settings |= ANC_FLAG;
            } else if (my_data->fluence_type == FLUENCE_NONE ||
                (my_data->fluence_in_voice_call == false &&
                 my_data->fluence_in_hfp_call == false)) {
                 snd_device = my_data->fluence_sb_enabled ?
                                 SND_DEVICE_IN_HANDSET_MIC_SB
                                 : (my_data->fluence_nn_enabled ?
                                        SND_DEVICE_IN_HANDSET_MIC_NN
                                        : SND_DEVICE_IN_HANDSET_MIC);
                 if (audio_extn_hfp_is_active(adev))
                     platform_set_echo_reference(adev, true, out_devices);
            } else {
                if ((my_data->fluence_type & FLUENCE_TRI_MIC) &&
                    (my_data->source_mic_type & SOURCE_THREE_MIC)) {
                    snd_device = SND_DEVICE_IN_HANDSET_TMIC;
                    adev->acdb_settings |= TMIC_FLAG;
                } else if (is_operator_tmus())
                    snd_device = SND_DEVICE_IN_VOICE_DMIC_TMUS;
                else { /* for FLUENCE_DUAL_MIC and SOURCE_DUAL_MIC */
                    snd_device = my_data->fluence_sb_enabled ?
                                     SND_DEVICE_IN_VOICE_DMIC_SB
                                     : SND_DEVICE_IN_VOICE_DMIC;
                    adev->acdb_settings |= DMIC_FLAG;
                }
            }
        } else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADSET)) {
            snd_device = SND_DEVICE_IN_VOICE_HEADSET_MIC;
            if (audio_extn_hfp_is_active(adev))
                platform_set_echo_reference(adev, true, out_devices);
        } else if (is_sco_out_device_type(out_devices)) {
            if (adev->swb_speech_mode != SPEECH_MODE_INVALID) {
                if (adev->bluetooth_nrec)
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC_SWB_NREC;
                else
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC_SWB;
            } else if (adev->bt_wb_speech_enabled) {
                if (adev->bluetooth_nrec)
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC_WB_NREC;
                else
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC_WB;
            } else {
                if (adev->bluetooth_nrec)
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC_NREC;
                else
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC;
            }
        } else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_AUX_DIGITAL) &&
                   adev->dp_allowed_for_voice) {
            if (audio_extn_usb_is_capture_supported())
                snd_device = SND_DEVICE_IN_VOICE_USB_HEADSET_MIC;
            else
                snd_device = my_data->fluence_sb_enabled ?
                                SND_DEVICE_IN_HANDSET_MIC_SB
                                 : SND_DEVICE_IN_HANDSET_MIC;

            if (voice_is_in_call(adev))
                platform_set_echo_reference(adev, true, out_devices);
        } else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_SPEAKER) ||
                   compare_device_type(out_devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE) ||
                   compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) ||
                   compare_device_type(out_devices, AUDIO_DEVICE_OUT_LINE)) {
            if (my_data->fluence_type != FLUENCE_NONE &&
                (my_data->fluence_in_voice_call ||
                 my_data->fluence_in_hfp_call) &&
                my_data->fluence_in_spkr_mode) {
                if((my_data->fluence_type & FLUENCE_QUAD_MIC) &&
                   (my_data->source_mic_type & SOURCE_QUAD_MIC)) {
                    adev->acdb_settings |= QMIC_FLAG;
                    snd_device = SND_DEVICE_IN_VOICE_SPEAKER_QMIC;
                } else if ((my_data->fluence_type & FLUENCE_TRI_MIC) &&
                           (my_data->source_mic_type & SOURCE_THREE_MIC)) {
                    adev->acdb_settings |= TMIC_FLAG;
                    snd_device = SND_DEVICE_IN_VOICE_SPEAKER_TMIC;
                } else {
                    adev->acdb_settings |= DMIC_FLAG;
                    if (my_data->fluence_mode == FLUENCE_BROADSIDE)
                       snd_device = SND_DEVICE_IN_VOICE_SPEAKER_DMIC_BROADSIDE;
                    else
                       snd_device = my_data->fluence_sb_enabled ?
                                        SND_DEVICE_IN_VOICE_SPEAKER_DMIC_SB
                                        : SND_DEVICE_IN_VOICE_SPEAKER_DMIC;
                }
                if (audio_extn_hfp_is_active(adev))
                    platform_set_echo_reference(adev, true, out_devices);
            } else {
                if (adev->enable_hfp) {
                    snd_device = SND_DEVICE_IN_VOICE_SPEAKER_MIC_HFP;
                    platform_set_echo_reference(adev, true, out_devices);
                } else {
                    snd_device = my_data->fluence_sb_enabled ?
                                     SND_DEVICE_IN_VOICE_SPEAKER_MIC_SB
                                     : (my_data->fluence_nn_enabled ?
                                         SND_DEVICE_IN_VOICE_SPEAKER_MIC_NN
                                         : SND_DEVICE_IN_VOICE_SPEAKER_MIC);
                    if (audio_extn_hfp_is_active(adev))
                        platform_set_echo_reference(adev, true, out_devices);
                }
            }
        } else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_TELEPHONY_TX)) {
            snd_device = SND_DEVICE_IN_VOICE_RX;
        } else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_USB_DEVICE) ||
                   compare_device_type(out_devices, AUDIO_DEVICE_OUT_USB_HEADSET)) {
            if (audio_extn_usb_is_capture_supported()) {
                snd_device = SND_DEVICE_IN_VOICE_USB_HEADSET_MIC;
            } else if (my_data->fluence_in_voice_call && my_data->fluence_in_spkr_mode) {
                if (my_data->source_mic_type & SOURCE_DUAL_MIC) {
                    snd_device = SND_DEVICE_IN_VOICE_SPEAKER_DMIC;
                } else {
                    snd_device = SND_DEVICE_IN_VOICE_SPEAKER_MIC;
                }
            } else {
                snd_device = SND_DEVICE_IN_HANDSET_MIC;
            }
        } else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_HEARING_AID)) {
            snd_device = SND_DEVICE_IN_VOICE_HEARING_AID;
        }
    } else if (my_data->use_generic_handset == true &&  //     system prop is enabled
               (my_data->source_mic_type & SOURCE_QUAD_MIC) &&  // AND 4mic is available
               (compare_device_type(&in_devices, AUDIO_DEVICE_IN_BUILTIN_MIC) ||    // AND device is buit-in mic or back mic
                compare_device_type(&in_devices, AUDIO_DEVICE_IN_BACK_MIC)) &&
               (my_data->fluence_in_audio_rec == true &&       //  AND fluencepro is enabled
                my_data->fluence_type & FLUENCE_QUAD_MIC) &&
               (source == AUDIO_SOURCE_CAMCORDER ||           // AND source is cam/mic/unprocessed
                source == AUDIO_SOURCE_UNPROCESSED ||
                source == AUDIO_SOURCE_MIC)) {
                snd_device = SND_DEVICE_IN_HANDSET_GENERIC_QMIC;
                platform_set_echo_reference(adev, true, out_devices);
    } else if (my_data->use_generic_handset == true &&          // System prop is enabled
               (my_data->ambisonic_capture == true) &&          // Enable Ambisonic capture
               (my_data->source_mic_type & SOURCE_QUAD_MIC) &&  // AND 4mic is available
               (compare_device_type(&in_devices, AUDIO_DEVICE_IN_BUILTIN_MIC) ||    // AND device is Built-in
                compare_device_type(&in_devices, AUDIO_DEVICE_IN_BACK_MIC)) &&       // OR Back-mic
               (source == AUDIO_SOURCE_MIC ||                   // AND source is MIC for 16bit
                source == AUDIO_SOURCE_UNPROCESSED ||           // OR unprocessed for 24bit
                source == AUDIO_SOURCE_CAMCORDER) &&            // OR camera usecase
                ((int)channel_mask == (int)AUDIO_CHANNEL_INDEX_MASK_4) && // AND 4mic channel mask
                (sample_rate == 48000)) {             // AND sample rate is 48Khz
                snd_device = SND_DEVICE_IN_HANDSET_GENERIC_QMIC;
                /* Below check is true only in LA build to set
                   ambisonic profile. In LE hal client will set profile
                 */
                if (my_data->ambisonic_profile == true &&
                    in != NULL)
                    strlcpy(in->profile, "record_ambisonic",
                            sizeof(in->profile));

                if (in != NULL && !strncmp(in->profile, "record_ambisonic",
                                           strlen("record_ambisonic"))) {
                    /* Validate input stream configuration for
                       Ambisonic capture.
                     */
                    if (((int)channel_mask != (int)AUDIO_CHANNEL_INDEX_MASK_4) ||
                         (sample_rate != 48000)) {
                          snd_device = SND_DEVICE_NONE;
                          ALOGW("Unsupported Input configuration for ambisonic capture");
                          goto exit;
                    }
                }
    } else if (source == AUDIO_SOURCE_CAMCORDER) {
        if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_BUILTIN_MIC) ||
            compare_device_type(&in_devices, AUDIO_DEVICE_IN_BACK_MIC)) {
            switch (adev->camera_orientation) {
            case CAMERA_BACK_LANDSCAPE:
                snd_device = SND_DEVICE_IN_CAMCORDER_LANDSCAPE;
                break;
            case CAMERA_BACK_INVERT_LANDSCAPE:
                snd_device = SND_DEVICE_IN_CAMCORDER_INVERT_LANDSCAPE;
                break;
            case CAMERA_BACK_PORTRAIT:
                snd_device = SND_DEVICE_IN_CAMCORDER_PORTRAIT;
                break;
            case CAMERA_FRONT_LANDSCAPE:
                snd_device = SND_DEVICE_IN_CAMCORDER_SELFIE_LANDSCAPE;
                break;
            case CAMERA_FRONT_INVERT_LANDSCAPE:
                snd_device = SND_DEVICE_IN_CAMCORDER_SELFIE_INVERT_LANDSCAPE;
                break;
            case CAMERA_FRONT_PORTRAIT:
                snd_device = SND_DEVICE_IN_CAMCORDER_SELFIE_PORTRAIT;
                break;
            default:
                ALOGW("%s: invalid camera orientation %08x", __func__, adev->camera_orientation);
                snd_device = SND_DEVICE_IN_CAMCORDER_LANDSCAPE;
                break;
            }
            if (str_bitwidth == 16) {
                if ((my_data->fluence_type & FLUENCE_DUAL_MIC) &&
                    (my_data->source_mic_type & SOURCE_DUAL_MIC) &&
                    (channel_count == 2))
                    snd_device = SND_DEVICE_IN_HANDSET_DMIC_STEREO;
                else
                    snd_device = SND_DEVICE_IN_CAMCORDER_MIC;
            }
            /*
             * for other bit widths
             */
            else {
                if (((channel_mask == AUDIO_CHANNEL_IN_FRONT_BACK) ||
                    (channel_mask == AUDIO_CHANNEL_IN_STEREO)) &&
                    (my_data->source_mic_type & SOURCE_DUAL_MIC)) {
                    snd_device = SND_DEVICE_IN_UNPROCESSED_STEREO_MIC;
                }
                else if (((int)channel_mask == (int)AUDIO_CHANNEL_INDEX_MASK_3) &&
                         (my_data->source_mic_type & SOURCE_THREE_MIC)) {
                         snd_device = SND_DEVICE_IN_UNPROCESSED_THREE_MIC;
               } else if (((int)channel_mask == (int)AUDIO_CHANNEL_INDEX_MASK_4) &&
                          (my_data->source_mic_type & SOURCE_QUAD_MIC)) {
                          snd_device = SND_DEVICE_IN_UNPROCESSED_QUAD_MIC;
               } else {
                          snd_device = SND_DEVICE_IN_UNPROCESSED_MIC;
               }
           }
       }
    }  else if (source == AUDIO_SOURCE_VOICE_RECOGNITION) {
        if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_BUILTIN_MIC)) {
            if (my_data->fluence_in_voice_rec && channel_count == 1) {
                if ((my_data->fluence_type & FLUENCE_QUAD_MIC) &&
                    (my_data->source_mic_type & SOURCE_QUAD_MIC)) {
                    if (in != NULL && in->enable_aec)
                        snd_device = SND_DEVICE_IN_HANDSET_QMIC_AEC;
                    else
                        snd_device = SND_DEVICE_IN_VOICE_REC_QMIC_FLUENCE;
                } else if ((my_data->fluence_type & FLUENCE_QUAD_MIC) &&
                    (my_data->source_mic_type & SOURCE_THREE_MIC)) {
                    if (in != NULL && in->enable_aec)
                        snd_device = SND_DEVICE_IN_HANDSET_TMIC_AEC;
                    else
                        snd_device = SND_DEVICE_IN_VOICE_REC_TMIC;
                } else if ((my_data->fluence_type & FLUENCE_DUAL_MIC) &&
                    (my_data->source_mic_type & SOURCE_DUAL_MIC)) {
                    if (in != NULL && in->enable_aec)
                        snd_device = SND_DEVICE_IN_HANDSET_DMIC_AEC;
                    else
                        snd_device = SND_DEVICE_IN_VOICE_REC_DMIC_FLUENCE;
                }
                in->enable_ec_port = true;
            } else if (((channel_mask == AUDIO_CHANNEL_IN_FRONT_BACK) ||
                       (channel_mask == AUDIO_CHANNEL_IN_STEREO)) &&
                       (my_data->source_mic_type & SOURCE_DUAL_MIC)) {
                snd_device = SND_DEVICE_IN_VOICE_REC_DMIC_STEREO;
            } else if (((int)channel_mask == (int)AUDIO_CHANNEL_INDEX_MASK_3) &&
                       (my_data->source_mic_type & SOURCE_THREE_MIC)) {
                snd_device = SND_DEVICE_IN_THREE_MIC;
            } else if (((int)channel_mask == (int)AUDIO_CHANNEL_INDEX_MASK_4) &&
                       (my_data->source_mic_type & SOURCE_QUAD_MIC)) {
                snd_device = SND_DEVICE_IN_QUAD_MIC;
            }
            if (snd_device == SND_DEVICE_NONE) {
                if (in != NULL && in->enable_aec) {
                    if (in->enable_ns) {
                        snd_device = SND_DEVICE_IN_VOICE_REC_MIC_AEC_NS;
                    } else {
                        snd_device = SND_DEVICE_IN_VOICE_REC_MIC_AEC;
                    }
                } else if (in != NULL && in->enable_ns)
                    snd_device = SND_DEVICE_IN_VOICE_REC_MIC_NS;
                else
                    snd_device = SND_DEVICE_IN_VOICE_REC_MIC;
            }
        } else if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_WIRED_HEADSET)) {
            snd_device = SND_DEVICE_IN_VOICE_REC_HEADSET_MIC;
        } else if (is_usb_in_device_type(&in_devices)) {
            snd_device = fixup_usb_headset_mic_snd_device(platform,
                                      SND_DEVICE_IN_VOICE_RECOG_USB_HEADSET_MIC,
                                      SND_DEVICE_IN_VOICE_RECOG_USB_HEADSET_MULTI_CHANNEL_MIC);
        }
    } else if (source == AUDIO_SOURCE_UNPROCESSED) {
         if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_BUILTIN_MIC)) {
             if (((channel_mask == AUDIO_CHANNEL_IN_FRONT_BACK) ||
                 (channel_mask == AUDIO_CHANNEL_IN_STEREO)) &&
                 (my_data->source_mic_type & SOURCE_DUAL_MIC)) {
                 snd_device = SND_DEVICE_IN_UNPROCESSED_STEREO_MIC;
             } else if (((int)channel_mask == (int)AUDIO_CHANNEL_INDEX_MASK_3) &&
                 (my_data->source_mic_type & SOURCE_THREE_MIC)) {
                 snd_device = SND_DEVICE_IN_UNPROCESSED_THREE_MIC;
             } else if (((int)channel_mask == (int)AUDIO_CHANNEL_INDEX_MASK_4) &&
                 (my_data->source_mic_type & SOURCE_QUAD_MIC)) {
                 snd_device = SND_DEVICE_IN_UNPROCESSED_QUAD_MIC;
             } else {
                 snd_device = SND_DEVICE_IN_UNPROCESSED_MIC;
             }
         } else if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_WIRED_HEADSET)) {
                 snd_device = SND_DEVICE_IN_UNPROCESSED_HEADSET_MIC;
         } else if (is_usb_in_device_type(&in_devices)) {
             snd_device = fixup_usb_headset_mic_snd_device(platform,
                                          SND_DEVICE_IN_UNPROCESSED_USB_HEADSET_MIC,
                                          SND_DEVICE_IN_UNPROCESSED_USB_HEADSET_MULTI_CHANNEL_MIC);
         }
    } else if ((source == AUDIO_SOURCE_VOICE_COMMUNICATION) ||
              (mode == AUDIO_MODE_IN_COMMUNICATION)) {
        if ((compare_device_type(out_devices, AUDIO_DEVICE_OUT_SPEAKER) ||
             compare_device_type(out_devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE)) ||
            compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) ||
            ((compare_device_type(out_devices, AUDIO_DEVICE_OUT_USB_DEVICE) ||
              compare_device_type(out_devices, AUDIO_DEVICE_OUT_USB_HEADSET)) &&
            !audio_extn_usb_is_capture_supported()))
            reassign_device_list(&in_devices, AUDIO_DEVICE_IN_BACK_MIC, address);
        else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_EARPIECE))
            reassign_device_list(&in_devices, AUDIO_DEVICE_IN_BUILTIN_MIC, address);
        else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADSET))
            reassign_device_list(&in_devices, AUDIO_DEVICE_IN_WIRED_HEADSET, address);
        else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_USB_DEVICE))
            reassign_device_list(&in_devices, AUDIO_DEVICE_IN_USB_DEVICE, address);

        if (list_empty(out_devices))
            reassign_device_list(&in_devices, AUDIO_DEVICE_IN_BUILTIN_MIC,
                                 address);

        if (in)
            snd_device = get_snd_device_for_voice_comm(my_data, in, out_devices, &in_devices);
    } else if (source == AUDIO_SOURCE_MIC) {
        if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_BUILTIN_MIC) &&
                channel_count == 1 ) {
            if(my_data->fluence_in_audio_rec) {
               if ((my_data->fluence_type & FLUENCE_QUAD_MIC) &&
                    (my_data->source_mic_type & SOURCE_QUAD_MIC)) {
                    snd_device = SND_DEVICE_IN_HANDSET_QMIC;
                    platform_set_echo_reference(adev, true, out_devices);
                } else if ((my_data->fluence_type & FLUENCE_QUAD_MIC) &&
                    (my_data->source_mic_type & SOURCE_THREE_MIC)) {
                    snd_device = SND_DEVICE_IN_HANDSET_TMIC_FLUENCE_PRO;
                } else if ((my_data->fluence_type & FLUENCE_TRI_MIC) &&
                           (my_data->source_mic_type & SOURCE_THREE_MIC)) {
                    snd_device = SND_DEVICE_IN_HANDSET_TMIC;
                } else if ((my_data->fluence_type & FLUENCE_DUAL_MIC) &&
                    (my_data->source_mic_type & SOURCE_DUAL_MIC)) {
                    snd_device = SND_DEVICE_IN_HANDSET_DMIC;
                    platform_set_echo_reference(adev, true, out_devices);
                }
            }
        } else if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_LOOPBACK)) {
            if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_BUILTIN_MIC)) {
                usecase = get_usecase_from_list(adev, uc_id);
                if (usecase == NULL) {
                    ALOGE("%s: Could not find the record usecase", __func__);
                    snd_device = SND_DEVICE_NONE;
                    goto exit;
                }

                int ch_count = audio_channel_count_from_in_mask(channel_mask);
                snd_device = audio_extn_get_loopback_snd_device(adev, usecase,
                                  ch_count);
                ALOGD("%s: snd device %d", __func__, snd_device);
            }
        }
    } else if (source == AUDIO_SOURCE_FM_TUNER) {
        snd_device = SND_DEVICE_IN_CAPTURE_FM;
    } else if (source == AUDIO_SOURCE_DEFAULT) {
        goto exit;
    }

    if (in && (audio_extn_ssr_get_stream() == in))
        snd_device = SND_DEVICE_IN_THREE_MIC;

    if (snd_device != SND_DEVICE_NONE) {
        goto exit;
    }

    if (!list_empty(&in_devices) &&
            !(compare_device_type(&in_devices, AUDIO_DEVICE_IN_VOICE_CALL)) &&
            !(compare_device_type(&in_devices, AUDIO_DEVICE_IN_COMMUNICATION))) {
        if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_BUILTIN_MIC)) {
            if ((in && (audio_extn_ssr_get_stream() == in)) ||
                ((my_data->source_mic_type & SOURCE_QUAD_MIC) &&
                 channel_mask == AUDIO_CHANNEL_INDEX_MASK_4))
                snd_device = SND_DEVICE_IN_QUAD_MIC;
            else if ((my_data->source_mic_type & SOURCE_THREE_MIC) &&
                       channel_mask == AUDIO_CHANNEL_INDEX_MASK_3)
                snd_device = SND_DEVICE_IN_THREE_MIC;
            else if ((my_data->fluence_type & (FLUENCE_DUAL_MIC | FLUENCE_TRI_MIC | FLUENCE_QUAD_MIC)) &&
                    (channel_count == 2) && (my_data->source_mic_type & SOURCE_DUAL_MIC))
                snd_device = SND_DEVICE_IN_HANDSET_DMIC_STEREO;
            else
                snd_device = my_data->fluence_sb_enabled ? SND_DEVICE_IN_HANDSET_MIC_SB
                                 : (my_data->fluence_nn_enabled? SND_DEVICE_IN_HANDSET_MIC_NN
                                 : SND_DEVICE_IN_HANDSET_MIC);
        } else if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_BACK_MIC)) {
            if ((my_data->source_mic_type & SOURCE_DUAL_MIC) &&
                    channel_count == 2)
                snd_device = SND_DEVICE_IN_SPEAKER_DMIC_STEREO;
            else
                snd_device = my_data->fluence_sb_enabled ?
                                 SND_DEVICE_IN_SPEAKER_MIC_SB
                                 : (my_data->fluence_nn_enabled ?
                                     SND_DEVICE_IN_SPEAKER_MIC_NN
                                    : SND_DEVICE_IN_SPEAKER_MIC);
        } else if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_LINE)) {
            snd_device = SND_DEVICE_IN_LINE;
        } else if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_WIRED_HEADSET)) {
            snd_device = SND_DEVICE_IN_HEADSET_MIC;
        } else if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET)) {
            if (adev->swb_speech_mode != SPEECH_MODE_INVALID) {
                if (adev->bluetooth_nrec)
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC_SWB_NREC;
                else
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC_SWB;
            } else if (adev->bt_wb_speech_enabled) {
                if (adev->bluetooth_nrec)
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC_WB_NREC;
                else
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC_WB;
            } else {
                if (adev->bluetooth_nrec)
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC_NREC;
                else
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC;
            }
        } else if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_SPDIF)) {
            snd_device = SND_DEVICE_IN_SPDIF;
        } else if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_BLUETOOTH_A2DP)) {
            snd_device = SND_DEVICE_IN_BT_A2DP;
        } else if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_AUX_DIGITAL)) {
            snd_device = SND_DEVICE_IN_HDMI_MIC;
        } else if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_HDMI_ARC)) {
            snd_device = SND_DEVICE_IN_HDMI_ARC;
        } else if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET) ||
                   compare_device_type(&in_devices, AUDIO_DEVICE_IN_DGTL_DOCK_HEADSET)) {
            snd_device = SND_DEVICE_IN_USB_HEADSET_MIC;
        } else if (compare_device_type(&in_devices, AUDIO_DEVICE_IN_FM_TUNER)) {
            snd_device = SND_DEVICE_IN_CAPTURE_FM;
        } else if (audio_extn_usb_connected(NULL) &&
                   is_usb_in_device_type(&in_devices)) {
            snd_device = fixup_usb_headset_mic_snd_device(platform,
                                                  SND_DEVICE_IN_USB_HEADSET_MIC,
                                                  SND_DEVICE_IN_USB_HEADSET_MULTI_CHANNEL_MIC);
        } else {
            ALOGE("%s: Unknown input device(s) %#x", __func__, get_device_types(&in_devices));
            ALOGW("%s: Using default handset-mic", __func__);
            snd_device = my_data->fluence_sb_enabled ? SND_DEVICE_IN_HANDSET_MIC_SB
                             : (my_data->fluence_nn_enabled ? SND_DEVICE_IN_HANDSET_MIC_NN
                               : SND_DEVICE_IN_HANDSET_MIC);
        }
    } else {
        if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_EARPIECE)) {
            snd_device = my_data->fluence_sb_enabled ? SND_DEVICE_IN_HANDSET_MIC_SB
                             : (my_data->fluence_nn_enabled ? SND_DEVICE_IN_HANDSET_MIC_NN
                               : SND_DEVICE_IN_HANDSET_MIC);
        } else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADSET)) {
            snd_device = SND_DEVICE_IN_HEADSET_MIC;
        } else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_SPEAKER) ||
                   compare_device_type(out_devices, AUDIO_DEVICE_OUT_SPEAKER_SAFE)) {
            if ((my_data->source_mic_type & SOURCE_DUAL_MIC) &&
                (channel_count == 2)) {
                snd_device = SND_DEVICE_IN_SPEAKER_DMIC_STEREO;
            } else if ((my_data->source_mic_type & SOURCE_MONO_MIC) &&
                       (channel_count == 1)) {
                snd_device = my_data->fluence_sb_enabled ?
                                 SND_DEVICE_IN_SPEAKER_MIC_SB
                                 : (my_data->fluence_nn_enabled ?
                                       SND_DEVICE_IN_SPEAKER_MIC_NN
                                       : SND_DEVICE_IN_SPEAKER_MIC);
            } else {
                ALOGE("%s: something wrong: source type (%d) channel_count (%d) .."
                      " no combination found .. setting to mono", __func__,
                      my_data->source_mic_type, channel_count);
                snd_device = my_data->fluence_sb_enabled ?
                                 SND_DEVICE_IN_SPEAKER_MIC_SB
                                 : (my_data->fluence_nn_enabled ?
                                       SND_DEVICE_IN_SPEAKER_MIC_NN
                                       : SND_DEVICE_IN_SPEAKER_MIC);
            }
        } else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_WIRED_HEADPHONE) ||
                   compare_device_type(out_devices, AUDIO_DEVICE_OUT_LINE)) {
            snd_device = my_data->fluence_sb_enabled ? SND_DEVICE_IN_HANDSET_MIC_SB
                             : (my_data->fluence_nn_enabled ? SND_DEVICE_IN_HANDSET_MIC_NN
                               : SND_DEVICE_IN_HANDSET_MIC);
        } else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET)) {
            if (adev->swb_speech_mode != SPEECH_MODE_INVALID) {
                if (adev->bluetooth_nrec)
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC_SWB_NREC;
                else
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC_SWB;
            } else if (adev->bt_wb_speech_enabled) {
                if (adev->bluetooth_nrec)
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC_WB_NREC;
                else
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC_WB;
            } else {
                if (adev->bluetooth_nrec)
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC_NREC;
                else
                    snd_device = SND_DEVICE_IN_BT_SCO_MIC;
            }
        } else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
            snd_device = SND_DEVICE_IN_HDMI_MIC;
        } else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET) ||
                   compare_device_type(out_devices, AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET)) {
            snd_device = SND_DEVICE_IN_USB_HEADSET_MIC;
        } else if (compare_device_type(out_devices, AUDIO_DEVICE_OUT_USB_DEVICE) ||
                   compare_device_type(out_devices, AUDIO_DEVICE_OUT_USB_HEADSET)) {
            if (audio_extn_usb_is_capture_supported() && audio_extn_usb_connected(NULL))
                snd_device = fixup_usb_headset_mic_snd_device(platform,
                                                      SND_DEVICE_IN_USB_HEADSET_MIC,
                                                      SND_DEVICE_IN_USB_HEADSET_MULTI_CHANNEL_MIC);
            else
              snd_device = SND_DEVICE_IN_HANDSET_MIC;

        } else {
            ALOGE("%s: Unknown output device(s) %#x", __func__, get_device_types(out_devices));
            ALOGW("%s: Using default handset-mic", __func__);
            snd_device = my_data->fluence_sb_enabled ? SND_DEVICE_IN_HANDSET_MIC_SB
                             : (my_data->fluence_nn_enabled ? SND_DEVICE_IN_HANDSET_MIC_NN
                               : SND_DEVICE_IN_HANDSET_MIC);
        }
    }
exit:
    ALOGV("%s: exit: in_snd_device(%s)", __func__, device_table[snd_device]);
    return snd_device;
}

int platform_set_hdmi_channels(void *platform,  int channel_count) {
    return platform_set_hdmi_channels_v2(platform, channel_count, 0, 0);
}

int platform_set_hdmi_channels_v2(void *platform,  int channel_count,
                               int controller, int stream)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    const char *channel_cnt_str = NULL;
    int ctl_index = 0;
    struct mixer_ctl *ctl = NULL;
    const char *ctl_name_prefix = "Display Port";
    const char *ctl_name_suffix = "RX Channels";
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};

    switch (channel_count) {
    case 8:
        channel_cnt_str = "Eight"; break;
    case 7:
        channel_cnt_str = "Seven"; break;
    case 6:
        channel_cnt_str = "Six"; break;
    case 5:
        channel_cnt_str = "Five"; break;
    case 4:
        channel_cnt_str = "Four"; break;
    case 3:
        channel_cnt_str = "Three"; break;
    default:
        channel_cnt_str = "Two"; break;
    }

    ctl_index = platform_get_display_port_ctl_index(controller, stream);
    if (-EINVAL == ctl_index) {
        ALOGE("%s: Unknown controller/stream %d/%d",
              __func__, controller, stream);
        return -EINVAL;
    }

    switch(my_data->ext_disp[controller][stream].type) {
        case EXT_DISPLAY_TYPE_HDMI:
            snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "HDMI_RX Channels");
            break;
        case EXT_DISPLAY_TYPE_DP:
            if (!audio_extn_is_display_port_enabled()) {
                ALOGE("%s: display port is not supported", __func__);
                return -EINVAL;
            }

            if (0 == ctl_index)
                snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                         "%s %s", ctl_name_prefix, ctl_name_suffix);
            else
                snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                         "%s%d %s", ctl_name_prefix, ctl_index, ctl_name_suffix);
        default:
            ALOGE("%s: Invalid disp_type %d", __func__,
                  my_data->ext_disp[controller][stream].type);
            return -EINVAL;
    }

    ALOGV("%s: mixer ctl name: %s", __func__, mixer_ctl_name);

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }

    ALOGV("%s: ext disp channel count: %s", __func__, channel_cnt_str);
    mixer_ctl_set_enum_by_string(ctl, channel_cnt_str);
    return 0;
}

int platform_edid_get_max_channels(void *platform) {
    return platform_edid_get_max_channels_v2(platform, 0, 0);
}

int platform_edid_get_max_channels_v2(void *platform, int controller, int stream)
{
    int channel_count;
    int max_channels = 2;
    int i = 0, ret = 0;
    struct platform_data *my_data = (struct platform_data *)platform;
    edid_audio_info *info = NULL;

    ret = platform_get_edid_info_v2(platform, controller, stream);
    if(ret == 0)
        info = (edid_audio_info *)my_data->ext_disp[controller][stream].edid_info;

    if(ret == 0 && info != NULL) {
        for (i = 0; i < info->audio_blocks && i < MAX_EDID_BLOCKS; i++) {
            ALOGV("%s:format %d channel %d", __func__,
                   info->audio_blocks_array[i].format_id,
                   info->audio_blocks_array[i].channels);
            if (info->audio_blocks_array[i].format_id == LPCM) {
                channel_count = info->audio_blocks_array[i].channels;
                if (channel_count > max_channels) {
                   max_channels = channel_count;
                }
            }
        }
    }
    return max_channels;
}

static int platform_set_slowtalk(struct platform_data *my_data, bool state)
{
    int ret = 0;
    struct audio_device *adev = my_data->adev;
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name = "Slowtalk Enable";
    long set_values[ ] = {0,
                          ALL_SESSION_VSID};

    set_values[0] = state;
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        ret = -EINVAL;
    } else {
        ALOGV("Setting slowtalk state: %d", state);
        ret = mixer_ctl_set_array(ctl, set_values, ARRAY_SIZE(set_values));
        my_data->slowtalk = state;
    }

    if (my_data->csd != NULL) {
        ret = my_data->csd->slow_talk(ALL_SESSION_VSID, state);
        if (ret < 0) {
            ALOGE("%s: csd_client_disable_device, failed, error %d",
                  __func__, ret);
        }
    }
    return ret;
}

static int set_hd_voice(struct platform_data *my_data, bool state)
{
    struct audio_device *adev = my_data->adev;
    struct mixer_ctl *ctl;
    char *mixer_ctl_name = "HD Voice Enable";
    int ret = 0;
    long set_values[ ] = {0,
                          ALL_SESSION_VSID};

    set_values[0] = state;
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    } else {
        ALOGV("Setting HD Voice state: %d", state);
        ret = mixer_ctl_set_array(ctl, set_values, ARRAY_SIZE(set_values));
        my_data->hd_voice = state;
    }

    return ret;
}

bool platform_get_eccarstate(void *platform)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    return my_data->ec_car_state;
}

static int platform_set_eccarstate(struct platform_data *my_data, bool state)
{
    int ret = 0;
    ALOGD("Setting EC Car state: %d", state);
    my_data->ec_car_state = state;

    return ret;
}

static int update_external_device_status(struct platform_data *my_data,
                                 char* event_name, bool status)
{
    int ret = 0;
    struct audio_usecase *usecase;
    struct listnode *node;

    ALOGD("Recieved  external event switch %s", event_name);

    if (!strcmp(event_name, EVENT_EXTERNAL_SPK_1))
        my_data->external_spk_1 = status;
    else if (!strcmp(event_name, EVENT_EXTERNAL_SPK_2))
        my_data->external_spk_2 = status;
    else if (!strcmp(event_name, EVENT_EXTERNAL_MIC))
        my_data->external_mic = status;
    else {
        ALOGE("The audio event type is not found");
        return -EINVAL;
    }

    list_for_each(node, &my_data->adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        select_devices(my_data->adev, usecase->id);
    }

    return ret;
}

static int platform_set_hfp_zone(struct platform_data *my_data, uint32_t zone)
{
    int ret = 0;
    int acdb_dev_id = -1;
    struct audio_usecase *usecase = NULL;
    struct audio_device *adev = (struct audio_device *)(my_data->adev);
    struct audio_fluence_mmsecns_config fluence_mmsecns_config;
    acdb_audio_cal_cfg_t cal = {0};
    ALOGV("Setting HFP Zone: %d", zone);

    /* Zone control is available only when EC car state is set. */
    if (!platform_get_eccarstate((void *) my_data)) {
        ALOGE("%s: EC State should be enabled first.", __func__);
        return -EINVAL;
    }

    usecase = get_usecase_from_list(adev, audio_extn_hfp_get_usecase());
    if (usecase == NULL) {
        ALOGE("%s: Could not find the usecase.", __func__);
        return -EINVAL;
    }

    acdb_dev_id = acdb_device_table[platform_get_spkr_prot_snd_device(usecase->in_snd_device)];
    if (acdb_dev_id < 0) {
        ALOGE("%s: Could not find acdb id for device(%d)",
              __func__, usecase->in_snd_device);
        return -EINVAL;
    }

    if (platform_get_fluence_mmsecns_config_data(&fluence_mmsecns_config) < 0) {
        ALOGE("%s: Failed to get fluence mmsecns config data.", __func__);
        return -EINVAL;
    }

    cal.acdb_dev_id = acdb_dev_id;
    cal.app_type = DEFAULT_APP_TYPE_TX_PATH;
    cal.topo_id = fluence_mmsecns_config.topology_id;
    cal.module_id = fluence_mmsecns_config.module_id;
#ifdef INSTANCE_ID_ENABLED
    cal.instance_id = fluence_mmsecns_config.instance_id;
#endif
    cal.param_id = fluence_mmsecns_config.param_id;

    if (my_data->acdb_set_audio_cal) {
        ret = my_data->acdb_set_audio_cal((void *)&cal, (void *)&zone, sizeof(uint32_t));
    }

    if (ret < 0)
        ALOGE("%s: Could not set hfp zone calibration to zone %d",
              __func__, zone);
    else
        ALOGV("%s: Successfully set hfp zone calibration to zone %d",
              __func__, zone);

    return ret;
}

static int platform_get_hfp_zone(struct platform_data *my_data)
{
    int ret = 0;
    int acdb_dev_id = -1;
    struct audio_usecase *usecase = NULL;
    struct audio_device *adev = (struct audio_device *)(my_data->adev);
    struct audio_fluence_mmsecns_config fluence_mmsecns_config;
    acdb_audio_cal_cfg_t cal = {0};
    uint8_t *dptr = NULL;
    uint32_t zone = 0;
    uint32_t param_len = MAX_SET_CAL_BYTE_SIZE;
    ALOGV("Getting HFP Zone");

    /* Zone control is available only when EC car state is set. */
    if (!platform_get_eccarstate((void *) my_data)) {
        ALOGE("%s: EC State should be enabled first.", __func__);
        return -EINVAL;
    }

    usecase = get_usecase_from_list(adev, audio_extn_hfp_get_usecase());
    if (usecase == NULL) {
        ALOGE("%s: Could not find the usecase.", __func__);
        return -EINVAL;
    }

    acdb_dev_id = acdb_device_table[platform_get_spkr_prot_snd_device(usecase->in_snd_device)];
    if (acdb_dev_id < 0) {
        ALOGE("%s: Could not find acdb id for device(%d)",
              __func__, usecase->in_snd_device);
        return -EINVAL;
    }

    if (platform_get_fluence_mmsecns_config_data(&fluence_mmsecns_config) < 0) {
        ALOGE("%s: Failed to get fluence mmsecns config data.", __func__);
        return -EINVAL;
    }

    cal.acdb_dev_id = acdb_dev_id;
    cal.app_type = DEFAULT_APP_TYPE_TX_PATH;
    cal.topo_id = fluence_mmsecns_config.topology_id;
    cal.module_id = fluence_mmsecns_config.module_id;
#ifdef INSTANCE_ID_ENABLED
    cal.instance_id = fluence_mmsecns_config.instance_id;
#endif
    cal.param_id = fluence_mmsecns_config.param_id;

    dptr = (uint8_t*)calloc(param_len, sizeof(uint8_t));
    if (!dptr) {
        ALOGE("%s: Failed to allocate memory.", __func__);
        return -ENOMEM;
    }

    if (my_data->acdb_get_audio_cal) {
        ret = my_data->acdb_get_audio_cal((void *)&cal, (void *)dptr, &param_len);
        if (ret == 0) {
            if ((param_len == 0) || (param_len == MAX_SET_CAL_BYTE_SIZE)) {
                ret = -EINVAL;
            } else if (param_len > 16) {
                /* returned data structure:
                 *  u32 module_id
                 *  u32 instance_id
                 *  u32 parameter_id
                 *  u32 payload_size
                 *  u8  payload[payload_size]
                 */
                zone = *(uint32_t *)(dptr + 16);
            }
        }
    }

    if (ret < 0)
        ALOGE("%s: Could not get hfp zone calibration to zone %d",
              __func__, zone);
    else
        ALOGV("%s: Successfully get hfp zone calibration to zone %d",
              __func__, zone);

    if (dptr)
        free(dptr);
    return zone;
}

static int parse_audiocal_cfg(struct str_parms *parms, acdb_audio_cal_cfg_t *cal)
{
    int err;
    char value[64];
    int ret = 0;

    if(parms == NULL || cal == NULL)
        return ret;

    err = str_parms_get_str(parms, "cal_persist", value, sizeof(value));
    if (err >= 0) {
        str_parms_del(parms, "cal_persist");
        cal->persist = (uint32_t) strtoul(value, NULL, 0);
        ret = ret | 0x1;
    }
    err = str_parms_get_str(parms, "cal_apptype", value, sizeof(value));
    if (err >= 0) {
        str_parms_del(parms, "cal_apptype");
        cal->app_type = (uint32_t) strtoul(value, NULL, 0);
        ret = ret | 0x2;
    }
    err = str_parms_get_str(parms, "cal_caltype", value, sizeof(value));
    if (err >= 0) {
        str_parms_del(parms, "cal_caltype");
        cal->cal_type = (uint32_t) strtoul(value, NULL, 0);
        ret = ret | 0x4;
    }
    err = str_parms_get_str(parms, "cal_samplerate", value, sizeof(value));
    if (err >= 0) {
        str_parms_del(parms, "cal_samplerate");
        cal->sampling_rate = (uint32_t) strtoul(value, NULL, 0);
        ret = ret | 0x8;
    }
    err = str_parms_get_str(parms, "cal_devid", value, sizeof(value));
    if (err >= 0) {
        str_parms_del(parms, "cal_devid");
        cal->dev_id = (uint32_t) strtoul(value, NULL, 0);
        ret = ret | 0x10;
    }
    err = str_parms_get_str(parms, "cal_snddevid", value, sizeof(value));
    if (err >= 0) {
        str_parms_del(parms, "cal_snddevid");
        cal->snd_dev_id = (uint32_t) strtoul(value, NULL, 0);
        ret = ret | 0x20;
    }
    err = str_parms_get_str(parms, "cal_topoid", value, sizeof(value));
    if (err >= 0) {
        str_parms_del(parms, "cal_topoid");
        cal->topo_id = (uint32_t) strtoul(value, NULL, 0);
        ret = ret | 0x40;
    }
    err = str_parms_get_str(parms, "cal_moduleid", value, sizeof(value));
    if (err >= 0) {
        str_parms_del(parms, "cal_moduleid");
        cal->module_id = (uint32_t) strtoul(value, NULL, 0);
        ret = ret | 0x80;
    }
#ifdef INSTANCE_ID_ENABLED
    err = str_parms_get_str(parms, "cal_instanceid", value, sizeof(value));
    if (err >= 0) {
        str_parms_del(parms, "cal_instanceid");
        cal->instance_id = (uint32_t) strtoul(value, NULL, 0);
        ret = ret | 0x100;
    }
#endif
    err = str_parms_get_str(parms, "cal_paramid", value, sizeof(value));
    if (err >= 0) {
        str_parms_del(parms, "cal_paramid");
        cal->param_id = (uint32_t) strtoul(value, NULL, 0);
        ret = ret | 0x200;
    }
    return ret;
}

static void set_audiocal(void *platform, struct str_parms *parms, char *value, int len) {
    struct platform_data *my_data = (struct platform_data *)platform;
    struct stream_out out;
    acdb_audio_cal_cfg_t cal;
    uint8_t *dptr = NULL;
    int32_t dlen;
    int err, ret;
    char *address = "";
    if(value == NULL || platform == NULL || parms == NULL) {
        ALOGE("[%s] received null pointer, failed",__func__);
        goto done_key_audcal;
    }

    /* handle audio calibration data now */
    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_AUD_CALDATA, value, len);
    if (err >= 0) {
        memset(&cal, 0, sizeof(acdb_audio_cal_cfg_t));
        /* parse audio calibration keys */
        ret = parse_audiocal_cfg(parms, &cal);

        str_parms_del(parms, AUDIO_PARAMETER_KEY_AUD_CALDATA);
        dlen = strlen(value);
        if(dlen <= 0) {
            ALOGE("[%s] null data received",__func__);
            goto done_key_audcal;
        }
        dptr = (uint8_t*) calloc(dlen, sizeof(uint8_t));
        if(dptr == NULL) {
            ALOGE("[%s] memory allocation failed for %d",__func__, dlen);
            goto done_key_audcal;
        }
        dlen = b64_pton(value, dptr, dlen);
        if(dlen<=0) {
            ALOGE("[%s] data decoding failed %d", __func__, dlen);
            goto done_key_audcal;
        }

        list_init(&out.device_list);
        if (cal.dev_id) {
          if (audio_is_input_device(cal.dev_id)) {
              // FIXME: why pass an input device whereas
              // platform_get_input_snd_device() expects as an output device?
              struct listnode cal_devices;
              list_init(&cal_devices);
              update_device_list(&cal_devices, cal.dev_id, address, true);
              cal.snd_dev_id = platform_get_input_snd_device(platform, NULL, &cal_devices,
                                                             USECASE_TYPE_MAX);
          } else {
              reassign_device_list(&out.device_list, cal.dev_id, address);
              out.sample_rate = cal.sampling_rate;
              cal.snd_dev_id = platform_get_output_snd_device(platform, &out, USECASE_TYPE_MAX);
          }
        }
        cal.acdb_dev_id = platform_get_snd_device_acdb_id(cal.snd_dev_id);
        ALOGD("Setting audio calibration for snd_device(%d) acdb_id(%d)",
                cal.snd_dev_id, cal.acdb_dev_id);
        if(cal.acdb_dev_id == -EINVAL) {
            ALOGE("[%s] Invalid acdb_device id %d for snd device id %d",
                       __func__, cal.acdb_dev_id, cal.snd_dev_id);
            goto done_key_audcal;
        }
        if(my_data->acdb_set_audio_cal) {
            ret = my_data->acdb_set_audio_cal((void *)&cal, (void*)dptr, dlen);
        }
    }
done_key_audcal:
    if(dptr != NULL)
        free(dptr);
}

static void true_32_bit_set_params(struct str_parms *parms,
                                 char *value, int len)
{
    int ret = 0;

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_TRUE_32_BIT,
                            value,len);
    if (ret >= 0) {
        if (value && !strncmp(value, "true", sizeof("true")))
            supports_true_32_bit = true;
        else
            supports_true_32_bit = false;
        str_parms_del(parms, AUDIO_PARAMETER_KEY_TRUE_32_BIT);
    }

}

bool platform_supports_true_32bit()
{
   return supports_true_32_bit;
}

static void perf_lock_set_params(struct platform_data *platform,
                          struct str_parms *parms,
                          char *value, int len)
{
    int err = 0, i = 0, num_opts = 0;
    char *test_r = NULL;
    char *opts = NULL;
    char *opts_size = NULL;

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_PERF_LOCK_OPTS,
                            value, len);
    if (err >= 0) {
        opts_size = strtok_r(value, ", ", &test_r);
        if (opts_size == NULL) {
            ALOGE("%s: incorrect perf lock opts\n", __func__);
            return;
        }
        num_opts = atoi(opts_size);
        if (num_opts > 0) {
            if (num_opts > MAX_PERF_LOCK_OPTS) {
                ALOGD("%s: num_opts %d exceeds max %d, setting to max\n",
                      __func__, num_opts, MAX_PERF_LOCK_OPTS);
                num_opts = MAX_PERF_LOCK_OPTS;
            }
            for (i = 0; i < num_opts; i++) {
                opts = strtok_r(NULL, ", ", &test_r);
                if (opts == NULL) {
                    ALOGE("%s: incorrect perf lock opts\n", __func__);
                    break;
                }
                platform->adev->perf_lock_opts[i] = strtoul(opts, NULL, 16);
            }
            platform->adev->perf_lock_opts_size = i;
        }
        str_parms_del(parms, AUDIO_PARAMETER_KEY_PERF_LOCK_OPTS);
    }
}

static void platform_spkr_device_set_params(struct platform_data *platform,
                                            struct str_parms *parms,
                                            char *value, int len)
{
    int err = 0, i = 0, num_ch = 0;
    char *test_r = NULL;
    char *opts = NULL;
    char *ch_count = NULL;

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_SPKR_DEVICE_CHMAP,
                            value, len);
    if (err >= 0) {
        platform->spkr_ch_map = calloc(1, sizeof(struct spkr_device_chmap));
        if (!platform->spkr_ch_map) {
            ALOGE("%s: failed to allocate mem for adm channel map\n", __func__);
            str_parms_del(parms, AUDIO_PARAMETER_KEY_SPKR_DEVICE_CHMAP);
            return ;
        }

        ch_count = strtok_r(value, ", ", &test_r);
        if (ch_count == NULL) {
            ALOGE("%s: incorrect ch_map\n", __func__);
            free(platform->spkr_ch_map);
            platform->spkr_ch_map = NULL;
            str_parms_del(parms, AUDIO_PARAMETER_KEY_SPKR_DEVICE_CHMAP);
            return;
        }

        num_ch = atoi(ch_count);
        if ((num_ch > 0) && (num_ch <= AUDIO_CHANNEL_COUNT_MAX) ) {
            platform->spkr_ch_map->num_ch = num_ch;
            for (i = 0; i < num_ch; i++) {
                opts = strtok_r(NULL, ", ", &test_r);
                if (opts == NULL) {
                    ALOGE("%s: incorrect ch_map\n", __func__);
                    free(platform->spkr_ch_map);
                    platform->spkr_ch_map = NULL;
                    str_parms_del(parms, AUDIO_PARAMETER_KEY_SPKR_DEVICE_CHMAP);
                    return;
                } else {
                    platform->spkr_ch_map->chmap[i] = strtoul(opts, NULL, 16);
                }
            }
        }
        str_parms_del(parms, AUDIO_PARAMETER_KEY_SPKR_DEVICE_CHMAP);
    }
}

static void platform_set_fluence_params(void *platform, struct str_parms *parms, char *value, int len)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    int err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FLUENCE_TYPE, value, len);

    if (err >= 0) {
        if (!strncmp("fluence", value, sizeof("fluence")))
            my_data->fluence_type = FLUENCE_DUAL_MIC;
        else if (!strncmp("fluencepro", value, sizeof("fluencepro")))
                 my_data->fluence_type = FLUENCE_QUAD_MIC | FLUENCE_DUAL_MIC;
        else if (!strncmp("none", value, sizeof("none")))
                 my_data->fluence_type = FLUENCE_NONE;

        str_parms_del(parms, AUDIO_PARAMETER_KEY_FLUENCE_TYPE);
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FLUENCE_TRI_MIC, value, len);
    if (err >= 0) {
        if (!strncmp("true", value, sizeof("true")))
            my_data->fluence_type |= FLUENCE_TRI_MIC;

        str_parms_del(parms, AUDIO_PARAMETER_KEY_FLUENCE_TRI_MIC);
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FLUENCE_VOICE_CALL, value, len);
    if (err >= 0) {
        if (!strncmp("true", value, sizeof("true")))
            my_data->fluence_in_voice_call = true;
        else
            my_data->fluence_in_voice_call = false;

        str_parms_del(parms, AUDIO_PARAMETER_KEY_FLUENCE_VOICE_CALL);
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FLUENCE_VOICE_REC, value, len);
    if (err >= 0) {
        if (!strncmp("true", value, sizeof("true")))
            my_data->fluence_in_voice_rec = true;
        else
            my_data->fluence_in_voice_rec = false;

        str_parms_del(parms, AUDIO_PARAMETER_KEY_FLUENCE_VOICE_REC);
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FLUENCE_AUDIO_REC, value, len);
    if (err >= 0) {
        if (!strncmp("true", value, sizeof("true")))
            my_data->fluence_in_audio_rec = true;
        else
            my_data->fluence_in_audio_rec = false;

        str_parms_del(parms, AUDIO_PARAMETER_KEY_FLUENCE_AUDIO_REC);
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FLUENCE_SPEAKER, value, len);
    if (err >= 0) {
        if (!strncmp("true", value, sizeof("true")))
            my_data->fluence_in_spkr_mode = true;
        else
            my_data->fluence_in_spkr_mode = false;

        str_parms_del(parms, AUDIO_PARAMETER_KEY_FLUENCE_SPEAKER);
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FLUENCE_MODE, value, len);
    if (err >= 0) {
        if (!strncmp("broadside", value, sizeof("broadside")))
            my_data->fluence_mode = FLUENCE_BROADSIDE;
        else if (!strncmp("endfire", value, sizeof("endfire")))
            my_data->fluence_mode = FLUENCE_ENDFIRE;

        str_parms_del(parms, AUDIO_PARAMETER_KEY_FLUENCE_MODE);
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FLUENCE_HFPCALL, value, len);
    if (err >= 0) {
        if (!strncmp("true", value, sizeof("true")))
            my_data->fluence_in_hfp_call = true;
        else
            my_data->fluence_in_hfp_call = false;

        str_parms_del(parms, AUDIO_PARAMETER_KEY_FLUENCE_HFPCALL);
    }
}

int platform_set_parameters(void *platform, struct str_parms *parms)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    char *value=NULL;
    int len;
    int ret = 0, err;
    char *kv_pairs = str_parms_to_str(parms);
    struct listnode *node;
    struct meta_key_list *key_info;
    int key = 0;

    if(kv_pairs == NULL) {
        ret = -ENOMEM;
        ALOGE("[%s] key-value pair is NULL",__func__);
        goto done;
    }

    ALOGV_IF(kv_pairs != NULL, "%s: enter: %s", __func__, kv_pairs);

    len = strlen(kv_pairs);
    value = (char*)calloc(len, sizeof(char));
    if(value == NULL) {
        ret = -ENOMEM;
        ALOGE("[%s] failed to allocate memory",__func__);
        goto done;
    }
    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_SLOWTALK, value, len);
    if (err >= 0) {
        bool state = false;
        if (!strncmp("true", value, sizeof("true"))) {
            state = true;
        }

        str_parms_del(parms, AUDIO_PARAMETER_KEY_SLOWTALK);
        ret = platform_set_slowtalk(my_data, state);
        if (ret)
            ALOGE("%s: Failed to set slow talk err: %d", __func__, ret);
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_HD_VOICE, value, len);
    if (err >= 0) {
        bool state = false;
        if (!strncmp("true", value, sizeof("true"))) {
            state = true;
        }

        str_parms_del(parms, AUDIO_PARAMETER_KEY_HD_VOICE);
        if (my_data->hd_voice != state) {
            ret = set_hd_voice(my_data, state);
            if (ret)
                ALOGE("%s: Failed to set HD voice err: %d", __func__, ret);
        } else {
            ALOGV("%s: HD Voice already set to %d", __func__, state);
        }
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_VOLUME_BOOST,
                            value, len);
    if (err >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_VOLUME_BOOST);

        if (my_data->acdb_reload_vocvoltable == NULL) {
            ALOGE("%s: acdb_reload_vocvoltable is NULL", __func__);
        } else if (!strcmp(value, "on")) {
            if (!my_data->acdb_reload_vocvoltable(VOICE_FEATURE_SET_VOLUME_BOOST)) {
                my_data->voice_feature_set = 1;
            }
        } else {
            if (!my_data->acdb_reload_vocvoltable(VOICE_FEATURE_SET_DEFAULT)) {
                my_data->voice_feature_set = 0;
            }
        }
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_EC_CAR_STATE,
                            value, len);
    if (err >= 0) {
        bool state = false;
        if (!strncmp("true", value, sizeof("true"))) {
            state = true;
            ALOGD("%s: Value of EC CAR STATE set to true!", __func__);
        }
        str_parms_del(parms, AUDIO_PARAMETER_KEY_EC_CAR_STATE);
        platform_set_eccarstate(my_data, state);
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_RELOAD_ACDB,
                            value, len);
    if (err >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_RELOAD_ACDB);

        if (my_data->acdb_reload_v2) {
            my_data->acdb_reload_v2(value, my_data->snd_card_name,
                                  my_data->cvd_version, &my_data->acdb_meta_key_list);
        } else if (my_data->acdb_reload) {
            node = list_head(&my_data->acdb_meta_key_list);
            key_info = node_to_item(node, struct meta_key_list, list);
            key = key_info->cal_info.nKey;
            my_data->acdb_reload(value, my_data->snd_card_name,
                                  my_data->cvd_version, key);
        }
    }

    if (hw_info_is_stereo_spkr(my_data->hw_info)) {
        err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_MONO_SPEAKER, value, len);
        if (err >= 0) {
            if (!strncmp("left", value, sizeof("left")))
                my_data->mono_speaker = SPKR_1;
            else if (!strncmp("right", value, sizeof("right")))
                my_data->mono_speaker = SPKR_2;

            str_parms_del(parms, AUDIO_PARAMETER_KEY_MONO_SPEAKER);
        }
    }
    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_EXT_AUDIO_DEVICE,
                            value, len);
    if (err >= 0) {
        char *event_name, *status_str;
        bool status = false;
        str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_AUDIO_DEVICE);
        event_name = strtok_r(value, ",", &status_str);
        if (!event_name) {
            ret = -EINVAL;
            ALOGE("%s: event_name is NULL", __func__);
            goto done;
        }
        ALOGV("%s: recieved update of external audio device %s %s",
                         __func__,
                         event_name, status_str);
        if (!strncmp(status_str, "ON", sizeof("ON")))
            status = true;
        else if (!strncmp(status_str, "OFF", sizeof("OFF")))
            status = false;
        update_external_device_status(my_data, event_name, status);
    }

    err = str_parms_get_str(parms, PLATFORM_CONFIG_KEY_OPERATOR_INFO,
                            value, len);
    if (err >= 0) {
        struct operator_info *info;
        char *str = value, *context = NULL;
        char *name;

        str_parms_del(parms, PLATFORM_CONFIG_KEY_OPERATOR_INFO);
        info = (struct operator_info *)calloc(1, sizeof(struct operator_info));
        name = strtok_r(str, ";", &context);
        if ((info == NULL) || (name == NULL)) {
            ret = -EINVAL;
            ALOGE("%s invalid info or name", __func__);
            goto done;
        }

        info->name = strdup(name);
        info->mccmnc = strdup(str + strlen(name) + 1);

        list_add_tail(&operator_info_list, &info->list);
        ALOGV("%s: add operator[%s] mccmnc[%s]", __func__, info->name, info->mccmnc);
    }

    err = str_parms_get_str(parms, PLATFORM_MAX_MIC_COUNT,
                            value, sizeof(value));
    if (err >= 0) {
        str_parms_del(parms, PLATFORM_MAX_MIC_COUNT);
        my_data->max_mic_count = atoi(value);
        ALOGV("%s: max_mic_count %d", __func__, my_data->max_mic_count);
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_HFP_ZONE,
                            value, len);
    if (err >= 0) {
        uint32_t zone = atoi(value);

        str_parms_del(parms, AUDIO_PARAMETER_KEY_HFP_ZONE);
        if (zone > 6)
            ALOGE("%s: Only Zones 0 through 6 are supported", __func__);
        else
            platform_set_hfp_zone(my_data, zone);
    }

    platform_set_fluence_params(platform, parms, value, len);

    /* handle audio calibration parameters */
    set_audiocal(platform, parms, value, len);
    native_audio_set_params(platform, parms, value, len);
    audio_extn_spkr_prot_set_parameters(parms, value, len);
    audio_extn_usb_set_sidetone_gain(parms, value, len);
    audio_extn_hfp_set_parameters(my_data->adev, parms);
    perf_lock_set_params(platform, parms, value, len);
    true_32_bit_set_params(parms, value, len);
    audio_extn_hifi_filter_set_params(parms, value, len);
    platform_spkr_device_set_params(platform, parms, value, len);
done:
    ALOGV("%s: exit with code(%d)", __func__, ret);
    if(kv_pairs != NULL)
        free(kv_pairs);
    if(value != NULL)
        free(value);
    return ret;
}

int platform_set_incall_recording_session_id(void *platform,
                                             uint32_t session_id, int rec_mode)
{
    int ret = 0;
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name = "Voc VSID";
    int num_ctl_values;
    int i;

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        ret = -EINVAL;
    } else {
        num_ctl_values = mixer_ctl_get_num_values(ctl);
        for (i = 0; i < num_ctl_values; i++) {
            if (mixer_ctl_set_value(ctl, i, session_id)) {
                ALOGV("Error: invalid session_id: %x", session_id);
                ret = -EINVAL;
                break;
            }
        }
    }

    if (my_data->csd != NULL) {
        ret = my_data->csd->start_record(ALL_SESSION_VSID, rec_mode);
        if (ret < 0) {
            ALOGE("%s: csd_client_start_record failed, error %d",
                  __func__, ret);
        }
    }

    return ret;
}

#ifdef INCALL_STEREO_CAPTURE_ENABLED
int platform_set_incall_recording_session_channels(void *platform,
                                             uint32_t channel_count)
{
    int ret = 0;
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    const char *mixer_ctl_name = "Voc Rec Config";
    int num_ctl_values;
    int i;
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);

    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        ret = -EINVAL;
    } else {
        num_ctl_values = mixer_ctl_get_num_values(ctl);
        for (i = 0; i < num_ctl_values; i++) {
            if (mixer_ctl_set_value(ctl, i, channel_count)) {
                ALOGE("Error: invalid channel count: %x", channel_count);
                ret = -EINVAL;
                break;
            }
        }
    }

    return ret;
}
#endif /* INCALL_STEREO_CAPTURE_ENABLED end */

int platform_stop_incall_recording_usecase(void *platform)
{
    int ret = 0;
    struct platform_data *my_data = (struct platform_data *)platform;

    if (my_data->csd != NULL) {
        ret = my_data->csd->stop_record(ALL_SESSION_VSID);
        if (ret < 0) {
            ALOGE("%s: csd_client_stop_record failed, error %d",
                  __func__, ret);
        }
    }

    return ret;
}

int platform_start_incall_music_usecase(void *platform)
{
    int ret = 0;
    struct platform_data *my_data = (struct platform_data *)platform;

    if (my_data->csd != NULL) {
        ret = my_data->csd->start_playback(ALL_SESSION_VSID);
        if (ret < 0) {
            ALOGE("%s: csd_client_start_playback failed, error %d",
                  __func__, ret);
        }
    }

    return ret;
}

int platform_stop_incall_music_usecase(void *platform)
{
    int ret = 0;
    struct platform_data *my_data = (struct platform_data *)platform;

    if (my_data->csd != NULL) {
        ret = my_data->csd->stop_playback(ALL_SESSION_VSID);
        if (ret < 0) {
            ALOGE("%s: csd_client_stop_playback failed, error %d",
                  __func__, ret);
        }
    }

    return ret;
}

int platform_update_lch(void *platform, struct voice_session *session,
                        enum voice_lch_mode lch_mode)
{
    int ret = 0;
    struct platform_data *my_data = (struct platform_data *)platform;

    if ((my_data->csd != NULL) && (my_data->csd->set_lch != NULL))
        ret = my_data->csd->set_lch(session->vsid, lch_mode);
    else
        ret = pcm_ioctl(session->pcm_tx, SNDRV_VOICE_IOCTL_LCH, &lch_mode);

    return ret;
}

static void get_audiocal(void *platform, void *keys, void *pReply) {
    struct platform_data *my_data = (struct platform_data *)platform;
    struct stream_out out;
    struct str_parms *query = (struct str_parms *)keys;
    struct str_parms *reply=(struct str_parms *)pReply;
    acdb_audio_cal_cfg_t cal;
    uint8_t *dptr = NULL;
    char value[512] = {0};
    char *rparms=NULL;
    int ret=0, err;
    uint32_t param_len;
    char *address = "";

    if(query==NULL || platform==NULL || reply==NULL) {
        ALOGE("[%s] received null pointer",__func__);
        ret=-EINVAL;
        goto done;
    }

    // init cal
    memset(&cal, 0, sizeof(acdb_audio_cal_cfg_t));

    err = str_parms_get_str(query, AUDIO_PARAMETER_KEY_AUD_CALDATA, value, sizeof(value));
    if (err >= 0) {
        /* parse audiocal configuration keys */
        ret = parse_audiocal_cfg(query, &cal);
        if (ret == 0) {
            /* No calibration keys found */
            goto done;
        }

        str_parms_del(query, AUDIO_PARAMETER_KEY_AUD_CALDATA);
    } else {
        goto done;
    }

    list_init(&out.device_list);
    if (cal.dev_id & AUDIO_DEVICE_BIT_IN) {
        struct listnode devices;
        list_init(&devices);
        update_device_list(&devices, cal.dev_id, address, true);
        cal.snd_dev_id = platform_get_input_snd_device(platform, NULL, &devices, USECASE_TYPE_MAX);
    } else if (cal.dev_id) {
        reassign_device_list(&out.device_list, cal.dev_id, address);
        out.sample_rate = cal.sampling_rate;
        cal.snd_dev_id = platform_get_output_snd_device(platform, &out, USECASE_TYPE_MAX);
    }
    cal.acdb_dev_id =  platform_get_snd_device_acdb_id(cal.snd_dev_id);
    if (cal.acdb_dev_id < 0) {
        ALOGE("%s: Failed. Could not find acdb id for snd device(%d)",
              __func__, cal.snd_dev_id);
        ret = -EINVAL;
        goto done_key_audcal;
    }
    ALOGD("[%s] Getting audio calibration for snd_device(%d) acdb_id(%d)",
           __func__, cal.snd_dev_id, cal.acdb_dev_id);

    param_len = MAX_SET_CAL_BYTE_SIZE;
    dptr = (uint8_t*)calloc(param_len, sizeof(uint8_t));
    if(dptr == NULL) {
        ALOGE("[%s] Memory allocation failed for length %d",__func__,param_len);
        ret = -ENOMEM;
        goto done_key_audcal;
    }
    if (my_data->acdb_get_audio_cal != NULL) {
        ret = my_data->acdb_get_audio_cal((void*)&cal, (void*)dptr, &param_len);
        if (ret == 0) {
            if(param_len == 0 || param_len == MAX_SET_CAL_BYTE_SIZE) {
                ret = -EINVAL;
                goto done_key_audcal;
            }
            /* Allocate memory for encoding */
            rparms = (char*)calloc((param_len*2), sizeof(char));
            if(rparms == NULL) {
                ALOGE("[%s] Memory allocation failed for size %d",
                            __func__, param_len*2);
                ret = -ENOMEM;
                goto done_key_audcal;
            }
            if(cal.persist==0 && cal.module_id && cal.param_id) {
                err = b64encode(dptr+12, param_len-12, rparms);
            } else {
                err = b64encode(dptr, param_len, rparms);
            }
            if(err < 0) {
                ALOGE("[%s] failed to convert data to string", __func__);
                ret = -EINVAL;
                goto done_key_audcal;
            }
            str_parms_add_int(reply, AUDIO_PARAMETER_KEY_AUD_CALRESULT, ret);
            str_parms_add_str(reply, AUDIO_PARAMETER_KEY_AUD_CALDATA, rparms);
        }
    }
done_key_audcal:
    if(ret != 0) {
        str_parms_add_int(reply, AUDIO_PARAMETER_KEY_AUD_CALRESULT, ret);
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_AUD_CALDATA, "");
    }
done:
    if(dptr != NULL)
        free(dptr);
    if(rparms != NULL)
        free(rparms);
}

void platform_get_parameters(void *platform,
                            struct str_parms *query,
                            struct str_parms *reply)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    char value[512] = {0};
    int ret;
    char *kv_pairs = NULL;
    char propValue[PROPERTY_VALUE_MAX]={0};
    bool prop_playback_enabled = false;

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_SLOWTALK,
                            value, sizeof(value));
    if (ret >= 0) {
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_SLOWTALK,
                          my_data->slowtalk?"true":"false");
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_HD_VOICE,
                            value, sizeof(value));
    if (ret >= 0) {
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_HD_VOICE,
                          my_data->hd_voice?"true":"false");
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_VOLUME_BOOST,
                            value, sizeof(value));
    if (ret >= 0) {
        if (my_data->voice_feature_set == VOICE_FEATURE_SET_VOLUME_BOOST) {
            strlcpy(value, "on", sizeof(value));
        } else {
            strlcpy(value, "off", sizeof(value));
        }

        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_VOLUME_BOOST, value);
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_EC_CAR_STATE,
                            value, sizeof(value));
    if (ret >= 0) {
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_EC_CAR_STATE,
                          my_data->ec_car_state? "true" : "false");
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_DP_FOR_VOICE_USECASE,
                            value, sizeof(value));

    if (ret >= 0) {
        bool enabled = false;
        int i = 0, j = 0;
        if (adev->dp_allowed_for_voice) {
            for (i = 0; i < MAX_CONTROLLERS; ++i) {
                for (j = 0; j < MAX_STREAMS_PER_CONTROLLER; ++j) {
                    if (my_data->ext_disp[i][j].type == EXT_DISPLAY_TYPE_DP) {
                        enabled = true;
                        break;
                    }
                }
            }
        }
        strlcpy(value, enabled ? "true" : "false", sizeof(value));
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_DP_FOR_VOICE_USECASE, value);
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_DP_CHANNEL_MASK,
                            value, sizeof(value));
    if (ret >= 0) {
        int controller = 0, stream = 0;
        edid_audio_info *info = NULL;
        platform_get_controller_stream_from_params(query, &controller, &stream);
        ret = platform_get_edid_info_v2(platform, controller, stream);
        if (ret == 0)
            info = (edid_audio_info *)my_data->ext_disp[controller][stream].edid_info;
        if (ret == 0 && info != NULL) {
            str_parms_add_int(reply, AUDIO_PARAMETER_KEY_DP_CHANNEL_MASK, info->channel_mask);
        }
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_HFP_ZONE,
                            value, sizeof(value));
    if (ret >= 0) {
        snprintf(value, sizeof(value), "%d", platform_get_hfp_zone(my_data));
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_HFP_ZONE, value);
    }

    /* Handle audio calibration keys */
    get_audiocal(platform, query, reply);
    native_audio_get_params(query, reply, value, sizeof(value));

    ret = str_parms_get_str(query, AUDIO_PARAMETER_IS_HW_DECODER_SESSION_ALLOWED,
                                    value, sizeof(value));
    if (ret >= 0) {
        int isallowed = 1; /*true*/

        if (property_get("vendor.voice.playback.conc.disabled", propValue, NULL)) {
            prop_playback_enabled = atoi(propValue) ||
                !strncmp("true", propValue, 4);
        }

        if ((prop_playback_enabled && (voice_is_in_call(my_data->adev))) ||
             (CARD_STATUS_OFFLINE == my_data->adev->card_status)) {
            char *decoder_mime_type = value;

            //check if unsupported mime type or not
            if(decoder_mime_type) {
                unsigned int i = 0;
                for (i = 0; i < sizeof(dsp_only_decoders_mime)/sizeof(dsp_only_decoders_mime[0]); i++) {
                    if (!strncmp(decoder_mime_type, dsp_only_decoders_mime[i],
                    strlen(dsp_only_decoders_mime[i]))) {
                       ALOGD("Rejecting request for DSP only session from HAL during voice call/SSR state");
                       isallowed = 0;
                       break;
                    }
                }
            }
        }
        str_parms_add_int(reply, AUDIO_PARAMETER_IS_HW_DECODER_SESSION_ALLOWED, isallowed);
    }

    kv_pairs = str_parms_to_str(reply);
    ALOGV_IF(kv_pairs != NULL, "%s: exit: returns - %s", __func__, kv_pairs);
    free(kv_pairs);
}

unsigned char* platform_get_license(void *platform, int *size)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    char value[PROPERTY_VALUE_MAX] = {0};
    acdb_audio_cal_cfg_t cal;
    unsigned char *dptr = NULL;
    int ret=0;
    uint32_t param_len;

    if (platform == NULL) {
        ALOGE("[%s] received null pointer %d ",__func__, __LINE__);
        ret = -EINVAL;
        goto done;
    }
    memset(&cal, 0, sizeof(cal));
    cal.persist = 1;
    cal.cal_type = AUDIO_CORE_METAINFO_CAL_TYPE;
    if (!property_get("vendor.audio.qaf.acdbid", value , "") && !atoi(value)) {
        ALOGE("[%s] vendor.audio.qaf.acdbid is not set %d ",__func__, __LINE__);
        ret = -EINVAL;
        goto done;
    }
    cal.acdb_dev_id = (uint32_t) atoi (value);
    param_len = MAX_SET_CAL_BYTE_SIZE;
    dptr = (unsigned char*) calloc(param_len, sizeof(unsigned char));
    if (dptr == NULL) {
        ALOGE("[%s] Memory allocation failed for length %d",__func__,param_len);
        ret = -ENOMEM;
        goto done;
    }
    if (my_data->acdb_get_audio_cal != NULL) {
        ret = my_data->acdb_get_audio_cal((void*)&cal, (void*)dptr, &param_len);
        ALOGE("%s, ret[%d], param_len[%d] line %d", __func__, ret, param_len, __LINE__);
        if (ret == 0) {
            *size = param_len;
            return dptr;
        } else {
            *size = 0;
        }
    }
done:
    if (dptr != NULL)
        free(dptr);

    return NULL;
}

void platform_set_audio_source_delay(audio_source_t audio_source, int delay_ms)
{
    if ((audio_source < AUDIO_SOURCE_DEFAULT) ||
           (audio_source > AUDIO_SOURCE_MAX)) {
        ALOGE("%s: Invalid audio_source = %d", __func__, audio_source);
        return;
    }

    audio_source_delay_ms[audio_source] = delay_ms;
}

/* Delay in Us */
int64_t platform_get_audio_source_delay(audio_source_t audio_source)
{
    if ((audio_source < AUDIO_SOURCE_DEFAULT) ||
            (audio_source > AUDIO_SOURCE_MAX)) {
        ALOGE("%s: Invalid audio_source = %d", __func__, audio_source);
        return 0;
    }

    return 1000LL * audio_source_delay_ms[audio_source];
}

/* Delay in Us, only to be used for PCM formats */
int64_t platform_render_latency(struct stream_out *out)
{
    int64_t delay = 0LL;

    if (!out)
        return delay;
    switch (out->usecase) {
        case USECASE_AUDIO_PLAYBACK_DEEP_BUFFER:
        case USECASE_AUDIO_PLAYBACK_MEDIA:
        case USECASE_AUDIO_PLAYBACK_NAV_GUIDANCE:
        case USECASE_AUDIO_PLAYBACK_REAR_SEAT:
            delay = DEEP_BUFFER_PLATFORM_DELAY;
            break;
        case USECASE_AUDIO_PLAYBACK_LOW_LATENCY:
        case USECASE_AUDIO_PLAYBACK_WITH_HAPTICS:
        case USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION:
        case USECASE_AUDIO_PLAYBACK_FRONT_PASSENGER:
        case USECASE_AUDIO_PLAYBACK_PHONE:
            delay = LOW_LATENCY_PLATFORM_DELAY;
            break;
        case USECASE_AUDIO_PLAYBACK_OFFLOAD:
        case USECASE_AUDIO_PLAYBACK_OFFLOAD2:
            delay = PCM_OFFLOAD_PLATFORM_DELAY;
            break;
        case USECASE_AUDIO_PLAYBACK_ULL:
            delay = ULL_PLATFORM_DELAY;
            break;
        case USECASE_AUDIO_PLAYBACK_MMAP:
            delay = MMAP_PLATFORM_DELAY;
            break;
        default:
            break;
    }

    /* out->device could be used to add delay time if it's necessary */
    return delay;
}

int64_t platform_capture_latency(struct stream_in *in)
{
    int64_t delay = 0LL;

    if (!in)
        return delay;

    delay = platform_get_audio_source_delay(in->source);

    /* in->device could be used to add delay time if it's necessary */
    return delay;
}

int platform_update_usecase_from_source(int source, int usecase)
{
    ALOGV("%s: input source :%d", __func__, source);
    if (source == AUDIO_SOURCE_FM_TUNER)
        usecase = USECASE_AUDIO_RECORD_FM_VIRTUAL;
    return usecase;
}

bool platform_listen_device_needs_event(snd_device_t snd_device)
{
    bool needs_event = false;

    if ((snd_device >= SND_DEVICE_IN_BEGIN) &&
        (snd_device < SND_DEVICE_IN_END) &&
        (snd_device != SND_DEVICE_IN_CAPTURE_FM) &&
        (snd_device != SND_DEVICE_IN_CAPTURE_VI_FEEDBACK) &&
        (snd_device != SND_DEVICE_IN_CAPTURE_VI_FEEDBACK_MONO_1) &&
        (snd_device != SND_DEVICE_IN_CAPTURE_VI_FEEDBACK_MONO_2))
        needs_event = true;

    return needs_event;
}

bool platform_listen_usecase_needs_event(audio_usecase_t uc_id __unused)
{
    return false;
}

bool platform_sound_trigger_device_needs_event(snd_device_t snd_device)
{
    bool needs_event = false;

    if ((snd_device >= SND_DEVICE_IN_BEGIN) &&
        (snd_device < SND_DEVICE_IN_END) &&
        (snd_device != SND_DEVICE_IN_CAPTURE_FM) &&
        (snd_device != SND_DEVICE_IN_CAPTURE_VI_FEEDBACK) &&
        (snd_device != SND_DEVICE_IN_CAPTURE_VI_FEEDBACK_MONO_1) &&
        (snd_device != SND_DEVICE_IN_CAPTURE_VI_FEEDBACK_MONO_2))
        needs_event = true;

    return needs_event;
}

bool platform_sound_trigger_usecase_needs_event(audio_usecase_t uc_id)
{
    bool needs_event = false;

    switch(uc_id){
    /* concurrent playback usecases needs event */
    case USECASE_AUDIO_PLAYBACK_DEEP_BUFFER:
    case USECASE_AUDIO_PLAYBACK_MULTI_CH:
    case USECASE_AUDIO_PLAYBACK_OFFLOAD:
    case USECASE_AUDIO_PLAYBACK_OFFLOAD2:
    case USECASE_AUDIO_PLAYBACK_FM:
        needs_event = true;
        break;
    case USECASE_AUDIO_PLAYBACK_ULL:
    case USECASE_AUDIO_PLAYBACK_MMAP:
        if (property_get_bool("persist.vendor.audio.ull_playback_bargein",
            false))
            needs_event = true;
        break;
    case USECASE_AUDIO_PLAYBACK_LOW_LATENCY:
        if (property_get_bool("persist.vendor.audio.ll_playback_bargein",
            false))
            needs_event = true;
        break;

    /* concurrent capture usecases which needs event */
    case USECASE_AUDIO_RECORD:
    case USECASE_AUDIO_RECORD_LOW_LATENCY:
    case USECASE_AUDIO_RECORD_COMPRESS:
    case USECASE_AUDIO_RECORD_MMAP:
    case USECASE_AUDIO_RECORD_HIFI:
    case USECASE_VOICE_CALL:
    case USECASE_VOICE2_CALL:
    case USECASE_VOLTE_CALL:
    case USECASE_QCHAT_CALL:
    case USECASE_VOWLAN_CALL:
    case USECASE_VOICEMMODE1_CALL:
    case USECASE_VOICEMMODE2_CALL:
    case USECASE_COMPRESS_VOIP_CALL:
    case USECASE_INCALL_REC_UPLINK:
    case USECASE_INCALL_REC_DOWNLINK:
    case USECASE_INCALL_REC_UPLINK_AND_DOWNLINK:
    case USECASE_INCALL_REC_UPLINK_COMPRESS:
    case USECASE_INCALL_REC_DOWNLINK_COMPRESS:
    case USECASE_INCALL_REC_UPLINK_AND_DOWNLINK_COMPRESS:
    case USECASE_INCALL_MUSIC_UPLINK:
    case USECASE_INCALL_MUSIC_UPLINK2:
    case USECASE_AUDIO_RECORD_VOIP:
        needs_event = true;
        break;
    default:
        ALOGV("%s:usecase_id[%d] no need to raise event.", __func__, uc_id);
    }
    return needs_event;
}

/* Read  offload buffer size from a property.
 * If value is not power of 2  round it to
 * power of 2.
 */
uint32_t platform_get_compress_offload_buffer_size(audio_offload_info_t* info)
{
    char value[PROPERTY_VALUE_MAX] = {0};
    uint32_t fragment_size = COMPRESS_OFFLOAD_FRAGMENT_SIZE;
    uint32_t new_fragment_size = 0;
    int32_t duration_ms = 0;
    int channel_count = 0;
    if((property_get("vendor.audio.offload.buffer.size.kb", value, "")) &&
            atoi(value)) {
        fragment_size =  atoi(value) * 1024;
    }

    /* Use incoming offload buffer size if default buffer size is less */
    if ((info != NULL) && (fragment_size < info->offload_buffer_size)) {
        ALOGI("%s:: Overwriting offload buffer size default:%d new:%d", __func__,
              fragment_size,
              info->offload_buffer_size);
        fragment_size = info->offload_buffer_size;
    }

    /* Use client specified buffer size if mentioned */
    if (property_get_bool("vendor.audio.offload.buffer.duration.enabled", false)) {
        if ((info != NULL) &&
            (info->duration_us >= MIN_OFFLOAD_BUFFER_DURATION_MS) &&
            (info->duration_us <= MAX_OFFLOAD_BUFFER_DURATION_MS)) {
            duration_ms = info->duration_us / 1000;
            channel_count = audio_channel_count_from_in_mask(info->channel_mask);

            new_fragment_size = (duration_ms * info->sample_rate * channel_count *
                                     audio_bytes_per_sample(info->format)) / 1000;
            ALOGI("%s:: Overwriting offload buffer size with client requested size old:%d new:%d",
                                                       __func__, fragment_size, new_fragment_size);

            fragment_size = new_fragment_size;
        }
    }

    if (info != NULL) {
        if (info->is_streaming && info->has_video) {
            fragment_size = COMPRESS_OFFLOAD_FRAGMENT_SIZE_FOR_AV_STREAMING;
            ALOGV("%s: offload fragment size reduced for AV streaming to %d",
                   __func__, fragment_size);
        } else if (info->format == AUDIO_FORMAT_FLAC) {
            fragment_size = FLAC_COMPRESS_OFFLOAD_FRAGMENT_SIZE;
            ALOGV("FLAC fragment size %d", fragment_size);
        } else if (info->format == AUDIO_FORMAT_DSD) {
            fragment_size = MAX_COMPRESS_OFFLOAD_FRAGMENT_SIZE;
            if((property_get("vendor.audio.native.dsd.buffer.size.kb", value, "")) &&
                    atoi(value))
                fragment_size =  atoi(value) * 1024;
            ALOGV("DSD fragment size %d", fragment_size);
        }
    }

    fragment_size = ALIGN( fragment_size, 1024);

    if(fragment_size < MIN_COMPRESS_OFFLOAD_FRAGMENT_SIZE)
        fragment_size = MIN_COMPRESS_OFFLOAD_FRAGMENT_SIZE;
    else if(fragment_size > MAX_COMPRESS_OFFLOAD_FRAGMENT_SIZE)
        fragment_size = MAX_COMPRESS_OFFLOAD_FRAGMENT_SIZE;
    ALOGV("%s: fragment_size %d", __func__, fragment_size);
    return fragment_size;
}

/*
 * return backend_idx on which voice call is active
 */
static int platform_get_voice_call_backend(struct audio_device* adev)
{
   struct audio_usecase *uc = NULL;
   struct listnode *node;
   snd_device_t out_snd_device = SND_DEVICE_NONE;

   int backend_idx = -1;

   if (voice_is_in_call(adev) || adev->mode == AUDIO_MODE_IN_COMMUNICATION) {
       list_for_each(node, &adev->usecase_list) {
           uc =  node_to_item(node, struct audio_usecase, list);
           if (uc && uc->stream.out &&
               (uc->type == VOICE_CALL ||
                uc->type == VOIP_CALL ||
                uc->id == USECASE_AUDIO_PLAYBACK_VOIP)) {
               out_snd_device = platform_get_output_snd_device(adev->platform, uc->stream.out,
                                                               uc->type);
               backend_idx = platform_get_backend_index(out_snd_device);
               break;
           }
       }
   }
   return backend_idx;
}

bool platform_get_power_mode_on_device(void *platform, snd_device_t snd_device) {
    struct platform_data *my_data = (struct platform_data *)platform;

    ALOGD("%s:power mode status on snd_device = (%s %d)", __func__,
           platform_get_snd_device_name(snd_device),
           my_data->power_mode_cfg[snd_device].enable);
    return my_data->power_mode_cfg[snd_device].enable;

}

bool platform_get_island_cfg_on_device(void *platform, snd_device_t snd_device) {
    struct platform_data *my_data = (struct platform_data *)platform;

    ALOGD("%s:island cfg status on snd_device = (%s %d)", __func__,
           platform_get_snd_device_name(snd_device),
           my_data->island_cfg[snd_device].enable);
    return my_data->island_cfg[snd_device].enable;
}

int platform_set_power_mode_on_device(struct audio_device* adev,
                                      snd_device_t snd_device,
                                      bool enable)
{
    int ret = 0;
    struct  mixer_ctl *ctl;
    struct platform_data *my_data = (struct platform_data *)adev->platform;

    ctl = mixer_get_ctl_by_name(adev->mixer,
                                my_data->power_mode_cfg[snd_device].mixer_ctl);

    if (ctl) {
        ALOGD("%s:set power mode to %s",
               __func__, (enable == true) ? "true" : "false");
        mixer_ctl_set_value(ctl, 0, (int)enable);
    } else {
        ALOGE("%s:Could not get ctl for power mode mixer", __func__);
        ret = -EINVAL;
        goto error;
    }
    return ret;

error:
    my_data->power_mode_cfg[snd_device].enable = false;
    my_data->power_mode_cfg[snd_device].mixer_ctl = NULL;
    return ret;
}

int platform_set_island_cfg_on_device(struct audio_device* adev,
                                      snd_device_t snd_device,
                                      bool enable)
{
    int ret = 0;
    struct  mixer_ctl *ctl;
    struct platform_data *my_data = (struct platform_data *)adev->platform;

    ctl = mixer_get_ctl_by_name(adev->mixer,
                                my_data->island_cfg[snd_device].mixer_ctl);

    if (ctl) {
        ALOGD("%s:set island cfg to %s",
               __func__, (enable == true) ? "true" : "false");
        mixer_ctl_set_value(ctl, 0, (int)enable);
    } else {
        ALOGE("%s:Could not get ctl for island cfg mixer", __func__);
        return -EINVAL;
        goto error;
    }
    return ret;

error:
    my_data->island_cfg[snd_device].enable = false;
    my_data->island_cfg[snd_device].mixer_ctl = NULL;
    return ret;
}

char * platform_update_power_mode_mixer_ctrl(snd_device_t snd_device)
{
    char mixer_ctl[MIXER_PATH_MAX_LENGTH];
    char *power_mode_mixer_ctrl = NULL;
    char * be_itf = hw_interface_table[snd_device];

    if (be_itf != NULL) {
        snprintf(mixer_ctl, sizeof(mixer_ctl),
                 "%s Power Mode", be_itf);
        power_mode_mixer_ctrl = strdup(mixer_ctl);
        ALOGD("%s: power mode mixer ctrl %s\n",
              __func__, power_mode_mixer_ctrl);
    }

    return power_mode_mixer_ctrl;
}

char * platform_update_island_cfg_mixer_ctrl(snd_device_t snd_device)
{
    char mixer_ctl[MIXER_PATH_MAX_LENGTH];
    char *island_cfg_mixer_ctrl = NULL;
    char * be_itf = hw_interface_table[snd_device];

    if (be_itf != NULL) {
        snprintf(mixer_ctl, sizeof(mixer_ctl),
                 "%s Island Config", be_itf);
        island_cfg_mixer_ctrl = strdup(mixer_ctl);
        ALOGD("%s: island cfg mixer ctrl %s\n",
              __func__, island_cfg_mixer_ctrl);
    }

    return island_cfg_mixer_ctrl;
}

bool platform_check_and_update_island_power_status(void *platform,
                                          struct audio_usecase* usecase,
                                          snd_device_t snd_device)
{
    bool ret = false;
    struct platform_data *my_data = (struct platform_data *)platform;

    if (compare_device_type(&usecase->device_list, AUDIO_DEVICE_OUT_EARPIECE) ||
        compare_device_type(&usecase->device_list, AUDIO_DEVICE_OUT_WIRED_HEADSET) ||
        compare_device_type(&usecase->device_list, AUDIO_DEVICE_OUT_WIRED_HEADPHONE)) {
        if (snd_device >= SND_DEVICE_MIN && snd_device < SND_DEVICE_MAX) {
            /* update island and power mode in current device */
            my_data->island_cfg[snd_device].mixer_ctl =
                            platform_update_island_cfg_mixer_ctrl(snd_device);
            my_data->power_mode_cfg[snd_device].mixer_ctl =
                            platform_update_power_mode_mixer_ctrl(snd_device);
            if (my_data->island_cfg[snd_device].mixer_ctl != NULL &&
                my_data->power_mode_cfg[snd_device].mixer_ctl != NULL) {
                /* enable island and power mode in current device */
                my_data->island_cfg[snd_device].enable = true;
                my_data->power_mode_cfg[snd_device].enable = true;
                ret = true;
            }
        }
    }

    return ret;
}

void platform_reset_island_power_status(void *platform, snd_device_t snd_device)
{
     struct platform_data *my_data = (struct platform_data *)platform;

     my_data->island_cfg[snd_device].mixer_ctl = NULL;
     my_data->power_mode_cfg[snd_device].mixer_ctl = NULL;
     my_data->island_cfg[snd_device].enable = false;
     my_data->power_mode_cfg[snd_device].enable = false;
}

/*
 * configures afe with bit width and Sample Rate
 */
static int platform_set_codec_backend_cfg(struct audio_device* adev,
                                          struct audio_usecase *usecase,
                         snd_device_t snd_device, struct audio_backend_cfg backend_cfg)
{
    int ret = -EINVAL;
    int backend_idx = platform_get_backend_index(snd_device);
    struct platform_data *my_data = (struct platform_data *)adev->platform;
    unsigned int bit_width = backend_cfg.bit_width;
    unsigned int sample_rate = backend_cfg.sample_rate;
    unsigned int channels = backend_cfg.channels;
    audio_format_t format = backend_cfg.format;
    bool passthrough_enabled = backend_cfg.passthrough_enabled;
    struct audio_device_config_param *adev_device_cfg_ptr = adev->device_cfg_params;
    int controller = -1;
    int stream = -1;
    const char *id_string = NULL;
    int cfg_value = -1;

    if (usecase != NULL && usecase->stream.out &&
            usecase->type == PCM_PLAYBACK) {
        controller = usecase->stream.out->extconn.cs.controller;
        stream = usecase->stream.out->extconn.cs.stream;
    }

    /* Override the config params if client has already set them */
    adev_device_cfg_ptr += backend_idx;
    if (adev_device_cfg_ptr->use_client_dev_cfg) {
        ALOGV("%s::: Updating with the config set by client "
              "bitwidth %d, samplerate %d,  channels %d  format %d",
              __func__, adev_device_cfg_ptr->dev_cfg_params.bit_width,
              adev_device_cfg_ptr->dev_cfg_params.sample_rate,
              adev_device_cfg_ptr->dev_cfg_params.channels,
              adev_device_cfg_ptr->dev_cfg_params.format);

        bit_width = adev_device_cfg_ptr->dev_cfg_params.bit_width;
        sample_rate = adev_device_cfg_ptr->dev_cfg_params.sample_rate;
        channels = adev_device_cfg_ptr->dev_cfg_params.channels;
        format = adev_device_cfg_ptr->dev_cfg_params.format;
    }

    ALOGI("%s:becf: afe: bitwidth %d, samplerate %d channels %d format %d"
          ", backend_idx %d device (%s)", __func__,  bit_width,
          sample_rate, channels, format, backend_idx,
          platform_get_snd_device_name(snd_device));

    if ((my_data->current_backend_cfg[backend_idx].bitwidth_mixer_ctl) &&
        (bit_width != my_data->current_backend_cfg[backend_idx].bit_width)) {

        struct  mixer_ctl *ctl = NULL;
        ctl = mixer_get_ctl_by_name(adev->mixer,
                    my_data->current_backend_cfg[backend_idx].bitwidth_mixer_ctl);
        if (!ctl) {
            ALOGE("%s:becf: afe: Could not get ctl for mixer command - %s",
                  __func__,
                  my_data->current_backend_cfg[backend_idx].bitwidth_mixer_ctl);
            return -EINVAL;
        }

        if (bit_width == 24) {
            if (format == AUDIO_FORMAT_PCM_24_BIT_PACKED)
                 ret = mixer_ctl_set_enum_by_string(ctl, "S24_3LE");
            else
                 ret = mixer_ctl_set_enum_by_string(ctl, "S24_LE");
        } else if (bit_width == 32) {
            ret = mixer_ctl_set_enum_by_string(ctl, "S32_LE");
        } else {
            ret = mixer_ctl_set_enum_by_string(ctl, "S16_LE");
        }
        if (ret < 0) {
            ALOGE("%s:becf: afe: fail for %s mixer set to %d bit for %x format", __func__,
                  my_data->current_backend_cfg[backend_idx].bitwidth_mixer_ctl, bit_width, format);
        } else {
            ALOGD("%s:becf: afe: %s mixer set to %d bit for %x format", __func__,
                  my_data->current_backend_cfg[backend_idx].bitwidth_mixer_ctl, bit_width, format);
            for (int idx = 0; idx < MAX_CODEC_BACKENDS; idx++) {
                if (my_data->current_backend_cfg[idx].bitwidth_mixer_ctl) {
                    ctl = mixer_get_ctl_by_name(adev->mixer,
                                 my_data->current_backend_cfg[idx].bitwidth_mixer_ctl);
                    id_string = platform_get_mixer_control(ctl);
                    if (id_string) {
                        cfg_value = audio_extn_utils_get_bit_width_from_string(id_string);
                        if (cfg_value > 0)
                            my_data->current_backend_cfg[idx].bit_width = cfg_value;
                    }
                }
            }
        }
        /* set the ret as 0 and not pass back to upper layer */
        ret = 0;
    }

    if ((my_data->current_backend_cfg[backend_idx].samplerate_mixer_ctl) &&
        (passthrough_enabled || (sample_rate != my_data->current_backend_cfg[backend_idx].sample_rate))) {
        char *rate_str = NULL;
        struct  mixer_ctl *ctl = NULL;

        if (backend_idx == USB_AUDIO_RX_BACKEND ||
                backend_idx == USB_AUDIO_TX_BACKEND) {
            switch (sample_rate) {
            case 32000:
                    rate_str = "KHZ_32";
                    break;
            case 8000:
                    rate_str = "KHZ_8";
                    break;
            case 11025:
                    rate_str = "KHZ_11P025";
                    break;
            case 16000:
                    rate_str = "KHZ_16";
                    break;
            case 22050:
                    rate_str = "KHZ_22P05";
                    break;
            }
        }

        if (rate_str == NULL) {
            switch (sample_rate) {
            case 32000:
                if (passthrough_enabled || (backend_idx == SPDIF_TX_BACKEND) ||
                    (backend_idx == HDMI_TX_BACKEND) ||
                    (backend_idx == HDMI_ARC_TX_BACKEND) ||
                    (backend_idx == DISP_PORT_RX_BACKEND)) {
                    rate_str = "KHZ_32";
                    break;
                }
            case 48000:
                rate_str = "KHZ_48";
                break;
            case 44100:
                rate_str = "KHZ_44P1";
                break;
            case 64000:
            case 96000:
                rate_str = "KHZ_96";
                break;
            case 88200:
                rate_str = "KHZ_88P2";
                break;
            case 176400:
                rate_str = "KHZ_176P4";
                break;
            case 192000:
                rate_str = "KHZ_192";
                break;
            case 352800:
                rate_str = "KHZ_352P8";
                break;
            case 384000:
                rate_str = "KHZ_384";
                break;
            case 144000:
                if (passthrough_enabled) {
                    rate_str = "KHZ_144";
                    break;
                }
            default:
                rate_str = "KHZ_48";
                break;
            }
        }

        ctl = mixer_get_ctl_by_name(adev->mixer,
            my_data->current_backend_cfg[backend_idx].samplerate_mixer_ctl);
        if(!ctl) {
            ALOGE("%s:becf: afe: Could not get ctl for mixer command - %s",
                  __func__,
                  my_data->current_backend_cfg[backend_idx].samplerate_mixer_ctl);
            return -EINVAL;
        }

        ret = mixer_ctl_set_enum_by_string(ctl, rate_str);
        if (ret < 0) {
            ALOGE("%s:becf: afe: fail for %s mixer set to %s", __func__,
                  my_data->current_backend_cfg[backend_idx].samplerate_mixer_ctl, rate_str);
        } else {
            ALOGD("%s:becf: afe: %s set to %s", __func__,
                  my_data->current_backend_cfg[backend_idx].samplerate_mixer_ctl, rate_str);
            for (int idx = 0; idx < MAX_CODEC_BACKENDS; idx++) {
                if (my_data->current_backend_cfg[idx].samplerate_mixer_ctl) {
                    ctl = mixer_get_ctl_by_name(adev->mixer,
                                 my_data->current_backend_cfg[idx].samplerate_mixer_ctl);
                    id_string = platform_get_mixer_control(ctl);
                    if (id_string) {
                        cfg_value = audio_extn_utils_get_sample_rate_from_string(id_string);
                        if (cfg_value > 0)
                            my_data->current_backend_cfg[idx].sample_rate = cfg_value;
                    }
                }
            }
        }
        ret = 0;
    }
    if ((my_data->current_backend_cfg[backend_idx].channels_mixer_ctl) &&
        (channels != my_data->current_backend_cfg[backend_idx].channels)) {
        struct  mixer_ctl *ctl = NULL;
        char *channel_cnt_str = NULL;

        switch (channels) {
        case 8:
            channel_cnt_str = "Eight"; break;
        case 7:
            channel_cnt_str = "Seven"; break;
        case 6:
            channel_cnt_str = "Six"; break;
        case 5:
            channel_cnt_str = "Five"; break;
        case 4:
            channel_cnt_str = "Four"; break;
        case 3:
            channel_cnt_str = "Three"; break;
        case 1:
            channel_cnt_str = "One"; break;
        case 2:
        default:
            channel_cnt_str = "Two"; break;
        }

        ctl = mixer_get_ctl_by_name(adev->mixer,
           my_data->current_backend_cfg[backend_idx].channels_mixer_ctl);
        if (!ctl) {
            ALOGE("%s:becf: afe: Could not get ctl for mixer command - %s",
                   __func__,
                   my_data->current_backend_cfg[backend_idx].channels_mixer_ctl);
            return -EINVAL;
        }
        ret = mixer_ctl_set_enum_by_string(ctl, channel_cnt_str);
        if (ret < 0) {
            ALOGE("%s:becf: afe: fail for %s mixer set to %s", __func__,
                  my_data->current_backend_cfg[backend_idx].channels_mixer_ctl, channel_cnt_str);
        } else {
            ALOGD("%s:becf: afe: %s set to %s", __func__,
                  my_data->current_backend_cfg[backend_idx].channels_mixer_ctl, channel_cnt_str);
            for (int idx = 0; idx < MAX_CODEC_BACKENDS; idx++) {
                if (my_data->current_backend_cfg[idx].channels_mixer_ctl) {
                    ctl = mixer_get_ctl_by_name(adev->mixer,
                                 my_data->current_backend_cfg[idx].channels_mixer_ctl);
                    id_string = platform_get_mixer_control(ctl);
                    if (id_string) {
                        cfg_value = audio_extn_utils_get_channels_from_string(id_string);
                        if (cfg_value > 0)
                            my_data->current_backend_cfg[idx].channels = cfg_value;
                    }
                }
            }
        }
        ret = 0;

        if ((backend_idx == HDMI_RX_BACKEND) ||
                (backend_idx == DISP_PORT_RX_BACKEND) ||
                (backend_idx == DISP_PORT1_RX_BACKEND))
            platform_set_edid_channels_configuration_v2(adev->platform, channels,
                                                     backend_idx, snd_device,
                                                     controller, stream);
    }

    bool set_ext_disp_format = false, set_mi2s_tx_data_format = false;
    char *ext_disp_format = NULL;
    bool set_ext_disp_device_idx = false;

    if (backend_idx == HDMI_RX_BACKEND) {
        ext_disp_format = "HDMI RX Format";
        set_ext_disp_format = true;
    } else if (backend_idx == DISP_PORT_RX_BACKEND) {
        ext_disp_format = "Display Port RX Format";
        set_ext_disp_format = true;
        set_ext_disp_device_idx = true;
    } else if (backend_idx == DISP_PORT1_RX_BACKEND) {
        ext_disp_format = "Display Port1 RX Format";
        set_ext_disp_format = true;
        set_ext_disp_device_idx = true;
    } else if (backend_idx == SPDIF_TX_BACKEND) {
        ext_disp_format = "PRI SPDIF TX Format";
        set_mi2s_tx_data_format = true;
    } else if (backend_idx == HDMI_TX_BACKEND) {
        if (!strncmp(platform_get_snd_device_backend_interface(snd_device),
            "SEC_MI2S_TX", sizeof("SEC_MI2S_TX")))
            ext_disp_format = "SEC MI2S TX Format";
        else
            ext_disp_format = "QUAT MI2S TX Format";
        set_mi2s_tx_data_format = true;
    } else if (backend_idx == HDMI_ARC_TX_BACKEND) {
        ext_disp_format = "SEC SPDIF TX Format";
        set_mi2s_tx_data_format = true;
    } else {
        ALOGV("%s: Format doesnt have to be set", __func__);
    }

    /* Set data format only if there is a change from PCM to compressed
       and vice versa */
    if (set_mi2s_tx_data_format && (format ^ my_data->current_backend_cfg[backend_idx].format)) {
        struct mixer_ctl *ctl = mixer_get_ctl_by_name(adev->mixer, ext_disp_format);
        if (!ctl) {
            ALOGE("%s:becf: afe: Could not get ctl for mixer command - %s",
                  __func__, ext_disp_format);
            return -EINVAL;
        }
        if ((format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_PCM) {
            ALOGE("%s:Set %s to LPCM", __func__, ext_disp_format);
            mixer_ctl_set_enum_by_string(ctl, "LPCM");
        } else {
            ALOGE("%s:Set %s to Compr", __func__, ext_disp_format);
            mixer_ctl_set_enum_by_string(ctl, "Compr");
        }
        my_data->current_backend_cfg[backend_idx].format = format;
    }
    if (set_ext_disp_device_idx && usecase) {
        struct mixer_ctl *ctl;
        int ctl_index = 0;
        const char *ctl_name_prefix = "Display Port";
        const char *ctl_name_suffix = "RX DEVICE IDX";
        char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};
        int dev[] = {controller, stream};

        ctl_index = platform_get_display_port_ctl_index(controller, stream);
        if (-EINVAL == ctl_index) {
            ALOGE("%s: Unknown controller/stream %d/%d",
                  __func__, controller, stream);
            return -EINVAL;
        }

        if (0 == ctl_index)
            snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                     "%s %s", ctl_name_prefix, ctl_name_suffix);
        else
            snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                     "%s%d %s", ctl_name_prefix, ctl_index, ctl_name_suffix);

        ALOGV("%s: mixer ctl name: %s", __func__, mixer_ctl_name);

        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s:becf: afe: Could not get ctl for mixer command - %s",
                  __func__, ext_disp_format);
            return -EINVAL;
        }

        mixer_ctl_set_array(ctl, dev, ARRAY_SIZE(dev));
        my_data->current_backend_cfg[backend_idx].controller = controller;
        my_data->current_backend_cfg[backend_idx].stream = stream;
    }
    if (set_ext_disp_format) {
        struct mixer_ctl *ctl = mixer_get_ctl_by_name(adev->mixer, ext_disp_format);
        if (!ctl) {
            ALOGE("%s:becf: afe: Could not get ctl for mixer command - %s",
                  __func__, ext_disp_format);
            return -EINVAL;
        }

        if (passthrough_enabled) {
            ALOGD("%s:Ext display compress format", __func__);
            mixer_ctl_set_enum_by_string(ctl, "Compr");
        } else {
            ALOGD("%s: Ext display PCM format", __func__);
            mixer_ctl_set_enum_by_string(ctl, "LPCM");
        }
        ret = 0;
    }
    return ret;
}

/*
 * Get the backend configuration for current snd device
 */
int platform_get_codec_backend_cfg(struct audio_device* adev,
                         snd_device_t snd_device,
                         struct audio_backend_cfg *backend_cfg)
{
    int backend_idx = platform_get_backend_index(snd_device);
    struct platform_data *my_data = (struct platform_data *)adev->platform;

    backend_cfg->bit_width = my_data->current_backend_cfg[backend_idx].bit_width;
    backend_cfg->sample_rate =
                       my_data->current_backend_cfg[backend_idx].sample_rate;
    backend_cfg->channels =
                       my_data->current_backend_cfg[backend_idx].channels;
    backend_cfg->format =
                       my_data->current_backend_cfg[backend_idx].format;

    ALOGV("%s:becf: afe: bitwidth %d, samplerate %d channels %d format %d"
          ", backend_idx %d device (%s)", __func__,  backend_cfg->bit_width,
          backend_cfg->sample_rate, backend_cfg->channels, backend_cfg->format,
          backend_idx, platform_get_snd_device_name(snd_device));

   return 0;
}

/*
 *Validate the selected bit_width, sample_rate and channels using the edid
 *of the connected sink device.
 */
static void platform_check_hdmi_backend_cfg(struct audio_device* adev,
                                   struct audio_usecase* usecase,
                                   int backend_idx,
                                   struct audio_backend_cfg *hdmi_backend_cfg)
{
    unsigned int bit_width;
    unsigned int sample_rate;
    int channels, max_supported_channels = 0;
    struct platform_data *my_data = (struct platform_data *)adev->platform;
    edid_audio_info *edid_info = NULL;
    bool passthrough_enabled = false;
    int controller = -1;
    int stream = -1;
    uint32_t compr_passthr = 0;

    if (!usecase) {
        ALOGE("%s: becf: HDMI: usecase is NULL", __func__);
        return;
    }

    controller = usecase->stream.out->extconn.cs.controller;
    stream = usecase->stream.out->extconn.cs.stream;

    if (controller < 0 || controller >= MAX_CONTROLLERS ||
            stream < 0 || stream >= MAX_STREAMS_PER_CONTROLLER) {
        controller = 0;
        stream = 0;
    }

    edid_info = (edid_audio_info *)my_data->ext_disp[controller][stream].edid_info;
    bit_width = hdmi_backend_cfg->bit_width;
    sample_rate = hdmi_backend_cfg->sample_rate;
    channels = hdmi_backend_cfg->channels;


    ALOGI("%s:becf: HDMI: bitwidth %d, samplerate %d, channels %d"
          ", usecase = %d", __func__, bit_width,
          sample_rate, channels, usecase->id);

    if (is_offload_usecase(usecase->id)) {
#ifdef AUDIO_GKI_ENABLED
        /* out->compr_config.codec->reserved[0] is for compr_passthr */
        compr_passthr = usecase->stream.out->compr_config.codec->reserved[0];
#else
        compr_passthr = usecase->stream.out->compr_config.codec->compr_passthr;
#endif
    }

    if (audio_extn_passthru_is_enabled() && audio_extn_passthru_is_active()
        && (compr_passthr != 0)) {
        passthrough_enabled = true;
        ALOGI("passthrough is enabled for this stream");
    }

    // For voice calls use default configuration i.e. 16b/48K, only applicable to
    // default backend
    if (!passthrough_enabled) {

        max_supported_channels = platform_edid_get_max_channels_v2(my_data,
                                                                controller, stream);

        //Check EDID info for supported samplerate
        if (!audio_extn_edid_is_supported_sr(edid_info,sample_rate)) {
            //check to see if current BE sample rate is supported by EDID
            //else assign the highest sample rate supported by EDID
            if (audio_extn_edid_is_supported_sr(edid_info,my_data->current_backend_cfg[backend_idx].sample_rate))
                sample_rate = my_data->current_backend_cfg[backend_idx].sample_rate;
            else
                sample_rate = audio_extn_edid_get_highest_supported_sr(edid_info);
        }

        //Check EDID info for supported bit width
        if (!audio_extn_edid_is_supported_bps(edid_info,bit_width)) {
            //reset to current sample rate
            bit_width = my_data->current_backend_cfg[backend_idx].bit_width;
        }

        if (channels > max_supported_channels)
            channels = max_supported_channels;

    } else {
        channels = audio_extn_passthru_get_channel_count(usecase->stream.out);
        if (channels <= 0) {
            ALOGE("%s: becf: afe: HDMI backend using defalut channel %u",
                  __func__, DEFAULT_HDMI_OUT_CHANNELS);
            channels = DEFAULT_HDMI_OUT_CHANNELS;
        }
        if (((usecase->stream.out->format == AUDIO_FORMAT_E_AC3) ||
            (usecase->stream.out->format == AUDIO_FORMAT_E_AC3_JOC) ||
            (usecase->stream.out->format == AUDIO_FORMAT_DOLBY_TRUEHD))
            && (compr_passthr == PASSTHROUGH)) {
            sample_rate = sample_rate * 4;
            if (sample_rate > HDMI_PASSTHROUGH_MAX_SAMPLE_RATE)
                sample_rate = HDMI_PASSTHROUGH_MAX_SAMPLE_RATE;
        }

        bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
        /* We force route so that the BE format can be set to Compr */
    }

    ALOGI("%s:becf: afe: HDMI backend: passthrough %d updated bit width: %d and sample rate: %d"
           "channels %d", __func__, passthrough_enabled , bit_width,
           sample_rate, channels);

    hdmi_backend_cfg->bit_width = bit_width;
    hdmi_backend_cfg->sample_rate = sample_rate;
    hdmi_backend_cfg->channels = channels;
    hdmi_backend_cfg->passthrough_enabled = passthrough_enabled;
}

/*
 * goes through all the current usecases and picks the highest
 * bitwidth & samplerate
 */
static bool platform_check_codec_backend_cfg(struct audio_device* adev,
                                   struct audio_usecase* usecase,
                                   snd_device_t snd_device,
                                   struct audio_backend_cfg *backend_cfg)
{
    bool backend_change = false;
    struct listnode *node;
    unsigned int bit_width;
    unsigned int sample_rate;
    unsigned int channels;
    unsigned long service_interval = 0;
    bool service_interval_update = false;
    bool passthrough_enabled = false;
    bool voice_call_active = false;
    int backend_idx = DEFAULT_CODEC_BACKEND;
    struct platform_data *my_data = (struct platform_data *)adev->platform;
    int na_mode = platform_get_native_support();
    bool channels_updated = false;
    bool display_port_updated = false;
    struct audio_device_config_param *adev_device_cfg_ptr = adev->device_cfg_params;
    int controller = -1;
    int stream = -1;


    /*BT devices backend is not configured from HAL hence skip*/
    if (snd_device == SND_DEVICE_OUT_BT_A2DP ||
        snd_device == SND_DEVICE_OUT_BT_SCO ||
        snd_device == SND_DEVICE_OUT_BT_SCO_WB ||
        snd_device == SND_DEVICE_OUT_BT_SCO_SWB ||
        snd_device == SND_DEVICE_IN_BT_A2DP ||
        snd_device == SND_DEVICE_OUT_AFE_PROXY) {
        backend_change = false;
        return backend_change;
    }

    controller = usecase->stream.out->extconn.cs.controller;
    stream = usecase->stream.out->extconn.cs.stream;

    backend_idx = platform_get_backend_index(snd_device);

    bit_width = backend_cfg->bit_width;
    sample_rate = backend_cfg->sample_rate;
    channels = backend_cfg->channels;

    ALOGI("%s:becf: afe: bitwidth %d, samplerate %d channels %d"
          ", backend_idx %d usecase = %d device (%s)", __func__, bit_width,
          sample_rate, channels, backend_idx, usecase->id,
          platform_get_snd_device_name(snd_device));

    // For voice calls use default configuration i.e. 16b/48K, only applicable to
    // default backend
    // force routing is not required here, caller will do it anyway
    if (backend_idx == platform_get_voice_call_backend(adev)) {
        ALOGW("%s:becf: afe:Use default bw and sr for voice/voip calls ",
              __func__);
        bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
        sample_rate =  CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
        channels = CODEC_BACKEND_DEFAULT_CHANNELS;
        voice_call_active = true;
    } else {
        /*
         * The backend should be configured at highest bit width and/or
         * sample rate amongst all playback usecases.
         * If the selected sample rate and/or bit width differ with
         * current backend sample rate and/or bit width, then, we set the
         * backend re-configuration flag.
         *
         * Exception: 16 bit playbacks is allowed through 16 bit/48/44.1 khz backend only
         */
        int i =0;
        list_for_each(node, &adev->usecase_list) {
            struct audio_usecase *uc;
            uc = node_to_item(node, struct audio_usecase, list);
            struct stream_out *out = (struct stream_out*) uc->stream.out;
            if (uc->type == PCM_PLAYBACK && out && usecase != uc) {
                unsigned int out_channels = audio_channel_count_from_out_mask(out->channel_mask);

                ALOGD("%s:napb: (%d) - (%s)id (%d) sr %d bw "
                      "(%d) ch (%d) device %s", __func__, i++, use_case_table[uc->id],
                      uc->id, out->sample_rate,
                      out->bit_width, out_channels,
                      platform_get_snd_device_name(uc->out_snd_device));

                if (platform_check_backends_match(snd_device, uc->out_snd_device)) {
                        if (bit_width < out->bit_width)
                            bit_width = out->bit_width;
                        if (sample_rate < out->sample_rate)
                            sample_rate = out->sample_rate;
                        /*
                         * TODO: Add Support for Backend configuration for devices which support
                         * sample rate less than 44.1
                         */
                        if (sample_rate < OUTPUT_SAMPLING_RATE_44100)
                            sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
                        if (channels < out_channels)
                            channels = out_channels;
                }
                if ((snd_device == SND_DEVICE_OUT_HEADPHONES_HIFI_FILTER) &&
                    (usecase->id==USECASE_AUDIO_PLAYBACK_LOW_LATENCY ||
                      usecase->id == USECASE_AUDIO_PLAYBACK_ULL)) {
                       sample_rate = my_data->current_backend_cfg[backend_idx].sample_rate;
                       bit_width = my_data->current_backend_cfg[backend_idx].bit_width;
                }
            }
        }
    }

    /* Native playback is preferred for Headphone/HS device over 192Khz */
    if (!voice_call_active && codec_device_supports_native_playback(&usecase->device_list)) {
        if (audio_is_true_native_stream_active(adev)) {
            if (check_hdset_combo_device(snd_device)) {
                /*
                 * In true native mode Tasha has a limitation that one port at 44.1 khz
                 * cannot drive both spkr and hdset, to simiplify the solution lets
                 * move the AFE to 48khzwhen a ring tone selects combo device.
                 * or if NATIVE playback is not enabled.
                 */
                    sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
                    bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
                    ALOGD("%s:becf: afe: port to run at 48k if combo device or in voice call"
                           , __func__);
            } else {
             /*
              * in single BE mode, if native audio playback
              * is active then it will take priority
              */
                 sample_rate = OUTPUT_SAMPLING_RATE_44100;
                 ALOGD("%s:becf: afe: true napb active set rate to 44.1 khz",
                       __func__);
            }
        } else if (na_mode == NATIVE_AUDIO_MODE_MULTIPLE_MIX_IN_DSP) {
            struct listnode *node;
            list_for_each(node, &adev->usecase_list) {
                struct audio_usecase *uc;
                uc = node_to_item(node, struct audio_usecase, list);
                struct stream_out *curr_out =
                    (struct stream_out*) uc->stream.out;

                /*if native audio playback
                * is active then it will take priority
                */
                if (curr_out && PCM_PLAYBACK == uc->type) {
                    if (is_offload_usecase(uc->id) &&
                        (curr_out->sample_rate % OUTPUT_SAMPLING_RATE_44100 == 0)) {
                        ALOGD("%s:napb:native stream detected %d sampling rate", __func__, curr_out->sample_rate);
                        sample_rate = curr_out->sample_rate;
                    }
                }

                /* WCD9380 support SR upto 192Khz only, hence reset
                 * SR > 192Khz to 192Khz.
                 */
                if (strstr(my_data->codec_variant, "WCD9380")) {
                    switch (sample_rate) {
                        case 352800:
                        case 384000:
                            sample_rate = 192000;
                            ALOGD("%s:Reset Sampling rate to %d",  __func__, sample_rate);
                            break;
                        default:
                            break;
                    }
                }
            }
        } else if (na_mode != NATIVE_AUDIO_MODE_MULTIPLE_MIX_IN_CODEC) {
            /*
             * Map native sampling rates to upper limit range
             * if multiple of native sampling rates are not supported.
             * This check also indicates that this is not tavil codec
             * And 32bit/384kHz is only supported on tavil
             * Hence reset 32b/384kHz to 24b/192kHz.
             */
            switch (sample_rate) {
                case 44100:
                    sample_rate = 48000;
                    break;
                case 88200:
                    sample_rate = 96000;
                    break;
                case 176400:
                case 352800:
                case 384000:
                    sample_rate = 192000;
                    break;
            }
            if (bit_width > 24)
                bit_width = 24;

            ALOGD("%s:becf: afe: napb not active - set non fractional rate",
                       __func__);
        }
        /*
         * reset sample rate to 48khz if sample rate less than 44.1khz, or device backend does not
         * support 44.1 khz and the multiple of 44.1khz
         */
        if ((sample_rate % OUTPUT_SAMPLING_RATE_44100 == 0 &&
             backend_idx != HEADPHONE_44_1_BACKEND &&
             backend_idx != HEADPHONE_BACKEND &&
             backend_idx != USB_AUDIO_RX_BACKEND) ||
            sample_rate < OUTPUT_SAMPLING_RATE_44100) {
            sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
            ALOGD("%s:becf: afe: set sample rate to default Sample Rate(48k)",__func__);
        }

        /*set sample rate to 48khz if multiple sample rates are not supported in spkr and hdset*/
        if (is_hdset_combo_device(&usecase->device_list) &&
            !my_data->is_multiple_sample_rate_combo_supported)
            sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
            ALOGD("%s:becf: afe: set default Sample Rate(48k) for combo device",__func__);
    }

    if (backend_idx != platform_get_voice_call_backend(adev)
        && usecase->type == PCM_PLAYBACK) {
        struct stream_out *out = (struct stream_out*) usecase->stream.out;
        if(audio_extn_is_hifi_filter_enabled(adev, out, snd_device,
            my_data->codec_variant, channels, 0))  {
            switch (sample_rate) {
                case 48000:
                    audio_extn_enable_hifi_filter(adev, true);
                    if (audio_is_true_native_stream_active(adev))
                        sample_rate = 352800;
                    else
                        sample_rate = 384000;
                    bit_width = 32;
                    break;
                case 44100:
                    audio_extn_enable_hifi_filter(adev, true);
                    sample_rate = 352800;
                    bit_width = 32;
                    break;
                default:
                    audio_extn_enable_hifi_filter(adev, false);
            }
        }
        if (snd_device != SND_DEVICE_OUT_HEADPHONES_HIFI_FILTER)
            audio_extn_enable_hifi_filter(adev, false);
        ALOGD("%s:becf: updated afe: bitwidth %d, samplerate %d channels %d,"
            "backend_idx %d usecase = %d device (%s)", __func__, bit_width,
            sample_rate, channels, backend_idx, usecase->id,
            platform_get_snd_device_name(snd_device));
    }

    /*
     * Handset and speaker may have diffrent backend. Check if the device is speaker or handset,
     * and these devices are restricited to 48kHz.
     */
    if (!codec_device_supports_native_playback(&usecase->device_list) &&
        (platform_check_backends_match(SND_DEVICE_OUT_SPEAKER, snd_device) ||
         platform_check_backends_match(SND_DEVICE_OUT_HANDSET, snd_device))) {
        int bw = platform_get_snd_device_bit_width(SND_DEVICE_OUT_SPEAKER);
        if ((-ENOSYS != bw) && (bit_width > (uint32_t)bw)) {
            bit_width = (uint32_t)bw;
            ALOGD("%s:becf: afe: reset bitwidth to %d (based on supported"
                  " value for this platform)", __func__, bit_width);
        } else if (-ENOSYS == bw) {
            bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
            ALOGD("%s:becf: afe: reset to default bitwidth %d", __func__, bit_width);
        }
        /*
         * In case of CSRA speaker out, all sample rates are supported, so
         *  check platform here
         */
        if (platform_spkr_use_default_sample_rate(adev->platform)) {
            sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
            ALOGD("%s:becf: afe: playback on codec device not supporting native playback set "
            "default Sample Rate(48k)", __func__);
        }
    }

    if (backend_idx == USB_AUDIO_RX_BACKEND) {
        audio_extn_usb_is_config_supported(&bit_width, &sample_rate, &channels, true);
        ALOGV("%s: USB BE configured as bit_width(%d)sample_rate(%d)channels(%d)",
                   __func__, bit_width, sample_rate, channels);

        if (audio_extn_usb_get_service_interval(true,
                                                &service_interval) == 0) {
            /* overwrite with best altset for this service interval */
            int ret =
                    audio_extn_usb_altset_for_service_interval(true /*playback*/,
                                                               service_interval,
                                                               &bit_width,
                                                               &sample_rate,
                                                               &channels);
            ALOGD("%s: Override USB BE configured as bit_width(%d)sample_rate(%d)channels(%d)SI(%lu)",
           __func__, bit_width, sample_rate, channels, service_interval);
            if (ret < 0) {
                ALOGW("Failed to find altset for service interval %lu, skip reconfig",
                      service_interval);
                return false;
            }
            service_interval_update = audio_extn_usb_is_reconfig_req();
            audio_extn_usb_set_reconfig(false);
        }

        if (channels != my_data->current_backend_cfg[backend_idx].channels)
            channels_updated = true;
    }

    if (backend_idx == HDMI_RX_BACKEND || backend_idx == DISP_PORT_RX_BACKEND ||
             backend_idx == DISP_PORT1_RX_BACKEND) {
        struct audio_backend_cfg hdmi_backend_cfg;
        hdmi_backend_cfg.bit_width = bit_width;
        hdmi_backend_cfg.sample_rate = sample_rate;
        hdmi_backend_cfg.channels = channels;
        hdmi_backend_cfg.passthrough_enabled = false;

        /*
         * HDMI does not support 384Khz/32bit playback hence configure BE to 24b/192Khz
         * TODO: Instead have the validation against edid return the next best match
         */
        if (bit_width > 24)
            hdmi_backend_cfg.bit_width = 24;
        if (sample_rate > 192000)
            hdmi_backend_cfg.sample_rate = 192000;

        platform_check_hdmi_backend_cfg(adev, usecase, backend_idx, &hdmi_backend_cfg);

        bit_width = hdmi_backend_cfg.bit_width;
        sample_rate = hdmi_backend_cfg.sample_rate;
        channels = hdmi_backend_cfg.channels;
        passthrough_enabled = hdmi_backend_cfg.passthrough_enabled;

        if (channels != my_data->current_backend_cfg[backend_idx].channels)
            channels_updated = true;

        platform_set_edid_channels_configuration_v2(adev->platform, channels,
                                                 backend_idx, snd_device,
                                                 controller, stream);

        if ((controller != my_data->current_backend_cfg[backend_idx].controller) ||
             (stream != my_data->current_backend_cfg[backend_idx].stream)) {
            ALOGD("%s:becf: afe: display port changed", __func__);
            display_port_updated = true;
        }
    }

    ALOGI("%s:becf: afe: Codec selected backend: %d updated bit width: %d and sample rate: %d",
          __func__, backend_idx , bit_width, sample_rate);

    // Force routing if the expected bitwdith or samplerate
    // is not same as current backend comfiguration
    if ((bit_width != my_data->current_backend_cfg[backend_idx].bit_width) ||
        (sample_rate != my_data->current_backend_cfg[backend_idx].sample_rate) ||
         passthrough_enabled || channels_updated || service_interval_update ||
         display_port_updated) {
        backend_cfg->bit_width = bit_width;
        backend_cfg->sample_rate = sample_rate;
        backend_cfg->channels = channels;
        backend_cfg->passthrough_enabled = passthrough_enabled;
        backend_change = true;
        ALOGI("%s:becf: afe: Codec backend needs to be updated. new bit width: %d"
               "new sample rate: %d new channels: %d",
              __func__, backend_cfg->bit_width, backend_cfg->sample_rate, backend_cfg->channels);
    }

    // Force routing if the client sends config params for this backend
    adev_device_cfg_ptr += backend_idx;
    if (adev_device_cfg_ptr->use_client_dev_cfg) {
        ALOGV("%s: Codec backend needs to be updated as Client provided "
              "config params", __func__);
        backend_change = true;
    }

    if (snd_device == SND_DEVICE_OUT_HEADPHONES || snd_device ==
        SND_DEVICE_OUT_HEADPHONES_44_1 || snd_device == SND_DEVICE_OUT_HEADPHONES_HIFI_FILTER) {
        if (my_data->is_cls_ab_only_supported) {
           ALOGI("%s: apply CLS AB HPH power mode\n", __func__);
           audio_route_apply_and_update_path(adev->audio_route, "hph-class-ab-mode");
        } else {
            if (sample_rate > 48000 ||
                (bit_width >= 24 && (sample_rate == 48000  || sample_rate == 44100))) {
                ALOGI("%s: apply HPH HQ mode\n", __func__);
                audio_route_apply_and_update_path(adev->audio_route, "hph-highquality-mode");
            } else {
                ALOGI("%s: apply HPH LP mode\n", __func__);
                audio_route_apply_and_update_path(adev->audio_route, "hph-lowpower-mode");
            }
        }
    }

    return backend_change;
}

bool platform_check_and_set_codec_backend_cfg(struct audio_device* adev,
    struct audio_usecase *usecase, snd_device_t snd_device)
{
    int backend_idx = DEFAULT_CODEC_BACKEND;
    int new_snd_devices[SND_DEVICE_OUT_END] = {0};
    int i, num_devices = 1;
    int device_be_idx = -1;
    bool ret = false;
    struct platform_data *my_data = (struct platform_data *)adev->platform;
    struct audio_backend_cfg backend_cfg;

    backend_idx = platform_get_backend_index(snd_device);
    device_be_idx = platform_get_snd_device_backend_index(snd_device);

    if (usecase->type == TRANSCODE_LOOPBACK_RX) {
        backend_cfg.bit_width = usecase->stream.inout->out_config.bit_width;
        backend_cfg.sample_rate = usecase->stream.inout->out_config.sample_rate;
        backend_cfg.format = usecase->stream.inout->out_config.format;
        backend_cfg.channels = audio_channel_count_from_out_mask(
                usecase->stream.inout->out_config.channel_mask);
    } else {
        backend_cfg.bit_width = usecase->stream.out->bit_width;
        backend_cfg.sample_rate = usecase->stream.out->sample_rate;
        backend_cfg.format = usecase->stream.out->format;
        backend_cfg.channels = audio_channel_count_from_out_mask(usecase->stream.out->channel_mask);
        backend_cfg.controller = 0;
        backend_cfg.stream = 0;
    }
    if (audio_extn_is_dsp_bit_width_enforce_mode_supported(usecase->stream.out->flags) &&
                (adev->dsp_bit_width_enforce_mode > backend_cfg.bit_width))
        backend_cfg.bit_width = adev->dsp_bit_width_enforce_mode;

    /*this is populated by check_codec_backend_cfg hence set default value to false*/
    backend_cfg.passthrough_enabled = false;

    /* Set Backend sampling rate to 176.4 for DSD64 and
     * 352.8Khz for DSD128.
     * Set Bit Width to 16
     */
    if ((backend_idx == DSD_NATIVE_BACKEND) && (backend_cfg.format == AUDIO_FORMAT_DSD)) {
        backend_cfg.bit_width = 16;
        if (backend_cfg.sample_rate == INPUT_SAMPLING_RATE_DSD64)
            backend_cfg.sample_rate = OUTPUT_SAMPLING_RATE_DSD64;
        else if (backend_cfg.sample_rate == INPUT_SAMPLING_RATE_DSD128)
            backend_cfg.sample_rate = OUTPUT_SAMPLING_RATE_DSD128;
    }
    ALOGI("%s:becf: afe: bitwidth %d, samplerate %d channels %d"
          ", backend_idx %d usecase = %d device (%s)", __func__, backend_cfg.bit_width,
          backend_cfg.sample_rate, backend_cfg.channels, backend_idx, usecase->id,
          platform_get_snd_device_name(snd_device));

    if ((my_data->spkr_ch_map != NULL) &&
        (platform_get_backend_index(snd_device) == DEFAULT_CODEC_BACKEND))
        platform_set_channel_map(my_data, my_data->spkr_ch_map->num_ch,
                                 my_data->spkr_ch_map->chmap, -1, device_be_idx);

    if (platform_split_snd_device(my_data, snd_device, &num_devices,
                                  new_snd_devices) < 0)
        new_snd_devices[0] = snd_device;

    for (i = 0; i < num_devices; i++) {
        ALOGI("%s: new_snd_devices[%d] is %d", __func__, i, new_snd_devices[i]);
        if ((platform_check_codec_backend_cfg(adev, usecase, new_snd_devices[i],
                                             &backend_cfg))) {
            ret = platform_set_codec_backend_cfg(adev, usecase, new_snd_devices[i],
                                           backend_cfg);
            if (!ret) {
                ret = true;
            } else {
                ret = false;
            }
        }
    }
    return ret;
}

/*
 * goes through all the current usecases and picks the highest
 * bitwidth & samplerate
 */
static bool platform_check_capture_codec_backend_cfg(struct audio_device* adev,
                                   int backend_idx,
                                   struct audio_backend_cfg *backend_cfg,
                                   snd_device_t snd_device)
{
    bool backend_change = false;
    unsigned int bit_width;
    unsigned int sample_rate;
    unsigned int channels;
    unsigned int format;
    struct platform_data *my_data = (struct platform_data *)adev->platform;
    struct stream_in *in = adev_get_active_input(adev);

    bit_width = backend_cfg->bit_width;
    sample_rate = backend_cfg->sample_rate;
    channels = backend_cfg->channels;
    format = backend_cfg->format;

    ALOGI("%s:txbecf: afe: Codec selected backend: %d current bit width: %d and "
          "sample rate: %d, channels %d format %d",__func__,backend_idx, bit_width,
          sample_rate, channels,format);

    // For voice calls use default configuration i.e. 16b/48K, only applicable to
    // default backend
    // force routing is not required here, caller will do it anyway
    if ((voice_is_in_call(adev) || adev->mode == AUDIO_MODE_IN_COMMUNICATION)) {

        ALOGW("%s:txbecf: afe: Use default bw and sr for voice/voip calls and "
              "for unprocessed/camera source", __func__);
        bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
        sample_rate =  CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
        channels = CODEC_BACKEND_DEFAULT_TX_CHANNELS;
    } else if (my_data->is_internal_codec &&
               my_data->is_default_be_config &&
               !is_usb_in_snd_dev(snd_device)) {
        sample_rate =  CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
        channels = CODEC_BACKEND_DEFAULT_TX_CHANNELS;
        if (in && in->bit_width == 24)
            bit_width = platform_get_snd_device_bit_width(snd_device);
    } else {
        struct listnode *node;
        struct audio_usecase *uc = NULL;
        unsigned int uc_channels = 0;
        struct stream_in *in = NULL;
        /* update cfg against other existing capture usecases on same backend */
        list_for_each(node, &adev->usecase_list) {
            uc = node_to_item(node, struct audio_usecase, list);
            in = (struct stream_in *) uc->stream.in;
            if (in != NULL && uc->type == PCM_CAPTURE &&
                backend_idx == platform_get_backend_index(uc->in_snd_device)) {
                uc_channels = audio_channel_count_from_in_mask(in->channel_mask);

                ALOGV("%s:txbecf: uc %s, id %d, sr %d, bw %d, ch %d, device %s",
                      __func__, use_case_table[uc->id], uc->id, in->sample_rate,
                      in->bit_width, uc_channels,
                      platform_get_snd_device_name(uc->in_snd_device));

                if (sample_rate < in->sample_rate)
                    sample_rate = in->sample_rate;
                if (bit_width < in->bit_width)
                    bit_width = in->bit_width;
                if (channels < uc_channels)
                    channels = uc_channels;
            }
        }
        if ((sample_rate % INPUT_SAMPLING_RATE_11025 == 0) &&
            (!is_usb_in_snd_dev(snd_device))) {
            ALOGV("%s:txbecf: afe: set sample rate to default Sample Rate(48k)",__func__);
            sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
        }
    }
    if (backend_idx == USB_AUDIO_TX_BACKEND) {
        audio_extn_usb_is_config_supported(&bit_width, &sample_rate, &channels, false);
        ALOGV("%s:txbecf: afe: USB BE configured as bit_width(%d)sample_rate(%d)channels(%d)",
              __func__, bit_width, sample_rate, channels);
    }

    ALOGI("%s:txbecf: afe: current backend bit_width %d sample_rate %d channels %d, format %x",
                            __func__,
                            my_data->current_backend_cfg[backend_idx].bit_width,
                            my_data->current_backend_cfg[backend_idx].sample_rate,
                            my_data->current_backend_cfg[backend_idx].channels,
                            my_data->current_backend_cfg[backend_idx].format);
    // Force routing if the expected bitwdith or samplerate
    // is not same as current backend comfiguration
    // Note that below if block would be entered even if main format is same
    // but subformat is different for e.g. current backend is configured for 16 bit PCM
    // as compared to 24 bit PCM backend requested
    if ((bit_width != my_data->current_backend_cfg[backend_idx].bit_width) ||
        (sample_rate != my_data->current_backend_cfg[backend_idx].sample_rate) ||
        (channels != my_data->current_backend_cfg[backend_idx].channels) ||
        (format != my_data->current_backend_cfg[backend_idx].format)) {
        backend_cfg->bit_width = bit_width;
        backend_cfg->sample_rate= sample_rate;
        backend_cfg->channels = channels;
        backend_cfg->format = format;
        backend_change = true;
        ALOGI("%s:txbecf: afe: Codec backend needs to be updated. new bit width: %d "
              "new sample rate: %d new channel: %d new format: %d",
              __func__, backend_cfg->bit_width,
              backend_cfg->sample_rate, backend_cfg->channels, backend_cfg->format);
    }

    return backend_change;
}

bool platform_check_and_set_capture_codec_backend_cfg(struct audio_device* adev,
    struct audio_usecase *usecase, snd_device_t snd_device)
{
    int backend_idx = platform_get_backend_index(snd_device);
    int ret = 0;
    struct audio_backend_cfg backend_cfg;

    backend_cfg.passthrough_enabled = false;

    if (usecase->type == TRANSCODE_LOOPBACK_TX) {
        backend_cfg.bit_width = usecase->stream.inout->in_config.bit_width;
        backend_cfg.sample_rate = usecase->stream.inout->in_config.sample_rate;
        backend_cfg.format = usecase->stream.inout->in_config.format;
        backend_cfg.channels = audio_channel_count_from_out_mask(
                usecase->stream.inout->in_config.channel_mask);
    } else if (usecase->type == PCM_CAPTURE) {
        backend_cfg.sample_rate= usecase->stream.in->sample_rate;
        backend_cfg.bit_width= usecase->stream.in->bit_width;
        backend_cfg.format= usecase->stream.in->format;
        backend_cfg.channels = audio_channel_count_from_in_mask(usecase->stream.in->channel_mask);
        if (is_loopback_input_device(
                    get_device_types(&usecase->stream.in->device_list))) {
            int bw = platform_get_snd_device_bit_width(snd_device);
            if ((-ENOSYS != bw) && (backend_cfg.bit_width > (uint32_t)bw)) {
                backend_cfg.bit_width = bw;
                ALOGD("%s:txbecf: set bitwidth to %d from platform info",
                       __func__, bw);
            }
        }
    } else {
        backend_cfg.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
        backend_cfg.sample_rate =  CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
        backend_cfg.format = AUDIO_FORMAT_PCM_16_BIT;
        backend_cfg.channels = 1;
    }

    ALOGI("%s:txbecf: afe: bitwidth %d, samplerate %d, channel %d format %d"
          ", backend_idx %d usecase = %d device (%s)", __func__,
          backend_cfg.bit_width,
          backend_cfg.sample_rate,
          backend_cfg.channels,
          backend_cfg.format,
          backend_idx, usecase->id,
          platform_get_snd_device_name(snd_device));
    if (platform_check_capture_codec_backend_cfg(adev, backend_idx,
                                                 &backend_cfg, snd_device)) {
        ret = platform_set_codec_backend_cfg(adev, usecase, snd_device,
                                             backend_cfg);
        if(!ret)
            return true;
    }

    return false;
}

int platform_set_snd_device_backend(snd_device_t device, const char *backend_tag,
                                    const char * hw_interface)
{
    int ret = 0;

    if ((device < SND_DEVICE_MIN) || (device >= SND_DEVICE_MAX)) {
        ALOGE("%s: Invalid snd_device = %d",
            __func__, device);
        ret = -EINVAL;
        goto done;
    }

    ALOGD("%s: backend_tag_table[%s]: old = %s new = %s", __func__,
          platform_get_snd_device_name(device),
          backend_tag_table[device] != NULL ? backend_tag_table[device]: "null",
          backend_tag);

    if (backend_tag != NULL ) {
        if (backend_tag_table[device]) {
           free(backend_tag_table[device]);
        }
        backend_tag_table[device] = strdup(backend_tag);
    }

    if (hw_interface != NULL) {
        if (hw_interface_table[device])
            free(hw_interface_table[device]);

        ALOGD("%s: hw_interface_table[%d] = %s", __func__, device, hw_interface);
        hw_interface_table[device] = strdup(hw_interface);
    }
done:
    return ret;
}

const char *platform_get_snd_device_backend_interface(snd_device_t device)
{
    const char *hw_interface_name = NULL;

    if ((device < SND_DEVICE_MIN) || (device >= SND_DEVICE_MAX)) {
        ALOGE("%s: Invalid snd_device = %d",
            __func__, device);
        goto done;
    }

    /* Get string value of necessary backend for device */
    hw_interface_name = hw_interface_table[device];
    if (hw_interface_name == NULL)
        ALOGE("%s: no hw_interface set for device %d\n", __func__, device);
    else
        ALOGD("%s: hw_interface set for device %s\n", __func__, hw_interface_name);
done:
    return hw_interface_name;
}

int platform_get_snd_device_backend_index(snd_device_t device)
{
    int i, be_dai_id;
    const char * hw_interface_name = NULL;

    ALOGV("%s: enter with device %d\n", __func__, device);

    if ((device < SND_DEVICE_MIN) || (device >= SND_DEVICE_MAX)) {
        ALOGE("%s: Invalid snd_device = %d",
              __func__, device);
        be_dai_id = -EINVAL;
        goto done;
    }

    /* Get string value of necessary backend for device */
    hw_interface_name = hw_interface_table[device];
    if (hw_interface_name == NULL) {
        ALOGE("%s: no hw_interface set for device %d\n", __func__, device);
        be_dai_id = -EINVAL;
        goto done;
    }

    /* Check if be dai name table was retrieved successfully */
    if (be_dai_name_table == NULL) {
        ALOGE("%s: BE DAI Name Table is not present\n", __func__);
        be_dai_id = -EFAULT;
        goto done;
    }

    /* Get backend ID for device specified */
    for (i = 0; i < max_be_dai_names; i++) {
        if (strcmp(hw_interface_name, be_dai_name_table[i].be_name) == 0) {
            be_dai_id = be_dai_name_table[i].be_id;
            goto done;
        }
    }
    ALOGE("%s: no interface matching name %s\n", __func__, hw_interface_name);
    be_dai_id = -EINVAL;
    goto done;

done:
    return be_dai_id;
}

int platform_set_usecase_pcm_id(audio_usecase_t usecase, int32_t type, int32_t pcm_id)
{
    int ret = 0;
    if ((usecase <= USECASE_INVALID) || (usecase >= AUDIO_USECASE_MAX)) {
        ALOGE("%s: invalid usecase case idx %d", __func__, usecase);
        ret = -EINVAL;
        goto done;
    }

    if ((type != 0) && (type != 1)) {
        ALOGE("%s: invalid usecase type", __func__);
        ret = -EINVAL;
    }
    ALOGV("%s: pcm_device_table[%d][%d] = %d", __func__, usecase, type, pcm_id);
    pcm_device_table[usecase][type] = pcm_id;
done:
    return ret;
}

void platform_get_device_to_be_id_map(int **device_to_be_id, int *length)
{
     *device_to_be_id = (int*) msm_device_to_be_id;
     *length = msm_be_id_array_len;
}

int platform_set_stream_pan_scale_params(void *platform,
                                         int snd_id,
                                         struct mix_matrix_params mm_params)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    struct mixer_ctl *ctl = NULL;
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};
    int ret = 0;
    int iter_i = 0;
    int iter_j = 0;
    int length = 0;
    char *pan_scale_data = NULL;

    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                          "Audio Stream %d Pan Scale Control", snd_id);
    ALOGD("%s mixer_ctl_name:%s", __func__, mixer_ctl_name);

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        ret = -EINVAL;
        goto end;
    }
    pan_scale_data = (char *) calloc(1, sizeof(mm_params));
    if (!pan_scale_data) {
        ret = -ENOMEM;
        goto end;
    }
    memcpy(&pan_scale_data[length], &mm_params.num_output_channels,
                              sizeof(mm_params.num_output_channels));
    length += sizeof(mm_params.num_output_channels);
    memcpy(&pan_scale_data[length], &mm_params.num_input_channels,
                              sizeof(mm_params.num_input_channels));
    length += sizeof(mm_params.num_input_channels);

    memcpy(&pan_scale_data[length], &mm_params.has_output_channel_map,
                              sizeof(mm_params.has_output_channel_map));
    length += sizeof(mm_params.has_output_channel_map);
    if (mm_params.has_output_channel_map &&
        mm_params.num_output_channels <= MAX_CHANNELS_SUPPORTED &&
        mm_params.num_output_channels > 0) {
        memcpy(&pan_scale_data[length], mm_params.output_channel_map,
                (mm_params.num_output_channels * sizeof(mm_params.output_channel_map[0])));
        length += (mm_params.num_output_channels * sizeof(mm_params.output_channel_map[0]));
    } else {
        ret = -EINVAL;
        goto end;
    }

    memcpy(&pan_scale_data[length], &mm_params.has_input_channel_map,
                                sizeof(mm_params.has_input_channel_map));
    length += sizeof(mm_params.has_input_channel_map);
    if (mm_params.has_input_channel_map &&
        mm_params.num_input_channels <= MAX_CHANNELS_SUPPORTED &&
        mm_params.num_input_channels > 0) {
        memcpy(&pan_scale_data[length], mm_params.input_channel_map,
               (mm_params.num_input_channels * sizeof(mm_params.input_channel_map[0])));
            length += (mm_params.num_input_channels * sizeof(mm_params.input_channel_map[0]));
    } else {
        ret = -EINVAL;
        goto end;
    }
    pan_scale_data[length] = mm_params.has_mixer_coeffs;
    length += sizeof(mm_params.has_mixer_coeffs);
    if (mm_params.has_mixer_coeffs)
        for (iter_i = 0; iter_i < mm_params.num_output_channels; iter_i++)
            for (iter_j = 0; iter_j < mm_params.num_input_channels; iter_j++) {
                 memcpy(&pan_scale_data[length],
                        &mm_params.mixer_coeffs[iter_i][iter_j],
                        (sizeof(mm_params.mixer_coeffs[0][0])));
                 length += (sizeof(mm_params.mixer_coeffs[0][0]));
            }

    ret = mixer_ctl_set_array(ctl, pan_scale_data, length);
end:
    if (pan_scale_data)
        free(pan_scale_data);
    return ret;
}

int platform_set_stream_downmix_params(void *platform,
                                       int snd_id,
                                       snd_device_t snd_device,
                                       struct mix_matrix_params mm_params)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    struct mixer_ctl *ctl;
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};
    char *downmix_param_data = NULL;
    int ret = 0;
    int iter_i = 0;
    int iter_j = 0;
    int length = 0;
    int be_idx = 0;

    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                          "Audio Device %d Downmix Control", snd_id);
    ALOGD("%s mixer_ctl_name:%s", __func__, mixer_ctl_name);

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        ret = -EINVAL;
    }

    downmix_param_data = (char *) calloc(1, sizeof(mm_params) + sizeof(be_idx));
    if (!downmix_param_data) {
        ret = -ENOMEM;
        goto end;
    }
    be_idx = platform_get_snd_device_backend_index(snd_device);
    memcpy(&downmix_param_data[length], &be_idx, sizeof(be_idx));
    length += sizeof(be_idx);
    memcpy(&downmix_param_data[length], &mm_params.num_output_channels,
                                    sizeof(mm_params.num_output_channels));
    length += sizeof(mm_params.num_output_channels);
    memcpy(&downmix_param_data[length], &mm_params.num_input_channels,
                                    sizeof(mm_params.num_input_channels));
    length += sizeof(mm_params.num_input_channels);

    memcpy(&downmix_param_data[length], &mm_params.has_output_channel_map,
                                   sizeof(mm_params.has_output_channel_map));
    length += sizeof(mm_params.has_output_channel_map);
    if (mm_params.has_output_channel_map &&
        mm_params.num_output_channels <= MAX_CHANNELS_SUPPORTED &&
        mm_params.num_output_channels > 0) {
        memcpy(&downmix_param_data[length], mm_params.output_channel_map,
                (mm_params.num_output_channels * sizeof(mm_params.output_channel_map[0])));
        length += (mm_params.num_output_channels * sizeof(mm_params.output_channel_map[0]));
    } else {
        ret = -EINVAL;
        goto end;
    }

    memcpy(&downmix_param_data[length], &mm_params.has_input_channel_map,
                                   sizeof(mm_params.has_input_channel_map));
    length += sizeof(mm_params.has_input_channel_map);
    if (mm_params.has_input_channel_map &&
        mm_params.num_input_channels <= MAX_CHANNELS_SUPPORTED &&
        mm_params.num_input_channels > 0) {
        memcpy(&downmix_param_data[length], mm_params.input_channel_map,
                (mm_params.num_input_channels * sizeof(mm_params.input_channel_map[0])));
            length += (mm_params.num_input_channels * sizeof(mm_params.input_channel_map[0]));
    } else {
        ret = -EINVAL;
        goto end;
    }
    memcpy(&downmix_param_data[length], &mm_params.has_mixer_coeffs,
                                    sizeof(mm_params.has_mixer_coeffs));
    length += sizeof(mm_params.has_mixer_coeffs);
    if (mm_params.has_mixer_coeffs)
        for (iter_i = 0; iter_i < mm_params.num_output_channels; iter_i++)
            for (iter_j = 0; iter_j < mm_params.num_input_channels; iter_j++) {
                memcpy((uint32_t *) &downmix_param_data[length],
                        &mm_params.mixer_coeffs[iter_i][iter_j],
                        (sizeof(mm_params.mixer_coeffs[0][0])));
                length += (sizeof(mm_params.mixer_coeffs[0][0]));
            }

    ret = mixer_ctl_set_array(ctl, downmix_param_data, length);
end:
    if (downmix_param_data)
        free(downmix_param_data);
    return ret;
}

int platform_set_stream_channel_map(void *platform, audio_channel_mask_t channel_mask,
                                               int snd_id, uint8_t *input_channel_map)
{
    int ret = 0, i = 0;
    int channels = audio_channel_count_from_out_mask(channel_mask);

    char channel_map[AUDIO_CHANNEL_COUNT_MAX];
    memset(channel_map, 0, sizeof(channel_map));
    if (*input_channel_map) {
        for (i = 0; i < channels; i++) {
             ALOGV("%s:: Channel Map channel_map[%d] - %d", __func__, i, *input_channel_map);
             channel_map[i] = *input_channel_map;
             input_channel_map++;
        }
    } else {
        /* Following are all most common standard WAV channel layouts
           overridden by channel mask if its allowed and different */
        switch (channels) {
            case 1:
                /* AUDIO_CHANNEL_OUT_MONO */
                channel_map[0] = PCM_CHANNEL_FC;
                break;
            case 2:
                /* AUDIO_CHANNEL_OUT_STEREO */
                channel_map[0] = PCM_CHANNEL_FL;
                channel_map[1] = PCM_CHANNEL_FR;
                break;
            case 3:
                /* AUDIO_CHANNEL_OUT_2POINT1 */
                channel_map[0] = PCM_CHANNEL_FL;
                channel_map[1] = PCM_CHANNEL_FR;
                channel_map[2] = PCM_CHANNEL_FC;
                break;
            case 4:
                /* AUDIO_CHANNEL_OUT_QUAD_SIDE */
                channel_map[0] = PCM_CHANNEL_FL;
                channel_map[1] = PCM_CHANNEL_FR;
                channel_map[2] = PCM_CHANNEL_LS;
                channel_map[3] = PCM_CHANNEL_RS;
                if (channel_mask == AUDIO_CHANNEL_OUT_QUAD_BACK) {
                    channel_map[2] = PCM_CHANNEL_LB;
                    channel_map[3] = PCM_CHANNEL_RB;
                }
                if (channel_mask == AUDIO_CHANNEL_OUT_SURROUND) {
                    channel_map[2] = PCM_CHANNEL_FC;
                    channel_map[3] = PCM_CHANNEL_CS;
                }
                break;
            case 5:
                /* AUDIO_CHANNEL_OUT_PENTA */
                channel_map[0] = PCM_CHANNEL_FL;
                channel_map[1] = PCM_CHANNEL_FR;
                channel_map[2] = PCM_CHANNEL_FC;
                channel_map[3] = PCM_CHANNEL_LB;
                channel_map[4] = PCM_CHANNEL_RB;
                break;
            case 6:
                /* AUDIO_CHANNEL_OUT_5POINT1 */
                channel_map[0] = PCM_CHANNEL_FL;
                channel_map[1] = PCM_CHANNEL_FR;
                channel_map[2] = PCM_CHANNEL_FC;
                channel_map[3] = PCM_CHANNEL_LFE;
                channel_map[4] = PCM_CHANNEL_LB;
                channel_map[5] = PCM_CHANNEL_RB;
                if (channel_mask == AUDIO_CHANNEL_OUT_5POINT1_SIDE) {
                    channel_map[4] = PCM_CHANNEL_LS;
                    channel_map[5] = PCM_CHANNEL_RS;
                }
                break;
            case 7:
                /* AUDIO_CHANNEL_OUT_6POINT1 */
                channel_map[0] = PCM_CHANNEL_FL;
                channel_map[1] = PCM_CHANNEL_FR;
                channel_map[2] = PCM_CHANNEL_FC;
                channel_map[3] = PCM_CHANNEL_LFE;
                channel_map[4] = PCM_CHANNEL_LB;
                channel_map[5] = PCM_CHANNEL_RB;
                channel_map[6] = PCM_CHANNEL_CS;
                break;
            case 8:
                /* AUDIO_CHANNEL_OUT_7POINT1 */
                channel_map[0] = PCM_CHANNEL_FL;
                channel_map[1] = PCM_CHANNEL_FR;
                channel_map[2] = PCM_CHANNEL_FC;
                channel_map[3] = PCM_CHANNEL_LFE;
                channel_map[4] = PCM_CHANNEL_LB;
                channel_map[5] = PCM_CHANNEL_RB;
                if (channel_mask == AUDIO_CHANNEL_OUT_5POINT1POINT2) {
                    channel_map[6] = PCM_CHANNEL_TFL;
                    channel_map[7] = PCM_CHANNEL_TFR;
                } else {
                    channel_map[6] = PCM_CHANNEL_LS;
                    channel_map[7] = PCM_CHANNEL_RS;
                }
                break;
           case 12:
                /* AUDIO_CHANNEL_OUT_7POINT1POINT4 */
                channel_map[0] = PCM_CHANNEL_FL;
                channel_map[1] = PCM_CHANNEL_FR;
                channel_map[2] = PCM_CHANNEL_FC;
                channel_map[3] = PCM_CHANNEL_LFE;
                channel_map[4] = PCM_CHANNEL_LB;
                channel_map[5] = PCM_CHANNEL_RB;
                channel_map[6] = PCM_CHANNEL_LS;
                channel_map[7] = PCM_CHANNEL_RS;
                channel_map[8] = PCM_CHANNEL_TFL;
                channel_map[9] = PCM_CHANNEL_TFR;
                channel_map[10] = PCM_CHANNEL_TSL;
                channel_map[11] = PCM_CHANNEL_TSR;
                break;
          case 16:
                /* 16 channels */
                channel_map[0] = PCM_CHANNEL_FL;
                channel_map[1] = PCM_CHANNEL_FR;
                channel_map[2] = PCM_CHANNEL_FC;
                channel_map[3] = PCM_CHANNEL_LFE;
                channel_map[4] = PCM_CHANNEL_LB;
                channel_map[5] = PCM_CHANNEL_RB;
                channel_map[6] = PCM_CHANNEL_LS;
                channel_map[7] = PCM_CHANNEL_RS;
                channel_map[8] = PCM_CHANNEL_TFL;
                channel_map[9] = PCM_CHANNEL_TFR;
                channel_map[10] = PCM_CHANNEL_TSL;
                channel_map[11] = PCM_CHANNEL_TSR;
                channel_map[12] = PCM_CHANNEL_FLC;
                channel_map[13] = PCM_CHANNEL_FRC;
                channel_map[14] = PCM_CHANNEL_RLC;
                channel_map[15] = PCM_CHANNEL_RRC;
                break;
            default:
                ALOGE("unsupported channels %d for setting channel map", channels);
                return -1;
        }
    }
    ret = platform_set_channel_map(platform, channels, channel_map, snd_id, -1);
    return ret;
}

int platform_get_edid_info(void *platform)
{
    return platform_get_edid_info_v2(platform, 0, 0);
}

int platform_get_edid_info_v2(void *platform, int controller, int stream)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    char block[MAX_SAD_BLOCKS * SAD_BLOCK_SIZE];
    int ret, count;
    char edid_data[MAX_SAD_BLOCKS * SAD_BLOCK_SIZE + 1] = {0};
    struct ext_disp_state *state = NULL;
    int ctl_index = 0;
    struct mixer_ctl *ctl = NULL;
    const char *ctl_name_prefix = "Display Port";
    const char *ctl_name_suffix = "EDID";
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};

    ctl_index = platform_get_display_port_ctl_index(controller, stream);
    if (-EINVAL == ctl_index) {
        ALOGE("%s: Unknown controller/stream %d/%d",
              __func__, controller, stream);
        return -EINVAL;
    }

    state = &my_data->ext_disp[controller][stream];
    if (state->valid) {
        /* use cached edid */
        return 0;
    }

    switch(state->type) {
        case EXT_DISPLAY_TYPE_HDMI:
            snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "HDMI EDID");
            break;
        case EXT_DISPLAY_TYPE_DP:
            if (!audio_extn_is_display_port_enabled()) {
                ALOGE("%s: display port is not supported", __func__);
                return -EINVAL;
            }

            if (0 == ctl_index)
                snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                         "%s %s", ctl_name_prefix, ctl_name_suffix);
            else
                snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                         "%s%d %s", ctl_name_prefix, ctl_index, ctl_name_suffix);
            break;
        default:
            ALOGE("%s: Invalid disp_type %d", __func__, state->type);
            return -EINVAL;
    }

    if (state->edid_info == NULL)
        state->edid_info =
            (struct edid_audio_info *)calloc(1, sizeof(struct edid_audio_info));

    ALOGV("%s: mixer ctl name: %s", __func__, mixer_ctl_name);

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        goto fail;
    }

    mixer_ctl_update(ctl);

    count = mixer_ctl_get_num_values(ctl);

    /* Read SAD blocks, clamping the maximum size for safety */
    if (count > (int)sizeof(block))
        count = (int)sizeof(block);

    ret = mixer_ctl_get_array(ctl, block, count);
    if (ret != 0) {
        ALOGE("%s: mixer_ctl_get_array() failed to get EDID info", __func__);
        goto fail;
    }
    edid_data[0] = count;
    memcpy(&edid_data[1], block, count);

    ALOGV("%s: received edid data: count %d", __func__, edid_data[0]);

    if (!audio_extn_edid_get_sink_caps(state->edid_info, edid_data)) {
        ALOGE("%s: Failed to get extn disp sink capabilities", __func__);
        goto fail;
    }
    state->valid = true;
    return 0;
fail:
    if (state->edid_info) {
        free(state->edid_info);
        state->edid_info = NULL;
        state->valid = false;
    }
    ALOGE("%s: return -EINVAL", __func__);
    return -EINVAL;
}

int platform_set_channel_allocation(void *platform, int channel_alloc)
{
    return platform_set_channel_allocation_v2(platform, channel_alloc, 0, 0);
}

int platform_set_channel_allocation_v2(void *platform, int channel_alloc,
                                    int controller, int stream)
{
    int ret;
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    int ctl_index = 0;
    struct mixer_ctl *ctl = NULL;
    const char *ctl_name_prefix = "Display Port";
    const char *ctl_name_suffix = "RX CA";
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};

    ctl_index = platform_get_display_port_ctl_index(controller, stream);
    if (-EINVAL == ctl_index) {
        ALOGE("%s: Unknown controller/stream %d/%d",
              __func__, controller, stream);
        return -EINVAL;
    }

    switch(my_data->ext_disp[controller][stream].type) {
        case EXT_DISPLAY_TYPE_HDMI:
            snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "HDMI RX CA");
            break;
        case EXT_DISPLAY_TYPE_DP:
            if (!audio_extn_is_display_port_enabled()) {
                ALOGE("%s: display port is not supported", __func__);
                return -EINVAL;
            }

            if (0 == ctl_index)
                snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                         "%s %s", ctl_name_prefix, ctl_name_suffix);
            else
                snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                         "%s%d %s", ctl_name_prefix, ctl_index, ctl_name_suffix);
            break;
        default:
            ALOGE("%s: Invalid disp_type %d", __func__,
                  my_data->ext_disp[controller][stream].type);
            return -EINVAL;
    }

    ALOGV("%s: mixer ctl name: %s", __func__, mixer_ctl_name);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }

    ALOGV("%s: channel allocation: 0x%x", __func__, channel_alloc);
    ret = mixer_ctl_set_value(ctl, 0, channel_alloc);

    if (ret < 0) {
        ALOGE("%s: Could not set ctl, error:%d ", __func__, ret);
    }

    return ret;
}

int platform_set_channel_map(void *platform, int ch_count, char *ch_map, int snd_id, int be_idx)
{
    struct mixer_ctl *ctl, *be_ctl = NULL;
    char mixer_ctl_name[44] = {0}; // max length of name is 44 as defined
    char be_mixer_ctl_name[] = "Backend Device Channel Map";
    int ret;
    int i=0, n=0;
    int be_id_count = 0;
    long set_values[AUDIO_MAX_DSP_CHANNELS];
    long be_set_values[AUDIO_MAX_DSP_CHANNELS + 1] = {0};
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    ALOGV("%s channel_count:%d",__func__, ch_count);

    /*
     * FIXME:
     * Currently the channel mask in audio.h is limited to 30 channels,
     * (=AUDIO_CHANNEL_COUNT_MAX), whereas the mixer controls already
     * allow up to AUDIO_MAX_DSP_CHANNELS channels as per final requirement.
     * Until channel mask definition is not changed from a uint32_t value
     * to something else, a sanity check is needed here.
     */
    if (NULL == ch_map || (ch_count < 1) || (ch_count > AUDIO_CHANNEL_COUNT_MAX)) {
        ALOGE("%s: Invalid channel mapping or channel count value", __func__);
        return -EINVAL;
    }

    /*
     * If snd_id is greater than 0, stream channel mapping
     * If snd_id is below 0, typically -1, device channel mapping
     */
    if (snd_id >= 0) {
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "Playback Channel Map%d", snd_id);
    } else {
        if (be_idx >= 0) {
            be_ctl = mixer_get_ctl_by_name(adev->mixer, be_mixer_ctl_name);
            if (!be_ctl) {
                ALOGD("%s: Could not get ctl for mixer cmd - %s, using default control",
                       __func__, be_mixer_ctl_name);
                strlcpy(mixer_ctl_name, "Playback Device Channel Map", sizeof(mixer_ctl_name));
                be_idx = -1;
            } else {
                strlcpy(mixer_ctl_name, "Backend Device Channel Map", sizeof(mixer_ctl_name));
                be_id_count = 1;
            }
        } else {
            strlcpy(mixer_ctl_name, "Playback Device Channel Map", sizeof(mixer_ctl_name));
        }
    }

    ALOGD("%s mixer_ctl_name:%s", __func__, mixer_ctl_name);

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);

    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }

    /* find out how many values the control can set */
    n = mixer_ctl_get_num_values(ctl);

    if (n != ch_count)
        ALOGV("%s chcnt %d != mixerctl elem size %d",__func__, ch_count, n);

    if (n < ch_count) {
        ALOGE("%s chcnt %d > mixerctl elem size %d",__func__, ch_count, n);
        return -EINVAL;
    }

    if (n > (AUDIO_MAX_DSP_CHANNELS + be_id_count)) {
        ALOGE("%s mixerctl elem size %d > AUDIO_MAX_DSP_CHANNELS %d",__func__, n, AUDIO_MAX_DSP_CHANNELS);
        return -EINVAL;
    }

    /* initialize all set_values to zero */
    memset (set_values, 0, sizeof(set_values));

    /* copy only as many values as corresponding mixer_ctrl allows */
    for (i = 0; i < ch_count; i++) {
        set_values[i] = ch_map[i];
    }

    ALOGD("%s: set mapping(%ld %ld %ld %ld %ld %ld %ld %ld) for channel:%d", __func__,
        set_values[0], set_values[1], set_values[2], set_values[3], set_values[4],
        set_values[5], set_values[6], set_values[7], ch_count);

    if (be_idx >= 0) {
        be_set_values[0] = be_idx;
        memcpy(&be_set_values[1], set_values, sizeof(long) * ch_count);
        ret = mixer_ctl_set_array(ctl, be_set_values, ARRAY_SIZE(be_set_values));
    } else {
        ret = mixer_ctl_set_array(ctl, set_values, ARRAY_SIZE(set_values));
    }

    if (ret < 0) {
        ALOGE("%s: Could not set ctl, error:%d ch_count:%d",
              __func__, ret, ch_count);
    }
    return ret;
}

unsigned char platform_map_to_edid_format(int audio_format)
{
    unsigned char format;
    switch (audio_format & AUDIO_FORMAT_MAIN_MASK) {
    case AUDIO_FORMAT_AC3:
        ALOGV("%s: AC3", __func__);
        format = AC3;
        break;
    case AUDIO_FORMAT_AAC:
        ALOGV("%s:AAC", __func__);
        format = AAC;
        break;
    case AUDIO_FORMAT_AAC_ADTS:
        ALOGV("%s:AAC_ADTS", __func__);
        format = AAC;
        break;
    case AUDIO_FORMAT_E_AC3:
    case AUDIO_FORMAT_E_AC3_JOC:
        ALOGV("%s:E_AC3", __func__);
        format = DOLBY_DIGITAL_PLUS;
        break;
    case AUDIO_FORMAT_DOLBY_TRUEHD:
        ALOGV("%s:MAT", __func__);
        format = MAT;
        break;
    case AUDIO_FORMAT_DTS:
        ALOGV("%s:DTS", __func__);
        format = DTS;
        break;
    case AUDIO_FORMAT_DTS_HD:
        ALOGV("%s:DTS_HD", __func__);
        format = DTS_HD;
        break;
    case AUDIO_FORMAT_PCM_16_BIT:
    case AUDIO_FORMAT_PCM_24_BIT_PACKED:
    case AUDIO_FORMAT_PCM_8_24_BIT:
        ALOGV("%s:PCM", __func__);
        format = LPCM;
        break;
    case AUDIO_FORMAT_IEC61937:
        ALOGV("%s:IEC61937", __func__);
        format = 0;
        break;
    default:
        format =  -1;
        ALOGE("%s:invalid format: 0x%x", __func__, audio_format);
        break;
    }
    return format;
}

void platform_check_and_update_copp_sample_rate(void* platform, snd_device_t snd_device,
                                                unsigned int stream_sr, int* sample_rate)
{
    struct platform_data* my_data = (struct platform_data *)platform;
    int backend_idx = platform_get_backend_index(snd_device);
    int device_sr = my_data->current_backend_cfg[backend_idx].sample_rate;
    /*
     *Check if device SR is multiple of 8K or 11.025 Khz
     *check if the stream SR is multiple of same base, if yes
     *then have copp SR equal to stream SR, this ensures that
     *post processing happens at stream SR, else have
     *copp SR equal to device SR.
     */
     if (!(((sample_rate_multiple(device_sr, SAMPLE_RATE_8000)) &&
                 (sample_rate_multiple(stream_sr, SAMPLE_RATE_8000))) ||
           ((sample_rate_multiple(device_sr, SAMPLE_RATE_11025)) &&
                 (sample_rate_multiple(stream_sr, SAMPLE_RATE_11025))))) {
         *sample_rate = device_sr;
     } else
         *sample_rate = stream_sr;

    if ((snd_device == SND_DEVICE_OUT_HDMI) || (snd_device == SND_DEVICE_OUT_DISPLAY_PORT) ||
                  (snd_device == SND_DEVICE_OUT_DISPLAY_PORT1) ||
                  (snd_device == SND_DEVICE_OUT_USB_HEADSET))
        *sample_rate = platform_get_supported_copp_sampling_rate(stream_sr);

     ALOGI("sn_device %d device sr %d stream sr %d copp sr %d", snd_device, device_sr, stream_sr, *sample_rate);

}

// called from info parser
void platform_add_app_type(const char *uc_type,
                           const char *mode,
                           int bw,
                           int app_type, int max_rate) {
    struct app_type_entry *ap =
            (struct app_type_entry *)calloc(1, sizeof(struct app_type_entry));

    if (!ap) {
        ALOGE("%s failed to allocate mem for app type", __func__);
        return;
    }

    ap->uc_type = -1;
    for (int i=0; i<USECASE_TYPE_MAX; i++) {
        if (!strcmp(uc_type, usecase_type_index[i].name)) {
            ap->uc_type = usecase_type_index[i].index;
            break;
        }
    }

    if (ap->uc_type == -1) {
        free(ap);
        return;
    }

    ALOGI("%s uc %s mode %s bw %d app_type %d max_rate %d",
          __func__, uc_type, mode, bw, app_type, max_rate);
    ap->bit_width = bw;
    ap->app_type = app_type;
    ap->max_rate = max_rate;
    ap->mode = strdup(mode);
    list_add_tail(&app_type_entry_list, &ap->node);
}

bool platform_is_edid_supported_format(void *platform, int format) {
    return platform_is_edid_supported_format_v2(platform, format, 0, 0);
}

bool platform_is_edid_supported_format_v2(void *platform,
                                          int format, int controller, int stream)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    edid_audio_info *info = NULL;
    int i, ret;
    unsigned char format_id = platform_map_to_edid_format(format);

    if (format == AUDIO_FORMAT_IEC61937)
        return true;

    if (format_id <= 0) {
        ALOGE("%s invalid edid format mappting for :%x" ,__func__, format);
        return false;
    }

    ret = platform_get_edid_info_v2(platform, controller, stream);
    if (ret == 0)
        info = (edid_audio_info *)my_data->ext_disp[controller][stream].edid_info;
    if (ret == 0 && info != NULL) {
        for (i = 0; i < info->audio_blocks && i < MAX_EDID_BLOCKS; i++) {
             /*
              * To check
              *  is there any special for CONFIG_HDMI_PASSTHROUGH_CONVERT
              *  & DOLBY_DIGITAL_PLUS
              */
            if (info->audio_blocks_array[i].format_id == format_id) {
                ALOGV("%s:returns true %x",
                      __func__, format);
                return true;
            }
        }
    }
    ALOGV("%s:returns false %x",
           __func__, format);
    return false;
}

bool platform_is_edid_supported_sample_rate(void *platform, int sample_rate)
{
    return platform_is_edid_supported_sample_rate_v2(platform, sample_rate, 0, 0);
}

bool platform_is_edid_supported_sample_rate_v2(void *platform,
                                               int sample_rate,
                                               int controller, int stream)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    edid_audio_info *info = NULL;
    int ret = 0;

    ret = platform_get_edid_info_v2(platform, controller, stream);
    if (ret == 0)
        info = (edid_audio_info *)my_data->ext_disp[controller][stream].edid_info;
    if (ret == 0 && info != NULL) {
        return audio_extn_edid_is_supported_sr(info, sample_rate);
    }

    return false;
}

int platform_edid_get_highest_supported_sr_v2(void *platform, int controller, int stream)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    edid_audio_info *info = NULL;
    int ret = 0;

    ret = platform_get_edid_info_v2(platform, controller, stream);
    if (ret == 0)
        info = (edid_audio_info *)my_data->ext_disp[controller][stream].edid_info;
    if (ret == 0 && info != NULL) {
        return audio_extn_edid_get_highest_supported_sr(info);
    }

    return 0;
}

int platform_edid_get_highest_supported_sr(void *platform)
{
    return  platform_edid_get_highest_supported_sr_v2(platform, 0, 0);
}

bool platform_spkr_use_default_sample_rate(void *platform) {
    struct platform_data *my_data = (struct platform_data *)platform;
    return my_data->use_sprk_default_sample_rate;
}

int platform_set_edid_channels_configuration(void *platform, int channels,
                                     int backend_idx, snd_device_t snd_device) {
    return platform_set_edid_channels_configuration_v2(platform, channels,
                        backend_idx, snd_device, 0, 0);
}

int platform_set_edid_channels_configuration_v2(void *platform, int channels,
                                     int backend_idx, snd_device_t snd_device,
                                     int controller, int stream) {

    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    edid_audio_info *info = NULL;
    int ret;
    char default_channelMap[MAX_CHANNELS_SUPPORTED] = {0};
    struct audio_device_config_param *adev_device_cfg_ptr = adev->device_cfg_params;
    int be_idx = -1;
    int channel_alloc = 0;
    int max_supported_channels = 0;

    if ((backend_idx != HDMI_RX_BACKEND) &&
            (backend_idx != DISP_PORT_RX_BACKEND) &&
            (backend_idx != DISP_PORT1_RX_BACKEND)) {
        ALOGE("%s: Invalid backend idx %d", __func__, backend_idx);
        return -EINVAL;
    }

    if (controller < 0 || controller >= MAX_CONTROLLERS ||
            stream < 0 || stream >= MAX_STREAMS_PER_CONTROLLER) {
        ALOGE("%s: Invalid controller/stream - %d/%d",
              __func__, controller, stream);
        return -EINVAL;
    }

    // backend port id
    be_idx = platform_get_snd_device_backend_index(snd_device);

    ret = platform_get_edid_info_v2(platform, controller, stream);
    info = (edid_audio_info *)my_data->ext_disp[controller][stream].edid_info;
    adev_device_cfg_ptr += backend_idx;
    if(ret == 0 && info != NULL) {
        if ((channels > 2) && (channels <= MAX_HDMI_CHANNEL_CNT)) {
            ALOGV("%s:able to get HDMI/DP sink capabilities multi channel playback",
                   __func__);
            max_supported_channels = platform_edid_get_max_channels(my_data);
            if (channels > max_supported_channels)
                channels = max_supported_channels;
            // refer to HDMI spec CEA-861-E: Table 28 Audio InfoFrame Data Byte 4
            switch (channels) {
            case 3:
                channel_alloc = 0x02; break;
            case 4:
                channel_alloc = 0x06; break;
            case 5:
                channel_alloc = 0x0A; break;
            case 6:
                channel_alloc = 0x0B; break;
            case 7:
                channel_alloc = 0x12; break;
            case 8:
                channel_alloc = 0x13; break;
            default:
                ALOGE("%s: invalid channel %d", __func__, channels);
                return -EINVAL;
            }
            ALOGVV("%s:channels:%d", __func__, channels);

            if (adev_device_cfg_ptr->use_client_dev_cfg) {
                platform_set_channel_map(platform, adev_device_cfg_ptr->dev_cfg_params.channels,
                                     (char *)adev_device_cfg_ptr->dev_cfg_params.channel_map, -1, be_idx);
            } else {
                platform_set_channel_map(platform, channels, info->channel_map, -1, be_idx);
            }

            if (adev_device_cfg_ptr->use_client_dev_cfg) {
                ALOGV("%s:: Setting client selected CA %d", __func__,
                            adev_device_cfg_ptr->dev_cfg_params.channel_allocation);
                platform_set_channel_allocation_v2(platform,
                       adev_device_cfg_ptr->dev_cfg_params.channel_allocation,
                       controller, stream);
            } else {
                platform_set_channel_allocation_v2(platform, channel_alloc,
                                                controller, stream);
            }
        } else {
            if (adev_device_cfg_ptr->use_client_dev_cfg) {
                default_channelMap[0] = adev_device_cfg_ptr->dev_cfg_params.channel_map[0];
                default_channelMap[1] = adev_device_cfg_ptr->dev_cfg_params.channel_map[1];
            } else {
                default_channelMap[0] = PCM_CHANNEL_FL;
                default_channelMap[1] = PCM_CHANNEL_FR;
            }
            platform_set_channel_map(platform, 2, default_channelMap, -1, be_idx);
            platform_set_channel_allocation_v2(platform, 0, controller, stream);
        }
    }

    return 0;
}

void platform_cache_edid_v2(void * platform, int controller, int stream)
{
    platform_get_edid_info_v2(platform, controller, stream);
}

void platform_cache_edid(void * platform)
{
    platform_get_edid_info_v2(platform, 0, 0);
}

void platform_invalidate_backend_config(void * platform,snd_device_t snd_device)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    struct audio_backend_cfg backend_cfg;
    int backend_idx;

    backend_cfg.sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
    backend_cfg.channels = CODEC_BACKEND_DEFAULT_CHANNELS;
    backend_cfg.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    backend_cfg.format = AUDIO_FORMAT_PCM_16_BIT;
    backend_cfg.passthrough_enabled = false;

    backend_idx = platform_get_backend_index(snd_device);
    platform_set_codec_backend_cfg(adev, NULL, snd_device, backend_cfg);
    my_data->current_backend_cfg[backend_idx].sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
    my_data->current_backend_cfg[backend_idx].channels = CODEC_BACKEND_DEFAULT_CHANNELS;
    my_data->current_backend_cfg[backend_idx].bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    my_data->current_backend_cfg[backend_idx].format = AUDIO_FORMAT_PCM_16_BIT;
    my_data->current_backend_cfg[backend_idx].controller = 0;
    my_data->current_backend_cfg[backend_idx].stream = 0;
}

void platform_invalidate_hdmi_config_v2(void * platform, int controller, int stream)
{
    //reset ext display EDID info
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    struct audio_backend_cfg backend_cfg;
    int backend_idx;
    snd_device_t snd_device;

    if (controller < 0 || controller >= MAX_CONTROLLERS ||
            stream < 0 || stream >= MAX_STREAMS_PER_CONTROLLER) {
        ALOGE("%s: Invalid controller/stream - %d/%d",
              __func__, controller, stream);
        return;
    }

    backend_cfg.sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
    backend_cfg.channels = DEFAULT_HDMI_OUT_CHANNELS;
    backend_cfg.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    backend_cfg.format = 0;
    backend_cfg.passthrough_enabled = false;

    struct ext_disp_state *state = &my_data->ext_disp[controller][stream];
    state->valid = false;
    if (state->edid_info) {
        memset(state->edid_info, 0, sizeof(struct edid_audio_info));
    }

    if (my_data->ext_disp[controller][stream].type == EXT_DISPLAY_TYPE_HDMI) {
        //reset HDMI_RX_BACKEND to default values
        backend_idx = HDMI_RX_BACKEND;
        snd_device = SND_DEVICE_OUT_HDMI;
    } else {
        //reset Display port BACKEND to default values
        backend_idx = DISP_PORT_RX_BACKEND +
                      ((controller * MAX_STREAMS_PER_CONTROLLER) + stream);
        snd_device = SND_DEVICE_OUT_DISPLAY_PORT +
                     ((controller * MAX_STREAMS_PER_CONTROLLER) + stream);
    }
    platform_set_codec_backend_cfg(adev, NULL, snd_device, backend_cfg);
    my_data->current_backend_cfg[backend_idx].sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
    my_data->current_backend_cfg[backend_idx].channels = DEFAULT_HDMI_OUT_CHANNELS;
    my_data->current_backend_cfg[backend_idx].bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    my_data->ext_disp[controller][stream].type = EXT_DISPLAY_TYPE_NONE;
}

void platform_invalidate_hdmi_config(void * platform) {
    platform_invalidate_hdmi_config_v2(platform, 0, 0);
}

int platform_set_mixer_control(struct stream_out *out, const char * mixer_ctl_name,
                      const char *mixer_val)
{
    struct audio_device *adev = out->dev;
    struct mixer_ctl *ctl = NULL;
    ALOGD("setting mixer ctl %s with value %s", mixer_ctl_name, mixer_val);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }

    return mixer_ctl_set_enum_by_string(ctl, mixer_val);
}

int platform_set_device_params(struct stream_out *out, int param, int value)
{
    struct audio_device *adev = out->dev;
    struct mixer_ctl *ctl;
    char *mixer_ctl_name = "Device PP Params";
    int ret = 0;
    long set_values[] = {0,0};

    set_values[0] = param;
    set_values[1] = value;

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        ret = -EINVAL;
        goto end;
    }

    ALOGV("%s: Setting device pp params param: %d, value %d mixer ctrl:%s",
          __func__,param, value, mixer_ctl_name);
    mixer_ctl_set_array(ctl, set_values, ARRAY_SIZE(set_values));

end:
    return ret;
}

bool platform_can_enable_spkr_prot_on_device(snd_device_t snd_device)
{
    bool ret = false;

    if (snd_device == SND_DEVICE_OUT_SPEAKER ||
        snd_device == SND_DEVICE_OUT_SPEAKER_REVERSE ||
        snd_device == SND_DEVICE_OUT_SPEAKER_WSA ||
        snd_device == SND_DEVICE_OUT_SPEAKER_VBAT ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_VBAT ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_2_VBAT ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_STEREO ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_2 ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_WSA ||
        snd_device == SND_DEVICE_OUT_VOICE_SPEAKER_2_WSA) {
        ret = true;
    }

    return ret;
}

int platform_get_spkr_prot_acdb_id(snd_device_t snd_device)
{
    int acdb_id;

    switch(snd_device) {
        case SND_DEVICE_OUT_SPEAKER:
        case SND_DEVICE_OUT_SPEAKER_WSA:
             acdb_id = platform_get_snd_device_acdb_id(SND_DEVICE_OUT_SPEAKER_PROTECTED);
             break;
        case SND_DEVICE_OUT_VOICE_SPEAKER:
        case SND_DEVICE_OUT_VOICE_SPEAKER_WSA:
             acdb_id = platform_get_snd_device_acdb_id(SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED);
             break;
        case SND_DEVICE_OUT_VOICE_SPEAKER_2:
        case SND_DEVICE_OUT_VOICE_SPEAKER_2_WSA:
             acdb_id = platform_get_snd_device_acdb_id(SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED);
             break;
        case SND_DEVICE_OUT_VOICE_SPEAKER_STEREO:
             acdb_id = platform_get_snd_device_acdb_id(SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_PROTECTED);
             break;
        case SND_DEVICE_OUT_SPEAKER_VBAT:
             acdb_id = platform_get_snd_device_acdb_id(SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT);
             break;
        case SND_DEVICE_OUT_VOICE_SPEAKER_VBAT:
             acdb_id = platform_get_snd_device_acdb_id(SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED_VBAT);
             break;
        case SND_DEVICE_OUT_VOICE_SPEAKER_2_VBAT:
             acdb_id = platform_get_snd_device_acdb_id(SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED_VBAT);
             break;
        default:
             acdb_id = -EINVAL;
             break;
    }
    return acdb_id;
}

int platform_get_spkr_prot_snd_device(snd_device_t snd_device)
{
    if (!audio_extn_spkr_prot_is_enabled())
        return snd_device;

    switch(snd_device) {
        case SND_DEVICE_OUT_SPEAKER:
        case SND_DEVICE_OUT_SPEAKER_REVERSE:
        case SND_DEVICE_OUT_SPEAKER_WSA:
             return SND_DEVICE_OUT_SPEAKER_PROTECTED;
        case SND_DEVICE_OUT_SPEAKER_SAFE:
             return SND_DEVICE_OUT_SPEAKER_SAFE;
        case SND_DEVICE_OUT_VOICE_SPEAKER:
        case SND_DEVICE_OUT_VOICE_SPEAKER_WSA:
             return SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED;
        case SND_DEVICE_OUT_VOICE_SPEAKER_2:
        case SND_DEVICE_OUT_VOICE_SPEAKER_2_WSA:
             return SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED;
        case SND_DEVICE_OUT_VOICE_SPEAKER_STEREO:
             return SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_PROTECTED;
        case SND_DEVICE_OUT_SPEAKER_VBAT:
             return SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT;
        case SND_DEVICE_OUT_VOICE_SPEAKER_VBAT:
             return SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED_VBAT;
        case SND_DEVICE_OUT_VOICE_SPEAKER_2_VBAT:
             return SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED_VBAT;
        default:
             return snd_device;
    }
}
int platform_get_vi_feedback_snd_device(snd_device_t snd_device)
{
    switch(snd_device) {
        case SND_DEVICE_OUT_SPEAKER_PROTECTED:
        case SND_DEVICE_OUT_SPEAKER_PROTECTED_VBAT:
        case SND_DEVICE_OUT_VOICE_SPEAKER_STEREO_PROTECTED:
             return SND_DEVICE_IN_CAPTURE_VI_FEEDBACK;
        case SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED:
        case SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED_VBAT:
             return SND_DEVICE_IN_CAPTURE_VI_FEEDBACK_MONO_1;
        case SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED:
        case SND_DEVICE_OUT_VOICE_SPEAKER_2_PROTECTED_VBAT:
             return SND_DEVICE_IN_CAPTURE_VI_FEEDBACK_MONO_2;
        default:
             return SND_DEVICE_IN_CAPTURE_VI_FEEDBACK;
    }
}

int platform_spkr_prot_is_wsa_analog_mode(void *adev __unused)
{
   struct audio_device *adev_h = adev;
   const char *snd_card_name;

   /*
    * wsa analog mode is decided based on the sound card name
    */
   snd_card_name = mixer_get_name(adev_h->mixer);
   if ((!strcmp(snd_card_name, "msm8953-snd-card-mtp")) ||
       (!strcmp(snd_card_name, "msm8953-sku4-snd-card")) ||
       (!strcmp(snd_card_name, "sdm439-sku1-snd-card")) ||
       (!strcmp(snd_card_name, "sdm439-snd-card-mtp")))
       return 1;
   else
       return 0;
}

/*
 * This is a lookup table to map android audio input device to audio h/w interface (backend).
 * The table can be extended for other input devices by adding appropriate entries.
 * Also the audio interface for a particular input device can be overriden by adding
 * corresponding entry in audio_platform_info.xml file.
 */
struct audio_device_to_audio_interface audio_device_to_interface_table[] = {
    {AUDIO_DEVICE_IN_BUILTIN_MIC, ENUM_TO_STRING(AUDIO_DEVICE_IN_BUILTIN_MIC), "SLIMBUS_0"},
    {AUDIO_DEVICE_IN_BACK_MIC, ENUM_TO_STRING(AUDIO_DEVICE_IN_BACK_MIC), "SLIMBUS_0"},
};

int audio_device_to_interface_table_len  =
    sizeof(audio_device_to_interface_table) / sizeof(audio_device_to_interface_table[0]);

int platform_set_audio_device_interface(const char *device_name, const char *intf_name,
                                        const char *codec_type __unused)
{
    int ret = 0;
    int i;

    if (device_name == NULL || intf_name == NULL) {
        ALOGE("%s: Invalid input", __func__);

        ret = -EINVAL;
        goto done;
    }

    ALOGD("%s: Enter, device name:%s, intf name:%s", __func__, device_name, intf_name);

    size_t device_name_len = strlen(device_name);
    for (i = 0; i < audio_device_to_interface_table_len; i++) {
        char* name = audio_device_to_interface_table[i].device_name;
        size_t name_len = strlen(name);
        if ((name_len == device_name_len) &&
            (strncmp(device_name, name, name_len) == 0)) {
            ALOGD("%s: Matched device name:%s, overwrite intf name with %s",
                  __func__, device_name, intf_name);

            strlcpy(audio_device_to_interface_table[i].interface_name, intf_name,
                    sizeof(audio_device_to_interface_table[i].interface_name));
            goto done;
        }
    }
    ALOGE("%s: Could not find matching device name %s",
            __func__, device_name);

    ret = -EINVAL;

done:
    return ret;
}

int platform_set_snd_device_name(snd_device_t device, const char *name)
{
    int ret = 0;

    if ((device < SND_DEVICE_MIN) || (device >= SND_DEVICE_MAX)) {
        ALOGE("%s:: Invalid snd_device = %d", __func__, device);
        ret = -EINVAL;
        goto done;
    }

    device_table[device] = strdup(name);
done:
    return ret;
}

int platform_set_sidetone(struct audio_device *adev,
                          snd_device_t out_snd_device,
                          bool enable,
                          char *str)
{
    int ret;
    if ((out_snd_device == SND_DEVICE_OUT_USB_HEADSET) ||
        (out_snd_device == SND_DEVICE_OUT_USB_HEADPHONES) ||
        (out_snd_device == SND_DEVICE_OUT_VOICE_USB_HEADSET)) {
        if (property_get_bool("vendor.audio.usb.disable.sidetone", 0)) {
            ALOGI("Debug: Disable sidetone");
        } else {
            ret = audio_extn_usb_enable_sidetone(out_snd_device, enable);
            if (ret) {
                /*fall back to AFE sidetone*/
                ALOGV("%s: No USB sidetone supported, switching to AFE sidetone",
                        __func__);

                if (enable)
                    audio_route_apply_and_update_path(adev->audio_route, AFE_SIDETONE_MIXER_PATH);
                else
                    audio_route_reset_and_update_path(adev->audio_route, AFE_SIDETONE_MIXER_PATH);
            }
        }
    } else {
        ALOGV("%s: sidetone out device(%d) mixer cmd = %s\n",
              __func__, out_snd_device, str);

        if (enable) {
            ret = audio_route_apply_and_update_path(adev->audio_route, str);
            if (ret) {
                ALOGV("%s: No device sidetone supported, switching to AFE sidetone",
                        __func__);
                audio_route_apply_and_update_path(adev->audio_route, AFE_SIDETONE_MIXER_PATH);
            }
        }
        else {
            ret = audio_route_reset_and_update_path(adev->audio_route, str);
            if (ret) {
                 ALOGV("%s: No device sidetone supported, switching to AFE sidetone",
                        __func__);
                audio_route_reset_and_update_path(adev->audio_route, AFE_SIDETONE_MIXER_PATH);
            }
        }
    }
    return 0;
}

void platform_update_aanc_path(struct audio_device *adev,
                               snd_device_t out_snd_device,
                               bool enable,
                               char *str)
{
    ALOGD("%s: aanc out device(%d) mixer cmd = %s, enable = %d\n",
          __func__, out_snd_device, str, enable);

    if (enable)
        audio_route_apply_and_update_path(adev->audio_route, str);
    else
        audio_route_reset_and_update_path(adev->audio_route, str);

    return;
}

#ifdef INSTANCE_ID_ENABLED
void platform_make_cal_cfg(acdb_audio_cal_cfg_t* cal, int acdb_dev_id,
        int acdb_device_type, int app_type, int topology_id,
        int sample_rate, uint32_t module_id, uint16_t instance_id,
        uint32_t param_id, bool persist)
{
    int persist_send_flags = 1;

    if (!cal) {
        return;
    }

    if (persist)
        persist_send_flags |= 0x2;

    memset(cal, 0, sizeof(acdb_audio_cal_cfg_t));

    cal->persist = persist;
    cal->app_type = app_type;
    cal->acdb_dev_id = acdb_dev_id;
    cal->sampling_rate = sample_rate;
    cal->topo_id = topology_id;
    //if module and param id is set to 0, the whole blob will be stored
    //or sent to the DSP
    cal->module_id = module_id;
    cal->instance_id = instance_id;
    cal->param_id = param_id;
    cal->cal_type = acdb_device_type;
}
#else
void platform_make_cal_cfg(acdb_audio_cal_cfg_t* cal, int acdb_dev_id,
        int acdb_device_type, int app_type, int topology_id,
        int sample_rate, uint32_t module_id, uint32_t param_id, bool persist)
{
    int persist_send_flags = 1;

    if (!cal) {
        return;
    }

    if (persist)
        persist_send_flags |= 0x2;

    memset(cal, 0, sizeof(acdb_audio_cal_cfg_t));

    cal->persist = persist;
    cal->app_type = app_type;
    cal->acdb_dev_id = acdb_dev_id;
    cal->sampling_rate = sample_rate;
    cal->topo_id = topology_id;
    //if module and param id is set to 0, the whole blob will be stored
    //or sent to the DSP
    cal->module_id = module_id;
    cal->param_id = param_id;
    cal->cal_type = acdb_device_type;
}
#endif

int platform_send_audio_cal(void* platform, acdb_audio_cal_cfg_t* cal,
       void* data, int length, bool persist)
{
    int ret = 0;
    struct platform_data *my_data = (struct platform_data *)platform;

    if (!my_data) {
        ret = -EINVAL;
        goto ERROR_RETURN;
    }
    if ((cal->acdb_dev_id == ACDB_ID_STEREO_SPEAKER_DEVICE) &&
       (cal->topo_id == TRUMPET_TOPOLOGY))
        audio_extn_ip_hdlr_copp_update_cal_info((void*)cal, data);

    if (my_data->acdb_set_audio_cal) {
        // persist audio cal in local cache
        if (persist) {
            ret = my_data->acdb_set_audio_cal((void*)cal, data, (uint32_t)length);
        }
        // send audio cal to dsp
        if (ret == 0) {
            cal->persist = false;
            ret = my_data->acdb_set_audio_cal((void*)cal, data, (uint32_t)length);
            if (persist && (ret != 0)) {
                ALOGV("[%s] audio cal stored with success, ignore set cal failure", __func__);
                ret = 0;
            }
        }
    }

ERROR_RETURN:
    return ret;
}

int platform_get_audio_cal(void* platform, acdb_audio_cal_cfg_t* cal,
       void* data, int* length, bool persist)
{
    int ret = 0;
    struct platform_data *my_data = (struct platform_data *)platform;

    if (!my_data) {
        ret = -EINVAL;
        goto ERROR_RETURN;
    }

    if (my_data->acdb_get_audio_cal) {
        // get cal from dsp
        ret = my_data->acdb_get_audio_cal((void*)cal, data, (uint32_t*)length);
        // get cached cal if prevoius attempt fails and persist flag is set
        if ((ret != 0) && persist) {
            cal->persist = true;
            ret = my_data->acdb_get_audio_cal((void*)cal, data, (uint32_t*)length);
        }
    }

ERROR_RETURN:
    return ret;
}

int platform_store_audio_cal(void* platform, acdb_audio_cal_cfg_t* cal,
       void* data, int length)
{
    int ret = 0;
    struct platform_data *my_data = (struct platform_data *)platform;

    if (!my_data) {
        ret = -EINVAL;
        goto ERROR_RETURN;
    }

    if (my_data->acdb_set_audio_cal) {
        ret = my_data->acdb_set_audio_cal((void*)cal, data, (uint32_t)length);
    }

ERROR_RETURN:
    return ret;
}

int platform_retrieve_audio_cal(void* platform, acdb_audio_cal_cfg_t* cal,
        void* data, int* length)
{
    int ret = 0;
    struct platform_data *my_data = (struct platform_data *)platform;

    if (!my_data) {
        ret = -EINVAL;
        goto ERROR_RETURN;
    }

    if (my_data->acdb_get_audio_cal) {
        ret = my_data->acdb_get_audio_cal((void*)cal, data, (uint32_t*)length);
    }

ERROR_RETURN:
    return ret;
}

int platform_get_max_mic_count(void *platform) {
    struct platform_data *my_data = (struct platform_data *)platform;
    return my_data->max_mic_count;
}

#define DEFAULT_NOMINAL_SPEAKER_GAIN 20
int ramp_speaker_gain(struct audio_device *adev, bool ramp_up, int target_ramp_up_gain) {
    // backup_gain: gain to try to set in case of an error during ramp
    int start_gain, end_gain, step, backup_gain, i;
    bool error = false;
    const char *mixer_ctl_name_gain_left = "Left Speaker Gain";
    const char *mixer_ctl_name_gain_right = "Right Speaker Gain";
    struct mixer_ctl *ctl_left = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name_gain_left);
    struct mixer_ctl *ctl_right = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name_gain_right);
    if (!ctl_left || !ctl_right) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s or %s, not applying speaker gain ramp",
                      __func__, mixer_ctl_name_gain_left, mixer_ctl_name_gain_right);
        return -EINVAL;
    } else if ((mixer_ctl_get_num_values(ctl_left) != 1)
            || (mixer_ctl_get_num_values(ctl_right) != 1)) {
        ALOGE("%s: Unexpected num values for mixer cmd - %s or %s, not applying speaker gain ramp",
                              __func__, mixer_ctl_name_gain_left, mixer_ctl_name_gain_right);
        return -EINVAL;
    }
    if (ramp_up) {
        start_gain = 0;
        end_gain = target_ramp_up_gain > 0 ? target_ramp_up_gain : DEFAULT_NOMINAL_SPEAKER_GAIN;
        step = +1;
        backup_gain = end_gain;
    } else {
        // using same gain on left and right
        const int left_gain = mixer_ctl_get_value(ctl_left, 0);
        start_gain = left_gain > 0 ? left_gain : DEFAULT_NOMINAL_SPEAKER_GAIN;
        end_gain = 0;
        step = -1;
        backup_gain = start_gain;
    }
    for (i = start_gain ; i != (end_gain + step) ; i += step) {
        if (mixer_ctl_set_value(ctl_left, 0, i)) {
            ALOGE("%s: error setting %s to %d during gain ramp",
                    __func__, mixer_ctl_name_gain_left, i);
            error = true;
            break;
        }
        if (mixer_ctl_set_value(ctl_right, 0, i)) {
            ALOGE("%s: error setting %s to %d during gain ramp",
                    __func__, mixer_ctl_name_gain_right, i);
            error = true;
            break;
        }
        usleep(1000);
    }
    if (error) {
        // an error occured during the ramp, let's still try to go back to a safe volume
        if (mixer_ctl_set_value(ctl_left, 0, backup_gain)) {
            ALOGE("%s: error restoring left gain to %d", __func__, backup_gain);
        }
        if (mixer_ctl_set_value(ctl_right, 0, backup_gain)) {
            ALOGE("%s: error restoring right gain to %d", __func__, backup_gain);
        }
    }
    return start_gain;
}

int platform_set_swap_mixer(struct audio_device *adev, bool swap_channels)
{
    const char *mixer_ctl_name = "Swap channel";
    struct mixer_ctl *ctl;
    struct platform_data *my_data = (struct platform_data *)adev->platform;

    // forced to set to swap, but device not rotated ... ignore set
    if (swap_channels && !my_data->speaker_lr_swap)
        return 0;

    ALOGV("%s:", __func__);

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",__func__, mixer_ctl_name);
        return -EINVAL;
    }

    if (mixer_ctl_set_value(ctl, 0, swap_channels) < 0) {
        ALOGE("%s: Could not set reverse cotrol %d",__func__, swap_channels);
        return -EINVAL;
    }

    ALOGV("platfor_force_swap_channel :: Channel orientation ( %s ) ",
           swap_channels?"R --> L":"L --> R");

    return 0;
}

int platform_check_and_set_swap_lr_channels(struct audio_device *adev, bool swap_channels)
{
    // only update if there is active pcm playback on speaker
    struct platform_data *my_data = (struct platform_data *)adev->platform;

    my_data->speaker_lr_swap = swap_channels;

    return platform_set_swap_channels(adev, swap_channels);
}

int platform_set_swap_channels(struct audio_device *adev, bool swap_channels)
{
    // only update if there is active pcm playback on speaker
    struct audio_usecase *usecase;
    struct listnode *node;

    //swap channels only for stereo spkr
    struct platform_data *my_data = (struct platform_data *)adev->platform;
    if (my_data) {
        if (!hw_info_is_stereo_spkr(my_data->hw_info)) {
            ALOGV("%s: will not swap due to it is not stereo spkr", __func__);
            return 0;
        }
    } else {
        ALOGE("%s: failed to allocate platform data", __func__);
        return -EINVAL;
    }

    // do not swap channels in audio modes with concurrent capture and playback
    // as this may break the echo reference
    if ((adev->mode == AUDIO_MODE_IN_COMMUNICATION) || (adev->mode == AUDIO_MODE_IN_CALL)) {
        ALOGV("%s: will not swap due to audio mode %d", __func__, adev->mode);
        return 0;
    }

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->stream.out && usecase->type == PCM_PLAYBACK &&
            compare_device_type(&usecase->stream.out->device_list,
                                AUDIO_DEVICE_OUT_SPEAKER)) {
            /*
             * If acdb tuning is different for SPEAKER_REVERSE, it is must
             * to perform device switch to disable the current backend to
             * enable it with new acdb data.
             */
            if (my_data->speaker_lr_swap &&
                (acdb_device_table[SND_DEVICE_OUT_SPEAKER] !=
                acdb_device_table[SND_DEVICE_OUT_SPEAKER_REVERSE])) {
                const int initial_skpr_gain = ramp_speaker_gain(adev, false /*ramp_up*/, -1);
                select_devices(adev, usecase->id);
                if (initial_skpr_gain != -EINVAL)
                    ramp_speaker_gain(adev, true /*ramp_up*/, initial_skpr_gain);

            } else {
                platform_set_swap_mixer(adev, swap_channels);
            }
            break;
        }
    }

    return 0;
}

static struct amp_db_and_gain_table tbl_mapping[MAX_VOLUME_CAL_STEPS];
static int num_gain_tbl_entry = 0;

bool platform_add_gain_level_mapping(struct amp_db_and_gain_table *tbl_entry) {

    ALOGV("%s: enter .. add %f %f %d", __func__, tbl_entry->amp, tbl_entry->db, tbl_entry->level);
    if (num_gain_tbl_entry == -1) {
        ALOGE("%s: num entry beyond valid step levels or corrupted..rejecting custom mapping",
               __func__);
        return false;
    }

    if (num_gain_tbl_entry >= MAX_VOLUME_CAL_STEPS) {
        ALOGE("%s: max entry reached max[%d] current index[%d]  .. rejecting", __func__,
               MAX_VOLUME_CAL_STEPS, num_gain_tbl_entry);
        num_gain_tbl_entry  = -1; // indicates error and no more info will be cached
        return false;
    }

    if (num_gain_tbl_entry > 0 && tbl_mapping[num_gain_tbl_entry - 1].amp >= tbl_entry->amp) {
        ALOGE("%s: value not in ascending order .. rejecting custom mapping", __func__);
        num_gain_tbl_entry  = -1; // indicates error and no more info will be cached
        return false;
    }

    tbl_mapping[num_gain_tbl_entry] = *tbl_entry;
    ++num_gain_tbl_entry;

    return true;
}

int platform_get_gain_level_mapping(struct amp_db_and_gain_table *mapping_tbl,
                                    int table_size) {
    int itt = 0;
    ALOGV("platform_get_gain_level_mapping called ");

    if (num_gain_tbl_entry <= 0 || num_gain_tbl_entry > MAX_VOLUME_CAL_STEPS) {
        ALOGD("%s: empty or currupted gain_mapping_table", __func__);
        return 0;
    }

    for (; itt < num_gain_tbl_entry && itt <= table_size; itt++) {
        mapping_tbl[itt] = tbl_mapping[itt];
        ALOGV("%s: added amp[%f] db[%f] level[%d]", __func__,
               mapping_tbl[itt].amp, mapping_tbl[itt].db, mapping_tbl[itt].level);
    }

    return num_gain_tbl_entry;
}

int platform_get_max_codec_backend() {

    return MAX_CODEC_BACKENDS;
}
int platform_get_supported_copp_sampling_rate(uint32_t stream_sr)
{
    int sample_rate;
    switch (stream_sr){
        case 8000:
        case 11025:
        case 16000:
        case 22050:
        case 32000:
        case 48000:
            sample_rate = 48000;
            break;
        case 44100:
            sample_rate = 44100;
            break;
        case 64000:
        case 96000:
            sample_rate = 96000;
            break;
        case 88200:
            sample_rate = 88200;
            break;
        case 176400:
            sample_rate = 176400;
            break;
        case 192000:
            sample_rate = 192000;
            break;
        case 352800:
            sample_rate = 352800;
            break;
        case 384000:
            sample_rate = 384000;
            break;
        case 144000:
        default:
            sample_rate = 48000;
            break;
    }
    return sample_rate;
}

#if defined (PLATFORM_MSM8998) || (PLATFORM_SDM845) || (PLATFORM_SDM710) || \
    defined (PLATFORM_QCS605) || defined (PLATFORM_MSMNILE) || \
    defined (PLATFORM_KONA) || defined (PLATFORM_MSMSTEPPE) || \
    defined (PLATFORM_QCS405) || defined (PLATFORM_TRINKET) || \
    defined (PLATFORM_LITO) || defined (PLATFORM_MSMFALCON) || \
    defined (PLATFORM_ATOLL) || defined (PLATFORM_BENGAL) || \
    defined (PLATFORM_HOLI)
int platform_get_mmap_data_fd(void *platform, int fe_dev, int dir, int *fd,
                              uint32_t *size)
{
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_device *adev = my_data->adev;
    int hw_fd = -1;
    char dev_name[128];
    struct snd_pcm_mmap_fd mmap_fd;
    memset(&mmap_fd, 0, sizeof(mmap_fd));
    mmap_fd.dir = dir;
    snprintf(dev_name, sizeof(dev_name), "/dev/snd/hwC%uD%u",
             adev->snd_card, HWDEP_FE_BASE+fe_dev);
    hw_fd = open(dev_name, O_RDONLY);
    if (hw_fd < 0) {
        ALOGE("fe hw dep node open %d/%d failed", adev->snd_card, fe_dev);
        return -1;
    }
    if (ioctl(hw_fd, SNDRV_PCM_IOCTL_MMAP_DATA_FD, &mmap_fd) < 0) {
        ALOGE("fe hw dep node ioctl failed");
        close(hw_fd);
        return -1;
    }
    *fd = mmap_fd.fd;
    *size = mmap_fd.size;
    close(hw_fd); // mmap_fd should still be valid
    return 0;
}
#else
int platform_get_mmap_data_fd(void *platform __unused, int fe_dev __unused,
                              int dir __unused, int *fd __unused,
                              uint32_t *size __unused)
{
    return -1;
}
#endif

static const char *platform_get_mixer_control(struct mixer_ctl *ctl)
{
    int id = -1;
    const char *id_string = NULL;

    if (!ctl) {
        ALOGD("%s: mixer ctl not obtained", __func__);
    } else {
        id = mixer_ctl_get_value(ctl, 0);
        if (id >= 0) {
            id_string = mixer_ctl_get_enum_string(ctl, id);
        }
    }

    return id_string;
}

bool platform_set_microphone_characteristic(void *platform,
                                            struct audio_microphone_characteristic_t mic) {
    struct platform_data *my_data = (struct platform_data *)platform;
    if (my_data->declared_mic_count >= AUDIO_MICROPHONE_MAX_COUNT) {
        ALOGE("mic number is more than maximum number");
        return false;
    }
    for (size_t ch = 0; ch < AUDIO_CHANNEL_COUNT_MAX; ch++) {
        mic.channel_mapping[ch] = AUDIO_MICROPHONE_CHANNEL_MAPPING_UNUSED;
    }
    my_data->microphones[my_data->declared_mic_count++] = mic;
    return true;
}

int platform_get_microphones(void *platform,
                             struct audio_microphone_characteristic_t *mic_array,
                             size_t *mic_count) {
    struct platform_data *my_data = (struct platform_data *)platform;
    if (mic_count == NULL) {
        return -EINVAL;
    }
    if (mic_array == NULL) {
        return -EINVAL;
    }

    if (*mic_count == 0) {
        *mic_count = my_data->declared_mic_count;
        return 0;
    }

    size_t max_mic_count = *mic_count;
    size_t actual_mic_count = 0;
    for (size_t i = 0; i < max_mic_count && i < my_data->declared_mic_count; i++) {
        mic_array[i] = my_data->microphones[i];
        actual_mic_count++;
    }
    *mic_count = actual_mic_count;
    return 0;
}

bool platform_set_microphone_map(void *platform, snd_device_t in_snd_device,
                                 const struct mic_info *info) {
    struct platform_data *my_data = (struct platform_data *)platform;
    if (in_snd_device < SND_DEVICE_IN_BEGIN || in_snd_device >= SND_DEVICE_IN_END) {
        ALOGE("%s: Sound device not valid", __func__);
        return false;
    }
    size_t m_count = my_data->mic_map[in_snd_device].mic_count++;
    if (m_count >= AUDIO_MICROPHONE_MAX_COUNT) {
        ALOGE("%s: Microphone count is greater than max allowed value", __func__);
        my_data->mic_map[in_snd_device].mic_count--;
        return false;
    }
    my_data->mic_map[in_snd_device].microphones[m_count] = *info;
    return true;
}

int platform_get_active_microphones(void *platform, unsigned int channels,
                                    audio_usecase_t uc_id,
                                    struct audio_microphone_characteristic_t *mic_array,
                                    size_t *mic_count) {
    struct platform_data *my_data = (struct platform_data *)platform;
    struct audio_usecase *usecase = get_usecase_from_list(my_data->adev, uc_id);
    if (mic_count == NULL || mic_array == NULL || usecase == NULL) {
        return -EINVAL;
    }
    size_t max_mic_count = my_data->declared_mic_count;
    size_t actual_mic_count = 0;
    struct listnode devices;
    list_init(&devices);

    snd_device_t active_input_snd_device =
            platform_get_input_snd_device(platform, usecase->stream.in, &devices, USECASE_TYPE_MAX);
    if (active_input_snd_device == SND_DEVICE_NONE) {
        ALOGI("%s: No active microphones found", __func__);
        goto end;
    }

    size_t  active_mic_count = my_data->mic_map[active_input_snd_device].mic_count;
    struct mic_info *m_info = my_data->mic_map[active_input_snd_device].microphones;

    for (size_t i = 0; i < active_mic_count; i++) {
        unsigned int channels_for_active_mic = channels;
        if (channels_for_active_mic > m_info[i].channel_count) {
            channels_for_active_mic = m_info[i].channel_count;
        }
        for (size_t j = 0; j < max_mic_count; j++) {
            if (strcmp(my_data->microphones[j].device_id,
                       m_info[i].device_id) == 0) {
                mic_array[actual_mic_count] = my_data->microphones[j];
                for (size_t ch = 0; ch < channels_for_active_mic; ch++) {
                     mic_array[actual_mic_count].channel_mapping[ch] =
                             m_info[i].channel_mapping[ch];
                }
                actual_mic_count++;
                break;
            }
        }
    }
end:
    *mic_count = actual_mic_count;
    return 0;
}

int platform_get_license_by_product(void *platform __unused,
                                    const char* product_name __unused,
                                    int *product_id __unused,
                                    char* product_license __unused)
{
    return -ENOSYS;
}

int platform_get_controller_stream_from_params(struct str_parms *parms,
                                               int *controller, int *stream) {
    str_parms_get_int(parms, "controller", controller);
    str_parms_get_int(parms, "stream", stream);
    if (*controller < 0 || *controller >= MAX_CONTROLLERS ||
            *stream < 0 || *stream >= MAX_STREAMS_PER_CONTROLLER) {
        *controller = 0;
        *stream = 0;
        return -1;
    }
    return 0;
}

bool platform_is_call_proxy_snd_device(snd_device_t snd_device) {
    if (snd_device == SND_DEVICE_IN_CALL_PROXY || snd_device == SND_DEVICE_OUT_CALL_PROXY)
        return true;
    return false;
}
