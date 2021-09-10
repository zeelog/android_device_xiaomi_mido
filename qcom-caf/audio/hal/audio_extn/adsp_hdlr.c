/*
* Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "audio_adsp_hdlr_event"

/*#define LOG_NDEBUG 0*/
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <log/log.h>
#include <cutils/sched_policy.h>
#include <system/thread_defs.h>
#include <sound/asound.h>
#include <linux/msm_audio.h>

#include "audio_hw.h"
#include "audio_defs.h"
#include "platform.h"
#include "platform_api.h"
#include "adsp_hdlr.h"

#define MAX_EVENT_PAYLOAD             512
#define WAIT_EVENT_POLL_TIMEOUT       50

#define MIXER_MAX_BYTE_LENGTH 512

struct adsp_hdlr_stream_data {
    struct adsp_hdlr_stream_cfg config;
    stream_callback_t client_callback;
    void *client_cookie;
};

struct adsp_hdlr_event_info {
    struct listnode list;
    void *stream_handle;
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH];
    char cb_mixer_ctl_name[MIXER_PATH_MAX_LENGTH];
    adsp_event_callback_t cb;
    void *cookie;
    int event_type;
};

struct adsp_hdlr_inst {
    struct listnode event_list;
    bool binit;
    struct mixer *mixer;
    pthread_mutex_t event_list_lock;

    struct listnode list;
    pthread_cond_t event_wait_cond;
    pthread_t event_wait_thread;
    struct listnode event_wait_cmd_list;
    pthread_mutex_t event_wait_lock;
    bool event_wait_thread_active;

    pthread_cond_t event_callback_cond;
    pthread_t event_callback_thread;
    struct listnode event_callback_cmd_list;
    pthread_mutex_t event_callback_lock;
    bool event_callback_thread_active;
};

enum {
    EVENT_CMD_EXIT,             /* event thread exit command loop*/
    EVENT_CMD_WAIT,             /* event thread wait on mixer control */
    EVENT_CMD_GET               /* event thread get param data from mixer */
};

struct event_cmd {
    struct listnode list;
    int opcode;
    char cb_mixer_ctl_name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
};

static struct adsp_hdlr_inst *adsp_hdlr_inst = NULL;

static void *event_wait_thread_loop(void *context);
static void *event_callback_thread_loop(void *context);

static int send_cmd_event_wait_thread(struct adsp_hdlr_inst *adsp_hdlr_inst, int opcode)
{
    struct event_cmd *cmd = calloc(1, sizeof(*cmd));

    if (!cmd) {
        ALOGE("Failed to allocate mem for command 0x%x", opcode);
        return -ENOMEM;
    }

    ALOGVV("%s %d", __func__, opcode);

    cmd->opcode = opcode;

    pthread_mutex_lock(&adsp_hdlr_inst->event_wait_lock);
    list_add_tail(&adsp_hdlr_inst->event_wait_cmd_list, &cmd->list);
    pthread_cond_signal(&adsp_hdlr_inst->event_wait_cond);
    pthread_mutex_unlock(&adsp_hdlr_inst->event_wait_lock);

    return 0;
}

static int send_cmd_event_callback_thread(struct adsp_hdlr_inst *adsp_hdlr_inst,
                                          int opcode, char *mixer_ctl_name)
{
    struct event_cmd *cmd = calloc(1, sizeof(*cmd));

    if (!cmd) {
        ALOGE("Failed to allocate mem for command 0x%x", opcode);
        return -ENOMEM;
    }

    ALOGVV("%s opcode %d, name = %s", __func__, opcode, mixer_ctl_name);

    cmd->opcode = opcode;
    if (mixer_ctl_name)
        strlcpy(cmd->cb_mixer_ctl_name, mixer_ctl_name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN);

    pthread_mutex_lock(&adsp_hdlr_inst->event_callback_lock);
    list_add_tail(&adsp_hdlr_inst->event_callback_cmd_list, &cmd->list);
    pthread_cond_signal(&adsp_hdlr_inst->event_callback_cond);
    pthread_mutex_unlock(&adsp_hdlr_inst->event_callback_lock);

    return 0;
}

