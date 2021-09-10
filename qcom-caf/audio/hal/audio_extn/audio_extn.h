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

#ifndef AUDIO_EXTN_H
#define AUDIO_EXTN_H

#include <cutils/str_parms.h>
#include "adsp_hdlr.h"
#include "audio_hidl.h"
#include "ip_hdlr_intf.h"
#include "platform_api.h"
#include "edid.h"
#include "battery_listener.h"

#define AUDIO_PARAMETER_DUAL_MONO  "dual_mono"

#ifndef AUDIO_DEVICE_IN_PROXY
#define AUDIO_DEVICE_IN_PROXY (AUDIO_DEVICE_BIT_IN | 0x1000000)
#endif

#ifndef AUDIO_DEVICE_IN_HDMI_ARC
#define AUDIO_DEVICE_IN_HDMI_ARC (AUDIO_DEVICE_BIT_IN | 0x8000000)
#endif

// #ifndef INCALL_MUSIC_ENABLED
// #define AUDIO_OUTPUT_FLAG_INCALL_MUSIC 0x80000000 //0x8000
// #endif

#ifndef AUDIO_FORMAT_AAC_LATM
#define AUDIO_FORMAT_AAC_LATM 0x80000000UL
#define AUDIO_FORMAT_AAC_LATM_LC   (AUDIO_FORMAT_AAC_LATM |\
                                      AUDIO_FORMAT_AAC_SUB_LC)
#define AUDIO_FORMAT_AAC_LATM_HE_V1 (AUDIO_FORMAT_AAC_LATM |\
                                      AUDIO_FORMAT_AAC_SUB_HE_V1)
#define AUDIO_FORMAT_AAC_LATM_HE_V2  (AUDIO_FORMAT_AAC_LATM |\
                                      AUDIO_FORMAT_AAC_SUB_HE_V2)
#endif

#ifndef AUDIO_FORMAT_AC4
#define AUDIO_FORMAT_AC4  0x22000000UL
#endif

#ifndef AUDIO_FORMAT_LDAC
#define AUDIO_FORMAT_LDAC 0x23000000UL
#endif

#ifndef AUDIO_OUTPUT_FLAG_MAIN
#define AUDIO_OUTPUT_FLAG_MAIN 0x8000000
#endif

#ifndef AUDIO_OUTPUT_FLAG_ASSOCIATED
#define AUDIO_OUTPUT_FLAG_ASSOCIATED 0x10000000
#endif

#ifndef AUDIO_OUTPUT_FLAG_TIMESTAMP
#define AUDIO_OUTPUT_FLAG_TIMESTAMP 0x20000000
#endif

#ifndef AUDIO_OUTPUT_FLAG_BD
#define AUDIO_OUTPUT_FLAG_BD 0x40000000
#endif

#ifndef AUDIO_OUTPUT_FLAG_INTERACTIVE
#define AUDIO_OUTPUT_FLAG_INTERACTIVE 0x4000000
#endif

int audio_extn_parse_compress_metadata(struct stream_out *out,
                                       struct str_parms *parms);

#define AUDIO_OUTPUT_BIT_WIDTH ((config->offload_info.bit_width == 32) ? 24\
                                   :config->offload_info.bit_width)

#ifndef ENABLE_EXTENDED_COMPRESS_FORMAT
#define compress_set_metadata(compress, metadata) (0)
#define compress_get_metadata(compress, metadata) (0)
#define compress_set_next_track_param(compress, codec_options) (0)
#endif

#define MAX_LENGTH_MIXER_CONTROL_IN_INT                  (128)
#define HW_INFO_ARRAY_MAX_SIZE 32

#define AUDIO_PARAMETER_KEY_HIFI_AUDIO_FILTER "hifi_filter"

#define VENDOR_CONFIG_PATH_MAX_LENGTH 128
#define VENDOR_CONFIG_FILE_MAX_LENGTH 128

struct snd_card_split {
    char device[HW_INFO_ARRAY_MAX_SIZE];
    char snd_card[HW_INFO_ARRAY_MAX_SIZE];
    char form_factor[HW_INFO_ARRAY_MAX_SIZE];
    char variant[HW_INFO_ARRAY_MAX_SIZE];
};

struct snd_card_split *audio_extn_get_snd_card_split();

// -- function pointers needed for audio extn
typedef void (*fp_platform_make_cal_cfg_t)(acdb_audio_cal_cfg_t *, int, int,
                                         int, int, int, uint32_t, uint16_t,
                                         uint32_t, bool);
typedef int (*fp_platform_send_audio_cal_t)(void *, acdb_audio_cal_cfg_t *,
                                            void *, int, bool);
typedef int (*fp_platform_get_audio_cal_t)(void *, acdb_audio_cal_cfg_t *,
                                           void *, int *, bool);
typedef int (*fp_platform_store_audio_cal_t)(void *, acdb_audio_cal_cfg_t *,
                                             void *, int);
typedef int (*fp_platform_retrieve_audio_cal_t)(void *, acdb_audio_cal_cfg_t *,
                                             void *, int *);

typedef struct gef_init_config {
    fp_platform_make_cal_cfg_t         fp_platform_make_cal_cfg;
    fp_platform_send_audio_cal_t       fp_platform_send_audio_cal;
    fp_platform_get_audio_cal_t        fp_platform_get_audio_cal;
    fp_platform_store_audio_cal_t      fp_platform_store_audio_cal;
    fp_platform_retrieve_audio_cal_t   fp_platform_retrieve_audio_cal;
} gef_init_config_t;

typedef int (*fp_read_line_from_file_t)(const char *, char *, size_t);
typedef struct audio_usecase *(*fp_get_usecase_from_list_t)(const struct audio_device *,
                                            audio_usecase_t);
typedef int (*fp_enable_disable_snd_device_t)(struct audio_device *, snd_device_t);
typedef int (*fp_enable_disable_audio_route_t)(struct audio_device *, struct audio_usecase *);
typedef int (*fp_platform_set_snd_device_backend_t)(snd_device_t, const char *,
                                    const char *);
typedef int (*fp_platform_get_snd_device_name_extn_t)(void *platform, snd_device_t snd_device,
                                      char *device_name);
typedef int (*fp_platform_get_default_app_type_v2_t)(void *, usecase_type_t);
typedef int (*fp_platform_send_audio_calibration_t)(void *, struct audio_usecase *,
                                                   int);
typedef int (*fp_platform_get_pcm_device_id_t)(audio_usecase_t, int);
typedef const char *(*fp_platform_get_snd_device_name_t)(snd_device_t);
typedef int (*fp_platform_spkr_prot_is_wsa_analog_mode_t)(void *);
typedef int (*fp_platform_get_snd_device_t)(snd_device_t);
typedef bool(*fp_platform_check_and_set_codec_backend_cfg_t)(struct audio_device*,
                                      struct audio_usecase *, snd_device_t);
typedef struct snd_card_split *(*fp_audio_extn_get_snd_card_split_t)();
typedef bool (*fp_audio_extn_is_vbat_enabled_t)(void);

struct spkr_prot_init_config {
    fp_read_line_from_file_t                       fp_read_line_from_file;
    fp_get_usecase_from_list_t                     fp_get_usecase_from_list;
    fp_enable_disable_snd_device_t                 fp_disable_snd_device;
    fp_enable_disable_snd_device_t                 fp_enable_snd_device;
    fp_enable_disable_audio_route_t                fp_disable_audio_route;
    fp_enable_disable_audio_route_t                fp_enable_audio_route;
    fp_platform_set_snd_device_backend_t           fp_platform_set_snd_device_backend;
    fp_platform_get_snd_device_name_extn_t         fp_platform_get_snd_device_name_extn;
    fp_platform_get_default_app_type_v2_t          fp_platform_get_default_app_type_v2;
    fp_platform_send_audio_calibration_t           fp_platform_send_audio_calibration;
    fp_platform_get_pcm_device_id_t                fp_platform_get_pcm_device_id;
    fp_platform_get_snd_device_name_t              fp_platform_get_snd_device_name;
    fp_platform_spkr_prot_is_wsa_analog_mode_t     fp_platform_spkr_prot_is_wsa_analog_mode;
    fp_platform_get_snd_device_t                   fp_platform_get_vi_feedback_snd_device;
    fp_platform_get_snd_device_t                   fp_platform_get_spkr_prot_snd_device;
    fp_platform_check_and_set_codec_backend_cfg_t  fp_platform_check_and_set_codec_backend_cfg;
    fp_audio_extn_get_snd_card_split_t             fp_audio_extn_get_snd_card_split;
    fp_audio_extn_is_vbat_enabled_t                fp_audio_extn_is_vbat_enabled;
};

typedef struct spkr_prot_init_config spkr_prot_init_config_t;

