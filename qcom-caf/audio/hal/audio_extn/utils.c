/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2014 The Android Open Source Project
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

#define LOG_TAG "audio_hw_utils"
/* #define LOG_NDEBUG 0 */

#include <inttypes.h>
#include <errno.h>
#include <cutils/properties.h>
#include <cutils/config_utils.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <cutils/str_parms.h>
#include <log/log.h>
#include <cutils/misc.h>
#include <unistd.h>
#include <sys/ioctl.h>


#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include "audio_extn.h"
#include "voice_extn.h"
#include "voice.h"
#include <sound/compress_params.h>
#include <sound/compress_offload.h>
#include <sound/devdep_params.h>
#include <tinycompress/tinycompress.h>

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_UTILS
#include <log_utils.h>
#endif

#ifdef AUDIO_EXTERNAL_HDMI_ENABLED
#include "audio_parsers.h"
#endif

#define AUDIO_IO_POLICY_VENDOR_CONFIG_FILE_NAME "audio_io_policy.conf"
#define AUDIO_OUTPUT_POLICY_VENDOR_CONFIG_FILE_NAME "audio_output_policy.conf"

#define OUTPUTS_TAG "outputs"
#define INPUTS_TAG "inputs"

#define DYNAMIC_VALUE_TAG "dynamic"
#define FLAGS_TAG "flags"
#define PROFILES_TAG "profile"
#define FORMATS_TAG "formats"
#define SAMPLING_RATES_TAG "sampling_rates"
#define BIT_WIDTH_TAG "bit_width"
#define APP_TYPE_TAG "app_type"

#define STRING_TO_ENUM(string) { #string, string }
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define BASE_TABLE_SIZE 64
#define MAX_BASEINDEX_LEN 256

#ifndef SND_AUDIOCODEC_TRUEHD
#define SND_AUDIOCODEC_TRUEHD 0x00000023
#endif

#define APP_TYPE_VOIP_AUDIO 0x1113A

#ifdef AUDIO_EXTERNAL_HDMI_ENABLED
#define PROFESSIONAL        (1<<0)      /* 0 = consumer, 1 = professional */
#define NON_LPCM            (1<<1)      /* 0 = audio, 1 = non-audio */
#define SR_44100            (0<<0)      /* 44.1kHz */
#define SR_NOTID            (1<<0)      /* non indicated */
#define SR_48000            (2<<0)      /* 48kHz */
#define SR_32000            (3<<0)      /* 32kHz */
#define SR_22050            (4<<0)      /* 22.05kHz */
#define SR_24000            (6<<0)      /* 24kHz */
#define SR_88200            (8<<0)      /* 88.2kHz */
#define SR_96000            (10<<0)     /* 96kHz */
#define SR_176400           (12<<0)     /* 176.4kHz */
#define SR_192000           (14<<0)     /* 192kHz */

#endif

/* ToDo: Check and update a proper value in msec */
#define COMPRESS_OFFLOAD_PLAYBACK_LATENCY  50
#define PCM_OFFLOAD_PLAYBACK_DSP_PATHDELAY 62
#define PCM_OFFLOAD_PLAYBACK_LATENCY PCM_OFFLOAD_PLAYBACK_DSP_PATHDELAY

#ifndef MAX_CHANNELS_SUPPORTED
#define MAX_CHANNELS_SUPPORTED 8
#endif

#ifdef __LP64__
#define VNDK_FWK_LIB_PATH "/vendor/lib64/libqti_vndfwk_detect.so"
#else
#define VNDK_FWK_LIB_PATH "/vendor/lib/libqti_vndfwk_detect.so"
#endif

typedef struct vndkfwk_s {
    void *lib_handle;
    int (*isVendorEnhancedFwk)(void);
    int (*getVendorEnhancedInfo)(void);
    const char *lib_name;
} vndkfwk_t;

static vndkfwk_t mVndkFwk = {
    NULL, NULL, NULL, VNDK_FWK_LIB_PATH};

typedef struct {
    const char *id_string;
    const int value;
} mixer_config_lookup;

struct string_to_enum {
    const char *name;
    uint32_t value;
};

const struct string_to_enum s_flag_name_to_enum_table[] = {
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_DIRECT),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_PRIMARY),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_FAST),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_RAW),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_DEEP_BUFFER),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_NON_BLOCKING),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_HW_AV_SYNC),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_INCALL_MUSIC),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_COMPRESS_PASSTHROUGH),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_TIMESTAMP),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_VOIP_RX),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_BD),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_INTERACTIVE),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_MEDIA),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_SYS_NOTIFICATION),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_NAV_GUIDANCE),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_PHONE),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_FRONT_PASSENGER),
    STRING_TO_ENUM(AUDIO_OUTPUT_FLAG_REAR_SEAT),
    STRING_TO_ENUM(AUDIO_INPUT_FLAG_NONE),
    STRING_TO_ENUM(AUDIO_INPUT_FLAG_FAST),
    STRING_TO_ENUM(AUDIO_INPUT_FLAG_HW_HOTWORD),
    STRING_TO_ENUM(AUDIO_INPUT_FLAG_RAW),
    STRING_TO_ENUM(AUDIO_INPUT_FLAG_SYNC),
    STRING_TO_ENUM(AUDIO_INPUT_FLAG_TIMESTAMP),
    STRING_TO_ENUM(AUDIO_INPUT_FLAG_COMPRESS),
    STRING_TO_ENUM(AUDIO_INPUT_FLAG_PASSTHROUGH),
    STRING_TO_ENUM(AUDIO_INPUT_FLAG_MMAP_NOIRQ),
    STRING_TO_ENUM(AUDIO_INPUT_FLAG_VOIP_TX),
    STRING_TO_ENUM(AUDIO_INPUT_FLAG_HW_AV_SYNC),
    STRING_TO_ENUM(AUDIO_INPUT_FLAG_DIRECT),
};

const struct string_to_enum s_format_name_to_enum_table[] = {
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_8_BIT),
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_16_BIT),
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_24_BIT_PACKED),
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_8_24_BIT),
    STRING_TO_ENUM(AUDIO_FORMAT_PCM_32_BIT),
    STRING_TO_ENUM(AUDIO_FORMAT_MP3),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC),
    STRING_TO_ENUM(AUDIO_FORMAT_VORBIS),
    STRING_TO_ENUM(AUDIO_FORMAT_AMR_NB),
    STRING_TO_ENUM(AUDIO_FORMAT_AMR_WB),
    STRING_TO_ENUM(AUDIO_FORMAT_AC3),
    STRING_TO_ENUM(AUDIO_FORMAT_E_AC3),
    STRING_TO_ENUM(AUDIO_FORMAT_DTS),
    STRING_TO_ENUM(AUDIO_FORMAT_DTS_HD),
    STRING_TO_ENUM(AUDIO_FORMAT_DOLBY_TRUEHD),
    STRING_TO_ENUM(AUDIO_FORMAT_IEC61937),
    STRING_TO_ENUM(AUDIO_FORMAT_E_AC3_JOC),
    STRING_TO_ENUM(AUDIO_FORMAT_WMA),
    STRING_TO_ENUM(AUDIO_FORMAT_WMA_PRO),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_ADIF),
    STRING_TO_ENUM(AUDIO_FORMAT_AMR_WB_PLUS),
    STRING_TO_ENUM(AUDIO_FORMAT_EVRC),
    STRING_TO_ENUM(AUDIO_FORMAT_EVRCB),
    STRING_TO_ENUM(AUDIO_FORMAT_EVRCWB),
    STRING_TO_ENUM(AUDIO_FORMAT_QCELP),
    STRING_TO_ENUM(AUDIO_FORMAT_MP2),
    STRING_TO_ENUM(AUDIO_FORMAT_EVRCNW),
    STRING_TO_ENUM(AUDIO_FORMAT_FLAC),
    STRING_TO_ENUM(AUDIO_FORMAT_ALAC),
    STRING_TO_ENUM(AUDIO_FORMAT_APE),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_LC),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_HE_V1),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_HE_V2),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_ADTS),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_ADTS_LC),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_ADTS_HE_V1),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_ADTS_HE_V2),
    STRING_TO_ENUM(AUDIO_FORMAT_DSD),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_LATM),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_LATM_LC),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_LATM_HE_V1),
    STRING_TO_ENUM(AUDIO_FORMAT_AAC_LATM_HE_V2),
    STRING_TO_ENUM(AUDIO_FORMAT_APTX),
};

/* payload structure avt_device drift query */
struct audio_avt_device_drift_stats {
    uint32_t       minor_version;

    /* Indicates the device interface direction as either
     * source (Tx) or sink (Rx).
    */
    uint16_t        device_direction;

    /* Reference timer for drift accumulation and time stamp information.
     * currently it only support AFE_REF_TIMER_TYPE_AVTIMER
     */
    uint16_t        reference_timer;
    struct audio_avt_device_drift_param drift_param;
} __attribute__((packed));

static char bTable[BASE_TABLE_SIZE] = {
            'A','B','C','D','E','F','G','H','I','J','K','L',
            'M','N','O','P','Q','R','S','T','U','V','W','X',
            'Y','Z','a','b','c','d','e','f','g','h','i','j',
            'k','l','m','n','o','p','q','r','s','t','u','v',
            'w','x','y','z','0','1','2','3','4','5','6','7',
            '8','9','+','/'
};

static uint32_t string_to_enum(const struct string_to_enum *table, size_t size,
                               const char *name)
{
    size_t i;
    for (i = 0; i < size; i++) {
        if (strcmp(table[i].name, name) == 0) {
            ALOGV("%s found %s", __func__, table[i].name);
            return table[i].value;
        }
    }
    ALOGE("%s cound not find %s", __func__, name);
    return 0;
}

static audio_io_flags_t parse_flag_names(char *name)
{
    uint32_t flag = 0;
    audio_io_flags_t io_flags;
    char *last_r;
    char *flag_name = strtok_r(name, "|", &last_r);
    while (flag_name != NULL) {
        if (strlen(flag_name) != 0) {
            flag |= string_to_enum(s_flag_name_to_enum_table,
                               ARRAY_SIZE(s_flag_name_to_enum_table),
                               flag_name);
        }
        flag_name = strtok_r(NULL, "|", &last_r);
    }

    ALOGV("parse_flag_names: flag - %x", flag);
    io_flags.in_flags = (audio_input_flags_t)flag;
    io_flags.out_flags = (audio_output_flags_t)flag;
    return io_flags;
}

static void parse_format_names(char *name, struct streams_io_cfg *s_info)
{
    struct stream_format *sf_info = NULL;
    char *last_r;
    char *str = strtok_r(name, "|", &last_r);

    if (str != NULL && strcmp(str, DYNAMIC_VALUE_TAG) == 0)
        return;

    list_init(&s_info->format_list);
    while (str != NULL) {
        audio_format_t format = (audio_format_t)string_to_enum(s_format_name_to_enum_table,
                                              ARRAY_SIZE(s_format_name_to_enum_table), str);
        ALOGV("%s: format - %d", __func__, format);
        if (format != 0) {
            sf_info = (struct stream_format *)calloc(1, sizeof(struct stream_format));
            if (sf_info == NULL)
                break; /* return whatever was parsed */

            sf_info->format = format;
            list_add_tail(&s_info->format_list, &sf_info->list);
        }
        str = strtok_r(NULL, "|", &last_r);
    }
}

static void parse_sample_rate_names(char *name, struct streams_io_cfg *s_info)
{
    struct stream_sample_rate *ss_info = NULL;
    uint32_t sample_rate = 48000;
    char *last_r;
    char *str = strtok_r(name, "|", &last_r);

    if (str != NULL && 0 == strcmp(str, DYNAMIC_VALUE_TAG))
        return;

    list_init(&s_info->sample_rate_list);
    while (str != NULL) {
        sample_rate = (uint32_t)strtol(str, (char **)NULL, 10);
        ALOGV("%s: sample_rate - %d", __func__, sample_rate);
        if (0 != sample_rate) {
            ss_info = (struct stream_sample_rate *)calloc(1, sizeof(struct stream_sample_rate));
            if (!ss_info) {
                ALOGE("%s: memory allocation failure", __func__);
                return;
            }
            ss_info->sample_rate = sample_rate;
            list_add_tail(&s_info->sample_rate_list, &ss_info->list);
        }
        str = strtok_r(NULL, "|", &last_r);
    }
}

static int parse_bit_width_names(char *name)
{
    int bit_width = 16;
    char *last_r;
    char *str = strtok_r(name, "|", &last_r);

    if (str != NULL && strcmp(str, DYNAMIC_VALUE_TAG))
        bit_width = (int)strtol(str, (char **)NULL, 10);

    ALOGV("%s: bit_width - %d", __func__, bit_width);
    return bit_width;
}

static int parse_app_type_names(void *platform, char *name)
{
    int app_type = platform_get_default_app_type(platform);
    char *last_r;
    char *str = strtok_r(name, "|", &last_r);

    if (str != NULL && strcmp(str, DYNAMIC_VALUE_TAG))
        app_type = (int)strtol(str, (char **)NULL, 10);

    ALOGV("%s: app_type - %d", __func__, app_type);
    return app_type;
}

static void update_streams_cfg_list(cnode *root, void *platform,
                                    struct listnode *streams_cfg_list)
{
    cnode *node = root->first_child;
    struct streams_io_cfg *s_info;

    ALOGV("%s", __func__);
    s_info = (struct streams_io_cfg *)calloc(1, sizeof(struct streams_io_cfg));

    if (!s_info) {
        ALOGE("failed to allocate mem for s_info list element");
        return;
    }

    while (node) {
        if (strcmp(node->name, FLAGS_TAG) == 0) {
            s_info->flags = parse_flag_names((char *)node->value);
        } else if (strcmp(node->name, PROFILES_TAG) == 0) {
            strlcpy(s_info->profile, (char *)node->value, sizeof(s_info->profile));
        } else if (strcmp(node->name, FORMATS_TAG) == 0) {
            parse_format_names((char *)node->value, s_info);
        } else if (strcmp(node->name, SAMPLING_RATES_TAG) == 0) {
            s_info->app_type_cfg.sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
            parse_sample_rate_names((char *)node->value, s_info);
        } else if (strcmp(node->name, BIT_WIDTH_TAG) == 0) {
            s_info->app_type_cfg.bit_width = parse_bit_width_names((char *)node->value);
        } else if (strcmp(node->name, APP_TYPE_TAG) == 0) {
            s_info->app_type_cfg.app_type = parse_app_type_names(platform, (char *)node->value);
        }
        node = node->next;
    }
    list_add_tail(streams_cfg_list, &s_info->list);
}

static void load_cfg_list(cnode *root, void *platform,
                          struct listnode *streams_output_cfg_list,
                          struct listnode *streams_input_cfg_list)
{
    cnode *node = NULL;

    node = config_find(root, OUTPUTS_TAG);
    if (node != NULL) {
        node = node->first_child;
        while (node) {
            ALOGV("%s: loading output %s", __func__, node->name);
            update_streams_cfg_list(node, platform, streams_output_cfg_list);
            node = node->next;
        }
    } else {
        ALOGI("%s: could not load output, node is NULL", __func__);
    }

