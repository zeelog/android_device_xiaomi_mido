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

#ifndef AUDIO_HW_EXTN_IP_HDLR_H
#define AUDIO_HW_EXTN_IP_HDLR_H

#ifdef AUDIO_EXTN_IP_HDLR_ENABLED

int audio_extn_ip_hdlr_intf_open(void *handle, bool is_dsp_decode, void *aud_sess_handle,
                                 audio_usecase_t usecase);
int audio_extn_ip_hdlr_intf_close(void *handle, bool is_dsp_decode, void *aud_sess_handle);
int audio_extn_ip_hdlr_intf_init(void **handle, char *lib_path, void **lib_handle,
                                 struct audio_device *dev, audio_usecase_t usecase);
int audio_extn_ip_hdlr_intf_deinit(void *handle);
bool audio_extn_ip_hdlr_intf_supported(audio_format_t format,
                                       bool is_direct_passthru,
                                       bool is_transcode_loopback);
bool audio_extn_ip_hdlr_intf_supported_for_copp(void *platform);
int audio_extn_ip_hdlr_copp_update_cal_info(void *cfg, void *data);

#else

#define audio_extn_ip_hdlr_intf_open(handle, is_dsp_decode, aud_sess_handle, usecase)  (0)
#define audio_extn_ip_hdlr_intf_close(handle, is_dsp_decode, aud_sess_handle)          (0)
#define audio_extn_ip_hdlr_intf_init(handle, lib_path, lib_handlei, adev, usecase)     (0)
#define audio_extn_ip_hdlr_intf_deinit(handle)                                (0)
#define audio_extn_ip_hdlr_intf_supported(format, is_direct_passthru, is_loopback) (0)
#define audio_extn_ip_hdlr_intf_supported_for_copp(platform) (0)
#define audio_extn_ip_hdlr_copp_update_cal_info(cfg, data) (0)

#endif

#endif
