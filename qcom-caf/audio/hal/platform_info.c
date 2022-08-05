/*
 * Copyright (c) 2014-2020, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "platform_info"
#define LOG_NDDEBUG 0

#include <errno.h>
#include <stdio.h>
#include <expat.h>
#include <pthread.h>
#include <log/log.h>
#include <cutils/str_parms.h>
#include <audio_hw.h>
#include "acdb.h"
#include "platform_api.h"
#include "audio_extn.h"
#include <platform.h>
#include <math.h>
#ifdef LINUX_ENABLED
#include <float.h>
#endif

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_PLATFORM_INFO
#include <log_utils.h>
#endif

#define BUF_SIZE                    1024
char vendor_config_path[VENDOR_CONFIG_PATH_MAX_LENGTH];
char platform_info_xml_path_file[VENDOR_CONFIG_FILE_MAX_LENGTH];

typedef enum {
    ROOT,
    ACDB,
    MODULE,
    AEC,
    NS,
    BITWIDTH,
    PCM_ID,
    BACKEND_NAME,
    INTERFACE_NAME,
    CONFIG_PARAMS,
    OPERATOR_SPECIFIC,
    GAIN_LEVEL_MAPPING,
    APP_TYPE,
    ACDB_METAINFO_KEY,
    MICROPHONE_CHARACTERISTIC,
    SND_DEVICES,
    INPUT_SND_DEVICE,
    INPUT_SND_DEVICE_TO_MIC_MAPPING,
    SND_DEV,
    MIC_INFO,
    CUSTOM_MTMX_PARAMS,
    CUSTOM_MTMX_PARAM_COEFFS,
    EXTERNAL_DEVICE_SPECIFIC,
    CUSTOM_MTMX_IN_PARAMS,
    CUSTOM_MTMX_PARAM_IN_CH_INFO,
    MMSECNS,
    AUDIO_SOURCE_DELAY,
} section_t;

typedef void (* section_process_fn)(const XML_Char **attr);

const char* get_platform_xml_path()
{
    audio_get_vendor_config_path(vendor_config_path, sizeof(vendor_config_path));
    /* Get path for platorm_info_xml_path_name in vendor */
    snprintf(platform_info_xml_path_file, sizeof(platform_info_xml_path_file),
            "%s/%s", vendor_config_path, PLATFORM_INFO_XML_PATH_NAME);
    return platform_info_xml_path_file;
}

static void process_acdb_id(const XML_Char **attr);
static void process_audio_effect(const XML_Char **attr, effect_type_t effect_type);
static void process_effect_aec(const XML_Char **attr);
static void process_effect_ns(const XML_Char **attr);
static void process_bit_width(const XML_Char **attr);
static void process_pcm_id(const XML_Char **attr);
static void process_backend_name(const XML_Char **attr);
static void process_interface_name(const XML_Char **attr);
static void process_config_params(const XML_Char **attr);
static void process_root(const XML_Char **attr);
static void process_operator_specific(const XML_Char **attr);
static void process_gain_db_to_level_map(const XML_Char **attr);
static void process_app_type(const XML_Char **attr);
static void process_acdb_metainfo_key(const XML_Char **attr);
static void process_microphone_characteristic(const XML_Char **attr);
static void process_snd_dev(const XML_Char **attr);
static void process_mic_info(const XML_Char **attr);
static void process_custom_mtmx_params(const XML_Char **attr);
static void process_custom_mtmx_param_coeffs(const XML_Char **attr);
static void process_external_dev(const XML_Char **attr);
static void process_custom_mtmx_in_params(const XML_Char **attr);
static void process_custom_mtmx_param_in_ch_info(const XML_Char **attr);
static void process_fluence_mmsecns(const XML_Char **attr);
static void process_audio_source_delay(const XML_Char **attr);

static section_process_fn section_table[] = {
    [ROOT] = process_root,
    [ACDB] = process_acdb_id,
    [AEC] = process_effect_aec,
    [NS] = process_effect_ns,
    [BITWIDTH] = process_bit_width,
    [PCM_ID] = process_pcm_id,
    [BACKEND_NAME] = process_backend_name,
    [INTERFACE_NAME] = process_interface_name,
    [CONFIG_PARAMS] = process_config_params,
    [OPERATOR_SPECIFIC] = process_operator_specific,
    [APP_TYPE] = process_app_type,
    [GAIN_LEVEL_MAPPING] = process_gain_db_to_level_map,
    [ACDB_METAINFO_KEY] = process_acdb_metainfo_key,
    [MICROPHONE_CHARACTERISTIC] = process_microphone_characteristic,
    [SND_DEV] = process_snd_dev,
    [MIC_INFO] = process_mic_info,
    [CUSTOM_MTMX_PARAMS] = process_custom_mtmx_params,
    [CUSTOM_MTMX_PARAM_COEFFS] = process_custom_mtmx_param_coeffs,
    [EXTERNAL_DEVICE_SPECIFIC] = process_external_dev,
    [CUSTOM_MTMX_IN_PARAMS] = process_custom_mtmx_in_params,
    [CUSTOM_MTMX_PARAM_IN_CH_INFO] = process_custom_mtmx_param_in_ch_info,
    [MMSECNS] = process_fluence_mmsecns,
    [AUDIO_SOURCE_DELAY] = process_audio_source_delay,
};

static section_t section;

struct platform_info {
    caller_t          caller;
    void             *platform;
    struct str_parms *kvpairs;
};

static struct platform_info my_data;
static pthread_mutex_t parser_lock = PTHREAD_MUTEX_INITIALIZER;


struct audio_string_to_enum {
    const char* name;
    unsigned int value;
};

static snd_device_t in_snd_device;

static const struct audio_string_to_enum mic_locations[AUDIO_MICROPHONE_LOCATION_CNT] = {
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_LOCATION_UNKNOWN),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_LOCATION_MAINBODY),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_LOCATION_MAINBODY_MOVABLE),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_LOCATION_PERIPHERAL),
};

static const struct audio_string_to_enum mic_directionalities[AUDIO_MICROPHONE_DIRECTIONALITY_CNT] = {
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_OMNI),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_BI_DIRECTIONAL),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_UNKNOWN),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_CARDIOID),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_HYPER_CARDIOID),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_DIRECTIONALITY_SUPER_CARDIOID),
};

static const struct audio_string_to_enum mic_channel_mapping[AUDIO_MICROPHONE_CHANNEL_MAPPING_CNT] = {
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_CHANNEL_MAPPING_UNUSED),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_CHANNEL_MAPPING_DIRECT),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_MICROPHONE_CHANNEL_MAPPING_PROCESSED),
};

static const struct audio_string_to_enum device_in_types[] = {
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_AMBIENT),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_COMMUNICATION),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BUILTIN_MIC),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_WIRED_HEADSET),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_AUX_DIGITAL),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_HDMI),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_VOICE_CALL),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_TELEPHONY_RX),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BACK_MIC),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_REMOTE_SUBMIX),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_DGTL_DOCK_HEADSET),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_USB_ACCESSORY),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_USB_DEVICE),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_FM_TUNER),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_TV_TUNER),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_LINE),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_SPDIF),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BLUETOOTH_A2DP),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_LOOPBACK),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_IP),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BUS),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_PROXY),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_USB_HEADSET),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_BLUETOOTH_BLE),
    AUDIO_MAKE_STRING_FROM_ENUM(AUDIO_DEVICE_IN_DEFAULT),
};

