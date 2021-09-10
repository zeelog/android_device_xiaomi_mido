/*
 * Copyright (c) 2016-2017,2019 The Linux Foundation. All rights reserved.
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

/* Test app extension to exercise QAP (Non-tunnel Decode) */

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <cutils/properties.h>
#include <cutils/list.h>
#include <cutils/str_parms.h>
#include <log/log.h>
#include <system/audio.h>
#include <qap_api.h>
#include <qti_audio.h>
#include "qahw_playback_test.h"
#include <dolby_ms12.h>

#undef LOG_TAG
#define LOG_TAG "HAL_TEST"
#undef LOG_NDEBUG
/*#define LOG_NDEBUG 0*/

#if LINUX_ENABLED
#if defined(__LP64__)
#define QAC_LIB_MS12 "/usr/lib64/libdolby_ms12_wrapper.so"
#else
#define QAC_LIB_MS12 "/usr/lib/libdolby_ms12_wrapper.so"
#endif
#define QAC_LIB_M8   "libdts_m8_wrapper.so"
#else
#define QAC_LIB_MS12 "/system/lib/libdolby_ms12_wrapper.so"
#define QAC_LIB_M8   "/system/lib/libdts_m8_wrapper.so"
#endif

#define SESSION_BLURAY   1
#define SESSION_BROADCAST 2
#define MAX_OUTPUT_CHANNELS 8
#define FRAME_SIZE 32768 /* 32k size */
#define MAX_BUFFER_SIZE 32768 /* 32k size */
#define CONTIGUOUS_TIMESTAMP 0x7fffffff
#define TIMESTAMP_ARRAY_SIZE 2048
#define DOLBY 1
#define DTS   2
#define FRAME_SIZE_FOR_2CH_PCM 6144 /* For 48k samplerate, 2 ch, 2 bytes */
#define PCM_16_BITWIDTH 16
#define PCM_24_BITWIDTH 24
#define DEFAULT_SAMPLE_RATE 48000
#define MAX_QAP_MODULE_OUT 3

extern bool stop_playback;
bool is_media_fmt_changed[MAX_QAP_MODULE_OUT];
int new_output_conf_index = 0;

qap_lib_handle_t ms12_lib_handle = NULL;
qap_lib_handle_t m8_lib_handle = NULL;
qap_session_handle_t qap_session_handle = NULL;
qahw_module_handle_t *qap_out_hal_handle = NULL;
qahw_module_handle_t *qap_out_spk_handle = NULL;
qahw_module_handle_t *qap_out_hdmi_handle = NULL;
qahw_module_handle_t *qap_out_hp_handle = NULL;

audio_io_handle_t qap_stream_out_spk_handle = 0x999;
audio_io_handle_t qap_stream_out_hdmi_handle = 0x998;
audio_io_handle_t qap_stream_out_hp_handle = 0x997;
audio_io_handle_t qap_stream_out_cmpr_handle = 0x996;

FILE *fp_output_writer_spk = NULL;
FILE *fp_output_writer_hp = NULL;
FILE *fp_output_writer_hdmi = NULL;
FILE *fp_output_timestamp_file = NULL;
FILE *fp_ecref = NULL;
unsigned char data_buf[MAX_BUFFER_SIZE];
uint32_t output_device_id = 0;
uint16_t input_streams_count = 0;

bool hdmi_connected = false;
bool play_through_bt = false;
bool encode = false;
bool dolby_formats = false;
bool timestamp_mode = false;
int  data_write_count = 0;
int data_callback_count = 0;
bool play_list = false;
int play_list_cnt = 0;
uint8_t session_type = SESSION_BLURAY;

pthread_t main_input_thread;
pthread_attr_t main_input_thrd_attr;
pthread_cond_t main_eos_cond;
pthread_mutex_t main_eos_lock;
pthread_cond_t sec_eos_cond;
pthread_mutex_t sec_eos_lock;
pthread_cond_t main2_eos_cond;
pthread_mutex_t main2_eos_lock;
bool main_eos_received = false;
bool main2_eos_received = false;
bool sec_eos_received = false;

dlb_ms12_session_param_t dlb_param;
dlb_ms12_session_param_t dlb_param_hp;
qap_session_outputs_config_t session_output_config;
bool session_output_configured = false;
struct timeval tcold_start, tcold_stop;
struct timeval tcont_ts1, tcont_ts2;
double cold_start, cold_stop;
long int data_callback_ts_arr[TIMESTAMP_ARRAY_SIZE];
long int data_input_ts_arr[TIMESTAMP_ARRAY_SIZE];
double data_input_st_arr[TIMESTAMP_ARRAY_SIZE];
double data_callback_st_arr[TIMESTAMP_ARRAY_SIZE];
bool has_system_input = false;
char session_kv_pairs[256];
bool primary_stream_close = false;
int8_t stream_cnt = 0;
uint32_t dsp_latency = 0;

static int get_qap_session_out_config_index_for_id(uint32_t out_id)
{
    int index = -1, i;

    for (i = 0; i < MAX_QAP_MODULE_OUT; i++)
        if (session_output_config.output_config[i].id == out_id)
            index = i;

    return index;
}

static void set_qahw_stream_channel_map(qahw_stream_handle_t *out_handle, qap_output_config_t *qap_config)
{
    struct qahw_out_channel_map_param chmap_param = {0};
    int i = 0;
    if (qap_config == NULL || out_handle == NULL) {
        return;
    }
    chmap_param.channels = qap_config->channels;
    for (i = 0; i < chmap_param.channels && i < AUDIO_CHANNEL_COUNT_MAX && i < QAP_AUDIO_MAX_CHANNELS;
            i++) {
        chmap_param.channel_map[i] = qap_config->ch_map[i];
    }
    qahw_out_set_param_data(out_handle, QAHW_PARAM_OUT_CHANNEL_MAP, (qahw_param_payload *) &chmap_param);
}

static void update_combo_dev_kvpairs()
{
    bool enable_spk = false;
    bool enable_hp = false;
    bool enable_hdmi = false;
    bool combo_enabled = false;
    char dev_kv_pair[16] = {0};

    ALOGV("%s:%d output device id %d", __func__, __LINE__, output_device_id);

    if (output_device_id & AUDIO_DEVICE_OUT_HDMI)
        enable_hdmi = true;
    if (output_device_id & AUDIO_DEVICE_OUT_WIRED_HEADPHONE)
        enable_hp = true;
    if (output_device_id & AUDIO_DEVICE_OUT_SPEAKER)
        enable_spk = true;

    // Update the kv_pair based on the output device selsction
    // To select hdmi, spekaer and headphone: o_device=1,2,8
    // To select hdmi, headphone: o_device=2,8
    // To select hdmi, spekaer : o_device=1,8
    // To select spekaer and headphone: o_device=1,2
    if (enable_hdmi && enable_hp && enable_spk) {
        sprintf(dev_kv_pair, "o_device=1,2,8");
        combo_enabled = true;
    } else if (enable_hdmi && enable_hp) {
        sprintf(dev_kv_pair, "o_device=2,8");
        combo_enabled = true;
    } else if (enable_hdmi && enable_spk) {
        sprintf(dev_kv_pair, "o_device=1,8");
        combo_enabled = true;
    } else if (enable_hp && enable_spk) {
        sprintf(dev_kv_pair, "o_device=1,2");
        combo_enabled = true;
    }

    if (combo_enabled)
        strcat(session_kv_pairs, dev_kv_pair);

    ALOGV("%s:%d session set param %s and combo_enabled %d", __func__, __LINE__, session_kv_pairs, combo_enabled);
    return;
}
static void update_session_outputs_config(int hdmi_render_format, int in_channels, int bitwidth, int smpl_rate)
{
    bool enable_spk = false;
    bool enable_hp = false;
    bool enable_hdmi = false;
    bool combo_enabled = false;
    char dev_kv_pair[16] = {0};
    bool enable_ecref = false;

    ALOGV("%s:%d output device id %d render format = %d", __func__, __LINE__, output_device_id, hdmi_render_format);

    if (output_device_id & AUDIO_DEVICE_OUT_HDMI)
        enable_hdmi = true;
    if (output_device_id & AUDIO_DEVICE_OUT_WIRED_HEADPHONE ||
        output_device_id & AUDIO_DEVICE_OUT_LINE)
        enable_hp = true;
    if (output_device_id & AUDIO_DEVICE_OUT_SPEAKER)
        enable_spk = true;
    if (ec_ref)
        enable_ecref = true;

    if (enable_hdmi) {
        session_output_config.output_config[session_output_config.num_output].id = AUDIO_DEVICE_OUT_HDMI;
        if (hdmi_render_format == 1) {
            session_output_config.output_config[session_output_config.num_output].id = AUDIO_DEVICE_OUT_HDMI|AUDIO_FORMAT_AC3;
            session_output_config.output_config[session_output_config.num_output].format = QAP_AUDIO_FORMAT_AC3;
        } else if (hdmi_render_format == 2) {
            session_output_config.output_config[session_output_config.num_output].id = AUDIO_DEVICE_OUT_HDMI|AUDIO_FORMAT_E_AC3;
            session_output_config.output_config[session_output_config.num_output].format = QAP_AUDIO_FORMAT_EAC3;
        } else if (hdmi_render_format == 3) {
            session_output_config.output_config[session_output_config.num_output].id = AUDIO_DEVICE_OUT_HDMI|AUDIO_FORMAT_DTS;
            session_output_config.output_config[session_output_config.num_output].format = QAP_AUDIO_FORMAT_DTS;
        } else {
            if (bitwidth == PCM_24_BITWIDTH) {
                session_output_config.output_config[session_output_config.num_output].format = QAP_AUDIO_FORMAT_PCM_24_BIT_PACKED;
                session_output_config.output_config[session_output_config.num_output].bit_width = PCM_24_BITWIDTH;
            } else {
                session_output_config.output_config[session_output_config.num_output].format = QAP_AUDIO_FORMAT_PCM_16_BIT;
                session_output_config.output_config[session_output_config.num_output].bit_width = PCM_16_BITWIDTH;
            }
        }
        session_output_config.output_config[session_output_config.num_output].channels = in_channels;
        session_output_config.output_config[session_output_config.num_output].sample_rate = smpl_rate;
        session_output_config.num_output++;
    }

    if (enable_hp) {
        session_output_config.output_config[session_output_config.num_output].id = AUDIO_DEVICE_OUT_LINE;
        session_output_config.output_config[session_output_config.num_output].channels = popcount(AUDIO_CHANNEL_OUT_STEREO);
        session_output_config.output_config[session_output_config.num_output].sample_rate = smpl_rate;
        if (bitwidth == PCM_24_BITWIDTH) {
            session_output_config.output_config[session_output_config.num_output].format = QAP_AUDIO_FORMAT_PCM_24_BIT_PACKED;
            session_output_config.output_config[session_output_config.num_output].bit_width = PCM_24_BITWIDTH;
        } else {
            session_output_config.output_config[session_output_config.num_output].format = QAP_AUDIO_FORMAT_PCM_16_BIT;
            session_output_config.output_config[session_output_config.num_output].bit_width = PCM_16_BITWIDTH;
        }
        session_output_config.num_output++;
    }
    if (enable_spk) {
        session_output_config.output_config[session_output_config.num_output].channels = popcount(AUDIO_CHANNEL_OUT_STEREO);
        session_output_config.output_config[session_output_config.num_output].id = AUDIO_DEVICE_OUT_SPEAKER;
        session_output_config.output_config[session_output_config.num_output].sample_rate = smpl_rate;
        if (bitwidth == PCM_24_BITWIDTH) {
            session_output_config.output_config[session_output_config.num_output].format = QAP_AUDIO_FORMAT_PCM_24_BIT_PACKED;
            session_output_config.output_config[session_output_config.num_output].bit_width = PCM_24_BITWIDTH;
        } else {
            session_output_config.output_config[session_output_config.num_output].format = QAP_AUDIO_FORMAT_PCM_16_BIT;
            session_output_config.output_config[session_output_config.num_output].bit_width = PCM_16_BITWIDTH;
        }
        session_output_config.num_output++;
    }

    if (enable_ecref) {
        session_output_config.output_config[session_output_config.num_output].channels = popcount(AUDIO_CHANNEL_OUT_STEREO);
        session_output_config.output_config[session_output_config.num_output].id = AUDIO_DEVICE_OUT_PROXY;
        session_output_config.output_config[session_output_config.num_output].sample_rate = smpl_rate;
        if (bitwidth == PCM_24_BITWIDTH) {
            session_output_config.output_config[session_output_config.num_output].format = QAP_AUDIO_FORMAT_PCM_24_BIT_PACKED;
            session_output_config.output_config[session_output_config.num_output].bit_width = PCM_24_BITWIDTH;
        } else {
            session_output_config.output_config[session_output_config.num_output].format = QAP_AUDIO_FORMAT_PCM_16_BIT;
            session_output_config.output_config[session_output_config.num_output].bit_width = PCM_16_BITWIDTH;
        }
        session_output_config.num_output++;
    }

    ALOGV("%s:%d num_output = %d", __func__, __LINE__, session_output_config.num_output);
    return;
}