// call at adev init
void audio_extn_init(struct audio_device *adev);
void audio_extn_feature_init();
//START: SND_MONITOR_FEATURE ===========================================
void snd_mon_feature_init (bool is_feature_enabled);
typedef void (* snd_mon_cb)(void * stream, struct str_parms * parms);

int audio_extn_snd_mon_init();
int audio_extn_snd_mon_deinit();
int audio_extn_snd_mon_register_listener(void *stream, snd_mon_cb cb);
int audio_extn_snd_mon_unregister_listener(void *stream);
//END: SND_MONITOR_FEATURE   ===========================================

//START: EXTN_QDSP_PLUGIN    ===========================================

void audio_extn_qdsp_init(void *platform);
void audio_extn_qdsp_deinit();
bool audio_extn_qdsp_set_state(struct audio_device *adev, int stream_type,
                             float vol, bool active);
void audio_extn_qdsp_set_device(struct audio_usecase *usecase);
void audio_extn_qdsp_set_parameters(struct audio_device *adev,
                                  struct str_parms *parms);
bool audio_extn_qdsp_supported_usb();

//END: EXTN_QDSP_PLUGIN      ===========================================

#define MIN_OFFLOAD_BUFFER_DURATION_MS 5 /* 5ms */
#define MAX_OFFLOAD_BUFFER_DURATION_MS (100 * 1000) /* 100s */

void audio_extn_set_parameters(struct audio_device *adev,
                               struct str_parms *parms);

void audio_extn_get_parameters(const struct audio_device *adev,
                               struct str_parms *query,
                               struct str_parms *reply);


bool audio_extn_get_anc_enabled(void);
bool audio_extn_should_use_fb_anc(void);
bool audio_extn_should_use_handset_anc(int in_channels);
void audio_extn_set_aanc_noise_level(struct audio_device *adev,
                                     struct str_parms *parms);

bool audio_extn_is_vbat_enabled(void);
bool audio_extn_can_use_vbat(void);
bool audio_extn_is_bcl_enabled(void);
bool audio_extn_can_use_bcl(void);

void ras_feature_init(bool is_feature_enabled);
bool audio_extn_is_ras_enabled(void);
bool audio_extn_can_use_ras(void);

//START: HIFI_AUDIO
void hifi_audio_feature_init(bool is_feature_enabled);
bool audio_extn_is_hifi_audio_enabled(void);
bool audio_extn_is_hifi_audio_supported(void);
//END: HIFI_AUDIO

//START: WSA
void wsa_feature_init(bool is_featuer_enabled);
bool audio_extn_is_wsa_enabled();
//END: WSA

//START: AFE_PROXY_FEATURE
int32_t audio_extn_set_afe_proxy_channel_mixer(struct audio_device *adev,
                                                    int channel_count,
                                                    snd_device_t snd_device);
int32_t audio_extn_read_afe_proxy_channel_masks(struct stream_out *out);
int32_t audio_extn_get_afe_proxy_channel_count();
//END: AFE_PROXY_FEATURE

//START: HIFI FILTER
void audio_extn_enable_hifi_filter(struct audio_device *adev, bool value);
void audio_extn_hifi_filter_set_params(struct str_parms *parms, char *value, int len);
bool audio_extn_is_hifi_filter_enabled(struct audio_device* adev,struct stream_out *out,
                                   snd_device_t snd_device, char *codec_variant,
                                   int channels, int usecase_init);
//END: HIFI FILTER

/// ---- USB feature ---------------------------------------------------------------
void audio_extn_usb_init(void *adev);
void audio_extn_usb_deinit();
void audio_extn_usb_add_device(audio_devices_t device, int card);
void audio_extn_usb_remove_device(audio_devices_t device, int card);
bool audio_extn_usb_is_config_supported(unsigned int *bit_width,
                                        unsigned int *sample_rate,
                                        unsigned int *ch,
                                        bool is_playback);
int audio_extn_usb_enable_sidetone(int device, bool enable);
void audio_extn_usb_set_sidetone_gain(struct str_parms *parms,
                                     char *value, int len);
bool audio_extn_usb_is_capture_supported();
int audio_extn_usb_get_max_channels(bool playback);
int audio_extn_usb_get_max_bit_width(bool playback);
int audio_extn_usb_get_sup_sample_rates(bool type, uint32_t *sr, uint32_t l);
bool audio_extn_usb_is_tunnel_supported();
bool audio_extn_usb_alive(int card);
bool audio_extn_usb_connected(struct str_parms *parms);
unsigned long audio_extn_usb_find_service_interval(bool min, bool playback);
int audio_extn_usb_altset_for_service_interval(bool is_playback,
                                               unsigned long service_interval,
                                               uint32_t *bit_width,
                                               uint32_t *sample_rate,
                                               uint32_t *channel_count);
char *audio_extn_usb_usbid(void);
int audio_extn_usb_set_service_interval(bool playback,
                                        unsigned long service_interval,
                                        bool *reconfig);
int audio_extn_usb_get_service_interval(bool playback,
                                        unsigned long *service_interval);
int audio_extn_usb_check_and_set_svc_int(struct audio_usecase *uc_info,
                                         bool starting_output_stream);
bool audio_extn_usb_is_reconfig_req();
void audio_extn_usb_set_reconfig(bool is_required);
bool audio_extn_usb_is_sidetone_volume_enabled();
//------------------------------------------------------------------------------------

// START: A2DP_OFFLOAD FEATURE ==================================================
int a2dp_offload_feature_init(bool is_feature_enabled);
void audio_extn_a2dp_init(void *adev);
int audio_extn_a2dp_start_playback();
int audio_extn_a2dp_stop_playback();
int audio_extn_a2dp_set_parameters(struct str_parms *parms, bool *reconfig);
int audio_extn_a2dp_get_parameters(struct str_parms *query,
                                   struct str_parms *reply);
bool audio_extn_a2dp_is_force_device_switch();
void audio_extn_a2dp_set_handoff_mode(bool is_on);
void audio_extn_a2dp_get_enc_sample_rate(int *sample_rate);
void audio_extn_a2dp_get_dec_sample_rate(int *sample_rate);
uint32_t audio_extn_a2dp_get_encoder_latency();
bool audio_extn_a2dp_sink_is_ready();
bool audio_extn_a2dp_source_is_ready();
bool audio_extn_a2dp_source_is_suspended();
int audio_extn_a2dp_start_capture();
int audio_extn_a2dp_stop_capture();
bool audio_extn_a2dp_set_source_backend_cfg();
int audio_extn_sco_start_configuration();
void audio_extn_sco_reset_configuration();


// --- Function pointers from audio_extn needed by A2DP_OFFLOAD
typedef int (*fp_check_a2dp_restore_t)(struct audio_device *,
                                       struct stream_out *, bool);
struct a2dp_offload_init_config {
    fp_platform_get_pcm_device_id_t fp_platform_get_pcm_device_id;
    fp_check_a2dp_restore_t fp_check_a2dp_restore_l;
};
typedef struct a2dp_offload_init_config a2dp_offload_init_config_t;
// END: A2DP_OFFLOAD FEATURE ====================================================

typedef int (*fp_platform_set_parameters_t)(void*, struct str_parms*);

// START: AUDIOZOOM FEATURE ==================================================
int audio_extn_audiozoom_init();
int audio_extn_audiozoom_set_microphone_direction(struct stream_in *stream,
                                           audio_microphone_direction_t dir);
int audio_extn_audiozoom_set_microphone_field_dimension(struct stream_in *stream, float zoom);
bool audio_extn_is_audiozoom_enabled();

struct audiozoom_init_config {
    fp_platform_set_parameters_t fp_platform_set_parameters;
};
typedef struct audiozoom_init_config audiozoom_init_config_t;
// END:   AUDIOZOOM FEATURE ==================================================

// START: MAXX_AUDIO FEATURE ==================================================
void audio_extn_ma_init(void *platform);
void audio_extn_ma_deinit();
bool audio_extn_ma_set_state(struct audio_device *adev, int stream_type,
                             float vol, bool active);
void audio_extn_ma_set_device(struct audio_usecase *usecase);
void audio_extn_ma_set_parameters(struct audio_device *adev,
                                  struct str_parms *parms);
void audio_extn_ma_get_parameters(struct audio_device *adev,
                                  struct str_parms *query,
                                  struct str_parms *reply);
bool audio_extn_ma_supported_usb();
bool audio_extn_is_maxx_audio_enabled();
// --- Function pointers from audio_extn needed by MAXX_AUDIO
struct maxx_audio_init_config {
    fp_platform_set_parameters_t fp_platform_set_parameters;
    fp_audio_extn_get_snd_card_split_t fp_audio_extn_get_snd_card_split;
};
typedef struct maxx_audio_init_config maxx_audio_init_config_t;
// START: MAXX_AUDIO FEATURE ==================================================
//START: SSRRC_FEATURE ==========================================================
bool audio_extn_ssr_check_usecase(struct stream_in *in);
int audio_extn_ssr_set_usecase(struct stream_in *in,
                                         struct audio_config *config,
                                         bool *channel_mask_updated);
