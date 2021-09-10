/*
* Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "audio_hw_loopback"
/*#define LOG_NDEBUG 0*/
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#define MAX_NUM_PATCHES 1
#define MAX_NUM_HW_LOOPBACK_PATCHES 1
#define PATCH_HANDLE_INVALID 0xFFFF
#define MAX_SOURCE_PORTS_PER_PATCH 1
#define MAX_SINK_PORTS_PER_PATCH 1
#define HW_LOOPBACK_RX_VOLUME     "Trans Loopback RX Volume"
#define HW_LOOPBACK_RX_UNITY_GAIN 0x2000

#include <math.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>
#include <cutils/str_parms.h>
#include <cutils/sched_policy.h>
#include <log/log.h>
#include "audio_utils/primitives.h"
#include "audio_hw.h"
#include "platform_api.h"
#include <platform.h>
#include <system/thread_defs.h>
#include "audio_extn.h"
#include <sound/compress_params.h>
#include <sound/compress_offload.h>
#include <system/audio.h>

typedef enum patch_state {
    PATCH_INACTIVE,// Patch is not created yet
    PATCH_CREATED, // Patch created but not in running state yet, probably due
                   // to lack of proper port config
    PATCH_RUNNING, // Patch in running state, moves to this state when patch
                   // created and proper port config is available
} patch_state_t;

typedef struct loopback_patch {
    audio_patch_handle_t patch_handle_id;            /* patch unique ID */
    struct audio_port_config loopback_source;        /* Source port config */
    struct audio_port_config loopback_sink;          /* Sink port config */
    struct compress *source_stream;                  /* Source stream */
    struct compress *sink_stream;                    /* Sink stream */
    struct stream_inout patch_stream;                /* InOut type stream */
    patch_state_t patch_state;                       /* Patch operation state */
} loopback_patch_t;

typedef struct patch_db_struct {
    int32_t num_patches;
    loopback_patch_t loopback_patch[MAX_NUM_PATCHES];
} patch_db_t;

typedef struct audio_loopback {
    struct audio_device *adev;
    patch_db_t patch_db;
    audio_usecase_t uc_id_rx;
    audio_usecase_t uc_id_tx;
    usecase_type_t  uc_type_rx;
    usecase_type_t  uc_type_tx;
    pthread_mutex_t lock;
} audio_loopback_t;

typedef struct port_info {
    audio_port_handle_t      id;                /* port unique ID */
    audio_port_role_t        role;              /* sink or source */
    audio_port_type_t        type;              /* device, mix ... */
} port_info_t;

/* Audio loopback module struct */
static audio_loopback_t *audio_loopback_mod = NULL;

uint32_t format_to_bitwidth(audio_format_t format)
{
    switch (format) {
        case AUDIO_FORMAT_PCM_16_BIT:
            return 16;
        case AUDIO_FORMAT_PCM_8_BIT:
            return 8;
        case AUDIO_FORMAT_PCM_32_BIT:
            return 32;
        case AUDIO_FORMAT_PCM_8_24_BIT:
            return 32;
        case AUDIO_FORMAT_PCM_24_BIT_PACKED:
            return 24;
        default:
            return 16;
    }
}

/* Set loopback volume : for mute implementation */
static int hw_loopback_set_volume(struct audio_device *adev, int value)
{
    int32_t ret = 0;
    struct mixer_ctl *ctl;
    char mixer_ctl_name[MAX_LENGTH_MIXER_CONTROL_IN_INT];
    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
            "Transcode Loopback Rx Volume");

    ALOGD("%s: (%d)\n", __func__, value);

    ALOGD("%s: Setting HW loopback volume to %d \n", __func__, value);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }

    if(mixer_ctl_set_value(ctl, 0, value) < 0) {
        ALOGE("%s: Couldn't set HW Loopback Volume: [%d]", __func__, value);
        return -EINVAL;
    }

    ALOGV("%s: exit", __func__);
    return ret;
}

/* Initialize patch database */
int init_patch_database(patch_db_t* patch_db)
{
    int patch_init_rc = 0, patch_num=0;
    patch_db->num_patches = 0;
    for (patch_num=0;patch_num < MAX_NUM_PATCHES;patch_num++) {
        patch_db->loopback_patch[patch_num].patch_handle_id = (int32_t)
        PATCH_HANDLE_INVALID;
    }
    return patch_init_rc;
}

bool is_supported_sink_device(audio_devices_t sink_device_mask)
{
    if((sink_device_mask & AUDIO_DEVICE_OUT_SPEAKER) ||
       (sink_device_mask & AUDIO_DEVICE_OUT_WIRED_HEADSET) ||
       (sink_device_mask & AUDIO_DEVICE_OUT_WIRED_HEADPHONE) ||
       (sink_device_mask & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP) ||
       (sink_device_mask & AUDIO_DEVICE_OUT_LINE)) {
           return true;
       }
    return false;
}

