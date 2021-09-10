/*
* Copyright (c) 2014-2020, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "passthru"
/*#define LOG_NDEBUG 0*/
#include <stdlib.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>
#include <cutils/str_parms.h>
#include <log/log.h>
#include <unistd.h>
#include <pthread.h>
#include "audio_hw.h"
#include "audio_extn.h"
#include "platform_api.h"
#include <platform.h>

#include "sound/compress_params.h"

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_PASSTH
#include <log_utils.h>
#endif
/*
 * Offload buffer size for compress passthrough
 */

#ifdef DTSHD_PARSER_ENABLED
#include "audio_parsers.h"

/* list of all supported DTS transmission sample rates */
static const int dts_transmission_sample_rates[] = {
    44100, 48000, 88200, 96000, 176400, 192000
};

 /*
 * for DTSHD stream one frame size can be upto 36kb and to extract iec61937
 * info for parsing usecase  minimum one frame needs to be sent to dts parser
 */
#define MAX_COMPRESS_PASSTHROUGH_FRAGMENT_SIZE (36 * 1024)
#else
#define MAX_COMPRESS_PASSTHROUGH_FRAGMENT_SIZE (8 * 1024)
#endif

#define MIN_COMPRESS_PASSTHROUGH_FRAGMENT_SIZE (2 * 1024)

#define DDP_COMPRESS_PASSTHROUGH_FRAGMENT_SIZE (10 * 1024)

static const audio_format_t audio_passthru_formats[] = {
    AUDIO_FORMAT_AC3,
    AUDIO_FORMAT_E_AC3,
    AUDIO_FORMAT_E_AC3_JOC,
    AUDIO_FORMAT_DTS,
    AUDIO_FORMAT_DTS_HD,
    AUDIO_FORMAT_DOLBY_TRUEHD,
    AUDIO_FORMAT_IEC61937
};

//external function depedency
static fp_platform_is_edid_supported_format_t fp_platform_is_edid_supported_format;
static fp_platform_set_device_params_t fp_platform_set_device_params;
static fp_platform_edid_get_max_channels_t fp_platform_edid_get_max_channels;
static fp_platform_get_output_snd_device_t fp_platform_get_output_snd_device;
static fp_platform_get_codec_backend_cfg_t fp_platform_get_codec_backend_cfg;
static fp_platform_get_snd_device_name_t fp_platform_get_snd_device_name;
static fp_platform_is_edid_supported_sample_rate_t fp_platform_is_edid_supported_sample_rate;
static fp_audio_extn_keep_alive_start_t fp_audio_extn_keep_alive_start;
static fp_audio_extn_keep_alive_stop_t fp_audio_extn_keep_alive_stop;
static fp_audio_extn_utils_is_dolby_format_t fp_audio_extn_utils_is_dolby_format;

/*
 * This atomic var is incremented/decremented by the offload stream to notify
 * other pcm playback streams that a pass thru session is about to start or has
 * finished. This hint can be used by the other streams to move to standby or
 * start calling pcm_write respectively.
 * This behavior is necessary as the DSP backend can only be configured to one
 * of PCM or compressed.
 */
static volatile int32_t compress_passthru_active;

