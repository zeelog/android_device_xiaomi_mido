/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
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

/* Test app to play audio at the HAL layer */

#include "qahw_playback_test.h"

#define nullptr NULL

#define LATENCY_NODE "/sys/kernel/debug/audio_out_latency_measurement_node"
#define LATENCY_NODE_INIT_STR "1"

#define AFE_PROXY_SAMPLING_RATE 48000
#define AFE_PROXY_CHANNEL_COUNT 2

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

#define KV_PAIR_MAX_LENGTH  1000

#define FORMAT_PCM 1
#define WAV_HEADER_LENGTH_MAX 128

#define MAX_PLAYBACK_STREAMS   105 //This value is changed to suppport 100 clips in playlist
#define PRIMARY_STREAM_INDEX   0

#define KVPAIRS_MAX 100
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[1]))

#define FORMAT_DESCRIPTOR_SIZE 12
#define SUBCHUNK1_SIZE(x) ((8) + (x))
#define SUBCHUNK2_SIZE 8

#define DEFAULT_PRESET_STRENGTH -1

#define DTSHD_CHUNK_HEADER_KEYWORD "DTSHDHDR"
#define DTSHD_CHUNK_STREAM_KEYWORD "STRMDATA"
#define DTSHD_META_KEYWORD_SIZE 8 /*in bytes */

#ifndef AUDIO_OUTPUT_FLAG_MAIN
#define AUDIO_OUTPUT_FLAG_MAIN 0x8000000
#endif

static ssize_t get_bytes_to_read(FILE* file, int filetype);
static void init_streams(void);
int pthread_cancel(pthread_t thread);

struct wav_header {
    uint32_t riff_id;
    uint32_t riff_sz;
    uint32_t riff_fmt;
    uint32_t fmt_id;
    uint32_t fmt_sz;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_id;
    uint32_t data_sz;
};

struct proxy_data {
    struct audio_config_params acp;
    struct wav_header hdr;
};

struct drift_data {
    qahw_module_handle_t *out_handle;
    bool enable_drift_correction;
    volatile bool thread_exit;
};

struct event_data {
    uint32_t version;
    uint32_t num_events;
    uint32_t event_id;
    uint32_t module_id;
    uint16_t instance_id;
    uint16_t reserved;
    uint32_t config_mask;
};

/* Lock for dual main usecase */
pthread_cond_t dual_main_cond;
pthread_mutex_t dual_main_lock;
bool is_dual_main = false;

qahw_module_handle_t *primary_hal_handle = NULL;
qahw_module_handle_t *usb_hal_handle = NULL;
qahw_module_handle_t *bt_hal_handle = NULL;


FILE * log_file = NULL;
volatile bool stop_playback = false;
const char *log_filename = NULL;
struct proxy_data proxy_params;
pthread_t playback_thread[MAX_PLAYBACK_STREAMS];
bool thread_active[MAX_PLAYBACK_STREAMS] = { false };
bool qap_wrapper_session_active = false;

stream_config stream_param[MAX_PLAYBACK_STREAMS];
bool event_trigger;

/*
 * Set to a high number so it doesn't interfere with existing stream handles
 */
audio_io_handle_t stream_handle = 0x999;

#define FLAC_KVPAIR "music_offload_avg_bit_rate=%d;" \
                    "music_offload_flac_max_blk_size=%d;" \
                    "music_offload_flac_max_frame_size=%d;" \
                    "music_offload_flac_min_blk_size=%d;" \
                    "music_offload_flac_min_frame_size=%d;" \
                    "music_offload_sample_rate=%d;"

#define ALAC_KVPAIR "music_offload_alac_avg_bit_rate=%d;" \
                    "music_offload_alac_bit_depth=%d;" \
                    "music_offload_alac_channel_layout_tag=%d;" \
                    "music_offload_alac_compatible_version=%d;" \
                    "music_offload_alac_frame_length=%d;" \
                    "music_offload_alac_kb=%d;" \
                    "music_offload_alac_max_frame_bytes=%d;" \
                    "music_offload_alac_max_run=%d;" \
                    "music_offload_alac_mb=%d;" \
                    "music_offload_alac_num_channels=%d;" \
                    "music_offload_alac_pb=%d;" \
                    "music_offload_alac_sampling_rate=%d;" \
                    "music_offload_avg_bit_rate=%d;" \
                    "music_offload_sample_rate=%d;"

#define VORBIS_KVPAIR "music_offload_avg_bit_rate=%d;" \
                      "music_offload_sample_rate=%d;" \
                      "music_offload_vorbis_bitstream_fmt=%d;"

#define WMA_KVPAIR "music_offload_avg_bit_rate=%d;" \
                   "music_offload_sample_rate=%d;" \
                   "music_offload_wma_bit_per_sample=%d;" \
                   "music_offload_wma_block_align=%d;" \
                   "music_offload_wma_channel_mask=%d;" \
                   "music_offload_wma_encode_option=%d;" \
                   "music_offload_wma_encode_option1=%d;" \
                   "music_offload_wma_encode_option2=%d;" \
                   "music_offload_wma_format_tag=%d;"

#define APE_KVPAIR "music_offload_ape_bits_per_sample=%d;" \
                   "music_offload_ape_blocks_per_frame=%d;" \
                   "music_offload_ape_compatible_version=%d;" \
                   "music_offload_ape_compression_level=%d;" \
                   "music_offload_ape_final_frame_blocks=%d;" \
                   "music_offload_ape_format_flags=%d;" \
                   "music_offload_ape_num_channels=%d;" \
                   "music_offload_ape_sample_rate=%d;" \
                   "music_offload_ape_total_frames=%d;" \
                   "music_offload_sample_rate=%d;" \
                   "music_offload_seek_table_present=%d;"

#ifndef AUDIO_OUTPUT_FLAG_ASSOCIATED
#define AUDIO_OUTPUT_FLAG_ASSOCIATED 0x10000000
#endif

#ifndef AUDIO_OUTPUT_FLAG_INTERACTIVE
#define AUDIO_OUTPUT_FLAG_INTERACTIVE 0x4000000
#endif

static bool request_wake_lock(bool wakelock_acquired, bool enable)
{
   int system_ret;

   if (enable) {
       if (!wakelock_acquired) {
           system_ret = system("echo audio_services > /sys/power/wake_lock");
           if (system_ret < 0) {
               fprintf(stderr, "%s.Failed to acquire audio_service lock\n", __func__);
               fprintf(log_file, "%s.Failed to acquire audio_service lock\n", __func__);
           } else {
               wakelock_acquired = true;
               fprintf(log_file, "%s.Success to acquire audio_service lock\n", __func__);
           }
       } else
            fprintf(log_file, "%s.Lock is already acquired\n", __func__);
   }

   if (!enable) {
       if (wakelock_acquired) {
           system_ret = system("echo audio_services > /sys/power/wake_unlock");
           if (system_ret < 0) {
               fprintf(stderr, "%s.Failed to release audio_service lock\n", __func__);
               fprintf(log_file, "%s.Failed to release audio_service lock\n", __func__);
           } else {
               wakelock_acquired = false;
               fprintf(log_file, "%s.Success to release audio_service lock\n", __func__);
           }
       } else
            fprintf(log_file, "%s.No Lock is acquired to release\n", __func__);
   }
   return wakelock_acquired;
}

#ifndef AUDIO_FORMAT_AAC_LATM
#define AUDIO_FORMAT_AAC_LATM 0x80000000UL
#define AUDIO_FORMAT_AAC_LATM_LC (AUDIO_FORMAT_AAC_LATM | AUDIO_FORMAT_AAC_SUB_LC)
#define AUDIO_FORMAT_AAC_LATM_HE_V1 (AUDIO_FORMAT_AAC_LATM | AUDIO_FORMAT_AAC_SUB_HE_V1)
#define AUDIO_FORMAT_AAC_LATM_HE_V2 (AUDIO_FORMAT_AAC_LATM | AUDIO_FORMAT_AAC_SUB_HE_V2)
#endif


void stop_signal_handler(int signal __unused)
{
   stop_playback = true;
}

void usage();
int measure_kpi_values(qahw_stream_handle_t* out_handle, bool is_offload);
int tigger_event(qahw_stream_handle_t* out_handle);

static void init_streams(void)
{
    int i = 0;
    for ( i = 0; i < MAX_PLAYBACK_STREAMS; i++) {
        memset(&stream_param[i], 0, sizeof(stream_config));

        stream_param[i].qahw_out_hal_handle                 =   nullptr;
        stream_param[i].qahw_in_hal_handle                  =   nullptr;
        stream_param[i].filename                            =   nullptr;
        stream_param[i].file_stream                         =   nullptr;
        stream_param[i].filetype                            =   FILE_WAV;
        stream_param[i].stream_index                        =   i+1;
        stream_param[i].output_device                       =   AUDIO_DEVICE_OUT_SPEAKER;
        stream_param[i].input_device                        =   AUDIO_DEVICE_NONE;
        stream_param[i].flags                               =   AUDIO_OUTPUT_FLAG_NONE;
        stream_param[i].out_handle                          =   nullptr;
        stream_param[i].in_handle                           =   nullptr;
        stream_param[i].channels                            =   2;
        stream_param[i].config.offload_info.sample_rate     =   44100;
        stream_param[i].config.offload_info.bit_width       =   16;
        stream_param[i].aac_fmt_type                        =   AAC_LC;
        stream_param[i].wma_fmt_type                        =   WMA;
        stream_param[i].kvpair_values                       =   nullptr;
        stream_param[i].flags_set                           =   false;
        stream_param[i].usb_mode                            =   USB_MODE_DEVICE;
        stream_param[i].effect_preset_strength              =   DEFAULT_PRESET_STRENGTH;
        stream_param[i].effect_index                        =   -1;
        stream_param[i].ethread_func                        =   nullptr;
        stream_param[i].ethread_data                        =   nullptr;
        stream_param[i].device_url                          =   "stream";
        stream_param[i].play_later                          =   false;
        stream_param[i].set_params                          =   nullptr;

        pthread_mutex_init(&stream_param[i].write_lock, (const pthread_mutexattr_t *)NULL);
        pthread_cond_init(&stream_param[i].write_cond, (const pthread_condattr_t *) NULL);
        pthread_mutex_init(&stream_param[i].drain_lock, (const pthread_mutexattr_t *)NULL);
        pthread_cond_init(&stream_param[i].drain_cond, (const pthread_condattr_t *) NULL);
        pthread_mutex_init(&stream_param[i].input_buffer_available_lock, (const pthread_mutexattr_t *)NULL);
        pthread_cond_init(&stream_param[i].input_buffer_available_cond, (const pthread_condattr_t *) NULL);

        stream_param[i].handle                              =   stream_handle;
        stream_handle--;
    }
    pthread_mutex_init(&dual_main_lock, (const pthread_mutexattr_t *)NULL);
    pthread_cond_init(&dual_main_cond, (const pthread_condattr_t *) NULL);
}

static void deinit_streams(void)
{
    int i = 0;
    for ( i = 0; i < MAX_PLAYBACK_STREAMS; i++) {
        pthread_cond_destroy(&stream_param[i].write_cond);
        pthread_mutex_destroy(&stream_param[i].write_lock);
        pthread_cond_destroy(&stream_param[i].drain_cond);
        pthread_mutex_destroy(&stream_param[i].drain_lock);
        pthread_cond_destroy(&stream_param[i].input_buffer_available_cond);
        pthread_mutex_destroy(&stream_param[i].input_buffer_available_lock);
    }
    pthread_cond_destroy(&dual_main_cond);
    pthread_mutex_destroy(&dual_main_lock);
}

void read_kvpair(char *kvpair, char* kvpair_values, int filetype)
{
    char *kvpair_type = NULL;
    char *token = NULL;
    int value = 0;
    int len = 0;
    int size = 0;

    switch (filetype) {
    case FILE_FLAC:
        kvpair_type = FLAC_KVPAIR;
        break;
    case FILE_ALAC:
        kvpair_type = ALAC_KVPAIR;
        break;
    case FILE_VORBIS:
        kvpair_type = VORBIS_KVPAIR;
        break;
    case FILE_WMA:
        kvpair_type = WMA_KVPAIR;
        break;
    case FILE_APE:
        kvpair_type = APE_KVPAIR;
        break;
    default:
        break;
    }

    if (kvpair_type) {
        token = strtok(kvpair_values, ",");
        while (token) {
            len = strcspn(kvpair_type, "=");
            size = len + strlen(token) + 2;
            value = atoi(token);
            snprintf(kvpair, size, kvpair_type, value);
            kvpair += size - 1;
            kvpair_type += len + 3;
            token = strtok(NULL, ",");
        }
    }
}

int async_callback(qahw_stream_callback_event_t event, void *param,
                  void *cookie)
{
    uint32_t *payload = param;
    int i;

    if(cookie == NULL) {
        fprintf(log_file, "Invalid callback handle\n");
        fprintf(stderr, "Invalid callback handle\n");
        return 0;
    }

    stream_config *params = (stream_config*) cookie;

    switch (event) {
    case QAHW_STREAM_CBK_EVENT_WRITE_READY:
        fprintf(log_file, "stream %d: received event - QAHW_STREAM_CBK_EVENT_WRITE_READY\n", params->stream_index);

        pthread_mutex_lock(&params->write_lock);
        pthread_cond_signal(&params->write_cond);
        pthread_mutex_unlock(&params->write_lock);

        break;
    case QAHW_STREAM_CBK_EVENT_DRAIN_READY:
        fprintf(log_file, "stream %d: received event - QAHW_STREAM_CBK_EVENT_DRAIN_READY\n", params->stream_index);
        pthread_mutex_lock(&params->drain_lock);
        params->drain_received = true;
        pthread_cond_signal(&params->drain_cond);
        pthread_mutex_unlock(&params->drain_lock);
        break;
    case QAHW_STREAM_CBK_EVENT_ADSP:
        fprintf(log_file, "stream %d: received event - QAHW_STREAM_CBK_EVENT_ADSP\n", params->stream_index);
        if (payload != NULL) {
            fprintf(log_file, "event_type %d\n", payload[0]);
            fprintf(log_file, "param_length %d\n", payload[1]);
            for (i=2; i* sizeof(uint32_t) <= payload[1]; i++)
                fprintf(log_file, "param[%d] = 0x%x\n", i, payload[i]);
        }
        break;
    case QAHW_STREAM_CBK_EVENT_ERROR:
        fprintf(log_file, "stream %d: received event - QAHW_STREAM_CBK_EVENT_ERROR\n", params->stream_index);
        stop_playback = true;
        break;
    default:
        break;
    }
    return 0;
}

