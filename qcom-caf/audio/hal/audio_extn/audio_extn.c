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

#define LOG_TAG "audio_hw_extn"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <stdlib.h>
#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <cutils/properties.h>
#include <log/log.h>
#include <unistd.h>
#include <sched.h>

#include "audio_hw.h"
#include "audio_extn.h"
#include "voice_extn.h"
#include "audio_defs.h"
#include "platform.h"
#include "platform_api.h"
#include "edid.h"
#include "sound/compress_params.h"

#ifdef AUDIO_GKI_ENABLED
#include "sound/audio_compressed_formats.h"
#endif

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_AUDIO_EXTN
#include <log_utils.h>
#endif

#define MAX_SLEEP_RETRY 100
#define WIFI_INIT_WAIT_SLEEP 50
#define MAX_NUM_CHANNELS 8
#define Q14_GAIN_UNITY 0x4000

static int  vendor_enhanced_info = 0;
static bool is_compress_meta_data_enabled = false;

struct snd_card_split cur_snd_card_split = {
    .device = {0},
    .snd_card = {0},
    .form_factor = {0},
    .variant = {0},
};

struct snd_card_split *audio_extn_get_snd_card_split()
{
    return &cur_snd_card_split;
}

void fm_set_parameters(struct audio_device *adev,
                                  struct str_parms *parms);
void fm_get_parameters(struct str_parms *query, struct str_parms *reply);

void keep_alive_init(struct audio_device *adev);
void keep_alive_deinit();
void keep_alive_start(ka_mode_t ka_mode);
void keep_alive_stop(ka_mode_t ka_mode);
int keep_alive_set_parameters(struct audio_device *adev,
                                         struct str_parms *parms);

bool cin_applicable_stream(struct stream_in *in);
bool cin_attached_usecase(struct stream_in *in);
bool cin_format_supported(audio_format_t format);
int cin_acquire_usecase(struct stream_in *in);
size_t cin_get_buffer_size(struct stream_in *in);
int cin_open_input_stream(struct stream_in *in);
void cin_stop_input_stream(struct stream_in *in);
void cin_close_input_stream(struct stream_in *in);
void cin_free_input_stream_resources(struct stream_in *in);
int cin_read(struct stream_in *in, void *buffer,
                        size_t bytes, size_t *bytes_read);
int cin_configure_input_stream(struct stream_in *in, struct audio_config *in_config);

void audio_extn_set_snd_card_split(const char* in_snd_card_name)
{
    /* sound card name follows below mentioned convention
       <target name>-<sound card name>-<form factor>-snd-card
       parse target name, sound card name and form factor
    */
    char *snd_card_name = NULL;
    char *tmp = NULL;
    char *device = NULL;
    char *snd_card = NULL;
    char *form_factor = NULL;
    char *variant = NULL;

    if (in_snd_card_name == NULL) {
        ALOGE("%s: snd_card_name passed is NULL", __func__);
        goto on_error;
    }
    snd_card_name = strdup(in_snd_card_name);

    device = strtok_r(snd_card_name, "-", &tmp);
    if (device == NULL) {
        ALOGE("%s: called on invalid snd card name", __func__);
        goto on_error;
    }
    strlcpy(cur_snd_card_split.device, device, HW_INFO_ARRAY_MAX_SIZE);

    snd_card = strtok_r(NULL, "-", &tmp);
    if (snd_card == NULL) {
        ALOGE("%s: called on invalid snd card name", __func__);
        goto on_error;
    }
    strlcpy(cur_snd_card_split.snd_card, snd_card, HW_INFO_ARRAY_MAX_SIZE);

    form_factor = strtok_r(NULL, "-", &tmp);
    if (form_factor == NULL) {
        ALOGE("%s: called on invalid snd card name", __func__);
        goto on_error;
    }
    strlcpy(cur_snd_card_split.form_factor, form_factor, HW_INFO_ARRAY_MAX_SIZE);

    variant = strtok_r(NULL, "-", &tmp);
    if (variant != NULL) {
        strlcpy(cur_snd_card_split.variant, variant, HW_INFO_ARRAY_MAX_SIZE);
    }

    ALOGI("%s: snd_card_name(%s) device(%s) snd_card(%s) form_factor(%s)",
               __func__, in_snd_card_name, device, snd_card, form_factor);

on_error:
    if (snd_card_name)
        free(snd_card_name);
}

struct audio_extn_module {
    bool anc_enabled;
    bool aanc_enabled;
    bool custom_stereo_enabled;
    uint32_t proxy_channel_num;
    bool hpx_enabled;
    bool vbat_enabled;
    bool bcl_enabled;
    bool hifi_audio_enabled;
    bool ras_enabled;
    struct aptx_dec_bt_addr addr;
    struct audio_device *adev;
};

static struct audio_extn_module aextnmod;
static bool audio_extn_fm_power_opt_enabled = false;
static bool audio_extn_keep_alive_enabled = false;
static bool audio_extn_hifi_audio_enabled = false;
static bool audio_extn_ras_feature_enabled = false;
static bool audio_extn_kpi_optimize_feature_enabled = false;
static bool audio_extn_display_port_feature_enabled = false;
static bool audio_extn_fluence_feature_enabled = false;
static bool audio_extn_custom_stereo_feature_enabled = false;
static bool audio_extn_anc_headset_feature_enabled = false;
static bool audio_extn_vbat_enabled = false;
static bool audio_extn_wsa_enabled = false;
static bool audio_extn_record_play_concurrency_enabled = false;
static bool audio_extn_hdmi_passthru_enabled = false;
static bool audio_extn_concurrent_capture_enabled = false;
static bool audio_extn_compress_in_enabled = false;
static bool audio_extn_battery_listener_enabled = false;
static bool audio_extn_maxx_audio_enabled = false;
static bool audio_extn_audiozoom_enabled = false;
static bool audio_extn_hifi_filter_enabled = false;

#define AUDIO_PARAMETER_KEY_AANC_NOISE_LEVEL "aanc_noise_level"
#define AUDIO_PARAMETER_KEY_ANC        "anc_enabled"
#define AUDIO_PARAMETER_KEY_WFD        "wfd_channel_cap"
#define AUDIO_PARAMETER_CAN_OPEN_PROXY "can_open_proxy"
#define AUDIO_PARAMETER_CUSTOM_STEREO  "stereo_as_dual_mono"
/* Query offload playback instances count */
#define AUDIO_PARAMETER_OFFLOAD_NUM_ACTIVE "offload_num_active"
#define AUDIO_PARAMETER_HPX            "HPX"
#define AUDIO_PARAMETER_APTX_DEC_BT_ADDR "bt_addr"

/*
* update sysfs node hdmi_audio_cb to enable notification acknowledge feature
* bit(5) set to 1 to enable this feature
* bit(4) set to 1 to enable acknowledgement
* this is done only once at the first connect event
*
* bit(0) set to 1 when HDMI cable is connected
* bit(0) set to 0 when HDMI cable is disconnected
* this is done when device switch happens by setting audioparamter
*/

#define IS_BIT_SET(NUM, bitno) (NUM & (1 << bitno))

#define EXT_DISPLAY_PLUG_STATUS_NOTIFY_ENABLE      0x30
#define EXT_DISPLAY_PLUG_STATUS_NOTIFY_CONNECT     0x01
#define EXT_DISPLAY_PLUG_STATUS_NOTIFY_DISCONNECT  0x00

static ssize_t update_sysfs_node(const char *path, const char *data, size_t len)
{
    ssize_t err = 0;
    int fd = -1;

    err = access(path, W_OK);
    if (!err) {
        fd = open(path, O_WRONLY);
        errno = 0;
        err = write(fd, data, len);
        if (err < 0) {
            err = -errno;
        }
        close(fd);
    } else {
        ALOGE("%s: Failed to access path: %s error: %s",
                __FUNCTION__, path, strerror(errno));
        err = -errno;
    }

    return err;
}

static int get_ext_disp_sysfs_node_index(int ext_disp_type)
{
    int node_index = -1;
    char fbvalue[80] = {0};
    char fbpath[80] = {0};
    int i = 0;
    FILE *ext_disp_fd = NULL;

    while (1) {
        snprintf(fbpath, sizeof(fbpath),
                  "/sys/class/graphics/fb%d/msm_fb_type", i);
        ext_disp_fd = fopen(fbpath, "r");
        if (ext_disp_fd) {
            if (fread(fbvalue, sizeof(char), 80, ext_disp_fd)) {
                if(((strncmp(fbvalue, "dtv panel", strlen("dtv panel")) == 0) &&
                    (ext_disp_type == EXT_DISPLAY_TYPE_HDMI)) ||
                   ((strncmp(fbvalue, "dp panel", strlen("dp panel")) == 0) &&
                    (ext_disp_type == EXT_DISPLAY_TYPE_DP))) {
                    node_index = i;
                    ALOGD("%s: Ext Disp:%d is at fb%d", __func__, ext_disp_type, i);
                    fclose(ext_disp_fd);
                    return node_index;
                }
            }
            fclose(ext_disp_fd);
            i++;
        } else {
            ALOGE("%s: Scanned till end of fbs or Failed to open fb node %d", __func__, i);
            break;
        }
    }

    return -1;
}

static int update_ext_disp_sysfs_node(const struct audio_device *adev,
                                      int node_value, int controller, int stream)
{
    char ext_disp_ack_path[80] = {0};
    char ext_disp_ack_value[3] = {0};
    int index, ret = -1;
    int ext_disp_type = platform_get_ext_disp_type_v2(adev->platform, controller,
                                                      stream);

    if (ext_disp_type < 0) {
        ALOGE("%s, Unable to get the external display type, err:%d",
              __func__, ext_disp_type);
        return -EINVAL;
    }

    index = get_ext_disp_sysfs_node_index(ext_disp_type);
    if (index >= 0) {
        snprintf(ext_disp_ack_value, sizeof(ext_disp_ack_value), "%d", node_value);
        snprintf(ext_disp_ack_path, sizeof(ext_disp_ack_path),
                  "/sys/class/graphics/fb%d/hdmi_audio_cb", index);

        ret = update_sysfs_node(ext_disp_ack_path, ext_disp_ack_value,
                sizeof(ext_disp_ack_value));

        ALOGI("update hdmi_audio_cb at fb[%d] to:[%d] %s",
            index, node_value, (ret >= 0) ? "success":"fail");
    }

    return ret;
}

static int update_audio_ack_state(const struct audio_device *adev,
                                  int node_value,
                                  int controller,
                                  int stream)
{
    int ret = 0;
    int ctl_index = 0;
    struct mixer_ctl *ctl = NULL;
    const char *ctl_prefix = "External Display";
    const char *ctl_suffix = "Audio Ack";
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};

    ctl_index = platform_get_display_port_ctl_index(controller, stream);
    if (-EINVAL == ctl_index) {
        ALOGE("%s: Unknown controller/stream %d/%d",
              __func__, controller, stream);
        return -EINVAL;
    }

    if (0 == ctl_index)
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                 "%s %s", ctl_prefix, ctl_suffix);
    else
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                 "%s%d %s", ctl_prefix, ctl_index, ctl_suffix);

    ALOGV("%s: mixer ctl name: %s", __func__, mixer_ctl_name);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    /* If no mixer command support, fall back to sysfs node approach */
    if (!ctl) {
        ALOGI("%s: could not get ctl for mixer cmd(%s), use sysfs node instead\n",
              __func__, mixer_ctl_name);
        ret = update_ext_disp_sysfs_node(adev, node_value, controller, stream);
    } else {
        char *ack_str = NULL;

        if (node_value == EXT_DISPLAY_PLUG_STATUS_NOTIFY_ENABLE)
            ack_str = "Ack_Enable";
        else if (node_value == EXT_DISPLAY_PLUG_STATUS_NOTIFY_CONNECT)
            ack_str = "Connect";
        else if (node_value == EXT_DISPLAY_PLUG_STATUS_NOTIFY_DISCONNECT)
            ack_str = "Disconnect";
        else {
            ALOGE("%s: Invalid input parameter - 0x%x\n",
                  __func__, node_value);
            return -EINVAL;
        }

        ret = mixer_ctl_set_enum_by_string(ctl, ack_str);
        if (ret)
            ALOGE("%s: Could not set ctl for mixer cmd - %s ret %d\n",
                  __func__, mixer_ctl_name, ret);
    }
    return ret;
}

static void audio_extn_ext_disp_set_parameters(const struct audio_device *adev,
                                                     struct str_parms *parms)
{
    int controller = 0;
    int stream = 0;
    char value[32] = {0};
    static bool is_hdmi_sysfs_node_init = false;

    if (str_parms_get_str(parms, "connect", value, sizeof(value)) >= 0
            && (atoi(value) & AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
        //params = "connect=1024" for external display connection.
        platform_get_controller_stream_from_params(parms, &controller, &stream);
        if (is_hdmi_sysfs_node_init == false) {
            //check if this is different for dp and hdmi
            is_hdmi_sysfs_node_init = true;
            update_audio_ack_state(adev,
                                   EXT_DISPLAY_PLUG_STATUS_NOTIFY_ENABLE,
                                   controller, stream);
        }
        update_audio_ack_state(adev, EXT_DISPLAY_PLUG_STATUS_NOTIFY_CONNECT,
                               controller, stream);
    } else if(str_parms_get_str(parms, "disconnect", value, sizeof(value)) >= 0
            && (atoi(value) & AUDIO_DEVICE_OUT_AUX_DIGITAL)){
        //params = "disconnect=1024" for external display disconnection.
        platform_get_controller_stream_from_params(parms, &controller, &stream);
        update_audio_ack_state(adev, EXT_DISPLAY_PLUG_STATUS_NOTIFY_DISCONNECT,
                               controller, stream);
        ALOGV("invalidate cached edid");
        platform_invalidate_hdmi_config_v2(adev->platform, controller, stream);
    } else {
        // handle ext disp devices only
        return;
    }
}

static int update_custom_mtmx_coefficients_v2(struct audio_device *adev,
                                              struct audio_custom_mtmx_params *params,
                                              int pcm_device_id)
{
    struct mixer_ctl *ctl = NULL;
    char *mixer_name_prefix = "AudStr";
    char *mixer_name_suffix = "ChMixer Weight Ch";
    char mixer_ctl_name[128] = {0};
    struct audio_custom_mtmx_params_info *pinfo = &params->info;
    int i = 0, err = 0;
    int cust_ch_mixer_cfg[128], len = 0;

    ALOGI("%s: ip_channels %d, op_channels %d, pcm_device_id %d",
          __func__, pinfo->ip_channels, pinfo->op_channels, pcm_device_id);

    if (adev->use_old_pspd_mix_ctrl) {
        /*
         * Below code is to ensure backward compatibilty with older
         * kernel version. Use old mixer control to set mixer coefficients
         */
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
         "Audio Stream %d Channel Mix Cfg", pcm_device_id);

        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
                  __func__, mixer_ctl_name);
            return -EINVAL;
        }
        cust_ch_mixer_cfg[len++] = pinfo->ip_channels;
        cust_ch_mixer_cfg[len++] = pinfo->op_channels;
        for (i = 0; i < (int) (pinfo->op_channels * pinfo->ip_channels); i++) {
            ALOGV("%s: coeff[%d] %d", __func__, i, params->coeffs[i]);
            cust_ch_mixer_cfg[len++] = params->coeffs[i];
        }
        err = mixer_ctl_set_array(ctl, cust_ch_mixer_cfg, len);
        if (err) {
            ALOGE("%s: ERROR. Mixer ctl set failed", __func__);
            return -EINVAL;
        }
        ALOGD("%s: Mixer ctl set for %s success", __func__, mixer_ctl_name);
    } else {
        for (i = 0; i < (int)pinfo->op_channels; i++) {
            snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "%s %d %s %d",
                    mixer_name_prefix, pcm_device_id, mixer_name_suffix, i+1);

            ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
            if (!ctl) {
                ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
                      __func__, mixer_ctl_name);
                 return -EINVAL;
            }
            err = mixer_ctl_set_array(ctl,
                                      &params->coeffs[pinfo->ip_channels * i],
                                      pinfo->ip_channels);
            if (err) {
                ALOGE("%s: ERROR. Mixer ctl set failed", __func__);
                return -EINVAL;
            }
        }
    }
    return 0;
}

static void set_custom_mtmx_params_v2(struct audio_device *adev,
                                      struct audio_custom_mtmx_params_info *pinfo,
                                      int pcm_device_id, bool enable)
{
    struct mixer_ctl *ctl = NULL;
    char *mixer_name_prefix = "AudStr";
    char *mixer_name_suffix = "ChMixer Cfg";
    char mixer_ctl_name[128] = {0};
    int chmixer_cfg[5] = {0}, len = 0;
    int be_id = -1, err = 0;

    be_id = platform_get_snd_device_backend_index(pinfo->snd_device);

    ALOGI("%s: ip_channels %d,op_channels %d,pcm_device_id %d,be_id %d",
          __func__, pinfo->ip_channels, pinfo->op_channels, pcm_device_id, be_id);

    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
             "%s %d %s", mixer_name_prefix, pcm_device_id, mixer_name_suffix);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return;
    }
    chmixer_cfg[len++] = enable ? 1 : 0;
    chmixer_cfg[len++] = 0; /* rule index */
    chmixer_cfg[len++] = pinfo->ip_channels;
    chmixer_cfg[len++] = pinfo->op_channels;
    chmixer_cfg[len++] = be_id + 1;

    err = mixer_ctl_set_array(ctl, chmixer_cfg, len);
    if (err)
        ALOGE("%s: ERROR. Mixer ctl set failed", __func__);
}

void audio_extn_set_custom_mtmx_params_v2(struct audio_device *adev,
                                        struct audio_usecase *usecase,
                                        bool enable)
{
    struct audio_custom_mtmx_params_info info = {0};
    struct audio_custom_mtmx_params *params = NULL;
    int num_devices = 0, pcm_device_id = -1, i = 0, ret = 0;
    snd_device_t new_snd_devices[SND_DEVICE_OUT_END] = {0};
    struct audio_backend_cfg backend_cfg = {0};
    uint32_t feature_id = 0, idx = 0;

    switch(usecase->type) {
    case PCM_PLAYBACK:
        if (usecase->stream.out) {
            pcm_device_id =
                platform_get_pcm_device_id(usecase->id, PCM_PLAYBACK);
            if (platform_split_snd_device(adev->platform,
                                          usecase->out_snd_device,
                                          &num_devices, new_snd_devices)) {
                new_snd_devices[0] = usecase->out_snd_device;
                num_devices = 1;
            }
        } else {
            ALOGE("%s: invalid output stream for playback usecase id:%d",
                  __func__, usecase->id);
            return;
        }
        break;
    case PCM_CAPTURE:
        if (usecase->stream.in) {
            pcm_device_id =
                platform_get_pcm_device_id(usecase->id, PCM_CAPTURE);
            if (platform_split_snd_device(adev->platform,
                                          usecase->in_snd_device,
                                          &num_devices, new_snd_devices)) {
                new_snd_devices[0] = usecase->in_snd_device;
                num_devices = 1;
            }
        } else {
            ALOGE("%s: invalid input stream for capture usecase id:%d",
                  __func__, usecase->id);
            return;
        }
        break;
    default:
        ALOGV("%s: unsupported usecase id:%d", __func__, usecase->id);
        return;
    }

    /*
     * check and update feature_id before this assignment,
     * if features like dual_mono is enabled and overrides the default(i.e. 0).
     */
    info.id = feature_id;
    info.usecase_id[0] = usecase->id;
    for (i = 0, ret = 0; i < num_devices; i++) {
        info.snd_device = new_snd_devices[i];
        platform_get_codec_backend_cfg(adev, info.snd_device, &backend_cfg);
        if (usecase->type == PCM_PLAYBACK) {
            info.ip_channels = audio_channel_count_from_out_mask(
                                   usecase->stream.out->channel_mask);
            info.op_channels = backend_cfg.channels;
        } else {
            info.ip_channels = backend_cfg.channels;
            info.op_channels = audio_channel_count_from_in_mask(
                                   usecase->stream.in->channel_mask);
        }
        params = platform_get_custom_mtmx_params(adev->platform, &info, &idx);
        if (params) {
            if (enable)
                ret = update_custom_mtmx_coefficients_v2(adev, params,
                                                      pcm_device_id);
            if (ret < 0)
                ALOGE("%s: error updating mtmx coeffs err:%d", __func__, ret);
            else
                set_custom_mtmx_params_v2(adev, &info, pcm_device_id, enable);
        }
    }
}

static int set_custom_mtmx_output_channel_map(struct audio_device *adev,
                                              char *mixer_name_prefix,
                                              uint32_t ch_count,
                                              bool enable)
{
    struct mixer_ctl *ctl = NULL;
    char mixer_ctl_name[128] = {0};
    int ret = 0;
    int channel_map[AUDIO_MAX_DSP_CHANNELS] = {0};

    ALOGV("%s channel_count %d", __func__, ch_count);

    if (!enable) {
        ALOGV("%s: reset output channel map", __func__);
        goto exit;
    }

    switch (ch_count) {
    case 2:
        channel_map[0] = PCM_CHANNEL_FL;
        channel_map[1] = PCM_CHANNEL_FR;
        break;
    case 4:
        channel_map[0] = PCM_CHANNEL_FL;
        channel_map[1] = PCM_CHANNEL_FR;
        channel_map[2] = PCM_CHANNEL_LS;
        channel_map[3] = PCM_CHANNEL_RS;
        break;
    case 6:
        channel_map[0] = PCM_CHANNEL_FL;
        channel_map[1] = PCM_CHANNEL_FR;
        channel_map[2] = PCM_CHANNEL_FC;
        channel_map[3] = PCM_CHANNEL_LFE;
        channel_map[4] = PCM_CHANNEL_LS;
        channel_map[5] = PCM_CHANNEL_RS;
        break;
    case 8:
        channel_map[0] = PCM_CHANNEL_FL;
        channel_map[1] = PCM_CHANNEL_FR;
        channel_map[2] = PCM_CHANNEL_FC;
        channel_map[3] = PCM_CHANNEL_LFE;
        channel_map[4] = PCM_CHANNEL_LB;
        channel_map[5] = PCM_CHANNEL_RB;
        channel_map[6] = PCM_CHANNEL_LS;
        channel_map[7] = PCM_CHANNEL_RS;
        break;
    case 10:
        channel_map[0] = PCM_CHANNEL_FL;
        channel_map[1] = PCM_CHANNEL_FR;
        channel_map[2] = PCM_CHANNEL_LFE;
        channel_map[3] = PCM_CHANNEL_FC;
        channel_map[4] = PCM_CHANNEL_LB;
        channel_map[5] = PCM_CHANNEL_RB;
        channel_map[6] = PCM_CHANNEL_LS;
        channel_map[7] = PCM_CHANNEL_RS;
        channel_map[8] = PCM_CHANNEL_TFL;
        channel_map[9] = PCM_CHANNEL_TFR;
        break;
    case 12:
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
    case 14:
        channel_map[0] = PCM_CHANNEL_FL;
        channel_map[1] = PCM_CHANNEL_FR;
        channel_map[2] = PCM_CHANNEL_LFE;
        channel_map[3] = PCM_CHANNEL_FC;
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
        break;
    case 16:
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
        ALOGE("%s: unsupported channels(%d) for setting channel map",
               __func__, ch_count);
        return -EINVAL;
    }

exit:
    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "%s %s",
             mixer_name_prefix, "Output Channel Map");

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
               __func__, mixer_ctl_name);
        return -EINVAL;
    }

    ret = mixer_ctl_set_array(ctl, channel_map, ch_count);
    return ret;
}

static int update_custom_mtmx_coefficients_v1(struct audio_device *adev,
                                           struct audio_custom_mtmx_params *params,
                                           struct audio_custom_mtmx_in_params *in_params,
                                           int pcm_device_id,
                                           usecase_type_t type,
                                           bool enable,
                                           uint32_t idx)
{
    struct mixer_ctl *ctl = NULL;
    char mixer_ctl_name[128] = {0};
    struct audio_custom_mtmx_params_info *pinfo = &params->info;
    char mixer_name_prefix[100];
    int i = 0, err = 0, rule = 0;
    uint32_t mtrx_row_cnt = 0, mtrx_column_cnt = 0;
    int reset_coeffs[AUDIO_MAX_DSP_CHANNELS] = {0};

    ALOGI("%s: ip_channels %d, op_channels %d, pcm_device_id %d, usecase type %d, enable %d",
          __func__, pinfo->ip_channels, pinfo->op_channels, pcm_device_id,
          type, enable);

    if (pinfo->fe_id[idx] == 0) {
        ALOGE("%s: Error. no front end defined", __func__);
        return -EINVAL;
    }

    snprintf(mixer_name_prefix, sizeof(mixer_name_prefix), "%s%d",
             "MultiMedia", pinfo->fe_id[idx]);
    /*
     * Enable/Disable channel mixer.
     * If enable, use params and in_params to configure mixer.
     * If disable, reset previously configured mixer.
    */
    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "%s %s",
             mixer_name_prefix, "Channel Mixer");

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
               __func__, mixer_ctl_name);
        return -EINVAL;
    }

    if (enable)
        err = mixer_ctl_set_enum_by_string(ctl, "Enable");
    else
        err = mixer_ctl_set_enum_by_string(ctl, "Disable");

    if (err) {
        ALOGE("%s: ERROR. %s channel mixer failed", __func__,
              enable ? "Enable" : "Disable");
        return -EINVAL;
    }

    /* Configure output channels of channel mixer */
    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "%s %s",
             mixer_name_prefix, "Channels");

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
               __func__, mixer_ctl_name);
        return -EINVAL;
    }

    mtrx_row_cnt = pinfo->op_channels;
    mtrx_column_cnt = pinfo->ip_channels;

    if (enable)
        err = mixer_ctl_set_value(ctl, 0, mtrx_row_cnt);
    else
        err = mixer_ctl_set_value(ctl, 0, 0);

    if (err) {
        ALOGE("%s: ERROR. %s mixer output channels failed", __func__,
              enable ? "Set" : "Reset");
        return -EINVAL;
    }


    /* To keep output channel map in sync with asm driver channel mapping */
    err = set_custom_mtmx_output_channel_map(adev, mixer_name_prefix, mtrx_row_cnt,
                                       enable);
    if (err) {
        ALOGE("%s: ERROR. %s mtmx output channel map failed", __func__,
              enable ? "Set" : "Reset");
        return -EINVAL;
    }

    /* Send channel mixer rule */
    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "%s %s",
             mixer_name_prefix, "Channel Rule");

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
               __func__, mixer_ctl_name);
        return -EINVAL;
    }

    mixer_ctl_set_value(ctl, 0, rule);

    /* Send channel coefficients for each output channel */
    for (i = 0; i < mtrx_row_cnt; i++) {
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "%s %s%d",
                 mixer_name_prefix, "Output Channel", i+1);
        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
                  __func__, mixer_ctl_name);
            return -EINVAL;
        }

        if (enable)
            err = mixer_ctl_set_array(ctl,
                                  &params->coeffs[mtrx_column_cnt * i],
                                  mtrx_column_cnt);
        else
            err = mixer_ctl_set_array(ctl,
                                  reset_coeffs,
                                  mtrx_column_cnt);
        if (err) {
            ALOGE("%s: ERROR. %s coefficients failed for output channel %d",
                   __func__, enable ? "Set" : "Reset", i);
            return -EINVAL;
        }
    }

    /* Configure backend interfaces with information provided in xml */
    i = 0;
    while (in_params->in_ch_info[i].ch_count != 0) {
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "%s %s%d",
                 mixer_name_prefix, "Channel", i+1);
        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
                  __func__, mixer_ctl_name);
            return -EINVAL;
        }
        if (enable) {
            ALOGD("%s: mixer %s, interface %s", __func__, mixer_ctl_name,
                   in_params->in_ch_info[i].hw_interface);
            err = mixer_ctl_set_enum_by_string(ctl,
                      in_params->in_ch_info[i].hw_interface);
        } else {
            err = mixer_ctl_set_enum_by_string(ctl, "ZERO");
        }

        if (err) {
            ALOGE("%s: ERROR. %s channel backend interface failed", __func__,
                   enable ? "Set" : "Reset");
            return -EINVAL;
        }
        i++;
    }

    return 0;
}