int passthru_update_dts_stream_configuration(struct stream_out *out,
        const void *buffer, size_t bytes)
{
    struct audio_parser_codec_info codec_info;
    struct dtshd_iec61937_info dtshd_tr_info;
    int i;
    int ret;
    bool is_valid_transmission_rate = false;
    bool is_valid_transmission_channels = false;

    if (!out) {
        ALOGE("Invalid session");
        return -EINVAL;
    }

    if ((out->format != AUDIO_FORMAT_DTS) &&
        (out->format != AUDIO_FORMAT_DTS_HD)) {
        ALOGE("Non DTS format %d", out->format);
        return -EINVAL;
    }

    if (!buffer || bytes <= 0) {
        ALOGD("Invalid buffer %p size %lu skipping dts stream conf update",
                buffer, (unsigned long)bytes);
        out->sample_rate = 48000;
        out->compr_config.codec->sample_rate = out->sample_rate;
        out->compr_config.codec->ch_in = 2;
        out->channel_mask = audio_channel_out_mask_from_count(2);
        return -EINVAL;
    }

    /* codec format is AUDIO_PARSER_CODEC_DTSHD for both DTS and DTSHD as
     *  DTSHD parser can support both DTS and DTSHD
     */
    memset(&codec_info, 0, sizeof(struct audio_parser_codec_info));
    memset(&dtshd_tr_info, 0, sizeof(struct dtshd_iec61937_info));

    init_audio_parser((unsigned char *)buffer, bytes, AUDIO_PARSER_CODEC_DTSHD);
    codec_info.codec_type = AUDIO_PARSER_CODEC_DTSHD;
    if (!(ret = get_iec61937_info(&codec_info))) {
        dtshd_tr_info = codec_info.codec_config.dtshd_tr_info;
        ALOGD("dts new sample rate %d and channels %d\n",
               dtshd_tr_info.sample_rate,
               dtshd_tr_info.num_channels);
        for (i = 0; i < (sizeof(dts_transmission_sample_rates)/sizeof(int)); i++) {
            if (dts_transmission_sample_rates[i] ==
                    dtshd_tr_info.sample_rate) {
                out->sample_rate = dtshd_tr_info.sample_rate;
                out->compr_config.codec->sample_rate = out->sample_rate;
                is_valid_transmission_rate = true;
                break;
            }
        }
        /* DTS transmission channels should be 2 or 8*/
        if ((dtshd_tr_info.num_channels == 2) ||
                (dtshd_tr_info.num_channels == 8)) {
            out->compr_config.codec->ch_in = dtshd_tr_info.num_channels;
            out->channel_mask = audio_channel_out_mask_from_count
                (dtshd_tr_info.num_channels);
            is_valid_transmission_channels = true;
        }
    } else {
        ALOGE("%s:: get_iec61937_info failed %d", __func__, ret);
    }

    if (!is_valid_transmission_rate) {
        ALOGE("%s:: Invalid dts transmission rate %d\n using default sample rate 48000",
                                                    __func__, dtshd_tr_info.sample_rate);
        out->sample_rate = 48000;
        out->compr_config.codec->sample_rate = out->sample_rate;
    }

    if (!is_valid_transmission_channels) {
        ALOGE("%s:: Invalid transmission channels %d using default transmission"
              " channels as 2", __func__, dtshd_tr_info.num_channels);
        out->compr_config.codec->ch_in = 2;
        out->channel_mask = audio_channel_out_mask_from_count(2);
    }
    return 0;
}

bool passthru_is_supported_format(audio_format_t format)
{
    int32_t num_passthru_formats = sizeof(audio_passthru_formats) /
                                    sizeof(audio_passthru_formats[0]);
    int32_t i;

    for (i = 0; i < num_passthru_formats; i++) {
        if (format == audio_passthru_formats[i]) {
            ALOGD("%s : pass through format is true", __func__);
            return true;
        }
    }
    ALOGD("%s : pass through format is false", __func__);
    return false;
}

int passthru_get_channel_count(struct stream_out *out)
{
    int channel_count = DEFAULT_HDMI_OUT_CHANNELS;

    if (!out) {
        ALOGE("%s:: Invalid param out %p", __func__, out);
        return -EINVAL;
    }

    if (!passthru_is_supported_format(out->format)) {
        ALOGE("%s:: not a passthrough format %d", __func__, out->format);
        return -EINVAL;
    }

    switch(out->format) {
    case AUDIO_FORMAT_DOLBY_TRUEHD:
       channel_count = 8;
       break;
    case AUDIO_FORMAT_DTS:
    case AUDIO_FORMAT_DTS_HD:
#ifdef DTSHD_PARSER_ENABLED
       /* taken channel count from parser*/
       channel_count = audio_channel_count_from_out_mask(out->channel_mask);
#endif
       break;
    case AUDIO_FORMAT_IEC61937:
       channel_count = audio_channel_count_from_out_mask(out->channel_mask);
   default:
       break;
   }

   ALOGE("%s: pass through channel count %d\n", __func__, channel_count);
   return channel_count;
}

/*
 * must be called with stream lock held
 * This function decides based on some rules whether the data
 * coming on stream out must be rendered or dropped.
 */
bool passthru_should_drop_data(struct stream_out * out)
{
    uint32_t compr_passthr = 0;
    /*Drop data only
     *stream is routed to HDMI and
     *stream has PCM format or
     *if a compress offload (DSP decode) session
     */

    if(out->compr_config.codec != NULL) {
#ifdef AUDIO_GKI_ENABLED
        /* out->compr_config.codec->reserved[0] is for compr_passthr */
        compr_passthr = out->compr_config.codec->reserved[0];
#else
        compr_passthr = out->compr_config.codec->compr_passthr;
#endif
    }

    if (compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_AUX_DIGITAL) &&
        (((out->format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_PCM) ||
        (compr_passthr == LEGACY_PCM))) {
        if (android_atomic_acquire_load(&compress_passthru_active) > 0) {
            ALOGI("drop data as pass thru is active");
            return true;
        }
    }

    return false;
}

