/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "ip_hdlr_intf"
/*#define LOG_NDEBUG 0*/
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#ifdef LINUX_ENABLED
#define LIB_PATH "libaudio_ip_handler.so"
#else
#define LIB_PATH "/system/vendor/lib/libaudio_ip_handler.so"
#endif

#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <log/log.h>
#include <sound/asound.h>
#include <cutils/properties.h>

#include "audio_hw.h"
#include "audio_defs.h"
#include "platform.h"
#include "audio_extn.h"
#include "platform_api.h"
#include "adsp_hdlr.h"
#include "audio_hw.h"

/* These values defined by ADSP */
#define ADSP_DEC_SERVICE_ID 1
#define ADSP_EVENT_ID_RTIC            0x00013239
#define ADSP_EVENT_ID_RTIC_FAIL       0x0001323A
#define TRUMPET_TOPOLOGY              0x11000099
#define TRUMPET_MODULE                0x0001099A

struct lib_fd_info {
    int32_t fd;
    int32_t flag;
};
struct ip_hdlr_stream {
    struct listnode list;
    void *stream;
    audio_usecase_t usecase;
};

struct ip_hdlr_intf {
    void *lib_hdl;
    int (*init)(void **handle, char *lib_path, void **lib_handle);
    int (*deinit)(void *handle);
    int (*open)(void *handle, bool is_dsp_decode, void *aud_sess_handle);
    int (*shm_info)(void *handle, int *fd);
    int (*shm_pp_info)(void *handle, int *fd);
    int (*shm_pp)(void *handle, bool is_adm_event);
    int (*get_lib_fd)(void *handle, int *lib_fd);
    int (*close)(void *handle);
    int (*event)(void *handle, void *payload);
    int (*reg_cb)(void *handle, void *ack_cb, void *fail_cb);
    int (*event_adm)(void *handle, void *payload);
    int (*deinit_lib)(void *handle);


    struct listnode stream_list;
    pthread_mutex_t stream_list_lock;
    int ref_cnt;
    bool lib_fd_created;
    void *ip_lib_handle; /*handle for dlclose of adsp lib*/
    bool adm_event;
    bool asm_event;
    void *ip_dev_handle;
};
static struct ip_hdlr_intf *ip_hdlr = NULL;
static bool adm_event_enable;
static bool asm_event_enable;
struct copp_cal_info {
    uint32_t             persist;
    uint32_t             snd_dev_id;
    audio_devices_t      dev_id;
    int32_t              acdb_dev_id;
    uint32_t             app_type;
    uint32_t             topo_id;
    uint32_t             sampling_rate;
    uint32_t             cal_type;
    uint32_t             module_id;
#ifdef INSTANCE_ID_ENABLED
    uint16_t             instance_id;
    uint16_t             reserved;
#endif
    uint32_t             param_id;
};
static struct copp_cal_info trumpet_data;
/* RTIC ack information */
struct rtic_ack_info {
    uint32_t token;
    uint32_t status;
};

struct module_info {
    uint32_t module_id;
    uint32_t instance_id;
    uint32_t token_coppidx;
};

struct rtic_ack_info_adm {
    uint32_t token;
    uint32_t status;
    struct module_info mod_data;
};

/* RTIC ack format sent to ADSP */
struct rtic_ack_param {
    uint32_t param_size;
    struct rtic_ack_info rtic_ack;
};

struct rtic_ack_param_adm {
    uint32_t param_size;
    struct rtic_ack_info rtic_ack;
    struct module_info mod_data;
};

/* each event payload format */
struct reg_ev_pl {
    uint32_t event_id;
    uint32_t cfg_mask;
};

struct reg_ev_pl_adm {
    uint32_t event_id;
    uint32_t module_id;
    uint16_t instance_id;
    uint16_t reserved;
    uint32_t cfg_mask;
};

struct adm_pp_module {
    uint32_t module_id;
    uint32_t instance_id;
    uint32_t be_id;
    uint32_t fe_id;
};

struct adm_fd_info {
    uint32_t fd;
    struct adm_pp_module adm_pp_pl_fd;
};
/* adm event registration format */
struct adm_reg_event {
    struct adm_pp_module adm_info;
    uint16_t version;
    uint32_t num_reg_events;
    struct reg_ev_pl_adm rtic;
    struct reg_ev_pl_adm rtic_fail;
};

/* event registration format */
struct reg_event {
    uint16_t version;
    uint16_t service_id;
    uint32_t num_reg_events;
    struct reg_ev_pl rtic;
    struct reg_ev_pl rtic_fail;
};