enum {
    AUDIO_MICROPHONE_CHARACTERISTIC_NONE = 0u, // 0x0
    AUDIO_MICROPHONE_CHARACTERISTIC_SENSITIVITY = 1u, // 0x1
    AUDIO_MICROPHONE_CHARACTERISTIC_MAX_SPL = 2u, // 0x2
    AUDIO_MICROPHONE_CHARACTERISTIC_MIN_SPL = 4u, // 0x4
    AUDIO_MICROPHONE_CHARACTERISTIC_ORIENTATION = 8u, // 0x8
    AUDIO_MICROPHONE_CHARACTERISTIC_GEOMETRIC_LOCATION = 16u, // 0x10
    AUDIO_MICROPHONE_CHARACTERISTIC_ALL = 31u, /* ((((SENSITIVITY | MAX_SPL) | MIN_SPL)
                                                  | ORIENTATION) | GEOMETRIC_LOCATION) */
};

static bool find_enum_by_string(const struct audio_string_to_enum * table, const char * name,
                                int32_t len, unsigned int *value)
{
    if (table == NULL) {
        ALOGE("%s: table is NULL", __func__);
        return false;
    }

    if (name == NULL) {
        ALOGE("null key");
        return false;
    }

    for (int i = 0; i < len; i++) {
        if (!strcmp(table[i].name, name)) {
            *value = table[i].value;
            return true;
        }
    }
    return false;
}

static struct audio_custom_mtmx_params_info mtmx_params_info;
static struct audio_custom_mtmx_in_params_info mtmx_in_params_info;

/*
 * <audio_platform_info>
 * <acdb_ids>
 * <device name="???" acdb_id="???"/>
 * ...
 * ...
 * </acdb_ids>
 * <module_ids>
 * <device name="???" module_id="???"/>
 * ...
 * ...
 * </module_ids>
 * <backend_names>
 * <device name="???" backend="???"/>
 * ...
 * ...
 * </backend_names>
 * <pcm_ids>
 * <usecase name="???" type="in/out" id="???"/>
 * ...
 * ...
 * </pcm_ids>
 * <interface_names>
 * <device name="Use audio device name here, not sound device name" interface="PRIMARY_I2S" codec_type="external/internal"/>
 * ...
 * ...
 * </interface_names>
 * <config_params>
 *      <param key="snd_card_name" value="msm8994-tomtom-mtp-snd-card"/>
 *      <param key="operator_info" value="tmus;aa;bb;cc"/>
 *      <param key="operator_info" value="sprint;xx;yy;zz"/>
 *      ...
 *      ...
 * </config_params>
 *
 * <operator_specific>
 *      <device name="???" operator="???" mixer_path="???" acdb_id="???"/>
 *      ...
 *      ...
 * </operator_specific>
 *
 * </audio_platform_info>
 */

static void process_root(const XML_Char **attr __unused)
{
}

/* mapping from usecase to pcm dev id */
static void process_pcm_id(const XML_Char **attr)
{
    int index;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found, no pcm_id set!", __func__);
        goto done;
    }

    index = platform_get_usecase_index((char *)attr[1]);
    if (index < 0) {
        ALOGE("%s: usecase %s not found!",
              __func__, attr[1]);
        goto done;
    }

    if (strcmp(attr[2], "type") != 0) {
        ALOGE("%s: usecase type not mentioned", __func__);
        goto done;
    }

    int type = -1;

    if (!strcasecmp((char *)attr[3], "in")) {
        type = 1;
    } else if (!strcasecmp((char *)attr[3], "out")) {
        type = 0;
    } else {
        ALOGE("%s: type must be IN or OUT", __func__);
        goto done;
    }

    if (strcmp(attr[4], "id") != 0) {
        ALOGE("%s: usecase id not mentioned", __func__);
        goto done;
    }

    int id = atoi((char *)attr[5]);

    if (platform_set_usecase_pcm_id(index, type, id) < 0) {
        ALOGE("%s: usecase %s type %d id %d was not set!",
              __func__, attr[1], type, id);
        goto done;
    }

done:
    return;
}

/* backend to be used for a device */
static void process_backend_name(const XML_Char **attr)
{
    int index;
    char *hw_interface = NULL;
    char *backend = NULL;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found, no ACDB ID set!", __func__);
        goto done;
    }

    index = platform_get_snd_device_index((char *)attr[1]);
    if (index < 0) {
        ALOGE("%s: Device %s not found, no ACDB ID set!",
              __func__, attr[1]);
        goto done;
    }

    if (strcmp(attr[2], "backend") != 0) {
        if (strcmp(attr[2], "interface") == 0)
            hw_interface = (char *)attr[3];
    } else {
        backend = (char *)attr[3];
    }

    if (attr[4] != NULL) {
        if (strcmp(attr[4], "interface") != 0) {
            hw_interface = NULL;
        } else {
            hw_interface = (char *)attr[5];
        }
    }

    if (platform_set_snd_device_backend(index, backend, hw_interface) < 0) {
        ALOGE("%s: Device %s backend %s was not set!",
              __func__, attr[1], attr[3]);
        goto done;
    }

done:
    return;
}

static void process_gain_db_to_level_map(const XML_Char **attr)
{
    struct amp_db_and_gain_table tbl_entry;

    if ((strcmp(attr[0], "db") != 0) ||
        (strcmp(attr[2], "level") != 0)) {
        ALOGE("%s: invalid attribute passed  %s %sexpected amp db level",
               __func__, attr[0], attr[2]);
        goto done;
    }

    tbl_entry.db = atof(attr[1]);
    tbl_entry.amp = exp(tbl_entry.db * 0.115129f);
    tbl_entry.level = atoi(attr[3]);

    //custome level should be > 0. Level 0 is fixed for default
    CHECK(tbl_entry.level > 0);

    ALOGV("%s: amp [%f]  db [%f] level [%d]", __func__,
           tbl_entry.amp, tbl_entry.db, tbl_entry.level);
    platform_add_gain_level_mapping(&tbl_entry);

done:
    return;
}


static void process_acdb_id(const XML_Char **attr)
{
    int index;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found, no ACDB ID set!", __func__);
        goto done;
    }

    index = platform_get_snd_device_index((char *)attr[1]);
    if (index < 0) {
        ALOGE("%s: Device %s in platform info xml not found, no ACDB ID set!",
              __func__, attr[1]);
        goto done;
    }

    if (strcmp(attr[2], "acdb_id") != 0) {
        ALOGE("%s: Device %s in platform info xml has no acdb_id, no ACDB ID set!",
              __func__, attr[1]);
        goto done;
    }

    if (platform_set_snd_device_acdb_id(index, atoi((char *)attr[3])) < 0) {
        ALOGE("%s: Device %s, ACDB ID %d was not set!",
              __func__, attr[1], atoi((char *)attr[3]));
        goto done;
    }

done:
    return;
}