/* called with adev lock held */
void passthru_on_start(struct stream_out * out)
{

    uint64_t max_period_us = 0;
    uint64_t temp;
    struct audio_usecase * usecase;
    struct listnode *node;
    struct stream_out * o;
    struct audio_device *adev = out->dev;

    if (android_atomic_acquire_load(&compress_passthru_active) > 0) {
        ALOGI("pass thru is already active");
        return;
    }

    ALOGV("inc pass thru count to notify other streams");
    android_atomic_inc(&compress_passthru_active);

    while (true) {
        /* find max period time among active playback use cases */
        list_for_each(node, &adev->usecase_list) {
            usecase = node_to_item(node, struct audio_usecase, list);
            if (usecase->stream.out && usecase->type == PCM_PLAYBACK &&
                compare_device_type(&usecase->device_list, AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
                o = usecase->stream.out;
                temp = o->config.period_size * 1000000LL / o->sample_rate;
                if (temp > max_period_us)
                    max_period_us = temp;
            }
        }

        if (max_period_us) {
            pthread_mutex_unlock(&adev->lock);
            usleep(2*max_period_us);
            max_period_us = 0;
            pthread_mutex_lock(&adev->lock);
        } else
            break;
    }
}

/* called with adev lock held */
void passthru_on_stop(struct stream_out * out)
{
    if (android_atomic_acquire_load(&compress_passthru_active) > 0) {
        /*
         * its possible the count is already zero if pause was called before
         * stop output stream
         */
        android_atomic_dec(&compress_passthru_active);
    }

    if (compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
        ALOGD("%s: passthru on aux digital, start keep alive", __func__);
        fp_audio_extn_keep_alive_start(KEEP_ALIVE_OUT_HDMI);
    }
}

void passthru_on_pause(struct stream_out * out __unused)
{
    if (android_atomic_acquire_load(&compress_passthru_active) == 0)
        return;
}

bool passthru_is_active()
{
    return android_atomic_acquire_load(&compress_passthru_active) > 0;
}

int passthru_set_parameters(struct audio_device *adev __unused,
                                       struct str_parms *parms)
{
    char value[32];
    int ret;
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_CONNECT, value, sizeof(value));
    if (ret >= 0) {
        int val = atoi(value);
        if (val & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            if (!passthru_is_active()) {
                ALOGV("%s: start keep alive on aux digital", __func__);
                fp_audio_extn_keep_alive_start(KEEP_ALIVE_OUT_HDMI);
            }
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, value,
                            sizeof(value));
    if (ret >= 0) {
        int val = atoi(value);
        if (val & AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            ALOGV("%s: stop keep_alive on aux digital on device", __func__);
            fp_audio_extn_keep_alive_stop(KEEP_ALIVE_OUT_HDMI);
        }
    }
    return 0;
}

bool passthru_is_enabled() { return true; }

void passthru_init(passthru_init_config_t init_config)
{
      fp_platform_is_edid_supported_format =
                                    init_config.fp_platform_is_edid_supported_format;
      fp_platform_set_device_params = init_config.fp_platform_set_device_params;
      fp_platform_edid_get_max_channels =
                                   init_config.fp_platform_edid_get_max_channels;
      fp_platform_get_output_snd_device = init_config.fp_platform_get_output_snd_device;
      fp_platform_get_codec_backend_cfg =
                                         init_config.fp_platform_get_codec_backend_cfg;
      fp_platform_get_snd_device_name = init_config.fp_platform_get_snd_device_name;
      fp_platform_is_edid_supported_sample_rate =
                                    init_config.fp_platform_is_edid_supported_sample_rate;
      fp_audio_extn_keep_alive_start = init_config.fp_audio_extn_keep_alive_start;
      fp_audio_extn_keep_alive_stop = init_config.fp_audio_extn_keep_alive_stop;
      fp_audio_extn_utils_is_dolby_format = init_config.fp_audio_extn_utils_is_dolby_format;
}

bool passthru_should_standby(struct stream_out * out __unused)
{
    return true;
}

bool passthru_is_convert_supported(struct audio_device *adev,
                                                 struct stream_out *out)
{

    bool convert = false;
    switch (out->format) {
    case AUDIO_FORMAT_E_AC3:
    case AUDIO_FORMAT_E_AC3_JOC:
        if (!fp_platform_is_edid_supported_format(adev->platform,
                                               out->format)) {
            if (fp_platform_is_edid_supported_format(adev->platform,
                                                  AUDIO_FORMAT_AC3)) {
                ALOGD("%s:PASSTHROUGH_CONVERT supported", __func__);
                convert = true;
            }
        }
        break;
    default:
        ALOGD("%s: PASSTHROUGH_CONVERT not supported for format 0x%x",
              __func__, out->format);
        break;
    }
    ALOGD("%s: convert %d", __func__, convert);
    return convert;
}

bool passthru_is_passt_supported(struct audio_device *adev,
                                         struct stream_out *out)
{
    bool passt = false;
    switch (out->format) {
    case AUDIO_FORMAT_E_AC3:
    case AUDIO_FORMAT_DTS_HD:
    case AUDIO_FORMAT_DOLBY_TRUEHD:
        if (fp_platform_is_edid_supported_format(adev->platform, out->format)) {
            ALOGV("%s:PASSTHROUGH supported for format %x",
                   __func__, out->format);
            passt = true;
        }
        break;
    case AUDIO_FORMAT_AC3:
        if (fp_platform_is_edid_supported_format(adev->platform, AUDIO_FORMAT_AC3)
            || fp_platform_is_edid_supported_format(adev->platform,
            AUDIO_FORMAT_E_AC3)) {
            ALOGV("%s:PASSTHROUGH supported for format %x",
                   __func__, out->format);
            passt = true;
        }
        break;
    case AUDIO_FORMAT_E_AC3_JOC:
         /* Check for DDP capability in edid for JOC contents.*/
         if (fp_platform_is_edid_supported_format(adev->platform,
             AUDIO_FORMAT_E_AC3)) {
             ALOGV("%s:PASSTHROUGH supported for format %x",
                   __func__, out->format);
             passt = true;
         }
         break;
    case AUDIO_FORMAT_DTS:
        if (fp_platform_is_edid_supported_format(adev->platform, AUDIO_FORMAT_DTS)
            || fp_platform_is_edid_supported_format(adev->platform,
            AUDIO_FORMAT_DTS_HD)) {
            ALOGV("%s:PASSTHROUGH supported for format %x",
                   __func__, out->format);
            passt = true;
        }
        break;
    default:
        ALOGV("%s:Passthrough not supported", __func__);
    }
    return passt;
}

void passthru_update_stream_configuration(
        struct audio_device *adev, struct stream_out *out,
        const void *buffer __unused, size_t bytes __unused)
{
    uint32_t compr_passthr = 0;

    if(out->compr_config.codec != NULL) {
        if (passthru_is_passt_supported(adev, out)) {
            ALOGV("%s:PASSTHROUGH", __func__);
            compr_passthr = PASSTHROUGH;
        } else if (passthru_is_convert_supported(adev, out)) {
            ALOGV("%s:PASSTHROUGH CONVERT", __func__);
            compr_passthr = PASSTHROUGH_CONVERT;
        } else if (out->format == AUDIO_FORMAT_IEC61937) {
            ALOGV("%s:PASSTHROUGH IEC61937", __func__);
            compr_passthr = PASSTHROUGH_IEC61937;
        } else {
            ALOGV("%s:NO PASSTHROUGH", __func__);
            compr_passthr = LEGACY_PCM;
       }
#ifdef AUDIO_GKI_ENABLED
        /* out->compr_config.codec->reserved[0] is for compr_passthr */
        out->compr_config.codec->reserved[0] = compr_passthr;
#else
        out->compr_config.codec->compr_passthr = compr_passthr;
#endif
    }

}

bool passthru_is_passthrough_stream(struct stream_out *out)
{
    //check passthrough system property
    if (!property_get_bool("vendor.audio.offload.passthrough", false)) {
        return false;
    }

    //check supported device, currently only on HDMI.
    if (compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
        //passthrough flag
        if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_PASSTHROUGH)
            return true;
        //direct flag, check supported formats.
        if (out->flags & AUDIO_OUTPUT_FLAG_DIRECT) {
            if (passthru_is_supported_format(out->format)) {
                if (fp_platform_is_edid_supported_format(out->dev->platform,
                        out->format)) {
                    ALOGV("%s : return true",__func__);
                    return true;
                } else if (fp_audio_extn_utils_is_dolby_format(out->format) &&
                            fp_platform_is_edid_supported_format(out->dev->platform,
                                AUDIO_FORMAT_AC3)){
                    //return true for EAC3/EAC3_JOC formats
                    //if sink supports only AC3
                    ALOGV("%s : return true",__func__);
                    return true;
                }
            }
        }
    }
    ALOGV("%s : return false",__func__);
    return false;
}

