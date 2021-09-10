/*
 * Copyright (C) 2018 The Android Open Source Project
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

#define LOG_TAG "audio_hw_waves"
/*#define LOG_NDEBUG 0*/

#include <audio_hw.h>
#include <cutils/str_parms.h>
#include <dlfcn.h>
#include <log/log.h>
#include <math.h>
#include <platform_api.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <system/audio.h>
#include <unistd.h>

#include "audio_extn.h"
#include "maxxaudio.h"

#define LIB_MA_PARAM "libmaxxaudioqdsp.so"
#define LIB_MA_PATH "vendor/lib/"
#define PRESET_PATH "/vendor/etc"
#define MPS_BASE_STRING "default"
#define USER_PRESET_PATH ""
#define CONFIG_BASE_STRING "maxx_conf"
#define CAL_PRESIST_STR "cal_persist"
#define CAL_SAMPLERATE_STR "cal_samplerate"

#define MA_QDSP_PARAM_INIT      "maxxaudio_qdsp_initialize"
#define MA_QDSP_PARAM_DEINIT    "maxxaudio_qdsp_uninitialize"
#define MA_QDSP_IS_FEATURE_USED "maxxaudio_qdsp_is_feature_supported"
#define MA_QDSP_SET_LR_SWAP     "maxxaudio_qdsp_set_lr_swap"
#define MA_QDSP_SET_ORIENTATION "maxxaudio_qdsp_set_orientation"
#define MA_QDSP_SET_MODE        "maxxaudio_qdsp_set_sound_mode"
#define MA_QDSP_SET_VOL         "maxxaudio_qdsp_set_volume"
#define MA_QDSP_SET_VOLT        "maxxaudio_qdsp_set_volume_table"
#define MA_QDSP_SET_PARAM       "maxxaudio_qdsp_set_parameter"
#define MA_QDSP_SET_COMMAND     "maxxaudio_qdsp_set_command"
#define MA_QDSP_GET_COMMAND     "maxxaudio_qdsp_get_command"

#define SUPPORT_DEV "18d1:5033" // Blackbird usbid
#define SUPPORTED_USB 0x01

#define WAVES_COMMAND_SIZE 10240

typedef unsigned int effective_scope_flag_t;
const effective_scope_flag_t EFFECTIVE_SCOPE_RTC = 1 << 0;   /* RTC  */
const effective_scope_flag_t EFFECTIVE_SCOPE_ACDB = 1 << 1;  /* ACDB */
const effective_scope_flag_t EFFECTIVE_SCOPE_ALL = EFFECTIVE_SCOPE_RTC | EFFECTIVE_SCOPE_ACDB;
const effective_scope_flag_t EFFECTIVE_SCOPE_NONE = 0;
const effective_scope_flag_t EFFECTIVE_SCOPE_DEFAULT = EFFECTIVE_SCOPE_NONE;

const unsigned int AUDIO_CAL_SETTINGS_VERSION_MAJOR = 2;
const unsigned int AUDIO_CAL_SETTINGS_VERSION_MINOR = 0;
const unsigned int AUDIO_CAL_SETTINGS_VERSION_MAJOR_DEFAULT = AUDIO_CAL_SETTINGS_VERSION_MAJOR;
const unsigned int AUDIO_CAL_SETTINGS_VERSION_MINOR_DEFAULT = AUDIO_CAL_SETTINGS_VERSION_MINOR;

const unsigned int VALUE_AUTO = 0xFFFFFFFF;
const unsigned int APP_TYPE_AUTO = VALUE_AUTO;
const unsigned int APP_TYPE_DEFAULT = APP_TYPE_AUTO;
const unsigned int DEVICE_AUTO = VALUE_AUTO;
const unsigned int DEVICE_DEFAULT = DEVICE_AUTO;

const unsigned int MAAP_OUTPUT_GAIN = 27;

typedef enum MA_STREAM_TYPE {
    STREAM_MIN_TYPES = 0,
    STREAM_VOICE = STREAM_MIN_TYPES,
    STREAM_SYSTEM,
    STREAM_RING,
    STREAM_MUSIC,
    STREAM_ALARM,
    STREAM_NOTIFICATION ,
    STREAM_MAX_TYPES,
} ma_stream_type_t;

typedef enum MA_CMD {
    MA_CMD_VOL,
    MA_CMD_SWAP_ENABLE,
    MA_CMD_SWAP_DISABLE,
    MA_CMD_ROTATE_ENABLE,
    MA_CMD_ROTATE_DISABLE,
} ma_cmd_t;

