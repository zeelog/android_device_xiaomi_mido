/*
* Copyright (c) 2014-2018, 2020, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "keep_alive"
/*#define LOG_NDEBUG 0*/

#include <cutils/properties.h>
#include <stdlib.h>
#include <log/log.h>
#include "audio_hw.h"
#include "audio_extn.h"
#include "platform_api.h"
#include <platform.h>
#include <pthread.h>

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_KEEP_ALIVE
#include <log_utils.h>
#endif

#define SILENCE_INTERVAL 2 /*In secs*/

typedef enum {
    STATE_DEINIT = -1,
    STATE_IDLE,
    STATE_ACTIVE,
    STATE_DISABLED,
} state_t;

typedef enum {
    REQUEST_WRITE,
    REQUEST_QUIT,
} request_t;

typedef struct {
    pthread_mutex_t lock;
    pthread_mutex_t sleep_lock;
    pthread_cond_t  cond;
    pthread_cond_t  wake_up_cond;
    pthread_t thread;
    state_t state;
    struct listnode cmd_list;
    struct pcm *pcm;
    struct stream_out *out;
    ka_mode_t prev_mode;
    bool done;
    void * userdata;
    struct listnode active_devices;
} keep_alive_t;

struct keep_alive_cmd {
    struct listnode node;
    request_t req;
};

static keep_alive_t ka;

static struct pcm_config silence_config = {
    .channels = 2,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = DEEP_BUFFER_OUTPUT_PERIOD_SIZE,
    .period_count = DEEP_BUFFER_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4,
};

static void * keep_alive_loop(void * context);
static int keep_alive_cleanup();
static int keep_alive_start_l();

static void send_cmd_l(request_t r)
{
    if (ka.state == STATE_DEINIT || ka.state == STATE_DISABLED)
        return;

    struct keep_alive_cmd *cmd =
        (struct keep_alive_cmd *)calloc(1, sizeof(struct keep_alive_cmd));

    if (cmd == NULL) {
        ALOGE("%s: cmd is NULL", __func__);
        return;
    }

    cmd->req = r;
    list_add_tail(&ka.cmd_list, &cmd->node);
    pthread_cond_signal(&ka.cond);
}

void keep_alive_init(struct audio_device *adev)
{
    ka.userdata = adev;
    ka.state = STATE_IDLE;
    ka.pcm = NULL;
    pthread_condattr_t attr;
    if (property_get_bool("vendor.audio.keep_alive.disabled", true)) {
        ALOGE("keep alive disabled");
        ka.state = STATE_DISABLED;
        return;
    }
    ka.done = false;
    ka.prev_mode = KEEP_ALIVE_OUT_NONE;
    list_init(&ka.active_devices);

    pthread_mutex_init(&ka.lock, (const pthread_mutexattr_t *) NULL);
    pthread_cond_init(&ka.cond, (const pthread_condattr_t *) NULL);
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&ka.wake_up_cond, &attr);
    pthread_mutex_init(&ka.sleep_lock, (const pthread_mutexattr_t *) NULL);
    list_init(&ka.cmd_list);
    if (pthread_create(&ka.thread,  (const pthread_attr_t *) NULL,
                       keep_alive_loop, NULL) < 0) {
        ALOGW("Failed to create keep_alive_thread");
        /* can continue without keep alive */
        ka.state = STATE_DEINIT;
        return;
    }
    ALOGV("%s init done", __func__);
}

void keep_alive_deinit()
{
    if (ka.state == STATE_DEINIT || ka.state == STATE_DISABLED)
        return;
    ka.userdata = NULL;
    ka.done = true;
    pthread_mutex_lock(&ka.lock);
    send_cmd_l(REQUEST_QUIT);
    pthread_mutex_unlock(&ka.lock);
    pthread_join(ka.thread, (void **) NULL);
    pthread_mutex_destroy(&ka.lock);
    pthread_cond_destroy(&ka.cond);
    pthread_cond_destroy(&ka.wake_up_cond);
    pthread_mutex_destroy(&ka.sleep_lock);
    ALOGV("%s deinit done", __func__);
}

void get_device_id_from_mode(ka_mode_t ka_mode,
                             struct listnode *out_devices)
{
    struct audio_device * adev = (struct audio_device *)ka.userdata;

    switch (ka_mode)
    {
        case KEEP_ALIVE_OUT_PRIMARY:
            if (adev->primary_output) {
                if (is_audio_out_device_type(&adev->primary_output->device_list))
                    assign_output_devices(out_devices, &adev->primary_output->device_list);
                else
                    reassign_device_list(out_devices, AUDIO_DEVICE_OUT_SPEAKER, "");
            }
            else {
                reassign_device_list(out_devices, AUDIO_DEVICE_OUT_SPEAKER, "");
            }
            break;

        case KEEP_ALIVE_OUT_HDMI:
            reassign_device_list(out_devices, AUDIO_DEVICE_OUT_AUX_DIGITAL, "");
            break;
        case KEEP_ALIVE_OUT_NONE:
        default:
            break;
    }
}