static void create_event_wait_thread(struct adsp_hdlr_inst *adsp_hdlr_inst)
{
    pthread_cond_init(&adsp_hdlr_inst->event_wait_cond,
                        (const pthread_condattr_t *) NULL);
    list_init(&adsp_hdlr_inst->event_wait_cmd_list);
    pthread_create(&adsp_hdlr_inst->event_wait_thread, (const pthread_attr_t *) NULL,
                    event_wait_thread_loop, adsp_hdlr_inst);
    adsp_hdlr_inst->event_wait_thread_active = true;
}

static void create_event_callback_thread(struct adsp_hdlr_inst *adsp_hdlr_inst)
{
    pthread_cond_init(&adsp_hdlr_inst->event_callback_cond,
                      (const pthread_condattr_t *) NULL);
    list_init(&adsp_hdlr_inst->event_callback_cmd_list);
    pthread_create(&adsp_hdlr_inst->event_callback_thread, (const pthread_attr_t *) NULL,
                   event_callback_thread_loop, adsp_hdlr_inst);
    adsp_hdlr_inst->event_callback_thread_active = true;
}

static void destroy_event_wait_thread(struct adsp_hdlr_inst *adsp_hdlr_inst)
{
    send_cmd_event_wait_thread(adsp_hdlr_inst, EVENT_CMD_EXIT);
    pthread_join(adsp_hdlr_inst->event_wait_thread, (void **) NULL);

    pthread_mutex_lock(&adsp_hdlr_inst->event_wait_lock);
    pthread_cond_destroy(&adsp_hdlr_inst->event_wait_cond);
    adsp_hdlr_inst->event_wait_thread_active = false;
    pthread_mutex_unlock(&adsp_hdlr_inst->event_wait_lock);
}

static void destroy_event_callback_thread(struct adsp_hdlr_inst *adsp_hdlr_inst)
{
    send_cmd_event_callback_thread(adsp_hdlr_inst, EVENT_CMD_EXIT, NULL);
    pthread_join(adsp_hdlr_inst->event_callback_thread, (void **) NULL);

    pthread_mutex_lock(&adsp_hdlr_inst->event_callback_lock);
    pthread_cond_destroy(&adsp_hdlr_inst->event_callback_cond);
    adsp_hdlr_inst->event_callback_thread_active = false;
    pthread_mutex_unlock(&adsp_hdlr_inst->event_callback_lock);
}

static void destroy_event_threads(struct adsp_hdlr_inst *adsp_hdlr_inst)
{
    if (adsp_hdlr_inst->event_wait_thread_active)
        destroy_event_wait_thread(adsp_hdlr_inst);
    if (adsp_hdlr_inst->event_callback_thread_active)
        destroy_event_callback_thread(adsp_hdlr_inst);
}