int32_t audio_extn_ssr_init(struct stream_in *in,
                            int num_out_chan);
int32_t audio_extn_ssr_deinit();
void audio_extn_ssr_update_enabled();
bool audio_extn_ssr_get_enabled();
int32_t audio_extn_ssr_read(struct audio_stream_in *stream,
                       void *buffer, size_t bytes);
void audio_extn_ssr_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms);
void audio_extn_ssr_get_parameters(const struct audio_device *adev,
                                   struct str_parms *query,
                                   struct str_parms *reply);
struct stream_in *audio_extn_ssr_get_stream();
//END: SSREC_FEATURE ============================================================

int audio_extn_check_and_set_multichannel_usecase(struct audio_device *adev,
                                                  struct stream_in *in,
                                                  struct audio_config *config,
                                                  bool *update_params);

#ifndef HW_VARIANTS_ENABLED
#define hw_info_init(snd_card_name)                      (0)
#define hw_info_deinit(hw_info)                          (0)
#define hw_info_append_hw_type(hw_info,\
        snd_device, device_name)                         (0)
#define hw_info_enable_wsa_combo_usecase_support(hw_info)   (0)
#define hw_info_is_stereo_spkr(hw_info)   (0)

#else
void *hw_info_init(const char *snd_card_name);
void hw_info_deinit(void *hw_info);
void hw_info_append_hw_type(void *hw_info, snd_device_t snd_device,
                             char *device_name);
void hw_info_enable_wsa_combo_usecase_support(void *hw_info);
bool hw_info_is_stereo_spkr(void *hw_info);

#endif

#ifndef AUDIO_LISTEN_ENABLED
#define audio_extn_listen_init(adev, snd_card)                  (0)
#define audio_extn_listen_deinit(adev)                          (0)
#define audio_extn_listen_update_device_status(snd_dev, event)  (0)
#define audio_extn_listen_update_stream_status(uc_info, event)  (0)
#define audio_extn_listen_set_parameters(adev, parms)           (0)
#else
enum listen_event_type {
    LISTEN_EVENT_SND_DEVICE_FREE,
    LISTEN_EVENT_SND_DEVICE_BUSY,
    LISTEN_EVENT_STREAM_FREE,
    LISTEN_EVENT_STREAM_BUSY
};
typedef enum listen_event_type listen_event_type_t;

int audio_extn_listen_init(struct audio_device *adev, unsigned int snd_card);
void audio_extn_listen_deinit(struct audio_device *adev);
void audio_extn_listen_update_device_status(snd_device_t snd_device,
                                     listen_event_type_t event);
void audio_extn_listen_update_stream_status(struct audio_usecase *uc_info,
                                     listen_event_type_t event);
void audio_extn_listen_set_parameters(struct audio_device *adev,
                                      struct str_parms *parms);
#endif /* AUDIO_LISTEN_ENABLED */

#ifndef SOUND_TRIGGER_ENABLED
#define audio_extn_sound_trigger_init(adev)                            (0)
#define audio_extn_sound_trigger_deinit(adev)                          (0)
#define audio_extn_sound_trigger_update_device_status(snd_dev, event)  (0)
#define audio_extn_sound_trigger_update_stream_status(uc_info, event)  (0)
#define audio_extn_sound_trigger_update_battery_status(charging)       (0)
#define audio_extn_sound_trigger_update_screen_status(screen_off)      (0)
#define audio_extn_sound_trigger_set_parameters(adev, parms)           (0)
#define audio_extn_sound_trigger_get_parameters(adev, query, reply)    (0)
#define audio_extn_sound_trigger_check_and_get_session(in)             (0)
#define audio_extn_sound_trigger_stop_lab(in)                          (0)
#define audio_extn_sound_trigger_read(in, buffer, bytes)               (0)
#define audio_extn_sound_trigger_check_ec_ref_enable()                 (0)
#define audio_extn_sound_trigger_update_ec_ref_status(on)              (0)
#else

enum st_event_type {
    ST_EVENT_SND_DEVICE_FREE,
    ST_EVENT_SND_DEVICE_BUSY,
    ST_EVENT_STREAM_FREE,
    ST_EVENT_STREAM_BUSY
};
typedef enum st_event_type st_event_type_t;

int audio_extn_sound_trigger_init(struct audio_device *adev);
void audio_extn_sound_trigger_deinit(struct audio_device *adev);
void audio_extn_sound_trigger_update_device_status(snd_device_t snd_device,
                                     st_event_type_t event);
void audio_extn_sound_trigger_update_stream_status(struct audio_usecase *uc_info,
                                     st_event_type_t event);
void audio_extn_sound_trigger_update_battery_status(bool charging);
void audio_extn_sound_trigger_update_screen_status(bool screen_off);
void audio_extn_sound_trigger_set_parameters(struct audio_device *adev,
                                             struct str_parms *parms);
void audio_extn_sound_trigger_check_and_get_session(struct stream_in *in);
void audio_extn_sound_trigger_stop_lab(struct stream_in *in);
int audio_extn_sound_trigger_read(struct stream_in *in, void *buffer,
                                  size_t bytes);
void audio_extn_sound_trigger_get_parameters(const struct audio_device *adev,
                     struct str_parms *query, struct str_parms *reply);
bool audio_extn_sound_trigger_check_ec_ref_enable();
void audio_extn_sound_trigger_update_ec_ref_status(bool on);
#endif

#ifndef AUXPCM_BT_ENABLED

#define HW_INFO_ARRAY_MAX_SIZE 32

void audio_extn_set_snd_card_split(const char* in_snd_card_name);
void *audio_extn_extspk_init(struct audio_device *adev);
void audio_extn_extspk_deinit(void *extn);
void audio_extn_extspk_update(void* extn);
void audio_extn_extspk_set_mode(void* extn, audio_mode_t mode);
void audio_extn_extspk_set_voice_vol(void* extn, float vol);

#define audio_extn_read_xml(adev, mixer_card, MIXER_XML_PATH, \
                            MIXER_XML_PATH_AUXPCM)               (-ENOSYS)
#else
int32_t audio_extn_read_xml(struct audio_device *adev, uint32_t mixer_card,
                            const char* mixer_xml_path,
                            const char* mixer_xml_path_auxpcm);
#endif /* AUXPCM_BT_ENABLED */

void audio_extn_spkr_prot_init(void *adev);
int audio_extn_spkr_prot_deinit();
int audio_extn_spkr_prot_start_processing(snd_device_t snd_device);
void audio_extn_spkr_prot_stop_processing(snd_device_t snd_device);
bool audio_extn_spkr_prot_is_enabled();
void audio_extn_spkr_prot_calib_cancel(void *adev);
void audio_extn_spkr_prot_set_parameters(struct str_parms *parms,
                                         char *value, int len);
int audio_extn_fbsp_set_parameters(struct str_parms *parms);
int audio_extn_fbsp_get_parameters(struct str_parms *query,
                                   struct str_parms *reply);
int audio_extn_get_spkr_prot_snd_device(snd_device_t snd_device);


// START: COMPRESS_CAPTURE FEATURE =========================
void compr_cap_feature_init(bool is_feature_enabled);
void audio_extn_compr_cap_init(struct stream_in *in);
bool audio_extn_compr_cap_enabled();
bool audio_extn_compr_cap_format_supported(audio_format_t format);
bool audio_extn_compr_cap_usecase_supported(audio_usecase_t usecase);
size_t audio_extn_compr_cap_get_buffer_size(audio_format_t format);
size_t audio_extn_compr_cap_read(struct stream_in *in,
                                        void *buffer, size_t bytes);
void audio_extn_compr_cap_deinit();
// END: COMPRESS_CAPTURE FEATURE =========================

#ifndef DTS_EAGLE
#define audio_extn_dts_eagle_set_parameters(adev, parms)     (0)
#define audio_extn_dts_eagle_get_parameters(adev, query, reply) (0)
#define audio_extn_dts_eagle_fade(adev, fade_in, out) (0)
#define audio_extn_dts_eagle_send_lic()               (0)
#define audio_extn_dts_create_state_notifier_node(stream_out) (0)
#define audio_extn_dts_notify_playback_state(stream_out, has_video, sample_rate, \
                                    channels, is_playing) (0)
#define audio_extn_dts_remove_state_notifier_node(stream_out) (0)
#define audio_extn_check_and_set_dts_hpx_state(adev)       (0)
#else
void audio_extn_dts_eagle_set_parameters(struct audio_device *adev,
                                         struct str_parms *parms);
int audio_extn_dts_eagle_get_parameters(const struct audio_device *adev,
                  struct str_parms *query, struct str_parms *reply);