    node = config_find(root, INPUTS_TAG);
    if (node != NULL) {
        node = node->first_child;
        while (node) {
            ALOGV("%s: loading input %s", __func__, node->name);
            update_streams_cfg_list(node, platform, streams_input_cfg_list);
            node = node->next;
        }
    } else {
        ALOGI("%s: could not load input, node is NULL", __func__);
    }
}

static void send_app_type_cfg(void *platform, struct mixer *mixer,
                              struct listnode *streams_output_cfg_list,
                              struct listnode *streams_input_cfg_list)
{
    size_t app_type_cfg[MAX_LENGTH_MIXER_CONTROL_IN_INT] = {0};
    int length = 0, i, num_app_types = 0;
    struct listnode *node;
    bool update;
    struct mixer_ctl *ctl = NULL;
    const char *mixer_ctl_name = "App Type Config";
    struct streams_io_cfg *s_info = NULL;
    uint32_t target_bit_width = 0;

    if (!mixer) {
        ALOGE("%s: mixer is null",__func__);
        return;
    }
    ctl = mixer_get_ctl_by_name(mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",__func__, mixer_ctl_name);
        return;
    }
    app_type_cfg[length++] = num_app_types;

    if (list_empty(streams_output_cfg_list)) {
        app_type_cfg[length++] = platform_get_default_app_type_v2(platform, PCM_PLAYBACK);
        app_type_cfg[length++] = 48000;
        app_type_cfg[length++] = 16;
        num_app_types += 1;
    }
    if (list_empty(streams_input_cfg_list)) {
        app_type_cfg[length++] = platform_get_default_app_type_v2(platform, PCM_CAPTURE);
        app_type_cfg[length++] = 48000;
        app_type_cfg[length++] = 16;
        num_app_types += 1;
    }

    /* get target bit width for ADM enforce mode */
    target_bit_width = adev_get_dsp_bit_width_enforce_mode();

    list_for_each(node, streams_output_cfg_list) {
        s_info = node_to_item(node, struct streams_io_cfg, list);
        update = true;
        for (i=0; i<length; i=i+3) {
            if (app_type_cfg[i+1] == 0)
                break;
            else if (app_type_cfg[i+1] == (size_t)s_info->app_type_cfg.app_type) {
                if (app_type_cfg[i+2] < (size_t)s_info->app_type_cfg.sample_rate)
                    app_type_cfg[i+2] = s_info->app_type_cfg.sample_rate;
                if (app_type_cfg[i+3] < (size_t)s_info->app_type_cfg.bit_width)
                    app_type_cfg[i+3] = s_info->app_type_cfg.bit_width;
                /* ADM bit width = max(enforce_bit_width, bit_width from s_info */
                if (audio_extn_is_dsp_bit_width_enforce_mode_supported(s_info->flags.out_flags) &&
                    (target_bit_width > app_type_cfg[i+3]))
                    app_type_cfg[i+3] = target_bit_width;

                update = false;
                break;
            }
        }
        if (update && ((length + 3) <= MAX_LENGTH_MIXER_CONTROL_IN_INT)) {
            num_app_types += 1;
            app_type_cfg[length++] = s_info->app_type_cfg.app_type;
            app_type_cfg[length++] = s_info->app_type_cfg.sample_rate;
            app_type_cfg[length] = s_info->app_type_cfg.bit_width;
            if (audio_extn_is_dsp_bit_width_enforce_mode_supported(s_info->flags.out_flags) &&
                (target_bit_width > app_type_cfg[length]))
                app_type_cfg[length] = target_bit_width;

            length++;
        }
    }
    list_for_each(node, streams_input_cfg_list) {
        s_info = node_to_item(node, struct streams_io_cfg, list);
        update = true;
        for (i=0; i<length; i=i+3) {
            if (app_type_cfg[i+1] == 0)
                break;
            else if (app_type_cfg[i+1] == (size_t)s_info->app_type_cfg.app_type) {
                if (app_type_cfg[i+2] < (size_t)s_info->app_type_cfg.sample_rate)
                    app_type_cfg[i+2] = s_info->app_type_cfg.sample_rate;
                if (app_type_cfg[i+3] < (size_t)s_info->app_type_cfg.bit_width)
                    app_type_cfg[i+3] = s_info->app_type_cfg.bit_width;
                update = false;
                break;
            }
        }
        if (update && ((length + 3) <= MAX_LENGTH_MIXER_CONTROL_IN_INT)) {
            num_app_types += 1;
            app_type_cfg[length++] = s_info->app_type_cfg.app_type;
            app_type_cfg[length++] = s_info->app_type_cfg.sample_rate;
            app_type_cfg[length++] = s_info->app_type_cfg.bit_width;
        }
    }
    ALOGV("%s: num_app_types: %d", __func__, num_app_types);
    if (num_app_types) {
        app_type_cfg[0] = num_app_types;
        mixer_ctl_set_array(ctl, app_type_cfg, length);
    }
}

/* Function to retrieve audio vendor configs path */
void audio_get_vendor_config_path (char* config_file_path, int path_size)
{
    char vendor_sku[PROPERTY_VALUE_MAX] = {'\0'};
    if (property_get("ro.boot.product.vendor.sku", vendor_sku, "") <= 0) {
#ifdef LINUX_ENABLED
        /* Audio configs are stored in /etc */
        snprintf(config_file_path, path_size, "%s", "/etc");
#else
        /* Audio configs are stored in /vendor/etc */
        snprintf(config_file_path, path_size, "%s", "/vendor/etc");
#endif
    } else {
        /* Audio configs are stored in /vendor/etc/audio/sku_${vendor_sku} */
        snprintf(config_file_path, path_size,
            "%s%s", "/vendor/etc/audio/sku_", vendor_sku);
    }
}

void audio_extn_utils_update_streams_cfg_lists(void *platform,
                                    struct mixer *mixer,
                                    struct listnode *streams_output_cfg_list,
                                    struct listnode *streams_input_cfg_list)
{
    cnode *root;
    char *data = NULL;
    char vendor_config_path[VENDOR_CONFIG_PATH_MAX_LENGTH];
    char audio_io_policy_file[VENDOR_CONFIG_FILE_MAX_LENGTH];
    char audio_output_policy_file[VENDOR_CONFIG_FILE_MAX_LENGTH];

    ALOGV("%s", __func__);
    list_init(streams_output_cfg_list);
    list_init(streams_input_cfg_list);

    root = config_node("", "");
    if (root == NULL) {
        ALOGE("cfg_list, NULL config root");
        return;
    }

    /* Get path for audio configuration files in vendor */
    audio_get_vendor_config_path(vendor_config_path,
        sizeof(vendor_config_path));

    /* Get path for audio_io_policy_file in vendor */
    snprintf(audio_io_policy_file, sizeof(audio_io_policy_file),
        "%s/%s", vendor_config_path, AUDIO_IO_POLICY_VENDOR_CONFIG_FILE_NAME);

    /* Load audio_io_policy_file from vendor */
    data = (char *)load_file(audio_io_policy_file, NULL);

    if (data == NULL) {
        ALOGD("%s: failed to open io config file(%s), trying older config file",
              __func__, audio_io_policy_file);

        /* Get path for audio_output_policy_file in vendor */
        snprintf(audio_output_policy_file, sizeof(audio_output_policy_file),
            "%s/%s", vendor_config_path,
                AUDIO_OUTPUT_POLICY_VENDOR_CONFIG_FILE_NAME);

        /* Load audio_output_policy_file from vendor */
        data = (char *)load_file(audio_output_policy_file, NULL);

        if (data == NULL) {
            send_app_type_cfg(platform, mixer,
                              streams_output_cfg_list,
                              streams_input_cfg_list);
            ALOGE("%s: could not load io policy config!", __func__);
            free(root);
            return;
        }
    }

    config_load(root, data);
    load_cfg_list(root, platform, streams_output_cfg_list,
                                  streams_input_cfg_list);

    send_app_type_cfg(platform, mixer, streams_output_cfg_list,
                                       streams_input_cfg_list);

    config_free(root);
    free(root);
    free(data);
}

static void audio_extn_utils_dump_streams_cfg_list(
                                    struct listnode *streams_cfg_list)
{
    struct listnode *node_i, *node_j;
    struct streams_io_cfg *s_info;
    struct stream_format *sf_info;
    struct stream_sample_rate *ss_info;

    list_for_each(node_i, streams_cfg_list) {
        s_info = node_to_item(node_i, struct streams_io_cfg, list);
        ALOGV("%s: flags-%d, sample_rate-%d, bit_width-%d, app_type-%d",
               __func__, s_info->flags.out_flags, s_info->app_type_cfg.sample_rate,
               s_info->app_type_cfg.bit_width, s_info->app_type_cfg.app_type);
        list_for_each(node_j, &s_info->format_list) {
            sf_info = node_to_item(node_j, struct stream_format, list);
            ALOGV("format-%x", sf_info->format);
        }
        list_for_each(node_j, &s_info->sample_rate_list) {
            ss_info = node_to_item(node_j, struct stream_sample_rate, list);
            ALOGV("sample rate-%d", ss_info->sample_rate);
        }
    }
}

void audio_extn_utils_dump_streams_cfg_lists(
                                    struct listnode *streams_output_cfg_list,
                                    struct listnode *streams_input_cfg_list)
{
    ALOGV("%s", __func__);
    audio_extn_utils_dump_streams_cfg_list(streams_output_cfg_list);
    audio_extn_utils_dump_streams_cfg_list(streams_input_cfg_list);
}

static void audio_extn_utils_release_streams_cfg_list(
                                    struct listnode *streams_cfg_list)
{
    struct listnode *node_i, *node_j;
    struct streams_io_cfg *s_info;

    ALOGV("%s", __func__);

    while (!list_empty(streams_cfg_list)) {
        node_i = list_head(streams_cfg_list);
        s_info = node_to_item(node_i, struct streams_io_cfg, list);
        while (!list_empty(&s_info->format_list)) {
            node_j = list_head(&s_info->format_list);
            list_remove(node_j);
            free(node_to_item(node_j, struct stream_format, list));
        }
        while (!list_empty(&s_info->sample_rate_list)) {
            node_j = list_head(&s_info->sample_rate_list);
            list_remove(node_j);
            free(node_to_item(node_j, struct stream_sample_rate, list));
        }
        list_remove(node_i);
        free(node_to_item(node_i, struct streams_io_cfg, list));
    }
}

void audio_extn_utils_release_streams_cfg_lists(
                                    struct listnode *streams_output_cfg_list,
                                    struct listnode *streams_input_cfg_list)
{
    ALOGV("%s", __func__);
    audio_extn_utils_release_streams_cfg_list(streams_output_cfg_list);
    audio_extn_utils_release_streams_cfg_list(streams_input_cfg_list);
}

static bool set_app_type_cfg(struct streams_io_cfg *s_info,
                    struct stream_app_type_cfg *app_type_cfg,
                    uint32_t sample_rate, uint32_t bit_width)
 {
    struct listnode *node_i;
    struct stream_sample_rate *ss_info;
    list_for_each(node_i, &s_info->sample_rate_list) {
        ss_info = node_to_item(node_i, struct stream_sample_rate, list);
        if ((sample_rate <= ss_info->sample_rate) &&
            (bit_width == s_info->app_type_cfg.bit_width)) {

            app_type_cfg->app_type = s_info->app_type_cfg.app_type;
            app_type_cfg->sample_rate = ss_info->sample_rate;
            app_type_cfg->bit_width = s_info->app_type_cfg.bit_width;
            ALOGV("%s app_type_cfg->app_type %d, app_type_cfg->sample_rate %d, app_type_cfg->bit_width %d",
                   __func__, app_type_cfg->app_type, app_type_cfg->sample_rate, app_type_cfg->bit_width);
            return true;
        }
    }
    /*
     * Reiterate through the list assuming dafault sample rate.
     * Handles scenario where input sample rate is higher
     * than all sample rates in list for the input bit width.
     */
    sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;

    list_for_each(node_i, &s_info->sample_rate_list) {
        ss_info = node_to_item(node_i, struct stream_sample_rate, list);
        if ((sample_rate <= ss_info->sample_rate) &&
            (bit_width == s_info->app_type_cfg.bit_width)) {
            app_type_cfg->app_type = s_info->app_type_cfg.app_type;
            app_type_cfg->sample_rate = sample_rate;
            app_type_cfg->bit_width = s_info->app_type_cfg.bit_width;
            ALOGV("%s Assuming sample rate. app_type_cfg->app_type %d, app_type_cfg->sample_rate %d, app_type_cfg->bit_width %d",
                   __func__, app_type_cfg->app_type, app_type_cfg->sample_rate, app_type_cfg->bit_width);
            return true;
        }
    }
    return false;
}

void audio_extn_utils_update_stream_input_app_type_cfg(void *platform,
                                  struct listnode *streams_input_cfg_list,
                                  struct listnode *devices __unused,
                                  audio_input_flags_t flags,
                                  audio_format_t format,
                                  uint32_t sample_rate,
                                  uint32_t bit_width,
                                  char* profile,
                                  struct stream_app_type_cfg *app_type_cfg)
{
    struct listnode *node_i, *node_j;
    struct streams_io_cfg *s_info;
    struct stream_format *sf_info;

    ALOGV("%s: flags: 0x%x, format: 0x%x sample_rate %d, profile %s",
           __func__, flags, format, sample_rate, profile);

    list_for_each(node_i, streams_input_cfg_list) {
        s_info = node_to_item(node_i, struct streams_io_cfg, list);
        /* Along with flags do profile matching if set at either end.*/
        if (s_info->flags.in_flags == flags &&
            ((profile[0] == '\0' && s_info->profile[0] == '\0') ||
             strncmp(s_info->profile, profile, sizeof(s_info->profile)) == 0)) {
            list_for_each(node_j, &s_info->format_list) {
                sf_info = node_to_item(node_j, struct stream_format, list);
                if (sf_info->format == format) {
                    if (set_app_type_cfg(s_info, app_type_cfg, sample_rate, bit_width))
                        return;
                }
            }
        }
    }
    ALOGW("%s: App type could not be selected. Falling back to default", __func__);
    app_type_cfg->app_type = platform_get_default_app_type_v2(platform, PCM_CAPTURE);
    app_type_cfg->sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
    app_type_cfg->bit_width = 16;
}

void audio_extn_utils_update_stream_output_app_type_cfg(void *platform,
                                  struct listnode *streams_output_cfg_list,
                                  struct listnode *devices,
                                  audio_output_flags_t flags,
                                  audio_format_t format,
                                  uint32_t sample_rate,
                                  uint32_t bit_width,
                                  audio_channel_mask_t channel_mask,
                                  char *profile,
                                  struct stream_app_type_cfg *app_type_cfg)
{
    struct listnode *node_i, *node_j;
    struct streams_io_cfg *s_info;
    struct stream_format *sf_info;
    char value[PROPERTY_VALUE_MAX] = {0};

    if (compare_device_type(devices, AUDIO_DEVICE_OUT_SPEAKER)) {
        int bw = platform_get_snd_device_bit_width(SND_DEVICE_OUT_SPEAKER);
        if ((-ENOSYS != bw) && (bit_width > (uint32_t)bw))
            bit_width = (uint32_t)bw;
        else if (-ENOSYS == bw)
            bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
        sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
        ALOGI("%s Allowing 24 and above bits playback on speaker ONLY at default sampling rate", __func__);
    }