static void *event_wait_thread_loop(void *context)
{
    int ret = 0;
    int opcode = 0;
    bool wait = false;
    struct adsp_hdlr_inst *adsp_hdlr_inst =
                        (struct adsp_hdlr_inst *) context;
    struct event_cmd *cmd;
    struct listnode *node, *tempnode;
    struct snd_ctl_event mixer_event = {0};

    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_AUDIO);
    set_sched_policy(0, SP_BACKGROUND);
    prctl(PR_SET_NAME, (unsigned long)"Event Wait", 0, 0, 0);

    ret = mixer_subscribe_events(adsp_hdlr_inst->mixer, 1);
    if (ret < 0) {
        ALOGE("%s: Could not subscribe for mixer events, ret %d",
              __func__, ret);
        goto done;
    }

    pthread_mutex_lock(&adsp_hdlr_inst->event_wait_lock);
    while (1) {
        if (list_empty(&adsp_hdlr_inst->event_wait_cmd_list) && !wait) {
            ALOGVV("%s SLEEPING", __func__);
            pthread_cond_wait(&adsp_hdlr_inst->event_wait_cond, &adsp_hdlr_inst->event_wait_lock);
            ALOGVV("%s RUNNING", __func__);
        }
        /* execute command if available */
        if (!list_empty(&adsp_hdlr_inst->event_wait_cmd_list)) {
            node = list_head(&adsp_hdlr_inst->event_wait_cmd_list);
            list_remove(node);
            pthread_mutex_unlock(&adsp_hdlr_inst->event_wait_lock);
            cmd = node_to_item(node, struct event_cmd, list);
            opcode = cmd->opcode;
       /* wait if no command avialable */
       } else if (wait)
           opcode = EVENT_CMD_WAIT;
       /* check que again and sleep if needed */
       else
           continue;

        ALOGVV("%s command received: %d", __func__, opcode);
        switch(opcode) {
        case EVENT_CMD_EXIT:
            free(cmd);
            goto thread_exit;
        case EVENT_CMD_WAIT:
            ret = mixer_wait_event(adsp_hdlr_inst->mixer, WAIT_EVENT_POLL_TIMEOUT);
            ALOGVV("%s: mixer_wait_event unblocked!, ret = %d", __func__, ret);
            if (ret < 0) {
                ALOGE("%s: mixer_wait_event err!, ret = %d", __func__, ret);
            } else if (ret > 0) {
                ret = mixer_read(adsp_hdlr_inst->mixer, &mixer_event);
                if (ret >= 0)
                    send_cmd_event_callback_thread(adsp_hdlr_inst, EVENT_CMD_GET, mixer_event.data.elem.id.name);
                else
                    ALOGE("%s: mixer_read failed, ret = %d", __func__, ret);
            }
            /* Once wait command has been sent continue to wait for
               events unless something else is in the command que */
            wait = true;
            break;
        default:
            ALOGE("%s unknown command received: %d", __func__, opcode);
            break;
        }

        if (cmd != NULL) {
            free(cmd);
            cmd = NULL;
        }
    }
thread_exit:
    ret = mixer_subscribe_events(adsp_hdlr_inst->mixer, 0);
    if (ret < 0) {
        ALOGE("%s: Could not un-subscribe for mixer events, ret %d",
              __func__, ret);
        goto done;
    }
    pthread_mutex_lock(&adsp_hdlr_inst->event_wait_lock);
    list_for_each_safe(node, tempnode, &adsp_hdlr_inst->event_wait_cmd_list) {
        list_remove(node);
        free(node);
    }
    pthread_mutex_unlock(&adsp_hdlr_inst->event_wait_lock);
done:
    return NULL;
}

