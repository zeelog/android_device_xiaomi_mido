/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "BT_HAL_EXTN"
/*#define LOG_NDEBUG 0*/

#include <inttypes.h>
#include <log/log.h>
#include <audio_hw.h>
#include <audio_extn.h>
#include <platform_api.h>
#include <audio_utils/resampler.h>
#include <audio_utils/format.h>

#include <../../../../system/bt/audio_a2dp_hw/bthost_ipc.h>
#include <dlfcn.h>

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_BT_HAL
#include <log_utils.h>
#endif

#define DEFAULT_BUF_SIZE 6144

#define UNUSED(x) (void)(x)
#define ASSERTC(cond, msg, val) if (!(cond)) {ALOGE("### ASSERT : %s line %d %s (%d) ###", __FILE__, __LINE__, msg, val);}

static void *lib_handle = NULL;
bt_host_ipc_interface_t *ipc_if = NULL;

struct a2dp_stream_out {
    struct audio_stream_out stream;
    struct a2dp_stream_common common;
};


typedef struct bt_hal_module {
    struct audio_hw_device *a2dp_device;
    struct a2dp_stream_out *a2dp_output;
    struct audio_config config;
    audio_channel_mask_t in_channel_mask;
    int bit_width;
    struct resampler_itfe *resampler;
    void *data;
    void *reformatted_buf;
} bt_hal_module_t;

static int bt_create_resampler(uint32_t in_rate,
                        struct bt_hal_module *bt)
{
    int err = 0;

    ALOGV("%s", __FUNCTION__);

    if (bt->resampler == NULL) {
        err = create_resampler(in_rate,
                               bt->config.sample_rate,
                               popcount(bt->config.channel_mask),
                               RESAMPLER_QUALITY_DEFAULT,
                               NULL,
                               &bt->resampler);

        if (err) {
            ALOGE("%s: Failed to create resampler", __FUNCTION__);
            free(bt->resampler);
            bt->resampler = NULL;
            return -EINVAL;
        }

        ALOGV("%s: Created resampler[%p] with in_sample_rate[%d], out_sample_rate[%d]", __FUNCTION__, bt->resampler, in_rate, bt->config.sample_rate);
    }
    return 0;
}

static void bt_destroy_resampler(struct bt_hal_module *bt)
{
    ALOGV("%s", __FUNCTION__);

    if (bt->resampler != NULL) {
        release_resampler(bt->resampler);
        bt->resampler = NULL;
    }

    if (bt->data != NULL) {
        free(bt->data);
        bt->data = NULL;
    }
}

static uint32_t out_get_latency(struct a2dp_stream_out * out)
{
    int latency_us;

    ALOGV("%s",__FUNCTION__);

    latency_us = ((out->common.buffer_sz * 1000 ) /
                    audio_stream_out_frame_size(&out->stream) /
                    out->common.cfg.rate) * 1000;


    return (latency_us / 1000) + 200;
}

static int calc_audiotime(struct a2dp_config cfg, int bytes)
{
    int chan_count = popcount(cfg.channel_flags);
    int bytes_per_sample = 4;

    ASSERTC(cfg.format == AUDIO_FORMAT_PCM_8_24_BIT,
            "unsupported sample sz", cfg.format);

    return (int)(((int64_t)bytes * (1000000 / (chan_count * bytes_per_sample))) / cfg.rate);
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    struct a2dp_stream_out *out = (struct a2dp_stream_out *)stream;
    int sent;
    int us_delay;

    ALOGV("write %zu bytes (fd %d)", bytes, out->common.audio_fd);

    pthread_mutex_lock(&out->common.lock);
    if (out->common.state == AUDIO_A2DP_STATE_SUSPENDED ||
            out->common.state == AUDIO_A2DP_STATE_STOPPING) {
        ALOGV("stream suspended or closing");
        goto error;
    }

    /* only allow autostarting if we are in stopped or standby */
    if ((out->common.state == AUDIO_A2DP_STATE_STOPPED) ||
        (out->common.state == AUDIO_A2DP_STATE_STANDBY))
    {
        if (ipc_if->start_audio_datapath(&out->common) < 0)
        {
            goto error;
        }
    }
    else if (out->common.state != AUDIO_A2DP_STATE_STARTED)
    {
        ALOGE("%s: stream not in stopped or standby", __FUNCTION__);
        goto error;
    }

    pthread_mutex_unlock(&out->common.lock);

    sent = ipc_if->skt_write(out->common.audio_fd, buffer,  bytes);
    pthread_mutex_lock(&out->common.lock);

    if (sent == -1)
    {
        ipc_if->skt_disconnect(out->common.audio_fd);
        out->common.audio_fd = AUDIO_SKT_DISCONNECTED;
        if ((out->common.state != AUDIO_A2DP_STATE_SUSPENDED) &&
                (out->common.state != AUDIO_A2DP_STATE_STOPPING)) {
            out->common.state = AUDIO_A2DP_STATE_STOPPED;
        } else {
            ALOGE("%s: write failed : stream suspended, avoid resetting state", __FUNCTION__);
        }
        goto error;
    }

    pthread_mutex_unlock(&out->common.lock);
    return bytes;

error:
    pthread_mutex_unlock(&out->common.lock);
    us_delay = calc_audiotime(out->common.cfg, bytes);
    usleep(us_delay);
    return bytes;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    struct a2dp_stream_out *out = (struct a2dp_stream_out *)stream;
    ALOGV("format 0x%x", out->common.cfg.format);
    return out->common.cfg.format;
}