int audio_extn_dts_eagle_fade(const struct audio_device *adev, bool fade_in, const struct stream_out *out);
void audio_extn_dts_eagle_send_lic();
void audio_extn_dts_create_state_notifier_node(int stream_out);
void audio_extn_dts_notify_playback_state(int stream_out, int has_video, int sample_rate,
                                  int channels, int is_playing);
void audio_extn_dts_remove_state_notifier_node(int stream_out);
void audio_extn_check_and_set_dts_hpx_state(const struct audio_device *adev);
#endif

#if defined(DS1_DOLBY_DDP_ENABLED) || defined(DS1_DOLBY_DAP_ENABLED)
void audio_extn_dolby_set_dmid(struct audio_device *adev);
#else
#define audio_extn_dolby_set_dmid(adev)                 (0)
#define AUDIO_CHANNEL_OUT_PENTA (AUDIO_CHANNEL_OUT_QUAD | AUDIO_CHANNEL_OUT_FRONT_CENTER)
#define AUDIO_CHANNEL_OUT_SURROUND (AUDIO_CHANNEL_OUT_FRONT_LEFT | AUDIO_CHANNEL_OUT_FRONT_RIGHT | \
                                    AUDIO_CHANNEL_OUT_FRONT_CENTER | AUDIO_CHANNEL_OUT_BACK_CENTER)
#endif

#if defined(DS1_DOLBY_DDP_ENABLED) || defined(DS1_DOLBY_DAP_ENABLED) || defined(DS2_DOLBY_DAP_ENABLED)
void audio_extn_dolby_set_license(struct audio_device *adev);
#else
static void __unused audio_extn_dolby_set_license(struct audio_device *adev __unused) {};
#endif

#ifndef DS1_DOLBY_DAP_ENABLED
#define audio_extn_dolby_set_endpoint(adev)                 (0)
#else
void audio_extn_dolby_set_endpoint(struct audio_device *adev);
#endif


#if defined(DS1_DOLBY_DDP_ENABLED) || defined(DS2_DOLBY_DAP_ENABLED)
int audio_extn_dolby_get_snd_codec_id(struct audio_device *adev,
                                      struct stream_out *out,
                                      audio_format_t format);
#else
#define audio_extn_dolby_get_snd_codec_id(adev, out, format)       (0)
#endif

#ifndef DS1_DOLBY_DDP_ENABLED
#define audio_extn_ddp_set_parameters(adev, parms)      (0)
#define audio_extn_dolby_send_ddp_endp_params(adev)     (0)
#else
void audio_extn_ddp_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms);
void audio_extn_dolby_send_ddp_endp_params(struct audio_device *adev);

#endif

#ifndef AUDIO_OUTPUT_FLAG_COMPRESS_PASSTHROUGH
#define AUDIO_OUTPUT_FLAG_COMPRESS_PASSTHROUGH  0x1000
#endif

enum {
    EXT_DISPLAY_TYPE_NONE,
    EXT_DISPLAY_TYPE_HDMI,
    EXT_DISPLAY_TYPE_DP
};

// START: MST ==================================================
#define MAX_CONTROLLERS 1
#define MAX_STREAMS_PER_CONTROLLER 2
// END: MST ==================================================

// START: HDMI_PASSTHROUGH ==================================================
/* Used to limit sample rate for TrueHD & EC3 */
#define HDMI_PASSTHROUGH_MAX_SAMPLE_RATE 192000

typedef bool (*fp_platform_is_edid_supported_format_t)(void*, int);
typedef int (*fp_platform_set_device_params_t)(struct stream_out*, int, int);
typedef int (*fp_platform_edid_get_max_channels_t)(void*);
typedef snd_device_t (*fp_platform_get_output_snd_device_t)(void*, struct stream_out*,
                                                            usecase_type_t);
typedef int (*fp_platform_get_codec_backend_cfg_t)(struct audio_device*,
                                                snd_device_t, struct audio_backend_cfg*);
typedef bool (*fp_platform_is_edid_supported_sample_rate_t)(void*, int);

typedef void (*fp_audio_extn_keep_alive_start_t)(ka_mode_t);
typedef void (*fp_audio_extn_keep_alive_stop_t)(ka_mode_t);
typedef bool (*fp_audio_extn_utils_is_dolby_format_t)(audio_format_t);


typedef struct passthru_init_config {
  fp_platform_is_edid_supported_format_t fp_platform_is_edid_supported_format;
  fp_platform_set_device_params_t fp_platform_set_device_params;
  fp_platform_edid_get_max_channels_t fp_platform_edid_get_max_channels;
  fp_platform_get_output_snd_device_t fp_platform_get_output_snd_device;
  fp_platform_get_codec_backend_cfg_t fp_platform_get_codec_backend_cfg;
  fp_platform_get_snd_device_name_t fp_platform_get_snd_device_name;
  fp_platform_is_edid_supported_sample_rate_t fp_platform_is_edid_supported_sample_rate;
  fp_audio_extn_keep_alive_start_t fp_audio_extn_keep_alive_start;
  fp_audio_extn_keep_alive_stop_t fp_audio_extn_keep_alive_stop;
  fp_audio_extn_utils_is_dolby_format_t fp_audio_extn_utils_is_dolby_format;
} passthru_init_config_t;

bool audio_extn_passthru_is_convert_supported(struct audio_device *adev,
                                                 struct stream_out *out);
bool audio_extn_passthru_is_passt_supported(struct stream_out *out);
void audio_extn_passthru_update_stream_configuration(
        struct audio_device *adev, struct stream_out *out,
        const void *buffer, size_t bytes);
bool audio_extn_passthru_is_passthrough_stream(struct stream_out *out);
int audio_extn_passthru_get_buffer_size(audio_offload_info_t* info);
int audio_extn_passthru_set_volume(struct stream_out *out, int mute);
int audio_extn_passthru_set_latency(struct stream_out *out, int latency);
bool audio_extn_passthru_is_supported_format(audio_format_t format);
bool audio_extn_passthru_should_drop_data(struct stream_out * out);
void audio_extn_passthru_on_start(struct stream_out *out);
void audio_extn_passthru_on_stop(struct stream_out *out);
void audio_extn_passthru_on_pause(struct stream_out *out);
int audio_extn_passthru_set_parameters(struct audio_device *adev,
                                       struct str_parms *parms);
bool audio_extn_passthru_is_enabled();
bool audio_extn_passthru_is_active();
bool audio_extn_passthru_should_standby(struct stream_out *out);
int audio_extn_passthru_get_channel_count(struct stream_out *out);
int audio_extn_passthru_update_dts_stream_configuration(struct stream_out *out,
        const void *buffer, size_t bytes);
bool audio_extn_passthru_is_direct_passthrough(struct stream_out *out);
bool audio_extn_passthru_is_supported_backend_edid_cfg(struct audio_device *adev,
                                                   struct stream_out *out);
bool audio_extn_is_hdmi_passthru_enabled();

// END: HDMI_PASSTHROUGH ==================================================
// START: HFP FEATURE ==================================================
bool audio_extn_hfp_is_active(struct audio_device *adev);
audio_usecase_t audio_extn_hfp_get_usecase();
int audio_extn_hfp_set_mic_mute(struct audio_device *adev, bool state);
void audio_extn_hfp_set_parameters(struct audio_device *adev,
                                           struct str_parms *parms);
int audio_extn_hfp_set_mic_mute2(struct audio_device *adev, bool state);

typedef int (*fp_platform_set_mic_mute_t)(void *, bool);
//typedef int (*fp_platform_get_pcm_device_id_t)(audio_usecase_t, int);
typedef void (*fp_platform_set_echo_reference_t)(struct audio_device *, bool,
                                                     struct listnode *);
typedef int (*fp_select_devices_t)(struct audio_device *, audio_usecase_t);
typedef int (*fp_audio_extn_ext_hw_plugin_usecase_start_t)(void *,
                                                      struct audio_usecase *);
typedef int (*fp_audio_extn_ext_hw_plugin_usecase_stop_t)(void *,
                                                      struct audio_usecase *);
//typedef struct audio_usecase (*fp_get_usecase_from_list_t)(const struct audio_device *,
//                                                                  audio_usecase_t);
typedef int (*fp_disable_audio_route_t)(struct audio_device *,
                                                struct audio_usecase *);
typedef int (*fp_disable_snd_device_t)(struct audio_device *, snd_device_t);
typedef bool (*fp_voice_get_mic_mute_t)(struct audio_device *);
typedef int (*fp_audio_extn_auto_hal_start_hfp_downlink_t)(struct audio_device *,
                                                        struct audio_usecase *);
typedef int (*fp_audio_extn_auto_hal_stop_hfp_downlink_t)(struct audio_device *,
                                                        struct audio_usecase *);
typedef bool (*fp_platform_get_eccarstate_t)(void *);