void *proxy_read (void* data)
{
    struct proxy_data* params = (struct proxy_data*) data;
    qahw_module_handle_t *qahw_mod_handle = params->acp.qahw_mod_handle;
    qahw_in_buffer_t in_buf;
    char *buffer = NULL;
    int rc = 0;
    int bytes_to_read, bytes_written = 0, bytes_wrote = 0;
    FILE *fp = NULL;
    qahw_stream_handle_t* in_handle = nullptr;

    rc = qahw_open_input_stream(qahw_mod_handle, params->acp.handle,
              params->acp.input_device, &params->acp.config, &in_handle,
              params->acp.flags, params->acp.kStreamName, params->acp.kInputSource);
    if (rc) {
        fprintf(log_file, "Could not open input stream %d \n",rc);
        fprintf(stderr, "Could not open input stream %d \n",rc);
        pthread_exit(0);
     }

    if (in_handle != NULL) {
        bytes_to_read = qahw_in_get_buffer_size(in_handle);
        buffer = (char *) calloc(1, bytes_to_read);
        if (buffer == NULL) {
            fprintf(log_file, "calloc failed!!\n");
            fprintf(stderr, "calloc failed!!\n");
            pthread_exit(0);
        }

        if ((fp = fopen(params->acp.file_name,"w"))== NULL) {
            fprintf(log_file, "Cannot open file to dump proxy data\n");
            fprintf(stderr, "Cannot open file to dump proxy data\n");
            free(buffer);
            pthread_exit(0);
        }
        else {
          params->hdr.num_channels = audio_channel_count_from_in_mask(params->acp.config.channel_mask);
          params->hdr.sample_rate = params->acp.config.sample_rate;
          params->hdr.byte_rate = params->hdr.sample_rate * params->hdr.num_channels * 2;
          params->hdr.block_align = params->hdr.num_channels * 2;
          params->hdr.bits_per_sample = 16;
          fwrite(&params->hdr, 1, sizeof(params->hdr), fp);
        }
        memset(&in_buf,0, sizeof(qahw_in_buffer_t));
        in_buf.buffer = buffer;
        in_buf.bytes = bytes_to_read;

        while (!(params->acp.thread_exit)) {
            rc = qahw_in_read(in_handle, &in_buf);
            if (rc > 0) {
                bytes_wrote = fwrite((char *)(in_buf.buffer), sizeof(char), (int)in_buf.bytes, fp);
                bytes_written += bytes_wrote;
                if(bytes_wrote < in_buf.bytes) {
                   stop_playback = true;
                   fprintf(log_file, "Error in fwrite due to no memory(%d)=%s\n",ferror(fp), strerror(ferror(fp)));
                   break;
                }
            }
        }
        params->hdr.data_sz = bytes_written;
        params->hdr.riff_sz = bytes_written + 36; //sizeof(hdr) - sizeof(riff_id) - sizeof(riff_sz)
        fseek(fp, 0L , SEEK_SET);
        fwrite(&params->hdr, 1, sizeof(params->hdr), fp);
        fclose(fp);
        rc = qahw_in_standby(in_handle);
        if (rc) {
            fprintf(log_file, "in standby failed %d \n", rc);
            fprintf(stderr, "in standby failed %d \n", rc);
        }
        rc = qahw_close_input_stream(in_handle);
        if (rc) {
            fprintf(log_file, "could not close input stream %d \n", rc);
            fprintf(stderr, "could not close input stream %d \n", rc);
        }
        fprintf(log_file, "pcm data saved to file %s", params->acp.file_name);
        free(buffer);
    }
    return 0;
}

void *drift_read(void* data)
{
    struct drift_data* params = (struct drift_data*) data;
    qahw_stream_handle_t* out_handle = params->out_handle;
    struct qahw_avt_device_drift_param drift_param;
    int rc = -EINVAL;

    printf("drift queried at 100ms interval\n");
    while (!(params->thread_exit)) {
        memset(&drift_param, 0, sizeof(struct qahw_avt_device_drift_param));
        rc = qahw_out_get_param_data(out_handle, QAHW_PARAM_AVT_DEVICE_DRIFT,
                (qahw_param_payload *)&drift_param);
        if (!rc) {
            printf("resync flag = %d, drift %d, av timer %lld\n",
                    drift_param.resync_flag,
                    drift_param.avt_device_drift_value,
                    drift_param.ref_timer_abs_ts);
        } else {
            printf("drift query failed rc = %d retry after 100ms\n", rc);
        }

        usleep(100000);
        if (params->enable_drift_correction &&
            drift_param.avt_device_drift_value) {
            struct qahw_out_correct_drift param;
            param.adjust_time = drift_param.avt_device_drift_value;
            printf("sending drift correction value %dus\n",
                    drift_param.avt_device_drift_value);
            rc = qahw_out_set_param_data(out_handle,
                          QAHW_PARAM_OUT_CORRECT_DRIFT,
                         (qahw_param_payload *)&param);
            if (rc < 0)
                fprintf(log_file, "qahw_out_set_param_data failed with err %d %d\n",
                        rc, __LINE__);
        }
    }
    return NULL;
}

static int __unused is_eof (stream_config *stream) {
    if (stream->filename) {
        if (feof(stream->file_stream)) {
            fprintf(log_file, "stream %d: error in fread, error %d\n", stream->stream_index, ferror(stream->file_stream));
            fprintf(stderr, "stream %d: error in fread, error %d\n", stream->stream_index, ferror(stream->file_stream));
            return true;
        }
    } else if (AUDIO_DEVICE_NONE != stream->input_device)
        /*
         * assuming this is called after we got -ve bytes value from hal read
         */
        return true;
    return false;
}
static int read_bytes(stream_config *stream, void *buff, int size) {
    if (stream->filename)
        return fread(buff, 1, size, stream->file_stream);
    else if (AUDIO_DEVICE_NONE != stream->input_device) {
        qahw_in_buffer_t in_buf;
        memset(&in_buf,0, sizeof(qahw_in_buffer_t));
        in_buf.buffer = buff;
        in_buf.bytes = size;
        return qahw_in_read(stream->in_handle, &in_buf);
    }
    return 0;
}

int write_to_hal(qahw_stream_handle_t* out_handle, char *data, size_t bytes, void *params_ptr)
{
    stream_config *stream_params = (stream_config*) params_ptr;

    ssize_t ret;

    qahw_out_buffer_t out_buf;

    memset(&out_buf,0, sizeof(qahw_out_buffer_t));
    out_buf.buffer = data;
    out_buf.bytes = bytes;

    ret = qahw_out_write(out_handle, &out_buf);
    if (ret < 0) {
        fprintf(log_file, "stream %d: writing data to hal failed (ret = %zd)\n", stream_params->stream_index, ret);
    } else if ((ret != bytes) && (!stop_playback)) {
        pthread_mutex_lock(&stream_params->write_lock);
        fprintf(log_file, "stream %d: provided bytes %zd, written bytes %d\n",stream_params->stream_index, bytes, ret);
        fprintf(log_file, "stream %d: waiting for event write ready\n", stream_params->stream_index);
        pthread_cond_wait(&stream_params->write_cond, &stream_params->write_lock);
        fprintf(log_file, "stream %d: out of wait for event write ready\n", stream_params->stream_index);
        pthread_mutex_unlock(&stream_params->write_lock);
    }

    return ret;
}

static bool __unused is_assoc_active()
{
    int i = 0;
    bool is_assoc_active = false;

    for (i = 0; i < MAX_PLAYBACK_STREAMS; i++) {
        if (stream_param[i].flags & AUDIO_OUTPUT_FLAG_ASSOCIATED) {
            is_assoc_active = true;
            break;
        }
    }
    return is_assoc_active;
}

static int __unused get_assoc_index()
{
    int i = 0;

    for (i = 0; i < MAX_PLAYBACK_STREAMS; i++) {
        if (stream_param[i].flags & AUDIO_OUTPUT_FLAG_ASSOCIATED) {
            break;
        }
    }
    return i;
}

/* Entry point function for stream playback
 * Opens the stream
 * Reads KV pairs, sets volume, allocates input buffer
 * Opens proxy and effects threads if enabled
 * Starts freading the file and writing to HAL
 * Drains out and close the stream after EOF
 */
