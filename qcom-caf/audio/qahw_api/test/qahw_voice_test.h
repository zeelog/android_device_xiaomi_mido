/*
* Copyright (c) 2019, The Linux Foundation. All rights reserved.
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

/* Test app for voice call */

#ifndef QAHW_VOICE_TEST_H
#define QAHW_VOICE_TEST_H

#include <getopt.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <cutils/str_parms.h>
#include <tinyalsa/asoundlib.h>
#include "qahw_api.h"
#include "qahw_defs.h"

#define MAX_VOICE_TEST_DEVICES 2

typedef struct {
    qahw_module_handle_t *qahw_mod_handle;
    qahw_stream_handle_t *out_voice_handle;
    char* vsid;
    audio_devices_t output_device[MAX_VOICE_TEST_DEVICES];
    uint32_t call_length; /*sec*/
    int multi_call; /*number of calls to make */
    qahw_module_handle_t *out_handle;
    bool in_call_rec;
    bool in_call_playback;
    bool hpcm;
    int hpcm_tp;
    int tp_dir;
    char* rec_file;
    char* playback_file;
    float vol;
    bool mute;
    int mute_dir;
    int tty_mode;
    int dtmf_gen_enable;
    int dtmf_freq_low;
    int dtmf_freq_high;
    int dtmf_gain;
}voice_stream_config;

#endif /* QAHW_VOICE_TEST_H */
