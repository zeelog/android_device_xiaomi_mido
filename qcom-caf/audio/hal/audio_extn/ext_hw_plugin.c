/*
* Copyright (c) 2014-2019 The Linux Foundation. All rights reserved.
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

#define LOG_TAG "audio_ext_hw_plugin"
#define LOG_NDEBUG 0

#include <errno.h>
#include <pthread.h>
#include <dlfcn.h>
#include <log/log.h>
#include <audio_hw.h>
#include "audio_extn.h"
#include "platform_api.h"
#include "platform.h"
#include "audio_hal_plugin.h"

//external feature dependency
static fp_b64decode_t fp_b64decode;
static fp_b64encode_t fp_b64encode;

typedef int32_t (*audio_hal_plugin_init_t)(void);
typedef int32_t (*audio_hal_plugin_deinit_t)(void);
typedef int32_t (*audio_hal_plugin_send_msg_t)(audio_hal_plugin_msg_type_t,
                                           void *, uint32_t);

struct ext_hw_plugin_data {
    struct audio_device           *adev;
    void                          *plugin_handle;
    audio_hal_plugin_init_t        audio_hal_plugin_init;
    audio_hal_plugin_deinit_t      audio_hal_plugin_deinit;
    audio_hal_plugin_send_msg_t    audio_hal_plugin_send_msg;
    int32_t                        usecase_ref_count[AUDIO_HAL_PLUGIN_USECASE_MAX];
    snd_device_t                   out_snd_dev[AUDIO_HAL_PLUGIN_USECASE_MAX];
    snd_device_t                   in_snd_dev[AUDIO_HAL_PLUGIN_USECASE_MAX];
    bool                           mic_mute;
};

/* This can be defined in platform specific file or use compile flag */
#define LIB_PLUGIN_DRIVER "libaudiohalplugin.so"

void* ext_hw_plugin_init(struct audio_device *adev, ext_hw_plugin_init_config_t init_config)
{
    int32_t ret = 0;
    struct ext_hw_plugin_data *my_plugin = NULL;

    my_plugin = calloc(1, sizeof(struct ext_hw_plugin_data));

    if (my_plugin == NULL) {
        ALOGE("[%s] Memory allocation failed for plugin data",__func__);
        return NULL;
    }

    my_plugin->adev = adev;

    fp_b64decode = init_config.fp_b64decode;
    fp_b64encode = init_config.fp_b64encode;

    my_plugin->plugin_handle = dlopen(LIB_PLUGIN_DRIVER, RTLD_NOW);
    if (my_plugin->plugin_handle == NULL) {
        ALOGE("%s: DLOPEN failed for %s", __func__, LIB_PLUGIN_DRIVER);
        goto plugin_init_fail;
    } else {
        ALOGV("%s: DLOPEN successful for %s", __func__, LIB_PLUGIN_DRIVER);
        my_plugin->audio_hal_plugin_init = (audio_hal_plugin_init_t)dlsym(
            my_plugin->plugin_handle, "audio_hal_plugin_init");
        if (!my_plugin->audio_hal_plugin_init) {
            ALOGE("%s: Could not find the symbol audio_hal_plugin_init from %s",
                  __func__, LIB_PLUGIN_DRIVER);
            goto plugin_init_fail;
        }

        my_plugin->audio_hal_plugin_deinit = (audio_hal_plugin_deinit_t)dlsym(
           my_plugin->plugin_handle, "audio_hal_plugin_deinit");
        if (!my_plugin->audio_hal_plugin_deinit) {
            ALOGE("%s: Could not find the symbol audio_hal_plugin_deinit from %s",
                  __func__, LIB_PLUGIN_DRIVER);
            goto plugin_init_fail;
        }

        my_plugin->audio_hal_plugin_send_msg = (audio_hal_plugin_send_msg_t)
            dlsym(my_plugin->plugin_handle, "audio_hal_plugin_send_msg");
        if (!my_plugin->audio_hal_plugin_send_msg) {
            ALOGE("%s: Could not find the symbol audio_hal_plugin_send_msg from %s",
                  __func__, LIB_PLUGIN_DRIVER);
            goto plugin_init_fail;
        }

        ret = my_plugin->audio_hal_plugin_init();
        if (ret) {
            ALOGE("%s: audio_hal_plugin_init failed with ret = %d",
               __func__, ret);
            goto plugin_init_fail;
        }
    }
    my_plugin->mic_mute = false;
    return my_plugin;

plugin_init_fail:
    if (my_plugin->plugin_handle != NULL)
        dlclose(my_plugin->plugin_handle);
    free(my_plugin);
    return NULL;
}

int32_t ext_hw_plugin_deinit(void *plugin)
{
    int32_t ret = 0;
    struct ext_hw_plugin_data *my_plugin = (struct ext_hw_plugin_data *)plugin;

    if (my_plugin == NULL) {
        ALOGE("[%s] NULL plugin pointer",__func__);
        return -EINVAL;
    }
    if (my_plugin->audio_hal_plugin_deinit) {
        ret = my_plugin->audio_hal_plugin_deinit();
        if (ret) {
            ALOGE("%s: audio_hal_plugin_deinit failed with ret = %d",
                  __func__, ret);
        }
    }
    if(my_plugin->plugin_handle != NULL)
        dlclose(my_plugin->plugin_handle);

    free(my_plugin);
    return ret;
}

static int32_t ext_hw_plugin_check_plugin_usecase(audio_usecase_t hal_usecase,
        audio_hal_plugin_usecase_type_t *plugin_usecase)
{
    int32_t ret = 0;

    switch(hal_usecase) {
    case USECASE_AUDIO_PLAYBACK_DEEP_BUFFER:
    case USECASE_AUDIO_PLAYBACK_LOW_LATENCY:
    case USECASE_AUDIO_PLAYBACK_MULTI_CH:
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
    case USECASE_AUDIO_PLAYBACK_MEDIA:
    case USECASE_AUDIO_PLAYBACK_SYS_NOTIFICATION:
        *plugin_usecase = AUDIO_HAL_PLUGIN_USECASE_DEFAULT_PLAYBACK;
        break;
    case USECASE_AUDIO_RECORD:
    case USECASE_AUDIO_RECORD_COMPRESS:
    case USECASE_AUDIO_RECORD_LOW_LATENCY:
    case USECASE_AUDIO_RECORD_FM_VIRTUAL:
        *plugin_usecase = AUDIO_HAL_PLUGIN_USECASE_DEFAULT_CAPTURE;
        break;
    case USECASE_AUDIO_HFP_SCO:
    case USECASE_AUDIO_HFP_SCO_WB:
    case USECASE_AUDIO_HFP_SCO_DOWNLINK:
    case USECASE_AUDIO_HFP_SCO_WB_DOWNLINK:
        *plugin_usecase = AUDIO_HAL_PLUGIN_USECASE_HFP_VOICE_CALL;
        break;
    case USECASE_VOICE_CALL:
    case USECASE_VOICEMMODE1_CALL:
        *plugin_usecase = AUDIO_HAL_PLUGIN_USECASE_CS_VOICE_CALL;
        break;
    case USECASE_AUDIO_PLAYBACK_NAV_GUIDANCE:
        *plugin_usecase = AUDIO_HAL_PLUGIN_USECASE_DRIVER_SIDE_PLAYBACK;
        break;
    case USECASE_AUDIO_PLAYBACK_PHONE:
        *plugin_usecase = AUDIO_HAL_PLUGIN_USECASE_PHONE_PLAYBACK;
        break;
    case USECASE_AUDIO_FM_TUNER_EXT:
       *plugin_usecase = AUDIO_HAL_PLUGIN_USECASE_FM_TUNER;
        break;
    default:
        ret = -EINVAL;
    }

    return ret;
}