static void process_operator_specific(const XML_Char **attr)
{
    snd_device_t snd_device = SND_DEVICE_NONE;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found", __func__);
        goto done;
    }

    snd_device = platform_get_snd_device_index((char *)attr[1]);
    if (snd_device < 0) {
        ALOGE("%s: Device %s in %s not found, no ACDB ID set!",
              __func__, (char *)attr[3], get_platform_xml_path());
        goto done;
    }

    if (strcmp(attr[2], "operator") != 0) {
        ALOGE("%s: 'operator' not found", __func__);
        goto done;
    }

    if (strcmp(attr[4], "mixer_path") != 0) {
        ALOGE("%s: 'mixer_path' not found", __func__);
        goto done;
    }

    if (strcmp(attr[6], "acdb_id") != 0) {
        ALOGE("%s: 'acdb_id' not found", __func__);
        goto done;
    }

    platform_add_operator_specific_device(snd_device, (char *)attr[3], (char *)attr[5], atoi((char *)attr[7]));

done:
    return;
}

static void process_external_dev(const XML_Char **attr)
{
    snd_device_t snd_device = SND_DEVICE_NONE;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found", __func__);
        goto done;
    }

    snd_device = platform_get_snd_device_index((char *)attr[1]);
    if (snd_device < 0) {
        ALOGE("%s: Device %s in %s not found, no ACDB ID set!",
              __func__, (char *)attr[3], get_platform_xml_path());
        goto done;
    }

    if (strcmp(attr[2], "usbid") != 0) {
        ALOGE("%s: 'usbid' not found", __func__);
        goto done;
    }

    if (strcmp(attr[4], "acdb_id") != 0) {
        ALOGE("%s: 'acdb_id' not found", __func__);
        goto done;
    }

    platform_add_external_specific_device(snd_device, (char *)attr[3], atoi((char *)attr[5]));

done:
    return;
}

static void process_audio_effect(const XML_Char **attr, effect_type_t effect_type)
{
    int index;
    struct audio_effect_config effect_config;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found, no MODULE ID set!", __func__);
        goto done;
    }

    index = platform_get_snd_device_index((char *)attr[1]);
    if (index < 0) {
        ALOGE("%s: Device %s in platform info xml not found, no MODULE ID set!",
              __func__, attr[1]);
        goto done;
    }

    if (strcmp(attr[2], "module_id") != 0) {
        ALOGE("%s: Device %s in platform info xml has no module_id, no MODULE ID set!",
              __func__, attr[2]);
        goto done;
    }

    if (strcmp(attr[4], "instance_id") != 0) {
        ALOGE("%s: Device %s in platform info xml has no instance_id, no INSTANCE ID set!",
              __func__, attr[4]);
        goto done;
    }

    if (strcmp(attr[6], "param_id") != 0) {
        ALOGE("%s: Device %s in platform info xml has no param_id, no PARAM ID set!",
              __func__, attr[6]);
        goto done;
    }

    if (strcmp(attr[8], "param_value") != 0) {
        ALOGE("%s: Device %s in platform info xml has no param_value, no PARAM VALUE set!",
              __func__, attr[8]);
        goto done;
    }

    effect_config = (struct audio_effect_config){strtol((char *)attr[3], NULL, 0),
                                                 strtol((char *)attr[5], NULL, 0),
                                                 strtol((char *)attr[7], NULL, 0),
                                                 strtol((char *)attr[9], NULL, 0)};

    if (platform_set_effect_config_data(index, effect_config, effect_type) < 0) {
        ALOGE("%s: Effect = %d Device %s, MODULE/INSTANCE/PARAM ID %lu %lu %lu %lu was not set!",
              __func__, effect_type, attr[1], strtol((char *)attr[3], NULL, 0),
              strtol((char *)attr[5], NULL, 0), strtol((char *)attr[7], NULL, 0),
              strtol((char *)attr[9], NULL, 0));
        goto done;
    }

done:
    return;
}

static void process_effect_aec(const XML_Char **attr)
{
    process_audio_effect(attr, EFFECT_AEC);
    return;
}

static void process_effect_ns(const XML_Char **attr)
{
    process_audio_effect(attr, EFFECT_NS);
    return;
}

static void process_fluence_mmsecns(const XML_Char **attr)
{
    int index;
    struct audio_fluence_mmsecns_config fluence_mmsecns_config;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found, no MODULE ID set!", __func__);
        goto done;
    }

    index = platform_get_snd_device_index((char *)attr[1]);
    if (index < 0) {
        ALOGE("%s: Device %s in platform info xml not found, no MODULE ID set!",
              __func__, attr[1]);
        goto done;
    }

    if (strcmp(attr[2], "topology_id") != 0) {
        ALOGE("%s: Device %s in platform info xml has no topology_id, no MODULE ID set!",
              __func__, attr[2]);
        goto done;
    }

    if (strcmp(attr[4], "module_id") != 0) {
        ALOGE("%s: Device %s in platform info xml has no module_id, no MODULE ID set!",
              __func__, attr[4]);
        goto done;
    }

    if (strcmp(attr[6], "instance_id") != 0) {
        ALOGE("%s: Device %s in platform info xml has no instance_id, no INSTANCE ID set!",
              __func__, attr[6]);
        goto done;
    }

    if (strcmp(attr[8], "param_id") != 0) {
        ALOGE("%s: Device %s in platform info xml has no param_id, no PARAM ID set!",
              __func__, attr[8]);
        goto done;
    }

    fluence_mmsecns_config = (struct audio_fluence_mmsecns_config){strtol((char *)attr[3], NULL, 0),
                                                                   strtol((char *)attr[5], NULL, 0),
                                                                   strtol((char *)attr[7], NULL, 0),
                                                                   strtol((char *)attr[9], NULL, 0)};


    if (platform_set_fluence_mmsecns_config(fluence_mmsecns_config) < 0) {
        ALOGE("%s: Device %s, TOPOLOGY/MODULE/INSTANCE/PARAM ID %lu %lu %lu %lu was not set!",
              __func__, attr[1], strtol((char *)attr[3], NULL, 0), strtol((char *)attr[5], NULL, 0),
              strtol((char *)attr[7], NULL, 0), strtol((char *)attr[9], NULL, 0));
        goto done;
    }

done:
    return;
}
static void process_bit_width(const XML_Char **attr)
{
    int index;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found, no ACDB ID set!", __func__);
        goto done;
    }

    index = platform_get_snd_device_index((char *)attr[1]);
    if (index < 0) {
        ALOGE("%s: Device %s in platform info xml not found, no ACDB ID set!",
              __func__, attr[1]);
        goto done;
    }

    if (strcmp(attr[2], "bit_width") != 0) {
        ALOGE("%s: Device %s in platform info xml has no bit_width, no ACDB ID set!",
              __func__, attr[1]);
        goto done;
    }

    if (platform_set_snd_device_bit_width(index, atoi((char *)attr[3])) < 0) {
        ALOGE("%s: Device %s, ACDB ID %d was not set!",
              __func__, attr[1], atoi((char *)attr[3]));
        goto done;
    }

done:
    return;
}