static void update_kvpairs_for_encode(int out_device_id) {
    uint8_t device_id;
    char command[64];
    if (out_device_id == AUDIO_DEVICE_OUT_HDMI)
        device_id = 8;
    switch (render_format) {
        case 1:
            sprintf(command, "o_device=%d;od=;", device_id);
            strcat(session_kv_pairs, command);
            encode = true;
            break;
        case 2:
            sprintf(command, "o_device=%d;odp=;", device_id);
            strcat(session_kv_pairs, command);
            encode = true;
            break;
        case 3:
            sprintf(command, "o_device=%d;render_format=odts;enabletransencode;", device_id);
            strcat(session_kv_pairs, command);
            encode = true;
            break;
        default:
            encode = false;
            break;
    }
    ALOGV("%s::%d output device %d and session set params %s", __func__, __LINE__, out_device_id, session_kv_pairs);
    return;
}

static void qap_wrapper_create_multi_channel_dump(char *path) {
    fp_output_writer_hdmi = fopen(path,"wb");
    if (fp_output_writer_hdmi)
        fprintf(stdout, "output file ::%s has been generated.\n", path);
    else
        fprintf(stderr, "Failed open hdmi dump file\n");
}

/*
 * cold_time_latency is the time difference between the session open
 * and the first data output received from decoder.
 *
 * cont_time_latency is the avg time taken to decode the input buffers
 * i.e. avg of the time differences between the time the input buffer feeded
 * and the time the o/p buffer received
 */
static void qap_wrapper_measure_kpi_values(double cold_start, double cold_stop)
{
    int i=0, m=0, j=0;
    int cnt = 0;
    double syst_time_arr[TIMESTAMP_ARRAY_SIZE];
    double total_lat = 0;
    double cold_time_latency, cont_time_latency;
    cold_time_latency = cold_stop - cold_start;
    memset(syst_time_arr,0, sizeof(syst_time_arr));

    if (data_write_count > TIMESTAMP_ARRAY_SIZE)
        data_write_count = TIMESTAMP_ARRAY_SIZE;
    if (data_callback_count > TIMESTAMP_ARRAY_SIZE)
        data_callback_count = TIMESTAMP_ARRAY_SIZE;

    for (i = 6; i < data_write_count; i++) {
        for(j = 6; j < ((data_callback_count < TIMESTAMP_ARRAY_SIZE) ? data_callback_count : TIMESTAMP_ARRAY_SIZE); j++) {
            if( abs(data_input_ts_arr[i] - data_callback_ts_arr[j]) <= 4000) {
                syst_time_arr[cnt++] = (data_callback_st_arr[j] - data_input_st_arr[i]);
            }
        }
    }

    for(m = 0; m < cnt; m++) {
        total_lat += syst_time_arr[m];
        ALOGV("%d system time diff %lf", __LINE__, syst_time_arr[m]);
    }
    cont_time_latency = total_lat/(cnt);
    fprintf(stdout, "cold time latency %lf ms, avg cont time latency %lf ms,"
            "total cont time latency %f ms, total count %d\n",
            cold_time_latency, cont_time_latency, total_lat, cnt);
    if (dsp_latency)
        fprintf(stdout, "Dsp latency = %lu ms \n", dsp_latency);
}

static void qap_wrapper_read_frame_size_from_file(qap_audio_buffer_t *buffer, FILE *fp_framesize)
{
    if (NULL != fp_framesize) {
        char tempstr[100];
        fgets(tempstr, sizeof(tempstr), fp_framesize);
        buffer->common_params.size = atoi(tempstr);
    }
}

static void read_bytes_timestamps_from_file(qap_audio_buffer_t *buffer, FILE *fp_timestamp, FILE *fp_input_file)
{
    if (NULL != fp_timestamp) {
        char tempstr[100] = {0};
        int seek_offset = 0;
        fgets(tempstr, sizeof(tempstr), fp_timestamp);
        printf("%s and tempstr is %s \n", __FUNCTION__,  tempstr);
        char * token = strtok(tempstr, ",");
        if (token != NULL) {
            buffer->common_params.size = atoi(token);
            if(token!= NULL) {
                token = strtok(NULL, ",");
                if (token!= NULL) {
                    buffer->common_params.timestamp = atoi(token);
                    ALOGV("%s and timestamp to be pushed to queue is %lld", __FUNCTION__, buffer->common_params.timestamp);
                }
                token = strtok(NULL, ",");
                if (token != NULL) {
                    seek_offset = atoi(token);
                    if (fp_input_file && seek_offset > 0)
                        fseek(fp_input_file, seek_offset, SEEK_CUR);
                }
            }
        } else {
            buffer->common_params.timestamp = CONTIGUOUS_TIMESTAMP;
            buffer->common_params.size = 0;
        }
    }
}

bool is_qap_session_active(int argc, char* argv[], char *kvp_string) {
    char *qap_kvp = NULL;
    char *cmd_str = NULL;
    char *tmp_str = NULL;
    int status = 0;
    cmd_str = (char *)qap_wrapper_get_cmd_string_from_arg_array(argc, argv, &status);
    if (status > 0) {
        qap_kvp = qap_wrapper_get_single_kvp("qap", cmd_str, &status);
        if (qap_kvp == NULL) {
            return false;
        }
        strncpy(kvp_string, cmd_str, strlen(cmd_str));
        if (cmd_str != NULL) {
            free(cmd_str);
            cmd_str = NULL;
        }
    }
    return true;
}

char* check_for_playlist(char *kvp_string) {
    char *file_str = NULL;
    char *tmp_str = NULL;
    char *play_list = NULL;
    int len = 0;

    tmp_str = strstr(kvp_string, "g=/");
    if (tmp_str != NULL) {
        file_str = strstr(kvp_string, ".txt");
        len = file_str - tmp_str;
        play_list = (char*) calloc(1, sizeof(char) * (len+4));
        strncpy(play_list, tmp_str+2, len+2);
    }
    return play_list;
}

int start_playback_through_qap_playlist(char *cmd_kvp_str[], int num_of_streams, char *kvp_string, stream_config stream_param[],
                                     bool qap_wrapper_session_active, qahw_module_handle_t *hal_handle) {
    stream_config *stream = NULL;
    int rc = 0;
    bool broad_cast = false, bd = false;
    int i = 0, curr_clip_type = DOLBY, prev_clip_type;

    if (strstr(kvp_string, "broadcast"))
       broad_cast = true;
    else if(strstr(kvp_string, "bd"))
       bd = true;
    do {
        fprintf(stdout, "cmd_kvp_string is %s kvp_string %s and num_of_streams %d\n", cmd_kvp_str[i], kvp_string, num_of_streams);
        stream = &stream_param[i];
        fprintf(stdout, "stream->filename is %s\n", stream->filename);
        if (stream->filename) {
            prev_clip_type = curr_clip_type;
            if ((stream->file_stream = fopen(stream->filename, "r"))== NULL) {
                fprintf(stderr, "Cannot open audio file %s\n", stream->filename);
                return -EINVAL;
            }
            if (strstr(stream->filename, ".dts")) {
                curr_clip_type = DTS;
            } else {
                curr_clip_type = DOLBY;
            }
        }
        get_file_format(stream);
        fprintf(stdout, "Playing from:%s\n", stream->filename);
        qap_module_handle_t qap_module_handle = NULL;
        if ((bd || (prev_clip_type != curr_clip_type)) && qap_wrapper_session_active) {
            fprintf(stdout, " prev_clip_type is %d curr_clip_type is %d\n", prev_clip_type, curr_clip_type);
            qap_wrapper_session_close();
            qap_wrapper_session_active = false;
        }
        if (!qap_wrapper_session_active) {
            if (broad_cast) {
                cmd_kvp_str[i] = realloc(cmd_kvp_str[i], strlen(cmd_kvp_str[i])+11);
                strcat(strcat(cmd_kvp_str[i], ";"), "broadcast");
            } else if (bd) {
                cmd_kvp_str[i] = realloc(cmd_kvp_str[i], strlen(cmd_kvp_str[i])+4);
                strcat(strcat(cmd_kvp_str[i], ";"), "bd");
            }
            rc = qap_wrapper_session_open(cmd_kvp_str[i], stream, num_of_streams, hal_handle);
            if (rc != 0) {
                fprintf(stderr, "Session Open failed\n");
                return -EINVAL;
            }
            qap_wrapper_session_active = true;
        }

        if (qap_wrapper_session_active) {
            stream->qap_module_handle = qap_wrapper_stream_open(stream);
            if (stream->qap_module_handle == NULL) {
                fprintf(stderr, "QAP Stream open Failed\n");
            } else {
                fprintf(stdout, "QAP module handle is %p and file name is %s\n", stream->qap_module_handle, stream->filename);
                qap_wrapper_start_stream(&stream_param[i]);
                free(stream->filename);
                stream->filename = NULL;
                free(cmd_kvp_str[i]);
                cmd_kvp_str[i] = NULL;
            }
        }
        i++;
        while (!primary_stream_close) {
            usleep(50000);
            fprintf(stderr, "QAP Stream not closed\n");
        }
        fprintf(stderr, "QAP Stream closed\n");
    } while (i <num_of_streams);
    if (qap_wrapper_session_active) {
        qap_wrapper_session_close();
        qap_wrapper_session_active = false;
    }
    return 0;
}

