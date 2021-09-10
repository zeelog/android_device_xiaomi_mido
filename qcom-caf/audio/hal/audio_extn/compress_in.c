/*
* Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "audio_hw_cin"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <cutils/properties.h>
#include <cutils/str_parms.h>
#include <log/log.h>
#include <pthread.h>

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"

#include <hardware/audio.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "audio_extn.h"
#include "audio_defs.h"
#include "sound/compress_params.h"

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_COMPR_IN
#include <log_utils.h>
#endif
/* default timestamp metadata definition if not defined in kernel*/
#ifndef COMPRESSED_TIMESTAMP_FLAG
#define COMPRESSED_TIMESTAMP_FLAG 0
struct snd_codec_metadata {
uint64_t timestamp;
};
#define compress_config_set_timstamp_flag(config) (-ENOSYS)
#else
#ifdef AUDIO_GKI_ENABLED
/* (config).codec->reserved[1] is for flags */
#define compress_config_set_timstamp_flag(config) \
            (config)->codec->reserved[1] |= COMPRESSED_TIMESTAMP_FLAG
#else
#define compress_config_set_timstamp_flag(config) \
            (config)->codec->flags |= COMPRESSED_TIMESTAMP_FLAG
#endif /* AUDIO_GKI_ENABLED */
#endif /* COMPRESSED_TIMESTAMP_FLAG */

#define COMPRESS_RECORD_NUM_FRAGMENTS 8

struct cin_private_data {
    struct compr_config compr_config;
    struct compress *compr;
    bool usecase_acquired;
};

typedef struct cin_private_data cin_private_data_t;

static unsigned int cin_usecases_state;

static const audio_usecase_t cin_usecases[] = {
    USECASE_AUDIO_RECORD_COMPRESS2,
    USECASE_AUDIO_RECORD_COMPRESS3,
    USECASE_AUDIO_RECORD_COMPRESS4,
    USECASE_AUDIO_RECORD_COMPRESS5,
    USECASE_AUDIO_RECORD_COMPRESS6
};

static pthread_mutex_t cin_lock = PTHREAD_MUTEX_INITIALIZER;

bool cin_applicable_stream(struct stream_in *in)
{
    if (in->flags & (AUDIO_INPUT_FLAG_COMPRESS | AUDIO_INPUT_FLAG_TIMESTAMP))
        return true;

    return false;
}

/* all cin_xxx calls must be made on an input
 * only after validating that input against cin_attached_usecase
 * except below calls
 * 1. cin_applicable_stream(in)
 * 2. cin_configure_input_stream(in, in_config)
 */

bool cin_attached_usecase(struct stream_in *in)
{
    unsigned int i = 0;
    audio_usecase_t uc_id = in->usecase;

    for (i = 0; i < sizeof(cin_usecases)/
                    sizeof(cin_usecases[0]); i++) {
        if (uc_id == cin_usecases[i] && in->cin_extn != NULL)
            return true;
    }
    return false;
}

static audio_usecase_t get_cin_usecase(void)
{
    audio_usecase_t ret_uc = USECASE_INVALID;
    unsigned int i, num_usecase = sizeof(cin_usecases) / sizeof(cin_usecases[0]);
    char value[PROPERTY_VALUE_MAX] = {0};

    property_get("vendor.audio.record.multiple.enabled", value, NULL);
    if (!(atoi(value) || !strncmp("true", value, 4)))
        num_usecase = 1; /* If prop is not set, limit the num of record usecases to 1 */

    ALOGV("%s: num_usecase: %d", __func__, num_usecase);
    pthread_mutex_lock(&cin_lock);
    for (i = 0; i < num_usecase; i++) {
        if (!(cin_usecases_state & (0x1 << i))) {
            cin_usecases_state |= 0x1 << i;
            ret_uc = cin_usecases[i];
            break;
        }
    }
    pthread_mutex_unlock(&cin_lock);
    ALOGV("%s: picked usecase: %d", __func__, ret_uc);
    return ret_uc;
}

static void free_cin_usecase(audio_usecase_t uc_id)
{
    unsigned int i;

    ALOGV("%s: free usecase %d", __func__, uc_id);
    pthread_mutex_lock(&cin_lock);
    for (i = 0; i < sizeof(cin_usecases) /
                    sizeof(cin_usecases[0]); i++) {
        if (uc_id == cin_usecases[i]) {
            cin_usecases_state &= ~(0x1 << i);
            break;
        }
    }
    pthread_mutex_unlock(&cin_lock);
}

