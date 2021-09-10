/*
 * Copyright (C) 2019 The Android Open Source Project
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

#define LOG_TAG "audio_hw_audiozoom"
/*#define LOG_NDEBUG 0*/

#include <errno.h>
#include <log/log.h>
#include <stdlib.h>
#include <expat.h>
#include <audio_hw.h>
#include <system/audio.h>
#include "audio_extn.h"

#include "audiozoom.h"

#include <resolv.h>

#define AUDIOZOOM_PRESET_FILE "/vendor/etc/audiozoom.xml"

// --- external function dependency ---
fp_platform_set_parameters_t fp_platform_set_parameters;

typedef struct qdsp_audiozoom_cfg {
    uint32_t             topo_id;
    uint32_t             module_id;
    uint32_t             instance_id;
    uint32_t             zoom_param_id;
    uint32_t             wide_param_id;
    uint32_t             dir_param_id;
    uint32_t             app_type;
} qdsp_audiozoom_cfg_t;

static qdsp_audiozoom_cfg_t qdsp_audiozoom;

static void start_tag(void *userdata __unused, const XML_Char *tag_name,
                      const XML_Char **attr)
{
    uint32_t index = 0;

    if (!attr) {
        ALOGE("%s: NULL platform/tag_name/attr", __func__);
        return;
    }

    if (strcmp(tag_name, "topo") == 0) {
        if (strcmp(attr[0], "id") == 0) {
            if (attr[1])
                qdsp_audiozoom.topo_id = atoi(attr[1]);
        }
    } else if (strcmp(tag_name, "module") == 0) {
        if (strcmp(attr[0], "id") == 0) {
            if (attr[1])
                qdsp_audiozoom.module_id = atoi(attr[1]);
        }
    } else if (strcmp(tag_name, "param") == 0) {
        while (attr[index] != NULL) {
            if (strcmp(attr[index], "zoom_id") == 0) {
                index++;
                if (attr[index])
                    qdsp_audiozoom.zoom_param_id = atoi(attr[index]);
                else
                    break;
            } else if (strcmp(attr[index], "wide_id") == 0) {
                index++;
                if (attr[index])
                    qdsp_audiozoom.wide_param_id = atoi(attr[index]);
                else
                    break;
            } else if (strcmp(attr[index], "dir_id") == 0) {
                index++;
                if (attr[index])
                    qdsp_audiozoom.dir_param_id = atoi(attr[index]);
                else
                    break;
            }
            index++;
        }
    } else if (strcmp(tag_name, "app_type") == 0) {
        if (strcmp(attr[0], "id") == 0) {
            if (attr[1])
                qdsp_audiozoom.app_type = atoi(attr[1]);
        }
    } else if (strcmp(tag_name, "instance") == 0) {
        if (strcmp(attr[0], "id") == 0) {
            if (attr[1])
                qdsp_audiozoom.instance_id = atoi(attr[1]);
        }
    } else {
        ALOGE("%s: %s is not a supported tag", __func__, tag_name);
    }

    return;
}

static void end_tag(void *userdata __unused, const XML_Char *tag_name)
{
    if (strcmp(tag_name, "topo") == 0) {
    } else if (strcmp(tag_name, "module") == 0) {
    } else if (strcmp(tag_name, "param") == 0) {
    } else if (strcmp(tag_name, "app_type") == 0) {
    } else if (strcmp(tag_name, "instance") == 0) {
    } else {
        ALOGE("%s: %s is not a supported tag", __func__, tag_name);
    }
}