typedef struct ma_audio_cal_version {
    unsigned int major;
    unsigned int minor;
} ma_audio_cal_version_t;

typedef struct ma_audio_cal_common_settings {
    unsigned int app_type;
    struct listnode devices;
} ma_audio_cal_common_settings_t;

struct ma_audio_cal_settings {
    ma_audio_cal_version_t version;
    ma_audio_cal_common_settings_t common;
    effective_scope_flag_t effect_scope_flag;
};

struct ma_state {
    float vol;
    bool active;
};

typedef void *ma_audio_cal_handle_t;
typedef int (*set_audio_cal_t)(const char *);

typedef bool (*ma_param_init_t)(ma_audio_cal_handle_t *, const char *,
                                const char *, const char *, set_audio_cal_t);

typedef bool (*ma_param_deinit_t)(ma_audio_cal_handle_t *);

typedef bool (*ma_is_feature_used_t)(ma_audio_cal_handle_t, const char *);

typedef bool (*ma_set_lr_swap_t)(ma_audio_cal_handle_t,
                                 const struct ma_audio_cal_settings *, bool);

typedef bool (*ma_set_orientation_t)(ma_audio_cal_handle_t,
                                     const struct ma_audio_cal_settings *, int);

typedef bool (*ma_set_sound_mode_t)(ma_audio_cal_handle_t,
                                    const struct ma_audio_cal_settings *,
                                    unsigned int);

typedef bool (*ma_set_volume_t)(ma_audio_cal_handle_t,
                                const struct ma_audio_cal_settings *, double);

typedef bool (*ma_set_volume_table_t)(ma_audio_cal_handle_t,
                                      const struct ma_audio_cal_settings *,
                                      size_t, struct ma_state *);

typedef bool (*ma_set_param_t)(ma_audio_cal_handle_t,
                               const struct ma_audio_cal_settings *,
                               unsigned int, double);

typedef bool (*ma_set_cmd_t)(ma_audio_cal_handle_t handle,
                             const struct ma_audio_cal_settings *,
                             const char*);

typedef bool (*ma_get_cmd_t)(ma_audio_cal_handle_t handle,
                             const struct ma_audio_cal_settings *,
                             const char *,
                             char *,
                             uint32_t);

struct ma_platform_data {
    void *waves_handle;
    void *platform;
    pthread_mutex_t lock;
    ma_param_init_t          ma_param_init;
    ma_param_deinit_t        ma_param_deinit;
    ma_is_feature_used_t     ma_is_feature_used;
    ma_set_lr_swap_t         ma_set_lr_swap;
    ma_set_orientation_t     ma_set_orientation;
    ma_set_sound_mode_t      ma_set_sound_mode;
    ma_set_volume_t          ma_set_volume;
    ma_set_volume_table_t    ma_set_volume_table;
    ma_set_param_t           ma_set_param;
    ma_set_cmd_t             ma_set_cmd;
    ma_get_cmd_t             ma_get_cmd;
    bool speaker_lr_swap;
    bool orientation_used;
    int dispaly_orientation;
};

ma_audio_cal_handle_t g_ma_audio_cal_handle = NULL;
static uint16_t g_supported_dev = 0;
static struct ma_state ma_cur_state_table[STREAM_MAX_TYPES];
static struct ma_platform_data *my_data = NULL;
static char ma_command_data[WAVES_COMMAND_SIZE];
static char ma_reply_data[WAVES_COMMAND_SIZE];

// --- external function dependency ---
fp_platform_set_parameters_t fp_platform_set_parameters;
fp_audio_extn_get_snd_card_split_t fp_audio_extn_get_snd_card_split;

static int set_audio_cal(const char *audio_cal)
{
    ALOGV("set_audio_cal: %s", audio_cal);

    return fp_platform_set_parameters(my_data->platform,
                                   str_parms_create_str(audio_cal));
}

static bool ma_set_lr_swap_l(
    const struct ma_audio_cal_settings *audio_cal_settings, bool swap)
{
    return my_data->ma_set_lr_swap(g_ma_audio_cal_handle,
                                   audio_cal_settings, swap);
}

static bool ma_set_orientation_l(
    const struct ma_audio_cal_settings *audio_cal_settings, int orientation)
{
    return my_data->ma_set_orientation(g_ma_audio_cal_handle,
                                   audio_cal_settings, orientation);
}