void *start_stream_playback (void* stream_data)
{
    int rc = 0;
    stream_config *params = (stream_config*) stream_data;
    bool proxy_thread_active = false;
    pthread_t proxy_thread;

    bool drift_thread_active = false;
    pthread_t drift_query_thread;
    struct drift_data drift_params;

    int offset = 0;
    bool is_offload = params->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD;
    size_t bytes_wanted = 0;
    size_t write_length = 0;
    size_t bytes_remaining = 0;
    ssize_t bytes_written = 0;

    size_t bytes_read = 0;
    char  *data_ptr = NULL;
    bool exit = false;
    bool read_complete_file = true;
    ssize_t bytes_to_read = 0;
    int32_t latency;
    char kvpair[KV_PAIR_MAX_LENGTH] = {0};


    memset(&drift_params, 0, sizeof(struct drift_data));

    fprintf(log_file, "stream %d: play_later %d \n", params->stream_index, params->play_later);

    if(params->play_later) {
            pthread_mutex_lock(&dual_main_lock);
            fprintf(log_file, "stream %d: waiting for dual main signal\n", params->stream_index);
            pthread_cond_wait(&dual_main_cond, &dual_main_lock);
            fprintf(log_file, "stream %d: after the dual main signal\n", params->stream_index);
            pthread_mutex_unlock(&dual_main_lock);
    }

    if (params->interactive_strm) {
        params->flags = AUDIO_OUTPUT_FLAG_INTERACTIVE;
        fprintf(stderr, "stream %s %d: Interactive stream\n", __func__, params->stream_index);
    }

    rc = qahw_open_output_stream(params->qahw_out_hal_handle,
                             params->handle,
                             params->output_device,
                             params->flags,
                             &(params->config),
                             &(params->out_handle),
                             params->device_url);

    if (rc) {
        fprintf(log_file, "stream %d: could not open output stream, error - %d \n", params->stream_index, rc);
        fprintf(stderr, "stream %d: could not open output stream, error - %d \n", params->stream_index, rc);
        return NULL;
    }

    fprintf(log_file, "stream %d: open output stream is success, out_handle %p\n", params->stream_index, params->out_handle);

    if (audio_is_bluetooth_sco_device(params->output_device)) {
        char param1[50];
        int ret = -1;
        snprintf(param1, sizeof(param1), "bt_wbs=%s", ((params->bt_wbs == 1) ? "on" : "off"));
        ret = qahw_set_parameters(params->qahw_out_hal_handle, param1);
        fprintf(log_file, " param %s set to hal with return value %d\n", param1, ret);
    }

    if (kpi_mode == true) {
        measure_kpi_values(params->out_handle, params->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD);
        rc = qahw_close_output_stream(params->out_handle);
        if (rc) {
            fprintf(log_file, "stream %d: could not close output stream, error - %d \n", params->stream_index, rc);
            fprintf(stderr, "stream %d: could not close output stream, error - %d \n", params->stream_index, rc);
        }
        return NULL;
    }

    switch(params->filetype) {
        case FILE_WMA:
        case FILE_VORBIS:
        case FILE_ALAC:
        case FILE_FLAC:
        case FILE_APE:
            fprintf(log_file, "%s:calling setparam for kvpairs\n", __func__);
            if (!(params->kvpair_values)) {
               fprintf(log_file, "stream %d: error!!No metadata for the clip\n", params->stream_index);
               fprintf(stderr, "stream %d: error!!No metadata for the clip\n", params->stream_index);
               return NULL;
            }
            read_kvpair(kvpair, params->kvpair_values, params->filetype);
            rc = qahw_out_set_parameters(params->out_handle, kvpair);
            if(rc){
                fprintf(log_file, "stream %d: failed to set kvpairs\n", params->stream_index);
                fprintf(stderr, "stream %d: failed to set kvpairs\n", params->stream_index);
                return NULL;
            }
            fprintf(log_file, "stream %d: kvpairs are set\n", params->stream_index);
            break;
    case FILE_DTS:
            read_complete_file = false;
            break;
    default:
            break;
    }

    if (is_offload) {
        fprintf(log_file, "stream %d: set callback for offload stream for playback usecase\n", params->stream_index);
        qahw_out_set_callback(params->out_handle, async_callback, params);
    }

    // create effect thread, use thread_data to transfer command
    if (params->ethread_func &&
            (params->flags & (AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|AUDIO_OUTPUT_FLAG_DIRECT))) {

        fprintf(log_file, "stream %d: effect type:%s\n", params->stream_index, effect_str[params->effect_index]);
        params->ethread_data = create_effect_thread(params->effect_index, params->ethread_func);

        // create effect command thread
        params->cmd_data.exit = false;
        params->cmd_data.fx_data_ptr = &(params->ethread_data);
        pthread_attr_init(&(params->cmd_data.attr));
        pthread_attr_setdetachstate(&(params->cmd_data.attr), PTHREAD_CREATE_JOINABLE);
        rc = pthread_create(&(params->cmd_data.cmd_thread), &(params->cmd_data.attr),
                &command_thread_func, &(params->cmd_data));
        if (rc < 0) {
            fprintf(log_file, "stream %d: could not create effect command thread!\n", params->stream_index);
            fprintf(stderr, "stream %d: could not create effect command thread!\n", params->stream_index);
            return NULL;
        }

        fprintf(log_file, "stream %d: loading effects\n", params->stream_index);
        if (params->ethread_data != nullptr) {
            // load effect module
            notify_effect_command(params->ethread_data, EFFECT_LOAD_LIB, -1, 0, NULL);

            // get effect desc
            notify_effect_command(params->ethread_data, EFFECT_GET_DESC, -1, 0, NULL);

            // create effect
            params->ethread_data->io_handle = params->handle;
            notify_effect_command(params->ethread_data, EFFECT_CREATE, -1, 0, NULL);

            // broadcast device info
            notify_effect_command(params->ethread_data, EFFECT_CMD, QAHW_EFFECT_CMD_SET_DEVICE, sizeof(audio_devices_t), &(params->output_device));

            // Enable and Set default values
            params->ethread_data->default_value = params->effect_preset_strength;
            params->ethread_data->default_flag = true;
            notify_effect_command(params->ethread_data, EFFECT_CMD, QAHW_EFFECT_CMD_ENABLE, 0, NULL);
        }
    }

    if (params->output_device & AUDIO_DEVICE_OUT_PROXY) {
        proxy_params.acp.qahw_mod_handle = params->qahw_out_hal_handle;
        proxy_params.acp.handle = stream_handle;
        stream_handle--;
        proxy_params.acp.input_device = AUDIO_DEVICE_IN_PROXY;
        proxy_params.acp.flags = AUDIO_INPUT_FLAG_NONE;
        proxy_params.acp.config.channel_mask = audio_channel_in_mask_from_count(AFE_PROXY_CHANNEL_COUNT);
        proxy_params.acp.config.sample_rate = AFE_PROXY_SAMPLING_RATE;
        proxy_params.acp.config.format = AUDIO_FORMAT_PCM_16_BIT;
        proxy_params.acp.kStreamName = "input_stream";
        proxy_params.acp.kInputSource = AUDIO_SOURCE_UNPROCESSED;
        proxy_params.acp.thread_exit = false;
        fprintf(log_file, "stream %d: create thread to read data from proxy\n", params->stream_index);
        rc = pthread_create(&proxy_thread, NULL, proxy_read, (void *)&proxy_params);
        if (!rc)
            proxy_thread_active = true;
    } else if (params->drift_query && !drift_thread_active) {
        struct qahw_out_enable_drift_correction drift_enable_param;

        drift_params.out_handle = params->out_handle;
        drift_params.thread_exit = false;
        fprintf(log_file, "create thread to read avtimer vs device drift\n");
        if(params->drift_correction) {
            drift_params.enable_drift_correction = true;
            drift_enable_param.enable = true;
            rc = qahw_out_set_param_data(params->out_handle,
                    QAHW_PARAM_OUT_ENABLE_DRIFT_CORRECTION,
                    (qahw_param_payload *)&drift_enable_param);
            if (rc < 0) {
                fprintf(log_file, "qahw_out_set_param_data failed with err %d %d\n",
                        rc, __LINE__);
                drift_enable_param.enable = false;
            }
        }
        rc = pthread_create(&drift_query_thread, NULL, drift_read, (void *)&drift_params);
        if (!rc)
            drift_thread_active = true;
        else
            fprintf(log_file, "drift query thread creation failure %d\n", rc);
    }

    rc = qahw_out_set_volume(params->out_handle, vol_level, vol_level);
    if (rc < 0) {
        fprintf(log_file, "stream %d: unable to set volume\n", params->stream_index);
        fprintf(stderr, "stream %d: unable to set volume\n", params->stream_index);
    }

    if (params->pan_scale_ctrl == QAHW_PARAM_OUT_MIX_MATRIX_PARAMS) {
        rc = qahw_out_set_param_data(params->out_handle, QAHW_PARAM_OUT_MIX_MATRIX_PARAMS,
                                     (qahw_param_payload *) &params->mm_params_pan_scale);
        if (rc != 0) {
            fprintf(log_file, "QAHW_PARAM_OUT_MIX_MATRIX_PARAMS could not be sent!\n");
        }
    }
    if (params->mix_ctrl == QAHW_PARAM_CH_MIX_MATRIX_PARAMS) {
        rc = qahw_out_set_param_data(params->out_handle, QAHW_PARAM_CH_MIX_MATRIX_PARAMS,
                                     (qahw_param_payload *) &params->mm_params_downmix);
        if (rc != 0) {
            fprintf(log_file, "QAHW_PARAM_CH_MIX_MATRIX_PARAMS could not be sent!\n");
        }
    }

    bytes_wanted = qahw_out_get_buffer_size(params->out_handle);
    data_ptr = (char *) malloc (bytes_wanted);
    if (data_ptr == NULL) {
        fprintf(log_file, "stream %d: failed to allocate data buffer\n", params->stream_index);
        fprintf(stderr, "stream %d: failed to allocate data buffer\n", params->stream_index);
        return NULL;
    }

    latency = qahw_out_get_latency(params->out_handle);
    fprintf(log_file, "playback latency before starting a session %dms!!\n",
            latency);

    if (event_trigger == true)
        tigger_event(params->out_handle);

    bytes_to_read = get_bytes_to_read(params->file_stream, params->filetype);
    if (bytes_to_read <= 0)
        read_complete_file = true;

    if (params->set_params) {
        rc = qahw_out_set_parameters(params->out_handle, params->set_params);
        if (rc) {
            fprintf(log_file, "stream %s: failed to set kvpairs\n", params->set_params);
            fprintf(stderr, "stream %s: failed to set kvpairs\n", params->set_params);
        }
    }

    while (!exit && !stop_playback) {
        if (!bytes_remaining) {
            fprintf(log_file, "\nstream %d: reading bytes %zd\n", params->stream_index, bytes_wanted);
            bytes_read = read_bytes(params, data_ptr, bytes_wanted);
            fprintf(log_file, "stream %d: read bytes %zd\n", params->stream_index, bytes_read);
            if ((!read_complete_file && (bytes_to_read <= 0)) || (bytes_read <= 0)) {
                fprintf(log_file, "stream %d: end of file\n", params->stream_index);
                if (is_offload) {
                    params->drain_received = false;
                    qahw_out_drain(params->out_handle, QAHW_DRAIN_ALL);
                    if(!params->drain_received) {
                        pthread_mutex_lock(&params->drain_lock);
                        pthread_cond_wait(&params->drain_cond, &params->drain_lock);
                        pthread_mutex_unlock(&params->drain_lock);
                    }
                    fprintf(log_file, "stream %d: out of compress drain\n", params->stream_index);
                }
                /*
                 * Caution: Below ADL log shouldnt be altered without notifying
                 * automation APT since it used for automation testing
                 */
                fprintf(log_file, "ADL: stream %d: playback completed successfully\n", params->stream_index);
                exit = true;
                continue;
            } else {
                if (!read_complete_file) {
                    bytes_to_read -= bytes_read;
                    if ((bytes_to_read > 0) && (bytes_to_read < bytes_wanted))
                        bytes_wanted = bytes_to_read;
                }
            }
            bytes_remaining = write_length = bytes_read;
        }

        offset = write_length - bytes_remaining;
        fprintf(log_file, "stream %d: writing to hal %zd bytes, offset %d, write length %zd\n",
                params->stream_index, bytes_remaining, offset, write_length);


        bytes_written = bytes_remaining;
        bytes_written = write_to_hal(params->out_handle, data_ptr+offset, bytes_remaining, params);
        if (bytes_written < 0) {
            fprintf(stderr, "write failed %d", bytes_written);
            exit = true;
            continue;
        }
        bytes_remaining -= bytes_written;

        latency = qahw_out_get_latency(params->out_handle);
        fprintf(log_file, "stream %d: bytes_written %zd, bytes_remaining %zd latency %d\n",
                params->stream_index, bytes_written, bytes_remaining, latency);
    }


    if (params->ethread_data != nullptr) {
        fprintf(log_file, "stream %d: un-loading effects\n", params->stream_index);
        // disable effect
        notify_effect_command(params->ethread_data, EFFECT_CMD, QAHW_EFFECT_CMD_DISABLE, 0, NULL);

        // release effect
        notify_effect_command(params->ethread_data, EFFECT_RELEASE, -1, 0, NULL);

        // unload effect module
        notify_effect_command(params->ethread_data, EFFECT_UNLOAD_LIB, -1, 0, NULL);

        // destroy effect thread
        destroy_effect_thread(params->ethread_data);

        free(params->ethread_data);
        params->ethread_data = NULL;

        // destory effect command thread
        params->cmd_data.exit = true;
        usleep(100000);  // give a chance for thread to exit gracefully

        //Send signal for input command_thread_func to stop
        rc = pthread_kill(params->cmd_data.cmd_thread, SIGUSR1);
        if (rc != 0) {
            fprintf(log_file, "Fail to kill effect command thread!\n");
            fprintf(stderr, "Fail to kill effect command thread!\n");
        }
        rc = pthread_join(params->cmd_data.cmd_thread, NULL);
        if (rc < 0) {
            fprintf(log_file, "Fail to join effect command thread!\n");
            fprintf(stderr, "Fail to join effect command thread!\n");
        }
    }

    if (proxy_thread_active) {
       /*
        * DSP gives drain ack for last buffer which will close proxy thread before
        * app reads last buffer. So add sleep before exiting proxy thread to read
        * last buffer of data. This is not a calculated value.
        */
        usleep(500000);
        proxy_params.acp.thread_exit = true;
        fprintf(log_file, "wait for proxy thread exit\n");
        pthread_join(proxy_thread, NULL);
    }

    if (drift_thread_active) {
        usleep(500000);
        drift_params.thread_exit = true;
        pthread_join(drift_query_thread, NULL);
    }
    rc = qahw_out_standby(params->out_handle);
    if (rc) {
        fprintf(log_file, "stream %d: out standby failed %d \n", params->stream_index, rc);
        fprintf(stderr, "stream %d: out standby failed %d \n", params->stream_index, rc);
    }

    fprintf(log_file, "stream %d: closing output stream\n", params->stream_index);
    rc = qahw_close_output_stream(params->out_handle);
    if (rc) {
        fprintf(log_file, "stream %d: could not close output stream, error - %d \n", params->stream_index, rc);
        fprintf(stderr, "stream %d: could not close output stream, error - %d \n", params->stream_index, rc);
    }

    if (data_ptr)
        free(data_ptr);

    fprintf(log_file, "stream %d: stream closed\n", params->stream_index);
    fprintf(log_file, "stream %d: is_dual_main- %d\n", params->stream_index,is_dual_main);
    if (is_dual_main) {
        usleep(500000);
        pthread_mutex_lock(&dual_main_lock);
        fprintf(log_file, "Dual main signal as we reached end of current running stream\n");
        is_dual_main = false;
        pthread_cond_signal(&dual_main_cond);
        pthread_mutex_unlock(&dual_main_lock);
    }

    return NULL;

}

bool is_valid_aac_format_type(aac_format_type_t format_type)
{
    bool valid_format_type = false;

    switch (format_type) {
    case AAC_LC:
    case AAC_HE_V1:
    case AAC_HE_V2:
    case AAC_LOAS:
        valid_format_type = true;
        break;
    default:
        break;
    }
    return valid_format_type;
}

/*
 * Obtain aac format (refer audio.h) for format type entered.
 */

audio_format_t get_aac_format(int filetype, aac_format_type_t format_type)
{
    audio_format_t aac_format = AUDIO_FORMAT_AAC_ADTS_LC; /* default aac frmt*/

    if (filetype == FILE_AAC_ADTS) {
        switch (format_type) {
        case AAC_LC:
            aac_format = AUDIO_FORMAT_AAC_ADTS_LC;
            break;
        case AAC_HE_V1:
            aac_format = AUDIO_FORMAT_AAC_ADTS_HE_V1;
            break;
        case AAC_HE_V2:
            aac_format = AUDIO_FORMAT_AAC_ADTS_HE_V2;
            break;
        default:
            break;
        }
    } else if (filetype == FILE_AAC) {
        switch (format_type) {
        case AAC_LC:
            aac_format = AUDIO_FORMAT_AAC_LC;
            break;
        case AAC_HE_V1:
            aac_format = AUDIO_FORMAT_AAC_HE_V1;
            break;
        case AAC_HE_V2:
            aac_format = AUDIO_FORMAT_AAC_HE_V2;
            break;
        default:
            break;
        }
    } else if (filetype == FILE_AAC_LATM) {
        switch (format_type) {
        case AAC_LC:
            aac_format = AUDIO_FORMAT_AAC_LATM_LC;
            break;
        case AAC_HE_V1:
            aac_format = AUDIO_FORMAT_AAC_LATM_HE_V1;
            break;
        case AAC_HE_V2:
            aac_format = AUDIO_FORMAT_AAC_LATM_HE_V2;
            break;
        default:
            break;
        }
    } else {
        fprintf(log_file, "Invalid filetype provided %d\n", filetype);
        fprintf(stderr, "Invalid filetype provided %d\n", filetype);
    }

    fprintf(log_file, "aac format %d\n", aac_format);
    return aac_format;
}

void get_file_format(stream_config *stream_info)
{
    int rc = 0;

    if (!(stream_info->flags_set)) {
        if (stream_info->interactive_strm)
            stream_info->flags = AUDIO_OUTPUT_FLAG_INTERACTIVE;
        else
            stream_info->flags = AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|AUDIO_OUTPUT_FLAG_NON_BLOCKING;
        stream_info->flags |= AUDIO_OUTPUT_FLAG_DIRECT;
    }

    char header[WAV_HEADER_LENGTH_MAX] = {0};
    int wav_header_len = 0;

    switch (stream_info->filetype) {
        case FILE_WAV:
            /*
             * Read the wave header
             */
            if((wav_header_len = get_wav_header_length(stream_info->file_stream)) <= 0) {
                fprintf(log_file, "wav header length is invalid:%d\n", wav_header_len);
                exit(1);
            }
            fseek(stream_info->file_stream, 0, SEEK_SET);
            rc = fread (header, wav_header_len , 1, stream_info->file_stream);
            if (rc != 1) {
               fprintf(log_file, "Error fread failed\n");
               fprintf(stderr, "Error fread failed\n");
               exit(1);
            }
            if (strncmp (header, "RIFF", 4) && strncmp (header+8, "WAVE", 4)) {
               fprintf(log_file, "Not a wave format\n");
               fprintf(stderr, "Not a wave format\n");
               exit (1);
            }
            memcpy (&stream_info->channels, &header[22], 2);
            memcpy (&stream_info->config.offload_info.sample_rate, &header[24], 4);
            memcpy (&stream_info->config.offload_info.bit_width, &header[34], 2);
            if (stream_info->config.offload_info.bit_width == 32)
                stream_info->config.offload_info.format = AUDIO_FORMAT_PCM_32_BIT;
            else if (stream_info->config.offload_info.bit_width == 24)
                stream_info->config.offload_info.format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
            else
                stream_info->config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
            if (!(stream_info->flags_set))
                stream_info->flags = AUDIO_OUTPUT_FLAG_DIRECT;
            break;

        case FILE_MP3:
            stream_info->config.offload_info.format = AUDIO_FORMAT_MP3;
            break;

        case FILE_AAC:
        case FILE_AAC_ADTS:
        case FILE_AAC_LATM:
            if (!is_valid_aac_format_type(stream_info->aac_fmt_type)) {
                fprintf(log_file, "Invalid format type for AAC %d\n", stream_info->aac_fmt_type);
                fprintf(stderr, "Invalid format type for AAC %d\n", stream_info->aac_fmt_type);
                return;
            }
            stream_info->config.offload_info.format = get_aac_format(stream_info->filetype, stream_info->aac_fmt_type);
            break;
        case FILE_FLAC:
            stream_info->config.offload_info.format = AUDIO_FORMAT_FLAC;
            break;
        case FILE_ALAC:
            stream_info->config.offload_info.format = AUDIO_FORMAT_ALAC;
            break;
        case FILE_VORBIS:
            stream_info->config.offload_info.format = AUDIO_FORMAT_VORBIS;
            break;
        case FILE_WMA:
            if (stream_info->wma_fmt_type == WMA)
               stream_info->config.offload_info.format = AUDIO_FORMAT_WMA;
            else
               stream_info->config.offload_info.format = AUDIO_FORMAT_WMA_PRO;
            break;
        case FILE_MP2:
            stream_info->config.offload_info.format = AUDIO_FORMAT_MP2;
            break;
        case FILE_AC3:
            stream_info->config.offload_info.format = AUDIO_FORMAT_AC3;
            break;
        case FILE_EAC3:
        case FILE_EAC3_JOC:
            stream_info->config.offload_info.format = AUDIO_FORMAT_E_AC3;
            break;
        case FILE_DTS:
            stream_info->config.offload_info.format = AUDIO_FORMAT_DTS;
            break;
        case FILE_APTX:
            stream_info->config.offload_info.format = AUDIO_FORMAT_APTX;
            break;
        case FILE_TRUEHD:
            stream_info->config.offload_info.format = AUDIO_FORMAT_DOLBY_TRUEHD;
            break;
        case FILE_IEC61937:
            stream_info->config.offload_info.format = AUDIO_FORMAT_IEC61937;
            break;
        case FILE_APE:
            stream_info->config.offload_info.format = AUDIO_FORMAT_APE;
            break;
        default:
           fprintf(log_file, "Does not support given filetype\n");
           fprintf(stderr, "Does not support given filetype\n");
           usage();
           hal_test_qap_usage();
           return;
    }
    stream_info->config.sample_rate = stream_info->config.offload_info.sample_rate;
    stream_info->config.format = stream_info->config.offload_info.format;
    stream_info->config.channel_mask = stream_info->config.offload_info.channel_mask = audio_channel_out_mask_from_count(stream_info->channels);
    return;
}