int32_t ext_hw_plugin_usecase_start(void *plugin, struct audio_usecase *usecase)
{
    int32_t ret = 0;
    struct ext_hw_plugin_data *my_plugin = (struct ext_hw_plugin_data *)plugin;

    if ((my_plugin == NULL) || (usecase == NULL)) {
        ALOGE("[%s] NULL input pointer",__func__);
        return -EINVAL;
    }

    if (my_plugin->audio_hal_plugin_send_msg) {
        audio_hal_plugin_msg_type_t msg = AUDIO_HAL_PLUGIN_MSG_CODEC_ENABLE;
        audio_hal_plugin_codec_enable_t codec_enable;

        ret = ext_hw_plugin_check_plugin_usecase(usecase->id, &codec_enable.usecase);
        if(ret){
            ALOGI("%s: enable audio hal plugin skipped for audio usecase %d",
                    __func__, usecase->id);
            return 0;
        }

        if(usecase->id == USECASE_AUDIO_RECORD) {
            if(usecase->in_snd_device == SND_DEVICE_IN_SPEAKER_QMIC_AEC) {
              codec_enable.usecase = AUDIO_HAL_PLUGIN_USECASE_EC_CAPTURE;
            }
        }

        if(my_plugin->usecase_ref_count[codec_enable.usecase]){
            ALOGV("%s: plugin usecase %d already enabled",
                    __func__, codec_enable.usecase);
            my_plugin->usecase_ref_count[codec_enable.usecase]++;
            return 0;
        }

        if (((usecase->type == PCM_CAPTURE) || (usecase->type == VOICE_CALL) ||
              (usecase->type == VOIP_CALL) || (usecase->type == PCM_HFP_CALL) ||
              (usecase->type == PCM_PASSTHROUGH)) &&
            (usecase->in_snd_device != SND_DEVICE_NONE)) {
            codec_enable.snd_dev = usecase->in_snd_device;
            /* TODO - below should be related with in_snd_dev */
            codec_enable.sample_rate = 48000;
            codec_enable.bit_width = 16;
            codec_enable.num_chs = 2;

            ALOGD("%s: enable audio hal plugin input, %d, %d, %d, %d, %d",
                __func__, (int)codec_enable.usecase,
                (int)codec_enable.snd_dev,
                (int)codec_enable.sample_rate,
                (int)codec_enable.bit_width,
                (int)codec_enable.num_chs);
            ret = my_plugin->audio_hal_plugin_send_msg(msg,
                (void*)&codec_enable, sizeof(codec_enable));
            if (ret) {
                ALOGE("%s: enable audio hal plugin input failed ret = %d",
                    __func__, ret);
                return ret;
            }
            my_plugin->in_snd_dev[codec_enable.usecase] = codec_enable.snd_dev;

            if (my_plugin->mic_mute &&
                codec_enable.usecase == AUDIO_HAL_PLUGIN_USECASE_HFP_VOICE_CALL) {
                int plugin_ret;
                audio_hal_plugin_codec_set_pp_mute_t pp_mute;

                pp_mute.usecase = codec_enable.usecase;
                pp_mute.snd_dev = codec_enable.snd_dev;
                pp_mute.ch_mask = AUDIO_CHANNEL_IN_ALL;
                pp_mute.flag = my_plugin->mic_mute;

                ALOGV("%s: sending codec pp mute msg to HAL plugin driver, %d, %d, %x, %d",
                        __func__, (int)pp_mute.usecase, (int)pp_mute.snd_dev,
                        (int)pp_mute.ch_mask, (int)pp_mute.flag);
                plugin_ret = my_plugin->audio_hal_plugin_send_msg(
                               AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_MUTE, &pp_mute,
                               sizeof(pp_mute));
                if (plugin_ret)
                  ALOGE("%s: Failed to set plugin pp mute err: %d", __func__, plugin_ret);
            }
        }

        if (((usecase->type == PCM_CAPTURE) &&
            (usecase->id == USECASE_AUDIO_RECORD) &&
            (usecase->in_snd_device == SND_DEVICE_IN_SPEAKER_QMIC_AEC)) ||
            ((usecase->type == PCM_HFP_CALL) &&
            ((usecase->id == USECASE_AUDIO_HFP_SCO) || (usecase->id == USECASE_AUDIO_HFP_SCO_WB)) &&
            (usecase->in_snd_device == SND_DEVICE_IN_VOICE_SPEAKER_MIC_HFP_MMSECNS))) {
            audio_hal_plugin_codec_enable_t codec_enable_ec = {0,};
            codec_enable_ec.snd_dev = usecase->in_snd_device;
            // TODO - below should be related with in_snd_dev
            codec_enable_ec.sample_rate = 48000;
            codec_enable_ec.bit_width = 16;
            codec_enable_ec.num_chs = 6;
            codec_enable_ec.usecase = AUDIO_HAL_PLUGIN_USECASE_EC_REF_CAPTURE;

            ALOGD("%s: enable audio hal plugin input for echo reference, %d, %d, %d, %d, %d",
                __func__, (int)codec_enable_ec.usecase,
                (int)codec_enable_ec.snd_dev,
                (int)codec_enable_ec.sample_rate,
                (int)codec_enable_ec.bit_width,
                (int)codec_enable_ec.num_chs);
            ret = my_plugin->audio_hal_plugin_send_msg(msg,
                (void*)&codec_enable_ec, sizeof(codec_enable_ec));
            if (ret) {
                ALOGE("%s: enable audio hal plugin input failed ret = %d",
                    __func__, ret);
                return ret;
            }
        }

        if (((usecase->type == PCM_PLAYBACK) || (usecase->type == VOICE_CALL) ||
                (usecase->type == VOIP_CALL) || (usecase->type == PCM_HFP_CALL)) &&
            (usecase->out_snd_device != SND_DEVICE_NONE)) {
            codec_enable.snd_dev = usecase->out_snd_device;
            /* TODO - below should be related with out_snd_dev */
            codec_enable.sample_rate = 48000;
            codec_enable.bit_width = 24;
            codec_enable.num_chs = 2;

            ALOGD("%s: enable audio hal plugin output, %d, %d, %d, %d, %d",
                __func__, (int)codec_enable.usecase,
                (int)codec_enable.snd_dev,
                (int)codec_enable.sample_rate,
                (int)codec_enable.bit_width,
                (int)codec_enable.num_chs);
            ret = my_plugin->audio_hal_plugin_send_msg(msg,
                (void*)&codec_enable, sizeof(codec_enable));
            if (ret) {
                ALOGE("%s: enable audio hal plugin output failed ret = %d",
                    __func__, ret);
                return ret;
            }
            my_plugin->out_snd_dev[codec_enable.usecase] = codec_enable.snd_dev;
        }
        my_plugin->usecase_ref_count[codec_enable.usecase]++;
    }

    ALOGD("%s: finished ext_hw_plugin usecase start", __func__);

    return ret;
}

