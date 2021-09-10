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

#define LOG_TAG "qahwi"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <inttypes.h>
#include <errno.h>
#include <log/log.h>
#include <cutils/atomic.h>

#include <hardware/audio.h>
#include "sound/compress_params.h"
#include "audio_hw.h"
#include "audio_extn.h"
#include "audio_hw_extn_api.h"

#include <pthread.h>

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_AUDIO_HW_EXTN_API
#include <log_utils.h>
#endif

/* default timestamp metadata definition if not defined in kernel*/
#ifndef COMPRESSED_TIMESTAMP_FLAG
#define COMPRESSED_TIMESTAMP_FLAG 0
struct snd_codec_metadata {
uint64_t timestamp;
};
#endif

static void lock_output_stream(struct stream_out *out)
{
    pthread_mutex_lock(&out->pre_lock);
    pthread_mutex_lock(&out->lock);
    pthread_mutex_unlock(&out->pre_lock);
}

/* API to send playback stream specific config parameters */
int qahwi_out_set_param_data(struct audio_stream_out *stream,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload) {
    int ret = 0;
    struct stream_out *out = (struct stream_out *)stream;

    /* call qaf extn set_param if needed */
    if (audio_extn_is_qaf_stream(out)) {
        /* qaf acquires out->lock internally*/
        ret = audio_extn_qaf_out_set_param_data(out, param_id, payload);
        if (ret)
            ALOGE("%s::qaf_out_set_param_data failed error %d", __func__ , ret);
    } else {
        if (out->standby && (param_id != AUDIO_EXTN_PARAM_OUT_CHANNEL_MAP))
            out->stream.write(&out->stream, NULL, 0);
        lock_output_stream(out);
        ret = audio_extn_out_set_param_data(out, param_id, payload);
        if (ret)
            ALOGE("%s::audio_extn_out_set_param_data error %d", __func__, ret);
        pthread_mutex_unlock(&out->lock);
    }
    return ret;
}

/* API to get playback stream specific config parameters */
int qahwi_out_get_param_data(struct audio_stream_out *stream,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload)
{
    int ret;
    struct stream_out *out = (struct stream_out *)stream;

    /* call qaf extn set_param if enabled */
    if (audio_extn_is_qaf_stream(out)) {
        /* qaf acquires out->lock internally*/
        ret = audio_extn_qaf_out_get_param_data(out, param_id, payload);
        if (ret)
            ALOGE("%s::qaf_out_get_param_data failed error %d", __func__, ret);
    } else  {
        if (out->standby)
            out->stream.write(&out->stream, NULL, 0);
        lock_output_stream(out);
        ret = audio_extn_out_get_param_data(out, param_id, payload);
        if (ret)
            ALOGE("%s::audio_extn_out_get_param_data failed error %d",__func__, ret);
        pthread_mutex_unlock(&out->lock);
    }

    return ret;
}

int qahwi_get_param_data(const struct audio_hw_device *adev,
                         audio_extn_param_id param_id,
                         audio_extn_param_payload *payload)
{
    int ret = 0;
    const struct audio_device *dev = (const struct audio_device *)adev;

    if (adev == NULL) {
        ALOGE("%s::INVALID PARAM adev\n",__func__);
        return -EINVAL;
    }

    if (payload == NULL) {
        ALOGE("%s::INVALID PAYLOAD VALUE\n",__func__);
        return -EINVAL;
    }

    switch (param_id) {
        case AUDIO_EXTN_PARAM_SOUND_FOCUS:
              ret = audio_extn_get_soundfocus_data(dev,
                     (struct sound_focus_param *)payload);
              break;
        case AUDIO_EXTN_PARAM_SOURCE_TRACK:
              ret = audio_extn_get_sourcetrack_data(dev,
                     (struct source_tracking_param*)payload);
              break;
        case AUDIO_EXTN_PARAM_LICENSE_PARAMS:
              ret = audio_extn_utils_get_license_params(dev,
                     (struct audio_license_params *)(payload));
              break;
       default:
             ALOGE("%s::INVALID PARAM ID:%d\n",__func__,param_id);
             ret = -EINVAL;
             break;
    }
    return ret;
}

