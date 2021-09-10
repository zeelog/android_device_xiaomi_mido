/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
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
#ifndef AUDIO_HW_EXTN_ADSP_HDLR_H
#define AUDIO_HW_EXTN_ADSP_HDLR_H

typedef enum adsp_hdlr_command {
    ADSP_HDLR_CMD_INVALID = 0,
    ADSP_HDLR_STREAM_CMD_REGISTER_EVENT,
    ADSP_HDLR_STREAM_CMD_DEREGISTER_EVENT,
} adsp_hdlr_cmd_t;

struct adsp_hdlr_stream_cfg {
    int pcm_device_id;
    uint32_t flags;
    usecase_type_t type;
};

#ifdef AUDIO_EXTN_ADSP_HDLR_ENABLED

typedef int (*adsp_event_callback_t)(void *handle, void *payload, void *cookie);

int audio_extn_adsp_hdlr_init(struct mixer *mixer);
int audio_extn_adsp_hdlr_deinit(void);
int audio_extn_adsp_hdlr_stream_open(void **handle,
                struct adsp_hdlr_stream_cfg *config);
int audio_extn_adsp_hdlr_stream_close(void *handle);
int audio_extn_adsp_hdlr_stream_set_callback(void *handle,
                    stream_callback_t callback,
                    void *cookie);
int audio_extn_adsp_hdlr_stream_set_param(void *handle,
                    adsp_hdlr_cmd_t cmd,
                    void *param);
int audio_extn_adsp_hdlr_stream_register_event(void *handle,
                void *param, adsp_event_callback_t cb, void *cookie, bool is_adm_event);
int audio_extn_adsp_hdlr_stream_deregister_event(void *handle, void *param);
#else
#define audio_extn_adsp_hdlr_init(mixer)                                     (0)
#define audio_extn_adsp_hdlr_deinit()                                        (0)
#define audio_extn_adsp_hdlr_stream_open(handle,config)                      (0)
#define audio_extn_adsp_hdlr_stream_close(handle)                            (0)
#define audio_extn_adsp_hdlr_stream_set_callback(handle, callback, cookie)   (0)
#define audio_extn_adsp_hdlr_stream_set_param(handle, cmd, param)            (0)
#define audio_extn_adsp_hdlr_stream_register_event(handle, param, cb, cookie) (0)
#define audio_extn_adsp_hdlr_stream_deregister_event(handle, param)          (0)
#endif

#endif
