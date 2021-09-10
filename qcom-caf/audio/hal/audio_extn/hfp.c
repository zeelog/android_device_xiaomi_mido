/* Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.

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

#define LOG_TAG "audio_hw_hfp"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <math.h>
#include <log/log.h>

#include "audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <stdlib.h>
#include <cutils/str_parms.h>
#include "audio_extn.h"

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_HFP
#include <log_utils.h>
#endif

#define AUDIO_PARAMETER_HFP_ENABLE      "hfp_enable"
#define AUDIO_PARAMETER_HFP_SET_SAMPLING_RATE "hfp_set_sampling_rate"
#define AUDIO_PARAMETER_KEY_HFP_VOLUME "hfp_volume"
#define AUDIO_PARAMETER_HFP_PCM_DEV_ID "hfp_pcm_dev_id"

#define AUDIO_PARAMETER_KEY_HFP_MIC_VOLUME "hfp_mic_volume"
#define PLAYBACK_VOLUME_MAX 0x2000
#define CAPTURE_VOLUME_DEFAULT                (15.0)

#ifdef PLATFORM_MSM8994
#define HFP_RX_VOLUME     "SEC AUXPCM LOOPBACK Volume"
#elif defined (PLATFORM_MSM8996) || defined (EXTERNAL_BT_SUPPORTED)
#define HFP_RX_VOLUME     "PRI AUXPCM LOOPBACK Volume"
#elif defined PLATFORM_AUTO
#define HFP_RX_VOLUME     "Playback 36 Volume"
#elif defined (PLATFORM_MSM8998) || defined (PLATFORM_MSMFALCON) || \
      defined (PLATFORM_SDM845) || defined (PLATFORM_SDM710) || \
      defined (PLATFORM_QCS605) || defined (PLATFORM_MSMNILE) || \
      defined (PLATFORM_KONA) || defined (PLATFORM_MSMSTEPPE) || \
      defined (PLATFORM_QCS405) || defined (PLATFORM_TRINKET) || \
      defined (PLATFORM_LITO) || defined(PLATFORM_ATOLL) || \
      defined (PLATFORM_BENGAL)
#define HFP_RX_VOLUME     "SLIMBUS_7 LOOPBACK Volume"
#else
#define HFP_RX_VOLUME     "Internal HFP RX Volume"
#endif

static int32_t start_hfp(struct audio_device *adev,
                               struct str_parms *parms);

static int32_t stop_hfp(struct audio_device *adev);

struct hfp_module {
    struct pcm *hfp_sco_rx;
    struct pcm *hfp_sco_tx;
    struct pcm *hfp_pcm_rx;
    struct pcm *hfp_pcm_tx;
    bool is_hfp_running;
    float hfp_volume;
    int32_t hfp_pcm_dev_id;
    audio_usecase_t ucid;
    float mic_volume;
    bool mic_mute;
};

static struct hfp_module hfpmod = {
    .hfp_sco_rx = NULL,
    .hfp_sco_tx = NULL,
    .hfp_pcm_rx = NULL,
    .hfp_pcm_tx = NULL,
    .is_hfp_running = 0,
    .hfp_volume = 0,
    .hfp_pcm_dev_id = HFP_ASM_RX_TX,
    .ucid = USECASE_AUDIO_HFP_SCO,
    .mic_volume = CAPTURE_VOLUME_DEFAULT,
    .mic_mute = 0,
};

static struct pcm_config pcm_config_hfp = {
    .channels = 1,
    .rate = 8000,
    .period_size = 240,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

//external feature dependency
static fp_platform_set_mic_mute_t                   fp_platform_set_mic_mute;
static fp_platform_get_pcm_device_id_t              fp_platform_get_pcm_device_id;
static fp_platform_set_echo_reference_t             fp_platform_set_echo_reference;
static fp_select_devices_t                          fp_select_devices;
static fp_audio_extn_ext_hw_plugin_usecase_start_t  fp_audio_extn_ext_hw_plugin_usecase_start;
static fp_audio_extn_ext_hw_plugin_usecase_stop_t   fp_audio_extn_ext_hw_plugin_usecase_stop;
static fp_get_usecase_from_list_t                   fp_get_usecase_from_list;
static fp_disable_audio_route_t                     fp_disable_audio_route;
static fp_disable_snd_device_t                      fp_disable_snd_device;
static fp_voice_get_mic_mute_t                      fp_voice_get_mic_mute;
static fp_audio_extn_auto_hal_start_hfp_downlink_t  fp_audio_extn_auto_hal_start_hfp_downlink;
static fp_audio_extn_auto_hal_stop_hfp_downlink_t   fp_audio_extn_auto_hal_stop_hfp_downlink;

static int32_t hfp_set_volume(struct audio_device *adev, float value)
{
    int32_t vol, ret = 0;
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name = HFP_RX_VOLUME;

    ALOGV("%s: entry", __func__);
    ALOGD("%s: (%f)\n", __func__, value);

    hfpmod.hfp_volume = value;

    if (value < 0.0) {
        ALOGW("%s: (%f) Under 0.0, assuming 0.0\n", __func__, value);
        value = 0.0;
    } else {
        value = ((value > 15.000000) ? 1.0 : (value / 15));
        ALOGW("%s: Volume brought with in range (%f)\n", __func__, value);
    }
    vol  = lrint((value * 0x2000) + 0.5);

    if (!hfpmod.is_hfp_running) {
        ALOGV("%s: HFP not active, ignoring set_hfp_volume call", __func__);
        return -EIO;
    }

    ALOGD("%s: Setting HFP volume to %d \n", __func__, vol);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }
    if(mixer_ctl_set_value(ctl, 0, vol) < 0) {
        ALOGE("%s: Couldn't set HFP Volume: [%d]", __func__, vol);
        return -EINVAL;
    }

    ALOGV("%s: exit", __func__);
    return ret;
}

/*Set mic volume to value.
*
* This interface is used for mic volume control, set mic volume as value(range 0 ~ 15).
*/
static int hfp_set_mic_volume(struct audio_device *adev, float value)
{
    int volume, ret = 0;
    char mixer_ctl_name[128];
    struct mixer_ctl *ctl;
    int pcm_device_id = HFP_ASM_RX_TX;

    ALOGD("%s: enter, value=%f", __func__, value);

    if (!hfpmod.is_hfp_running) {
        ALOGE("%s: HFP not active, ignoring set_hfp_mic_volume call", __func__);
        return -EIO;
    }

    if (value < 0.0) {
        ALOGW("%s: (%f) Under 0.0, assuming 0.0\n", __func__, value);
        value = 0.0;
    } else if (value > CAPTURE_VOLUME_DEFAULT) {
        value = CAPTURE_VOLUME_DEFAULT;
        ALOGW("%s: Volume brought within range (%f)\n", __func__, value);
    }

    value = value / CAPTURE_VOLUME_DEFAULT;
    memset(mixer_ctl_name, 0, sizeof(mixer_ctl_name));
    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
             "Playback %d Volume", pcm_device_id);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }
    volume = (int)(value * PLAYBACK_VOLUME_MAX);

    ALOGD("%s: Setting volume to %d (%s)\n", __func__, volume, mixer_ctl_name);
    if (mixer_ctl_set_value(ctl, 0, volume) < 0) {
        ALOGE("%s: Couldn't set HFP Volume: [%d]", __func__, volume);
        return -EINVAL;
    }

    return ret;
}

