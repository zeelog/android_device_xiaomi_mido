/* synth.c
Copyright (c) 2012-2015,2016,2020 The Linux Foundation. All rights reserved.

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

#define LOG_TAG "audio_hw_synth"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include <log/log.h>
#include <unistd.h>

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <stdlib.h>
#include <cutils/str_parms.h>
#include <audio_extn.h>

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_FM
#include <log_utils.h>
#endif

#define AUDIO_PARAMETER_KEY_SYNTH_ENABLE "synth_enable"

static int32_t synth_start(struct audio_device *adev);
static int32_t synth_stop(struct audio_device *adev);

static struct pcm_config pcm_config_synth = {
    .channels = 4,
    .rate = 16000,
    .period_size = 240,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

struct synth_module {
    struct pcm *pcm_rx;
    struct pcm *pcm_tx;
    bool is_synth_running;
    audio_usecase_t ucid;
};

static struct synth_module synthmod = {
  .pcm_rx = NULL,
  .pcm_tx = NULL,
  .is_synth_running = 0,
  .ucid = USECASE_AUDIO_PLAYBACK_SYNTHESIZER,
};

static fp_platform_get_pcm_device_id_t              fp_platform_get_pcm_device_id;
static fp_get_usecase_from_list_t                   fp_get_usecase_from_list;
static fp_select_devices_t                          fp_select_devices;
static fp_platform_get_pcm_device_id_t              fp_platform_get_pcm_device_id;
static fp_platform_send_audio_calibration_t         fp_platform_send_audio_calibration;
static fp_disable_audio_route_t                     fp_disable_audio_route;
static fp_disable_snd_device_t                      fp_disable_snd_device;


int32_t synth_start(struct audio_device *adev)
{
    int32_t ret = 0;
    int pcm_dev_rx = -1, pcm_dev_tx = -1;
    char mixer_path[MIXER_PATH_MAX_LENGTH];
    struct audio_usecase *uc_info = NULL;

    ALOGD("%s: Enable Synth", __func__);

    // select devices
    uc_info = (struct audio_usecase *)calloc(1, sizeof(*uc_info));
    if (!uc_info) {
        ALOGE("%s: allocate memory failed", __func__);
        return -ENOMEM;
    }

    uc_info->id = synthmod.ucid;
    uc_info->type = SYNTH_LOOPBACK;
    uc_info->stream.out = adev->primary_output;
    list_init(&uc_info->device_list);
    assign_devices(&uc_info->device_list, &adev->primary_output->device_list);
    uc_info->in_snd_device = SND_DEVICE_NONE;
    uc_info->out_snd_device = SND_DEVICE_OUT_SPEAKER;

    list_add_tail(&adev->usecase_list, &uc_info->list);

    fp_select_devices(adev, synthmod.ucid);

    // open pcm rx/tx
    pcm_dev_tx = fp_platform_get_pcm_device_id(USECASE_AUDIO_PLAYBACK_SYNTHESIZER, PCM_CAPTURE);
    pcm_dev_rx = fp_platform_get_pcm_device_id(USECASE_AUDIO_PLAYBACK_SYNTHESIZER, PCM_PLAYBACK);

    if (pcm_dev_tx < 0 || pcm_dev_rx < 0 ) {
        ALOGE("%s: Invalid PCM devices (rx: %d tx: %d) for the usecase(%d)",
            __func__, pcm_dev_rx, pcm_dev_tx, uc_info->id);
        ret = -EIO;
        goto exit;
    }

    //open pcm rx/tx
    synthmod.pcm_tx = pcm_open(adev->snd_card,
                                   pcm_dev_tx,
                                   PCM_IN, &pcm_config_synth);
    if (synthmod.pcm_tx &&
        !pcm_is_ready(synthmod.pcm_tx)) {
        ALOGE("%s: pcm_tx %s", __func__,
            pcm_get_error(synthmod.pcm_tx));
        ret = -EIO;
        goto exit;
    }
    synthmod.pcm_rx = pcm_open(adev->snd_card,
                                   pcm_dev_rx,
                                   PCM_OUT, &pcm_config_synth);
    if (synthmod.pcm_rx &&
        !pcm_is_ready(synthmod.pcm_rx)) {
        ALOGE("%s: pcm_rx %s", __func__,
            pcm_get_error(synthmod.pcm_rx));
        ret = -EIO;
        goto exit;
    }

    if (pcm_start(synthmod.pcm_tx) < 0) {
        ALOGE("%s: pcm start for pcm tx failed", __func__);
        ret = -EIO;
        goto exit;
    }
    if (pcm_start(synthmod.pcm_rx) < 0) {
        ALOGE("%s: pcm start for pcm rx failed", __func__);
        ret = -EIO;
        goto exit;
    }

    synthmod.is_synth_running = true;
    return ret;

exit:
    synth_stop(adev);
    ALOGE("%s: Problem in Synth start: status(%d)", __func__, ret);
    return ret;
}

int32_t synth_stop(struct audio_device *adev)
{
    int32_t ret = 0;
    struct audio_usecase *uc_info;

    ALOGD("Enter %s:", __func__);
    synthmod.is_synth_running = false;

    if (synthmod.pcm_tx) {
        pcm_close(synthmod.pcm_tx);
        synthmod.pcm_tx = NULL;
    }

    if (synthmod.pcm_rx) {
        pcm_close(synthmod.pcm_rx);
        synthmod.pcm_rx = NULL;
    }

    uc_info = fp_get_usecase_from_list(adev, synthmod.ucid);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
            __func__, synthmod.ucid);
        return -EINVAL;
    }

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

bool synth_is_active(struct audio_device *adev) {
    struct audio_usecase *synth_usecase = NULL;
    synth_usecase = fp_get_usecase_from_list(adev, synthmod.ucid);
    if (synth_usecase != NULL)
        return true;
    else
        return false;
}

void synth_set_parameters(struct audio_device *adev,
                                  struct str_parms *parms)
{
    int ret, val;
    char value[32]={0};

    ALOGD("%s: enter", __func__);
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_SYNTH_ENABLE, value, sizeof(value));
    if (ret >= 0) {
        if (!strncmp(value,"true",sizeof(value)) && !synthmod.is_synth_running) {
            synth_start(adev);
        }
        else if (!strncmp(value,"false",sizeof(value)) && synthmod.is_synth_running) {
            synth_stop(adev);
        } else {
            ALOGE("Not support key value");
        }
    }

    ALOGD("%s: exit", __func__);
}

void synth_init(synth_init_config_t init_config)
{
    fp_platform_get_pcm_device_id = init_config.fp_platform_get_pcm_device_id;
    fp_get_usecase_from_list = init_config.fp_get_usecase_from_list;
    fp_select_devices = init_config.fp_select_devices;
    fp_disable_audio_route = init_config.fp_disable_audio_route;
    fp_disable_snd_device = init_config.fp_disable_snd_device;
}