static uint32_t out_get_channels(const struct audio_stream *stream)
{
    struct a2dp_stream_out *out = (struct a2dp_stream_out *)stream;

    ALOGV("channels 0x%" PRIx32, out->common.cfg.channel_flags);

    return out->common.cfg.channel_flags;
}


int audio_extn_bt_hal_load(void **handle)
{
    hw_module_t *mod;
    int status = 0;
    struct bt_hal_module *bt = NULL;

    ALOGV("%s", __FUNCTION__);

    if (handle == NULL) {
        status = -EINVAL;
        goto EXIT;
    }

    bt = malloc(sizeof(bt_hal_module_t));
    if (bt == NULL){
        ALOGE("%s: Memory allocation failed!", __FUNCTION__);
        status = -ENOMEM;
        goto EXIT;
    }

    *handle = bt;

    status = hw_get_module_by_class(AUDIO_HARDWARE_MODULE_ID/*_A2DP*/,
                                    (const char*)"a2dp",
                                    (const hw_module_t**)&mod);
    if (status) {
        ALOGE("%s: Could not get a2dp hardware module", __FUNCTION__);
        goto EXIT;
    }

    bt->a2dp_device = calloc(1, sizeof(struct audio_hw_device));
    if (!bt->a2dp_device)
        return -ENOMEM;

    bt->a2dp_device->common.module = (struct hw_module_t *) mod;

    if (status) {
        ALOGE("%s: couldn't open a2dp audio hw device", __FUNCTION__);
        goto EXIT;
    }

EXIT:
    return status;
}

int audio_extn_bt_hal_unload(void *handle)
{
    int status = 0;
    struct bt_hal_module *bt = NULL;

    ALOGV("%s", __FUNCTION__);

    if (handle == NULL) {
        status = -EINVAL;
        goto EXIT;
    }

    bt = (struct bt_hal_module *) handle;

    if (!bt->a2dp_device) {
        ALOGE("%s: No active A2dp output found", __FUNCTION__);
        goto EXIT;
    }

    if (bt->a2dp_output != NULL) {
        audio_extn_bt_hal_close_output_stream(bt);
        bt->a2dp_output = NULL;
    }

    if (bt->reformatted_buf != NULL) {
        free(bt->reformatted_buf);
        bt->reformatted_buf = NULL;
    }

    free(bt->a2dp_device);

EXIT:
    return status;
}