    property_get("vendor.audio.playback.mch.downsample",value,"");
    if (!strncmp("true", value, sizeof("true"))) {
        if ((popcount(channel_mask) > 2) &&
                (sample_rate > CODEC_BACKEND_DEFAULT_SAMPLE_RATE) &&
                !(flags & (audio_output_flags_t)AUDIO_OUTPUT_FLAG_COMPRESS_PASSTHROUGH))  {
                    sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
                    ALOGD("%s: MCH session defaulting sample rate to %d",
                               __func__, sample_rate);
        }
    }

    /* Set sampling rate to 176.4 for DSD64
     * and 352.8Khz for DSD128.
     * Set Bit Width to 16. output will be 16 bit
     * post DoP in ASM.
     */
    if ((flags & (audio_output_flags_t)AUDIO_OUTPUT_FLAG_COMPRESS_PASSTHROUGH) &&
        (format == AUDIO_FORMAT_DSD)) {
        bit_width = 16;
        if (sample_rate == INPUT_SAMPLING_RATE_DSD64)
            sample_rate = OUTPUT_SAMPLING_RATE_DSD64;
        else if (sample_rate == INPUT_SAMPLING_RATE_DSD128)
            sample_rate = OUTPUT_SAMPLING_RATE_DSD128;
    }


    ALOGV("%s: flags: %x, format: %x sample_rate %d, profile %s, app_type %d",
           __func__, flags, format, sample_rate, profile, app_type_cfg->app_type);
    list_for_each(node_i, streams_output_cfg_list) {
        s_info = node_to_item(node_i, struct streams_io_cfg, list);
        /* Along with flags do profile matching if set at either end.*/
        if (s_info->flags.out_flags == flags &&
            ((profile[0] == '\0' && s_info->profile[0] == '\0') ||
             strncmp(s_info->profile, profile, sizeof(s_info->profile)) == 0)) {
            list_for_each(node_j, &s_info->format_list) {
                sf_info = node_to_item(node_j, struct stream_format, list);
                if (sf_info->format == format) {
                    if (set_app_type_cfg(s_info, app_type_cfg, sample_rate, bit_width))
                        return;
                }
            }
        }
    }
    list_for_each(node_i, streams_output_cfg_list) {
        s_info = node_to_item(node_i, struct streams_io_cfg, list);
        if (s_info->flags.out_flags == AUDIO_OUTPUT_FLAG_PRIMARY) {
            ALOGV("Compatible output profile not found.");
            app_type_cfg->app_type = s_info->app_type_cfg.app_type;
            app_type_cfg->sample_rate = s_info->app_type_cfg.sample_rate;
            app_type_cfg->bit_width = s_info->app_type_cfg.bit_width;
            ALOGV("%s Default to primary output: App type: %d sample_rate %d",
                  __func__, s_info->app_type_cfg.app_type, app_type_cfg->sample_rate);
            return;
        }
    }
    ALOGW("%s: App type could not be selected. Falling back to default", __func__);
    app_type_cfg->app_type = platform_get_default_app_type(platform);
    app_type_cfg->sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
    app_type_cfg->bit_width = 16;
}

static bool audio_is_this_native_usecase(struct audio_usecase *uc)
{
    bool native_usecase = false;
    struct stream_out *out = (struct stream_out*) uc->stream.out;

    if (PCM_PLAYBACK == uc->type && out != NULL &&
        NATIVE_AUDIO_MODE_INVALID != platform_get_native_support() &&
        is_offload_usecase(uc->id) &&
        (out->sample_rate == OUTPUT_SAMPLING_RATE_44100))
        native_usecase = true;

    return native_usecase;
}

bool audio_extn_is_dsp_bit_width_enforce_mode_supported(audio_output_flags_t flags)
{
    /* DSP bitwidth enforce mode for ADM and AFE:
    * includes:
    *     deep buffer, low latency, direct pcm and offload.
    * excludes:
    *     ull(raw+fast), VOIP.
    */
    if ((flags & AUDIO_OUTPUT_FLAG_VOIP_RX) ||
            ((flags & AUDIO_OUTPUT_FLAG_RAW) &&
            (flags & AUDIO_OUTPUT_FLAG_FAST)))
        return false;


    if ((flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) ||
            (flags & AUDIO_OUTPUT_FLAG_DIRECT) ||
            (flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) ||
            (flags & AUDIO_OUTPUT_FLAG_PRIMARY))
        return true;
    else
        return false;
}

static inline bool audio_is_vr_mode_on(struct audio_device *(__attribute__((unused)) adev))
{
    return adev->vr_audio_mode_enabled;
}

static void audio_extn_btsco_get_sample_rate(int snd_device, int *sample_rate)
{
    switch (snd_device) {
    case SND_DEVICE_OUT_BT_SCO:
    case SND_DEVICE_IN_BT_SCO_MIC:
    case SND_DEVICE_IN_BT_SCO_MIC_NREC:
        *sample_rate = 8000;
        break;
    case SND_DEVICE_OUT_BT_SCO_WB:
    case SND_DEVICE_IN_BT_SCO_MIC_WB:
    case SND_DEVICE_IN_BT_SCO_MIC_WB_NREC:
        *sample_rate = 16000;
        break;
    default:
        ALOGD("%s:Not a BT SCO device, need not update sampling rate\n", __func__);
        break;
    }
}

void audio_extn_utils_update_stream_app_type_cfg_for_usecase(
                                    struct audio_device *adev,
                                    struct audio_usecase *usecase)
{
    ALOGV("%s", __func__);

    switch(usecase->type) {
    case PCM_PLAYBACK:
        audio_extn_utils_update_stream_output_app_type_cfg(adev->platform,
                                                &adev->streams_output_cfg_list,
                                                &usecase->stream.out->device_list,
                                                usecase->stream.out->flags,
                                                usecase->stream.out->hal_op_format,
                                                usecase->stream.out->sample_rate,
                                                usecase->stream.out->bit_width,
                                                usecase->stream.out->channel_mask,
                                                usecase->stream.out->profile,
                                                &usecase->stream.out->app_type_cfg);
        ALOGV("%s Selected apptype: %d", __func__, usecase->stream.out->app_type_cfg.app_type);
        break;
    case PCM_CAPTURE:
        if (usecase->id == USECASE_AUDIO_RECORD_VOIP)
            usecase->stream.in->app_type_cfg.app_type = APP_TYPE_VOIP_AUDIO;
        else
            audio_extn_utils_update_stream_input_app_type_cfg(adev->platform,
                                                &adev->streams_input_cfg_list,
                                                &usecase->stream.in->device_list,
                                                usecase->stream.in->flags,
                                                usecase->stream.in->format,
                                                usecase->stream.in->sample_rate,
                                                usecase->stream.in->bit_width,
                                                usecase->stream.in->profile,
                                                &usecase->stream.in->app_type_cfg);
        ALOGV("%s Selected apptype: %d", __func__, usecase->stream.in->app_type_cfg.app_type);
        break;
    case TRANSCODE_LOOPBACK_RX :
        audio_extn_utils_update_stream_output_app_type_cfg(adev->platform,
                                                &adev->streams_output_cfg_list,
                                                &usecase->stream.inout->out_config.device_list,
                                                0,
                                                usecase->stream.inout->out_config.format,
                                                usecase->stream.inout->out_config.sample_rate,
                                                usecase->stream.inout->out_config.bit_width,
                                                usecase->stream.inout->out_config.channel_mask,
                                                usecase->stream.inout->profile,
                                                &usecase->stream.inout->out_app_type_cfg);
        ALOGV("%s Selected apptype: %d", __func__, usecase->stream.inout->out_app_type_cfg.app_type);
        break;
    case PCM_HFP_CALL:
        switch (usecase->id) {
        case USECASE_AUDIO_HFP_SCO:
        case USECASE_AUDIO_HFP_SCO_WB:
            audio_extn_btsco_get_sample_rate(usecase->out_snd_device,
                                             &usecase->out_app_type_cfg.sample_rate);
            usecase->in_app_type_cfg.sample_rate = usecase->out_app_type_cfg.sample_rate;
            break;
        case USECASE_AUDIO_HFP_SCO_DOWNLINK:
        case USECASE_AUDIO_HFP_SCO_WB_DOWNLINK:
            audio_extn_btsco_get_sample_rate(usecase->in_snd_device,
                                             &usecase->in_app_type_cfg.sample_rate);
            usecase->out_app_type_cfg.sample_rate = usecase->in_app_type_cfg.sample_rate;
            break;
        default:
            ALOGE("%s: usecase id (%d) not supported, use default sample rate",
                __func__, usecase->id);
            usecase->in_app_type_cfg.sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
            usecase->out_app_type_cfg.sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
            break;
        }
        /* update out_app_type_cfg */
        usecase->out_app_type_cfg.bit_width =
                                platform_get_snd_device_bit_width(usecase->out_snd_device);
        usecase->out_app_type_cfg.app_type =
                                platform_get_default_app_type_v2(adev->platform, PCM_PLAYBACK);
        /* update in_app_type_cfg */
        usecase->in_app_type_cfg.bit_width =
                                platform_get_snd_device_bit_width(usecase->in_snd_device);
        usecase->in_app_type_cfg.app_type =
                                platform_get_default_app_type_v2(adev->platform, PCM_CAPTURE);
        ALOGV("%s Selected apptype: playback %d capture %d",
            __func__, usecase->out_app_type_cfg.app_type, usecase->in_app_type_cfg.app_type);
        break;
    default:
        ALOGE("%s: app type cfg not supported for usecase type (%d)",
            __func__, usecase->type);
    }
}

static int set_stream_app_type_mixer_ctrl(struct audio_device *adev,
                                          int pcm_device_id, int app_type,
                                          int acdb_dev_id, int sample_rate,
                                          int stream_type,
                                          snd_device_t snd_device)
{

    char mixer_ctl_name[MAX_LENGTH_MIXER_CONTROL_IN_INT];
    struct mixer_ctl *ctl;
    int app_type_cfg[MAX_LENGTH_MIXER_CONTROL_IN_INT], len = 0, rc = 0;
    int snd_device_be_idx = -1;

    if (stream_type == PCM_PLAYBACK) {
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
             "Audio Stream %d App Type Cfg", pcm_device_id);
    } else if (stream_type == PCM_CAPTURE) {
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
             "Audio Stream Capture %d App Type Cfg", pcm_device_id);
    }

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
             __func__, mixer_ctl_name);
        rc = -EINVAL;
        goto exit;
    }
    app_type_cfg[len++] = app_type;
    app_type_cfg[len++] = acdb_dev_id;
    app_type_cfg[len++] = sample_rate;

    snd_device_be_idx = platform_get_snd_device_backend_index(snd_device);
    if (snd_device_be_idx > 0)
        app_type_cfg[len++] = snd_device_be_idx;
    ALOGV("%s: stream type %d app_type %d, acdb_dev_id %d "
          "sample rate %d, snd_device_be_idx %d",
          __func__, stream_type, app_type, acdb_dev_id, sample_rate,
          snd_device_be_idx);
    mixer_ctl_set_array(ctl, app_type_cfg, len);

exit:
    return rc;
}

static int audio_extn_utils_send_app_type_cfg_hfp(struct audio_device *adev,
                                       struct audio_usecase *usecase)
{
    int pcm_device_id, acdb_dev_id = 0, snd_device = usecase->out_snd_device;
    int32_t sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
    int app_type = 0, rc = 0;
    bool is_bus_dev_usecase = false;

    ALOGV("%s", __func__);

    if (usecase->type != PCM_HFP_CALL) {
        ALOGV("%s: not a HFP path, no need to cfg app type", __func__);
        rc = 0;
        goto exit_send_app_type_cfg;
    }
    if ((usecase->id != USECASE_AUDIO_HFP_SCO) &&
        (usecase->id != USECASE_AUDIO_HFP_SCO_WB) &&
        (usecase->id != USECASE_AUDIO_HFP_SCO_DOWNLINK) &&
        (usecase->id != USECASE_AUDIO_HFP_SCO_WB_DOWNLINK)) {
        ALOGV("%s: a usecase where app type cfg is not required", __func__);
        rc = 0;
        goto exit_send_app_type_cfg;
    }

    if (compare_device_type(&usecase->device_list, AUDIO_DEVICE_OUT_BUS))
        is_bus_dev_usecase = true;

    snd_device = usecase->out_snd_device;
    pcm_device_id = platform_get_pcm_device_id(usecase->id, PCM_PLAYBACK);

    acdb_dev_id = platform_get_snd_device_acdb_id(snd_device);
    if (acdb_dev_id < 0) {
        ALOGE("%s: Couldn't get the acdb dev id", __func__);
        rc = -EINVAL;
        goto exit_send_app_type_cfg;
    }

    if (usecase->type == PCM_HFP_CALL) {

        /* config HFP session:1 playback path */
        if (is_bus_dev_usecase) {
            app_type = usecase->out_app_type_cfg.app_type;
            sample_rate= usecase->out_app_type_cfg.sample_rate;
        } else {
            snd_device = SND_DEVICE_NONE; // use legacy behavior
            app_type = platform_get_default_app_type_v2(adev->platform, PCM_PLAYBACK);
            sample_rate= CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
        }
        rc = set_stream_app_type_mixer_ctrl(adev, pcm_device_id, app_type,
                                            acdb_dev_id, sample_rate,
                                            PCM_PLAYBACK,
                                            snd_device);
        if (rc < 0)
            goto exit_send_app_type_cfg;

        /* config HFP session:1 capture path */
        if (is_bus_dev_usecase) {
            snd_device = usecase->in_snd_device;
            pcm_device_id = platform_get_pcm_device_id(usecase->id, PCM_CAPTURE);
            acdb_dev_id = platform_get_snd_device_acdb_id(snd_device);
            if (acdb_dev_id < 0) {
                ALOGE("%s: Couldn't get the acdb dev id", __func__);
                rc = -EINVAL;
                goto exit_send_app_type_cfg;
            }
            app_type = usecase->in_app_type_cfg.app_type;
            sample_rate= usecase->in_app_type_cfg.sample_rate;
        } else {
            snd_device = SND_DEVICE_NONE; // use legacy behavior
            app_type = platform_get_default_app_type_v2(adev->platform, PCM_CAPTURE);
        }
        rc = set_stream_app_type_mixer_ctrl(adev, pcm_device_id, app_type,
                                            acdb_dev_id, sample_rate,
                                            PCM_CAPTURE,
                                            snd_device);
        if (rc < 0)
            goto exit_send_app_type_cfg;

        if (is_bus_dev_usecase)
            goto exit_send_app_type_cfg;

        /* config HFP session:2 capture path */
        pcm_device_id = HFP_ASM_RX_TX;
        snd_device = usecase->in_snd_device;
        acdb_dev_id = platform_get_snd_device_acdb_id(snd_device);
        if (acdb_dev_id <= 0) {
            ALOGE("%s: Couldn't get the acdb dev id", __func__);
            rc = -EINVAL;
            goto exit_send_app_type_cfg;
        }
        app_type = platform_get_default_app_type_v2(adev->platform, PCM_CAPTURE);
        rc = set_stream_app_type_mixer_ctrl(adev, pcm_device_id, app_type,
                                            acdb_dev_id, sample_rate, PCM_CAPTURE,
                                            SND_DEVICE_NONE);
        if (rc < 0)
            goto exit_send_app_type_cfg;

        /* config HFP session:2 playback path */
        app_type = platform_get_default_app_type_v2(adev->platform, PCM_PLAYBACK);
        rc = set_stream_app_type_mixer_ctrl(adev, pcm_device_id, app_type,
                                            acdb_dev_id, sample_rate,
                                            PCM_PLAYBACK, SND_DEVICE_NONE);
        if (rc < 0)
            goto exit_send_app_type_cfg;
    }

