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

#define LOG_TAG "qahw"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <dlfcn.h>
#include <utils/Log.h>
#include <stdlib.h>
#include <cutils/list.h>
#include <pthread.h>
#include <hardware/audio.h>
#include <hardware/sound_trigger.h>
#include "qahw.h"

#define NO_ERROR 0
#define MAX_MODULE_NAME_LENGTH  100

/*
 * The current HAL API version.
 * version 1.0 has support for voice only in new stream based APIS
 */
#ifdef QAHW_MODULE_API_VERSION_1_0
#define QAHW_MODULE_API_VERSION_CURRENT QAHW_MODULE_API_VERSION_1_0
#else
#define QAHW_MODULE_API_VERSION_CURRENT QAHW_MODULE_API_VERSION_0_0
#endif


typedef uint64_t (*qahwi_out_write_v2_t)(audio_stream_out_t *out, const void* buffer,
                                       size_t bytes, int64_t* timestamp);

typedef int (*qahwi_get_param_data_t) (const audio_hw_device_t *,
                              qahw_param_id, qahw_param_payload *);

typedef int (*qahwi_set_param_data_t) (audio_hw_device_t *,
                              qahw_param_id, qahw_param_payload *);

typedef uint64_t (*qahwi_in_read_v2_t)(audio_stream_in_t *in, void* buffer,
                                       size_t bytes, int64_t *timestamp);

typedef int (*qahwi_in_stop_t)(audio_stream_in_t *in);

typedef int (*qahwi_out_set_param_data_t)(struct audio_stream_out *out,
                                      qahw_param_id param_id,
                                      qahw_param_payload *payload);

typedef int (*qahwi_out_get_param_data_t)(struct audio_stream_out *out,
                                      qahw_param_id param_id,
                                      qahw_param_payload *payload);

typedef int (*qahwi_loopback_set_param_data_t)(audio_patch_handle_t patch_handle,
                                               qahw_loopback_param_id param_id,
                                               qahw_loopback_param_payload *payload);

typedef struct {
    audio_hw_device_t *audio_device;
    char module_name[MAX_MODULE_NAME_LENGTH];
    struct listnode module_list;
    struct listnode in_list;
    struct listnode out_list;
    pthread_mutex_t lock;
    uint32_t ref_count;
    const hw_module_t* module;
    qahwi_get_param_data_t qahwi_get_param_data;
    qahwi_set_param_data_t qahwi_set_param_data;
    qahwi_loopback_set_param_data_t qahwi_loopback_set_param_data;
} qahw_module_t;

typedef struct {
    qahw_module_t *module;
    struct listnode module_list;
    pthread_mutex_t lock;
} qahw_module_instances_t;

typedef struct {
    audio_stream_out_t *stream;
    qahw_module_t *module;
    struct listnode list;
    pthread_mutex_t lock;
    qahwi_out_set_param_data_t qahwi_out_get_param_data;
    qahwi_out_get_param_data_t qahwi_out_set_param_data;
    qahwi_out_write_v2_t qahwi_out_write_v2;
} qahw_stream_out_t;

typedef struct {
    audio_stream_in_t *stream;
    qahw_module_t *module;
    struct listnode list;
    pthread_mutex_t lock;
    qahwi_in_read_v2_t qahwi_in_read_v2;
    qahwi_in_stop_t qahwi_in_stop;
} qahw_stream_in_t;

typedef enum {
    STREAM_DIR_IN,
    STREAM_DIR_OUT,
} qahw_stream_direction_t;

static struct listnode qahw_module_list;
static int qahw_list_count;
static pthread_mutex_t qahw_module_init_lock = PTHREAD_MUTEX_INITIALIZER;
sound_trigger_hw_device_t *st_hw_device = NULL;


/** Start of internal functions */
/******************************************************************************/

/* call this function without anylock held */
static bool is_valid_qahw_stream_l(void *qahw_stream,
                                 qahw_stream_direction_t dir)
{

    int is_valid = false;
    struct listnode *module_node = NULL;
    struct listnode *stream_node = NULL;
    struct listnode *list_node = NULL;
    void  *stream = NULL;
    qahw_module_t *qahw_module = NULL;

    if (qahw_stream == NULL) {
        ALOGE("%s:: Invalid stream", __func__);
        goto exit;
    }

    if ((dir != STREAM_DIR_OUT) && (dir != STREAM_DIR_IN)) {
        ALOGE("%s:: Invalid stream direction %d", __func__, dir);
        goto exit;
    }

    /* go through all the modules and check for valid stream */
    pthread_mutex_lock(&qahw_module_init_lock);
    list_for_each(module_node, &qahw_module_list) {
        qahw_module = node_to_item(module_node, qahw_module_t, module_list);
        pthread_mutex_lock(&qahw_module->lock);
        if(dir == STREAM_DIR_OUT)
            list_node = &qahw_module->out_list;
        else
            list_node = &qahw_module->in_list;
        list_for_each(stream_node, list_node) {
            if(dir == STREAM_DIR_OUT)
                stream = (void *)node_to_item(stream_node,
                                              qahw_stream_out_t,
                                              list);
            else
                stream = (void *)node_to_item(stream_node,
                                              qahw_stream_in_t,
                                              list);
            if(stream == qahw_stream) {
                is_valid = true;
                break;
            }
        }
        pthread_mutex_unlock(&qahw_module->lock);
        if(is_valid)
            break;
    }
    pthread_mutex_unlock(&qahw_module_init_lock);

exit:
    return is_valid;
}

/* call this fucntion with ahw_module_init_lock held*/
static qahw_module_t* get_qahw_module_by_ptr_l(qahw_module_t *qahw_module)
{
    struct listnode *node = NULL;
    qahw_module_t *module = NULL, *module_temp = NULL;

    if (qahw_module == NULL)
        goto exit;

    list_for_each(node, &qahw_module_list) {
        module_temp = node_to_item(node, qahw_module_t, module_list);
        if (module_temp == qahw_module) {
            module = module_temp;
            break;
        }
    }
exit:
    return module;
}