/* Get patch type based on source and sink ports configuration */
/* Only ports of type 'DEVICE' are supported */
audio_patch_handle_t get_loopback_patch_type(loopback_patch_t*  loopback_patch)
{
    bool is_source_supported = false, is_sink_supported = false;
    audio_devices_t source_device = loopback_patch->loopback_source.ext.device.type;
    audio_devices_t sink_device = loopback_patch->loopback_sink.ext.device.type;

    source_device &= ~AUDIO_DEVICE_BIT_IN;

    if (loopback_patch->patch_handle_id != PATCH_HANDLE_INVALID) {
        ALOGE("%s, Patch handle already exists", __func__);
        return loopback_patch->patch_handle_id;
    }

    if (loopback_patch->loopback_source.role == AUDIO_PORT_ROLE_SOURCE) {
        switch (loopback_patch->loopback_source.type) {
            case AUDIO_PORT_TYPE_DEVICE :
                if ((loopback_patch->loopback_source.config_mask & AUDIO_PORT_CONFIG_FORMAT)) {
                    if ((source_device & AUDIO_DEVICE_IN_HDMI) ||
                        (source_device & AUDIO_DEVICE_IN_SPDIF) ||
                        (source_device & AUDIO_DEVICE_IN_BLUETOOTH_A2DP) ||
                        (source_device & AUDIO_DEVICE_IN_HDMI_ARC)) {

                       switch (loopback_patch->loopback_source.format) {
                           case AUDIO_FORMAT_PCM:
                           case AUDIO_FORMAT_PCM_16_BIT:
                           case AUDIO_FORMAT_PCM_8_24_BIT:
                           case AUDIO_FORMAT_PCM_24_BIT_PACKED:
                           case AUDIO_FORMAT_IEC61937:
                           case AUDIO_FORMAT_AC3:
                           case AUDIO_FORMAT_E_AC3:
                           case AUDIO_FORMAT_AAC_LATM_LC:
                           case AUDIO_FORMAT_AAC_LATM_HE_V1:
                           case AUDIO_FORMAT_AAC_LATM_HE_V2:
                           case AUDIO_FORMAT_SBC:
                              is_source_supported = true;
                           break;
                       }
                    } else if (source_device & AUDIO_DEVICE_IN_LINE) {
                       is_source_supported = true;
                    }
                }
            break;
            default :
                //Unsupported as of now, need to extend for other source types
                break;
        }
    }

    if (loopback_patch->loopback_sink.role == AUDIO_PORT_ROLE_SINK) {
        switch (loopback_patch->loopback_sink.type) {
        case AUDIO_PORT_TYPE_DEVICE :
            if ((loopback_patch->loopback_sink.config_mask &
                AUDIO_PORT_CONFIG_FORMAT) &&
                (is_supported_sink_device(loopback_patch->loopback_sink.ext.device.type))) {
                    switch (loopback_patch->loopback_sink.format) {
                    case AUDIO_FORMAT_PCM:
                    case AUDIO_FORMAT_PCM_16_BIT:
                    case AUDIO_FORMAT_PCM_32_BIT:
                    case AUDIO_FORMAT_PCM_8_24_BIT:
                    case AUDIO_FORMAT_PCM_24_BIT_PACKED:
                        is_sink_supported = true;
                        break;
                    default:
                        break;
                    }
            } else {
                ALOGE("%s, Unsupported sink port device %d", __func__,loopback_patch->loopback_sink.ext.device.type);
            }
            break;
        default :
            //Unsupported as of now, need to extend for other sink types
            break;
        }
    }
    if (is_source_supported && is_sink_supported) {
        return AUDIO_DEVICE_BIT_IN | source_device | sink_device;
    }
    ALOGE("%s, Unsupported source or sink port config", __func__);
    return loopback_patch->patch_handle_id;
}