bool passthru_is_direct_passthrough(struct stream_out *out)
{
    if (((out != NULL) && passthru_is_passthrough_stream(out)) &&
          !passthru_is_convert_supported(out->dev, out))
        return true;
    else
        return false;
}

int passthru_get_buffer_size(audio_offload_info_t* info)
{
    uint32_t fragment_size = MIN_COMPRESS_PASSTHROUGH_FRAGMENT_SIZE;
    char value[PROPERTY_VALUE_MAX] = {0};

    if (((info->format == AUDIO_FORMAT_DOLBY_TRUEHD) ||
            (info->format == AUDIO_FORMAT_IEC61937)) &&
            property_get("vendor.audio.truehd.buffer.size.kb", value, "") &&
            atoi(value)) {
        fragment_size = atoi(value) * 1024;
        goto done;
    } else if ((info->format == AUDIO_FORMAT_DTS) ||
               (info->format == AUDIO_FORMAT_DTS_HD)) {
        fragment_size = MAX_COMPRESS_PASSTHROUGH_FRAGMENT_SIZE;
        goto done;
    } else if (info->format == AUDIO_FORMAT_E_AC3) {
        fragment_size = DDP_COMPRESS_PASSTHROUGH_FRAGMENT_SIZE;
        if(property_get("vendor.audio.ddp.buffer.size.kb", value, "") &&
                atoi(value)) {
            fragment_size = atoi(value) * 1024;
        }
        goto done;
    }
done:
    return fragment_size;

}