    rc = 0;
exit_send_app_type_cfg:
    return rc;
}

int audio_extn_utils_get_app_sample_rate_for_device(
                              struct audio_device *adev,
                              struct audio_usecase *usecase, int snd_device)
{
    char value[PROPERTY_VALUE_MAX] = {0};
    int sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;

    if ((usecase->type == PCM_PLAYBACK) && (usecase->stream.out != NULL)) {
        property_get("vendor.audio.playback.mch.downsample",value,"");
        if (!strncmp("true", value, sizeof("true"))) {
            if ((popcount(usecase->stream.out->channel_mask) > 2) &&
                (usecase->stream.out->app_type_cfg.sample_rate > CODEC_BACKEND_DEFAULT_SAMPLE_RATE) &&
                !(usecase->stream.out->flags &
                            (audio_output_flags_t)AUDIO_OUTPUT_FLAG_COMPRESS_PASSTHROUGH))
               sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
        }

        if (usecase->id == USECASE_AUDIO_PLAYBACK_VOIP) {
            usecase->stream.out->app_type_cfg.sample_rate = usecase->stream.out->sample_rate;
        } else if ((snd_device == SND_DEVICE_OUT_HDMI ||
                    snd_device == SND_DEVICE_OUT_USB_HEADSET ||
                    snd_device == SND_DEVICE_OUT_DISPLAY_PORT) &&
                   (usecase->stream.out->sample_rate >= OUTPUT_SAMPLING_RATE_44100)) {
             /*
              * To best utlize DSP, check if the stream sample rate is supported/multiple of
              * configured device sample rate, if not update the COPP rate to be equal to the
              * device sample rate, else open COPP at stream sample rate
              */
              platform_check_and_update_copp_sample_rate(adev->platform, snd_device,
                                      usecase->stream.out->sample_rate,
                                      &usecase->stream.out->app_type_cfg.sample_rate);
        } else if (((snd_device != SND_DEVICE_OUT_HEADPHONES_44_1 &&
                     !audio_is_this_native_usecase(usecase)) &&
            usecase->stream.out->sample_rate == OUTPUT_SAMPLING_RATE_44100) ||
            (usecase->stream.out->sample_rate < OUTPUT_SAMPLING_RATE_44100)) {
            /* Reset to default if no native stream is active*/
            usecase->stream.out->app_type_cfg.sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
        } else if (snd_device == SND_DEVICE_OUT_BT_A2DP) {
                 /*
                  * For a2dp playback get encoder sampling rate and set copp sampling rate,
                  * for bit width use the stream param only.
                  */
                   audio_extn_a2dp_get_enc_sample_rate(&usecase->stream.out->app_type_cfg.sample_rate);
                   ALOGI("%s using %d sample rate rate for A2DP CoPP",
                        __func__, usecase->stream.out->app_type_cfg.sample_rate);
        } else if (compare_device_type(&usecase->stream.out->device_list,
                                       AUDIO_DEVICE_OUT_SPEAKER)) {
            usecase->stream.out->app_type_cfg.sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
        }
        audio_extn_btsco_get_sample_rate(snd_device, &usecase->stream.out->app_type_cfg.sample_rate);
        sample_rate = usecase->stream.out->app_type_cfg.sample_rate;

        if (((usecase->stream.out->format == AUDIO_FORMAT_E_AC3) ||
            (usecase->stream.out->format == AUDIO_FORMAT_E_AC3_JOC) ||
            (usecase->stream.out->format == AUDIO_FORMAT_DOLBY_TRUEHD))
            && audio_extn_passthru_is_passthrough_stream(usecase->stream.out)
            && !audio_extn_passthru_is_convert_supported(adev, usecase->stream.out)) {
            sample_rate = sample_rate * 4;
            if (sample_rate > HDMI_PASSTHROUGH_MAX_SAMPLE_RATE)
                sample_rate = HDMI_PASSTHROUGH_MAX_SAMPLE_RATE;
        }
    } else if (usecase->type == PCM_CAPTURE) {
        if (usecase->stream.in != NULL) {
            if (usecase->id == USECASE_AUDIO_RECORD_VOIP)
                usecase->stream.in->app_type_cfg.sample_rate = usecase->stream.in->sample_rate;
            if (voice_is_in_call_rec_stream(usecase->stream.in)) {
                audio_extn_btsco_get_sample_rate(usecase->in_snd_device,
                                                 &usecase->stream.in->app_type_cfg.sample_rate);
            } else {
                audio_extn_btsco_get_sample_rate(snd_device,
                                                 &usecase->stream.in->app_type_cfg.sample_rate);
            }
            sample_rate = usecase->stream.in->app_type_cfg.sample_rate;
        } else if (usecase->id == USECASE_AUDIO_SPKR_CALIB_TX) {
            if ((property_get("vendor.audio.spkr_prot.tx.sampling_rate", value, NULL) > 0))
                sample_rate = atoi(value);
            else
                sample_rate = SAMPLE_RATE_8000;
        }
    } else if (usecase->type == TRANSCODE_LOOPBACK_RX) {
        sample_rate = usecase->stream.inout->out_config.sample_rate;
    }
    return sample_rate;
}

static int send_app_type_cfg_for_device(struct audio_device *adev,
                                        struct audio_usecase *usecase,
                                        int split_snd_device)
{
    char mixer_ctl_name[MAX_LENGTH_MIXER_CONTROL_IN_INT];
    size_t app_type_cfg[MAX_LENGTH_MIXER_CONTROL_IN_INT] = {0};
    int len = 0, rc;
    struct mixer_ctl *ctl;
    int pcm_device_id = 0, acdb_dev_id, app_type;
    int snd_device = split_snd_device, snd_device_be_idx = -1;
    int32_t sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
    struct streams_io_cfg *s_info = NULL;
    struct listnode *node = NULL;
    int bd_app_type = 0;

    ALOGV("%s: usecase->out_snd_device %s, usecase->in_snd_device %s, split_snd_device %s",
          __func__, platform_get_snd_device_name(usecase->out_snd_device),
          platform_get_snd_device_name(usecase->in_snd_device),
          platform_get_snd_device_name(split_snd_device));

    if (usecase->type != PCM_PLAYBACK && usecase->type != PCM_CAPTURE &&
        usecase->type != TRANSCODE_LOOPBACK_RX) {
        ALOGE("%s: not a playback/capture path, no need to cfg app type", __func__);
        rc = 0;
        goto exit_send_app_type_cfg;
    }
    if ((usecase->id != USECASE_AUDIO_PLAYBACK_DEEP_BUFFER) &&
        (usecase->id != USECASE_AUDIO_PLAYBACK_WITH_HAPTICS) &&
        (usecase->id != USECASE_AUDIO_PLAYBACK_LOW_LATENCY) &&
        (usecase->id != USECASE_AUDIO_PLAYBACK_MULTI_CH) &&
        (usecase->id != USECASE_AUDIO_PLAYBACK_ULL) &&
        (usecase->id != USECASE_AUDIO_PLAYBACK_VOIP) &&
        (usecase->id != USECASE_AUDIO_TRANSCODE_LOOPBACK_RX) &&
        (!is_interactive_usecase(usecase->id)) &&
        (!is_offload_usecase(usecase->id)) &&
        (usecase->type != PCM_CAPTURE) &&
        (!audio_extn_auto_hal_is_bus_device_usecase(usecase->id))) {
        ALOGV("%s: a rx/tx/loopback path where app type cfg is not required %d", __func__, usecase->id);
        rc = 0;
        goto exit_send_app_type_cfg;
    }

    //if VR is active then only send the mixer control
    if (usecase->id == USECASE_AUDIO_PLAYBACK_ULL && !audio_is_vr_mode_on(adev)) {
            ALOGI("ULL doesnt need sending app type cfg, returning");
            rc = 0;
            goto exit_send_app_type_cfg;
    }

    if (usecase->type == PCM_PLAYBACK || usecase->type == TRANSCODE_LOOPBACK_RX) {
        pcm_device_id = platform_get_pcm_device_id(usecase->id, PCM_PLAYBACK);
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
            "Audio Stream %d App Type Cfg", pcm_device_id);
    } else if (usecase->type == PCM_CAPTURE) {
        pcm_device_id = platform_get_pcm_device_id(usecase->id, PCM_CAPTURE);
        snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
            "Audio Stream Capture %d App Type Cfg", pcm_device_id);
    }

    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s", __func__,
              mixer_ctl_name);
        rc = -EINVAL;
        goto exit_send_app_type_cfg;
    }
    snd_device = platform_get_spkr_prot_snd_device(snd_device);
    if (voice_is_in_call_rec_stream(usecase->stream.in) && usecase->type == PCM_CAPTURE) {
        snd_device_t voice_device = voice_get_incall_rec_snd_device(usecase->in_snd_device);
        acdb_dev_id = platform_get_snd_device_acdb_id(voice_device);
        ALOGV("acdb id for voice call use case %d", acdb_dev_id);
    } else {
        acdb_dev_id = platform_get_snd_device_acdb_id(snd_device);
    }
    if (acdb_dev_id <= 0) {
        ALOGE("%s: Couldn't get the acdb dev id", __func__);
        rc = -EINVAL;
        goto exit_send_app_type_cfg;
    }

    snd_device_be_idx = platform_get_snd_device_backend_index(snd_device);
    if (snd_device_be_idx < 0) {
        ALOGE("%s: Couldn't get the backend index for snd device %s ret=%d",
              __func__, platform_get_snd_device_name(snd_device),
              snd_device_be_idx);
    }

    sample_rate = audio_extn_utils_get_app_sample_rate_for_device(adev, usecase, snd_device);

    if ((usecase->type == PCM_PLAYBACK) && (usecase->stream.out != NULL)) {
        /* Interactive streams are supported with only direct app type id.
         * Get Direct profile app type and use it for interactive streams
         */
        list_for_each(node, &adev->streams_output_cfg_list) {
            s_info = node_to_item(node, struct streams_io_cfg, list);
            if (s_info->flags.out_flags == (audio_output_flags_t)(AUDIO_OUTPUT_FLAG_BD |
                                            AUDIO_OUTPUT_FLAG_DIRECT_PCM |
                                            AUDIO_OUTPUT_FLAG_DIRECT))
                bd_app_type = s_info->app_type_cfg.app_type;
        }
        if (usecase->stream.out->flags == (audio_output_flags_t)AUDIO_OUTPUT_FLAG_INTERACTIVE)
            app_type = bd_app_type;
        else
            app_type = usecase->stream.out->app_type_cfg.app_type;
        app_type_cfg[len++] = app_type;
        app_type_cfg[len++] = acdb_dev_id;
        app_type_cfg[len++] = sample_rate;

        if (snd_device_be_idx > 0)
            app_type_cfg[len++] = snd_device_be_idx;

         ALOGI("%s PLAYBACK app_type %d, acdb_dev_id %d, sample_rate %d, snd_device_be_idx %d",
             __func__, app_type, acdb_dev_id, sample_rate, snd_device_be_idx);

    } else if ((usecase->type == PCM_CAPTURE) && (usecase->stream.in != NULL)) {
        app_type = usecase->stream.in->app_type_cfg.app_type;
        app_type_cfg[len++] = app_type;
        app_type_cfg[len++] = acdb_dev_id;
        app_type_cfg[len++] = sample_rate;
        if (snd_device_be_idx > 0)
            app_type_cfg[len++] = snd_device_be_idx;
        ALOGI("%s CAPTURE app_type %d, acdb_dev_id %d, sample_rate %d, snd_device_be_idx %d",
           __func__, app_type, acdb_dev_id, sample_rate, snd_device_be_idx);
    } else {
        app_type = platform_get_default_app_type_v2(adev->platform, usecase->type);
        if(usecase->type == TRANSCODE_LOOPBACK_RX) {
            app_type = usecase->stream.inout->out_app_type_cfg.app_type;
        }
        app_type_cfg[len++] = app_type;
        app_type_cfg[len++] = acdb_dev_id;
        app_type_cfg[len++] = sample_rate;
        if (snd_device_be_idx > 0)
            app_type_cfg[len++] = snd_device_be_idx;
        ALOGI("%s default app_type %d, acdb_dev_id %d, sample_rate %d, snd_device_be_idx %d",
              __func__, app_type, acdb_dev_id, sample_rate, snd_device_be_idx);
    }

    if(ctl)
        mixer_ctl_set_array(ctl, app_type_cfg, len);
    rc = 0;
exit_send_app_type_cfg:
    return rc;
}