/* Releases an existing loopback session */
/* Conditions : Session setup goes bad or actual session teardown */
int32_t release_loopback_session(loopback_patch_t *active_loopback_patch)
{
    int32_t ret = 0;
    struct audio_usecase *uc_info_rx, *uc_info_tx;
    struct audio_device *adev = audio_loopback_mod->adev;
    struct stream_inout *inout =  &active_loopback_patch->patch_stream;
    struct audio_port_config *source_patch_config = &active_loopback_patch->
                                                    loopback_source;
    int32_t pcm_dev_asm_rx_id = platform_get_pcm_device_id(USECASE_AUDIO_TRANSCODE_LOOPBACK_RX,
                                                           PCM_PLAYBACK);

    /* Close the PCM devices */
    if (active_loopback_patch->source_stream) {
        compress_close(active_loopback_patch->source_stream);
        active_loopback_patch->source_stream = NULL;
    } else {
        ALOGE("%s: Failed to close loopback stream in capture path",
            __func__);
    }
    if (active_loopback_patch->sink_stream) {
        compress_close(active_loopback_patch->sink_stream);
        active_loopback_patch->sink_stream = NULL;
    } else {
        ALOGE("%s: Failed to close loopback stream in playback path",
            __func__);
    }

    uc_info_tx = get_usecase_from_list(adev, audio_loopback_mod->uc_id_tx);
    if (uc_info_tx == NULL) {
        ALOGE("%s: Could not find the loopback usecase (%d) in the list",
            __func__, active_loopback_patch->patch_handle_id);
        return -EINVAL;
    }

    disable_audio_route(adev, uc_info_tx);

    /* Disable tx device */
    disable_snd_device(adev, uc_info_tx->in_snd_device);

    /* Reset backend device to default state */
    platform_invalidate_backend_config(adev->platform,uc_info_tx->in_snd_device);

    list_remove(&uc_info_tx->list);
    free(uc_info_tx);

    uc_info_rx = get_usecase_from_list(adev, audio_loopback_mod->uc_id_rx);
    if (uc_info_rx == NULL) {
        ALOGE("%s: Could not find the loopback usecase (%d) in the list",
            __func__, active_loopback_patch->patch_handle_id);
        return -EINVAL;
    }

    if (adev->offload_effects_stop_output != NULL)
            adev->offload_effects_stop_output(active_loopback_patch->patch_handle_id, pcm_dev_asm_rx_id);

    active_loopback_patch->patch_state = PATCH_INACTIVE;

    /* Get and set stream specific mixer controls */
    disable_audio_route(adev, uc_info_rx);

    /* Disable the rx device */
    disable_snd_device(adev, uc_info_rx->out_snd_device);

    list_remove(&uc_info_rx->list);
    free(uc_info_rx);

    if (inout->ip_hdlr_handle) {
        ret = audio_extn_ip_hdlr_intf_close(inout->ip_hdlr_handle, true, inout);
        if (ret < 0)
            ALOGE("%s: audio_extn_ip_hdlr_intf_close failed %d",__func__, ret);
    }

    /* close adsp hdrl session before standby */
    if (inout->adsp_hdlr_stream_handle) {
        ret = audio_extn_adsp_hdlr_stream_close(inout->adsp_hdlr_stream_handle);
        if (ret)
            ALOGE("%s: adsp_hdlr_stream_close failed %d",__func__, ret);
        inout->adsp_hdlr_stream_handle = NULL;
    }

    if (inout->ip_hdlr_handle) {
        audio_extn_ip_hdlr_intf_deinit(inout->ip_hdlr_handle);
        inout->ip_hdlr_handle = NULL;
    }

    ALOGD("%s: Release loopback session exit: status(%d)", __func__, ret);
    return ret;
}

/* Callback funtion called in the case of failures */
int loopback_stream_cb(stream_callback_event_t event, void *param, void *cookie)
{
    if (event == AUDIO_EXTN_STREAM_CBK_EVENT_ERROR) {
        pthread_mutex_lock(&audio_loopback_mod->lock);
        release_loopback_session(cookie);
        audio_loopback_mod->patch_db.num_patches--;
        pthread_mutex_unlock(&audio_loopback_mod->lock);
    }
    return 0;
}

#ifdef SNDRV_COMPRESS_RENDER_WINDOW
static loopback_patch_t *get_active_loopback_patch(audio_patch_handle_t handle)
{
    int n = 0;
    int patch_index = -1;
    loopback_patch_t *active_loopback_patch = NULL;

    for (n=0; n < MAX_NUM_PATCHES; n++) {
        if (audio_loopback_mod->patch_db.num_patches > 0) {
            if (audio_loopback_mod->patch_db.loopback_patch[n].patch_handle_id == handle) {
                patch_index = n;
                break;
            }
        } else {
            ALOGE("%s, No active audio loopback patch", __func__);
            return active_loopback_patch;
        }
    }

    if ((patch_index > -1) && (patch_index < MAX_NUM_PATCHES))
        active_loopback_patch = &(audio_loopback_mod->patch_db.loopback_patch[
                                patch_index]);
    else
        ALOGE("%s, Requested Patch handle does not exist", __func__);

    return active_loopback_patch;
}

int audio_extn_hw_loopback_set_render_window(audio_patch_handle_t handle,
                      struct audio_out_render_window_param *render_window)
{
    struct snd_compr_metadata metadata = {0};
    int ret = 0;
    loopback_patch_t *active_loopback_patch = get_active_loopback_patch(handle);

    if (active_loopback_patch == NULL) {
        ALOGE("%s: Invalid patch handle", __func__);
        ret = -EINVAL;
        goto exit;
    }

    if (render_window == NULL) {
        ALOGE("%s: Invalid render_window", __func__);
        ret = -EINVAL;
        goto exit;
    }

    metadata.key = SNDRV_COMPRESS_RENDER_WINDOW;
    /*render window start value */
    metadata.value[0] = 0xFFFFFFFF & render_window->render_ws; /* lsb */
    metadata.value[1] = \
            (0xFFFFFFFF00000000 & render_window->render_ws) >> 32; /* msb*/
    /*render window end value */
    metadata.value[2] = 0xFFFFFFFF & render_window->render_we; /* lsb */
    metadata.value[3] = \
            (0xFFFFFFFF00000000 & render_window->render_we) >> 32; /* msb*/

    ret = compress_set_metadata(active_loopback_patch->sink_stream, &metadata);

exit:
    return ret;
}
#else
int audio_extn_hw_loopback_set_render_window(struct audio_hw_device *dev,
                      audio_patch_handle_t handle __unused,
                      struct audio_out_render_window_param *render_window __unused)
{
    ALOGD("%s:: configuring render window not supported", __func__);
    return 0;
}
#endif