int qahwi_set_param_data(struct audio_hw_device *adev,
                         audio_extn_param_id param_id,
                         audio_extn_param_payload *payload)
{
    int ret = 0;
    struct audio_device *dev = (struct audio_device *)adev;

    if (adev == NULL) {
        ALOGE("%s::INVALID PARAM adev\n",__func__);
        return -EINVAL;
    }

    if (payload == NULL) {
        ALOGE("%s::INVALID PAYLOAD VALUE\n",__func__);
        return -EINVAL;
    }

    switch (param_id) {
        case AUDIO_EXTN_PARAM_SOUND_FOCUS:
              ret = audio_extn_set_soundfocus_data(dev,
                     (struct sound_focus_param *)payload);
              break;
        case AUDIO_EXTN_PARAM_APTX_DEC:
              audio_extn_set_aptx_dec_params((struct aptx_dec_param *)payload);
              break;
        case AUDIO_EXTN_PARAM_DEVICE_CONFIG:
              ALOGV("%s:: Calling audio_extn_set_device_cfg_params", __func__);
              audio_extn_set_device_cfg_params(dev,
                               (struct audio_device_cfg_param *)payload);
              break;
       default:
             ALOGE("%s::INVALID PARAM ID:%d\n",__func__,param_id);
             ret = -EINVAL;
             break;
    }
    return ret;
}

int qahwi_in_stop(struct audio_stream_in* stream) {
    struct stream_in *in = (struct stream_in *)stream;
    struct audio_device *adev = in->dev;

    ALOGD("%s processing, in %p", __func__, in);

    pthread_mutex_lock(&adev->lock);

    if (!in->standby) {
        if (in->pcm != NULL ) {
            pcm_stop(in->pcm);
        } else if (audio_extn_cin_attached_usecase(in)) {
            audio_extn_cin_stop_input_stream(in);
        }

        /* Set the atomic variable when the session is stopped */
        if (android_atomic_acquire_cas(false, true, &(in->capture_stopped)) == 0)
            ALOGI("%s: capture_stopped bit set", __func__);
    }

    pthread_mutex_unlock(&adev->lock);

    return 0;
}

ssize_t qahwi_in_read_v2(struct audio_stream_in *stream, void* buffer,
                          size_t bytes, uint64_t *timestamp)
{
    struct stream_in *in = (struct stream_in *)stream;
    struct snd_codec_metadata *mdata = NULL;
    size_t mdata_size = 0, bytes_read = 0;
    char *buf = NULL;
    size_t ret = 0;

    if (!in->qahwi_in.is_inititalized) {
        ALOGE("%s: invalid state!", __func__);
        return -EINVAL;
    }
    if (COMPRESSED_TIMESTAMP_FLAG &&
        ((in->flags & AUDIO_INPUT_FLAG_TIMESTAMP) ||
         (in->flags & AUDIO_INPUT_FLAG_PASSTHROUGH))) {
        if (bytes != in->stream.common.get_buffer_size(&stream->common)) {
            ALOGE("%s: bytes requested must be fragment size in timestamp mode!", __func__);
            return -EINVAL;
        }
        mdata_size = sizeof(struct snd_codec_metadata);
        buf = (char *) in->qahwi_in.ibuf;
        ret = in->qahwi_in.base.read(&in->stream, (void *)buf, bytes + mdata_size);
        if (ret == bytes + mdata_size) {
           mdata = (struct snd_codec_metadata *) buf;
           bytes_read = mdata->length;
           if (bytes_read > bytes) {
              ALOGE("%s: bytes requested to small (given %zu, required %zu)",
                 __func__, bytes, bytes_read);
              return -EINVAL;
           }
           memcpy(buffer, buf + mdata_size, bytes_read);
           if (timestamp) {
               *timestamp = mdata->timestamp;
           }
        } else {
           ALOGE("%s: error! read returned %zd", __func__, ret);
        }
    } else {
        bytes_read = in->qahwi_in.base.read(stream, buffer, bytes);
        if (timestamp)
            *timestamp = (uint64_t ) -1;
    }
    ALOGV("%s: flag 0x%x, bytes %zd, read %zd, ret %zd",
          __func__, in->flags, bytes, bytes_read, ret);
    return bytes_read;
}

static void qahwi_close_input_stream(struct audio_hw_device *dev,
                               struct audio_stream_in *stream_in)
{
    struct audio_device *adev = (struct audio_device *) dev;
    struct stream_in *in = (struct stream_in *)stream_in;