int audio_extn_bt_hal_open_output_stream(void *handle, int in_rate, audio_channel_mask_t channel_mask, int bit_width)
{
    int status = 0;
    struct bt_hal_module *bt = NULL;

    ALOGV("%s", __FUNCTION__);

    if (handle == NULL) {
        status = -EINVAL;
        goto EXIT;
    }

    bt = (struct bt_hal_module *) handle;

    if (bt->a2dp_device == NULL) {
        ALOGE("%s: Invalid device instance!", __FUNCTION__);
        status = -EINVAL;
        goto EXIT;
    }

    if (channel_mask != AUDIO_CHANNEL_OUT_STEREO && channel_mask != AUDIO_CHANNEL_OUT_MONO) {
        status = -EINVAL;
        goto EXIT;
    }

    if (bit_width != CODEC_BACKEND_DEFAULT_BIT_WIDTH) {
        status = -EINVAL;
        goto EXIT;
    }

    bt->in_channel_mask = channel_mask;
    bt->bit_width = bit_width;

    bt->a2dp_output = (struct a2dp_stream_out *)calloc(1, sizeof(struct a2dp_stream_out));

    if (!bt->a2dp_output) {
        status = -ENOMEM;
        goto EXIT;
    }

    lib_handle = dlopen("libbthost_if.so", RTLD_NOW);
    if (!lib_handle)
    {
        ALOGV("Failed to load bthost-ipc library %s",dlerror());
        status = -EINVAL;
        goto EXIT;
    }
    else
    {
        ipc_if = (bt_host_ipc_interface_t*) dlsym(lib_handle,"BTHOST_IPC_INTERFACE");
        if (!ipc_if)
        {
            ALOGE("%s: Failed to load BT IPC library symbol", __FUNCTION__);
            status  = -EINVAL;
            goto EXIT;
        }
    }

    bt->a2dp_output->stream.common.get_channels = out_get_channels;
    bt->a2dp_output->stream.common.get_format = out_get_format;
    bt->a2dp_output->stream.write = out_write;

    /* initialize a2dp specifics */
    ipc_if->a2dp_stream_common_init(&bt->a2dp_output->common);
    bt->a2dp_output->common.cfg.channel_flags = AUDIO_STREAM_DEFAULT_CHANNEL_FLAG;
    bt->a2dp_output->common.cfg.format = AUDIO_FORMAT_PCM_8_24_BIT;
    bt->a2dp_output->common.cfg.rate = AUDIO_STREAM_DEFAULT_RATE;

    /* set output bt->config values */

    bt->config.format = bt->a2dp_output->common.cfg.format;
    bt->config.sample_rate = bt->a2dp_output->common.cfg.rate;
    bt->config.channel_mask = bt->a2dp_output->common.cfg.channel_flags;

    ipc_if->a2dp_open_ctrl_path(&bt->a2dp_output->common);

    if (bt->a2dp_output->common.ctrl_fd == AUDIO_SKT_DISCONNECTED)
    {
        ALOGE("%s: ctrl socket failed to connect (%s)", __FUNCTION__, strerror(errno));
        status = -EINVAL;
        goto EXIT;
    }

    if (ipc_if->a2dp_command(&bt->a2dp_output->common, A2DP_CTRL_CMD_OFFLOAD_NOT_SUPPORTED) == 0) {
        ALOGI("Streaming mode set successfully");
    }
    /* Delay to ensure Headset is in proper state when START is initiated
       from DUT immediately after the connection due to ongoing music playback. */
    usleep(250000);
    if (status) {
        ALOGE("%s: Failed to open output stream for a2dp: status %d", __FUNCTION__, status);
        goto EXIT;
    } else {

        if (in_rate != 44100) {
            bt_create_resampler(in_rate, bt);
        }
        if (bt->config.format != AUDIO_FORMAT_PCM_16_BIT) {
            bt->reformatted_buf = malloc(DEFAULT_BUF_SIZE * 4);
            if (bt->reformatted_buf == NULL) {
                ALOGE("%s: memory allocation failed!", __FUNCTION__);
                status = -ENOMEM;
                goto EXIT;
            }
        }
    }

EXIT:
    if (status != 0 && bt->a2dp_output != NULL) {
        free(bt->a2dp_output);
        bt->a2dp_output = NULL;
    }
    return status;
}

int audio_extn_bt_hal_close_output_stream(void *handle)
{
    int status = 0;
    struct bt_hal_module *bt = NULL;

    ALOGV("%s", __FUNCTION__);

    if (handle == NULL) {
        status = -EINVAL;
        goto EXIT;
    }

    bt = (struct bt_hal_module *) handle;

    if (bt->a2dp_device == NULL) {
        ALOGI("%s: No active A2dp output found", __FUNCTION__);
        bt->a2dp_output = NULL;
        goto EXIT;
    }

    if (bt->a2dp_output == NULL) {
        ALOGE("%s: Invalid A2DP stream instance!", __FUNCTION__);
        status = -EINVAL;
        goto EXIT;
    }

    bt_destroy_resampler(bt);

    pthread_mutex_lock(&bt->a2dp_output->common.lock);
    if ((bt->a2dp_output->common.state == AUDIO_A2DP_STATE_STARTED) ||
            (bt->a2dp_output->common.state == AUDIO_A2DP_STATE_STOPPING))
        ipc_if->stop_audio_datapath(&bt->a2dp_output->common);

    ipc_if->skt_disconnect(bt->a2dp_output->common.ctrl_fd);
    bt->a2dp_output->common.ctrl_fd = AUDIO_SKT_DISCONNECTED;
    if (lib_handle)
        dlclose(lib_handle);
    free(bt->a2dp_output);
    pthread_mutex_unlock(&bt->a2dp_output->common.lock);
    bt->a2dp_output = NULL;

    if (bt->reformatted_buf != NULL) {
        free(bt->reformatted_buf);
        bt->reformatted_buf = NULL;
    }
EXIT:
    return status;
}