static bool ma_set_volume_table_l(
    const struct ma_audio_cal_settings *audio_cal_settings,
    size_t num_streams, struct ma_state *volume_table)
{
    return my_data->ma_set_volume_table(g_ma_audio_cal_handle,
                                        audio_cal_settings, num_streams,
                                        volume_table);
}

static bool ma_set_param_l(
    const struct ma_audio_cal_settings *audio_cal_settings,
    unsigned int index, double value)
{
    return my_data->ma_set_param(g_ma_audio_cal_handle,
                                 audio_cal_settings, index, value);
}

static void print_state_log()
{
    ALOGD("%s: send volume table -(%i,%f,%s),(%i,%f,%s),(%i,%f,%s),(%i,%f,%s),"
          "(%i,%f,%s),(%i,%f,%s)", __func__,
          STREAM_VOICE, ma_cur_state_table[STREAM_VOICE].vol,
          ma_cur_state_table[STREAM_VOICE].active ? "T" : "F",
          STREAM_SYSTEM, ma_cur_state_table[STREAM_SYSTEM].vol,
          ma_cur_state_table[STREAM_SYSTEM].active ? "T" : "F",
          STREAM_RING, ma_cur_state_table[STREAM_RING].vol,
          ma_cur_state_table[STREAM_RING].active ? "T" : "F",
          STREAM_MUSIC, ma_cur_state_table[STREAM_MUSIC].vol,
          ma_cur_state_table[STREAM_MUSIC].active ? "T" : "F",
          STREAM_ALARM, ma_cur_state_table[STREAM_ALARM].vol,
          ma_cur_state_table[STREAM_ALARM].active ? "T" : "F",
          STREAM_NOTIFICATION, ma_cur_state_table[STREAM_NOTIFICATION].vol,
          ma_cur_state_table[STREAM_NOTIFICATION].active ? "T" : "F");

}

static inline bool valid_usecase(struct audio_usecase *usecase)
{
    if ((usecase->type == PCM_PLAYBACK) &&
        /* supported usecases */
        ((usecase->id == USECASE_AUDIO_PLAYBACK_DEEP_BUFFER) ||
         (usecase->id == USECASE_AUDIO_PLAYBACK_LOW_LATENCY) ||
         (usecase->id == USECASE_AUDIO_PLAYBACK_OFFLOAD)) &&
        /* support devices */
        (compare_device_type(&usecase->device_list, AUDIO_DEVICE_OUT_SPEAKER) ||
         compare_device_type(&usecase->device_list, AUDIO_DEVICE_OUT_SPEAKER_SAFE) ||
         (is_usb_out_device_type(&usecase->device_list) &&
          ma_supported_usb())))
        /* TODO: enable A2DP when it is ready */

        return true;

    ALOGV("%s: not support type %d usecase %d device %d",
           __func__, usecase->type, usecase->id, get_device_types(&usecase->device_list));

    return false;
}

// already hold lock
static inline bool is_active()
{
    ma_stream_type_t i = 0;

    for (i = 0; i < STREAM_MAX_TYPES; i++)
        if (ma_cur_state_table[i].active)
            return true;

    return false;
}

static void ma_cal_init(struct ma_audio_cal_settings *ma_cal)
{
    ma_cal->version.major = AUDIO_CAL_SETTINGS_VERSION_MAJOR_DEFAULT;
    ma_cal->version.minor = AUDIO_CAL_SETTINGS_VERSION_MINOR_DEFAULT;
    ma_cal->common.app_type = APP_TYPE_DEFAULT;
    list_init(&ma_cal->common.devices);
    update_device_list(&ma_cal->common.devices, DEVICE_DEFAULT,
                       "", true);
    ma_cal->effect_scope_flag = EFFECTIVE_SCOPE_ALL;
}