bool cin_format_supported(audio_format_t format)
{
    if (format == AUDIO_FORMAT_IEC61937)
        return true;
    else
        return false;
}

int cin_acquire_usecase(struct stream_in *in)
{
    audio_usecase_t usecase = USECASE_INVALID;
    cin_private_data_t *cin_data = (cin_private_data_t *) in->cin_extn;

    if (cin_data->usecase_acquired) {
        ALOGW("%s: in %p, usecase already acquired!", __func__, in);
        return 0;
    }

    usecase = get_cin_usecase();
    if (usecase == USECASE_INVALID) {
        ALOGE("%s: in %p, failed to acquire usecase, max count reached!", __func__, in);
        return -EBUSY;
    }

    in->usecase = usecase;
    cin_data->usecase_acquired = true;
    return 0;
}

size_t cin_get_buffer_size(struct stream_in *in)
{
    size_t sz = 0;
    cin_private_data_t *cin_data = (cin_private_data_t *) in->cin_extn;

    sz = cin_data->compr_config.fragment_size;
    if ((in->flags & AUDIO_INPUT_FLAG_TIMESTAMP) ||
        (in->flags & AUDIO_INPUT_FLAG_PASSTHROUGH))
        sz -= sizeof(struct snd_codec_metadata);

    ALOGV("%s: in %p, flags 0x%x, cin_data %p, size %zd",
                  __func__, in, in->flags, cin_data, sz);
    return sz;
}

int cin_open_input_stream(struct stream_in *in)
{
    int ret = -EINVAL;
    struct audio_device *adev = in->dev;
    cin_private_data_t *cin_data = (cin_private_data_t *) in->cin_extn;

    ALOGV("%s: in %p, cin_data %p", __func__, in, cin_data);

    if (!cin_data->usecase_acquired) {
        ALOGE("%s: in %p, invalid state: usecase not acquired yet!", __func__, in);
        return ret;
    }

    cin_data->compr = compress_open(adev->snd_card, in->pcm_device_id,
                                    COMPRESS_OUT, &cin_data->compr_config);
    if (cin_data->compr == NULL || !is_compress_ready(cin_data->compr)) {
        ALOGE("%s: %s", __func__,
              cin_data->compr ? compress_get_error(cin_data->compr) : "null");
        if (cin_data->compr) {
            compress_close(cin_data->compr);
            cin_data->compr = NULL;
        }
        ret = -EIO;
    } else {
        ret = 0;
    }
    return ret;
}

void cin_stop_input_stream(struct stream_in *in)
{
    cin_private_data_t *cin_data = (cin_private_data_t *) in->cin_extn;

    ALOGV("%s: in %p, cin_data %p", __func__, in, cin_data);
    if (cin_data->compr) {
        compress_stop(cin_data->compr);
    }
}


void cin_close_input_stream(struct stream_in *in)
{
    cin_private_data_t *cin_data = (cin_private_data_t *) in->cin_extn;

    ALOGV("%s: in %p, cin_data %p", __func__, in, cin_data);
    if (cin_data->compr) {
        compress_close(cin_data->compr);
        cin_data->compr = NULL;
    }

    if (cin_data->usecase_acquired) {
        free_cin_usecase(in->usecase);
        cin_data->usecase_acquired = false;
    }
}

void cin_free_input_stream_resources(struct stream_in *in)
{
    cin_private_data_t *cin_data = (cin_private_data_t *) in->cin_extn;

    ALOGV("%s: in %p, cin_data %p", __func__, in, cin_data);
    if (cin_data) {
        free(cin_data->compr_config.codec);
        free(cin_data);
    }
}

int cin_read(struct stream_in *in, void *buffer,
                        size_t bytes, size_t *bytes_read)
{
    int ret = -EINVAL;
    size_t read_size = bytes;
    size_t mdata_size = (sizeof(struct snd_codec_metadata));
    cin_private_data_t *cin_data = (cin_private_data_t *) in->cin_extn;

    if (cin_data->compr) {
        /* start stream if not already done */
        if (!is_compress_running(cin_data->compr))
            compress_start(cin_data->compr);

        if (!(in->flags & (AUDIO_INPUT_FLAG_TIMESTAMP | AUDIO_INPUT_FLAG_PASSTHROUGH)))
            mdata_size = 0;

        if (buffer && read_size) {
            read_size = compress_read(cin_data->compr, buffer, read_size);
            if (read_size == bytes) {
                /* set ret to 0 if compress_read succeeded*/
                ret = 0;
                *bytes_read = bytes;
                /* data from DSP comes in 24_8 format, convert it to 8_24 */
                if (in->format == AUDIO_FORMAT_PCM_8_24_BIT) {
                    if (audio_extn_utils_convert_format_24_8_to_8_24(
                                          (char *)buffer + mdata_size, bytes) != bytes)
                        ret = -EIO;
                }
            } else {
                ret = errno;
                ALOGE("%s: failed error = %d, read = %zd, err_str %s", __func__,
                           ret, read_size, compress_get_error(cin_data->compr));
            }
        }
    }
    ALOGV("%s: in %p, flags 0x%x, buf %p, bytes %zd, read_size %zd, ret %d",
                        __func__, in, in->flags, buffer, bytes, read_size, ret);
    return ret;
}

