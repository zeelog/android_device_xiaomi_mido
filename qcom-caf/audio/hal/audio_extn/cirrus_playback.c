/*
 * Copyright (C) 2017 The Android Open Source Project
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

#define LOG_TAG "audio_hw_cirrus_playback"
/*#define LOG_NDEBUG 0*/

#include <errno.h>
#include <math.h>
#include <log/log.h>
#include <fcntl.h>
#include "../audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <cutils/properties.h>
#include "audio_extn.h"

// - external function dependency -
static fp_platform_get_snd_device_name_t fp_platform_get_snd_device_name;
static fp_platform_get_pcm_device_id_t fp_platform_get_pcm_device_id;
static fp_get_usecase_from_list_t fp_get_usecase_from_list;
static fp_enable_disable_snd_device_t fp_disable_snd_device;
static fp_enable_disable_snd_device_t  fp_enable_snd_device;
static fp_enable_disable_audio_route_t fp_disable_audio_route;
static fp_enable_disable_audio_route_t fp_enable_audio_route;
static fp_audio_extn_get_snd_card_split_t fp_audio_extn_get_snd_card_split;

struct cirrus_playback_session {
    void *adev_handle;
    pthread_mutex_t fb_prot_mutex;
    pthread_t calibration_thread;
#ifdef ENABLE_CIRRUS_DETECTION
    pthread_t failure_detect_thread;
#endif
    struct pcm *pcm_rx;
    struct pcm *pcm_tx;
    volatile int32_t state;
};

enum cirrus_playback_state {
    INIT = 0,
    CALIBRATING = 1,
    IDLE = 2,
    PLAYBACK = 3
};

struct crus_sp_ioctl_header {
    uint32_t size;
    uint32_t module_id;
    uint32_t param_id;
    uint32_t data_length;
    void *data;
};

/* Payload struct for getting calibration result from DSP module */
struct cirrus_cal_result_t {
    int32_t status_l;
    int32_t checksum_l;
    int32_t z_l;
    int32_t status_r;
    int32_t checksum_r;
    int32_t z_r;
};

/* Payload struct for setting the RX and TX use cases */
struct crus_rx_run_case_ctrl_t {
    int32_t value;
    int32_t status_l;
    int32_t checksum_l;
    int32_t z_l;
    int32_t status_r;
    int32_t checksum_r;
    int32_t z_r;
};

#define CRUS_SP_FILE "/dev/msm_cirrus_playback"
#define CRUS_CAL_FILE "/persist/audio/audio.cal"
#define CRUS_TX_CONF_FILE "vendor/firmware/crus_sp_config_%s_tx.bin"
#define CRUS_RX_CONF_FILE "vendor/firmware/crus_sp_config_%s_rx.bin"
#define CONFIG_FILE_SIZE 128

#define CRUS_SP_USECASE_MIXER   "Cirrus SP Usecase"
#define CRUS_SP_LOAD_CONF_MIXER "Cirrus SP Load Config"
#define CRUS_SP_FAIL_DET_MIXER  "Cirrus SP Failure Detection"

#define CIRRUS_SP 0x10027053

#define CRUS_MODULE_ID_TX 0x00000002
#define CRUS_MODULE_ID_RX 0x00000001

#define CRUS_PARAM_RX_SET_USECASE 0x00A1AF02
#define CRUS_PARAM_TX_SET_USECASE 0x00A1BF0A

#define CRUS_PARAM_RX_SET_CALIB 0x00A1AF03
#define CRUS_PARAM_TX_SET_CALIB 0x00A1BF03

#define CRUS_PARAM_RX_SET_EXT_CONFIG 0x00A1AF05
#define CRUS_PARAM_TX_SET_EXT_CONFIG 0x00A1BF08

#define CRUS_PARAM_RX_GET_TEMP 0x00A1AF07
#define CRUS_PARAM_TX_GET_TEMP_CAL 0x00A1BF06
// variables based on CSPL tuning file, max parameter length is 96 integers (384 bytes)
#define CRUS_PARAM_TEMP_MAX_LENGTH 384

#define CRUS_AFE_PARAM_ID_ENABLE 0x00010203

#define FAIL_DETECT_INIT_WAIT_US 500000
#define FAIL_DETECT_LOOP_WAIT_US 300000