static bool check_and_send_all_audio_cal(struct audio_device *adev, ma_cmd_t cmd)
{
    bool ret = false;
    struct listnode *node;
    struct audio_usecase *usecase;
    struct ma_audio_cal_settings ma_cal;

    ma_cal_init(&ma_cal);

    list_for_each(node, &adev->usecase_list) {
        usecase = node_to_item(node, struct audio_usecase, list);
        if (usecase->stream.out && valid_usecase(usecase)) {
            ma_cal.common.app_type = usecase->stream.out->app_type_cfg.app_type;
            assign_devices(&ma_cal.common.devices, &usecase->stream.out->device_list);
            ALOGV("%s: send usecase(%d) app_type(%d) device(%d)",
                      __func__, usecase->id, ma_cal.common.app_type,
                      get_device_types(&ma_cal.common.devices));

            switch (cmd) {
                case MA_CMD_VOL:
                    ret = ma_set_volume_table_l(&ma_cal, STREAM_MAX_TYPES,
                                                ma_cur_state_table);
                    if (ret)
                        ALOGV("ma_set_volume_table_l success");
                    else
                        ALOGE("ma_set_volume_table_l returned with error.");
                    print_state_log();
                    break;

                case MA_CMD_SWAP_ENABLE:
                    /* lr swap only enable for speaker path */
                    if (compare_device_type(&ma_cal.common.devices,
                                            AUDIO_DEVICE_OUT_SPEAKER)) {
                        ret = ma_set_lr_swap_l(&ma_cal, true);
                        if (ret)
                            ALOGV("ma_set_lr_swap_l enable returned with success.");
                        else
                            ALOGE("ma_set_lr_swap_l enable returned with error.");
                    }
                    break;

                case MA_CMD_SWAP_DISABLE:
                    ret = ma_set_lr_swap_l(&ma_cal, false);
                    if (ret)
                        ALOGV("ma_set_lr_swap_l disable returned with success.");
                    else
                        ALOGE("ma_set_lr_swap_l disable returned with error.");
                    break;

                case MA_CMD_ROTATE_ENABLE:
                    if (compare_device_type(&ma_cal.common.devices,
                                            AUDIO_DEVICE_OUT_SPEAKER)) {
                        ret = ma_set_orientation_l(&ma_cal, my_data->dispaly_orientation);
                        if (ret)
                            ALOGV("ma_set_orientation_l %d returned with success.",
                                  my_data->dispaly_orientation);
                        else
                            ALOGE("ma_set_orientation_l %d returned with error.",
                                  my_data->dispaly_orientation);
                    }
                    break;

                case MA_CMD_ROTATE_DISABLE:
                    ret = ma_set_orientation_l(&ma_cal, 0);
                    if (ret)
                        ALOGV("ma_set_orientation_l 0 returned with success.");
                    else
                        ALOGE("ma_set_orientation_l 0 returned with error.");
                    break;

                default:
                    ALOGE("%s: unsupported cmd %d", __func__, cmd);
            }
        }
    }

    return ret;
}

static bool find_sup_dev(char *name)
{
    char *token, *saveptr = NULL;
    const char s[2] = ",";
    bool ret = false;
    char sup_devs[128];

    // the rule of comforming suppored dev's name
    // 1. Both string len are equal
    // 2. Both string content are equal

    strncpy(sup_devs, SUPPORT_DEV, sizeof(sup_devs));
    token = strtok_r(sup_devs, s, &saveptr);
    while (token != NULL) {
        if (strncmp(token, name, strlen(token)) == 0 &&
            strlen(token) == strlen(name)) {
            ALOGD("%s: support dev %s", __func__, token);
            ret = true;
            break;
        }
        token = strtok_r(NULL, s, &saveptr);
    }

    return ret;
}

static void ma_set_swap_l(struct audio_device *adev, bool enable)
{
    if (enable)
        check_and_send_all_audio_cal(adev, MA_CMD_SWAP_ENABLE);
    else
        check_and_send_all_audio_cal(adev, MA_CMD_SWAP_DISABLE);
}

static void ma_set_rotation_l(struct audio_device *adev, int orientation)
{
    if (orientation != 0)
        check_and_send_all_audio_cal(adev, MA_CMD_ROTATE_ENABLE);
    else
        check_and_send_all_audio_cal(adev, MA_CMD_ROTATE_DISABLE);
}