static void *event_callback_thread_loop(void *context)
{
    int ret = 0;
    size_t count = 0;
    struct adsp_hdlr_inst *adsp_hdlr_inst =
                            (struct adsp_hdlr_inst *)context;
    struct mixer_ctl *ctl = NULL;
    uint8_t param[MAX_EVENT_PAYLOAD] = {0};
    struct event_cmd *cmd;
    struct listnode *node, *tempnode;
    struct adsp_hdlr_event_info *event_info;
    bool param_avail = false;
    struct msm_adsp_event_data *received_evt;

    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_AUDIO);
    set_sched_policy(0, SP_BACKGROUND);
    prctl(PR_SET_NAME, (unsigned long)"Event Callback", 0, 0, 0);

    pthread_mutex_lock(&adsp_hdlr_inst->event_callback_lock);
    while (1) {
        if (list_empty(&adsp_hdlr_inst->event_callback_cmd_list)) {
            ALOGVV("%s SLEEPING", __func__);
            pthread_cond_wait(&adsp_hdlr_inst->event_callback_cond,
                              &adsp_hdlr_inst->event_callback_lock);
            ALOGVV("%s RUNNING", __func__);
            continue;
        }
        node = list_head(&adsp_hdlr_inst->event_callback_cmd_list);
        list_remove(node);
        pthread_mutex_unlock(&adsp_hdlr_inst->event_callback_lock);
        cmd = node_to_item(node, struct event_cmd, list);

        ALOGVV("%s command received: %d", __func__, cmd->opcode);
        switch(cmd->opcode) {
        case EVENT_CMD_EXIT:
            free(cmd);
            goto thread_exit;
        case EVENT_CMD_GET:
            param_avail = false;
            pthread_mutex_lock(&adsp_hdlr_inst->event_list_lock);
            /* Find the mixer control for which event is triggered */
            list_for_each(node, &adsp_hdlr_inst->event_list) {
                event_info = node_to_item(node, struct adsp_hdlr_event_info, list);
                ALOGVV("%s: cmd mixer name: %s, event list mixer name: %s", __func__,
                       cmd->cb_mixer_ctl_name, event_info->cb_mixer_ctl_name);
                if (!strcmp(cmd->cb_mixer_ctl_name, event_info->cb_mixer_ctl_name)) {
                    if (!param_avail) {
                        ctl = mixer_get_ctl_by_name(adsp_hdlr_inst->mixer, cmd->cb_mixer_ctl_name);
                        if (!ctl) {
                            ALOGE("%s: Could not get ctl for mixer cmd - %s", __func__,
                                  cmd->cb_mixer_ctl_name);
                            break;
                        }
                        mixer_ctl_update(ctl);
                        count = mixer_ctl_get_num_values(ctl);
                        if ((count > MAX_EVENT_PAYLOAD) || (count <= 0)) {
                            ALOGE("%s: count is %d greater than allowed for %s mixer cmd",
                                  __func__, count, cmd->cb_mixer_ctl_name);
                            break;
                        }
                        ret = mixer_ctl_get_array(ctl, param, count);
                        if (ret < 0) {
                            ALOGE("%s: mixer_ctl_get_array failed! mixer - %s, ret = %d",
                                  __func__, cmd->cb_mixer_ctl_name, ret);
                            break;
                        }
                        param_avail = true;
                        received_evt = (struct msm_adsp_event_data *)param;
                        ALOGD("%s: event type = %d", __func__, received_evt->event_type);
                    }
                    /* Call appropriate event type client callback */
                    if (param_avail && event_info->event_type == received_evt->event_type) {
                        struct adsp_hdlr_stream_data *stream_data = event_info->stream_handle;
                        if (event_info->cb != NULL) {
                            ALOGVV("%s: calling event callback function", __func__);
                            event_info->cb(event_info->stream_handle,
                                           received_evt->payload,
                                           event_info->cookie);
                        } else if (stream_data->client_callback != NULL) {
                            ALOGVV("%s: sending client callback event %d", __func__,
                                   AUDIO_EXTN_STREAM_CBK_EVENT_ADSP);
                            stream_data->client_callback((stream_callback_event_t)
                                                         AUDIO_EXTN_STREAM_CBK_EVENT_ADSP,
                                                         received_evt,
                                                         stream_data->client_cookie);
                        }
                        break;
                    }
                }
            }
            pthread_mutex_unlock(&adsp_hdlr_inst->event_list_lock);

        break;
        default:
            ALOGE("%s unknown command received: %d", __func__, cmd->opcode);
            break;
        }
        free(cmd);
    }
thread_exit:
    pthread_mutex_lock(&adsp_hdlr_inst->event_callback_lock);
    list_for_each_safe(node, tempnode, &adsp_hdlr_inst->event_callback_cmd_list) {
        list_remove(node);
        free(node);
    }
    pthread_mutex_unlock(&adsp_hdlr_inst->event_callback_lock);
done:
    return NULL;
}