/* call this function with qahw_module_init_lock held*/
static qahw_module_t* get_qahw_module_by_name_l(const char *qahw_name)
{
    struct listnode *node = NULL;
    qahw_module_t *module = NULL, *module_temp = NULL;

    if (qahw_name == NULL)
        goto exit;

    list_for_each(node, &qahw_module_list) {
        module_temp = node_to_item(node, qahw_module_t, module_list);
        if(!strncmp(qahw_name, module_temp->module_name, MAX_MODULE_NAME_LENGTH)) {
            module = module_temp;
            break;
        }
    }
exit:
    return module;
}
/* End of of internal functions */

/*
 * Return the sampling rate in Hz - eg. 44100.
 */
uint32_t qahw_out_get_sample_rate_l(const qahw_stream_handle_t *out_handle)
{
    uint32_t rate = 0;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGV("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->common.get_sample_rate)
        rate = out->common.get_sample_rate(&out->common);
    else
        ALOGW("%s not supported", __func__);
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
     return rate;
}

/*
 * currently unused - use set_parameters with key
 *    AUDIO_PARAMETER_STREAM_SAMPLING_RATE
 */
int qahw_out_set_sample_rate_l(qahw_stream_handle_t *out_handle, uint32_t rate)
{
    int32_t rc = -EINVAL;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }
    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->common.set_sample_rate) {
        rc = out->common.set_sample_rate(&out->common, rate);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);
exit:
    return rc;
}

size_t qahw_out_get_buffer_size_l(const qahw_stream_handle_t *out_handle)
{
    size_t buf_size = 0;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }
    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->common.get_buffer_size) {
        buf_size = out->common.get_buffer_size(&out->common);
    } else {
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
    return buf_size;
}

audio_channel_mask_t qahw_out_get_channels_l(const qahw_stream_handle_t *out_handle)
{
    audio_channel_mask_t ch_mask = 0;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }
    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->common.get_channels) {
        ch_mask = out->common.get_channels(&out->common);
    } else {
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
    return ch_mask;
}

audio_format_t qahw_out_get_format_l(const qahw_stream_handle_t *out_handle)
{
    audio_format_t format = AUDIO_FORMAT_INVALID;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }
    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->common.get_format) {
        format = out->common.get_format(&out->common);
    } else {
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
    return format;
}

int qahw_out_standby_l(qahw_stream_handle_t *out_handle)
{
    int32_t rc = -EINVAL;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->common.standby) {
        rc = out->common.standby(&out->common);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
    return rc;
}

int qahw_out_set_parameters_l(qahw_stream_handle_t *out_handle, const char *kv_pairs)
{
    int rc = NO_ERROR;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        rc = -EINVAL;
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->common.set_parameters) {
        rc = out->common.set_parameters(&out->common, kv_pairs);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
    return rc;
}

char *qahw_out_get_parameters_l(const qahw_stream_handle_t *out_handle,
                               const char *keys)
{
    char *str_param = NULL;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->common.get_parameters) {
        str_param = out->common.get_parameters(&out->common, keys);
    } else {
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
    return str_param;
}

/* API to get playback stream specific config parameters */
int qahw_out_set_param_data_l(qahw_stream_handle_t *out_handle,
                            qahw_param_id param_id,
                            qahw_param_payload *payload)
{
    int rc = -EINVAL;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!payload) {
        ALOGE("%s::Invalid param", __func__);
        goto exit;
    }

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (qahw_stream_out->qahwi_out_set_param_data) {
        rc = qahw_stream_out->qahwi_out_set_param_data(out, param_id, payload);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
    return rc;
}

/* API to get playback stream specific config parameters */
int qahw_out_get_param_data_l(qahw_stream_handle_t *out_handle,
                            qahw_param_id param_id,
                            qahw_param_payload *payload)
{
    int rc = -EINVAL;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (qahw_stream_out->qahwi_out_get_param_data) {
        rc = qahw_stream_out->qahwi_out_get_param_data(out, param_id, payload);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
    return rc;
}

uint32_t qahw_out_get_latency_l(const qahw_stream_handle_t *out_handle)
{
    uint32_t latency = 0;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->get_latency) {
        latency = out->get_latency(out);
    } else {
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
    return latency;
}

int qahw_out_set_volume_l(qahw_stream_handle_t *out_handle, float left, float right)
{
    int rc = -EINVAL;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->set_volume) {
        rc = out->set_volume(out, left, right);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
   return rc;
}

ssize_t qahw_out_write_l(qahw_stream_handle_t *out_handle,
        qahw_out_buffer_t *out_buf)
{
    int rc = -EINVAL;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if ((out_buf == NULL) || (out_buf->buffer == NULL)) {
        ALOGE("%s::Invalid meta data %p", __func__, out_buf);
        goto exit;
    }

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }

    /*TBD:: validate other meta data parameters */
    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (qahw_stream_out->qahwi_out_write_v2) {
        rc = qahw_stream_out->qahwi_out_write_v2(out, out_buf->buffer,
                                         out_buf->bytes, out_buf->timestamp);
        out_buf->offset = 0;
    } else if (out->write) {
        rc = out->write(out, out_buf->buffer, out_buf->bytes);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);
exit:
    return rc;
}

int qahw_out_get_render_position_l(const qahw_stream_handle_t *out_handle,
                                 uint32_t *dsp_frames)
{
    int rc = -EINVAL;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->get_render_position) {
        rc = out->get_render_position(out, dsp_frames);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);
exit:
    return rc;
}

int qahw_out_set_callback_l(qahw_stream_handle_t *out_handle,
                          qahw_stream_callback_t callback,
                          void *cookie)
{
    /*TBD:load hal func pointer and call */
    int rc = -EINVAL;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->set_callback) {
        rc = out->set_callback(out, (stream_callback_t)callback, cookie);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
    return rc;
}

int qahw_out_pause_l(qahw_stream_handle_t *out_handle)
{
    /*TBD:load hal func pointer and call */
    int rc = -EINVAL;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->pause) {
        rc = out->pause(out);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
    return rc;
}

int qahw_out_resume_l(qahw_stream_handle_t *out_handle)
{
    /*TBD:load hal func pointer and call */
    int rc = -EINVAL;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->resume) {
        rc = out->resume(out);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
    return rc;
}