#if defined SNDRV_COMPRESS_LATENCY_MODE
static void transcode_loopback_util_set_latency_mode(
                             loopback_patch_t *active_loopback_patch,
                             uint32_t latency_mode)
{
    struct snd_compr_metadata metadata;

    metadata.key = SNDRV_COMPRESS_LATENCY_MODE;
    metadata.value[0] = latency_mode;
    ALOGV("%s: Setting latency mode %d",__func__, latency_mode);
    compress_set_metadata(active_loopback_patch->source_stream,&metadata);
}
#else
static void transcode_loopback_util_set_latency_mode(
                            loopback_patch_t *active_loopback_patch __unused,
                            uint32_t latency_mode __unused)
{
    ALOGD("%s:: Latency mode configuration not supported", __func__);
}
#endif

/* Create a loopback session based on active loopback patch selected */
int create_loopback_session(loopback_patch_t *active_loopback_patch)
{
    int32_t ret = 0, bits_per_sample;
    struct audio_usecase *uc_info_rx, *uc_info_tx;
    int32_t pcm_dev_asm_rx_id, pcm_dev_asm_tx_id;
    char dummy_write_buf[64];
    struct audio_device *adev = audio_loopback_mod->adev;
    struct compr_config source_config, sink_config;
    struct snd_codec codec;
    struct audio_port_config *source_patch_config = &active_loopback_patch->
                                                    loopback_source;
    struct audio_port_config *sink_patch_config = &active_loopback_patch->
                                                    loopback_sink;
    struct stream_inout *inout =  &active_loopback_patch->patch_stream;
    struct adsp_hdlr_stream_cfg hdlr_stream_cfg;
    struct stream_in loopback_source_stream;
    char prop_value[PROPERTY_VALUE_MAX] = {0};

    ALOGD("%s: Create loopback session begin", __func__);

    uc_info_rx = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));

    if (!uc_info_rx) {
        ALOGE("%s: Failure to open loopback session", __func__);
        return -ENOMEM;
    }

    uc_info_tx = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));

    if (!uc_info_tx) {
        free(uc_info_rx);
        ALOGE("%s: Failure to open loopback session", __func__);
        return -ENOMEM;
    }

    uc_info_rx->id = USECASE_AUDIO_TRANSCODE_LOOPBACK_RX;
    uc_info_rx->type = audio_loopback_mod->uc_type_rx;
    uc_info_rx->stream.inout = &active_loopback_patch->patch_stream;
    list_init(&uc_info_rx->device_list);
    assign_devices(&uc_info_rx->device_list,
                   &active_loopback_patch->patch_stream.out_config.device_list);
    uc_info_rx->in_snd_device = SND_DEVICE_NONE;
    uc_info_rx->out_snd_device = SND_DEVICE_NONE;


    uc_info_tx->id = USECASE_AUDIO_TRANSCODE_LOOPBACK_TX;
    uc_info_tx->type = audio_loopback_mod->uc_type_tx;
    uc_info_tx->stream.inout = &active_loopback_patch->patch_stream;
    list_init(&uc_info_tx->device_list);
    assign_devices(&uc_info_tx->device_list,
                   &active_loopback_patch->patch_stream.in_config.device_list);
    uc_info_tx->in_snd_device = SND_DEVICE_NONE;
    uc_info_tx->out_snd_device = SND_DEVICE_NONE;

    list_add_tail(&adev->usecase_list, &uc_info_rx->list);
    list_add_tail(&adev->usecase_list, &uc_info_tx->list);

    loopback_source_stream.source = AUDIO_SOURCE_UNPROCESSED;
    loopback_source_stream.device = inout->in_config.devices;
    loopback_source_stream.channel_mask = inout->in_config.channel_mask;
    loopback_source_stream.bit_width = inout->in_config.bit_width;
    loopback_source_stream.sample_rate = inout->in_config.sample_rate;
    loopback_source_stream.format = inout->in_config.format;

    memcpy(&loopback_source_stream.usecase, uc_info_rx,
           sizeof(struct audio_usecase));
    select_devices(adev, uc_info_rx->id);
    select_devices(adev, uc_info_tx->id);

    pcm_dev_asm_rx_id = platform_get_pcm_device_id(uc_info_rx->id, PCM_PLAYBACK);
    pcm_dev_asm_tx_id = platform_get_pcm_device_id(uc_info_tx->id, PCM_CAPTURE);

    if (pcm_dev_asm_rx_id < 0 || pcm_dev_asm_tx_id < 0 ) {
        ALOGE("%s: Invalid PCM devices (asm: rx %d tx %d) for the RX usecase(%d), TX usecase(%d)",
            __func__, pcm_dev_asm_rx_id, pcm_dev_asm_tx_id, uc_info_rx->id, uc_info_tx->id);
        ret = -EIO;
        goto exit;
    }

    ALOGD("%s: LOOPBACK PCM devices (rx: %d tx: %d) RX usecase(%d) TX usecase(%d)",
        __func__, pcm_dev_asm_rx_id, pcm_dev_asm_tx_id, uc_info_rx->id, uc_info_tx->id);

    /* setup a channel for client <--> adsp communication for stream events */
    inout->dev = adev;
    inout->client_callback = loopback_stream_cb;
    inout->client_cookie = active_loopback_patch;
    hdlr_stream_cfg.pcm_device_id = pcm_dev_asm_rx_id;
    hdlr_stream_cfg.flags = 0;
    hdlr_stream_cfg.type = PCM_PLAYBACK;
    ret = audio_extn_adsp_hdlr_stream_open(&inout->adsp_hdlr_stream_handle,
            &hdlr_stream_cfg);
    if (ret) {
        ALOGE("%s: adsp_hdlr_stream_open failed %d", __func__, ret);
        inout->adsp_hdlr_stream_handle = NULL;
        goto exit;
    }
    if (audio_extn_ip_hdlr_intf_supported(source_patch_config->format,false, true) ||
        audio_extn_ip_hdlr_intf_supported_for_copp(adev->platform)) {
        ret = audio_extn_ip_hdlr_intf_init(&inout->ip_hdlr_handle, NULL, NULL, adev,
                                           USECASE_AUDIO_TRANSCODE_LOOPBACK_RX);
        if (ret < 0) {
            ALOGE("%s: audio_extn_ip_hdlr_intf_init failed %d", __func__, ret);
            inout->ip_hdlr_handle = NULL;
            goto exit;
        }
    }

    if (source_patch_config->format == AUDIO_FORMAT_IEC61937) {
        // This is needed to set a known format to DSP and handle
        // any format change via ADSP event
        codec.id = AUDIO_FORMAT_AC3;
    }

    /* Set config for compress stream open in capture path */
    codec.id = get_snd_codec_id(source_patch_config->format);
    codec.ch_in = audio_channel_count_from_out_mask(source_patch_config->
                                                    channel_mask);
    codec.ch_out = 2; // Irrelevant for loopback case in this direction
    codec.sample_rate = source_patch_config->sample_rate;
    codec.format = hal_format_to_alsa(source_patch_config->format);
    source_config.fragment_size = 1024;
    source_config.fragments = 1;
    source_config.codec = &codec;

    /* Open compress stream in capture path */
    active_loopback_patch->source_stream = compress_open(adev->snd_card,
                        pcm_dev_asm_tx_id, COMPRESS_OUT, &source_config);
    if (active_loopback_patch->source_stream && !is_compress_ready(
        active_loopback_patch->source_stream)) {
        ALOGE("%s: %s", __func__, compress_get_error(active_loopback_patch->
        source_stream));
        active_loopback_patch->source_stream = NULL;
        ret = -EIO;
        goto exit;
    } else if (active_loopback_patch->source_stream == NULL) {
        ALOGE("%s: Failure to open loopback stream in capture path", __func__);
        ret = -EINVAL;
        goto exit;
    }

    /* Set config for compress stream open in playback path */
    codec.id = get_snd_codec_id(sink_patch_config->format);
    codec.ch_in = 2; // Irrelevant for loopback case in this direction
    codec.ch_out = audio_channel_count_from_out_mask(sink_patch_config->
                                                     channel_mask);
    codec.sample_rate = sink_patch_config->sample_rate;
    codec.format = hal_format_to_alsa(sink_patch_config->format);
    sink_config.fragment_size = 1024;
    sink_config.fragments = 1;
    sink_config.codec = &codec;

    /* Do not alter the location of sending latency mode property */
    /* Mode set on any stream but before both streams are open */
    if(property_get("vendor.audio.transcode.latency.mode", prop_value, "")) {
        uint32_t latency_mode = atoi(prop_value);
        transcode_loopback_util_set_latency_mode(active_loopback_patch,
                                                 latency_mode);
    }

    /* Open compress stream in playback path */
    active_loopback_patch->sink_stream = compress_open(adev->snd_card,
                         pcm_dev_asm_rx_id, COMPRESS_IN, &sink_config);
    if (active_loopback_patch->sink_stream && !is_compress_ready(
        active_loopback_patch->sink_stream)) {
        ALOGE("%s: %s", __func__, compress_get_error(active_loopback_patch->
                sink_stream));
        active_loopback_patch->sink_stream = NULL;
        ret = -EIO;
        goto exit;
    } else if (active_loopback_patch->sink_stream == NULL) {
        ALOGE("%s: Failure to open loopback stream in playback path", __func__);
        ret = -EINVAL;
        goto exit;
    }

    active_loopback_patch->patch_state = PATCH_CREATED;

    if (compress_start(active_loopback_patch->source_stream) < 0) {
        ALOGE("%s: Failure to start loopback stream in capture path",
        __func__);
        ret = -EINVAL;
        goto exit;
    }

    /* Dummy compress_write to ensure compress_start does not fail */
    compress_write(active_loopback_patch->sink_stream, dummy_write_buf, 64);
    if (compress_start(active_loopback_patch->sink_stream) < 0) {
        ALOGE("%s: Cannot start loopback stream in playback path",
                __func__);
        ret = -EINVAL;
        goto exit;
    }
    if (inout->ip_hdlr_handle) {
        ret = audio_extn_ip_hdlr_intf_open(inout->ip_hdlr_handle, true, inout,
                                           USECASE_AUDIO_TRANSCODE_LOOPBACK_RX);
        if (ret < 0) {
            ALOGE("%s: audio_extn_ip_hdlr_intf_open failed %d",__func__, ret);
            goto exit;
        }
    }

    /* Move patch state to running, now that session is set up */
    active_loopback_patch->patch_state = PATCH_RUNNING;
    ALOGD("%s: Create loopback session end: status(%d)", __func__, ret);

    if (adev->offload_effects_start_output != NULL)
        adev->offload_effects_start_output(active_loopback_patch->patch_handle_id,
                                           pcm_dev_asm_rx_id, adev->mixer);

    return ret;