int audio_extn_adsp_hdlr_stream_deregister_event(void *handle, void *data)
{
    struct listnode *node, *tempnode;
    struct adsp_hdlr_stream_data *stream_data = (struct adsp_hdlr_stream_data *)handle;
    struct adsp_hdlr_event_info *event_info;
    struct audio_adsp_event *param = (struct audio_adsp_event *)data;

    if (!handle) {
        ALOGE("%s: Invalid handle", __func__);
        return -EINVAL;
    }

    pthread_mutex_lock(&adsp_hdlr_inst->event_list_lock);
    if (list_empty(&adsp_hdlr_inst->event_list)) {
        ALOGD("%s: event list is empty", __func__);
        pthread_mutex_unlock(&adsp_hdlr_inst->event_list_lock);
        return 0;
    }
    list_for_each_safe(node, tempnode, &adsp_hdlr_inst->event_list) {
        event_info = node_to_item(node, struct adsp_hdlr_event_info, list);
        if (param && event_info->stream_handle == stream_data) {
            /* if the type of event is avaliable to dereg then dereg only that event */
            if (event_info->event_type == param->event_type) {
                ALOGD("%s: Deregister event type = %d", __func__, event_info->event_type);
                list_remove(node);
                free(event_info);
            }
        } else if (event_info->stream_handle == stream_data) {
            /* Dereg all the events related to that stream */
            ALOGD("%s: Deregister all stream events", __func__);
            list_remove(node);
            free(event_info);
        }
    }
    pthread_mutex_unlock(&adsp_hdlr_inst->event_list_lock);

    if (list_empty(&adsp_hdlr_inst->event_list)) {
        ALOGD("%s: Closing event threads", __func__);
        destroy_event_threads(adsp_hdlr_inst);
        pthread_mutex_destroy(&adsp_hdlr_inst->event_wait_lock);
        pthread_mutex_destroy(&adsp_hdlr_inst->event_callback_lock);
        pthread_mutex_destroy(&adsp_hdlr_inst->event_list_lock);
    }

    return 0;
}

