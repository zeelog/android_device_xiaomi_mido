/*
* Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "qahw_api"
#define LOG_NDEBUG 0
#define LOG_NDDEBUG 0

#include <utils/Errors.h>
#include <dlfcn.h>
#include <utils/Log.h>
#include <stdlib.h>
#include <cutils/list.h>
#include <assert.h>
#include <string.h>

#include <hardware/audio.h>
#include <cutils/properties.h>
#include "qahw_api.h"
#include "qahw.h"
#include <errno.h>

#ifndef ANDROID
#define strlcpy g_strlcpy
#define strlcat g_strlcat
#endif

#if QAHW_V1
#define QAHW_DEV_ROUTE_LENGTH 15
#define QAHW_KV_PAIR_LENGTH 255
#define QAHW_NUM_OF_ROUTINGS 4
#define QAHW_NUM_OF_SESSIONS 4
#define QAHW_NAMES_PER_SESSION 2
#define QAHW_SESSION_NAME_MAX_LEN 255
#define QAHW_MAX_INT_STRING 12
#define QAHW_NUM_OF_MUTE_TYPES 4

#define MAX_NUM_DEVICES 10

typedef struct {
    qahw_stream_handle_t *out_stream;
    qahw_stream_handle_t *in_stream;
    qahw_module_handle_t *hw_module;
    uint32_t num_of_devices;
    audio_devices_t devices[MAX_NUM_DEVICES];
    qahw_stream_direction dir;
    qahw_audio_stream_type type;
    struct qahw_volume_data vol;
    struct qahw_mute_data out_mute;
    struct qahw_mute_data in_mute;
} qahw_api_stream_t;

/* Array to store sound devices */
static const char * const stream_name_map[QAHW_AUDIO_STREAM_TYPE_MAX] = {
    [QAHW_STREAM_TYPE_INVALID] = "",
    [QAHW_AUDIO_PLAYBACK_LOW_LATENCY]= "playback-low-latency",
    [QAHW_AUDIO_PLAYBACK_DEEP_BUFFER]= "playback-deep-buffer",
    [QAHW_AUDIO_PLAYBACK_COMPRESSED]= "playback-compressed",
    [QAHW_AUDIO_PLAYBACK_VOIP]= "playback-voip",
    [QAHW_AUDIO_PLAYBACK_VOICE_CALL_MUSIC]= "playback-in-call-music",
    [QAHW_AUDIO_CAPTURE_LOW_LATENCY]= "capture-low-latency",
    [QAHW_AUDIO_CAPTURE_DEEP_BUFFER]= "capture-deep-buffer",
    [QAHW_AUDIO_CAPTURE_COMPRESSED]= "capture-compressed",
    [QAHW_AUDIO_CAPTURE_RAW]= "capture-raw",
    [QAHW_AUDIO_CAPTURE_VOIP]= "capture-voip",
    [QAHW_AUDIO_CAPTURE_VOICE_ACTIVATION]= "capture-voice-activation",
    [QAHW_AUDIO_CAPTURE_VOICE_CALL_RX]= "capture-voice-rx",
    [QAHW_AUDIO_CAPTURE_VOICE_CALL_TX]= "capture-voice-tx",
    [QAHW_AUDIO_CAPTURE_VOICE_CALL_RX_TX]= "capture-voice-rx-tx",
    [QAHW_VOICE_CALL]= "voice-call",
    [QAHW_AUDIO_TRANSCODE]= "audio-transcode",
    [QAHW_AUDIO_HOST_PCM_TX]= "host-pcm-tx",
    [QAHW_AUDIO_HOST_PCM_RX]= "host-pcm-rx",
    [QAHW_AUDIO_HOST_PCM_TX_RX]= "host-pcm-tx-rx",
};

static const char * const tty_mode_map[QAHW_TTY_MODE_MAX] = {
    [QAHW_TTY_MODE_OFF] = "tty_mode=tty_off",
    [QAHW_TTY_MODE_FULL] = "tty_mode=tty_full",
    [QAHW_TTY_MODE_VCO] = "tty_mode=tty_vco",
    [QAHW_TTY_MODE_HCO] = "tty_mode=tty_hco",
};

typedef struct {
    char sess_id[QAHW_NAMES_PER_SESSION][QAHW_SESSION_NAME_MAX_LEN + 1];
    char sess_id_call_state[QAHW_KV_PAIR_LENGTH];
} session_info_t;

static session_info_t session_info[QAHW_NUM_OF_SESSIONS] = {
    {{"default mmodevoice1", "11C05000"}, "vsid=297816064;call_state=2"},
    {{"default mmodevoice2", "11DC5000"}, "vsid=299651072;call_state=2"},
    {{"default modem voice", "10C01000"}, "vsid=281022464;call_state=2"},
    {{"default volte voice", "10C02000"}, "vsid=281026560;call_state=2"}
};

typedef struct {
    struct qahw_mute_data mute;
    char mute_state[QAHW_KV_PAIR_LENGTH];
} qahw_mute_info_t;

static qahw_mute_info_t mute_info[QAHW_NUM_OF_MUTE_TYPES] = {
    {{1, QAHW_STREAM_INPUT}, "device_mute=true;direction=tx" },
    {{0, QAHW_STREAM_INPUT}, "device_mute=false;direction=tx" },
    {{1, QAHW_STREAM_OUTPUT}, "device_mute=true;direction=rx" },
    {{0, QAHW_STREAM_OUTPUT}, "device_mute=false;direction=rx" },
};
#endif

#if QTI_AUDIO_SERVER_ENABLED
#include <mm-audio/qti-audio-server/qti_audio_server.h>
#include <mm-audio/qti-audio-server/qti_audio_server_client.h>

using namespace audiohal;
extern struct listnode stream_list;
extern pthread_mutex_t list_lock;

/* Flag to indicate if QAS is enabled or not */
bool g_binder_enabled = false;
/* QTI audio server handle */
sp<Iqti_audio_server> g_qas = NULL;
/* Handle for client context*/
void* g_ctxt = NULL;
/* Death notification handle */
sp<death_notifier> g_death_notifier = NULL;
/* Client callback handle */
audio_error_callback g_audio_err_cb = NULL;
/* Flag to indicate qas status */
bool g_qas_died = false;
/* Count how many times hal is loaded */
static unsigned int g_qas_load_count = 0;
/* Store HAL handle */
qahw_module_handle_t *g_qas_handle = NULL;

inline int qas_status(sp<Iqti_audio_server> server)
{
    if (server == 0) {
        ALOGE("%d:%s: invalid HAL handle",__LINE__, __func__);
        return -1;
    }
    return 1;
}

void death_notifier::binderDied(const wp<IBinder>& who)
{
    struct listnode *node = NULL;
    p_stream_handle *handle = NULL;

    if (g_audio_err_cb) {
        ALOGD("%s %d", __func__, __LINE__);
        g_audio_err_cb(g_ctxt);
    }
    g_qas_died = true;

    pthread_mutex_lock(&list_lock);
    list_for_each(node, &stream_list) {
        handle = node_to_item(node, p_stream_handle, list);
         if (handle != NULL) {
            sh_mem_data *shmem_data = handle->shmem_data;
            ALOGD("%s: %d: signal to unblock any wait conditions", __func__, __LINE__);
            pthread_cond_signal(&shmem_data->c_cond);
            shmem_data->status = 0;
        }
    }
    pthread_mutex_unlock(&list_lock);

}

void qahw_register_qas_death_notify_cb(audio_error_callback cb, void* context)
{
    ALOGD("%s %d", __func__, __LINE__);
    g_audio_err_cb = cb;
    g_ctxt = context;
}

death_notifier::death_notifier()
{
    ALOGV("%s %d", __func__, __LINE__);
    sp<ProcessState> proc(ProcessState::self());
    proc->startThreadPool();
}

sp<Iqti_audio_server> get_qti_audio_server() {
    sp<IServiceManager> sm;
    sp<IBinder> binder;
    int retry_cnt = 5;

    if (g_qas == 0) {
        sm = defaultServiceManager();
        if (sm != NULL) {
            do {
                binder = sm->getService(String16(QTI_AUDIO_SERVER));
                if (binder != 0)
                    break;
                else
                    ALOGE("%d:%s: get qas service failed",__LINE__, __func__);

                 ALOGW("qti_audio_server not published, waiting...");
                usleep(500000);
            } while (--retry_cnt);
        } else {
            ALOGE("%d:%s: defaultServiceManager failed",__LINE__, __func__);
        }
        if (binder == NULL)
            return NULL;

        if (g_death_notifier == NULL) {
            g_death_notifier = new death_notifier();
            if (g_death_notifier == NULL) {
                ALOGE("%d: %s() unable to allocate death notifier", __LINE__, __func__);
                return NULL;
            }
        }
        binder->linkToDeath(g_death_notifier);
        g_qas = interface_cast<Iqti_audio_server>(binder);
        assert(g_qas != 0);
    }
    return g_qas;
}

uint32_t qahw_out_get_sample_rate(const qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_get_sample_rate(out_handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_get_sample_rate_l(out_handle);
    }
}

int qahw_out_set_sample_rate(qahw_stream_handle_t *out_handle, uint32_t rate)
{
    ALOGV("%d:%s %d",__LINE__, __func__, rate);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_set_sample_rate(out_handle, rate);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_set_sample_rate_l(out_handle, rate);
    }
}