#ifdef QAP
char *qap_wrapper_get_single_kvp(const char *key, const char *kv_pairs, int *status)
{
    char *kvp = NULL;
    char *tempstr = NULL;
    char *token = NULL;
    char *context1 = NULL;
    char *context2 = NULL;
    char *temp_kvp = NULL;
    char *temp_key = NULL;

    if (NULL == key || NULL == kv_pairs) {
        *status = -EINVAL;
        return NULL;
    }
    tempstr = strdup(kv_pairs);
    token = strtok_r(tempstr, ";", &context1);
    if (token != NULL) {
        temp_kvp = strdup(token);
        if (temp_kvp != NULL) {
            temp_key = strtok_r(temp_kvp, "=", &context2);
            if (!strncmp(key, temp_key, strlen(key))) {
                kvp = calloc(1, (strlen(token) + 1) * sizeof(char));
                strncat(kvp, token, strlen(token));
                return kvp;
            }
            free(temp_kvp);
        }
        while (token != NULL) {
            token = strtok_r(NULL, ";", &context1);
            if (token != NULL) {
                temp_kvp = strdup(token);
                if (temp_kvp != NULL) {
                    temp_key = strtok_r(temp_kvp, "=", &context2);
                    if (!strncmp(key, temp_key, strlen(key))) {
                        kvp = calloc(1, (strlen(token) + 1) * sizeof(char));
                        strncat(kvp, token, strlen(token));
                        return kvp;
                    }
                    free(temp_kvp);
                    temp_kvp = NULL;
                }
            }
        }
        free(tempstr);
    }
    return NULL;
}
#endif

int *qap_wrapper_get_int_value_array(const char *kvp, int *count, int *status __unused)
{
    char *tempstr1;
    char *tempstr2;
    char *l1;
    char *l2 __unused;
    char *ctx1;
    char *ctx2 __unused;
    int *val = NULL;
    int i = 0;
    char *s;
    char *endstr;
    int temp = 0;
    char *jump;

    *count = 0;
    if (kvp == NULL) {
        return NULL;
    }
    tempstr1 = strdup(kvp);
    l1 = strtok_r(tempstr1, "=", &ctx1);
    if (l1 != NULL) {
        /* jump from key to value */
        l1 = strtok_r(NULL, "=", &ctx1);
        if (l1 != NULL) {
            tempstr2 = strdup(l1);

            s = tempstr2;
            for (i=0; s[i]; s[i]==',' ? i++ : *s++);

            temp = i;
            val = calloc(1, (i + 1)*sizeof(int));
            i = 0;
            val[i++] = strtol(tempstr2, &endstr, 0);

            while (i <= temp) {
                 jump = endstr + 1;
                val[i++] = strtol(jump, &endstr, 0);
            }
            free(tempstr2);
        }
    }
    free(tempstr1);
    *count = i;
    return val;
}

char * qap_wrapper_get_cmd_string_from_arg_array(int argc, char * argv[], int *status)
{
    char * kvps;
    int idx;
    int has_key = 0;
    int mem = 0;

    fprintf(stdout, "%s %d in", __func__, __LINE__);
    if (argc < 2 || NULL == argv) {
        fprintf(stdout, "%s %d returning EINVAL\n", __func__, __LINE__);
        *status = -EINVAL;
        return NULL;
    }

    for (idx = 0; idx < argc; idx++) {
        mem += (strlen(argv[idx]) + 2);     /* Extra byte to insert delim ';' */
    }

    if (mem > 0)
        kvps = calloc(1, mem * sizeof(char));
    else {
        *status = -EINVAL;
        fprintf(stdout, "%s %d returning EINVAL\n", __func__, __LINE__);
        return NULL;
    }

    if (NULL == kvps) {
        *status = -ENOMEM;
        fprintf(stdout, "%s %d returning EINVAL\n", __func__, __LINE__);
        return NULL;
    }

    for (idx = 1; idx < argc; idx++) {
        if (( argv[idx][0] == '-') &&
                (argv[idx][1] < '0' || argv[idx][1] > '9')) {
            if (has_key) {
                strcat(kvps, ";");
                has_key = 0;
            }
            strcat(kvps, argv[idx]+1);
            strcat(kvps, "=");
            has_key = 1;
        } else if (has_key) {
            strcat(kvps, argv[idx]);
            strcat(kvps, ";");
            has_key = 0;
        } else {
            *status = -EINVAL;
            if (kvps != NULL) {
                free(kvps);
                kvps = NULL;
            }
            fprintf(stdout, "%s %d returning EINVAL\n", __func__, __LINE__);
            return NULL;
        }
    }
    *status = mem;
    fprintf(stdout, "%s %d returning\n", __func__, __LINE__);
    return kvps;
}

static int qap_wrapper_map_input_format(audio_format_t audio_format, qap_audio_format_t *format)
{
    if (audio_format == AUDIO_FORMAT_AC3) {
        *format = QAP_AUDIO_FORMAT_AC3;
        fprintf(stdout, "File Format is AC3!\n");
    } else if (audio_format == AUDIO_FORMAT_E_AC3) {
        *format = QAP_AUDIO_FORMAT_EAC3;
        fprintf(stdout, "File Format is E_AC3!\n");
    } else if ((audio_format == AUDIO_FORMAT_AAC_ADTS_LC) ||
               (audio_format == AUDIO_FORMAT_AAC_ADTS_HE_V1) ||
               (audio_format == AUDIO_FORMAT_AAC_ADTS_HE_V2) ||
               (audio_format == AUDIO_FORMAT_AAC_LC) ||
               (audio_format == AUDIO_FORMAT_AAC_HE_V1) ||
               (audio_format == AUDIO_FORMAT_AAC_HE_V2) ||
               (audio_format == AUDIO_FORMAT_AAC_LATM_LC) ||
               (audio_format == AUDIO_FORMAT_AAC_LATM_HE_V1) ||
               (audio_format == AUDIO_FORMAT_AAC_LATM_HE_V2)) {
        *format = QAP_AUDIO_FORMAT_AAC_ADTS;
        fprintf(stdout, "File Format is AAC!\n");
    } else if (audio_format == AUDIO_FORMAT_DTS) {
        *format = QAP_AUDIO_FORMAT_DTS;
        fprintf(stdout, "File Format is DTS!\n");
    } else if (audio_format == AUDIO_FORMAT_DTS_HD) {
        *format = QAP_AUDIO_FORMAT_DTS_HD;
        fprintf(stdout, "File Format is DTS_HD!\n");
    } else if (audio_format == AUDIO_FORMAT_PCM_16_BIT) {
        *format = QAP_AUDIO_FORMAT_PCM_16_BIT;
        fprintf(stdout, "File Format is PCM_16!\n");
    } else if (audio_format == AUDIO_FORMAT_PCM_32_BIT) {
        *format = QAP_AUDIO_FORMAT_PCM_32_BIT;
        fprintf(stdout, "File Format is PCM_32!\n");
    } else if (audio_format == AUDIO_FORMAT_PCM_24_BIT_PACKED) {
        *format = QAP_AUDIO_FORMAT_PCM_24_BIT_PACKED;
        fprintf(stdout, "File Format is PCM_24!\n");
    } else if ((audio_format == AUDIO_FORMAT_PCM_8_BIT) ||
               (audio_format == AUDIO_FORMAT_PCM_8_24_BIT)) {
        *format = QAP_AUDIO_FORMAT_PCM_8_24_BIT;
        fprintf(stdout, "File Format is PCM_8_24!\n");
    } else {
        fprintf(stdout, "File Format not supported!\n");
        return -EINVAL;
    }
    return 0;
}

char *get_string_value(const char *kvp, int *status)
{
    char *tempstr1 = NULL;
    char *tempstr2 = NULL;
    char *l1;
    char *ctx1;
    if (kvp == NULL)
        return NULL;
    tempstr1 = strdup(kvp);
    l1 = strtok_r(tempstr1, "=", &ctx1);
    if (l1 != NULL) {
        /* jump from key to value */
        l1 = strtok_r(NULL, "=", &ctx1);
        if (l1 != NULL)
            tempstr2 = strdup(l1);
    }
    free(tempstr1);
    return tempstr2;
}

int qap_wrapper_write_to_hal(qahw_stream_handle_t* out_handle, char *data, size_t bytes)
{
    ssize_t ret;
    qahw_out_buffer_t out_buf;

    memset(&out_buf,0, sizeof(qahw_out_buffer_t));
    out_buf.buffer = data;
    out_buf.bytes = bytes;

    ret = qahw_out_write(out_handle, &out_buf);
    if (ret < 0)
        fprintf(stderr, "%s::%d: writing data to hal failed (ret = %zd)\n", __func__, __LINE__, ret);
    else if (ret != bytes)
        fprintf(stdout, "%s::%d provided bytes %zd, written bytes %d\n",__func__, __LINE__, bytes, ret);

    return ret;
}

static void close_output_streams()
{
    int ret;
    if (qap_out_hal_handle && qap_out_spk_handle) {
        ret = qahw_out_standby(qap_out_spk_handle);
        if (ret)
            fprintf(stderr, "%s::%d: out standby failed %d \n", __func__, __LINE__, ret);
        if (play_through_bt) {
            fprintf(stdout, "%s::%d: disconnecting BT\n", __func__, __LINE__);
            char param[100] = {0};
            snprintf(param, sizeof(param), "%s=%d", "disconnect", AUDIO_DEVICE_OUT_BLUETOOTH_A2DP);
            qahw_set_parameters(qap_out_hal_handle, param);
        }
        fprintf(stdout, "%s::%d: closing output stream\n", __func__, __LINE__);
        ret = qahw_close_output_stream(qap_out_spk_handle);
        if (ret)
            fprintf(stderr, "%s::%d: could not close output stream, error - %d\n", __func__, __LINE__, ret);
        qap_out_spk_handle = NULL;
    }
    if (qap_out_hal_handle && qap_out_hp_handle) {
        ret = qahw_out_standby(qap_out_hp_handle);
        if (ret)
            fprintf(stderr, "%s::%d: out standby failed %d \n", __func__, __LINE__, ret);
        fprintf(stdout, "%s::%d: closing output stream\n", __func__, __LINE__);
        ret = qahw_close_output_stream(qap_out_hp_handle);
        if (ret)
            fprintf(stderr, "%s::%d: could not close output stream, error - %d\n", __func__, __LINE__, ret);
        qap_out_hp_handle = NULL;
    }
    if (qap_out_hal_handle && qap_out_hdmi_handle) {
        char param[100] = {0};
        snprintf(param, sizeof(param), "%s=%d", "disconnect", AUDIO_DEVICE_OUT_HDMI);
        ret = qahw_out_standby(qap_out_hdmi_handle);
        if (ret)
            fprintf(stderr, "%s::%d: out standby failed %d\n", __func__, __LINE__, ret);
        qahw_set_parameters(qap_out_hal_handle, param);
        fprintf(stdout, "%s::%d: closing output stream\n", __func__, __LINE__);
        ret = qahw_close_output_stream(qap_out_hdmi_handle);
        if (ret)
            fprintf(stderr, "%s::%d: could not close output stream, error - %d\n", __func__, __LINE__, ret);
        qap_out_hdmi_handle = NULL;
    }
    primary_stream_close = true;
}