static void process_interface_name(const XML_Char **attr)
{
    int ret;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found, no Audio Interface set!", __func__);

        goto done;
    }

    if (strcmp(attr[2], "interface") != 0) {
        ALOGE("%s: Device %s has no Audio Interface set!",
              __func__, attr[1]);

        goto done;
    }

    if (strcmp(attr[4], "codec_type") != 0) {
        ALOGE("%s: Device %s has no codec type set!",
              __func__, attr[1]);

        goto done;
    }

    ret = platform_set_audio_device_interface((char *)attr[1], (char *)attr[3],
                                              (char *)attr[5]);
    if (ret < 0) {
        ALOGE("%s: Audio Interface not set!", __func__);
        goto done;
    }

done:
    return;
}

static void process_audio_source_delay(const XML_Char **attr)
{
    audio_source_t audio_source = -1;

    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found", __func__);
        goto done;
    }

    audio_source = platform_get_audio_source_index((const char *)attr[1]);

    if (audio_source < 0) {
        ALOGE("%s: audio_source %s is not defined",
              __func__, (char *)attr[1]);
        goto done;
    }

    if (strcmp(attr[2], "delay") != 0) {
        ALOGE("%s: 'delay' not found", __func__);
        goto done;
    }

    platform_set_audio_source_delay(audio_source, atoi((char *)attr[3]));

done:
    return;
}

static void process_config_params(const XML_Char **attr)
{
    if (strcmp(attr[0], "key") != 0) {
        ALOGE("%s: 'key' not found", __func__);
        goto done;
    }

    if (strcmp(attr[2], "value") != 0) {
        ALOGE("%s: 'value' not found", __func__);
        goto done;
    }

    str_parms_add_str(my_data.kvpairs, (char*)attr[1], (char*)attr[3]);
    if (my_data.caller == PLATFORM)
        platform_set_parameters(my_data.platform, my_data.kvpairs);
done:
    return;
}

static void process_app_type(const XML_Char **attr)
{
    if (strcmp(attr[0], "uc_type")) {
        ALOGE("%s: uc_type not found", __func__);
        goto done;
    }

    if (strcmp(attr[2], "mode")) {
        ALOGE("%s: mode not found", __func__);
        goto done;
    }

    if (strcmp(attr[4], "bit_width")) {
        ALOGE("%s: bit_width not found", __func__);
        goto done;
    }

    if (strcmp(attr[6], "id")) {
        ALOGE("%s: id not found", __func__);
        goto done;
    }

    if (strcmp(attr[8], "max_rate")) {
        ALOGE("%s: max rate not found", __func__);
        goto done;
    }

    platform_add_app_type(attr[1], attr[3], atoi(attr[5]), atoi(attr[7]),
                          atoi(attr[9]));
done:
    return;
}

static void process_microphone_characteristic(const XML_Char **attr) {
    struct audio_microphone_characteristic_t microphone;
    uint32_t curIdx = 0;
    char platform_info_xml_path[VENDOR_CONFIG_FILE_MAX_LENGTH];

    strlcpy(platform_info_xml_path, get_platform_xml_path(),
        sizeof(platform_info_xml_path));
    if (strcmp(attr[curIdx++], "valid_mask")) {
        ALOGE("%s: valid_mask not found", __func__);
        goto done;
    }
    uint32_t valid_mask = atoi(attr[curIdx++]);

    if (strcmp(attr[curIdx++], "device_id")) {
        ALOGE("%s: device_id not found", __func__);
        goto done;
    }
    if (strlen(attr[curIdx]) > AUDIO_MICROPHONE_ID_MAX_LEN) {
        ALOGE("%s: device_id %s is too long", __func__, attr[curIdx]);
        goto done;
    }
    strlcpy(microphone.device_id, attr[curIdx++], sizeof(microphone.device_id));

    if (strcmp(attr[curIdx++], "type")) {
        ALOGE("%s: device not found", __func__);
        goto done;
    }
    if (!find_enum_by_string(device_in_types, (char*)attr[curIdx++],
            ARRAY_SIZE(device_in_types), &microphone.device)) {
        ALOGE("%s: type %s in %s not found!",
              __func__, attr[--curIdx], platform_info_xml_path);
        goto done;
    }

    if (strcmp(attr[curIdx++], "address")) {
        ALOGE("%s: address not found", __func__);
        goto done;
    }
    if (strlen(attr[curIdx]) > AUDIO_DEVICE_MAX_ADDRESS_LEN) {
        ALOGE("%s, address %s is too long", __func__, attr[curIdx]);
        goto done;
    }
    strlcpy(microphone.address, attr[curIdx++], sizeof(microphone.address));
    if (strlen(microphone.address) == 0) {
        // If the address is empty, populate the address according to device type.
        if (microphone.device == AUDIO_DEVICE_IN_BUILTIN_MIC) {
            strlcpy(microphone.address, AUDIO_BOTTOM_MICROPHONE_ADDRESS, sizeof(microphone.address));
        } else if (microphone.device == AUDIO_DEVICE_IN_BACK_MIC) {
            strlcpy(microphone.address, AUDIO_BACK_MICROPHONE_ADDRESS, sizeof(microphone.address));
        }
    }

    if (strcmp(attr[curIdx++], "location")) {
        ALOGE("%s: location not found", __func__);
        goto done;
    }
    if (!find_enum_by_string(mic_locations, (char*)attr[curIdx++],
            AUDIO_MICROPHONE_LOCATION_CNT, &microphone.location)) {
        ALOGE("%s: location %s in %s not found!",
              __func__, attr[--curIdx], platform_info_xml_path);
        goto done;
    }

    if (strcmp(attr[curIdx++], "group")) {
        ALOGE("%s: group not found", __func__);
        goto done;
    }
    microphone.group = atoi(attr[curIdx++]);

    if (strcmp(attr[curIdx++], "index_in_the_group")) {
        ALOGE("%s: index_in_the_group not found", __func__);
        goto done;
    }
    microphone.index_in_the_group = atoi(attr[curIdx++]);

    if (strcmp(attr[curIdx++], "directionality")) {
        ALOGE("%s: directionality not found", __func__);
        goto done;
    }
    if (!find_enum_by_string(mic_directionalities, (char*)attr[curIdx++],
                AUDIO_MICROPHONE_DIRECTIONALITY_CNT, &microphone.directionality)) {
        ALOGE("%s: directionality %s in %s not found!",
              __func__, attr[--curIdx], platform_info_xml_path);
        goto done;
    }

    if (strcmp(attr[curIdx++], "num_frequency_responses")) {
        ALOGE("%s: num_frequency_responses not found", __func__);
        goto done;
    }
    microphone.num_frequency_responses = atoi(attr[curIdx++]);
    if (microphone.num_frequency_responses > AUDIO_MICROPHONE_MAX_FREQUENCY_RESPONSES) {
        ALOGE("%s: num_frequency_responses is too large", __func__);
        goto done;
    }
    if (microphone.num_frequency_responses > 0) {
        if (strcmp(attr[curIdx++], "frequencies")) {
            ALOGE("%s: frequencies not found", __func__);
            goto done;
        }
        char *context = NULL;
        char *token = strtok_r((char *)attr[curIdx++], " ", &context);
        uint32_t num_frequencies = 0;
        while (token) {
            microphone.frequency_responses[0][num_frequencies++] = atof(token);
            if (num_frequencies >= AUDIO_MICROPHONE_MAX_FREQUENCY_RESPONSES) {
                break;
            }
            token = strtok_r(NULL, " ", &context);
        }

        if (strcmp(attr[curIdx++], "responses")) {
            ALOGE("%s: responses not found", __func__);
            goto done;
        }
        token = strtok_r((char *)attr[curIdx++], " ", &context);
        uint32_t num_responses = 0;
        while (token) {
            microphone.frequency_responses[1][num_responses++] = atof(token);
            if (num_responses >= AUDIO_MICROPHONE_MAX_FREQUENCY_RESPONSES) {
                break;
            }
            token = strtok_r(NULL, " ", &context);
        }

        if (num_frequencies != num_responses
                || num_frequencies != microphone.num_frequency_responses) {
            ALOGE("%s: num of frequency and response not match: %u, %u, %u",
                  __func__, num_frequencies, num_responses, microphone.num_frequency_responses);
            goto done;
        }
    }

    if (valid_mask & AUDIO_MICROPHONE_CHARACTERISTIC_SENSITIVITY) {
        if (strcmp(attr[curIdx++], "sensitivity")) {
            ALOGE("%s: sensitivity not found", __func__);
            goto done;
        }
        microphone.sensitivity = atof(attr[curIdx++]);
    } else {
        microphone.sensitivity = AUDIO_MICROPHONE_SENSITIVITY_UNKNOWN;
    }

    if (valid_mask & AUDIO_MICROPHONE_CHARACTERISTIC_MAX_SPL) {
        if (strcmp(attr[curIdx++], "max_spl")) {
            ALOGE("%s: max_spl not found", __func__);
            goto done;
        }
        microphone.max_spl = atof(attr[curIdx++]);
    } else {
        microphone.max_spl = AUDIO_MICROPHONE_SPL_UNKNOWN;
    }

    if (valid_mask & AUDIO_MICROPHONE_CHARACTERISTIC_MIN_SPL) {
        if (strcmp(attr[curIdx++], "min_spl")) {
            ALOGE("%s: min_spl not found", __func__);
            goto done;
        }
        microphone.min_spl = atof(attr[curIdx++]);
    } else {
        microphone.min_spl = AUDIO_MICROPHONE_SPL_UNKNOWN;
    }

    if (valid_mask & AUDIO_MICROPHONE_CHARACTERISTIC_ORIENTATION) {
        if (strcmp(attr[curIdx++], "orientation")) {
            ALOGE("%s: orientation not found", __func__);
            goto done;
        }
        char *context = NULL;
        char *token = strtok_r((char *)attr[curIdx++], " ", &context);
        float orientation[3];
        uint32_t idx = 0;
        while (token) {
            orientation[idx++] = atof(token);
            if (idx >= 3) {
                break;
            }
            token = strtok_r(NULL, " ", &context);
        }
        if (idx != 3) {
            ALOGE("%s: orientation invalid", __func__);
            goto done;
        }
        microphone.orientation.x = orientation[0];
        microphone.orientation.y = orientation[1];
        microphone.orientation.z = orientation[2];
    } else {
        microphone.orientation.x = 0.0f;
        microphone.orientation.y = 0.0f;
        microphone.orientation.z = 0.0f;
    }

    if (valid_mask & AUDIO_MICROPHONE_CHARACTERISTIC_GEOMETRIC_LOCATION) {
        if (strcmp(attr[curIdx++], "geometric_location")) {
            ALOGE("%s: geometric_location not found", __func__);
            goto done;
        }
        char *context = NULL;
        char *token = strtok_r((char *)attr[curIdx++], " ", &context);
        float geometric_location[3];
        uint32_t idx = 0;
        while (token) {
            geometric_location[idx++] = atof(token);
            if (idx >= 3) {
                break;
            }
            token = strtok_r(NULL, " ", &context);
        }
        if (idx != 3) {
            ALOGE("%s: geometric_location invalid", __func__);
            goto done;
        }
        microphone.geometric_location.x = geometric_location[0];
        microphone.geometric_location.y = geometric_location[1];
        microphone.geometric_location.z = geometric_location[2];
    } else {
        microphone.geometric_location.x = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
        microphone.geometric_location.y = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
        microphone.geometric_location.z = AUDIO_MICROPHONE_COORDINATE_UNKNOWN;
    }

    platform_set_microphone_characteristic(my_data.platform, microphone);
done:
    return;
}

