/*
 * Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
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

#ifndef ACDB_H
#define ACDB_H

#include <stdbool.h>
#include <linux/msm_audio_calibration.h>

#define MAX_CVD_VERSION_STRING_SIZE 100
#define LIB_ACDB_LOADER "libacdbloader.so"
#define CVD_VERSION_MIXER_CTL "CVD Version"
#define ACDB_METAINFO_KEY_MODULE_NAME_LEN 100


#define PLATFORM_INFO_XML_PATH_NAME "audio_platform_info.xml"


enum {
        ACDB_LOADER_INIT_V1 = 1,
        ACDB_LOADER_INIT_V2,
        ACDB_LOADER_INIT_V3,
        ACDB_LOADER_INIT_V4,
};

struct mixer;
/* Audio calibration related functions */
typedef void (*acdb_deallocate_t)(void);
typedef int  (*acdb_init_t)();
typedef int  (*acdb_init_v2_t)(const char *, char *, int);
typedef int  (*acdb_init_v3_t)(const char *, char *, struct listnode *);
typedef int  (*acdb_init_v4_t)(void *, int);
typedef void (*acdb_send_audio_cal_t)(int, int, int , int);
typedef void (*acdb_send_audio_cal_v3_t)(int, int, int, int, int);
typedef void (*acdb_send_audio_cal_v4_t)(int, int, int, int, int, int);
typedef void (*acdb_send_voice_cal_t)(int, int);
typedef int (*acdb_reload_vocvoltable_t)(int);
typedef int  (*acdb_get_default_app_type_t)(void);
typedef int (*acdb_loader_get_calibration_t)(char *attr, int size, void *data);
typedef int (*acdb_set_audio_cal_t) (void *, void *, uint32_t);
typedef int (*acdb_get_audio_cal_t) (void *, void *, uint32_t*);
typedef int (*acdb_send_common_top_t) (void);
typedef int (*acdb_set_codec_data_t) (void *, char *);
typedef int (*acdb_reload_t) (char *, char *, char *, int);
typedef int (*acdb_reload_v2_t) (char *, char *, char *, struct listnode *);
typedef int (*acdb_send_gain_dep_cal_t)(int, int, int, int, int);

struct meta_key_list {
    struct listnode list;
    struct audio_cal_info_metainfo cal_info;
    char name[ACDB_METAINFO_KEY_MODULE_NAME_LEN];
};

struct acdb_init_data_v4 {
        char                   *cvd_version;
        char                   *snd_card_name;
        struct listnode        *meta_key_list;
        bool                   *is_instance_id_supported;
};

struct acdb_platform_data {
    /* Audio calibration related functions */
    void                       *acdb_handle;
    acdb_init_t                acdb_init;
    acdb_init_v2_t             acdb_init_v2;
    acdb_init_v3_t             acdb_init_v3;
    acdb_init_v4_t             acdb_init_v4;
    struct listnode acdb_meta_key_list;
    struct acdb_init_data_v4   acdb_init_data;
};

int acdb_init(int);
int acdb_init_v2(struct mixer *);

int acdb_set_metainfo_key(void *platform, char *name, int key);
#endif //ACDB_H