exit:
    ALOGE("%s: Problem in Loopback session creation: \
            status(%d), releasing session ", __func__, ret);
    release_loopback_session(active_loopback_patch);
    return ret;
}

void update_patch_stream_config(struct stream_config *stream_cfg ,
                                struct audio_port_config *port_cfg)
{
    stream_cfg->sample_rate = port_cfg->sample_rate;
    stream_cfg->channel_mask = port_cfg->channel_mask;
    stream_cfg->format = port_cfg->format;
    reassign_device_list(&stream_cfg->device_list, port_cfg->ext.device.type, "");
    stream_cfg->bit_width = format_to_bitwidth(port_cfg->format);
}
/* API to create audio patch */
int audio_extn_hw_loopback_create_audio_patch(struct audio_hw_device *dev,
                                     unsigned int num_sources,
                                     const struct audio_port_config *sources,
                                     unsigned int num_sinks,
                                     const struct audio_port_config *sinks,
                                     audio_patch_handle_t *handle)
{
    int status = 0;
    audio_patch_handle_t loopback_patch_id = 0x0;
    loopback_patch_t loopback_patch, *active_loopback_patch = NULL;

    ALOGV("%s : Create audio patch begin", __func__);

    if ((audio_loopback_mod == NULL) || (dev == NULL)) {
        ALOGE("%s, Loopback module not initialized orInvalid device", __func__);
        status = -EINVAL;
        return status;
    }

    pthread_mutex_lock(&audio_loopback_mod->lock);
    if (audio_loopback_mod->patch_db.num_patches >= MAX_NUM_PATCHES ) {
        ALOGE("%s, Exhausted maximum possible patches per device", __func__);
        status = -EINVAL;
        goto exit_create_patch;
    }

    /* Port configuration check & validation */
    if (num_sources > MAX_SOURCE_PORTS_PER_PATCH ||
        num_sinks > MAX_SINK_PORTS_PER_PATCH) {
        ALOGE("%s, Unsupported patch configuration, sources %d sinks %d ",
                __func__, num_sources, num_sources);
        status = -EINVAL;
        goto exit_create_patch;
    }

    /* Use an empty patch from patch database and initialze */
    active_loopback_patch = &(audio_loopback_mod->patch_db.loopback_patch[
                                audio_loopback_mod->patch_db.num_patches]);

    memset(active_loopback_patch, 0, sizeof(loopback_patch_t));

    active_loopback_patch->patch_handle_id = PATCH_HANDLE_INVALID;
    active_loopback_patch->patch_state = PATCH_INACTIVE;
    active_loopback_patch->patch_stream.ip_hdlr_handle = NULL;
    active_loopback_patch->patch_stream.adsp_hdlr_stream_handle = NULL;
    memcpy(&active_loopback_patch->loopback_source, &sources[0], sizeof(struct
    audio_port_config));
    memcpy(&active_loopback_patch->loopback_sink, &sinks[0], sizeof(struct
    audio_port_config));

    /* Get loopback patch type based on source and sink ports configuration */
    loopback_patch_id = get_loopback_patch_type(active_loopback_patch);

    if (loopback_patch_id == PATCH_HANDLE_INVALID) {
        ALOGE("%s, Unsupported patch type", __func__);
        status = -EINVAL;
        goto exit_create_patch;
    }

    update_patch_stream_config(&active_loopback_patch->patch_stream.in_config,
                                &active_loopback_patch->loopback_source);
    update_patch_stream_config(&active_loopback_patch->patch_stream.out_config,
                                &active_loopback_patch->loopback_sink);
    // Lock patch database, create patch handle and add patch handle to the list

    active_loopback_patch->patch_handle_id = loopback_patch_id;

    /* Is usecase transcode loopback? If yes, invoke loopback driver */
    if ((active_loopback_patch->loopback_source.type == AUDIO_PORT_TYPE_DEVICE)
       &&
       (active_loopback_patch->loopback_sink.type == AUDIO_PORT_TYPE_DEVICE)) {
        status = create_loopback_session(active_loopback_patch);
        if (status != 0)
            goto exit_create_patch;
    }

    // Create callback thread to listen to events from HW data path

    /* Fill unique handle ID generated based on active loopback patch */
    *handle = audio_loopback_mod->patch_db.loopback_patch[audio_loopback_mod->
                                        patch_db.num_patches].patch_handle_id;
    audio_loopback_mod->patch_db.num_patches++;

exit_create_patch :
    ALOGV("%s : Create audio patch end, status(%d)", __func__, status);
    pthread_mutex_unlock(&audio_loopback_mod->lock);
    return status;
}