static void process_snd_dev(const XML_Char **attr)
{
    uint32_t curIdx = 0;
    in_snd_device = SND_DEVICE_NONE;

    if (strcmp(attr[curIdx++], "in_snd_device")) {
        ALOGE("%s: snd_device not found", __func__);
        return;
    }
    in_snd_device = platform_get_snd_device_index((char *)attr[curIdx++]);
    if (in_snd_device < SND_DEVICE_IN_BEGIN ||
            in_snd_device >= SND_DEVICE_IN_END) {
        ALOGE("%s: Sound device not valid", __func__);
        in_snd_device = SND_DEVICE_NONE;
    }

    return;
}

static void process_mic_info(const XML_Char **attr)
{
    uint32_t curIdx = 0;
    struct mic_info microphone;
    char platform_info_xml_path[VENDOR_CONFIG_FILE_MAX_LENGTH];

    strlcpy(platform_info_xml_path, get_platform_xml_path(),
        sizeof(platform_info_xml_path));
    memset(&microphone.channel_mapping, AUDIO_MICROPHONE_CHANNEL_MAPPING_UNUSED,
               sizeof(microphone.channel_mapping));

    if (strcmp(attr[curIdx++], "mic_device_id")) {
        ALOGE("%s: mic_device_id not found", __func__);
        goto on_error;
    }
    strlcpy(microphone.device_id,
                (char *)attr[curIdx++], AUDIO_MICROPHONE_ID_MAX_LEN);

    if (strcmp(attr[curIdx++], "channel_mapping")) {
        ALOGE("%s: channel_mapping not found", __func__);
        goto on_error;
    }
    char *context = NULL;
    const char *token = strtok_r((char *)attr[curIdx++], " ", &context);
    uint32_t idx = 0;
    while (token) {
        if (!find_enum_by_string(mic_channel_mapping, token,
                AUDIO_MICROPHONE_CHANNEL_MAPPING_CNT,
                &microphone.channel_mapping[idx++])) {
            ALOGE("%s: channel_mapping %s in %s not found!",
                      __func__, attr[--curIdx], platform_info_xml_path);
            goto on_error;
        }
        token = strtok_r(NULL, " ", &context);
    }
    microphone.channel_count = idx;

    platform_set_microphone_map(my_data.platform, in_snd_device,
                                    &microphone);
    return;
on_error:
    in_snd_device = SND_DEVICE_NONE;
    return;
}


