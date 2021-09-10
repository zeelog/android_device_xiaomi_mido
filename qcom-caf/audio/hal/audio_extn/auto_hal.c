/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
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
#define LOG_TAG "auto_hal_extn"
/*#define LOG_NDEBUG 0*/

#include <errno.h>
#include <pthread.h>
#include <log/log.h>
#include <math.h>
#include <audio_hw.h>
#include "audio_extn.h"
#include "platform_api.h"
#include "platform.h"
#include "audio_hal_plugin.h"
#include "auto_hal.h"

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_AUTO_HAL
#include <log_utils.h>
#endif

//external feature dependency
static fp_in_get_stream_t                           fp_in_get_stream;
static fp_out_get_stream_t                          fp_out_get_stream;
static fp_audio_extn_ext_hw_plugin_usecase_start_t  fp_audio_extn_ext_hw_plugin_usecase_start;
static fp_audio_extn_ext_hw_plugin_usecase_stop_t   fp_audio_extn_ext_hw_plugin_usecase_stop;
static fp_get_usecase_from_list_t                   fp_get_usecase_from_list;
static fp_get_output_period_size_t                  fp_get_output_period_size;
static fp_audio_extn_ext_hw_plugin_set_audio_gain_t fp_audio_extn_ext_hw_plugin_set_audio_gain;
static fp_select_devices_t                          fp_select_devices;
static fp_disable_audio_route_t                     fp_disable_audio_route;
static fp_disable_snd_device_t                      fp_disable_snd_device;
static fp_adev_get_active_input_t                   fp_adev_get_active_input;
static fp_platform_set_echo_reference_t             fp_platform_set_echo_reference;
static fp_platform_get_eccarstate_t                 fp_platform_get_eccarstate;
static fp_generate_patch_handle_t                   fp_generate_patch_handle;

/* Auto hal module struct */
static struct auto_hal_module *auto_hal = NULL;

int auto_hal_release_audio_patch(struct audio_hw_device *dev,
                                audio_patch_handle_t handle);
int auto_hal_stop_hfp_downlink(struct audio_device *adev,
                               struct audio_usecase *uc_info);

static struct audio_patch_record *get_patch_from_list(struct audio_device *adev,
                                                    audio_patch_handle_t patch_id)
{
    struct audio_patch_record *patch;
    struct listnode *node;
    list_for_each(node, &adev->audio_patch_record_list) {
        patch = node_to_item(node, struct audio_patch_record, list);
        if (patch->handle == patch_id)
            return patch;
    }
    return NULL;
}