typedef struct hfp_init_config {
    fp_platform_set_mic_mute_t                   fp_platform_set_mic_mute;
    fp_platform_get_pcm_device_id_t              fp_platform_get_pcm_device_id;
    fp_platform_set_echo_reference_t             fp_platform_set_echo_reference;
    fp_select_devices_t                          fp_select_devices;
    fp_audio_extn_ext_hw_plugin_usecase_start_t  fp_audio_extn_ext_hw_plugin_usecase_start;
    fp_audio_extn_ext_hw_plugin_usecase_stop_t   fp_audio_extn_ext_hw_plugin_usecase_stop;
    fp_get_usecase_from_list_t                   fp_get_usecase_from_list;
    fp_disable_audio_route_t                     fp_disable_audio_route;
    fp_disable_snd_device_t                      fp_disable_snd_device;
    fp_voice_get_mic_mute_t                      fp_voice_get_mic_mute;
    fp_audio_extn_auto_hal_start_hfp_downlink_t  fp_audio_extn_auto_hal_start_hfp_downlink;
    fp_audio_extn_auto_hal_stop_hfp_downlink_t   fp_audio_extn_auto_hal_stop_hfp_downlink;
} hfp_init_config_t;


// END: HFP FEATURE ==================================================

// START: EXT_HW_PLUGIN FEATURE ==================================================
void* audio_extn_ext_hw_plugin_init(struct audio_device *adev);
int audio_extn_ext_hw_plugin_deinit(void *plugin);
int audio_extn_ext_hw_plugin_usecase_start(void *plugin, struct audio_usecase *usecase);
int audio_extn_ext_hw_plugin_usecase_stop(void *plugin, struct audio_usecase *usecase);
int audio_extn_ext_hw_plugin_set_parameters(void *plugin,
                                           struct str_parms *parms);
int audio_extn_ext_hw_plugin_get_parameters(void *plugin,
                  struct str_parms *query, struct str_parms *reply);
int audio_extn_ext_hw_plugin_set_mic_mute(void *plugin, bool mute);
int audio_extn_ext_hw_plugin_get_mic_mute(void *plugin, bool *mute);
int audio_extn_ext_hw_plugin_set_audio_gain(void *plugin,
            struct audio_usecase *usecase, uint32_t gain);

typedef int (*fp_b64decode_t)(char *inp, int ilen, uint8_t* outp);
typedef int (*fp_b64encode_t)(uint8_t *inp, int ilen, char* outp);

typedef struct ext_hw_plugin_init_config {
    fp_b64decode_t fp_b64decode;
    fp_b64encode_t fp_b64encode;
} ext_hw_plugin_init_config_t;
// END: EXT_HW_PLUGIN FEATURE ==================================================

// START: BATTERY_LISTENER FEATURE ==================================================
void audio_extn_battery_properties_listener_init(battery_status_change_fn_t fn);
void audio_extn_battery_properties_listener_deinit();
bool audio_extn_battery_properties_is_charging();
// END: BATTERY_LISTENER FEATURE ==================================================

int audio_extn_utils_send_app_type_gain(struct audio_device *adev,
                                        int app_type,
                                        int *gain);

void audio_extn_dsm_feedback_enable(struct audio_device *adev,
                         snd_device_t snd_device,
                         bool benable);
void dsm_feedback_feature_init (bool is_feature_enabled);

int audio_extn_utils_send_app_type_gain(struct audio_device *adev,
                                        int app_type,
                                        int *gain);

void audio_extn_hwdep_cal_send(int snd_card, void *acdb_handle);

#ifndef DEV_ARBI_ENABLED
#define audio_extn_dev_arbi_init()                  (0)
#define audio_extn_dev_arbi_deinit()                (0)
#define audio_extn_dev_arbi_acquire(snd_device)     (0)
#define audio_extn_dev_arbi_release(snd_device)     (0)
#else
int audio_extn_dev_arbi_init();
int audio_extn_dev_arbi_deinit();
int audio_extn_dev_arbi_acquire(snd_device_t snd_device);
int audio_extn_dev_arbi_release(snd_device_t snd_device);
#endif

#ifndef PM_SUPPORT_ENABLED
#define audio_extn_pm_set_parameters(params) (0)
#define audio_extn_pm_vote(void) (0)
#define audio_extn_pm_unvote(void) (0)
#else
void audio_extn_pm_set_parameters(struct str_parms *parms);
int audio_extn_pm_vote (void);
void audio_extn_pm_unvote(void);
#endif

void audio_extn_init(struct audio_device *adev);
void audio_extn_utils_update_streams_cfg_lists(void *platform,
                                  struct mixer *mixer,
                                  struct listnode *streams_output_cfg_list,
                                  struct listnode *streams_input_cfg_list);
void audio_extn_utils_dump_streams_cfg_lists(
                                  struct listnode *streams_output_cfg_list,
                                  struct listnode *streams_input_cfg_list);
void audio_extn_utils_release_streams_cfg_lists(
                                  struct listnode *streams_output_cfg_list,
                                  struct listnode *streams_input_cfg_list);
void audio_extn_utils_update_stream_output_app_type_cfg(void *platform,
                                  struct listnode *streams_output_cfg_list,
                                  struct listnode *devices,
                                  audio_output_flags_t flags,
                                  audio_format_t format,
                                  uint32_t sample_rate,
                                  uint32_t bit_width,
                                  audio_channel_mask_t channel_mask,
                                  char *profile,
                                  struct stream_app_type_cfg *app_type_cfg);
void audio_extn_utils_update_stream_input_app_type_cfg(void *platform,
                                  struct listnode *streams_input_cfg_list,
                                  struct listnode *devices,
                                  audio_input_flags_t flags,
                                  audio_format_t format,
                                  uint32_t sample_rate,
                                  uint32_t bit_width,
                                  char *profile,
                                  struct stream_app_type_cfg *app_type_cfg);
int audio_extn_utils_send_app_type_cfg(struct audio_device *adev,
                                       struct audio_usecase *usecase);
void audio_extn_utils_send_audio_calibration(struct audio_device *adev,
                                             struct audio_usecase *usecase);
void audio_extn_utils_update_stream_app_type_cfg_for_usecase(
                                  struct audio_device *adev,
                                  struct audio_usecase *usecase);
bool audio_extn_utils_resolve_config_file(char[]);
int audio_extn_utils_get_platform_info(const char* snd_card_name,
                                       char* platform_info_file);
int audio_extn_utils_get_snd_card_num();
int audio_extn_utils_open_snd_mixer(struct mixer **mixer_handle);
void audio_extn_utils_close_snd_mixer(struct mixer *mixer);
bool audio_extn_is_dsp_bit_width_enforce_mode_supported(audio_output_flags_t flags);
bool audio_extn_utils_is_dolby_format(audio_format_t format);
int audio_extn_utils_get_bit_width_from_string(const char *);
int audio_extn_utils_get_sample_rate_from_string(const char *);
int audio_extn_utils_get_channels_from_string(const char *);
void audio_extn_utils_release_snd_device(snd_device_t snd_device);
bool audio_extn_utils_is_vendor_enhanced_fwk();
int audio_extn_utils_get_vendor_enhanced_info();
int audio_extn_utils_get_app_sample_rate_for_device(struct audio_device *adev,
                                    struct audio_usecase *usecase, int snd_device);
int audio_extn_utils_hash_fn(void *key);
bool audio_extn_utils_hash_eq(void *key1, void *key2);

#ifdef DS2_DOLBY_DAP_ENABLED
#define LIB_DS2_DAP_HAL "vendor/lib/libhwdaphal.so"
#define SET_HW_INFO_FUNC "dap_hal_set_hw_info"
typedef enum {
    SND_CARD            = 0,
    HW_ENDPOINT         = 1,
    DMID                = 2,
    DEVICE_BE_ID_MAP    = 3,
    DAP_BYPASS          = 4,
} dap_hal_hw_info_t;
typedef int (*dap_hal_set_hw_info_t)(int32_t hw_info, void* data);
typedef struct {
     int (*device_id_to_be_id)[2];
     int len;
} dap_hal_device_be_id_map_t;

int audio_extn_dap_hal_init(int snd_card);
int audio_extn_dap_hal_deinit();
void audio_extn_dolby_ds2_set_endpoint(struct audio_device *adev);
int audio_extn_ds2_enable(struct audio_device *adev);
int audio_extn_dolby_set_dap_bypass(struct audio_device *adev, int state);
void audio_extn_ds2_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms);