int32_t ext_hw_plugin_usecase_stop(void *plugin, struct audio_usecase *usecase)
{
    int32_t ret = 0;
    struct ext_hw_plugin_data *my_plugin = (struct ext_hw_plugin_data *)plugin;

    if ((my_plugin == NULL) || (usecase == NULL)) {
        ALOGE("[%s] NULL input pointer",__func__);
        return -EINVAL;
    }
    if (my_plugin->audio_hal_plugin_send_msg) {
        audio_hal_plugin_msg_type_t msg = AUDIO_HAL_PLUGIN_MSG_CODEC_DISABLE;
        audio_hal_plugin_codec_disable_t codec_disable;

        ret = ext_hw_plugin_check_plugin_usecase(usecase->id, &codec_disable.usecase);
        if(ret){
            ALOGI("%s: disable audio hal plugin skipped for audio usecase %d",
                    __func__, usecase->id);
            return 0;
        }

        if((usecase->id == USECASE_AUDIO_RECORD) &&
           (usecase->in_snd_device == SND_DEVICE_IN_SPEAKER_QMIC_AEC))
        {
           codec_disable.usecase = AUDIO_HAL_PLUGIN_USECASE_EC_CAPTURE;
        }
        if(my_plugin->usecase_ref_count[codec_disable.usecase] > 1){
            ALOGI("%s: plugin usecase %d still in use and can not be disabled",
                    __func__, codec_disable.usecase);
            my_plugin->usecase_ref_count[codec_disable.usecase]--;
            return 0;
        } else if(my_plugin->usecase_ref_count[codec_disable.usecase] < 1){
            ALOGE("%s: plugin usecase %d not enabled",
                    __func__, codec_disable.usecase);
            return -EINVAL;
        }

        if (((usecase->type == PCM_PLAYBACK) || (usecase->type == VOICE_CALL) ||
                (usecase->type == VOIP_CALL) || (usecase->type == PCM_HFP_CALL)) &&
            (usecase->out_snd_device != SND_DEVICE_NONE)) {
            codec_disable.snd_dev = usecase->out_snd_device;

            ALOGD("%s: disable audio hal plugin output, %d, %d",
                __func__, (int)codec_disable.usecase,
                (int)codec_disable.snd_dev);
            ret = my_plugin->audio_hal_plugin_send_msg(msg,
                (void*)&codec_disable, sizeof(codec_disable));
            if (ret) {
                ALOGE("%s: disable audio hal plugin output failed ret = %d",
                    __func__, ret);
            }
            my_plugin->out_snd_dev[codec_disable.usecase] = 0;
        }
        if (((usecase->type == PCM_CAPTURE) || (usecase->type == VOICE_CALL) ||
             (usecase->type == VOIP_CALL) || (usecase->type == PCM_HFP_CALL) ||
             (usecase->type == PCM_PASSTHROUGH)) &&
            (usecase->in_snd_device != SND_DEVICE_NONE)) {
            codec_disable.snd_dev = usecase->in_snd_device;

            ALOGD("%s: disable audio hal plugin input, %d, %d",
                __func__, (int)codec_disable.usecase,
                (int)codec_disable.snd_dev);
            ret = my_plugin->audio_hal_plugin_send_msg(msg,
                (void*)&codec_disable, sizeof(codec_disable));
            if (ret) {
                ALOGE("%s: disable audio hal plugin input failed ret = %d",
                    __func__, ret);
            }
            my_plugin->in_snd_dev[codec_disable.usecase] = 0;
        }

        if ((usecase->type == PCM_CAPTURE) &&
            (usecase->id ==  USECASE_AUDIO_RECORD) &&
            (usecase->in_snd_device == SND_DEVICE_IN_SPEAKER_QMIC_AEC)) {
            audio_hal_plugin_codec_disable_t codec_disable_ec = {0,};
            codec_disable_ec.snd_dev = usecase->in_snd_device;
            codec_disable_ec.usecase = AUDIO_HAL_PLUGIN_USECASE_EC_REF_CAPTURE;

            ALOGD("%s: disable audio hal plugin input for echo reference, %d, %d",
                __func__, (int)codec_disable_ec.usecase,
                (int)codec_disable_ec.snd_dev);
            ret = my_plugin->audio_hal_plugin_send_msg(msg,
                (void*)&codec_disable_ec, sizeof(codec_disable_ec));
            if (ret) {
                ALOGE("%s: disable audio hal plugin input failed ret = %d",
                    __func__, ret);
                return ret;
            }
        }
        my_plugin->usecase_ref_count[codec_disable.usecase]--;
    }

    ALOGD("%s: finished ext_hw_plugin usecase stop", __func__);

    return ret;
}

static int32_t ext_hw_plugin_string_to_dword(char *string_value, void **dword_ptr,
        uint32_t dword_len)
{
    int32_t ret = 0;
    uint32_t i,tmp;
    uint8_t *dptr = NULL;
    uint8_t *tmpptr = NULL;
    int32_t dlen;
    uint32_t *outptr = NULL;

    dlen = strlen(string_value);
    if (dlen <= 0) {
        ALOGE("%s: NULL data received", __func__);
        return -EINVAL;
    }
    dptr = (uint8_t*) calloc(dlen, sizeof(uint8_t));
    if (dptr == NULL) {
        ALOGE("%s: memory allocation failed", __func__);
        return -ENOMEM;
    }
    dlen = fp_b64decode(string_value, strlen(string_value), dptr);
    if ((dlen <= 0) || ((uint32_t)dlen != 4*dword_len)){
        ALOGE("%s: data decoding failed", __func__);
        ret = -EINVAL;
        goto done_string_to_dword;
    }

    outptr = calloc(dword_len, sizeof(uint32_t));
    if (outptr == NULL) {
        ALOGE("%s: memory allocation failed", __func__);
        ret = -ENOMEM;
        goto done_string_to_dword;
    }

    for(i=0; i<dword_len; i++) {
        tmpptr = dptr+4*i;
        tmp = (uint32_t) *(tmpptr);
        tmp |= ((uint32_t) *(tmpptr+1))<<8;
        tmp |= ((uint32_t) *(tmpptr+2))<<16;
        tmp |= ((uint32_t) *(tmpptr+3))<<24;
        *(outptr + i) = tmp;
    }
    *dword_ptr = (void*)outptr;

done_string_to_dword:
    if (dptr != NULL)
        free(dptr);

    return ret;
}

static int32_t ext_hw_plugin_dword_to_string(uint32_t *dword_ptr, uint32_t dword_len,
            char **string_ptr)
{
    int32_t ret = 0;
    uint32_t i,tmp;
    uint8_t *dptr = NULL;
    uint8_t *tmpptr = NULL;
    int32_t dlen;
    char *outptr = NULL;

    dptr = (uint8_t*)calloc(dword_len, sizeof(uint32_t));
    if(dptr == NULL) {
        ALOGE("[%s] Memory allocation failed for dword length %d",__func__,dword_len);
        return -ENOMEM;
    }

    /* convert dword to byte array */
    for(i=0; i<dword_len; i++) {
        tmp = *(dword_ptr + i);
        tmpptr = dptr+4*i;
        *tmpptr = (uint8_t) (tmp & 0xFF);
        *(tmpptr + 1) = (uint8_t) ((tmp>>8) & 0xFF);
        *(tmpptr + 2) = (uint8_t) ((tmp>>16) & 0xFF);
        *(tmpptr + 3) = (uint8_t) ((tmp>>24) & 0xFF);
    }

    /* Allocate memory for encoding */
    dlen = dword_len * 4;
    outptr = (char*)calloc((dlen*2), sizeof(char));
    if(outptr == NULL) {
        ALOGE("[%s] Memory allocation failed for size %d",
                    __func__, dlen*2);
        ret = -ENOMEM;
        goto done_dword_to_string;
    }

    ret = fp_b64encode(dptr, dlen, outptr);
    if(ret < 0) {
        ALOGE("[%s] failed to convert data to string ret = %d", __func__, ret);
        free(outptr);
        ret = -EINVAL;
        goto done_dword_to_string;
    }
    *string_ptr = outptr;

done_dword_to_string:
    if (dptr != NULL)
        free(dptr);

    return ret;
}