static int audio_extn_utils_check_input_parameters(uint32_t sample_rate,
                                  audio_format_t format,
                                  int channel_count)
{
    int ret = 0;

    if (((format != AUDIO_FORMAT_PCM_16_BIT) && (format != AUDIO_FORMAT_PCM_8_24_BIT) &&
        (format != AUDIO_FORMAT_PCM_24_BIT_PACKED) && (format != AUDIO_FORMAT_PCM_32_BIT) &&
        (format != AUDIO_FORMAT_PCM_FLOAT)) &&
        !voice_extn_compress_voip_is_format_supported(format) &&
        !audio_extn_compr_cap_format_supported(format) &&
        !audio_extn_cin_format_supported(format))
            ret = -EINVAL;

    switch (channel_count) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 6:
    case 8:
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

static inline uint32_t audio_extn_utils_nearest_multiple(uint32_t num, uint32_t multiplier)
{
    uint32_t remainder = 0;

    if (!multiplier)
        return num;

    remainder = num % multiplier;
    if (remainder)
        num += (multiplier - remainder);

    return num;
}

static inline uint32_t audio_extn_utils_lcm(uint32_t num1, uint32_t num2)
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

int audio_extn_utils_send_app_type_cfg(struct audio_device *adev,
                                       struct audio_usecase *usecase)
{
    int i, num_devices = 0;
    snd_device_t new_snd_devices[SND_DEVICE_OUT_END] = {0};
    snd_device_t in_snd_device = usecase->in_snd_device;
    int rc = 0;

    if (usecase->type == PCM_HFP_CALL) {
        return audio_extn_utils_send_app_type_cfg_hfp(adev, usecase);
    }

    switch (usecase->type) {
    case PCM_PLAYBACK:
    case TRANSCODE_LOOPBACK_RX:
        ALOGD("%s: usecase->out_snd_device %s",
              __func__, platform_get_snd_device_name(usecase->out_snd_device));
        /* check for out combo device */
        if (platform_split_snd_device(adev->platform,
                                      usecase->out_snd_device,
                                      &num_devices, new_snd_devices)) {
            new_snd_devices[0] = usecase->out_snd_device;
            num_devices = 1;
        }
        break;
    case PCM_CAPTURE:
        ALOGD("%s: usecase->in_snd_device %s",
              __func__, platform_get_snd_device_name(usecase->in_snd_device));
        if (voice_is_in_call_rec_stream(usecase->stream.in)) {
            in_snd_device = voice_get_incall_rec_backend_device(usecase->stream.in);
        }
        /* check for in combo device */
        if (platform_split_snd_device(adev->platform,
                                      in_snd_device,
                                      &num_devices, new_snd_devices)) {
            new_snd_devices[0] = in_snd_device;
            num_devices = 1;
        }
        break;
    default:
        ALOGI("%s: not a playback/capture path, no need to cfg app type", __func__);
        rc = 0;
        break;
    }

    for (i = 0; i < num_devices; i++) {
        rc = send_app_type_cfg_for_device(adev, usecase, new_snd_devices[i]);
        if (rc)
            break;
    }

    return rc;
}

int read_line_from_file(const char *path, char *buf, size_t count)
{
    char * fgets_ret;
    FILE * fd;
    int rv;

    fd = fopen(path, "r");
    if (fd == NULL)
        return -1;

    fgets_ret = fgets(buf, (int)count, fd);
    if (NULL != fgets_ret) {
        rv = (int)strlen(buf);
    } else {
        rv = ferror(fd);
    }
    fclose(fd);

   return rv;
}

/*Translates ALSA formats to AOSP PCM formats*/
audio_format_t alsa_format_to_hal(uint32_t alsa_format)
{
    audio_format_t format;

    switch(alsa_format) {
    case SNDRV_PCM_FORMAT_S16_LE:
        format = AUDIO_FORMAT_PCM_16_BIT;
        break;
    case SNDRV_PCM_FORMAT_S24_3LE:
        format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
        break;
    case SNDRV_PCM_FORMAT_S24_LE:
        format = AUDIO_FORMAT_PCM_8_24_BIT;
        break;
    case SNDRV_PCM_FORMAT_S32_LE:
        format = AUDIO_FORMAT_PCM_32_BIT;
        break;
    default:
        ALOGW("Incorrect ALSA format");
        format = AUDIO_FORMAT_INVALID;
    }
    return format;
}

/*Translates hal format (AOSP) to alsa formats*/
uint32_t hal_format_to_alsa(audio_format_t hal_format)
{
    uint32_t alsa_format;

    switch (hal_format) {
    case AUDIO_FORMAT_PCM_32_BIT: {
        if (platform_supports_true_32bit())
            alsa_format = SNDRV_PCM_FORMAT_S32_LE;
        else
            alsa_format = SNDRV_PCM_FORMAT_S24_3LE;
        }
        break;
    case AUDIO_FORMAT_PCM_8_BIT:
        alsa_format = SNDRV_PCM_FORMAT_S8;
        break;
    case AUDIO_FORMAT_PCM_24_BIT_PACKED:
        alsa_format = SNDRV_PCM_FORMAT_S24_3LE;
        break;
    case AUDIO_FORMAT_PCM_8_24_BIT: {
        if (platform_supports_true_32bit())
            alsa_format = SNDRV_PCM_FORMAT_S32_LE;
        else
            alsa_format = SNDRV_PCM_FORMAT_S24_3LE;
        }
        break;
    case AUDIO_FORMAT_PCM_FLOAT:
        alsa_format = SNDRV_PCM_FORMAT_S24_3LE;
        break;
    default:
    case AUDIO_FORMAT_PCM_16_BIT:
        alsa_format = SNDRV_PCM_FORMAT_S16_LE;
        break;
    }
    return alsa_format;
}

/*Translates PCM formats to AOSP formats*/
audio_format_t pcm_format_to_hal(uint32_t pcm_format)
{
    audio_format_t format = AUDIO_FORMAT_INVALID;

    switch(pcm_format) {
    case PCM_FORMAT_S16_LE:
        format = AUDIO_FORMAT_PCM_16_BIT;
        break;
    case PCM_FORMAT_S24_3LE:
        format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
        break;
    case PCM_FORMAT_S24_LE:
        format = AUDIO_FORMAT_PCM_8_24_BIT;
        break;
    case PCM_FORMAT_S32_LE:
        format = AUDIO_FORMAT_PCM_32_BIT;
        break;
    default:
        ALOGW("Incorrect PCM format");
        format = AUDIO_FORMAT_INVALID;
    }
    return format;
}

/*Translates hal format (AOSP) to alsa formats*/
uint32_t hal_format_to_pcm(audio_format_t hal_format)
{
    uint32_t pcm_format;

    switch (hal_format) {
    case AUDIO_FORMAT_PCM_32_BIT:
    case AUDIO_FORMAT_PCM_8_24_BIT:
    case AUDIO_FORMAT_PCM_FLOAT: {
        if (platform_supports_true_32bit())
            pcm_format = PCM_FORMAT_S32_LE;
        else
            pcm_format = PCM_FORMAT_S24_3LE;
        }
        break;
    case AUDIO_FORMAT_PCM_8_BIT:
        pcm_format = PCM_FORMAT_S8;
        break;
    case AUDIO_FORMAT_PCM_24_BIT_PACKED:
        pcm_format = PCM_FORMAT_S24_3LE;
        break;
    default:
    case AUDIO_FORMAT_PCM_16_BIT:
        pcm_format = PCM_FORMAT_S16_LE;
        break;
    }
    return pcm_format;
}

uint32_t get_alsa_fragment_size(uint32_t bytes_per_sample,
                                  uint32_t sample_rate,
                                  uint32_t noOfChannels,
                                  int64_t duration_ms)
{
    uint32_t fragment_size = 0;
    uint32_t pcm_offload_time = PCM_OFFLOAD_BUFFER_DURATION;

    if (duration_ms >= MIN_OFFLOAD_BUFFER_DURATION_MS && duration_ms <= MAX_OFFLOAD_BUFFER_DURATION_MS)
        pcm_offload_time = duration_ms;

    fragment_size = (pcm_offload_time
                     * sample_rate
                     * bytes_per_sample
                     * noOfChannels)/1000;
    if (fragment_size < MIN_PCM_OFFLOAD_FRAGMENT_SIZE)
        fragment_size = MIN_PCM_OFFLOAD_FRAGMENT_SIZE;
    else if (fragment_size > MAX_PCM_OFFLOAD_FRAGMENT_SIZE)
        fragment_size = MAX_PCM_OFFLOAD_FRAGMENT_SIZE;
    /*To have same PCM samples for all channels, the buffer size requires to
     *be multiple of (number of channels * bytes per sample)
     *For writes to succeed, the buffer must be written at address which is multiple of 32
     */
    fragment_size = ALIGN(fragment_size, (bytes_per_sample * noOfChannels * 32));

    ALOGI("PCM offload Fragment size to %d bytes", fragment_size);
    return fragment_size;
}

/* Calculates the fragment size required to configure compress session.
 * Based on the alsa format selected, decide if conversion is needed in

 * HAL ( e.g. convert AUDIO_FORMAT_PCM_FLOAT input format to
 * AUDIO_FORMAT_PCM_24_BIT_PACKED before writing to the compress driver.
 */
void audio_extn_utils_update_direct_pcm_fragment_size(struct stream_out *out)
{
    audio_format_t dst_format = out->hal_op_format;
    audio_format_t src_format = out->hal_ip_format;
    uint32_t hal_op_bytes_per_sample = audio_bytes_per_sample(dst_format);
    uint32_t hal_ip_bytes_per_sample = audio_bytes_per_sample(src_format);

    out->compr_config.fragment_size =
             get_alsa_fragment_size(hal_op_bytes_per_sample,
                                      out->sample_rate,
                                      popcount(out->channel_mask),
                                      out->info.duration_us / 1000);

    if ((src_format != dst_format) &&
         hal_op_bytes_per_sample != hal_ip_bytes_per_sample) {

        out->hal_fragment_size =
                  ((out->compr_config.fragment_size * hal_ip_bytes_per_sample) /
                   hal_op_bytes_per_sample);
        ALOGI("enable conversion hal_input_fragment_size is %d src_format %x dst_format %x",
               out->hal_fragment_size, src_format, dst_format);
    } else {
        out->hal_fragment_size = out->compr_config.fragment_size;
    }
}

/* converts pcm format 24_8 to 8_24 inplace */
size_t audio_extn_utils_convert_format_24_8_to_8_24(void *buf, size_t bytes)
{
    size_t i = 0;
    int *int_buf_stream = buf;

    if ((bytes % 4) != 0) {
        ALOGE("%s: wrong inout buffer! ... is not 32 bit aligned ", __func__);
        return -EINVAL;
    }

    for (; i < (bytes / 4); i++)
        int_buf_stream[i] >>= 8;

    return bytes;
}

#ifdef AUDIO_GKI_ENABLED
int get_snd_codec_id(audio_format_t format)
{
    int id = 0;

    switch (format & AUDIO_FORMAT_MAIN_MASK) {
    case AUDIO_FORMAT_MP3:
        id = SND_AUDIOCODEC_MP3;
        break;
    case AUDIO_FORMAT_AAC:
        id = SND_AUDIOCODEC_AAC;
        break;
    case AUDIO_FORMAT_AAC_ADTS:
        id = SND_AUDIOCODEC_AAC;
        break;
    case AUDIO_FORMAT_AAC_LATM:
        id = SND_AUDIOCODEC_AAC;
        break;
    case AUDIO_FORMAT_PCM:
        id = SND_AUDIOCODEC_PCM;
        break;
    case AUDIO_FORMAT_FLAC:
    case AUDIO_FORMAT_ALAC:
    case AUDIO_FORMAT_APE:
    case AUDIO_FORMAT_VORBIS:
    case AUDIO_FORMAT_WMA:
    case AUDIO_FORMAT_WMA_PRO:
    case AUDIO_FORMAT_DSD:
    case AUDIO_FORMAT_APTX:
        id = SND_AUDIOCODEC_BESPOKE;
        break;
    case AUDIO_FORMAT_MP2:
        id = SND_AUDIOCODEC_MP2;
        break;
    case AUDIO_FORMAT_AC3:
        id = SND_AUDIOCODEC_AC3;
        break;
    case AUDIO_FORMAT_E_AC3:
    case AUDIO_FORMAT_E_AC3_JOC:
        id = SND_AUDIOCODEC_EAC3;
        break;
    case AUDIO_FORMAT_DTS:
    case AUDIO_FORMAT_DTS_HD:
        id = SND_AUDIOCODEC_DTS;
        break;
    case AUDIO_FORMAT_DOLBY_TRUEHD:
        id = SND_AUDIOCODEC_TRUEHD;
        break;
    case AUDIO_FORMAT_IEC61937:
        id = SND_AUDIOCODEC_IEC61937;
        break;
    default:
        ALOGE("%s: Unsupported audio format :%x", __func__, format);
    }

    return id;
}
#else
int get_snd_codec_id(audio_format_t format)
{
    int id = 0;

    switch (format & AUDIO_FORMAT_MAIN_MASK) {
    case AUDIO_FORMAT_MP3:
        id = SND_AUDIOCODEC_MP3;
        break;
    case AUDIO_FORMAT_AAC:
        id = SND_AUDIOCODEC_AAC;
        break;
    case AUDIO_FORMAT_AAC_ADTS:
        id = SND_AUDIOCODEC_AAC;
        break;
    case AUDIO_FORMAT_AAC_LATM:
        id = SND_AUDIOCODEC_AAC;
        break;
    case AUDIO_FORMAT_PCM:
        id = SND_AUDIOCODEC_PCM;
        break;
    case AUDIO_FORMAT_FLAC:
        id = SND_AUDIOCODEC_FLAC;
        break;
    case AUDIO_FORMAT_ALAC:
        id = SND_AUDIOCODEC_ALAC;
        break;
    case AUDIO_FORMAT_APE:
        id = SND_AUDIOCODEC_APE;
        break;
    case AUDIO_FORMAT_VORBIS:
        id = SND_AUDIOCODEC_VORBIS;
        break;
    case AUDIO_FORMAT_WMA:
        id = SND_AUDIOCODEC_WMA;
        break;
    case AUDIO_FORMAT_WMA_PRO:
        id = SND_AUDIOCODEC_WMA_PRO;
        break;
    case AUDIO_FORMAT_MP2:
        id = SND_AUDIOCODEC_MP2;
        break;
    case AUDIO_FORMAT_AC3:
        id = SND_AUDIOCODEC_AC3;
        break;
    case AUDIO_FORMAT_E_AC3:
    case AUDIO_FORMAT_E_AC3_JOC:
        id = SND_AUDIOCODEC_EAC3;
        break;
    case AUDIO_FORMAT_DTS:
    case AUDIO_FORMAT_DTS_HD:
        id = SND_AUDIOCODEC_DTS;
        break;
    case AUDIO_FORMAT_DOLBY_TRUEHD:
        id = SND_AUDIOCODEC_TRUEHD;
        break;
    case AUDIO_FORMAT_IEC61937:
        id = SND_AUDIOCODEC_IEC61937;
        break;
    case AUDIO_FORMAT_DSD:
        id = SND_AUDIOCODEC_DSD;
        break;
    case AUDIO_FORMAT_APTX:
        id = SND_AUDIOCODEC_APTX;
        break;
    default:
        ALOGE("%s: Unsupported audio format :%x", __func__, format);
    }

    return id;
}
#endif

void audio_extn_utils_send_audio_calibration(struct audio_device *adev,
                                             struct audio_usecase *usecase)
{
    int type = usecase->type;

    if (type == PCM_PLAYBACK && usecase->stream.out != NULL) {
        platform_send_audio_calibration(adev->platform, usecase,
                         usecase->stream.out->app_type_cfg.app_type);
    } else if (type == PCM_CAPTURE && usecase->stream.in != NULL) {
        platform_send_audio_calibration(adev->platform, usecase,
                         usecase->stream.in->app_type_cfg.app_type);
    } else if ((type == PCM_HFP_CALL) || (type == PCM_CAPTURE) ||
               (type == TRANSCODE_LOOPBACK_RX && usecase->stream.inout != NULL)) {
        platform_send_audio_calibration(adev->platform, usecase,
                         platform_get_default_app_type_v2(adev->platform, usecase->type));
    } else {
        /* No need to send audio calibration for voice and voip call usecases */
        if ((type != VOICE_CALL) && (type != VOIP_CALL))
            ALOGW("%s: No audio calibration for usecase type = %d",  __func__, type);
    }
}

// Base64 Encode and Decode
// Not all features supported. This must be used only with following conditions.
// Decode Modes: Support with and without padding
//         CRLF not handling. So no CRLF in string to decode.
// Encode Modes: Supports only padding
int b64decode(char *inp, int ilen, uint8_t* outp)
{
    int i, j, k, ii, num;
    int rem, pcnt;
    uint32_t res=0;
    uint8_t getIndex[MAX_BASEINDEX_LEN];
    uint8_t tmp, cflag;

    if(inp == NULL || outp == NULL || ilen <= 0) {
        ALOGE("[%s] received NULL pointer or zero length",__func__);
        return -1;
    }

    memset(getIndex, MAX_BASEINDEX_LEN-1, sizeof(getIndex));
    for(i=0;i<BASE_TABLE_SIZE;i++) {
        getIndex[(uint8_t)bTable[i]] = (uint8_t)i;
    }
    getIndex[(uint8_t)'=']=0;

    j=0;k=0;
    num = ilen/4;
    rem = ilen%4;
    if(rem==0)
        num = num-1;
    cflag=0;
    for(i=0; i<num; i++) {
        res=0;
        for(ii=0;ii<4;ii++) {
            res = res << 6;
            tmp = getIndex[(uint8_t)inp[j++]];
            res = res | tmp;
            cflag = cflag | tmp;
        }
        outp[k++] = (res >> 16)&0xFF;
        outp[k++] = (res >> 8)&0xFF;
        outp[k++] = res & 0xFF;
    }

    // Handle last bytes special
    pcnt=0;
    if(rem == 0) {
        //With padding or full data
        res = 0;
        for(ii=0;ii<4;ii++) {
            if(inp[j] == '=')
                pcnt++;
            res = res << 6;
            tmp = getIndex[(uint8_t)inp[j++]];
            res = res | tmp;
            cflag = cflag | tmp;
        }
        outp[k++] = res >> 16;
        if(pcnt == 2)
            goto done;
        outp[k++] = (res>>8)&0xFF;
        if(pcnt == 1)
            goto done;
        outp[k++] = res&0xFF;
    } else {
        //without padding
        res = 0;
        for(i=0;i<rem;i++) {
            res = res << 6;
            tmp = getIndex[(uint8_t)inp[j++]];
            res = res | tmp;
            cflag = cflag | tmp;
        }
        for(i=rem;i<4;i++) {
            res = res << 6;
            pcnt++;
        }
        outp[k++] = res >> 16;
        if(pcnt == 2)
            goto done;
        outp[k++] = (res>>8)&0xFF;
        if(pcnt == 1)
            goto done;
        outp[k++] = res&0xFF;
    }
done:
    if(cflag == 0xFF) {
        ALOGE("[%s] base64 decode failed. Invalid character found %s",
            __func__, inp);
        return 0;
    }
    return k;
}

int b64encode(uint8_t *inp, int ilen, char* outp)
{
    int i,j,k, num;
    int rem=0;
    uint32_t res=0;

    if(inp == NULL || outp == NULL || ilen<=0) {
        ALOGE("[%s] received NULL pointer or zero input length",__func__);
        return -1;
    }

    num = ilen/3;
    rem = ilen%3;
    j=0;k=0;
    for(i=0; i<num; i++) {
        //prepare index
        res = inp[j++]<<16;
        res = res | inp[j++]<<8;
        res = res | inp[j++];
        //get output map from index
        outp[k++] = (char) bTable[(res>>18)&0x3F];
        outp[k++] = (char) bTable[(res>>12)&0x3F];
        outp[k++] = (char) bTable[(res>>6)&0x3F];
        outp[k++] = (char) bTable[res&0x3F];
    }

    switch(rem) {
        case 1:
            res = inp[j++]<<16;
            outp[k++] = (char) bTable[res>>18];
            outp[k++] = (char) bTable[(res>>12)&0x3F];
            //outp[k++] = '=';
            //outp[k++] = '=';
            break;
        case 2:
            res = inp[j++]<<16;
            res = res | inp[j++]<<8;
            outp[k++] = (char) bTable[res>>18];
            outp[k++] = (char) bTable[(res>>12)&0x3F];
            outp[k++] = (char) bTable[(res>>6)&0x3F];
            //outp[k++] = '=';
            break;
        default:
            break;
    }
    outp[k] = '\0';
    return k;
}


int audio_extn_utils_get_codec_version(const char *snd_card_name,
                            int card_num,
                            char *codec_version)
{
    char procfs_path[50];
    FILE *fp;

    if (strstr(snd_card_name, "tasha")) {
        snprintf(procfs_path, sizeof(procfs_path),
                 "/proc/asound/card%d/codecs/tasha/version", card_num);
        if ((fp = fopen(procfs_path, "r")) != NULL) {
            fgets(codec_version, CODEC_VERSION_MAX_LENGTH, fp);
            fclose(fp);
        } else {
            ALOGE("%s: ERROR. cannot open %s", __func__, procfs_path);
            return -ENOENT;
        }
        ALOGD("%s: codec version %s", __func__, codec_version);
    }

    return 0;
}

int audio_extn_utils_get_codec_variant(int card_num,
                            char *codec_variant)
{
    char procfs_path[50];
    FILE *fp;
    snprintf(procfs_path, sizeof(procfs_path),
             "/proc/asound/card%d/codecs/wcd938x/variant", card_num);
    if ((fp = fopen(procfs_path, "r")) == NULL) {
        snprintf(procfs_path, sizeof(procfs_path),
                 "/proc/asound/card%d/codecs/wcd937x/variant", card_num);
        if ((fp = fopen(procfs_path, "r")) == NULL) {
            ALOGE("%s: ERROR. cannot open %s", __func__, procfs_path);
            return -ENOENT;
        }
    }
    fgets(codec_variant, CODEC_VARIANT_MAX_LENGTH, fp);
    fclose(fp);
    ALOGD("%s: codec variant is %s", __func__, codec_variant);
    return 0;
}


#ifdef AUDIO_EXTERNAL_HDMI_ENABLED

void get_default_compressed_channel_status(
                                  unsigned char *channel_status)
{
     memset(channel_status,0,24);

     /* block start bit in preamble bit 3 */
     channel_status[0] |= PROFESSIONAL;
     //compre out
     channel_status[0] |= NON_LPCM;
     // sample rate; fixed 48K for default/transcode
     channel_status[3] |= SR_48000;
}

int32_t get_compressed_channel_status(void *audio_stream_data,
                                                   uint32_t audio_frame_size,
                                                   unsigned char *channel_status,
                                                   enum audio_parser_code_type codec_type)
                                                   // codec_type - AUDIO_PARSER_CODEC_AC3
                                                   //            - AUDIO_PARSER_CODEC_DTS
{
     unsigned char *stream;
     int ret = 0;
     stream = (unsigned char *)audio_stream_data;

     if (audio_stream_data == NULL || audio_frame_size == 0) {
         ALOGW("no buffer to get channel status, return default for compress");
         get_default_compressed_channel_status(channel_status);
         return ret;
     }

     memset(channel_status,0,24);
     if(init_audio_parser(stream, audio_frame_size, codec_type) == -1)
     {
         ALOGE("init audio parser failed");
         return -1;
     }
     ret = get_channel_status(channel_status, codec_type);
     return ret;

}

void get_lpcm_channel_status(uint32_t sampleRate,
                                                  unsigned char *channel_status)
{
     int32_t status = 0;
     memset(channel_status,0,24);
     /* block start bit in preamble bit 3 */
     channel_status[0] |= PROFESSIONAL;
     //LPCM OUT
     channel_status[0] &= ~NON_LPCM;

     switch (sampleRate) {
         case 8000:
         case 11025:
         case 12000:
         case 16000:
         case 22050:
             channel_status[3] |= SR_NOTID;
             break;
         case 24000:
             channel_status[3] |= SR_24000;
             break;
         case 32000:
             channel_status[3] |= SR_32000;
             break;
         case 44100:
             channel_status[3] |= SR_44100;
             break;
         case 48000:
             channel_status[3] |= SR_48000;
             break;
         case 88200:
             channel_status[3] |= SR_88200;
             break;
         case 96000:
             channel_status[3] |= SR_96000;
             break;
         case 176400:
             channel_status[3] |= SR_176400;
             break;
         case 192000:
            channel_status[3] |= SR_192000;
             break;
         default:
             ALOGV("Invalid sample_rate %u\n", sampleRate);
             status = -1;
             break;
     }
}

void audio_utils_set_hdmi_channel_status(struct stream_out *out, char * buffer, size_t bytes)
{
    unsigned char channel_status[24]={0};
    struct snd_aes_iec958 iec958;
    const char *mixer_ctl_name = "IEC958 Playback PCM Stream";
    struct mixer_ctl *ctl;
    ALOGV("%s: buffer %s bytes %zd", __func__, buffer, bytes);

    if (audio_extn_utils_is_dolby_format(out->format) &&
        /*TODO:Extend code to support DTS passthrough*/
        /*set compressed channel status bits*/
        audio_extn_passthru_is_passthrough_stream(out) &&
        audio_extn_is_hdmi_passthru_enabled()) {
        get_compressed_channel_status(buffer, bytes, channel_status, AUDIO_PARSER_CODEC_AC3);
    } else {
        /*set channel status bit for LPCM*/
        get_lpcm_channel_status(out->sample_rate, channel_status);
    }

    memcpy(iec958.status, channel_status,sizeof(iec958.status));
    ctl = mixer_get_ctl_by_name(out->dev->mixer, mixer_ctl_name);
    if (!ctl) {
            ALOGE("%s: Could not get ctl for mixer cmd - %s",
                  __func__, mixer_ctl_name);
            return;
    }
    if (mixer_ctl_set_array(ctl, &iec958, sizeof(iec958)) < 0) {
        ALOGE("%s: Could not set channel status for ext HDMI ",
              __func__);
        return;
    }

}
#endif

int audio_extn_utils_get_avt_device_drift(
                struct audio_usecase *usecase,
                struct audio_avt_device_drift_param *drift_param)
{
    int ret = 0, count = 0;
    char avt_device_drift_mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};
    const char *backend = NULL;
    struct mixer_ctl *ctl = NULL;
    struct audio_avt_device_drift_stats drift_stats;
    struct audio_device *adev = NULL;

    if (usecase != NULL && usecase->type == PCM_PLAYBACK) {
        backend = platform_get_snd_device_backend_interface(usecase->out_snd_device);
        if (!backend) {
            ALOGE("%s: Unsupported device %d", __func__,
                   get_device_types(&usecase->stream.out->device_list));
            ret = -EINVAL;
            goto done;
        }
        strlcpy(avt_device_drift_mixer_ctl_name,
                backend,
                MIXER_PATH_MAX_LENGTH);

        count = strlen(backend);
        if (MIXER_PATH_MAX_LENGTH - count > 0) {
            strlcat(&avt_device_drift_mixer_ctl_name[count],
                    " DRIFT",
                    MIXER_PATH_MAX_LENGTH - count);
        } else {
            ret = -EINVAL;
            goto done;
        }
    } else {
        ALOGE("%s: Invalid usecase",__func__);
        ret = -EINVAL;
        goto done;
    }

    adev = usecase->stream.out->dev;
    ctl = mixer_get_ctl_by_name(adev->mixer, avt_device_drift_mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
                __func__, avt_device_drift_mixer_ctl_name);

        ret = -EINVAL;
        goto done;
    }

    ALOGV("%s: Getting AV Timer vs Device Drift mixer ctrl name %s", __func__,
            avt_device_drift_mixer_ctl_name);

    mixer_ctl_update(ctl);
    count = mixer_ctl_get_num_values(ctl);
    if (count != sizeof(struct audio_avt_device_drift_stats)) {
        ALOGE("%s: mixer_ctl_get_num_values() invalid drift_stats data size",
                __func__);

        ret = -EINVAL;
        goto done;
    }

    ret = mixer_ctl_get_array(ctl, (void *)&drift_stats, count);
    if (ret != 0) {
        ALOGE("%s: mixer_ctl_get_array() failed to get drift_stats Params",
                __func__);

        ret = -EINVAL;
        goto done;
    }
    memcpy(drift_param, &drift_stats.drift_param,
            sizeof(struct audio_avt_device_drift_param));