#else
#define audio_extn_dap_hal_init(snd_card)                             (0)
#define audio_extn_dap_hal_deinit()                                   (0)
#define audio_extn_dolby_ds2_set_endpoint(adev)                       (0)
#define audio_extn_ds2_enable(adev)                                   (0)
#define audio_extn_dolby_set_dap_bypass(adev, state)                  (0)
#define audio_extn_ds2_set_parameters(adev, parms);                   (0)
#endif
typedef enum {
    DAP_STATE_ON = 0,
    DAP_STATE_BYPASS,
} dap_state;
#ifndef AUDIO_FORMAT_E_AC3_JOC
#define AUDIO_FORMAT_E_AC3_JOC  0x19000000UL
#endif
#ifndef AUDIO_FORMAT_DTS_LBR
#define AUDIO_FORMAT_DTS_LBR 0x1E000000UL
#endif

#define DIV_ROUND_UP(x, y) (((x) + (y) - 1)/(y))
#define ALIGN(x, y) ((y) * DIV_ROUND_UP((x), (y)))

int b64decode(char *inp, int ilen, uint8_t* outp);
int b64encode(uint8_t *inp, int ilen, char* outp);
int read_line_from_file(const char *path, char *buf, size_t count);
int audio_extn_utils_get_codec_version(const char *snd_card_name, int card_num, char *codec_version);
int audio_extn_utils_get_codec_variant(int card_num, char *codec_variant);
audio_format_t alsa_format_to_hal(uint32_t alsa_format);
uint32_t hal_format_to_alsa(audio_format_t hal_format);
audio_format_t pcm_format_to_hal(uint32_t pcm_format);
uint32_t hal_format_to_pcm(audio_format_t hal_format);

void audio_extn_utils_update_direct_pcm_fragment_size(struct stream_out *out);
size_t audio_extn_utils_convert_format_24_8_to_8_24(void *buf, size_t bytes);
int get_snd_codec_id(audio_format_t format);

void kpi_optimize_feature_init(bool is_feature_enabled);
int audio_extn_perf_lock_init(void);
void audio_extn_perf_lock_acquire(int *handle, int duration,
                                 int *opts, int size);
void audio_extn_perf_lock_release(int *handle);



#ifndef AUDIO_EXTERNAL_HDMI_ENABLED
#define audio_utils_set_hdmi_channel_status(out, buffer, bytes) (0)
#else
void audio_utils_set_hdmi_channel_status(struct stream_out *out, char * buffer, size_t bytes);
#endif

#ifdef QAF_EXTN_ENABLED
bool audio_extn_qaf_is_enabled();
void audio_extn_qaf_deinit();
void audio_extn_qaf_close_output_stream(struct audio_hw_device *dev __unused,
                                        struct audio_stream_out *stream);
int audio_extn_qaf_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address __unused);
int audio_extn_qaf_init(struct audio_device *adev);
int audio_extn_qaf_set_parameters(struct audio_device *adev, struct str_parms *parms);
int audio_extn_qaf_out_set_param_data(struct stream_out *out,
                           audio_extn_param_id param_id,
                           audio_extn_param_payload *payload);
int audio_extn_qaf_out_get_param_data(struct stream_out *out,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload);
bool audio_extn_is_qaf_stream(struct stream_out *out);
#else
#define audio_extn_qaf_is_enabled()                                     (0)
#define audio_extn_qaf_deinit()                                         (0)
#define audio_extn_qaf_close_output_stream         adev_close_output_stream
#define audio_extn_qaf_open_output_stream           adev_open_output_stream
#define audio_extn_qaf_init(adev)                                       (0)
#define audio_extn_qaf_set_parameters(adev, parms)                      (0)
#define audio_extn_qaf_out_set_param_data(out, param_id, payload)       (0)
#define audio_extn_qaf_out_get_param_data(out, param_id, payload)       (0)
#define audio_extn_is_qaf_stream(out)                                   (0)
#endif


#ifdef QAP_EXTN_ENABLED
/*
 * Helper funtion to know if HAL QAP extention is enabled or not.
 */
bool audio_extn_qap_is_enabled();
/*
 * QAP HAL extention init, called during bootup/HAL device open.
 * QAP library will be loaded in this funtion.
 */
int audio_extn_qap_init(struct audio_device *adev);
void audio_extn_qap_deinit();
/*
 * if HAL QAP is enabled and inited succesfully then all then this funtion
 * gets called for all the open_output_stream requests, in other words
 * the core audio_hw->open_output_stream is overridden by this funtion
 */
int audio_extn_qap_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char *address __unused);
void audio_extn_qap_close_output_stream(struct audio_hw_device *dev __unused,
                                        struct audio_stream_out *stream);
/*
 * this funtion is how HAL QAP extention gets to know the device connection/disconnection
 */
int audio_extn_qap_set_parameters(struct audio_device *adev, struct str_parms *parms);
int audio_extn_qap_out_set_param_data(struct stream_out *out,
                           audio_extn_param_id param_id,
                           audio_extn_param_payload *payload);
int audio_extn_qap_out_get_param_data(struct stream_out *out,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload);
/*
 * helper funtion.
 */
bool audio_extn_is_qap_stream(struct stream_out *out);
#else
#define audio_extn_qap_is_enabled()                                     (0)
#define audio_extn_qap_deinit()                                         (0)
#define audio_extn_qap_close_output_stream         adev_close_output_stream
#define audio_extn_qap_open_output_stream           adev_open_output_stream
#define audio_extn_qap_init(adev)                                       (0)
#define audio_extn_qap_set_parameters(adev, parms)                      (0)
#define audio_extn_qap_out_set_param_data(out, param_id, payload)       (0)
#define audio_extn_qap_out_get_param_data(out, param_id, payload)       (0)
#define audio_extn_is_qap_stream(out)                                   (0)
#endif


#ifdef AUDIO_EXTN_BT_HAL_ENABLED
int audio_extn_bt_hal_load(void **handle);
int audio_extn_bt_hal_open_output_stream(void *handle, int in_rate, audio_channel_mask_t channel_mask, int bit_width);
int audio_extn_bt_hal_unload(void *handle);
int audio_extn_bt_hal_close_output_stream(void *handle);
int audio_extn_bt_hal_out_write(void *handle, void *buf, int size);
struct audio_stream_out *audio_extn_bt_hal_get_output_stream(void *handle);
void *audio_extn_bt_hal_get_device(void *handle);
int audio_extn_bt_hal_get_latency(void *handle);
#else
#define audio_extn_bt_hal_load(...)                   (-EINVAL)
#define audio_extn_bt_hal_unload(...)                 (-EINVAL)
#define audio_extn_bt_hal_open_output_stream(...)     (-EINVAL)
#define audio_extn_bt_hal_close_output_stream(...)    (-EINVAL)
#define audio_extn_bt_hal_out_write(...)              (-EINVAL)
#define audio_extn_bt_hal_get_latency(...)            (-EINVAL)
#define audio_extn_bt_hal_get_output_stream(...)      NULL
#define audio_extn_bt_hal_get_device(...)             NULL
#endif

void audio_extn_keep_alive_init(struct audio_device *adev);
void audio_extn_keep_alive_deinit();
void audio_extn_keep_alive_start(ka_mode_t ka_mode);
void audio_extn_keep_alive_stop(ka_mode_t ka_mode);
bool audio_extn_keep_alive_is_active();
int audio_extn_keep_alive_set_parameters(struct audio_device *adev,
                                         struct str_parms *parms);


#ifndef AUDIO_GENERIC_EFFECT_FRAMEWORK_ENABLED

#define audio_extn_gef_init(adev) (0)
#define audio_extn_gef_deinit(adev) (0)
#define audio_extn_gef_notify_device_config(devices, cmask, sample_rate, \
        acdb_id, app_type) (0)

#ifndef INSTANCE_ID_ENABLED
#define audio_extn_gef_send_audio_cal(dev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, param_id, data, length, persist) (0)
#define audio_extn_gef_get_audio_cal(adev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, param_id, data, length, persist) (0)
#define audio_extn_gef_store_audio_cal(adev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, param_id, data, length) (0)
#define audio_extn_gef_retrieve_audio_cal(adev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, param_id, data, length) (0)
#else
#define audio_extn_gef_send_audio_cal(dev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, instance_id, param_id, data,\
    length, persist) (0)
#define audio_extn_gef_get_audio_cal(adev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, instance_id, param_id, data,\
    length, persist) (0)
#define audio_extn_gef_store_audio_cal(adev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, instance_id, param_id, data,\
    length) (0)
#define audio_extn_gef_retrieve_audio_cal(adev, acdb_dev_id, acdb_device_type,\
    app_type, topology_id, sample_rate, module_id, instance_id, param_id, data,\
    length) (0)
#endif

#else

void audio_extn_gef_init(struct audio_device *adev);
void audio_extn_gef_deinit(struct audio_device *adev);

void audio_extn_gef_notify_device_config(struct listnode *audio_devices,
    audio_channel_mask_t channel_mask, int sample_rate, int acdb_id, int app_type);
