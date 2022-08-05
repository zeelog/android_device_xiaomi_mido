/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

#define LOG_TAG "audio_hw_generic_effect"
//#define LOG_NDEBUG 0
#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include <log/log.h>
#include <fcntl.h>
#include <dirent.h>
#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <sys/stat.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <cutils/properties.h>
#include "audio_extn.h"
#include "audio_hw.h"

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_GEF
#include <log_utils.h>
#endif

#ifdef AUDIO_GENERIC_EFFECT_FRAMEWORK_ENABLED

#if LINUX_ENABLED
#define GEF_LIBRARY "libqtigef.so"
#else
#define GEF_LIBRARY "/vendor/lib/libqtigef.so"
#endif

typedef void* (*gef_init_t)(void*);
typedef void (*gef_deinit_t)(void*);
typedef void (*gef_device_config_cb_t)(void*, audio_devices_t,
    audio_channel_mask_t, int, int, int);

typedef struct {
    void* handle;
    void* gef_ptr;
    gef_init_t init;
    gef_deinit_t deinit;
    gef_device_config_cb_t device_config_cb;
} gef_data;

static gef_data gef_hal_handle;

typedef enum {
    ASM = 0,
    ADM
} gef_calibration_type;

typedef enum {
    AUDIO_DEVICE_CAL_TYPE = 0,
    AUDIO_STREAM_CAL_TYPE,
} acdb_device_type;


static acdb_device_type make_acdb_device_type_from_gef_cal_type
                            (gef_calibration_type gef_cal_type)
{
    int acdb_device_type = 0;

    switch (gef_cal_type) {
        case ASM:
            acdb_device_type = AUDIO_STREAM_CAL_TYPE;
            break;
        case ADM:
            acdb_device_type = AUDIO_DEVICE_CAL_TYPE;
            break;
        default:
            acdb_device_type = -1;
            break;
    }

    return ((int)acdb_device_type);
}

void audio_extn_gef_init(struct audio_device *adev)
{
    const char* error = NULL;

    ALOGV("%s: Enter with error", __func__);

    pthread_mutex_init(&adev->cal_lock, (const pthread_mutexattr_t *) NULL);
    memset(&gef_hal_handle, 0, sizeof(gef_data));

    //: check error for dlopen
    gef_hal_handle.handle = dlopen(GEF_LIBRARY, RTLD_LAZY);
    if (gef_hal_handle.handle == NULL) {
        ALOGE("%s: DLOPEN failed for %s with error %s",
            __func__, GEF_LIBRARY, dlerror());
        goto ERROR_RETURN;
    } else {
        ALOGV("%s: DLOPEN successful for %s", __func__, GEF_LIBRARY);

        //call dlerror to clear the error
        dlerror();
        gef_hal_handle.init =
            (gef_init_t)dlsym(gef_hal_handle.handle, "gef_init");
        error = dlerror();

        if(error != NULL) {
            ALOGE("%s: dlsym of %s failed with error %s",
                 __func__, "gef_init", error);
            goto ERROR_RETURN;
        }

        //call dlerror to clear the error
        dlerror();
        gef_hal_handle.deinit =
            (gef_deinit_t)dlsym(gef_hal_handle.handle, "gef_deinit");
        error = dlerror();

        if(error != NULL) {
            ALOGE("%s: dlsym of %s failed with error %s",
                 __func__, "gef_deinit", error);
            goto ERROR_RETURN;
        }

        //call dlerror to clear the error
        error = dlerror();
        gef_hal_handle.device_config_cb =
            (gef_device_config_cb_t)dlsym(gef_hal_handle.handle,
             "gef_device_config_cb");
        error = dlerror();

        if(error != NULL) {
            ALOGE("%s: dlsym of %s failed with error %s",
                 __func__, "gef_device_config_cb", error);
            goto ERROR_RETURN;
        }

        gef_hal_handle.gef_ptr = gef_hal_handle.init((void*)adev);
    }

ERROR_RETURN:
    ALOGV("%s: Exit with error ", __func__);
    return;
}


#ifdef INSTANCE_ID_ENABLED
//this will be called from GEF to exchange calibration using acdb
int audio_extn_gef_send_audio_cal(void* dev, int acdb_dev_id,
    int gef_cal_type, int app_type, int topology_id, int sample_rate,
    uint32_t module_id, uint16_t instance_id, uint32_t param_id,
    void* data, int length, bool persist)
{
    int ret = 0;
    struct audio_device *adev = (struct audio_device*)dev;
    acdb_audio_cal_cfg_t cal;
    int acdb_device_type =
        make_acdb_device_type_from_gef_cal_type(gef_cal_type);

    ALOGV("%s: Enter", __func__);
    memset(&cal, 0, sizeof(acdb_audio_cal_cfg_t));

    //lock adev->cal_lock
    pthread_mutex_lock(&adev->cal_lock);

    //pack cal
    platform_make_cal_cfg(&cal, acdb_dev_id,
        acdb_device_type, app_type, topology_id, sample_rate,
        module_id, instance_id, param_id, true);

    ret = platform_send_audio_cal(adev->platform, &cal, data, length, persist);

    pthread_mutex_unlock(&adev->cal_lock);

    ALOGV("%s: Exit with error %d", __func__, ret);

    return ret;
}