size_t qahw_out_get_buffer_size(const qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_get_buffer_size(out_handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_get_buffer_size_l(out_handle);
    }
}

audio_channel_mask_t qahw_out_get_channels(const qahw_stream_handle_t
                                              *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return (audio_channel_mask_t)(-ENODEV);
            return qas->qahw_out_get_channels(out_handle);
        } else {
            return (audio_channel_mask_t)(-ENODEV);
        }
    } else {
        return qahw_out_get_channels_l(out_handle);
    }
}

audio_format_t qahw_out_get_format(const qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return AUDIO_FORMAT_INVALID;
            return qas->qahw_out_get_format(out_handle);
        } else {
            return AUDIO_FORMAT_INVALID;;
        }
    } else {
        return qahw_out_get_format_l(out_handle);
    }
}

int qahw_out_standby(qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_standby(out_handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_standby_l(out_handle);
    }
}

int qahw_out_set_parameters(qahw_stream_handle_t *out_handle,
                                const char *kv_pairs)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_set_parameters(out_handle, kv_pairs);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_set_parameters_l(out_handle, kv_pairs);
    }
}

char *qahw_out_get_parameters(const qahw_stream_handle_t *out_handle,
                                 const char *keys)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return NULL;
            return qas->qahw_out_get_parameters(out_handle, keys);
        } else {
            return NULL;
        }
    } else {
        return qahw_out_get_parameters_l(out_handle, keys);
    }
}

int qahw_out_set_param_data(qahw_stream_handle_t *out_handle,
                            qahw_param_id param_id,
                            qahw_param_payload *payload)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_set_param_data(out_handle, param_id, payload);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_set_param_data_l(out_handle, param_id, payload);
    }
}

int qahw_out_get_param_data(qahw_stream_handle_t *out_handle,
                            qahw_param_id param_id,
                            qahw_param_payload *payload)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_get_param_data(out_handle, param_id, payload);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_get_param_data_l(out_handle, param_id, payload);
    }
}

uint32_t qahw_out_get_latency(const qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_get_latency(out_handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_get_latency_l(out_handle);
    }
}

int qahw_out_set_volume(qahw_stream_handle_t *out_handle, float left, float right)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_set_volume(out_handle, left, right);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_set_volume_l(out_handle, left, right);
    }
}

ssize_t qahw_out_write(qahw_stream_handle_t *out_handle,
                        qahw_out_buffer_t *out_buf)
{
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_write(out_handle, out_buf);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_write_l(out_handle, out_buf);
    }
}

int qahw_out_get_render_position(const qahw_stream_handle_t *out_handle,
                                 uint32_t *dsp_frames)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_get_render_position(out_handle, dsp_frames);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_get_render_position_l(out_handle, dsp_frames);
    }
}

int qahw_out_set_callback(qahw_stream_handle_t *out_handle,
                          qahw_stream_callback_t callback,
                          void *cookie)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_set_callback(out_handle, callback, cookie);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_set_callback_l(out_handle, callback, cookie);
    }
}

int qahw_out_pause(qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_pause(out_handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_pause_l(out_handle);
    }
}

int qahw_out_resume(qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_resume(out_handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_resume_l(out_handle);
    }
}

int qahw_out_drain(qahw_stream_handle_t *out_handle, qahw_drain_type_t type )
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_drain(out_handle, type);
        } else {
            return -EINVAL;
        }
    } else {
        return qahw_out_drain_l(out_handle, type);
    }
}

int qahw_out_flush(qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_flush(out_handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_flush_l(out_handle);
    }
}

int qahw_out_get_presentation_position(const qahw_stream_handle_t *out_handle,
                           uint64_t *frames, struct timespec *timestamp)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_out_get_presentation_position(out_handle,
                                                 frames, timestamp);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_out_get_presentation_position_l(out_handle,
                                         frames, timestamp);
    }
}

uint32_t qahw_in_get_sample_rate(const qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_in_get_sample_rate(in_handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_in_get_sample_rate_l(in_handle);
    }
}

int qahw_in_set_sample_rate(qahw_stream_handle_t *in_handle, uint32_t rate)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_in_set_sample_rate(in_handle, rate);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_in_set_sample_rate_l(in_handle, rate);
    }
}

size_t qahw_in_get_buffer_size(const qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_in_get_buffer_size(in_handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_in_get_buffer_size_l(in_handle);
    }
}

audio_channel_mask_t qahw_in_get_channels(const qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_in_get_channels(in_handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_in_get_channels_l(in_handle);
    }
}

audio_format_t qahw_in_get_format(const qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return (audio_format_t)(-ENODEV);
            return qas->qahw_in_get_format(in_handle);
        } else {
            return (audio_format_t)-ENODEV;
        }
    } else {
        return qahw_in_get_format_l(in_handle);
    }
}

int qahw_in_set_format(qahw_stream_handle_t *in_handle, audio_format_t format)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_in_set_format(in_handle, format);
        } else {
            return (audio_format_t)-ENODEV;
        }
    } else {
        return qahw_in_set_format_l(in_handle, format);
    }
}

int qahw_in_standby(qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_in_standby(in_handle);
        } else {
            return -EINVAL;
        }
    } else {
        return qahw_in_standby_l(in_handle);
    }
}

int qahw_in_set_parameters(qahw_stream_handle_t *in_handle, const char *kv_pairs)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_in_set_parameters(in_handle, kv_pairs);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_in_set_parameters_l(in_handle, kv_pairs);
    }
}

char* qahw_in_get_parameters(const qahw_stream_handle_t *in_handle,
                              const char *keys)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return NULL;
            return qas->qahw_in_get_parameters(in_handle, keys);
        } else {
            return NULL;
        }
    } else {
        return qahw_in_get_parameters_l(in_handle, keys);
    }
}

ssize_t qahw_in_read(qahw_stream_handle_t *in_handle,
                     qahw_in_buffer_t *in_buf)
{
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_in_read(in_handle, in_buf);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_in_read_l(in_handle, in_buf);
    }
}

int qahw_in_stop(qahw_stream_handle_t *in_handle)
{
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_in_stop(in_handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_in_stop_l(in_handle);
    }
}

uint32_t qahw_in_get_input_frames_lost(qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_in_get_input_frames_lost(in_handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_in_get_input_frames_lost_l(in_handle);
    }
}

int qahw_in_get_capture_position(const qahw_stream_handle_t *in_handle,
                                 int64_t *frames, int64_t *time)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_in_get_capture_position(in_handle, frames, time);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_in_get_capture_position_l(in_handle, frames, time);
    }
}

int qahw_init_check(const qahw_module_handle_t *hw_module)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_init_check(hw_module);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_init_check_l(hw_module);
    }
}

int qahw_set_voice_volume(qahw_module_handle_t *hw_module, float volume)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_set_voice_volume(hw_module, volume);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_set_voice_volume_l(hw_module, volume);
    }
}

int qahw_set_mode(qahw_module_handle_t *hw_module, audio_mode_t mode)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_set_mode(hw_module, mode);;
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_set_mode_l(hw_module, mode);
    }
}

int qahw_set_mic_mute(qahw_module_handle_t *hw_module, bool state)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_set_mic_mute(hw_module, state);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_set_mic_mute_l(hw_module, state);
    }
}

int qahw_get_mic_mute(qahw_module_handle_t *hw_module, bool *state)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_get_mic_mute(hw_module, state);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_get_mic_mute_l(hw_module, state);
    }
}

int qahw_set_parameters(qahw_module_handle_t *hw_module, const char *kv_pairs)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_set_parameters(hw_module, kv_pairs);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_set_parameters_l(hw_module, kv_pairs);
    }
}

char* qahw_get_parameters(const qahw_module_handle_t *hw_module,
                           const char *keys)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return NULL;
            return qas->qahw_get_parameters(hw_module, keys);;
        } else {
            return NULL;
        }
    } else {
        return qahw_get_parameters_l(hw_module, keys);
    }
}

int qahw_get_param_data(const qahw_module_handle_t *hw_module,
                        qahw_param_id param_id,
                        qahw_param_payload *payload)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_get_param_data(hw_module, param_id, payload);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_get_param_data_l(hw_module, param_id, payload);
    }
}

int qahw_set_param_data(const qahw_module_handle_t *hw_module,
                        qahw_param_id param_id,
                        qahw_param_payload *payload)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_set_param_data(hw_module, param_id, payload);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_set_param_data_l(hw_module, param_id, payload);
    }
}

int qahw_create_audio_patch(qahw_module_handle_t *hw_module,
                        unsigned int num_sources,
                        const struct audio_port_config *sources,
                        unsigned int num_sinks,
                        const struct audio_port_config *sinks,
                        audio_patch_handle_t *handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_create_audio_patch(hw_module, num_sources,
                                         sources, num_sinks, sinks,
                                         handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_create_audio_patch_l(hw_module, num_sources,
                                         sources, num_sinks, sinks,
                                         handle);
    }
}

int qahw_release_audio_patch(qahw_module_handle_t *hw_module,
                        audio_patch_handle_t handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_release_audio_patch(hw_module, handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_release_audio_patch_l(hw_module, handle);
    }
}

int qahw_loopback_set_param_data(qahw_module_handle_t *hw_module __unused,
                                 audio_patch_handle_t handle __unused,
                                 qahw_loopback_param_id param_id __unused,
                                 qahw_loopback_param_payload *payload __unused)
{
    ALOGD("%d:%s", __LINE__, __func__);
    return -ENOSYS;
}