#ifndef INSTANCE_ID_ENABLED
int audio_extn_gef_send_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint32_t param_id, void* data, int length, bool persist);
int audio_extn_gef_get_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint32_t param_id, void* data, int* length, bool persist);
int audio_extn_gef_store_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint32_t param_id, void* data, int length);
int audio_extn_gef_retrieve_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint32_t param_id, void* data, int* length);
#else
int audio_extn_gef_send_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint16_t instance_id, uint32_t param_id, void* data, int length, bool persist);
int audio_extn_gef_get_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint16_t instance_id, uint32_t param_id, void* data, int* length, bool persist);
int audio_extn_gef_store_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint16_t instance_id, uint32_t param_id, void* data, int length);
int audio_extn_gef_retrieve_audio_cal(void* adev, int acdb_dev_id, int acdb_device_type,
    int app_type, int topology_id, int sample_rate, uint32_t module_id,
    uint16_t instance_id, uint32_t param_id, void* data, int* length);
#endif

#endif /* AUDIO_GENERIC_EFFECT_FRAMEWORK_ENABLED */

// START: COMPRESS_INPUT_ENABLED ===============================
bool audio_extn_cin_applicable_stream(struct stream_in *in);
bool audio_extn_cin_attached_usecase(struct stream_in *in);
bool audio_extn_cin_format_supported(audio_format_t format);
int audio_extn_cin_acquire_usecase(struct stream_in *in);
size_t audio_extn_cin_get_buffer_size(struct stream_in *in);
int audio_extn_cin_open_input_stream(struct stream_in *in);
void audio_extn_cin_stop_input_stream(struct stream_in *in);
void audio_extn_cin_close_input_stream(struct stream_in *in);
void audio_extn_cin_free_input_stream_resources(struct stream_in *in);
int audio_extn_cin_read(struct stream_in *in, void *buffer,
                        size_t bytes, size_t *bytes_read);
int audio_extn_cin_configure_input_stream(struct stream_in *in, struct audio_config *in_config);
// END: COMPRESS_INPUT_ENABLED ===============================

//START: SOURCE_TRACKING_FEATURE ==============================================
int audio_extn_get_soundfocus_data(const struct audio_device *adev,
                                   struct sound_focus_param *payload);
int audio_extn_get_sourcetrack_data(const struct audio_device *adev,
                                    struct source_tracking_param *payload);
int audio_extn_set_soundfocus_data(struct audio_device *adev,
                                   struct sound_focus_param *payload);
void audio_extn_source_track_set_parameters(struct audio_device *adev,
                                            struct str_parms *parms);
void audio_extn_source_track_get_parameters(const struct audio_device *adev,
                                            struct str_parms *query,
                                            struct str_parms *reply);
//END: SOURCE_TRACKING_FEATURE ================================================

void audio_extn_fm_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms);
void audio_extn_fm_get_parameters(struct str_parms *query, struct str_parms *reply);
void audio_extn_fm_route_on_selected_device(struct audio_device *adev,
                                            struct listnode *devices);

#ifndef APTX_DECODER_ENABLED
#define audio_extn_aptx_dec_set_license(adev); (0)
#define audio_extn_set_aptx_dec_bt_addr(adev, parms); (0)
#define audio_extn_send_aptx_dec_bt_addr_to_dsp(out); (0)
#define audio_extn_parse_aptx_dec_bt_addr(value); (0)
#define audio_extn_set_aptx_dec_params(payload); (0)
#else
static void audio_extn_aptx_dec_set_license(struct audio_device *adev);
static void audio_extn_set_aptx_dec_bt_addr(struct audio_device *adev, struct str_parms *parms);
void audio_extn_send_aptx_dec_bt_addr_to_dsp(struct stream_out *out);
static void audio_extn_parse_aptx_dec_bt_addr(char *value);
int audio_extn_set_aptx_dec_params(struct aptx_dec_param *payload);
#endif
int audio_extn_out_set_param_data(struct stream_out *out,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload);
int audio_extn_out_get_param_data(struct stream_out *out,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload);
int audio_extn_set_device_cfg_params(struct audio_device *adev,
                                     struct audio_device_cfg_param *payload);
int audio_extn_utils_get_avt_device_drift(
                struct audio_usecase *usecase,
                struct audio_avt_device_drift_param *drift_param);
int audio_extn_utils_compress_get_dsp_latency(struct stream_out *out);
int audio_extn_utils_compress_set_render_mode(struct stream_out *out);
int audio_extn_utils_compress_set_clk_rec_mode(struct audio_usecase *usecase);
int audio_extn_utils_compress_set_render_window(
            struct stream_out *out,
            struct audio_out_render_window_param *render_window);
int audio_extn_utils_compress_set_start_delay(
            struct stream_out *out,
            struct audio_out_start_delay_param *start_delay_param);
int audio_extn_utils_compress_enable_drift_correction(
            struct stream_out *out,
            struct audio_out_enable_drift_correction *drift_enable);
int audio_extn_utils_compress_correct_drift(
            struct stream_out *out,
            struct audio_out_correct_drift *drift_correction_param);
int audio_extn_utils_set_channel_map(
            struct stream_out *out,
            struct audio_out_channel_map_param *channel_map_param);
int audio_extn_utils_set_pan_scale_params(
            struct stream_out *out,
            struct mix_matrix_params *mm_params);
int audio_extn_utils_set_downmix_params(
            struct stream_out *out,
            struct mix_matrix_params *mm_params);
int audio_ext_get_presentation_position(struct stream_out *out,
            struct audio_out_presentation_position_param *pos_param);
int audio_extn_utils_compress_get_dsp_presentation_pos(struct stream_out *out,
            uint64_t *frames, struct timespec *timestamp, int32_t clock_id);
int audio_extn_utils_pcm_get_dsp_presentation_pos(struct stream_out *out,
            uint64_t *frames, struct timespec *timestamp, int32_t clock_id);
size_t audio_extn_utils_get_input_buffer_size(uint32_t, audio_format_t, int, int64_t, bool);
int audio_extn_utils_get_perf_mode_flag(void);
#ifdef AUDIO_HW_LOOPBACK_ENABLED
/* API to create audio patch */
int audio_extn_hw_loopback_create_audio_patch(struct audio_hw_device *dev,
                                     unsigned int num_sources,
                                     const struct audio_port_config *sources,
                                     unsigned int num_sinks,
                                     const struct audio_port_config *sinks,
                                     audio_patch_handle_t *handle);
/* API to release audio patch */
int audio_extn_hw_loopback_release_audio_patch(struct audio_hw_device *dev,
                                             audio_patch_handle_t handle);

int audio_extn_hw_loopback_set_audio_port_config(struct audio_hw_device *dev,
                                    const struct audio_port_config *config);
int audio_extn_hw_loopback_get_audio_port(struct audio_hw_device *dev,
                                    struct audio_port *port_in);

int audio_extn_hw_loopback_set_param_data(audio_patch_handle_t handle,
                                          audio_extn_loopback_param_id param_id,
                                          audio_extn_loopback_param_payload *payload);

int audio_extn_hw_loopback_set_render_window(audio_patch_handle_t handle,
                                             struct audio_out_render_window_param *render_window);

int audio_extn_hw_loopback_init(struct audio_device *adev);
void audio_extn_hw_loopback_deinit(struct audio_device *adev);
#else
static int __unused audio_extn_hw_loopback_create_audio_patch(struct audio_hw_device *dev __unused,
                                     unsigned int num_sources __unused,
                                     const struct audio_port_config *sources __unused,
                                     unsigned int num_sinks __unused,
                                     const struct audio_port_config *sinks __unused,
                                     audio_patch_handle_t *handle __unused)
{
    return 0;
}
static int __unused audio_extn_hw_loopback_release_audio_patch(struct audio_hw_device *dev __unused,
                                             audio_patch_handle_t handle __unused)
{
    return 0;
}
static int __unused audio_extn_hw_loopback_set_audio_port_config(struct audio_hw_device *dev __unused,
                                    const struct audio_port_config *config __unused)
{
    return 0;
}
static int __unused audio_extn_hw_loopback_get_audio_port(struct audio_hw_device *dev __unused,
                                    struct audio_port *port_in __unused)
{
    return 0;
}
static int __unused audio_extn_hw_loopback_set_param_data(audio_patch_handle_t handle __unused,
                                               audio_extn_loopback_param_id param_id __unused,
                                               audio_extn_loopback_param_payload *payload __unused)
{
    return -ENOSYS;
}

static int __unused audio_extn_hw_loopback_set_render_window(audio_patch_handle_t handle __unused,
                                     struct audio_out_render_window_param *render_window __unused)
{
    return -ENOSYS;
}
static int __unused audio_extn_hw_loopback_init(struct audio_device *adev __unused)
{
    return -ENOSYS;
}
static void __unused audio_extn_hw_loopback_deinit(struct audio_device *adev __unused)
{
}
#endif

