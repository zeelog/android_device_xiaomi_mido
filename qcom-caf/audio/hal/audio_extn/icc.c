/* icc.c
Copyright (c) 2012-2015, 2016, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.*/

#define LOG_TAG "audio_hw_icc"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include <cutils/log.h>

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <stdlib.h>
#include <cutils/str_parms.h>
#include "audio_extn.h"

#define AUDIO_PARAMETER_ICC_ENABLE      "conversation_mode_state"
#define AUDIO_PARAMETER_ICC_SET_SAMPLING_RATE "icc_set_sampling_rate"
#define AUDIO_PARAMETER_KEY_ICC_VOLUME "icc_volume"

#ifdef PLATFORM_AUTO
#define ICC_RX_VOLUME     "Playback 33 Volume"
#else
#define ICC_RX_VOLUME     "NULL"
#endif

static int32_t start_icc(struct audio_device *adev,
                               struct str_parms *parms);

static int32_t stop_icc(struct audio_device *adev);

struct icc_module {
    struct pcm *icc_pcm_rx;
    struct pcm *icc_pcm_tx;
    bool is_icc_running;
    float icc_volume;
    audio_usecase_t ucid;
};

static struct icc_module iccmod = {
    .icc_pcm_rx = NULL,
    .icc_pcm_tx = NULL,
    .icc_volume = 0,
    .is_icc_running = 0,
    .ucid = USECASE_ICC_CALL,
};
static struct pcm_config pcm_config_icc = {
    .channels = 4,
    .rate = 16000,
    .period_size = 240,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

static fp_platform_get_pcm_device_id_t              fp_platform_get_pcm_device_id;
static fp_platform_set_echo_reference_t             fp_platform_set_echo_reference;
static fp_select_devices_t                          fp_select_devices;
static fp_audio_extn_ext_hw_plugin_usecase_start_t  fp_audio_extn_ext_hw_plugin_usecase_start;
static fp_audio_extn_ext_hw_plugin_usecase_stop_t   fp_audio_extn_ext_hw_plugin_usecase_stop;
static fp_get_usecase_from_list_t                   fp_get_usecase_from_list;
static fp_disable_audio_route_t                     fp_disable_audio_route;
static fp_disable_snd_device_t                      fp_disable_snd_device;

static int32_t icc_set_volume(struct audio_device *adev, float value)
{
    int32_t ret = 0, vol = 0;
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name = ICC_RX_VOLUME;

    ALOGD("%s: enter", __func__);
    ALOGD("%s: (%f)", __func__, value);

    iccmod.icc_volume = value;
    if (value < 0.0) {
        ALOGW("%s: (%f) Under 0.0, assuming 0.0", __func__, value);
        value = 0.0;
    } else {
        value = ((value > 15.000000) ? 1.0 : (value / 15));
        ALOGW("%s: Volume brought with in range (%f)", __func__, value);
    }
    vol = lrint((value * 0x2000) + 0.5);

    if(!iccmod.is_icc_running) {
        ALOGV("%s: ICC not active, ignoring icc_set_volume call", __func__);
        return -EIO;
    }

    ALOGD("%s: Setting ICC Volume to %d", __func__, vol);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if(!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
             __func__, mixer_ctl_name);
        return -EINVAL;
    }
    if(mixer_ctl_set_value(ctl, 0, vol) < 0) {
        ALOGE("%s: Couldn't set ICC Volume [%d]", __func__, vol);
        return -EINVAL;
    }

    ALOGD("%s: exit: status(%d)", __func__, ret);
    return ret;
}

static int32_t start_icc(struct audio_device *adev,
                         struct str_parms *parms __unused)
{
    int32_t ret = 0;
    struct audio_usecase *uc_info;
    int32_t pcm_dev_rx_id, pcm_dev_tx_id;

    ALOGD("%s: enter", __func__);