int auto_hal_create_audio_patch(struct audio_hw_device *dev,
                                unsigned int num_sources,
                                const struct audio_port_config *sources,
                                unsigned int num_sinks,
                                const struct audio_port_config *sinks,
                                audio_patch_handle_t *handle)
{
    struct audio_device *adev = (struct audio_device *)dev;
    int ret = 0;
    struct audio_usecase *uc_info = NULL;
    struct audio_patch_record *patch_record = NULL;
    audio_usecase_t usecase = USECASE_INVALID;

    ALOGV("%s: enter", __func__);

    if (!dev || !sources || !sinks || !handle ) {
        ALOGE("%s: null audio patch parameters", __func__);
        return -EINVAL;
    }

    /* Port configuration check & validation */
    if (num_sources > MAX_SOURCE_PORTS_PER_PATCH ||
         num_sinks > MAX_SINK_PORTS_PER_PATCH) {
         ALOGE("%s: invalid audio patch parameters, sources %d sinks %d ",
                 __func__, num_sources, num_sources);
         return -EINVAL;
    }

    /* No validation on num of sources and sinks to allow patch with
     * multiple sinks being created, but only the first source and
     * sink are used to create patch.
     *
     * Stream set_parameters for AUDIO_PARAMETER_STREAM_ROUTING and
     * AUDIO_PARAMETER_STREAM_INPUT_SOURCE is replaced with audio_patch
     * callback in audioflinger for AUDIO_DEVICE_API_VERSION_3_0 and above.
     * Need to handle device routing notification in audio HAL for
     *   Capture:  DEVICE -> MIX
     *   Playback: MIX -> DEVICE
     * For DEVICE -> DEVICE patch type, it refers to routing from/to external
     * codec/amplifier and allow Android streams to be mixed at the H/W level.
     *
     * Auto extension here is to act on Device to Device patch only as playback
     * and capture patches as well as the book-keeeping information are already
     * being handled at audio_hw entry.
     */
    if ((sources->type == AUDIO_PORT_TYPE_DEVICE) &&
            (sinks->type == AUDIO_PORT_TYPE_DEVICE)) {
        /* allocate use case and call to plugin driver*/
        uc_info = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));
        if (!uc_info) {
            ALOGE("%s fail to allocate uc_info", __func__);
            return -ENOMEM;
        }
        /* TODO - add sink type check and printout for non speaker sink */
        switch(sources->ext.device.type) {
#ifdef FM_TUNER_EXT_ENABLED
            case AUDIO_DEVICE_IN_FM_TUNER:
                ALOGV("Creating audio patch for external FM tuner");
                uc_info->id = USECASE_AUDIO_FM_TUNER_EXT;
                uc_info->type = PCM_PASSTHROUGH;
                list_init(&uc_info->device_list);
                reassign_device_list(&uc_info->device_list, AUDIO_DEVICE_IN_FM_TUNER,
                                     sources->ext.device.address);
                uc_info->in_snd_device = SND_DEVICE_IN_CAPTURE_FM;
                uc_info->out_snd_device = SND_DEVICE_OUT_BUS_MEDIA;
                break;
#endif
            default:
                ALOGE("%s: Unsupported audio source type %x", __func__,
                            sources->ext.device.type);
                goto error;
        }

        ALOGD("%s: Starting ext hw plugin use case (%d) in_snd_device (%d) out_snd_device (%d)",
              __func__, uc_info->id, uc_info->in_snd_device, uc_info->out_snd_device);

        ret = fp_audio_extn_ext_hw_plugin_usecase_start(adev->ext_hw_plugin, uc_info);
        if (ret) {
            ALOGE("%s: failed to start ext hw plugin use case (%d)",
                __func__, uc_info->id);
            goto error;
        }
        /* TODO: apply audio port gain to codec if applicable */
        usecase = uc_info->id;
        pthread_mutex_lock(&adev->lock);
        list_add_tail(&adev->usecase_list, &uc_info->list);
        pthread_mutex_unlock(&adev->lock);
    } else {
        ALOGV("%s: audio patch not supported", __func__);
        goto exit;
    }

    /* patch created success, add to patch record list */
    patch_record = (struct audio_patch_record *)calloc(1,
                    sizeof(struct audio_patch_record));
    if (!patch_record) {
        ALOGE("%s fail to allocate patch_record", __func__);
        ret = -ENOMEM;
        if (uc_info)
            list_remove(&uc_info->list);
        goto error;
    }

    pthread_mutex_lock(&adev->lock);
    if (*handle == AUDIO_PATCH_HANDLE_NONE) {
        ALOGD("%s: audio patch handle not allocated 0x%x", __func__, *handle);
        *handle = fp_generate_patch_handle();
    }
    patch_record->handle = *handle;
    patch_record->usecase = usecase;
    patch_record->patch.id = *handle;
    patch_record->patch.num_sources = num_sources;
    patch_record->patch.num_sinks = num_sinks;
    for (int i = 0; i < num_sources; i++)
        patch_record->patch.sources[i] = sources[i];
    for (int i = 0; i < num_sinks; i++)
        patch_record->patch.sinks[i] = sinks[i];

    list_add_tail(&adev->audio_patch_record_list, &patch_record->list);
    pthread_mutex_unlock(&adev->lock);

    goto exit;

error:
    if(uc_info)
        free(uc_info);
exit:
    ALOGV("%s: exit: handle 0x%x", __func__, *handle);
    return ret;
}