/* event received from ADSP is in this format */
struct rtic_event {
    uint16_t service_id;
    uint16_t reserved;
    uint32_t event_id;
    uint32_t payload_size;
    uint8_t payload[0];
};

int audio_extn_ip_hdlr_copp_update_cal_info(void *cfg, void *data)
{
    int ret = 0;
    acdb_audio_cal_cfg_t *cal = (acdb_audio_cal_cfg_t*) cfg;
    adm_event_enable = true; /* default enable with trumpet cal */

    memcpy(&trumpet_data, cal, sizeof(struct copp_cal_info));
    return ret;

}

bool audio_extn_ip_hdlr_intf_supported_for_copp(void *platform)
{
    return adm_event_enable;
}

bool audio_extn_ip_hdlr_intf_supported(audio_format_t format,
                    bool is_direct_passthrough,
                    bool is_transcode_loopback)
{

    if ((format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_DOLBY_TRUEHD) {
        asm_event_enable = true;
        return true;
    } else if ((format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_MAT) {
        asm_event_enable = true;
        return true;
    } else if (!is_direct_passthrough && !audio_extn_qaf_is_enabled() &&
            (((format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_E_AC3) ||
             ((format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AC3))) {
        asm_event_enable = true;
        return true;
   } else if (is_transcode_loopback &&
            (((format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_E_AC3) ||
             ((format & AUDIO_FORMAT_MAIN_MASK) == AUDIO_FORMAT_AC3))) {
       asm_event_enable = true;
       return true;
   } else {
       asm_event_enable = false;
       return false;
}

int audio_extn_ip_hdlr_intf_event_adm(void *stream_handle __unused,
                               void *payload, void *ip_hdlr_handle )
{
        ALOGVV("%s:[%d] handle = %p\n",__func__, ip_hdlr->ref_cnt, ip_hdlr_handle);

        return ip_hdlr->event_adm(ip_hdlr_handle, payload);
}

int audio_extn_ip_hdlr_intf_event(void *stream_handle __unused, void *payload, void *ip_hdlr_handle )
{
    ALOGVV("%s:[%d] handle = %p",__func__, ip_hdlr->ref_cnt, ip_hdlr_handle);

    return ip_hdlr->event(ip_hdlr_handle, payload);
}

int audio_extn_ip_hdlr_intf_rtic_ack_adm(void *aud_sess_handle,
                               struct rtic_ack_info_adm *info)
{
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};
    int ret = 0;
    int pcm_device_id = 0;
    struct mixer_ctl *ctl = NULL;
    struct rtic_ack_param_adm param;
    struct listnode *node = NULL, *tempnode = NULL;
    struct ip_hdlr_stream *stream_info = NULL;
    struct audio_device *adev = NULL;
    audio_usecase_t usecase = 0;

    memset(&param, 0, sizeof(struct rtic_ack_param_adm));
    pthread_mutex_lock(&ip_hdlr->stream_list_lock);
    list_for_each_safe(node, tempnode, &ip_hdlr->stream_list) {
        stream_info = node_to_item(node, struct ip_hdlr_stream, list);
        /* send the error if rtic failure notifcation is received */
        if ((stream_info->stream == aud_sess_handle) &&
            (stream_info->usecase == USECASE_AUDIO_TRANSCODE_LOOPBACK_RX)) {
            struct stream_inout *inout = (struct stream_inout *)aud_sess_handle;
            usecase = stream_info->usecase;
            adev = inout->dev;
            break;
        } else if (stream_info->stream == aud_sess_handle) {
            struct stream_out *out = (struct stream_out *)aud_sess_handle;
            usecase = stream_info->usecase;
            adev = out->dev;
            break;
        }
    }
    pthread_mutex_unlock(&ip_hdlr->stream_list_lock);

    if (adev == NULL) {
        ALOGE("%s:[%d] Invalid adev", __func__, ip_hdlr->ref_cnt);
        ret = -EINVAL;
        goto done;
    }

    pcm_device_id = platform_get_pcm_device_id(usecase, PCM_PLAYBACK);

    ALOGVV("%s:[%d] token = %d, info->status = %d, pcm_id = %d\n",__func__,
          ip_hdlr->ref_cnt, info->token, info->status, pcm_device_id);

    /* set mixer control to send RTIC done information */
    ret = snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                   "COPP Event Ack");
    if (ret < 0) {
        ALOGE("%s:[%d] snprintf failed",__func__, ip_hdlr->ref_cnt);
        ret = -EINVAL;
        goto done;
    }
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s:[%d] Could not get ctl for mixer cmd - %s", __func__,
              ip_hdlr->ref_cnt, mixer_ctl_name);
        ret = -EINVAL;
        goto done;
    }

    param.param_size = sizeof(struct rtic_ack_info);
    memcpy(&param.rtic_ack, info, sizeof(struct rtic_ack_info));
    memcpy(&param.mod_data, &info->mod_data, sizeof(struct module_info));
    ret = mixer_ctl_set_array(ctl, (void *)&param, sizeof(param));
    if (ret < 0) {
        ALOGE("%s:[%d] Could not set ctl for mixer cmd - %s, ret %d",
                    __func__, ip_hdlr->ref_cnt, mixer_ctl_name, ret);
        goto done;
    }

done:
    return ret;
}

int audio_extn_ip_hdlr_intf_rtic_ack(void *aud_sess_handle,
                                struct rtic_ack_info *info)
{
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};
    int ret = 0;
    int pcm_device_id = 0;
    struct mixer_ctl *ctl = NULL;
    struct rtic_ack_param param;
    struct listnode *node, *tempnode;
    struct ip_hdlr_stream *stream_info;
    struct audio_device *adev = NULL;
    audio_usecase_t usecase = 0;

    pthread_mutex_lock(&ip_hdlr->stream_list_lock);
    list_for_each_safe(node, tempnode, &ip_hdlr->stream_list) {
        stream_info = node_to_item(node, struct ip_hdlr_stream, list);
        /* send the error if rtic failure notifcation is received */
        if ((stream_info->stream == aud_sess_handle) &&
            (stream_info->usecase == USECASE_AUDIO_TRANSCODE_LOOPBACK_RX)) {
            struct stream_inout *inout = (struct stream_inout *)aud_sess_handle;
            usecase = stream_info->usecase;
            adev = inout->dev;
            break;
        } else if (stream_info->stream == aud_sess_handle) {
            struct stream_out *out = (struct stream_out *)aud_sess_handle;
            usecase = stream_info->usecase;
            adev = out->dev;
            break;
        }
    }
    pthread_mutex_unlock(&ip_hdlr->stream_list_lock);

    if (adev == NULL) {
        ALOGE("%s:[%d] Invalid adev", __func__, ip_hdlr->ref_cnt);
        ret = -EINVAL;
        goto done;
    }

    pcm_device_id = platform_get_pcm_device_id(usecase, PCM_PLAYBACK);

    ALOGVV("%s:[%d] token = %d, info->status = %d, pcm_id = %d",__func__,
          ip_hdlr->ref_cnt, info->token, info->status, pcm_device_id);

    /* set mixer control to send RTIC done information */
    ret = snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                   "Playback Event Ack %d", pcm_device_id);
    if (ret < 0) {
        ALOGE("%s:[%d] snprintf failed",__func__, ip_hdlr->ref_cnt);
        ret = -EINVAL;
        goto done;
    }
    ctl = mixer_get_ctl_by_name(adev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s:[%d] Could not get ctl for mixer cmd - %s", __func__,
              ip_hdlr->ref_cnt, mixer_ctl_name);
        ret = -EINVAL;
        goto done;
    }

    param.param_size = sizeof(struct rtic_ack_info);
    memcpy(&param.rtic_ack, info, sizeof(struct rtic_ack_info));
    ret = mixer_ctl_set_array(ctl, (void *)&param, sizeof(param));
    if (ret < 0) {
        ALOGE("%s:[%d] Could not set ctl for mixer cmd - %s, ret %d", __func__, ip_hdlr->ref_cnt,
              mixer_ctl_name, ret);
        goto done;
    }

