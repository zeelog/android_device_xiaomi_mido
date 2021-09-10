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

#define LOG_TAG "audio_hw_fm"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include <log/log.h>
#include <unistd.h>

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <stdlib.h>
#include <cutils/str_parms.h>
#include <audio_extn.h>

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_FM
#include <log_utils.h>
#endif

#define AUDIO_PARAMETER_KEY_HANDLE_FM "handle_fm"
#define AUDIO_PARAMETER_KEY_FM_VOLUME "fm_volume"
#define AUDIO_PARAMETER_KEY_REC_PLAY_CONC "rec_play_conc_on"
#define AUDIO_PARAMETER_KEY_FM_MUTE "fm_mute"
#define AUDIO_PARAMETER_KEY_FM_RESTORE_VOLUME "fm_restore_volume"
#define AUDIO_PARAMETER_KEY_FM_ROUTING "fm_routing"
#define AUDIO_PARAMETER_KEY_FM_STATUS "fm_status"
#define FM_LOOPBACK_DRAIN_TIME_MS 2

static struct pcm_config pcm_config_fm = {
    .channels = 2,
    .rate = 48000,
    .period_size = 256,
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

struct fm_module {
    struct pcm *fm_pcm_rx;
    struct pcm *fm_pcm_tx;
    bool is_fm_running;
    bool is_fm_muted;
    float fm_volume;
    bool restart_fm;
    audio_devices_t fm_device;
    card_status_t card_status;
};

static struct fm_module fmmod = {
  .fm_pcm_rx = NULL,
  .fm_pcm_tx = NULL,
  .fm_volume = 0,
  .is_fm_running = 0,
  .is_fm_muted = 0,
  .restart_fm = 0,
  .fm_device = 0,
  .card_status = CARD_STATUS_ONLINE,
};

static int32_t fm_set_volume(struct audio_device *adev, float value, bool persist)
{
    int32_t vol, ret = 0;
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name = FM_RX_VOLUME;

    ALOGV("%s: entry", __func__);
    ALOGD("%s: (%f)\n", __func__, value);

    if (value < 0.0) {
        ALOGW("%s: (%f) Under 0.0, assuming 0.0\n", __func__, value);
        value = 0.0;
    } else if (value > 1.0) {
        ALOGW("%s: (%f) Over 1.0, assuming 1.0\n", __func__, value);
        value = 1.0;
    }
    vol  = lrint((value * 0x2000) + 0.5);
    if (persist)
        fmmod.fm_volume = value;

    if (fmmod.is_fm_muted == true && vol > 0) {
        ALOGD("%s: fm is muted, applying '0' volume instead of '%d'.",
                                                        __func__, vol);
        vol = 0;
    }

    if (!fmmod.is_fm_running) {
        ALOGV("%s: FM not active, ignoring set_fm_volume call", __func__);
        return -EIO;
    }

    ALOGD("%s: Setting FM volume to %d \n", __func__, vol);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }
    mixer_ctl_set_value(ctl, 0, vol);
    ALOGV("%s: exit", __func__);
    return ret;
}

static int32_t fm_stop(struct audio_device *adev)
{
    int32_t ret = 0;
    struct audio_usecase *uc_info;

    ALOGD("%s: enter", __func__);
    fmmod.is_fm_running = false;

    /* 1. Close the PCM devices */
    if (fmmod.fm_pcm_rx) {
        pcm_close(fmmod.fm_pcm_rx);
        fmmod.fm_pcm_rx = NULL;
    }
    if (fmmod.fm_pcm_tx) {
        pcm_close(fmmod.fm_pcm_tx);
        fmmod.fm_pcm_tx = NULL;
    }

    uc_info = get_usecase_from_list(adev, USECASE_AUDIO_PLAYBACK_FM);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
              __func__, USECASE_VOICE_CALL);
        return -EINVAL;
    }

    /* 2. Get and set stream specific mixer controls */
    disable_audio_route(adev, uc_info);

    /* 3. Disable the rx and tx devices */
    disable_snd_device(adev, uc_info->out_snd_device);
    disable_snd_device(adev, uc_info->in_snd_device);

    list_remove(&uc_info->list);
    free(uc_info->stream.out);
    free(uc_info);

    ALOGD("%s: exit: status(%d)", __func__, ret);
    return ret;
}