int audio_extn_bt_hal_out_write(void *handle, void *buf, int size)
{
    int status = 0;
    size_t in_frames = 0;
    size_t out_frames = 0;
    struct bt_hal_module *bt = NULL;

    ALOGV("%s", __FUNCTION__);

    if (handle == NULL) {
        status = -EINVAL;
        goto EXIT;
    }

    bt = (struct bt_hal_module *) handle;

    in_frames = size/(popcount(bt->in_channel_mask) * (bt->bit_width / 8));

    if (bt->resampler != NULL) {
        if (bt->data == NULL) {
            bt->data = malloc(size);
            if (bt->data == NULL){
                ALOGE("%s: Memory Allocation failed!", __FUNCTION__);
                status = -EINVAL;
                goto EXIT;
            }
        }
        out_frames = in_frames;
        bt->resampler->resample_from_input(bt->resampler,
                                           (int16_t *)buf,
                                           &in_frames,
                                           (int16_t *)bt->data,
                                           &out_frames);

        if (out_frames > 0) {
            ALOGV("%s: writing %d bytes to BT device", __FUNCTION__, (int) (out_frames * popcount(bt->in_channel_mask) * (bt->bit_width / 8)));

            if (bt->reformatted_buf != NULL) {
                if (size > DEFAULT_BUF_SIZE) {
                    bt->reformatted_buf = realloc(bt->reformatted_buf, size * 4);
                    if (bt->reformatted_buf == NULL) {
                        status = -ENOMEM;
                        goto EXIT;
                    }
                }

                memcpy_by_audio_format(bt->reformatted_buf, bt->config.format, bt->data, AUDIO_FORMAT_PCM_16_BIT, out_frames * popcount(bt->in_channel_mask));
                (bt->a2dp_output)->stream.write(&bt->a2dp_output->stream, bt->reformatted_buf, out_frames * popcount(bt->in_channel_mask) * (bt->bit_width/8) * 2);
            } else {
                (bt->a2dp_output)->stream.write(&bt->a2dp_output->stream, bt->data, (out_frames * popcount(bt->in_channel_mask) * (bt->bit_width / 8)));
            }
        }
    } else {
        if (bt->reformatted_buf != NULL) {
            if (size > DEFAULT_BUF_SIZE) {
                bt->reformatted_buf = realloc(bt->reformatted_buf, size * 4);
                if (bt->reformatted_buf == NULL) {
                    status = -ENOMEM;
                    goto EXIT;
                }
            }

            memcpy_by_audio_format(bt->reformatted_buf, bt->config.format, buf, AUDIO_FORMAT_PCM_16_BIT, in_frames * popcount(bt->in_channel_mask));
            (bt->a2dp_output)->stream.write(&bt->a2dp_output->stream, bt->reformatted_buf, size * 2);
        } else {
            (bt->a2dp_output)->stream.write(&bt->a2dp_output->stream, buf, size);
        }
    }

EXIT:
    return status;
}

int audio_extn_bt_hal_get_latency(void *handle) {
    int status = 0;
    struct bt_hal_module *bt = NULL;
    ALOGV("%s", __FUNCTION__);

    if (handle == NULL) {
        status = -EINVAL;
    }

    bt = (struct bt_hal_module *) handle;
    if (bt->a2dp_output != NULL) {
        status = out_get_latency(bt->a2dp_output);
    } else {
        status = -EINVAL;
    }

    return status;
}

struct audio_stream_out *audio_extn_bt_hal_get_output_stream(void *handle)
{
    struct bt_hal_module *bt = NULL;

    ALOGV("%s", __FUNCTION__);

    if (handle == NULL) {
        return NULL;
    }

    bt = (struct bt_hal_module *) handle;

    return (bt->a2dp_output == NULL)? NULL: &bt->a2dp_output->stream;
}

void *audio_extn_bt_hal_get_device(void *handle)
{
    int status = 0;
    struct bt_hal_module *bt = NULL;

    ALOGV("%s", __FUNCTION__);

    if (handle == NULL) {
        status = -EINVAL;
        goto EXIT;
    }

    bt = (struct bt_hal_module *) handle;

    return bt->a2dp_device;

EXIT:
    return NULL;
}