static int audiozoom_parse_info(const char *filename)
{
    XML_Parser      parser;
    FILE            *file;
    int             ret = 0;
    int             bytes_read;
    void            *buf;
    static const uint32_t kBufSize = 1024;

    file = fopen(filename, "r");
    if (!file) {
        ALOGE("%s: Failed to open %s", __func__, filename);
        ret = -ENODEV;
        goto done;
    }

    parser = XML_ParserCreate(NULL);
    if (!parser) {
        ALOGE("%s: Failed to create XML parser!", __func__);
        ret = -ENODEV;
        goto err_close_file;
    }

    XML_SetElementHandler(parser, start_tag, end_tag);

    while (1) {
        buf = XML_GetBuffer(parser, kBufSize);
        if (buf == NULL) {
            ALOGE("%s: XML_GetBuffer failed", __func__);
            ret = -ENOMEM;
            goto err_free_parser;
        }

        bytes_read = fread(buf, 1, kBufSize, file);
        if (bytes_read < 0) {
            ALOGE("%s: fread failed, bytes read = %d", __func__, bytes_read);
             ret = bytes_read;
            goto err_free_parser;
        }

        if (XML_ParseBuffer(parser, bytes_read,
                            bytes_read == 0) == XML_STATUS_ERROR) {
            ALOGE("%s: XML_ParseBuffer failed, for %s",
                __func__, filename);
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
    return ret;
}

int audiozoom_set_microphone_direction(
    struct stream_in *in, audio_microphone_direction_t dir)
{
    (void)in;
    (void)dir;
    return 0;
}

static int audiozoom_set_microphone_field_dimension_zoom(
    struct stream_in *in, float zoom)
{
    struct audio_device *adev = in->dev;
    struct str_parms *parms = str_parms_create();
    /* The encoding process in b64_ntop represents 24-bit groups of input bits
       as output strings of 4 encoded characters. */
    char data[((sizeof(zoom) + 2) / 3) * 4 + 1] = {0};
    int32_t ret;

    if (zoom > 1.0 || zoom < 0)
        return -EINVAL;

    if (qdsp_audiozoom.topo_id == 0 || qdsp_audiozoom.module_id == 0 ||
        qdsp_audiozoom.zoom_param_id == 0)
        return -ENOSYS;

    str_parms_add_int(parms, "cal_devid", get_device_types(&in->device_list));
    str_parms_add_int(parms, "cal_apptype", in->app_type_cfg.app_type);
    str_parms_add_int(parms, "cal_topoid", qdsp_audiozoom.topo_id);
    str_parms_add_int(parms, "cal_moduleid", qdsp_audiozoom.module_id);
    str_parms_add_int(parms, "cal_instanceid", qdsp_audiozoom.instance_id);
    str_parms_add_int(parms, "cal_paramid", qdsp_audiozoom.zoom_param_id);

    ret = b64_ntop((uint8_t*)&zoom, sizeof(zoom), data, sizeof(data));
    if (ret > 0) {
        str_parms_add_str(parms, "cal_data", data);

        fp_platform_set_parameters(adev->platform, parms);
    } else {
        ALOGE("%s: failed to convert data to string, ret %d", __func__, ret);
    }

    str_parms_destroy(parms);

    return 0;
}

static int audiozoom_set_microphone_field_dimension_wide_angle(
    struct stream_in *in, float zoom)
{
    (void)in;
    (void)zoom;
    return 0;
}

int audiozoom_set_microphone_field_dimension(
    struct stream_in *in, float zoom)
{
    if (zoom > 1.0 || zoom < -1.0)
        return -EINVAL;

    if (zoom >= 0 && zoom <= 1.0)
        return audiozoom_set_microphone_field_dimension_zoom(in, zoom);

    if (zoom >= -1.0 && zoom <= 0)
        return audiozoom_set_microphone_field_dimension_wide_angle(in, zoom);

    return 0;
}

int audiozoom_init(audiozoom_init_config_t init_config)
{
    fp_platform_set_parameters = init_config.fp_platform_set_parameters;
    audiozoom_parse_info(AUDIOZOOM_PRESET_FILE);

    ALOGV("%s: topo_id=%d, module_id=%d, instance_id=%d, zoom__id=%d, dir_id=%d, app_type=%d",
        __func__, qdsp_audiozoom.topo_id, qdsp_audiozoom.module_id, qdsp_audiozoom.instance_id,
        qdsp_audiozoom.zoom_param_id, qdsp_audiozoom.dir_param_id,qdsp_audiozoom.app_type);

    return 0;
}