#ifndef FFV_ENABLED
#define audio_extn_ffv_init(adev) (0)
#define audio_extn_ffv_deinit() (0)
#define audio_extn_ffv_check_usecase(in) (0)
#define audio_extn_ffv_set_usecase(in, key, lic) (0)
#define audio_extn_ffv_stream_init(in, key, lic) (0)
#define audio_extn_ffv_stream_deinit() (0)
#define audio_extn_ffv_update_enabled() (0)
#define audio_extn_ffv_get_enabled() (0)
#define audio_extn_ffv_read(stream, buffer, bytes) (0)
#define audio_extn_ffv_set_parameters(adev, parms) (0)
#define audio_extn_ffv_get_stream() (0)
#define audio_extn_ffv_update_pcm_config(config) (0)
#define audio_extn_ffv_init_ec_ref_loopback(adev, snd_device) (0)
#define audio_extn_ffv_deinit_ec_ref_loopback(adev, snd_device) (0)
#define audio_extn_ffv_check_and_append_ec_ref_dev(device_name) (0)
#define audio_extn_ffv_get_capture_snd_device() (0)
#define audio_extn_ffv_append_ec_ref_dev_name(device_name) (0)
#else
int32_t audio_extn_ffv_init(struct audio_device *adev);
int32_t audio_extn_ffv_deinit();
bool audio_extn_ffv_check_usecase(struct stream_in *in);
int audio_extn_ffv_set_usecase( struct stream_in *in, int key, char* lic);
int32_t audio_extn_ffv_stream_init(struct stream_in *in, int key, char* lic);
int32_t audio_extn_ffv_stream_deinit();
void audio_extn_ffv_update_enabled();
bool audio_extn_ffv_get_enabled();
int32_t audio_extn_ffv_read(struct audio_stream_in *stream,
                       void *buffer, size_t bytes);
void audio_extn_ffv_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms);
struct stream_in *audio_extn_ffv_get_stream();
void audio_extn_ffv_update_pcm_config(struct pcm_config *config);
int audio_extn_ffv_init_ec_ref_loopback(struct audio_device *adev,
                                        snd_device_t snd_device);
int audio_extn_ffv_deinit_ec_ref_loopback(struct audio_device *adev,
                                          snd_device_t snd_device);
void audio_extn_ffv_check_and_append_ec_ref_dev(char *device_name);
snd_device_t audio_extn_ffv_get_capture_snd_device();
void audio_extn_ffv_append_ec_ref_dev_name(char *device_name);
#endif

int audio_extn_utils_get_license_params(const struct audio_device *adev,  struct audio_license_params *lic_params);

// START: AUTO_HAL FEATURE ==================================================
#ifndef AUDIO_OUTPUT_FLAG_MEDIA
#define AUDIO_OUTPUT_FLAG_MEDIA 0x100000
#endif
#ifndef AUDIO_OUTPUT_FLAG_SYS_NOTIFICATION
#define AUDIO_OUTPUT_FLAG_SYS_NOTIFICATION 0x200000
#endif
#ifndef AUDIO_OUTPUT_FLAG_NAV_GUIDANCE
#define AUDIO_OUTPUT_FLAG_NAV_GUIDANCE 0x400000
#endif
#ifndef AUDIO_OUTPUT_FLAG_PHONE
#define AUDIO_OUTPUT_FLAG_PHONE 0x800000
#endif
#ifndef AUDIO_OUTPUT_FLAG_FRONT_PASSENGER
#define AUDIO_OUTPUT_FLAG_FRONT_PASSENGER 0x1000000
#endif
#ifndef AUDIO_OUTPUT_FLAG_REAR_SEAT
#define AUDIO_OUTPUT_FLAG_REAR_SEAT 0x2000000
#endif
int audio_extn_auto_hal_init(struct audio_device *adev);
void audio_extn_auto_hal_deinit(void);
int audio_extn_auto_hal_create_audio_patch(struct audio_hw_device *dev,
                                unsigned int num_sources,
                                const struct audio_port_config *sources,
                                unsigned int num_sinks,
                                const struct audio_port_config *sinks,
                                audio_patch_handle_t *handle);
int audio_extn_auto_hal_release_audio_patch(struct audio_hw_device *dev,
                                audio_patch_handle_t handle);
int audio_extn_auto_hal_get_car_audio_stream_from_address(const char *address);
int audio_extn_auto_hal_open_output_stream(struct stream_out *out);
bool audio_extn_auto_hal_is_bus_device_usecase(audio_usecase_t uc_id);
int audio_extn_auto_hal_get_audio_port(struct audio_hw_device *dev,
                                struct audio_port *config);
int audio_extn_auto_hal_set_audio_port_config(struct audio_hw_device *dev,
                                const struct audio_port_config *config);
void audio_extn_auto_hal_set_parameters(struct audio_device *adev,
                                struct str_parms *parms);
int audio_extn_auto_hal_start_hfp_downlink(struct audio_device *adev,
                                struct audio_usecase *uc_info);
int audio_extn_auto_hal_stop_hfp_downlink(struct audio_device *adev,
                                struct audio_usecase *uc_info);
snd_device_t audio_extn_auto_hal_get_input_snd_device(struct audio_device *adev,
                                audio_usecase_t uc_id);
snd_device_t audio_extn_auto_hal_get_output_snd_device(struct audio_device *adev,
                                audio_usecase_t uc_id);

typedef streams_input_ctxt_t* (*fp_in_get_stream_t)(struct audio_device*, audio_io_handle_t);
typedef streams_output_ctxt_t* (*fp_out_get_stream_t)(struct audio_device*, audio_io_handle_t);
typedef size_t (*fp_get_output_period_size_t)(uint32_t, audio_format_t, int, int);
typedef int (*fp_audio_extn_ext_hw_plugin_set_audio_gain_t)(void*, struct audio_usecase*, uint32_t);
typedef struct stream_in* (*fp_adev_get_active_input_t)(const struct audio_device*);
typedef audio_patch_handle_t (*fp_generate_patch_handle_t)(void);

typedef struct auto_hal_init_config {
    fp_in_get_stream_t                           fp_in_get_stream;
    fp_out_get_stream_t                          fp_out_get_stream;
    fp_audio_extn_ext_hw_plugin_usecase_start_t  fp_audio_extn_ext_hw_plugin_usecase_start;
    fp_audio_extn_ext_hw_plugin_usecase_stop_t   fp_audio_extn_ext_hw_plugin_usecase_stop;
    fp_get_usecase_from_list_t                   fp_get_usecase_from_list;
    fp_get_output_period_size_t                  fp_get_output_period_size;
    fp_audio_extn_ext_hw_plugin_set_audio_gain_t fp_audio_extn_ext_hw_plugin_set_audio_gain;
    fp_select_devices_t                          fp_select_devices;
    fp_disable_audio_route_t                     fp_disable_audio_route;
    fp_disable_snd_device_t                      fp_disable_snd_device;
    fp_adev_get_active_input_t                   fp_adev_get_active_input;
    fp_platform_set_echo_reference_t             fp_platform_set_echo_reference;
    fp_platform_get_eccarstate_t                 fp_platform_get_eccarstate;
    fp_generate_patch_handle_t                   fp_generate_patch_handle;
} auto_hal_init_config_t;
// END: AUTO_HAL FEATURE ==================================================

bool audio_extn_edid_is_supported_sr(edid_audio_info* info, int sr);
bool audio_extn_edid_is_supported_bps(edid_audio_info* info, int bps);
int audio_extn_edid_get_highest_supported_sr(edid_audio_info* info);
bool audio_extn_edid_get_sink_caps(edid_audio_info* info, char *edid_data);

bool audio_extn_is_display_port_enabled();

bool audio_extn_is_fluence_enabled();
void audio_extn_set_fluence_parameters(struct audio_device *adev,
                                           struct str_parms *parms);
int audio_extn_get_fluence_parameters(const struct audio_device *adev,
                  struct str_parms *query, struct str_parms *reply);

bool audio_extn_is_custom_stereo_enabled();
void audio_extn_send_dual_mono_mixing_coefficients(struct stream_out *out);

void audio_extn_set_cpu_affinity();
bool audio_extn_is_record_play_concurrency_enabled();
bool audio_extn_is_concurrent_capture_enabled();
void audio_extn_set_custom_mtmx_params_v2(struct audio_device *adev,
                                        struct audio_usecase *usecase,
                                        bool enable);
void audio_extn_set_custom_mtmx_params_v1(struct audio_device *adev,
                                        struct audio_usecase *usecase,
                                        bool enable);
snd_device_t audio_extn_get_loopback_snd_device(struct audio_device *adev,
                                                struct audio_usecase *usecase,
                                                int channel_count);

void audio_get_vendor_config_path(char* config_file_path, int path_size);
#endif /* AUDIO_EXTN_H */
