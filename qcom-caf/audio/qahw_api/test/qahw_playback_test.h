/*
 * Copyright (c) 2016-2017,2019, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright (C) 2015 The Android Open Source Project *
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

#ifndef QAHW_PLAYBACK_TEST_H
#define QAHW_PLAYBACK_TEST_H

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
#include "qahw_effect_api.h"
#include "qahw_effect_test.h"

#define MAX_STR_LEN 256
typedef void* qap_module_handle_t;
bool kpi_mode;
bool enable_dump;
float vol_level;
uint8_t render_format;
bool ec_ref;


enum {
    FILE_WAV = 1,
    FILE_MP3,
    FILE_AAC,
    FILE_AAC_ADTS,
    FILE_FLAC,
    FILE_ALAC,
    FILE_VORBIS,
    FILE_WMA,
    FILE_AC3,
    FILE_AAC_LATM,
    FILE_EAC3,
    FILE_EAC3_JOC,
    FILE_DTS,
    FILE_MP2,
    FILE_APTX,
    FILE_TRUEHD,
    FILE_IEC61937,
    FILE_APE
};

typedef enum {
    USB_MODE_DEVICE,
    USB_MODE_HOST,
    USB_MODE_NONE
} usb_mode_type_t;

typedef enum {
    AAC_LC = 1,
    AAC_HE_V1,
    AAC_HE_V2,
    AAC_LOAS
} aac_format_type_t;

typedef enum {
    WMA = 1,
    WMA_PRO,
    WMA_LOSSLESS
} wma_format_type_t;

struct audio_config_params {
    qahw_module_handle_t *qahw_mod_handle;
    audio_io_handle_t handle;
    audio_devices_t input_device;
    audio_config_t config;
    audio_input_flags_t flags;
    const char* kStreamName ;
    audio_source_t kInputSource;
    char *file_name;
    volatile bool thread_exit;
};

typedef struct {
    qahw_module_handle_t *qahw_in_hal_handle;
    qahw_module_handle_t *qahw_out_hal_handle;
    audio_io_handle_t handle;
    char* filename;
    FILE* file_stream;
    char* timestamp_filename;
    FILE* timestamp_file_ptr;
    FILE* framesize_file_ptr;
    int filetype;
    int stream_index;
    audio_devices_t output_device;
    audio_devices_t input_device;
    audio_config_t config;
    audio_output_flags_t flags;
    qahw_stream_handle_t* out_handle;
    qahw_stream_handle_t* in_handle;
    int channels;
    aac_format_type_t aac_fmt_type;
    wma_format_type_t wma_fmt_type;
    char *kvpair_values;
    bool flags_set;
    usb_mode_type_t usb_mode;
    int effect_index;
    int effect_preset_strength;
    bool drift_query;
    bool drift_correction;
    bool play_later;
    char *device_url;
    thread_func_t ethread_func;
    thread_data_t *ethread_data;
    cmd_data_t cmd_data;
    int bytes_to_read;
    qap_module_handle_t qap_module_handle;
    bool sec_input;
    bool system_input;
    pthread_cond_t write_cond;
    pthread_mutex_t write_lock;
    pthread_cond_t drain_cond;
    pthread_mutex_t drain_lock;
    bool drain_received;
    bool interactive_strm;
    qahw_mix_matrix_params_t mm_params_pan_scale;
    qahw_mix_matrix_params_t mm_params_downmix;
    int mix_ctrl;
    int pan_scale_ctrl;
    pthread_cond_t input_buffer_available_cond;
    pthread_mutex_t input_buffer_available_lock;
    uint32_t input_buffer_available_size;
    char *set_params;
    bool bt_wbs;
}stream_config;

qahw_module_handle_t * load_hal(audio_devices_t dev);
int unload_hals();
int get_wav_header_length (FILE* file_stream);

#ifndef QAP
#define hal_test_qap_usage()                                             (0)
#define qap_wrapper_get_single_kvp(key, kv_pairs, status)                (0)
#define qap_wrapper_session_open(kv_pairs, stream_data, num_of_streams,\
                              qap_out_hal_handle_t)                      (0)
#define qap_wrapper_session_close()                                      (0)
#define qap_wrapper_stream_open(stream_data)                             (0)
#define qap_wrapper_get_cmd_string_from_arg_array(argc, argv, status)    (0)
#define qap_wrapper_start_stream (stream_data)                           (0)
#define is_qap_session_active(argc, argv, kvp_string)                    (0)
#define get_play_list(fp, stream_param, num_of_streams, kvp_str)         (0)
#define check_for_playlist(kvp_string)                                   (0)
inline int start_playback_through_qap(char * kvp_string __unused,
                                      int num_of_streams __unused,
                                      qahw_module_handle_t *qap_out_hal_handle_t __unused)
{
    return 0;
}
#define start_playback_through_qap_playlist(cmd_kvp_str, num_of_streams,\
                   kvp_string, stream_param, qap_wrapper_session_active,\
                   qap_out_hal_handle_t)                                 (0)
#else
void hal_test_qap_usage();
char * qap_wrapper_get_single_kvp(const char *key, const char *kv_pairs, int *status);
int qap_wrapper_session_open(char *kv_pairs, void* stream_data, int num_of_streams,\
                              qahw_module_handle_t *qap_out_hal_handle_t);
int qap_wrapper_session_close();
qap_module_handle_t qap_wrapper_stream_open(void* stream_data);
char * qap_wrapper_get_cmd_string_from_arg_array(int argc, char * argv[], int *status);
void *qap_wrapper_start_stream (void* stream_data);
void get_file_format(stream_config *stream_info);
bool is_qap_session_active(int argc, char* argv[], char *kvp_string);
void get_play_list(FILE *fp, stream_config (*stream_param)[], int *num_of_streams, char *kvp_str[]);
char* check_for_playlist(char *kvp_string);
int start_playback_through_qap(char * kvp_string, int num_of_streams,\
                                qahw_module_handle_t *qap_out_hal_handle_t);
int start_playback_through_qap_playlist(char *cmd_kvp_str[], int num_of_streams,\
    char *kvp_string, stream_config stream_param[], bool qap_wrapper_session_active,\
    qahw_module_handle_t *qap_out_hal_handle_t);
#endif
#endif /* QAHW_PLAYBACK_TEST_H */