void qap_wrapper_module_callback(qap_module_handle_t module_handle, void* priv_data, qap_module_callback_event_t event_id, int size, void *data)
{
    stream_config *p_stream_param = (stream_config*)priv_data;
    if(p_stream_param == NULL) {
        ALOGE("%s %d, callback handle is null.",__func__,__LINE__);
    }
    ALOGV("%s %d, %s Received event id %d\n", __func__, __LINE__, p_stream_param->filename, event_id);

    switch (event_id) {
        case QAP_MODULE_CALLBACK_EVENT_SEND_INPUT_BUFFER:
        {
            if (size < sizeof(qap_send_buffer_t)) {
                ALOGE("%s %d event id %d, wrong payload size %d\n",
                      __func__, __LINE__, event_id, size);
                break;
            }
            qap_send_buffer_t *p_send_buffer_event = (qap_send_buffer_t*)data;
            pthread_mutex_lock(&p_stream_param->input_buffer_available_lock);
            p_stream_param->input_buffer_available_size = p_send_buffer_event->bytes_available;
            pthread_cond_signal(&p_stream_param->input_buffer_available_cond);
            pthread_mutex_unlock(&p_stream_param->input_buffer_available_lock);

            break;
        }
        case QAP_MODULE_CALLBACK_EVENT_INPUT_CFG_CHANGE:
        {
            if (size < sizeof(qap_input_config_t)) {
                ALOGE("%s %d event id %d, wrong payload size %d\n",
                      __func__, __LINE__, event_id, size);
                break;
            }
            qap_input_config_t *p_stream_format = (qap_input_config_t*)data;

            ALOGV(" %s %d Input format updated; sample_rate %lu, channels %lu, bitwidth %lu",
                  __func__, __LINE__,
                  p_stream_format->sample_rate,
                  p_stream_format->channels,
                  p_stream_format->bit_width);
            break;
        }
        default:
        break;
    }
}

