/*
 * Copyright (c) 2013-2020, The Linux Foundation. All rights reserved.
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
 */

#ifndef AUDIO_PLATFORM_API_H
#define AUDIO_PLATFORM_API_H
#include <sound/voice_params.h>
#include "audio_hw.h"
#include "voice.h"

#define CODEC_BACKEND_DEFAULT_BIT_WIDTH 16
#define CODEC_BACKEND_DEFAULT_SAMPLE_RATE 48000
#define CODEC_BACKEND_DEFAULT_CHANNELS 2
#define CODEC_BACKEND_DEFAULT_TX_CHANNELS 1
#define SAMPLE_RATE_8000 8000
#define SAMPLE_RATE_11025 11025
#define sample_rate_multiple(sr, base) ((sr % base)== 0?true:false)
#define MAX_VOLUME_CAL_STEPS 15
#define LICENSE_STR_MAX_LEN  (64)
#define PRODUCT_FFV      "ffv"
#define PRODUCT_ALLPLAY  "allplay"
#define MAX_IN_CHANNELS 32
#define CUSTOM_MTRX_PARAMS_MAX_USECASE 8

typedef enum {
    PLATFORM,
    ACDB_EXTN,
} caller_t;

struct audio_backend_cfg {
    unsigned int   sample_rate;
    unsigned int   channels;
    unsigned int   bit_width;
    bool           passthrough_enabled;
    audio_format_t format;
    int controller;
    int stream;
};

struct amp_db_and_gain_table {
    float amp;
    float db;
    uint32_t level;
};

struct mic_info {
    char device_id[AUDIO_MICROPHONE_ID_MAX_LEN];
    size_t channel_count;
    audio_microphone_channel_mapping_t channel_mapping[AUDIO_CHANNEL_COUNT_MAX];
};

enum {
    NATIVE_AUDIO_MODE_SRC = 1,
    NATIVE_AUDIO_MODE_TRUE_44_1,
    NATIVE_AUDIO_MODE_MULTIPLE_MIX_IN_CODEC,
    NATIVE_AUDIO_MODE_MULTIPLE_MIX_IN_DSP,
    NATIVE_AUDIO_MODE_INVALID
};

typedef struct {
    bool platform_na_prop_enabled;
    bool ui_na_prop_enabled;
    int na_mode;
} native_audio_prop;

#define BE_DAI_NAME_MAX_LENGTH 24
struct be_dai_name_struct {
    unsigned int be_id;
    char be_name[BE_DAI_NAME_MAX_LENGTH];
};

typedef struct acdb_audio_cal_cfg {
    uint32_t             persist;
    uint32_t             snd_dev_id;
    audio_devices_t      dev_id;
    int32_t              acdb_dev_id;
    uint32_t             app_type;
    uint32_t             topo_id;
    uint32_t             sampling_rate;
    uint32_t             cal_type;
    uint32_t             module_id;
#ifdef INSTANCE_ID_ENABLED
    uint16_t             instance_id;
    uint16_t             reserved;
#endif
    uint32_t             param_id;
} acdb_audio_cal_cfg_t;


struct audio_custom_mtmx_params_info {
    uint32_t id;
    uint32_t ip_channels;
    uint32_t op_channels;
    uint32_t usecase_id[CUSTOM_MTRX_PARAMS_MAX_USECASE];
    uint32_t snd_device;
    uint32_t fe_id[CUSTOM_MTRX_PARAMS_MAX_USECASE];
};

struct audio_custom_mtmx_params {
    struct listnode list;
    struct audio_custom_mtmx_params_info info;
    uint32_t coeffs[0];
};

struct audio_custom_mtmx_in_params_info {
    uint32_t op_channels;
    uint32_t usecase_id[CUSTOM_MTRX_PARAMS_MAX_USECASE];
};

struct audio_custom_mtmx_params_in_ch_info {
    uint32_t ch_count;
    char device[128];
    char hw_interface[128];
};