int auto_hal_release_audio_patch(struct audio_hw_device *dev,
                                audio_patch_handle_t handle)
{
    int ret = 0;
    struct audio_device *adev = (struct audio_device *)dev;
    struct audio_usecase *uc_info = NULL;
    struct audio_patch_record *patch_record = NULL;

    ALOGV("%s: enter: handle 0x%x", __func__, handle);

    if (!dev) {
        ALOGE("%s: null audio patch parameters", __func__);
        return -EINVAL;
    }

    if (handle == AUDIO_PATCH_HANDLE_NONE) {
        ALOGW("%s: null audio patch handle", __func__);
        return -EINVAL;
    }

    /* get the patch record from handle */
    pthread_mutex_lock(&adev->lock);
    patch_record = get_patch_from_list(adev, handle);
    if(!patch_record) {
        ALOGE("%s: failed to find the patch record with handle (%d) in the list",
                __func__, handle);
        ret = -EINVAL;
    }
    pthread_mutex_unlock(&adev->lock);
    if(ret)
        goto exit;

    if (patch_record->usecase != USECASE_INVALID) {
        pthread_mutex_lock(&adev->lock);
        uc_info = fp_get_usecase_from_list(adev, patch_record->usecase);
        if (!uc_info) {
            ALOGE("%s: failed to find the usecase (%d)",
                    __func__, patch_record->usecase);
            ret = -EINVAL;
        } else {
            /* call to plugin to stop the usecase */
            ret = fp_audio_extn_ext_hw_plugin_usecase_stop(adev->ext_hw_plugin, uc_info);
            if (ret) {
                ALOGE("%s: failed to stop ext hw plugin use case (%d)",
                        __func__, uc_info->id);
            }

            /* remove usecase from list and free it */
            list_remove(&uc_info->list);
            free(uc_info);
        }
        pthread_mutex_unlock(&adev->lock);
    }

    /* remove the patch record from list and free it */
    pthread_mutex_lock(&adev->lock);
    list_remove(&patch_record->list);
    pthread_mutex_unlock(&adev->lock);
    free(patch_record);

exit:
    ALOGV("%s: exit", __func__);
    return ret;
}

int auto_hal_get_car_audio_stream_from_address(const char *address)
{
    int bus_num = -1;
    char *str = NULL;
    char *last_r = NULL;
    char local_address[AUDIO_DEVICE_MAX_ADDRESS_LEN];

    /* bus device with null address error out */
    if (address == NULL) {
        ALOGE("%s: null address for car stream", __func__);
        return -1;
    }

    /* strtok will modify the original string. make a copy first */
    strlcpy(local_address, address, AUDIO_DEVICE_MAX_ADDRESS_LEN);

    /* extract bus number from address */
    str = strtok_r(local_address, "BUS_",&last_r);
    if (str != NULL)
        bus_num = (int)strtol(str, (char **)NULL, 10);

    /* validate bus number */
    if ((bus_num < 0) || (bus_num >= MAX_CAR_AUDIO_STREAMS)) {
        ALOGE("%s: invalid bus number %d", __func__, bus_num);
        return -1;
    }

    return (0x1 << bus_num);
}

int auto_hal_open_output_stream(struct stream_out *out)
{
    int ret = 0;
    unsigned int channels = audio_channel_count_from_out_mask(out->channel_mask);

    switch(out->car_audio_stream) {
    case CAR_AUDIO_STREAM_MEDIA:
        /* media bus stream shares pcm device with deep-buffer */
        out->usecase = USECASE_AUDIO_PLAYBACK_MEDIA;
        out->config = pcm_config_media;
        out->config.period_size = fp_get_output_period_size(out->sample_rate, out->format,
                                        channels, DEEP_BUFFER_OUTPUT_PERIOD_DURATION);
        if (out->config.period_size <= 0) {
            ALOGE("Invalid configuration period size is not valid");
            ret = -EINVAL;
            goto error;
        }
        if (out->flags == AUDIO_OUTPUT_FLAG_NONE ||
            out->flags == AUDIO_OUTPUT_FLAG_PRIMARY)
            out->flags |= AUDIO_OUTPUT_FLAG_MEDIA;
        out->volume_l = out->volume_r = MAX_VOLUME_GAIN;
        break;
    case CAR_AUDIO_STREAM_SYS_NOTIFICATION:
        /* sys notification bus stream shares pcm device with low-latency */
        out->usecase = USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION;
        out->config = pcm_config_system;
        if (out->flags == AUDIO_OUTPUT_FLAG_NONE)
            out->flags |= AUDIO_OUTPUT_FLAG_SYS_NOTIFICATION;
        out->volume_l = out->volume_r = MAX_VOLUME_GAIN;
        break;
    case CAR_AUDIO_STREAM_NAV_GUIDANCE:
        out->usecase = USECASE_AUDIO_PLAYBACK_NAV_GUIDANCE;
        out->config = pcm_config_media;
        out->config.period_size = fp_get_output_period_size(out->sample_rate, out->format,
                                        channels, DEEP_BUFFER_OUTPUT_PERIOD_DURATION);
        if (out->config.period_size <= 0) {
            ALOGE("Invalid configuration period size is not valid");
            ret = -EINVAL;
            goto error;
        }
        if (out->flags == AUDIO_OUTPUT_FLAG_NONE)
            out->flags |= AUDIO_OUTPUT_FLAG_NAV_GUIDANCE;
        out->volume_l = out->volume_r = MAX_VOLUME_GAIN;
        break;
    case CAR_AUDIO_STREAM_PHONE:
        out->usecase = USECASE_AUDIO_PLAYBACK_PHONE;
        out->config = pcm_config_system;
        if (out->flags == AUDIO_OUTPUT_FLAG_NONE)
            out->flags |= AUDIO_OUTPUT_FLAG_PHONE;
        out->volume_l = out->volume_r = MAX_VOLUME_GAIN;
        break;
    case CAR_AUDIO_STREAM_FRONT_PASSENGER:
        out->usecase = USECASE_AUDIO_PLAYBACK_FRONT_PASSENGER;
        out->config = pcm_config_system;
        if (out->flags == AUDIO_OUTPUT_FLAG_NONE)
            out->flags |= AUDIO_OUTPUT_FLAG_FRONT_PASSENGER;
        break;
    case CAR_AUDIO_STREAM_REAR_SEAT:
        out->usecase = USECASE_AUDIO_PLAYBACK_REAR_SEAT;
        out->config = pcm_config_media;
        out->config.period_size = fp_get_output_period_size(out->sample_rate, out->format,
                                        channels, DEEP_BUFFER_OUTPUT_PERIOD_DURATION);
        if (out->config.period_size <= 0) {
            ALOGE("Invalid configuration period size is not valid");
            ret = -EINVAL;
            goto error;
        }
        if (out->flags == AUDIO_OUTPUT_FLAG_NONE)
            out->flags |= AUDIO_OUTPUT_FLAG_REAR_SEAT;
        out->volume_l = out->volume_r = MAX_VOLUME_GAIN;
        break;
    default:
        ALOGE("%s: Car audio stream %x not supported", __func__,
            out->car_audio_stream);
        ret = -EINVAL;
        goto error;
    }

error:
    return ret;
}