/* API to release audio patch */
int audio_extn_hw_loopback_release_audio_patch(struct audio_hw_device *dev,
                                             audio_patch_handle_t handle)
{
    int status = 0, n=0, patch_index=-1;
    bool patch_found = false;
    loopback_patch_t *active_loopback_patch = NULL;
    ALOGV("%s audio_extn_hw_loopback_release_audio_patch begin %d", __func__, __LINE__);

    if ((audio_loopback_mod == NULL) || (dev == NULL)) {
        ALOGE("%s, Invalid device", __func__);
        status = -1;
        return status;
    }

    pthread_mutex_lock(&audio_loopback_mod->lock);

    for (n=0;n < MAX_NUM_PATCHES;n++) {
        if (audio_loopback_mod->patch_db.loopback_patch[n].patch_handle_id ==
           handle) {
            patch_found = true;
            patch_index = n;
            break;
        }
    }

    if (patch_found && (audio_loopback_mod->patch_db.num_patches > 0)) {
        active_loopback_patch = &(audio_loopback_mod->patch_db.loopback_patch[
                                patch_index]);
        status = release_loopback_session(active_loopback_patch);
        audio_loopback_mod->patch_db.num_patches--;
    } else {
        ALOGE("%s, Requested Patch handle does not exist", __func__);
        status = -1;
    }
    pthread_mutex_unlock(&audio_loopback_mod->lock);

    ALOGV("%s audio_extn_hw_loopback_release_audio_patch done, status(%d)", __func__,
    status);
    return status;
}