int qahw_out_drain_l(qahw_stream_handle_t *out_handle, qahw_drain_type_t type )
{
    /*TBD:load hal func pointer and call */
    int rc = -EINVAL;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->drain) {
        rc = out->drain(out,(audio_drain_type_t)type);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
    return rc;
}

int qahw_out_flush_l(qahw_stream_handle_t *out_handle)
{
    int rc = -EINVAL;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->flush) {
        rc = out->flush(out);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
    return rc;
}

int qahw_out_get_presentation_position_l(const qahw_stream_handle_t *out_handle,
                           uint64_t *frames, struct timespec *timestamp)
{
    int rc = -EINVAL;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    audio_stream_out_t *out = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_out->lock);
    out = qahw_stream_out->stream;
    if (out->get_presentation_position) {
        rc = out->get_presentation_position(out, frames, timestamp);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_out->lock);

exit:
    return rc;
}

/* Input stream specific APIs */
uint32_t qahw_in_get_sample_rate_l(const qahw_stream_handle_t *in_handle)
{
    uint32_t rate = 0;
    qahw_stream_in_t *qahw_stream_in = (qahw_stream_in_t *)in_handle;
    audio_stream_in_t *in = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_in, STREAM_DIR_IN)) {
        ALOGV("%s::Invalid in handle %p", __func__, in_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_in->lock);
    in = qahw_stream_in->stream;
    if (in->common.get_sample_rate) {
        rate = in->common.get_sample_rate(&in->common);
    } else {
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_in->lock);

exit:
    return rate;
}

/*
 * currently unused - use set_parameters with key
 *    AUDIO_PARAMETER_STREAM_SAMPLING_RATE
 */
int qahw_in_set_sample_rate_l(qahw_stream_handle_t *in_handle, uint32_t rate)
{
    int rc = -EINVAL;
    qahw_stream_in_t *qahw_stream_in = (qahw_stream_in_t *)in_handle;
    audio_stream_in_t *in = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_in, STREAM_DIR_IN)) {
        ALOGV("%s::Invalid in handle %p", __func__, in_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_in->lock);
    in = qahw_stream_in->stream;
    if (in->common.set_sample_rate) {
        rc = in->common.set_sample_rate(&in->common, rate);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_in->lock);

exit:
    return rc;
}

size_t qahw_in_get_buffer_size_l(const qahw_stream_handle_t *in_handle)
{
    size_t buf_size = 0;
    qahw_stream_in_t *qahw_stream_in = (qahw_stream_in_t *)in_handle;
    audio_stream_in_t *in = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_in, STREAM_DIR_IN)) {
        ALOGV("%s::Invalid in handle %p", __func__, in_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_in->lock);
    in = qahw_stream_in->stream;
    if (in->common.get_sample_rate) {
        buf_size = in->common.get_buffer_size(&in->common);
    } else {
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_in->lock);

exit:
    return buf_size;
}


audio_channel_mask_t qahw_in_get_channels_l(const qahw_stream_handle_t *in_handle)
{
    audio_channel_mask_t ch_mask = 0;;
    qahw_stream_in_t *qahw_stream_in = (qahw_stream_in_t *)in_handle;
    audio_stream_in_t *in = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_in, STREAM_DIR_IN)) {
        ALOGV("%s::Invalid in handle %p", __func__, in_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_in->lock);
    in = qahw_stream_in->stream;
    if (in->common.get_channels) {
        ch_mask = in->common.get_channels(&in->common);
    } else {
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_in->lock);

exit:
    return ch_mask;
}

audio_format_t qahw_in_get_format_l(const qahw_stream_handle_t *in_handle)
{
    audio_format_t format = AUDIO_FORMAT_INVALID;
    qahw_stream_in_t *qahw_stream_in = (qahw_stream_in_t *)in_handle;
    audio_stream_in_t *in = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_in, STREAM_DIR_IN)) {
        ALOGV("%s::Invalid in handle %p", __func__, in_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_in->lock);
    in = qahw_stream_in->stream;
    if (in->common.get_format) {
        format = in->common.get_format(&in->common);
    } else {
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_in->lock);

exit:
    return format;
}

/*
 * currently unused - use set_parameters with key
 *     AUDIO_PARAMETER_STREAM_FORMAT
 */
int qahw_in_set_format_l(qahw_stream_handle_t *in_handle, audio_format_t format)
{
    int rc = -EINVAL;
    qahw_stream_in_t *qahw_stream_in = (qahw_stream_in_t *)in_handle;
    audio_stream_in_t *in = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_in, STREAM_DIR_IN)) {
        ALOGV("%s::Invalid in handle %p", __func__, in_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_in->lock);
    in = qahw_stream_in->stream;
    if (in->common.set_format) {
        rc = in->common.set_format(&in->common, format);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_in->lock);

exit:
    return rc;
}

int qahw_in_standby_l(qahw_stream_handle_t *in_handle)
{
    int rc = -EINVAL;
    qahw_stream_in_t *qahw_stream_in = (qahw_stream_in_t *)in_handle;
    audio_stream_in_t *in = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_in, STREAM_DIR_IN)) {
        ALOGV("%s::Invalid in handle %p", __func__, in_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_in->lock);
    in = qahw_stream_in->stream;
    if (in->common.standby) {
        rc = in->common.standby(&in->common);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_in->lock);

exit:
    return rc;
}

/*
 * set/get audio stream parameters. The function accepts a list of
 * parameter key value pairs in the form: key1=value1;key2=value2;...
 *
 * Some keys are reserved for standard parameters (See AudioParameter class)
 *
 * If the implementation does not accept a parameter change while
 * the output is active but the parameter is acceptable otherwise, it must
 * return -ENOSYS.
 *
 * The audio flinger will put the stream in standby and then change the
 * parameter value.
 */
int qahw_in_set_parameters_l(qahw_stream_handle_t *in_handle, const char *kv_pairs)
{
    int rc = -EINVAL;
    qahw_stream_in_t *qahw_stream_in = (qahw_stream_in_t *)in_handle;
    audio_stream_in_t *in = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_in, STREAM_DIR_IN)) {
        ALOGV("%s::Invalid in handle %p", __func__, in_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_in->lock);
    in = qahw_stream_in->stream;
    if (in->common.set_parameters) {
        rc = in->common.set_parameters(&in->common, kv_pairs);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_in->lock);
exit:
    return rc;
}