int audio_extn_adsp_hdlr_stream_register_event(void *handle, void *data,
                                               adsp_event_callback_t cb,
                                               void *cookie, bool is_adm_event)
{
    int ret = 0;
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};
    char cb_mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};
    struct mixer_ctl *ctl = NULL;
    uint8_t payload[AUDIO_MAX_ADSP_STREAM_CMD_PAYLOAD_LEN] = {0};
    struct adsp_hdlr_stream_data *stream_data = (struct adsp_hdlr_stream_data *)handle;
    struct adsp_hdlr_stream_cfg *config = &stream_data->config;
    struct adsp_hdlr_event_info *event_info;
    struct audio_adsp_event *param = (struct audio_adsp_event *)data;

    if (!param || !handle) {
        ret = -EINVAL;
        ALOGE("%s: Invalid input arguments", __func__);
        goto done;
    }

    /* check if param size exceeds max size supported by mixer */
    if (param->payload_length > AUDIO_MAX_ADSP_STREAM_CMD_PAYLOAD_LEN) {
        ALOGE("%s: Invalid payload_length %d",__func__, param->payload_length);
        return -EINVAL;
    }

    if (is_adm_event)
        ret = snprintf(cb_mixer_ctl_name, sizeof(cb_mixer_ctl_name),
            "ADSP COPP Callback Event");
    else
        ret = snprintf(cb_mixer_ctl_name, sizeof(cb_mixer_ctl_name),
            "ADSP Stream Callback Event %d", config->pcm_device_id);

    if (ret < 0) {
        ALOGE("%s: snprintf failed",__func__);
        ret = -EINVAL;
        goto done;
    }

    ctl = mixer_get_ctl_by_name(adsp_hdlr_inst->mixer, cb_mixer_ctl_name);

    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s", __func__,
              cb_mixer_ctl_name);
        ret = -EINVAL;
        goto done;
    }

    if (is_adm_event)
        ret = snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
            "COPP Event Cmd");
    else
       ret = snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
            "ADSP Stream Cmd %d", config->pcm_device_id);

    if (ret < 0) {
        ALOGE("%s: snprintf failed",__func__);
        ret = -EINVAL;
        goto done;
    }

    ctl = mixer_get_ctl_by_name(adsp_hdlr_inst->mixer, mixer_ctl_name);

    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s", __func__,
              mixer_ctl_name);
        ret = -EINVAL;
        goto done;
    }

    ALOGD("%s: event = %d, payload_length %d", __func__, param->event_type, param->payload_length);

    /* copy event_type, payload size and payload */
    memcpy(payload, &param->event_type,
                    sizeof(param->event_type));
    memcpy(payload + sizeof(param->event_type), &param->payload_length,
                    sizeof(param->payload_length));
    memcpy(payload + sizeof(param->event_type) + sizeof(param->payload_length),
           param->payload, param->payload_length);
    ret = mixer_ctl_set_array(ctl, payload, (sizeof(param->event_type) +
                               sizeof(param->payload_length) + param->payload_length));

    if (ret < 0) {
        ALOGE("%s: Could not set ctl for mixer cmd - %s, ret %d", __func__,
              mixer_ctl_name, ret);
        goto done;
    }

    if (list_empty(&adsp_hdlr_inst->event_list)) {
        pthread_mutex_init(&adsp_hdlr_inst->event_wait_lock,
                           (const pthread_mutexattr_t *) NULL);
        pthread_mutex_init(&adsp_hdlr_inst->event_callback_lock,
                           (const pthread_mutexattr_t *) NULL);
        pthread_mutex_init(&adsp_hdlr_inst->event_list_lock,
                           (const pthread_mutexattr_t *) NULL);

        /* create event threads during first event registration */
        pthread_mutex_lock(&adsp_hdlr_inst->event_wait_lock);

        if (!adsp_hdlr_inst->event_wait_thread_active)
            create_event_wait_thread(adsp_hdlr_inst);

        pthread_mutex_unlock(&adsp_hdlr_inst->event_wait_lock);
        pthread_mutex_lock(&adsp_hdlr_inst->event_callback_lock);

        if (!adsp_hdlr_inst->event_callback_thread_active)
            create_event_callback_thread(adsp_hdlr_inst);

        pthread_mutex_unlock(&adsp_hdlr_inst->event_callback_lock);
        send_cmd_event_wait_thread(adsp_hdlr_inst, EVENT_CMD_WAIT);
    }

    event_info = (struct adsp_hdlr_event_info *) calloc(1,
                                   sizeof(struct adsp_hdlr_event_info));
    if (event_info == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    event_info->event_type = param->event_type;
    event_info->cb = cb;
    event_info->cookie = cookie;
    event_info->stream_handle = stream_data;
    strlcpy(event_info->mixer_ctl_name, mixer_ctl_name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN);
    strlcpy(event_info->cb_mixer_ctl_name, cb_mixer_ctl_name, SNDRV_CTL_ELEM_ID_NAME_MAXLEN);
    pthread_mutex_lock(&adsp_hdlr_inst->event_list_lock);
    list_add_tail(&adsp_hdlr_inst->event_list, &event_info->list);
    ALOGD("%s: event_info type %d added from the list", __func__, event_info->event_type);
    pthread_mutex_unlock(&adsp_hdlr_inst->event_list_lock);

done:
    return ret;
}