void keep_alive_start(ka_mode_t ka_mode)
{
    struct audio_device * adev = (struct audio_device *)ka.userdata;
    struct listnode out_devices;

    pthread_mutex_lock(&ka.lock);
    ALOGV("%s: mode %x", __func__, ka_mode);
    if ((ka.state == STATE_DISABLED)||(ka.state == STATE_DEINIT)) {
        ALOGE(" %s : Unexpected state %x",__func__, ka.state);
        goto exit;
    }

    list_init(&out_devices);
    get_device_id_from_mode(ka_mode, &out_devices);
    if (compare_devices(&out_devices, &ka.active_devices) &&
            (ka.state == STATE_ACTIVE)) {
        ALOGV(" %s : Already feeding silence to device %x",__func__,
              get_device_types(&out_devices));
        ka.prev_mode |= ka_mode;
        goto exit;
    }
    ALOGV(" %s : active devices %x, new device %x",__func__,
           get_device_types(&ka.active_devices), get_device_types(&out_devices));

    if (list_empty(&out_devices))
        goto exit;

    if (audio_extn_passthru_is_active()) {
        update_device_list(&ka.active_devices, AUDIO_DEVICE_OUT_AUX_DIGITAL,
                           "", false);
        if (list_empty(&ka.active_devices))
            goto exit;
    }

    append_devices(&ka.active_devices, &out_devices);
    ka.prev_mode |= ka_mode;
    if (ka.state == STATE_ACTIVE) {
        assign_devices(&ka.out->device_list, &ka.active_devices);
        select_devices(adev, USECASE_AUDIO_PLAYBACK_SILENCE);
    } else if (ka.state == STATE_IDLE) {
        keep_alive_start_l();
    }

exit:
    pthread_mutex_unlock(&ka.lock);
}

/* must be called with adev lock held */
static int keep_alive_start_l()
{
    struct audio_device * adev = (struct audio_device *)ka.userdata;
    unsigned int flags = PCM_OUT|PCM_MONOTONIC;
    struct audio_usecase *usecase;
    int rc = 0;

    int silence_pcm_dev_id =
            platform_get_pcm_device_id(USECASE_AUDIO_PLAYBACK_SILENCE,
                                       PCM_PLAYBACK);

    ka.done = false;
    usecase = calloc(1, sizeof(struct audio_usecase));
    if (usecase == NULL) {
        ALOGE("%s: usecase is NULL", __func__);
        rc = -ENOMEM;
        goto exit;
    }

    ka.out = (struct stream_out *)calloc(1, sizeof(struct stream_out));
    if (ka.out == NULL) {
        ALOGE("%s: keep_alive out is NULL", __func__);
        free(usecase);
        rc = -ENOMEM;
        goto exit;
    }

    ka.out->flags = 0;
    list_init(&ka.out->device_list);
    assign_devices(&ka.out->device_list, &ka.active_devices);
    ka.out->dev = adev;
    ka.out->format = AUDIO_FORMAT_PCM_16_BIT;
    ka.out->sample_rate = DEFAULT_OUTPUT_SAMPLING_RATE;
    ka.out->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    ka.out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_STEREO;
    ka.out->config = silence_config;

    usecase->stream.out = ka.out;
    usecase->type = PCM_PLAYBACK;
    usecase->id = USECASE_AUDIO_PLAYBACK_SILENCE;
    list_init(&usecase->device_list);
    usecase->out_snd_device = SND_DEVICE_NONE;
    usecase->in_snd_device = SND_DEVICE_NONE;

    list_add_tail(&adev->usecase_list, &usecase->list);
    select_devices(adev, USECASE_AUDIO_PLAYBACK_SILENCE);

    ALOGD("opening pcm device for silence playback %x", silence_pcm_dev_id);
    ka.pcm = pcm_open(adev->snd_card, silence_pcm_dev_id,
                      flags, &silence_config);
    if (ka.pcm == NULL || !pcm_is_ready(ka.pcm)) {
        ALOGE("%s: %s", __func__, pcm_get_error(ka.pcm));
        if (ka.pcm != NULL) {
            pcm_close(ka.pcm);
            ka.pcm = NULL;
        }
        goto exit;
    }
    send_cmd_l(REQUEST_WRITE);
    while (ka.state != STATE_ACTIVE) {
        pthread_cond_wait(&ka.cond, &ka.lock);
    }
    return rc;
exit:
    keep_alive_cleanup();
    return rc;
}