#define CRUS_DEFAULT_CAL_L 0x2A11
#define CRUS_DEFAULT_CAL_R 0x29CB

#define CRUS_SP_IOCTL_MAGIC 'a'

#define CRUS_SP_IOCTL_GET _IOWR(CRUS_SP_IOCTL_MAGIC, 219, void *)
#define CRUS_SP_IOCTL_SET _IOWR(CRUS_SP_IOCTL_MAGIC, 220, void *)
#define CRUS_SP_IOCTL_GET_CALIB _IOWR(CRUS_SP_IOCTL_MAGIC, 221, void *)
#define CRUS_SP_IOCTL_SET_CALIB _IOWR(CRUS_SP_IOCTL_MAGIC, 222, void *)


struct pcm_config pcm_config_cirrus_rx = {
    .channels = 8,
    .rate = 48000,
    .period_size = 320,
    .period_count = 4,
    .format = PCM_FORMAT_S32_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

struct pcm_config pcm_config_cirrus_tx = {
    .channels = 2,
    .rate = 48000,
    .period_size = 320,
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

static struct cirrus_playback_session handle;

#ifdef CIRRUS_FACTORY_CALIBRATION
static void *audio_extn_cirrus_calibration_thread();
#else
static void *audio_extn_cirrus_config_thread();
#endif

#ifdef ENABLE_CIRRUS_DETECTION
static void *audio_extn_cirrus_failure_detect_thread();
#endif

void spkr_prot_init(void *adev, spkr_prot_init_config_t spkr_prot_init_config_val) {
    ALOGI("%s: Initialize Cirrus Logic Playback module", __func__);

    memset(&handle, 0, sizeof(handle));
    if (!adev) {
        ALOGE("%s: Invalid params", __func__);
        return;
    }

    handle.adev_handle = adev;
    handle.state = INIT;

    // init function pointers
    fp_platform_get_snd_device_name = spkr_prot_init_config_val.fp_platform_get_snd_device_name;
    fp_platform_get_pcm_device_id = spkr_prot_init_config_val.fp_platform_get_pcm_device_id;
    fp_get_usecase_from_list =  spkr_prot_init_config_val.fp_get_usecase_from_list;
    fp_disable_snd_device = spkr_prot_init_config_val.fp_disable_snd_device;
    fp_enable_snd_device = spkr_prot_init_config_val.fp_enable_snd_device;
    fp_disable_audio_route = spkr_prot_init_config_val.fp_disable_audio_route;
    fp_enable_audio_route = spkr_prot_init_config_val.fp_enable_audio_route;
    fp_audio_extn_get_snd_card_split = spkr_prot_init_config_val.fp_audio_extn_get_snd_card_split;

    pthread_mutex_init(&handle.fb_prot_mutex, NULL);

#ifdef CIRRUS_FACTORY_CALIBRATION
    (void)pthread_create(&handle.calibration_thread,
                (const pthread_attr_t *) NULL,
                audio_extn_cirrus_calibration_thread, &handle);
#else
    (void)pthread_create(&handle.calibration_thread,
                (const pthread_attr_t *) NULL,
                audio_extn_cirrus_config_thread, &handle);
#endif
}

int spkr_prot_deinit() {
    ALOGV("%s: Entry", __func__);

#ifdef ENABLE_CIRRUS_DETECTION
    pthread_join(handle.failure_detect_thread, NULL);
#endif
    pthread_join(handle.calibration_thread, NULL);
    pthread_mutex_destroy(&handle.fb_prot_mutex);

    ALOGV("%s: Exit", __func__);
    return 0;
}

#ifdef CIRRUS_FACTORY_CALIBRATION
static int audio_extn_cirrus_run_calibration() {
    struct audio_device *adev = handle.adev_handle;
    struct crus_sp_ioctl_header header;
    struct cirrus_cal_result_t result;
    struct mixer_ctl *ctl = NULL;
    FILE *cal_file = NULL;
    int ret = 0, dev_file = -1;
    char *buffer = NULL;
    uint32_t option = 1;

    ALOGI("%s: Running speaker calibration", __func__);

    dev_file = open(CRUS_SP_FILE, O_RDWR | O_NONBLOCK);
    if (dev_file < 0) {
        ALOGE("%s: Failed to open Cirrus Playback IOCTL (%d)",
              __func__, dev_file);
        ret = -EINVAL;
        goto exit;
    }

    buffer = calloc(1, CRUS_PARAM_TEMP_MAX_LENGTH);
    if (!buffer) {
        ALOGE("%s: allocate memory failed", __func__);
        ret = -ENOMEM;
        goto exit;
    }

    cal_file = fopen(CRUS_CAL_FILE, "r");
    if (cal_file) {
        ret = fread(&result, sizeof(result), 1, cal_file);
        if (ret != 1) {
            ALOGE("%s: Cirrus SP calibration file cannot be read , read size: %lu file error: %d",
                  __func__, (unsigned long)ret * sizeof(result), ferror(cal_file));
            ret = -EINVAL;
            fclose(cal_file);
            goto exit;
        }

        fclose(cal_file);
    } else {

        ALOGV("%s: Calibrating...", __func__);

        header.size = sizeof(header);
        header.module_id = CRUS_MODULE_ID_RX;
        header.param_id = CRUS_PARAM_RX_SET_CALIB;
        header.data_length = sizeof(option);
        header.data = &option;

        ret = ioctl(dev_file, CRUS_SP_IOCTL_SET, &header);
        if (ret < 0) {
            ALOGE("%s: Cirrus SP calibration IOCTL failure (%d)",
                  __func__, ret);
            ret = -EINVAL;
            goto exit;
        }

        header.size = sizeof(header);
        header.module_id = CRUS_MODULE_ID_TX;
        header.param_id = CRUS_PARAM_TX_SET_CALIB;
        header.data_length = sizeof(option);
        header.data = &option;

        ret = ioctl(dev_file, CRUS_SP_IOCTL_SET, &header);
        if (ret < 0) {
            ALOGE("%s: Cirrus SP calibration IOCTL failure (%d)",
                  __func__, ret);
            ret = -EINVAL;
            goto exit;
        }

        sleep(2);

        header.size = sizeof(header);
        header.module_id = CRUS_MODULE_ID_TX;
        header.param_id = CRUS_PARAM_TX_GET_TEMP_CAL;
        header.data_length = sizeof(result);
        header.data = &result;

        ret = ioctl(dev_file, CRUS_SP_IOCTL_GET, &header);
        if (ret < 0) {
            ALOGE("%s: Cirrus SP calibration IOCTL failure (%d)",
                  __func__, ret);
            ret = -EINVAL;
            goto exit;
        }

        if (result.status_l != 1) {
            ALOGE("%s: Left calibration failure. Please check speakers",
                    __func__);
            ret = -EINVAL;
        }

        if (result.status_r != 1) {
            ALOGE("%s: Right calibration failure. Please check speakers",
                    __func__);
            ret = -EINVAL;
        }

        if (ret < 0)
            goto exit;

        cal_file = fopen(CRUS_CAL_FILE, "wb");
        if (cal_file == NULL) {
            ALOGE("%s: Cannot create Cirrus SP calibration file (%s)",
                  __func__, strerror(errno));
            ret = -EINVAL;
            goto exit;
        }

        ret = fwrite(&result, sizeof(result), 1, cal_file);

        if (ret != 1) {
            ALOGE("%s: Unable to save Cirrus SP calibration data, write size %lu, file error %d",
                  __func__, (unsigned long)ret * sizeof(result), ferror(cal_file));
            fclose(cal_file);
            ret = -EINVAL;
            goto exit;
        }

        fclose(cal_file);

        ALOGI("%s: Cirrus calibration file successfully written",
              __func__);
    }

    header.size = sizeof(header);
    header.module_id = CRUS_MODULE_ID_TX;
    header.param_id = 0;
    header.data_length = sizeof(result);
    header.data = &result;

    ret = ioctl(dev_file, CRUS_SP_IOCTL_SET_CALIB, &header);

    if (ret < 0) {
        ALOGE("%s: Cirrus SP calibration IOCTL failure (%d)", __func__, ret);
        ret = -EINVAL;
        goto exit;
    }

    ctl = mixer_get_ctl_by_name(adev->mixer,
                    CRUS_SP_USECASE_MIXER);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
             __func__, CRUS_SP_USECASE_MIXER);
        ret = -EINVAL;
        goto exit;
    }