done:
    return ret;
}

int audio_extn_ip_hdlr_intf_rtic_fail(void *aud_sess_handle)
{
    struct listnode *node, *tempnode;
    struct ip_hdlr_stream *stream_info;

    ALOGD("%s:[%d] sess_handle = %p",__func__, ip_hdlr->ref_cnt, aud_sess_handle);

    pthread_mutex_lock(&ip_hdlr->stream_list_lock);
    list_for_each_safe(node, tempnode, &ip_hdlr->stream_list) {
        stream_info = node_to_item(node, struct ip_hdlr_stream, list);
        /* send the error if rtic failure notifcation is received */
        if ((stream_info->stream == aud_sess_handle) &&
            (stream_info->usecase == USECASE_AUDIO_TRANSCODE_LOOPBACK_RX)) {
            struct stream_inout *inout = (struct stream_inout *)aud_sess_handle;
            pthread_mutex_lock(&inout->pre_lock);
            pthread_mutex_lock(&inout->lock);
            pthread_mutex_unlock(&inout->pre_lock);
            ALOGVV("%s:[%d] calling client callback", __func__, ip_hdlr->ref_cnt);
            if (inout && inout->client_callback)
                inout->client_callback((stream_callback_event_t)AUDIO_EXTN_STREAM_CBK_EVENT_ERROR, NULL, inout->client_cookie);
            pthread_mutex_unlock(&inout->lock);
            break;
        } else if (stream_info->stream == aud_sess_handle) {
            struct stream_out *out = (struct stream_out *)aud_sess_handle;
            pthread_mutex_lock(&out->pre_lock);
            pthread_mutex_lock(&out->lock);
            pthread_mutex_unlock(&out->pre_lock);
            ALOGVV("%s:[%d] calling client callback", __func__, ip_hdlr->ref_cnt);
            if (out && out->client_callback)
                out->client_callback((stream_callback_event_t)AUDIO_EXTN_STREAM_CBK_EVENT_ERROR, NULL, out->client_cookie);
            pthread_mutex_unlock(&out->lock);
            break;
        }
    }
    pthread_mutex_unlock(&ip_hdlr->stream_list_lock);

    return 0;
}