int32_t ext_hw_plugin_set_parameters(void *plugin, struct str_parms *parms)
{

    char *value = NULL;
    int32_t val, len = 0;
    int32_t ret = 0, err;
    char *kv_pairs = NULL;
    struct ext_hw_plugin_data *my_plugin = NULL;

    if (plugin == NULL || parms == NULL) {
        ALOGE("[%s] received null pointer",__func__);
        return -EINVAL;
    }

    my_plugin = (struct ext_hw_plugin_data *)plugin;
    if (!my_plugin->audio_hal_plugin_send_msg) {
        ALOGE("%s: NULL audio_hal_plugin_send_msg func ptr", __func__);
        return -EINVAL;
    }

    err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_MSG_TYPE, &val);
    if (err < 0) {
        ALOGE("%s: Invalid or missing TYPE param for plugin msg", __func__);
        return -EINVAL;
    }
    ALOGD("%s: received plugin msg type (%d)", __func__, val);
    str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_MSG_TYPE);

    if(val == AUDIO_HAL_PLUGIN_MSG_CODEC_TUNNEL_CMD ||
        val == AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_EQ) {
        kv_pairs = str_parms_to_str(parms);
        if (kv_pairs == NULL) {
            ret = -EINVAL;
            ALOGE("%s: key-value pair is NULL", __func__);
            goto done;
        }
        len = strlen(kv_pairs);
        value = (char*)calloc(len, sizeof(char));
        if (value == NULL) {
            ret = -ENOMEM;
            ALOGE("[%s] failed to allocate memory",__func__);
            goto done;
        }
    }

    if (val == AUDIO_HAL_PLUGIN_MSG_CODEC_TUNNEL_CMD) {
        uint32_t plsize;
        int32_t *plptr = NULL;

        err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_TUNNEL_SIZE,
                (int*)&plsize);
        if ((err < 0) || (!plsize)) {
            ALOGE("%s: Invalid or missing size param for TUNNEL command", __func__);
            ret = -EINVAL;
            goto done;
        }
        str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_TUNNEL_SIZE);

        err = str_parms_get_str(parms,
                AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_TUNNEL_DATA, value, len);
        if (err < 0) {
            ALOGE("%s: Invalid or missing band_data for TUNNEL command", __func__);
            ret = -EINVAL;
            goto done;
        }
        str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_TUNNEL_DATA);

        ret = ext_hw_plugin_string_to_dword(value, (void**)&plptr, plsize);
        if (ret) {
            ALOGE("%s: Failed to parse payload for TUNNEL command", __func__);
            ret = -EINVAL;
            goto done_tunnel;
        }

        audio_hal_plugin_msg_type_t msg =
                AUDIO_HAL_PLUGIN_MSG_CODEC_TUNNEL_CMD;

        ALOGD("%s: sending codec tunnel cmd msg to HAL plugin driver,size = %d",
                __func__, (int)plsize);

        ret = my_plugin->audio_hal_plugin_send_msg(msg, (void*)plptr, plsize);
        if (ret) {
            ALOGE("%s: Failed to send plugin tunnel cmd err: %d", __func__, ret);
        }

done_tunnel:
        if (plptr!= NULL)
            free(plptr);
    } else {
        audio_hal_plugin_usecase_type_t use_case;
        audio_hal_plugin_direction_type_t dir;
        snd_device_t snd_dev = 0;

        err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_UC,
                &use_case);
        if (err < 0) {
            ALOGE("%s: Invalid or missing usecase param for plugin msg", __func__);
            ret = -EINVAL;
            /* TODO: do we need to support no use case in kvpair? */
            goto done;
        }
        str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_UC);
        if ((use_case < 0) || (use_case >= AUDIO_HAL_PLUGIN_USECASE_MAX)) {
            ALOGE("%s: Invalid usecase param for plugin msg", __func__);
            ret = -EINVAL;
            goto done;
        }

        if (my_plugin->usecase_ref_count[use_case] == 0) {
            /* allow param set when usecase not enabled */
            ALOGI("%s: plugin usecase (%d) is not enabled", __func__, use_case);
        } else {
            err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_DIRECTION,
                    &dir);
            if (err < 0) {
                if (my_plugin->out_snd_dev[use_case]) {
                    snd_dev = my_plugin->out_snd_dev[use_case];
                } else if (my_plugin->in_snd_dev[use_case]) {
                    snd_dev = my_plugin->in_snd_dev[use_case];
                } else {
                    ALOGE("%s: No valid snd_device found for the usecase (%d)",
                            __func__, use_case);
                    ret = -EINVAL;
                    goto done;
                }
            } else {
                str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_DIRECTION);

                switch(dir) {
                case AUDIO_HAL_PLUGIN_DIRECTION_PLAYBACK:
                {
                    if (!my_plugin->out_snd_dev[use_case]) {
                        ALOGE("%s: No valid out_snd_device found for playback (%d)",
                                __func__, use_case);
                        ret = -EINVAL;
                        goto done;
                    }
                    snd_dev = my_plugin->out_snd_dev[use_case];
                    break;
                }
                case AUDIO_HAL_PLUGIN_DIRECTION_CAPTURE:
                {
                    if (!my_plugin->in_snd_dev[use_case]) {
                        ALOGE("%s: No valid in_snd_device found for capture (%d)",
                                __func__, use_case);
                        ret = -EINVAL;
                        goto done;
                    }
                    snd_dev = my_plugin->in_snd_dev[use_case];
                    break;
                }
                default:
                    ALOGE("%s: Invalid direction param for plugin msg", __func__);
                    ret = -EINVAL;
                    goto done;
                }
            }
        }

        switch(val) {
        case AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_VOLUME:
        {
            audio_hal_plugin_codec_set_pp_vol_t pp_vol;
            memset(&pp_vol,0,sizeof(pp_vol));
            err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_CMASK,
                    (int*)&pp_vol.ch_mask);
            if ((err < 0)) {
                /* TODO: properly handle no cmask param from client case */
                ALOGE("%s: Invalid or missing CMASK param for SET_PP_VOLUME", __func__);
                ret = -EINVAL;
                goto done;
            }
            str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_CMASK);
            err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_GAIN,
                    (int*)&pp_vol.gain);
            if ((err < 0)) {
                /* TODO: properly handle no gain param from client case */
                ALOGE("%s: Invalid or missing GAIN param for SET_PP_VOLUME", __func__);
                ret = -EINVAL;
                goto done;
            }
            str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_GAIN);

            audio_hal_plugin_msg_type_t msg =
                    AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_VOLUME;
            pp_vol.usecase = use_case;
            pp_vol.snd_dev= snd_dev;

            ALOGD("%s: sending codec pp vol msg to HAL plugin driver, %d, %d, %d, %d",
                    __func__, (int)pp_vol.usecase, (int)pp_vol.snd_dev,
                    (int)pp_vol.ch_mask, (int)pp_vol.gain);

            ret = my_plugin->audio_hal_plugin_send_msg(msg, &pp_vol, sizeof(pp_vol));
            if (ret) {
                ALOGE("%s: Failed to set plugin pp vol err: %d", __func__, ret);
            }
            break;
        }
        case AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_MUTE:
        {
            int32_t flag;
            audio_hal_plugin_codec_set_pp_mute_t pp_mute;
            memset(&pp_mute,0,sizeof(pp_mute));
            err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_CMASK,
                    (int*)&pp_mute.ch_mask);
            if ((err < 0)) {
                /* TODO: properly handle no cmask param from client case */
                ALOGE("%s: Invalid or missing CMASK param for SET_PP_MUTE", __func__);
                ret = -EINVAL;
                goto done;
            }
            str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_CMASK);
            err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_MUTE_FLAG,
                    (int*)&flag);
            if ((err < 0)) {
                ALOGE("%s: Invalid or missing FLAG param for SET_PP_MUTE", __func__);
                ret = -EINVAL;
                goto done;
            }
            str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_MUTE_FLAG);
            pp_mute.flag = (bool)flag;

            audio_hal_plugin_msg_type_t msg =
                    AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_MUTE;
            pp_mute.usecase = use_case;
            pp_mute.snd_dev= snd_dev;

            ALOGD("%s: sending codec pp mute msg to HAL plugin driver, %d, %d, %d, %d",
                    __func__, (int)pp_mute.usecase, (int)pp_mute.snd_dev,
                    (int)pp_mute.ch_mask, (int)pp_mute.flag);

            ret = my_plugin->audio_hal_plugin_send_msg(msg, &pp_mute,
                    sizeof(pp_mute));
            if (ret) {
                ALOGE("%s: Failed to set plugin pp vol err: %d", __func__, ret);
            }
            break;
        }
        case AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_FADE:
        {
            audio_hal_plugin_codec_set_pp_fade_t pp_fade;
            memset(&pp_fade,0,sizeof(pp_fade));
            err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_FADE,
                    (int*)&pp_fade.fade);
            if ((err < 0)) {
                ALOGE("%s: Invalid or missing FADE param for SET_PP_FADE", __func__);
                ret = -EINVAL;
                goto done;
            }
            str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_FADE);

            audio_hal_plugin_msg_type_t msg =
                        AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_FADE;
            pp_fade.usecase = use_case;
            pp_fade.snd_dev= snd_dev;

            ALOGD("%s: sending codec pp fade msg to HAL plugin driver, %d, %d, %d",
                    __func__, (int)pp_fade.usecase, (int)pp_fade.snd_dev,
                    (int)pp_fade.fade);

            ret = my_plugin->audio_hal_plugin_send_msg(msg, &pp_fade,
                    sizeof(pp_fade));
            if (ret) {
                ALOGE("%s: Failed to set plugin pp fade err: %d", __func__, ret);
            }
            break;
        }
        case AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_BALANCE:
        {
            audio_hal_plugin_codec_set_pp_balance_t pp_balance;
            memset(&pp_balance,0,sizeof(pp_balance));
            err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_BALANCE,
                    (int*)&pp_balance.balance);
            if ((err < 0)) {
                ALOGE("%s: Invalid or missing balance param for SET_PP_BALANCE", __func__);
                ret = -EINVAL;
                goto done;
            }
            str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_BALANCE);

            audio_hal_plugin_msg_type_t msg =
                    AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_BALANCE;
            pp_balance.usecase = use_case;
            pp_balance.snd_dev= snd_dev;

            ALOGD("%s: sending codec pp balance msg to HAL plugin driver, %d, %d, %d",
                    __func__, (int)pp_balance.usecase, (int)pp_balance.snd_dev,
                    (int)pp_balance.balance);

            ret = my_plugin->audio_hal_plugin_send_msg(msg, &pp_balance,
                    sizeof(pp_balance));
            if (ret) {
                ALOGE("%s: Failed to set plugin pp balance err: %d", __func__, ret);
            }
            break;
        }
        case AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_BMT:
        {
            int32_t filter_type, enable_flag;
            audio_hal_plugin_codec_set_pp_bmt_t pp_bmt;
            memset(&pp_bmt,0,sizeof(pp_bmt));
            err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_BMT_FTYPE,
                    (int*)&filter_type);
            if ((err < 0)) {
                ALOGE("%s: Invalid or missing filter type param for SET_PP_BMT", __func__);
                ret = -EINVAL;
                goto done;
            }
            str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_BMT_FTYPE);
            if ((filter_type <= AUDIO_HAL_PLUGIN_CODEC_PP_FILTER_TYPE_INVALID) &&
                    (filter_type >= AUDIO_HAL_PLUGIN_CODEC_PP_FILTER_TYPE_MAX)) {
                ALOGE("%s: Invalid filter type value for SET_PP_BMT", __func__);
                ret = -EINVAL;
                goto done;
            }
            pp_bmt.filter_type = filter_type;
            err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_BMT_FLAG,
                    (int*)&enable_flag);
            if ((err < 0)) {
                ALOGE("%s: Invalid or missing enable flag param for SET_PP_BMT", __func__);
                ret = -EINVAL;
                goto done;
            }
            str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_BMT_FLAG);
            pp_bmt.enable_flag = (bool)enable_flag;
            err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_BMT_VAL,
                    (int*)&pp_bmt.value);
            if ((err < 0)) {
                ALOGE("%s: Invalid or missing value param for SET_PP_BMT", __func__);
                ret = -EINVAL;
                goto done;
            }
            str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_BMT_VAL);

            audio_hal_plugin_msg_type_t msg =
                    AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_BMT;
            pp_bmt.usecase = use_case;
            pp_bmt.snd_dev= snd_dev;

            ALOGD("%s: sending codec pp bmt msg to HAL plugin driver,%d,%d,%d,%d,%d",
                    __func__, (int)pp_bmt.usecase, (int)pp_bmt.snd_dev,
                    (int)pp_bmt.filter_type, (int)pp_bmt.enable_flag,
                    (int)pp_bmt.value);

            ret = my_plugin->audio_hal_plugin_send_msg(msg, &pp_bmt,
                    sizeof(pp_bmt));
            if (ret) {
                ALOGE("%s: Failed to set plugin pp bmt err: %d", __func__, ret);
            }
            break;
        }
        case AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_EQ:
        {
            int32_t enable_flag;
            audio_hal_plugin_codec_set_pp_eq_t pp_eq;
            memset(&pp_eq,0,sizeof(pp_eq));

            err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_EQ_FLAG,
                    (int*)&enable_flag);
            if (err < 0) {
                ALOGE("%s: Invalid or missing enable flag param for SET_PP_EQ", __func__);
                ret = -EINVAL;
                goto done;
            }
            str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_EQ_FLAG);
            pp_eq.enable_flag = (bool)enable_flag;

            err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_EQ_ID,
                    (int*)&pp_eq.preset_id);
            if (err < 0) {
                ALOGE("%s: Invalid or missing preset_id param for SET_PP_EQ", __func__);
                ret = -EINVAL;
                goto done;
            }
            str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_EQ_ID);
            if (pp_eq.preset_id < -1) {
                ALOGE("%s: Invalid preset_id param for SET_PP_EQ", __func__);
                ret = -EINVAL;
                goto done;
            }

            if (pp_eq.preset_id == -1) {
                err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_EQ_NUM_BANDS,
                        (int*)&pp_eq.num_bands);
                if (err < 0) {
                    ALOGE("%s: Invalid or missing num_bands param for SET_PP_EQ", __func__);
                    ret = -EINVAL;
                    goto done;
                }
                str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_EQ_NUM_BANDS);
                if (!pp_eq.num_bands) {
                    ALOGE("%s: Invalid num_bands param for SET_PP_EQ", __func__);
                    ret = -EINVAL;
                    goto done;
                }

                err = str_parms_get_str(parms,
                        AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_EQ_BAND_DATA, value, len);
                if (err < 0) {
                    ALOGE("%s: Invalid or missing band_data for SET_PP_EQ", __func__);
                    ret = -EINVAL;
                    goto done;
                }
                str_parms_del(parms, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_EQ_BAND_DATA);

                ret = ext_hw_plugin_string_to_dword(value, (void**)&pp_eq.bands,
                        3*pp_eq.num_bands);
                if (ret) {
                    ALOGE("%s: Failed to parse band info for SET_PP_EQ", __func__);
                    ret = -EINVAL;
                    goto done_eq;
                }
            }

            audio_hal_plugin_msg_type_t msg =
                    AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_EQ;
            pp_eq.usecase = use_case;
            pp_eq.snd_dev= snd_dev;

            ALOGD("%s: sending codec pp eq msg to HAL plugin driver,%d,%d,%d,%d,%d",
                    __func__, (int)pp_eq.usecase, (int)pp_eq.snd_dev,
                    (int)pp_eq.enable_flag, (int)pp_eq.preset_id,
                    (int)pp_eq.num_bands);

            ret = my_plugin->audio_hal_plugin_send_msg(msg, &pp_eq, sizeof(pp_eq));
            if (ret) {
                ALOGE("%s: Failed to set plugin pp eq err: %d", __func__, ret);
            }