struct audio_custom_mtmx_in_params {
    struct listnode list;
    struct audio_custom_mtmx_in_params_info in_info;
    uint32_t ip_channels;
    uint32_t mic_ch;
    uint32_t ec_ref_ch;
    struct audio_custom_mtmx_params_in_ch_info in_ch_info[MAX_IN_CHANNELS];
};

enum card_status_t;

void *platform_init(struct audio_device *adev);
void platform_deinit(void *platform);
const char *platform_get_snd_device_name(snd_device_t snd_device);

/* return true if adding entry success
   return false if adding entry fails */

bool platform_add_gain_level_mapping(struct amp_db_and_gain_table *tbl_entry);

/* return 0 if no custome mapping table found or when error detected
            use default mapping in this case
   return > 0 indicates number of entries in mapping table */

int platform_get_gain_level_mapping(struct amp_db_and_gain_table *mapping_tbl,
                                    int table_size);

int platform_get_snd_device_name_extn(void *platform, snd_device_t snd_device,
                                      char *device_name);
void platform_add_backend_name(char *mixer_path, snd_device_t snd_device,
                               struct audio_usecase *usecase);
bool platform_send_gain_dep_cal(void *platform, int level);
int platform_get_pcm_device_id(audio_usecase_t usecase, int device_type);
int platform_get_snd_device_index(char *snd_device_index_name);
int platform_set_fluence_type(void *platform, char *value);
int platform_get_fluence_type(void *platform, char *value, uint32_t len);
int platform_set_snd_device_acdb_id(snd_device_t snd_device, unsigned int acdb_id);
int platform_get_snd_device_acdb_id(snd_device_t snd_device);
int platform_set_snd_device_bit_width(snd_device_t snd_device, unsigned int bit_width);
int platform_set_effect_config_data(snd_device_t snd_device,
                                      struct audio_effect_config effect_config,
                                      effect_type_t effect_type);
int platform_get_effect_config_data(snd_device_t snd_device,
                                      struct audio_effect_config *effect_config,
                                      effect_type_t effect_type);
int platform_set_fluence_mmsecns_config(struct audio_fluence_mmsecns_config fluence_mmsecns_config);
int platform_get_snd_device_bit_width(snd_device_t snd_device);
int platform_set_acdb_metainfo_key(void *platform, char *name, int key);
void platform_release_acdb_metainfo_key(void *platform);
int platform_get_meta_info_key_from_list(void *platform, char *mod_name);
int platform_set_native_support(int na_mode);
int platform_get_native_support();
int platform_send_audio_calibration(void *platform, struct audio_usecase *usecase,
                                    int app_type);
int platform_get_default_app_type(void *platform);
int platform_get_default_app_type_v2(void *platform, usecase_type_t  type);
int platform_switch_voice_call_device_pre(void *platform);
int platform_switch_voice_call_enable_device_config(void *platform,
                                                    snd_device_t out_snd_device,
                                                    snd_device_t in_snd_device);
int platform_switch_voice_call_device_post(void *platform,
                                           snd_device_t out_snd_device,
                                           snd_device_t in_snd_device);
int platform_switch_voice_call_usecase_route_post(void *platform,
                                                  snd_device_t out_snd_device,
                                                  snd_device_t in_snd_device);
int platform_start_voice_call(void *platform, uint32_t vsid);
int platform_stop_voice_call(void *platform, uint32_t vsid);
int platform_set_mic_break_det(void *platform, bool enable);
int platform_set_voice_volume(void *platform, int volume);
void platform_set_speaker_gain_in_combo(struct audio_device *adev,
                                        snd_device_t snd_device,
                                        bool enable);
int platform_set_mic_mute(void *platform, bool state);
int platform_get_sample_rate(void *platform, uint32_t *rate);
int platform_set_device_mute(void *platform, bool state, char *dir);
snd_device_t platform_get_output_snd_device(void *platform, struct stream_out *out,
                                            usecase_type_t uc_type);
snd_device_t platform_get_input_snd_device(void *platform,
                                           struct stream_in *in,
                                           struct listnode *out_devices,
                                           usecase_type_t uc_type);