done:
    return ret;
}

#ifdef SNDRV_COMPRESS_PATH_DELAY
int audio_extn_utils_compress_get_dsp_latency(struct stream_out *out)
{
    int ret = -EINVAL;
    struct snd_compr_metadata metadata;
    int delay_ms = COMPRESS_OFFLOAD_PLAYBACK_LATENCY;

    /* override the latency for pcm offload use case */
    if ((out->flags & AUDIO_OUTPUT_FLAG_DIRECT) &&
        !(out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)) {
        delay_ms = PCM_OFFLOAD_PLAYBACK_LATENCY;
    }

    if (property_get_bool("vendor.audio.playback.dsp.pathdelay", false)) {
        ALOGD("%s:: Quering DSP delay %d",__func__, __LINE__);
        if (!(is_offload_usecase(out->usecase))) {
            ALOGE("%s:: not supported for non offload session", __func__);
            goto exit;
        }

        if (!out->compr) {
            ALOGD("%s:: Invalid compress handle,returning default dsp latency",
                    __func__);
            goto exit;
        }

        metadata.key = SNDRV_COMPRESS_PATH_DELAY;
        ret = compress_get_metadata(out->compr, &metadata);
        if(ret) {
            ALOGE("%s::error %s", __func__, compress_get_error(out->compr));
            goto exit;
        }
        delay_ms = metadata.value[0] / 1000; /*convert to ms*/
    } else {
        ALOGD("%s:: Using Fix DSP delay",__func__);
    }

exit:
    ALOGD("%s:: delay in ms is %d",__func__, delay_ms);
    return delay_ms;
}
#else
int audio_extn_utils_compress_get_dsp_latency(struct stream_out *out __unused)
{
    return COMPRESS_OFFLOAD_PLAYBACK_LATENCY;
}
#endif

#ifdef SNDRV_COMPRESS_RENDER_MODE
int audio_extn_utils_compress_set_render_mode(struct stream_out *out)
{
    struct snd_compr_metadata metadata;
    int ret = -EINVAL;

    if (!(is_offload_usecase(out->usecase))) {
        ALOGE("%s:: not supported for non offload session", __func__);
        goto exit;
    }

    if (!out->compr) {
        ALOGD("%s:: Invalid compress handle",
                __func__);
        goto exit;
    }

    ALOGD("%s:: render mode %d", __func__, out->render_mode);

    metadata.key = SNDRV_COMPRESS_RENDER_MODE;
    if (out->render_mode == RENDER_MODE_AUDIO_MASTER) {
        metadata.value[0] = SNDRV_COMPRESS_RENDER_MODE_AUDIO_MASTER;
    } else if (out->render_mode == RENDER_MODE_AUDIO_STC_MASTER) {
        metadata.value[0] = SNDRV_COMPRESS_RENDER_MODE_STC_MASTER;
    } else {
        ret = 0;
        goto exit;
    }
    ret = compress_set_metadata(out->compr, &metadata);
    if(ret) {
        ALOGE("%s::error %s", __func__, compress_get_error(out->compr));
    }
exit:
    return ret;
}
#else
int audio_extn_utils_compress_set_render_mode(struct stream_out *out __unused)
{
    ALOGD("%s:: configuring render mode not supported", __func__);
    return 0;
}
#endif

#ifdef SNDRV_COMPRESS_CLK_REC_MODE
int audio_extn_utils_compress_set_clk_rec_mode(
            struct audio_usecase *usecase)
{
    struct snd_compr_metadata metadata;
    struct stream_out *out = NULL;
    int ret = -EINVAL;

    if (usecase == NULL || usecase->type != PCM_PLAYBACK) {
        ALOGE("%s:: Invalid use case", __func__);
        goto exit;
    }