done_eq:
            if (pp_eq.bands != NULL)
                free(pp_eq.bands);
            break;
        }
        default:
            ALOGE("%s: Invalid plugin message type: %d", __func__, val);
            ret = -EINVAL;
        }
    }

done:
    ALOGI("%s: exit with code(%d)", __func__, ret);
    if(kv_pairs != NULL)
        free(kv_pairs);
    if(value != NULL)
        free(value);
    return ret;
}

int ext_hw_plugin_get_parameters(void *plugin,
                  struct str_parms *query, struct str_parms *reply)
{
    char *value = NULL;
    int32_t val, len = 0;;
    int32_t ret = 0, err;
    int32_t rbuf_dlen = 0;
    uint32_t *rbuf_dptr = NULL;
    char *rparms = NULL;
    audio_hal_plugin_usecase_type_t use_case = AUDIO_HAL_PLUGIN_USECASE_INVALID;
    snd_device_t snd_dev = 0;
    struct ext_hw_plugin_data *my_plugin = NULL;
    char *kv_pairs = NULL;

    if(plugin == NULL || query == NULL || reply == NULL) {
        ALOGE("[%s] received null pointer",__func__);
        return -EINVAL;
    }

    err = str_parms_get_int(query, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_MSG_TYPE, &val);
    if (err < 0) {
        ALOGE("%s: Invalid or missing TYPE param for plugin msg", __func__);
        return -EINVAL;
    }
    ALOGD("%s: received plugin msg type (%d)", __func__, val);
    str_parms_del(query, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_MSG_TYPE);

    my_plugin = (struct ext_hw_plugin_data *)plugin;
    if (!my_plugin->audio_hal_plugin_send_msg) {
        ALOGE("%s: NULL audio_hal_plugin_send_msg func ptr", __func__);
        ret = -EINVAL;
        goto done_get_param;
    }

