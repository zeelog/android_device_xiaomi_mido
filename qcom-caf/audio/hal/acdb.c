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

#define LOG_TAG "audio_hw_acdb"
//#define LOG_NDEBUG 0
#define LOG_NDDEBUG 0

#include <stdlib.h>
#include <dlfcn.h>
#include <log/log.h>
#include <cutils/list.h>
#include <time.h>
#include "acdb.h"
#include "platform_api.h"
#include "audio_extn.h"
#include <platform.h>

#ifdef INSTANCE_ID_ENABLED
int check_and_set_instance_id_support(struct mixer* mixer, bool acdb_support)
{
    const char *mixer_ctl_name = "Instance ID Support";
    struct mixer_ctl* ctl;

    ALOGV("%s", __func__);

    /* Check for ACDB and property instance ID support and issue mixer control */
    ctl = mixer_get_ctl_by_name(mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
                               __func__, mixer_ctl_name);
        return -EINVAL;
    }

    ALOGD("%s: Final Instance ID support:%d\n", __func__, acdb_support);
    if (mixer_ctl_set_value(ctl, 0, acdb_support) < 0) {
        ALOGE("%s: Could not set Instance ID support %d", __func__,
              acdb_support);
        return -EINVAL;
    }
    return 0;
}
#else
#define check_and_set_instance_id_support(x, y) -ENOSYS
#endif

void get_platform_file_for_device(struct mixer *mixer, char* platform_info_file)
{
    const char *snd_card_name = NULL;

    if (mixer != NULL) {
        /* Get Sound card name */
        snd_card_name = mixer_get_name(mixer);
        if (!snd_card_name) {
            ALOGE("failed to allocate memory for snd_card_name");
            return;
        }
        /* Get platform info file for target */
        audio_extn_utils_get_platform_info(snd_card_name, platform_info_file);
    }
}

int acdb_init(int snd_card_num)
{

    int result = -1;
    struct mixer *mixer = NULL;

    if(snd_card_num < 0) {
        ALOGE("invalid sound card number");
        return result;
    }

    mixer = mixer_open(snd_card_num);
    if (!mixer) {
        ALOGE("%s: Unable to open the mixer card: %d", __func__,
               snd_card_num);
        return result;
    }
    result = acdb_init_v2(mixer);
    mixer_close(mixer);
    return result;
}