    out = usecase->stream.out;
    if (!out) {
        ALOGE("%s:: invalid stream", __func__);
        goto exit;
    }

    if (!is_offload_usecase(out->usecase)) {
        ALOGE("%s:: not supported for non offload session", __func__);
        goto exit;
    }

    if (out->render_mode != RENDER_MODE_AUDIO_STC_MASTER) {
        ALOGD("%s:: clk recovery is only supported in STC render mode",
                __func__);
        ret = 0;
        goto exit;
    }

    if (!out->compr) {
        ALOGD("%s:: Invalid compress handle",
                __func__);
        goto exit;
    }
    metadata.key = SNDRV_COMPRESS_CLK_REC_MODE;
    switch(usecase->out_snd_device) {
        case SND_DEVICE_OUT_HDMI:
        case SND_DEVICE_OUT_SPEAKER_AND_HDMI:
        case SND_DEVICE_OUT_DISPLAY_PORT:
        case SND_DEVICE_OUT_SPEAKER_AND_DISPLAY_PORT:
            metadata.value[0] = SNDRV_COMPRESS_CLK_REC_MODE_NONE;
            break;
        default:
            metadata.value[0] = SNDRV_COMPRESS_CLK_REC_MODE_AUTO;
            break;
    }

    ALOGD("%s:: clk recovery mode %d",__func__, metadata.value[0]);

    ret = compress_set_metadata(out->compr, &metadata);
    if(ret) {
        ALOGE("%s::error %s", __func__, compress_get_error(out->compr));
    }

exit:
    return ret;
}
#else
int audio_extn_utils_compress_set_clk_rec_mode(
            struct audio_usecase *usecase __unused)
{
    ALOGD("%s:: configuring render mode not supported", __func__);
    return 0;
}
#endif

#ifdef SNDRV_COMPRESS_RENDER_WINDOW
int audio_extn_utils_compress_set_render_window(
            struct stream_out *out,
            struct audio_out_render_window_param *render_window)
{
    struct snd_compr_metadata metadata;
    int ret = -EINVAL;

    if(render_window == NULL) {
        ALOGE("%s:: Invalid render_window", __func__);
        goto exit;
    }

    ALOGD("%s:: render window start 0x%"PRIx64" end 0x%"PRIx64"",
          __func__,render_window->render_ws, render_window->render_we);

    if (!is_offload_usecase(out->usecase)) {
        ALOGE("%s:: not supported for non offload session", __func__);
        goto exit;
    }

    if ((out->render_mode != RENDER_MODE_AUDIO_MASTER) &&
        (out->render_mode != RENDER_MODE_AUDIO_STC_MASTER)) {
        ALOGD("%s:: only supported in timestamp mode, current "
              "render mode mode %d", __func__, out->render_mode);
        goto exit;
    }

    if (!out->compr) {
        ALOGW("%s:: offload session not yet opened,"
               "render window will be configure later", __func__);
        /* store render window to reconfigure in start_output_stream() */
       goto exit;
    }

    metadata.key = SNDRV_COMPRESS_RENDER_WINDOW;
    /*render window start value */
    metadata.value[0] = 0xFFFFFFFF & render_window->render_ws; /* lsb */
    metadata.value[1] = \
            (0xFFFFFFFF00000000 & render_window->render_ws) >> 32; /* msb*/
    /*render window end value */
    metadata.value[2] = 0xFFFFFFFF & render_window->render_we; /* lsb */
    metadata.value[3] = \
            (0xFFFFFFFF00000000 & render_window->render_we) >> 32; /* msb*/

    ret = compress_set_metadata(out->compr, &metadata);
    if(ret) {
        ALOGE("%s::error %s", __func__, compress_get_error(out->compr));
    }

exit:
    return ret;
}
#else
int audio_extn_utils_compress_set_render_window(
            struct stream_out *out __unused,
            struct audio_out_render_window_param *render_window __unused)
{
    ALOGD("%s:: configuring render window not supported", __func__);
    return 0;
}
#endif

#ifdef SNDRV_COMPRESS_START_DELAY
int audio_extn_utils_compress_set_start_delay(
            struct stream_out *out,
            struct audio_out_start_delay_param *delay_param)
{
    struct snd_compr_metadata metadata;
    int ret = -EINVAL;

    if(delay_param == NULL) {
        ALOGE("%s:: Invalid delay_param", __func__);
        goto exit;
    }

    ALOGD("%s:: render start delay 0x%"PRIx64" ", __func__,
          delay_param->start_delay);

    if (!is_offload_usecase(out->usecase)) {
        ALOGE("%s:: not supported for non offload session", __func__);
        goto exit;
    }

   if ((out->render_mode != RENDER_MODE_AUDIO_MASTER) &&
       (out->render_mode != RENDER_MODE_AUDIO_STC_MASTER)) {
        ALOGD("%s:: only supported in timestamp mode, current "
              "render mode mode %d", __func__, out->render_mode);
        goto exit;
    }

    if (!out->compr) {
        ALOGW("%s:: offload session not yet opened,"
               "start delay will be configure later", __func__);
       goto exit;
    }

    metadata.key = SNDRV_COMPRESS_START_DELAY;
    metadata.value[0] = 0xFFFFFFFF & delay_param->start_delay; /* lsb */
    metadata.value[1] = \
            (0xFFFFFFFF00000000 & delay_param->start_delay) >> 32; /* msb*/

    ret = compress_set_metadata(out->compr, &metadata);
    if(ret) {
        ALOGE("%s::error %s", __func__, compress_get_error(out->compr));
    }

exit:
    return ret;
}
#else
int audio_extn_utils_compress_set_start_delay(
            struct stream_out *out __unused,
            struct audio_out_start_delay_param *delay_param __unused)
{
    ALOGD("%s:: configuring render window not supported", __func__);
    return 0;
}
#endif

#ifdef SNDRV_COMPRESS_DSP_POSITION
int audio_extn_utils_compress_get_dsp_presentation_pos(struct stream_out *out,
            uint64_t *frames, struct timespec *timestamp, int32_t clock_id)
{
    int ret = -EINVAL;
    uint64_t *val = NULL;
    uint64_t time = 0;
    struct snd_compr_metadata metadata;

    ALOGV("%s:: Quering DSP position with clock id %d",__func__, clock_id);
    metadata.key = SNDRV_COMPRESS_DSP_POSITION;
    metadata.value[0] = clock_id;
    ret = compress_get_metadata(out->compr, &metadata);
    if (ret) {
        ALOGE("%s::error %s", __func__, compress_get_error(out->compr));
        ret = -errno;
        goto exit;
    }
    val = (uint64_t *)&metadata.value[1];
    *frames = *val;
    time = *(val + 1);
    timestamp->tv_sec = time / 1000000;
    timestamp->tv_nsec = (time % 1000000)*1000;

exit:
    return ret;
}
#else
int audio_extn_utils_compress_get_dsp_presentation_pos(struct stream_out *out __unused,
            uint64_t *frames __unused, struct timespec *timestamp __unused,
            int32_t clock_id __unused)
{
    ALOGD("%s:: dsp presentation position not supported", __func__);
    return 0;

}
#endif

#ifdef SNDRV_PCM_IOCTL_DSP_POSITION
int audio_extn_utils_pcm_get_dsp_presentation_pos(struct stream_out *out,
            uint64_t *frames, struct timespec *timestamp, int32_t clock_id)
{
    int ret = -EINVAL;
    uint64_t time = 0;
    struct snd_pcm_prsnt_position prsnt_position;

    ALOGV("%s:: Quering DSP position with clock id %d",__func__, clock_id);
    prsnt_position.clock_id = clock_id;
    ret = pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_DSP_POSITION, &prsnt_position);
    if (ret) {
        ALOGE("%s::error  %d", __func__, ret);
        ret = -EIO;
        goto exit;
    }

    *frames = prsnt_position.frames;
    time = prsnt_position.timestamp;
    timestamp->tv_sec = time / 1000000;
    timestamp->tv_nsec = (time % 1000000)*1000;

exit:
    return ret;
}
#else
int audio_extn_utils_pcm_get_dsp_presentation_pos(struct stream_out *out __unused,
            uint64_t *frames __unused, struct timespec *timestamp __unused,
            int32_t clock_id __unused)
{
    ALOGD("%s:: dsp presentation position not supported", __func__);
    return 0;

}
#endif

#define MAX_SND_CARD 8
#define RETRY_US 1000000
#define RETRY_NUMBER 40
#define PLATFORM_INFO_XML_PATH          "audio_platform_info.xml"
#define PLATFORM_INFO_XML_BASE_STRING   "audio_platform_info"

bool audio_extn_utils_resolve_config_file(char file_name[MIXER_PATH_MAX_LENGTH])
{
    char full_config_path[MIXER_PATH_MAX_LENGTH];
    char vendor_config_path[VENDOR_CONFIG_PATH_MAX_LENGTH];

    /* Get path for audio configuration files in vendor */
    audio_get_vendor_config_path(vendor_config_path,
        sizeof(vendor_config_path));
    snprintf(full_config_path, sizeof(full_config_path),
        "%s/%s", vendor_config_path, file_name);
    if (F_OK == access(full_config_path, 0)) {
        strlcpy(file_name, full_config_path, MIXER_PATH_MAX_LENGTH);
        return true;
    }
    return false;
}

/* platform_info_file should be size 'MIXER_PATH_MAX_LENGTH' */
int audio_extn_utils_get_platform_info(const char* snd_card_name, char* platform_info_file)
{
    if (NULL == snd_card_name) {
        return -1;
    }

    struct snd_card_split *snd_split_handle = NULL;
    int ret = 0;
    audio_extn_set_snd_card_split(snd_card_name);
    snd_split_handle = audio_extn_get_snd_card_split();

    snprintf(platform_info_file, MIXER_PATH_MAX_LENGTH, "%s_%s_%s.xml",
                     PLATFORM_INFO_XML_BASE_STRING, snd_split_handle->snd_card,
                     snd_split_handle->form_factor);

    if (!audio_extn_utils_resolve_config_file(platform_info_file)) {
        memset(platform_info_file, 0, MIXER_PATH_MAX_LENGTH);
        snprintf(platform_info_file, MIXER_PATH_MAX_LENGTH, "%s_%s.xml",
                     PLATFORM_INFO_XML_BASE_STRING, snd_split_handle->snd_card);

        if (!audio_extn_utils_resolve_config_file(platform_info_file)) {
            memset(platform_info_file, 0, MIXER_PATH_MAX_LENGTH);
            strlcpy(platform_info_file, PLATFORM_INFO_XML_PATH, MIXER_PATH_MAX_LENGTH);
            ret = audio_extn_utils_resolve_config_file(platform_info_file) ? 0 : -1;
        }
    }

    return ret;
}

int audio_extn_utils_get_snd_card_num()
{
    int snd_card_num = 0;
    struct mixer *mixer = NULL;

    snd_card_num = audio_extn_utils_open_snd_mixer(&mixer);
    if (mixer)
        mixer_close(mixer);
    return snd_card_num;
}

int audio_extn_utils_open_snd_mixer(struct mixer **mixer_handle)
{

    void *hw_info = NULL;
    struct mixer *mixer = NULL;
    int retry_num = 0;
    int snd_card_num = 0;
    char* snd_card_name = NULL;
    int snd_card_detection_info[MAX_SND_CARD] = {0};

    if (!mixer_handle) {
        ALOGE("invalid mixer handle");
        return -1;
    }
    *mixer_handle = NULL;
    /*
    * Try with all the sound cards ( 0 to 7 ) and if none of them were detected
    * sleep for 1 sec and try detections with sound card 0 again.
    * If sound card gets detected, check if it is relevant, if not check with the
    * other sound cards. To ensure that the irrelevant sound card is not check again,
    * we maintain it in min_snd_card_num.
    */
    while (retry_num < RETRY_NUMBER) {
        snd_card_num = 0;
        while (snd_card_num < MAX_SND_CARD) {
            if (snd_card_detection_info[snd_card_num] == 0) {
                mixer = mixer_open(snd_card_num);
                if (!mixer)
                    snd_card_num++;
                else
                    break;
            } else
                snd_card_num++;
        }

        if (!mixer) {
           usleep(RETRY_US);
           retry_num++;
           ALOGD("%s: retry, retry_num %d", __func__, retry_num);
           continue;
        }

        snd_card_name = strdup(mixer_get_name(mixer));
        if (!snd_card_name) {
            ALOGE("failed to allocate memory for snd_card_name\n");
            mixer_close(mixer);
            return -1;
        }
        ALOGD("%s: snd_card_name: %s", __func__, snd_card_name);
        snd_card_detection_info[snd_card_num] = 1;
        hw_info = hw_info_init(snd_card_name);
        if (hw_info) {
            ALOGD("%s: Opened sound card:%d", __func__, snd_card_num);
            break;
        }
        ALOGE("%s: Failed to init hardware info, snd_card_num:%d", __func__, snd_card_num);

        free(snd_card_name);
        snd_card_name = NULL;

        mixer_close(mixer);
        mixer = NULL;
    }
    if (snd_card_name)
        free(snd_card_name);

    if (hw_info)
        hw_info_deinit(hw_info);

    if (retry_num >= RETRY_NUMBER) {
        ALOGE("%s: Unable to find correct sound card, aborting.", __func__);
        return -1;
    }

    if (mixer)
        *mixer_handle = mixer;

    return snd_card_num;
}

void audio_extn_utils_close_snd_mixer(struct mixer *mixer)
{
    if (mixer)
        mixer_close(mixer);
}

#ifdef SNDRV_COMPRESS_ENABLE_ADJUST_SESSION_CLOCK
int audio_extn_utils_compress_enable_drift_correction(
        struct stream_out *out,
        struct audio_out_enable_drift_correction *drift)
{
    struct snd_compr_metadata metadata;
    int ret = -EINVAL;

    if(drift == NULL) {
        ALOGE("%s:: Invalid param", __func__);
        goto exit;
    }

    ALOGD("%s:: drift enable %d", __func__,drift->enable);

    if (!is_offload_usecase(out->usecase)) {
        ALOGE("%s:: not supported for non offload session", __func__);
        goto exit;
    }

    if (!out->compr) {
        ALOGW("%s:: offload session not yet opened,"
                "start delay will be configure later", __func__);
        goto exit;
    }

    metadata.key = SNDRV_COMPRESS_ENABLE_ADJUST_SESSION_CLOCK;
    metadata.value[0] = drift->enable;
    out->drift_correction_enabled = drift->enable;

    ret = compress_set_metadata(out->compr, &metadata);
    if(ret) {
        ALOGE("%s::error %s", __func__, compress_get_error(out->compr));
        out->drift_correction_enabled = false;
    }

exit:
    return ret;
}
#else
int audio_extn_utils_compress_enable_drift_correction(
        struct stream_out *out __unused,
        struct audio_out_enable_drift_correction *drift __unused)
{
    ALOGD("%s:: configuring drift enablement not supported", __func__);
    return 0;
}
#endif

#ifdef SNDRV_COMPRESS_ADJUST_SESSION_CLOCK
int audio_extn_utils_compress_correct_drift(
        struct stream_out *out,
        struct audio_out_correct_drift *drift_param)
{
    struct snd_compr_metadata metadata;
    int ret = -EINVAL;