static void ma_support_usb(bool enable, int card)
{
    char path[128];
    char id[32];
    int ret = 0;
    int32_t fd = -1;
    char *idd;
    char *saveptr = NULL;

    if (enable) {
        ret = snprintf(path, sizeof(path), "/proc/asound/card%u/usbid", card);
        if (ret < 0) {
            ALOGE("%s: failed on snprintf (%d) to path %s\n",
                  __func__, ret, path);
            goto done;
        }
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            ALOGE("%s: error failed to open id file %s error: %d\n",
                  __func__, path, errno);
            goto done;
        }
        if (read(fd, id, sizeof(id)) < 0) {
            ALOGE("%s: file read error", __func__);
            goto done;
        }
        //replace '\n' to '\0'
        idd = strtok_r(id, "\n", &saveptr);

        if (find_sup_dev(idd)) {
            ALOGV("%s: support usbid is %s", __func__, id);
            g_supported_dev |= SUPPORTED_USB;
        } else
            ALOGV("%s: usbid %s isn't found from %s", __func__, id, SUPPORT_DEV);
    } else {
        g_supported_dev &= ~SUPPORTED_USB;
    }

done:
    if (fd >= 0) close(fd);
}

// adev_init lock held
void ma_init(void *platform, maxx_audio_init_config_t init_config)
{
    ma_stream_type_t i = 0;
    int ret = 0;
    char lib_path[128] = {0};
    char mps_path[128] = {0};
    char cnf_path[128] = {0};
    struct snd_card_split *snd_split_handle = NULL;

    fp_platform_set_parameters = init_config.fp_platform_set_parameters;
    fp_audio_extn_get_snd_card_split = init_config.fp_audio_extn_get_snd_card_split;

    snd_split_handle = fp_audio_extn_get_snd_card_split();

    if (platform == NULL) {
        ALOGE("%s: platform is NULL", __func__);
        goto error;
    }

    if (my_data) { free(my_data); }
    my_data = calloc(1, sizeof(struct ma_platform_data));
    if (my_data == NULL) {
        ALOGE("%s: ma_cal alloct fail", __func__);
        goto error;
    }

    pthread_mutex_init(&my_data->lock, NULL);

    my_data->platform = platform;
    ret = snprintf(lib_path, sizeof(lib_path), "%s/%s", LIB_MA_PATH, LIB_MA_PARAM);
    if (ret < 0) {
        ALOGE("%s: snprintf failed for lib %s, ret %d", __func__, LIB_MA_PARAM, ret);
        goto error;
    }

    my_data->waves_handle = dlopen(lib_path, RTLD_NOW);
    if (my_data->waves_handle == NULL) {
        ALOGE("%s: DLOPEN failed for %s, %s", __func__, LIB_MA_PARAM, dlerror());
        goto error;
    } else {
        ALOGV("%s: DLOPEN successful for %s", __func__, LIB_MA_PARAM);

        my_data->ma_param_init = (ma_param_init_t)dlsym(my_data->waves_handle, MA_QDSP_PARAM_INIT);
        if (!my_data->ma_param_init) {
            ALOGE("%s: dlsym error %s for ma_param_init", __func__, dlerror());
            goto error;
        }

        my_data->ma_param_deinit = (ma_param_deinit_t)dlsym(my_data->waves_handle,
                                                            MA_QDSP_PARAM_DEINIT);
        if (!my_data->ma_param_deinit) {
            ALOGE("%s: dlsym error %s for ma_param_deinit", __func__, dlerror());
            goto error;
        }

        my_data->ma_is_feature_used = (ma_is_feature_used_t)dlsym(my_data->waves_handle,
                                                                  MA_QDSP_IS_FEATURE_USED);
        if (!my_data->ma_is_feature_used) {
            ALOGV("%s: dlsym error %s for ma_is_feature_used", __func__, dlerror());
        }

        my_data->ma_set_orientation = (ma_set_orientation_t)dlsym(my_data->waves_handle,
                                                                  MA_QDSP_SET_ORIENTATION);
        if (!my_data->ma_set_orientation) {
            ALOGV("%s: dlsym error %s for ma_set_orientation", __func__, dlerror());
        }

        my_data->ma_set_lr_swap = (ma_set_lr_swap_t)dlsym(my_data->waves_handle,
                                                          MA_QDSP_SET_LR_SWAP);
        if (!my_data->ma_set_lr_swap) {
            ALOGE("%s: dlsym error %s for ma_set_lr_swap", __func__, dlerror());
            goto error;
        }

        my_data->ma_set_sound_mode = (ma_set_sound_mode_t)dlsym(my_data->waves_handle,
                                                                MA_QDSP_SET_MODE);
        if (!my_data->ma_set_sound_mode) {
            ALOGE("%s: dlsym error %s for ma_set_sound_mode", __func__, dlerror());
            goto error;
        }

        my_data->ma_set_volume = (ma_set_volume_t)dlsym(my_data->waves_handle, MA_QDSP_SET_VOL);
        if (!my_data->ma_set_volume) {
            ALOGE("%s: dlsym error %s for ma_set_volume", __func__, dlerror());
            goto error;
        }

        my_data->ma_set_volume_table = (ma_set_volume_table_t)dlsym(my_data->waves_handle,
                                                                    MA_QDSP_SET_VOLT);
        if (!my_data->ma_set_volume_table) {
            ALOGE("%s: dlsym error %s for ma_set_volume_table", __func__, dlerror());
            goto error;
        }

        my_data->ma_set_param = (ma_set_param_t)dlsym(my_data->waves_handle, MA_QDSP_SET_PARAM);
        if (!my_data->ma_set_param) {
            ALOGE("%s: dlsym error %s for ma_set_param", __func__, dlerror());
            goto error;
        }

        my_data->ma_set_cmd = (ma_set_cmd_t)dlsym(my_data->waves_handle, MA_QDSP_SET_COMMAND);
        if (!my_data->ma_set_cmd) {
            ALOGE("%s: dlsym error %s for ma_set_cmd", __func__, dlerror());
        }

        my_data->ma_get_cmd = (ma_get_cmd_t)dlsym(my_data->waves_handle, MA_QDSP_GET_COMMAND);
        if (!my_data->ma_get_cmd) {
            ALOGE("%s: dlsym error %s for ma_get_cmd", __func__, dlerror());
        }
    }

    /* get preset table */
    if (snd_split_handle == NULL) {
        snprintf(mps_path, sizeof(mps_path), "%s/%s.mps",
                 PRESET_PATH, MPS_BASE_STRING);
    } else {
        snprintf(mps_path, sizeof(mps_path), "%s/%s_%s.mps",
                 PRESET_PATH, MPS_BASE_STRING, snd_split_handle->form_factor);
    }

    /* get config files */
    if (snd_split_handle == NULL) {
        snprintf(cnf_path, sizeof(cnf_path), "%s/%s.ini",
                 PRESET_PATH, CONFIG_BASE_STRING);
    } else {
        snprintf(cnf_path, sizeof(cnf_path), "%s/%s_%s.ini",
                 PRESET_PATH, CONFIG_BASE_STRING, snd_split_handle->form_factor);
    }

    /* check file */
    if (access(mps_path, R_OK) < 0) {
        ALOGW("%s: file %s isn't existed.", __func__, mps_path);
        goto error;
    } else
        ALOGD("%s: Loading mps file: %s", __func__, mps_path);

    /* TODO: check user preset table once the feature is enabled
    if (access(USER_PRESET_PATH, F_OK) < 0 ){
        ALOGW("%s: file %s isn't existed.", __func__, USER_PRESET_PATH);
        goto error;
    }
    */

    if (access(cnf_path, R_OK) < 0) {
        ALOGW("%s: file %s isn't existed.", __func__, cnf_path);
        goto error;
    } else
        ALOGD("%s: Loading ini file: %s", __func__, cnf_path);

    /* init ma parameter */
    if (my_data->ma_param_init(&g_ma_audio_cal_handle,
                               mps_path,
                               USER_PRESET_PATH, /* unused */
                               cnf_path,
                               &set_audio_cal)) {
        if (!g_ma_audio_cal_handle) {
            ALOGE("%s: ma parameters initialize failed", __func__);
            my_data->ma_param_deinit(&g_ma_audio_cal_handle);
            goto error;
        }
        ALOGD("%s: ma parameters initialize successful", __func__);
    } else {
        ALOGE("%s: ma parameters initialize failed", __func__);
        goto error;
    }

    /* init volume table */
    for (i = 0; i < STREAM_MAX_TYPES; i++) {
        ma_cur_state_table[i].vol = 0.0;
        ma_cur_state_table[i].active = false;
    }

    my_data->speaker_lr_swap = false;
    my_data->orientation_used = false;
    my_data->dispaly_orientation = 0;

    if (g_ma_audio_cal_handle && my_data->ma_is_feature_used) {
        my_data->orientation_used = my_data->ma_is_feature_used(
                g_ma_audio_cal_handle, "SET_ORIENTATION");
    }

    return;

error:
    if (my_data) { free(my_data); }
    my_data = NULL;
}