    ret = mixer_ctl_set_value(ctl, 0, 0); // Set RX external firmware config
    if (ret < 0) {
        ALOGE("%s: set default usecase failed", __func__);
        goto exit;
    }

    sleep(1);

    header.size = sizeof(header);
    header.module_id = CRUS_MODULE_ID_RX;
    header.param_id = CRUS_PARAM_RX_GET_TEMP;
    header.data_length = CRUS_PARAM_TEMP_MAX_LENGTH;
    header.data = buffer;

    ret = ioctl(dev_file, CRUS_SP_IOCTL_GET, &header);
    if (ret < 0) {
        ALOGE("%s: Cirrus SP temperature IOCTL failure (%d)", __func__, ret);
        ret = -EINVAL;
        goto exit;
    }

    ALOGI("%s: Cirrus SP successfully calibrated", __func__);

exit:
    if (dev_file >= 0)
        close(dev_file);
    free(buffer);
    ALOGV("%s: Exit", __func__);

    return ret;
}

static int audio_extn_cirrus_load_usecase_configs(void) {
    struct audio_device *adev = handle.adev_handle;
    struct mixer_ctl *ctl_uc = NULL, *ctl_config = NULL;
    char *filename = NULL;
    int ret = 0, default_uc = 0;
    struct snd_card_split *snd_split_handle = NULL;

    snd_split_handle = fp_audio_extn_get_snd_card_split();

    ALOGI("%s: Loading usecase tuning configs", __func__);

    ctl_uc = mixer_get_ctl_by_name(adev->mixer, CRUS_SP_USECASE_MIXER);
    ctl_config = mixer_get_ctl_by_name(adev->mixer,
                    CRUS_SP_LOAD_CONF_MIXER);
    if (!ctl_uc || !ctl_config) {
        ALOGE("%s: Could not get ctl for mixer commands", __func__);
        ret = -EINVAL;
        goto exit;
    }

    filename = calloc(1 , CONFIG_FILE_SIZE);
    if (!filename) {
        ALOGE("%s: allocate memory failed", __func__);
        ret = -ENOMEM;
        goto exit;
    }

    default_uc = mixer_ctl_get_value(ctl_uc, 0);

    ret = mixer_ctl_set_value(ctl_uc, 0, default_uc);
    if (ret < 0) {
        ALOGE("%s set uscase %d failed", __func__, default_uc);
        goto exit;
    }

    /* Load TX Tuning Config (if available) */
    snprintf(filename, CONFIG_FILE_SIZE, CRUS_TX_CONF_FILE, snd_split_handle->form_factor);
    if (access(filename, R_OK) == 0) {
        ret = mixer_ctl_set_value(ctl_config, 0, 2);
        if (ret < 0) {
            ALOGE("%s set tx config failed", __func__);
            goto exit;
        }
    } else {
        ALOGE("%s: Tuning file not found (%s)", __func__,
              filename);
        ret = -EINVAL;
        goto exit;
    }
    /* Load RX Tuning Config (if available) */
    snprintf(filename, CONFIG_FILE_SIZE, CRUS_RX_CONF_FILE, snd_split_handle->form_factor);
    if (access(filename, R_OK) == 0) {
        ret = mixer_ctl_set_value(ctl_config, 0, 1);
        if (ret < 0) {
            ALOGE("%s set rx config failed", __func__);
            goto exit;
        }
    } else {
        ALOGE("%s: Tuning file not found (%s)", __func__,
              filename);
        ret = -EINVAL;
        goto exit;
    }

    ALOGI("%s: Cirrus SP loaded available usecase configs", __func__);
exit:
    free(filename);
    ALOGI("%s: Exit", __func__);

    return ret;
}