bool auto_hal_is_bus_device_usecase(audio_usecase_t uc_id)
{
    unsigned int i;
    for (i = 0; i < sizeof(bus_device_usecases)/sizeof(bus_device_usecases[0]); i++) {
        if (uc_id == bus_device_usecases[i])
            return true;
    }
    return false;
}

snd_device_t auto_hal_get_snd_device_for_car_audio_stream(struct stream_out *out)
{
    snd_device_t snd_device = SND_DEVICE_NONE;

    switch(out->car_audio_stream) {
    case CAR_AUDIO_STREAM_MEDIA:
        snd_device = SND_DEVICE_OUT_BUS_MEDIA;
        break;
    case CAR_AUDIO_STREAM_SYS_NOTIFICATION:
        snd_device = SND_DEVICE_OUT_BUS_SYS;
        break;
    case CAR_AUDIO_STREAM_NAV_GUIDANCE:
        snd_device = SND_DEVICE_OUT_BUS_NAV;
        break;
    case CAR_AUDIO_STREAM_PHONE:
        snd_device = SND_DEVICE_OUT_BUS_PHN;
        break;
    case CAR_AUDIO_STREAM_FRONT_PASSENGER:
        snd_device = SND_DEVICE_OUT_BUS_PAX;
        break;
    case CAR_AUDIO_STREAM_REAR_SEAT:
        snd_device = SND_DEVICE_OUT_BUS_RSE;
        break;
    default:
        ALOGE("%s: Unknown car audio stream (%x)",
            __func__, out->car_audio_stream);
    }
    return snd_device;
}

int auto_hal_get_audio_port(struct audio_hw_device *dev __unused,
                        struct audio_port *config __unused)
{
    return -ENOSYS;
}