int platform_set_hdmi_channels(void *platform, int channel_count);
int platform_edid_get_max_channels(void *platform);
void platform_add_operator_specific_device(snd_device_t snd_device,
                                           const char *operator,
                                           const char *mixer_path,
                                           unsigned int acdb_id);
void platform_add_external_specific_device(snd_device_t snd_device,
                                           const char *name,
                                           unsigned int acdb_id);
void platform_get_parameters(void *platform, struct str_parms *query,
                             struct str_parms *reply);
int platform_set_parameters(void *platform, struct str_parms *parms);
int platform_set_incall_recording_session_id(void *platform, uint32_t session_id,
                                             int rec_mode);
#ifndef INCALL_STEREO_CAPTURE_ENABLED
#define platform_set_incall_recording_session_channels(p, sc)  (0)
#else
int platform_set_incall_recording_session_channels(void *platform,
                                                   uint32_t session_channels);
#endif
int platform_stop_incall_recording_usecase(void *platform);
int platform_start_incall_music_usecase(void *platform);
int platform_stop_incall_music_usecase(void *platform);
int platform_update_lch(void *platform, struct voice_session *session,
                        enum voice_lch_mode lch_mode);
/* returns the latency for a usecase in Us */
int64_t platform_render_latency(struct stream_out *out);
int64_t platform_capture_latency(struct stream_in *in);
int platform_update_usecase_from_source(int source, audio_usecase_t usecase);

bool platform_listen_device_needs_event(snd_device_t snd_device);
bool platform_listen_usecase_needs_event(audio_usecase_t uc_id);

bool platform_sound_trigger_device_needs_event(snd_device_t snd_device);
bool platform_sound_trigger_usecase_needs_event(audio_usecase_t uc_id);

int platform_set_snd_device_backend(snd_device_t snd_device, const char * backend,
                                    const char * hw_interface);
int platform_get_snd_device_backend_index(snd_device_t device);
const char * platform_get_snd_device_backend_interface(snd_device_t device);
void platform_add_app_type(const char *uc_type,
                           const char *mode,
                           int bw, int app_type, int max_sr);
int platform_set_snd_device_name(snd_device_t snd_device, const char * name);

/* From platform_info.c */
int platform_info_init(const char *filename, void *, caller_t);

void platform_snd_card_update(void *platform, card_status_t scard_status);

struct audio_offload_info_t;
uint32_t platform_get_compress_offload_buffer_size(audio_offload_info_t* info);
int platform_get_codec_backend_cfg(struct audio_device* adev,
                                   snd_device_t snd_device,
                                   struct audio_backend_cfg *backend_cfg);

bool platform_check_and_set_codec_backend_cfg(struct audio_device* adev,
                   struct audio_usecase *usecase, snd_device_t snd_device);
bool platform_check_and_set_capture_codec_backend_cfg(struct audio_device* adev,
                   struct audio_usecase *usecase, snd_device_t snd_device);
int platform_get_usecase_index(const char * usecase);
int platform_set_usecase_pcm_id(audio_usecase_t usecase, int32_t type, int32_t pcm_id);
void platform_set_echo_reference(struct audio_device *adev, bool enable,
                                 struct listnode *out_devices);
int platform_check_and_set_swap_lr_channels(struct audio_device *adev, bool swap_channels);
int platform_set_swap_channels(struct audio_device *adev, bool swap_channels);
void platform_get_device_to_be_id_map(int **be_id_map, int *length);

int platform_set_channel_allocation(void *platform, int channel_alloc);
int platform_get_edid_info(void *platform);
int platform_get_supported_copp_sampling_rate(uint32_t stream_sr);
int platform_set_channel_map(void *platform, int ch_count, char *ch_map,
                             int snd_id, int be_idx);
int platform_set_stream_channel_map(void *platform, audio_channel_mask_t channel_mask,
                                   int snd_id, uint8_t *input_channel_map);