static void *audio_extn_cirrus_calibration_thread() {
    struct audio_device *adev = handle.adev_handle;
    struct audio_usecase *uc_info_rx = NULL;
    int ret = 0;
    int32_t pcm_dev_rx_id, prev_state;
    uint32_t retries = 5;

    ALOGI("%s: PCM Stream thread", __func__);

    while (!adev->platform && retries) {
        sleep(1);
        ALOGI("%s: Waiting...", __func__);
        retries--;
    }

    prev_state = handle.state;
    handle.state = CALIBRATING;

    uc_info_rx = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));
    if (!uc_info_rx) {
        ALOGE("%s: rx usecase can not be found", __func__);
        goto exit;
    }
    pthread_mutex_lock(&adev->lock);

    uc_info_rx->id = USECASE_AUDIO_PLAYBACK_DEEP_BUFFER;
    uc_info_rx->type = PCM_PLAYBACK;
    uc_info_rx->in_snd_device = SND_DEVICE_NONE;
    uc_info_rx->stream.out = adev->primary_output;
    uc_info_rx->out_snd_device = SND_DEVICE_OUT_SPEAKER;
    list_init(&uc_info_rx->device_list);
    list_add_tail(&adev->usecase_list, &uc_info_rx->list);

    fp_enable_snd_device(adev, SND_DEVICE_OUT_SPEAKER);
    fp_enable_audio_route(adev, uc_info_rx);
    pcm_dev_rx_id = fp_platform_get_pcm_device_id(uc_info_rx->id, PCM_PLAYBACK);

    if (pcm_dev_rx_id < 0) {
        ALOGE("%s: Invalid pcm device for usecase (%d)",
              __func__, uc_info_rx->id);
        pthread_mutex_unlock(&adev->lock);
        goto exit;
    }

    handle.pcm_rx = pcm_open(adev->snd_card, pcm_dev_rx_id,
                PCM_OUT, &pcm_config_cirrus_rx);

    if (handle.pcm_rx && !pcm_is_ready(handle.pcm_rx)) {
        ALOGE("%s: PCM device not ready: %s", __func__,
              pcm_get_error(handle.pcm_rx));
        pthread_mutex_unlock(&adev->lock);
        goto close_stream;
    }

    if (pcm_start(handle.pcm_rx) < 0) {
        ALOGE("%s: pcm start for RX failed; error = %s", __func__,
              pcm_get_error(handle.pcm_rx));
        pthread_mutex_unlock(&adev->lock);
        goto close_stream;
    }
    pthread_mutex_unlock(&adev->lock);
    ALOGI("%s: PCM thread streaming", __func__);

    ret = audio_extn_cirrus_run_calibration();
    ALOGE_IF(ret < 0, "%s: Calibration procedure failed (%d)", __func__, ret);

    ret = audio_extn_cirrus_load_usecase_configs();
    ALOGE_IF(ret < 0, "%s: Set tuning configs failed (%d)", __func__, ret);