/* process acdb meta info key value */
static void process_acdb_metainfo_key(const XML_Char **attr)
{
    if (strcmp(attr[0], "name") != 0) {
        ALOGE("%s: 'name' not found", __func__);
        goto done;
    }

    if (strcmp(attr[2], "value") != 0) {
        ALOGE("%s: 'value' not found", __func__);
        goto done;
    }

    int key = atoi((char *)attr[3]);
    switch(my_data.caller) {
        case ACDB_EXTN:
                if(acdb_set_metainfo_key(my_data.platform, (char*)attr[1], key) < 0) {
                    ALOGE("%s: key %d was not set!", __func__, key);
                    goto done;
                }
                break;
        case PLATFORM:
                if(platform_set_acdb_metainfo_key(my_data.platform, (char*)attr[1], key) < 0) {
                    ALOGE("%s: key %d was not set!", __func__, key);
                    goto done;
                }
                break;
        default:
                ALOGE("%s: unknown caller!", __func__);
    }

done:
    return;
}

static void process_custom_mtmx_param_in_ch_info(const XML_Char **attr)
{
    uint32_t attr_idx = 0;
    int32_t in_ch_idx = -1;
    struct audio_custom_mtmx_in_params *mtmx_in_params = NULL;

    mtmx_in_params = platform_get_custom_mtmx_in_params((void *)my_data.platform,
                                                  &mtmx_in_params_info);
    if (mtmx_in_params == NULL) {
        ALOGE("%s: mtmx in params with given param info, not found", __func__);
        return;
    }

    if (strcmp(attr[attr_idx++], "in_channel_index") != 0) {
        ALOGE("%s: 'in_channel_index' not found", __func__);
        return;
    }

    in_ch_idx = atoi((char *)attr[attr_idx++]);
    if (in_ch_idx < 0 || in_ch_idx >= MAX_IN_CHANNELS) {
        ALOGE("%s: invalid input channel index(%d)", __func__, in_ch_idx);
        return;
    }

    if (strcmp(attr[attr_idx++], "channel_count") != 0) {
        ALOGE("%s: 'channel_count' not found", __func__);
        return;
    }
    mtmx_in_params->in_ch_info[in_ch_idx].ch_count = atoi((char *)attr[attr_idx++]);

    if (strcmp(attr[attr_idx++], "device") != 0) {
        ALOGE("%s: 'device' not found", __func__);
        return;
    }
    strlcpy(mtmx_in_params->in_ch_info[in_ch_idx].device, attr[attr_idx++],
            sizeof(mtmx_in_params->in_ch_info[in_ch_idx].device));

    if (strcmp(attr[attr_idx++], "interface") != 0) {
        ALOGE("%s: 'interface' not found", __func__);
        return;
    }
    strlcpy(mtmx_in_params->in_ch_info[in_ch_idx].hw_interface, attr[attr_idx++],
            sizeof(mtmx_in_params->in_ch_info[in_ch_idx].hw_interface));

    if (!strncmp(mtmx_in_params->in_ch_info[in_ch_idx].device,
                 ENUM_TO_STRING(AUDIO_DEVICE_IN_BUILTIN_MIC),
                 sizeof(mtmx_in_params->in_ch_info[in_ch_idx].device)))
        mtmx_in_params->mic_ch = mtmx_in_params->in_ch_info[in_ch_idx].ch_count;
    else if (!strncmp(mtmx_in_params->in_ch_info[in_ch_idx].device,
              ENUM_TO_STRING(AUDIO_DEVICE_IN_LOOPBACK),
              sizeof(mtmx_in_params->in_ch_info[in_ch_idx].device)))
        mtmx_in_params->ec_ref_ch = mtmx_in_params->in_ch_info[in_ch_idx].ch_count;

    mtmx_in_params->ip_channels += mtmx_in_params->in_ch_info[in_ch_idx].ch_count;
}

static void process_custom_mtmx_in_params(const XML_Char **attr)
{
    int attr_idx = 0, i = 0;
    char *context = NULL, *value = NULL;

    if (strcmp(attr[attr_idx++], "usecase") != 0) {
        ALOGE("%s: 'usecase' not found", __func__);
        return;
    }
    /* Check if multi usecases are supported for this custom mtrx params */
    value = strtok_r((char *)attr[attr_idx++], ",", &context);
    while (value && (i < CUSTOM_MTRX_PARAMS_MAX_USECASE)) {
        mtmx_in_params_info.usecase_id[i++] = platform_get_usecase_index(value);
        value = strtok_r(NULL, ",", &context);
    }

    if (strcmp(attr[attr_idx++], "out_channel_count") != 0) {
        ALOGE("%s: 'out_channel_count' not found", __func__);
        return;
    }
    mtmx_in_params_info.op_channels = atoi((char *)attr[attr_idx++]);

    platform_add_custom_mtmx_in_params((void *)my_data.platform, &mtmx_in_params_info);

}

static void process_custom_mtmx_param_coeffs(const XML_Char **attr)
{
    uint32_t attr_idx = 0, out_ch_idx = -1, ch_coeff_count = 0;
    uint32_t ip_channels = 0, op_channels = 0, idx = 0;
    char *context = NULL, *ch_coeff_value = NULL;
    struct audio_custom_mtmx_params *mtmx_params = NULL;

    if (strcmp(attr[attr_idx++], "out_channel_index") != 0) {
        ALOGE("%s: 'out_channel_index' not found", __func__);
        return;
    }
    out_ch_idx = atoi((char *)attr[attr_idx++]);

    if (out_ch_idx < 0 || out_ch_idx >= mtmx_params_info.op_channels) {
        ALOGE("%s: invalid out channel index(%d)", __func__, out_ch_idx);
        return;
    }

    if (strcmp(attr[attr_idx++], "values") != 0) {
        ALOGE("%s: 'values' not found", __func__);
        return;
    }
    mtmx_params = platform_get_custom_mtmx_params((void *)my_data.platform,
                                                  &mtmx_params_info, &idx);
    if (mtmx_params == NULL) {
        ALOGE("%s: mtmx params with given param info, not found", __func__);
        return;
    }
    ch_coeff_value = strtok_r((char *)attr[attr_idx++], " ", &context);
    ip_channels = mtmx_params->info.ip_channels;
    op_channels = mtmx_params->info.op_channels;
    while(ch_coeff_value && ch_coeff_count < ip_channels) {
        mtmx_params->coeffs[ip_channels * out_ch_idx + ch_coeff_count++]
                           = atoi(ch_coeff_value);
        ch_coeff_value = strtok_r(NULL, " ", &context);
    }
    if (ch_coeff_count != mtmx_params->info.ip_channels ||
        ch_coeff_value != NULL)
        ALOGE("%s: invalid/malformed coefficient values", __func__);
}