int measure_kpi_values(qahw_stream_handle_t* out_handle, bool is_offload) {
    int rc = 0;
    int offset = 0;
    size_t bytes_wanted = 0;
    size_t write_length = 0;
    size_t bytes_remaining = 0;
    size_t bytes_written = 0;
    char  *data = NULL;
    int ret = 0, count = 0;
    struct timespec ts_cold, ts_cont;
    uint64_t tcold, tcont, scold = 0, uscold = 0, scont = 0, uscont = 0;

    if (is_offload) {
        fprintf(log_file, "Set callback for offload stream in kpi mesaurement usecase\n");
        qahw_out_set_callback(out_handle, async_callback, &stream_param[PRIMARY_STREAM_INDEX]);
    }

    FILE *fd_latency_node = fopen(LATENCY_NODE, "r+");
    if (fd_latency_node) {
        ret = fwrite(LATENCY_NODE_INIT_STR, sizeof(LATENCY_NODE_INIT_STR), 1, fd_latency_node);
        if (ret<1)
            fprintf(log_file, "error(%d) writing to debug node!", ret);
            fprintf(stderr, "error(%d) writing to debug node!", ret);
        fflush(fd_latency_node);
    } else {
        fprintf(log_file, "debug node(%s) open failed!", LATENCY_NODE);
        fprintf(stderr, "debug node(%s) open failed!", LATENCY_NODE);
        return -1;
    }

    bytes_wanted = qahw_out_get_buffer_size(out_handle);
    data = (char *) calloc (1, bytes_wanted);
    if (data == NULL) {
        fprintf(log_file, "calloc failed!!\n");
        fprintf(stderr, "calloc failed!!\n");
        fclose(fd_latency_node);
        return -ENOMEM;
    }

    while (count < 64) {
        if (!bytes_remaining) {
            bytes_remaining = write_length = bytes_wanted;
        }
        if (count == 0) {
            ret = clock_gettime(CLOCK_MONOTONIC, &ts_cold);
            if (ret) {
                fprintf(log_file, "error(%d) fetching start time for cold latency", ret);
                fprintf(stderr, "error(%d) fetching start time for cold latency", ret);
                rc = -1;
                goto exit;
            }
        } else if (count == 16) {
            int *d = (int *)data;
            d[0] = 0x01010000;
            ret = clock_gettime(CLOCK_MONOTONIC, &ts_cont);
            if (ret) {
                fprintf(log_file, "error(%d) fetching start time for continuous latency", ret);
                fprintf(stderr, "error(%d) fetching start time for continuous latency", ret);
                rc = -1;
                goto exit;
            }
        }

        offset = write_length - bytes_remaining;
        bytes_written = write_to_hal(out_handle, data+offset, bytes_remaining, &stream_param[PRIMARY_STREAM_INDEX]);
        bytes_remaining -= bytes_written;
        fprintf(log_file, "bytes_written %zd, bytes_remaining %zd\n",
                bytes_written, bytes_remaining);

        if (count == 16) {
            int *i = (int *)data;
            i[0] = 0x00000000;
        }
        count++;
    }

    char latency_buf[200] = {0};
    fread((void *) latency_buf, 100, 1, fd_latency_node);
    sscanf(latency_buf, " %llu,%llu,%*llu,%*llu,%llu,%llu", &scold, &uscold, &scont, &uscont);
    tcold = scold*1000 - ((uint64_t)ts_cold.tv_sec)*1000 + uscold/1000 - ((uint64_t)ts_cold.tv_nsec)/1000000;
    tcont = scont*1000 - ((uint64_t)ts_cont.tv_sec)*1000 + uscont/1000 - ((uint64_t)ts_cont.tv_nsec)/1000000;
    fprintf(log_file, "\n values from debug node %s\n", latency_buf);
    fprintf(log_file, " cold latency %llums, continuous latency %llums,\n", tcold, tcont);
    fprintf(log_file, " **Note: please add DSP Pipe/PP latency numbers to this, for final latency values\n");
exit:
    fclose(fd_latency_node);
    free(data);
    return rc;
}

int tigger_event(qahw_stream_handle_t* out_handle)
{
    qahw_param_payload payload;
    struct event_data event_payload = {0, 0, 0, 0, 0, 0, 0};
    int ret = 0;

    event_payload.num_events = 1;
    event_payload.event_id = 0x13236;
    event_payload.module_id = 0x10940;
    event_payload.config_mask = 1;

    payload.adsp_event_params.event_type = QAHW_STREAM_PP_EVENT;
    payload.adsp_event_params.payload_length = sizeof(event_payload);
    payload.adsp_event_params.payload = &event_payload;

    fprintf(log_file, "Set callback for event trigger usecase\n");
    ret = qahw_out_set_callback(out_handle, async_callback,
            &stream_param[PRIMARY_STREAM_INDEX]);
    if (ret < 0) {
        fprintf(log_file, "qahw_out_set_callback failed with err %d\n",
                ret);
        goto done;
    }
    fprintf(log_file, "Register for event using qahw_out_set_param_data\n");
    ret = qahw_out_set_param_data(out_handle, QAHW_PARAM_ADSP_STREAM_CMD,
            (qahw_param_payload *)&payload);
    if (ret < 0) {
        fprintf(log_file, "qahw_out_set_param_data failed with err %d\n",
                ret);
        goto done;
    }
    fprintf(log_file, "qahw_out_set_paramdata succeeded\n");

done:
    return ret;
}

void parse_aptx_dec_bt_addr(char *value, struct qahw_aptx_dec_param *aptx_cfg)
{
    int ba[6] = {0, 0, 0, 0, 0, 0};
    char *str, *tok;
    uint32_t addr[3];
    int i = 0;

    tok = strtok_r(value, ":", &str);
    while (tok != NULL) {
        ba[i] = strtol(tok, NULL, 16);
        i++;
        tok = strtok_r(NULL, ":", &str);
    }
    addr[0] = (ba[0] << 8) | ba[1];
    addr[1] = ba[2];
    addr[2] = (ba[3] << 16) | (ba[4] << 8) | ba[5];

    aptx_cfg->bt_addr.nap = addr[0];
    aptx_cfg->bt_addr.uap = addr[1];
    aptx_cfg->bt_addr.lap = addr[2];
}

typedef struct {
    char *string;
    int val;
} param_converter_type;

param_converter_type format_table[] = {
    {"AUDIO_FORMAT_PCM_16_BIT",        AUDIO_FORMAT_PCM_16_BIT},
    {"AUDIO_FORMAT_PCM_32_BIT",        AUDIO_FORMAT_PCM_32_BIT},
    {"AUDIO_FORMAT_PCM_8_BIT",         AUDIO_FORMAT_PCM_8_BIT},
    {"AUDIO_FORMAT_PCM_8_24_BIT",      AUDIO_FORMAT_PCM_8_24_BIT},
    {"AUDIO_FORMAT_PCM_24_BIT_PACKED", AUDIO_FORMAT_PCM_24_BIT_PACKED}
};


int rate_table[] = {48000, 44100};


static int get_kvpairs_string(char *kvpairs, const char *key, char *value) {
    struct str_parms *parms = NULL;

    if (!kvpairs)
        return -1;

    parms = str_parms_create_str(kvpairs);
    if (parms == NULL)
        return -1;

    if (str_parms_get_str(parms, key, value, KVPAIRS_MAX) < 0)
        return -1;

    str_parms_destroy(parms);
    return 1;
}

static int get_pcm_format(char *kvpairs) {
    bool match = false;
    int i = 0;
    char value[KVPAIRS_MAX] = {0};

    if(!kvpairs)
        return -1;


    if (get_kvpairs_string(kvpairs, QAHW_PARAMETER_STREAM_SUP_FORMATS, value) < 0)
        return -1;

    fprintf(log_file, "formats=%s\n", value);

   /*
    * for now we assume usb hal/pcm device announces suport for one format ONLY
    */
    for (i = 0; i < (sizeof(format_table)/sizeof(format_table[0])); i++) {
        if(!strncmp(format_table[i].string, value, sizeof(value))) {
            match = true;
            break;
        }
    }

    if (match)
        return format_table[i].val;
    else
        return -1;

}


static int get_rate(char *kvpairs) {
    int match = false;
    int rate = 0;
    int i = 0;
    char value[KVPAIRS_MAX] = {0};

    if(!kvpairs)
        return -1;

    if (get_kvpairs_string(kvpairs, QAHW_PARAMETER_STREAM_SUP_SAMPLING_RATES, value) < 0)
        return -1;

    fprintf(log_file, "sample rates=%s\n", value);
   /*
    * for now we assume usb hal/pcm device announces suport for one rate ONLY
    */

    rate = atoi(value);
    for (i = 0; i < ARRAY_SIZE(rate_table); i++)
        if (rate_table[i] == rate)
            match = true;

    if (match)
        return rate;
    else
        return -1;
}


static int get_channels(char *kvpairs) {
    int ch = -1;
    char value[KVPAIRS_MAX] = {0};

    if(!kvpairs)
        return -1;

    if (get_kvpairs_string(kvpairs, QAHW_PARAMETER_STREAM_SUP_CHANNELS, value) < 0)
        return -1;

    fprintf(log_file, "channels=%s\n", value);

   /*
    * this is to work around a bug in usb hal which annouces support for stereo
    * though the pcm dev/host stream is mono.
    */
    if (strstr(value, "MONO"))
        ch = 1;
    else if (strstr(value, "STEREO"))
        ch = 2;

    return ch;
}