close_stream:
    pthread_mutex_lock(&adev->lock);
    if (handle.pcm_rx) {
        ALOGI("%s: pcm_rx_close", __func__);
        pcm_close(handle.pcm_rx);
        handle.pcm_rx = NULL;
    }

    fp_disable_audio_route(adev, uc_info_rx);
    fp_disable_snd_device(adev, SND_DEVICE_OUT_SPEAKER);
    list_remove(&uc_info_rx->list);
    free(uc_info_rx);
    pthread_mutex_unlock(&adev->lock);
exit:
    handle.state = (prev_state == PLAYBACK) ? PLAYBACK : IDLE;

#ifdef ENABLE_CIRRUS_DETECTION
    if (handle.state == PLAYBACK)
        (void)pthread_create(&handle.failure_detect_thread,
                    (const pthread_attr_t *) NULL,
                    audio_extn_cirrus_failure_detect_thread,
                    &handle);
#endif

    ALOGV("%s: Exit", __func__);

    pthread_exit(0);
    return NULL;
}

#else
static void *audio_extn_cirrus_config_thread(void) {
    struct audio_device *adev = handle.adev_handle;
    struct crus_sp_ioctl_header header;
    struct cirrus_cal_result_t result;
    struct mixer_ctl *ctl_config = NULL;
    FILE *cal_file = NULL;
    int ret = 0, dev_file = -1;

    ALOGI("%s: ++", __func__);

    memset(&result, 0, sizeof(result));

    dev_file = open(CRUS_SP_FILE, O_RDWR | O_NONBLOCK);
    if (dev_file < 0) {
        ALOGE("%s: Failed to open Cirrus Playback IOCTL (%d)",
              __func__, dev_file);
        ret = -EINVAL;
        goto exit;
    }

    cal_file = fopen(CRUS_CAL_FILE, "r");
    if (cal_file) {
        ret = fread(&result, sizeof(result), 1, cal_file);

        if (ret != 1) {
            ALOGE("%s: Cirrus SP calibration file cannot be read , read size: %lu file error: %d",
                 __func__, (unsigned long)ret * sizeof(result), ferror(cal_file));
            ret = -EINVAL;
            goto exit;
        }
    }

    header.size = sizeof(header);
    header.module_id = CRUS_MODULE_ID_TX;
    header.param_id = 0;
    header.data_length = sizeof(result);
    header.data = &result;

    ret = ioctl(dev_file, CRUS_SP_IOCTL_SET_CALIB, &header);

    if (ret < 0) {
        ALOGE("%s: Cirrus SP calibration IOCTL failure", __func__);
        goto exit;
    }

    ctl_config = mixer_get_ctl_by_name(adev->mixer,
                       CRUS_SP_LOAD_CONF_MIXER);
    if (!ctl_config) {
        ALOGE("%s: Could not get ctl for mixer commands", __func__);
        ret = -EINVAL;
        goto exit;
    }

    ret = mixer_ctl_set_value(ctl_config, 0, 2);
    if (ret < 0) {
        ALOGE("%s load tx config failed", __func__);
        goto exit;
    }

    ret = mixer_ctl_set_value(ctl_config, 0, 1);
    if (ret < 0) {
        ALOGE("%s load rx config failed", __func__);
        goto exit;
    }

    ret = mixer_ctl_set_value(ctl_config, 0, 0);
    if (ret < 0) {
        ALOGE("%s set idle state failed", __func__);
        goto exit;
    }

exit:
    if (dev_file >= 0)
        close(dev_file);
    if (cal_file)
        fclose(cal_file);

    ALOGI("%s: ret: %d --", __func__, ret);
    return NULL;
}
#endif