int qahw_get_audio_port(qahw_module_handle_t *hw_module,
                      struct audio_port *port)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_get_audio_port(hw_module, port);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_get_audio_port_l(hw_module, port);
    }
}

int qahw_set_audio_port_config(qahw_module_handle_t *hw_module,
                     const struct audio_port_config *config)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_set_audio_port_config(hw_module, config);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_set_audio_port_config_l(hw_module, config);
    }
}

size_t qahw_get_input_buffer_size(const qahw_module_handle_t *hw_module,
                                  const struct audio_config *config)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_get_input_buffer_size(hw_module, config);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_get_input_buffer_size_l(hw_module, config);
    }
}

int qahw_open_output_stream(qahw_module_handle_t *hw_module,
                            audio_io_handle_t handle,
                            audio_devices_t devices,
                            audio_output_flags_t flags,
                            struct audio_config *config,
                            qahw_stream_handle_t **out_handle,
                            const char *address)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_open_output_stream(hw_module, handle, devices,
                                                 flags, config, out_handle,
                                                 address);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_open_output_stream_l(hw_module, handle, devices,
                                           flags, config, out_handle,
                                           address);
    }
}

int qahw_close_output_stream(qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    int status;
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_close_output_stream(out_handle);
        } else {
            p_stream_handle *handle = NULL;
            struct listnode *node = NULL;
            struct listnode *tempnode = NULL;
            pthread_mutex_lock(&list_lock);
            list_for_each_safe(node, tempnode, &stream_list) {
                handle = node_to_item(node, p_stream_handle, list);
                p_stream_handle *p_stream = (p_stream_handle *)out_handle;
                if (handle != NULL && handle == p_stream) {
                    sh_mem_data *shmem_data = handle->shmem_data;
                    ALOGD("%s %d: clear memory of handle %p &handle %p", __func__, __LINE__, handle, &handle);
                    handle->sh_mem_dealer.clear();
                    handle->sh_mem_handle.clear();
                    list_remove(node);
                    free(node_to_item(node, p_stream_handle, list));
                }
            }
            pthread_mutex_unlock(&list_lock);
            return -ENODEV;
        }
    } else {
        return qahw_close_output_stream_l(out_handle);
    }
}

int qahw_open_input_stream(qahw_module_handle_t *hw_module,
                           audio_io_handle_t handle,
                           audio_devices_t devices,
                           struct audio_config *config,
                           qahw_stream_handle_t **in_handle,
                           audio_input_flags_t flags,
                           const char *address,
                           audio_source_t source)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_open_input_stream(hw_module, handle, devices,
                                           config, in_handle, flags,
                                           address, source);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_open_input_stream_l(hw_module, handle, devices,
                                       config, in_handle, flags,
                                       address, source);
    }
}

int qahw_close_input_stream(qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_close_input_stream(in_handle);
        } else {
            p_stream_handle *handle = NULL;
            struct listnode *node = NULL;
            struct listnode *tempnode = NULL;
            pthread_mutex_lock(&list_lock);
            list_for_each_safe(node, tempnode, &stream_list) {
                ALOGD("%s %d", __func__, __LINE__);
                handle = node_to_item(node, p_stream_handle, list);
                p_stream_handle *p_stream = (p_stream_handle *)in_handle;
                if (handle != NULL && handle == p_stream) {
                    sh_mem_data *shmem_data = handle->shmem_data;
                    ALOGV("%s %d: clear memory of handle %p", __func__, __LINE__, handle);
                    handle->sh_mem_dealer.clear();
                    handle->sh_mem_handle.clear();
                    list_remove(node);
                    free(node_to_item(node, p_stream_handle, list));
                }
            }
            pthread_mutex_unlock(&list_lock);
            return -EINVAL;
        }
    } else {
        return qahw_close_input_stream_l(in_handle);
    }
}

int qahw_get_version()
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_get_version();
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_get_version_l();
    }
}

int qahw_unload_module(qahw_module_handle_t *hw_module)
{
    int rc = -EINVAL;
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died && ((g_qas_load_count > 0) && (--g_qas_load_count == 0))) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            pthread_mutex_destroy(&list_lock);
            rc = qas->qahw_unload_module(hw_module);
            if (g_death_notifier != NULL) {
                IInterface::asBinder(qas)->unlinkToDeath(g_death_notifier);
                g_death_notifier.clear();
            }
            g_qas = NULL;
            return rc;
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_unload_module_l(hw_module);
    }
}

qahw_module_handle_t *qahw_load_module(const char *hw_module_id)
{
    char value[PROPERTY_VALUE_MAX];

    ALOGV("%d:%s",__LINE__, __func__);
    g_binder_enabled = property_get_bool("persist.vendor.audio.qas.enabled", false);
    ALOGV("%d:%s: g_binder_enabled %d",__LINE__, __func__, g_binder_enabled);
    if (g_binder_enabled) {
        sp<Iqti_audio_server> qas = get_qti_audio_server();
        if (qas_status(qas) == -1)
            return (void*)(-ENODEV);
        g_qas_handle = qas->qahw_load_module(hw_module_id);
        if (g_qas_handle == NULL) {
            ALOGE("%s: HAL loading failed", __func__);
        } else if (g_qas_load_count == 0) {
            g_qas_load_count++;
            g_qas_died = false;
            pthread_mutex_init(&list_lock, (const pthread_mutexattr_t *) NULL);
            list_init(&stream_list);
            ALOGV("%s %d: stream_list %p", __func__, __LINE__, stream_list);
        } else {
            g_qas_load_count++;
            ALOGD("%s: returning existing instance of hal", __func__);
        }
    } else {
        g_qas_handle = qahw_load_module_l(hw_module_id);
    }
    return g_qas_handle;
}

/* Audio effects API */
qahw_effect_lib_handle_t qahw_effect_load_library(const char *lib_name)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return NULL;
            return qas->qahw_effect_load_library(lib_name);
        } else {
            return NULL;
        }
    } else {
        return qahw_effect_load_library_l(lib_name);
    }
}

int32_t qahw_effect_unload_library(qahw_effect_lib_handle_t handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_effect_unload_library(handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_effect_unload_library_l(handle);
    }
}

int32_t qahw_effect_create(qahw_effect_lib_handle_t handle,
                           const qahw_effect_uuid_t *uuid,
                           int32_t io_handle,
                           qahw_effect_handle_t *effect_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_effect_create(handle, uuid, io_handle, effect_handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_effect_create_l(handle, uuid, io_handle, effect_handle);
    }
}

int32_t qahw_effect_release(qahw_effect_lib_handle_t handle,
                            qahw_effect_handle_t effect_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_effect_release(handle, effect_handle);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_effect_release_l(handle, effect_handle);
    }
}

int32_t qahw_effect_get_descriptor(qahw_effect_lib_handle_t handle,
                                   const qahw_effect_uuid_t *uuid,
                                   qahw_effect_descriptor_t *effect_desc)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_effect_get_descriptor(handle, uuid, effect_desc);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_effect_get_descriptor_l(handle, uuid, effect_desc);
    }
}

int32_t qahw_effect_get_version()
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_effect_get_version();
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_effect_get_version_l();
    }
}

int32_t qahw_effect_process(qahw_effect_handle_t self,
                            qahw_audio_buffer_t *in_buffer,
                            qahw_audio_buffer_t *out_buffer)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_effect_process(self, in_buffer, out_buffer);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_effect_process_l(self, in_buffer, out_buffer);
    }
}

int32_t qahw_effect_command(qahw_effect_handle_t self,
                            uint32_t cmd_code,
                            uint32_t cmd_size,
                            void *cmd_data,
                            uint32_t *reply_size,
                            void *reply_data)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_effect_command(self, cmd_code, cmd_size, cmd_data,
                                            reply_size, reply_data);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_effect_command_l(self, cmd_code, cmd_size, cmd_data,
                                     reply_size, reply_data);
    }
}

int32_t qahw_effect_process_reverse(qahw_effect_handle_t self,
                                    qahw_audio_buffer_t *in_buffer,
                                    qahw_audio_buffer_t *out_buffer)
{
    ALOGV("%d:%s",__LINE__, __func__);
    if (g_binder_enabled) {
        if (!g_qas_died) {
            sp<Iqti_audio_server> qas = get_qti_audio_server();
            if (qas_status(qas) == -1)
                return -ENODEV;
            return qas->qahw_effect_process_reverse(self, in_buffer, out_buffer);
        } else {
            return -ENODEV;
        }
    } else {
        return qahw_effect_process_reverse_l(self, in_buffer, out_buffer);
    }
}

#else
void qahw_register_qas_death_notify_cb(audio_error_callback cb __unused, void* context __unused)
{
}

uint32_t qahw_out_get_sample_rate(const qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_get_sample_rate_l(out_handle);
}

int qahw_out_set_sample_rate(qahw_stream_handle_t *out_handle, uint32_t rate)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_set_sample_rate_l(out_handle, rate);
}

size_t qahw_out_get_buffer_size(const qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_get_buffer_size_l(out_handle);
}

audio_channel_mask_t qahw_out_get_channels(const qahw_stream_handle_t
                                              *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_get_channels_l(out_handle);
}

audio_format_t qahw_out_get_format(const qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_get_format_l(out_handle);
}

int qahw_out_standby(qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_standby_l(out_handle);
}

int qahw_out_set_parameters(qahw_stream_handle_t *out_handle,
                                const char *kv_pairs)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_set_parameters_l(out_handle, kv_pairs);
}