static int audio_extn_ip_hdlr_intf_open_adm_event(void *handle,
                    void *stream_handle, audio_usecase_t usecase)
{
    int ret = 0, fd = 0, pcm_device_id = 0;
    struct adm_fd_info *fd_param_data = NULL;
    struct audio_adsp_event *param = NULL;
    struct adm_reg_event *reg_ev = NULL;
    size_t shm_size = 0;
    void  *shm_buf = NULL;
    struct stream_out *out = NULL;
    struct stream_inout *inout = NULL;
    void *adsp_hdlr_stream_handle = NULL;
    struct audio_device *dev = NULL;
    struct mixer_ctl *ctl = NULL;
    struct audio_usecase *uc = NULL;
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};

    param = (struct audio_adsp_event *)calloc(1,
                sizeof(struct audio_adsp_event));

    if (param == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    reg_ev = (struct adm_reg_event *)calloc(1, sizeof(struct adm_reg_event));

    if (reg_ev == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    fd_param_data = (struct adm_fd_info *)calloc(1, sizeof(struct adm_fd_info));

    if (fd_param_data == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    uc = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));

    if (uc == NULL) {
        ret = -ENOMEM;
        goto done;
    }

    if (usecase == USECASE_AUDIO_TRANSCODE_LOOPBACK_RX) {
        inout = (struct stream_inout *)stream_handle;
        adsp_hdlr_stream_handle = inout->adsp_hdlr_stream_handle;
        dev = inout->dev;
    } else {
        out = (struct stream_out *)stream_handle;
        adsp_hdlr_stream_handle = out->adsp_hdlr_stream_handle;
        dev = out->dev;
    }
    uc = get_usecase_from_list(dev, usecase);

    reg_ev->adm_info.module_id = TRUMPET_MODULE;
    reg_ev->adm_info.instance_id = 0;
    reg_ev->adm_info.be_id = platform_get_snd_device_backend_index(
                                                uc->out_snd_device);
    reg_ev->adm_info.fe_id = platform_get_pcm_device_id(usecase, PCM_PLAYBACK);
    reg_ev->num_reg_events = 2;
    reg_ev->rtic.event_id = ADSP_EVENT_ID_RTIC;
    reg_ev->rtic.module_id = TRUMPET_MODULE;
    reg_ev->rtic.instance_id = 0;
    reg_ev->rtic.cfg_mask = 1; /* event enabled */
    reg_ev->rtic_fail.event_id = ADSP_EVENT_ID_RTIC_FAIL;
    reg_ev->rtic_fail.module_id = TRUMPET_MODULE;
    reg_ev->rtic_fail.instance_id = 0;
    reg_ev->rtic_fail.cfg_mask = 1; /* event enabled */

    param->event_type = AUDIO_COPP_EVENT;
    param->payload_length = sizeof(struct adm_reg_event);
    param->payload = reg_ev;
    bool is_adm_event = true;

    /* Register for event and its callback */
    ret = audio_extn_adsp_hdlr_stream_register_event(adsp_hdlr_stream_handle,
               param, audio_extn_ip_hdlr_intf_event_adm, handle, is_adm_event);
    if (ret < 0) {
        ALOGE("%s:[%d] failed to register event",__func__,
                                    ip_hdlr->ref_cnt, ret);
        goto done;
    }

    ip_hdlr->reg_cb(handle, &audio_extn_ip_hdlr_intf_rtic_ack_adm,
                                &audio_extn_ip_hdlr_intf_rtic_fail);
    ip_hdlr->shm_pp_info(handle, &fd);
    fd_param_data->fd = fd;
    memcpy(&fd_param_data->adm_pp_pl_fd, &reg_ev->adm_info,
                                sizeof(struct adm_pp_module));

    ret = snprintf(mixer_ctl_name, sizeof(mixer_ctl_name), "COPP ION FD");

    if (ret < 0) {
        ALOGE("%s:[%d] snprintf failed",__func__, ip_hdlr->ref_cnt, ret);
        goto done;
    }
    ALOGV("%s: fd = %d\n", __func__, fd);

    ctl = mixer_get_ctl_by_name(dev->mixer, mixer_ctl_name);

    if (!ctl) {
        ALOGE("%s:[%d] Could not get ctl for mixer cmd - %s", __func__,
              ip_hdlr->ref_cnt, mixer_ctl_name);
        ret = -EINVAL;
        goto done;
    }
    ret = mixer_ctl_set_array(ctl, fd_param_data, sizeof(fd_param_data));

    if (ret < 0) {
        ALOGE("%s:[%d] Could not set ctl for mixer cmd - %s, ret %d"
                , __func__, ip_hdlr->ref_cnt, mixer_ctl_name, ret);
        goto done;
    }

done:
    if (param)
        free(param);

    if (reg_ev)
        free(reg_ev);

    if (fd_param_data)
        free(fd_param_data);

    if (uc)
        free(uc);

    return ret;
}