void audio_extn_set_custom_mtmx_params_v1(struct audio_device *adev,
                                       struct audio_usecase *usecase,
                                       bool enable)
{
    struct audio_custom_mtmx_params_info info = {0};
    struct audio_custom_mtmx_params *params = NULL;
    struct audio_custom_mtmx_in_params_info in_info = {0};
    struct audio_custom_mtmx_in_params *in_params = NULL;
    int pcm_device_id = -1, ret = 0;
    uint32_t feature_id = 0, idx = 0;

    switch(usecase->type) {
    case PCM_CAPTURE:
        if (usecase->stream.in) {
            pcm_device_id =
                platform_get_pcm_device_id(usecase->id, PCM_CAPTURE);
            info.snd_device = usecase->in_snd_device;
        } else {
            ALOGE("%s: invalid input stream for capture usecase id:%d",
                  __func__, usecase->id);
            return;
        }
        break;
    case PCM_PLAYBACK:
    default:
        ALOGV("%s: unsupported usecase id:%d", __func__, usecase->id);
        return;
    }

    ALOGD("%s: snd device %d", __func__, info.snd_device);
    info.id = feature_id;
    info.usecase_id[0] = usecase->id;
    info.op_channels = audio_channel_count_from_in_mask(
                                usecase->stream.in->channel_mask);

    in_info.usecase_id[0] = info.usecase_id[0];
    in_info.op_channels = info.op_channels;
    in_params = platform_get_custom_mtmx_in_params(adev->platform, &in_info);
    if (!in_params) {
        ALOGE("%s: Could not get in params for usecase %d, channels %d",
               __func__, in_info.usecase_id[0], in_info.op_channels);
        return;
    }

    info.ip_channels = in_params->ip_channels;
    ALOGD("%s: ip channels %d, op channels %d", __func__, info.ip_channels, info.op_channels);

    params = platform_get_custom_mtmx_params(adev->platform, &info, &idx);
    if (params) {
        ret = update_custom_mtmx_coefficients_v1(adev, params, in_params,
                             pcm_device_id, usecase->type, enable, idx);
        if (ret < 0)
            ALOGE("%s: error updating mtmx coeffs err:%d", __func__, ret);
    }
}

snd_device_t audio_extn_get_loopback_snd_device(struct audio_device *adev,
                                                struct audio_usecase *usecase,
                                                int channel_count)
{
    snd_device_t snd_device = SND_DEVICE_NONE;
    struct audio_custom_mtmx_in_params_info in_info = {0};
    struct audio_custom_mtmx_in_params *in_params = NULL;

    if (!adev || !usecase) {
        ALOGE("%s: Invalid params", __func__);
        return snd_device;
    }

    in_info.usecase_id[0] = usecase->id;
    in_info.op_channels = channel_count;
    in_params = platform_get_custom_mtmx_in_params(adev->platform, &in_info);
    if (!in_params) {
        ALOGE("%s: Could not get in params for usecase %d, channels %d",
               __func__, in_info.usecase_id[0], in_info.op_channels);
        return snd_device;
    }

    switch(in_params->mic_ch) {
    case 2:
        snd_device = SND_DEVICE_IN_HANDSET_DMIC_AND_EC_REF_LOOPBACK;
        break;
    case 4:
        snd_device = SND_DEVICE_IN_HANDSET_QMIC_AND_EC_REF_LOOPBACK;
        break;
    case 6:
        snd_device = SND_DEVICE_IN_HANDSET_6MIC_AND_EC_REF_LOOPBACK;
        break;
    case 8:
        snd_device = SND_DEVICE_IN_HANDSET_8MIC_AND_EC_REF_LOOPBACK;
        break;
    default:
        ALOGE("%s: Unsupported mic channels %d",
               __func__, in_params->mic_ch);
        break;
    }

    ALOGD("%s: return snd device %d", __func__, snd_device);
    return snd_device;
}

#ifndef DTS_EAGLE
#define audio_extn_hpx_set_parameters(adev, parms)         (0)
#define audio_extn_hpx_get_parameters(query, reply)  (0)
#define audio_extn_check_and_set_dts_hpx_state(adev)       (0)
#else
void audio_extn_hpx_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms)
{
    int ret = 0;
    char value[32]={0};
    char prop[PROPERTY_VALUE_MAX] = "false";
    bool hpx_state = false;
    const char *mixer_ctl_name = "Set HPX OnOff";
    struct mixer_ctl *ctl = NULL;
    ALOGV("%s", __func__);

    property_get("vendor.audio.use.dts_eagle", prop, "0");
    if (strncmp("true", prop, sizeof("true")))
        return;

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_HPX, value,
                            sizeof(value));
    if (ret >= 0) {
        if (!strncmp("ON", value, sizeof("ON")))
            hpx_state = true;

        if (hpx_state == aextnmod.hpx_enabled)
            return;

        aextnmod.hpx_enabled = hpx_state;
        /* set HPX state on stream pp */
        if (adev->offload_effects_set_hpx_state != NULL)
            adev->offload_effects_set_hpx_state(hpx_state);

        audio_extn_dts_eagle_fade(adev, aextnmod.hpx_enabled, NULL);
        /* set HPX state on device pp */
        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (ctl)
            mixer_ctl_set_value(ctl, 0, aextnmod.hpx_enabled);
    }
}

static int audio_extn_hpx_get_parameters(struct str_parms *query,
                                       struct str_parms *reply)
{
    int ret;
    char value[32]={0};

    ALOGV("%s: hpx %d", __func__, aextnmod.hpx_enabled);
    ret = str_parms_get_str(query, AUDIO_PARAMETER_HPX, value,
                            sizeof(value));
    if (ret >= 0) {
        if (aextnmod.hpx_enabled)
            str_parms_add_str(reply, AUDIO_PARAMETER_HPX, "ON");
        else
            str_parms_add_str(reply, AUDIO_PARAMETER_HPX, "OFF");
    }
    return ret;
}

void audio_extn_check_and_set_dts_hpx_state(const struct audio_device *adev)
{
    char prop[PROPERTY_VALUE_MAX];
    property_get("vendor.audio.use.dts_eagle", prop, "0");
    if (strncmp("true", prop, sizeof("true")))
        return;
    if (adev->offload_effects_set_hpx_state)
        adev->offload_effects_set_hpx_state(aextnmod.hpx_enabled);
}
#endif

/* Affine AHAL thread to CPU core */
void audio_extn_set_cpu_affinity()
{
    cpu_set_t cpuset;
    struct sched_param sched_param;
    int policy = SCHED_FIFO, rc = 0;

    ALOGV("%s: Set CPU affinity for read thread", __func__);
    CPU_ZERO(&cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0)
        ALOGE("%s: CPU Affinity allocation failed for Capture thread",
               __func__);

    sched_param.sched_priority = sched_get_priority_min(policy);
    rc = sched_setscheduler(0, policy, &sched_param);
    if (rc != 0)
         ALOGE("%s: Failed to set realtime priority", __func__);
}

// START: VBAT =============================================================
void vbat_feature_init(bool is_feature_enabled)
{
    audio_extn_vbat_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature VBAT is %s ----",
                  __func__, is_feature_enabled ? "ENABLED": " NOT ENABLED");
}

bool audio_extn_is_vbat_enabled(void)
{
    if (!audio_extn_vbat_enabled)
        return false;

    ALOGD("%s: status: %d", __func__, aextnmod.vbat_enabled);
    return (aextnmod.vbat_enabled ? true: false);
}

bool audio_extn_can_use_vbat(void)
{
    if (!audio_extn_vbat_enabled)
        return false;

    char prop_vbat_enabled[PROPERTY_VALUE_MAX] = "false";

    property_get("persist.vendor.audio.vbat.enabled", prop_vbat_enabled, "0");
    if (!strncmp("true", prop_vbat_enabled, 4)) {
        aextnmod.vbat_enabled = 1;
    }

    ALOGD("%s: vbat.enabled property is set to %s", __func__, prop_vbat_enabled);
    return (aextnmod.vbat_enabled ? true: false);
}

bool audio_extn_is_bcl_enabled(void)
{
    if (!audio_extn_vbat_enabled)
        return false;

    ALOGD("%s: status: %d", __func__, aextnmod.bcl_enabled);
    return (aextnmod.bcl_enabled ? true: false);
}

bool audio_extn_can_use_bcl(void)
{
    if (!audio_extn_vbat_enabled)
        return false;

    char prop_bcl_enabled[PROPERTY_VALUE_MAX] = "false";

    property_get("persist.vendor.audio.bcl.enabled", prop_bcl_enabled, "0");
    if (!strncmp("true", prop_bcl_enabled, 4)) {
        aextnmod.bcl_enabled = 1;
    }

    ALOGD("%s: bcl.enabled property is set to %s", __func__, prop_bcl_enabled);
    return (aextnmod.bcl_enabled ? true: false);
}

// START: ANC_HEADSET -------------------------------------------------------
void anc_headset_feature_init(bool is_feature_enabled)
{
    audio_extn_anc_headset_feature_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature ANC_HEADSET is %s----", __func__,
                                    is_feature_enabled? "ENABLED": "NOT ENABLED");

}

bool audio_extn_get_anc_enabled(void)
{
    ALOGD("%s: anc_enabled:%d", __func__,
        (aextnmod.anc_enabled && audio_extn_anc_headset_feature_enabled));
    return (aextnmod.anc_enabled &&
        audio_extn_anc_headset_feature_enabled);
}

bool audio_extn_should_use_handset_anc(int in_channels)
{
    if (audio_extn_anc_headset_feature_enabled) {
        char prop_aanc[PROPERTY_VALUE_MAX] = "false";

        property_get("persist.vendor.audio.aanc.enable", prop_aanc, "0");
        if (!strncmp("true", prop_aanc, 4)) {
            ALOGD("%s: AANC enabled in the property", __func__);
            aextnmod.aanc_enabled = 1;
        }

        return (aextnmod.aanc_enabled && aextnmod.anc_enabled
                && (in_channels == 1));
    }
    return false;
}

bool audio_extn_should_use_fb_anc(void)
{
  char prop_anc[PROPERTY_VALUE_MAX] = "feedforward";

  if (audio_extn_anc_headset_feature_enabled) {
      property_get("persist.vendor.audio.headset.anc.type", prop_anc, "0");
      if (!strncmp("feedback", prop_anc, sizeof("feedback"))) {
        ALOGD("%s: FB ANC headset type enabled\n", __func__);
        return true;
      }
  }
  return false;
}

void audio_extn_set_aanc_noise_level(struct audio_device *adev,
                                     struct str_parms *parms)
{
    int ret;
    char value[32] = {0};
    struct mixer_ctl *ctl = NULL;
    const char *mixer_ctl_name = "AANC Noise Level";

    if(audio_extn_anc_headset_feature_enabled) {
        ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_AANC_NOISE_LEVEL, value,
                            sizeof(value));
        if (ret >= 0) {
            ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
            if (ctl)
                mixer_ctl_set_value(ctl, 0, atoi(value));
            else
                ALOGW("%s: Not able to get mixer ctl: %s",
                      __func__, mixer_ctl_name);
        }
    }
}

void audio_extn_set_anc_parameters(struct audio_device *adev,
                                   struct str_parms *parms)
{
    int ret;
    char value[32] ={0};
    struct listnode *node;
    struct audio_usecase *usecase;
    struct str_parms *query_44_1;
    struct str_parms *reply_44_1;
    struct str_parms *parms_disable_44_1;

    if(!audio_extn_anc_headset_feature_enabled)
        return;

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_ANC, value,
                            sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, "true") == 0)
            aextnmod.anc_enabled = true;
        else
            aextnmod.anc_enabled = false;

        /* Store current 44.1 configuration and disable it temporarily before
         * changing ANC state.
         * Since 44.1 playback is not allowed with anc on.
         * If ANC switch is done when 44.1 is active three devices would need
         * sequencing 1. "headphones-44.1", 2. "headphones-anc" and
         * 3. "headphones".
         * Note: Enable/diable of anc would affect other two device's state.
         */
        query_44_1 = str_parms_create_str(AUDIO_PARAMETER_KEY_NATIVE_AUDIO);
        reply_44_1 = str_parms_create();
        if (!query_44_1 || !reply_44_1) {
            if (query_44_1) {
                str_parms_destroy(query_44_1);
            }
            if (reply_44_1) {
                str_parms_destroy(reply_44_1);
            }

            ALOGE("%s: param creation failed", __func__);
            return;
        }

        platform_get_parameters(adev->platform, query_44_1, reply_44_1);

        parms_disable_44_1 = str_parms_create();
        if (!parms_disable_44_1) {
            str_parms_destroy(query_44_1);
            str_parms_destroy(reply_44_1);
            ALOGE("%s: param creation failed for parms_disable_44_1", __func__);
            return;
        }

        str_parms_add_str(parms_disable_44_1, AUDIO_PARAMETER_KEY_NATIVE_AUDIO, "false");
        platform_set_parameters(adev->platform, parms_disable_44_1);
        str_parms_destroy(parms_disable_44_1);

        // Refresh device selection for anc playback
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (usecase->stream.out && usecase->type != PCM_CAPTURE) {
                if (is_single_device_type_equal(&usecase->stream.out->device_list,
                                AUDIO_DEVICE_OUT_WIRED_HEADPHONE) ||
                    is_single_device_type_equal(&usecase->stream.out->device_list,
                                AUDIO_DEVICE_OUT_WIRED_HEADSET) ||
                    is_single_device_type_equal(&usecase->stream.out->device_list,
                                AUDIO_DEVICE_OUT_EARPIECE)) {
                        select_devices(adev, usecase->id);
                        ALOGV("%s: switching device completed", __func__);
                        break;
                }
            }
        }

        // Restore 44.1 configuration on top of updated anc state
        platform_set_parameters(adev->platform, reply_44_1);
        str_parms_destroy(query_44_1);
        str_parms_destroy(reply_44_1);
    }

    ALOGV("%s: anc_enabled:%d", __func__, aextnmod.anc_enabled);
}
// END: ANC_HEADSET -------------------------------------------------------

static int32_t afe_proxy_set_channel_mapping(struct audio_device *adev,
                                                     int channel_count,
                                                     snd_device_t snd_device)
{
    struct mixer_ctl *ctl = NULL, *be_ctl = NULL;
    const char *mixer_ctl_name = "Playback Device Channel Map";
    const char *be_mixer_ctl_name = "Backend Device Channel Map";
    long set_values[FCC_8] = {0};
    long be_set_values[FCC_8 + 1] = {0};
    int ret = -1;
    int be_idx = -1;

    ALOGV("%s channel_count:%d",__func__, channel_count);

    switch (channel_count) {
    case 2:
        set_values[0] = PCM_CHANNEL_FL;
        set_values[1] = PCM_CHANNEL_FR;
        break;
     case 4:
        set_values[0] = PCM_CHANNEL_FL;
        set_values[1] = PCM_CHANNEL_FR;
        set_values[2] = PCM_CHANNEL_LS;
        set_values[3] = PCM_CHANNEL_LFE;
        break;
    case 6:
        set_values[0] = PCM_CHANNEL_FL;
        set_values[1] = PCM_CHANNEL_FR;
        set_values[2] = PCM_CHANNEL_FC;
        set_values[3] = PCM_CHANNEL_LFE;
        set_values[4] = PCM_CHANNEL_LS;
        set_values[5] = PCM_CHANNEL_RS;
        break;
    case 8:
        set_values[0] = PCM_CHANNEL_FL;
        set_values[1] = PCM_CHANNEL_FR;
        set_values[2] = PCM_CHANNEL_FC;
        set_values[3] = PCM_CHANNEL_LFE;
        set_values[4] = PCM_CHANNEL_LS;
        set_values[5] = PCM_CHANNEL_RS;
        set_values[6] = PCM_CHANNEL_LB;
        set_values[7] = PCM_CHANNEL_RB;
        break;
    default:
        ALOGE("unsupported channels(%d) for setting channel map",
                                                    channel_count);
        return -EINVAL;
    }

    be_idx = platform_get_snd_device_backend_index(snd_device);

    if (be_idx >= 0) {
        be_ctl = mixer_get_ctl_by_name(adev->mixer, be_mixer_ctl_name);
        if (!be_ctl) {
            ALOGD("%s: Could not get ctl for mixer cmd - %s, using default control",
                  __func__, be_mixer_ctl_name);
            ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        } else
            ctl = be_ctl;
    } else
         ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);

    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }

    ALOGV("AFE: set mapping(%ld %ld %ld %ld %ld %ld %ld %ld) for channel:%d",
        set_values[0], set_values[1], set_values[2], set_values[3], set_values[4],
        set_values[5], set_values[6], set_values[7], channel_count);

    if (!be_ctl)
        ret = mixer_ctl_set_array(ctl, set_values, channel_count);
    else {
       be_set_values[0] = be_idx;
       memcpy(&be_set_values[1], set_values, sizeof(long) * channel_count);
       ret = mixer_ctl_set_array(ctl, be_set_values, ARRAY_SIZE(be_set_values));
    }

    return ret;
}

int32_t audio_extn_set_afe_proxy_channel_mixer(struct audio_device *adev,
                                    int channel_count, snd_device_t snd_device)
{
    int32_t ret = 0;
    const char *channel_cnt_str = NULL;
    struct mixer_ctl *ctl = NULL;
    const char *mixer_ctl_name = "PROXY_RX Channels";

    if (!property_get_bool("vendor.audio.feature.afe_proxy.enable", false)) {
        ALOGW("%s: AFE_PROXY is disabled", __func__);
        return -ENOSYS;
    }

    ALOGD("%s: entry", __func__);
    /* use the existing channel count set by hardware params to
    configure the back end for stereo as usb/a2dp would be
    stereo by default */
    ALOGD("%s: channels = %d", __func__, channel_count);
    switch (channel_count) {
    case 8: channel_cnt_str = "Eight"; break;
    case 7: channel_cnt_str = "Seven"; break;
    case 6: channel_cnt_str = "Six"; break;
    case 5: channel_cnt_str = "Five"; break;
    case 4: channel_cnt_str = "Four"; break;
    case 3: channel_cnt_str = "Three"; break;
    default: channel_cnt_str = "Two"; break;
    }

    if(channel_count >= 2 && channel_count <= 8) {
       ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
       if (!ctl) {
            ALOGE("%s: could not get ctl for mixer cmd - %s",
                  __func__, mixer_ctl_name);
        return -EINVAL;
       }
    }
    mixer_ctl_set_enum_by_string(ctl, channel_cnt_str);

    if (channel_count == 6 || channel_count == 8 || channel_count == 2 || channel_count == 4) {
        ret = afe_proxy_set_channel_mapping(adev, channel_count, snd_device);
    } else {
        ALOGE("%s: set unsupported channel count(%d)",  __func__, channel_count);
        ret = -EINVAL;
    }

    ALOGD("%s: exit", __func__);
    return ret;
}

void audio_extn_set_afe_proxy_parameters(struct audio_device *adev,
                                         struct str_parms *parms)
{
    int ret, val;
    char value[32]={0};

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_WFD, value,
                            sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        aextnmod.proxy_channel_num = val;
        adev->cur_wfd_channels = val;
        ALOGD("%s: channel capability set to: %d", __func__,
               aextnmod.proxy_channel_num);
    }
}

int audio_extn_get_afe_proxy_parameters(const struct audio_device *adev,
                                        struct str_parms *query,
                                        struct str_parms *reply)
{
    int ret, val = 0;
    char value[32]={0};

    ret = str_parms_get_str(query, AUDIO_PARAMETER_CAN_OPEN_PROXY, value,
                            sizeof(value));
    if (ret >= 0) {
        val = (adev->allow_afe_proxy_usage ? 1: 0);
        str_parms_add_int(reply, AUDIO_PARAMETER_CAN_OPEN_PROXY, val);
    }
    ALOGV("%s: called ... can_use_proxy %d", __func__, val);
    return 0;
}

/* must be called with hw device mutex locked */
int32_t audio_extn_read_afe_proxy_channel_masks(struct stream_out *out)
{
    int ret = 0;
    int channels = aextnmod.proxy_channel_num;

    if (!property_get_bool("vendor.audio.feature.afe_proxy.enable",
                            false)) {
        ALOGW("%s: AFE_PROXY is disabled", __func__);
        return -ENOSYS;
    }

    switch (channels) {
        /*
         * Do not handle stereo output in Multi-channel cases
         * Stereo case is handled in normal playback path
         */
    case 6:
        ALOGV("%s: AFE PROXY supports 5.1", __func__);
        out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_5POINT1;
        break;
    case 8:
        ALOGV("%s: AFE PROXY supports 5.1 and 7.1 channels", __func__);
        out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_5POINT1;
        out->supported_channel_masks[1] = AUDIO_CHANNEL_OUT_7POINT1;
        break;
    default:
        ALOGE("AFE PROXY does not support multi channel playback");
        ret = -ENOSYS;
        break;
    }
    return ret;
}

int32_t audio_extn_get_afe_proxy_channel_count()
{

    if (!property_get_bool("vendor.audio.feature.afe_proxy.enable",
                            false)) {
        ALOGW("%s: AFE_PROXY is disabled", __func__);
        return -ENOSYS;
    }

    return aextnmod.proxy_channel_num;
}

static int get_active_offload_usecases(const struct audio_device *adev,
                                       struct str_parms *query,
                                       struct str_parms *reply)
{
    int ret, count = 0;
    char value[32]={0};
    struct listnode *node;
    struct audio_usecase *usecase;

    ALOGV("%s", __func__);
    ret = str_parms_get_str(query, AUDIO_PARAMETER_OFFLOAD_NUM_ACTIVE, value,
                            sizeof(value));
    if (ret >= 0) {
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (is_offload_usecase(usecase->id))
                count++;
        }
        ALOGV("%s, number of active offload usecases: %d", __func__, count);
        str_parms_add_int(reply, AUDIO_PARAMETER_OFFLOAD_NUM_ACTIVE, count);
    }
    return ret;
}

void compress_meta_data_feature_init(bool is_featuer_enabled)
{
    is_compress_meta_data_enabled = is_featuer_enabled;
}

bool if_compress_meta_data_feature_enabled()
{
    return is_compress_meta_data_enabled;
}

//START: USB_OFFLOAD ==========================================================
// LIB is part of hal lib, so no need for dlopen and getting function pointer
// rather have function declared here

void usb_init(void *adev);
void usb_deinit();
void usb_add_device(audio_devices_t device, int card);
void usb_remove_device(audio_devices_t device, int card);
bool usb_is_config_supported(unsigned int *bit_width,
                                        unsigned int *sample_rate,
                                        unsigned int *ch,
                                        bool is_playback);
int usb_enable_sidetone(int device, bool enable);
void usb_set_sidetone_gain(struct str_parms *parms,
                                     char *value, int len);
bool usb_is_capture_supported();
int usb_get_max_channels(bool playback);
int usb_get_max_bit_width(bool playback);
int usb_get_sup_sample_rates(bool type, uint32_t *sr, uint32_t l);
bool usb_is_tunnel_supported();
bool usb_alive(int card);
bool usb_connected(struct str_parms *parms);
char *usb_usbid(void);
unsigned long usb_find_service_interval(bool min, bool playback);
int usb_altset_for_service_interval(bool is_playback,
                                               unsigned long service_interval,
                                               uint32_t *bit_width,
                                               uint32_t *sample_rate,
                                               uint32_t *channel_count);
int usb_set_service_interval(bool playback,
                                        unsigned long service_interval,
                                        bool *reconfig);
int usb_get_service_interval(bool playback,
                                        unsigned long *service_interval);
int usb_check_and_set_svc_int(struct audio_usecase *uc_info,
                                         bool starting_output_stream);
bool usb_is_reconfig_req();
void usb_set_reconfig(bool is_required);

static bool is_usb_offload_enabled = false;
static bool is_usb_burst_mode_enabled = false;
static bool is_usb_sidetone_vol_enabled = false;

void usb_offload_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    is_usb_offload_enabled = is_feature_enabled;
}

void usb_offload_burst_mode_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    is_usb_burst_mode_enabled = is_feature_enabled;
}

void usb_offload_sidetone_volume_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    is_usb_sidetone_vol_enabled = is_feature_enabled;
}

bool audio_extn_usb_is_sidetone_volume_enabled()
{
    return is_usb_sidetone_vol_enabled;
}

void audio_extn_usb_init(void *adev)
{
    if (is_usb_offload_enabled)
        usb_init(adev);
}


void audio_extn_usb_deinit()
{
    if (is_usb_offload_enabled)
        usb_deinit();
}

void audio_extn_usb_add_device(audio_devices_t device, int card)
{
    if (is_usb_offload_enabled)
        usb_add_device(device, card);
}

void audio_extn_usb_remove_device(audio_devices_t device, int card)
{
    if (is_usb_offload_enabled)
        usb_remove_device(device, card);
}

bool audio_extn_usb_is_config_supported(unsigned int *bit_width,
                                        unsigned int *sample_rate,
                                        unsigned int *ch,
                                        bool is_playback)
{
    bool ret_val = false;
    if (is_usb_offload_enabled)
        ret_val = usb_is_config_supported(bit_width, sample_rate, ch, is_playback);

    return ret_val;
}

int audio_extn_usb_enable_sidetone(int device, bool enable)
{
    int ret_val = 0;
    if (is_usb_offload_enabled)
        ret_val = usb_enable_sidetone(device, enable);

    return ret_val;
}

void audio_extn_usb_set_sidetone_gain(struct str_parms *parms,
                                      char *value, int len)
{
    if (is_usb_offload_enabled)
        usb_set_sidetone_gain(parms, value, len);
}

bool audio_extn_usb_is_capture_supported()
{
    bool ret_val = false;
    if (is_usb_offload_enabled)
        ret_val = usb_is_capture_supported();

    return ret_val;
}

int audio_extn_usb_get_max_channels(bool playback)
{
    int ret_val = 0;
    if (is_usb_offload_enabled)
        ret_val = usb_get_max_channels(playback);

    return ret_val;
}

int audio_extn_usb_get_max_bit_width(bool playback)
{
    int ret_val = 0;
    if (is_usb_offload_enabled)
        ret_val = usb_get_max_bit_width(playback);

    return ret_val;
}


int audio_extn_usb_get_sup_sample_rates(bool type, uint32_t *sr, uint32_t l)
{
    int ret_val = 0;
    if (is_usb_offload_enabled)
        ret_val = usb_get_sup_sample_rates(type, sr, l);

    return ret_val;
}

bool audio_extn_usb_is_tunnel_supported()
{
    bool ret_val = false;
    if (is_usb_offload_enabled)
        ret_val = usb_is_tunnel_supported();

    return ret_val;
}

bool audio_extn_usb_alive(int card)
{
    bool ret_val = false;
    if (is_usb_offload_enabled)
        ret_val = usb_alive(card);

    return ret_val;
}

bool audio_extn_usb_connected(struct str_parms *parms)
{
    bool ret_val = false;
    if (is_usb_offload_enabled)
        ret_val = usb_connected(parms);

    return ret_val;
}

char *audio_extn_usb_usbid(void)
{
    char *ret_val = NULL;
    if (is_usb_offload_enabled)
        ret_val = usb_usbid();

    return ret_val;
}

unsigned long audio_extn_usb_find_service_interval(bool min, bool playback)
{
    unsigned long ret_val = 0;
    if (is_usb_offload_enabled && is_usb_burst_mode_enabled)
        ret_val = usb_find_service_interval(min, playback);

    return ret_val;
}