char *qahw_out_get_parameters(const qahw_stream_handle_t *out_handle,
                                 const char *keys)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_get_parameters_l(out_handle, keys);
}

int qahw_out_set_param_data(qahw_stream_handle_t *out_handle,
                            qahw_param_id param_id,
                            qahw_param_payload *payload)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_set_param_data_l(out_handle, param_id, payload);
}

int qahw_out_get_param_data(qahw_stream_handle_t *out_handle,
                            qahw_param_id param_id,
                            qahw_param_payload *payload)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_get_param_data_l(out_handle, param_id, payload);
}

uint32_t qahw_out_get_latency(const qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_get_latency_l(out_handle);
}

int qahw_out_set_volume(qahw_stream_handle_t *out_handle, float left, float right)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_set_volume_l(out_handle, left, right);
}

ssize_t qahw_out_write(qahw_stream_handle_t *out_handle,
                        qahw_out_buffer_t *out_buf)
{
    return qahw_out_write_l(out_handle, out_buf);
}

int qahw_out_get_render_position(const qahw_stream_handle_t *out_handle,
                                 uint32_t *dsp_frames)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_get_render_position_l(out_handle, dsp_frames);
}

int qahw_out_set_callback(qahw_stream_handle_t *out_handle,
                          qahw_stream_callback_t callback,
                          void *cookie)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_set_callback_l(out_handle, callback, cookie);
}

int qahw_out_pause(qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_pause_l(out_handle);
}

int qahw_out_resume(qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_resume_l(out_handle);
}

int qahw_out_drain(qahw_stream_handle_t *out_handle, qahw_drain_type_t type )
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_drain_l(out_handle, type);
}

int qahw_out_flush(qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_flush_l(out_handle);
}

int qahw_out_get_presentation_position(const qahw_stream_handle_t *out_handle,
                           uint64_t *frames, struct timespec *timestamp)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_out_get_presentation_position_l(out_handle,
                                     frames, timestamp);
}

uint32_t qahw_in_get_sample_rate(const qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_in_get_sample_rate_l(in_handle);
}

int qahw_in_set_sample_rate(qahw_stream_handle_t *in_handle, uint32_t rate)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_in_set_sample_rate_l(in_handle, rate);
}

size_t qahw_in_get_buffer_size(const qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_in_get_buffer_size_l(in_handle);
}

audio_channel_mask_t qahw_in_get_channels(const qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_in_get_channels_l(in_handle);
}

audio_format_t qahw_in_get_format(const qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_in_get_format_l(in_handle);
}

int qahw_in_set_format(qahw_stream_handle_t *in_handle, audio_format_t format)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_in_set_format_l(in_handle, format);
}

int qahw_in_standby(qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_in_standby_l(in_handle);
}

int qahw_in_set_parameters(qahw_stream_handle_t *in_handle, const char *kv_pairs)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_in_set_parameters_l(in_handle, kv_pairs);
}

char* qahw_in_get_parameters(const qahw_stream_handle_t *in_handle,
                              const char *keys)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_in_get_parameters_l(in_handle, keys);
}

ssize_t qahw_in_read(qahw_stream_handle_t *in_handle,
                     qahw_in_buffer_t *in_buf)
{
    return qahw_in_read_l(in_handle, in_buf);
}

int qahw_in_stop(qahw_stream_handle_t *in_handle)
{
    return qahw_in_stop_l(in_handle);
}

uint32_t qahw_in_get_input_frames_lost(qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_in_get_input_frames_lost_l(in_handle);
}

int qahw_in_get_capture_position(const qahw_stream_handle_t *in_handle,
                                 int64_t *frames, int64_t *time)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_in_get_capture_position_l(in_handle, frames, time);
}

int qahw_init_check(const qahw_module_handle_t *hw_module)
{
    ALOGV("%d:%s start",__LINE__, __func__);
    int rc = qahw_init_check_l(hw_module);
    ALOGV("%d:%s end",__LINE__, __func__);
    return rc;
}

int qahw_set_voice_volume(qahw_module_handle_t *hw_module, float volume)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_set_voice_volume_l(hw_module, volume);
}

int qahw_set_mode(qahw_module_handle_t *hw_module, audio_mode_t mode)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_set_mode_l(hw_module, mode);
}

int qahw_set_mic_mute(qahw_module_handle_t *hw_module, bool state)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_set_mic_mute_l(hw_module, state);
}

int qahw_get_mic_mute(qahw_module_handle_t *hw_module, bool *state)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_get_mic_mute_l(hw_module, state);
}

int qahw_set_parameters(qahw_module_handle_t *hw_module, const char *kv_pairs)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_set_parameters_l(hw_module, kv_pairs);
}

char* qahw_get_parameters(const qahw_module_handle_t *hw_module,
                           const char *keys)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_get_parameters_l(hw_module, keys);
}

int qahw_get_param_data(const qahw_module_handle_t *hw_module,
                        qahw_param_id param_id,
                        qahw_param_payload *payload)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_get_param_data_l(hw_module, param_id, payload);
}

int qahw_set_param_data(const qahw_module_handle_t *hw_module,
                        qahw_param_id param_id,
                        qahw_param_payload *payload)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_set_param_data_l(hw_module, param_id, payload);
}

/* Audio effects API */
qahw_effect_lib_handle_t qahw_effect_load_library(const char *lib_path)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_effect_load_library_l(lib_path);
}

int32_t qahw_effect_unload_library(qahw_effect_lib_handle_t handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_effect_unload_library_l(handle);
}

int32_t qahw_effect_create(qahw_effect_lib_handle_t handle,
                           const qahw_effect_uuid_t *uuid,
                           int32_t io_handle,
                           qahw_effect_handle_t *effect_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_effect_create_l(handle, uuid, io_handle, effect_handle);
}

int32_t qahw_effect_release(qahw_effect_lib_handle_t handle,
                            qahw_effect_handle_t effect_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_effect_release_l(handle, effect_handle);
}

int32_t qahw_effect_get_descriptor(qahw_effect_lib_handle_t handle,
                                   const qahw_effect_uuid_t *uuid,
                                   qahw_effect_descriptor_t *effect_desc)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_effect_get_descriptor_l(handle, uuid, effect_desc);
}

int32_t qahw_effect_get_version()
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_effect_get_version_l();
}

int32_t qahw_effect_process(qahw_effect_handle_t self,
                            qahw_audio_buffer_t *in_buffer,
                            qahw_audio_buffer_t *out_buffer)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_effect_process_l(self, in_buffer, out_buffer);
}

int32_t qahw_effect_command(qahw_effect_handle_t self,
                            uint32_t cmd_code,
                            uint32_t cmd_size,
                            void *cmd_data,
                            uint32_t *reply_size,
                            void *reply_data)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_effect_command_l(self, cmd_code, cmd_size,
                                 cmd_data, reply_size, reply_data);
}

int32_t qahw_effect_process_reverse(qahw_effect_handle_t self,
                                    qahw_audio_buffer_t *in_buffer,
                                    qahw_audio_buffer_t *out_buffer)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_effect_process_reverse_l(self, in_buffer,
                                         out_buffer);
}

int qahw_create_audio_patch(qahw_module_handle_t *hw_module,
                        unsigned int num_sources,
                        const struct audio_port_config *sources,
                        unsigned int num_sinks,
                        const struct audio_port_config *sinks,
                        audio_patch_handle_t *handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_create_audio_patch_l(hw_module, num_sources,
                                     sources, num_sinks, sinks,
                                     handle);
}

int qahw_release_audio_patch(qahw_module_handle_t *hw_module,
                        audio_patch_handle_t handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_release_audio_patch_l(hw_module, handle);
}

int qahw_loopback_set_param_data(qahw_module_handle_t *hw_module,
                                 audio_patch_handle_t handle,
                                 qahw_loopback_param_id param_id,
                                 qahw_loopback_param_payload *payload)
{
    ALOGV("%d:%s\n", __LINE__, __func__);
    return qahw_loopback_set_param_data_l(hw_module, handle, param_id, payload);
}

int qahw_get_audio_port(qahw_module_handle_t *hw_module,
                      struct audio_port *port)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_get_audio_port_l(hw_module, port);
}

int qahw_set_audio_port_config(qahw_module_handle_t *hw_module,
                     const struct audio_port_config *config)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_set_audio_port_config_l(hw_module, config);
}

size_t qahw_get_input_buffer_size(const qahw_module_handle_t *hw_module,
                                  const struct audio_config *config)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_get_input_buffer_size_l(hw_module, config);
}

int qahw_open_output_stream(qahw_module_handle_t *hw_module,
                            audio_io_handle_t handle,
                            audio_devices_t devices,
                            audio_output_flags_t flags,
                            struct audio_config *config,
                            qahw_stream_handle_t **out_handle,
                            const char *address)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_open_output_stream_l(hw_module, handle, devices,
                                       flags, config, out_handle,
                                       address);
}

int qahw_close_output_stream(qahw_stream_handle_t *out_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_close_output_stream_l(out_handle);
}

int qahw_open_input_stream(qahw_module_handle_t *hw_module,
                           audio_io_handle_t handle,
                           audio_devices_t devices,
                           struct audio_config *config,
                           qahw_stream_handle_t **in_handle,
                           audio_input_flags_t flags,
                           const char *address,
                           audio_source_t source)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_open_input_stream_l(hw_module, handle, devices,
                                   config, in_handle, flags,
                                   address, source);
}