    if(val == AUDIO_HAL_PLUGIN_MSG_CODEC_TUNNEL_GET_CMD) {
        kv_pairs = str_parms_to_str(query);
        if (kv_pairs == NULL) {
            ret = -EINVAL;
            ALOGE("%s: key-value pair is NULL", __func__);
            goto done_get_param;
        }
        len = strlen(kv_pairs);
        value = (char*)calloc(len, sizeof(char));
        if (value == NULL) {
            ret = -ENOMEM;
            ALOGE("[%s] failed to allocate memory",__func__);
            goto done_get_param;
        }
    } else {
        err = str_parms_get_int(query, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_UC,
                &use_case);
        if (err < 0) {
            ALOGI("%s: Invalid or missing usecase param for plugin msg", __func__);
            use_case = AUDIO_HAL_PLUGIN_USECASE_INVALID;
        } else {
            str_parms_del(query, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_UC);

            if ((use_case < 0) || (use_case >= AUDIO_HAL_PLUGIN_USECASE_MAX)) {
                ALOGI("%s: Invalid usecase param for plugin msg", __func__);
                use_case = AUDIO_HAL_PLUGIN_USECASE_INVALID;
                goto done_get_param;
            }

            if (my_plugin->usecase_ref_count[use_case] == 0) {
                ALOGI("%s: plugin usecase (%d) is not enabled",
                        __func__, use_case);
            } else {
                /* TODO: confirm this handles all usecase */
                if (my_plugin->out_snd_dev[use_case]) {
                    snd_dev = my_plugin->out_snd_dev[use_case];
                } else if (my_plugin->in_snd_dev[use_case]) {
                    snd_dev = my_plugin->in_snd_dev[use_case];
                } else {
                    ALOGE("%s: No valid snd_device found for the usecase (%d)",
                            __func__, use_case);
                    ret = -EINVAL;
                    goto done_get_param;
                }
            }
        }
    }