int platform_set_stream_pan_scale_params(void *platform,
                                         int snd_id,
                                         struct mix_matrix_params mm_params);
int platform_set_stream_downmix_params(void *platform,
                                       int snd_id,
                                       snd_device_t snd_device,
                                       struct mix_matrix_params mm_params);
int platform_set_edid_channels_configuration(void *platform, int channels,
                                             int backend_idx, snd_device_t snd_device);
bool platform_spkr_use_default_sample_rate(void *platform);
unsigned char platform_map_to_edid_format(int format);
bool platform_is_edid_supported_format(void *platform, int format);
bool platform_is_edid_supported_sample_rate(void *platform, int sample_rate);
void platform_cache_edid(void * platform);
void platform_invalidate_hdmi_config(void * platform);
int platform_set_hdmi_config(void *platform, uint32_t channel_count,
                             uint32_t sample_rate, bool enable_passthrough);
int platform_set_device_params(struct stream_out *out, int param, int value);
int platform_set_audio_device_interface(const char * device_name, const char *intf_name,
                                        const char * codec_type);
void platform_set_gsm_mode(void *platform, bool enable);
bool platform_can_enable_spkr_prot_on_device(snd_device_t snd_device);
int platform_get_spkr_prot_acdb_id(snd_device_t snd_device);
int platform_get_spkr_prot_snd_device(snd_device_t snd_device);
int platform_get_vi_feedback_snd_device(snd_device_t snd_device);
int platform_spkr_prot_is_wsa_analog_mode(void *adev);
int platform_split_snd_device(void *platform,
                              snd_device_t snd_device,
                              int *num_devices,
                              snd_device_t *new_snd_devices);

bool platform_check_all_backends_match(snd_device_t snd_device1, snd_device_t snd_device2);
bool platform_check_backends_match(snd_device_t snd_device1, snd_device_t snd_device2);
int platform_set_sidetone(struct audio_device *adev,
                          snd_device_t out_snd_device,
                          bool enable,
                          char * str);
void platform_update_aanc_path(struct audio_device *adev,
                              snd_device_t out_snd_device,
                              bool enable,
                              char * str);
bool platform_supports_true_32bit();
bool platform_check_if_backend_has_to_be_disabled(snd_device_t new_snd_device, snd_device_t cuurent_snd_device);
bool platform_check_codec_dsd_support(void *platform);
bool platform_check_codec_asrc_support(void *platform);
int platform_get_backend_index(snd_device_t snd_device);
int platform_get_ext_disp_type(void *platform);
void platform_invalidate_hdmi_config(void *platform);
void platform_invalidate_backend_config(void * platform,snd_device_t snd_device);

#ifdef INSTANCE_ID_ENABLED
void platform_make_cal_cfg(acdb_audio_cal_cfg_t* cal, int acdb_dev_id,
        int acdb_device_type, int app_type, int topology_id,
        int sample_rate, uint32_t module_id, uint16_t instance_id,
        uint32_t param_id, bool persist);
#else
void platform_make_cal_cfg(acdb_audio_cal_cfg_t* cal, int acdb_dev_id,
        int acdb_device_type, int app_type, int topology_id,
        int sample_rate, uint32_t module_id, uint32_t param_id, bool persist);
#endif

int platform_send_audio_cal(void* platform, acdb_audio_cal_cfg_t* cal,
    void* data, int length, bool persist);

int platform_get_audio_cal(void* platform, acdb_audio_cal_cfg_t* cal,
    void* data, int* length, bool persist);

int platform_store_audio_cal(void* platform, acdb_audio_cal_cfg_t* cal,
    void* data, int length);

int platform_retrieve_audio_cal(void* platform, acdb_audio_cal_cfg_t* cal,
    void* data, int* length);

unsigned char* platform_get_license(void* platform, int* size);
int platform_get_max_mic_count(void *platform);
void platform_check_and_update_copp_sample_rate(void *platform, snd_device_t snd_device,
     unsigned int stream_sr,int *sample_rate);