static void process_custom_mtmx_params(const XML_Char **attr)
{
    int attr_idx = 0, i = 0;
    char *context = NULL, *value = NULL;

    memset(&mtmx_params_info, 0, sizeof(mtmx_params_info));

    if (strcmp(attr[attr_idx++], "param_id") != 0) {
        ALOGE("%s: 'param_id' not found", __func__);
        return;
    }
    mtmx_params_info.id = atoi((char *)attr[attr_idx++]);

    if (strcmp(attr[attr_idx++], "in_channel_count") != 0) {
        ALOGE("%s: 'in_channel_count' not found", __func__);
        return;
    }
    mtmx_params_info.ip_channels = atoi((char *)attr[attr_idx++]);

    if (strcmp(attr[attr_idx++], "out_channel_count") != 0) {
        ALOGE("%s: 'out_channel_count' not found", __func__);
        return;
    }
    mtmx_params_info.op_channels = atoi((char *)attr[attr_idx++]);

    if (strcmp(attr[attr_idx++], "usecase") != 0) {
        ALOGE("%s: 'usecase' not found", __func__);
        return;
    }

    /* check if multi usecases are supported for this custom mtrx params */
    value = strtok_r((char *)attr[attr_idx++], ",", &context);
    while (value && (i < CUSTOM_MTRX_PARAMS_MAX_USECASE)) {
        mtmx_params_info.usecase_id[i++] = platform_get_usecase_index(value);
        value = strtok_r(NULL, ",", &context);
    }

    if (strcmp(attr[attr_idx++], "snd_device") != 0) {
        ALOGE("%s: 'snd_device' not found", __func__);
        return;
    }
    mtmx_params_info.snd_device = platform_get_snd_device_index((char *)attr[attr_idx++]);

    if ((attr[attr_idx] != NULL) && (strcmp(attr[attr_idx++], "fe_id") == 0)) {
        i = 0;
        value = strtok_r((char *)attr[attr_idx++], ",", &context);
        while (value && (i < CUSTOM_MTRX_PARAMS_MAX_USECASE)) {
            mtmx_params_info.fe_id[i++] = atoi(value);
            value = strtok_r(NULL, ",", &context);
        }

        attr_idx++;
    }

    platform_add_custom_mtmx_params((void *)my_data.platform, &mtmx_params_info);

}

static void start_tag(void *userdata __unused, const XML_Char *tag_name,
                      const XML_Char **attr)
{
    if (my_data.caller == ACDB_EXTN) {
        if(strcmp(tag_name, "acdb_metainfo_key") == 0) {
            section = ACDB_METAINFO_KEY;
        } else if (strcmp(tag_name, "param") == 0) {
            if ((section != CONFIG_PARAMS) && (section != ACDB_METAINFO_KEY)) {
                ALOGE("param tag only supported with CONFIG_PARAMS section");
                return;
            }

            section_process_fn fn = section_table[section];
            fn(attr);
        }
    } else if(my_data.caller == PLATFORM) {
        if (strcmp(tag_name, "bit_width_configs") == 0) {
            section = BITWIDTH;
        } else if (strcmp(tag_name, "acdb_ids") == 0) {
            section = ACDB;
        } else if (strcmp(tag_name, "module_ids") == 0) {
            section = MODULE;
        } else if (strcmp(tag_name, "pcm_ids") == 0) {
            section = PCM_ID;
        } else if (strcmp(tag_name, "backend_names") == 0) {
            section = BACKEND_NAME;
        } else if (strcmp(tag_name, "config_params") == 0) {
            section = CONFIG_PARAMS;
        } else if (strcmp(tag_name, "operator_specific") == 0) {
            section = OPERATOR_SPECIFIC;
        } else if (strcmp(tag_name, "interface_names") == 0) {
            section = INTERFACE_NAME;
        } else if (strcmp(tag_name, "gain_db_to_level_mapping") == 0) {
            section = GAIN_LEVEL_MAPPING;
        } else if (strcmp(tag_name, "app_types") == 0) {
            section = APP_TYPE;
        } else if(strcmp(tag_name, "acdb_metainfo_key") == 0) {
            section = ACDB_METAINFO_KEY;
        } else if (strcmp(tag_name, "microphone_characteristics") == 0) {
            section = MICROPHONE_CHARACTERISTIC;
        } else if (strcmp(tag_name, "snd_devices") == 0) {
            section = SND_DEVICES;
        } else if (strcmp(tag_name, "device") == 0) {
            if ((section != ACDB) && (section != AEC) && (section != NS) && (section != MMSECNS) &&
                (section != BACKEND_NAME) && (section != BITWIDTH) &&
                (section != INTERFACE_NAME) && (section != OPERATOR_SPECIFIC)) {
                ALOGE("device tag only supported for acdb/backend names/bitwitdh/interface names");
                return;
            }

            /* call into process function for the current section */
            section_process_fn fn = section_table[section];
            fn(attr);
        } else if (strcmp(tag_name, "gain_level_map") == 0) {
            if (section != GAIN_LEVEL_MAPPING) {
                ALOGE("usecase tag only supported with GAIN_LEVEL_MAPPING section");
                return;
            }

            section_process_fn fn = section_table[GAIN_LEVEL_MAPPING];
            fn(attr);
        } else if (strcmp(tag_name, "usecase") == 0) {
            if (section != PCM_ID) {
                ALOGE("usecase tag only supported with PCM_ID section");
                return;
            }

            section_process_fn fn = section_table[PCM_ID];
            fn(attr);
        } else if (strcmp(tag_name, "param") == 0) {
            if ((section != CONFIG_PARAMS) && (section != ACDB_METAINFO_KEY)) {
                ALOGE("param tag only supported with CONFIG_PARAMS section");
                return;
            }

            section_process_fn fn = section_table[section];
            fn(attr);
        } else if (strcmp(tag_name, "aec") == 0) {
            if (section != MODULE) {
                ALOGE("aec tag only supported with MODULE section");
                return;
            }
            section = AEC;
        } else if (strcmp(tag_name, "ns") == 0) {
            if (section != MODULE) {
                ALOGE("ns tag only supported with MODULE section");
                return;
            }
            section = NS;
        } else if (strcmp(tag_name, "mmsecns") == 0) {
            if (section != MODULE) {
                ALOGE("mmsecns tag only supported with MODULE section");
                return;
            }
            section = MMSECNS;
        } else if (strcmp(tag_name, "gain_level_map") == 0) {
            if (section != GAIN_LEVEL_MAPPING) {
                ALOGE("gain_level_map tag only supported with GAIN_LEVEL_MAPPING section");
                return;
            }

            section_process_fn fn = section_table[GAIN_LEVEL_MAPPING];
            fn(attr);
        } else if (!strcmp(tag_name, "app")) {
            if (section != APP_TYPE) {
                ALOGE("app tag only valid in section APP_TYPE");
                return;
            }

            section_process_fn fn = section_table[APP_TYPE];
            fn(attr);
        } else if (strcmp(tag_name, "microphone") == 0) {
            if (section != MICROPHONE_CHARACTERISTIC) {
                ALOGE("microphone tag only supported with MICROPHONE_CHARACTERISTIC section");
                return;
            }
            section_process_fn fn = section_table[MICROPHONE_CHARACTERISTIC];
            fn(attr);
        } else if (strcmp(tag_name, "input_snd_device") == 0) {
            if (section != SND_DEVICES) {
                ALOGE("input_snd_device tag only supported with SND_DEVICES section");
                return;
            }
            section = INPUT_SND_DEVICE;
        } else if (strcmp(tag_name, "input_snd_device_mic_mapping") == 0) {
            if (section != INPUT_SND_DEVICE) {
                ALOGE("input_snd_device_mic_mapping tag only supported with INPUT_SND_DEVICE section");
                return;
            }
            section = INPUT_SND_DEVICE_TO_MIC_MAPPING;
        } else if (strcmp(tag_name, "snd_dev") == 0) {
            if (section != INPUT_SND_DEVICE_TO_MIC_MAPPING) {
                ALOGE("snd_dev tag only supported with INPUT_SND_DEVICE_TO_MIC_MAPPING section");
                return;
            }
            section_process_fn fn = section_table[SND_DEV];
            fn(attr);
        } else if (strcmp(tag_name, "mic_info") == 0) {
            if (section != INPUT_SND_DEVICE_TO_MIC_MAPPING) {
                ALOGE("mic_info tag only supported with INPUT_SND_DEVICE_TO_MIC_MAPPING section");
                return;
            }
            if (in_snd_device == SND_DEVICE_NONE) {
                ALOGE("%s: Error in previous tags, do not process mic info", __func__);
                return;
            }
            section_process_fn fn = section_table[MIC_INFO];
            fn(attr);
        } else if (strcmp(tag_name, "custom_mtmx_params") == 0) {
            if (section != ROOT) {
                ALOGE("custom_mtmx_params tag supported only in ROOT section");
                return;
            }
            section = CUSTOM_MTMX_PARAMS;
            section_process_fn fn = section_table[section];
            fn(attr);
        } else if (strcmp(tag_name, "custom_mtmx_param_coeffs") == 0) {
            if (section != CUSTOM_MTMX_PARAMS) {
                ALOGE("custom_mtmx_param_coeffs tag supported only with CUSTOM_MTMX_PARAMS section");
                return;
            }
            section = CUSTOM_MTMX_PARAM_COEFFS;
            section_process_fn fn = section_table[section];
            fn(attr);
        } else if (strcmp(tag_name, "external_specific_dev") == 0) {
            section = EXTERNAL_DEVICE_SPECIFIC;
        } else if (strcmp(tag_name, "ext_device") == 0) {
            section_process_fn fn = section_table[section];
            fn(attr);
        } else if (strcmp(tag_name, "custom_mtmx_in_params") == 0) {
            if (section != ROOT) {
                ALOGE("custom_mtmx_in_params tag supported only in ROOT section");
                return;
            }
            section = CUSTOM_MTMX_IN_PARAMS;
            section_process_fn fn = section_table[section];
            fn(attr);
        } else if (strcmp(tag_name, "custom_mtmx_param_in_chs") == 0) {
            if (section != CUSTOM_MTMX_IN_PARAMS) {
                ALOGE("custom_mtmx_param_in_chs tag supported only with CUSTOM_MTMX_IN_PARAMS section");
                return;
            }
            section = CUSTOM_MTMX_PARAM_IN_CH_INFO;
        } else if (strcmp(tag_name, "audio_input_source_delay") == 0) {
            section = AUDIO_SOURCE_DELAY;
        } else if (strcmp(tag_name, "audio_source_delay") == 0) {
            section_process_fn fn = section_table[section];
            fn(attr);
        }
    } else {
        if(strcmp(tag_name, "config_params") == 0) {
            section = CONFIG_PARAMS;
        } else if (strcmp(tag_name, "param") == 0) {
            if (section != CONFIG_PARAMS) {
                ALOGE("param tag only supported with CONFIG_PARAMS section");
                return;
            }

            section_process_fn fn = section_table[section];
            fn(attr);
        }
    }
    return;
}