//adev_init lock held
void ma_deinit()
{
    if (my_data) {
        /* deinit ma parameter */
        if (my_data->ma_param_deinit &&
            my_data->ma_param_deinit(&g_ma_audio_cal_handle))
            ALOGD("%s: ma parameters uninitialize successful", __func__);
        else
            ALOGD("%s: ma parameters uninitialize failed", __func__);

        pthread_mutex_destroy(&my_data->lock);
        free(my_data);
        my_data = NULL;
    }
}

// adev_init and adev lock held
bool ma_set_state(struct audio_device *adev, int stream_type,
                             float vol, bool active)
{
    bool ret = false;
    struct ma_state pr_mstate;

    if (stream_type >= STREAM_MAX_TYPES ||
        stream_type < STREAM_MIN_TYPES) {
        ALOGE("%s: stream_type %d out of range.", __func__, stream_type);
        return ret;
    }

    if (!my_data) {
        ALOGV("%s: maxxaudio isn't initialized.", __func__);
        return ret;
    }

    ALOGV("%s: stream[%d] vol[%f] active[%s]",
          __func__, stream_type, vol, active ? "true" : "false");

    pr_mstate.vol = ma_cur_state_table[(ma_stream_type_t)stream_type].vol;
    pr_mstate.active = ma_cur_state_table[(ma_stream_type_t)stream_type].active;

    // update condition: vol or active state changes
    if (pr_mstate.vol != vol || pr_mstate.active != active) {

        pthread_mutex_lock(&my_data->lock);

        ma_cur_state_table[(ma_stream_type_t)stream_type].vol = vol;
        ma_cur_state_table[(ma_stream_type_t)stream_type].active = active;

        ret = check_and_send_all_audio_cal(adev, MA_CMD_VOL);

        pthread_mutex_unlock(&my_data->lock);
    }

    return ret;
}