    switch(val) {
    case AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_VOLUME:
    {
        audio_hal_plugin_codec_get_pp_vol_t get_pp_vol;
        memset(&get_pp_vol,0,sizeof(get_pp_vol));
        err = str_parms_get_int(query, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_CMASK,
                (int*)&get_pp_vol.ch_mask);
        if ((err < 0)) {
            ALOGI("%s: Invalid or missing CMASK param for GET_PP_VOLUME", __func__);
            get_pp_vol.ch_mask = AUDIO_CHANNEL_NONE;
        } else {
            str_parms_del(query, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_CMASK);
        }

        audio_hal_plugin_msg_type_t msg =
                AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_VOLUME;
        get_pp_vol.usecase = use_case;
        get_pp_vol.snd_dev = snd_dev;

        ALOGD("%s: sending get codec pp vol msg to HAL plugin driver, %d, %d, %d",
                __func__, (int)get_pp_vol.usecase, (int)get_pp_vol.snd_dev,
                (int)get_pp_vol.ch_mask);

        ret = my_plugin->audio_hal_plugin_send_msg(msg, &get_pp_vol, sizeof(get_pp_vol));
        if (ret) {
            ALOGE("%s: Failed to get plugin pp vol err: %d", __func__, ret);
            goto done_get_param;
        }

        rbuf_dlen = sizeof(get_pp_vol.ret_gain)/sizeof(int32_t);
        rbuf_dptr = calloc(rbuf_dlen, sizeof(uint32_t));
        if(rbuf_dptr == NULL) {
            ALOGE("[%s] Memory allocation failed for dword length %d",__func__,rbuf_dlen);
            ret = -ENOMEM;
            goto done_get_param;
        }
        memcpy(rbuf_dptr, &get_pp_vol.ret_gain, sizeof(get_pp_vol.ret_gain));
        break;
    }
    case AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_FADE:
    {
        audio_hal_plugin_codec_get_pp_fade_t get_pp_fade;
        memset(&get_pp_fade,0,sizeof(get_pp_fade));

        audio_hal_plugin_msg_type_t msg =
                AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_FADE;
        get_pp_fade.usecase = use_case;
        get_pp_fade.snd_dev = snd_dev;

        ALOGD("%s: sending get codec pp fade msg to HAL plugin driver, %d, %d",
                __func__, (int)get_pp_fade.usecase, (int)get_pp_fade.snd_dev);

        ret = my_plugin->audio_hal_plugin_send_msg(msg, &get_pp_fade, sizeof(get_pp_fade));
        if (ret) {
            ALOGE("%s: Failed to get plugin pp fade err: %d", __func__, ret);
            goto done_get_param;
        }

        rbuf_dlen = sizeof(get_pp_fade.ret_fade)/sizeof(int32_t);
        rbuf_dptr = calloc(rbuf_dlen, sizeof(uint32_t));
        if(rbuf_dptr == NULL) {
            ALOGE("[%s] Memory allocation failed for dword length %d",__func__,rbuf_dlen);
            ret = -ENOMEM;
            goto done_get_param;
        }
        memcpy(rbuf_dptr, &get_pp_fade.ret_fade, sizeof(get_pp_fade.ret_fade));
        break;
    }
    case AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_BALANCE:
    {
        audio_hal_plugin_codec_get_pp_balance_t get_pp_balance;
        memset(&get_pp_balance,0,sizeof(get_pp_balance));

        audio_hal_plugin_msg_type_t msg =
                AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_BALANCE;
        get_pp_balance.usecase = use_case;
        get_pp_balance.snd_dev = snd_dev;

        ALOGD("%s: sending get codec pp balance msg to HAL plugin driver, %d, %d",
                __func__, (int)get_pp_balance.usecase, (int)get_pp_balance.snd_dev);

        ret = my_plugin->audio_hal_plugin_send_msg(msg, &get_pp_balance,
            sizeof(get_pp_balance));
        if (ret) {
            ALOGE("%s: Failed to get plugin pp balance err: %d", __func__, ret);
            goto done_get_param;
        }

        rbuf_dlen = sizeof(get_pp_balance.ret_balance)/sizeof(int32_t);
        rbuf_dptr = calloc(rbuf_dlen, sizeof(uint32_t));
        if(rbuf_dptr == NULL) {
            ALOGE("[%s] Memory allocation failed for dword length %d",__func__,rbuf_dlen);
            ret = -ENOMEM;
            goto done_get_param;
        }
        memcpy(rbuf_dptr, &get_pp_balance.ret_balance, sizeof(get_pp_balance.ret_balance));
        break;
    }
    case AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_BMT:
    {
        int32_t filter_type;
        audio_hal_plugin_codec_get_pp_bmt_t get_pp_bmt;
        memset(&get_pp_bmt,0,sizeof(get_pp_bmt));
        err = str_parms_get_int(query, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_BMT_FTYPE,
                (int*)&filter_type);
        if ((err < 0)) {
            ALOGE("%s: Invalid or missing filter type param for GET_PP_BMT", __func__);
            get_pp_bmt.filter_type = AUDIO_HAL_PLUGIN_CODEC_PP_FILTER_TYPE_INVALID;
        } else {
            str_parms_del(query, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_BMT_FTYPE);

            if ((filter_type <= AUDIO_HAL_PLUGIN_CODEC_PP_FILTER_TYPE_INVALID) &&
                    (filter_type >= AUDIO_HAL_PLUGIN_CODEC_PP_FILTER_TYPE_MAX)) {
                ALOGE("%s: Invalid filter type value for SET_PP_BMT", __func__);
                get_pp_bmt.filter_type = AUDIO_HAL_PLUGIN_CODEC_PP_FILTER_TYPE_INVALID;
            } else {
                get_pp_bmt.filter_type = filter_type;
            }
        }

        audio_hal_plugin_msg_type_t msg =
                AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_BMT;
        get_pp_bmt.usecase = use_case;
        get_pp_bmt.snd_dev = snd_dev;

        ALOGD("%s: sending get codec pp bmt msg to HAL plugin driver, %d, %d, %d",
                __func__, (int)get_pp_bmt.usecase, (int)get_pp_bmt.snd_dev,
                (int)get_pp_bmt.filter_type);

        ret = my_plugin->audio_hal_plugin_send_msg(msg, &get_pp_bmt, sizeof(get_pp_bmt));
        if (ret) {
            ALOGE("%s: Failed to get plugin pp bmt err: %d", __func__, ret);
            goto done_get_param;
        }

        rbuf_dlen = sizeof(get_pp_bmt.ret_value)/sizeof(int32_t);
        rbuf_dptr = calloc(rbuf_dlen, sizeof(uint32_t));
        if(rbuf_dptr == NULL) {
            ALOGE("[%s] Memory allocation failed for dword length %d",__func__,rbuf_dlen);
            ret = -ENOMEM;
            goto done_get_param;
        }
        memcpy(rbuf_dptr, &get_pp_bmt.ret_value, sizeof(get_pp_bmt.ret_value));
        break;
    }
    case AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_EQ:
    {
        uint32_t rbuf_len = 0;
        char *tmp_ptr = NULL;
        audio_hal_plugin_codec_get_pp_eq_t get_pp_eq;
        memset(&get_pp_eq,0,sizeof(get_pp_eq));

        audio_hal_plugin_msg_type_t msg = AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_EQ;
        get_pp_eq.usecase = use_case;
        get_pp_eq.snd_dev = snd_dev;

        ALOGD("%s: sending get codec pp eq msg to HAL plugin driver, %d, %d",
                __func__, (int)get_pp_eq.usecase, (int)get_pp_eq.snd_dev);

        ret = my_plugin->audio_hal_plugin_send_msg(msg, &get_pp_eq, sizeof(get_pp_eq));
        if (ret) {
            ALOGE("%s: Failed to get plugin pp eq err: %d", __func__, ret);
            goto done_get_param;
        }

        rbuf_len = sizeof(get_pp_eq.ret_preset_id) + sizeof(get_pp_eq.ret_num_bands);
        rbuf_dlen = rbuf_len / sizeof(uint32_t);
        rbuf_dptr = calloc(rbuf_dlen, sizeof(uint32_t));
        if(rbuf_dptr == NULL) {
            ALOGE("[%s] Memory allocation failed for dword length %d",__func__,rbuf_dlen);
            ret = -ENOMEM;
            goto done_get_param;
        }
        tmp_ptr = (char*)rbuf_dptr;
        memcpy(tmp_ptr, &get_pp_eq.ret_preset_id, sizeof(get_pp_eq.ret_preset_id));
        tmp_ptr += sizeof(get_pp_eq.ret_preset_id);
        memcpy(tmp_ptr, &get_pp_eq.ret_num_bands, sizeof(get_pp_eq.ret_num_bands));
        break;
    }
    case AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_EQ_SUBBANDS:
    {
        uint32_t rbuf_len = 0;
        audio_hal_plugin_codec_get_pp_eq_subbands_t get_pp_eq_subbands;
        memset(&get_pp_eq_subbands,0,sizeof(get_pp_eq_subbands));
        err = str_parms_get_int(query, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_EQ_NUM_BANDS,
                (int*)&get_pp_eq_subbands.num_bands);
        if ((err < 0)) {
            ALOGE("%s: Invalid or missing num bands param for GET_PP_EQ_SUBBANDS",
                    __func__);
            ret = -EINVAL;
            goto done_get_param;
        } else {
            str_parms_del(query, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_EQ_NUM_BANDS);

            if(get_pp_eq_subbands.num_bands == 0) {
                ALOGE("%s: Zero num bands param for GET_PP_EQ_SUBBANDS",
                        __func__);
                ret = -EINVAL;
                goto done_get_param;
            }
        }
        rbuf_len = get_pp_eq_subbands.num_bands *
                sizeof(audio_hal_plugin_pp_eq_subband_binfo_t);

        audio_hal_plugin_msg_type_t msg = AUDIO_HAL_PLUGIN_MSG_CODEC_GET_PP_EQ_SUBBANDS;
        get_pp_eq_subbands.usecase = use_case;
        get_pp_eq_subbands.snd_dev = snd_dev;
        get_pp_eq_subbands.ret_bands = calloc(rbuf_len, 1);
        if(get_pp_eq_subbands.ret_bands == NULL) {
            ret = -ENOMEM;
            ALOGE("[%s] failed to allocate memory",__func__);
            goto done_get_param;
        }

        ALOGD("%s: sending get codec pp eq subbands msg to plugin driver, %d, %d, %d",
                __func__, (int)get_pp_eq_subbands.usecase,
                (int)get_pp_eq_subbands.snd_dev, (int)get_pp_eq_subbands.num_bands);

        ret = my_plugin->audio_hal_plugin_send_msg(msg, &get_pp_eq_subbands,
                sizeof(get_pp_eq_subbands));
        if (ret) {
            ALOGE("%s: Failed to get plugin pp eq subbands err: %d", __func__, ret);
            goto done_get_eq_subbands;
        }

        rbuf_dlen = rbuf_len / sizeof(uint32_t);
        rbuf_dptr = calloc(rbuf_dlen, sizeof(uint32_t));
        if(rbuf_dptr == NULL) {
            ALOGE("[%s] Memory allocation failed for dword length %d",__func__,rbuf_dlen);
            ret = -ENOMEM;
            goto done_get_eq_subbands;
        }
        memcpy(rbuf_dptr, get_pp_eq_subbands.ret_bands, rbuf_len);

done_get_eq_subbands:
        if(get_pp_eq_subbands.ret_bands != NULL)
            free(get_pp_eq_subbands.ret_bands);
        break;
    }
    case AUDIO_HAL_PLUGIN_MSG_CODEC_TUNNEL_GET_CMD:
    {
        char *tmp_ptr = NULL;
        audio_hal_plugin_codec_tunnel_get_t tunnel_get;
        memset(&tunnel_get,0,sizeof(tunnel_get));

        err = str_parms_get_int(query, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_TUNNEL_SIZE,
                (int*)&tunnel_get.param_size);
        if ((err < 0) || (!tunnel_get.param_size)) {
            ALOGE("%s: Invalid or missing size param for TUNNEL GET command", __func__);
            ret = -EINVAL;
            goto done_get_param;
        }
        str_parms_del(query, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_TUNNEL_SIZE);

        err = str_parms_get_str(query,
                AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_TUNNEL_DATA, value, len);
        if (err < 0) {
            ALOGE("%s: Invalid or missing data param for TUNNEL GET command", __func__);
            ret = -EINVAL;
            goto done_get_param;
        }
        str_parms_del(query, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_TUNNEL_DATA);

        err = str_parms_get_int(query, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_TUNNEL_GET_SIZE,
                (int*)&tunnel_get.size_to_get);
        if (err < 0 || (!tunnel_get.size_to_get)) {
            ALOGE("%s: Invalid or missing size_to_get param for TUNNEL GET command",
                    __func__);
            ret = -EINVAL;
            goto done_get_param;
        }
        str_parms_del(query, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_TUNNEL_GET_SIZE);

        ret = ext_hw_plugin_string_to_dword(value, (void**)&tunnel_get.param_data,
            tunnel_get.param_size);
        if (ret) {
            ALOGE("%s: Failed to parse payload for TUNNEL GET command", __func__);
            ret = -EINVAL;
            goto done_tunnel_get;
        }

        audio_hal_plugin_msg_type_t msg =
                AUDIO_HAL_PLUGIN_MSG_CODEC_TUNNEL_GET_CMD;
        tunnel_get.ret_data = calloc(tunnel_get.size_to_get, sizeof(int32_t));
        if(tunnel_get.ret_data == NULL) {
            ret = -ENOMEM;
            ALOGE("[%s] failed to allocate memory",__func__);
            goto done_tunnel_get;
        }

        ALOGD("%s: sending tunnel get cmd to plugin driver,size = %d, size_to_get = %d",
                __func__, (int)tunnel_get.param_size, (int)tunnel_get.size_to_get);

        ret = my_plugin->audio_hal_plugin_send_msg(msg, (void*)&tunnel_get,
                sizeof(tunnel_get));
        if (ret) {
            ALOGE("%s: Failed to send plugin tunnel get cmd err: %d", __func__, ret);
            goto done_tunnel_get;
        }
        if ((tunnel_get.ret_size == 0) ||
                (tunnel_get.ret_size > tunnel_get.size_to_get)) {
            ret = -EINVAL;
            ALOGE("[%s] Invalid tunnel get cmd return size: %d",
                    __func__, tunnel_get.ret_size);
            goto done_tunnel_get;
        }

        rbuf_dlen = tunnel_get.ret_size + 1;
        rbuf_dptr = calloc(rbuf_dlen, sizeof(uint32_t));
        if(rbuf_dptr == NULL) {
            ALOGE("[%s] Memory allocation failed for dword length %d",__func__,rbuf_dlen);
            ret = -ENOMEM;
            goto done_tunnel_get;
        }
        tmp_ptr = (char*)rbuf_dptr;
        memcpy(tmp_ptr, &tunnel_get.ret_size, sizeof(uint32_t));
        tmp_ptr += sizeof(uint32_t);
        memcpy(tmp_ptr, tunnel_get.ret_data, 4*tunnel_get.ret_size);

done_tunnel_get:
        if (tunnel_get.param_data!= NULL)
            free(tunnel_get.param_data);
        if (tunnel_get.ret_data!= NULL)
            free(tunnel_get.ret_data);
        break;
    }
    default:
        ALOGE("%s: Invalid plugin message type: %d", __func__, val);
        ret = -EINVAL;
    }