/*
 * Returns a pointer to a heap allocated string. The caller is responsible
 * for freeing the memory for it using free().
 */
char * qahw_in_get_parameters_l(const qahw_stream_handle_t *in_handle,
                              const char *keys)
{
    char *str_param = NULL;
    qahw_stream_in_t *qahw_stream_in = (qahw_stream_in_t *)in_handle;
    audio_stream_in_t *in = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_in, STREAM_DIR_IN)) {
        ALOGV("%s::Invalid in handle %p", __func__, in_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_in->lock);
    in = qahw_stream_in->stream;
    if (in->common.get_parameters) {
        str_param = in->common.get_parameters(&in->common, keys);
    } else {
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_in->lock);

exit:
    return str_param;
}

/*
 * Read audio buffer in from audio driver. Returns number of bytes read, or a
 *  negative status_t. If at least one frame was read prior to the error,
 *  read should return that byte count and then return an error in the subsequent call.
 */
ssize_t qahw_in_read_l(qahw_stream_handle_t *in_handle,
                     qahw_in_buffer_t *in_buf)
{
    int rc = -EINVAL;
    qahw_stream_in_t *qahw_stream_in = (qahw_stream_in_t *)in_handle;
    audio_stream_in_t *in = NULL;

    if ((in_buf == NULL) || (in_buf->buffer == NULL)) {
        ALOGE("%s::Invalid meta data %p", __func__, in_buf);
        goto exit;
    }

    if (!is_valid_qahw_stream_l((void *)qahw_stream_in, STREAM_DIR_IN)) {
        ALOGV("%s::Invalid in handle %p", __func__, in_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_in->lock);
    in = qahw_stream_in->stream;
    if (qahw_stream_in->qahwi_in_read_v2) {
        rc = qahw_stream_in->qahwi_in_read_v2(in, in_buf->buffer,
                                         in_buf->bytes, in_buf->timestamp);
        in_buf->offset = 0;
    } else if (in->read) {
        rc = in->read(in, in_buf->buffer, in_buf->bytes);
        in_buf->offset = 0;
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_in->lock);

exit:
    return rc;
}

/*
 * Stop input stream. Returns zero on success.
 */
int qahw_in_stop_l(qahw_stream_handle_t *in_handle)
{
    int rc = -EINVAL;
    qahw_stream_in_t *qahw_stream_in = (qahw_stream_in_t *)in_handle;
    audio_stream_in_t *in = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_in, STREAM_DIR_IN)) {
        ALOGV("%s::Invalid in handle %p", __func__, in_handle);
        goto exit;
    }
    ALOGD("%s", __func__);

    in = qahw_stream_in->stream;

    if (qahw_stream_in->qahwi_in_stop)
        rc = qahw_stream_in->qahwi_in_stop(in);
    ALOGD("%s: exit", __func__);

exit:
    return rc;
}

/*
 * Return the amount of input frames lost in the audio driver since the
 * last call of this function.
 * Audio driver is expected to reset the value to 0 and restart counting
 * upon returning the current value by this function call.
 * Such loss typically occurs when the user space process is blocked
 * longer than the capacity of audio driver buffers.
 *
 * Unit: the number of input audio frames
 */
uint32_t qahw_in_get_input_frames_lost_l(qahw_stream_handle_t *in_handle)
{
    uint32_t rc = 0;
    qahw_stream_in_t *qahw_stream_in = (qahw_stream_in_t *)in_handle;
    audio_stream_in_t *in = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_in, STREAM_DIR_IN)) {
        ALOGV("%s::Invalid in handle %p", __func__, in_handle);
        goto exit;
    }

    pthread_mutex_lock(&qahw_stream_in->lock);
    in = qahw_stream_in->stream;
    if (in->get_input_frames_lost) {
        rc = in->get_input_frames_lost(in);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_stream_in->lock);

exit:
    return rc;
}

/*
 * Return a recent count of the number of audio frames received and
 * the clock time associated with that frame count.
 *
 * frames is the total frame count received. This should be as early in
 *     the capture pipeline as possible. In general,
 *     frames should be non-negative and should not go "backwards".
 *
 * time is the clock MONOTONIC time when frames was measured. In general,
 *     time should be a positive quantity and should not go "backwards".
 *
 * The status returned is 0 on success, -ENOSYS if the device is not
 * ready/available, or -EINVAL if the arguments are null or otherwise invalid.
 */
int qahw_in_get_capture_position_l(const qahw_stream_handle_t *in_handle __unused,
                                 int64_t *frames __unused, int64_t *time __unused)
{
    /*TBD:: do we need this*/
    return -ENOSYS;
}

/*
 * check to see if the audio hardware interface has been initialized.
 * returns 0 on success, -ENODEV on failure.
 */
int qahw_init_check_l(const qahw_module_handle_t *hw_module)
{
    int rc = -EINVAL;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        goto exit;
    }

    pthread_mutex_lock(&qahw_module->lock);
    if (qahw_module->audio_device->init_check) {
        rc = qahw_module->audio_device->init_check(qahw_module->audio_device);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_module->lock);

exit:
    return rc;
}
/* set the audio volume of a voice call. Range is between 0.0 and 1.0 */
int qahw_set_voice_volume_l(qahw_module_handle_t *hw_module, float volume)
{
    int rc = -EINVAL;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        goto exit;
    }

    pthread_mutex_lock(&qahw_module->lock);
    if (qahw_module->audio_device->set_voice_volume) {
        rc = qahw_module->audio_device->set_voice_volume(qahw_module->audio_device,
                                                         volume);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_module->lock);

exit:
    return rc;
}

/*
 * set_mode is called when the audio mode changes. AUDIO_MODE_NORMAL mode
 * is for standard audio playback, AUDIO_MODE_RINGTONE when a ringtone is
 * playing, and AUDIO_MODE_IN_CALL when a call is in progress.
 */