int qahw_close_input_stream(qahw_stream_handle_t *in_handle)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_close_input_stream_l(in_handle);
}

int qahw_get_version()
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_get_version_l();
}

int qahw_unload_module(qahw_module_handle_t *hw_module)
{
    ALOGV("%d:%s",__LINE__, __func__);
    return qahw_unload_module_l(hw_module);
}

qahw_module_handle_t *qahw_load_module(const char *hw_module_id)
{
    ALOGV("%d:%s start",__LINE__, __func__);
    qahw_module_handle_t *module = qahw_load_module_l(hw_module_id);
    ALOGV("%d:%s end",__LINE__, __func__);
    return module;
}

#if QAHW_V1
char * qahw_get_session_id(const char* vsid)
{
    int i = 0;
    int j = 0;
    char *ret = "vsid=281022464;call_state=2";

    for(i = 0; i < QAHW_NUM_OF_SESSIONS; i++) {
        for(j = 0; j < QAHW_NAMES_PER_SESSION; j++) {
            if(!strcmp(vsid,session_info[i].sess_id[j])) {
                ret = session_info[i].sess_id_call_state;
                ALOGV("%s: vsid %s\n", __func__, vsid);
                break;
            }
        }
    }
    ALOGV("%s: sess_id_call_state %s\n", __func__, ret);
    return ret;
}

int qahw_add_flags_source(struct qahw_stream_attributes attr,
                          int *flags, audio_source_t *source) {
    int rc = 0;
    /*default source */
    if (source && flags) {
        *source = AUDIO_SOURCE_MIC;
        /*default flag*/
        *flags = 0;
    } else
        return -EINVAL;

    switch (attr.type) {
    case QAHW_AUDIO_PLAYBACK_LOW_LATENCY:
        /*TODO*/
        break;
    case QAHW_AUDIO_PLAYBACK_DEEP_BUFFER:
        *flags = AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
        break;
    case QAHW_AUDIO_PLAYBACK_COMPRESSED:
        *flags = AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD;
        break;
    case QAHW_AUDIO_PLAYBACK_VOIP:
        /*TODO*/
        break;
    case QAHW_AUDIO_PLAYBACK_VOICE_CALL_MUSIC:
        *flags = QAHW_OUTPUT_FLAG_INCALL_MUSIC;
        break;
    case QAHW_AUDIO_CAPTURE_LOW_LATENCY:
        /*TODO*/
        break;
    case QAHW_AUDIO_CAPTURE_DEEP_BUFFER:
        /*TODO*/
        break;
    case QAHW_AUDIO_CAPTURE_COMPRESSED:
        /*TODO*/
        break;
    case QAHW_AUDIO_CAPTURE_RAW:
        /*TODO*/
        break;
    case QAHW_AUDIO_CAPTURE_VOIP:
        break;
    case QAHW_AUDIO_CAPTURE_VOICE_ACTIVATION:
        /*TODO*/
        break;
    case QAHW_AUDIO_CAPTURE_VOICE_CALL_RX:
        *source = AUDIO_SOURCE_VOICE_DOWNLINK;
        break;
    case QAHW_AUDIO_CAPTURE_VOICE_CALL_TX:
        *source = AUDIO_SOURCE_VOICE_UPLINK;
        break;
    case QAHW_AUDIO_CAPTURE_VOICE_CALL_RX_TX:
        /*unsupported */
        break;
    case QAHW_VOICE_CALL:
        *flags = AUDIO_OUTPUT_FLAG_PRIMARY;
        break;
    case QAHW_AUDIO_TRANSCODE:
        /*TODO*/
        break;
    case QAHW_AUDIO_HOST_PCM_TX:
        *flags = QAHW_AUDIO_FLAG_HPCM_TX;
        break;
    case QAHW_AUDIO_HOST_PCM_RX:
        *flags = QAHW_AUDIO_FLAG_HPCM_RX;
        break;
    case QAHW_AUDIO_HOST_PCM_TX_RX:
        /*TODO*/
        break;
    default:
        rc = -EINVAL;
        break;
    }

    return rc;
}

int qahw_stream_open(qahw_module_handle_t *hw_module,
                     struct qahw_stream_attributes attr,
                     uint32_t num_of_devices,
                     qahw_device_t *devices,
                     uint32_t no_of_modifiers,
                     struct qahw_modifier_kv *modifiers,
                     qahw_stream_callback_t cb,
                     void *cookie,
                     qahw_stream_handle_t **stream_handle) {

    ALOGV("%d:%s start",__LINE__, __func__);
    audio_io_handle_t handle = 0x999;
    int rc = -EINVAL;
    const char *address = stream_name_map[attr.type];
    const char *session_id;
    int flags = 0;
    audio_source_t source = AUDIO_SOURCE_MIC;
    qahw_api_stream_t *stream;
    struct qahw_channel_vol *vols;

    /* validate number of devices */
    if (num_of_devices > MAX_NUM_DEVICES)
    {
        ALOGE("%s: invalid number of devices for stream", __func__);
        return rc;
    }
    /* validate direction for voice stream */
    if (attr.type == QAHW_VOICE_CALL &&
        attr.direction != QAHW_STREAM_INPUT_OUTPUT) {
        ALOGE("%s: invalid direction for a voice stream", __func__);
        return rc;
    }
    /* add flag*/
    rc = qahw_add_flags_source(attr, &flags, &source);

    if (rc) {
        ALOGE("%s: invalid type %d", __func__, attr.type);
        return rc;
    }

    stream = (qahw_api_stream_t *)calloc(1, sizeof(qahw_api_stream_t));
    if (!stream) {
        ALOGE("%s: stream allocation failed ", __func__);
        return -ENOMEM;
    }
    vols = (struct qahw_channel_vol *)
            calloc(1, sizeof(struct qahw_channel_vol)*QAHW_CHANNELS_MAX);
    if (!vols) {
        ALOGE("%s: vol allocation failed ", __func__);
        return -ENOMEM;
    }

    memset(stream, 0, sizeof(qahw_api_stream_t));
    memset(vols, 0, sizeof(struct qahw_channel_vol)*QAHW_CHANNELS_MAX);
    stream->dir = attr.direction;
    stream->hw_module = hw_module;
    stream->num_of_devices = num_of_devices;
    memset(&stream->devices[0], 0, sizeof(stream->devices));
    memcpy(&stream->devices[0], devices, num_of_devices);
    stream->type = attr.type;
    stream->vol.vol_pair = vols;
    /* if voice call stream, num_of_channels set to 1 */
    if (attr.type == QAHW_VOICE_CALL)
        stream->vol.num_of_channels = 1;
    else
        stream->vol.num_of_channels = QAHW_CHANNELS_MAX;

    switch (attr.direction) {
    case QAHW_STREAM_INPUT_OUTPUT:
        /*for now only support one stream to one device*/
        if (num_of_devices != 2 && attr.type != QAHW_VOICE_CALL) {
            ALOGE("%s: invalid num of streams %d for dir %d",
                  __func__, num_of_devices, attr.direction);
            return rc;
        }
        rc = qahw_open_output_stream(hw_module, handle, devices[0],
                                     (audio_output_flags_t)flags,
                                     &(attr.attr.shared.config),
                                     &stream->out_stream,
                                     address);
        /*set cb function */
        if (rc)
            rc = qahw_out_set_callback(stream->out_stream, cb, cookie);
        if (rc)
            ALOGE("%s: setting callback failed %d \n", __func__, rc);

        if (attr.type != QAHW_VOICE_CALL) {
            rc = qahw_open_input_stream(hw_module, handle, devices[1],
                                        &(attr.attr.shared.config),
                                        &stream->in_stream,
                                        (audio_input_flags_t)flags,
                                        address,
                                        source);
        }
        break;
    case QAHW_STREAM_OUTPUT:
        if (num_of_devices != 1) {
            ALOGE("%s: invalid num of streams %d for dir %d",
                  __func__, num_of_devices, attr.direction);
            return rc;
        }
        rc = qahw_open_output_stream(hw_module, handle, devices[0],
                                     (audio_output_flags_t)flags,
                                     &(attr.attr.shared.config),
                                     &stream->out_stream,
                                     address);
        /*set cb function */
        if (rc)
            rc = qahw_out_set_callback(stream->out_stream, cb, cookie);
        if (rc)
            ALOGE("%s: setting callback failed %d \n", __func__, rc);
        break;
    case QAHW_STREAM_INPUT:
        if (num_of_devices != 1) {
            ALOGE("%s: invalid num of streams %d for dir %d",
                  __func__, num_of_devices, attr.direction);
            return rc;
        }
        rc = qahw_open_input_stream(hw_module, handle, devices[0],
                                    &(attr.attr.shared.config),
                                    &stream->in_stream,
                                    (audio_input_flags_t)flags,
                                    address,
                                    source);
        break;
    default:
        ALOGE("%s: invalid stream direction %d ", __func__, attr.direction);
        return rc;
    }
    /*set the stream type as the handle add to list*/
    *stream_handle = (qahw_stream_handle_t *)stream;

    /*if voice call set vsid and call state/mode*/
    if (attr.type == QAHW_VOICE_CALL) {
        session_id = qahw_get_session_id(attr.attr.voice.vsid);

        rc = qahw_set_parameters(hw_module, session_id);
        if (rc) {
            ALOGE("%s: setting vsid failed %d \n", __func__, rc);
        }
    }

    if(no_of_modifiers){
        ALOGE("%s: modifiers not currently supported\n", __func__);
    }
    ALOGV("%d:%s end",__LINE__, __func__);
    return rc;
}