int auto_hal_set_audio_port_config(struct audio_hw_device *dev,
                        const struct audio_port_config *config)
{
    struct audio_device *adev = (struct audio_device *)dev;
    int ret = 0;
    struct listnode *node = NULL;
    float volume = 0.0;

    ALOGV("%s: enter", __func__);

    if (!config) {
        ALOGE("%s: invalid input parameters", __func__);
        return -EINVAL;
    }

    /* For Android automotive, audio port config from car framework
     * allows volume gain to be set to device at audio HAL level, where
     * the gain can be applied in DSP mixer or CODEC amplifier.
     *
     * Following routing should be considered:
     *     MIX -> DEVICE
     *     DEVICE -> MIX
     *     DEVICE -> DEVICE
     *
     * For BUS devices routed to/from mixer, gain will be applied to DSP
     * mixer via kernel control which audio HAL stream is associated with.
     *
     * For external (source) device (FM TUNER/AUX), routing is typically
     * done with AudioPatch to (sink) device (SPKR), thus gain should be
     * applied to CODEC amplifier via codec plugin extention as audio HAL
     * stream may not be available for external audio routing.
     */
    if (config->type == AUDIO_PORT_TYPE_DEVICE) {
        ALOGI("%s: device port: type %x, address %s, gain %d mB", __func__,
            config->ext.device.type,
            config->ext.device.address,
            config->gain.values[0]);
        if (config->role == AUDIO_PORT_ROLE_SINK) {
            /* handle output devices */
            pthread_mutex_lock(&adev->lock);
            list_for_each(node, &adev->active_outputs_list) {
                streams_output_ctxt_t *out_ctxt = node_to_item(node,
                                                    streams_output_ctxt_t,
                                                    list);
                /* limit audio gain support for bus device only */
                if (config->ext.device.type == AUDIO_DEVICE_OUT_BUS &&
                    compare_device_type_and_address(&out_ctxt->output->device_list,
                                                    config->ext.device.type,
                                                    config->ext.device.address)) {
                    /* millibel = 1/100 dB = 1/1000 bel
                     * q13 = (10^(mdb/100/20))*(2^13)
                     */
                    if(config->gain.values[0] <= (MIN_VOLUME_VALUE_MB + STEP_VALUE_MB))
                        volume = MIN_VOLUME_GAIN;
                    else
                        volume = powf(10.0f, ((float)config->gain.values[0] / 2000));
                    ALOGV("%s: set volume to stream: %p", __func__,
                        &out_ctxt->output->stream);
                    /* set gain if output stream is active */
                    out_ctxt->output->stream.set_volume(
                                                &out_ctxt->output->stream,
                                                volume, volume);
                }
            }
            /* NOTE: Ideally audio patch list is a superset of output stream list above.
             *       However, audio HAL does not maintain patches for mix -> device or
             *       device -> mix currently. Thus doing separate lookups for device ->
             *       device in audio patch list.
             * FIXME: Cannot cache the gain if audio patch is not created. Expected gain
             *        to be part of port config upon audio patch creation. If not, need
             *        to create a list of audio port configs in adev context.
             */
            list_for_each(node, &adev->audio_patch_record_list) {
                struct audio_patch_record *patch_record = node_to_item(node,
                                                    struct audio_patch_record,
                                                    list);
                /* limit audio gain support for device -> bus device patch */
                if (patch_record->patch.sources[0].type == AUDIO_PORT_TYPE_DEVICE &&
                    patch_record->patch.sinks[0].type == AUDIO_PORT_TYPE_DEVICE &&
                    patch_record->patch.sinks[0].role == AUDIO_PORT_ROLE_SINK &&
                    patch_record->patch.sinks[0].ext.device.type == AUDIO_DEVICE_OUT_BUS &&
                    patch_record->patch.sinks[0].ext.device.type == config->ext.device.type &&
                    strcmp(patch_record->patch.sinks[0].ext.device.address,
                        config->ext.device.address) == 0) {
                    /* cache audio port configuration for sink */
                    memcpy((void *)&patch_record->patch.sinks[0], (void *)config,
                        sizeof(struct audio_port_config));

                    struct audio_usecase *uc_info = fp_get_usecase_from_list(adev,
                                                        patch_record->usecase);
                    if (!uc_info) {
                        ALOGE("%s: failed to find the usecase %d",
                            __func__, patch_record->usecase);
                        ret = -EINVAL;
                    } else {
                        volume = config->gain.values[0];
                        /* linear interpolation from millibel to level */
                        int vol_level = lrint(((volume + (0 - MIN_VOLUME_VALUE_MB)) /
                                               (MAX_VOLUME_VALUE_MB - MIN_VOLUME_VALUE_MB)) * 40);
                        ALOGV("%s: set volume to patch %x", __func__,
                            patch_record->handle);
                        ret = fp_audio_extn_ext_hw_plugin_set_audio_gain(adev,
                                uc_info, vol_level);
                    }
                }
            }
            pthread_mutex_unlock(&adev->lock);
        } else if (config->role == AUDIO_PORT_ROLE_SOURCE) {
            // FIXME: handle input devices.
        }
    }

    /* Only handle device port currently. */

    ALOGV("%s: exit", __func__);
    return ret;
}