//this will be called from GEF to exchange calibration using acdb
int audio_extn_gef_get_audio_cal(void* dev, int acdb_dev_id,
    int gef_cal_type, int app_type, int topology_id, int sample_rate,
    uint32_t module_id, uint16_t instance_id, uint32_t param_id,
    void* data, int* length, bool persist)
{
    int ret = 0;
    struct audio_device *adev = (struct audio_device*)dev;
    acdb_audio_cal_cfg_t cal;
    int acdb_device_type =
        make_acdb_device_type_from_gef_cal_type(gef_cal_type);

    ALOGV("%s: Enter", __func__);
    memset(&cal, 0, sizeof(acdb_audio_cal_cfg_t));

    //lock adev->cal_lock
    pthread_mutex_lock(&adev->cal_lock);

    //pack cal
    platform_make_cal_cfg(&cal, acdb_dev_id,
        acdb_device_type, app_type, topology_id, sample_rate,
        module_id, instance_id, param_id, false);

    ret = platform_get_audio_cal(adev->platform, &cal, data, length, persist);

    pthread_mutex_unlock(&adev->cal_lock);

    ALOGV("%s: Exit with error %d", __func__, ret);

    return ret;
}

//this will be called from GEF to store into acdb
int audio_extn_gef_store_audio_cal(void* dev, int acdb_dev_id,
    int gef_cal_type, int app_type, int topology_id, int sample_rate,
    uint32_t module_id, uint16_t instance_id,
    uint32_t param_id, void* data, int length)
{
    int ret = 0;
    struct audio_device *adev = (struct audio_device*)dev;
    acdb_audio_cal_cfg_t cal;
    int acdb_device_type =
        make_acdb_device_type_from_gef_cal_type(gef_cal_type);

    ALOGV("%s: Enter", __func__);
    memset(&cal, 0, sizeof(acdb_audio_cal_cfg_t));

    //lock adev->cal_lock
    pthread_mutex_lock(&adev->cal_lock);

    //pack cal
    platform_make_cal_cfg(&cal, acdb_dev_id,
        acdb_device_type, app_type, topology_id, sample_rate,
        module_id, instance_id, param_id, true);

    ret = platform_store_audio_cal(adev->platform, &cal, data, length);

    pthread_mutex_unlock(&adev->cal_lock);

    ALOGV("%s: Exit with error %d", __func__, ret);

    return ret;
}

//this will be called from GEF to retrieve calibration using acdb
int audio_extn_gef_retrieve_audio_cal(void* dev, int acdb_dev_id,
    int gef_cal_type, int app_type, int topology_id, int sample_rate,
    uint32_t module_id, uint16_t instance_id, uint32_t param_id, void* data, int* length)
{
    int ret = 0;
    struct audio_device *adev = (struct audio_device*)dev;
    acdb_audio_cal_cfg_t cal;
    int acdb_device_type =
        make_acdb_device_type_from_gef_cal_type(gef_cal_type);

    ALOGV("%s: Enter", __func__);
    memset(&cal, 0, sizeof(acdb_audio_cal_cfg_t));

    //lock adev->cal_lock
    pthread_mutex_lock(&adev->cal_lock);

    //pack cal
    platform_make_cal_cfg(&cal, acdb_dev_id,
        acdb_device_type, app_type, topology_id, sample_rate,
        module_id, instance_id, param_id, true);

    ret = platform_retrieve_audio_cal(adev->platform, &cal, data, length);

    pthread_mutex_unlock(&adev->cal_lock);

    ALOGV("%s: Exit with error %d", __func__, ret);

    return ret;
}
#else
//this will be called from GEF to exchange calibration using acdb
int audio_extn_gef_send_audio_cal(void* dev, int acdb_dev_id,
    int gef_cal_type, int app_type, int topology_id, int sample_rate,
    uint32_t module_id, uint32_t param_id, void* data, int length,
    bool persist)
{
    int ret = 0;
    struct audio_device *adev = (struct audio_device*)dev;
    acdb_audio_cal_cfg_t cal;
    int acdb_device_type =
        make_acdb_device_type_from_gef_cal_type(gef_cal_type);

    ALOGV("%s: Enter", __func__);
    memset(&cal, 0, sizeof(acdb_audio_cal_cfg_t));

    //lock adev->cal_lock
    pthread_mutex_lock(&adev->cal_lock);

    //pack cal
    platform_make_cal_cfg(&cal, acdb_dev_id,
        acdb_device_type, app_type, topology_id, sample_rate,
        module_id, param_id, true);

    ret = platform_send_audio_cal(adev->platform, &cal, data, length, persist);

    pthread_mutex_unlock(&adev->cal_lock);

    ALOGV("%s: Exit with error %d", __func__, ret);

    return ret;
}