int acdb_init_v2(struct mixer *mixer)
{

    int result = -1;
    char *cvd_version = NULL;
    char vendor_config_path[VENDOR_CONFIG_PATH_MAX_LENGTH];
    char platform_info_file[VENDOR_CONFIG_FILE_MAX_LENGTH];
    const char *snd_card_name = NULL;
    struct acdb_platform_data *my_data = NULL;

    if (!mixer) {
       ALOGE("Invalid mixer handle");
       return result;
    }

    my_data = calloc(1, sizeof(struct acdb_platform_data));
    if (!my_data) {
        ALOGE("failed to allocate acdb platform data");
        goto cleanup;
    }

    list_init(&my_data->acdb_meta_key_list);
    audio_get_vendor_config_path(vendor_config_path, sizeof(vendor_config_path));
    /* Get path for platorm_info_xml_path_name in vendor */
    snprintf(platform_info_file, sizeof(platform_info_file),
            "%s/%s", vendor_config_path, PLATFORM_INFO_XML_PATH_NAME);
    get_platform_file_for_device(mixer, platform_info_file);
    /* Extract META KEY LIST INFO */
    platform_info_init(platform_info_file, my_data, ACDB_EXTN);

    my_data->acdb_handle = dlopen(LIB_ACDB_LOADER, RTLD_NOW);
    if (my_data->acdb_handle == NULL) {
        ALOGE("%s: DLOPEN failed for %s", __func__, LIB_ACDB_LOADER);
        goto cleanup;
    }

    ALOGV("%s: DLOPEN successful for %s", __func__, LIB_ACDB_LOADER);

    my_data->acdb_init_v4 = (acdb_init_v4_t)dlsym(my_data->acdb_handle,
                                                     "acdb_loader_init_v4");
    if (my_data->acdb_init_v4 == NULL)
        ALOGE("%s: dlsym error %s for acdb_loader_init_v4", __func__, dlerror());

    my_data->acdb_init_v3 = (acdb_init_v3_t)dlsym(my_data->acdb_handle,
                                                     "acdb_loader_init_v3");
    if (my_data->acdb_init_v3 == NULL)
        ALOGE("%s: dlsym error %s for acdb_loader_init_v3", __func__, dlerror());

    my_data->acdb_init_v2 = (acdb_init_v2_t)dlsym(my_data->acdb_handle,
                                                     "acdb_loader_init_v2");
    if (my_data->acdb_init_v2 == NULL)
        ALOGE("%s: dlsym error %s for acdb_loader_init_v2", __func__, dlerror());

    my_data->acdb_init = (acdb_init_t)dlsym(my_data->acdb_handle,
                                                 "acdb_loader_init_ACDB");
    if (my_data->acdb_init == NULL && my_data->acdb_init_v2 == NULL
        && my_data->acdb_init_v3 == NULL && my_data->acdb_init_v4 == NULL) {
        ALOGE("%s: dlsym error %s for acdb_loader_init_ACDB", __func__, dlerror());
        goto cleanup;
    }

    /* Get CVD version */
    cvd_version = calloc(1, MAX_CVD_VERSION_STRING_SIZE);
    if (!cvd_version) {
        ALOGE("%s: Failed to allocate cvd version", __func__);
        goto cleanup;
    } else {
        struct mixer_ctl *ctl = NULL;
        int count = 0;

        ctl = mixer_get_ctl_by_name(mixer, CVD_VERSION_MIXER_CTL);
        if (!ctl) {
            ALOGE("%s: Could not get ctl for mixer cmd - %s",  __func__, CVD_VERSION_MIXER_CTL);
            goto cleanup;
        }
        mixer_ctl_update(ctl);

        count = mixer_ctl_get_num_values(ctl);
        if (count > MAX_CVD_VERSION_STRING_SIZE)
            count = MAX_CVD_VERSION_STRING_SIZE;

        result = mixer_ctl_get_array(ctl, cvd_version, count);
        if (result != 0) {
            ALOGE("%s: ERROR! mixer_ctl_get_array() failed to get CVD Version", __func__);
            goto cleanup;
        }
    }

    /* Get Sound card name */
    snd_card_name = mixer_get_name(mixer);
    snd_card_name = platform_get_snd_card_name_for_acdb_loader(snd_card_name);
    if (!snd_card_name) {
        ALOGE("failed to get snd_card_name");
        result = -1;
        goto cleanup;
    }

    int key = 0;
    struct listnode *node = NULL;
    struct meta_key_list *key_info = NULL;
    static bool acdb_instance_id_support = false;

    my_data->acdb_init_data.cvd_version = cvd_version;
    my_data->acdb_init_data.snd_card_name = strdup(snd_card_name);
    my_data->acdb_init_data.meta_key_list = &my_data->acdb_meta_key_list;
    my_data->acdb_init_data.is_instance_id_supported = &acdb_instance_id_support;

    if (my_data->acdb_init_v4) {
        result = my_data->acdb_init_v4(&my_data->acdb_init_data, ACDB_LOADER_INIT_V4);
    } else if (my_data->acdb_init_v3) {
        result = my_data->acdb_init_v3(snd_card_name, cvd_version,
                                       &my_data->acdb_meta_key_list);
    } else if (my_data->acdb_init_v2) {
        node = list_head(&my_data->acdb_meta_key_list);
        key_info = node_to_item(node, struct meta_key_list, list);
        key = key_info->cal_info.nKey;
        result = my_data->acdb_init_v2(snd_card_name, cvd_version, key);
    } else {
        result = my_data->acdb_init();
    }
    ALOGD("%s: ACDB Instance ID support after ACDB init:%d\n",
          __func__, acdb_instance_id_support);
    check_and_set_instance_id_support(mixer, acdb_instance_id_support);

cleanup:
    if (NULL != my_data) {
        if (my_data->acdb_handle)
            dlclose(my_data->acdb_handle);

        struct listnode *node = NULL;
        struct meta_key_list *key_info = NULL;
        struct listnode *tempnode = NULL;
        list_for_each_safe(node, tempnode, &my_data->acdb_meta_key_list) {
            key_info = node_to_item(node, struct meta_key_list, list);
            list_remove(node);
            free(key_info);
        }
        free(my_data);
    }

    if (cvd_version)
        free(cvd_version);

    return result;
}

int acdb_set_metainfo_key(void *platform, char *name, int key) {

    struct meta_key_list *key_info = (struct meta_key_list *)
                                        calloc(1, sizeof(struct meta_key_list));
    struct acdb_platform_data *pdata = (struct acdb_platform_data *)platform;
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