static int audio_extn_ip_hdlr_intf_open_dsp(void *handle, void *stream_handle, audio_usecase_t usecase)
{
    int ret = 0, fd = 0, pcm_device_id = 0;
    struct audio_adsp_event *param;
    struct reg_event *reg_ev;
    struct stream_out *out;
    struct stream_inout *inout;
    void *adsp_hdlr_stream_handle;
    struct audio_device *dev = NULL;
    struct mixer_ctl *ctl = NULL;
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};
    bool is_adm_event = false;
    param = (struct audio_adsp_event *)calloc(1, sizeof(struct audio_adsp_event));
    if (!param)
        return -ENOMEM;

    reg_ev = (struct reg_event *)calloc(1, sizeof(struct reg_event));
    if (!reg_ev)
        return -ENOMEM;

    reg_ev->service_id = ADSP_DEC_SERVICE_ID;
    reg_ev->num_reg_events = 2;
    reg_ev->rtic.event_id = ADSP_EVENT_ID_RTIC;
    reg_ev->rtic.cfg_mask = 1; /* event enabled */
    reg_ev->rtic_fail.event_id = ADSP_EVENT_ID_RTIC_FAIL;
    reg_ev->rtic_fail.cfg_mask = 1; /* event enabled */

    param->event_type = AUDIO_STREAM_ENCDEC_EVENT;
    param->payload_length = sizeof(struct reg_event);
    param->payload = reg_ev;

    if (usecase == USECASE_AUDIO_TRANSCODE_LOOPBACK_RX) {
        inout = (struct stream_inout *)stream_handle;
        adsp_hdlr_stream_handle = inout->adsp_hdlr_stream_handle;
        dev = inout->dev;
    } else {
        out = (struct stream_out *)stream_handle;
        adsp_hdlr_stream_handle = out->adsp_hdlr_stream_handle;
        dev = out->dev;
    }

    /* Register for event and its callback */
    ret = audio_extn_adsp_hdlr_stream_register_event(adsp_hdlr_stream_handle, param,
                                                     audio_extn_ip_hdlr_intf_event,
                                                     handle, is_adm_event);
    if (ret < 0) {
        ALOGE("%s:[%d] failed to register event %d",__func__, ip_hdlr->ref_cnt, ret);
        goto done;
    }

    ip_hdlr->reg_cb(handle, &audio_extn_ip_hdlr_intf_rtic_ack, &audio_extn_ip_hdlr_intf_rtic_fail);
    ip_hdlr->shm_info(handle, &fd);

    pcm_device_id = platform_get_pcm_device_id(usecase, PCM_PLAYBACK);
    ret = snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                   "Playback ION FD %d", pcm_device_id);
    if (ret < 0) {
        ALOGE("%s:[%d] snprintf failed %d",__func__, ip_hdlr->ref_cnt, ret);
        goto done;
    }
    ALOGV("%s: fd = %d  pcm_id = %d", __func__, fd, pcm_device_id);

    ctl = mixer_get_ctl_by_name(dev->mixer, mixer_ctl_name);
    if (!ctl) {
        ALOGE("%s:[%d] Could not get ctl for mixer cmd - %s", __func__,
              ip_hdlr->ref_cnt, mixer_ctl_name);
        ret = -EINVAL;
        goto done;
    }
    ret = mixer_ctl_set_array(ctl, &fd, sizeof(fd));
    if (ret < 0) {
        ALOGE("%s:[%d] Could not set ctl for mixer cmd - %s, ret %d", __func__, ip_hdlr->ref_cnt,
              mixer_ctl_name, ret);
        goto done;
    }