void ma_set_device(struct audio_usecase *usecase)
{
    struct ma_audio_cal_settings ma_cal;

    if (!my_data) {
        ALOGV("%s: maxxaudio isn't initialized.", __func__);
        return;
    }

    if (!valid_usecase(usecase)) {
        ALOGV("%s: %d is not supported usecase", __func__, usecase->id);
        return;
    }

    ma_cal_init(&ma_cal);

    /* update audio_cal and send it */
    ma_cal.common.app_type = usecase->stream.out->app_type_cfg.app_type;
    assign_devices(&ma_cal.common.devices, &usecase->stream.out->device_list);
    ALOGV("%s: send usecase(%d) app_type(%d) device(%d)",
              __func__, usecase->id, ma_cal.common.app_type,
              get_device_types(&ma_cal.common.devices));

    pthread_mutex_lock(&my_data->lock);

    if (is_active()) {

        if (compare_device_type(&ma_cal.common.devices,
                                AUDIO_DEVICE_OUT_SPEAKER)) {
            if (my_data->orientation_used)
                ma_set_rotation_l(usecase->stream.out->dev,
                                  my_data->dispaly_orientation);
            else
                ma_set_swap_l(usecase->stream.out->dev, my_data->speaker_lr_swap);
        } else {
            if (my_data->orientation_used)
                ma_set_rotation_l(usecase->stream.out->dev, 0);
            else
                ma_set_swap_l(usecase->stream.out->dev, false);
        }

        if (!ma_set_volume_table_l(&ma_cal,
                                   STREAM_MAX_TYPES,
                                   ma_cur_state_table))
            ALOGE("ma_set_volume_table_l returned with error.");
        else
            ALOGV("ma_set_volume_table_l success");
        print_state_log();

    }
    pthread_mutex_unlock(&my_data->lock);
}

static bool ma_set_command(struct ma_audio_cal_settings *audio_cal_settings, char *cmd_data)
{
    if (my_data->ma_set_cmd)
        return my_data->ma_set_cmd(g_ma_audio_cal_handle, audio_cal_settings, cmd_data);
    return false;
}

static bool ma_get_command(struct ma_audio_cal_settings *audio_cal_settings, char *cmd_data,
                           char *reply_data, uint32_t reply_size)
{
    if (my_data->ma_get_cmd)
        return my_data->ma_get_cmd(g_ma_audio_cal_handle, audio_cal_settings, cmd_data, reply_data,
                                   reply_size);
    return false;
}

static bool ma_fill_apptype_and_device_from_params(struct str_parms *parms, uint32_t *app_type,
                                                   struct listnode *devices)
{
    int ret;
    char value[128];

    ret = str_parms_get_str(parms, "cal_apptype", value, sizeof(value));