static float hfp_get_mic_volume(struct audio_device *adev)
{
    int volume;
    char mixer_ctl_name[128];
    struct mixer_ctl *ctl;
    int pcm_device_id = HFP_ASM_RX_TX;
    float value = 0.0;

    ALOGD("%s: enter", __func__);

    if (!hfpmod.is_hfp_running) {
        ALOGE("%s: HFP not active, ignoring set_hfp_mic_volume call", __func__);
        return -EIO;
    }

    memset(mixer_ctl_name, 0, sizeof(mixer_ctl_name));
    snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
             "Playback %d Volume", pcm_device_id);
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
              __func__, mixer_ctl_name);
        return -EINVAL;
    }

    volume = mixer_ctl_get_value(ctl, 0);
    if ( volume < 0) {
        ALOGE("%s: Couldn't set HFP Volume: [%d]", __func__, volume);
        return -EINVAL;
    }
    ALOGD("%s: getting mic volume %d \n", __func__, volume);

    value = (volume / PLAYBACK_VOLUME_MAX) * CAPTURE_VOLUME_DEFAULT;
    if (value < 0.0) {
        ALOGW("%s: (%f) Under 0.0, assuming 0.0\n", __func__, value);
        value = 0.0;
    } else if (value > CAPTURE_VOLUME_DEFAULT) {
        value = CAPTURE_VOLUME_DEFAULT;
        ALOGW("%s: Volume brought within range (%f)\n", __func__, value);
    }

    return value;
}