/* Find port config from patch database based on port info */
struct audio_port_config* get_port_from_patch_db(port_info_t *port,
                               patch_db_t *audio_patch_db, int *patch_num)
{
    int n=0, patch_index=-1;
    struct audio_port_config *cur_port=NULL;

    if (port->role == AUDIO_PORT_ROLE_SOURCE) {
        for (n=0;n < audio_patch_db->num_patches;n++) {
            cur_port = &(audio_patch_db->loopback_patch[n].loopback_source);
            if ((cur_port->id == port->id) && (cur_port->type == port->type) && (
               cur_port->role == port->role)) {
                patch_index = n;
                break;
            }
        }
    } else if (port->role == AUDIO_PORT_ROLE_SINK) {
        for (n=0;n < audio_patch_db->num_patches;n++) {
            cur_port = &(audio_patch_db->loopback_patch[n].loopback_sink);
            if ((cur_port->id == port->id) && (cur_port->type == port->type) && (
               cur_port->role == port->role)) {
                patch_index = n;
                break;
            }
        }
    }
    *patch_num = patch_index;
    return cur_port;
}

/* API to get port config based on port unique ID */
int audio_extn_hw_loopback_get_audio_port(struct audio_hw_device *dev,
                                    struct audio_port *port_in)
{
    int status = 0, n=0, patch_num=-1;
    port_info_t port_info;
    struct audio_port_config *port_out=NULL;
    ALOGV("%s %d", __func__, __LINE__);

    if ((audio_loopback_mod == NULL) || (dev == NULL)) {
        ALOGE("%s, Invalid device", __func__);
        status = -1;
        return status;
    }

    pthread_mutex_lock(&audio_loopback_mod->lock);

    port_info.id = port_in->id;
    port_info.role = port_in->role;              /* sink or source */
    port_info.type = port_in->type;              /* device, mix ... */
    port_out = get_port_from_patch_db(&port_info, &audio_loopback_mod->patch_db,
                                      &patch_num);
    if (port_out == NULL) {
        ALOGE("%s, Unable to find a valid matching port in patch \
        database,exiting", __func__);
        status = -EINVAL;
        return status;
    }

    /* Fill port output properties before returning the port */
    memcpy(&port_in->active_config,port_out, sizeof(struct audio_port_config));

    /* Multiple fields are not valid for loopback extension usecases, TODO :
    enhance for all patch handler cases. */
    port_in->num_sample_rates = 1;
    port_in->sample_rates[0] = port_out->sample_rate;
    port_in->num_channel_masks = 1;
    port_in->channel_masks[0] = port_out->channel_mask;
    port_in->num_formats = 1;
    port_in->formats[0] = port_out->format;
    port_in->num_gains = 1;

    pthread_mutex_unlock(&audio_loopback_mod->lock);
    return status;
}