int qahw_set_mode_l(qahw_module_handle_t *hw_module, audio_mode_t mode)
{
    int rc = -EINVAL;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        goto exit;
    }

    pthread_mutex_lock(&qahw_module->lock);
    if (qahw_module->audio_device->set_mode) {
        rc = qahw_module->audio_device->set_mode(qahw_module->audio_device,
                                                 mode);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_module->lock);

exit:
    return rc;
}

int qahw_set_mic_mute_l(qahw_module_handle_t *hw_module, bool state)
{
    int rc = -EINVAL;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        goto exit;
    }

    pthread_mutex_lock(&qahw_module->lock);
    if (qahw_module->audio_device->set_mic_mute) {
        rc = qahw_module->audio_device->set_mic_mute(qahw_module->audio_device,
                                                 state);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_module->lock);

exit:
    return rc;
}

int qahw_get_mic_mute_l(qahw_module_handle_t *hw_module, bool *state)
{
    size_t rc = 0;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp;
    audio_hw_device_t *audio_device;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        goto exit;
    }

    pthread_mutex_lock(&qahw_module->lock);
    audio_device = qahw_module->audio_device;
    if (qahw_module->audio_device->get_mic_mute) {
        rc = audio_device->get_mic_mute(qahw_module->audio_device, state);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_module->lock);

exit:
    return rc;
}

/* set/get global audio parameters */
int qahw_set_parameters_l(qahw_module_handle_t *hw_module, const char *kv_pairs)
{
    int rc = -EINVAL;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp;
    audio_hw_device_t *audio_device;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        goto exit;
    }

    pthread_mutex_lock(&qahw_module->lock);
    audio_device = qahw_module->audio_device;
    if (qahw_module->audio_device->set_parameters) {
        rc = audio_device->set_parameters(qahw_module->audio_device, kv_pairs);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_module->lock);

exit:
    return rc;
}

/*
 * Returns a pointer to a heap allocated string. The caller is responsible
 * for freeing the memory for it using free().
 */
char * qahw_get_parameters_l(const qahw_module_handle_t *hw_module,
                           const char *keys)
{
    char *str_param = NULL;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp;
    audio_hw_device_t *audio_device;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        goto exit;
    }

    pthread_mutex_lock(&qahw_module->lock);
    audio_device = qahw_module->audio_device;
    if (qahw_module->audio_device->get_parameters) {
        str_param = audio_device->get_parameters(qahw_module->audio_device, keys);
    } else {
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_module->lock);

exit:
    return str_param;
}

/* Api to implement get parameters  based on keyword param_id
 * and store data in payload.
 */
int qahw_get_param_data_l(const qahw_module_handle_t *hw_module,
                        qahw_param_id param_id,
                        qahw_param_payload *payload)
{
    int ret = 0;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        goto exit;
    }

    pthread_mutex_lock(&qahw_module->lock);

    if (qahw_module->qahwi_get_param_data){
        ret = qahw_module->qahwi_get_param_data (qahw_module->audio_device,
                                   param_id, payload);
    } else {
         ret = -ENOSYS;
         ALOGE("%s not supported\n",__func__);
    }
    pthread_mutex_unlock(&qahw_module->lock);

exit:
     return ret;
}

/* Api to implement set parameters  based on keyword param_id
 * and data present in payload.
 */
int qahw_set_param_data_l(const qahw_module_handle_t *hw_module,
                        qahw_param_id param_id,
                        qahw_param_payload *payload)
{
    int ret = 0;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        goto exit;
    }

    pthread_mutex_lock(&qahw_module->lock);

    if (qahw_module->qahwi_set_param_data){
        ret = qahw_module->qahwi_set_param_data (qahw_module->audio_device,
                                   param_id, payload);
    } else {
         ret = -ENOSYS;
         ALOGE("%s not supported\n",__func__);
    }
    pthread_mutex_unlock(&qahw_module->lock);

exit:
     return ret;
}

/* Creates an audio patch between several source and sink ports.
 * The handle is allocated by the HAL and should be unique for this
 * audio HAL module.
 */
int qahw_create_audio_patch_l(qahw_module_handle_t *hw_module,
                        unsigned int num_sources,
                        const struct audio_port_config *sources,
                        unsigned int num_sinks,
                        const struct audio_port_config *sinks,
                        audio_patch_handle_t *handle)
{
    int ret = 0;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        goto exit;
    }

    pthread_mutex_lock(&qahw_module->lock);
    if (qahw_module->audio_device->create_audio_patch) {
        ret = qahw_module->audio_device->create_audio_patch(
                        qahw_module->audio_device,
                        num_sources,
                        sources,
                        num_sinks,
                        sinks,
                        handle);
    } else {
         ret = -ENOSYS;
         ALOGE("%s not supported\n",__func__);
    }
    pthread_mutex_unlock(&qahw_module->lock);

exit:
     return ret;
}

/* Release an audio patch */
int qahw_release_audio_patch_l(qahw_module_handle_t *hw_module,
                        audio_patch_handle_t handle)
{
    int ret = 0;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        goto exit;
    }

    pthread_mutex_lock(&qahw_module->lock);
    if (qahw_module->audio_device->release_audio_patch) {
        ret = qahw_module->audio_device->release_audio_patch(
                        qahw_module->audio_device,
                        handle);
    } else {
         ret = -ENOSYS;
         ALOGE("%s not supported\n",__func__);
    }
    pthread_mutex_unlock(&qahw_module->lock);

exit:
     return ret;
}

int qahw_loopback_set_param_data_l(qahw_module_handle_t *hw_module,
                                   audio_patch_handle_t handle,
                                   qahw_loopback_param_id param_id,
                                   qahw_loopback_param_payload *payload)

{
    int ret = -EINVAL;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;

    if (!payload) {
        ALOGE("%s:: invalid param", __func__);
        goto exit;
    }

    if (qahw_module->qahwi_loopback_set_param_data) {
        ret = qahw_module->qahwi_loopback_set_param_data(handle,
                                                         param_id,
                                                         payload);
    } else {
        ret = -ENOSYS;
        ALOGE("%s not supported\n", __func__);
    }

exit:
    return ret;

}