    if (drift_param == NULL) {
        ALOGE("%s:: Invalid drift_param", __func__);
        goto exit;
    }

    ALOGD("%s:: adjust time 0x%"PRIx64" ", __func__,
            drift_param->adjust_time);

    if (!is_offload_usecase(out->usecase)) {
        ALOGE("%s:: not supported for non offload session", __func__);
        goto exit;
    }

    if (!out->compr) {
        ALOGW("%s:: offload session not yet opened", __func__);
        goto exit;
    }

    if (!out->drift_correction_enabled) {
        ALOGE("%s:: drift correction not enabled", __func__);
        goto exit;
    }

    metadata.key = SNDRV_COMPRESS_ADJUST_SESSION_CLOCK;
    metadata.value[0] = 0xFFFFFFFF & drift_param->adjust_time; /* lsb */
    metadata.value[1] = \
             (0xFFFFFFFF00000000 & drift_param->adjust_time) >> 32; /* msb*/

    ret = compress_set_metadata(out->compr, &metadata);
    if(ret)
        ALOGE("%s::error %s", __func__, compress_get_error(out->compr));
exit:
    return ret;
}
#else
int audio_extn_utils_compress_correct_drift(
        struct stream_out *out __unused,
        struct audio_out_correct_drift *drift_param __unused)
{
    ALOGD("%s:: setting adjust clock not supported", __func__);
    return 0;
}
#endif

int audio_extn_utils_set_channel_map(
            struct stream_out *out,
            struct audio_out_channel_map_param *channel_map_param)
{
    int ret = -EINVAL, i = 0;
    int channels = audio_channel_count_from_out_mask(out->channel_mask);

    if (channel_map_param == NULL) {
        ALOGE("%s:: Invalid channel_map", __func__);
        goto exit;
    }

    if (channel_map_param->channels != channels) {
        ALOGE("%s:: Channels(%d) does not match stream channels(%d)",
                                __func__, channel_map_param->channels, channels);
        goto exit;
    }

    for ( i = 0; i < channels; i++) {
        ALOGV("%s:: channel_map[%d]- %d", __func__, i, channel_map_param->channel_map[i]);
        out->channel_map_param.channel_map[i] = channel_map_param->channel_map[i];
    }
    ret = 0;
exit:
    return ret;
}

int audio_extn_utils_set_pan_scale_params(
            struct stream_out *out,
            struct mix_matrix_params *mm_params)
{
    int ret = -EINVAL, i = 0, j = 0;

    if (mm_params == NULL || out == NULL) {
        ALOGE("%s:: Invalid mix matrix or out param", __func__);
        goto exit;
    }

    if (mm_params->num_output_channels > MAX_CHANNELS_SUPPORTED ||
        mm_params->num_output_channels <= 0 ||
        mm_params->num_input_channels > MAX_CHANNELS_SUPPORTED ||
        mm_params->num_input_channels <= 0)
        goto exit;

    out->pan_scale_params.num_output_channels = mm_params->num_output_channels;
    out->pan_scale_params.num_input_channels = mm_params->num_input_channels;
    out->pan_scale_params.has_output_channel_map =
                                        mm_params->has_output_channel_map;
    for (i = 0; i < mm_params->num_output_channels; i++)
        out->pan_scale_params.output_channel_map[i] =
                                         mm_params->output_channel_map[i];

    out->pan_scale_params.has_input_channel_map =
                                         mm_params->has_input_channel_map;
    for (i = 0; i < mm_params->num_input_channels; i++)
        out->pan_scale_params.input_channel_map[i] =
                                         mm_params->input_channel_map[i];

    out->pan_scale_params.has_mixer_coeffs = mm_params->has_mixer_coeffs;
    for (i = 0; i < mm_params->num_output_channels; i++)
        for (j = 0; j < mm_params->num_input_channels; j++) {
            //Convert the channel coefficient gains in Q14 format
            out->pan_scale_params.mixer_coeffs[i][j] =
                               mm_params->mixer_coeffs[i][j] * (2 << 13);
        }

    ret = platform_set_stream_pan_scale_params(out->dev->platform,
                                               out->pcm_device_id,
                                               out->pan_scale_params);

exit:
    return ret;
}

int audio_extn_utils_set_downmix_params(
            struct stream_out *out,
            struct mix_matrix_params *mm_params)
{
    int ret = -EINVAL, i = 0, j = 0;
    struct audio_usecase *usecase = NULL;

    if (mm_params == NULL || out == NULL) {
        ALOGE("%s:: Invalid mix matrix or out param", __func__);
        goto exit;
    }

    if (mm_params->num_output_channels > MAX_CHANNELS_SUPPORTED ||
        mm_params->num_output_channels <= 0 ||
        mm_params->num_input_channels > MAX_CHANNELS_SUPPORTED ||
        mm_params->num_input_channels <= 0)
        goto exit;

    usecase = get_usecase_from_list(out->dev, out->usecase);
    if (!usecase) {
        ALOGE("%s: Get usecase list failed!", __func__);
        goto exit;
    }
    out->downmix_params.num_output_channels = mm_params->num_output_channels;
    out->downmix_params.num_input_channels = mm_params->num_input_channels;

    out->downmix_params.has_output_channel_map =
                                        mm_params->has_output_channel_map;
    for (i = 0; i < mm_params->num_output_channels; i++) {
        out->downmix_params.output_channel_map[i] =
                                          mm_params->output_channel_map[i];
    }

    out->downmix_params.has_input_channel_map =
                                        mm_params->has_input_channel_map;
    for (i = 0; i < mm_params->num_input_channels; i++)
        out->downmix_params.input_channel_map[i] =
                                          mm_params->input_channel_map[i];

    out->downmix_params.has_mixer_coeffs = mm_params->has_mixer_coeffs;
    for (i = 0; i < mm_params->num_output_channels; i++)
        for (j = 0; j < mm_params->num_input_channels; j++) {
            //Convert the channel coefficient gains in Q14 format
            out->downmix_params.mixer_coeffs[i][j] =
                               mm_params->mixer_coeffs[i][j] * (2 << 13);
    }

    ret = platform_set_stream_downmix_params(out->dev->platform,
                                             out->pcm_device_id,
                                             usecase->out_snd_device,
                                             out->downmix_params);

exit:
    return ret;
}

bool audio_extn_utils_is_dolby_format(audio_format_t format)
{
    if (format == AUDIO_FORMAT_AC3 ||
            format == AUDIO_FORMAT_E_AC3 ||
            format == AUDIO_FORMAT_E_AC3_JOC)
        return true;
    else
        return false;
}

int audio_extn_utils_get_bit_width_from_string(const char *id_string)
{
    int i;
    const mixer_config_lookup mixer_bitwidth_config[] = {{"S24_3LE", 24},
                                                         {"S32_LE", 32},
                                                         {"S24_LE", 24},
                                                         {"S16_LE", 16}};
    int num_configs = sizeof(mixer_bitwidth_config) / sizeof(mixer_bitwidth_config[0]);

    for (i = 0; i < num_configs; i++) {
        if (!strcmp(id_string, mixer_bitwidth_config[i].id_string))
            return mixer_bitwidth_config[i].value;
    }

    return -EINVAL;
}

int audio_extn_utils_get_sample_rate_from_string(const char *id_string)
{
    int i;
    const mixer_config_lookup mixer_samplerate_config[] = {{"KHZ_8", 8000},
                                                           {"KHZ_11P025", 11025},
                                                           {"KHZ_16", 16000},
                                                           {"KHZ_22P05", 22050},
                                                           {"KHZ_32", 32000},
                                                           {"KHZ_48", 48000},
                                                           {"KHZ_96", 96000},
                                                           {"KHZ_144", 144000},
                                                           {"KHZ_192", 192000},
                                                           {"KHZ_384", 384000},
                                                           {"KHZ_44P1", 44100},
                                                           {"KHZ_88P2", 88200},
                                                           {"KHZ_176P4", 176400},
                                                           {"KHZ_352P8", 352800}};
    int num_configs = sizeof(mixer_samplerate_config) / sizeof(mixer_samplerate_config[0]);

    for (i = 0; i < num_configs; i++) {
        if (!strcmp(id_string, mixer_samplerate_config[i].id_string))
            return mixer_samplerate_config[i].value;
    }

    return -EINVAL;
}

int audio_extn_utils_get_channels_from_string(const char *id_string)
{
    int i;
    const mixer_config_lookup mixer_channels_config[] = {{"One", 1},
                                                         {"Two", 2},
                                                         {"Three",3},
                                                         {"Four", 4},
                                                         {"Five", 5},
                                                         {"Six", 6},
                                                         {"Seven", 7},
                                                         {"Eight", 8}};
    int num_configs = sizeof(mixer_channels_config) / sizeof(mixer_channels_config[0]);

    for (i = 0; i < num_configs; i++) {
        if (!strcmp(id_string, mixer_channels_config[i].id_string))
            return mixer_channels_config[i].value;
    }

    return -EINVAL;
}

void audio_extn_utils_release_snd_device(snd_device_t snd_device)
{
    audio_extn_dev_arbi_release(snd_device);
    audio_extn_sound_trigger_update_device_status(snd_device,
            ST_EVENT_SND_DEVICE_FREE);
    audio_extn_listen_update_device_status(snd_device,
            LISTEN_EVENT_SND_DEVICE_FREE);
}

int audio_extn_utils_get_license_params(
        const struct audio_device *adev,
        struct audio_license_params *license_params)
{
    if(!license_params)
        return -EINVAL;

    return platform_get_license_by_product(adev->platform,
            (const char*)license_params->product, &license_params->key, license_params->license);
}

int audio_extn_utils_send_app_type_gain(struct audio_device *adev,
                                        int app_type,
                                        int *gain)
{
    int gain_cfg[4];
    const char *mixer_ctl_name = "App Type Gain";
    struct mixer_ctl *ctl;
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get volume ctl mixer %s", __func__,
              mixer_ctl_name);
        return -EINVAL;
    }
    gain_cfg[0] = 0;
    gain_cfg[1] = app_type;
    gain_cfg[2] = gain[0];
    gain_cfg[3] = gain[1];
    ALOGV("%s app_type %d l(%d) r(%d)", __func__,  app_type, gain[0], gain[1]);
    return mixer_ctl_set_array(ctl, gain_cfg,
                               sizeof(gain_cfg)/sizeof(gain_cfg[0]));
}

static void vndk_fwk_init()
{
    if (mVndkFwk.lib_handle != NULL)
        return;

    mVndkFwk.lib_handle = dlopen(VNDK_FWK_LIB_PATH, RTLD_NOW);
    if (mVndkFwk.lib_handle == NULL) {
        ALOGW("%s: failed to dlopen VNDK_FWK_LIB %s", __func__,  strerror(errno));
        return;
    }

    *(void **)(&mVndkFwk.isVendorEnhancedFwk) =
        dlsym(mVndkFwk.lib_handle, "isRunningWithVendorEnhancedFramework");
    if (mVndkFwk.isVendorEnhancedFwk == NULL) {
        ALOGW("%s: dlsym failed %s", __func__, strerror(errno));
        if (mVndkFwk.lib_handle) {
            dlclose(mVndkFwk.lib_handle);
            mVndkFwk.lib_handle = NULL;
        }
        return;
    }


    *(void **)(&mVndkFwk.getVendorEnhancedInfo) =
        dlsym(mVndkFwk.lib_handle, "getVendorEnhancedInfo");
    if (mVndkFwk.getVendorEnhancedInfo == NULL) {
        ALOGW("%s: dlsym failed %s", __func__, strerror(errno));
        if (mVndkFwk.lib_handle) {
            dlclose(mVndkFwk.lib_handle);
            mVndkFwk.lib_handle = NULL;
        }
    }

    return;
}

bool audio_extn_utils_is_vendor_enhanced_fwk()
{
    static int is_vendor_enhanced_fwk = -EINVAL;
    if (is_vendor_enhanced_fwk != -EINVAL)
        return (bool)is_vendor_enhanced_fwk;

    vndk_fwk_init();

    if (mVndkFwk.isVendorEnhancedFwk != NULL) {
        is_vendor_enhanced_fwk = mVndkFwk.isVendorEnhancedFwk();
        ALOGW("%s: is_vendor_enhanced_fwk %d", __func__, is_vendor_enhanced_fwk);
    } else {
        is_vendor_enhanced_fwk = 0;
        ALOGW("%s: default to non enhanced_fwk config", __func__);
    }

    return (bool)is_vendor_enhanced_fwk;
}

int audio_extn_utils_get_vendor_enhanced_info()
{
    static int vendor_enhanced_info = -EINVAL;
    if (vendor_enhanced_info != -EINVAL)
        return vendor_enhanced_info;

    vndk_fwk_init();

    if (mVndkFwk.getVendorEnhancedInfo != NULL) {
        vendor_enhanced_info = mVndkFwk.getVendorEnhancedInfo();
        ALOGW("%s: vendor_enhanced_info 0x%x", __func__, vendor_enhanced_info);
    } else {
        vendor_enhanced_info = 0x0;
        ALOGW("%s: default to vendor_enhanced_info 0x0", __func__);
    }

    return vendor_enhanced_info;
}

int audio_extn_utils_get_perf_mode_flag(void)
{
#ifdef COMPRESSED_PERF_MODE_FLAG
    return COMPRESSED_PERF_MODE_FLAG;
#else
    return 0;
#endif
}

size_t audio_extn_utils_get_input_buffer_size(uint32_t sample_rate,
                                            audio_format_t format,
                                            int channel_count,
                                            int64_t duration_ms,
                                            bool is_low_latency)
{
    size_t size = 0;
    size_t capture_duration = AUDIO_CAPTURE_PERIOD_DURATION_MSEC;
    uint32_t bytes_per_period_sample = 0;


    if (audio_extn_utils_check_input_parameters(sample_rate, format, channel_count) != 0)
        return 0;

    if (duration_ms >= MIN_OFFLOAD_BUFFER_DURATION_MS && duration_ms <= MAX_OFFLOAD_BUFFER_DURATION_MS)
        capture_duration = duration_ms;

    size = (sample_rate * capture_duration) / 1000;
    if (is_low_latency)
        size = LOW_LATENCY_CAPTURE_PERIOD_SIZE;


    bytes_per_period_sample = audio_bytes_per_sample(format) * channel_count;
    size *= bytes_per_period_sample;

    /* make sure the size is multiple of 32 bytes and additionally multiple of
     * the frame_size (required for 24bit samples and non-power-of-2 channel counts)
     * At 48 kHz mono 16-bit PCM:
     *  5.000 ms = 240 frames = 15*16*1*2 = 480, a whole multiple of 32 (15)
     *  3.333 ms = 160 frames = 10*16*1*2 = 320, a whole multiple of 32 (10)
     *
     *  The loop reaches result within 32 iterations, as initial size is
     *  already a multiple of frame_size
     */
    size = audio_extn_utils_nearest_multiple(size, audio_extn_utils_lcm(32, bytes_per_period_sample));

    return size;
}

int audio_extn_utils_hash_fn(void *key)
{
    return (int)key;
}

bool audio_extn_utils_hash_eq(void *key1, void *key2)
{
    return (key1 == key2);
}