int passthru_set_volume(struct stream_out *out,  int mute)
{
    return fp_platform_set_device_params(out, DEVICE_PARAM_MUTE_ID, mute);
}

int passthru_set_latency(struct stream_out *out, int latency)
{
    return fp_platform_set_device_params(out, DEVICE_PARAM_LATENCY_ID, latency);
}

bool passthru_is_supported_backend_edid_cfg(struct audio_device *adev,
                                                   struct stream_out *out)
{
    struct audio_backend_cfg backend_cfg;
    backend_cfg.sample_rate = CODEC_BACKEND_DEFAULT_SAMPLE_RATE;
    backend_cfg.channels = CODEC_BACKEND_DEFAULT_CHANNELS;
    backend_cfg.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    backend_cfg.format = AUDIO_FORMAT_PCM_16_BIT;
    backend_cfg.passthrough_enabled = false;

    snd_device_t out_snd_device = SND_DEVICE_NONE;
    int max_edid_channels = fp_platform_edid_get_max_channels(out->dev->platform);

    out_snd_device = fp_platform_get_output_snd_device(adev->platform, out, USECASE_TYPE_MAX);

    if (fp_platform_get_codec_backend_cfg(adev, out_snd_device, &backend_cfg)) {
        ALOGE("%s: ERROR: Unable to get current backend config!!!", __func__);
        return false;
    }

    ALOGV("%s:becf: afe: bitwidth %d, samplerate %d channels %d format %d"
          ", device (%s)", __func__,  backend_cfg.bit_width,
          backend_cfg.sample_rate, backend_cfg.channels, backend_cfg.format,
          fp_platform_get_snd_device_name(out_snd_device));

    /* Check if the channels are supported */
    if (max_edid_channels < (int)backend_cfg.channels) {

        ALOGE("%s: ERROR: Unsupported channels in passthru mode!!!"
              " max_edid_channels - %d backend_channels - %d",
              __func__, max_edid_channels, backend_cfg.channels);
        return false;
    }

    /* Check if the sample rate supported */
    if (!fp_platform_is_edid_supported_sample_rate(adev->platform,
                                       backend_cfg.sample_rate)) {

        ALOGE("%s: ERROR: Unsupported sample rate in passthru mode!!!"
              " backend_samplerate - %d",
              __func__, backend_cfg.sample_rate);
        return false;
    }

    return true;
}