/* Fills the list of supported attributes for a given audio port.
 * As input, "port" contains the information (type, role, address etc...)
 * needed by the HAL to identify the port.
 * As output, "port" contains possible attributes (sampling rates, formats,
 * channel masks, gain controllers...) for this port.
 */
int qahw_get_audio_port_l(qahw_module_handle_t *hw_module,
                      struct audio_port *port)
{
    int ret = 0;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        goto exit;
    }

    pthread_mutex_lock(&qahw_module->lock);
    if (qahw_module->audio_device->get_audio_port) {
        ret = qahw_module->audio_device->get_audio_port(
                    qahw_module->audio_device,
                    port);
    } else {
         ret = -ENOSYS;
         ALOGE("%s not supported\n",__func__);
    }
    pthread_mutex_unlock(&qahw_module->lock);

exit:
     return ret;
}

/* Set audio port configuration */
int qahw_set_audio_port_config_l(qahw_module_handle_t *hw_module,
                     const struct audio_port_config *config)
{
    int ret = 0;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        goto exit;
    }

    pthread_mutex_lock(&qahw_module->lock);
    if (qahw_module->audio_device->set_audio_port_config) {
        ret = qahw_module->audio_device->set_audio_port_config(
                    qahw_module->audio_device,
                        config);
    } else {
         ret = -ENOSYS;
         ALOGE("%s not supported\n",__func__);
    }
    pthread_mutex_unlock(&qahw_module->lock);

exit:
     return ret;
}

/* Returns audio input buffer size according to parameters passed or
 * 0 if one of the parameters is not supported.
 * See also get_buffer_size which is for a particular stream.
 */
size_t qahw_get_input_buffer_size_l(const qahw_module_handle_t *hw_module,
                                  const struct audio_config *config)
{
    size_t rc = 0;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp;
    audio_hw_device_t *audio_device;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        goto exit;
    }

    pthread_mutex_lock(&qahw_module->lock);
    audio_device = qahw_module->audio_device;
    if (qahw_module->audio_device->get_input_buffer_size) {
        rc = audio_device->get_input_buffer_size(qahw_module->audio_device,
                                                 config);
    } else {
        rc = -ENOSYS;
        ALOGW("%s not supported", __func__);
    }
    pthread_mutex_unlock(&qahw_module->lock);

exit:
    return rc;
}

/*
 * This method creates and opens the audio hardware output stream.
 * The "address" parameter qualifies the "devices" audio device type if needed.
 * The format format depends on the device type:
 * - Bluetooth devices use the MAC address of the device in the form "00:11:22:AA:BB:CC"
 * - USB devices use the ALSA card and device numbers in the form  "card=X;device=Y"
 * - Other devices may use a number or any other string.
 */
int qahw_open_output_stream_l(qahw_module_handle_t *hw_module,
                            audio_io_handle_t handle,
                            audio_devices_t devices,
                            audio_output_flags_t flags,
                            struct audio_config *config,
                            qahw_stream_handle_t **out_handle,
                            const char *address)
{
    int rc = -EINVAL;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp = NULL;
    audio_hw_device_t *audio_device = NULL;
    qahw_stream_out_t *qahw_stream_out = NULL;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        return rc;
    }

    pthread_mutex_lock(&qahw_module->lock);
    audio_device = qahw_module->audio_device;
    qahw_stream_out = (qahw_stream_out_t *)calloc(1, sizeof(qahw_stream_out_t));
    if (qahw_stream_out == NULL) {
        ALOGE("%s:: calloc failed for out stream_out_t",__func__);
        rc = -ENOMEM;
        goto exit;
    }

    rc = audio_device->open_output_stream(audio_device,
                                          handle,
                                          devices,
                                          flags,
                                          config,
                                          &qahw_stream_out->stream,
                                          address);
    if (rc) {
        ALOGE("%s::open output stream failed %d",__func__, rc);
        free(qahw_stream_out);
    } else {
        qahw_stream_out->module = hw_module;
        *out_handle = (void *)qahw_stream_out;
        pthread_mutex_init(&qahw_stream_out->lock, (const pthread_mutexattr_t *)NULL);
        list_add_tail(&qahw_module->out_list, &qahw_stream_out->list);

        /* clear any existing errors */
        const char *error;
        dlerror();
        qahw_stream_out->qahwi_out_get_param_data = (qahwi_out_get_param_data_t)
                                                 dlsym(qahw_module->module->dso,
                                                 "qahwi_out_get_param_data");
        if ((error = dlerror()) != NULL) {
            ALOGI("%s: dlsym error %s for qahwi_out_get_param_data",
                   __func__, error);
            qahw_stream_out->qahwi_out_get_param_data = NULL;
        }

        dlerror();
        qahw_stream_out->qahwi_out_set_param_data = (qahwi_out_set_param_data_t)
                                                 dlsym(qahw_module->module->dso,
                                                 "qahwi_out_set_param_data");
        if ((error = dlerror()) != NULL) {
            ALOGI("%s: dlsym error %s for qahwi_out_set_param_data",
                   __func__, error);
            qahw_stream_out->qahwi_out_set_param_data = NULL;
        }
}

    /* dlsym qahwi_out_write_v2 */
    if (!rc) {
        const char *error;

        /* clear any existing errors */
        dlerror();
        qahw_stream_out->qahwi_out_write_v2 = (qahwi_out_write_v2_t)dlsym(qahw_module->module->dso, "qahwi_out_write_v2");
        if ((error = dlerror()) != NULL) {
            ALOGI("%s: dlsym error %s for qahwi_out_write_v2", __func__, error);
            qahw_stream_out->qahwi_out_write_v2 = NULL;
        }
    }

exit:
    pthread_mutex_unlock(&qahw_module->lock);
    return rc;
}