int qahw_stream_close(qahw_stream_handle_t *stream_handle) {
    int rc = -EINVAL;
    qahw_stream_direction dir;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;

    ALOGV("%d:%s start",__LINE__, __func__);
    if (stream) {
        switch (stream->dir) {
        case QAHW_STREAM_OUTPUT:
            ALOGV("%s: closing output stream\n", __func__);
            rc = qahw_close_output_stream(stream->out_stream);
            break;
        case QAHW_STREAM_INPUT:
            ALOGV("%s: closing input stream\n", __func__);
            rc = qahw_close_input_stream(stream->in_stream);
            break;
        case QAHW_STREAM_INPUT_OUTPUT:
            rc = qahw_close_output_stream(stream->out_stream);
            if (rc)
                ALOGE("%s: closing output stream failed\n", __func__);
            /*if not voice call close input stream*/
            if (stream->type != QAHW_VOICE_CALL) {
                rc = qahw_close_input_stream(stream->in_stream);
                if (rc)
                    ALOGE("%s: closing output stream failed\n", __func__);
            }
            break;
        default:
            ALOGE("%s: invalid dir close failed\n", __func__);
        }
    } else
        ALOGE("%s: null stream handle\n", __func__);

    free(stream->vol.vol_pair);
    free(stream);
    ALOGV("%d:%s end",__LINE__, __func__);
    return rc;
}

int qahw_stream_start(qahw_stream_handle_t *stream_handle) {
    int rc = -EINVAL;
    qahw_audio_stream_type type;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;

    ALOGV("%d:%s start",__LINE__, __func__);
    if (!stream) {
        ALOGE("%d:%s invalid stream handle", __LINE__, __func__);
        return rc;
    }
    /*set call state and call mode for voice */
    if (stream->type == QAHW_VOICE_CALL) {
        rc = qahw_set_mode(stream->hw_module, AUDIO_MODE_IN_CALL);
    }

    qahw_stream_set_device(stream, stream->num_of_devices, stream->devices);
    memset(&devices[0], 0, sizeof(devices));
    memcpy(&devices[0], &stream->devices[0], stream->num_of_devices);
    qahw_stream_set_device(stream, stream->num_of_devices, &devices[0]);
    ALOGV("%d:%s end",__LINE__, __func__);
    return rc;
}

int qahw_stream_stop(qahw_stream_handle_t *stream_handle) {
    int rc = -EINVAL;
    qahw_audio_stream_type type;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;

    ALOGV("%d:%s start",__LINE__, __func__);
    /*reset call state and call mode for voice */
    if (stream->type == QAHW_VOICE_CALL) {
        rc = qahw_set_parameters(stream->hw_module, "call_state=1");
        rc = qahw_set_mode(stream->hw_module, AUDIO_MODE_NORMAL);
    }
    ALOGV("%d:%s end",__LINE__, __func__);
    return rc;
}

int qahw_stream_set_device(qahw_stream_handle_t *stream_handle,
                    uint32_t num_of_devices,
                    qahw_device_t *devices) {
    int rc = -EINVAL;
    char dev_s[QAHW_MAX_INT_STRING];
    char device_route[QAHW_MAX_INT_STRING];
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;
    bool is_voice = false;

    ALOGV("%d:%s start",__LINE__, __func__);
    strlcpy(device_route, "routing=", QAHW_MAX_INT_STRING);

    if (stream && num_of_devices && devices) {
        if (stream->type == QAHW_VOICE_CALL)
            is_voice = true;

        switch (stream->dir) {
        case QAHW_STREAM_OUTPUT:
            if (num_of_devices != 1 || !devices) {
                ALOGE("%s: invalid device params\n", __func__);
                return rc;
            }

            snprintf(dev_s, QAHW_MAX_INT_STRING, "%d", devices[0]);
            strlcat(device_route, dev_s, QAHW_MAX_INT_STRING);
            rc = qahw_out_set_parameters(stream->out_stream,
                                         device_route);
            break;
        case QAHW_STREAM_INPUT:
            if (num_of_devices != 1 || !devices) {
                ALOGE("%s: invalid device params\n", __func__);
                return rc;
            }

            snprintf(dev_s, QAHW_MAX_INT_STRING, "%d", devices[0]);
            strlcat(device_route, dev_s, QAHW_MAX_INT_STRING);
            rc = qahw_in_set_parameters(stream->in_stream,
                                        device_route);
            break;
        case QAHW_STREAM_INPUT_OUTPUT:
            if (!devices) {
                ALOGE("%s: invalid device params\n", __func__);
                return rc;
            }
            snprintf(dev_s, QAHW_MAX_INT_STRING, "%d", devices[0]);
            strlcat(device_route, dev_s, QAHW_MAX_INT_STRING);
            rc = qahw_out_set_parameters(stream->out_stream,
                                         device_route);
            if (rc)
                ALOGE("%s: failed to set out device\n", __func__);
            /*if not voice set input stream*/
            if (!is_voice) {
                strlcpy(device_route, "routing=", QAHW_MAX_INT_STRING);
                snprintf(dev_s, QAHW_MAX_INT_STRING, "%d", devices[1]);
                strlcat(device_route, dev_s, QAHW_MAX_INT_STRING);
                rc = qahw_in_set_parameters(stream->in_stream,
                                            device_route);
                if (rc)
                    ALOGE("%s: failed to set in device\n", __func__);
            }
            break;
        default:
            ALOGE("%s: invalid dir close failed\n", __func__);
        }
    }

    if (!rc)
    {
        stream->num_of_devices = num_of_devices;
        memset(&stream->devices[0], 0, sizeof(stream->devices));
        memcpy(&stream->devices[0], devices, num_of_devices);
    }

    ALOGV("%d:%s end",__LINE__, __func__);
    return rc;
}

int qahw_stream_get_device(qahw_stream_handle_t *stream_handle, uint32_t *num_of_dev,
                    qahw_device_t **devices) {
    int rc = 0;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;

    ALOGV("%d:%s start",__LINE__, __func__);
    if (stream && num_of_dev && devices) {
        *num_of_dev = stream->num_of_devices;
        *devices = stream->devices;
    } else {
        ALOGE("%s: invalid params\n", __func__);
        rc = -EINVAL;
    }

    ALOGV("%d:%s end",__LINE__, __func__);
    return rc;
}

int qahw_stream_set_volume(qahw_stream_handle_t *stream_handle,
                           struct qahw_volume_data vol_data) {
    int rc = -EINVAL;
    qahw_audio_stream_type type;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;
    float left;
    float right;
    bool l_found = false;
    bool r_found = false;
    int i;

    ALOGV("%d:%s start",__LINE__, __func__);
    if(stream) {
        /*currently max 2 channels is supported */
        if ( vol_data.num_of_channels > QAHW_CHANNELS_MAX) {
           return -ENOTSUP;
        }

        /*set voice call vol*/
        if (stream->type == QAHW_VOICE_CALL &&
            (vol_data.vol_pair && (vol_data.num_of_channels == 1))) {
            ALOGV("%s: calling voice set volume with vol value %f\n",
                  __func__, vol_data.vol_pair[0].vol);
            rc = qahw_set_voice_volume(stream->hw_module,
                                       vol_data.vol_pair[0].vol);
            /* Voice Stream picks up only single channel */
            stream->vol.num_of_channels = vol_data.num_of_channels;
            stream->vol.vol_pair[0] = vol_data.vol_pair[0];
        } /*currently HAL requires 2 channels only */
        else if (vol_data.num_of_channels == QAHW_CHANNELS_MAX &&
                   vol_data.vol_pair) {
            for(i=0; i < vol_data.num_of_channels; i++) {
                if(vol_data.vol_pair[i].channel == QAHW_CHANNEL_L) {
                    left = vol_data.vol_pair[i].vol;
                    l_found = true;
                }
                if(vol_data.vol_pair[i].channel == QAHW_CHANNEL_R) {
                    right = vol_data.vol_pair[i].vol;
                    r_found = true;
                }
            }
            if(l_found && r_found) {
                rc = qahw_out_set_volume(stream->out_stream,
                                         left, right);
                /* Cache volume if applied successfully */
                if (!rc) {
                    for(i=0; i < vol_data.num_of_channels; i++) {
                        stream->vol.vol_pair[i] = vol_data.vol_pair[i];
                    }
                }
            } else
                ALOGE("%s: setting vol requires left and right channel vol\n",
                      __func__);
        } else {
            ALOGE("%s: invalid input \n", __func__);
        }
    } else
        ALOGE("%s: null stream handle\n", __func__);

    ALOGV("%d:%s end",__LINE__, __func__);
    return rc;
}

int qahw_stream_get_volume(qahw_stream_handle_t *stream_handle,
                           struct qahw_volume_data **vol_data) {
    int rc = 0;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;
    ALOGV("%d:%s start",__LINE__, __func__);
    if (stream)
        *vol_data = &stream->vol;
    else
        rc = -EINVAL;

    ALOGV("%d:%s end",__LINE__, __func__);
    return rc;
}