void qap_wrapper_session_callback(qap_session_handle_t session_handle __unused, void* priv_data __unused, qap_callback_event_t event_id, int size, void *data)
{
    int ret = 0;
    int bytes_written = 0;
    int bytes_remaining = 0;
    int offset = 0;
    audio_output_flags_t flags;
    flags = (AUDIO_OUTPUT_FLAG_NON_BLOCKING |
             AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD |
             AUDIO_OUTPUT_FLAG_DIRECT);
    ALOGV("%s %d Received event id %d\n", __func__, __LINE__, event_id);
    switch (event_id) {
        case QAP_CALLBACK_EVENT_EOS:
            ALOGV("%s %d Received Main Input EOS", __func__, __LINE__);
            if (stream_cnt > 0)
                stream_cnt--;
            pthread_mutex_lock(&main_eos_lock);
            pthread_cond_signal(&main_eos_cond);
            main_eos_received = true;
            pthread_mutex_unlock(&main_eos_lock);

            if (!stream_cnt)
                close_output_streams();
            if (play_list_cnt && input_streams_count) {
                play_list_cnt--;
                input_streams_count = 0;
            }
            break;
        case QAP_CALLBACK_EVENT_EOS_ASSOC:
            if (stream_cnt > 0)
                stream_cnt--;
            //if (!has_system_input)
            {
                ALOGV("%s %d Received Secondary Input EOS", __func__, __LINE__);
                pthread_mutex_lock(&sec_eos_lock);
                pthread_cond_signal(&sec_eos_cond);
                sec_eos_received = true;
                pthread_mutex_unlock(&sec_eos_lock);
            }
            if (!stream_cnt)
                close_output_streams();
            break;
        case QAP_CALLBACK_EVENT_MAIN_2_EOS:
            if (stream_cnt > 0)
                stream_cnt--;
            //if (!has_system_input)
            {
                ALOGV("%s %d Received main2 Input EOS", __func__, __LINE__);
                pthread_mutex_lock(&main2_eos_lock);
                pthread_cond_signal(&main2_eos_cond);
                main2_eos_received = true;
                pthread_mutex_unlock(&main2_eos_lock);
            }
            if (!stream_cnt)
                close_output_streams();
            break;
        case QAP_CALLBACK_EVENT_ERROR:
            break;
        case QAP_CALLBACK_EVENT_SUCCESS:
            break;
        case QAP_CALLBACK_EVENT_METADATA:
            break;
        case QAP_CALLBACK_EVENT_OUTPUT_CFG_CHANGE:
            if (data != NULL) {
                qap_audio_buffer_t *buffer = (qap_audio_buffer_t *) data;
                qap_output_config_t *new_conf = &buffer->buffer_parms.output_buf_params.output_config;
                qap_output_config_t *cached_conf = NULL;
                int index = -1;

                ALOGV("%s %d Received Output cfg change", __func__, __LINE__);
                if (buffer) {
                    index = get_qap_session_out_config_index_for_id(
                              buffer->buffer_parms.output_buf_params.output_id);
                    if (index >= 0)
                        cached_conf = &session_output_config.output_config[index];
                }
                if (cached_conf == NULL) {
                    ALOGE("Invalid output config from QAP is reached");
                    return;
                }
                if (memcmp(cached_conf, new_conf, sizeof(qap_output_config_t)) != 0) {
                    memcpy(cached_conf, new_conf, sizeof(qap_output_config_t));
                    cached_conf->id = buffer->buffer_parms.output_buf_params.output_id;
                    is_media_fmt_changed[index] = true;
                }
            }
            break;
        case QAP_CALLBACK_EVENT_DATA:
            if (data != NULL) {
                qap_audio_buffer_t *buffer = (qap_audio_buffer_t *) data;

                if (buffer && timestamp_mode) {
                    char ch[100] = {0};
                    if (kpi_mode) {
                        if ((data_callback_count > 5) && (data_callback_count < TIMESTAMP_ARRAY_SIZE)) {
                            gettimeofday(&tcont_ts2, NULL);
                            data_callback_ts_arr[data_callback_count] = buffer->common_params.timestamp;
                            data_callback_st_arr[data_callback_count] = (tcont_ts2.tv_sec) * 1000 + (tcont_ts2.tv_usec) / 1000;
                            ALOGV("%s::%d data size %d, Kpi cont ts2 %lf, buffer timestamp %ld",
                                  __func__, __LINE__, buffer->common_params.size,
                                 data_callback_st_arr[data_callback_count], data_callback_ts_arr[data_callback_count]);
                        }
                        if (data_callback_count < TIMESTAMP_ARRAY_SIZE)
                            data_callback_count++;
                    }
                    if (fp_output_timestamp_file == NULL) {
                        fp_output_timestamp_file =
                                 fopen("/sdcard/output_timestamp_file.txt","w");
                        if(fp_output_timestamp_file) {
                            fprintf(stdout, "output file :: "
                                   "/sdcard/output_file_timestamp.txt"
                                   " has been generated.");
                            }
                    }
                    if (fp_output_timestamp_file) {
                        sprintf(ch, "%d,%lld\n", buffer->common_params.size, buffer->common_params.timestamp);
                        fprintf(stdout, "%s: %s", __func__, ch);
                        ret = fwrite((char *)&ch, sizeof(char),
                                     strlen(ch), fp_output_timestamp_file);
                        fflush(fp_output_timestamp_file);
                    }
                }

                if (buffer && buffer->common_params.data) {
                    int index = -1;
                    bool is_reopen_stream = false;
                    index = get_qap_session_out_config_index_for_id(buffer->buffer_parms.output_buf_params.output_id);
                    if (index > -1 && is_media_fmt_changed[index]) {
                        is_reopen_stream = true;
                        is_media_fmt_changed[index] = false;
                    } else if (index < 0) {
                        ALOGE("%s: No Valid Output Config found for id = %d",
                                     __func__, buffer->buffer_parms.output_buf_params.output_id);
                        break;
                    }

                    if ((buffer->buffer_parms.output_buf_params.output_id &
                            AUDIO_DEVICE_OUT_HDMI) == AUDIO_DEVICE_OUT_HDMI) {
                        if (!hdmi_connected) {
                            char param[100] = {0};
                            snprintf(param, sizeof(param), "%s=%d", "connect", AUDIO_DEVICE_OUT_HDMI);
                            qahw_set_parameters(qap_out_hal_handle, param);
                            hdmi_connected = true;
                        }
                        if (encode) {
                            if (enable_dump && fp_output_writer_hdmi == NULL) {
                                if (buffer->buffer_parms.output_buf_params.output_id ==
                                        (AUDIO_FORMAT_E_AC3|AUDIO_DEVICE_OUT_HDMI))
                                    qap_wrapper_create_multi_channel_dump("/sdcard/output_hdmi.ddp");
                                else if (buffer->buffer_parms.output_buf_params.output_id ==
                                            (AUDIO_FORMAT_AC3|AUDIO_DEVICE_OUT_HDMI))
                                    qap_wrapper_create_multi_channel_dump("/sdcard/output_hdmi.dd");
                                else
                                    qap_wrapper_create_multi_channel_dump("/sdcard/output_hdmi_dts.dts");
                            }
                        } else {
                            if (enable_dump && fp_output_writer_hdmi == NULL)
                                qap_wrapper_create_multi_channel_dump("/sdcard/output_hdmi.dump");
                        }
                        if (fp_output_writer_hdmi) {
                            ret = fwrite((unsigned char *)buffer->common_params.data, sizeof(unsigned char),
                                          buffer->common_params.size, fp_output_writer_hdmi);
                            fflush(fp_output_writer_hdmi);
                        }

                        if (is_reopen_stream && qap_out_hdmi_handle) {
                            qahw_close_output_stream(qap_out_hdmi_handle);
                            qap_out_hdmi_handle = NULL;
                            is_reopen_stream = false;
                        }

                        if (hdmi_connected && qap_out_hdmi_handle == NULL) {
                            struct audio_config config;
                            audio_devices_t devices;

                            config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
                            config.offload_info.size = AUDIO_INFO_INITIALIZER.size;
                            config.sample_rate = config.offload_info.sample_rate = DEFAULT_SAMPLE_RATE;

                            if (index > -1) {
                                if (session_output_config.output_config[index].sample_rate > 0)
                                    config.sample_rate = config.offload_info.sample_rate = session_output_config.output_config[index].sample_rate;
                                config.offload_info.channel_mask = config.channel_mask =
                                                   audio_channel_out_mask_from_count(session_output_config.output_config[index].channels);
                                if (session_output_config.output_config[index].bit_width == 24) {
                                    config.format = config.offload_info.format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
                                    config.offload_info.bit_width = 24;
                                } else {
                                    config.format = config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
                                    config.offload_info.bit_width = 16;
                                }
                                if (session_output_config.output_config[index].format == QAP_AUDIO_FORMAT_AC3)
                                    config.format = config.offload_info.format = AUDIO_FORMAT_AC3;
                                else if (session_output_config.output_config[index].format == QAP_AUDIO_FORMAT_EAC3)
                                    config.format = config.offload_info.format = AUDIO_FORMAT_E_AC3;
                                else if (session_output_config.output_config[index].format == QAP_AUDIO_FORMAT_DTS)
                                    config.format = config.offload_info.format = AUDIO_FORMAT_DTS;
                            }

                            devices = AUDIO_DEVICE_OUT_HDMI;
                            if (timestamp_mode)
                                flags |= AUDIO_OUTPUT_FLAG_TIMESTAMP;
                            if (encode) {
                                ALOGV("%s:%d output format %x", __func__, __LINE__,
                                        config.format, config.offload_info.format);
                                ret = qahw_open_output_stream(qap_out_hal_handle, qap_stream_out_cmpr_handle, devices,
                                                              flags, &config, &qap_out_hdmi_handle, "stream");
                            } else {
                                ret = qahw_open_output_stream(qap_out_hal_handle, qap_stream_out_hdmi_handle, devices,
                                                              flags, &config, &qap_out_hdmi_handle, "stream");
                                if (index > -1)
                                    set_qahw_stream_channel_map(qap_out_hdmi_handle, &session_output_config.output_config[index]);
                            }

                            ret = qahw_out_set_volume(qap_out_hdmi_handle, vol_level, vol_level);
                            if (ret < 0)
                                ALOGE("unable to set volume");
                        }
                        if (qap_out_hdmi_handle) {
                                bytes_written = qap_wrapper_write_to_hal(qap_out_hdmi_handle,
                                                    buffer->common_params.data, buffer->common_params.size);
                                if (bytes_written == -1) {
                                    ALOGE("%s::%d write failed in hal", __func__, __LINE__);
                                }
                            if (kpi_mode && data_callback_count == 6)
                                dsp_latency = qahw_out_get_latency(qap_out_hdmi_handle);
                        }
                        if (kpi_mode && data_callback_count == 1) {
                             gettimeofday(&tcold_stop, NULL);
                             cold_stop = (tcold_stop.tv_sec) * 1000 + (tcold_stop.tv_usec) / 1000;
                             ALOGD("%s::%d Measuring Kpi cold stop %lf", __func__, __LINE__, cold_stop);
                        }
                    }
                    if (buffer->buffer_parms.output_buf_params.output_id == AUDIO_DEVICE_OUT_WIRED_HEADPHONE ||
                        buffer->buffer_parms.output_buf_params.output_id == AUDIO_DEVICE_OUT_LINE) {
                        if (enable_dump && fp_output_writer_hp == NULL) {
                            fp_output_writer_hp =
                                         fopen("/sdcard/output_hp.dump","wb");
                            if (fp_output_writer_hp) {
                                fprintf(stdout, "output file :: "
                                      "/sdcard/output_hp.dump"
                                      " has been generated.\n");
                            } else {
                                fprintf(stderr, "Failed open hp dump file\n");
                            }
                        }
                        if (fp_output_writer_hp) {
                            ret = fwrite((unsigned char *)buffer->common_params.data, sizeof(unsigned char),
                                          buffer->common_params.size, fp_output_writer_hp);
                            fflush(fp_output_writer_hp);
                        }
                        if (is_reopen_stream && qap_out_hp_handle) {
                            qahw_close_output_stream(qap_out_hp_handle);
                            qap_out_hp_handle = NULL;
                            is_reopen_stream = false;
                        }

                        if (qap_out_hp_handle == NULL) {
                            struct audio_config config;
                            audio_devices_t devices;
                            config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
                            config.offload_info.size = AUDIO_INFO_INITIALIZER.size;
                            config.sample_rate = config.offload_info.sample_rate = DEFAULT_SAMPLE_RATE;
                            config.format = config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
                            config.offload_info.bit_width = 16;
                            config.offload_info.channel_mask = config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;

                           if (index > -1) {
                                config.sample_rate = config.offload_info.sample_rate = session_output_config.output_config[index].sample_rate;
                                config.offload_info.channel_mask = config.channel_mask =
                                                   audio_channel_out_mask_from_count(session_output_config.output_config[index].channels);
                                if (session_output_config.output_config[index].bit_width == 24) {
                                    config.format = config.offload_info.format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
                                    config.offload_info.bit_width = 24;
                                }
                            }

                            devices = AUDIO_DEVICE_OUT_LINE;//ToDO - Need to change to AUDIO_DEVICE_OUT_WIRED_HEADPHONE

                            if (timestamp_mode)
                                flags |= AUDIO_OUTPUT_FLAG_TIMESTAMP;
                            ret = qahw_open_output_stream(qap_out_hal_handle, qap_stream_out_hp_handle, devices,
                                 flags, &config, &qap_out_hp_handle, "stream");

                            if (ret) {
                                ALOGE("%s:%d could not open output stream, error - %d", __func__, __LINE__, ret);
                                return;
                            }
                            if (index > -1)
                                set_qahw_stream_channel_map(qap_out_hp_handle, &session_output_config.output_config[index]);
                            ret = qahw_out_set_volume(qap_out_hp_handle, vol_level, vol_level);
                            if (ret < 0)
                                 ALOGE("unable to set volume");
                        }
                        if (qap_out_hp_handle) {
                                bytes_written = qap_wrapper_write_to_hal(qap_out_hp_handle,
                                                    buffer->common_params.data, buffer->common_params.size);
                                if (bytes_written == -1) {
                                    ALOGE("%s::%d write failed in hal", __func__, __LINE__);
                                }
                            if (kpi_mode && data_callback_count == 6)
                                dsp_latency = qahw_out_get_latency(qap_out_hp_handle);
                        }
                        if (kpi_mode && data_callback_count == 1) {
                             gettimeofday(&tcold_stop, NULL);
                             cold_stop = (tcold_stop.tv_sec) * 1000 + (tcold_stop.tv_usec) / 1000;
                             ALOGD("%s::%d Measuring Kpi cold stop %lf", __func__, __LINE__, cold_stop);
                        }
                    }
                    if (buffer->buffer_parms.output_buf_params.output_id == AUDIO_DEVICE_OUT_SPEAKER) {
                        if (enable_dump && fp_output_writer_spk == NULL) {
                            char ch[4] = {0};
                            fp_output_writer_spk =
                                         fopen("/sdcard/output_speaker.dump","wb");
                            if (fp_output_writer_spk) {
                                fprintf(stdout, "output file :: "
                                      "/sdcard/output_speaker.dump"
                                      " has been generated.\n");
                                if (dolby_formats) {
                                    ret = fwrite((unsigned char *)&ch, sizeof(unsigned char),
                                                  4, fp_output_writer_spk);
                                }
                            } else {
                                fprintf(stderr, "Failed open speaker dump file\n");
                            }
                        }
                        if (fp_output_writer_spk) {
                            ret = fwrite((unsigned char *)buffer->common_params.data, sizeof(unsigned char),
                                          buffer->common_params.size, fp_output_writer_spk);
                            fflush(fp_output_writer_spk);
                        }
                        if (is_reopen_stream && qap_out_spk_handle) {
                            qahw_close_output_stream(qap_out_spk_handle);
                            qap_out_spk_handle = NULL;
                            is_reopen_stream = false;
                        }
                        if (qap_out_spk_handle == NULL) {
                            struct audio_config config;
                            audio_devices_t devices;

                            config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
                            config.offload_info.size = AUDIO_INFO_INITIALIZER.size;
                            config.sample_rate = config.offload_info.sample_rate = DEFAULT_SAMPLE_RATE;
                            config.format = config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
                            config.offload_info.bit_width = 16;
                            config.offload_info.channel_mask = config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;

                           if (index > -1) {
                                 config.sample_rate = config.offload_info.sample_rate = session_output_config.output_config[index].sample_rate;
                                 config.offload_info.channel_mask = config.channel_mask =
                                 audio_channel_out_mask_from_count(session_output_config.output_config[index].channels);
                                 if (session_output_config.output_config[index].bit_width == 24) {
                                     config.format = config.offload_info.format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
                                     config.offload_info.bit_width = 24;
                                 }
                            }

                            if (play_through_bt) {
                                fprintf(stderr, "%s::%d: connecting BT\n", __func__, __LINE__);
                                char param[100] = {0};
                                snprintf(param, sizeof(param), "%s=%d", "connect", AUDIO_DEVICE_OUT_BLUETOOTH_A2DP);
                                qahw_set_parameters(qap_out_hal_handle, param);
                                devices = AUDIO_DEVICE_OUT_BLUETOOTH_A2DP;
                            } else {
                                devices = AUDIO_DEVICE_OUT_SPEAKER;
                            }
                            if (timestamp_mode)
                                flags |= AUDIO_OUTPUT_FLAG_TIMESTAMP;
                            ALOGD("%s::%d: open output for device %d", __func__, __LINE__, devices);
                            ret = qahw_open_output_stream(qap_out_hal_handle, qap_stream_out_spk_handle, devices,
                                 flags, &config, &qap_out_spk_handle, "stream");

                            if (ret) {
                                ALOGE("%s:%d could not open output stream, error - %d", __func__, __LINE__, ret);
                                return;
                            }
                            if (index > -1)
                                set_qahw_stream_channel_map(qap_out_spk_handle, &session_output_config.output_config[index]);
                            ret = qahw_out_set_volume(qap_out_spk_handle, vol_level, vol_level);
                            if (ret < 0)
                                 ALOGE("unable to set volume");
                        }
                        if (qap_out_spk_handle) {
                                bytes_written = qap_wrapper_write_to_hal(qap_out_spk_handle,
                                                    buffer->common_params.data, buffer->common_params.size);
                                if (bytes_written == -1) {
                                    ALOGE("%s::%d write failed in hal", __func__, __LINE__);
                                }
                            if (kpi_mode && data_callback_count == 6)
                                dsp_latency = qahw_out_get_latency(qap_out_spk_handle);
                        }
                        if (kpi_mode && data_callback_count == 1) {
                             gettimeofday(&tcold_stop, NULL);
                             cold_stop = (tcold_stop.tv_sec) * 1000 + (tcold_stop.tv_usec) / 1000;
                             ALOGD("%s::%d Measuring Kpi cold stop %lf", __func__, __LINE__, cold_stop);
                        }
                    }
                    if (buffer->buffer_parms.output_buf_params.output_id == AUDIO_DEVICE_OUT_PROXY) {

                        if (fp_ecref == NULL) {
                            fp_ecref = fopen("/data/vendor/misc/audio/ecref", "w+");
                        }

                        if (fp_ecref) {
                            ALOGD("%s: write %d bytes to ecref dump",__func__,buffer->common_params.size);
                            fwrite((unsigned char *)buffer->common_params.data, 1, buffer->common_params.size, fp_ecref);
                        } else {
                            ALOGE("%s: failed to open ecref dump file",__func__);
                        }

                    }
                }
            }
            break;
        default:
            break;
    }
}