int qahw_close_output_stream_l(qahw_stream_handle_t *out_handle)
{

    int rc = 0;
    qahw_stream_out_t *qahw_stream_out = (qahw_stream_out_t *)out_handle;
    qahw_module_t *qahw_module = NULL;
    audio_hw_device_t *audio_device = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_out, STREAM_DIR_OUT)) {
        ALOGE("%s::Invalid out handle %p", __func__, out_handle);
        rc = -EINVAL;
        goto exit;
    }

    ALOGV("%s::calling device close_output_stream %p", __func__, out_handle);
    pthread_mutex_lock(&qahw_stream_out->lock);
    qahw_module = qahw_stream_out->module;
    audio_device = qahw_module->audio_device;
    audio_device->close_output_stream(audio_device,
                                      qahw_stream_out->stream);

    pthread_mutex_lock(&qahw_module->lock);
    list_remove(&qahw_stream_out->list);
    pthread_mutex_unlock(&qahw_module->lock);

    pthread_mutex_unlock(&qahw_stream_out->lock);

    pthread_mutex_destroy(&qahw_stream_out->lock);
    free(qahw_stream_out);

exit:
    return rc;
}

/* This method creates and opens the audio hardware input stream */
int qahw_open_input_stream_l(qahw_module_handle_t *hw_module,
                           audio_io_handle_t handle,
                           audio_devices_t devices,
                           struct audio_config *config,
                           qahw_stream_handle_t **in_handle,
                           audio_input_flags_t flags,
                           const char *address,
                           audio_source_t source)
{
    int rc = -EINVAL;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp = NULL;
    audio_hw_device_t *audio_device = NULL;
    qahw_stream_in_t *qahw_stream_in = NULL;
    const char *error;

    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    pthread_mutex_unlock(&qahw_module_init_lock);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        return rc;
    }

    pthread_mutex_lock(&qahw_module->lock);
    audio_device = qahw_module->audio_device;
    qahw_stream_in = (qahw_stream_in_t *)calloc(1, sizeof(qahw_stream_in_t));
    if (qahw_stream_in == NULL) {
        ALOGE("%s:: calloc failed for in stream_in_t",__func__);
        rc = -ENOMEM;
        goto exit;
    }

    rc = audio_device->open_input_stream(audio_device,
                                          handle,
                                          devices,
                                          config,
                                          &qahw_stream_in->stream,
                                          flags,
                                          address,
                                          source);
    if (rc) {
        ALOGE("%s::open input stream failed %d",__func__, rc);
        free(qahw_stream_in);
        goto exit;
    } else {
        qahw_stream_in->module = hw_module;
        *in_handle = (void *)qahw_stream_in;
        pthread_mutex_init(&qahw_stream_in->lock, (const pthread_mutexattr_t *)NULL);
        list_add_tail(&qahw_module->in_list, &qahw_stream_in->list);
    }

    /* dlsym qahwi_in_read_v2 if timestamp flag is used */
    if (!rc && ((flags & QAHW_INPUT_FLAG_TIMESTAMP) ||
                (flags & QAHW_INPUT_FLAG_PASSTHROUGH))) {

        /* clear any existing errors */
        dlerror();
        qahw_stream_in->qahwi_in_read_v2 = (qahwi_in_read_v2_t)
                          dlsym(qahw_module->module->dso, "qahwi_in_read_v2");
        if ((error = dlerror()) != NULL) {
            ALOGI("%s: dlsym error %s for qahwi_in_read_v2", __func__, error);
            qahw_stream_in->qahwi_in_read_v2 = NULL;
        }
    }

    /* clear any existing errors */
    dlerror();
    qahw_stream_in->qahwi_in_stop = (qahwi_in_stop_t)
        dlsym(qahw_module->module->dso, "qahwi_in_stop");
    if ((error = dlerror()) != NULL) {
        ALOGI("%s: dlsym error %s for qahwi_in_stop", __func__, error);
        qahw_stream_in->qahwi_in_stop = NULL;
    }

 exit:
    pthread_mutex_unlock(&qahw_module->lock);
    return rc;
}

int qahw_close_input_stream_l(qahw_stream_handle_t *in_handle)
{
    int rc = 0;
    qahw_stream_in_t *qahw_stream_in = (qahw_stream_in_t *)in_handle;
    qahw_module_t *qahw_module = NULL;
    audio_hw_device_t *audio_device = NULL;

    if (!is_valid_qahw_stream_l((void *)qahw_stream_in, STREAM_DIR_IN)) {
        ALOGV("%s::Invalid in handle %p", __func__, in_handle);
        rc = -EINVAL;
        goto exit;
    }

    ALOGV("%s:: calling device close_input_stream %p", __func__, in_handle);
    pthread_mutex_lock(&qahw_stream_in->lock);
    qahw_module = qahw_stream_in->module;
    audio_device = qahw_module->audio_device;
    audio_device->close_input_stream(audio_device,
                                     qahw_stream_in->stream);

    pthread_mutex_lock(&qahw_module->lock);
    list_remove(&qahw_stream_in->list);
    pthread_mutex_unlock(&qahw_module->lock);

    pthread_mutex_unlock(&qahw_stream_in->lock);

    pthread_mutex_destroy(&qahw_stream_in->lock);
    free(qahw_stream_in);

exit:
    return rc;
}

/*returns current QTI HAL verison */
int qahw_get_version_l() {
    return QAHW_MODULE_API_VERSION_CURRENT;
}

/* Load AHAL module to run audio and sva concurrency */
static void load_st_hal()
{
#ifdef SVA_AUDIO_CONC
    int rc = -EINVAL;
    const hw_module_t* module = NULL;

    rc = hw_get_module_by_class(SOUND_TRIGGER_HARDWARE_MODULE_ID, "primary", &module);
    if (rc) {
        ALOGE("%s: AHAL Loading failed %d", __func__, rc);
        goto error;
    }

    rc = sound_trigger_hw_device_open(module, &st_hw_device);
    if (rc) {
        ALOGE("%s: AHAL Device open failed %d", __func__, rc);
        st_hw_device = NULL;
    }
error:
    return;
#else
    return;
#endif /*SVA_AUDIO_CONC*/
}

static void unload_st_hal()
{
#ifdef SVA_AUDIO_CONC
    if (st_hw_device == NULL) {
        ALOGE("%s: audio device is NULL",__func__);
        return;
    }
    sound_trigger_hw_device_close(st_hw_device);
    st_hw_device = NULL;
#else
    return;
#endif /*SVA_AUDIO_CONC*/
}