/*Set mic mute state.
*
* This interface is used for mic mute state control
*/
int hfp_set_mic_mute(struct audio_device *adev, bool state)
{
    int rc = 0;

    if (state == hfpmod.mic_mute)
        return rc;

    if (state == true) {
        hfpmod.mic_volume = hfp_get_mic_volume(adev);
    }
    rc = hfp_set_mic_volume(adev, (state == true) ? 0.0 : hfpmod.mic_volume);
    adev->voice.mic_mute = state;
    hfpmod.mic_mute = state;
    ALOGD("%s: Setting mute state %d, rc %d\n", __func__, state, rc);
    return rc;
}

static int32_t start_hfp(struct audio_device *adev,
                         struct str_parms *parms __unused)
{
    int32_t ret = 0;
    struct audio_usecase *uc_info;
    int32_t pcm_dev_rx_id, pcm_dev_tx_id, pcm_dev_asm_rx_id, pcm_dev_asm_tx_id;

    ALOGD("%s: enter", __func__);

    if (adev->enable_hfp == true) {
        ALOGD("%s: HFP is already active!\n", __func__);
        return 0;
    }
    adev->enable_hfp = true;
    fp_platform_set_mic_mute(adev->platform, false);

    uc_info = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));

    if (!uc_info)
        return -ENOMEM;

    uc_info->id = hfpmod.ucid;
    uc_info->type = PCM_HFP_CALL;
    uc_info->stream.out = adev->primary_output;
    list_init(&uc_info->device_list);
    assign_devices(&uc_info->device_list, &adev->primary_output->device_list);
    uc_info->in_snd_device = SND_DEVICE_NONE;
    uc_info->out_snd_device = SND_DEVICE_NONE;

    list_add_tail(&adev->usecase_list, &uc_info->list);

    fp_select_devices(adev, hfpmod.ucid);

    if ((uc_info->out_snd_device != SND_DEVICE_NONE) ||
        (uc_info->in_snd_device != SND_DEVICE_NONE)) {
        if (fp_audio_extn_ext_hw_plugin_usecase_start(adev->ext_hw_plugin, uc_info))
            ALOGE("%s: failed to start ext hw plugin", __func__);
    }

    pcm_dev_rx_id = fp_platform_get_pcm_device_id(uc_info->id, PCM_PLAYBACK);
    pcm_dev_tx_id = fp_platform_get_pcm_device_id(uc_info->id, PCM_CAPTURE);
    pcm_dev_asm_rx_id = hfpmod.hfp_pcm_dev_id;
    pcm_dev_asm_tx_id = hfpmod.hfp_pcm_dev_id;
    if (pcm_dev_rx_id < 0 || pcm_dev_tx_id < 0 ||
        pcm_dev_asm_rx_id < 0 || pcm_dev_asm_tx_id < 0 ) {
        ALOGE("%s: Invalid PCM devices (rx: %d tx: %d asm: rx tx %d) for the usecase(%d)",
              __func__, pcm_dev_rx_id, pcm_dev_tx_id, pcm_dev_asm_rx_id, uc_info->id);
        ret = -EIO;
        goto exit;
    }

    ALOGD("%s: HFP PCM devices (rx: %d tx: %d pcm dev id: %d) usecase(%d)",
              __func__, pcm_dev_rx_id, pcm_dev_tx_id, hfpmod.hfp_pcm_dev_id, uc_info->id);

    hfpmod.hfp_pcm_rx = pcm_open(adev->snd_card,
                                 pcm_dev_rx_id,
                                 PCM_OUT, &pcm_config_hfp);
    if (hfpmod.hfp_pcm_rx && !pcm_is_ready(hfpmod.hfp_pcm_rx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(hfpmod.hfp_pcm_rx));
        ret = -EIO;
        goto exit;
    }

    hfpmod.hfp_pcm_tx = pcm_open(adev->snd_card,
                                 pcm_dev_tx_id,
                                 PCM_IN, &pcm_config_hfp);
    if (hfpmod.hfp_pcm_tx && !pcm_is_ready(hfpmod.hfp_pcm_tx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(hfpmod.hfp_pcm_tx));
        ret = -EIO;
        goto exit;
    }

    if (pcm_start(hfpmod.hfp_pcm_rx) < 0) {
        ALOGE("%s: pcm start for hfp pcm rx failed", __func__);
        ret = -EINVAL;
        goto exit;
    }
    if (pcm_start(hfpmod.hfp_pcm_tx) < 0) {
        ALOGE("%s: pcm start for hfp pcm tx failed", __func__);
        ret = -EINVAL;
        goto exit;
    }

    if (fp_audio_extn_auto_hal_start_hfp_downlink(adev, uc_info))
        ALOGE("%s: start hfp downlink failed", __func__);

    hfpmod.hfp_sco_rx = pcm_open(adev->snd_card,
                                  pcm_dev_asm_rx_id,
                                  PCM_OUT, &pcm_config_hfp);
    if (hfpmod.hfp_sco_rx && !pcm_is_ready(hfpmod.hfp_sco_rx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(hfpmod.hfp_sco_rx));
        ret = -EIO;
        goto exit;
    }

    hfpmod.hfp_sco_tx = pcm_open(adev->snd_card,
                                  pcm_dev_asm_tx_id,
                                  PCM_IN, &pcm_config_hfp);
    if (hfpmod.hfp_sco_tx && !pcm_is_ready(hfpmod.hfp_sco_tx)) {
        ALOGE("%s: %s", __func__, pcm_get_error(hfpmod.hfp_sco_tx));
        ret = -EIO;
        goto exit;
    }

    if (pcm_start(hfpmod.hfp_sco_rx) < 0) {
        ALOGE("%s: pcm start for hfp sco rx failed", __func__);
        ret = -EINVAL;
        goto exit;
    }
    if (pcm_start(hfpmod.hfp_sco_tx) < 0) {
        ALOGE("%s: pcm start for hfp sco tx failed", __func__);
        ret = -EINVAL;
        goto exit;
    }

    hfpmod.is_hfp_running = true;
    hfp_set_volume(adev, hfpmod.hfp_volume);

    /* Set mic volume by mute status, we don't provide set mic volume in phone app, only
    provide mute and unmute. */
    hfp_set_mic_mute(adev, adev->mic_muted);

    ALOGD("%s: exit: status(%d)", __func__, ret);
    return 0;