void auto_hal_set_parameters(struct audio_device *adev __unused,
                                        struct str_parms *parms)
{
    int ret = 0;
    char value[32]={0};

    ALOGV("%s: enter", __func__);

    ret = str_parms_get_str(parms, "SND_CARD_STATUS", value, sizeof(value));
    if (ret >= 0) {
        char *snd_card_status = value+2;
        ALOGV("%s: snd card status %s", __func__, snd_card_status);
        if (strstr(snd_card_status, "OFFLINE")) {
            auto_hal->card_status = CARD_STATUS_OFFLINE;
        }
        else if (strstr(snd_card_status, "ONLINE")) {
            auto_hal->card_status = CARD_STATUS_ONLINE;
        }
    }

    ALOGV("%s: exit", __func__);
}

int auto_hal_start_hfp_downlink(struct audio_device *adev,
                                struct audio_usecase *uc_info)
{
    int32_t ret = 0;
    struct audio_usecase *uc_downlink_info;

    ALOGD("%s: enter", __func__);

    uc_downlink_info = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));

    if (!uc_downlink_info)
        return -ENOMEM;

    uc_downlink_info->type = PCM_HFP_CALL;
    uc_downlink_info->stream.out = adev->primary_output;
    list_init(&uc_downlink_info->device_list);
    assign_devices(&uc_downlink_info->device_list, &adev->primary_output->device_list);
    uc_downlink_info->in_snd_device = SND_DEVICE_NONE;
    uc_downlink_info->out_snd_device = SND_DEVICE_NONE;

    switch (uc_info->id) {
    case USECASE_AUDIO_HFP_SCO:
        uc_downlink_info->id = USECASE_AUDIO_HFP_SCO_DOWNLINK;
        break;
    case USECASE_AUDIO_HFP_SCO_WB:
        uc_downlink_info->id = USECASE_AUDIO_HFP_SCO_WB_DOWNLINK;
        break;
    default:
        ALOGE("%s: Invalid usecase %d", __func__, uc_info->id);
        free(uc_downlink_info);
        return -EINVAL;
    }

    list_add_tail(&adev->usecase_list, &uc_downlink_info->list);

    ret = fp_select_devices(adev, uc_downlink_info->id);
    if (ret) {
        ALOGE("%s: Select devices failed %d", __func__, ret);
        goto exit;
    }

    ALOGD("%s: exit: status(%d)", __func__, ret);
    return 0;

exit:
    auto_hal_stop_hfp_downlink(adev, uc_info);
    ALOGE("%s: Problem in start hfp downlink: status(%d)", __func__, ret);
    return ret;
}

int auto_hal_stop_hfp_downlink(struct audio_device *adev,
                               struct audio_usecase *uc_info)
{
    int32_t ret = 0;
    struct audio_usecase *uc_downlink_info;
    audio_usecase_t ucid;

    ALOGD("%s: enter", __func__);

    switch (uc_info->id) {
    case USECASE_AUDIO_HFP_SCO:
        ucid = USECASE_AUDIO_HFP_SCO_DOWNLINK;
        break;
    case USECASE_AUDIO_HFP_SCO_WB:
        ucid = USECASE_AUDIO_HFP_SCO_WB_DOWNLINK;
        break;
    default:
        ALOGE("%s: Invalid usecase %d", __func__, uc_info->id);
        return -EINVAL;
    }

    uc_downlink_info = fp_get_usecase_from_list(adev, ucid);
    if (uc_downlink_info == NULL) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
              __func__, ucid);
        return -EINVAL;
    }

    /* Get and set stream specific mixer controls */
    fp_disable_audio_route(adev, uc_downlink_info);

    /* Disable the rx and tx devices */
    fp_disable_snd_device(adev, uc_downlink_info->out_snd_device);
    fp_disable_snd_device(adev, uc_downlink_info->in_snd_device);

    list_remove(&uc_downlink_info->list);
    free(uc_downlink_info);

    ALOGD("%s: exit: status(%d)", __func__, ret);
    return ret;
}