done:
    free(param);
    free(reg_ev);
    return ret;
}

int audio_extn_ip_hdlr_intf_open(void *handle, bool is_dsp_decode,
                                 void *aud_sess_handle, audio_usecase_t usecase)
{
    int ret = 0;
    struct ip_hdlr_stream *stream_info;

    if (!handle || !aud_sess_handle) {
        ALOGE("%s:[%d] Invalid arguments, handle %p", __func__, ip_hdlr->ref_cnt, handle);
        return -EINVAL;
    }

    stream_info = (struct ip_hdlr_stream *)calloc(1, sizeof(struct ip_hdlr_stream));
    if (!stream_info)
        return -ENOMEM;
    stream_info->stream = aud_sess_handle;
    stream_info->usecase = usecase;

    ret = ip_hdlr->open(handle, is_dsp_decode, aud_sess_handle);
    if (ret < 0) {
        ALOGE("%s:[%d] open failed", __func__, ip_hdlr->ref_cnt);
        return -EINVAL;
    }
    ALOGD("%s:[%d] handle = %p, sess_handle = %p, is_dsp_decode = %d, usecase = %d",
          __func__, ip_hdlr->ref_cnt, handle, aud_sess_handle, is_dsp_decode, usecase);
    if (is_dsp_decode && ip_hdlr->asm_event) {
        ret = audio_extn_ip_hdlr_intf_open_dsp(handle, aud_sess_handle, usecase);
        if (ret < 0)
            ip_hdlr->close(handle);
    }

    if (is_dsp_decode && ip_hdlr->adm_event) {
        ret = audio_extn_ip_hdlr_intf_open_adm_event(handle, aud_sess_handle, usecase);
        if (ret < 0)
            ip_hdlr->close(handle);
    }

    pthread_mutex_lock(&ip_hdlr->stream_list_lock);
    list_add_tail(&ip_hdlr->stream_list, &stream_info->list);
    pthread_mutex_unlock(&ip_hdlr->stream_list_lock);

    return ret;
}

int audio_extn_ip_hdlr_intf_close(void *handle, bool is_dsp_decode, void *aud_sess_handle __unused)
{
    struct audio_adsp_event param;
    void *adsp_hdlr_stream_handle;
    struct listnode *node, *tempnode;
    struct ip_hdlr_stream *stream_info;
    audio_usecase_t usecase = 0;
    int ret = 0;

    if (!handle) {
        ALOGE("%s:[%d] handle is NULL", __func__, ip_hdlr->ref_cnt);
        return -EINVAL;
    }

    ret = ip_hdlr->close(handle);
    if (ret < 0)
        ALOGE("%s:[%d] close failed", __func__, ip_hdlr->ref_cnt);

    pthread_mutex_lock(&ip_hdlr->stream_list_lock);
    list_for_each_safe(node, tempnode, &ip_hdlr->stream_list) {
        stream_info = node_to_item(node, struct ip_hdlr_stream, list);
        if (stream_info->stream == aud_sess_handle) {
            usecase = stream_info->usecase;
            list_remove(node);
            free(stream_info);
            break;
        }
    }
    pthread_mutex_unlock(&ip_hdlr->stream_list_lock);
    ALOGD("%s:[%d] handle = %p, usecase = %d",__func__, ip_hdlr->ref_cnt, handle, usecase);

    if (is_dsp_decode) {
        if (usecase == USECASE_AUDIO_TRANSCODE_LOOPBACK_RX) {
            struct stream_inout *inout = (struct stream_inout *)aud_sess_handle;
            adsp_hdlr_stream_handle = inout->adsp_hdlr_stream_handle;
        } else {
            struct stream_out *out = (struct stream_out *)aud_sess_handle;
            adsp_hdlr_stream_handle = out->adsp_hdlr_stream_handle;
        }
        param.event_type = AUDIO_STREAM_ENCDEC_EVENT;
        param.payload_length = 0;
        /* Deregister the event */
        ret = audio_extn_adsp_hdlr_stream_deregister_event(adsp_hdlr_stream_handle, &param);
        if (ret < 0)
            ALOGE("%s:[%d] event deregister failed", __func__, ip_hdlr->ref_cnt);
    }

    return ret;
}