    if(ret == 0) {
        ret = ext_hw_plugin_dword_to_string(rbuf_dptr, rbuf_dlen, &rparms);
        if (ret < 0) {
            ALOGE("%s: Failed to convert param info for MSG_TYPE %d", __func__, val);
            goto done_get_param;
        }
        ret = 0;
        str_parms_add_int(reply, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_GETPARAM_RESULT, ret);
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_GETPARAM_DATA, rparms);
    }

done_get_param:
    if(ret) {
        str_parms_add_int(reply, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_GETPARAM_RESULT, ret);
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_EXT_HW_PLUGIN_GETPARAM_DATA, "");
    }
    if(rbuf_dptr!= NULL)
        free(rbuf_dptr);
    if(rparms!= NULL)
        free(rparms);

    ALOGI("%s: exit with code(%d)", __func__, ret);
    if(kv_pairs != NULL)
        free(kv_pairs);
    if(value != NULL)
        free(value);
    return ret;
}

int ext_hw_plugin_set_mic_mute(void *plugin, bool mute)
{
    struct ext_hw_plugin_data *my_plugin = NULL;
    audio_hal_plugin_codec_set_pp_mute_t pp_mute;
    int ret = 0;

    ALOGD("%s: received set mic mute (%d)", __func__, mute);

    if (plugin == NULL) {
        ALOGE("[%s] received null pointer",__func__);
        return -EINVAL;
    }

    my_plugin = (struct ext_hw_plugin_data *)plugin;
    if (!my_plugin->audio_hal_plugin_send_msg) {
        ALOGE("%s: NULL audio_hal_plugin_send_msg func ptr", __func__);
        return -EINVAL;
    }

    my_plugin->mic_mute = mute;

    /* Set mic mute is currently supported only for HFP call use case. */
    if (my_plugin->usecase_ref_count[AUDIO_HAL_PLUGIN_USECASE_HFP_VOICE_CALL]) {
        pp_mute.snd_dev= my_plugin->in_snd_dev[AUDIO_HAL_PLUGIN_USECASE_HFP_VOICE_CALL];
        pp_mute.usecase = AUDIO_HAL_PLUGIN_USECASE_HFP_VOICE_CALL;
        pp_mute.ch_mask = AUDIO_CHANNEL_IN_ALL;
        pp_mute.flag = mute;

        ALOGV("%s: sending codec pp mute msg to HAL plugin driver, %d, %d, %x, %d",
                __func__, (int)pp_mute.usecase, (int)pp_mute.snd_dev,
                (int)pp_mute.ch_mask, (int)pp_mute.flag);
        ret = my_plugin->audio_hal_plugin_send_msg(
                AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_MUTE, &pp_mute,
                sizeof(pp_mute));
        if (ret) {
            ALOGE("%s: Failed to set plugin pp mute err: %d", __func__, ret);
        }
    }

    return ret;
}

int ext_hw_plugin_get_mic_mute(void *plugin, bool *mute)
{
    struct ext_hw_plugin_data *my_plugin = (struct ext_hw_plugin_data *)plugin;

    if (my_plugin == NULL || mute == NULL) {
        ALOGE("[%s] received null pointer", __func__);
        return -EINVAL;
    }

    *mute = my_plugin->mic_mute;
    ALOGD("%s: received get mic mute (%d)", __func__, *mute);

    return 0;
}

int ext_hw_plugin_set_audio_gain(void *plugin,
            struct audio_usecase *usecase, uint32_t gain)
{
    int32_t ret = 0;
    struct ext_hw_plugin_data *my_plugin = (struct ext_hw_plugin_data *)plugin;
    audio_hal_plugin_codec_set_pp_vol_t pp_vol;

    if ((my_plugin == NULL) || (usecase == NULL)) {
        ALOGE("%s: NULL input pointer", __func__);
        return -EINVAL;
    }

    if (!my_plugin->audio_hal_plugin_send_msg) {
        ALOGE("%s: NULL audio_hal_plugin_send_msg func ptr", __func__);
        return -EINVAL;
    }

    memset(&pp_vol, 0, sizeof(pp_vol));

    ret = ext_hw_plugin_check_plugin_usecase(usecase->id, &pp_vol.usecase);
    if (ret) {
        ALOGI("%s: Set audio gain skipped for audio usecase %d",
                __func__, usecase->id);
        return 0;
    }

#if 0
    /* Skip active usecase check and continue vol set to plugin
     * to allow volume cached per usecase in plugin.
     */
    if (!my_plugin->usecase_ref_count[pp_vol.usecase]) {
        ALOGV("%s: Plugin usecase %d is not enabled",
                __func__, pp_vol.usecase);
        return 0;
    }

    if (my_plugin->out_snd_dev[pp_vol.usecase]) {
        pp_vol.snd_dev = my_plugin->out_snd_dev[pp_vol.usecase];
        pp_vol.ch_mask = AUDIO_CHANNEL_OUT_ALL;
    } else if (my_plugin->in_snd_dev[pp_vol.usecase]) {
        pp_vol.snd_dev = my_plugin->in_snd_dev[pp_vol.usecase];
        pp_vol.ch_mask = AUDIO_CHANNEL_IN_ALL;
    } else {
        ALOGE("%s: No valid snd_device found for usecase %d",
                __func__, pp_vol.usecase);
        return -EINVAL;
    }
#endif

    /* NOTE: Use in/out snd device from usecase to decide
     *       which direction pp_volume should apply.
     */
    if (usecase->out_snd_device != SND_DEVICE_NONE) {
        pp_vol.snd_dev = usecase->out_snd_device;
        pp_vol.ch_mask = AUDIO_CHANNEL_OUT_ALL;
    } else if (usecase->in_snd_device != SND_DEVICE_NONE) {
        pp_vol.snd_dev = usecase->in_snd_device;
        pp_vol.ch_mask = AUDIO_CHANNEL_IN_ALL;
    } else {
        ALOGE("%s: No valid snd_device found for usecase %d",
                __func__, pp_vol.usecase);
        return -EINVAL;
    }

    pp_vol.gain = gain;

    ALOGD("%s: Sending codec pp vol msg to HAL plugin driver, %d, %d, %d, %d",
            __func__, (int)pp_vol.usecase, (int)pp_vol.snd_dev,
            (int)pp_vol.ch_mask, (int)pp_vol.gain);
    ret = my_plugin->audio_hal_plugin_send_msg(
            AUDIO_HAL_PLUGIN_MSG_CODEC_SET_PP_VOLUME,
            &pp_vol, sizeof(pp_vol));
    if (ret) {
        ALOGE("%s: Failed to set plugin pp vol err: %d", __func__, ret);
    }
    return ret;
}