snd_device_t auto_hal_get_input_snd_device(struct audio_device *adev,
                                audio_usecase_t uc_id)
{
    snd_device_t snd_device = SND_DEVICE_NONE;
    struct listnode out_devices;
    struct audio_usecase *usecase = NULL;
    struct stream_in *in = fp_adev_get_active_input(adev);
    struct listnode in_devices;
    struct listnode *node;
    struct audio_device_info *item = NULL;

    list_init(&in_devices);
    if (in != NULL)
        assign_devices(&in_devices, &in->device_list);

    if (uc_id == USECASE_INVALID) {
        ALOGE("%s: Invalid usecase (%d)", __func__, uc_id);
        return -EINVAL;
    }

    usecase = fp_get_usecase_from_list(adev, uc_id);
    if (usecase == NULL) {
        ALOGE("%s: Could not find the usecase (%d)", __func__, uc_id);
        return -EINVAL;
    }

    if (usecase->stream.out == NULL) {
        ALOGE("%s: stream.out is NULL", __func__);
        return -EINVAL;
    }

    list_init(&out_devices);
    assign_devices(&out_devices, &usecase->stream.out->device_list);
    if (list_empty(&out_devices) ||
        compare_device_type(&out_devices, AUDIO_DEVICE_BIT_IN)) {
        ALOGE("%s: Invalid output devices (%#x)", __func__, get_device_types(&out_devices));
        return -EINVAL;
    }

    ALOGV("%s: output device(%#x), input device(%#x), usecase(%d)",
        __func__, get_device_types(&out_devices), get_device_types(&in_devices), uc_id);

    if (compare_device_type(&out_devices, AUDIO_DEVICE_OUT_BUS)) {
        /* usecase->id is token as judgement for HFP calls */
        switch (usecase->id) {
        case USECASE_AUDIO_HFP_SCO:
        case USECASE_AUDIO_HFP_SCO_WB:
            if (fp_platform_get_eccarstate((void *) adev->platform)) {
                snd_device = SND_DEVICE_IN_VOICE_SPEAKER_MIC_HFP_MMSECNS;
            } else {
                snd_device = SND_DEVICE_IN_VOICE_SPEAKER_MIC_HFP;
            }
            if (adev->enable_hfp)
                fp_platform_set_echo_reference(adev, false, &out_devices);
            break;
        case USECASE_AUDIO_HFP_SCO_DOWNLINK:
            snd_device = SND_DEVICE_IN_BT_SCO_MIC;
            break;
        case USECASE_AUDIO_HFP_SCO_WB_DOWNLINK:
            snd_device = SND_DEVICE_IN_BT_SCO_MIC_WB;
            break;
        case USECASE_VOICE_CALL:
            snd_device = SND_DEVICE_IN_VOICE_SPEAKER_MIC;
            break;
        default:
            ALOGE("%s: Usecase (%d) not supported", __func__, uc_id);
            return -EINVAL;
        }
    } else {
        ALOGE("%s: Output devices (%#x) not supported", __func__, get_device_types(&out_devices));
        return -EINVAL;
    }

    return snd_device;
}