char *qahw_get_device_mute_info(struct qahw_mute_data mute) {
    int i = 0;
    char *ret = NULL;

    for (i=0; i < QAHW_NUM_OF_MUTE_TYPES; i++) {
        if ((mute.enable == mute_info[i].mute.enable) &&
            (mute.direction == mute_info[i].mute.direction)) {
            ret = mute_info[i].mute_state;
            break;
        }
    }
    ALOGV("%s mute_state %s \n", __func__, ret == NULL ? "null" : ret);

    return ret;
}

int qahw_stream_set_mute(qahw_stream_handle_t *stream_handle,
                         struct qahw_mute_data mute_data) {
    int rc = -EINVAL;
    qahw_module_handle_t *hw_module;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;
    char *mute_param;

    ALOGV("%d:%s start",__LINE__, __func__);
    if(stream) {
        mute_param = qahw_get_device_mute_info(mute_data);

        if (mute_param == NULL)
            return rc;

        rc = qahw_set_parameters(stream->hw_module, mute_param);

        if(!rc){
            switch(mute_data.direction) {
                case QAHW_STREAM_INPUT_OUTPUT:
                    stream->out_mute.enable = mute_data.enable;
                    stream->in_mute.enable = mute_data.enable;
                    break;
                case QAHW_STREAM_OUTPUT:
                    stream->out_mute.enable = mute_data.enable;
                    break;
                case QAHW_STREAM_INPUT:
                    stream->in_mute.enable = mute_data.enable;
                    break;
                default:
                    ALOGE("%s: invalid dir mute failed\n", __func__);
                    break;
            }
        }
    }
    ALOGV("%d:%s end",__LINE__, __func__);
    return rc;
}

int qahw_stream_get_mute(qahw_stream_handle_t *stream_handle,
                         struct qahw_mute_data *mute_data) {
    int rc = 0;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;

    ALOGV("%d:%s start",__LINE__, __func__);
    if(stream && mute_data){
            switch(mute_data->direction) {
                case QAHW_STREAM_OUTPUT:
                    mute_data->enable = stream->out_mute.enable;
                    break;
                case QAHW_STREAM_INPUT:
                    mute_data->enable = stream->in_mute.enable;
                    break;
                default:
                    ALOGE("%s: invalid mute dir get failed\n", __func__);
                    rc = -EINVAL;
                    break;
            }
    }

    ALOGV("%d:%s end",__LINE__, __func__);
    return rc;
}

ssize_t qahw_stream_read(qahw_stream_handle_t *stream_handle,
                         qahw_buffer_t *in_buf) {
    ssize_t rc = 0;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;
    qahw_in_buffer_t buff;

    if(in_buf) {
        buff.buffer = in_buf->buffer;
        buff.bytes = in_buf->size;
        buff.offset = in_buf->offset;
        buff.timestamp = in_buf->timestamp;
    }

    if (stream && stream->in_stream) {
        rc = qahw_in_read(stream->in_stream, &buff);
    } else {
        ALOGE("%d:%s input stream invalid, read failed", __LINE__, __func__);
        rc = -ENODEV;
    }
    return rc;
}

ssize_t qahw_stream_write(qahw_stream_handle_t *stream_handle,
                   qahw_buffer_t *out_buf) {
    ssize_t rc = 0;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;
    qahw_out_buffer_t buff;

    if(out_buf) {
        buff.buffer = out_buf->buffer;
        buff.bytes = out_buf->size;
        buff.offset = out_buf->offset;
        buff.timestamp = out_buf->timestamp;
        buff.flags = out_buf->flags;
    }

    if (stream && stream->out_stream) {
        rc = qahw_out_write(stream->out_stream, &buff);
    } else {
        ALOGE("%d:%s out stream invalid, write failed", __LINE__, __func__);
        rc = -ENODEV;
    }
    return rc;
}

int qahw_stream_standby(qahw_stream_handle_t *stream_handle) {
    int rc = -EINVAL;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;
    if (stream) {
        switch (stream->dir) {
        case QAHW_STREAM_OUTPUT:
            if (stream->out_stream)
                rc = qahw_out_standby(stream->out_stream);
            else
                ALOGE("%d:%s out stream invalid, cannot put in standby"
                      , __LINE__, __func__);
            break;
        case QAHW_STREAM_INPUT:
            if (stream->in_stream)
                rc = qahw_in_standby(stream->in_stream);
            else
                ALOGE("%d:%s in stream invalid, cannot put in standby"
                      , __LINE__, __func__);
            break;
        case QAHW_STREAM_INPUT_OUTPUT:
            if (stream->in_stream)
                rc = qahw_in_standby(stream->in_stream);
            else
                ALOGE("%d:%s in stream invalid, cannot put in standby"
                      , __LINE__, __func__);
            if (stream->out_stream)
                rc = qahw_out_standby(stream->out_stream);
            else
                ALOGE("%d:%s out stream invalid, cannot put in standby"
                      , __LINE__, __func__);
            break;
        }
    } else
        ALOGE("%d:%s invalid stream handle, standby failed"
              , __LINE__, __func__);
    return rc;
}

int qahw_stream_pause(qahw_stream_handle_t *stream_handle) {
    int rc = -EINVAL;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;
    if (stream) {
        switch (stream->dir) {
        case QAHW_STREAM_OUTPUT:
            if (stream->out_stream)
                rc = qahw_out_pause(stream->out_stream);
            else
                ALOGE("%d:%s out stream invalid, cannot put in pause"
                      , __LINE__, __func__);
            break;
        case QAHW_STREAM_INPUT:
                ALOGE("%d:%s cannot pause input stream"
                      , __LINE__, __func__);
            break;
        case QAHW_STREAM_INPUT_OUTPUT:
            if (stream->out_stream)
                rc = qahw_out_pause(stream->out_stream);
            else
                ALOGE("%d:%s out stream invalid, cannot put in pause"
                      , __LINE__, __func__);
            break;
        }
    } else
        ALOGE("%d:%s invalid stream handle, pause failed"
              , __LINE__, __func__);
    return rc;
}

int qahw_stream_resume(qahw_stream_handle_t *stream_handle) {
    int rc = -EINVAL;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;
    if (stream) {
        switch (stream->dir) {
        case QAHW_STREAM_OUTPUT:
            if (stream->out_stream)
                rc = qahw_out_resume(stream->out_stream);
            else
                ALOGE("%d:%s out stream invalid, cannot put in resume"
                      , __LINE__, __func__);
            break;
        case QAHW_STREAM_INPUT:
                ALOGE("%d:%s cannot resume input stream"
                      , __LINE__, __func__);
            break;
        case QAHW_STREAM_INPUT_OUTPUT:
            if (stream->out_stream)
                rc = qahw_out_resume(stream->out_stream);
            else
                ALOGE("%d:%s out stream invalid, cannot put in resume"
                      , __LINE__, __func__);
            break;
        }
    } else
        ALOGE("%d:%s invalid stream handle, resume failed"
              , __LINE__, __func__);
    return rc;
}

int qahw_stream_flush(qahw_stream_handle_t *stream_handle) {
    int rc = -EINVAL;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;
    if (stream) {
        switch (stream->dir) {
        case QAHW_STREAM_OUTPUT:
            if (stream->out_stream)
                rc = qahw_out_flush(stream->out_stream);
            else
                ALOGE("%d:%s out stream invalid, cannot flush"
                      , __LINE__, __func__);
            break;
        case QAHW_STREAM_INPUT:
                ALOGE("%d:%s cannot flush input stream"
                      , __LINE__, __func__);
            break;
        case QAHW_STREAM_INPUT_OUTPUT:
            if (stream->out_stream)
                rc = qahw_out_flush(stream->out_stream);
            else
                ALOGE("%d:%s out stream invalid, cannot flush"
                      , __LINE__, __func__);
            break;
        }
    } else
        ALOGE("%d:%s invalid stream handle, flush failed"
              , __LINE__, __func__);
    return rc;
}

int32_t qahw_stream_drain(qahw_stream_handle_t *stream_handle,
                          qahw_drain_type_t type) {
    int rc = -EINVAL;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;
    if (stream) {
        switch (stream->dir) {
        case QAHW_STREAM_OUTPUT:
            if (stream->out_stream)
                rc = qahw_out_drain(stream->out_stream, type);
            else
                ALOGE("%d:%s out stream invalid, cannot drain"
                      , __LINE__, __func__);
            break;
        case QAHW_STREAM_INPUT:
                ALOGE("%d:%s cannot drain input stream"
                      , __LINE__, __func__);
            break;
        case QAHW_STREAM_INPUT_OUTPUT:
            if (stream->out_stream)
                rc = qahw_out_drain(stream->out_stream, type);
            else
                ALOGE("%d:%s out stream invalid, cannot drain"
                      , __LINE__, __func__);
            break;
        }
    } else
        ALOGE("%d:%s invalid stream handle, drain failed"
              , __LINE__, __func__);
    return rc;
}