void keep_alive_stop(ka_mode_t ka_mode)
{
    struct audio_device * adev = (struct audio_device *)ka.userdata;
    struct listnode out_devices;
    if (ka.state == STATE_DISABLED)
        return;

    pthread_mutex_lock(&ka.lock);

    ALOGV("%s: mode %x", __func__, ka_mode);
    list_init(&out_devices);
    if (ka_mode && (ka.state != STATE_ACTIVE)) {
        get_device_id_from_mode(ka_mode, &out_devices);
        ALOGV(" %s : Can't stop, keep_alive",__func__);
        ALOGV(" %s : keep_alive is not running on device %x",__func__,
                get_device_types(&out_devices));
        ka.prev_mode |= ka_mode;
        goto exit;
    }
    get_device_id_from_mode(ka_mode, &out_devices);
    if (ka.prev_mode & ka_mode) {
        ka.prev_mode &= ~ka_mode;
        get_device_id_from_mode(ka.prev_mode, &ka.active_devices);
    }

    if (list_empty(&ka.active_devices)) {
        keep_alive_cleanup();
    } else if (!compare_devices(&ka.out->device_list, &ka.active_devices)) {
        assign_devices(&ka.out->device_list, &ka.active_devices);
        select_devices(adev, USECASE_AUDIO_PLAYBACK_SILENCE);
    }
exit:
    pthread_mutex_unlock(&ka.lock);
}

/* must be called with adev lock held */
static int keep_alive_cleanup()
{
    struct audio_device * adev = (struct audio_device *)ka.userdata;
    struct audio_usecase *uc_info;

    ka.done = true;
    if (ka.out != NULL)
        free(ka.out);

    pthread_mutex_lock(&ka.sleep_lock);
    pthread_cond_signal(&ka.wake_up_cond);
    pthread_mutex_unlock(&ka.sleep_lock);
    while (ka.state != STATE_IDLE) {
        pthread_cond_wait(&ka.cond, &ka.lock);
    }
    ALOGV("%s: keep_alive state changed to %x", __func__, ka.state);

    uc_info = get_usecase_from_list(adev, USECASE_AUDIO_PLAYBACK_SILENCE);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find keep alive usecase in the list", __func__);
    } else {
        disable_audio_route(adev, uc_info);
        disable_snd_device(adev, uc_info->out_snd_device);
        list_remove(&uc_info->list);
        free(uc_info);
    }
    pcm_close(ka.pcm);
    ka.pcm = NULL;
    clear_devices(&ka.active_devices);
    return 0;
}

int keep_alive_set_parameters(struct audio_device *adev __unused,
                                         struct str_parms *parms __unused)
{
    char value[32];
    int ret, pcm_device_id=0;
    if (ka.state == STATE_DISABLED)
        return 0;

    if ((ka.state == STATE_ACTIVE) && (ka.prev_mode & KEEP_ALIVE_OUT_PRIMARY)){
        ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING,
                                value, sizeof(value));
        if (ret >= 0) {
            pcm_device_id = atoi(value);
            if(pcm_device_id > 0)
            {
                audio_extn_keep_alive_start(KEEP_ALIVE_OUT_PRIMARY);
            }
        }
    }
    return 0;
}

static void * keep_alive_loop(void * context __unused)
{
    struct keep_alive_cmd *cmd = NULL;
    struct listnode *item;
    uint8_t * silence = NULL;
    int32_t bytes = 0;
    struct timespec ts;

    while (true) {
        pthread_mutex_lock(&ka.lock);
        if (list_empty(&ka.cmd_list)) {
            pthread_cond_wait(&ka.cond, &ka.lock);
            pthread_mutex_unlock(&ka.lock);
            continue;
        }

        item = list_head(&ka.cmd_list);
        cmd = node_to_item(item, struct keep_alive_cmd, node);
        list_remove(item);

        if (cmd->req == REQUEST_QUIT) {
            free(cmd);
            pthread_mutex_unlock(&ka.lock);
            break;
        } else if (cmd->req != REQUEST_WRITE) {
            free(cmd);
            pthread_mutex_unlock(&ka.lock);
            continue;
        }

        free(cmd);
        ka.state = STATE_ACTIVE;
        ALOGV("%s: state changed to %x", __func__, ka.state);
        pthread_cond_signal(&ka.cond);
        pthread_mutex_unlock(&ka.lock);

        if (!silence) {
            /* 50 ms */
            bytes =
                (silence_config.rate * silence_config.channels * sizeof(int16_t)) / 20;
            silence = (uint8_t *)calloc(1, bytes);
        }

        while (!ka.done) {
            ALOGV("write %d bytes of silence", bytes);
            pcm_write(ka.pcm, (void *)silence, bytes);
            /* This thread does not have to write silence continuously.
             * Just something to keep the connection alive is sufficient.
             * Hence a short burst of silence periodically.
             */
            pthread_mutex_lock(&ka.sleep_lock);
            clock_gettime(CLOCK_MONOTONIC, &ts);
            ts.tv_sec += SILENCE_INTERVAL;

            if (!ka.done)
              pthread_cond_timedwait(&ka.wake_up_cond,
                            &ka.sleep_lock, &ts);

            pthread_mutex_unlock(&ka.sleep_lock);
        }
        pthread_mutex_lock(&ka.lock);
        ka.state = STATE_IDLE;
        ALOGV("%s: state changed to %x", __func__, ka.state);
        pthread_cond_signal(&ka.cond);
        pthread_mutex_unlock(&ka.lock);
    }
    return 0;
}