#ifdef ENABLE_CIRRUS_DETECTION
void *audio_extn_cirrus_failure_detect_thread() {
    struct audio_device *adev = handle.adev_handle;
    struct crus_sp_ioctl_header header;
    struct mixer_ctl *ctl = NULL;
    const int32_t r_scale_factor = 100000000;
    const int32_t t_scale_factor = 100000;
    const int32_t r_err_range = 70000000;
    const int32_t t_err_range = 210000;
    const int32_t amp_factor = 71498;
    const int32_t material = 250;
    int32_t *buffer = NULL;
    int ret = 0, dev_file = -1, out_cal0 = 0, out_cal1 = 0;
    int rL = 0, rR = 0, zL = 0, zR = 0, tL = 0, tR = 0;
    int rdL = 0, rdR = 0, tdL = 0, tdR = 0, ambL = 0, ambR = 0;
    bool left_cal_done = false, right_cal_done = false;
    bool det_en = false;

    ALOGI("%s: Entry", __func__);

    ctl = mixer_get_ctl_by_name(adev->mixer, CRUS_SP_FAIL_DET_MIXER);
    det_en = mixer_ctl_get_value(ctl, 0);

    if (!det_en)
        goto exit;

    dev_file = open(CRUS_SP_FILE, O_RDWR | O_NONBLOCK);
    if (dev_file < 0) {
        ALOGE("%s: Failed to open Cirrus Playback IOCTL (%d)",
                __func__, dev_file);
        goto exit;
    }

    buffer = calloc(1, CRUS_PARAM_TEMP_MAX_LENGTH);
    if (!buffer) {
        ALOGE("%s: allocate memory failed", __func__);
        goto exit;
    }

    header.size = sizeof(header);
    header.module_id = CRUS_MODULE_ID_RX;
    header.param_id = CRUS_PARAM_RX_GET_TEMP;
    header.data_length = CRUS_PARAM_TEMP_MAX_LENGTH;
    header.data = buffer;

    usleep(FAIL_DETECT_INIT_WAIT_US);

    pthread_mutex_lock(&handle.fb_prot_mutex);
    ret = ioctl(dev_file, CRUS_SP_IOCTL_GET, &header);
    pthread_mutex_unlock(&handle.fb_prot_mutex);
    if (ret < 0) {
        ALOGE("%s: Cirrus SP IOCTL failure (%d)",
               __func__, ret);
        goto exit;
    }

    zL = buffer[2] * amp_factor;
    zR = buffer[4] * amp_factor;

    ambL = buffer[10];
    ambR = buffer[6];

    out_cal0 = buffer[12];
    out_cal1 = buffer[13];

    left_cal_done = (out_cal0 == 2) && (out_cal1 == 2) &&
                    (buffer[2] != CRUS_DEFAULT_CAL_L);

    out_cal0 = buffer[14];
    out_cal1 = buffer[15];

    right_cal_done = (out_cal0 == 2) && (out_cal1 == 2) &&
                     (buffer[4] != CRUS_DEFAULT_CAL_R);

    if (left_cal_done) {
        ALOGI("%s: L Speaker Impedance: %d.%08d ohms", __func__,
              zL / r_scale_factor, abs(zL) % r_scale_factor);
        ALOGI("%s: L Calibration Temperature: %d C", __func__, ambL);
    } else
        ALOGE("%s: Left speaker uncalibrated", __func__);

    if (right_cal_done) {
        ALOGI("%s: R Speaker Impedance: %d.%08d ohms", __func__,
               zR / r_scale_factor, abs(zR) % r_scale_factor);
        ALOGI("%s: R Calibration Temperature: %d C", __func__, ambR);
    } else
        ALOGE("%s: Right speaker uncalibrated", __func__);

    if (!left_cal_done && !right_cal_done)
        goto exit;

    ALOGI("%s: Monitoring speaker impedance & temperature...", __func__);

    while ((handle.state == PLAYBACK) && det_en) {
        pthread_mutex_lock(&handle.fb_prot_mutex);
        ret = ioctl(dev_file, CRUS_SP_IOCTL_GET, &header);
        pthread_mutex_unlock(&handle.fb_prot_mutex);
        if (ret < 0) {
            ALOGE("%s: Cirrus SP IOCTL failure (%d)",
                  __func__, ret);
            goto loop;
        }

        rL = buffer[3];
        rR = buffer[1];

        zL = buffer[2];
        zR = buffer[4];

        if ((zL == 0) || (zR == 0))
            goto loop;

        tdL = (material * t_scale_factor * (rL-zL) / zL);
        tdR = (material * t_scale_factor * (rR-zR) / zR);

        rL *= amp_factor;
        rR *= amp_factor;

        zL *= amp_factor;
        zR *= amp_factor;

        tL = tdL + (ambL * t_scale_factor);
        tR = tdR + (ambR * t_scale_factor);

        rdL = abs(zL - rL);
        rdR = abs(zR - rR);

        if (left_cal_done && (rL != 0) && (rdL > r_err_range))
            ALOGI("%s: Left speaker impedance out of range (%d.%08d ohms)",
                  __func__, rL / r_scale_factor,
                  abs(rL % r_scale_factor));

        if (right_cal_done && (rR != 0) && (rdR > r_err_range))
            ALOGI("%s: Right speaker impedance out of range (%d.%08d ohms)",
                  __func__, rR / r_scale_factor,
                  abs(rR % r_scale_factor));

        if (left_cal_done && (rL != 0) && (tdL > t_err_range))
            ALOGI("%s: Left speaker temperature out of range (%d.%05d C)",
                  __func__, tL / t_scale_factor,
                  abs(tL % t_scale_factor));

        if (right_cal_done && (rR != 0) && (tdR > t_err_range))
            ALOGI("%s: Right speaker temperature out of range (%d.%05d C)",
                  __func__, tR / t_scale_factor,
                  abs(tR % t_scale_factor));

loop:
        det_en = mixer_ctl_get_value(ctl, 0);
        usleep(FAIL_DETECT_LOOP_WAIT_US);
    }

exit:
    if (dev_file >= 0)
        close(dev_file);
    free(buffer);
    ALOGI("%s: Exit ", __func__);

    pthread_exit(0);
    return NULL;
}
#endif