static void end_tag(void *userdata __unused, const XML_Char *tag_name)
{
    if (strcmp(tag_name, "bit_width_configs") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "acdb_ids") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "module_ids") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "aec") == 0) {
        section = MODULE;
    } else if (strcmp(tag_name, "ns") == 0) {
        section = MODULE;
    } else if (strcmp(tag_name, "mmsecns") == 0) {
        section = MODULE;
    } else if (strcmp(tag_name, "pcm_ids") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "backend_names") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "config_params") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "operator_specific") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "interface_names") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "gain_db_to_level_mapping") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "app_types") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "acdb_metainfo_key") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "microphone_characteristics") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "snd_devices") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "external_specific_dev") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "input_snd_device") == 0) {
        section = SND_DEVICES;
    } else if (strcmp(tag_name, "input_snd_device_mic_mapping") == 0) {
        section = INPUT_SND_DEVICE;
    } else if (strcmp(tag_name, "custom_mtmx_params") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "custom_mtmx_param_coeffs") == 0) {
        section = CUSTOM_MTMX_PARAMS;
    } else if (strcmp(tag_name, "custom_mtmx_in_params") == 0) {
        section = ROOT;
    } else if (strcmp(tag_name, "custom_mtmx_param_in_chs") == 0) {
        section = CUSTOM_MTMX_IN_PARAMS;
    }
}

int platform_info_init(const char *filename, void *platform, caller_t caller_type)
{
    XML_Parser      parser;
    FILE            *file;
    int             ret = 0;
    int             bytes_read;
    void            *buf;
    char            platform_info_file_name[MIXER_PATH_MAX_LENGTH]= {0};
    char platform_info_xml_path[VENDOR_CONFIG_FILE_MAX_LENGTH];

    strlcpy(platform_info_xml_path, get_platform_xml_path(),
        sizeof(platform_info_xml_path));
    pthread_mutex_lock(&parser_lock);
    if (filename == NULL)
        strlcpy(platform_info_file_name, platform_info_xml_path,
                MIXER_PATH_MAX_LENGTH);
    else
        strlcpy(platform_info_file_name, filename, MIXER_PATH_MAX_LENGTH);

    ALOGV("%s: platform info file name is %s", __func__,
          platform_info_file_name);

    file = fopen(platform_info_file_name, "r");
    section = ROOT;

    if (!file) {
        ALOGD("%s: Failed to open %s, using defaults.",
            __func__, platform_info_file_name);
        ret = -ENODEV;
        goto done;
    }

    parser = XML_ParserCreate(NULL);
    if (!parser) {
        ALOGE("%s: Failed to create XML parser!", __func__);
        ret = -ENODEV;
        goto err_close_file;
    }

    my_data.caller = caller_type;
    my_data.platform = platform;
    my_data.kvpairs = str_parms_create();

    XML_SetElementHandler(parser, start_tag, end_tag);

    while (1) {
        buf = XML_GetBuffer(parser, BUF_SIZE);
        if (buf == NULL) {
            ALOGE("%s: XML_GetBuffer failed", __func__);
            ret = -ENOMEM;
            goto err_free_parser;
        }

        bytes_read = fread(buf, 1, BUF_SIZE, file);
        if (bytes_read < 0) {
            ALOGE("%s: fread failed, bytes read = %d", __func__, bytes_read);
             ret = bytes_read;
            goto err_free_parser;
        }

        if (XML_ParseBuffer(parser, bytes_read,
                            bytes_read == 0) == XML_STATUS_ERROR) {
            ALOGE("%s: XML_ParseBuffer failed, for %s",
                __func__, platform_info_file_name);
            ret = -EINVAL;
            goto err_free_parser;
        }

        if (bytes_read == 0)
            break;
    }

err_free_parser:
    XML_ParserFree(parser);
err_close_file:
    fclose(file);
done:
    pthread_mutex_unlock(&parser_lock);
    return ret;
}