static int32_t fm_start(struct audio_device *adev, audio_devices_t outputDevices)
{
    struct stream_out *fm_out;
    int32_t ret = 0;
    struct audio_usecase *uc_info;
    int32_t pcm_dev_rx_id, pcm_dev_tx_id;

    ALOGD("%s: Start FM over output device %d ", __func__, outputDevices);

    fm_out = (struct stream_out *)calloc(1, sizeof(struct stream_out));
    if (!fm_out)
        return -ENOMEM;

    fm_out->sample_rate = 48000;
    fm_out->format = AUDIO_FORMAT_PCM_16_BIT;
    fm_out->usecase = USECASE_AUDIO_PLAYBACK_FM;
    fm_out->config = pcm_config_fm;
    list_init(&fm_out->device_list);
    reassign_device_list(&fm_out->device_list, outputDevices, "");
    fmmod.is_fm_running = true;

    uc_info = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));

    if (!uc_info) {
        free(fm_out);
        return -ENOMEM;
    }

    uc_info->id = USECASE_AUDIO_PLAYBACK_FM;
    uc_info->type = PCM_PLAYBACK;
    uc_info->stream.out = fm_out;
    list_init(&uc_info->device_list);
    reassign_device_list(&uc_info->device_list, outputDevices, "");
    uc_info->in_snd_device = SND_DEVICE_NONE;
    uc_info->out_snd_device = SND_DEVICE_NONE;

    list_add_tail(&adev->usecase_list, &uc_info->list);

    select_devices(adev, USECASE_AUDIO_PLAYBACK_FM);

    pcm_dev_rx_id = platform_get_pcm_device_id(uc_info->id, PCM_PLAYBACK);
    pcm_dev_tx_id = platform_get_pcm_device_id(uc_info->id, PCM_CAPTURE);

    if (pcm_dev_rx_id < 0 || pcm_dev_tx_id < 0) {
        ALOGE("%s: Invalid PCM devices (rx: %d tx: %d) for the usecase(%d)",
              __func__, pcm_dev_rx_id, pcm_dev_tx_id, uc_info->id);
        ret = -EIO;
        goto exit;
    }

    ALOGV("%s: FM PCM devices (rx: %d tx: %d) for the usecase(%d)",
              __func__, pcm_dev_rx_id, pcm_dev_tx_id, uc_info->id);

    ALOGV("%s: Opening PCM playback device card_id(%d) device_id(%d)",
          __func__, adev->snd_card, pcm_dev_rx_id);
    fmmod.fm_pcm_rx = pcm_open(adev->snd_card,
                               pcm_dev_rx_id,
                               PCM_OUT, &pcm_config_fm);
    if (fmmod.fm_pcm_rx && !pcm_is_ready(fmmod.fm_pcm_rx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(fmmod.fm_pcm_rx));
        ret = -EIO;
        goto exit;
    }

    ALOGV("%s: Opening PCM capture device card_id(%d) device_id(%d)",
          __func__, adev->snd_card, pcm_dev_tx_id);
    fmmod.fm_pcm_tx = pcm_open(adev->snd_card,
                               pcm_dev_tx_id,
                               PCM_IN, &pcm_config_fm);
    if (fmmod.fm_pcm_tx && !pcm_is_ready(fmmod.fm_pcm_tx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(fmmod.fm_pcm_tx));
        ret = -EIO;
        goto exit;
    }
    pcm_start(fmmod.fm_pcm_rx);
    pcm_start(fmmod.fm_pcm_tx);

    fmmod.fm_device = get_device_types(&fm_out->device_list);

    ALOGD("%s: exit: status(%d)", __func__, ret);
    return 0;

exit:
    fm_stop(adev);
    ALOGE("%s: Problem in FM start: status(%d)", __func__, ret);
    return ret;
}

void fm_get_parameters(struct str_parms *query, struct str_parms *reply)
{
    int ret, val;
    char value[32]={0};

    ALOGV("%s: enter", __func__);
    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_FM_STATUS, value, sizeof(value));
    if (ret >= 0) {
        val = (fmmod.is_fm_running ? 1: 0);
        str_parms_add_int(reply, AUDIO_PARAMETER_KEY_FM_STATUS, val);
    }
}

void fm_set_parameters(struct audio_device *adev,
                                  struct str_parms *parms)
{
    int ret, val;
    char value[32]={0};
    float vol =0.0;