    uc_info = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));

    if (!uc_info)
        return -ENOMEM;

    uc_info->id = iccmod.ucid;
    uc_info->type = ICC_CALL;
    uc_info->stream.out = adev->primary_output;
    list_init(&uc_info->device_list);
    assign_devices(&uc_info->device_list, &adev->primary_output->device_list);
    uc_info->in_snd_device = SND_DEVICE_NONE;
    uc_info->out_snd_device = SND_DEVICE_NONE;

    list_add_tail(&adev->usecase_list, &uc_info->list);

    fp_select_devices(adev, iccmod.ucid);

    if ((uc_info->out_snd_device != SND_DEVICE_NONE) ||
        (uc_info->in_snd_device != SND_DEVICE_NONE)) {
        if (fp_audio_extn_ext_hw_plugin_usecase_start(adev->ext_hw_plugin, uc_info))
            ALOGE("%s: failed to start ext hw plugin", __func__);
    }

    pcm_dev_rx_id = fp_platform_get_pcm_device_id(uc_info->id, PCM_PLAYBACK);
    pcm_dev_tx_id = fp_platform_get_pcm_device_id(uc_info->id, PCM_CAPTURE);
    if (pcm_dev_rx_id < 0 || pcm_dev_tx_id < 0 ) {
        ALOGE("%s: Invalid PCM devices (rx: %d tx: %d) for the usecase(%d)",
              __func__, pcm_dev_rx_id, pcm_dev_tx_id, uc_info->id);
        ret = -EIO;
        goto exit;
    }

    ALOGV("%s: ICC PCM devices (ICC pcm rx: %d pcm tx: %d) for the usecase(%d)",
              __func__, pcm_dev_rx_id, pcm_dev_tx_id, uc_info->id);

    iccmod.icc_pcm_rx = pcm_open(adev->snd_card,
                                 pcm_dev_rx_id,
                                 PCM_OUT, &pcm_config_icc);
    if (iccmod.icc_pcm_rx && !pcm_is_ready(iccmod.icc_pcm_rx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(iccmod.icc_pcm_rx));
        ret = -EIO;
        goto exit;
    }

    iccmod.icc_pcm_tx = pcm_open(adev->snd_card,
                                 pcm_dev_tx_id,
                                 PCM_IN, &pcm_config_icc);
    if (iccmod.icc_pcm_tx && !pcm_is_ready(iccmod.icc_pcm_tx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(iccmod.icc_pcm_tx));
        ret = -EIO;
        goto exit;
    }

    if (pcm_start(iccmod.icc_pcm_rx) < 0) {
        ALOGE("%s: pcm start for icc pcm rx failed", __func__);
        ret = -EINVAL;
        goto exit;
    }
    if (pcm_start(iccmod.icc_pcm_tx) < 0) {
        ALOGE("%s: pcm start for icc pcm tx failed", __func__);
        ret = -EINVAL;
        goto exit;
    }

    iccmod.is_icc_running = true;
    icc_set_volume(adev, iccmod.icc_volume);

    ALOGD("%s: exit: status(%d)", __func__, ret);
    return 0;

exit:
    stop_icc(adev);
    ALOGE("%s: Problem in ICC start: status(%d)", __func__, ret);
    return ret;
}