    ALOGV("%s", __func__);
    if (!adev->qahwi_dev.is_inititalized || !in->qahwi_in.is_inititalized) {
        ALOGE("%s: invalid state!", __func__);
        return;
    }
    if (in->qahwi_in.ibuf)
        free(in->qahwi_in.ibuf);
    adev->qahwi_dev.base.close_input_stream(dev, stream_in);
}

static int qahwi_open_input_stream(struct audio_hw_device *dev,
                             audio_io_handle_t handle,
                             audio_devices_t devices,
                             struct audio_config *config,
                             struct audio_stream_in **stream_in,
                             audio_input_flags_t flags,
                             const char *address,
                             audio_source_t source)
{
    struct audio_device *adev = (struct audio_device *) dev;
    struct stream_in *in = NULL;
    size_t buf_size = 0, mdata_size = 0;
    int ret = 0;

    ALOGV("%s: dev_init %d, flags 0x%x", __func__,
              adev->qahwi_dev.is_inititalized, flags);
    if (!adev->qahwi_dev.is_inititalized) {
        ALOGE("%s: invalid state!", __func__);
        return -EINVAL;
    }

    ret = adev->qahwi_dev.base.open_input_stream(dev, handle, devices, config,
                                                 stream_in, flags, address, source);
    if (ret)
        return ret;

    in = (struct stream_in *)*stream_in;
    // keep adev fptrs before overriding
    in->qahwi_in.base = in->stream;

    in->qahwi_in.is_inititalized = true;

    if (COMPRESSED_TIMESTAMP_FLAG &&
        ((in->flags & AUDIO_INPUT_FLAG_TIMESTAMP) ||
         (in->flags & AUDIO_INPUT_FLAG_PASSTHROUGH))) {
        // set read to NULL as this is not supported in timestamp mode
        in->stream.read = NULL;

        mdata_size = sizeof(struct snd_codec_metadata);
        buf_size = mdata_size +
                   in->qahwi_in.base.common.get_buffer_size(&in->stream.common);

        in->qahwi_in.ibuf = malloc(buf_size);
        if (!in->qahwi_in.ibuf) {
            ALOGE("%s: allocation failed for timestamp metadata!", __func__);
            qahwi_close_input_stream(dev, &in->stream);
            *stream_in = NULL;
            ret = -ENOMEM;
        }
        ALOGD("%s: ibuf %p, buff_size %zd",
              __func__, in->qahwi_in.ibuf, buf_size);
    }
    return ret;
}

ssize_t qahwi_out_write_v2(struct audio_stream_out *stream, const void* buffer,
                          size_t bytes, int64_t* timestamp)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct snd_codec_metadata *mdata = NULL;
    size_t mdata_size = 0, bytes_written = 0;
    char *buf = NULL;
    ssize_t ret = 0;

    if (!out->qahwi_out.is_inititalized) {
        ALOGE("%s: invalid state!", __func__);
        return -EINVAL;
    }
    if (COMPRESSED_TIMESTAMP_FLAG &&
        (out->flags & AUDIO_OUTPUT_FLAG_TIMESTAMP)) {

        mdata_size = sizeof(struct snd_codec_metadata);
        buf = (char *) out->qahwi_out.obuf;
        if (timestamp) {
            mdata = (struct snd_codec_metadata *) buf;
            mdata->length = bytes;
            mdata->offset = mdata_size;
            mdata->timestamp = *timestamp;
        }
        memcpy(buf + mdata_size, buffer, bytes);
        ret = out->qahwi_out.base.write(&out->stream, (void *)buf, out->qahwi_out.buf_size);
        if (ret <= 0) {
            ALOGE("%s: error! write returned %zd", __func__, ret);
        } else {
            bytes_written = bytes;
        }
        ALOGV("%s: flag 0x%x, bytes %zd, read %zd, ret %zd timestamp 0x%"PRIx64"",
              __func__, out->flags, bytes, bytes_written, ret, timestamp == NULL ? 0 : *timestamp);
    } else {
        bytes_written = out->qahwi_out.base.write(&out->stream, buffer, bytes);
        ALOGV("%s: flag 0x%x, bytes %zd, read %zd, ret %zd",
              __func__, out->flags, bytes, bytes_written, ret);
    }
    return bytes_written;
}