static int detect_stream_params(stream_config *stream) {
    bool detection_needed = false;
    int direction = PCM_OUT;

    int rc = 0;
    char *param_string = NULL;
    int ch = 0;

    if (AUDIO_DEVICE_IN_USB_DEVICE == stream->input_device ||
        AUDIO_DEVICE_OUT_USB_DEVICE == stream->output_device)
        if (USB_MODE_DEVICE == stream->usb_mode)
            detection_needed = true;

    if (!detection_needed)
    /*
     * we will go with given params through args or with default params.
     */
        return true;

    if (AUDIO_DEVICE_IN_USB_DEVICE == stream->input_device)
        direction = PCM_IN;
    else
        direction = PCM_OUT;

    if (direction == PCM_OUT && stream->interactive_strm) {
        stream->flags = AUDIO_OUTPUT_FLAG_INTERACTIVE;
        fprintf(stderr, "stream %d: Interactive stream\n", stream->stream_index);
    }
    fprintf(log_file, "%s: opening %s stream\n", __func__, ((direction == PCM_IN)? "input":"output"));

    if (PCM_IN == direction)
        rc = qahw_open_input_stream(stream->qahw_in_hal_handle,
                                stream->handle,
                                stream->input_device,
                                &(stream->config),
                                &(stream->in_handle),
                                AUDIO_INPUT_FLAG_NONE,
                                stream->device_url,
                                AUDIO_SOURCE_DEFAULT);
    else
        rc = qahw_open_output_stream(stream->qahw_out_hal_handle,
                                stream->handle,
                                stream->output_device,
                                stream->flags,
                                &(stream->config),
                                &(stream->out_handle),
                                stream->device_url);

    if (rc) {
        fprintf(log_file, "stream could not be opened\n");
        fprintf(stderr, "stream could not be opened\n");
        return rc;
    }

    fprintf(log_file,"\n**Supported Parameters**\n");
    if (PCM_IN == direction)
        param_string = qahw_in_get_parameters(stream->in_handle, QAHW_PARAMETER_STREAM_SUP_SAMPLING_RATES);
    else
        param_string = qahw_out_get_parameters(stream->out_handle, QAHW_PARAMETER_STREAM_SUP_SAMPLING_RATES);

    if ((stream->config.sample_rate = get_rate(param_string)) <= 0) {
        fprintf(log_file, "Unable to extract sample rate val =(%d) string(%s)\n", stream->config.sample_rate, param_string);
        fprintf(stderr, "Unable to extract sample rate val =(%d) string(%s)\n", stream->config.sample_rate, param_string);
        return -1;
    }
    if (PCM_IN == direction)
        param_string = qahw_in_get_parameters(stream->in_handle, QAHW_PARAMETER_STREAM_SUP_CHANNELS);
    else
        param_string = qahw_out_get_parameters(stream->out_handle, QAHW_PARAMETER_STREAM_SUP_CHANNELS);

    if ((ch = get_channels(param_string)) <= 0) {
        fprintf(log_file, "Unable to extract channels =(%d) string(%s)\n", ch, param_string == NULL ? "null":param_string);
        fprintf(stderr, "Unable to extract channels =(%d) string(%s)\n", ch, param_string == NULL ? "null":param_string);
        return -1;
    }
    stream->config.channel_mask = audio_channel_in_mask_from_count(ch);

    if (PCM_IN == direction)
        param_string = qahw_in_get_parameters(stream->in_handle, QAHW_PARAMETER_STREAM_SUP_FORMATS);
    else
        param_string = qahw_out_get_parameters(stream->out_handle, QAHW_PARAMETER_STREAM_SUP_FORMATS);

    if ((stream->config.format = get_pcm_format(param_string)) <= 0) {
        fprintf(log_file, "Unable to extract pcm format val =(%d) string(%s)\n", stream->config.format, param_string);
        fprintf(stderr, "Unable to extract pcm format val =(%d) string(%s)\n", stream->config.format, param_string);
        return -1;
    }
    stream->config.offload_info.format = stream->config.format;
    fprintf(log_file, "\n**Extracted Parameters**\nrate=%d\nch=%d,ch_mask=0x%x\nformats=%d\n\n",
        stream->config.sample_rate,
        ch, stream->config.channel_mask,
        stream->config.format);
    /*
     * Detection done now close usb stream it will be re-open later
     */
    fprintf(log_file, "%s:closing the usb stream\n", __func__);

    if (PCM_IN == direction)
        rc = qahw_close_input_stream(stream->in_handle);
    else
        rc = qahw_close_output_stream(stream->out_handle);

    if (rc) {
        fprintf(log_file, "%s:stream could not be closed\n", __func__);
        fprintf(stderr, "%s:stream could not be closed\n", __func__);
        return rc;
    }
    stream->config.offload_info.sample_rate = stream->config.sample_rate;
    stream->config.offload_info.format = stream->config.format;
    stream->config.offload_info.channel_mask = stream->config.channel_mask;
    return rc;

}
void usage() {
    printf(" \n Command \n");
    printf(" \n hal_play_test -f file-path <options>   - Plays audio file from the path provided\n");
    printf(" \n Options\n");
    printf(" -f  --file-path <file path>               - file path to be used for playback.\n");
    printf("                                             file path must be provided unless -K(--kpi) is used\n\n");
    printf("                                             file path must be provided unless -s(input data is not from a file) is used\n\n");
    printf(" -r  --sample-rate <sampling rate>         - Required for Non-WAV streams\n");
    printf("                                             For AAC-HE pls specify half the sample rate\n\n");
    printf(" -c  --channel count <channels>            - Required for Non-WAV streams\n\n");
    printf(" -b  --bitwidth <bitwidth>                 - Give either 16 or 24.Default value is 16.\n\n");
    printf(" -v  --volume <float volume level>         - Volume level float value between 0.0 - 1.0.\n");
    printf(" -d  --device <decimal value>              - see system/media/audio/include/system/audio.h for device values\n");
    printf("                                             Optional Argument and Default value is 2, i.e Speaker\n\n");
    printf(" -s  --source-device <decimal value>       - see system/media/audio/include/system/audio.h for device values\n");
    printf("                                             obtain data from a device instead of an SD card file\n\n");
    printf("                                             for example catpure data from USB HAL(device) and play it on Regular HAL(device)\n\n");
    printf(" -t  --file-type <file type>               - 1:WAV 2:MP3 3:AAC 4:AAC_ADTS 5:FLAC\n");
    printf("                                             6:ALAC 7:VORBIS 8:WMA 10:AAC_LATM \n");
    printf("                                             Required for non WAV formats\n\n");
    printf(" -a  --aac-type <aac type>                 - Required for AAC streams\n");
    printf("                                             1: LC 2: HE_V1 3: HE_V2\n\n");
    printf(" -w  --wma-type <wma type>                 - Required for WMA clips.Default vlaue is 1\n");
    printf("                                             1: WMA 2: WMAPRO 3:WMA_LOSSLESS \n\n");
    printf(" -k  --kvpairs <values>                    - Metadata information of clip\n");
    printf("                                             See Example for more info\n\n");
    printf(" -l  --log-file <ABSOLUTE FILEPATH>        - File path for debug msg, to print\n");
    printf("                                             on console use stdout or 1 \n\n");
    printf(" -D  --dump-file <ABSOLUTE FILEPATH>       - File path to dump pcm data from proxy\n");
    printf(" -F  --flags <int value for output flags>  - Output flag to be used\n\n");
    printf(" -k  --kpi-mode                            - Required for Latency KPI measurement\n");
    printf("                                             file path is not used here as file playback is not done in this mode\n");
    printf("                                             file path and other file specific options would be ignored in this mode.\n\n");
    printf(" -E  --event-trigger                       - Trigger DTMF event during playback\n");
    printf(" -e  --effect-type <effect type>           - Effect used for test\n");
    printf("                                             0:bassboost 1:virtualizer 2:equalizer 3:visualizer(NA) 4:reverb 5:audiosphere others:null\n\n");
    printf(" -p  --effect-preset <effect preset type>  - Effect preset type for respective effect-type\n");
    printf(" -S  --effect-strength <effect strength>   - Effect strength for respective effect-type\n");
    printf(" -A  --bt-addr <bt device addr>            - Required to set bt device adress for aptx decoder\n\n");
    printf(" -q  --drift query                         - Required for querying avtime vs hdmi drift\n");
    printf(" -Q  --drift query and correction          - Enable Drift query and correction\n");
    printf(" -z  --bt-wbs                              - Set bt_wbs param\n\n");
    printf(" -P                                        - Argument to do multi-stream playback, currently 2 streams are supported to run concurrently\n");
    printf("                                             Put -P and mention required attributes for the next stream\n");
    printf("                                             0:bassboost 1:virtualizer 2:equalizer 3:visualizer(NA) 4:reverb 5:audiosphere others:null");
    printf(" PLUS                                      - For multi-stream playback, currently 2 streams are supported to play concurrently\n");
    printf("                                             Put PLUS and mention required attributes for the next files");
    printf(" -u  --device-nodeurl                      - URL of PCM device\n");
    printf("                                             in the following format card=x;device=x");
    printf("                                             this option is mandatory in working with USB HAL");
    printf(" -m  --mode                                - usb operating mode(Device Mode is default)\n");
    printf("                                             0:Device Mode(host drives the stream and its params and so no need to give params as input)\n");
    printf("                                             1:Host Mode(user can give stream and stream params via a stream(SD card file) or setup loopback with given params\n");
    printf(" -i  --intr-strm                           - interactive stream indicator\n");
    printf(" -C  --Device Config                       - Device Configuration params\n");
    printf("                                             Params should be in the order defined in struct qahw_device_cfg_param. Order is: \n");
    printf("                                             <sample_rate>, <channels>, <bit_width>, <format>, <device>, <channel_map[channels]>, <channel_allocation> \n");
    printf("                                             Example(6 channel HDMI config): hal_play_test -f /data/ChID16bit_5.1ch_48k.wav -v 0.9 -d 1024 -c 6 -C 48000 6 16 1 1024 1 2 6 3 4 5 19\n");
    printf(" \n Examples \n");
    printf(" hal_play_test -f /data/Anukoledenadu.wav  -> plays Wav stream with default params\n\n");
    printf(" hal_play_test -f /data/MateRani.mp3 -t 2 -d 2 -v 0.01 -r 44100 -c 2 \n");
    printf("                                          -> plays MP3 stream(-t = 2) on speaker device(-d = 2)\n");
    printf("                                          -> 2 channels and 44100 sample rate\n\n");
    printf(" hal_play_test -f /data/v1-CBR-32kHz-stereo-40kbps.mp3 -t 2 -d 33554432 -v 0.01 -r 32000 -c 2 -D /data/proxy_dump.wav\n");
    printf("                                          -> plays MP3 stream(-t = 2) on BT device in non-split path (-d = 33554432)\n");
    printf("                                          -> 2 channels and 32000 sample rate\n");
    printf("                                          -> dumps pcm data to file at /data/proxy_dump.wav\n\n");
    printf(" hal_play_test -f /data/v1-CBR-32kHz-stereo-40kbps.mp3 -t 2 -d 128 -v 0.01 -r 32000 -c 2 \n");
    printf("                                          -> plays MP3 stream(-t = 2) on BT device in split path (-d = 128)\n");
    printf("                                          -> 2 channels and 32000 sample rate\n");
    printf(" hal_play_test -f /data/AACLC-71-48000Hz-384000bps.aac  -t 4 -d 2 -v 0.05 -r 48000 -c 2 -a 1 \n");
    printf("                                          -> plays AAC-ADTS stream(-t = 4) on speaker device(-d = 2)\n");
    printf("                                          -> AAC format type is LC(-a = 1)\n");
    printf("                                          -> 2 channels and 48000 sample rate\n\n");
    printf(" hal_play_test -f /data/AACHE-adts-stereo-32000KHz-128000Kbps.aac  -t 4 -d 2 -v 0.05 -r 16000 -c 2 -a 3 \n");
    printf("                                          -> plays AAC-ADTS stream(-t = 4) on speaker device(-d = 2)\n");
    printf("                                          -> AAC format type is HE V2(-a = 3)\n");
    printf("                                          -> 2 channels and 16000 sample rate\n");
    printf("                                          -> note that the sample rate is half the actual sample rate\n\n");
    printf(" hal_play_test -f /data/2.0_16bit_48khz.m4a -k 1536000,16,0,0,4096,14,16388,0,10,2,40,48000,1536000,48000 -t 6 -r 48000 -c 2 -v 0.5 \n");
    printf("                                          -> Play alac clip (-t = 6)\n");
    printf("                                          -> kvpair(-k) values represent media-info of clip & values should be in below mentioned sequence\n");
    printf("                                          ->alac_avg_bit_rate,alac_bit_depth,alac_channel_layout,alac_compatible_version,\n");
    printf("                                          ->alac_frame_length,alac_kb,alac_max_frame_bytes,alac_max_run,alac_mb,\n");
    printf("                                          ->alac_num_channels,alac_pb,alac_sampling_rate,avg_bit_rate,sample_rate\n\n");
    printf(" hal_play_test -f /data/DIL CHAHTA HAI.flac -k 0,4096,13740,4096,14 -t 5 -r 48000 -c 2 -v 0.5 \n");
    printf("                                          -> Play flac clip (-t = 5)\n");
    printf("                                          -> kvpair(-k) values represent media-info of clip & values should be in below mentioned sequence\n");
    printf("                                          ->avg_bit_rate,flac_max_blk_size,flac_max_frame_size\n");
    printf("                                          ->flac_min_blk_size,flac_min_frame_size,sample_rate\n");
    printf(" hal_play_test -f /data/vorbis.mka -k 500000,48000,1 -t 7 -r 48000 -c 2 -v 0.5 \n");
    printf("                                          -> Play vorbis clip (-t = 7)\n");
    printf("                                          -> kvpair(-k) values represent media-info of clip & values should be in below mentioned sequence\n");
    printf("                                          ->avg_bit_rate,sample_rate,vorbis_bitstream_fmt\n");
    printf(" hal_play_test -f /data/file.wma -k 192000,48000,16,8192,3,15,0,0,353 -t 8 -w 1 -r 48000 -c 2 -v 0.5 \n");
    printf("                                          -> Play wma clip (-t = 8)\n");
    printf("                                          -> kvpair(-k) values represent media-info of clip & values should be in below mentioned sequence\n");
    printf("                                          ->avg_bit_rate,sample_rate,wma_bit_per_sample,wma_block_align\n");
    printf("                                          ->wma_channel_mask,wma_encode_option,wma_format_tag\n");
    printf(" hal_play_test -f /data/03_Kuch_Khaas_BE.btaptx -t 9 -d 2 -v 0.2 -r 44100 -c 2 -A 00:02:5b:00:ff:03 \n");
    printf("                                          -> Play aptx clip (-t = 9)\n");
    printf("                                          -> 2 channels and 44100 sample rate\n");
    printf("                                          -> BT addr: bt_addr=00:02:5b:00:ff:03\n\n");
    printf(" hal_play_test -f /data/silence.ac3 -t 9 -r 48000 -c 2 -v 0.05 -F 16433 -P -f /data/music_48k.ac3 -t 9 -r 48000 -c 2 -F 32817\n");
    printf("                                          -> Plays a silence clip as main stream and music clip as associated\n\n");
    printf(" hal_play_test -K -F 4                    -> Measure latency KPIs for low latency output\n\n");
    printf(" hal_play_test -f /data/Moto_320kbps.mp3 -t 2 -d 2 -v 0.1 -r 44100 -c 2 -e 2\n");
    printf("                                          -> plays MP3 stream(-t = 2) on speaker device(-d = 2)\n");
    printf("                                          -> 2 channels and 44100 sample rate\n\n");
    printf("                                          -> sound effect equalizer enabled\n\n");
    printf(" hal_play_test -d 2 -v 0.05 -s 2147487744 -u \"card=1\\;device=0\" \n");
    printf("                                          -> capture audio stream from usb device(HAL)@ 44.1 KHz, 2ch\n");
    printf("                                          -> and loop/play it back on to primary HAL \n\n");
    printf("                                          -> Device Mode is default\n");
    printf("                                          -> card=1\\;device=0 -> specifies the URL, pls not that the ';' is escaped\n\n");
    printf(" hal_play_test -f /data/Anukoledenadu.wav -d 16384 -u \"card=1\\;device=0\" \n");
    printf("                                          -> Play PCM to USB out\n\n");
    printf(" hal_play_test -d 16384 -u \"card=1\\;device=0\" -s 2\n");
    printf("                                          ->Capture PCM from Local Mic and play it on USB(usb to primary hal loopback)\n\n");
    printf(" hal_play_test -d 16384 -u \"card=1\\;device=0\" -s 2 -P -d 2 -v 0.05 -s 2147487744 -u \"card=1\\;device=0\"\n");
    printf("                                          ->full duplex, setup both primary to usb and usb to primary loopbacks\n");
    printf("                                          ->Note:-P separates the steam params for both the loopbacks\n");
    printf("                                          ->Note:all the USB device commmands(above) should be accompanied with the host side commands\n\n");
    printf("hal_play_test -f interactive_audio.wav -d 2 -l out.txt -k \"mixer_ctrl=pan_scale;c=1;o=6;I=fc;O=fl,fr,fc,lfe,bl,br;M=0.5,0.5,0,0,0,0\" -i 1\n");
    printf("                                          ->kv_pair for downmix or pan_scale should folow the above sequence, one can pass downmix & pan_scale params/coeff matrices. For each control params should be sent separately \n");
    printf("hal_play_test -f /data/ape_dsp.isf.0x152E.bitstream.0x10100400.0x2.0x12F32.rx.bin -k 16,73728,3990,2000,53808,32,2,44100,157,44100,1 -t 18 -r 48000 -c 2 -v 0.5 -d 131072");
    printf("                                          -> kvpair(-k) values represent media-info of clip & values should be in below mentioned sequence\n");
    printf("                                          ->bits_per_sample,blocks_per_frame,compatible_version,compression_level,final_frame_blocks,format_flags,num_channels,sample_rate,total_frames,sample_rate,seek_table_present \n");
}