    ALOGV("%s: enter", __func__);
    ret = str_parms_get_str(parms, "SND_CARD_STATUS", value, sizeof(value));
    if (ret >= 0) {
        char *snd_card_status = value+2;
        if (strstr(snd_card_status, "OFFLINE")) {
            fmmod.card_status = CARD_STATUS_OFFLINE;
        }
        else if (strstr(snd_card_status, "ONLINE")) {
            fmmod.card_status = CARD_STATUS_ONLINE;
        }
    }
    if(fmmod.is_fm_running) {
        if (fmmod.card_status == CARD_STATUS_OFFLINE) {
            ALOGD("sound card is OFFLINE, stop FM");
            fm_stop(adev);
            fmmod.restart_fm = 1;
        }

        ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING,
                                value, sizeof(value));
        if (ret >= 0) {
            val = atoi(value);
            if(val > 0)
                select_devices(adev, USECASE_AUDIO_PLAYBACK_FM);
        }
    }
    if (fmmod.restart_fm && (fmmod.card_status == CARD_STATUS_ONLINE)) {
        ALOGD("sound card is ONLINE, restart FM");
        fmmod.restart_fm = 0;
        fm_start(adev, fmmod.fm_device);
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_HANDLE_FM,
                            value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        ALOGD("%s: FM usecase", __func__);
        if (val != 0) {
            if(val & AUDIO_DEVICE_OUT_FM
               && fmmod.is_fm_running == false) {
                audio_devices_t OutputDevice = val & ~AUDIO_DEVICE_OUT_FM;
                fm_start(adev, OutputDevice);
            } else if (!(val & AUDIO_DEVICE_OUT_FM)
                     && fmmod.is_fm_running == true) {
                fm_set_volume(adev, 0, false);
                usleep(FM_LOOPBACK_DRAIN_TIME_MS*1000);
                fm_stop(adev);
            }
       }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FM_ROUTING,
                            value, sizeof(value));
    if (ret >= 0 && fmmod.is_fm_running) {
        val = atoi(value);
        ALOGD("%s: FM usecase", __func__);
        if (val != 0) {
            if(val & AUDIO_DEVICE_OUT_FM) {
                audio_devices_t OutputDevice = val & ~AUDIO_DEVICE_OUT_FM;
                fm_set_volume(adev, 0, false);
                fm_stop(adev);
                fm_start(adev, OutputDevice);
            }
        }
    }

    memset(value, 0, sizeof(value));
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FM_VOLUME,
                            value, sizeof(value));
    if (ret >= 0) {
        if (sscanf(value, "%f", &vol) != 1){
            ALOGE("%s: error in retrieving fm volume", __func__);
            ret = -EIO;
            goto exit;
        }
        ALOGD("%s: set_fm_volume usecase", __func__);
        fm_set_volume(adev, vol, true);
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FM_MUTE,
                            value, sizeof(value));
    if (ret >= 0) {
        if (value[0] == '1')
            fmmod.is_fm_muted = true;
        else
            fmmod.is_fm_muted = false;
        ALOGV("%s: set_fm_volume from param mute", __func__);
        fm_set_volume(adev, fmmod.fm_volume, false);
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_FM_RESTORE_VOLUME,
                            value, sizeof(value));
    if (ret >= 0) {
        if (value[0] == '1')
            fm_set_volume(adev, fmmod.fm_volume, false);
        ALOGV("%s: set_fm_volume from param restore volume", __func__);
    }

    if(audio_extn_is_record_play_concurrency_enabled()) {
        ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_REC_PLAY_CONC,
                               value, sizeof(value));
        if ((ret >= 0)
              && (fmmod.is_fm_running == true)) {

            if (!strncmp("true", value, sizeof("true")))
                ALOGD("Record play concurrency ON Forcing FM device reroute");
            else
                ALOGD("Record play concurrency OFF Forcing FM device reroute");

            select_devices(adev, USECASE_AUDIO_PLAYBACK_FM);
            fm_set_volume(adev, fmmod.fm_volume, false);
        }
    }
exit:
    ALOGV("%s: exit", __func__);
}

void audio_extn_fm_route_on_selected_device(struct audio_device *adev,
                                            struct listnode *devices)
{
    struct listnode *node;
    struct audio_usecase *usecase;

    if (fmmod.is_fm_running) {
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (usecase->id == USECASE_AUDIO_PLAYBACK_FM) {
                if (fmmod.fm_device != get_device_types(devices)) {
                    ALOGV("%s selected routing device %x current device %x"
                          "are different, reroute on selected device", __func__,
                          fmmod.fm_device, get_device_types(devices));
                    select_devices(adev, usecase->id);
                }
            }
        }
    }
}