exit:
    stop_hfp(adev);
    ALOGE("%s: Problem in HFP start: status(%d)", __func__, ret);
    return ret;
}

static int32_t stop_hfp(struct audio_device *adev)
{
    int32_t ret = 0;
    struct audio_usecase *uc_info;
    struct listnode *node;
    struct audio_device_info *item = NULL;

    ALOGD("%s: enter", __func__);
    hfpmod.is_hfp_running = false;

    /* 1. Close the PCM devices */
    if (hfpmod.hfp_sco_rx) {
        pcm_close(hfpmod.hfp_sco_rx);
        hfpmod.hfp_sco_rx = NULL;
    }
    if (hfpmod.hfp_sco_tx) {
        pcm_close(hfpmod.hfp_sco_tx);
        hfpmod.hfp_sco_tx = NULL;
    }
    if (hfpmod.hfp_pcm_rx) {
        pcm_close(hfpmod.hfp_pcm_rx);
        hfpmod.hfp_pcm_rx = NULL;
    }
    if (hfpmod.hfp_pcm_tx) {
        pcm_close(hfpmod.hfp_pcm_tx);
        hfpmod.hfp_pcm_tx = NULL;
    }

    uc_info = fp_get_usecase_from_list(adev, hfpmod.ucid);
    if (uc_info == NULL) {
        ALOGE("%s: Could not find the usecase (%d) in the list",
              __func__, hfpmod.ucid);
        return -EINVAL;
    }

    if ((uc_info->out_snd_device != SND_DEVICE_NONE) ||
        (uc_info->in_snd_device != SND_DEVICE_NONE)) {
        if (fp_audio_extn_ext_hw_plugin_usecase_stop(adev->ext_hw_plugin, uc_info))
            ALOGE("%s: failed to stop ext hw plugin", __func__);
    }

    /* 2. Disable echo reference while stopping hfp */
    fp_platform_set_echo_reference(adev, false, &uc_info->device_list);

    /* 3. Get and set stream specific mixer controls */
    fp_disable_audio_route(adev, uc_info);

    /* 4. Disable the rx and tx devices */
    fp_disable_snd_device(adev, uc_info->out_snd_device);
    fp_disable_snd_device(adev, uc_info->in_snd_device);

    if (fp_audio_extn_auto_hal_stop_hfp_downlink(adev, uc_info))
        ALOGE("%s: stop hfp downlink failed", __func__);

    /* Set the unmute Tx mixer control */
    if (fp_voice_get_mic_mute(adev)) {
        fp_platform_set_mic_mute(adev->platform, false);
        ALOGD("%s: unMute HFP Tx", __func__);
    }
    adev->enable_hfp = false;

    list_remove(&uc_info->list);
    free(uc_info);

    ALOGD("%s: exit: status(%d)", __func__, ret);
    return ret;
}