int platform_get_max_codec_backend();
int platform_get_mmap_data_fd(void *platform, int dev, int dir,
                               int *fd, uint32_t *size);
int platform_get_ec_ref_loopback_snd_device(int channel_count);
const char * platform_get_snd_card_name_for_acdb_loader(const char *snd_card_name);

bool platform_set_microphone_characteristic(void *platform,
                                            struct audio_microphone_characteristic_t mic);
bool platform_set_microphone_map(void *platform, snd_device_t in_snd_device,
                                 const struct mic_info *info);
int platform_get_microphones(void *platform,
                             struct audio_microphone_characteristic_t *mic_array,
                             size_t *mic_count);
int platform_get_active_microphones(void *platform, unsigned int channels,
                                    audio_usecase_t usecase,
                                    struct audio_microphone_characteristic_t *mic_array,
                                    size_t *mic_count);

int platform_get_license_by_product(void *platform, const char* product_name, int *product_id, char* product_license);
bool platform_get_eccarstate(void *platform);
int platform_set_qtime(void *platform, int audio_pcm_device_id,
                       int haptic_pcm_device_id);
int platform_get_delay(void *platform, int pcm_device_id);
struct audio_custom_mtmx_params *
    platform_get_custom_mtmx_params(void *platform,
                                    struct audio_custom_mtmx_params_info *info,
                                    uint32_t *idx);
int platform_add_custom_mtmx_params(void *platform,
                                    struct audio_custom_mtmx_params_info *info);
/* callback functions from platform to common audio HAL */
struct stream_in *adev_get_active_input(const struct audio_device *adev);

struct audio_custom_mtmx_in_params * platform_get_custom_mtmx_in_params(void *platform,
                                    struct audio_custom_mtmx_in_params_info *info);
int platform_add_custom_mtmx_in_params(void *platform,
                                    struct audio_custom_mtmx_in_params_info *info);

int platform_get_edid_info_v2(void *platform, int controller, int stream);
int platform_edid_get_max_channels_v2(void *platform, int controller, int stream);
bool platform_is_edid_supported_format_v2(void *platform, int format,
                                          int contoller, int stream);
bool platform_is_edid_supported_sample_rate_v2(void *platform, int sample_rate,
                                               int contoller, int stream);
void platform_cache_edid_v2(void * platform, int controller, int stream);
void platform_invalidate_hdmi_config_v2(void * platform, int controller, int stream);
int platform_get_controller_stream_from_params(struct str_parms *parms,
                                               int *controller, int *stream);
int platform_set_ext_display_device_v2(void *platform, int controller, int stream);
int platform_get_ext_disp_type_v2(void *platform, int controller, int stream);
int platform_set_edid_channels_configuration_v2(void *platform, int channels,
                                             int backend_idx, snd_device_t snd_device,
                                             int controller, int stream);
int platform_set_channel_allocation_v2(void *platform, int channel_alloc,
                                             int controller, int stream);
int platform_set_hdmi_channels_v2(void *platform, int channel_count,
                                  int controller, int stream);
int platform_get_display_port_ctl_index(int controller, int stream);
bool platform_is_call_proxy_snd_device(snd_device_t snd_device);
void platform_set_audio_source_delay(audio_source_t audio_source, int delay_ms);

int platform_get_audio_source_index(const char *audio_source_name);
bool platform_check_and_update_island_power_status(void *platform,
                                                   struct audio_usecase* usecase,
                                                    snd_device_t snd_device);
bool platform_get_power_mode_on_device(void *platform, snd_device_t snd_device);
bool platform_get_island_cfg_on_device(void *platform, snd_device_t snd_device);
int platform_set_power_mode_on_device(struct audio_device* adev, snd_device_t snd_device,
                                      bool enable);
int platform_set_island_cfg_on_device(struct audio_device* adev, snd_device_t snd_device,
                                      bool enable);
void platform_reset_island_power_status(void *platform, snd_device_t snd_device);
#endif // AUDIO_PLATFORM_API_H