static int32_t stop_icc(struct audio_device *adev)
{
    int32_t ret = 0;
    struct audio_usecase *uc_info;

    ALOGD("%s: enter", __func__);
    iccmod.is_icc_running = false;

    /* 1. Close the PCM devices */

    if (iccmod.icc_pcm_rx) {
        pcm_close(iccmod.icc_pcm_rx);
        iccmod.icc_pcm_rx = NULL;
    }
    if (iccmod.icc_pcm_tx) {
        pcm_close(iccmod.icc_pcm_tx);
        iccmod.icc_pcm_tx = NULL;
    }

    uc_info = fp_get_usecase_from_list(adev, iccmod.ucid);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
              __func__, iccmod.ucid);
        return -EINVAL;
    }

    if ((uc_info->out_snd_device != SND_DEVICE_NONE) ||
        (uc_info->in_snd_device != SND_DEVICE_NONE)) {
        if (fp_audio_extn_ext_hw_plugin_usecase_stop(adev->ext_hw_plugin, uc_info))
            ALOGE("%s: failed to stop ext hw plugin", __func__);
    }

    /* 2. Disable echo reference while stopping icc */
    fp_platform_set_echo_reference(adev, false, &uc_info->device_list);

    /* 3. Get and set stream specific mixer controls */
    fp_disable_audio_route(adev, uc_info);

    /* 4. Disable the rx and tx devices */
    fp_disable_snd_device(adev, uc_info->out_snd_device);
    fp_disable_snd_device(adev, uc_info->in_snd_device);

    list_remove(&uc_info->list);
    free(uc_info);

    ALOGD("%s: exit: status(%d)", __func__, ret);
    return ret;
}

void icc_init(icc_init_config_t init_config)
{
    fp_platform_get_pcm_device_id = init_config.fp_platform_get_pcm_device_id;
    fp_platform_set_echo_reference = init_config.fp_platform_set_echo_reference;
    fp_select_devices = init_config.fp_select_devices;
    fp_audio_extn_ext_hw_plugin_usecase_start =
                                init_config.fp_audio_extn_ext_hw_plugin_usecase_start;
    fp_audio_extn_ext_hw_plugin_usecase_stop =
                                init_config.fp_audio_extn_ext_hw_plugin_usecase_stop;
    fp_get_usecase_from_list = init_config.fp_get_usecase_from_list;
    fp_disable_audio_route = init_config.fp_disable_audio_route;
    fp_disable_snd_device = init_config.fp_disable_snd_device;
}

bool icc_is_active(struct audio_device *adev)
{
    struct audio_usecase *icc_usecase = NULL;
    icc_usecase = fp_get_usecase_from_list(adev, iccmod.ucid);

    if (icc_usecase != NULL)
        return true;
    else
        return false;
}

audio_usecase_t icc_get_usecase()
{
    return iccmod.ucid;
}

void icc_set_parameters(struct audio_device *adev, struct str_parms *parms)
{
    int ret;
    int rate;
    int val;
    float vol;
    char value[32]={0};

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_ICC_ENABLE, value,
                            sizeof(value));
    if (ret >= 0) {
           if (!strncmp(value,"true",sizeof(value)) && !iccmod.is_icc_running)
               ret = start_icc(adev,parms);
           else if (!strncmp(value, "false", sizeof(value)) && iccmod.is_icc_running)
               stop_icc(adev);
           else
               ALOGE("%s=%s is unsupported", AUDIO_PARAMETER_ICC_ENABLE, value);
    }
    memset(value, 0, sizeof(value));
    ret = str_parms_get_str(parms,AUDIO_PARAMETER_ICC_SET_SAMPLING_RATE, value,
                            sizeof(value));
    if (ret >= 0) {
           rate = atoi(value);
           if (rate == 16000){
               iccmod.ucid = USECASE_ICC_CALL;
               pcm_config_icc.rate = rate;
           } else
               ALOGE("Unsupported rate..");
    }

    if (iccmod.is_icc_running) {
        memset(value, 0, sizeof(value));
        ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING,
                                value, sizeof(value));
        if (ret >= 0) {
            val = atoi(value);
            if (val > 0)
                fp_select_devices(adev, iccmod.ucid);
        }
    }

    memset(value, 0, sizeof(value));
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_ICC_VOLUME,
                            value, sizeof(value));
    if (ret >= 0) {
        if (sscanf(value, "%f", &vol) != 1){
            ALOGE("%s: error in retrieving icc volume", __func__);
            ret = -EIO;
            goto exit;
        }
        ALOGD("%s: icc_set_volume usecase, Vol: [%f]", __func__, vol);
        icc_set_volume(adev, vol);
    }
exit:
    ALOGV("%s Exit",__func__);
}