    if (ret >= 0) {
        *app_type = (uint32_t)atoi(value);
        ret = str_parms_get_str(parms, "cal_devid", value, sizeof(value));
        if (ret >= 0) {
            update_device_list(devices, (uint32_t)atoi(value), "", true);
            return true;
        }
    }
    return false;
}

static bool ma_add_apptype_and_device_to_params(struct str_parms *parms, uint32_t app_type,
                                                struct listnode *devices)
{
    if (0 <= str_parms_add_int(parms, "cal_apptype", app_type)) {
        if (0 <= str_parms_add_int(parms, "cal_devid", get_device_types(devices))) {
            return true;
        }
    }
    return false;
}

static bool ma_get_command_parameters(struct str_parms *query, struct str_parms *reply)
{
    struct ma_audio_cal_settings ma_cal;
    int ret;

    ret = str_parms_get_str(query, "waves_data", ma_command_data, sizeof(ma_command_data));
    if (ret >= 0) {
        ma_cal_init(&ma_cal);
        if (ma_fill_apptype_and_device_from_params(query, &ma_cal.common.app_type,
                &ma_cal.common.devices)) {
            ma_add_apptype_and_device_to_params(reply, ma_cal.common.app_type,
                                                &ma_cal.common.devices);
            ALOGV("%s: before - command=%s", __func__, (char *)ma_command_data);
            if (ma_get_command(&ma_cal, ma_command_data, ma_reply_data, sizeof(ma_reply_data))) {
                str_parms_add_str(reply, "waves_data", ma_reply_data);
                ALOGV("%s: after - command=%s", __func__, (char *)ma_reply_data);
                return true;
            } else {
                str_parms_add_str(reply, "waves_data", "");
            }
        }
    }
    return false;
}

static bool ma_set_command_parameters(struct str_parms *parms)
{
    struct ma_audio_cal_settings ma_cal;
    int ret;

    ret = str_parms_get_str(parms, "waves_data", ma_command_data, sizeof(ma_command_data));
    if (ret >= 0) {
        ma_cal_init(&ma_cal);
        if (ma_fill_apptype_and_device_from_params(parms, &ma_cal.common.app_type,
                &ma_cal.common.devices)) {
            return ma_set_command(&ma_cal, ma_command_data);
        }
    }
    return false;
}

void ma_get_parameters(struct audio_device *adev, struct str_parms *query,
                       struct str_parms *reply)
{
    (void)adev;
    ma_get_command_parameters(query, reply);
}

void ma_set_parameters(struct audio_device *adev, struct str_parms *parms)
{
    int ret;
    int val;
    char value[128];

    // do LR swap and usb recognition
    ret = str_parms_get_int(parms, "rotation", &val);
    if (ret >= 0) {
        if (!my_data) {
            ALOGV("%s: maxxaudio isn't initialized.", __func__);
            return;
        }

        switch (val) {
        case 270:
            my_data->speaker_lr_swap = true;
            break;
        case 0:
        case 90:
        case 180:
            my_data->speaker_lr_swap = false;
            break;
        }
        my_data->dispaly_orientation = val;

        if (my_data->orientation_used)
            ma_set_rotation_l(adev, my_data->dispaly_orientation);
        else
            ma_set_swap_l(adev, my_data->speaker_lr_swap);
    }

    // check connect status
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT, value,
                            sizeof(value));
    if (ret >= 0) {
        audio_devices_t device = (audio_devices_t)strtoul(value, NULL, 10);
        if (audio_is_usb_out_device(device)) {
            ret = str_parms_get_str(parms, "card", value, sizeof(value));
            if (ret >= 0) {
                const int card = atoi(value);
                ma_support_usb(true, card);
            }
        }
    }

    // check disconnect status
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value,
                            sizeof(value));
    if (ret >= 0) {
        audio_devices_t device = (audio_devices_t)strtoul(value, NULL, 10);
        if (audio_is_usb_out_device(device)) {
            ret = str_parms_get_str(parms, "card", value, sizeof(value));
            if (ret >= 0) {
                const int card = atoi(value);
                ma_support_usb(false, card /*useless*/);
            }
        }
    }

    ma_set_command_parameters(parms);
}

bool ma_supported_usb()
{
    ALOGV("%s: current support 0x%x", __func__, g_supported_dev);
    return (g_supported_dev & SUPPORTED_USB) ? true : false;
}