static void qap_wrapper_is_dap_enabled(char *kv_pairs, int out_device_id, qap_session_handle_t handle) {
    int status = 0;
    int temp = 0;
    char *dap_kvp = NULL;
    int *dap_value = NULL;
    int dap_enable = 0;
    qap_session_pp_configs_t dap_pp_config;
    dap_kvp = qap_wrapper_get_single_kvp("dap_enable", kv_pairs, &status);
    if (dap_kvp != NULL) {
        dap_value = qap_wrapper_get_int_value_array(dap_kvp, &temp, &status);
        if (dap_value != NULL)
            dap_enable = dap_value[0];
        if (dap_enable) {
            fprintf(stdout, "dap enable %d and device id %d\n", dap_enable, out_device_id);
            char *dev_kvp = NULL;
            if ((out_device_id & AUDIO_DEVICE_OUT_SPEAKER) == AUDIO_DEVICE_OUT_SPEAKER) {
                dev_kvp = (char *) calloc(1, status + strlen("o_device=1; "));
                if (dev_kvp != NULL) {
                    strcat(dev_kvp, "o_device=1;");
                    strcat(session_kv_pairs, dev_kvp);
                    fprintf(stdout, "session set params %s\n", session_kv_pairs);
                    free(dev_kvp);
                    dev_kvp = NULL;
                }
                dap_pp_config.pp_config[dap_pp_config.num_confs].id = AUDIO_DEVICE_OUT_SPEAKER;
                dlb_param = MS12_SESSION_CFG_DAP_ENABLE_SPEAKER;
                dap_pp_config.pp_config[dap_pp_config.num_confs].pp_type = (void *) &dlb_param;
                dap_pp_config.num_confs++;
            }
            if (((out_device_id & AUDIO_DEVICE_OUT_LINE) == AUDIO_DEVICE_OUT_LINE) ||
                ((out_device_id & AUDIO_DEVICE_OUT_WIRED_HEADPHONE) == AUDIO_DEVICE_OUT_WIRED_HEADPHONE)) {
                dev_kvp = (char *) calloc(1, status + strlen("o_device=2; "));
                if (dev_kvp != NULL) {
                    strcat(dev_kvp, "o_device=2;");
                    strcat(session_kv_pairs, dev_kvp);
                    fprintf(stdout, "session set params %s\n", session_kv_pairs);
                    free(dev_kvp);
                    dev_kvp = NULL;
                }
                if ((out_device_id & AUDIO_DEVICE_OUT_LINE) == AUDIO_DEVICE_OUT_LINE)
                dap_pp_config.pp_config[dap_pp_config.num_confs].id = AUDIO_DEVICE_OUT_LINE;
                else
                dap_pp_config.pp_config[dap_pp_config.num_confs].id = AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
                dlb_param_hp = MS12_SESSION_CFG_DAP_ENABLE_HEADPHONE;
                dap_pp_config.pp_config[dap_pp_config.num_confs].pp_type = (void *) &dlb_param_hp;
                dap_pp_config.num_confs++;
            }
            status = qap_session_cmd(qap_session_handle, QAP_SESSION_CMD_SET_PP_OUTPUTS,
                                          sizeof(qap_session_pp_configs_t), &dap_pp_config, NULL, NULL);
            if (status != QAP_STATUS_OK)
                fprintf(stderr, "Output config failed\n");
        }
        free(dap_kvp);
        dap_kvp = NULL;
    }
}

void update_qap_session_init_params(char *kv_pairs)
{
    int status = 0;
    char *kvp = NULL;
    int temp = 0;
    int *temp_val = NULL;
    uint32_t cmd_data[16] = {0};
    uint32_t cmd_size = 0;

    kvp = qap_wrapper_get_single_kvp("max_chs", kv_pairs, &status);
    if (kvp != NULL) {
        temp_val = qap_wrapper_get_int_value_array(kvp, &temp, &status);
        if (temp_val != NULL) {
            cmd_data[cmd_size++] = MS12_SESSION_CFG_MAX_CHS;
            cmd_data[cmd_size++] = temp_val[0];
            free(temp_val);
            temp_val = NULL;
        }
        free(kvp);
        kvp = NULL;
    }

    kvp = qap_wrapper_get_single_kvp("bs_out_mode", kv_pairs, &status);
    if (kvp != NULL) {
        temp_val = qap_wrapper_get_int_value_array(kvp, &temp, &status);
        if (temp_val != NULL) {
            cmd_data[cmd_size++] = MS12_SESSION_CFG_BS_OUTPUT_MODE;
            cmd_data[cmd_size++] = temp_val[0];
            free(temp_val);
            temp_val = NULL;
        }
        free(kvp);
        kvp = NULL;
    }

    kvp = qap_wrapper_get_single_kvp("chmod_locking", kv_pairs, &status);
    if (kvp != NULL) {
        temp_val = qap_wrapper_get_int_value_array(kvp, &temp, &status);
        if (temp_val != NULL) {
            cmd_data[cmd_size++] = MS12_SESSION_CFG_CHMOD_LOCKING;
            cmd_data[cmd_size++] = temp_val[0];
            free(temp_val);
            temp_val = NULL;
        }
        free(kvp);
        kvp = NULL;
    }

    kvp = qap_wrapper_get_single_kvp("dn", kv_pairs, &status);
    if (kvp != NULL) {
        temp_val = qap_wrapper_get_int_value_array(kvp, &temp, &status);
        if (temp_val != NULL) {
            cmd_data[cmd_size++] = MS12_SESSION_CFG_DIALOG_NORM;
            cmd_data[cmd_size++] = temp_val[0];
            free(temp_val);
            temp_val = NULL;
        }
        free(kvp);
        kvp = NULL;
    }

    kvp = qap_wrapper_get_single_kvp("rp", kv_pairs, &status);
    if (kvp != NULL) {
        temp_val = qap_wrapper_get_int_value_array(kvp, &temp, &status);
        if (temp_val != NULL) {
            cmd_data[cmd_size++] = MS12_SESSION_CFG_COMPR_PROF;
            cmd_data[cmd_size++] = temp_val[0];
            free(temp_val);
            temp_val = NULL;
        }
        free(kvp);
        kvp = NULL;
    }

    if (!cmd_size) {
        return;
    }

    temp = qap_session_cmd(qap_session_handle,
            QAP_SESSION_CMD_SET_PARAM,
            cmd_size * sizeof(uint32_t),
            &cmd_data[0],
            NULL,
            NULL);
    if (temp != QAP_STATUS_OK) {
        fprintf(stderr, "session init config failed\n");
    }
}

int qap_wrapper_session_open(char *kv_pairs, void* stream_data, int num_of_streams,  qahw_module_handle_t *hal_handle)
{
    int status = 0;
    int ret = 0;
    int i;
    int temp = 0;
    stream_config *stream = (stream_config *)stream_data;
    char *session_type_kvp = NULL;
    char *encode_kvp = NULL;
    int *temp_val = NULL;
    char *bitwidth_kvp = NULL;
    int out_bitwidth = PCM_16_BITWIDTH;
    int out_sample_rate = DEFAULT_SAMPLE_RATE;

    qap_out_hal_handle = hal_handle;
    if (kpi_mode) {
        memset(data_input_st_arr, 0, sizeof(data_input_st_arr));
        memset(data_input_ts_arr, 0, sizeof(data_input_ts_arr));
        memset(data_callback_st_arr, 0, sizeof(data_callback_st_arr));
        memset(data_callback_ts_arr, 0, sizeof(data_callback_ts_arr));
        gettimeofday(&tcold_start, NULL);
        cold_start = (tcold_start.tv_sec) * 1000 + (tcold_start.tv_usec) / 1000;
        ALOGD("%s::%d Measuring Kpi cold start %lf", __func__, __LINE__, cold_start);
    }
    if (play_list)
        play_list_cnt = num_of_streams;

    memset(&session_output_config, 0, sizeof(session_output_config));
    strcpy(session_kv_pairs, kv_pairs);
    ALOGV("%s session_kv_pairs = %s", __func__, session_kv_pairs);
    if (NULL != (session_type_kvp = qap_wrapper_get_single_kvp("broadcast", kv_pairs, &status))) {
        session_type = SESSION_BROADCAST;
        fprintf(stdout, "Session Type is Broadcast\n");
        free(session_type_kvp);
        session_type_kvp = NULL;
    } else if (NULL != (session_type_kvp = qap_wrapper_get_single_kvp("bd", kv_pairs, &status))) {
        session_type = SESSION_BLURAY;
        free(session_type_kvp);
        session_type_kvp = NULL;
        fprintf(stdout, "Session Type is Bluray\n");
    }

    if (session_type == SESSION_BLURAY) {
        if ((stream->filetype == FILE_WAV) ||
            (stream->filetype == FILE_AAC)) {
            fprintf(stderr, "Format is not supported for BD usecase\n");
            return -EINVAL;
        }
        if (!play_list && num_of_streams > 1) {
            fprintf(stderr, "Please specifiy proper session type\n");
            return -EINVAL;
        }
    }

    if (stream->filetype == FILE_DTS && (NULL == m8_lib_handle)) {
        m8_lib_handle = (qap_session_handle_t) qap_load_library(QAC_LIB_M8);
        if (m8_lib_handle == NULL) {
            fprintf(stdout, "Failed to load M8 library\n");
            return -EINVAL;
        }
        fprintf(stdout, "loaded M8 library\n");
        dolby_formats = false;
    } else if ((stream->filetype == FILE_AC3) ||
                (stream->filetype == FILE_EAC3) ||
                (stream->filetype == FILE_EAC3_JOC) ||
                (stream->filetype == FILE_WAV) ||
                (stream->filetype == FILE_AAC) ||
                (stream->filetype == FILE_AAC_ADTS) ||
                (stream->filetype == FILE_AAC_LATM) && (NULL == ms12_lib_handle)) {
        ms12_lib_handle = (qap_session_handle_t) qap_load_library(QAC_LIB_MS12);
        if (ms12_lib_handle == NULL) {
            fprintf(stderr, "Failed to load MS12 library\n");
            return -EINVAL;
        }
        dolby_formats = true;
    }


    // To-Do - Need to check SPDIF out also when SPDIF out is supported
    ALOGD("%s::%d output device %d", __func__, __LINE__, stream->output_device);

    bitwidth_kvp = qap_wrapper_get_single_kvp("bitwidth", kv_pairs, &status);
    if (bitwidth_kvp != NULL) {
        temp_val = qap_wrapper_get_int_value_array(bitwidth_kvp, &temp, &status);
        if (temp_val != NULL) {
            if (stream->filetype == FILE_DTS)
                out_bitwidth = temp_val[0];
            free(temp_val);
            temp_val = NULL;
        }
        free(bitwidth_kvp);
        bitwidth_kvp = NULL;
    }

    if ((session_type == SESSION_BROADCAST) && dolby_formats) {
        fprintf(stdout, "%s::%d Setting BROADCAST session for dolby formats\n", __func__, __LINE__);
        qap_session_handle = (qap_session_handle_t) qap_session_open(QAP_SESSION_MS12_OTT, ms12_lib_handle);
        if (qap_session_handle == NULL)
            return -EINVAL;
    } else if ((session_type == SESSION_BROADCAST) && !dolby_formats) {
        fprintf(stdout, "%s::%d Setting BROADCAST session for dts formats\n", __func__, __LINE__);
        qap_session_handle = (qap_session_handle_t) qap_session_open(QAP_SESSION_BROADCAST, m8_lib_handle);
        if (qap_session_handle == NULL)
            return -EINVAL;
    } else if (session_type == SESSION_BLURAY) {
        fprintf(stdout, "%s::%d Setting BD session\n", __func__, __LINE__);
        if (!encode && dolby_formats) {
            fprintf(stdout, "%s::%d Setting BD session for decoding dolby formats\n", __func__, __LINE__);
            qap_session_handle = (qap_session_handle_t) qap_session_open(QAP_SESSION_DECODE_ONLY, ms12_lib_handle);
            if (qap_session_handle == NULL)
                return -EINVAL;
        } else if (!encode && !dolby_formats) {
            fprintf(stdout, "%s::%d Setting BD session for decoding dts formats \n", __func__, __LINE__, qap_session_handle);
            qap_session_handle = (qap_session_handle_t) qap_session_open(QAP_SESSION_DECODE_ONLY, m8_lib_handle);
            if (qap_session_handle == NULL)
                return -EINVAL;
        } else if (encode && dolby_formats)  {
            fprintf(stdout, "%s::%d Setting BD session for encoding dolby formats \n", __func__, __LINE__, qap_session_handle);
            qap_session_handle = (qap_session_handle_t) qap_session_open(QAP_SESSION_ENCODE_ONLY, ms12_lib_handle);
            if (qap_session_handle == NULL)
                return -EINVAL;
        } else if (encode && !dolby_formats) {
            fprintf(stdout, "%s::%d Setting BD session for encoding dts formats \n", __func__, __LINE__, qap_session_handle);
            qap_session_handle = (qap_session_handle_t) qap_session_open(QAP_SESSION_ENCODE_ONLY, m8_lib_handle);
            if (qap_session_handle == NULL)
                return -EINVAL;
        }
    }

    ret = qap_session_set_callback(qap_session_handle, &qap_wrapper_session_callback, NULL);
    if (ret != QAP_STATUS_OK) {
        fprintf(stderr, "!!!! Please specify appropriate Session\n");
        return -EINVAL;
    }

    if (dolby_formats) {
        update_qap_session_init_params(kv_pairs);
    }

    if (!session_output_configured) {
        if (session_type != SESSION_BROADCAST)
            out_sample_rate = stream->config.sample_rate;;

        output_device_id = stream->output_device;
        if (output_device_id & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP) {
            output_device_id |= AUDIO_DEVICE_OUT_SPEAKER;
            play_through_bt = true;
        }
        update_session_outputs_config(render_format, stream->channels, out_bitwidth, out_sample_rate);
        ret = qap_session_cmd(qap_session_handle, QAP_SESSION_CMD_SET_OUTPUTS, sizeof(session_output_config), &session_output_config, NULL, NULL);
        if (ret != QAP_STATUS_OK) {
            fprintf(stderr, "Output config failed\n");
            return -EINVAL;
        }
        qap_wrapper_is_dap_enabled(kv_pairs, stream->output_device, qap_session_handle);

        bitwidth_kvp = qap_wrapper_get_single_kvp("k", kv_pairs, &status);
        if (bitwidth_kvp && strncmp(bitwidth_kvp, "k=", 2) == 0) {
            fprintf(stdout, "Session set params, kvpair %s\n",&bitwidth_kvp[2]);
            ret = qap_session_cmd(qap_session_handle, QAP_SESSION_CMD_SET_KVPAIRS, (sizeof(bitwidth_kvp) - 2), &bitwidth_kvp[2], NULL, NULL);
            if (ret != QAP_STATUS_OK)
                fprintf(stderr, "Session set params failed\n");
        }
        usleep(2000);
        session_output_configured = true;
    }

    pthread_mutex_init(&main_eos_lock, (const pthread_mutexattr_t *)NULL);
    pthread_mutex_init(&main2_eos_lock, (const pthread_mutexattr_t *)NULL);
    pthread_mutex_init(&sec_eos_lock, (const pthread_mutexattr_t *)NULL);
    pthread_cond_init(&main_eos_cond, (const pthread_condattr_t *) NULL);
    pthread_cond_init(&main2_eos_cond, (const pthread_condattr_t *) NULL);
    pthread_cond_init(&sec_eos_cond, (const pthread_condattr_t *) NULL);
    fprintf(stdout, "Session open returing success\n");
    return 0;
}