int spkr_prot_start_processing(snd_device_t snd_device) {
    struct audio_usecase *uc_info_tx;
    struct audio_device *adev = handle.adev_handle;
    int32_t pcm_dev_tx_id = -1, ret = 0;

    ALOGV("%s: Entry", __func__);

    if (!adev) {
        ALOGE("%s: Invalid params", __func__);
        return -EINVAL;
    }

    uc_info_tx = (struct audio_usecase *)calloc(1, sizeof(*uc_info_tx));
    if (!uc_info_tx) {
        ALOGE("%s: allocate memory failed", __func__);
        return -ENOMEM;
    }

    audio_route_apply_and_update_path(adev->audio_route,
                                      fp_platform_get_snd_device_name(snd_device));

    pthread_mutex_lock(&handle.fb_prot_mutex);
    uc_info_tx->id = USECASE_AUDIO_SPKR_CALIB_TX;
    uc_info_tx->type = PCM_CAPTURE;
    uc_info_tx->in_snd_device = SND_DEVICE_IN_CAPTURE_VI_FEEDBACK;
    uc_info_tx->out_snd_device = SND_DEVICE_NONE;
    list_init(&uc_info_tx->device_list);
    handle.pcm_tx = NULL;

    list_add_tail(&adev->usecase_list, &uc_info_tx->list);

    fp_enable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
    fp_enable_audio_route(adev, uc_info_tx);

    pcm_dev_tx_id = fp_platform_get_pcm_device_id(uc_info_tx->id, PCM_CAPTURE);

    if (pcm_dev_tx_id < 0) {
        ALOGE("%s: Invalid pcm device for usecase (%d)",
              __func__, uc_info_tx->id);
        ret = -ENODEV;
        goto exit;
    }

    handle.pcm_tx = pcm_open(adev->snd_card,
                             pcm_dev_tx_id,
                             PCM_IN, &pcm_config_cirrus_tx);

    if (handle.pcm_tx && !pcm_is_ready(handle.pcm_tx)) {
        ALOGE("%s: PCM device not ready: %s", __func__, pcm_get_error(handle.pcm_tx));
        ret = -EIO;
        goto exit;
    }

    if (pcm_start(handle.pcm_tx) < 0) {
        ALOGE("%s: pcm start for TX failed; error = %s", __func__,
              pcm_get_error(handle.pcm_tx));
        ret = -EINVAL;
        goto exit;
    }

#ifdef ENABLE_CIRRUS_DETECTION
    if (handle.state == IDLE)
        (void)pthread_create(&handle.failure_detect_thread,
                    (const pthread_attr_t *) NULL,
                    audio_extn_cirrus_failure_detect_thread,
                    &handle);
#endif

    handle.state = PLAYBACK;
exit:
    if (ret) {
        handle.state = IDLE;
        if (handle.pcm_tx) {
            ALOGI("%s: pcm_tx_close", __func__);
            pcm_close(handle.pcm_tx);
            handle.pcm_tx = NULL;
        }

        fp_disable_audio_route(adev, uc_info_tx);
        fp_disable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
        list_remove(&uc_info_tx->list);
        free(uc_info_tx);
    }

    pthread_mutex_unlock(&handle.fb_prot_mutex);
    ALOGV("%s: Exit", __func__);
    return ret;
}