snd_device_t auto_hal_get_output_snd_device(struct audio_device *adev,
                                audio_usecase_t uc_id)
{
    snd_device_t snd_device = SND_DEVICE_NONE;
    struct listnode devices;
    struct audio_usecase *usecase = NULL;

    if (uc_id == USECASE_INVALID) {
        ALOGE("%s: Invalid usecase (%d)", __func__, uc_id);
        return -EINVAL;
    }

    usecase = fp_get_usecase_from_list(adev, uc_id);
    if (usecase == NULL) {
        ALOGE("%s: Could not find the usecase (%d)", __func__, uc_id);
        return -EINVAL;
    }

    if (usecase->stream.out == NULL) {
        ALOGE("%s: stream.out is NULL", __func__);
        return -EINVAL;
    }

    list_init(&devices);
    assign_devices(&devices, &usecase->stream.out->device_list);
    if (list_empty(&devices) ||
        compare_device_type(&devices, AUDIO_DEVICE_BIT_IN)) {
        ALOGE("%s: Invalid output devices (%#x)", __func__, get_device_types(&devices));
        return -EINVAL;
    }

    ALOGV("%s: output devices(%#x), usecase(%d)", __func__,
              get_device_types(&devices), uc_id);

    if (compare_device_type(&devices, AUDIO_DEVICE_OUT_BUS)) {
        /* usecase->id is token as judgement for HFP calls */
        switch (usecase->id) {
        case USECASE_AUDIO_HFP_SCO:
            snd_device = SND_DEVICE_OUT_BT_SCO;
            break;
        case USECASE_AUDIO_HFP_SCO_WB:
            snd_device = SND_DEVICE_OUT_BT_SCO_WB;
            break;
        case USECASE_AUDIO_HFP_SCO_DOWNLINK:
        case USECASE_AUDIO_HFP_SCO_WB_DOWNLINK:
            snd_device = SND_DEVICE_OUT_VOICE_SPEAKER_HFP;
            break;
        case USECASE_VOICE_CALL:
            snd_device = SND_DEVICE_OUT_VOICE_SPEAKER;
            break;
        case USECASE_AUDIO_PLAYBACK_MEDIA:
        case USECASE_AUDIO_PLAYBACK_OFFLOAD:
        case USECASE_AUDIO_PLAYBACK_OFFLOAD2:
        case USECASE_AUDIO_PLAYBACK_OFFLOAD3:
        case USECASE_AUDIO_PLAYBACK_OFFLOAD4:
        case USECASE_AUDIO_PLAYBACK_OFFLOAD5:
        case USECASE_AUDIO_PLAYBACK_OFFLOAD6:
        case USECASE_AUDIO_PLAYBACK_OFFLOAD7:
        case USECASE_AUDIO_PLAYBACK_OFFLOAD8:
        case USECASE_AUDIO_PLAYBACK_OFFLOAD9:
        case USECASE_AUDIO_PLAYBACK_ULL:
        case USECASE_AUDIO_PLAYBACK_MMAP:
        case USECASE_AUDIO_PLAYBACK_VOIP:
            snd_device = SND_DEVICE_OUT_BUS_MEDIA;
            break;
        case USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION:
            snd_device = SND_DEVICE_OUT_BUS_SYS;
            break;
        case USECASE_AUDIO_PLAYBACK_NAV_GUIDANCE:
            snd_device = SND_DEVICE_OUT_BUS_NAV;
            break;
        case USECASE_AUDIO_PLAYBACK_PHONE:
            snd_device = SND_DEVICE_OUT_BUS_PHN;
            break;
        case USECASE_AUDIO_PLAYBACK_FRONT_PASSENGER:
            snd_device = SND_DEVICE_OUT_BUS_PAX;
            break;
        case USECASE_AUDIO_PLAYBACK_REAR_SEAT:
            snd_device = SND_DEVICE_OUT_BUS_RSE;
            break;
        default:
            ALOGE("%s: Usecase (%d) not supported", __func__, uc_id);
            return -EINVAL;
        }
    } else {
        ALOGE("%s: Output devices (%#x) not supported", __func__, get_device_types(&devices));
        return -EINVAL;
    }

    return snd_device;
}

int auto_hal_init(struct audio_device *adev, auto_hal_init_config_t init_config)
{
    int ret = 0;

    if (auto_hal != NULL) {
        ALOGD("%s: Auto hal module already exists",
                __func__);
        return ret;
    }

    auto_hal = calloc(1, sizeof(struct auto_hal_module));

    if (auto_hal == NULL) {
        ALOGE("%s: Memory allocation failed for auto hal module",
                __func__);
        return -ENOMEM;
    }

    auto_hal->adev = adev;

    fp_in_get_stream = init_config.fp_in_get_stream;
    fp_out_get_stream = init_config.fp_out_get_stream;
    fp_audio_extn_ext_hw_plugin_usecase_start = init_config.fp_audio_extn_ext_hw_plugin_usecase_start;
    fp_audio_extn_ext_hw_plugin_usecase_stop = init_config.fp_audio_extn_ext_hw_plugin_usecase_stop;
    fp_get_usecase_from_list = init_config.fp_get_usecase_from_list;
    fp_get_output_period_size = init_config.fp_get_output_period_size;
    fp_audio_extn_ext_hw_plugin_set_audio_gain = init_config.fp_audio_extn_ext_hw_plugin_set_audio_gain;
    fp_select_devices = init_config.fp_select_devices;
    fp_disable_audio_route = init_config.fp_disable_audio_route;
    fp_disable_snd_device = init_config.fp_disable_snd_device;
    fp_adev_get_active_input = init_config.fp_adev_get_active_input;
    fp_platform_set_echo_reference = init_config.fp_platform_set_echo_reference;
    fp_platform_get_eccarstate = init_config.fp_platform_get_eccarstate;
    fp_generate_patch_handle = init_config.fp_generate_patch_handle;

    return ret;
}

void auto_hal_deinit(void)
{
    if (auto_hal == NULL) {
        ALOGE("%s: Auto hal module is NULL, cannot deinitialize",
                __func__);
        return;
    }

    free(auto_hal);

    return;
}