int cin_configure_input_stream(struct stream_in *in, struct audio_config *in_config)
{
    struct audio_config config = {.format = 0};
    int ret = 0, buffer_size = 0, meta_size = sizeof(struct snd_codec_metadata);
    cin_private_data_t *cin_data = NULL;
    uint32_t compr_passthr = 0, flags = 0;

    if (!COMPRESSED_TIMESTAMP_FLAG &&
        (in->flags & (AUDIO_INPUT_FLAG_TIMESTAMP | AUDIO_INPUT_FLAG_PASSTHROUGH))) {
        ALOGE("%s: timestamp mode not supported!", __func__);
        return -EINVAL;
    }

    cin_data = (cin_private_data_t *) calloc(1, sizeof(cin_private_data_t));
    in->cin_extn = (void *)cin_data;
    if (!cin_data) {
        ALOGE("%s, allocation for private data failed!", __func__);
        return -ENOMEM;
    }

    cin_data->compr_config.codec = (struct snd_codec *)
                              calloc(1, sizeof(struct snd_codec));
    if (!cin_data->compr_config.codec) {
        ALOGE("%s, allocation for codec data failed!", __func__);
        ret = -ENOMEM;
        goto err_config;
    }

    config.sample_rate = in->sample_rate;
    config.channel_mask = in->channel_mask;
    config.format = in->format;
    in->config.channels = audio_channel_count_from_in_mask(in->channel_mask);
    buffer_size = audio_extn_utils_get_input_buffer_size(config.sample_rate, config.format,
                    in->config.channels, in_config->offload_info.duration_us / 1000, false);

    cin_data->compr_config.fragment_size = buffer_size;
    cin_data->compr_config.codec->id = get_snd_codec_id(in->format);
    cin_data->compr_config.fragments = COMPRESS_RECORD_NUM_FRAGMENTS;
    cin_data->compr_config.codec->sample_rate = in->sample_rate;
    cin_data->compr_config.codec->ch_in = in->config.channels;
    cin_data->compr_config.codec->ch_out = in->config.channels;
    cin_data->compr_config.codec->format = hal_format_to_alsa(in->format);

    if (cin_data->compr_config.codec->id == SND_AUDIOCODEC_PCM)
        compr_passthr = LEGACY_PCM;
    else if (cin_data->compr_config.codec->id == SND_AUDIOCODEC_IEC61937)
        compr_passthr = PASSTHROUGH_IEC61937;
    else
        compr_passthr = PASSTHROUGH_GEN;

    if (in->flags & AUDIO_INPUT_FLAG_FAST) {
        ALOGD("%s: Setting latency mode to true", __func__);
        flags |= audio_extn_utils_get_perf_mode_flag();
    }

#ifdef AUDIO_GKI_ENABLED
    /* out->compr_config.codec->reserved[0] is for compr_passthr */
    cin_data->compr_config.codec->reserved[0] = compr_passthr;
    /* out->compr_config.codec->reserved[1] is for flags */
    cin_data->compr_config.codec->reserved[1] = flags;
#else
    cin_data->compr_config.codec->compr_passthr =  compr_passthr;
    cin_data->compr_config.codec->flags = flags;
#endif
    if ((in->flags & AUDIO_INPUT_FLAG_TIMESTAMP) ||
        (in->flags & AUDIO_INPUT_FLAG_PASSTHROUGH)) {
        compress_config_set_timstamp_flag(&cin_data->compr_config);
        cin_data->compr_config.fragment_size += meta_size;
    }
    ALOGD("%s: format %d flags 0x%x SR %d CM 0x%x buf_size %d in %p",
          __func__, in->format, in->flags, in->sample_rate, in->channel_mask,
          cin_data->compr_config.fragment_size, in);
    return ret;

err_config:
    cin_free_input_stream_resources(in);
    return ret;
}