void hfp_init(hfp_init_config_t init_config)
{
    fp_platform_set_mic_mute = init_config.fp_platform_set_mic_mute;
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
    fp_voice_get_mic_mute = init_config.fp_voice_get_mic_mute;
    fp_audio_extn_auto_hal_start_hfp_downlink =
                                init_config.fp_audio_extn_auto_hal_start_hfp_downlink;
    fp_audio_extn_auto_hal_stop_hfp_downlink =
                                init_config.fp_audio_extn_auto_hal_stop_hfp_downlink;
}

bool hfp_is_active(struct audio_device *adev)
{
    struct audio_usecase *hfp_usecase = NULL;
    hfp_usecase = fp_get_usecase_from_list(adev, hfpmod.ucid);

    if (hfp_usecase != NULL)
        return true;
    else
        return false;
}

int hfp_set_mic_mute2(struct audio_device *adev, bool state)
{
    struct mixer_ctl *ctl;
    const char *mixer_ctl_name = "HFP TX Mute";
    long set_values[ ] = {0};

    ALOGD("%s: enter, state=%d", __func__, state);

    set_values[0] = state;
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s: Could not get ctl for mixer cmd - %s",
               __func__, mixer_ctl_name);
        return -EINVAL;
    }
    mixer_ctl_set_array(ctl, set_values, ARRAY_SIZE(set_values));
    ALOGV("%s: exit", __func__);
    return 0;
}

audio_usecase_t hfp_get_usecase()
{
    return hfpmod.ucid;
}

void hfp_set_parameters(struct audio_device *adev, struct str_parms *parms)
{
    int ret;
    int rate;
    int val;
    float vol;
    char value[32]={0};

    ALOGD("%s: enter", __func__);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_HFP_ENABLE, value,
                            sizeof(value));
    if (ret >= 0) {
            if (!strncmp(value, "true", sizeof(value)) && !hfpmod.is_hfp_running)
                ret = start_hfp(adev,parms);
            else if (!strncmp(value, "false", sizeof(value)) && hfpmod.is_hfp_running)
                stop_hfp(adev);
            else
                ALOGE("hfp_enable=%s is unsupported", value);
    }
    memset(value, 0, sizeof(value));
    ret = str_parms_get_str(parms,AUDIO_PARAMETER_HFP_SET_SAMPLING_RATE, value,
                            sizeof(value));
    if (ret >= 0) {
           rate = atoi(value);
           if (rate == 8000){
               hfpmod.ucid = USECASE_AUDIO_HFP_SCO;
               pcm_config_hfp.rate = rate;
           } else if (rate == 16000){
               hfpmod.ucid = USECASE_AUDIO_HFP_SCO_WB;
               pcm_config_hfp.rate = rate;
           } else
               ALOGE("Unsupported rate..");
    }

    if (hfpmod.is_hfp_running) {
        memset(value, 0, sizeof(value));
        ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING,
                                value, sizeof(value));
        if (ret >= 0) {
            val = atoi(value);
            if (val > 0)
                fp_select_devices(adev, hfpmod.ucid);
        }
    }

    memset(value, 0, sizeof(value));
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_HFP_VOLUME,
                            value, sizeof(value));
    if (ret >= 0) {
        if (sscanf(value, "%f", &vol) != 1){
            ALOGE("%s: error in retrieving hfp volume", __func__);
            ret = -EIO;
            goto exit;
        }
        ALOGD("%s: set_hfp_volume usecase, Vol: [%f]", __func__, vol);
        hfp_set_volume(adev, vol);
    }

    memset(value, 0, sizeof(value));
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_HFP_PCM_DEV_ID, value, sizeof(value));
    if (ret >= 0) {
        hfpmod.hfp_pcm_dev_id = atoi(value);
        ALOGD("Updating HFP_PCM_DEV_ID as %d from platform XML", hfpmod.hfp_pcm_dev_id);
        str_parms_del(parms, AUDIO_PARAMETER_HFP_PCM_DEV_ID);
    }

    memset(value, 0, sizeof(value));
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_HFP_MIC_VOLUME,
                            value, sizeof(value));
    if (ret >= 0) {
        if (sscanf(value, "%f", &vol) != 1){
            ALOGE("%s: error in retrieving hfp mic volume", __func__);
            ret = -EIO;
            goto exit;
        }
        ALOGD("%s: set_hfp_mic_volume usecase, Vol: [%f]", __func__, vol);
        hfp_set_mic_volume(adev, vol);
    }

exit:
    ALOGV("%s Exit",__func__);
}