int32_t qahw_stream_get_buffer_size(const qahw_stream_handle_t *stream_handle,
                                   size_t *in_buffer, size_t *out_buffer) {
    int32_t rc = 0;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;
    if (stream) {
        switch (stream->dir) {
        case QAHW_STREAM_OUTPUT:
            if (stream->out_stream)
                *out_buffer = qahw_out_get_buffer_size(stream->out_stream);
            else {
                ALOGE("%d:%s out stream invalid, cannot get size"
                      , __LINE__, __func__);
                rc = -EINVAL;
            }
            break;
        case QAHW_STREAM_INPUT:
            if (stream->in_stream)
                *in_buffer = qahw_in_get_buffer_size(stream->in_stream);
            else {
                ALOGE("%d:%s in stream invalid, cannot get size"
                      , __LINE__, __func__);
                rc = -EINVAL;
            }
            break;
        case QAHW_STREAM_INPUT_OUTPUT:
            if (stream->out_stream)
                *out_buffer = qahw_out_get_buffer_size(stream->out_stream);
            else {
                ALOGE("%d:%s out stream invalid, cannot get size"
                      , __LINE__, __func__);
                rc = -EINVAL;
            }
            if (stream->in_stream)
                *in_buffer = qahw_in_get_buffer_size(stream->in_stream);
            else {
                ALOGE("%d:%s in stream invalid, cannot get size"
                      , __LINE__, __func__);
                rc = -EINVAL;
            }
            break;
        default:
            ALOGE("%d:%s invalid stream direction, cannot get size", __LINE__, __func__);
            rc = -EINVAL;
            break;
        }
    } else {
        ALOGE("%d:%s invalid stream handle, get size failed failed"
              , __LINE__, __func__);
        rc = -EINVAL;
    }
    ALOGV("%d:%s inSz %d outSz %d ret 0x%8x", __LINE__, __func__, *in_buffer, *out_buffer, rc);
    return rc;
}

int32_t qahw_stream_set_buffer_size(const qahw_stream_handle_t *stream_handle,
                                   size_t in_buffer, size_t out_buffer){
    return -ENOTSUP;
}

int32_t qahw_stream_set_dtmf_gen_params(qahw_api_stream_t *stream,
                                      struct qahw_dtmf_gen_params *dtmf_params){
    int32_t rc = -EINVAL;
    char kv[QAHW_KV_PAIR_LENGTH];

    if(stream->type == QAHW_VOICE_CALL) {
        if(dtmf_params->enable) {
            snprintf(kv, QAHW_KV_PAIR_LENGTH,
                     "dtmf_low_freq=%d;dtmf_high_freq=%d;dtmf_tone_gain=%d",
                     dtmf_params->low_freq,
                     dtmf_params->high_freq,
                     dtmf_params->gain);
           ALOGV("%d:%s kv set is %s", __LINE__, __func__, kv);
        } else
            snprintf(kv, QAHW_KV_PAIR_LENGTH, "dtmf_tone_off");
        rc = qahw_out_set_parameters(stream->out_stream,kv);
    } else
        ALOGE("%d:%s cannot set dtmf on non voice stream", __LINE__, __func__);
    return rc;
}

int32_t qahw_stream_set_tty_mode_params(qahw_api_stream_t *stream,
                                       struct qahw_tty_params *tty_params){
    int32_t rc = -EINVAL;

    if(stream->type == QAHW_VOICE_CALL) {
        if(tty_params->mode >= QAHW_TTY_MODE_MAX) {
            ALOGE("%d:%s invalid tty mode", __LINE__, __func__);
            return rc;
        }

        ALOGV("%d:%s kv set is %s", __LINE__, __func__,
              tty_mode_map[tty_params->mode]);
        /*currently tty is set on the dev */
        rc = qahw_set_parameters(stream->hw_module,
                                 tty_mode_map[tty_params->mode]);
    } else
        ALOGE("%d:%s cannot set tty mode on non voice stream", __LINE__,
              __func__);
    return rc;
}

int32_t qahw_stream_set_hpcm_params(qahw_api_stream_t *stream,
                                    struct qahw_hpcm_params *hpcm_params){
    int32_t rc = -EINVAL;
    char kv[QAHW_KV_PAIR_LENGTH];
    int32_t tp;

    if(stream->type == QAHW_VOICE_CALL) {
        /*if rx and tx call both mixer commands */
        if(hpcm_params->tap_point == QAHW_HPCM_TAP_POINT_RX_TX) {
            snprintf(kv, QAHW_KV_PAIR_LENGTH,
                     "hpcm_tp=%d;hpcm_dir=%d",
                     QAHW_HPCM_TAP_POINT_RX,
                     hpcm_params->direction);
            ALOGV("%d:%s kv set is %s", __LINE__, __func__, kv);
            rc = qahw_out_set_parameters(stream->out_stream, kv);
            if(rc) {
                ALOGE("%d:%s failed to set hpcm on RX Path", __LINE__,
                __func__);
            }
            snprintf(kv, QAHW_KV_PAIR_LENGTH,
                     "hpcm_tp=%d;hpcm_dir=%d",
                     QAHW_HPCM_TAP_POINT_TX,
                     hpcm_params->direction);
            ALOGV("%d:%s kv set is %s", __LINE__, __func__, kv);
            rc = qahw_out_set_parameters(stream->out_stream, kv);
            if(rc) {
                ALOGE("%d:%s failed to set hpcm on TX Path", __LINE__,
                __func__);
            }
        } else {
            snprintf(kv, QAHW_KV_PAIR_LENGTH,
                     "hpcm_tp=%d;hpcm_dir=%d",
                     hpcm_params->tap_point,
                     hpcm_params->direction);
            ALOGV("%d:%s kv set is %s", __LINE__, __func__, kv);
            rc = qahw_out_set_parameters(stream->out_stream, kv);
            if(rc) {
                ALOGE("%d:%s failed to set hpcm params", __LINE__,
                __func__);
            }
        }
    } else
        ALOGE("%d:%s cannot set hpcm params on non voice stream",
              __LINE__, __func__);
    return rc;
}

int32_t qahw_stream_set_parameters(qahw_stream_handle_t *stream_handle,
                                   uint32_t param_id,
                                   qahw_param_payload *param_payload) {
    int32_t rc = -EINVAL;
    qahw_api_stream_t *stream = (qahw_api_stream_t *)stream_handle;

    if(stream && param_payload) {
        switch(param_id){
            case QAHW_PARAM_DTMF_GEN:
                rc = qahw_stream_set_dtmf_gen_params(stream,
                                               &param_payload->dtmf_gen_params);
                break;
            case QAHW_PARAM_TTY_MODE:
                rc = qahw_stream_set_tty_mode_params(stream,
                                               &param_payload->tty_mode_params);
                break;
            case QAHW_PARAM_HPCM:
                rc = qahw_stream_set_hpcm_params(stream,
                                                 &param_payload->hpcm_params);
                break;
            default:
            ALOGE("%d:%s unsupported param id %d"
                  ,__LINE__, __func__, param_id);
            break;
        }
    } else
        ALOGE("%d:%s invalid stream handle, cannot set param"
              , __LINE__, __func__);
    return rc;
}

int32_t qahw_stream_get_parameters(qahw_stream_handle_t *stream_handle,
                                   uint32_t param_id,
                                   qahw_param_payload *param_payload) {
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}
#else
int qahw_stream_open(qahw_module_handle_t *hw_module,
                     struct qahw_stream_attributes attr,
                     uint32_t num_of_devices,
                     qahw_device_t *devices,
                     uint32_t no_of_modifiers,
                     struct qahw_modifier_kv *modifiers,
                     qahw_stream_callback_t cb,
                     void *cookie,
                     qahw_stream_handle_t **stream_handle){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int qahw_stream_close(qahw_stream_handle_t *stream_handle){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int qahw_stream_start(qahw_stream_handle_t *stream_handle){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int qahw_stream_stop(qahw_stream_handle_t *stream_handle){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int qahw_stream_set_device(qahw_stream_handle_t *stream_handle,
                           uint32_t num_of_dev,
                    qahw_device_t *devices){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int qahw_stream_get_device(qahw_stream_handle_t *stream_handle,
                           uint32_t *num_of_dev,
                           qahw_device_t **devices){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int qahw_stream_set_volume(qahw_stream_handle_t *stream_handle,
                           struct qahw_volume_data vol_data){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int qahw_stream_get_volume(qahw_stream_handle_t *stream_handle,
                           struct qahw_volume_data **vol_data){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int qahw_stream_set_mute(qahw_stream_handle_t *stream_handle,
                         struct qahw_mute_data mute_data){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int qahw_stream_get_mute(qahw_stream_handle_t *stream_handle,
                         struct qahw_mute_data *mute_data){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

ssize_t qahw_stream_read(qahw_stream_handle_t *stream_handle,
                         qahw_buffer_t *in_buf){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

ssize_t qahw_stream_write(qahw_stream_handle_t *stream_handle,
                          qahw_buffer_t *out_buf){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int32_t qahw_stream_pause(qahw_stream_handle_t *stream_handle){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int32_t qahw_stream_standby(qahw_stream_handle_t *stream_handle){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int32_t qahw_stream_resume(qahw_stream_handle_t *stream_handle){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int32_t qahw_stream_flush(qahw_stream_handle_t *stream_handle){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int32_t qahw_stream_drain(qahw_stream_handle_t *stream_handle,
                          qahw_drain_type_t type){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int32_t qahw_stream_get_buffer_size(const qahw_stream_handle_t *stream_handle,
                                    size_t *in_buffer, size_t *out_buffer){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int32_t qahw_stream_set_buffer_size(const qahw_stream_handle_t *stream_handle,
                                    size_t in_buffer, size_t out_buffer){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int32_t qahw_stream_set_parameters(qahw_stream_handle_t *stream_handle,
                              uint32_t param_id,
                              qahw_param_payload *param_payload){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}

int32_t qahw_stream_get_parameters(qahw_stream_handle_t *stream_handle,
                              uint32_t param_id,
                              qahw_param_payload *param_payload){
    ALOGE("%s is an unsupported api", __func__);
    return -ENOTSUP;
}
#endif

#endif
