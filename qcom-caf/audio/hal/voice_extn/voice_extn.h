/*
 * Copyright (c) 2013-2014, 2016-2020, The Linux Foundation. All rights reserved.
 * Not a contribution.
 *
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef VOICE_EXTN_H
#define VOICE_EXTN_H

void voice_extn_init(struct audio_device *adev);
int voice_extn_start_call(struct audio_device *adev);
int voice_extn_stop_call(struct audio_device *adev);
int voice_extn_get_session_from_use_case(struct audio_device *adev,
                                         const audio_usecase_t usecase_id,
                                         struct voice_session **session);
int voice_extn_set_parameters(struct audio_device *adev,
                              struct str_parms *parms);
void voice_extn_get_parameters(const struct audio_device *adev,
                               struct str_parms *query,
                               struct str_parms *reply);
int voice_extn_is_call_state_active(struct audio_device *adev,
                                    bool *is_call_active);
int voice_extn_get_active_session_id(struct audio_device *adev,
                                     uint32_t *session_id);
void voice_extn_in_get_parameters(struct stream_in *in,
                                  struct str_parms *query,
                                  struct str_parms *reply);
void voice_extn_out_get_parameters(struct stream_out *out,
                                   struct str_parms *query,
                                   struct str_parms *reply);

int voice_extn_check_and_set_incall_music_usecase(struct audio_device *adev,
                                                  struct stream_out *out);
int voice_extn_compress_voip_close_output_stream(struct audio_stream *stream);
int voice_extn_compress_voip_open_output_stream(struct stream_out *out);

int voice_extn_compress_voip_close_input_stream(struct audio_stream *stream);
int voice_extn_compress_voip_open_input_stream(struct stream_in *in);

int voice_extn_compress_voip_out_get_buffer_size(struct stream_out *out);
int voice_extn_compress_voip_in_get_buffer_size(struct stream_in *in);

int voice_extn_compress_voip_start_input_stream(struct stream_in *in);
int voice_extn_compress_voip_start_output_stream(struct stream_out *out);

int voice_extn_compress_voip_set_mic_mute(struct audio_device *dev, bool state);
int voice_extn_compress_voip_set_volume(struct audio_device *adev, float volume);
int voice_extn_compress_voip_select_devices(struct audio_device *adev,
                                            snd_device_t *out_snd_device,
                                            snd_device_t *in_snd_device);
int voice_extn_compress_voip_set_parameters(struct audio_device *adev,
                                             struct str_parms *parms);
void voice_extn_compress_voip_get_parameters(struct str_parms *query,
                                             struct str_parms *reply);

void voice_extn_compress_voip_out_get_parameters(struct stream_out *out,
                                                 struct str_parms *query,
                                                 struct str_parms *reply);
void voice_extn_compress_voip_in_get_parameters(struct stream_in *in,
                                                struct str_parms *query,
                                                struct str_parms *reply);
bool voice_extn_compress_voip_pcm_prop_check();
bool voice_extn_compress_voip_is_active(const struct audio_device *adev);
bool voice_extn_compress_voip_is_format_supported(audio_format_t format);
bool voice_extn_compress_voip_is_config_supported(struct audio_config *config);
bool voice_extn_compress_voip_is_started(struct audio_device *adev);
void voice_extn_feature_init();
void dynamic_ecns_feature_init(bool is_feature_enabled);
bool voice_extn_is_dynamic_ecns_enabled();
void incall_music_feature_init(bool is_feature_enabled);
bool voice_extn_is_incall_music_enabled();
void compr_voip_feature_init(bool is_feature_enabled);
bool voice_extn_is_compress_voip_supported();
void multi_voice_session_feature_init(bool is_feature_enabled);
bool voice_extn_is_multi_session_supported();
void voice_power_mode_feature_init(bool is_feature_enabled);
bool voice_extn_is_voice_power_mode_supported();


#endif //VOICE_EXTN_H