int audio_extn_ip_hdlr_intf_init(void **handle, char *lib_path, void **lib_handle,
                                 struct audio_device *dev, audio_usecase_t usecase)
{
    int ret = 0, pcm_device_id;
    struct lib_fd_info lib_fd;
    struct mixer_ctl *ctl = NULL;
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};

    memset(&lib_fd, 0, sizeof(struct lib_fd_info));

    if (!ip_hdlr) {
        ip_hdlr = (struct ip_hdlr_intf *)calloc(1, sizeof(struct ip_hdlr_intf));
        if (!ip_hdlr)
            return -ENOMEM;

        list_init(&ip_hdlr->stream_list);
        pthread_mutex_init(&ip_hdlr->stream_list_lock, (const pthread_mutexattr_t *) NULL);

        ip_hdlr->lib_hdl = dlopen(LIB_PATH, RTLD_NOW);
        if (ip_hdlr->lib_hdl == NULL) {
             ALOGE("%s: DLOPEN failed, %s", __func__, dlerror());
             ret = -EINVAL;
             goto err;
        }
        ip_hdlr->init =(int (*)(void **handle, char *lib_path,
                                void **lib_handle))dlsym(ip_hdlr->lib_hdl, "audio_ip_hdlr_init");
        ip_hdlr->deinit = (int (*)(void *handle))dlsym(ip_hdlr->lib_hdl, "audio_ip_hdlr_deinit");
        ip_hdlr->deinit_lib = (int (*)(void *handle))dlsym(ip_hdlr->lib_hdl,
                                                "audio_ip_hdlr_deinit_lib");
        ip_hdlr->open = (int (*)(void *handle, bool is_dsp_decode,
                                 void *sess_handle))dlsym(ip_hdlr->lib_hdl, "audio_ip_hdlr_open");
        ip_hdlr->close =(int (*)(void *handle))dlsym(ip_hdlr->lib_hdl, "audio_ip_hdlr_close");
        ip_hdlr->reg_cb =(int (*)(void *handle, void *ack_cb,
                                  void *fail_cb))dlsym(ip_hdlr->lib_hdl, "audio_ip_hdlr_reg_cb");
        ip_hdlr->shm_info =(int (*)(void *handle, int *fd))dlsym(
                                   ip_hdlr->lib_hdl, "audio_ip_hdlr_shm_info");
        ip_hdlr->shm_pp_info =(int (*)(void *handle, int *fd))dlsym(ip_hdlr->lib_hdl,
                                                                 "audio_ip_hdlr_shm_pp_info");
        ip_hdlr->get_lib_fd =(int (*)(void *handle, int *fd))dlsym(ip_hdlr->lib_hdl,
                                                                 "audio_ip_hdlr_lib_fd");
        ip_hdlr->event =(int (*)(void *handle, void *payload))dlsym(ip_hdlr->lib_hdl,
                                                                    "audio_ip_hdlr_event");
        ip_hdlr->event_adm =(int (*)(void *handle, void *payload))dlsym(
                            ip_hdlr->lib_hdl, "audio_ip_hdlr_event_adm");
        ip_hdlr->shm_pp = (int (*)(void *handle, bool is_adm_event))dlsym(
                            ip_hdlr->lib_hdl, "audio_ip_hdlr_create_shm_pp");
        if (!ip_hdlr->init || !ip_hdlr->deinit || !ip_hdlr->open ||
            !ip_hdlr->close || !ip_hdlr->reg_cb || !ip_hdlr->shm_info ||
            !ip_hdlr->event || !ip_hdlr->get_lib_fd || !ip_hdlr->deinit_lib ||
            !ip_hdlr->shm_pp || !ip_hdlr->shm_pp_info || !ip_hdlr->event_adm) {
            ALOGE("%s: failed to get symbols", __func__);
            ret = -EINVAL;
            goto dlclose;
        }
    }

    ip_hdlr->ip_dev_handle = dev;
    ret = ip_hdlr->init(handle, lib_path, lib_handle);
    if (ret < 0) {
        ALOGE("%s:[%d] init failed ret = %d", __func__, ip_hdlr->ref_cnt, ret);
        ret = -EINVAL;
        goto dlclose;
    }

    if (adm_event_enable) {
        ret = ip_hdlr->shm_pp(*handle, adm_event_enable);

        if (ret < 0) {
            ALOGE("%s:[%d] init failed ret = %d", __func__, ip_hdlr->ref_cnt, ret);
            ret = -EINVAL;
            goto dlclose;
        }
        ip_hdlr->adm_event = adm_event_enable;
        adm_event_enable = false;
    }

    if (asm_event_enable) {
        ip_hdlr->asm_event = asm_event_enable;
        asm_event_enable = false;
    }

    if (!lib_path && !(ip_hdlr->lib_fd_created)) {
        /* save handle to dlcose of lib fd */
        ip_hdlr->ip_lib_handle = handle;
        ip_hdlr->get_lib_fd(*handle, &lib_fd.fd);
        lib_fd.flag = 1;
        /* sending lib ion fd to routing driver */
        ret = snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                       "ADSP ION LIB FD");
        if (ret < 0) {
            ALOGE("%s:[%d] snprintf failed %d", __func__, ip_hdlr->ref_cnt, ret);
            goto dlclose;
        }

        ctl = mixer_get_ctl_by_name(dev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s:[%d] Could not get ctl for mixer cmd - %s", __func__,
                  ip_hdlr->ref_cnt, mixer_ctl_name);
            ret = -EINVAL;
            goto dlclose;
        }

        ret = mixer_ctl_set_array(ctl, &lib_fd, sizeof(struct lib_fd_info));
        if (ret < 0) {
            ALOGE("%s:[%d] Could not set ctl for mixer cmd - %s, ret %d", __func__, ip_hdlr->ref_cnt,
                  mixer_ctl_name, ret);
            goto dlclose;
        }
        ip_hdlr->lib_fd_created = true;
    }
    ip_hdlr->ref_cnt++;
    ALOGD("%s:[%d] init done", __func__, ip_hdlr->ref_cnt);

    return 0;