/* convenience API for opening and closing an audio HAL module */

qahw_module_handle_t *qahw_load_module_l(const char *hw_module_id)
{
    int rc = -EINVAL;
    qahw_module_handle_t *qahw_mod_handle = NULL;
    qahw_module_t *qahw_module = NULL;
    char *ahal_name = NULL;
    const hw_module_t* module = NULL;
    audio_hw_device_t* audio_device = NULL;

    if (hw_module_id == NULL) {
        ALOGE("%s::module id is NULL",__func__);
        goto exit;
    }

    if (!strcmp(hw_module_id, QAHW_MODULE_ID_PRIMARY)) {
        ahal_name = "primary";
    } else if (!strcmp(hw_module_id, QAHW_MODULE_ID_A2DP)) {
        ahal_name = "a2dp";
    } else if (!strcmp(hw_module_id, QAHW_MODULE_ID_USB)) {
        ahal_name = "usb";
    } else {
        ALOGE("%s::Invalid Module id %s", __func__, hw_module_id);
        goto exit;
    }

    /* return exiting module ptr if already loaded */
    pthread_mutex_lock(&qahw_module_init_lock);
    if (qahw_list_count > 0) {
        qahw_module = get_qahw_module_by_name_l(hw_module_id);
        if(qahw_module != NULL) {
            qahw_mod_handle = (void *)qahw_module;
            pthread_mutex_lock(&qahw_module->lock);
            qahw_module->ref_count++;
            pthread_mutex_unlock(&qahw_module->lock);
            goto error_exit;
        }
    }

    rc = hw_get_module_by_class(AUDIO_HARDWARE_MODULE_ID, ahal_name, &module);
    if(rc) {
        ALOGE("%s::HAL Loading failed %d", __func__, rc);
        goto error_exit;
    }

    rc = audio_hw_device_open(module, &audio_device);
    if(rc) {
        ALOGE("%s::HAL Device open failed %d", __func__, rc);
        goto error_exit;
    }

    qahw_module = (qahw_module_t *)calloc(1, sizeof(qahw_module_t));
    if(qahw_module == NULL) {
        ALOGE("%s::calloc failed", __func__);
        audio_hw_device_close(audio_device);
        goto error_exit;
    }
    qahw_module->module = module;
    ALOGD("%s::Loaded HAL %s module %p", __func__, ahal_name, qahw_module);

    qahw_module->qahwi_get_param_data = (qahwi_get_param_data_t) dlsym (module->dso,
                            "qahwi_get_param_data");
    if (!qahw_module->qahwi_get_param_data)
         ALOGD("%s::qahwi_get_param_data api is not defined\n",__func__);

    qahw_module->qahwi_set_param_data = (qahwi_set_param_data_t) dlsym (module->dso,
                            "qahwi_set_param_data");
    if (!qahw_module->qahwi_set_param_data)
         ALOGD("%s::qahwi_set_param_data api is not defined\n",__func__);

    qahw_module->qahwi_loopback_set_param_data = (qahwi_loopback_set_param_data_t)
                                                  dlsym(module->dso,
                                                  "qahwi_loopback_set_param_data");
    if (!qahw_module->qahwi_loopback_set_param_data)
         ALOGD("%s::qahwi_loopback_set_param_data api is not defined\n", __func__);

    if (!qahw_list_count)
        list_init(&qahw_module_list);
    qahw_list_count++;

    pthread_mutex_init(&qahw_module->lock, (const pthread_mutexattr_t *) NULL);
    pthread_mutex_lock(&qahw_module->lock);
    qahw_module->ref_count++;
    pthread_mutex_unlock(&qahw_module->lock);

    list_init(&qahw_module->out_list);
    list_init(&qahw_module->in_list);

    /* update qahw_module */
    qahw_module->audio_device = audio_device;
    strlcpy(&qahw_module->module_name[0], hw_module_id, MAX_MODULE_NAME_LENGTH);

    qahw_mod_handle = (void *)qahw_module;

    /* Add module list to global module list */
    list_add_tail(&qahw_module_list, &qahw_module->module_list);
    load_st_hal();

error_exit:
    pthread_mutex_unlock(&qahw_module_init_lock);

exit:
    return qahw_mod_handle;
}

int qahw_unload_module_l(qahw_module_handle_t *hw_module)
{
    int rc = -EINVAL;
    bool is_empty = false;
    qahw_module_t *qahw_module = (qahw_module_t *)hw_module;
    qahw_module_t *qahw_module_temp = NULL;

    /* close HW device if its valid and all the streams on
     * it is closed
    */
    pthread_mutex_lock(&qahw_module_init_lock);
    qahw_module_temp = get_qahw_module_by_ptr_l(qahw_module);
    if (qahw_module_temp == NULL) {
        ALOGE("%s:: invalid hw module %p", __func__, qahw_module);
        goto error_exit;
    }

    pthread_mutex_lock(&qahw_module->lock);
    qahw_module->ref_count--;
    if (qahw_module->ref_count > 0) {
        rc = 0;
        ALOGE("%s:: skipping module unload of %p count %d", __func__,
              qahw_module,
              qahw_module->ref_count);
        pthread_mutex_unlock(&qahw_module->lock);
        goto error_exit;
    }

    is_empty = (list_empty(&qahw_module->out_list) &&
             list_empty(&qahw_module->in_list));
    if (is_empty) {
        rc = audio_hw_device_close(qahw_module->audio_device);
        if(rc) {
            ALOGE("%s::HAL Device close failed Error %d Module %p",__func__,
                    rc, qahw_module);
            rc = 0;
        }
        qahw_list_count--;
        list_remove(&qahw_module->module_list);
        pthread_mutex_unlock(&qahw_module->lock);
        pthread_mutex_destroy(&qahw_module->lock);
        free(qahw_module);
    } else {
        pthread_mutex_unlock(&qahw_module->lock);
        ALOGE("%s::failed as all the streams on this module"
               "is not closed", __func__);
        rc = -EINVAL;
    }
    unload_st_hal();

error_exit:
    pthread_mutex_unlock(&qahw_module_init_lock);

    return rc;
}

__END_DECLS