static void qahwi_close_output_stream(struct audio_hw_device *dev,
                               struct audio_stream_out *stream_out)
{
    struct audio_device *adev = (struct audio_device *) dev;
    struct stream_out *out = (struct stream_out *)stream_out;

    ALOGV("%s", __func__);
    if (!adev->qahwi_dev.is_inititalized || !out->qahwi_out.is_inititalized) {
        ALOGE("%s: invalid state!", __func__);
        return;
    }
    if (out->qahwi_out.obuf)
        free(out->qahwi_out.obuf);
    out->qahwi_out.buf_size = 0;
    adev->qahwi_dev.base.close_output_stream(dev, stream_out);
}

static int qahwi_open_output_stream(struct audio_hw_device *dev,
                             audio_io_handle_t handle,
                             audio_devices_t devices,
                             audio_output_flags_t flags,
                             struct audio_config *config,
                             struct audio_stream_out **stream_out,
                             const char *address)
{
    struct audio_device *adev = (struct audio_device *) dev;
    struct stream_out *out = NULL;
    size_t buf_size = 0, mdata_size = 0;
    int ret = 0;

    ALOGV("%s: dev_init %d, flags 0x%x", __func__,
              adev->qahwi_dev.is_inititalized, flags);
    if (!adev->qahwi_dev.is_inititalized) {
        ALOGE("%s: invalid state!", __func__);
        return -EINVAL;
    }

    if (flags & AUDIO_OUTPUT_FLAG_DIRECT_PCM)
        flags = (flags & ~AUDIO_OUTPUT_FLAG_DIRECT_PCM ) | AUDIO_OUTPUT_FLAG_DIRECT;

    ret = adev->qahwi_dev.base.open_output_stream(dev, handle, devices, flags,
                                                 config, stream_out, address);
    if (ret)
        return ret;

    out = (struct stream_out *)*stream_out;
    // keep adev fptrs before overriding
    out->qahwi_out.base = out->stream;

    out->qahwi_out.is_inititalized = true;

    if (COMPRESSED_TIMESTAMP_FLAG &&
        (flags & AUDIO_OUTPUT_FLAG_TIMESTAMP)) {
        // set write to NULL as this is not supported in timestamp mode
        out->stream.write = NULL;

        mdata_size = sizeof(struct snd_codec_metadata);
        buf_size = out->qahwi_out.base.common.get_buffer_size(&out->stream.common);
        buf_size += mdata_size;
        out->qahwi_out.buf_size = buf_size;
        out->qahwi_out.obuf = malloc(buf_size);
        if (!out->qahwi_out.obuf) {
            ALOGE("%s: allocation failed for timestamp metadata!", __func__);
            qahwi_close_output_stream(dev, &out->stream);
            *stream_out = NULL;
            ret = -ENOMEM;
        }
        ALOGD("%s: obuf %p, buff_size %zd",
              __func__, out->qahwi_out.obuf, buf_size);
    }
    return ret;
}

int qahwi_loopback_set_param_data(audio_patch_handle_t handle,
                                  audio_extn_loopback_param_id param_id,
                                  audio_extn_loopback_param_payload *payload) {
    int ret = 0;

    ret = audio_extn_hw_loopback_set_param_data(
                                             handle,
                                             param_id,
                                             payload);

    return ret;
}

void qahwi_init(hw_device_t *device)
{
    struct audio_device *adev = (struct audio_device *) device;

    ALOGD("%s", __func__);

    // keep adev fptrs before overriding,
    // as it might be used internally by overriding implementation
    adev->qahwi_dev.base = adev->device;

    adev->device.open_input_stream = qahwi_open_input_stream;
    adev->device.close_input_stream = qahwi_close_input_stream;

    adev->device.open_output_stream = qahwi_open_output_stream;
    adev->device.close_output_stream = qahwi_close_output_stream;

    adev->qahwi_dev.is_inititalized = true;
}
void qahwi_deinit(hw_device_t *device)
{
    struct audio_device *adev = (struct audio_device *) device;

    ALOGV("%s: is_initialized %d", __func__, adev->qahwi_dev.is_inititalized);
    if (!adev->qahwi_dev.is_inititalized) {
        ALOGE("%s: invalid state!", __func__);
    }
}