int audio_extn_usb_altset_for_service_interval(bool is_playback,
                                               unsigned long service_interval,
                                               uint32_t *bit_width,
                                               uint32_t *sample_rate,
                                               uint32_t *channel_count)
{
    int ret_val = 0;
    if (is_usb_offload_enabled && is_usb_burst_mode_enabled)
        ret_val = usb_altset_for_service_interval(is_playback, service_interval,
                                         bit_width, sample_rate, channel_count);

    return ret_val;
}

int audio_extn_usb_set_service_interval(bool playback,
                                        unsigned long service_interval,
                                        bool *reconfig)
{
    int ret_val = 0;
    if (is_usb_offload_enabled && is_usb_burst_mode_enabled)
        ret_val = usb_set_service_interval(playback, service_interval, reconfig);

    return ret_val;
}

int audio_extn_usb_get_service_interval(bool playback,
                                        unsigned long *service_interval)
{
    int ret_val = 0;
    if (is_usb_offload_enabled && is_usb_burst_mode_enabled)
        ret_val = usb_get_service_interval(playback, service_interval);

    return ret_val;
}

int audio_extn_usb_check_and_set_svc_int(struct audio_usecase *uc_info,
                                         bool starting_output_stream)
{
    int ret_val = 0;
    if (is_usb_offload_enabled && is_usb_burst_mode_enabled)
        ret_val = usb_check_and_set_svc_int(uc_info, starting_output_stream);

    return ret_val;
}

bool audio_extn_usb_is_reconfig_req()
{
    bool ret_val = 0;
    if (is_usb_offload_enabled && is_usb_burst_mode_enabled)
        ret_val = usb_is_reconfig_req();

    return ret_val;
}

void audio_extn_usb_set_reconfig(bool is_required)
{
    if (is_usb_offload_enabled && is_usb_burst_mode_enabled)
        usb_set_reconfig(is_required);
}

//END: USB_OFFLOAD ===========================================================

//START: SPEAKER_PROTECTION ==========================================================
#ifdef __LP64__
#define SPKR_PROT_LIB_PATH         "/vendor/lib64/libspkrprot.so"
#define CIRRUS_SPKR_PROT_LIB_PATH  "/vendor/lib64/libcirrusspkrprot.so"
#else
#define SPKR_PROT_LIB_PATH         "/vendor/lib/libspkrprot.so"
#define CIRRUS_SPKR_PROT_LIB_PATH  "/vendor/lib/libcirrusspkrprot.so"
#endif

static void *spkr_prot_lib_handle = NULL;

typedef void (*spkr_prot_init_t)(void *, spkr_prot_init_config_t);
static spkr_prot_init_t spkr_prot_init;

typedef int (*spkr_prot_deinit_t)();
static spkr_prot_deinit_t spkr_prot_deinit;

typedef int (*spkr_prot_start_processing_t)(snd_device_t);
static spkr_prot_start_processing_t spkr_prot_start_processing;

typedef void (*spkr_prot_stop_processing_t)(snd_device_t);
static spkr_prot_stop_processing_t spkr_prot_stop_processing;

typedef bool (*spkr_prot_is_enabled_t)();
static spkr_prot_is_enabled_t spkr_prot_is_enabled;

typedef void (*spkr_prot_calib_cancel_t)(void *);
static spkr_prot_calib_cancel_t spkr_prot_calib_cancel;

typedef void (*spkr_prot_set_parameters_t)(struct str_parms *,
                                           char *, int);
static spkr_prot_set_parameters_t spkr_prot_set_parameters;

typedef int (*fbsp_set_parameters_t)(struct str_parms *);
static fbsp_set_parameters_t fbsp_set_parameters;

typedef int (*fbsp_get_parameters_t)(struct str_parms *,
                                   struct str_parms *);
static fbsp_get_parameters_t fbsp_get_parameters;

typedef int (*get_spkr_prot_snd_device_t)(snd_device_t);
static get_spkr_prot_snd_device_t get_spkr_prot_snd_device;