int get_wav_header_length (FILE* file_stream)
{
    int subchunk_size = 0, wav_header_len = 0;

    fseek(file_stream, 16, SEEK_SET);
    if(fread(&subchunk_size, 4, 1, file_stream) != 1) {
        fprintf(log_file, "Unable to read subchunk:\n");
        fprintf(stderr, "Unable to read subchunk:\n");
        exit (1);
    }
    if(subchunk_size < 16) {
        fprintf(log_file, "This is not a valid wav file \n");
        fprintf(stderr, "This is not a valid wav file \n");
    } else {
         wav_header_len = FORMAT_DESCRIPTOR_SIZE + SUBCHUNK1_SIZE(subchunk_size) + SUBCHUNK2_SIZE;
    }
    return wav_header_len;
}

/* convert big-endian to little-endian */
uint64_t convert_BE_to_LE( uint64_t in)
{
    uint64_t out;
    char *p_in = (char *) &in;
    char *p_out = (char *) &out;
    p_out[0] = p_in[7];
    p_out[1] = p_in[6];
    p_out[2] = p_in[5];
    p_out[3] = p_in[4];
    p_out[4] = p_in[3];
    p_out[5] = p_in[2];
    p_out[6] = p_in[1];
    p_out[7] = p_in[0];
    return out;
}

static ssize_t  get_bytes_to_read(FILE* file, int file_type)
{
     char keyword[DTSHD_META_KEYWORD_SIZE + 1];
     bool is_dtshd_stream =false;
     uint64_t read_chunk_size = 0;
     uint64_t chunk_size = 0;
     ssize_t file_read_size = -1;
     ssize_t header_read_size = -1;
     long int pos;
     int ret = 0;

     if (file_type == FILE_DTS) {

         //first locate the ASCII header "DTSHDHDR"identifier
         while (!feof(file) && (header_read_size < 1024) &
                (fread(&keyword, sizeof(char), DTSHD_META_KEYWORD_SIZE, file)
                                 == DTSHD_META_KEYWORD_SIZE)) {
             //update the number of bytes was read for identifying the header
             header_read_size = ftell(file);

             if (strncmp(keyword, DTSHD_CHUNK_HEADER_KEYWORD,
                         DTSHD_META_KEYWORD_SIZE) == 0) {
                 // read the 8-byte size field
                 if (fread(&read_chunk_size, sizeof(char),
                     DTSHD_META_KEYWORD_SIZE, file) == DTSHD_META_KEYWORD_SIZE) {
                     is_dtshd_stream = true;
                     chunk_size = convert_BE_to_LE(read_chunk_size);
                     pos = ftell(file);
                     fseek(file, chunk_size, SEEK_CUR);
                     fprintf(stderr,"DTS header chunk offset:%lu and chunk_size:%llu \n",
                             pos, chunk_size);
                     break;
                 }
                 else {
                     printf(" file read error \n");
                     break;
                 } //end reading chunk size
             }
         }

         if (!is_dtshd_stream)  {
             fprintf(stderr, "raw dts hd stream");
             fseek(file, 0, SEEK_SET);
             return file_read_size;
         }
         /* parsing each chunk data */
         while (!feof(file) &&
               fread(&keyword, sizeof(uint8_t), DTSHD_META_KEYWORD_SIZE, file)
                                     == DTSHD_META_KEYWORD_SIZE) {
            /* check for the stream audio data */
            ret  = strncmp(keyword,
                        DTSHD_CHUNK_STREAM_KEYWORD,
                        DTSHD_META_KEYWORD_SIZE);
            if (!ret) {
                ret = fread(&read_chunk_size, 1, DTSHD_META_KEYWORD_SIZE, file);
                chunk_size = convert_BE_to_LE(read_chunk_size);
                if (ret != DTSHD_META_KEYWORD_SIZE) {
                    fprintf(stderr,"%s %d file read error ret %d \n",
                            __func__, __LINE__, ret);
                    file_read_size = -EINVAL;
                    break;
                }
                file_read_size =  chunk_size;
                fprintf(stderr, "DTS read_chunk_size %llu and file_read_size: %zd\n",
                        chunk_size,
                        file_read_size);
                break;
            } else {
                fprintf(log_file, "Identified chunk of %c %c %c %c %c %c %c %c \n",
                        keyword[0], keyword[1], keyword[2], keyword[3],
                        keyword[4], keyword[5], keyword[6], keyword[7] );
                ret = fread(&read_chunk_size, 1, DTSHD_META_KEYWORD_SIZE, file);
                pos = ftell(file);
                chunk_size = convert_BE_to_LE(read_chunk_size);
                fseek(file, chunk_size, SEEK_CUR);
            }
        }
     }
     return file_read_size;
}

qahw_module_handle_t * load_hal(audio_devices_t dev) {
    qahw_module_handle_t *hal = NULL;

    if ((AUDIO_DEVICE_IN_USB_DEVICE == dev) ||
        (AUDIO_DEVICE_OUT_USB_DEVICE == dev)){
        if (!usb_hal_handle) {
            fprintf(log_file,"\nLoading usb HAL\n");
            if ((usb_hal_handle = qahw_load_module(QAHW_MODULE_ID_USB)) == NULL) {
                fprintf(log_file,"failure in Loading usb HAL\n");
                fprintf(stderr,"failure in Loading usb HAL\n");
                return NULL;
            }
        }
        hal = usb_hal_handle;
    }
/*  else if ((AUDIO_DEVICE_IN_BLUETOOTH_A2DP == dev) ||
               (AUDIO_DEVICE_OUT_BLUETOOTH_A2DP == dev)){
        if (!bt_hal_handle) {
            fprintf(log_file,"Loading BT HAL\n");
            if ((bt_hal_handle = qahw_load_module(QAHW_MODULE_ID_A2DP)) == NULL) {
                fprintf(log_file,"failure in Loading BT HAL\n");
                fprintf(stderr,"failure in Loading BT HAL\n");
                return NULL;
            }
        }
        hal = bt_hal_handle;
    }*/
    else {
        if (!primary_hal_handle) {
            fprintf(log_file,"\nLoading Primary HAL\n");
            if ((primary_hal_handle = qahw_load_module(QAHW_MODULE_ID_PRIMARY)) == NULL) {
                fprintf(log_file,"failure in Loading Primary HAL\n");
                fprintf(stderr,"failure in Loading primary HAL\n");
                return NULL;
            }
        }
        hal = primary_hal_handle;
    }
    return hal;
}
/*
 * this function unloads all the loaded hal modules so this should be called
 * after all the stream playback are concluded.
 */
int unload_hals() {

    if (usb_hal_handle) {
        fprintf(log_file,"\nUnLoading usb HAL\n");
        if (qahw_unload_module(usb_hal_handle) < 0) {
            fprintf(log_file,"failure in Un Loading usb HAL\n");
            fprintf(stderr,"failure in Un Loading usb HAL\n");
            return -1;
        }
    }
    if (bt_hal_handle) {
        fprintf(log_file,"UnLoading BT HAL\n");
        if (qahw_unload_module(bt_hal_handle) < 0) {
            fprintf(log_file,"failure in UnLoading BT HAL\n");
            fprintf(stderr,"failure in Un Loading BT HAL\n");
            return -1;
        }
    }
    if (primary_hal_handle) {
        fprintf(log_file,"\nUnLoading Primary HAL\n");
        if (qahw_unload_module(primary_hal_handle) < 0) {
            fprintf(log_file,"failure in Un Loading Primary HAL\n");
            fprintf(stderr,"failure in Un Loading primary HAL\n");
            return -1;
        }
    }
    return 1;
}

audio_channel_mask_t get_channel_mask_for_name(char *name) {
    audio_channel_mask_t channel_type = AUDIO_CHANNEL_INVALID;
    if (NULL == name)
        return channel_type;
    else if (strncmp(name, "fl", 2) == 0)
        channel_type = QAHW_PCM_CHANNEL_FL;
    else if (strncmp(name, "fr", 2) == 0)
        channel_type = QAHW_PCM_CHANNEL_FR;
    else if (strncmp(name, "fc", 2) == 0)
        channel_type = QAHW_PCM_CHANNEL_FC;
    else if (strncmp(name, "lfe", 2) == 0)
        channel_type = QAHW_PCM_CHANNEL_LFE;
    else if (strncmp(name, "bl", 2) == 0)
        channel_type = QAHW_PCM_CHANNEL_LB;
    else if (strncmp(name, "br", 2) == 0)
        channel_type = QAHW_PCM_CHANNEL_RB;
    else if (strncmp(name, "flc", 3) == 0)
        channel_type = QAHW_PCM_CHANNEL_FLC;
    else if (strncmp(name, "frc", 3) == 0)
        channel_type = QAHW_PCM_CHANNEL_FRC;
    else if (strncmp(name, "cs", 2) == 0)
        channel_type = QAHW_PCM_CHANNEL_CS;
    else if (strncmp(name, "ls", 2) == 0)
        channel_type = QAHW_PCM_CHANNEL_LS;
    else if (strncmp(name, "rs", 2) == 0)
        channel_type = QAHW_PCM_CHANNEL_RS;
    else if (strncmp(name, "ts", 2) == 0)
        channel_type = QAHW_PCM_CHANNEL_TS;
    else if (strncmp(name, "cvh", 3) == 0)
        channel_type = QAHW_PCM_CHANNEL_CVH;
    else if (strncmp(name, "ms", 3) == 0)
        channel_type = QAHW_PCM_CHANNEL_MS;
    else if (strncmp(name, "rlc", 3) == 0)
        channel_type = QAHW_PCM_CHANNEL_RLC;
    else if (strncmp(name, "rrc", 3) == 0)
        channel_type = QAHW_PCM_CHANNEL_RRC;

    return channel_type;
}

int extract_channel_mapping(uint16_t *channel_map, const char * arg_string){

    char *token_string = NULL;
    char *init_ptr = NULL;
    char *token = NULL;
    char *saveptr = NULL;
    int rc = 0;

    if (NULL == channel_map)
        return -EINVAL;

    if (NULL == arg_string)
        return EINVAL;

    token_string = strdup(arg_string);

    if(token_string != NULL) {
        init_ptr = token_string;
        token = strtok_r(token_string, ",", &saveptr);
        int index = 0;
        if (NULL == token) {
            rc = -EINVAL;
            goto exit;
        }
        else
            channel_map[index++] = get_channel_mask_for_name(token);

        while(NULL !=(token = strtok_r(NULL,",",&saveptr)))
            channel_map[index++] = get_channel_mask_for_name(token);

        goto exit;
    } else
        return -EINVAL;
exit:
    free(init_ptr);
    init_ptr = NULL;
    token_string = NULL;
    return rc;
}

int extract_mixer_coeffs(qahw_mix_matrix_params_t * mm_params, const char * arg_string){

    char *token_string = NULL;
    char *init_ptr = NULL;
    char *token = NULL;
    char *saveptr = NULL;
    int i = 0;
    int j = 0, rc = 0;

    if (NULL == mm_params)
        return -EINVAL;

    if (NULL == arg_string)
        return -EINVAL;

    token_string = strdup(arg_string);

    if(token_string != NULL) {
        init_ptr = token_string;
        token = strtok_r(token_string, ",", &saveptr);
        if (NULL == token) {
            rc = -EINVAL;
            goto exit;
        }
        else {
            mm_params->mixer_coeffs[i][j] = atof(token);
            j++;
        }

        while(NULL !=(token = strtok_r(NULL,",",&saveptr))) {
            if(j == mm_params->num_input_channels) {
                j=0;
                i++;
            }
            if(i == mm_params->num_output_channels)
                break;
            mm_params->mixer_coeffs[i][j++] = atof(token);
        }
        goto exit;
    } else
        return -EINVAL;
exit:
    free(init_ptr);
    init_ptr = NULL;
    token_string = NULL;
    return rc;
}

#ifdef QAP
int start_playback_through_qap(char * kvp_string, int num_of_streams,  qahw_module_handle_t *hal_handle) {
    stream_config *stream = NULL;
    int rc = 0;
    int i;

    fprintf(stdout, "kvp_string %s and num_of_streams %d\n", kvp_string, num_of_streams);
    for (i = 0; i < num_of_streams; i++) {
        stream = &stream_param[i];
        if (stream->filename) {
            if ((stream->file_stream = fopen(stream->filename, "r"))== NULL) {
                fprintf(log_file, "Cannot open audio file %s\n", stream->filename);
                fprintf(stderr, "Cannot open audio file %s\n", stream->filename);
                return -EINVAL;
            }
        }
        get_file_format(stream);
        fprintf(stdout, "Playing from:%s\n", stream->filename);
        qap_module_handle_t qap_module_handle = NULL;
        if (!qap_wrapper_session_active) {
            rc = qap_wrapper_session_open(kvp_string, stream, num_of_streams, hal_handle);
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
                rc = pthread_create(&playback_thread[i], NULL, qap_wrapper_start_stream, (void *)&stream_param[i]);
                if (rc) {
                    fprintf(stderr, "stream %d: failed to create thread\n", stream->stream_index);
                    return -EINVAL;
                }
                thread_active[i] = true;
            }
        }
    }
    return 0;
}
#endif

static void qti_audio_server_death_notify_cb(void *ctxt) {
    fprintf(log_file, "qas died\n");
    fprintf(stderr, "qas died\n");

    stream_config *s_params = (stream_config*) ctxt;
    pthread_cond_signal(&s_params->write_cond);
    pthread_cond_signal(&s_params->drain_cond);
    stop_playback = true;
}