int qap_wrapper_session_close ()
{
    ALOGD("closing QAP session");
    session_output_configured = false;
    qap_session_close(qap_session_handle);
    qap_session_handle = NULL;
    if (stream_cnt == 0) {
        if (NULL != m8_lib_handle) {
            qap_unload_library(m8_lib_handle);
            m8_lib_handle = NULL;
        }
        if (NULL != ms12_lib_handle) {
            qap_unload_library(ms12_lib_handle);
            ms12_lib_handle = NULL;
        }
    }
}

void *qap_wrapper_start_stream (void* stream_data)
{
    int ret = 0;
    qap_audio_buffer_t *buffer;
    int8_t first_read = 1;
    int bytes_wanted;
    int bytes_read;
    int bytes_consumed = 0, status = 0;;
    qap_module_handle_t qap_module_handle = NULL;
    stream_config *stream_info = (stream_config *)stream_data;
    FILE *fp_input = stream_info->file_stream;
    int is_buffer_available = 0;
    char *temp_str = NULL;
    void *reply_data;
    char* temp_ptr = NULL;
    qap_audio_format_t format;

    if (fp_input == NULL) {
        fprintf(stderr, "Open File Failed for %s\n", stream_info->filename);
        return NULL;
    }
    qap_module_handle = stream_info->qap_module_handle;
    buffer = (qap_audio_buffer_t *) calloc(1, sizeof(qap_audio_buffer_t));
    if (buffer == NULL) {
        fprintf(stderr, "%s::%d: Memory Alloc Error\n", __func__, __LINE__);
        return NULL;
    }
    buffer->common_params.data = calloc(1, FRAME_SIZE);
    if (buffer->common_params.data == NULL) {
        fprintf(stderr, "%s::%d: Memory Alloc Error\n", __func__, __LINE__);
        if (NULL != buffer) {
            free( buffer);
            buffer = NULL;
        }
        return NULL;
    }
    buffer->buffer_parms.output_buf_params.output_id = output_device_id;
    fprintf(stdout, "%s::%d: output device id %d\n",
                __func__, __LINE__, buffer->buffer_parms.output_buf_params.output_id);

    fprintf(stdout, "Opened Input File %s format %d handle %p\n", stream_info->filename, format, fp_input);

    ret = qap_module_cmd(qap_module_handle, QAP_MODULE_CMD_START, sizeof(QAP_MODULE_CMD_START), NULL, NULL, NULL);
    if (ret != QAP_STATUS_OK) {
        fprintf(stderr, "START failed\n");
        if (NULL != buffer &&  NULL != buffer->common_params.data) {
            free( buffer->common_params.data);
            buffer->common_params.data = NULL;
            free( buffer);
            buffer = NULL;
        }
        return NULL;
    }

    do {
        if (stream_info->filetype == FILE_WAV) {
            if (first_read) {
                first_read = 0;
                int wav_header_len = get_wav_header_length(stream_info->file_stream);
                fseek(fp_input, wav_header_len, SEEK_SET);
            }
            if (stream_info->channels > 6)
                stream_info->bytes_to_read = (FRAME_SIZE_FOR_2CH_PCM * 4);
            else
                stream_info->bytes_to_read = (FRAME_SIZE_FOR_2CH_PCM * 3);
        }
        buffer->buffer_parms.input_buf_params.flags = QAP_BUFFER_NO_TSTAMP;
        buffer->common_params.timestamp = QAP_BUFFER_NO_TSTAMP;
        buffer->common_params.size = stream_info->bytes_to_read;

        if (stream_info->timestamp_filename != NULL) {
            if (!stream_info->timestamp_file_ptr) {
                stream_info->timestamp_file_ptr = fopen(stream_info->timestamp_filename, "r");
                if (!stream_info->timestamp_file_ptr) {
                    fprintf(stderr, "Cannot open audio file %s\n", stream_info->filename);
                    goto exit;
                }
            }
            read_bytes_timestamps_from_file(buffer, stream_info->timestamp_file_ptr, fp_input);
            if (buffer->common_params.timestamp == CONTIGUOUS_TIMESTAMP)
                buffer->buffer_parms.input_buf_params.flags = QAP_BUFFER_TSTAMP_CONTINUE;
            else
                buffer->buffer_parms.input_buf_params.flags = QAP_BUFFER_TSTAMP;
            timestamp_mode = true;
        }

        bytes_wanted = buffer->common_params.size;
        bytes_read = fread(buffer->common_params.data, sizeof(unsigned char), bytes_wanted, fp_input);

        buffer->common_params.offset = 0;
        buffer->common_params.size = bytes_read;
        //memcpy(buffer->common_params.data, data_buf, bytes_read);
        if (bytes_read <= 0 || stop_playback) {
            buffer->buffer_parms.input_buf_params.flags = QAP_BUFFER_EOS;
            bytes_consumed = qap_module_process(qap_module_handle, buffer);
            if (stop_playback)
                qap_module_cmd(qap_module_handle, QAP_MODULE_CMD_FLUSH, sizeof(QAP_MODULE_CMD_FLUSH), NULL, NULL, NULL);

            ret = qap_module_cmd(qap_module_handle, QAP_MODULE_CMD_STOP, sizeof(QAP_MODULE_CMD_STOP), NULL, NULL, NULL);
            fprintf(stdout, "Stopped feeding input %s : %p\n", stream_info->filename, fp_input);
            ALOGV("Stopped feeding input %s : %p", stream_info->filename, fp_input);
            break;
        }

        reply_data = (char*) calloc(1, 100);
        is_buffer_available = 0;
        temp_ptr = buffer->common_params.data;
        int time_index = data_write_count;
        if (kpi_mode) {
            if (data_write_count > 5 && data_write_count < TIMESTAMP_ARRAY_SIZE) {
                gettimeofday(&tcont_ts1, NULL);
                data_input_ts_arr[data_write_count] = buffer->common_params.timestamp;
                data_input_st_arr[data_write_count] = (tcont_ts1.tv_sec) * 1000 + (tcont_ts1.tv_usec) / 1000;
                ALOGV("%s::%d Kpi cont ts1 %lf, buffer timestamp %ld count %d",
                      __func__, __LINE__, data_input_st_arr[data_write_count], data_input_ts_arr[data_write_count], data_write_count);
            }
            if (data_write_count < TIMESTAMP_ARRAY_SIZE)
                data_write_count++;
        }
        do {
            bytes_consumed = qap_module_process(qap_module_handle, buffer);
            if (bytes_consumed > 0) {
                buffer->common_params.data += bytes_consumed;
                buffer->common_params.size -= bytes_consumed;
            }
            ALOGV("%s %d, %s feeding Input of size %d  and bytes_cosumed is %d",
                      __FUNCTION__, __LINE__,stream_info->filename, bytes_read, bytes_consumed);
            {
                if (bytes_consumed < 0) {
                    pthread_mutex_lock(&stream_info->input_buffer_available_lock);
                    stream_info->input_buffer_available_size = 0;
                    pthread_mutex_unlock(&stream_info->input_buffer_available_lock);

                    while (buffer->common_params.size > stream_info->input_buffer_available_size) {
                        ALOGV("%s %d: %s waiting for input buffer availability.",
                                     __FUNCTION__, __LINE__, stream_info->filename);
                        pthread_mutex_lock(&stream_info->input_buffer_available_lock);
                        pthread_cond_wait(&stream_info->input_buffer_available_cond,
                                          &stream_info->input_buffer_available_lock);
                        pthread_mutex_unlock(&stream_info->input_buffer_available_lock);
                        ALOGV("%s %d: %s input buffer available, size %lu.",
                                     __FUNCTION__, __LINE__,
                                     stream_info->filename,
                                     stream_info->input_buffer_available_size);
                    }
                    if(kpi_mode && time_index > 5) {
                        gettimeofday(&tcont_ts1, NULL);
                        data_input_st_arr[time_index] = (tcont_ts1.tv_sec) * 1000 + (tcont_ts1.tv_usec) / 1000;
                    }
                }
            }
        } while (buffer->common_params.size > 0 && !stop_playback);
        if (reply_data)
            free(reply_data);
        buffer->common_params.data = temp_ptr;
        if (!(stream_info->system_input || stream_info->sec_input) && !(kpi_mode)) {
            usleep(5000); //To swtich between main and secondary threads incase of dual input
        }
    } while (1);

wait_for_eos:
    if (stream_info->sec_input) {
        if (!(stream_info->flags & AUDIO_OUTPUT_FLAG_ASSOCIATED)) {
            if (!main2_eos_received) {
                pthread_mutex_lock(&main2_eos_lock);
                pthread_cond_wait(&main2_eos_cond, &main2_eos_lock);
                pthread_mutex_unlock(&main2_eos_lock);
            }
            main2_eos_received = false;
            fprintf(stdout, "Received EOS event for main2 input\n");
            ALOGV("Received EOS event for main2 input\n");
        } else {
            if (!sec_eos_received) {
                pthread_mutex_lock(&sec_eos_lock);
                pthread_cond_wait(&sec_eos_cond, &sec_eos_lock);
                pthread_mutex_unlock(&sec_eos_lock);
            }
            sec_eos_received = false;
            fprintf(stdout, "Received EOS event for secondary input\n");
            ALOGV("Received EOS event for secondary input\n");
        }
    }
    if (!(stream_info->system_input || stream_info->sec_input)){
        if (!main_eos_received) {
            pthread_mutex_lock(&main_eos_lock);
            pthread_cond_wait(&main_eos_cond, &main_eos_lock);
            pthread_mutex_unlock(&main_eos_lock);
        }
        main_eos_received = false;
        fprintf(stdout, "Received EOS event for main input\n");
        ALOGV("Received EOS event for main input\n");
    }

exit:
    if (NULL != buffer &&  NULL != buffer->common_params.data) {
        free( buffer->common_params.data);
        buffer->common_params.data = NULL;
        free( buffer);
        buffer = NULL;
    }
    qap_module_deinit(qap_module_handle);
    if ((true == play_list) && (0 == play_list_cnt) && qap_out_hal_handle) {
         ALOGV("%s %d QAP_CALLBACK_EVENT_EOS for play list received", __func__, __LINE__);
         qap_out_hal_handle = NULL;
    } else if (!play_list && qap_out_hal_handle) {
         ALOGV("%s %d QAP_CALLBACK_EVENT_EOS received", __func__, __LINE__);
         qap_out_hal_handle = NULL;
    }
    if (kpi_mode) {
        qap_wrapper_measure_kpi_values(cold_start, cold_stop);
    }
    fprintf(stdout, "%s::%d , THREAD EXIT \n", __func__, __LINE__);
    ALOGD("%s::%d , THREAD EXIT \n", __func__, __LINE__);
    return NULL;
}