void spkr_prot_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s, vendor_enhanced_info 0x%x", __func__,
            is_feature_enabled ? "Enabled" : "NOT Enabled", vendor_enhanced_info);
    if (is_feature_enabled) {
        // dlopen lib
        if ((vendor_enhanced_info & 0x3) == 0x0) // Pure AOSP
            spkr_prot_lib_handle = dlopen(CIRRUS_SPKR_PROT_LIB_PATH, RTLD_NOW);
        else
            spkr_prot_lib_handle = dlopen(SPKR_PROT_LIB_PATH, RTLD_NOW);

        if (spkr_prot_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }

        // map each function
        // if mandatoy functions are not found, disble feature
        // Mandatory functions
        if (((spkr_prot_init =
             (spkr_prot_init_t)dlsym(spkr_prot_lib_handle, "spkr_prot_init")) == NULL) ||
            ((spkr_prot_deinit =
             (spkr_prot_deinit_t)dlsym(spkr_prot_lib_handle, "spkr_prot_deinit")) == NULL) ||
            ((spkr_prot_start_processing =
             (spkr_prot_start_processing_t)dlsym(spkr_prot_lib_handle, "spkr_prot_start_processing")) == NULL) ||
            ((spkr_prot_stop_processing =
             (spkr_prot_stop_processing_t)dlsym(spkr_prot_lib_handle, "spkr_prot_stop_processing")) == NULL) ||
            ((spkr_prot_is_enabled =
             (spkr_prot_is_enabled_t)dlsym(spkr_prot_lib_handle, "spkr_prot_is_enabled")) == NULL) ||
            ((spkr_prot_calib_cancel =
             (spkr_prot_calib_cancel_t)dlsym(spkr_prot_lib_handle, "spkr_prot_calib_cancel")) == NULL) ||
            ((get_spkr_prot_snd_device =
             (get_spkr_prot_snd_device_t)dlsym(spkr_prot_lib_handle, "get_spkr_prot_snd_device")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        // optional functions, can be NULL
        spkr_prot_set_parameters = NULL;
        fbsp_set_parameters = NULL;
        fbsp_get_parameters = NULL;
        if ((spkr_prot_set_parameters =
             (spkr_prot_set_parameters_t)dlsym(spkr_prot_lib_handle, "spkr_prot_set_parameters")) == NULL) {
            ALOGW("%s: dlsym failed for spkr_prot_set_parameters", __func__);
        }

        if ((fbsp_set_parameters =
             (fbsp_set_parameters_t)dlsym(spkr_prot_lib_handle, "fbsp_set_parameters")) == NULL) {
            ALOGW("%s: dlsym failed for fbsp_set_parameters", __func__);
        }

        if ((fbsp_get_parameters =
             (fbsp_get_parameters_t)dlsym(spkr_prot_lib_handle, "fbsp_get_parameters")) == NULL) {
            ALOGW("%s: dlsym failed for fbsp_get_parameters", __func__);
        }

        ALOGD("%s:: ---- Feature SPKR_PROT is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (spkr_prot_lib_handle) {
        dlclose(spkr_prot_lib_handle);
        spkr_prot_lib_handle = NULL;
    }

    spkr_prot_init = NULL;
    spkr_prot_deinit = NULL;
    spkr_prot_start_processing = NULL;
    spkr_prot_stop_processing = NULL;
    spkr_prot_is_enabled = NULL;
    spkr_prot_calib_cancel = NULL;
    spkr_prot_set_parameters = NULL;
    fbsp_set_parameters = NULL;
    fbsp_get_parameters = NULL;
    get_spkr_prot_snd_device = NULL;

    ALOGW(":: %s: ---- Feature SPKR_PROT is disabled ----", __func__);
    return;
}

void audio_extn_spkr_prot_init(void *adev) {
    if (spkr_prot_init != NULL) {
        // init function pointers
        spkr_prot_init_config_t spkr_prot_config_val;
        spkr_prot_config_val.fp_read_line_from_file = read_line_from_file;
        spkr_prot_config_val.fp_get_usecase_from_list = get_usecase_from_list;
        spkr_prot_config_val.fp_disable_snd_device  = disable_snd_device;
        spkr_prot_config_val.fp_enable_snd_device = enable_snd_device;
        spkr_prot_config_val.fp_disable_audio_route = disable_audio_route;
        spkr_prot_config_val.fp_enable_audio_route = enable_audio_route;
        spkr_prot_config_val.fp_platform_set_snd_device_backend = platform_set_snd_device_backend;
        spkr_prot_config_val.fp_platform_get_snd_device_name_extn = platform_get_snd_device_name_extn;
        spkr_prot_config_val.fp_platform_get_default_app_type_v2 = platform_get_default_app_type_v2;
        spkr_prot_config_val.fp_platform_send_audio_calibration = platform_send_audio_calibration;
        spkr_prot_config_val.fp_platform_get_pcm_device_id = platform_get_pcm_device_id;
        spkr_prot_config_val.fp_platform_get_snd_device_name = platform_get_snd_device_name;
        spkr_prot_config_val.fp_platform_spkr_prot_is_wsa_analog_mode = platform_spkr_prot_is_wsa_analog_mode;
        spkr_prot_config_val.fp_platform_get_vi_feedback_snd_device = platform_get_vi_feedback_snd_device;
        spkr_prot_config_val.fp_platform_get_spkr_prot_snd_device = platform_get_spkr_prot_snd_device;
        spkr_prot_config_val.fp_platform_check_and_set_codec_backend_cfg = platform_check_and_set_codec_backend_cfg;
        spkr_prot_config_val.fp_audio_extn_get_snd_card_split = audio_extn_get_snd_card_split;
        spkr_prot_config_val.fp_audio_extn_is_vbat_enabled = audio_extn_is_vbat_enabled;

        spkr_prot_init(adev, spkr_prot_config_val);
    }

    return;
}

int audio_extn_spkr_prot_deinit() {
    int ret_val = 0;

    if (spkr_prot_deinit != NULL)
        ret_val = spkr_prot_deinit();

    return ret_val;
}

int audio_extn_spkr_prot_start_processing(snd_device_t snd_device) {
    int ret_val = 0;

    if (spkr_prot_start_processing != NULL)
        ret_val = spkr_prot_start_processing(snd_device);

    return ret_val;
}

void audio_extn_spkr_prot_stop_processing(snd_device_t snd_device)
{
    if (spkr_prot_stop_processing != NULL)
        spkr_prot_stop_processing(snd_device);

    return;
}

bool audio_extn_spkr_prot_is_enabled()
{
    bool ret_val = false;

    if (spkr_prot_is_enabled != NULL)
        ret_val = spkr_prot_is_enabled();

    return ret_val;
}

void audio_extn_spkr_prot_calib_cancel(void *adev)
{
    if (spkr_prot_calib_cancel != NULL)
        spkr_prot_calib_cancel(adev);

    return;
}

void audio_extn_spkr_prot_set_parameters(struct str_parms *parms,
                                         char *value, int len)
{
    if (spkr_prot_set_parameters != NULL)
        spkr_prot_set_parameters(parms, value, len);

    return;
}

int audio_extn_fbsp_set_parameters(struct str_parms *parms)
{
    int ret_val = 0;

    if (fbsp_set_parameters != NULL)
        ret_val = fbsp_set_parameters(parms);

    return ret_val;
}

int audio_extn_fbsp_get_parameters(struct str_parms *query,
                                   struct str_parms *reply)
{
    int ret_val = 0;

    if (fbsp_get_parameters != NULL)
        ret_val = fbsp_get_parameters(query, reply);

    return ret_val;
}

int audio_extn_get_spkr_prot_snd_device(snd_device_t snd_device)
{
    int ret_val = snd_device;

    if (get_spkr_prot_snd_device != NULL)
        ret_val = get_spkr_prot_snd_device(snd_device);

    return ret_val;
}

//END: SPEAKER_PROTECTION ==========================================================

//START: EXTERNAL_QDSP ================================================================
#ifdef __LP64__
#define EXTERNAL_QDSP_LIB_PATH  "/vendor/lib64/libextqdsp.so"
#else
#define EXTERNAL_QDSP_LIB_PATH  "/vendor/lib/libextqdsp.so"
#endif

static void *external_qdsp_lib_handle = NULL;

typedef void (*external_qdsp_init_t)(void *);
static external_qdsp_init_t external_qdsp_init;

typedef void (*external_qdsp_deinit_t)(void);
static external_qdsp_deinit_t external_qdsp_deinit;

typedef bool (*external_qdsp_set_state_t)(struct audio_device *,
                                        int , float , bool);
static external_qdsp_set_state_t external_qdsp_set_state;

typedef void (*external_qdsp_set_device_t)(struct audio_usecase *);
static external_qdsp_set_device_t external_qdsp_set_device;

typedef void (*external_qdsp_set_parameter_t)(struct audio_device *,
                                              struct str_parms *);
static external_qdsp_set_parameter_t external_qdsp_set_parameter;

typedef bool (*external_qdsp_supported_usb_t)(void);
static external_qdsp_supported_usb_t external_qdsp_supported_usb;

void external_qdsp_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    if (is_feature_enabled) {
        //dlopen lib
        external_qdsp_lib_handle = dlopen(EXTERNAL_QDSP_LIB_PATH, RTLD_NOW);
        if (external_qdsp_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if (((external_qdsp_init =
             (external_qdsp_init_t)dlsym(external_qdsp_lib_handle, "ma_init")) == NULL) ||
            ((external_qdsp_deinit =
             (external_qdsp_deinit_t)dlsym(external_qdsp_lib_handle, "ma_deinit")) == NULL) ||
            ((external_qdsp_set_state =
             (external_qdsp_set_state_t)dlsym(external_qdsp_lib_handle, "set_state")) == NULL) ||
            ((external_qdsp_set_device =
             (external_qdsp_set_device_t)dlsym(external_qdsp_lib_handle, "set_device")) == NULL) ||
            ((external_qdsp_set_parameter =
             (external_qdsp_set_parameter_t)dlsym(external_qdsp_lib_handle, "set_parameters")) == NULL) ||
            ((external_qdsp_supported_usb =
             (external_qdsp_supported_usb_t)dlsym(external_qdsp_lib_handle, "supported_usb")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature EXTERNAL_QDSP is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (external_qdsp_lib_handle) {
        dlclose(external_qdsp_lib_handle);
        external_qdsp_lib_handle = NULL;
    }

    external_qdsp_init = NULL;
    external_qdsp_deinit = NULL;
    external_qdsp_set_state = NULL;
    external_qdsp_set_device = NULL;
    external_qdsp_set_parameter = NULL;
    external_qdsp_supported_usb = NULL;

    ALOGW(":: %s: ---- Feature EXTERNAL_QDSP is disabled ----", __func__);
    return;
}

void audio_extn_qdsp_init(void *platform) {
    if (external_qdsp_init != NULL)
        external_qdsp_init(platform);

    return;
}

void audio_extn_qdsp_deinit() {
    if (external_qdsp_deinit != NULL)
        external_qdsp_deinit();

    return;
}

bool audio_extn_qdsp_set_state(struct audio_device *adev, int stream_type,
                             float vol, bool active) {
    bool ret_val = false;

    if (external_qdsp_set_state != NULL)
        ret_val = external_qdsp_set_state(adev, stream_type, vol, active);

    return ret_val;
}

void audio_extn_qdsp_set_device(struct audio_usecase *usecase) {
    if (external_qdsp_set_device != NULL)
        external_qdsp_set_device(usecase);

    return;
}

void audio_extn_qdsp_set_parameters(struct audio_device *adev,
                                  struct str_parms *parms) {
    if (external_qdsp_set_parameter != NULL)
        external_qdsp_set_parameter(adev, parms);

    return;
}

bool audio_extn_qdsp_supported_usb() {
    bool ret_val = false;

    if (external_qdsp_supported_usb != NULL)
        ret_val = external_qdsp_supported_usb();

    return ret_val;
}


//END: EXTERNAL_QDSP ================================================================

//START: EXTERNAL_SPEAKER ================================================================
#ifdef __LP64__
#define EXTERNAL_SPKR_LIB_PATH  "/vendor/lib64/libextspkr.so"
#else
#define EXTERNAL_SPKR_LIB_PATH  "/vendor/lib/libextspkr.so"
#endif

static void *external_speaker_lib_handle = NULL;

typedef void* (*external_speaker_init_t)(struct audio_device *);
static external_speaker_init_t external_speaker_init;

typedef void (*external_speaker_deinit_t)(void *);
static external_speaker_deinit_t external_speaker_deinit;

typedef void (*external_speaker_update_t)(void *);
static external_speaker_update_t external_speaker_update;

typedef void (*external_speaker_set_mode_t)(void *, audio_mode_t);
static external_speaker_set_mode_t external_speaker_set_mode;

typedef void (*external_speaker_set_voice_vol_t)(void *, float);
static external_speaker_set_voice_vol_t external_speaker_set_voice_vol;


void external_speaker_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    if (is_feature_enabled) {
        //dlopen lib
        external_speaker_lib_handle = dlopen(EXTERNAL_SPKR_LIB_PATH, RTLD_NOW);
        if (external_speaker_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if (((external_speaker_init =
             (external_speaker_init_t)dlsym(external_speaker_lib_handle, "extspk_init")) == NULL) ||
            ((external_speaker_deinit =
             (external_speaker_deinit_t)dlsym(external_speaker_lib_handle, "extspk_deinit")) == NULL) ||
            ((external_speaker_update =
             (external_speaker_update_t)dlsym(external_speaker_lib_handle, "extspk_update")) == NULL) ||
            ((external_speaker_set_mode =
             (external_speaker_set_mode_t)dlsym(external_speaker_lib_handle, "extspk_set_mode")) == NULL) ||
            ((external_speaker_set_voice_vol =
             (external_speaker_set_voice_vol_t)dlsym(external_speaker_lib_handle, "extspk_set_voice_vol")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature EXTERNAL_SPKR is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (external_speaker_lib_handle) {
        dlclose(external_speaker_lib_handle);
        external_speaker_lib_handle = NULL;
    }

    external_speaker_init = NULL;
    external_speaker_deinit = NULL;
    external_speaker_update = NULL;
    external_speaker_set_mode = NULL;
    external_speaker_set_voice_vol = NULL;

    ALOGW(":: %s: ---- Feature EXTERNAL_SPKR is disabled ----", __func__);
    return;
}

void *audio_extn_extspk_init(struct audio_device *adev) {
    void* ret_val = NULL;

    if (external_speaker_init != NULL)
        ret_val = external_speaker_init(adev);

    return ret_val;
}

void audio_extn_extspk_deinit(void *extn) {
    if (external_speaker_deinit != NULL)
        external_speaker_deinit(extn);

    return;
}

void audio_extn_extspk_update(void* extn) {
    if (external_speaker_update != NULL)
        external_speaker_update(extn);

    return;
}

void audio_extn_extspk_set_mode(void* extn, audio_mode_t mode) {
    if (external_speaker_set_mode != NULL)
        external_speaker_set_mode(extn, mode);

    return;
}

void audio_extn_extspk_set_voice_vol(void* extn, float vol) {
    if (external_speaker_set_voice_vol != NULL)
        external_speaker_set_voice_vol(extn, vol);

    return;
}

//END: EXTERNAL_SPEAKER ================================================================


//START: EXTERNAL_SPEAKER_TFA ================================================================
#ifdef __LP64__
#define EXTERNAL_SPKR_TFA_LIB_PATH  "/vendor/lib64/libextspkr_tfa.so"
#else
#define EXTERNAL_SPKR_TFA_LIB_PATH  "/vendor/lib/libextspkr_tfa.so"
#endif

static void *external_speaker_tfa_lib_handle = NULL;

typedef int (*external_speaker_tfa_enable_t)(void);
static external_speaker_tfa_enable_t external_speaker_tfa_enable;

typedef void (*external_speaker_tfa_disable_t)(snd_device_t);
static external_speaker_tfa_disable_t external_speaker_tfa_disable;

typedef void (*external_speaker_tfa_set_mode_t)();
static external_speaker_tfa_set_mode_t external_speaker_tfa_set_mode;

typedef void (*external_speaker_tfa_set_mode_bt_t)();
static external_speaker_tfa_set_mode_bt_t external_speaker_tfa_set_mode_bt;

typedef void (*external_speaker_tfa_update_t)(void);
static external_speaker_tfa_update_t external_speaker_tfa_update;

typedef void (*external_speaker_tfa_set_voice_vol_t)(float);
static external_speaker_tfa_set_voice_vol_t external_speaker_tfa_set_voice_vol;

typedef int (*external_speaker_tfa_init_t)(struct audio_device *);
static external_speaker_tfa_init_t external_speaker_tfa_init;

typedef void (*external_speaker_tfa_deinit_t)(void);
static external_speaker_tfa_deinit_t external_speaker_tfa_deinit;

typedef bool (*external_speaker_tfa_is_supported_t)(void);
static external_speaker_tfa_is_supported_t external_speaker_tfa_is_supported;

void external_speaker_tfa_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    if (is_feature_enabled) {
        //dlopen lib
        external_speaker_tfa_lib_handle = dlopen(EXTERNAL_SPKR_TFA_LIB_PATH, RTLD_NOW);
        if (external_speaker_tfa_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if (((external_speaker_tfa_enable =
             (external_speaker_tfa_enable_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_enable_speaker")) == NULL) ||
            ((external_speaker_tfa_disable =
             (external_speaker_tfa_disable_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_disable_speaker")) == NULL) ||
            ((external_speaker_tfa_set_mode =
             (external_speaker_tfa_set_mode_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_set_mode")) == NULL) ||
            ((external_speaker_tfa_set_mode_bt =
             (external_speaker_tfa_set_mode_bt_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_set_mode_bt")) == NULL) ||
            ((external_speaker_tfa_update =
             (external_speaker_tfa_update_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_update")) == NULL) ||
            ((external_speaker_tfa_set_voice_vol =
             (external_speaker_tfa_set_voice_vol_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_set_voice_vol")) == NULL) ||
            ((external_speaker_tfa_init =
             (external_speaker_tfa_init_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_init")) == NULL) ||
            ((external_speaker_tfa_deinit =
             (external_speaker_tfa_deinit_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_deinit")) == NULL) ||
            ((external_speaker_tfa_is_supported =
             (external_speaker_tfa_is_supported_t)dlsym(external_speaker_tfa_lib_handle, "tfa_98xx_is_supported")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature EXTERNAL_SPKR is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (external_speaker_tfa_lib_handle) {
        dlclose(external_speaker_tfa_lib_handle);
        external_speaker_tfa_lib_handle = NULL;
    }

    external_speaker_tfa_enable = NULL;
    external_speaker_tfa_disable = NULL;
    external_speaker_tfa_set_mode = NULL;
    external_speaker_tfa_update = NULL;
    external_speaker_tfa_set_voice_vol = NULL;
    external_speaker_tfa_init = NULL;
    external_speaker_tfa_deinit = NULL;
    external_speaker_tfa_is_supported = NULL;

    ALOGW(":: %s: ---- Feature EXTERNAL_SPKR_TFA is disabled ----", __func__);
    return;
}

int audio_extn_external_speaker_tfa_enable_speaker() {
    int ret_val = 0;

    if (external_speaker_tfa_enable != NULL)
        ret_val = external_speaker_tfa_enable();

    return ret_val;
}

void audio_extn_external_speaker_tfa_disable_speaker(snd_device_t snd_device) {
    if (external_speaker_tfa_disable != NULL)
        external_speaker_tfa_disable(snd_device);

    return;
}

void audio_extn_external_speaker_tfa_set_mode(bool is_mode_bt) {
    if (is_mode_bt && (external_speaker_tfa_set_mode_bt != NULL))
        external_speaker_tfa_set_mode_bt();
    else if (external_speaker_tfa_set_mode != NULL)
        external_speaker_tfa_set_mode();

    return;
}

void audio_extn_external_speaker_tfa_update() {
    if (external_speaker_tfa_update != NULL)
        external_speaker_tfa_update();

    return;
}

void audio_extn_external_speaker_tfa_set_voice_vol(float vol) {
    if (external_speaker_tfa_set_voice_vol != NULL)
        external_speaker_tfa_set_voice_vol(vol);

    return;
}

int  audio_extn_external_tfa_speaker_init(struct audio_device *adev) {
    int ret_val = 0;

    if (external_speaker_tfa_init != NULL)
        ret_val = external_speaker_tfa_init(adev);

    return ret_val;
}

void audio_extn_external_speaker_tfa_deinit() {
    if (external_speaker_tfa_deinit != NULL)
        external_speaker_tfa_deinit();

    return;
}

bool audio_extn_external_speaker_tfa_is_supported() {
    bool ret_val = false;

    if (external_speaker_tfa_is_supported != NULL)
        ret_val = external_speaker_tfa_is_supported;

    return ret_val;
}


//END: EXTERNAL_SPEAKER_TFA ================================================================


//START: HWDEP_CAL ================================================================
#ifdef __LP64__
#define HWDEP_CAL_LIB_PATH  "/vendor/lib64/libhwdepcal.so"
#else
#define HWDEP_CAL_LIB_PATH  "/vendor/lib/libhwdepcal.so"
#endif

static void *hwdep_cal_lib_handle = NULL;

typedef void (*hwdep_cal_send_t)(int, void*);
static hwdep_cal_send_t hwdep_cal_send;


//If feature is enabled, please copy hwdep_cal.c in the audio_extn dir
//Current lib doesn't have any src files
void hwdep_cal_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    if (is_feature_enabled) {
        //dlopen lib
        hwdep_cal_lib_handle = dlopen(HWDEP_CAL_LIB_PATH, RTLD_NOW);
        if (hwdep_cal_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if ((hwdep_cal_send =
            (hwdep_cal_send_t)dlsym(hwdep_cal_lib_handle, "hwdep_cal_send")) == NULL) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature HWDEP_CAL is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (hwdep_cal_lib_handle) {
        dlclose(hwdep_cal_lib_handle);
        hwdep_cal_lib_handle = NULL;
    }

    hwdep_cal_send = NULL;

    ALOGW(":: %s: ---- Feature HWDEP_CAL is disabled ----", __func__);
    return;
}

void audio_extn_hwdep_cal_send(int snd_card, void *acdb_handle)
{
    if (hwdep_cal_send != NULL)
        hwdep_cal_send(snd_card, acdb_handle);

    return;
}


//END: HWDEP_CAL =====================================================================


//START: DSM_FEEDBACK ================================================================
#ifdef __LP64__
#define DSM_FEEDBACK_LIB_PATH  "/vendor/lib64/libdsmfeedback.so"
#else
#define DSM_FEEDBACK_LIB_PATH  "/vendor/lib/libdsmfeedback.so"
#endif

static void *dsm_feedback_lib_handle = NULL;

typedef void (*dsm_feedback_enable_t)(struct audio_device*, snd_device_t, bool);
static dsm_feedback_enable_t dsm_feedback_enable;

void dsm_feedback_feature_init (bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    if (is_feature_enabled) {
        //dlopen lib
        dsm_feedback_lib_handle = dlopen(DSM_FEEDBACK_LIB_PATH, RTLD_NOW);
        if (dsm_feedback_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if ((dsm_feedback_enable =
            (dsm_feedback_enable_t)dlsym(dsm_feedback_lib_handle, "dsm_feedback_enable")) == NULL) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature DSM_FEEDBACK is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (dsm_feedback_lib_handle) {
        dlclose(dsm_feedback_lib_handle);
        dsm_feedback_lib_handle = NULL;
    }

    dsm_feedback_enable = NULL;

    ALOGW(":: %s: ---- Feature DSM_FEEDBACK is disabled ----", __func__);
    return;
}

void audio_extn_dsm_feedback_enable(struct audio_device *adev, snd_device_t snd_device, bool benable)
{
    if (dsm_feedback_enable != NULL)
        dsm_feedback_enable(adev, snd_device, benable);

    return;
}

//END:   DSM_FEEDBACK ================================================================

//START: SND_MONITOR_FEATURE ================================================================
#ifdef __LP64__
#define SND_MONITOR_PATH  "/vendor/lib64/libsndmonitor.so"
#else
#define SND_MONITOR_PATH  "/vendor/lib/libsndmonitor.so"
#endif
static void *snd_mnt_lib_handle = NULL;

typedef int (*snd_mon_init_t)();
static snd_mon_init_t snd_mon_init;
typedef int (*snd_mon_deinit_t)();
static snd_mon_deinit_t snd_mon_deinit;
typedef int (*snd_mon_register_listener_t)(void *, snd_mon_cb);
static snd_mon_register_listener_t snd_mon_register_listener;
typedef int (*snd_mon_unregister_listener_t)(void *);
static snd_mon_unregister_listener_t snd_mon_unregister_listener;

void snd_mon_feature_init (bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    if (is_feature_enabled) {
        //dlopen lib
        snd_mnt_lib_handle = dlopen(SND_MONITOR_PATH, RTLD_NOW);
        if (snd_mnt_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if (((snd_mon_init = (snd_mon_init_t)dlsym(snd_mnt_lib_handle,"snd_mon_init")) == NULL) ||
            ((snd_mon_deinit = (snd_mon_deinit_t)dlsym(snd_mnt_lib_handle,"snd_mon_deinit")) == NULL) ||
            ((snd_mon_register_listener = (snd_mon_register_listener_t)dlsym(snd_mnt_lib_handle,"snd_mon_register_listener")) == NULL) ||
            ((snd_mon_unregister_listener = (snd_mon_unregister_listener_t)dlsym(snd_mnt_lib_handle,"snd_mon_unregister_listener")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }
        ALOGD("%s:: ---- Feature SND_MONITOR is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (snd_mnt_lib_handle) {
        dlclose(snd_mnt_lib_handle);
        snd_mnt_lib_handle = NULL;
    }

    snd_mon_init                = NULL;
    snd_mon_deinit              = NULL;
    snd_mon_register_listener   = NULL;
    snd_mon_unregister_listener = NULL;
    ALOGW(":: %s: ---- Feature SND_MONITOR is disabled ----", __func__);
    return;
}

int audio_extn_snd_mon_init()
{
    int ret = 0;
    if (snd_mon_init != NULL)
        ret = snd_mon_init();

    return ret;
}

int audio_extn_snd_mon_deinit()
{
    int ret = 0;
    if (snd_mon_deinit != NULL)
        ret = snd_mon_deinit();

    return ret;
}

int audio_extn_snd_mon_register_listener(void *stream, snd_mon_cb cb)
{
    int ret = 0;
    if (snd_mon_register_listener != NULL)
        ret = snd_mon_register_listener(stream, cb);

    return ret;
}

int audio_extn_snd_mon_unregister_listener(void *stream)
{
    int ret = 0;
    if (snd_mon_unregister_listener != NULL)
        ret = snd_mon_unregister_listener(stream);

    return ret;
}

//END: SND_MONITOR_FEATURE ================================================================

//START: SOURCE_TRACKING_FEATURE ==============================================
int get_soundfocus_data(const struct audio_device *adev,
                                   struct sound_focus_param *payload);
int get_sourcetrack_data(const struct audio_device *adev,
                                    struct source_tracking_param *payload);
int set_soundfocus_data(struct audio_device *adev,
                                   struct sound_focus_param *payload);
void source_track_set_parameters(struct audio_device *adev,
                                            struct str_parms *parms);
void source_track_get_parameters(const struct audio_device *adev,
                                            struct str_parms *query,
                                            struct str_parms *reply);

static bool is_src_trkn_enabled = false;

void src_trkn_feature_init(bool is_feature_enabled) {
    is_src_trkn_enabled = is_feature_enabled;

    if (is_src_trkn_enabled) {
        ALOGD("%s:: ---- Feature SOURCE_TRACKING is Enabled ----", __func__);
        return;
    }

    ALOGW(":: %s: ---- Feature SOURCE_TRACKING is disabled ----", __func__);
}

int audio_extn_get_soundfocus_data(const struct audio_device *adev,
                                   struct sound_focus_param *payload) {
    int ret = 0;

    if (is_src_trkn_enabled)
        ret = get_soundfocus_data(adev, payload);

    return ret;
}

int audio_extn_get_sourcetrack_data(const struct audio_device *adev,
                                    struct source_tracking_param *payload) {
    int ret = 0;

    if (is_src_trkn_enabled)
        ret = get_sourcetrack_data(adev, payload);

    return ret;
}

int audio_extn_set_soundfocus_data(struct audio_device *adev,
                                   struct sound_focus_param *payload) {
    int ret = 0;

    if (is_src_trkn_enabled)
        ret = set_soundfocus_data(adev, payload);

    return ret;
}

void audio_extn_source_track_set_parameters(struct audio_device *adev,
                                            struct str_parms *parms) {
    if (is_src_trkn_enabled)
        source_track_set_parameters(adev, parms);
}

void audio_extn_source_track_get_parameters(const struct audio_device *adev,
                                            struct str_parms *query,
                                            struct str_parms *reply) {
    if (is_src_trkn_enabled)
        source_track_get_parameters(adev, query, reply);
}
//END: SOURCE_TRACKING_FEATURE ================================================

//START: SSREC_FEATURE ==========================================================
#ifdef __LP64__
#define SSREC_LIB_PATH  "/vendor/lib64/libssrec.so"
#else
#define SSREC_LIB_PATH  "/vendor/lib/libssrec.so"
#endif

static void *ssrec_lib_handle = NULL;

typedef bool (*ssr_check_usecase_t)(struct stream_in *);
static ssr_check_usecase_t ssr_check_usecase;

typedef int (*ssr_set_usecase_t)(struct stream_in *,
                                 struct audio_config *,
                                 bool *);
static ssr_set_usecase_t ssr_set_usecase;

typedef int32_t (*ssr_init_t)(struct stream_in *,
                              int num_out_chan);
static ssr_init_t ssr_init;

typedef int32_t (*ssr_deinit_t)();
static ssr_deinit_t ssr_deinit;

typedef void (*ssr_update_enabled_t)();
static ssr_update_enabled_t ssr_update_enabled;

typedef bool (*ssr_get_enabled_t)();
static ssr_get_enabled_t ssr_get_enabled;

typedef int32_t (*ssr_read_t)(struct audio_stream_in *,
                              void *,
                              size_t);
static ssr_read_t ssr_read;

typedef void (*ssr_set_parameters_t)(struct audio_device *,
                                     struct str_parms *);
static ssr_set_parameters_t ssr_set_parameters;

typedef void (*ssr_get_parameters_t)(const struct audio_device *,
                                     struct str_parms *,
                                     struct str_parms *);
static ssr_get_parameters_t ssr_get_parameters;

typedef struct stream_in *(*ssr_get_stream_t)();
static ssr_get_stream_t ssr_get_stream;

void ssrec_feature_init(bool is_feature_enabled) {

    if (is_feature_enabled) {
        ssrec_lib_handle = dlopen(SSREC_LIB_PATH, RTLD_NOW);
        if (ssrec_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }

        if (((ssr_check_usecase = (ssr_check_usecase_t)dlsym(ssrec_lib_handle, "ssr_check_usecase")) == NULL) ||
            ((ssr_set_usecase = (ssr_set_usecase_t)dlsym(ssrec_lib_handle, "ssr_set_usecase")) == NULL) ||
            ((ssr_init = (ssr_init_t)dlsym(ssrec_lib_handle, "ssr_init")) == NULL) ||
            ((ssr_deinit = (ssr_deinit_t)dlsym(ssrec_lib_handle, "ssr_deinit")) == NULL) ||
            ((ssr_update_enabled = (ssr_update_enabled_t)dlsym(ssrec_lib_handle, "ssr_update_enabled")) == NULL) ||
            ((ssr_get_enabled = (ssr_get_enabled_t)dlsym(ssrec_lib_handle, "ssr_get_enabled")) == NULL) ||
            ((ssr_read = (ssr_read_t)dlsym(ssrec_lib_handle, "ssr_read")) == NULL) ||
            ((ssr_set_parameters = (ssr_set_parameters_t)dlsym(ssrec_lib_handle, "ssr_set_parameters")) == NULL) ||
            ((ssr_get_parameters = (ssr_get_parameters_t)dlsym(ssrec_lib_handle, "ssr_get_parameters")) == NULL) ||
            ((ssr_get_stream = (ssr_get_stream_t)dlsym(ssrec_lib_handle, "ssr_get_stream")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature SSREC is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if(ssrec_lib_handle) {
        dlclose(ssrec_lib_handle);
        ssrec_lib_handle = NULL;
    }

    ssr_check_usecase = NULL;
    ssr_set_usecase = NULL;
    ssr_init = NULL;
    ssr_deinit = NULL;
    ssr_update_enabled = NULL;
    ssr_get_enabled = NULL;
    ssr_read = NULL;
    ssr_set_parameters = NULL;
    ssr_get_parameters = NULL;
    ssr_get_stream = NULL;

    ALOGW(":: %s: ---- Feature SSREC is disabled ----", __func__);
}

bool audio_extn_ssr_check_usecase(struct stream_in *in) {
    bool ret = false;

    if (ssrec_lib_handle != NULL)
        ret = ssr_check_usecase(in);

    return ret;
}

int audio_extn_ssr_set_usecase(struct stream_in *in,
                               struct audio_config *config,
                               bool *channel_mask_updated) {
    int ret = 0;

    if (ssrec_lib_handle != NULL)
        ret = ssr_set_usecase(in, config, channel_mask_updated);

    return ret;
}

int32_t audio_extn_ssr_init(struct stream_in *in,
                            int num_out_chan) {
    int32_t ret = 0;

    if (ssrec_lib_handle != NULL)
        ret = ssr_init(in, num_out_chan);

    return ret;
}

int32_t audio_extn_ssr_deinit() {
    int32_t ret = 0;

    if (ssrec_lib_handle != NULL)
        ret = ssr_deinit();

    return ret;
}

void audio_extn_ssr_update_enabled() {

    if (ssrec_lib_handle)
        ssr_update_enabled();
}

bool audio_extn_ssr_get_enabled() {
    bool ret = false;

    if (ssrec_lib_handle)
        ret = ssr_get_enabled();

    return ret;
}

int32_t audio_extn_ssr_read(struct audio_stream_in *stream,
                            void *buffer,
                            size_t bytes) {
    int32_t ret = 0;

    if (ssrec_lib_handle)
        ret = ssr_read(stream, buffer, bytes);

    return ret;
}

void audio_extn_ssr_set_parameters(struct audio_device *adev,
                                   struct str_parms *parms) {

    if (ssrec_lib_handle)
        ssr_set_parameters(adev, parms);
}

void audio_extn_ssr_get_parameters(const struct audio_device *adev,
                                   struct str_parms *query,
                                   struct str_parms *reply) {

    if (ssrec_lib_handle)
        ssr_get_parameters(adev, query, reply);
}

struct stream_in *audio_extn_ssr_get_stream() {
    struct stream_in *ret = NULL;

    if (ssrec_lib_handle)
        ret = ssr_get_stream();

    return ret;
}
//END: SSREC_FEATURE ============================================================

//START: COMPRESS_CAPTURE_FEATURE ================================================================
#ifdef __LP64__
#define COMPRESS_CAPTURE_PATH  "/vendor/lib64/libcomprcapture.so"
#else
#define COMPRESS_CAPTURE_PATH  "/vendor/lib/libcomprcapture.so"
#endif
static void *compr_cap_lib_handle = NULL;

typedef void (*compr_cap_init_t)(struct stream_in*);
static compr_cap_init_t compr_cap_init;

typedef void (*compr_cap_deinit_t)();
static compr_cap_deinit_t compr_cap_deinit;

typedef bool (*compr_cap_enabled_t)();
static compr_cap_enabled_t compr_cap_enabled;

typedef bool (*compr_cap_format_supported_t)(audio_format_t);
static compr_cap_format_supported_t compr_cap_format_supported;

typedef bool (*compr_cap_usecase_supported_t)(audio_usecase_t);
static compr_cap_usecase_supported_t compr_cap_usecase_supported;

typedef size_t (*compr_cap_get_buffer_size_t)(audio_usecase_t);
static compr_cap_get_buffer_size_t compr_cap_get_buffer_size;

typedef int (*compr_cap_read_t)(struct stream_in*, void*, size_t);
static compr_cap_read_t compr_cap_read;

void compr_cap_feature_init(bool is_feature_enabled)
{
    if(is_feature_enabled) {
        //dlopen lib
        compr_cap_lib_handle = dlopen(COMPRESS_CAPTURE_PATH, RTLD_NOW);
        if (compr_cap_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if (((compr_cap_init = (compr_cap_init_t)dlsym(compr_cap_lib_handle,"compr_cap_init")) == NULL) ||
            ((compr_cap_deinit = (compr_cap_deinit_t)dlsym(compr_cap_lib_handle,"compr_cap_deinit")) == NULL) ||
            ((compr_cap_enabled = (compr_cap_enabled_t)dlsym(compr_cap_lib_handle,"compr_cap_enabled")) == NULL) ||
            ((compr_cap_format_supported = (compr_cap_format_supported_t)dlsym(compr_cap_lib_handle,"compr_cap_format_supported")) == NULL) ||
            ((compr_cap_usecase_supported = (compr_cap_usecase_supported_t)dlsym(compr_cap_lib_handle,"compr_cap_usecase_supported")) == NULL) ||
            ((compr_cap_get_buffer_size = (compr_cap_get_buffer_size_t)dlsym(compr_cap_lib_handle,"compr_cap_get_buffer_size")) == NULL) ||
            ((compr_cap_read = (compr_cap_read_t)dlsym(compr_cap_lib_handle,"compr_cap_read")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature COMPRESS_CAPTURE is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (compr_cap_lib_handle) {
        dlclose(compr_cap_lib_handle);
        compr_cap_lib_handle = NULL;
    }

    compr_cap_init                 = NULL;
    compr_cap_deinit               = NULL;
    compr_cap_enabled              = NULL;
    compr_cap_format_supported     = NULL;
    compr_cap_usecase_supported    = NULL;
    compr_cap_get_buffer_size      = NULL;
    compr_cap_read                 = NULL;

    ALOGW(":: %s: ---- Feature COMPRESS_CAPTURE is disabled ----", __func__);
    return;
}

void audio_extn_compr_cap_init(struct stream_in* instream)
{
    if (compr_cap_init != NULL)
        compr_cap_init(instream);

    return;
}

void audio_extn_compr_cap_deinit()
{
    if(compr_cap_deinit)
        compr_cap_deinit();

    return;
}

bool audio_extn_compr_cap_enabled()
{
    bool ret_val = false;

    if (compr_cap_enabled)
        ret_val = compr_cap_enabled();

    return ret_val;
}

bool audio_extn_compr_cap_format_supported(audio_format_t format)
{
    bool ret_val = false;

    if (compr_cap_format_supported != NULL)
        ret_val =  compr_cap_format_supported(format);

    return ret_val;
}

bool audio_extn_compr_cap_usecase_supported(audio_usecase_t usecase)
{
    bool ret_val = false;

    if (compr_cap_usecase_supported != NULL)
        ret_val =  compr_cap_usecase_supported(usecase);

    return ret_val;
}

size_t audio_extn_compr_cap_get_buffer_size(audio_format_t format)
{
    size_t ret_val = 0;

    if (compr_cap_get_buffer_size != NULL)
        ret_val =  compr_cap_get_buffer_size(format);

    return ret_val;
}

size_t audio_extn_compr_cap_read(struct stream_in *in,
                                        void *buffer, size_t bytes)
{
    size_t ret_val = 0;

    if (compr_cap_read != NULL)
        ret_val =  compr_cap_read(in, buffer, bytes);

    return ret_val;
}


void audio_extn_init(struct audio_device *adev)
{
    aextnmod.anc_enabled = 0;
    aextnmod.aanc_enabled = 0;
    aextnmod.custom_stereo_enabled = 0;
    aextnmod.proxy_channel_num = 2;
    aextnmod.hpx_enabled = 0;
    aextnmod.vbat_enabled = 0;
    aextnmod.bcl_enabled = 0;
    aextnmod.hifi_audio_enabled = 0;
    aextnmod.addr.nap = 0;
    aextnmod.addr.uap = 0;
    aextnmod.addr.lap = 0;
    aextnmod.adev = adev;

    audio_extn_dolby_set_license(adev);
    audio_extn_aptx_dec_set_license(adev);
}

#ifdef AUDIO_GKI_ENABLED
static int get_wma_dec_info(struct stream_out *out, struct str_parms *parms) {
    int ret = 0;
    char value[32];

    struct snd_generic_dec_wma *wma_dec = NULL;

    /* reserved[0] will contain the WMA decoder type */
    if (out->format == AUDIO_FORMAT_WMA) {
        out->compr_config.codec->options.generic.reserved[0] = AUDIO_COMP_FORMAT_WMA;
    } else if (out->format == AUDIO_FORMAT_WMA_PRO) {
        out->compr_config.codec->options.generic.reserved[0] = AUDIO_COMP_FORMAT_WMA_PRO;
    } else {
        ALOGE("%s: unknown WMA format 0x%x\n", __func__, out->format);
        return -EINVAL;
    }

    /* reserved[1] onwards will contain the WMA decoder format info */
    wma_dec = (struct snd_generic_dec_wma *)
                &(out->compr_config.codec->options.generic.reserved[1]);
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_AVG_BIT_RATE,
                                value, sizeof(value));
    if (ret >= 0) {
        wma_dec->avg_bit_rate = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_BLOCK_ALIGN,
                                value, sizeof(value));
    if (ret >= 0) {
        wma_dec->super_block_align = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_BIT_PER_SAMPLE,
                                value, sizeof(value));
    if (ret >= 0) {
        wma_dec->bits_per_sample = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_CHANNEL_MASK,
                                value, sizeof(value));
    if (ret >= 0) {
        wma_dec->channelmask = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION,
                                value, sizeof(value));
    if (ret >= 0) {
        wma_dec->encodeopt = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION1,
                                value, sizeof(value));
    if (ret >= 0) {
        wma_dec->encodeopt1 = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION2,
                                value, sizeof(value));
    if (ret >= 0) {
        wma_dec->encodeopt2 = atoi(value);
        out->is_compr_metadata_avail = true;
    }

    ALOGV("WMA params: fmt 0x%x, id 0x%x, WMA type 0x%x, bit rate 0x%x,"
            " balgn 0x%x, sr %d, chmsk 0x%x"
            " encop 0x%x, op1 0x%x, op2 0x%x \n",
            out->compr_config.codec->format,
            out->compr_config.codec->id,
            out->compr_config.codec->options.generic.reserved[0],
            wma_dec->avg_bit_rate,
            wma_dec->super_block_align,
            wma_dec->bits_per_sample,
            wma_dec->channelmask,
            wma_dec->encodeopt,
            wma_dec->encodeopt1,
            wma_dec->encodeopt2);

    return ret;
}
#else
int get_wma_info(struct stream_out *out, struct str_parms *parms) {
    int ret = 0;
    char value[32];

    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_AVG_BIT_RATE, value, sizeof(value));
    if (ret >= 0) {
        out->compr_config.codec->options.wma.avg_bit_rate = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_BLOCK_ALIGN, value, sizeof(value));
    if (ret >= 0) {
        out->compr_config.codec->options.wma.super_block_align = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_BIT_PER_SAMPLE, value, sizeof(value));
    if (ret >= 0) {
        out->compr_config.codec->options.wma.bits_per_sample = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_CHANNEL_MASK, value, sizeof(value));
    if (ret >= 0) {
        out->compr_config.codec->options.wma.channelmask = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION, value, sizeof(value));
    if (ret >= 0) {
        out->compr_config.codec->options.wma.encodeopt = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION1, value, sizeof(value));
    if (ret >= 0) {
        out->compr_config.codec->options.wma.encodeopt1 = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION2, value, sizeof(value));
    if (ret >= 0) {
        out->compr_config.codec->options.wma.encodeopt2 = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ALOGV("WMA params: fmt %x, bit rate %x, balgn %x, sr %d, chmsk %x"
            " encop %x, op1 %x, op2 %x",
            out->compr_config.codec->format,
            out->compr_config.codec->options.wma.avg_bit_rate,
            out->compr_config.codec->options.wma.super_block_align,
            out->compr_config.codec->options.wma.bits_per_sample,
            out->compr_config.codec->options.wma.channelmask,
            out->compr_config.codec->options.wma.encodeopt,
            out->compr_config.codec->options.wma.encodeopt1,
            out->compr_config.codec->options.wma.encodeopt2);

    return ret;
}
#endif

#ifdef AUDIO_GKI_ENABLED
static int get_flac_dec_info(struct stream_out *out, struct str_parms *parms) {
    int ret = 0;
    char value[32];
    struct snd_generic_dec_flac *flac_dec = NULL;

    /* reserved[0] will contain the FLAC decoder type */
    out->compr_config.codec->options.generic.reserved[0] =
                                                AUDIO_COMP_FORMAT_FLAC;
    /* reserved[1] onwards will contain the FLAC decoder format info */
    flac_dec = (struct snd_generic_dec_flac *)
                &(out->compr_config.codec->options.generic.reserved[1]);
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MIN_BLK_SIZE,
                                value, sizeof(value));
    if (ret >= 0) {
        flac_dec->min_blk_size = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MAX_BLK_SIZE,
                                value, sizeof(value));
    if (ret >= 0) {
        flac_dec->max_blk_size = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MIN_FRAME_SIZE,
                                value, sizeof(value));
    if (ret >= 0) {
        flac_dec->min_frame_size = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MAX_FRAME_SIZE,
                                value, sizeof(value));
    if (ret >= 0) {
        flac_dec->max_frame_size = atoi(value);
        out->is_compr_metadata_avail = true;
    }

    ALOGV("FLAC metadata: fmt 0x%x, id 0x%x, FLAC type 0x%x min_blk_size %d,"
                "  max_blk_size %d min_frame_size %d max_frame_size %d \n",
          out->compr_config.codec->format,
          out->compr_config.codec->id,
          out->compr_config.codec->options.generic.reserved[0],
          flac_dec->min_blk_size,
          flac_dec->max_blk_size,
          flac_dec->min_frame_size,
          flac_dec->max_frame_size);

    return ret;
}

static int get_alac_dec_info(struct stream_out *out, struct str_parms *parms) {
    int ret = 0;
    char value[32];
    struct snd_generic_dec_alac *alac_dec = NULL;

    /* reserved[0] will contain the ALAC decoder type */
    out->compr_config.codec->options.generic.reserved[0] =
                                                AUDIO_COMP_FORMAT_ALAC;
    /* reserved[1] onwards will contain the ALAC decoder format info */
    alac_dec = (struct snd_generic_dec_alac *)
                &(out->compr_config.codec->options.generic.reserved[1]);
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_FRAME_LENGTH,
                                value, sizeof(value));
    if (ret >= 0) {
        alac_dec->frame_length = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_COMPATIBLE_VERSION,
                                value, sizeof(value));
    if (ret >= 0) {
        alac_dec->compatible_version = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_BIT_DEPTH,
                                value, sizeof(value));
    if (ret >= 0) {
        alac_dec->bit_depth = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_PB,
                                value, sizeof(value));
    if (ret >= 0) {
        alac_dec->pb = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_MB,
                                value, sizeof(value));
    if (ret >= 0) {
        alac_dec->mb = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_KB,
                                value, sizeof(value));
    if (ret >= 0) {
        alac_dec->kb = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_NUM_CHANNELS,
                                value, sizeof(value));
    if (ret >= 0) {
        alac_dec->num_channels = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_MAX_RUN,
                                value, sizeof(value));
    if (ret >= 0) {
        alac_dec->max_run = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_MAX_FRAME_BYTES,
                                value, sizeof(value));
    if (ret >= 0) {
        alac_dec->max_frame_bytes = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_AVG_BIT_RATE,
                                value, sizeof(value));
    if (ret >= 0) {
        alac_dec->avg_bit_rate = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_SAMPLING_RATE,
                                value, sizeof(value));
    if (ret >= 0) {
        alac_dec->sample_rate = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_CHANNEL_LAYOUT_TAG,
                                value, sizeof(value));
    if (ret >= 0) {
        alac_dec->channel_layout_tag = atoi(value);
        out->is_compr_metadata_avail = true;
    }

    ALOGV("ALAC CSD values: fmt 0x%x, id 0x%x, ALAC type 0x%x, frameLength %d"
            "bitDepth %d numChannels %d"
            " maxFrameBytes %d, avgBitRate %d, sampleRate %d \n",
            out->compr_config.codec->format,
            out->compr_config.codec->id,
            out->compr_config.codec->options.generic.reserved[0],
            alac_dec->frame_length,
            alac_dec->bit_depth,
            alac_dec->num_channels,
            alac_dec->max_frame_bytes,
            alac_dec->avg_bit_rate,
            alac_dec->sample_rate);

    return ret;
}

static int get_ape_dec_info(struct stream_out *out, struct str_parms *parms) {
    int ret = 0;
    char value[32];
    struct snd_generic_dec_ape *ape_dec = NULL;

    /* reserved[0] will contain the APE decoder type */
    out->compr_config.codec->options.generic.reserved[0] =
                                                        AUDIO_COMP_FORMAT_APE;

    /* reserved[1] onwards will contain the APE decoder format info */
    ape_dec = (struct snd_generic_dec_ape *)
                &(out->compr_config.codec->options.generic.reserved[1]);

    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_COMPATIBLE_VERSION,
                                value, sizeof(value));
    if (ret >= 0) {
        ape_dec->compatible_version = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_COMPRESSION_LEVEL,
                                value, sizeof(value));
    if (ret >= 0) {
        ape_dec->compression_level = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_FORMAT_FLAGS,
                                value, sizeof(value));
    if (ret >= 0) {
        ape_dec->format_flags = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_BLOCKS_PER_FRAME,
                                value, sizeof(value));
    if (ret >= 0) {
        ape_dec->blocks_per_frame = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_FINAL_FRAME_BLOCKS,
                                value, sizeof(value));
    if (ret >= 0) {
        ape_dec->final_frame_blocks = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_TOTAL_FRAMES, value,
                                sizeof(value));
    if (ret >= 0) {
        ape_dec->total_frames = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_BITS_PER_SAMPLE,
                                value, sizeof(value));
    if (ret >= 0) {
        ape_dec->bits_per_sample = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_NUM_CHANNELS,
                                 value, sizeof(value));
    if (ret >= 0) {
        ape_dec->num_channels = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_SAMPLE_RATE,
                                value, sizeof(value));
    if (ret >= 0) {
        ape_dec->sample_rate = atoi(value);
        out->is_compr_metadata_avail = true;
    }
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_SEEK_TABLE_PRESENT,
                                value, sizeof(value));
    if (ret >= 0) {
        ape_dec->seek_table_present = atoi(value);
        out->is_compr_metadata_avail = true;
    }

    ALOGV("APE CSD values: fmt 0x%x, id 0x%x, APE type 0x%x"
            " compatibleVersion %d compressionLevel %d"
            " formatFlags %d blocksPerFrame %d finalFrameBlocks %d"
            " totalFrames %d bitsPerSample %d numChannels %d"
            " sampleRate %d seekTablePresent %d",
            out->compr_config.codec->format,
            out->compr_config.codec->id,
            out->compr_config.codec->options.generic.reserved[0],
            ape_dec->compatible_version,
            ape_dec->compression_level,
            ape_dec->format_flags,
            ape_dec->blocks_per_frame,
            ape_dec->final_frame_blocks,
            ape_dec->total_frames,
            ape_dec->bits_per_sample,
            ape_dec->num_channels,
            ape_dec->sample_rate,
            ape_dec->seek_table_present);

    return ret;
}

static int get_vorbis_dec_info(struct stream_out *out,
                                struct str_parms *parms) {
    int ret = 0;
    char value[32];
    struct snd_generic_dec_vorbis *vorbis_dec = NULL;

    /* reserved[0] will contain the Vorbis decoder type */
    out->compr_config.codec->options.generic.reserved[0] =
                                                AUDIO_COMP_FORMAT_VORBIS;
    /* reserved[1] onwards will contain the Vorbis decoder format info */
    vorbis_dec = (struct snd_generic_dec_vorbis *)
                    &(out->compr_config.codec->options.generic.reserved[1]);
    ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_VORBIS_BITSTREAM_FMT,
                                value, sizeof(value));
    if (ret >= 0) {
        // transcoded bitstream mode
        vorbis_dec->bit_stream_fmt = (atoi(value) > 0) ? 1 : 0;
        out->is_compr_metadata_avail = true;
    }

    ALOGV("Vorbis values: fmt 0x%x, id 0x%x, Vorbis type 0x%x"
            " bitStreamFmt %d\n",
          out->compr_config.codec->format,
          out->compr_config.codec->id,
          out->compr_config.codec->options.generic.reserved[0],
          vorbis_dec->bit_stream_fmt);

     return ret;
}

int audio_extn_parse_compress_metadata(struct stream_out *out,
                                       struct str_parms *parms)
{
    int ret = 0;
    char value[32];

    if (!if_compress_meta_data_feature_enabled())
        return ret;

    if (out->format == AUDIO_FORMAT_FLAC) {
        ret = get_flac_dec_info(out, parms);
    } else if (out->format == AUDIO_FORMAT_ALAC) {
        ret = get_alac_dec_info(out, parms);
    } else if (out->format == AUDIO_FORMAT_APE) {
        ret = get_ape_dec_info(out, parms);
    } else if (out->format == AUDIO_FORMAT_VORBIS) {
        ret = get_vorbis_dec_info(out, parms);
    } else if (out->format == AUDIO_FORMAT_WMA ||
                out->format == AUDIO_FORMAT_WMA_PRO) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_FORMAT_TAG,
                                value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->format = atoi(value);
            out->is_compr_metadata_avail = true;
        }

        ret = get_wma_dec_info(out, parms);
    }

    return ret;
}

#else
int audio_extn_parse_compress_metadata(struct stream_out *out,
                                       struct str_parms *parms)
{
    int ret = 0;
    char value[32];

    if (!if_compress_meta_data_feature_enabled()) {
        return ret;
    }

    if (out->format == AUDIO_FORMAT_FLAC) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MIN_BLK_SIZE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.flac_dec.min_blk_size = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MAX_BLK_SIZE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.flac_dec.max_blk_size = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MIN_FRAME_SIZE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.flac_dec.min_frame_size = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_FLAC_MAX_FRAME_SIZE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.flac_dec.max_frame_size = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ALOGV("FLAC metadata: min_blk_size %d, max_blk_size %d min_frame_size %d max_frame_size %d",
              out->compr_config.codec->options.flac_dec.min_blk_size,
              out->compr_config.codec->options.flac_dec.max_blk_size,
              out->compr_config.codec->options.flac_dec.min_frame_size,
              out->compr_config.codec->options.flac_dec.max_frame_size);
    }

    else if (out->format == AUDIO_FORMAT_ALAC) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_FRAME_LENGTH, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.frame_length = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_COMPATIBLE_VERSION, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.compatible_version = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_BIT_DEPTH, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.bit_depth = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_PB, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.pb = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_MB, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.mb = atoi(value);
            out->is_compr_metadata_avail = true;
        }

        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_KB, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.kb = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_NUM_CHANNELS, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.num_channels = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_MAX_RUN, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.max_run = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_MAX_FRAME_BYTES, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.max_frame_bytes = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_AVG_BIT_RATE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.avg_bit_rate = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_SAMPLING_RATE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.sample_rate = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_ALAC_CHANNEL_LAYOUT_TAG, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.alac.channel_layout_tag = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ALOGV("ALAC CSD values: frameLength %d bitDepth %d numChannels %d"
                " maxFrameBytes %d, avgBitRate %d, sampleRate %d",
                out->compr_config.codec->options.alac.frame_length,
                out->compr_config.codec->options.alac.bit_depth,
                out->compr_config.codec->options.alac.num_channels,
                out->compr_config.codec->options.alac.max_frame_bytes,
                out->compr_config.codec->options.alac.avg_bit_rate,
                out->compr_config.codec->options.alac.sample_rate);
    }

    else if (out->format == AUDIO_FORMAT_APE) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_COMPATIBLE_VERSION, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.compatible_version = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_COMPRESSION_LEVEL, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.compression_level = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_FORMAT_FLAGS, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.format_flags = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_BLOCKS_PER_FRAME, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.blocks_per_frame = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_FINAL_FRAME_BLOCKS, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.final_frame_blocks = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_TOTAL_FRAMES, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.total_frames = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_BITS_PER_SAMPLE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.bits_per_sample = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_NUM_CHANNELS, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.num_channels = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_SAMPLE_RATE, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.sample_rate = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_APE_SEEK_TABLE_PRESENT, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->options.ape.seek_table_present = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ALOGV("APE CSD values: compatibleVersion %d compressionLevel %d"
                " formatFlags %d blocksPerFrame %d finalFrameBlocks %d"
                " totalFrames %d bitsPerSample %d numChannels %d"
                " sampleRate %d seekTablePresent %d",
                out->compr_config.codec->options.ape.compatible_version,
                out->compr_config.codec->options.ape.compression_level,
                out->compr_config.codec->options.ape.format_flags,
                out->compr_config.codec->options.ape.blocks_per_frame,
                out->compr_config.codec->options.ape.final_frame_blocks,
                out->compr_config.codec->options.ape.total_frames,
                out->compr_config.codec->options.ape.bits_per_sample,
                out->compr_config.codec->options.ape.num_channels,
                out->compr_config.codec->options.ape.sample_rate,
                out->compr_config.codec->options.ape.seek_table_present);
    }

    else if (out->format == AUDIO_FORMAT_VORBIS) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_VORBIS_BITSTREAM_FMT, value, sizeof(value));
        if (ret >= 0) {
        // transcoded bitstream mode
            out->compr_config.codec->options.vorbis_dec.bit_stream_fmt = (atoi(value) > 0) ? 1 : 0;
            out->is_compr_metadata_avail = true;
        }
    }

    else if (out->format == AUDIO_FORMAT_WMA || out->format == AUDIO_FORMAT_WMA_PRO) {
        ret = str_parms_get_str(parms, AUDIO_OFFLOAD_CODEC_WMA_FORMAT_TAG, value, sizeof(value));
        if (ret >= 0) {
            out->compr_config.codec->format = atoi(value);
            out->is_compr_metadata_avail = true;
        }
        ret = get_wma_info(out, parms);
    }

    return ret;
}
#endif

#ifdef AUXPCM_BT_ENABLED
int32_t audio_extn_read_xml(struct audio_device *adev, uint32_t mixer_card,
                            const char* mixer_xml_path,
                            const char* mixer_xml_path_auxpcm)
{
    char bt_soc[128];
    bool wifi_init_complete = false;
    int sleep_retry = 0;

    while (!wifi_init_complete && sleep_retry < MAX_SLEEP_RETRY) {
        property_get("qcom.bluetooth.soc", bt_soc, NULL);
        if (strncmp(bt_soc, "unknown", sizeof("unknown"))) {
            wifi_init_complete = true;
        } else {
            usleep(WIFI_INIT_WAIT_SLEEP*1000);
            sleep_retry++;
        }
    }

    if (!strncmp(bt_soc, "ath3k", sizeof("ath3k")))
        adev->audio_route = audio_route_init(mixer_card, mixer_xml_path_auxpcm);
    else
        adev->audio_route = audio_route_init(mixer_card, mixer_xml_path);

    return 0;
}
#endif /* AUXPCM_BT_ENABLED */

static int audio_extn_set_multichannel_mask(struct audio_device *adev,
                                            struct stream_in *in,
                                            struct audio_config *config,
                                            bool *channel_mask_updated)
{
    int ret = -EINVAL;
    int channel_count = audio_channel_count_from_in_mask(in->channel_mask);
    *channel_mask_updated = false;

    int max_mic_count = platform_get_max_mic_count(adev->platform);
    /* validate input params. Avoid updated channel mask if loopback device */
    if ((channel_count == 6) &&
        (in->format == AUDIO_FORMAT_PCM_16_BIT) &&
        (!is_loopback_input_device(get_device_types(&in->device_list)))) {
        switch (max_mic_count) {
            case 4:
                config->channel_mask = AUDIO_CHANNEL_INDEX_MASK_4;
                break;
            case 3:
                config->channel_mask = AUDIO_CHANNEL_INDEX_MASK_3;
                break;
            case 2:
                config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
                break;
            default:
                config->channel_mask = AUDIO_CHANNEL_IN_STEREO;
                break;
        }
        ret = 0;
        *channel_mask_updated = true;
    }
    return ret;
}

int audio_extn_check_and_set_multichannel_usecase(struct audio_device *adev,
                                                  struct stream_in *in,
                                                  struct audio_config *config,
                                                  bool *update_params)
{
    bool ssr_supported = false;
    in->config.rate = config->sample_rate;
    in->sample_rate = config->sample_rate;
    ssr_supported = audio_extn_ssr_check_usecase(in);
    if (ssr_supported) {
        return audio_extn_ssr_set_usecase(in, config, update_params);
    } else if (audio_extn_ffv_check_usecase(in)) {
        char ffv_lic[LICENSE_STR_MAX_LEN + 1] = {0};
        int ffv_key = 0;
        if(platform_get_license_by_product(adev->platform, PRODUCT_FFV, &ffv_key, ffv_lic))
        {
            ALOGD("%s: Valid licence not availble for %s ", __func__, PRODUCT_FFV);
            return -EINVAL;
        }
        ALOGD("%s: KEY: %d LICENSE: %s ", __func__, ffv_key, ffv_lic);
        return audio_extn_ffv_set_usecase(in, ffv_key, ffv_lic);
    } else {
        return audio_extn_set_multichannel_mask(adev, in, config,
                                                update_params);
    }
}

#ifdef APTX_DECODER_ENABLED
static void audio_extn_aptx_dec_set_license(struct audio_device *adev)
{
    int ret, key = 0;
    char value[128] = {0};
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name = "APTX Dec License";

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return;
    }
    key = platform_get_meta_info_key_from_list(adev->platform, "aptx");

    ALOGD("%s Setting APTX License with key:0x%x",__func__, key);
    ret = mixer_ctl_set_value(ctl, 0, key);
    if (ret)
        ALOGE("%s: cannot set license, error:%d",__func__, ret);
}

static void audio_extn_set_aptx_dec_bt_addr(struct audio_device *adev, struct str_parms *parms)
{
    int ret = 0;
    char value[256];

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_APTX_DEC_BT_ADDR, value,
                            sizeof(value));
    if (ret >= 0) {
        audio_extn_parse_aptx_dec_bt_addr(value);
    }
}

int audio_extn_set_aptx_dec_params(struct aptx_dec_param *payload)
{
    struct aptx_dec_param *aptx_cfg = payload;

    aextnmod.addr.nap = aptx_cfg->bt_addr.nap;
    aextnmod.addr.uap = aptx_cfg->bt_addr.uap;
    aextnmod.addr.lap = aptx_cfg->bt_addr.lap;
}

static void audio_extn_parse_aptx_dec_bt_addr(char *value)
{
    int ba[6];
    char *str, *tok;
    uint32_t addr[3];
    int i = 0;

    ALOGV("%s: value %s", __func__, value);
    tok = strtok_r(value, ":", &str);
    while (tok != NULL) {
        ba[i] = strtol(tok, NULL, 16);
        i++;
        tok = strtok_r(NULL, ":", &str);
    }
    addr[0] = (ba[0] << 8) | ba[1];
    addr[1] = ba[2];
    addr[2] = (ba[3] << 16) | (ba[4] << 8) | ba[5];

    aextnmod.addr.nap = addr[0];
    aextnmod.addr.uap = addr[1];
    aextnmod.addr.lap = addr[2];
}

void audio_extn_send_aptx_dec_bt_addr_to_dsp(struct stream_out *out)
{
    char mixer_ctl_name[128];
    struct mixer_ctl *ctl;
    uint32_t addr[3];

    ALOGV("%s", __func__);
    out->compr_config.codec->options.aptx_dec.nap = aextnmod.addr.nap;
    out->compr_config.codec->options.aptx_dec.uap = aextnmod.addr.uap;
    out->compr_config.codec->options.aptx_dec.lap = aextnmod.addr.lap;
}

#endif //APTX_DECODER_ENABLED

int audio_extn_out_set_param_data(struct stream_out *out,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload) {
    int ret = -EINVAL;

    if (!out || !payload) {
        ALOGE("%s:: Invalid Param",__func__);
        return ret;
    }

    ALOGD("%s: enter: stream (%p) usecase(%d: %s) param_id %d", __func__,
            out, out->usecase, use_case_table[out->usecase], param_id);

    switch (param_id) {
        case AUDIO_EXTN_PARAM_OUT_RENDER_WINDOW:
            ret = audio_extn_utils_compress_set_render_window(out,
                    (struct audio_out_render_window_param *)(payload));
            break;
        case AUDIO_EXTN_PARAM_OUT_START_DELAY:
            ret = audio_extn_utils_compress_set_start_delay(out,
                    (struct audio_out_start_delay_param *)(payload));
            break;
        case AUDIO_EXTN_PARAM_OUT_ENABLE_DRIFT_CORRECTION:
            ret = audio_extn_utils_compress_enable_drift_correction(out,
                    (struct audio_out_enable_drift_correction *)(payload));
            break;
        case AUDIO_EXTN_PARAM_OUT_CORRECT_DRIFT:
            ret = audio_extn_utils_compress_correct_drift(out,
                    (struct audio_out_correct_drift *)(payload));
            break;
        case AUDIO_EXTN_PARAM_ADSP_STREAM_CMD:
            ret = audio_extn_adsp_hdlr_stream_set_param(out->adsp_hdlr_stream_handle,
                    ADSP_HDLR_STREAM_CMD_REGISTER_EVENT,
                    (void *)&payload->adsp_event_params);
            break;
        case AUDIO_EXTN_PARAM_OUT_CHANNEL_MAP:
            ret = audio_extn_utils_set_channel_map(out,
                    (struct audio_out_channel_map_param *)(payload));
            break;
        case AUDIO_EXTN_PARAM_OUT_MIX_MATRIX_PARAMS:
            ret = audio_extn_utils_set_pan_scale_params(out,
                    (struct mix_matrix_params *)(payload));
            break;
        case AUDIO_EXTN_PARAM_CH_MIX_MATRIX_PARAMS:
            ret = audio_extn_utils_set_downmix_params(out,
                    (struct mix_matrix_params *)(payload));
            break;
        default:
            ALOGE("%s:: unsupported param_id %d", __func__, param_id);
            break;
    }
    return ret;
}

#ifdef AUDIO_HW_LOOPBACK_ENABLED
int audio_extn_hw_loopback_set_param_data(audio_patch_handle_t handle,
                                          audio_extn_loopback_param_id param_id,
                                          audio_extn_loopback_param_payload *payload) {
    int ret = -EINVAL;

    if (!payload) {
        ALOGE("%s:: Invalid Param",__func__);
        return ret;
    }

    ALOGD("%d: %s: param id is %d\n", __LINE__, __func__, param_id);

    switch(param_id) {
        case AUDIO_EXTN_PARAM_LOOPBACK_RENDER_WINDOW:
            ret = audio_extn_hw_loopback_set_render_window(handle, payload);
            break;
        default:
            ALOGE("%s: unsupported param id %d", __func__, param_id);
            break;
    }

    return ret;
}
#endif


/* API to get playback stream specific config parameters */
int audio_extn_out_get_param_data(struct stream_out *out,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload)
{
    int ret = -EINVAL;
    struct audio_usecase *uc_info;

    if (!out || !payload) {
        ALOGE("%s:: Invalid Param",__func__);
        return ret;
    }

    switch (param_id) {
        case AUDIO_EXTN_PARAM_AVT_DEVICE_DRIFT:
            uc_info = get_usecase_from_list(out->dev, out->usecase);
            if (uc_info == NULL) {
                ALOGE("%s: Could not find the usecase (%d) in the list",
                       __func__, out->usecase);
                ret = -EINVAL;
            } else {
                ret = audio_extn_utils_get_avt_device_drift(uc_info,
                        (struct audio_avt_device_drift_param *)payload);
                if(ret)
                    ALOGE("%s:: avdrift query failed error %d", __func__, ret);
            }
            break;
        case AUDIO_EXTN_PARAM_OUT_PRESENTATION_POSITION:
            ret = audio_ext_get_presentation_position(out,
                      (struct audio_out_presentation_position_param *)payload);
                if (ret < 0)
                    ALOGE("%s:: presentation position query failed error %d",
                           __func__, ret);
            break;
        default:
            ALOGE("%s:: unsupported param_id %d", __func__, param_id);
            break;
    }

    return ret;
}

int audio_extn_set_device_cfg_params(struct audio_device *adev,
                                     struct audio_device_cfg_param *payload)
{
    struct audio_device_cfg_param *device_cfg_params = payload;
    int ret = -EINVAL;
    struct stream_out out;
    uint32_t snd_device = 0, backend_idx = 0;
    struct audio_device_config_param *adev_device_cfg_ptr;

    ALOGV("%s", __func__);

    if (!device_cfg_params || !adev || !adev->device_cfg_params) {
        ALOGE("%s:: Invalid Param", __func__);
        return ret;
    }

    /* Config is not supported for combo devices */
    if (popcount(device_cfg_params->device) != 1) {
        ALOGE("%s:: Invalid Device (%#x) - Config is ignored", __func__, device_cfg_params->device);
        return ret;
    }

    adev_device_cfg_ptr = adev->device_cfg_params;
    /* Create an out stream to get snd device from audio device */
    reassign_device_list(&out.device_list, device_cfg_params->device, "");
    out.sample_rate = device_cfg_params->sample_rate;
    snd_device = platform_get_output_snd_device(adev->platform, &out, USECASE_TYPE_MAX);
    backend_idx = platform_get_backend_index(snd_device);

    ALOGV("%s:: device %d sample_rate %d snd_device %d backend_idx %d",
                __func__, get_device_types(&out.device_list),
                out.sample_rate, snd_device, backend_idx);

    ALOGV("%s:: Device Config Params from Client samplerate %d  channels %d"
          " bit_width %d  format %d  device %d  channel_map[0] %d channel_map[1] %d"
          " channel_map[2] %d channel_map[3] %d channel_map[4] %d channel_map[5] %d"
          " channel_allocation %d\n", __func__, device_cfg_params->sample_rate,
          device_cfg_params->channels, device_cfg_params->bit_width,
          device_cfg_params->format, device_cfg_params->device,
          device_cfg_params->channel_map[0], device_cfg_params->channel_map[1],
          device_cfg_params->channel_map[2], device_cfg_params->channel_map[3],
          device_cfg_params->channel_map[4], device_cfg_params->channel_map[5],
          device_cfg_params->channel_allocation);

    /* Copy the config values into adev structure variable */
    adev_device_cfg_ptr += backend_idx;
    adev_device_cfg_ptr->use_client_dev_cfg = true;
    memcpy(&adev_device_cfg_ptr->dev_cfg_params, device_cfg_params, sizeof(struct audio_device_cfg_param));

    return 0;
}

//START: FM_POWER_OPT_FEATURE ================================================================
void fm_feature_init(bool is_feature_enabled)
{
    audio_extn_fm_power_opt_enabled = is_feature_enabled;
    ALOGD("%s: ---- Feature FM_POWER_OPT is %s----", __func__, is_feature_enabled? "ENABLED": "NOT ENABLED");
}


void audio_extn_fm_get_parameters(struct str_parms *query, struct str_parms *reply)
{
    if(audio_extn_fm_power_opt_enabled) {
       ALOGD("%s: Enter", __func__);
       fm_get_parameters(query, reply);
    }
}

void audio_extn_fm_set_parameters(struct audio_device *adev,
                                  struct str_parms *parms)
{
    if(audio_extn_fm_power_opt_enabled) {
       ALOGD("%s: Enter", __func__);
       fm_set_parameters(adev, parms);
    }
}
//END: FM_POWER_OPT_FEATURE ================================================================

//START: HDMI_EDID =========================================================================
#ifdef __LP64__
#define HDMI_EDID_LIB_PATH  "/vendor/lib64/libhdmiedid.so"
#else
#define HDMI_EDID_LIB_PATH  "/vendor/lib/libhdmiedid.so"
#endif

static void *hdmi_edid_lib_handle = NULL;

typedef bool (*hdmi_edid_is_supported_sr_t)(edid_audio_info*, int);
static hdmi_edid_is_supported_sr_t hdmi_edid_is_supported_sr;

typedef bool (*hdmi_edid_is_supported_bps_t)(edid_audio_info*, int);
static hdmi_edid_is_supported_bps_t hdmi_edid_is_supported_bps;

typedef int (*hdmi_edid_get_highest_supported_sr_t)(edid_audio_info*);
static hdmi_edid_get_highest_supported_sr_t hdmi_edid_get_highest_supported_sr;

typedef bool (*hdmi_edid_get_sink_caps_t)(edid_audio_info*, char*);
static hdmi_edid_get_sink_caps_t hdmi_edid_get_sink_caps;

void hdmi_edid_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: HDMI_EDID feature %s", __func__, is_feature_enabled?"Enabled":"NOT Enabled");
    if (is_feature_enabled) {
        //dlopen lib
        hdmi_edid_lib_handle = dlopen(HDMI_EDID_LIB_PATH, RTLD_NOW);
        if (hdmi_edid_lib_handle == NULL) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        //map each function
        //on any faliure to map any function, disble feature
        if (((hdmi_edid_is_supported_sr =
             (hdmi_edid_is_supported_sr_t)dlsym(hdmi_edid_lib_handle, 
                                                "edid_is_supported_sr")) == NULL) ||
            ((hdmi_edid_is_supported_bps =
             (hdmi_edid_is_supported_bps_t)dlsym(hdmi_edid_lib_handle,
                                                "edid_is_supported_bps")) == NULL) ||
            ((hdmi_edid_get_highest_supported_sr =
             (hdmi_edid_get_highest_supported_sr_t)dlsym(hdmi_edid_lib_handle, 
                                                "edid_get_highest_supported_sr")) == NULL) ||
            ((hdmi_edid_get_sink_caps =
             (hdmi_edid_get_sink_caps_t)dlsym(hdmi_edid_lib_handle, 
                                                "edid_get_sink_caps")) == NULL)) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature HDMI_EDID is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (hdmi_edid_lib_handle) {
        dlclose(hdmi_edid_lib_handle);
        hdmi_edid_lib_handle = NULL;
    }

    hdmi_edid_is_supported_sr = NULL;
    hdmi_edid_is_supported_bps = NULL;
    hdmi_edid_get_highest_supported_sr = NULL;
    hdmi_edid_get_sink_caps = NULL;
    ALOGW(":: %s: ---- Feature HDMI_EDID is disabled ----", __func__);
    return;
}

bool audio_extn_edid_is_supported_sr(edid_audio_info* info, int sr)
{
    bool ret = false;

    if(hdmi_edid_is_supported_sr != NULL)
        ret = hdmi_edid_is_supported_sr(info, sr);
    return ret;
}

bool audio_extn_edid_is_supported_bps(edid_audio_info* info, int bps)
{
    bool ret = false;

    if(hdmi_edid_is_supported_bps != NULL)
        ret = hdmi_edid_is_supported_bps(info, bps);
    return ret;
}
int audio_extn_edid_get_highest_supported_sr(edid_audio_info* info)
{
    int ret = -1;

    if(hdmi_edid_get_highest_supported_sr != NULL)
        ret = hdmi_edid_get_highest_supported_sr(info);
    return ret;
}

bool audio_extn_edid_get_sink_caps(edid_audio_info* info, char *edid_data)
{
    bool ret = false;

    if(hdmi_edid_get_sink_caps != NULL)
        ret = hdmi_edid_get_sink_caps(info, edid_data);
    return ret;
}

//END: HDMI_EDID =========================================================================


//START: KEEP_ALIVE =========================================================================

void keep_alive_feature_init(bool is_feature_enabled)
{
    audio_extn_keep_alive_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature KEEP_ALIVE is %s ----", __func__, is_feature_enabled? "ENABLED": " NOT ENABLED");
}

void audio_extn_keep_alive_init(struct audio_device *adev)
{
    if(audio_extn_keep_alive_enabled)
        keep_alive_init(adev);
}

void audio_extn_keep_alive_deinit()
{
    if(audio_extn_keep_alive_enabled)
        keep_alive_deinit();
}

void audio_extn_keep_alive_start(ka_mode_t ka_mode)
{
    if(audio_extn_keep_alive_enabled)
        keep_alive_start(ka_mode);
}

void audio_extn_keep_alive_stop(ka_mode_t ka_mode)
{
    if(audio_extn_keep_alive_enabled)
        keep_alive_stop(ka_mode);
}

bool audio_extn_keep_alive_is_active()
{
    bool ret = false;
    return ret;
}

int audio_extn_keep_alive_set_parameters(struct audio_device *adev,
                                         struct str_parms *parms)
{
    int ret = -1;
    if(audio_extn_keep_alive_enabled)
        return keep_alive_set_parameters(adev, parms);
    return ret;
}
//END: KEEP_ALIVE =========================================================================

//START: HIFI_AUDIO =========================================================================
void hifi_audio_feature_init(bool is_feature_enabled)
{
    audio_extn_hifi_audio_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature HIFI_AUDIO is %s ----", __func__, is_feature_enabled? "ENABLED": " NOT ENABLED");
}

bool audio_extn_is_hifi_audio_enabled(void)
{
    bool ret = false;
    if(audio_extn_hifi_audio_enabled)
    {
        ALOGV("%s: status: %d", __func__, aextnmod.hifi_audio_enabled);
        return (aextnmod.hifi_audio_enabled ? true: false);
    }
    return ret;
}

bool audio_extn_is_hifi_audio_supported(void)
{
    bool ret = false;

    if(audio_extn_hifi_audio_enabled)
    {
        /*
         * for internal codec, check for hifiaudio property to enable hifi audio
         */
        if (property_get_bool("persist.vendor.audio.hifi.int_codec", false))
        {
            ALOGD("%s: hifi audio supported on internal codec", __func__);
            aextnmod.hifi_audio_enabled = 1;
        }
        return (aextnmod.hifi_audio_enabled ? true: false);
    }
    return ret;
}

//END: HIFI_AUDIO =========================================================================

//START: RAS =============================================================================
void ras_feature_init(bool is_feature_enabled)
{
    audio_extn_ras_feature_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature RAS_FEATURE is %s ----", __func__, is_feature_enabled? "ENABLED": " NOT ENABLED");
}

bool audio_extn_is_ras_enabled(void)
{
    bool ret = false;

    if(audio_extn_ras_feature_enabled)
    {
        ALOGD("%s: status: %d", __func__, aextnmod.ras_enabled);
        return (aextnmod.ras_enabled ? true: false);
    }
    return ret;
}

bool audio_extn_can_use_ras(void)
{
    bool ret = false;

    if(audio_extn_ras_feature_enabled)
    {
        if (property_get_bool("persist.vendor.audio.ras.enabled", false))
            aextnmod.ras_enabled = 1;
        ALOGD("%s: ras.enabled property is set to %d", __func__, aextnmod.ras_enabled);
        return (aextnmod.ras_enabled ? true: false);
    }
    return ret;
}

//END: RAS ===============================================================================

//START: KPI_OPTIMIZE =============================================================================
void kpi_optimize_feature_init(bool is_feature_enabled)
{
    audio_extn_kpi_optimize_feature_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature KPI_OPTIMIZE is %s ----", __func__, is_feature_enabled? "ENABLED": " NOT ENABLED");
}

typedef int (*perf_lock_acquire_t)(int, int, int*, int);
typedef int (*perf_lock_release_t)(int);

static void *qcopt_handle;
static perf_lock_acquire_t perf_lock_acq;
static perf_lock_release_t perf_lock_rel;

char opt_lib_path[512] = {0};

int audio_extn_perf_lock_init(void)
{
    int ret = 0;

    //if feature is disabled, exit immediately
    if(!audio_extn_kpi_optimize_feature_enabled)
        goto err;

    if (qcopt_handle == NULL) {
        if (property_get("ro.vendor.extension_library",
                         opt_lib_path, NULL) <= 0) {
            ALOGE("%s: Failed getting perf property \n", __func__);
            ret = -EINVAL;
            goto err;
        }
        if ((qcopt_handle = dlopen(opt_lib_path, RTLD_NOW)) == NULL) {
            ALOGE("%s: Failed to open perf handle \n", __func__);
            ret = -EINVAL;
            goto err;
        } else {
            perf_lock_acq = (perf_lock_acquire_t)dlsym(qcopt_handle,
                                                       "perf_lock_acq");
            if (perf_lock_acq == NULL) {
                ALOGE("%s: Perf lock Acquire NULL \n", __func__);
                dlclose(qcopt_handle);
                ret = -EINVAL;
                goto err;
            }
            perf_lock_rel = (perf_lock_release_t)dlsym(qcopt_handle,
                                                       "perf_lock_rel");
            if (perf_lock_rel == NULL) {
                ALOGE("%s: Perf lock Release NULL \n", __func__);
                dlclose(qcopt_handle);
                ret = -EINVAL;
                goto err;
            }
            ALOGE("%s: Perf lock handles Success \n", __func__);
        }
    }
err:
    return ret;
}

void audio_extn_perf_lock_acquire(int *handle, int duration,
                                 int *perf_lock_opts, int size)
{
    if (audio_extn_kpi_optimize_feature_enabled)
    {
        if (!perf_lock_opts || !size || !perf_lock_acq || !handle) {
            ALOGE("%s: Incorrect params, Failed to acquire perf lock, err ",
                  __func__);
            return;
        }
        /*
         * Acquire performance lock for 1 sec during device path bringup.
         * Lock will be released either after 1 sec or when perf_lock_release
         * function is executed.
         */
        *handle = perf_lock_acq(*handle, duration, perf_lock_opts, size);
        if (*handle <= 0)
            ALOGE("%s: Failed to acquire perf lock, err: %d\n",
                  __func__, *handle);
    }
}

void audio_extn_perf_lock_release(int *handle)
{
    if (audio_extn_kpi_optimize_feature_enabled) {
         if (perf_lock_rel && handle && (*handle > 0)) {
            perf_lock_rel(*handle);
            *handle = 0;
        } else
            ALOGE("%s: Perf lock release error \n", __func__);
    }
}

//END: KPI_OPTIMIZE =============================================================================

//START: DISPLAY_PORT =============================================================================
void display_port_feature_init(bool is_feature_enabled)
{
    audio_extn_display_port_feature_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature DISPLAY_PORT is %s ----", __func__, is_feature_enabled? "ENABLED": " NOT ENABLED");
}

bool audio_extn_is_display_port_enabled()
{
    return audio_extn_display_port_feature_enabled;
}
//END: DISPLAY_PORT ===============================================================================
//START: FLUENCE =============================================================================
void fluence_feature_init(bool is_feature_enabled)
{
    audio_extn_fluence_feature_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature FLUENCE is %s ----", __func__, is_feature_enabled? "ENABLED": " NOT ENABLED");
}

bool audio_extn_is_fluence_enabled()
{
    return audio_extn_fluence_feature_enabled;
}

void audio_extn_set_fluence_parameters(struct audio_device *adev,
                                            struct str_parms *parms)
{
    int ret = 0, err;
    char value[32];
    struct listnode *node;
    struct audio_usecase *usecase;

    if (audio_extn_is_fluence_enabled()) {
        err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FLUENCE,
                                 value, sizeof(value));
        ALOGV_IF(err >= 0, "%s: Set Fluence Type to %s", __func__, value);
        if (err >= 0) {
            ret = platform_set_fluence_type(adev->platform, value);
            if (ret != 0) {
                ALOGE("platform_set_fluence_type returned error: %d", ret);
            } else {
                /*
                 *If the fluence is manually set/reset, devices
                 *need to get updated for all the usecases
                 *i.e. audio and voice.
                 */
                 list_for_each(node, &adev->usecase_list) {
                     usecase = node_to_item(node, struct audio_usecase, list);
                     select_devices(adev, usecase->id);
                 }
            }
        }

    }
}

int audio_extn_get_fluence_parameters(const struct audio_device *adev,
                       struct str_parms *query, struct str_parms *reply)
{
    int ret = -1, err;
    char value[256] = {0};

    if (audio_extn_is_fluence_enabled()) {
        err = str_parms_get_str(query, AUDIO_PARAMETER_KEY_FLUENCE, value,
                                                          sizeof(value));
        if (err >= 0) {
            ret = platform_get_fluence_type(adev->platform, value, sizeof(value));
            if (ret >= 0) {
                ALOGV("%s: Fluence Type is %s", __func__, value);
                str_parms_add_str(reply, AUDIO_PARAMETER_KEY_FLUENCE, value);
            } else
                goto done;
        }
    }
done:
    return ret;
}
//END: FLUENCE ===============================================================================
//START: WSA =============================================================================
void wsa_feature_init(bool is_feature_enabled)
{
    audio_extn_wsa_enabled = is_feature_enabled;
}

bool audio_extn_is_wsa_enabled()
{
    return audio_extn_wsa_enabled;
}
//END: WSA ===============================================================================
//START: CUSTOM_STEREO =============================================================================
void custom_stereo_feature_init(bool is_feature_enabled)
{
    audio_extn_custom_stereo_feature_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature CUSTOM_STEREO is %s ----", __func__, is_feature_enabled? "ENABLED": " NOT ENABLED");
}

bool audio_extn_is_custom_stereo_enabled()
{
    return audio_extn_custom_stereo_feature_enabled;
}

void audio_extn_customstereo_set_parameters(struct audio_device *adev,
                                           struct str_parms *parms)
{
    int ret = 0;
    char value[32]={0};
    bool custom_stereo_state = false;
    const char *mixer_ctl_name = "Set Custom Stereo OnOff";
    struct mixer_ctl *ctl;

    ALOGV("%s", __func__);

    if (audio_extn_custom_stereo_feature_enabled) {
        ret = str_parms_get_str(parms, AUDIO_PARAMETER_CUSTOM_STEREO, value,
                            sizeof(value));
        if (ret >= 0) {
            if (!strncmp("true", value, sizeof("true")) || atoi(value))
                custom_stereo_state = true;

            if (custom_stereo_state == aextnmod.custom_stereo_enabled)
                return;

            ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
            if (!ctl) {
                ALOGE("%s: Could not get ctl for mixer cmd - %s",
                      __func__, mixer_ctl_name);
                return;
            }
            if (mixer_ctl_set_value(ctl, 0, custom_stereo_state) < 0) {
                ALOGE("%s: Could not set custom stereo state %d",
                      __func__, custom_stereo_state);
                return;
            }
            aextnmod.custom_stereo_enabled = custom_stereo_state;
            ALOGV("%s: Setting custom stereo state success", __func__);
        }
    }
}

void audio_extn_send_dual_mono_mixing_coefficients(struct stream_out *out)
{
    struct audio_device *adev = out->dev;
    struct mixer_ctl *ctl;
    char mixer_ctl_name[128];
    int cust_ch_mixer_cfg[128], len = 0;
    int ip_channel_cnt = audio_channel_count_from_out_mask(out->channel_mask);
    int pcm_device_id = platform_get_pcm_device_id(out->usecase, PCM_PLAYBACK);
    int op_channel_cnt= 2;
    int i, j, err;

    ALOGV("%s", __func__);

    if (audio_extn_custom_stereo_feature_enabled) {
        if (!out->started) {
        out->set_dual_mono = true;
        goto exit;
        }

        ALOGD("%s: i/p channel count %d, o/p channel count %d, pcm id %d", __func__,
               ip_channel_cnt, op_channel_cnt, pcm_device_id);

        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                 "Audio Stream %d Channel Mix Cfg", pcm_device_id);
        ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s: ERROR. Could not get ctl for mixer cmd - %s",
            __func__, mixer_ctl_name);
            goto exit;
        }

        /* Output channel count corresponds to backend configuration channels.
         * Input channel count corresponds to ASM session channels.
         * Set params is called with channels that need to be selected from
         * input to generate output.
         * ex: "8,2" to downmix from 8 to 2 i.e. to downmix from 8 to 2,
         *
         * This mixer control takes values in the following sequence:
         * - input channel count(m)
         * - output channel count(n)
         * - weight coeff for [out ch#1, in ch#1]
         * ....
         * - weight coeff for [out ch#1, in ch#m]
         *
         * - weight coeff for [out ch#2, in ch#1]
         * ....
         * - weight coeff for [out ch#2, in ch#m]
         *
         * - weight coeff for [out ch#n, in ch#1]
         * ....
         * - weight coeff for [out ch#n, in ch#m]
         *
         * To get dualmono ouptu weightage coeff is calculated as Unity gain
         * divided by number of input channels.
         */
        cust_ch_mixer_cfg[len++] = ip_channel_cnt;
        cust_ch_mixer_cfg[len++] = op_channel_cnt;
        for (i = 0; i < op_channel_cnt; i++) {
             for (j = 0; j < ip_channel_cnt; j++) {
                  cust_ch_mixer_cfg[len++] = Q14_GAIN_UNITY/ip_channel_cnt;
             }
        }

        err = mixer_ctl_set_array(ctl, cust_ch_mixer_cfg, len);
        if (err)
            ALOGE("%s: ERROR. Mixer ctl set failed", __func__);

    }
exit:
    return;
}
//END: CUSTOM_STEREO =============================================================================
// START: A2DP_OFFLOAD ===================================================================
#ifdef __LP64__
#define A2DP_OFFLOAD_LIB_PATH "/vendor/lib64/liba2dpoffload.so"
#else
#define A2DP_OFFLOAD_LIB_PATH "/vendor/lib/liba2dpoffload.so"
#endif

static void *a2dp_lib_handle = NULL;

typedef void (*a2dp_init_t)(void *, a2dp_offload_init_config_t);
static a2dp_init_t a2dp_init;

typedef int (*a2dp_start_playback_t)();
static a2dp_start_playback_t a2dp_start_playback;

typedef int (*a2dp_stop_playback_t)();
static a2dp_stop_playback_t a2dp_stop_playback;

typedef int (*a2dp_set_parameters_t)(struct str_parms *,
                                     bool *);
static a2dp_set_parameters_t a2dp_set_parameters;

typedef int (*a2dp_get_parameters_t)(struct str_parms *,
                                   struct str_parms *);
static a2dp_get_parameters_t a2dp_get_parameters;

typedef bool (*a2dp_is_force_device_switch_t)();
static a2dp_is_force_device_switch_t a2dp_is_force_device_switch;

typedef void (*a2dp_set_handoff_mode_t)(bool);
static a2dp_set_handoff_mode_t a2dp_set_handoff_mode;

typedef void (*a2dp_get_enc_sample_rate_t)(int *);
static a2dp_get_enc_sample_rate_t a2dp_get_enc_sample_rate;

typedef void (*a2dp_get_dec_sample_rate_t)(int *);
static a2dp_get_dec_sample_rate_t a2dp_get_dec_sample_rate;

typedef uint32_t (*a2dp_get_encoder_latency_t)();
static a2dp_get_encoder_latency_t a2dp_get_encoder_latency;

typedef bool (*a2dp_sink_is_ready_t)();
static a2dp_sink_is_ready_t a2dp_sink_is_ready;

typedef bool (*a2dp_source_is_ready_t)();
static a2dp_source_is_ready_t a2dp_source_is_ready;

typedef bool (*a2dp_source_is_suspended_t)();
static a2dp_source_is_suspended_t a2dp_source_is_suspended;

typedef int (*a2dp_start_capture_t)();
static a2dp_start_capture_t a2dp_start_capture;

typedef int (*a2dp_stop_capture_t)();
static a2dp_stop_capture_t a2dp_stop_capture;

typedef bool (*a2dp_set_source_backend_cfg_t)();
static a2dp_set_source_backend_cfg_t a2dp_set_source_backend_cfg;

typedef int (*sco_start_configuration_t)();
static sco_start_configuration_t sco_start_configuration;

typedef void (*sco_reset_configuration_t)();
static sco_reset_configuration_t sco_reset_configuration;


int a2dp_offload_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__,
                  is_feature_enabled ? "Enabled" : "NOT Enabled");
    if (is_feature_enabled) {
        // dlopen lib
        a2dp_lib_handle = dlopen(A2DP_OFFLOAD_LIB_PATH, RTLD_NOW);

        if (!a2dp_lib_handle) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }

        if (!(a2dp_init = (a2dp_init_t)dlsym(a2dp_lib_handle, "a2dp_init")) ||
            !(a2dp_start_playback =
                 (a2dp_start_playback_t)dlsym(a2dp_lib_handle, "a2dp_start_playback")) ||
            !(a2dp_stop_playback =
                 (a2dp_stop_playback_t)dlsym(a2dp_lib_handle, "a2dp_stop_playback")) ||
            !(a2dp_set_parameters =
                 (a2dp_set_parameters_t)dlsym(a2dp_lib_handle, "a2dp_set_parameters")) ||
            !(a2dp_get_parameters =
                 (a2dp_get_parameters_t)dlsym(a2dp_lib_handle, "a2dp_get_parameters")) ||
            !(a2dp_is_force_device_switch =
                 (a2dp_is_force_device_switch_t)dlsym(
                                    a2dp_lib_handle, "a2dp_is_force_device_switch")) ||
            !(a2dp_set_handoff_mode =
                 (a2dp_set_handoff_mode_t)dlsym(
                                          a2dp_lib_handle, "a2dp_set_handoff_mode")) ||
            !(a2dp_get_enc_sample_rate =
                 (a2dp_get_enc_sample_rate_t)dlsym(
                                       a2dp_lib_handle, "a2dp_get_enc_sample_rate")) ||
            !(a2dp_get_dec_sample_rate =
                 (a2dp_get_dec_sample_rate_t)dlsym(
                                       a2dp_lib_handle, "a2dp_get_dec_sample_rate")) ||
            !(a2dp_get_encoder_latency =
                 (a2dp_get_encoder_latency_t)dlsym(
                                       a2dp_lib_handle, "a2dp_get_encoder_latency")) ||
            !(a2dp_sink_is_ready =
                 (a2dp_sink_is_ready_t)dlsym(a2dp_lib_handle, "a2dp_sink_is_ready")) ||
            !(a2dp_source_is_ready =
                 (a2dp_source_is_ready_t)dlsym(a2dp_lib_handle, "a2dp_source_is_ready")) ||
            !(a2dp_source_is_suspended =
                 (a2dp_source_is_suspended_t)dlsym(
                                       a2dp_lib_handle, "a2dp_source_is_suspended")) ||
            !(a2dp_start_capture =
                 (a2dp_start_capture_t)dlsym(a2dp_lib_handle, "a2dp_start_capture")) ||
            !(a2dp_stop_capture =
                 (a2dp_stop_capture_t)dlsym(a2dp_lib_handle, "a2dp_stop_capture")) ||
            !(a2dp_set_source_backend_cfg =
                 (a2dp_set_source_backend_cfg_t)dlsym(
                                     a2dp_lib_handle, "a2dp_set_source_backend_cfg"))) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }
        // initialize APIs for SWB extension
        if (!(sco_start_configuration =
                 (sco_start_configuration_t)dlsym(a2dp_lib_handle, "sco_start_configuration")) ||
             !(sco_reset_configuration =
                 (sco_reset_configuration_t)dlsym(a2dp_lib_handle, "sco_reset_configuration"))) {
            ALOGE("%s: dlsym failed for swb APIs", __func__);
            sco_start_configuration = NULL;
            sco_reset_configuration = NULL;
        }
        ALOGD("%s:: ---- Feature A2DP_OFFLOAD is Enabled ----", __func__);
        return 0;
    }

feature_disabled:
    if (a2dp_lib_handle) {
        dlclose(a2dp_lib_handle);
        a2dp_lib_handle = NULL;
    }

    a2dp_init = NULL;
    a2dp_start_playback= NULL;
    a2dp_stop_playback = NULL;
    a2dp_set_parameters = NULL;
    a2dp_get_parameters = NULL;
    a2dp_is_force_device_switch = NULL;
    a2dp_set_handoff_mode = NULL;
    a2dp_get_enc_sample_rate = NULL;
    a2dp_get_dec_sample_rate = NULL;
    a2dp_get_encoder_latency = NULL;
    a2dp_sink_is_ready = NULL;
    a2dp_source_is_ready = NULL;
    a2dp_source_is_suspended = NULL;
    a2dp_start_capture = NULL;
    a2dp_stop_capture = NULL;
    a2dp_set_source_backend_cfg = NULL;

    ALOGW(":: %s: ---- Feature A2DP_OFFLOAD is disabled ----", __func__);
    return -ENOSYS;
}

void audio_extn_a2dp_init(void *adev)
{
    if (a2dp_init) {
        a2dp_offload_init_config_t a2dp_init_config;
        a2dp_init_config.fp_platform_get_pcm_device_id = platform_get_pcm_device_id;
        a2dp_init_config.fp_check_a2dp_restore_l = check_a2dp_restore_l;

        a2dp_init(adev, a2dp_init_config);
    }
}

int audio_extn_a2dp_start_playback()
{
    return (a2dp_start_playback ? a2dp_start_playback() : 0);
}

int audio_extn_a2dp_stop_playback()
{
    return (a2dp_stop_playback ? a2dp_stop_playback() : 0);
}

int audio_extn_a2dp_set_parameters(struct str_parms *parms,
                                   bool *reconfig)
{
    return (a2dp_set_parameters ?
                    a2dp_set_parameters(parms, reconfig) : 0);
}

int audio_extn_a2dp_get_parameters(struct str_parms *query,
                                   struct str_parms *reply)
{
    return (a2dp_get_parameters ?
                    a2dp_get_parameters(query, reply) : 0);
}

bool audio_extn_a2dp_is_force_device_switch()
{
    return (a2dp_is_force_device_switch ?
                a2dp_is_force_device_switch() : false);
}

void audio_extn_a2dp_set_handoff_mode(bool is_on)
{
    if (a2dp_set_handoff_mode)
        a2dp_set_handoff_mode(is_on);
}

void audio_extn_a2dp_get_enc_sample_rate(int *sample_rate)
{
    if (a2dp_get_enc_sample_rate)
        a2dp_get_enc_sample_rate(sample_rate);
}

void audio_extn_a2dp_get_dec_sample_rate(int *sample_rate)
{
    if (a2dp_get_dec_sample_rate)
        a2dp_get_dec_sample_rate(sample_rate);
}

uint32_t audio_extn_a2dp_get_encoder_latency()
{
    return (a2dp_get_encoder_latency ?
                a2dp_get_encoder_latency() : 0);
}

bool audio_extn_a2dp_sink_is_ready()
{
    return (a2dp_sink_is_ready ?
                a2dp_sink_is_ready() : false);
}

bool audio_extn_a2dp_source_is_ready()
{
    return (a2dp_source_is_ready ?
                a2dp_source_is_ready() : false);
}

bool audio_extn_a2dp_source_is_suspended()
{
    return (a2dp_source_is_suspended ?
                a2dp_source_is_suspended() : false);
}

int audio_extn_a2dp_start_capture()
{
    return (a2dp_start_capture ? a2dp_start_capture() : 0);
}

int audio_extn_a2dp_stop_capture()
{
    return (a2dp_stop_capture ? a2dp_stop_capture() : 0);
}

bool audio_extn_a2dp_set_source_backend_cfg()
{
    return (a2dp_set_source_backend_cfg ?
                a2dp_set_source_backend_cfg() : false);
}

int audio_extn_sco_start_configuration()
{
    return (sco_start_configuration? sco_start_configuration() : 0);
}

void audio_extn_sco_reset_configuration()
{
    return (sco_reset_configuration? sco_reset_configuration() : 0);
}

// END: A2DP_OFFLOAD =====================================================================

// START: HFP ======================================================================
#ifdef __LP64__
#define HFP_LIB_PATH "/vendor/lib64/libhfp.so"
#else
#define HFP_LIB_PATH "/vendor/lib/libhfp.so"
#endif

static void *hfp_lib_handle = NULL;

typedef void (*hfp_init_t)(hfp_init_config_t);
static hfp_init_t hfp_init;

typedef bool (*hfp_is_active_t)(struct audio_device *adev);
static hfp_is_active_t hfp_is_active;

typedef audio_usecase_t (*hfp_get_usecase_t)();
static hfp_get_usecase_t hfp_get_usecase;

typedef int (*hfp_set_mic_mute_t)(struct audio_device *dev, bool state);
static hfp_set_mic_mute_t hfp_set_mic_mute;

typedef void (*hfp_set_parameters_t)(struct audio_device *adev,
                                           struct str_parms *parms);
static hfp_set_parameters_t hfp_set_parameters;

typedef int (*hfp_set_mic_mute2_t)(struct audio_device *adev, bool state);
static hfp_set_mic_mute2_t hfp_set_mic_mute2;

int hfp_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__,
                  is_feature_enabled ? "Enabled" : "NOT Enabled");
    if (is_feature_enabled) {
        // dlopen lib
        hfp_lib_handle = dlopen(HFP_LIB_PATH, RTLD_NOW);

        if (!hfp_lib_handle) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        if (!(hfp_init = (hfp_init_t)dlsym(
                            hfp_lib_handle, "hfp_init")) ||
            !(hfp_is_active =
                 (hfp_is_active_t)dlsym(
                            hfp_lib_handle, "hfp_is_active")) ||
            !(hfp_get_usecase =
                 (hfp_get_usecase_t)dlsym(
                            hfp_lib_handle, "hfp_get_usecase")) ||
            !(hfp_set_mic_mute =
                 (hfp_set_mic_mute_t)dlsym(
                            hfp_lib_handle, "hfp_set_mic_mute")) ||
            !(hfp_set_mic_mute2 =
                 (hfp_set_mic_mute2_t)dlsym(
                            hfp_lib_handle, "hfp_set_mic_mute2")) ||
            !(hfp_set_parameters =
                 (hfp_set_parameters_t)dlsym(
                            hfp_lib_handle, "hfp_set_parameters"))) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }
        hfp_init_config_t init_config;
        init_config.fp_platform_set_mic_mute = platform_set_mic_mute;
        init_config.fp_platform_get_pcm_device_id = platform_get_pcm_device_id;
        init_config.fp_platform_set_echo_reference = platform_set_echo_reference;
        init_config.fp_platform_set_mic_mute = platform_set_mic_mute;
        init_config.fp_select_devices = select_devices;
        init_config.fp_audio_extn_ext_hw_plugin_usecase_start =
                                        audio_extn_ext_hw_plugin_usecase_start;
        init_config.fp_audio_extn_ext_hw_plugin_usecase_stop =
                                        audio_extn_ext_hw_plugin_usecase_stop;
        init_config.fp_get_usecase_from_list = get_usecase_from_list;
        init_config.fp_disable_audio_route = disable_audio_route;
        init_config.fp_disable_snd_device = disable_snd_device;
        init_config.fp_voice_get_mic_mute = voice_get_mic_mute;
        init_config.fp_audio_extn_auto_hal_start_hfp_downlink =
                                        audio_extn_auto_hal_start_hfp_downlink;
        init_config.fp_audio_extn_auto_hal_stop_hfp_downlink =
                                        audio_extn_auto_hal_stop_hfp_downlink;

        hfp_init(init_config);
        ALOGD("%s:: ---- Feature HFP is Enabled ----", __func__);
        return 0;
    }

feature_disabled:
    if (hfp_lib_handle) {
        dlclose(hfp_lib_handle);
        hfp_lib_handle = NULL;
    }

    hfp_init = NULL;
    hfp_is_active = NULL;
    hfp_get_usecase = NULL;
    hfp_set_mic_mute = NULL;
    hfp_set_mic_mute2 = NULL;
    hfp_set_parameters = NULL;

    ALOGW(":: %s: ---- Feature HFP is disabled ----", __func__);
    return -ENOSYS;
}

bool audio_extn_hfp_is_active(struct audio_device *adev)
{
    return ((hfp_is_active) ?
                    hfp_is_active(adev): false);
}

audio_usecase_t audio_extn_hfp_get_usecase()
{
    return ((hfp_get_usecase) ?
                    hfp_get_usecase(): -1);
}

int audio_extn_hfp_set_mic_mute(struct audio_device *adev, bool state)
{
    return ((hfp_set_mic_mute) ?
                    hfp_set_mic_mute(adev, state): -1);
}

void audio_extn_hfp_set_parameters(struct audio_device *adev,
                                           struct str_parms *parms)
{
    ((hfp_set_parameters) ?
                    hfp_set_parameters(adev, parms): NULL);
}

int audio_extn_hfp_set_mic_mute2(struct audio_device *adev, bool state)
{
    return ((hfp_set_mic_mute2) ?
                    hfp_set_mic_mute2(adev, state): -1);
}
// END: HFP ========================================================================

// START: EXT_HW_PLUGIN ===================================================================
#ifdef __LP64__
#define EXT_HW_PLUGIN_LIB_PATH "/vendor/lib64/libexthwplugin.so"
#else
#define EXT_HW_PLUGIN_LIB_PATH "/vendor/lib/libexthwplugin.so"
#endif

static void *ext_hw_plugin_lib_handle = NULL;

typedef void* (*ext_hw_plugin_init_t)(struct audio_device*,
                                        ext_hw_plugin_init_config_t init_config);
static ext_hw_plugin_init_t ext_hw_plugin_init;

typedef int (*ext_hw_plugin_deinit_t)(void*);
static ext_hw_plugin_deinit_t ext_hw_plugin_deinit;

typedef int(*ext_hw_plugin_usecase_start_t)(void*, struct audio_usecase*);
static ext_hw_plugin_usecase_start_t ext_hw_plugin_usecase_start;

typedef int(*ext_hw_plugin_usecase_stop_t)(void*, struct audio_usecase*);
static ext_hw_plugin_usecase_stop_t ext_hw_plugin_usecase_stop;

typedef int(*ext_hw_plugin_set_parameters_t)(void*, struct str_parms*);
static ext_hw_plugin_set_parameters_t ext_hw_plugin_set_parameters;

typedef int(*ext_hw_plugin_get_parameters_t)(void*,
                                        struct str_parms*, struct str_parms*);
static ext_hw_plugin_get_parameters_t ext_hw_plugin_get_parameters;

typedef int(*ext_hw_plugin_set_mic_mute_t)(void*, bool);
static ext_hw_plugin_set_mic_mute_t ext_hw_plugin_set_mic_mute;

typedef int(*ext_hw_plugin_get_mic_mute_t)(void*, bool*);
static ext_hw_plugin_get_mic_mute_t ext_hw_plugin_get_mic_mute;

typedef int(*ext_hw_plugin_set_audio_gain_t)(void*, struct audio_usecase*, uint32_t);
static ext_hw_plugin_set_audio_gain_t ext_hw_plugin_set_audio_gain;


int ext_hw_plugin_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__,
                  is_feature_enabled ? "Enabled" : "NOT Enabled");
    if (is_feature_enabled) {
        // dlopen lib
        ext_hw_plugin_lib_handle = dlopen(EXT_HW_PLUGIN_LIB_PATH, RTLD_NOW);

        if (!ext_hw_plugin_lib_handle) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
    if (!(ext_hw_plugin_init = (ext_hw_plugin_init_t)dlsym(
                            ext_hw_plugin_lib_handle, "ext_hw_plugin_init")) ||
            !(ext_hw_plugin_deinit =
                 (ext_hw_plugin_deinit_t)dlsym(
                            ext_hw_plugin_lib_handle, "ext_hw_plugin_deinit")) ||
            !(ext_hw_plugin_usecase_start =
                 (ext_hw_plugin_usecase_start_t)dlsym(
                            ext_hw_plugin_lib_handle, "ext_hw_plugin_usecase_start")) ||
            !(ext_hw_plugin_usecase_stop =
                 (ext_hw_plugin_usecase_stop_t)dlsym(
                            ext_hw_plugin_lib_handle, "ext_hw_plugin_usecase_stop")) ||
            !(ext_hw_plugin_set_parameters =
                 (ext_hw_plugin_set_parameters_t)dlsym(
                            ext_hw_plugin_lib_handle, "ext_hw_plugin_set_parameters")) ||
            !(ext_hw_plugin_get_parameters =
                 (ext_hw_plugin_get_parameters_t)dlsym(
                            ext_hw_plugin_lib_handle, "ext_hw_plugin_get_parameters")) ||
            !(ext_hw_plugin_set_mic_mute =
                 (ext_hw_plugin_set_mic_mute_t)dlsym(
                            ext_hw_plugin_lib_handle, "ext_hw_plugin_set_mic_mute")) ||
            !(ext_hw_plugin_get_mic_mute =
                 (ext_hw_plugin_get_mic_mute_t)dlsym(
                            ext_hw_plugin_lib_handle, "ext_hw_plugin_get_mic_mute")) ||
            !(ext_hw_plugin_set_audio_gain =
                 (ext_hw_plugin_set_audio_gain_t)dlsym(
                            ext_hw_plugin_lib_handle, "ext_hw_plugin_set_audio_gain"))) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }
        ALOGD("%s:: ---- Feature EXT_HW_PLUGIN is Enabled ----", __func__);
        return 0;
    }

feature_disabled:
    if (ext_hw_plugin_lib_handle) {
        dlclose(ext_hw_plugin_lib_handle);
        ext_hw_plugin_lib_handle = NULL;
    }

    ext_hw_plugin_init = NULL;
    ext_hw_plugin_deinit = NULL;
    ext_hw_plugin_usecase_start = NULL;
    ext_hw_plugin_usecase_stop = NULL;
    ext_hw_plugin_set_parameters = NULL;
    ext_hw_plugin_get_parameters = NULL;
    ext_hw_plugin_set_mic_mute = NULL;
    ext_hw_plugin_get_mic_mute = NULL;
    ext_hw_plugin_set_audio_gain = NULL;

    ALOGW(":: %s: ---- Feature EXT_HW_PLUGIN is disabled ----", __func__);
    return -ENOSYS;
}

void* audio_extn_ext_hw_plugin_init(struct audio_device *adev)
{
    if(ext_hw_plugin_init) {
        ext_hw_plugin_init_config_t ext_hw_plugin_init_config;
        ext_hw_plugin_init_config.fp_b64decode = b64decode;
        ext_hw_plugin_init_config.fp_b64encode = b64encode;
        return ext_hw_plugin_init(adev, ext_hw_plugin_init_config);
    }
    else
        return NULL;
}

int audio_extn_ext_hw_plugin_deinit(void *plugin)
{
    return ((ext_hw_plugin_deinit) ?
                            ext_hw_plugin_deinit(plugin): 0);
}

int audio_extn_ext_hw_plugin_usecase_start(void *plugin, struct audio_usecase *usecase)
{
    return ((ext_hw_plugin_usecase_start) ?
                            ext_hw_plugin_usecase_start(plugin, usecase): 0);
}

int audio_extn_ext_hw_plugin_usecase_stop(void *plugin, struct audio_usecase *usecase)
{
    return ((ext_hw_plugin_usecase_stop) ?
                            ext_hw_plugin_usecase_stop(plugin, usecase): 0);
}

int audio_extn_ext_hw_plugin_set_parameters(void *plugin,
                                           struct str_parms *parms)
{
    return ((ext_hw_plugin_set_parameters) ?
                            ext_hw_plugin_set_parameters(plugin, parms): 0);
}

int audio_extn_ext_hw_plugin_get_parameters(void *plugin,
                  struct str_parms *query, struct str_parms *reply)
{
    return ((ext_hw_plugin_get_parameters) ?
                        ext_hw_plugin_get_parameters(plugin, query, reply): 0);
}

int audio_extn_ext_hw_plugin_set_mic_mute(void *plugin, bool mute)
{
    return ((ext_hw_plugin_set_mic_mute) ?
                        ext_hw_plugin_set_mic_mute(plugin, mute): 0);
}

int audio_extn_ext_hw_plugin_get_mic_mute(void *plugin, bool *mute)
{
    return ((ext_hw_plugin_get_mic_mute) ?
                        ext_hw_plugin_get_mic_mute(plugin, mute): 0);
}

int audio_extn_ext_hw_plugin_set_audio_gain(void *plugin,
            struct audio_usecase *usecase, uint32_t gain)
{
    return ((ext_hw_plugin_set_audio_gain) ?
                        ext_hw_plugin_set_audio_gain(plugin, usecase, gain): 0);
}
// END: EXT_HW_PLUGIN ===================================================================

// START: RECORD_PLAY_CONCURRENCY =======================================================
void record_play_concurency_feature_init(bool is_feature_enabled)
{
    audio_extn_record_play_concurrency_enabled = is_feature_enabled;
    ALOGD("%s: ---- Feature RECORD_PLAY_CONCURRENCY is %s----", __func__,
                                        is_feature_enabled? "ENABLED": "NOT ENABLED");
}

bool audio_extn_is_record_play_concurrency_enabled()
{
    return audio_extn_record_play_concurrency_enabled;
}
// END: RECORD_PLAY_CONCURRENCY =========================================================

// START: HDMI_PASSTHROUGH ==================================================
#ifdef __LP64__
#define HDMI_PASSTHRU_LIB_PATH "/vendor/lib64/libhdmipassthru.so"
#else
#define HDMI_PASSTHRU_LIB_PATH "/vendor/lib/libhdmipassthru.so"
#endif

static void *hdmi_passthru_lib_handle = NULL;

typedef bool (*passthru_is_convert_supported_t)(struct audio_device *,
                                                 struct stream_out *);
static passthru_is_convert_supported_t passthru_is_convert_supported;

typedef bool (*passthru_is_passt_supported_t)(struct stream_out *);
static passthru_is_passt_supported_t passthru_is_passt_supported;

typedef void (*passthru_update_stream_configuration_t)(
        struct audio_device *, struct stream_out *, const void *, size_t);
static passthru_update_stream_configuration_t passthru_update_stream_configuration;

typedef bool (*passthru_is_passthrough_stream_t)(struct stream_out *);
static passthru_is_passthrough_stream_t passthru_is_passthrough_stream;

typedef int (*passthru_get_buffer_size_t)(audio_offload_info_t*);
static passthru_get_buffer_size_t passthru_get_buffer_size;

typedef int (*passthru_set_volume_t)(struct stream_out *, int);
static passthru_set_volume_t passthru_set_volume;

typedef int (*passthru_set_latency_t)(struct stream_out *, int);
static passthru_set_latency_t passthru_set_latency;

typedef bool (*passthru_is_supported_format_t)(audio_format_t);
static passthru_is_supported_format_t passthru_is_supported_format;

typedef bool (*passthru_should_drop_data_t)(struct stream_out * out);
static passthru_should_drop_data_t passthru_should_drop_data;

typedef void (*passthru_on_start_t)(struct stream_out *out);
static passthru_on_start_t passthru_on_start;

typedef void (*passthru_on_stop_t)(struct stream_out *out);
static passthru_on_stop_t passthru_on_stop;

typedef void (*passthru_on_pause_t)(struct stream_out *out);
static passthru_on_pause_t passthru_on_pause;

typedef int (*passthru_set_parameters_t)(struct audio_device *adev,
                                       struct str_parms *parms);
static passthru_set_parameters_t passthru_set_parameters;

typedef bool (*passthru_is_enabled_t)();
static passthru_is_enabled_t passthru_is_enabled;

typedef bool (*passthru_is_active_t)();
static passthru_is_active_t passthru_is_active;

typedef void (*passthru_init_t)(passthru_init_config_t);
static passthru_init_t passthru_init;

typedef bool (*passthru_should_standby_t)(struct stream_out *out);
static passthru_should_standby_t passthru_should_standby;

typedef int (*passthru_get_channel_count_t)(struct stream_out *out);
static passthru_get_channel_count_t passthru_get_channel_count;

typedef int (*passthru_update_dts_stream_configuration_t)(struct stream_out *out,
        const void *buffer, size_t bytes);
static passthru_update_dts_stream_configuration_t passthru_update_dts_stream_configuration;

typedef bool (*passthru_is_direct_passthrough_t)(struct stream_out *out);
static passthru_is_direct_passthrough_t passthru_is_direct_passthrough;

typedef bool (*passthru_is_supported_backend_edid_cfg_t)(struct audio_device *adev,
                                                   struct stream_out *out);
static passthru_is_supported_backend_edid_cfg_t passthru_is_supported_backend_edid_cfg;

bool audio_extn_passthru_is_convert_supported(struct audio_device *adev,
                                                 struct stream_out *out)
{
    return (passthru_is_convert_supported ? passthru_is_convert_supported(adev, out) : false);
}

bool audio_extn_passthru_is_passthrough_stream(struct stream_out *out)
{
    return (passthru_is_passthrough_stream ?
                passthru_is_passthrough_stream(out) : false);
}

void audio_extn_passthru_update_stream_configuration(
        struct audio_device *adev, struct stream_out *out,
        const void *buffer, size_t bytes)
{
    (passthru_update_stream_configuration ?
                passthru_update_stream_configuration(adev, out, buffer, bytes) : 0);
}

bool audio_extn_passthru_is_passt_supported(struct stream_out *out)
{
    return (passthru_is_passt_supported)? passthru_is_passt_supported(out): false;
}

int audio_extn_passthru_get_buffer_size(audio_offload_info_t* info)
{
    return (passthru_get_buffer_size)? passthru_get_buffer_size(info): 0;
}

int audio_extn_passthru_set_volume(struct stream_out *out, int mute)
{
    return (passthru_set_volume)? passthru_set_volume(out, mute): 0;
}

int audio_extn_passthru_set_latency(struct stream_out *out, int latency)
{
    return (passthru_set_latency)? passthru_set_latency(out, latency): 0;
}

bool audio_extn_passthru_is_supported_format(audio_format_t format)
{
    return (passthru_is_supported_format)? passthru_is_supported_format(format): false;
}

bool audio_extn_passthru_should_drop_data(struct stream_out * out)
{
    return (passthru_should_drop_data)? passthru_should_drop_data(out): false;
}

void audio_extn_passthru_on_start(struct stream_out *out)
{
    (passthru_on_start)? passthru_on_start(out): 0;
}

void audio_extn_passthru_on_stop(struct stream_out *out)
{
    (passthru_on_stop)? passthru_on_stop(out): 0;
}

void audio_extn_passthru_on_pause(struct stream_out *out)
{
    (passthru_on_pause)? passthru_on_pause(out): 0;
}

int audio_extn_passthru_set_parameters(struct audio_device *adev,
                                       struct str_parms *parms)
{
    return (passthru_set_parameters)?
                            passthru_set_parameters(adev, parms): false;
}

bool audio_extn_passthru_is_enabled()
{
    return (passthru_is_enabled)? passthru_is_enabled(): false;
}

bool audio_extn_passthru_is_active()
{
     return (passthru_is_active)? passthru_is_active(): false;
}

bool audio_extn_passthru_should_standby(struct stream_out *out)
{
    return (passthru_should_standby)? passthru_should_standby(out): false;
}
int audio_extn_passthru_get_channel_count(struct stream_out *out)
{
    return (passthru_get_channel_count)? passthru_get_channel_count(out): 0;
}

int audio_extn_passthru_update_dts_stream_configuration(struct stream_out *out,
        const void *buffer, size_t bytes)
{
    return (passthru_update_dts_stream_configuration)?
                        passthru_update_dts_stream_configuration(out, buffer, bytes): 0;
}

bool audio_extn_passthru_is_direct_passthrough(struct stream_out *out)
{
    return (passthru_is_direct_passthrough)? passthru_is_direct_passthrough(out): false;
}

bool audio_extn_passthru_is_supported_backend_edid_cfg(struct audio_device *adev,
                                                   struct stream_out *out)
{
    return (passthru_is_supported_backend_edid_cfg)?
                            passthru_is_supported_backend_edid_cfg(adev, out): false;
}
bool audio_extn_is_hdmi_passthru_enabled()
{
    return audio_extn_hdmi_passthru_enabled;
}

void hdmi_passthrough_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__,
                  is_feature_enabled ? "Enabled" : "NOT Enabled");

    audio_extn_hdmi_passthru_enabled = is_feature_enabled;
    if (is_feature_enabled) {
        // dlopen lib
        hdmi_passthru_lib_handle = dlopen(HDMI_PASSTHRU_LIB_PATH, RTLD_NOW);

        if (!hdmi_passthru_lib_handle) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
    if (!(passthru_init = (passthru_init_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_init")) ||
            !(passthru_is_convert_supported =
                 (passthru_is_convert_supported_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_is_convert_supported")) ||
            !(passthru_is_passthrough_stream =
                 (passthru_is_passthrough_stream_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_is_passthrough_stream")) ||
            !(passthru_get_buffer_size =
                 (passthru_get_buffer_size_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_get_buffer_size")) ||
            !(passthru_set_volume =
                 (passthru_set_volume_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_set_volume")) ||
            !(passthru_set_latency =
                 (passthru_set_latency_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_set_latency")) ||
            !(passthru_is_supported_format =
                 (passthru_is_supported_format_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_is_supported_format")) ||
            !(passthru_should_drop_data =
                 (passthru_should_drop_data_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_should_drop_data")) ||
            !(passthru_on_start =
                 (passthru_on_start_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_on_start")) ||
            !(passthru_on_stop =
                 (passthru_on_stop_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_on_stop")) ||
            !(passthru_on_pause =
                 (passthru_on_pause_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_on_pause")) ||
            !(passthru_set_parameters =
                 (passthru_set_parameters_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_set_parameters")) ||
            (passthru_is_enabled =
                 (passthru_is_enabled_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_is_enabled")) ||
            (passthru_is_active =
                 (passthru_is_active_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_is_active")) ||
            (passthru_should_standby =
                 (passthru_should_standby_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_should_standby")) ||
            (passthru_get_channel_count =
                 (passthru_get_channel_count_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_get_channel_count")) ||
            (passthru_update_dts_stream_configuration =
                 (passthru_update_dts_stream_configuration_t)dlsym(
                            hdmi_passthru_lib_handle,
                            "passthru_update_dts_stream_configuration")) ||
            (passthru_is_direct_passthrough =
                 (passthru_is_direct_passthrough_t)dlsym(
                            hdmi_passthru_lib_handle, "passthru_is_direct_passthrough")) ||
            (passthru_is_supported_backend_edid_cfg =
                 (passthru_is_supported_backend_edid_cfg_t)dlsym(
                            hdmi_passthru_lib_handle,
                            "passthru_is_supported_backend_edid_cfg"))) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        passthru_init_config_t init_config;
        init_config.fp_platform_is_edid_supported_format =
                                                    platform_is_edid_supported_format;
        init_config.fp_platform_set_device_params = platform_set_device_params;
        init_config.fp_platform_edid_get_max_channels = platform_edid_get_max_channels;
        init_config.fp_platform_get_output_snd_device = platform_get_output_snd_device;
        init_config.fp_platform_get_codec_backend_cfg = platform_get_codec_backend_cfg;
        init_config.fp_platform_get_snd_device_name = platform_get_snd_device_name;
        init_config.fp_platform_is_edid_supported_sample_rate =
                                                platform_is_edid_supported_sample_rate;
        init_config.fp_audio_extn_keep_alive_start = audio_extn_keep_alive_start;
        init_config.fp_audio_extn_keep_alive_stop = audio_extn_keep_alive_stop;
        init_config.fp_audio_extn_utils_is_dolby_format =
                                                    audio_extn_utils_is_dolby_format;
        passthru_init(init_config);
        ALOGD("%s:: ---- Feature HDMI_PASSTHROUGH is Enabled ----", __func__);
        return;
    }

feature_disabled:
    if (hdmi_passthru_lib_handle) {
        dlclose(hdmi_passthru_lib_handle);
        hdmi_passthru_lib_handle = NULL;
    }

    passthru_init = NULL;
    passthru_is_convert_supported = NULL;
    passthru_is_passthrough_stream = NULL;
    passthru_get_buffer_size = NULL;
    passthru_set_volume = NULL;
    passthru_set_latency = NULL;
    passthru_is_supported_format = NULL;
    passthru_should_drop_data = NULL;
    passthru_on_start = NULL;
    passthru_on_stop = NULL;
    passthru_on_pause = NULL;
    passthru_set_parameters = NULL;
    passthru_is_enabled = NULL;
    passthru_is_active = NULL;
    passthru_should_standby = NULL;
    passthru_get_channel_count = NULL;
    passthru_update_dts_stream_configuration = NULL;
    passthru_is_direct_passthrough = NULL;
    passthru_is_supported_backend_edid_cfg = NULL;

    ALOGW(":: %s: ---- Feature HDMI_PASSTHROUGH is disabled ----", __func__);
}
// END: HDMI_PASSTHROUGH ==================================================

// START: CONCURRENT_CAPTURE ==================================================
bool audio_extn_is_concurrent_capture_enabled()
{
    return audio_extn_concurrent_capture_enabled;
}

void concurrent_capture_feature_init(bool is_feature_enabled)
{
    audio_extn_concurrent_capture_enabled = is_feature_enabled;
    ALOGD("%s: ---- Feature CONCURRENT_CAPTURE is %s----", __func__, is_feature_enabled? "ENABLED": "NOT ENABLED");
}
// END: CONCURRENT_CAPTURE ====================================================

// START: COMPRESS_IN ==================================================
void compress_in_feature_init(bool is_feature_enabled)
{
    audio_extn_compress_in_enabled = is_feature_enabled;
    ALOGD("%s: ---- Feature COMPRESS_IN is %s----", __func__, is_feature_enabled? "ENABLED": "NOT ENABLED");
}

bool audio_extn_cin_applicable_stream(struct stream_in *in)
{
    return (audio_extn_compress_in_enabled? cin_applicable_stream(in): false);
}
bool audio_extn_cin_attached_usecase(struct stream_in *in)
{
    return (audio_extn_compress_in_enabled? cin_attached_usecase(in): false);
}
bool audio_extn_cin_format_supported(audio_format_t format)
{
    return (audio_extn_compress_in_enabled? cin_format_supported(format): false);
}
int audio_extn_cin_acquire_usecase(struct stream_in *in)
{
    return (audio_extn_compress_in_enabled? cin_acquire_usecase(in): 0);
}
size_t audio_extn_cin_get_buffer_size(struct stream_in *in)
{
    return (audio_extn_compress_in_enabled? cin_get_buffer_size(in): 0);
}
int audio_extn_cin_open_input_stream(struct stream_in *in)
{
    return (audio_extn_compress_in_enabled? cin_open_input_stream(in): -1);
}
void audio_extn_cin_stop_input_stream(struct stream_in *in)
{
    (audio_extn_compress_in_enabled? cin_stop_input_stream(in): NULL);
}
void audio_extn_cin_close_input_stream(struct stream_in *in)
{
    (audio_extn_compress_in_enabled? cin_close_input_stream(in): NULL);
}
void audio_extn_cin_free_input_stream_resources(struct stream_in *in)
{
    return (audio_extn_compress_in_enabled? cin_free_input_stream_resources(in): NULL);
}
int audio_extn_cin_read(struct stream_in *in, void *buffer,
                        size_t bytes, size_t *bytes_read)
{
    return (audio_extn_compress_in_enabled?
                            cin_read(in, buffer, bytes, bytes_read): -1);
}
int audio_extn_cin_configure_input_stream(struct stream_in *in, struct audio_config *in_config)
{
    return (audio_extn_compress_in_enabled? cin_configure_input_stream(in, in_config): -1);
}
// END: COMPRESS_IN ====================================================

// START: BATTERY_LISTENER ==================================================
#ifdef __LP64__
#define BATTERY_LISTENER_LIB_PATH "/vendor/lib64/libbatterylistener.so"
#else
#define BATTERY_LISTENER_LIB_PATH "/vendor/lib/libbatterylistener.so"
#endif

static void *batt_listener_lib_handle = NULL;

typedef void (*batt_listener_init_t)(battery_status_change_fn_t);
static batt_listener_init_t batt_listener_init;

typedef void (*batt_listener_deinit_t)(void);
static batt_listener_deinit_t batt_listener_deinit;

typedef bool (*batt_prop_is_charging_t)(void);
static batt_prop_is_charging_t batt_prop_is_charging;

void battery_listener_feature_init(bool is_feature_enabled)
{
    audio_extn_battery_listener_enabled = is_feature_enabled;
    if (is_feature_enabled) {
        // dlopen lib
        batt_listener_lib_handle = dlopen(BATTERY_LISTENER_LIB_PATH, RTLD_NOW);

        if (!batt_listener_lib_handle) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        if (!(batt_listener_init = (batt_listener_init_t)dlsym(
                            batt_listener_lib_handle, "battery_properties_listener_init")) ||
                !(batt_listener_deinit =
                     (batt_listener_deinit_t)dlsym(
                        batt_listener_lib_handle, "battery_properties_listener_deinit")) ||
                !(batt_prop_is_charging =
                     (batt_prop_is_charging_t)dlsym(
                        batt_listener_lib_handle, "battery_properties_is_charging"))) {
             ALOGE("%s: dlsym failed", __func__);
                goto feature_disabled;
        }
        ALOGD("%s: ---- Feature BATTERY_LISTENER is enabled ----", __func__);
        return;
    }

    feature_disabled:
    if (batt_listener_lib_handle) {
        dlclose(batt_listener_lib_handle);
        batt_listener_lib_handle = NULL;
    }

    batt_listener_init = NULL;
    batt_listener_deinit = NULL;
    batt_prop_is_charging = NULL;
    ALOGW(":: %s: ---- Feature BATTERY_LISTENER is disabled ----", __func__);
}

void audio_extn_battery_properties_listener_init(battery_status_change_fn_t fn)
{
    if(batt_listener_init)
        batt_listener_init(fn);
}
void audio_extn_battery_properties_listener_deinit()
{
    if(batt_listener_deinit)
        batt_listener_deinit();
}
bool audio_extn_battery_properties_is_charging()
{
    return (batt_prop_is_charging)? batt_prop_is_charging(): false;
}
// END: BATTERY_LISTENER ================================================================

// START: HiFi Filter Feature ============================================================
void audio_extn_enable_hifi_filter(struct audio_device *adev, bool value)
{
    const char *mixer_ctl_name = "HiFi Filter";
    struct mixer_ctl *ctl = NULL;
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s, using default control",
              __func__, mixer_ctl_name);
        return;
    } else {
        mixer_ctl_set_value(ctl, 0, value);
        ALOGD("%s: mixer_value set %d", __func__, value);
    }
    return;
}

void audio_extn_hifi_filter_set_params(struct str_parms *parms,
                                        char *value, int len)
{
    int ret = 0;
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_HIFI_AUDIO_FILTER, value,len);
    if (ret >= 0) {
        if (value && !strncmp(value, "true", sizeof("true")))
            audio_extn_hifi_filter_enabled = true;
        else
            audio_extn_hifi_filter_enabled = false;
        str_parms_del(parms, AUDIO_PARAMETER_KEY_HIFI_AUDIO_FILTER);
    }
}

bool audio_extn_hifi_check_usecase_params(int sample_rate, int usecase)
{
    if (sample_rate != 48000 && sample_rate != 44100)
        return false;
    if (usecase == USECASE_AUDIO_PLAYBACK_LOW_LATENCY || usecase == USECASE_AUDIO_PLAYBACK_ULL)
        return false;
    return true;
}

bool audio_extn_is_hifi_filter_enabled(struct audio_device* adev, struct stream_out *out,
                                   snd_device_t snd_device, char *codec_variant,
                                   int channels, int usecase_init)
{
    int na_mode = platform_get_native_support();
    struct audio_usecase *uc = NULL;
    struct listnode *node = NULL;
    bool hifi_active = false;

    if (audio_extn_hifi_filter_enabled) {
        /*Restricting the feature for Tavil and WCD9375 codecs only*/
        if ((strstr(codec_variant, "WCD9385") || strstr(codec_variant, "WCD9375"))
            && (na_mode == NATIVE_AUDIO_MODE_MULTIPLE_MIX_IN_DSP || na_mode ==
                       NATIVE_AUDIO_MODE_TRUE_44_1) && channels <=2) {
            /*Upsampling 8 time should be restricited to headphones playback only */
            if (snd_device == SND_DEVICE_OUT_HEADPHONES
                || snd_device == SND_DEVICE_OUT_HEADPHONES_44_1
                || snd_device == SND_DEVICE_OUT_HEADPHONES_HIFI_FILTER
                || usecase_init) {
                if (audio_extn_hifi_check_usecase_params(out->sample_rate,
                    out->usecase) && !usecase_init)
                    return true;

                list_for_each(node, &adev->usecase_list) {
                    /* checking if hifi_filter is already active to set */
                    /* concurrent playback sessions with hifi_filter enabled*/
                    uc = node_to_item(node, struct audio_usecase, list);
                    struct stream_out *curr_out = (struct stream_out*) uc->stream.out;
                    if (uc->type == PCM_PLAYBACK && curr_out
                        && audio_extn_hifi_check_usecase_params(
                        curr_out->sample_rate, curr_out->usecase) &&
                        (curr_out->channel_mask == AUDIO_CHANNEL_OUT_STEREO ||
                        curr_out->channel_mask == AUDIO_CHANNEL_OUT_MONO))
                            hifi_active = true;
                }
            }
        }
    }
    return hifi_active;
}
// END: HiFi Filter Feature ==============================================================

// START: AUDIOZOOM_FEATURE =====================================================================
#ifdef __LP64__
#define AUDIOZOOM_LIB_PATH "/vendor/lib64/libaudiozoom.so"
#else
#define AUDIOZOOM_LIB_PATH "/vendor/lib/libaudiozoom.so"
#endif

static void *audiozoom_lib_handle = NULL;

typedef int (*audiozoom_init_t)(audiozoom_init_config_t);
static audiozoom_init_t audiozoom_init;

typedef int (*audiozoom_set_microphone_direction_t)(struct stream_in *,
                                                    audio_microphone_direction_t);
static audiozoom_set_microphone_direction_t audiozoom_set_microphone_direction;

typedef int (*audiozoom_set_microphone_field_dimension_t)(struct stream_in *, float);
static audiozoom_set_microphone_field_dimension_t audiozoom_set_microphone_field_dimension;

int audiozoom_feature_init(bool is_feature_enabled)
{
    audio_extn_audiozoom_enabled = is_feature_enabled;
    ALOGD("%s: Called with feature %s", __func__,
                  is_feature_enabled ? "Enabled" : "NOT Enabled");
    if (is_feature_enabled) {
        // dlopen lib
        audiozoom_lib_handle = dlopen(AUDIOZOOM_LIB_PATH, RTLD_NOW);

        if (!audiozoom_lib_handle) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }

        if (!(audiozoom_init =
                    (audiozoom_init_t)dlsym(audiozoom_lib_handle, "audiozoom_init")) ||
            !(audiozoom_set_microphone_direction =
                 (audiozoom_set_microphone_direction_t)dlsym(audiozoom_lib_handle,
                                              "audiozoom_set_microphone_direction")) ||
            !(audiozoom_set_microphone_field_dimension =
                 (audiozoom_set_microphone_field_dimension_t)dlsym(audiozoom_lib_handle,
                                        "audiozoom_set_microphone_field_dimension"))) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }

        ALOGD("%s:: ---- Feature AUDIOZOOM is Enabled ----", __func__);
        return 0;
    }
feature_disabled:
    if (audiozoom_lib_handle) {
        dlclose(audiozoom_lib_handle);
        audiozoom_lib_handle = NULL;
    }

    audiozoom_init = NULL;
    audiozoom_set_microphone_direction = NULL;
    audiozoom_set_microphone_field_dimension = NULL;
    ALOGW(":: %s: ---- Feature AUDIOZOOM is disabled ----", __func__);
    return -ENOSYS;
}

bool audio_extn_is_audiozoom_enabled()
{
    return audio_extn_audiozoom_enabled;
}

int audio_extn_audiozoom_init()
{
     int ret_val = 0;
     if (audiozoom_init) {
        audiozoom_init_config_t init_config;
        init_config.fp_platform_set_parameters = platform_set_parameters;
        ret_val = audiozoom_init(init_config);
     }

     return ret_val;
}

int audio_extn_audiozoom_set_microphone_direction(struct stream_in *stream,
                                           audio_microphone_direction_t dir)
{
     int ret_val = -ENOSYS;
     if (audiozoom_set_microphone_direction)
        ret_val = audiozoom_set_microphone_direction(stream, dir);

     return ret_val;
}

int audio_extn_audiozoom_set_microphone_field_dimension(struct stream_in *stream,
                                                         float zoom)
{
    int ret_val = -ENOSYS;
    if (audiozoom_set_microphone_field_dimension)
        ret_val = audiozoom_set_microphone_field_dimension(stream, zoom);

    return ret_val;
}
// END:   AUDIOZOOM_FEATURE =====================================================================

// START: MAXX_AUDIO =====================================================================
#ifdef __LP64__
#define MAXX_AUDIO_LIB_PATH "/vendor/lib64/libmaxxaudio.so"
#else
#define MAXX_AUDIO_LIB_PATH "/vendor/lib/libmaxxaudio.so"
#endif

static void *maxxaudio_lib_handle = NULL;

typedef void (*maxxaudio_init_t)(void *, maxx_audio_init_config_t);
static maxxaudio_init_t maxxaudio_init;

typedef void (*maxxaudio_deinit_t)();
static maxxaudio_deinit_t maxxaudio_deinit;

typedef bool (*maxxaudio_set_state_t)(struct audio_device*, int,
                             float, bool);
static maxxaudio_set_state_t maxxaudio_set_state;

typedef void (*maxxaudio_set_device_t)(struct audio_usecase *);
static maxxaudio_set_device_t maxxaudio_set_device;

typedef void (*maxxaudio_set_parameters_t)(struct audio_device *,
                                  struct str_parms *);
static maxxaudio_set_parameters_t maxxaudio_set_parameters;

typedef void (*maxxaudio_get_parameters_t)(struct audio_device *,
                                  struct str_parms *,
                                  struct str_parms *);
static maxxaudio_get_parameters_t maxxaudio_get_parameters;

typedef bool (*maxxaudio_supported_usb_t)();
static maxxaudio_supported_usb_t maxxaudio_supported_usb;

int maxx_audio_feature_init(bool is_feature_enabled)
{
    audio_extn_maxx_audio_enabled = is_feature_enabled;
    ALOGD("%s: Called with feature %s", __func__,
                  is_feature_enabled ? "Enabled" : "NOT Enabled");
    if (is_feature_enabled) {
        // dlopen lib
        maxxaudio_lib_handle = dlopen(MAXX_AUDIO_LIB_PATH, RTLD_NOW);

        if (!maxxaudio_lib_handle) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }

        if (!(maxxaudio_init =
                    (maxxaudio_init_t)dlsym(maxxaudio_lib_handle, "ma_init")) ||
            !(maxxaudio_deinit =
                 (maxxaudio_deinit_t)dlsym(maxxaudio_lib_handle, "ma_deinit")) ||
            !(maxxaudio_set_state =
                 (maxxaudio_set_state_t)dlsym(maxxaudio_lib_handle, "ma_set_state")) ||
            !(maxxaudio_set_device =
                 (maxxaudio_set_device_t)dlsym(maxxaudio_lib_handle, "ma_set_device")) ||
            !(maxxaudio_set_parameters =
                 (maxxaudio_set_parameters_t)dlsym(maxxaudio_lib_handle, "ma_set_parameters")) ||
            !(maxxaudio_get_parameters =
                 (maxxaudio_get_parameters_t)dlsym(maxxaudio_lib_handle, "ma_get_parameters")) ||
            !(maxxaudio_supported_usb =
                 (maxxaudio_supported_usb_t)dlsym(
                                    maxxaudio_lib_handle, "ma_supported_usb"))) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }
        ALOGD("%s:: ---- Feature MAXX_AUDIO is Enabled ----", __func__);
        return 0;
    }

feature_disabled:
    if (maxxaudio_lib_handle) {
        dlclose(maxxaudio_lib_handle);
        maxxaudio_lib_handle = NULL;
    }

    maxxaudio_init = NULL;
    maxxaudio_deinit = NULL;
    maxxaudio_set_state = NULL;
    maxxaudio_set_device = NULL;
    maxxaudio_set_parameters = NULL;
    maxxaudio_get_parameters = NULL;
    maxxaudio_supported_usb = NULL;
    ALOGW(":: %s: ---- Feature MAXX_AUDIO is disabled ----", __func__);
    return -ENOSYS;
}

bool audio_extn_is_maxx_audio_enabled()
{
    return audio_extn_maxx_audio_enabled;
}

void audio_extn_ma_init(void *platform)
{

     if (maxxaudio_init) {
        maxx_audio_init_config_t init_config;
        init_config.fp_platform_set_parameters = platform_set_parameters;
        init_config.fp_audio_extn_get_snd_card_split = audio_extn_get_snd_card_split;
        maxxaudio_init(platform, init_config);
     }
}

void audio_extn_ma_deinit()
{
     if (maxxaudio_deinit)
        maxxaudio_deinit();
}

bool audio_extn_ma_set_state(struct audio_device *adev, int stream_type,
                             float vol, bool active)
{
    return (maxxaudio_set_state ?
                maxxaudio_set_state(adev, stream_type, vol, active): false);
}

void audio_extn_ma_set_device(struct audio_usecase *usecase)
{
    if (maxxaudio_set_device)
        maxxaudio_set_device(usecase);
}

void audio_extn_ma_set_parameters(struct audio_device *adev,
                                  struct str_parms *parms)
{
    if (maxxaudio_set_parameters)
        maxxaudio_set_parameters(adev, parms);
}

void audio_extn_ma_get_parameters(struct audio_device *adev,
                                  struct str_parms *query,
                                  struct str_parms *reply)
{
    if (maxxaudio_get_parameters)
        maxxaudio_get_parameters(adev, query, reply);
}

bool audio_extn_ma_supported_usb()
{
    return (maxxaudio_supported_usb ? maxxaudio_supported_usb(): false);
}
// END: MAXX_AUDIO =====================================================================

// START: AUTO_HAL ===================================================================
#ifdef __LP64__
#define AUTO_HAL_LIB_PATH "/vendor/lib64/libautohal.so"
#else
#define AUTO_HAL_LIB_PATH "/vendor/lib/libautohal.so"
#endif

static void *auto_hal_lib_handle = NULL;

typedef int (*auto_hal_init_t)(struct audio_device*,
                                auto_hal_init_config_t);
static auto_hal_init_t auto_hal_init;

typedef void (*auto_hal_deinit_t)();
static auto_hal_deinit_t auto_hal_deinit;

typedef int (*auto_hal_create_audio_patch_t)(struct audio_hw_device*,
                                unsigned int,
                                const struct audio_port_config*,
                                unsigned int,
                                const struct audio_port_config*,
                                audio_patch_handle_t*);
static auto_hal_create_audio_patch_t auto_hal_create_audio_patch;

typedef int (*auto_hal_release_audio_patch_t)(struct audio_hw_device*,
                                audio_patch_handle_t);
static auto_hal_release_audio_patch_t auto_hal_release_audio_patch;

typedef int (*auto_hal_get_car_audio_stream_from_address_t)(const char*);
static auto_hal_get_car_audio_stream_from_address_t auto_hal_get_car_audio_stream_from_address;

typedef int (*auto_hal_open_output_stream_t)(struct stream_out*);
static auto_hal_open_output_stream_t auto_hal_open_output_stream;

typedef bool (*auto_hal_is_bus_device_usecase_t)(audio_usecase_t);
static auto_hal_is_bus_device_usecase_t auto_hal_is_bus_device_usecase;

typedef int (*auto_hal_get_audio_port_t)(struct audio_hw_device*,
                                struct audio_port*);
static auto_hal_get_audio_port_t auto_hal_get_audio_port;

typedef int (*auto_hal_set_audio_port_config_t)(struct audio_hw_device*,
                                const struct audio_port_config*);
static auto_hal_set_audio_port_config_t auto_hal_set_audio_port_config;

typedef void (*auto_hal_set_parameters_t)(struct audio_device*,
                                struct str_parms*);
static auto_hal_set_parameters_t auto_hal_set_parameters;

typedef int (*auto_hal_start_hfp_downlink_t)(struct audio_device*,
                                struct audio_usecase*);
static auto_hal_start_hfp_downlink_t auto_hal_start_hfp_downlink;

typedef int (*auto_hal_stop_hfp_downlink_t)(struct audio_device*,
                                struct audio_usecase*);
static auto_hal_stop_hfp_downlink_t auto_hal_stop_hfp_downlink;

typedef snd_device_t (*auto_hal_get_input_snd_device_t)(struct audio_device*,
                                audio_usecase_t);
static auto_hal_get_input_snd_device_t auto_hal_get_input_snd_device;

typedef snd_device_t (*auto_hal_get_output_snd_device_t)(struct audio_device*,
                                audio_usecase_t);
static auto_hal_get_output_snd_device_t auto_hal_get_output_snd_device;

int auto_hal_feature_init(bool is_feature_enabled)
{
    ALOGD("%s: Called with feature %s", __func__,
                  is_feature_enabled ? "Enabled" : "NOT Enabled");
    if (is_feature_enabled) {
        // dlopen lib
        auto_hal_lib_handle = dlopen(AUTO_HAL_LIB_PATH, RTLD_NOW);

        if (!auto_hal_lib_handle) {
            ALOGE("%s: dlopen failed", __func__);
            goto feature_disabled;
        }
        if (!(auto_hal_init = (auto_hal_init_t)dlsym(
                            auto_hal_lib_handle, "auto_hal_init")) ||
            !(auto_hal_deinit =
                 (auto_hal_deinit_t)dlsym(
                            auto_hal_lib_handle, "auto_hal_deinit")) ||
            !(auto_hal_create_audio_patch =
                 (auto_hal_create_audio_patch_t)dlsym(
                            auto_hal_lib_handle, "auto_hal_create_audio_patch")) ||
            !(auto_hal_release_audio_patch =
                 (auto_hal_release_audio_patch_t)dlsym(
                            auto_hal_lib_handle, "auto_hal_release_audio_patch")) ||
            !(auto_hal_get_car_audio_stream_from_address =
                 (auto_hal_get_car_audio_stream_from_address_t)dlsym(
                            auto_hal_lib_handle, "auto_hal_get_car_audio_stream_from_address")) ||
            !(auto_hal_open_output_stream =
                 (auto_hal_open_output_stream_t)dlsym(
                            auto_hal_lib_handle, "auto_hal_open_output_stream")) ||
            !(auto_hal_is_bus_device_usecase =
                 (auto_hal_is_bus_device_usecase_t)dlsym(
                            auto_hal_lib_handle, "auto_hal_is_bus_device_usecase")) ||
            !(auto_hal_get_audio_port =
                 (auto_hal_get_audio_port_t)dlsym(
                            auto_hal_lib_handle, "auto_hal_get_audio_port")) ||
            !(auto_hal_set_audio_port_config =
                 (auto_hal_set_audio_port_config_t)dlsym(
                            auto_hal_lib_handle, "auto_hal_set_audio_port_config")) ||
            !(auto_hal_set_parameters =
                 (auto_hal_set_parameters_t)dlsym(
                            auto_hal_lib_handle, "auto_hal_set_parameters")) ||
            !(auto_hal_start_hfp_downlink =
                 (auto_hal_start_hfp_downlink_t)dlsym(
                            auto_hal_lib_handle, "auto_hal_start_hfp_downlink")) ||
            !(auto_hal_stop_hfp_downlink =
                 (auto_hal_stop_hfp_downlink_t)dlsym(
                            auto_hal_lib_handle, "auto_hal_stop_hfp_downlink")) ||
            !(auto_hal_get_input_snd_device =
                 (auto_hal_get_input_snd_device_t)dlsym(
                            auto_hal_lib_handle, "auto_hal_get_input_snd_device")) ||
            !(auto_hal_get_output_snd_device =
                 (auto_hal_get_output_snd_device_t)dlsym(
                            auto_hal_lib_handle, "auto_hal_get_output_snd_device"))) {
            ALOGE("%s: dlsym failed", __func__);
            goto feature_disabled;
        }
        ALOGD("%s:: ---- Feature AUTO_HAL is Enabled ----", __func__);
        return 0;
    }

feature_disabled:
    if (auto_hal_lib_handle) {
        dlclose(auto_hal_lib_handle);
        auto_hal_lib_handle = NULL;
    }

    auto_hal_init = NULL;
    auto_hal_deinit = NULL;
    auto_hal_create_audio_patch = NULL;
    auto_hal_release_audio_patch = NULL;
    auto_hal_get_car_audio_stream_from_address = NULL;
    auto_hal_open_output_stream = NULL;
    auto_hal_is_bus_device_usecase = NULL;
    auto_hal_get_audio_port = NULL;
    auto_hal_set_audio_port_config = NULL;
    auto_hal_set_parameters = NULL;
    auto_hal_start_hfp_downlink = NULL;
    auto_hal_stop_hfp_downlink = NULL;
    auto_hal_get_input_snd_device = NULL;
    auto_hal_get_output_snd_device = NULL;

    ALOGW(":: %s: ---- Feature AUTO_HAL is disabled ----", __func__);
    return -ENOSYS;
}

int audio_extn_auto_hal_init(struct audio_device *adev)
{
    if(auto_hal_init) {
        auto_hal_init_config_t auto_hal_init_config;
        auto_hal_init_config.fp_in_get_stream = in_get_stream;
        auto_hal_init_config.fp_out_get_stream = out_get_stream;
        auto_hal_init_config.fp_audio_extn_ext_hw_plugin_usecase_start = audio_extn_ext_hw_plugin_usecase_start;
        auto_hal_init_config.fp_audio_extn_ext_hw_plugin_usecase_stop = audio_extn_ext_hw_plugin_usecase_stop;
        auto_hal_init_config.fp_get_usecase_from_list = get_usecase_from_list;
        auto_hal_init_config.fp_get_output_period_size = get_output_period_size;
        auto_hal_init_config.fp_audio_extn_ext_hw_plugin_set_audio_gain = audio_extn_ext_hw_plugin_set_audio_gain;
        auto_hal_init_config.fp_select_devices = select_devices;
        auto_hal_init_config.fp_disable_audio_route = disable_audio_route;
        auto_hal_init_config.fp_disable_snd_device = disable_snd_device;
        auto_hal_init_config.fp_adev_get_active_input = adev_get_active_input;
        auto_hal_init_config.fp_platform_set_echo_reference = platform_set_echo_reference;
        auto_hal_init_config.fp_platform_get_eccarstate = platform_get_eccarstate;
        auto_hal_init_config.fp_generate_patch_handle = generate_patch_handle;
        return auto_hal_init(adev, auto_hal_init_config);
    }
    else
        return 0;
}

void audio_extn_auto_hal_deinit()
{
    if (auto_hal_deinit)
        auto_hal_deinit();
}

int audio_extn_auto_hal_create_audio_patch(struct audio_hw_device *dev,
                                unsigned int num_sources,
                                const struct audio_port_config *sources,
                                unsigned int num_sinks,
                                const struct audio_port_config *sinks,
                                audio_patch_handle_t *handle)
{
    return ((auto_hal_create_audio_patch) ?
                            auto_hal_create_audio_patch(dev,
                                num_sources,
                                sources,
                                num_sinks,
                                sinks,
                                handle): 0);
}

int audio_extn_auto_hal_release_audio_patch(struct audio_hw_device *dev,
                                audio_patch_handle_t handle)
{
    return ((auto_hal_release_audio_patch) ?
                            auto_hal_release_audio_patch(dev, handle): 0);
}

int audio_extn_auto_hal_get_car_audio_stream_from_address(const char *address)
{
    return ((auto_hal_get_car_audio_stream_from_address) ?
                            auto_hal_get_car_audio_stream_from_address(address): -ENOSYS);
}

int audio_extn_auto_hal_open_output_stream(struct stream_out *out)
{
    return ((auto_hal_open_output_stream) ?
                            auto_hal_open_output_stream(out): -ENOSYS);
}

bool audio_extn_auto_hal_is_bus_device_usecase(audio_usecase_t uc_id)
{
    return ((auto_hal_is_bus_device_usecase) ?
                            auto_hal_is_bus_device_usecase(uc_id): false);
}

int audio_extn_auto_hal_get_audio_port(struct audio_hw_device *dev,
                                struct audio_port *config)
{
    return ((auto_hal_get_audio_port) ?
                            auto_hal_get_audio_port(dev, config): 0);
}

int audio_extn_auto_hal_set_audio_port_config(struct audio_hw_device *dev,
                                const struct audio_port_config *config)
{
    return ((auto_hal_set_audio_port_config) ?
                            auto_hal_set_audio_port_config(dev, config): 0);
}

void audio_extn_auto_hal_set_parameters(struct audio_device *adev,
                                        struct str_parms *parms)
{
    if (auto_hal_set_parameters)
        auto_hal_set_parameters(adev, parms);
}

int audio_extn_auto_hal_start_hfp_downlink(struct audio_device *adev,
                                struct audio_usecase *uc_info)
{
    return ((auto_hal_start_hfp_downlink) ?
                            auto_hal_start_hfp_downlink(adev, uc_info): 0);
}

int audio_extn_auto_hal_stop_hfp_downlink(struct audio_device *adev,
                                struct audio_usecase *uc_info)
{
    return ((auto_hal_stop_hfp_downlink) ?
                            auto_hal_stop_hfp_downlink(adev, uc_info): 0);
}

snd_device_t audio_extn_auto_hal_get_input_snd_device(struct audio_device *adev,
                                audio_usecase_t uc_id)
{
    return ((auto_hal_get_input_snd_device) ?
                            auto_hal_get_input_snd_device(adev, uc_id): SND_DEVICE_NONE);
}

snd_device_t audio_extn_auto_hal_get_output_snd_device(struct audio_device *adev,
                                audio_usecase_t uc_id)
{
    return ((auto_hal_get_output_snd_device) ?
                            auto_hal_get_output_snd_device(adev, uc_id): SND_DEVICE_NONE);
}
// END: AUTO_HAL ===================================================================

void audio_extn_feature_init()
{
    vendor_enhanced_info = audio_extn_utils_get_vendor_enhanced_info();

    // register feature init functions here
    // each feature needs a vendor property
    // default value added is for GSI (non vendor modified images)
    snd_mon_feature_init(
        property_get_bool("vendor.audio.feature.snd_mon.enable",
                           false));
    compr_cap_feature_init(
        property_get_bool("vendor.audio.feature.compr_cap.enable",
                           false));
    dsm_feedback_feature_init(
        property_get_bool("vendor.audio.feature.dsm_feedback.enable",
                           false));
    ssrec_feature_init(
        property_get_bool("vendor.audio.feature.ssrec.enable",
                           false));
    src_trkn_feature_init(
        property_get_bool("vendor.audio.feature.src_trkn.enable",
                           false));
    hdmi_edid_feature_init(
        property_get_bool("vendor.audio.feature.hdmi_edid.enable",
                           false));
    keep_alive_feature_init(
        property_get_bool("vendor.audio.feature.keep_alive.enable",
                           false));
    hifi_audio_feature_init(
        property_get_bool("vendor.audio.feature.hifi_audio.enable",
                           false));
    ras_feature_init(
        property_get_bool("vendor.audio.feature.ras.enable",
                           false));
    kpi_optimize_feature_init(
        property_get_bool("vendor.audio.feature.kpi_optimize.enable",
                           false));
    usb_offload_feature_init(
        property_get_bool("vendor.audio.feature.usb_offload.enable",
                           false));
    usb_offload_burst_mode_feature_init(
        property_get_bool("vendor.audio.feature.usb_offload_burst_mode.enable",
                           false));
    usb_offload_sidetone_volume_feature_init(
        property_get_bool("vendor.audio.feature.usb_offload_sidetone_volume.enable",
                           false));
    a2dp_offload_feature_init(
        property_get_bool("vendor.audio.feature.a2dp_offload.enable",
                           false));
    wsa_feature_init(
        property_get_bool("vendor.audio.feature.wsa.enable",
                           false));
    compress_meta_data_feature_init(
        property_get_bool("vendor.audio.feature.compress_meta_data.enable",
                           false));
    vbat_feature_init(
        property_get_bool("vendor.audio.feature.vbat.enable",
                           false));
    display_port_feature_init(
        property_get_bool("vendor.audio.feature.display_port.enable",
                           false));
    fluence_feature_init(
        property_get_bool("vendor.audio.feature.fluence.enable",
                           false));
    custom_stereo_feature_init(
        property_get_bool("vendor.audio.feature.custom_stereo.enable",
                           false));
    anc_headset_feature_init(
        property_get_bool("vendor.audio.feature.anc_headset.enable",
                           false));
    spkr_prot_feature_init(
        property_get_bool("vendor.audio.feature.spkr_prot.enable",
                           false));
    fm_feature_init(
        property_get_bool("vendor.audio.feature.fm.enable",
                           false));
    external_qdsp_feature_init(
        property_get_bool("vendor.audio.feature.external_dsp.enable",
                           false));
    external_speaker_feature_init(
        property_get_bool("vendor.audio.feature.external_speaker.enable",
                           false));
    external_speaker_tfa_feature_init(
        property_get_bool("vendor.audio.feature.external_speaker_tfa.enable",
                           false));
    hwdep_cal_feature_init(
        property_get_bool("vendor.audio.feature.hwdep_cal.enable",
                           false));
    hfp_feature_init(
        property_get_bool("vendor.audio.feature.hfp.enable",
                           false));
    ext_hw_plugin_feature_init(
        property_get_bool("vendor.audio.feature.ext_hw_plugin.enable",
                           false));
    record_play_concurency_feature_init(
        property_get_bool("vendor.audio.feature.record_play_concurency.enable",
                           false));
    hdmi_passthrough_feature_init(
        property_get_bool("vendor.audio.feature.hdmi_passthrough.enable",
                           false));
    concurrent_capture_feature_init(
        property_get_bool("vendor.audio.feature.concurrent_capture.enable",
                           false));
    compress_in_feature_init(
        property_get_bool("vendor.audio.feature.compress_in.enable",
                           false));
    battery_listener_feature_init(
        property_get_bool("vendor.audio.feature.battery_listener.enable",
                           false));
    maxx_audio_feature_init(
        property_get_bool("vendor.audio.feature.maxx_audio.enable",
                           false));
    audiozoom_feature_init(
        property_get_bool("vendor.audio.feature.audiozoom.enable",
                           false));
    auto_hal_feature_init(
        property_get_bool("vendor.audio.feature.auto_hal.enable",
                           false));
}

void audio_extn_set_parameters(struct audio_device *adev,
                               struct str_parms *parms)
{
   audio_extn_set_aanc_noise_level(adev, parms);
   audio_extn_set_anc_parameters(adev, parms);
   audio_extn_set_fluence_parameters(adev, parms);
   audio_extn_set_afe_proxy_parameters(adev, parms);
   audio_extn_fm_set_parameters(adev, parms);
   audio_extn_sound_trigger_set_parameters(adev, parms);
   audio_extn_listen_set_parameters(adev, parms);
   audio_extn_ssr_set_parameters(adev, parms);
   audio_extn_dts_eagle_set_parameters(adev, parms);
   audio_extn_ddp_set_parameters(adev, parms);
   audio_extn_ds2_set_parameters(adev, parms);
   audio_extn_customstereo_set_parameters(adev, parms);
   audio_extn_hpx_set_parameters(adev, parms);
   audio_extn_pm_set_parameters(parms);
   audio_extn_source_track_set_parameters(adev, parms);
   audio_extn_fbsp_set_parameters(parms);
   audio_extn_keep_alive_set_parameters(adev, parms);
   audio_extn_passthru_set_parameters(adev, parms);
   audio_extn_ext_disp_set_parameters(adev, parms);
   audio_extn_qaf_set_parameters(adev, parms);
   if (audio_extn_qap_is_enabled())
       audio_extn_qap_set_parameters(adev, parms);
   if (adev->offload_effects_set_parameters != NULL)
       adev->offload_effects_set_parameters(parms);
   audio_extn_set_aptx_dec_bt_addr(adev, parms);
   audio_extn_ffv_set_parameters(adev, parms);
   audio_extn_ext_hw_plugin_set_parameters(adev->ext_hw_plugin, parms);
}

void audio_extn_get_parameters(const struct audio_device *adev,
                              struct str_parms *query,
                              struct str_parms *reply)
{
    char *kv_pairs = NULL;
    audio_extn_get_afe_proxy_parameters(adev, query, reply);
    audio_extn_get_fluence_parameters(adev, query, reply);
    audio_extn_ssr_get_parameters(adev, query, reply);
    get_active_offload_usecases(adev, query, reply);
    audio_extn_dts_eagle_get_parameters(adev, query, reply);
    audio_extn_hpx_get_parameters(query, reply);
    audio_extn_source_track_get_parameters(adev, query, reply);
    audio_extn_fbsp_get_parameters(query, reply);
    audio_extn_sound_trigger_get_parameters(adev, query, reply);
    audio_extn_fm_get_parameters(query, reply);
    if (adev->offload_effects_get_parameters != NULL)
        adev->offload_effects_get_parameters(query, reply);
    audio_extn_ext_hw_plugin_get_parameters(adev->ext_hw_plugin, query, reply);

    kv_pairs = str_parms_to_str(reply);
    ALOGD_IF(kv_pairs != NULL, "%s: returns %s", __func__, kv_pairs);
    free(kv_pairs);
}

int audio_ext_get_presentation_position(struct stream_out *out,
                           struct audio_out_presentation_position_param *pos_param)
{
    int ret = -ENODATA;

    if (!out) {
        ALOGE("%s:: Invalid stream",__func__);
        return ret;
    }

    if (is_offload_usecase(out->usecase)) {
        if (out->compr != NULL)
            ret = audio_extn_utils_compress_get_dsp_presentation_pos(out,
                                  &pos_param->frames, &pos_param->timestamp, pos_param->clock_id);
    } else {
        if (out->pcm)
            ret = audio_extn_utils_pcm_get_dsp_presentation_pos(out,
                                  &pos_param->frames, &pos_param->timestamp, pos_param->clock_id);
    }

    ALOGV("%s frames %lld timestamp %lld", __func__, (long long int)pos_param->frames,
           pos_param->timestamp.tv_sec*1000000000LL + pos_param->timestamp.tv_nsec);

    return ret;
}