int audio_extn_adsp_hdlr_stream_set_param(void *handle,
                    adsp_hdlr_cmd_t cmd,
                    void *param)
{
    int ret = 0;

    if (handle == NULL) {
        ALOGE("%s: Invalid handle",__func__);
        return -EINVAL;
    }

    switch (cmd) {
        case ADSP_HDLR_STREAM_CMD_REGISTER_EVENT :
            ret = audio_extn_adsp_hdlr_stream_register_event(handle, param, NULL, NULL, false);
            if (ret)
                ALOGE("%s:adsp_hdlr_stream_register_event failed error %d",
                       __func__, ret);
            break;
        case ADSP_HDLR_STREAM_CMD_DEREGISTER_EVENT:
            ret = audio_extn_adsp_hdlr_stream_deregister_event(handle, param);
            if (ret)
                ALOGE("%s:adsp_hdlr_stream_deregister_event failed error %d",
                       __func__, ret);
            break;
        default:
            ret = -EINVAL;
            ALOGE("%s: Unsupported command %d",__func__, cmd);
    }
    return ret;
}

int audio_extn_adsp_hdlr_stream_set_callback(void *handle,
                    stream_callback_t callback,
                    void *cookie)
{
    int ret = 0;
    struct adsp_hdlr_stream_data *stream_data;

    ALOGV("%s:: handle %p", __func__, handle);

    if (!handle) {
        ALOGE("%s:Invalid handle", __func__);
        ret = -EINVAL;
    } else {
        stream_data = (struct adsp_hdlr_stream_data *)handle;
        stream_data->client_callback = callback;
        stream_data->client_cookie = cookie;
    }
    return ret;
}

int audio_extn_adsp_hdlr_stream_close(void *handle)
{
    int ret = 0;
    struct adsp_hdlr_stream_data *stream_data;

    ALOGV("%s:: handle %p", __func__, handle);

    if (!handle) {
        ALOGE("%s:Invalid handle", __func__);
        ret = -EINVAL;
    } else {
        stream_data = (struct adsp_hdlr_stream_data *)handle;
        ret = audio_extn_adsp_hdlr_stream_deregister_event(stream_data, NULL);
        if (ret)
            ALOGE("%s:adsp_hdlr_stream_deregister_event failed error %d",
                  __func__, ret);
        free(stream_data);
        stream_data = NULL;
    }
    return ret;
}

int audio_extn_adsp_hdlr_stream_open(void **handle,
                struct adsp_hdlr_stream_cfg *config)
{

    int ret = 0;
    struct adsp_hdlr_stream_data *stream_data;

    if (!adsp_hdlr_inst) {
        ALOGE("%s: Not Inited", __func__);
        return -ENODEV;;
    }

    if ((!config) || (config->type != PCM_PLAYBACK)) {
        ALOGE("%s: Invalid config param", __func__);
        return -EINVAL;
    }

    ALOGV("%s::pcm_device_id %d, flags %x type %d ", __func__,
          config->pcm_device_id, config->flags, config->type);

    *handle = NULL;

    stream_data = (struct adsp_hdlr_stream_data *) calloc(1,
                                   sizeof(struct adsp_hdlr_stream_data));
    if (stream_data == NULL) {
        ret = -ENOMEM;
    }
    stream_data->config = *config;
    *handle = (void **)stream_data;

    return ret;
}

int audio_extn_adsp_hdlr_init(struct mixer *mixer)
{
    ALOGV("%s", __func__);

    if (!mixer) {
        ALOGE("%s: invalid mixer", __func__);
        return -EINVAL;
    }

    if (adsp_hdlr_inst) {
        ALOGD("%s: Already initialized", __func__);
        return 0;
    }
    adsp_hdlr_inst = (struct adsp_hdlr_inst *)calloc(1,
                                  sizeof(struct adsp_hdlr_inst));
    if (!adsp_hdlr_inst) {
        ALOGE("%s: calloc failed for adsp_hdlr_inst", __func__);
        return -EINVAL;
    }
    adsp_hdlr_inst->mixer = mixer;
    list_init(&adsp_hdlr_inst->event_list);

    return 0;
}

int audio_extn_adsp_hdlr_deinit(void)
{
    if (adsp_hdlr_inst) {
        free(adsp_hdlr_inst);
        adsp_hdlr_inst = NULL;
    } else {
        ALOGD("%s: Already Deinitialized", __func__);
    }
    return 0;
}