void spkr_prot_stop_processing(snd_device_t snd_device) {
    struct audio_usecase *uc_info_tx;
    struct audio_device *adev = handle.adev_handle;

    ALOGV("%s: Entry", __func__);

    pthread_mutex_lock(&handle.fb_prot_mutex);

    handle.state = IDLE;
    uc_info_tx = fp_get_usecase_from_list(adev, USECASE_AUDIO_SPKR_CALIB_TX);

    if (uc_info_tx) {
        if (handle.pcm_tx) {
            ALOGI("%s: pcm_tx_close", __func__);
            pcm_close(handle.pcm_tx);
            handle.pcm_tx = NULL;
        }

        fp_disable_audio_route(adev, uc_info_tx);
        fp_disable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
        list_remove(&uc_info_tx->list);
        free(uc_info_tx);

        audio_route_reset_path(adev->audio_route,
                               fp_platform_get_snd_device_name(snd_device));
    }

    pthread_mutex_unlock(&handle.fb_prot_mutex);

    ALOGV("%s: Exit", __func__);
}

bool spkr_prot_is_enabled() {
    return true;
}

int get_spkr_prot_snd_device(snd_device_t snd_device) {
    switch(snd_device) {
    case SND_DEVICE_OUT_SPEAKER:
    case SND_DEVICE_OUT_SPEAKER_REVERSE:
        return SND_DEVICE_OUT_SPEAKER_PROTECTED;
    case SND_DEVICE_OUT_SPEAKER_SAFE:
        return SND_DEVICE_OUT_SPEAKER_SAFE;
    case SND_DEVICE_OUT_VOICE_SPEAKER:
        return SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED;
    default:
        return snd_device;
    }
}

void spkr_prot_calib_cancel(__unused void *adev) {
    // FIXME: wait or cancel audio_extn_cirrus_run_calibration
}