dlclose:
    dlclose(ip_hdlr->lib_hdl);
err:
    pthread_mutex_destroy(&ip_hdlr->stream_list_lock);
    free(ip_hdlr);
    ip_hdlr = NULL;
    return ret;
}

int audio_extn_ip_hdlr_intf_deinit(void *handle)
{
    int ret = 0;
    struct lib_fd_info lib_fd;
    struct mixer_ctl *ctl = NULL;
    char mixer_ctl_name[MIXER_PATH_MAX_LENGTH] = {0};
    struct audio_device *ip_dev = (struct audio_device *) (ip_hdlr->ip_dev_handle);

    memset(&lib_fd, 0, sizeof(struct lib_fd_info));

    if (!handle) {
        ALOGE("%s:[%d] handle is NULL", __func__, ip_hdlr->ref_cnt);
        return -EINVAL;
    }
    ALOGD("%s:[%d] handle = %p",__func__, ip_hdlr->ref_cnt, handle);

    if (--ip_hdlr->ref_cnt == 0) {
        ip_hdlr->get_lib_fd(handle, &lib_fd.fd);
        lib_fd.flag = 0;
        /* sending lib ion fd to routing driver */
        ret = snprintf(mixer_ctl_name, sizeof(mixer_ctl_name),
                       "ADSP ION LIB FD");
        if (ret < 0) {
            ALOGE("%s:[%d] snprintf failed %d"
                , __func__, ip_hdlr->ref_cnt, ret);
            goto dlclose;
        }

        ctl = mixer_get_ctl_by_name(ip_dev->mixer, mixer_ctl_name);
        if (!ctl) {
            ALOGE("%s:[%d] Could not get ctl for mixer cmd - %s", __func__,
                  ip_hdlr->ref_cnt, mixer_ctl_name);
            ret = -EINVAL;
            goto dlclose;
        }
        ret = mixer_ctl_set_array(ctl, &lib_fd, sizeof(struct lib_fd_info));
        if (ret < 0) {
            ALOGE("%s:[%d] Could not set ctl for mixer cmd - %s, ret %d"
                        , __func__, ip_hdlr->ref_cnt, mixer_ctl_name, ret);
            goto dlclose;
        }

        ret = ip_hdlr->deinit_lib(handle);
        ip_hdlr->lib_fd_created = false;
        ret = ip_hdlr->deinit(handle);
        if (ret < 0)
            ALOGE("%s:[%d] deinit failed ret = %d", __func__, ip_hdlr->ref_cnt, ret);
        if (ip_hdlr->lib_hdl)
            dlclose(ip_hdlr->lib_hdl);
dlclose:
        pthread_mutex_destroy(&ip_hdlr->stream_list_lock);
        free(ip_hdlr);
        ip_hdlr = NULL;
    }
    ALOGD("%s: done\n", __func__);
    return ret;

}