//this will be called from GEF to exchange calibration using acdb
int audio_extn_gef_get_audio_cal(void* dev, int acdb_dev_id,
    int gef_cal_type, int app_type, int topology_id, int sample_rate,
    uint32_t module_id, uint32_t param_id, void* data, int* length,
    bool persist)
{
    int ret = 0;
    struct audio_device *adev = (struct audio_device*)dev;
    acdb_audio_cal_cfg_t cal;
    int acdb_device_type =
        make_acdb_device_type_from_gef_cal_type(gef_cal_type);

    ALOGV("%s: Enter", __func__);
    memset(&cal, 0, sizeof(acdb_audio_cal_cfg_t));

    //lock adev->cal_lock
    pthread_mutex_lock(&adev->cal_lock);

    //pack cal
    platform_make_cal_cfg(&cal, acdb_dev_id,
        acdb_device_type, app_type, topology_id, sample_rate,
        module_id, param_id, false);

    ret = platform_get_audio_cal(adev->platform, &cal, data, length, persist);

    pthread_mutex_unlock(&adev->cal_lock);

    ALOGV("%s: Exit with error %d", __func__, ret);

    return ret;
}

//this will be called from GEF to store into acdb
int audio_extn_gef_store_audio_cal(void* dev, int acdb_dev_id,
    int gef_cal_type, int app_type, int topology_id, int sample_rate,
    uint32_t module_id, uint32_t param_id, void* data, int length)
{
    int ret = 0;
    struct audio_device *adev = (struct audio_device*)dev;
    acdb_audio_cal_cfg_t cal;
    int acdb_device_type =
        make_acdb_device_type_from_gef_cal_type(gef_cal_type);

    ALOGV("%s: Enter", __func__);
    memset(&cal, 0, sizeof(acdb_audio_cal_cfg_t));

    //lock adev->cal_lock
    pthread_mutex_lock(&adev->cal_lock);

    //pack cal
    platform_make_cal_cfg(&cal, acdb_dev_id,
        acdb_device_type, app_type, topology_id, sample_rate,
        module_id, param_id, true);

    ret = platform_store_audio_cal(adev->platform, &cal, data, length);

    pthread_mutex_unlock(&adev->cal_lock);

    ALOGV("%s: Exit with error %d", __func__, ret);

    return ret;
}

//this will be called from GEF to retrieve calibration using acdb
int audio_extn_gef_retrieve_audio_cal(void* dev, int acdb_dev_id,
    int gef_cal_type, int app_type, int topology_id, int sample_rate,
    uint32_t module_id, uint32_t param_id, void* data, int* length)
{
    int ret = 0;
    struct audio_device *adev = (struct audio_device*)dev;
    acdb_audio_cal_cfg_t cal;
    int acdb_device_type =
        make_acdb_device_type_from_gef_cal_type(gef_cal_type);

    ALOGV("%s: Enter", __func__);
    memset(&cal, 0, sizeof(acdb_audio_cal_cfg_t));

    //lock adev->cal_lock
    pthread_mutex_lock(&adev->cal_lock);

    //pack cal
    platform_make_cal_cfg(&cal, acdb_dev_id,
        acdb_device_type, app_type, topology_id, sample_rate,
        module_id, param_id, true);

    ret = platform_retrieve_audio_cal(adev->platform, &cal, data, length);

    pthread_mutex_unlock(&adev->cal_lock);

    ALOGV("%s: Exit with error %d", __func__, ret);

    return ret;
}
#endif

//this will be called from HAL to notify GEF of new device configuration
void audio_extn_gef_notify_device_config(struct listnode *audio_devices,
    audio_channel_mask_t channel_mask, int sample_rate, int acdb_id, int app_type)
{
    ALOGV("%s: Enter", __func__);

    //call into GEF to share channel mask and device info
    if (gef_hal_handle.handle && gef_hal_handle.device_config_cb) {
        gef_hal_handle.device_config_cb(gef_hal_handle.gef_ptr, get_device_types(audio_devices),
            channel_mask, sample_rate, acdb_id, app_type);
    }

    ALOGV("%s: Exit", __func__);

    return;
}

void audio_extn_gef_deinit(struct audio_device *adev)
{
    ALOGV("%s: Enter", __func__);

    if (gef_hal_handle.handle) {
        if (gef_hal_handle.handle && gef_hal_handle.deinit)
            gef_hal_handle.deinit(gef_hal_handle.gef_ptr);
        dlclose(gef_hal_handle.handle);
    }

    pthread_mutex_destroy(&adev->cal_lock);
    memset(&gef_hal_handle, 0, sizeof(gef_data));

    ALOGV("%s: Exit", __func__);
}

#endif