qap_module_handle_t qap_wrapper_stream_open(void* stream_data)
{
    qap_module_config_t input_config = {0};
    int ret = 0;
    int i = 0;
    stream_config *stream_info = (stream_config *)stream_data;
    qap_module_handle_t qap_module_handle = NULL;

    input_config.sample_rate = stream_info->config.sample_rate;
    input_config.channels = stream_info->channels;
    input_config.bit_width = stream_info->config.offload_info.bit_width;

    if (stream_info->filetype == FILE_DTS)
        stream_info->bytes_to_read = FRAME_SIZE;
    else
        stream_info->bytes_to_read = 1024;
    input_streams_count++;

    if (stream_info->filetype == FILE_WAV) {
        switch (stream_info->flags)
        {
            case QAP_MODULE_FLAG_SYSTEM_SOUND:
                ALOGV("%s::%d Set System Sound Flag", __func__, __LINE__);
                break;
            case QAP_MODULE_FLAG_APP_SOUND:
                ALOGV("%s::%d Set System APP Flag", __func__, __LINE__);
                break;
            case QAP_MODULE_FLAG_OTT_SOUND:
                ALOGV("%s::%d Set OTT Sound Flag", __func__, __LINE__);
                break;
            default:
                ALOGE("%s::%d unsupported flag for PCM input.", __func__, __LINE__);
                return NULL;
        }
        input_config.flags = stream_info->flags;
        stream_info->system_input = true;
        has_system_input = true;
    } else {
        if (input_streams_count > 1) {
            if (stream_info->flags & AUDIO_OUTPUT_FLAG_ASSOCIATED) {
                ALOGV("%s::%d Set Secondary Assoc Input Flag", __func__, __LINE__);
                input_config.flags = QAP_MODULE_FLAG_SECONDARY;
                stream_info->sec_input = true;
            } else {
                ALOGV("%s::%d Set Secondary Main Input Flag", __func__, __LINE__);
                input_config.flags = QAP_MODULE_FLAG_PRIMARY;
                stream_info->sec_input = true;
            }
            stream_info->bytes_to_read = 2048;
        } else {
            if (stream_info->flags & AUDIO_OUTPUT_FLAG_ASSOCIATED) {
                ALOGV("%s::%d Set Secondary Assoc Input Flag", __func__, __LINE__);
                input_config.flags = QAP_MODULE_FLAG_SECONDARY;
                stream_info->sec_input = true;
            } else {
                ALOGV("%s::%d Set Primary Main Input Flag", __func__, __LINE__);
                input_config.flags = QAP_MODULE_FLAG_PRIMARY;
            }
        }
    }

    if (!encode)
        input_config.module_type = QAP_MODULE_DECODER;
    else
        input_config.module_type = QAP_MODULE_ENCODER;

    ret = qap_wrapper_map_input_format(stream_info->config.offload_info.format, &input_config.format);
    if (ret == -EINVAL)
        return NULL;

    ret = qap_module_init(qap_session_handle, &input_config, &qap_module_handle);
    if (qap_module_handle == NULL) {
        fprintf(stderr, "%s Module Handle is Null\n", __func__);
        return NULL;
    }

    qap_module_set_callback(qap_module_handle, &qap_wrapper_module_callback, stream_info);

    primary_stream_close = false;
    stream_cnt++;
    return qap_module_handle;

}
void get_play_list(FILE *fp, stream_config (*stream_param)[], int *num_of_streams, char *kvp_str[])
{
    char *token = NULL;
    char *strings[100] = {NULL};
    char cmd_str[1024] = {0};
    char *tmp_str = NULL;
    int i = 0;

    do {
        int j = 0, cnt = 0, status = 0;

        if (fgets(cmd_str, sizeof(cmd_str), fp) != NULL)
            tmp_str = strdup(cmd_str);
        else
            break;
        fprintf(stdout, "%s %d tmp_str is %s", __FUNCTION__, __LINE__, tmp_str);
        token = strtok(tmp_str, " ");
        if (NULL != token) {
            strings[cnt++] = strdup("playlist");
            strings[cnt++] = strdup(token);
            while (NULL != (token = strtok(NULL, " "))) {
                strings[cnt] = strdup(token);
                ALOGV("%s %d strings[%d] is %s", __FUNCTION__, __LINE__, cnt, strings[cnt]);
                cnt++;
            }
            for (j = 0;j< cnt;j++) {
                if (!strncmp(strings[j], "-f", 2)) {
                   (*stream_param)[i].filename = strdup(strings[j+1]);
                } else if (!strncmp(strings[j], "-r", 2)) {
                    (*stream_param)[i].config.offload_info.sample_rate = atoi(strings[j+1]);
                    (*stream_param)[i].config.sample_rate = atoi(strings[j+1]);
                } else if (!strncmp(strings[j], "-c", 2)) {
                   (*stream_param)[i].channels = atoi(strings[j+1]);
                   (*stream_param)[i].config.channel_mask = audio_channel_out_mask_from_count(atoi(strings[j+1]));
                } else if (!strncmp(strings[j], "-b", 2)) {
                    (*stream_param)[i].config.offload_info.bit_width = atoi(strings[j+1]);
                } else if (!strncmp(strings[j], "-d", 2)) {
                    (*stream_param)[i].output_device = atoll(strings[j+1]);
                } else if (!strncmp(strings[j], "-t", 2)) {
                    (*stream_param)[i].filetype = atoi(strings[j+1]);
                } else if (!strncmp(strings[j], "-a", 2)) {
                    (*stream_param)[i].aac_fmt_type = atoi(strings[j+1]);
                }
            }
            free(tmp_str);
            tmp_str = NULL;
        }
        if(NULL != (*stream_param)[i].filename) {
            *num_of_streams = i+1;
            play_list = true;
            kvp_str[i] = (char *)qap_wrapper_get_cmd_string_from_arg_array(cnt, strings, &status);
            ALOGV("%s %d kvp_str[%d] is %s", __FUNCTION__, __LINE__, i, kvp_str[i]);
        }
        for (j=0; j < cnt; j++) {
           if (NULL != strings[j]){
               free(strings[j]);
               strings[j] = NULL;
           }
        }
        i++;
    }while(NULL != cmd_str);

    return;
}

void hal_test_qap_usage() {
    printf(" \n qap commands \n");
    printf(" -qap                                      - Enabling playback through QAP for nun tunnel decoding mode\n");
    printf(" -bd                                       - Enabling Broadcast Decode/Encode session through QAP\n");
    printf(" -broadcast                                - Enabling playback through QAP for nun tunnel decoding mode\n");
    printf(" -y  --timestamp filename                  - Input timestamp file to be used to send timestamp and bytes to be read from main input file.\n");
    printf(" -z  --framesize filename                  - Input framesize file to be used to send bytes to be read from main input file.\n");
    printf(" hal_play_test -qap -broadcast -f /data/5ch_dd_25fps_channeld_id.ac3 -t 9 -d 2 -v 0.01 -r 48000 -c 6 \n");
    printf("                                          -> plays AC3 stream(-t = 9) on speaker device(-d = 2)\n");
    printf("                                          -> 6 channels and 48000 sample rate\n\n");
    printf("                                          -> using QAP with Broadcast session\n\n");
    printf(" hal_play_test -qap -bd -f /data/200_48_16_ieq_mix_voice_40s.ec3 -t 11 -d 2 -v 0.01 -r 48000 -c 2 \n");
    printf("                                          -> plays EAC3 stream(-t = 11) on speaker device(-d = 2)\n");
    printf("                                          -> 2 channels and 48000 sample rate\n\n");
    printf("                                          -> using QAP with Bluray session\n\n");
}
