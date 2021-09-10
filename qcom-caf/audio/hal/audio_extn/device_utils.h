/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

#ifndef AUDIO_HW_EXTN_DEVICE_UTILS_H
#define AUDIO_HW_EXTN_DEVICE_UTILS_H

struct audio_device_info {
    struct listnode list;
    audio_devices_t type;
    char address[AUDIO_DEVICE_MAX_ADDRESS_LEN];
};

int list_length(struct listnode *list);
bool is_audio_in_device_type(struct listnode *devices);
bool is_audio_out_device_type(struct listnode *devices);
bool is_codec_backend_in_device_type(struct listnode *devices);
bool is_codec_backend_out_device_type(struct listnode *devices);
bool is_usb_in_device_type(struct listnode *devices);
bool is_usb_out_device_type(struct listnode *devices);
const char *get_usb_device_address(struct listnode *devices);
bool is_sco_in_device_type(struct listnode *devices);
bool is_sco_out_device_type(struct listnode *devices);
bool is_a2dp_in_device_type(struct listnode *devices);
bool is_a2dp_out_device_type(struct listnode *devices);
int clear_devices(struct listnode *devices);
bool compare_device_type(struct listnode *devices, audio_devices_t device_type);
bool compare_device_type_and_address(struct listnode *devices,
                                     audio_devices_t type, const char* address);
bool compare_devices_for_any_match(struct listnode *d1, struct listnode *d2);
audio_devices_t get_device_types(struct listnode *devices);
bool is_single_device_type_equal(struct listnode *devices,
                                 audio_devices_t type);
bool compare_devices(struct listnode *d1, struct listnode *d2);
int update_device_list(struct listnode *head, audio_devices_t type,
                       const char* address, bool add_device);
int assign_devices(struct listnode *dest, const struct listnode *source);
int assign_output_devices(struct listnode *dest, const struct listnode *source);
int reassign_device_list(struct listnode *device_list,
                            audio_devices_t type, char *address);
int append_devices(struct listnode *dest, const struct listnode *source);

#endif