/* API to set port config based on port unique ID */
int audio_extn_hw_loopback_set_audio_port_config(struct audio_hw_device *dev,
                                        const struct audio_port_config *config)
{
    int status = 0, n=0, patch_num=-1;
    port_info_t port_info;
    struct audio_port_config *port_out=NULL;
    struct audio_device *adev = audio_loopback_mod->adev;
    int loopback_gain = HW_LOOPBACK_RX_UNITY_GAIN;

    ALOGV("%s %d", __func__, __LINE__);

    if ((audio_loopback_mod == NULL) || (dev == NULL)) {
        ALOGE("%s, Invalid device", __func__);
        status = -EINVAL;
        return status;
    }

    pthread_mutex_lock(&audio_loopback_mod->lock);

    port_info.id = config->id;
    port_info.role = config->role;              /* sink or source */
    port_info.type = config->type;              /* device, mix  */
    port_out = get_port_from_patch_db(&port_info, &audio_loopback_mod->patch_db
                                    , &patch_num);

    if (port_out == NULL) {
        ALOGE("%s, Unable to find a valid matching port in patch \
        database,exiting", __func__);
        status = -EINVAL;
        goto exit_set_port_config;
    }

    port_out->config_mask |= config->config_mask;
    if(config->config_mask & AUDIO_PORT_CONFIG_CHANNEL_MASK)
        port_out->channel_mask = config->channel_mask;
    if(config->config_mask & AUDIO_PORT_CONFIG_FORMAT)
        port_out->format = config->format;
    if(config->config_mask & AUDIO_PORT_CONFIG_GAIN)
        port_out->gain = config->gain;
    if(config->config_mask & AUDIO_PORT_CONFIG_SAMPLE_RATE)
        port_out->sample_rate = config->sample_rate;

    /* Convert gain in millibels to ratio and convert to Q13 */
    loopback_gain = pow(10, (float)((float)port_out->gain.values[0]/2000)) *
                       (1 << 13);
    ALOGV("%s, Port config gain_in_mbells: %d, gain_in_q13 : %d", __func__,
          port_out->gain.values[0], loopback_gain);
    if((port_out->config_mask & AUDIO_PORT_CONFIG_GAIN) &&
        port_out->gain.mode == AUDIO_GAIN_MODE_JOINT ) {
        status = hw_loopback_set_volume(adev, loopback_gain);
        if (status) {
            ALOGE("%s, Error setting loopback gain config: status %d",
                  __func__, status);
        }
    } else {
        ALOGE("%s, Unsupported port config ,exiting", __func__);
        status = -EINVAL;
    }

    /* Currently, port config is not used for anything,
    need to restart session    */
exit_set_port_config:
    pthread_mutex_unlock(&audio_loopback_mod->lock);
    return status;
}

/* Loopback extension initialization, part of hal init sequence */
int audio_extn_hw_loopback_init(struct audio_device *adev)
{
    ALOGV("%s Audio loopback extension initializing", __func__);
    int ret = 0, size = 0;

    if (audio_loopback_mod != NULL) {
        pthread_mutex_lock(&audio_loopback_mod->lock);
        if (audio_loopback_mod->adev == adev) {
            ALOGV("%s %d : Audio loopback module already exists", __func__,
                    __LINE__);
        } else {
            ALOGV("%s %d : Audio loopback module called for invalid device",
                    __func__, __LINE__);
            ret = -EINVAL;
        }
        goto loopback_done;
    }
    audio_loopback_mod = malloc(sizeof(struct audio_loopback));
    if (audio_loopback_mod == NULL) {
        ALOGE("%s, out of memory", __func__);
        ret = -ENOMEM;
        goto loopback_done;
    }

    pthread_mutex_init(&audio_loopback_mod->lock,
                        (const pthread_mutexattr_t *)NULL);
    pthread_mutex_lock(&audio_loopback_mod->lock);
    audio_loopback_mod->adev = adev;

    ret = init_patch_database(&audio_loopback_mod->patch_db);

    audio_loopback_mod->uc_id_rx = USECASE_AUDIO_TRANSCODE_LOOPBACK_RX;
    audio_loopback_mod->uc_id_tx = USECASE_AUDIO_TRANSCODE_LOOPBACK_TX;
    audio_loopback_mod->uc_type_rx = TRANSCODE_LOOPBACK_RX;
    audio_loopback_mod->uc_type_tx = TRANSCODE_LOOPBACK_TX;

loopback_done:
    if (ret != 0) {
        if (audio_loopback_mod != NULL) {
            pthread_mutex_unlock(&audio_loopback_mod->lock);
            pthread_mutex_destroy(&audio_loopback_mod->lock);
            free(audio_loopback_mod);
            audio_loopback_mod = NULL;
        }
    } else {
        pthread_mutex_unlock(&audio_loopback_mod->lock);
    }
    ALOGV("%s Audio loopback extension initialized", __func__);
    return ret;
}

void audio_extn_hw_loopback_deinit(struct audio_device *adev)
{
    ALOGV("%s Audio loopback extension de-initializing", __func__);

    if (audio_loopback_mod == NULL) {
        ALOGE("%s, loopback module NULL, cannot deinitialize", __func__);
        return;
    }
    pthread_mutex_lock(&audio_loopback_mod->lock);

    if (audio_loopback_mod->adev == adev) {
        if (audio_loopback_mod != NULL) {
            pthread_mutex_unlock(&audio_loopback_mod->lock);
            pthread_mutex_destroy(&audio_loopback_mod->lock);
            free(audio_loopback_mod);
            audio_loopback_mod = NULL;
        }
        return;
    } else {
        ALOGE("%s, loopback module not valid, cannot deinitialize", __func__);
    }
    pthread_mutex_unlock(&audio_loopback_mod->lock);
    return;
}