int main(int argc, char* argv[]) {
    char *ba = NULL;
    qahw_param_payload payload;
    qahw_param_id param_id;
    struct qahw_aptx_dec_param aptx_params;
    int rc = 0;
    int i = 0;
    int iter_i = 0;
    int iter_j = 0;
    int chmap_iter = 0;

    kpi_mode = false;
    char mixer_ctrl_name[64] = {0};
    char input_ch[64] = {0};
    char output_ch[64] = {0};
    char input_ch_map[64] = {0};
    char output_ch_map[64] = {0};
    char mixer_coeff[64] = {0};
    event_trigger = false;
    bool wakelock_acquired = false;

    log_file = stdout;
    proxy_params.acp.file_name = "/data/pcm_dump.wav";
    stream_config *stream = NULL;

    struct qahw_device_cfg_param device_cfg_params;
    bool send_device_config = false;

    init_streams();

    int num_of_streams = 1;
    char kvp_string[KV_PAIR_MAX_LENGTH] = {0};

    struct option long_options[] = {
        /* These options set a flag. */
        {"file-path",     required_argument,    0, 'f'},
        {"output-device", required_argument,    0, 'd'},
        {"input-device",  required_argument,    0, 's'},
        {"sample-rate",   required_argument,    0, 'r'},
        {"channels",      required_argument,    0, 'c'},
        {"bitwidth",      required_argument,    0, 'b'},
        {"volume",        required_argument,    0, 'v'},
        {"enable-dump",   required_argument,    0, 'V'},
        {"log-file",      required_argument,    0, 'l'},
        {"dump-file",     required_argument,    0, 'D'},
        {"file-type",     required_argument,    0, 't'},
        {"aac-type",      required_argument,    0, 'a'},
        {"wma-type",      required_argument,    0, 'w'},
        {"kvpairs",       required_argument,    0, 'k'},
        {"flags",         required_argument,    0, 'F'},
        {"kpi-mode",      no_argument,          0, 'K'},
        {"plus",          no_argument,          0, 'P'},
        {"event-trigger", no_argument,          0, 'E'},
        {"effect-path",   required_argument,    0, 'e'},
        {"bt-addr",       required_argument,    0, 'A'},
        {"query drift",   no_argument,          0, 'q'},
        {"drift correction",   no_argument,     0, 'Q'},
        {"device-nodeurl",required_argument,    0, 'u'},
        {"mode",          required_argument,    0, 'm'},
        {"effect-preset",   required_argument,    0, 'p'},
        {"effect-strength", required_argument,    0, 'S'},
        {"render-format", required_argument,    0, 'x'},
        {"timestamp-file", required_argument,    0, 'y'},
        {"intr-strm",    required_argument,    0, 'i'},
        {"device-config", required_argument,    0, 'C'},
        {"play-list",    required_argument,    0, 'g'},
        {"ec-ref",        no_argument,         0, 'L'},
        {"help",          no_argument,          0, 'h'},
        {"bt-wbs",        no_argument,    0, 'z'},
        {0, 0, 0, 0}
    };

    int opt = 0;
    int option_index = 0;

    proxy_params.hdr.riff_id = ID_RIFF;
    proxy_params.hdr.riff_sz = 0;
    proxy_params.hdr.riff_fmt = ID_WAVE;
    proxy_params.hdr.fmt_id = ID_FMT;
    proxy_params.hdr.fmt_sz = 16;
    proxy_params.hdr.audio_format = FORMAT_PCM;
    proxy_params.hdr.num_channels = 2;
    proxy_params.hdr.sample_rate = 44100;
    proxy_params.hdr.byte_rate = proxy_params.hdr.sample_rate * proxy_params.hdr.num_channels * 2;
    proxy_params.hdr.block_align = proxy_params.hdr.num_channels * 2;
    proxy_params.hdr.bits_per_sample = 16;
    proxy_params.hdr.data_id = ID_DATA;
    proxy_params.hdr.data_sz = 0;

    while ((opt = getopt_long(argc,
                              argv,
                              "-f:r:c:b:d:s:v:V:l:t:a:w:k:PD:KF:Ee:A:u:m:S:C:p::x:y:qQzLh:i:h:g:O:",
                              long_options,
                              &option_index)) != -1) {

        fprintf(log_file, "for argument %c, value is %s\n", opt, optarg);

        switch (opt) {
        case 'f':
            stream_param[i].filename = optarg;
            break;
        case 'r':
            stream_param[i].config.offload_info.sample_rate = atoi(optarg);
            stream_param[i].config.sample_rate = atoi(optarg);
            break;
        case 'c':
            stream_param[i].channels = atoi(optarg);
            stream_param[i].config.channel_mask = audio_channel_out_mask_from_count(atoi(optarg));
            break;
        case 'b':
            stream_param[i].config.offload_info.bit_width = atoi(optarg);
            break;
        case 'd':
            stream_param[i].output_device = atoll(optarg);
            break;
        case 's':
            stream_param[i].input_device = atoll(optarg);
            break;
        case 'v':
            vol_level = atof(optarg);
            break;
        case 'V':
            enable_dump = atof(optarg);
            break;
        case 'z':
            stream_param[i].bt_wbs = true;
            break;
        case 'l':
            log_filename = optarg;
            if (strcasecmp(log_filename, "stdout") &&
                strcasecmp(log_filename, "1") &&
                (log_file = fopen(log_filename,"wb")) == NULL) {
                fprintf(log_file, "Cannot open log file %s\n", log_filename);
                fprintf(stderr, "Cannot open log file %s\n", log_filename);
                /* continue to log to std out. */
                log_file = stdout;
            }
            break;
        case 'i':
            stream_param[i].interactive_strm = atoi(optarg);
            break;
        case 't':
            stream_param[i].filetype = atoi(optarg);
            break;
        case 'a':
            stream_param[i].aac_fmt_type = atoi(optarg);
            break;
        case 'w':
            stream_param[i].wma_fmt_type = atoi(optarg);
            break;
        case 'k':
            get_kvpairs_string(optarg, "mixer_ctrl", mixer_ctrl_name);
            printf("%s, mixer_ctrl_name- %s\n", __func__, mixer_ctrl_name);
            if(strncmp(mixer_ctrl_name, "downmix", 7) == 0) {
                stream_param[i].mix_ctrl = QAHW_PARAM_CH_MIX_MATRIX_PARAMS;

                get_kvpairs_string(optarg, "c", input_ch);
                stream_param[i].mm_params_downmix.num_input_channels = atoi(input_ch);
                get_kvpairs_string(optarg, "o", output_ch);
                stream_param[i].mm_params_downmix.num_output_channels = atoi(output_ch);
                get_kvpairs_string(optarg, "I", input_ch_map);
                get_kvpairs_string(optarg, "O", output_ch_map);
                get_kvpairs_string(optarg, "M", mixer_coeff);

                extract_channel_mapping((uint16_t *)(stream_param[i].mm_params_downmix.input_channel_map), input_ch_map);
                stream_param[i].mm_params_downmix.has_input_channel_map = 1;
                fprintf(log_file, "\ndownmix Input channel mapping: ");
                for (iter_i= 0; iter_i < stream_param[i].mm_params_downmix.num_input_channels; iter_i++) {
                    fprintf(log_file, "0x%x, ", stream_param[i].mm_params_downmix.input_channel_map[iter_i]);
                }

                extract_channel_mapping((uint16_t *)(stream_param[i].mm_params_downmix.output_channel_map), output_ch_map);
                stream_param[i].mm_params_downmix.has_output_channel_map = 1;
                fprintf(log_file, "\ndownmix Output channel mapping: ");
                for (iter_i = 0; iter_i < stream_param[i].mm_params_downmix.num_output_channels; iter_i++)
                    fprintf(log_file, "0x%x, ", stream_param[i].mm_params_downmix.output_channel_map[iter_i]);


                extract_mixer_coeffs(&stream_param[i].mm_params_downmix, mixer_coeff);
                stream_param[i].mm_params_downmix.has_mixer_coeffs = 1;
                fprintf(log_file, "\ndownmix mixer coeffs:\n");
                for (iter_i = 0; iter_i < stream_param[i].mm_params_downmix.num_output_channels; iter_i++){
                    for (iter_j = 0; iter_j < stream_param[i].mm_params_downmix.num_input_channels; iter_j++){
                        fprintf(log_file, "%.2f ",stream_param[i].mm_params_downmix.mixer_coeffs[iter_i][iter_j]);
                    }
                    fprintf(log_file, "\n");
                }

            } else if(strncmp(mixer_ctrl_name, "pan_scale", 9) == 0) {
                stream_param[i].pan_scale_ctrl = QAHW_PARAM_OUT_MIX_MATRIX_PARAMS;

                get_kvpairs_string(optarg, "c", input_ch);
                stream_param[i].mm_params_pan_scale.num_input_channels = atoi(input_ch);
                get_kvpairs_string(optarg, "o", output_ch);
                stream_param[i].mm_params_pan_scale.num_output_channels = atoi(output_ch);
                get_kvpairs_string(optarg, "I", input_ch_map);
                get_kvpairs_string(optarg, "O", output_ch_map);
                get_kvpairs_string(optarg, "M", mixer_coeff);

                extract_channel_mapping((uint16_t *)(stream_param[i].mm_params_pan_scale.input_channel_map), input_ch_map);
                stream_param[i].mm_params_pan_scale.has_input_channel_map = 1;
                fprintf(log_file, "\n pan_sclae Input channel mapping: ");
                for (iter_i= 0; iter_i < stream_param[i].mm_params_pan_scale.num_input_channels; iter_i++) {
                    fprintf(log_file, "0x%x, ", stream_param[i].mm_params_pan_scale.input_channel_map[iter_i]);
                }

                extract_channel_mapping((uint16_t *)(stream_param[i].mm_params_pan_scale.output_channel_map), output_ch_map);
                stream_param[i].mm_params_pan_scale.has_output_channel_map = 1;
                fprintf(log_file, "\n pan_scale Output channel mapping: ");
                for (iter_i = 0; iter_i < stream_param[i].mm_params_pan_scale.num_output_channels; iter_i++)
                    fprintf(log_file, "0x%x, ", stream_param[i].mm_params_pan_scale.output_channel_map[iter_i]);

                extract_mixer_coeffs(&stream_param[i].mm_params_pan_scale, mixer_coeff);
                stream_param[i].mm_params_pan_scale.has_mixer_coeffs = 1;
                fprintf(log_file, "\n pan_scale mixer coeffs:\n");
                for (iter_i = 0; iter_i < stream_param[i].mm_params_pan_scale.num_output_channels; iter_i++){
                    for (iter_j = 0; iter_j < stream_param[i].mm_params_pan_scale.num_input_channels; iter_j++){
                        fprintf(log_file, "%.2f ",stream_param[i].mm_params_pan_scale.mixer_coeffs[iter_i][iter_j]);
                    }
                    fprintf(log_file, "\n");
                }

            } else {
                stream_param[i].kvpair_values = optarg;
            }
            break;
        case 'D':
            proxy_params.acp.file_name = optarg;
            break;
        case 'K':
            kpi_mode = true;
            break;
        case 'O':
            stream_param[i].set_params = optarg;
            break;
        case 'F':
            stream_param[i].flags = atoll(optarg);
            stream_param[i].flags_set = true;
            break;
        case 'E':
            event_trigger = true;
            break;
        case 'e':
            stream_param[i].effect_index = atoi(optarg);
            if (stream_param[i].effect_index < 0 || stream_param[i].effect_index >= EFFECT_MAX) {
                fprintf(log_file, "Invalid effect type %d\n", stream_param[i].effect_index);
                fprintf(stderr, "Invalid effect type %d\n", stream_param[i].effect_index);
                stream_param[i].effect_index = -1;
            } else if (stream_param[i].effect_index == 3) {
                // visualizer is a special effect that is not perceivable by hearing
                // hence, add as an exception in test app.
                fprintf(log_file, "visualizer effect testing is not available\n");
                stream_param[i].effect_index = -1;
            } else {
                stream_param[i].ethread_func = effect_thread_funcs[stream_param[i].effect_index];
            }
            break;
        case 'p':
            stream_param[i].effect_preset_strength = atoi(optarg);
            break;
        case 'S':
            stream_param[i].effect_preset_strength = atoi(optarg);
            break;
        case 'A':
            ba = optarg;
            break;
        case 'q':
             stream_param[i].drift_query = true;
             break;
        case 'Q':
             stream_param[i].drift_query = true;
             stream_param[i].drift_correction = true;
             break;
        case 'P':
            if(i >= MAX_PLAYBACK_STREAMS - 1) {
                fprintf(log_file, "cannot have more than %d streams\n", MAX_PLAYBACK_STREAMS);
                fprintf(stderr, "cannot have more than %d streams\n", MAX_PLAYBACK_STREAMS);
                return 0;
            }
            i++;
            fprintf(log_file, "Stream index incremented to %d\n", i);
            break;
        case 'u':
            stream_param[i].device_url = optarg;
            break;
        case 'm':
            stream_param[i].usb_mode = atoi(optarg);
            break;
        case 'x':
            render_format = atoi(optarg);
            break;
        case 'L':
            ec_ref = true;
            break;
        case 'y':
            stream_param[i].timestamp_filename = optarg;
            break;
        case 'C':
            fprintf(log_file, " In Device config \n");
            fprintf(stderr, " In Device config \n");
            send_device_config = true;

            memset(&device_cfg_params, 0, sizeof(struct qahw_device_cfg_param));

            //Read Sample Rate
            if (optind < argc && *argv[optind] != '-') {
                 device_cfg_params.sample_rate = atoi(optarg);
                 fprintf(log_file, " Device config ::::  sample_rate - %d \n", device_cfg_params.sample_rate);
                 fprintf(stderr, " Device config :::: sample_rate - %d \n", device_cfg_params.sample_rate);
            }

            //Read Channels
            if (optind < argc && *argv[optind] != '-') {
                 device_cfg_params.channels = atoi(argv[optind]);
                 optind++;
                 fprintf(log_file, " Device config :::: channels - %d \n", device_cfg_params.channels);
                 fprintf(stderr, " Device config :::: channels - %d \n", device_cfg_params.channels);
            }

            //Read Bit width
            if (optind < argc && *argv[optind] != '-') {
                 device_cfg_params.bit_width = atoi(argv[optind]);
                 optind++;
                 fprintf(log_file, " Device config :::: bit_width - %d \n", device_cfg_params.bit_width);
                 fprintf(stderr, " Device config :::: bit_width - %d \n", device_cfg_params.bit_width);
            }

            //Read Format
            if (optind < argc && *argv[optind] != '-') {
                 device_cfg_params.format = atoi(argv[optind]);
                 optind++;
                 fprintf(log_file, " Device config :::: format - %d \n", device_cfg_params.format);
                 fprintf(stderr, " Device config :::: format - %d \n", device_cfg_params.format);
            }

            //Read Device
            if (optind < argc && *argv[optind] != '-') {
                 device_cfg_params.device = atoi(argv[optind]);
                 optind++;
                 fprintf(log_file, " Device config :::: device - %d \n", device_cfg_params.device);
                 fprintf(stderr, " Device config :::: device - %d \n", device_cfg_params.device);
            }

            //Read Channel Map
            while ((optind < argc && *argv[optind] != '-') && (chmap_iter < device_cfg_params.channels)) {
                 device_cfg_params.channel_map[chmap_iter] = atoi(argv[optind]);
                 optind++;
                 fprintf(log_file, " Device config :::: channel_map[%d] - %d \n", chmap_iter, device_cfg_params.channel_map[chmap_iter]);
                 fprintf(stderr, " Device config :::: channel_map[%d] - %d \n", chmap_iter, device_cfg_params.channel_map[chmap_iter]);
                 chmap_iter++;
            }

            //Read Channel Allocation
            if (optind < argc && *argv[optind] != '-') {
                 device_cfg_params.channel_allocation = atoi(argv[optind]);
                 optind++;
                 fprintf(log_file, " Device config :::: channel_allocation - %d \n", device_cfg_params.channel_allocation);
                 fprintf(stderr, " Device config :::: channel_allocation - %d \n", device_cfg_params.channel_allocation);
            }
            break;
        case 'g':
            break;
        case 'h':
            usage();
            hal_test_qap_usage();
            return 0;
            break;

        }
    }
    fprintf(log_file, "registering qas callback");
    qahw_register_qas_death_notify_cb((audio_error_callback)qti_audio_server_death_notify_cb, &stream_param);

    wakelock_acquired = request_wake_lock(wakelock_acquired, true);
    num_of_streams = i+1;
    /* Caution: Below ADL log shouldnt be altered without notifying automation APT since it used
     * for automation testing
     */
    fprintf(log_file, "ADL: Starting audio hal tests for streams : %d\n", num_of_streams);

    if (kpi_mode == true && num_of_streams > 1) {
        fprintf(log_file, "kpi-mode is not supported for multi-playback usecase\n");
        fprintf(stderr, "kpi-mode is not supported for multi-playback usecase\n");
        goto exit;
    } else if (event_trigger == true && num_of_streams > 1) {
        fprintf(log_file, "event_trigger is not supported for multi-playback usecase\n");
        fprintf(stderr, "event_trigger is not supported for multi-playback usecase\n");
        goto exit;
    }

    if (num_of_streams > 1 && stream_param[num_of_streams-1].output_device & AUDIO_DEVICE_OUT_PROXY) {
        fprintf(log_file, "Proxy thread is not supported for multi-playback usecase\n");
        fprintf(stderr, "Proxy thread is not supported for multi-playback usecase\n");
        goto exit;
    }

    /* Register the SIGINT to close the App properly */
    if (signal(SIGINT, stop_signal_handler) == SIG_ERR) {
        fprintf(log_file, "Failed to register SIGINT:%d\n", errno);
        fprintf(stderr, "Failed to register SIGINT:%d\n", errno);
    }

    /* Register the SIGTERM to close the App properly */
    if (signal(SIGTERM, stop_signal_handler) == SIG_ERR) {
        fprintf(log_file, "Failed to register SIGTERM:%d\n", errno);
        fprintf(stderr, "Failed to register SIGTERM:%d\n", errno);
    }

    /* Check for Dual main content */
    if (num_of_streams >= 2) {
         is_dual_main = true;

         for(i = 0; i < num_of_streams; i++) {
              fprintf(log_file, "is_dual_main - %d  stream_param[i].flags - %d\n", is_dual_main, stream_param[i].flags);
              is_dual_main = is_dual_main && (stream_param[i].flags & AUDIO_OUTPUT_FLAG_MAIN);
              fprintf(log_file, "is_dual_main - %d  stream_param[i].flags - %d\n", is_dual_main, stream_param[i].flags);
         }

    }

    if (is_qap_session_active(argc, argv, kvp_string)) {
        char *file_name = NULL;
        char *cmd_kvp_str[100] = {NULL};
        char *play_list_kvp_str[100] = {NULL};
        int i = 0, j = 0;
        qahw_module_handle_t *qap_out_hal_handle = NULL;

        stream = &stream_param[i];
        qap_out_hal_handle = load_hal(stream->output_device);
        if (qap_out_hal_handle == NULL) {
            fprintf(stderr, "Failed log load HAL\n");
            goto exit;
        }

        file_name = (char*) check_for_playlist(kvp_string);
        fprintf(stderr, "%s file_name is %s \n", __FUNCTION__, file_name);
        if (file_name != NULL) {
            FILE *fp = fopen(file_name, "r+");
            if (fp != NULL) {
                get_play_list(fp, &stream_param, &num_of_streams, cmd_kvp_str);
                for (j = 0; j < num_of_streams; j++) {
                     play_list_kvp_str[j] = strdup(cmd_kvp_str[j]);
                }
                rc = start_playback_through_qap_playlist(play_list_kvp_str, num_of_streams, kvp_string, stream_param, qap_wrapper_session_active, qap_out_hal_handle);
                if (rc != 0) {
                    fprintf(stderr, "QAP playback failed\n");
                }
            } else {
                fprintf(stderr, "%s file open failed\nnd errno is %d", __FUNCTION__, errno);
            }
        } else {
            rc = start_playback_through_qap(kvp_string, num_of_streams, qap_out_hal_handle);
            if (rc != 0) {
                fprintf(stderr, "QAP playback failed\n");
            }
        }
        goto exit;
    }
    for (i = 0; i < num_of_streams; i++) {
        stream = &stream_param[i];

        if ((kpi_mode == false) &&
            (AUDIO_DEVICE_NONE == stream->input_device)){
                if (stream_param[PRIMARY_STREAM_INDEX].filename == nullptr) {
                    fprintf(log_file, "Primary file name is must for non kpi-mode\n");
                    fprintf(stderr, "Primary file name is must for non kpi-mode\n");
                    goto exit;
                }
        }

        if (stream->output_device != AUDIO_DEVICE_NONE) {
            if ((stream->qahw_out_hal_handle = load_hal(stream->output_device)) <= 0)
                goto exit;

            /* Turn BT_SCO on if bt_sco recording */
            if(audio_is_bluetooth_sco_device(stream->output_device)) {
                int ret = -1;
                const char * bt_sco_on = "BT_SCO=on";
                ret = qahw_set_parameters(stream->qahw_out_hal_handle, bt_sco_on);
                fprintf(log_file, " param %s set to hal with return value %d\n", bt_sco_on, ret);
            }
        }

        if (stream->input_device != AUDIO_DEVICE_NONE)
            if ((stream->qahw_in_hal_handle = load_hal(stream->input_device))== 0)
                goto exit;

        if ((AUDIO_DEVICE_NONE != stream->output_device) &&
            (AUDIO_DEVICE_NONE != stream->input_device))
            /*
             * hal loopback at what params we need to probably detect.
            */
            if(detect_stream_params(stream) < 0)
                goto exit;

        if (stream->filename) {
            if ((stream->file_stream = fopen(stream->filename, "r"))== NULL) {
                fprintf(log_file, "Cannot open audio file %s\n", stream->filename);
                fprintf(stderr, "Cannot open audio file %s\n", stream->filename);
                goto exit;
            }
            fprintf(log_file, "Playing from:%s\n", stream->filename);
            get_file_format(&stream_param[i]);
        } else if (AUDIO_DEVICE_NONE != stream->input_device) {
            fprintf(log_file, "Playing from device:%x\n", stream->input_device);
            fprintf(log_file, "Playing from url:%s\n", stream->device_url);
            fprintf(log_file, "setting up input hal and stream:%s\n", stream->device_url);

            rc = qahw_open_input_stream(stream->qahw_in_hal_handle,
                                    stream->handle,
                                    stream->input_device,
                                    &(stream->config),
                                    &(stream->in_handle),
                                    AUDIO_INPUT_FLAG_NONE,
                                    stream->device_url,
                                    AUDIO_SOURCE_UNPROCESSED);
            if (rc) {
                fprintf(log_file, "input stream could not be re-opened\n");
                fprintf(stderr, "input stream could not be re-opened\n");
                return rc;
            }
            stream->flags = AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD|AUDIO_OUTPUT_FLAG_NON_BLOCKING;
            stream->flags |= AUDIO_OUTPUT_FLAG_DIRECT;
        } else if (kpi_mode == true)
            stream->config.format = stream->config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;

        if (stream->output_device & AUDIO_DEVICE_OUT_PROXY)
            fprintf(log_file, "Saving pcm data to file: %s\n", proxy_params.acp.file_name);

        /* Set device connection state for HDMI */
        if ((stream->output_device & AUDIO_DEVICE_OUT_AUX_DIGITAL) ||
            (stream->output_device & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP)) {
            char param[100] = {0};
            uint32_t device = 0;

            if (stream->output_device & AUDIO_DEVICE_OUT_AUX_DIGITAL)
                device = AUDIO_DEVICE_OUT_AUX_DIGITAL;
            else if (stream->output_device & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP)
                device = AUDIO_DEVICE_OUT_BLUETOOTH_A2DP;

            snprintf(param, sizeof(param), "%s=%d", "connect", device);
            qahw_set_parameters(stream->qahw_out_hal_handle, param);
            fprintf(log_file, "Sending Connect Event: %s\n", param);
            fprintf(stderr, "Sending Connect Event: %s\n", param);
        }

        fprintf(log_file, "stream %d: File Type:%d\n", stream->stream_index, stream->filetype);
        fprintf(log_file, "stream %d: Audio Format:%d\n", stream->stream_index, stream->config.offload_info.format);
        fprintf(log_file, "stream %d: Output Device:%d\n", stream->stream_index, stream->output_device);
        fprintf(log_file, "stream %d: Output Flags:%d\n", stream->stream_index, stream->flags);
        fprintf(log_file, "stream %d: Sample Rate:%d\n", stream->stream_index, stream->config.offload_info.sample_rate);
        fprintf(log_file, "stream %d: Channels:%d\n", stream->stream_index, stream->channels);
        fprintf(log_file, "stream %d: Channel Mask:%x\n", stream->stream_index, stream->config.channel_mask);
        fprintf(log_file, "stream %d: Bitwidth:%d\n", stream->stream_index, stream->config.offload_info.bit_width);
        fprintf(log_file, "stream %d: AAC Format Type:%d\n", stream->stream_index, stream->aac_fmt_type);
        fprintf(log_file, "stream %d: Kvpair Values:%s\n", stream->stream_index, stream->kvpair_values);
        fprintf(log_file, "stream %d: set params Values:%s\n", stream->stream_index, stream->set_params);
        fprintf(log_file, "Log file:%s\n", log_filename);
        fprintf(log_file, "Volume level:%f\n", vol_level);

        stream->config.offload_info.version = AUDIO_OFFLOAD_INFO_VERSION_CURRENT;
        stream->config.offload_info.size = sizeof(audio_offload_info_t);

        if (stream->filetype == FILE_APTX) {
            if (ba != NULL) {
                parse_aptx_dec_bt_addr(ba, &aptx_params);
                payload = (qahw_param_payload)aptx_params;
                param_id = QAHW_PARAM_APTX_DEC;
                fprintf(log_file, "Send BT addr nap %d, uap %d lap %d to HAL\n", aptx_params.bt_addr.nap,
                            aptx_params.bt_addr.uap, aptx_params.bt_addr.lap);
                rc = qahw_set_param_data(stream->qahw_out_hal_handle, param_id, &payload);
                if (rc != 0)
                     fprintf(log_file, "Error.Failed Set BT addr\n");
                     fprintf(stderr, "Error.Failed Set BT addr\n");
            } else {
                fprintf(log_file, "BT addr is NULL, Need valid BT addr for aptx file playback to work\n");
                fprintf(stderr, "BT addr is NULL, Need valid BT addr for aptx file playback to work\n");
                goto exit;
            }
        }

        if (send_device_config) {
            payload = (qahw_param_payload)device_cfg_params;
            rc = qahw_set_param_data(stream->qahw_out_hal_handle, QAHW_PARAM_DEVICE_CONFIG, &payload);
            if (rc != 0) {
                fprintf(log_file, "Set Device Config Failed\n");
                fprintf(stderr, "Set Device Config Failed\n");
            }
        }

        if (is_dual_main && i >= 2 ) {
            stream_param[i].play_later = true;
            fprintf(log_file, "stream %d: play_later = %d\n", i, stream_param[i].play_later);
        }

        rc = pthread_create(&playback_thread[i], NULL, start_stream_playback, (void *)&stream_param[i]);
        if (rc) {
            fprintf(log_file, "stream %d: failed to create thread\n", stream->stream_index);
            fprintf(stderr, "stream %d: failed to create thread\n", stream->stream_index);
            exit(0);
        }

        thread_active[i] = true;

    }

exit:
    for (i=0; i<MAX_PLAYBACK_STREAMS; i++) {
        if(thread_active[i])
           pthread_join(playback_thread[i], NULL);
    }

    if(qap_wrapper_session_active) {
        qap_wrapper_session_close();
    }

    /*
     * reset device connection state for HDMI and close the file streams
     */
     for (i = 0; i < num_of_streams; i++) {
         if ((stream_param[i].output_device == AUDIO_DEVICE_OUT_AUX_DIGITAL) ||
             (stream_param[i].output_device == AUDIO_DEVICE_OUT_BLUETOOTH_A2DP)) {
             char param[100] = {0};
             snprintf(param, sizeof(param), "%s=%d", "disconnect", stream_param[i].output_device);
             qahw_set_parameters(stream_param[i].qahw_out_hal_handle, param);
         }

        if (stream_param[i].file_stream != nullptr)
            fclose(stream_param[i].file_stream);
        else if (AUDIO_DEVICE_NONE != stream_param[i].input_device) {
            if (stream != NULL && stream->in_handle) {
                rc = qahw_close_input_stream(stream->in_handle);
                if (rc) {
                    fprintf(log_file, "input stream could not be closed\n");
                    fprintf(stderr, "input stream could not be closed\n");
                    return rc;
                }
            }
        }
    }

    deinit_streams();
    rc = unload_hals();

    if ((log_file != stdout) && (log_file != nullptr))
        fclose(log_file);

    wakelock_acquired = request_wake_lock(wakelock_acquired, false);
    /* Caution: Below ADL log shouldnt be altered without notifying automation APT since it used
     * for automation testing
     */
    fprintf(log_file, "\nADL: BYE BYE\n");
    return 0;
}
