/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "audio_hw_qaf"
/*#define LOG_NDEBUG 0*/
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define DEBUG_MSG_VV DEBUG_MSG
#else
#define DEBUG_MSG_VV(a...) do { } while(0)
#endif

#define DEBUG_MSG(arg,...) ALOGV("%s: %d:  " arg, __func__, __LINE__, ##__VA_ARGS__)
#define ERROR_MSG(arg,...) ALOGE("%s: %d:  " arg, __func__, __LINE__, ##__VA_ARGS__)

#define COMPRESS_OFFLOAD_NUM_FRAGMENTS 2
#define COMPRESS_PASSTHROUGH_DDP_FRAGMENT_SIZE 4608

#define QAF_DEFAULT_COMPR_AUDIO_HANDLE 1001
#define QAF_DEFAULT_COMPR_PASSTHROUGH_HANDLE 1002
#define QAF_DEFAULT_PASSTHROUGH_HANDLE 1003

#define COMPRESS_OFFLOAD_PLAYBACK_LATENCY 300

#define MIN_PCM_OFFLOAD_FRAGMENT_SIZE 512
#define MAX_PCM_OFFLOAD_FRAGMENT_SIZE (240 * 1024)

#define DIV_ROUND_UP(x, y) (((x) + (y) - 1)/(y))
#define ALIGN(x, y) ((y) * DIV_ROUND_UP((x), (y)))

/* Pcm input node buffer size is 6144 bytes, i.e, 32msec for 48000 samplerate */
#define QAF_MODULE_PCM_INPUT_BUFFER_LATENCY 32

#define MS12_PCM_OUT_FRAGMENT_SIZE 1536 //samples
#define MS12_PCM_IN_FRAGMENT_SIZE 1536 //samples

#define DD_FRAME_SIZE 1536
#define DDP_FRAME_SIZE DD_FRAME_SIZE
/*
 * DD encoder output size for one frame.
 */
#define DD_ENCODER_OUTPUT_SIZE 2560
/*
 * DDP encoder output size for one frame.
 */
#define DDP_ENCODER_OUTPUT_SIZE 4608

/*********TODO Need to get correct values.*************************/

#define DTS_PCM_OUT_FRAGMENT_SIZE 1024 //samples

#define DTS_FRAME_SIZE 1536
#define DTSHD_FRAME_SIZE DTS_FRAME_SIZE
/*
 * DTS encoder output size for one frame.
 */
#define DTS_ENCODER_OUTPUT_SIZE 2560
/*
 * DTSHD encoder output size for one frame.
 */
#define DTSHD_ENCODER_OUTPUT_SIZE 4608
/******************************************************************/

/*
 * QAF Latency to process buffers since out_write from primary HAL
 */
#define QAF_COMPRESS_OFFLOAD_PROCESSING_LATENCY 18
#define QAF_PCM_OFFLOAD_PROCESSING_LATENCY 48

//TODO: Need to handle for DTS
#define QAF_DEEP_BUFFER_OUTPUT_PERIOD_SIZE 1536

#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <dlfcn.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <cutils/properties.h>
#include <cutils/str_parms.h>
#include <log/log.h>
#include <cutils/atomic.h>
#include "audio_utils/primitives.h"
#include "audio_hw.h"
#include "platform_api.h"
#include <platform.h>
#include <system/thread_defs.h>
#include <cutils/sched_policy.h>
#include "audio_extn.h"
#include <qti_audio.h>
#include "sound/compress_params.h"
#include "ip_hdlr_intf.h"

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_QAF
#include <log_utils.h>
#endif

//TODO: Need to remove this.
#define QAF_OUTPUT_SAMPLING_RATE 48000

#ifdef QAF_DUMP_ENABLED
FILE *fp_output_writer_hdmi = NULL;
#endif

void set_hdmi_configuration_to_module();
void set_bt_configuration_to_module();

struct qaf_adsp_hdlr_config_state {
    struct audio_adsp_event event_params;
    /* For holding client audio_adsp_event payload */
    uint8_t event_payload[AUDIO_MAX_ADSP_STREAM_CMD_PAYLOAD_LEN];
    bool adsp_hdlr_config_valid;
};

//Types of MM module, currently supported by QAF.
typedef enum {
    MS12,
    DTS_M8,
    MAX_MM_MODULE_TYPE,
    INVALID_MM_MODULE
} mm_module_type;

typedef enum {
    QAF_OUT_TRANSCODE_PASSTHROUGH = 0, /* Transcode passthrough via MM module*/
    QAF_OUT_OFFLOAD_MCH, /* Multi-channel PCM offload*/
    QAF_OUT_OFFLOAD, /* PCM offload */

    MAX_QAF_MODULE_OUT
} mm_module_output_type;

typedef enum {
    QAF_IN_MAIN = 0, /* Single PID Main/Primary or Dual-PID stream */
    QAF_IN_ASSOC,    /* Associated/Secondary stream */
    QAF_IN_PCM,      /* PCM stream. */
    QAF_IN_MAIN_2,   /* Single PID Main2 stream */
    MAX_QAF_MODULE_IN
} mm_module_input_type;

typedef enum {
    STOPPED,    /*Stream is in stop state. */
    STOPPING,   /*Stream is stopping, waiting for EOS. */
    RUN,        /*Stream is in run state. */
    MAX_STATES
} qaf_stream_state;

struct qaf_module {
    audio_session_handle_t session_handle;
    void *ip_hdlr_hdl;
    void *qaf_lib;
    int (*qaf_audio_session_open)(audio_session_handle_t* session_handle,
                                  audio_session_type_t s_type,
                                  void *p_data,
                                  void* license_data);
    int (*qaf_audio_session_close)(audio_session_handle_t session_handle);
    int (*qaf_audio_stream_open)(audio_session_handle_t session_handle,
                                 audio_stream_handle_t* stream_handle,
                                 audio_stream_config_t input_config,
                                 audio_devices_t devices,
                                 stream_type_t flags);
    int (*qaf_audio_stream_close)(audio_stream_handle_t stream_handle);
    int (*qaf_audio_stream_set_param)(audio_stream_handle_t stream_handle, const char* kv_pairs);
    int (*qaf_audio_session_set_param)(audio_session_handle_t handle, const char* kv_pairs);
    char* (*qaf_audio_stream_get_param)(audio_stream_handle_t stream_handle, const char* key);
    char* (*qaf_audio_session_get_param)(audio_session_handle_t handle, const char* key);
    int (*qaf_audio_stream_start)(audio_stream_handle_t handle);
    int (*qaf_audio_stream_stop)(audio_stream_handle_t stream_handle);
    int (*qaf_audio_stream_pause)(audio_stream_handle_t stream_handle);
    int (*qaf_audio_stream_flush)(audio_stream_handle_t stream_handle);
    int (*qaf_audio_stream_write)(audio_stream_handle_t stream_handle, const void* buf, int size);
    void (*qaf_register_event_callback)(audio_session_handle_t session_handle,
                                        void *priv_data,
                                        notify_event_callback_t event_callback,
                                        audio_event_id_t event_id);

    /*Input stream of MM module */
    struct stream_out *stream_in[MAX_QAF_MODULE_IN];
    /*Output Stream from MM module */
    struct stream_out *stream_out[MAX_QAF_MODULE_OUT];

    /*Media format associated with each output id raised by mm module. */
    audio_qaf_media_format_t out_stream_fmt[MAX_QAF_MODULE_OUT];
    /*Flag is set if media format is changed for an mm module output. */
    bool is_media_fmt_changed[MAX_QAF_MODULE_OUT];
    /*Index to be updated in out_stream_fmt array for a new mm module output. */
    int new_out_format_index;

    struct qaf_adsp_hdlr_config_state adsp_hdlr_config[MAX_QAF_MODULE_IN];

    //BT session handle.
    void *bt_hdl;

    float vol_left;
    float vol_right;
    bool is_vol_set;
    qaf_stream_state stream_state[MAX_QAF_MODULE_IN];
    bool is_session_closing;
};

struct qaf {
    struct audio_device *adev;

    pthread_mutex_t lock;

    bool bt_connect;
    bool hdmi_connect;
    int hdmi_sink_channels;

    //Flag to indicate if QAF transcode output stream is enabled from any mm module.
    bool passthrough_enabled;
    //Flag to indicate if QAF mch pcm output stream is enabled from any mm module.
    bool mch_pcm_hdmi_enabled;

    //Flag to indicate if msmd is supported.
    bool qaf_msmd_enabled;

    //Handle of QAF input stream, which is routed as QAF passthrough.
    struct stream_out *passthrough_in;
    //Handle of QAF passthrough stream.
    struct stream_out *passthrough_out;

    struct qaf_module qaf_mod[MAX_MM_MODULE_TYPE];
};

static int qaf_out_pause(struct audio_stream_out* stream);
static int qaf_out_flush(struct audio_stream_out* stream);
static int qaf_out_drain(struct audio_stream_out* stream, audio_drain_type_t type);
static int qaf_session_close();

//Global handle of QAF. Access to this should be protected by mutex lock.
static struct qaf *p_qaf = NULL;

/* Gets the pointer to qaf module for the qaf input stream. */
static struct qaf_module* get_qaf_module_for_input_stream(struct stream_out *out)
{
    struct qaf_module *qaf_mod = NULL;
    int i, j;
    if (!p_qaf) return NULL;

    for (i = 0; i < MAX_MM_MODULE_TYPE; i++) {
        for (j = 0; j < MAX_QAF_MODULE_IN; j++) {
            if (p_qaf->qaf_mod[i].stream_in[j] == out) {
                qaf_mod = &(p_qaf->qaf_mod[i]);
                break;
            }
        }
    }

    return qaf_mod;
}

/* Finds the mm module input stream index for the QAF input stream. */
static int get_input_stream_index(struct stream_out *out)
{
    int index = -1, j;
    struct qaf_module* qaf_mod = NULL;

    qaf_mod = get_qaf_module_for_input_stream(out);
    if (!qaf_mod) return index;

    for (j = 0; j < MAX_QAF_MODULE_IN; j++) {
        if (qaf_mod->stream_in[j] == out) {
            index = j;
            break;
        }
    }

    return index;
}

static void set_stream_state(struct stream_out *out, int state)
{
    struct qaf_module *qaf_mod = get_qaf_module_for_input_stream(out);
    int index = get_input_stream_index(out);
    if (qaf_mod && index >= 0) qaf_mod->stream_state[index] = state;
}

static bool check_stream_state(struct stream_out *out, int state)
{
    struct qaf_module *qaf_mod = get_qaf_module_for_input_stream(out);
    int index = get_input_stream_index(out);
    if (qaf_mod && index >= 0) return (qaf_mod->stream_state[index] == state);
    return false;
}

/* Finds the right mm module for the QAF input stream format. */
static mm_module_type get_mm_module_for_format(audio_format_t format)
{
    int j;

    DEBUG_MSG("Format 0x%x", format);

    if (format == AUDIO_FORMAT_PCM_16_BIT) {
        //If dts is not supported then alway support pcm with MS12
        if (!property_get_bool("vendor.audio.qaf.dts_m8", false)) { //TODO: Need to add this property for DTS.
            return MS12;
        }

        //If QAF passthrough is active then send the PCM stream to primary HAL.
        if (!p_qaf->passthrough_out) {
            /* Iff any stream is active in MS12 module then route PCM stream to it. */
            for (j = 0; j < MAX_QAF_MODULE_IN; j++) {
                if (p_qaf->qaf_mod[MS12].stream_in[j]) {
                    return MS12;
                }
            }
        }
        return INVALID_MM_MODULE;
    }

    switch (format & AUDIO_FORMAT_MAIN_MASK) {
        case AUDIO_FORMAT_AC3:
        case AUDIO_FORMAT_E_AC3:
        case AUDIO_FORMAT_AAC:
        case AUDIO_FORMAT_AAC_ADTS:
        case AUDIO_FORMAT_AC4:
            return MS12;
        case AUDIO_FORMAT_DTS:
        case AUDIO_FORMAT_DTS_HD:
            return DTS_M8;
        default:
            return INVALID_MM_MODULE;
    }
}

static bool is_main_active(struct qaf_module* qaf_mod)
{
   return (qaf_mod->stream_in[QAF_IN_MAIN] || qaf_mod->stream_in[QAF_IN_MAIN_2]);
}

static bool is_dual_main_active(struct qaf_module* qaf_mod)
{
   return (qaf_mod->stream_in[QAF_IN_MAIN] && qaf_mod->stream_in[QAF_IN_MAIN_2]);
}

//Checks if any main or pcm stream is running in the session.
static bool is_any_stream_running(struct qaf_module* qaf_mod)
{
    //Not checking associated stream.
    struct stream_out *out = qaf_mod->stream_in[QAF_IN_MAIN];
    struct stream_out *out_pcm = qaf_mod->stream_in[QAF_IN_PCM];
    struct stream_out *out_main2 = qaf_mod->stream_in[QAF_IN_MAIN_2];

    if ((out == NULL || (out != NULL && check_stream_state(out, STOPPED)))
        && (out_main2 == NULL || (out_main2 != NULL && check_stream_state(out_main2, STOPPED)))
        && (out_pcm == NULL || (out_pcm != NULL && check_stream_state(out_pcm, STOPPED)))) {
        return false;
    }
    return true;
}

/* Gets the pcm output buffer size(in samples) for the mm module. */
static uint32_t get_pcm_output_buffer_size_samples(struct qaf_module *qaf_mod)
{
    uint32_t pcm_output_buffer_size = 0;

    if (qaf_mod == &p_qaf->qaf_mod[MS12]) {
        pcm_output_buffer_size = MS12_PCM_OUT_FRAGMENT_SIZE;
    } else if (qaf_mod == &p_qaf->qaf_mod[DTS_M8]) {
        pcm_output_buffer_size = DTS_PCM_OUT_FRAGMENT_SIZE;
    }

    return pcm_output_buffer_size;
}

static int get_media_fmt_array_index_for_output_id(
        struct qaf_module* qaf_mod,
        uint32_t output_id)
{
    int i;
    for (i = 0; i < MAX_QAF_MODULE_OUT; i++) {
        if (qaf_mod->out_stream_fmt[i].output_id == output_id) {
            return i;
        }
    }
    return -1;
}

/* Acquire Mutex lock on output stream */
static void lock_output_stream(struct stream_out *out)
{
    pthread_mutex_lock(&out->pre_lock);
    pthread_mutex_lock(&out->lock);
    pthread_mutex_unlock(&out->pre_lock);
}

/* Release Mutex lock on output stream */
static void unlock_output_stream(struct stream_out *out)
{
    pthread_mutex_unlock(&out->lock);
}

/* Checks if stream can be routed as QAF passthrough or not. */
static bool audio_extn_qaf_passthrough_enabled(struct stream_out *out)
{
    DEBUG_MSG("Format 0x%x", out->format);
    bool is_enabled = false;

    if (!p_qaf) return false;

    if ((!property_get_bool("vendor.audio.qaf.reencode", false))
        && property_get_bool("vendor.audio.qaf.passthrough", false)) {

        if ((out->format == AUDIO_FORMAT_PCM_16_BIT) && (popcount(out->channel_mask) > 2)) {
            is_enabled = true;
        } else if (property_get_bool("vendor.audio.offload.passthrough", false)) {
            switch (out->format) {
                case AUDIO_FORMAT_AC3:
                case AUDIO_FORMAT_E_AC3:
                case AUDIO_FORMAT_DTS:
                case AUDIO_FORMAT_DTS_HD:
                case AUDIO_FORMAT_DOLBY_TRUEHD:
                case AUDIO_FORMAT_IEC61937: {
                    is_enabled = true;
                    break;
                }
                default:
                    is_enabled = false;
                break;
            }
        }
    }

    return is_enabled;
}

/*Closes all pcm hdmi output from QAF. */
static void close_all_pcm_hdmi_output()
{
    int i;
    //Closing all the PCM HDMI output stream from QAF.
    for (i = 0; i < MAX_MM_MODULE_TYPE; i++) {
        if (p_qaf->qaf_mod[i].stream_out[QAF_OUT_OFFLOAD_MCH]) {
            adev_close_output_stream((struct audio_hw_device *)p_qaf->adev,
                                     (struct audio_stream_out *)(p_qaf->qaf_mod[i].stream_out[QAF_OUT_OFFLOAD_MCH]));
            p_qaf->qaf_mod[i].stream_out[QAF_OUT_OFFLOAD_MCH] = NULL;
        }

        if ((p_qaf->qaf_mod[i].stream_out[QAF_OUT_OFFLOAD])
            && compare_device_type(
                   &p_qaf->qaf_mod[i].stream_out[QAF_OUT_OFFLOAD]->device_list,
                   AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
            adev_close_output_stream((struct audio_hw_device *)p_qaf->adev,
                                     (struct audio_stream_out *)(p_qaf->qaf_mod[i].stream_out[QAF_OUT_OFFLOAD]));
            p_qaf->qaf_mod[i].stream_out[QAF_OUT_OFFLOAD] = NULL;
        }
    }

    p_qaf->mch_pcm_hdmi_enabled = 0;
}

static void close_all_hdmi_output()
{
    int k;
    for (k = 0; k < MAX_MM_MODULE_TYPE; k++) {
        if (p_qaf->qaf_mod[k].stream_out[QAF_OUT_TRANSCODE_PASSTHROUGH]) {
            adev_close_output_stream((struct audio_hw_device *)p_qaf->adev,
                                     (struct audio_stream_out *)(p_qaf->qaf_mod[k].stream_out[QAF_OUT_TRANSCODE_PASSTHROUGH]));
            p_qaf->qaf_mod[k].stream_out[QAF_OUT_TRANSCODE_PASSTHROUGH] = NULL;
        }
    }
    p_qaf->passthrough_enabled = 0;

    close_all_pcm_hdmi_output();
}

static int qaf_out_callback(stream_callback_event_t event, void *param __unused, void *cookie)
{
    struct stream_out *out = (struct stream_out *)cookie;

    out->client_callback(event, NULL, out->client_cookie);
    return 0;
}

/* Creates the QAF passthrough output stream. */
static int create_qaf_passthrough_stream()
{
    DEBUG_MSG();

    int ret = 0;
    struct stream_out *out = p_qaf->passthrough_in;

    if (!out) return -EINVAL;

    pthread_mutex_lock(&p_qaf->lock);
    lock_output_stream(out);

    //Creating QAF passthrough output stream.
    if (NULL == p_qaf->passthrough_out) {
        audio_output_flags_t flags;
        struct audio_config config;
        audio_devices_t devices;

        config.sample_rate = config.offload_info.sample_rate = out->sample_rate;
        config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
        config.offload_info.size = AUDIO_INFO_INITIALIZER.size;
        config.offload_info.format = out->format;
        config.offload_info.bit_width = out->bit_width;
        config.format = out->format;
        config.offload_info.channel_mask = config.channel_mask = out->channel_mask;

        //Device is copied from the QAF passthrough input stream.
        devices = get_device_types(&out->device_list);
        flags = out->flags;

        ret = adev_open_output_stream((struct audio_hw_device *)p_qaf->adev,
                                      QAF_DEFAULT_PASSTHROUGH_HANDLE,
                                      devices,
                                      flags,
                                      &config,
                                      (struct audio_stream_out **)&(p_qaf->passthrough_out),
                                      NULL);
        if (ret < 0) {
            ERROR_MSG("adev_open_output_stream failed with ret = %d!", ret);
            unlock_output_stream(out);
            return ret;
        }
        p_qaf->passthrough_in = out;
        p_qaf->passthrough_out->stream.set_callback((struct audio_stream_out *)p_qaf->passthrough_out,
                                                    (stream_callback_t) qaf_out_callback, out);
    }

    unlock_output_stream(out);

    //Since QAF-Passthrough is created, close other HDMI outputs.
    close_all_hdmi_output();

    pthread_mutex_unlock(&p_qaf->lock);
    return ret;
}

/* Closes the QAF passthrough output stream. */
static void close_qaf_passthrough_stream()
{
    if (p_qaf->passthrough_out != NULL) { //QAF pasthroug is enabled. Close it.
        pthread_mutex_lock(&p_qaf->lock);
        adev_close_output_stream((struct audio_hw_device *)p_qaf->adev,
                                 (struct audio_stream_out *)(p_qaf->passthrough_out));
        p_qaf->passthrough_out = NULL;
        pthread_mutex_unlock(&p_qaf->lock);

        if (p_qaf->passthrough_in->qaf_stream_handle) {
            qaf_out_pause((struct audio_stream_out*)p_qaf->passthrough_in);
            qaf_out_flush((struct audio_stream_out*)p_qaf->passthrough_in);
            qaf_out_drain((struct audio_stream_out*)p_qaf->passthrough_in,
                          (audio_drain_type_t)STREAM_CBK_EVENT_DRAIN_READY);
        }
    }
}

/* Sends a command to output stream offload thread. */
static int qaf_send_offload_cmd_l(struct stream_out* out, int command)
{
    DEBUG_MSG_VV("command is %d", command);

    struct offload_cmd *cmd = (struct offload_cmd *)calloc(1, sizeof(struct offload_cmd));

    if (!cmd) {
        ERROR_MSG("failed to allocate mem for command 0x%x", command);
        return -ENOMEM;
    }

    cmd->cmd = command;

    lock_output_stream(out);
    list_add_tail(&out->qaf_offload_cmd_list, &cmd->node);
    pthread_cond_signal(&out->qaf_offload_cond);
    unlock_output_stream(out);
    return 0;
}

/* Stops a QAF module stream.*/
static int audio_extn_qaf_stream_stop(struct stream_out *out)
{
    int ret = 0;
    DEBUG_MSG("Output Stream 0x%p", out);

    if (!check_stream_state(out, RUN)) return ret;

    struct qaf_module *qaf_mod = get_qaf_module_for_input_stream(out);
    if ((!qaf_mod) || (!qaf_mod->qaf_audio_stream_stop)) {
        return ret;
    }

    if (out->qaf_stream_handle) {
        ret = qaf_mod->qaf_audio_stream_stop(out->qaf_stream_handle);
    }

    return ret;
}

/* Puts a QAF module stream in standby. */
static int qaf_out_standby(struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = 0;

    ALOGD("%s: enter: stream (%p) usecase(%d: %s)", __func__,
          stream, out->usecase, use_case_table[out->usecase]);

    lock_output_stream(out);

    //If QAF passthrough is active then block standby on all the input streams of QAF mm modules.
    if (p_qaf->passthrough_out) {
        //If standby is received on QAF passthrough stream then forward it to primary HAL.
        if (p_qaf->passthrough_in == out) {
            status = p_qaf->passthrough_out->stream.common.standby(
                    (struct audio_stream *)p_qaf->passthrough_out);
        }
    } else if (check_stream_state(out, RUN)) {
        //If QAF passthrough stream is not active then stop the QAF module stream.
        status = audio_extn_qaf_stream_stop(out);

        if (status == 0) {
            //Setting state to stopped as client not expecting drain_ready event.
            set_stream_state(out, STOPPED);
        }
    }

    if (!out->standby) {
        out->standby = true;
    }

    unlock_output_stream(out);
    return status;
}

/* Sets the volume to PCM output stream. */
static int qaf_out_set_volume(struct audio_stream_out *stream, float left, float right)
{
    int ret = 0;
    struct stream_out *out = (struct stream_out *)stream;
    struct qaf_module *qaf_mod = NULL;

    DEBUG_MSG("Left %f, Right %f", left, right);

    qaf_mod = get_qaf_module_for_input_stream(out);
    if (!qaf_mod) {
        return -EINVAL;
    }

    pthread_mutex_lock(&p_qaf->lock);
    qaf_mod->vol_left = left;
    qaf_mod->vol_right = right;
    qaf_mod->is_vol_set = true;
    pthread_mutex_unlock(&p_qaf->lock);

    if (qaf_mod->stream_out[QAF_OUT_OFFLOAD] != NULL) {
        ret = qaf_mod->stream_out[QAF_OUT_OFFLOAD]->stream.set_volume(
                (struct audio_stream_out *)qaf_mod->stream_out[QAF_OUT_OFFLOAD], left, right);
    }

    return ret;
}

/* Starts a QAF module stream. */
static int qaf_stream_start(struct stream_out *out)
{
    int ret = -EINVAL;
    struct qaf_module *qaf_mod = NULL;

    DEBUG_MSG("Output Stream = %p", out);

    qaf_mod = get_qaf_module_for_input_stream(out);
    if ((!qaf_mod) || (!qaf_mod->qaf_audio_stream_start)) {
        return -EINVAL;
    }

    if (out->qaf_stream_handle) {
        ret = qaf_mod->qaf_audio_stream_start(out->qaf_stream_handle);
    }

    return ret;
}

static int qaf_start_output_stream(struct stream_out *out)
{
    int ret = 0;
    struct audio_device *adev = out->dev;

    if ((out->usecase < 0) || (out->usecase >= AUDIO_USECASE_MAX)) {
        ret = -EINVAL;
        usleep(50000);
        return ret;
    }

    ALOGD("%s: enter: stream(%p)usecase(%d: %s) devices(%#x)",
          __func__, &out->stream, out->usecase, use_case_table[out->usecase],
          get_device_types(&out->device_list));

    if (CARD_STATUS_OFFLINE == out->card_status ||
        CARD_STATUS_OFFLINE == adev->card_status) {
        ALOGE("%s: sound card is not active/SSR returning error", __func__);
        ret = -EIO;
        usleep(50000);
        return ret;
    }

    return qaf_stream_start(out);
}

/* Sends input buffer to the QAF MM module. */
static int qaf_module_write_input_buffer(struct stream_out *out, const void *buffer, int bytes)
{
    int ret = -EINVAL;
    struct qaf_module *qaf_mod = NULL;

    qaf_mod = get_qaf_module_for_input_stream(out);
    if ((!qaf_mod) || (!qaf_mod->qaf_audio_stream_write)) {
        return ret;
    }

    //If data received on associated stream when all other stream are stopped then drop the data.
    if (out == qaf_mod->stream_in[QAF_IN_ASSOC] && !is_any_stream_running(qaf_mod))
        return bytes;

    if (out->qaf_stream_handle) {
        ret = qaf_mod->qaf_audio_stream_write(out->qaf_stream_handle, buffer, bytes);
        if(ret > 0) set_stream_state(out, RUN);
    }
    return ret;
}

/* Writes buffer to QAF input stream. */
static ssize_t qaf_out_write(struct audio_stream_out *stream, const void *buffer, size_t bytes)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    ssize_t ret = 0;

    DEBUG_MSG_VV("bytes = %d, usecase[%d] and flags[%x] for handle[%p]",
          (int)bytes, out->usecase, out->flags, out);

    lock_output_stream(out);

    // If QAF passthrough is active then block writing data to QAF mm module.
    if (p_qaf->passthrough_out) {
        //If write is received for the QAF passthrough stream then send the buffer to primary HAL.
        if (p_qaf->passthrough_in == out) {
            ret = p_qaf->passthrough_out->stream.write(
                    (struct audio_stream_out *)(p_qaf->passthrough_out),
                    buffer,
                    bytes);
            if (ret > 0) out->standby = false;
        }
        unlock_output_stream(out);
        return ret;
    } else if (out->standby) {
        pthread_mutex_lock(&adev->lock);
        ret = qaf_start_output_stream(out);
        pthread_mutex_unlock(&adev->lock);
        if (ret == 0) {
            out->standby = false;
        } else {
            goto exit;
        }
    }

    if ((adev->is_channel_status_set == false) &&
         compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
        audio_utils_set_hdmi_channel_status(out, (char *)buffer, bytes);
        adev->is_channel_status_set = true;
    }

    ret = qaf_module_write_input_buffer(out, buffer, bytes);
    DEBUG_MSG_VV("ret [%d]", (int)ret);

    if (ret >= 0) {
        out->written += ret / ((popcount(out->channel_mask) * sizeof(short)));
    }


exit:
    unlock_output_stream(out);

    if (ret < 0) {
        if (ret == -EAGAIN) {
            DEBUG_MSG_VV("No space available in mm module, post msg to cb thread");
            ret = qaf_send_offload_cmd_l(out, OFFLOAD_CMD_WAIT_FOR_BUFFER);
            bytes = 0;
        } else if (ret == -ENOMEM || ret == -EPERM) {
            if (out->pcm)
                ERROR_MSG("error %d, %s", (int)ret, pcm_get_error(out->pcm));
            qaf_out_standby(&out->stream.common);
            usleep(bytes * 1000000
                   / audio_stream_out_frame_size(stream)
                   / out->stream.common.get_sample_rate(&out->stream.common));
        }
    } else if (ret < (ssize_t)bytes) {
        //partial buffer copied to the module.
        DEBUG_MSG_VV("Not enough space available in mm module, post msg to cb thread");
        (void)qaf_send_offload_cmd_l(out, OFFLOAD_CMD_WAIT_FOR_BUFFER);
        bytes = ret;
    }
    return bytes;
}

/* Gets PCM offload buffer size for a given config. */
static uint32_t qaf_get_pcm_offload_buffer_size(audio_offload_info_t* info,
                                                uint32_t samples_per_frame)
{
    uint32_t fragment_size = 0;

    fragment_size = (samples_per_frame * (info->bit_width >> 3) * popcount(info->channel_mask));

    if (fragment_size < MIN_PCM_OFFLOAD_FRAGMENT_SIZE)
        fragment_size = MIN_PCM_OFFLOAD_FRAGMENT_SIZE;
    else if (fragment_size > MAX_PCM_OFFLOAD_FRAGMENT_SIZE)
        fragment_size = MAX_PCM_OFFLOAD_FRAGMENT_SIZE;

    // To have same PCM samples for all channels, the buffer size requires to
    // be multiple of (number of channels * bytes per sample)
    // For writes to succeed, the buffer must be written at address which is multiple of 32
    fragment_size = ALIGN(fragment_size,
                          ((info->bit_width >> 3) * popcount(info->channel_mask) * 32));

    ALOGI("Qaf PCM offload Fragment size is %d bytes", fragment_size);

    return fragment_size;
}

static uint32_t qaf_get_pcm_offload_input_buffer_size(audio_offload_info_t* info)
{
    return qaf_get_pcm_offload_buffer_size(info, MS12_PCM_IN_FRAGMENT_SIZE);
}

static uint32_t qaf_get_pcm_offload_output_buffer_size(struct qaf_module *qaf_mod,
                                                audio_offload_info_t* info)
{
    return qaf_get_pcm_offload_buffer_size(info, get_pcm_output_buffer_size_samples(qaf_mod));
}

/* Gets buffer latency in samples. */
static int get_buffer_latency(struct stream_out *out, uint32_t buffer_size, uint32_t *latency)
{
    unsigned long int samples_in_one_encoded_frame;
    unsigned long int size_of_one_encoded_frame;

    switch (out->format) {
        case AUDIO_FORMAT_AC3:
            samples_in_one_encoded_frame = DD_FRAME_SIZE;
            size_of_one_encoded_frame = DD_ENCODER_OUTPUT_SIZE;
        break;
        case AUDIO_FORMAT_E_AC3:
            samples_in_one_encoded_frame = DDP_FRAME_SIZE;
            size_of_one_encoded_frame = DDP_ENCODER_OUTPUT_SIZE;
        break;
        case AUDIO_FORMAT_DTS:
            samples_in_one_encoded_frame = DTS_FRAME_SIZE;
            size_of_one_encoded_frame = DTS_ENCODER_OUTPUT_SIZE;
        break;
        case AUDIO_FORMAT_DTS_HD:
            samples_in_one_encoded_frame = DTSHD_FRAME_SIZE;
            size_of_one_encoded_frame = DTSHD_ENCODER_OUTPUT_SIZE;
        break;
        case AUDIO_FORMAT_PCM_16_BIT:
            samples_in_one_encoded_frame = 1;
            size_of_one_encoded_frame = ((out->bit_width) >> 3) * popcount(out->channel_mask);
        break;
        default:
            *latency = 0;
            return (-EINVAL);
    }

    *latency = ((buffer_size * samples_in_one_encoded_frame) / size_of_one_encoded_frame);
    return 0;
}

/* Returns the number of frames rendered to outside observer. */
static int qaf_get_rendered_frames(struct stream_out *out, uint64_t *frames)
{
    int ret = 0, i;
    struct str_parms *parms;
    int value = 0;
    int module_latency = 0;
    uint32_t kernel_latency = 0;
    uint32_t dsp_latency = 0;
    int signed_frames = 0;
    char* kvpairs = NULL;
    struct qaf_module *qaf_mod = NULL;

    DEBUG_MSG("Output Format %d", out->format);

    qaf_mod = get_qaf_module_for_input_stream(out);
    if ((!qaf_mod) || (!qaf_mod->qaf_audio_stream_get_param)) {
        return -EINVAL;
    }

    //Get MM module latency.
    kvpairs = qaf_mod->qaf_audio_stream_get_param(out->qaf_stream_handle, "get_latency");
    if (kvpairs) {
        parms = str_parms_create_str(kvpairs);
        ret = str_parms_get_int(parms, "get_latency", &module_latency);
        if (ret >= 0) {
            str_parms_destroy(parms);
            parms = NULL;
        }
        free(kvpairs);
        kvpairs = NULL;
    }

    //Get kernel Latency
    for (i = MAX_QAF_MODULE_OUT - 1; i >= 0; i--) {
        if (qaf_mod->stream_out[i] == NULL) {
            continue;
        } else {
            unsigned int num_fragments = qaf_mod->stream_out[i]->compr_config.fragments;
            uint32_t fragment_size = qaf_mod->stream_out[i]->compr_config.fragment_size;
            uint32_t kernel_buffer_size = num_fragments * fragment_size;
            get_buffer_latency(qaf_mod->stream_out[i], kernel_buffer_size, &kernel_latency);
            break;
        }
    }

    //Get DSP latency
    if ((qaf_mod->stream_out[QAF_OUT_OFFLOAD] != NULL)
        || (qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH] != NULL)) {
        unsigned int sample_rate = 0;
        audio_usecase_t platform_latency = 0;

        if (qaf_mod->stream_out[QAF_OUT_OFFLOAD])
            sample_rate = qaf_mod->stream_out[QAF_OUT_OFFLOAD]->sample_rate;
        else if (qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH])
            sample_rate = qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]->sample_rate;

        if (qaf_mod->stream_out[QAF_OUT_OFFLOAD])
            platform_latency =
                platform_render_latency(qaf_mod->stream_out[QAF_OUT_OFFLOAD]);
        else
            platform_latency =
                platform_render_latency(qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]);

        dsp_latency = (platform_latency * sample_rate) / 1000000LL;
    } else if (qaf_mod->stream_out[QAF_OUT_TRANSCODE_PASSTHROUGH] != NULL) {
        unsigned int sample_rate = 0;

        sample_rate = qaf_mod->stream_out[QAF_OUT_TRANSCODE_PASSTHROUGH]->sample_rate; //TODO: How this sample rate can be used?
        dsp_latency = (COMPRESS_OFFLOAD_PLAYBACK_LATENCY * sample_rate) / 1000;
    }

    // MM Module Latency + Kernel Latency + DSP Latency
    if ( audio_extn_bt_hal_get_output_stream(qaf_mod->bt_hdl) != NULL) {
        out->platform_latency = module_latency + audio_extn_bt_hal_get_latency(qaf_mod->bt_hdl);
    } else {
        out->platform_latency = (uint32_t)module_latency + kernel_latency + dsp_latency;
    }

    if (out->format & AUDIO_FORMAT_PCM_16_BIT) {
        *frames = 0;
        signed_frames = out->written - out->platform_latency;
        // It would be unusual for this value to be negative, but check just in case ...
        if (signed_frames >= 0) {
            *frames = signed_frames;
        }
    } else if (qaf_mod->qaf_audio_stream_get_param) {
        kvpairs = qaf_mod->qaf_audio_stream_get_param(out->qaf_stream_handle, "position");
        if (kvpairs) {
            parms = str_parms_create_str(kvpairs);
            ret = str_parms_get_int(parms, "position", &value);
            if (ret >= 0) {
                *frames = value;
                signed_frames = value - out->platform_latency;
                // It would be unusual for this value to be negative, but check just in case ...
                if (signed_frames >= 0) {
                    *frames = signed_frames;
                }
            }
            str_parms_destroy(parms);
        }
    } else {
        ret = -EINVAL;
    }

    return ret;
}

static int qaf_out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    struct stream_out *out = (struct stream_out *)stream;
    int ret = 0;
    uint64_t frames=0;
    struct qaf_module* qaf_mod = NULL;
    ALOGV("%s, Output Stream %p,dsp frames %d",__func__, stream, (int)dsp_frames);

    qaf_mod = get_qaf_module_for_input_stream(out);
    if (!qaf_mod) {
        ret = out->stream.get_render_position(stream, dsp_frames);
        ALOGV("%s, non qaf_MOD DSP FRAMES %d",__func__, (int)dsp_frames);
        return ret;
    }

    if (p_qaf->passthrough_out) {
        pthread_mutex_lock(&p_qaf->lock);
        ret = p_qaf->passthrough_out->stream.get_render_position((struct audio_stream_out *)p_qaf->passthrough_out, dsp_frames);
        pthread_mutex_unlock(&p_qaf->lock);
        ALOGV("%s, PASS THROUGH DSP FRAMES %p",__func__, dsp_frames);
        return ret;
        }
    frames=*dsp_frames;
    ret = qaf_get_rendered_frames(out, &frames);
    *dsp_frames = (uint32_t)frames;
    ALOGV("%s, DSP FRAMES %d",__func__, (int)dsp_frames);
    return ret;
}

static int qaf_out_get_presentation_position(const struct audio_stream_out *stream,
                                             uint64_t *frames,
                                             struct timespec *timestamp)
{
    struct stream_out *out = (struct stream_out *)stream;
    int ret = 0;

    DEBUG_MSG("Output Stream %p", stream);

    //If QAF passthorugh output stream is active.
    if (p_qaf->passthrough_out) {
        if (p_qaf->passthrough_in == out) {
            //If api is called for QAF passthorugh stream then call the primary HAL api to get the position.
            pthread_mutex_lock(&p_qaf->lock);
            ret = p_qaf->passthrough_out->stream.get_presentation_position(
                    (struct audio_stream_out *)p_qaf->passthrough_out,
                    frames,
                    timestamp);
            pthread_mutex_unlock(&p_qaf->lock);
        } else {
            //If api is called for other stream then return zero frames.
            *frames = 0;
            clock_gettime(CLOCK_MONOTONIC, timestamp);
        }
        return ret;
    }

    ret = qaf_get_rendered_frames(out, frames);
    clock_gettime(CLOCK_MONOTONIC, timestamp);

    return ret;
}

/* Pause the QAF module input stream. */
static int qaf_stream_pause(struct stream_out *out)
{
    struct qaf_module *qaf_mod = NULL;
    int ret = -EINVAL;

    qaf_mod = get_qaf_module_for_input_stream(out);
    if (!qaf_mod || !qaf_mod->qaf_audio_stream_pause) {
        return -EINVAL;
    }

    if (out->qaf_stream_handle)
        ret = qaf_mod->qaf_audio_stream_pause(out->qaf_stream_handle);

    return ret;
}

/* Pause a QAF input stream. */
static int qaf_out_pause(struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = 0;
    DEBUG_MSG("Output Stream %p", out);

    lock_output_stream(out);

    //If QAF passthrough is enabled then block the pause on module stream.
    if (p_qaf->passthrough_out) {
        pthread_mutex_lock(&p_qaf->lock);
        //If pause is received for QAF passthorugh stream then call the primary HAL api.
        if (p_qaf->passthrough_in == out) {
            status = p_qaf->passthrough_out->stream.pause(
                    (struct audio_stream_out *)p_qaf->passthrough_out);
            out->offload_state = OFFLOAD_STATE_PAUSED;
        }
        pthread_mutex_unlock(&p_qaf->lock);
    } else {
        //Pause the module input stream.
        status = qaf_stream_pause(out);
    }

    unlock_output_stream(out);
    return status;
}

/* Drains a qaf input stream. */
static int qaf_out_drain(struct audio_stream_out* stream, audio_drain_type_t type)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = 0;
    struct qaf_module *qaf_mod = NULL;

    qaf_mod = get_qaf_module_for_input_stream(out);
    DEBUG_MSG("Output Stream %p", out);

    lock_output_stream(out);

    //If QAF passthrough is enabled then block the drain on module stream.
    if (p_qaf->passthrough_out) {
        pthread_mutex_lock(&p_qaf->lock);
        //If drain is received for QAF passthorugh stream then call the primary HAL api.
        if (p_qaf->passthrough_in == out) {
            status = p_qaf->passthrough_out->stream.drain(
                    (struct audio_stream_out *)p_qaf->passthrough_out, type);
        }
        pthread_mutex_unlock(&p_qaf->lock);
    } else if (!is_any_stream_running(qaf_mod)) {
        //If stream is already stopped then send the drain ready.
        out->client_callback(STREAM_CBK_EVENT_DRAIN_READY, NULL, out->client_cookie);
        set_stream_state(out, STOPPED);
    } else {
        //Drain the module input stream.
        /* Stream stop will trigger EOS and on EOS_EVENT received
         from callback DRAIN_READY command is sent */
        status = audio_extn_qaf_stream_stop(out);

        if (status == 0) {
            //Setting state to stopping as client is expecting drain_ready event.
            set_stream_state(out, STOPPING);
        }
    }

    unlock_output_stream(out);
    return status;
}

/* Flush the QAF module input stream. */
static int audio_extn_qaf_stream_flush(struct stream_out *out)
{
    DEBUG_MSG("Output Stream %p", out);
    int ret = -EINVAL;
    struct qaf_module *qaf_mod = NULL;

    qaf_mod = get_qaf_module_for_input_stream(out);
    if ((!qaf_mod) || (!qaf_mod->qaf_audio_stream_flush)) {
        return -EINVAL;
    }

    if (out->qaf_stream_handle)
        ret = qaf_mod->qaf_audio_stream_flush(out->qaf_stream_handle);

    return ret;
}

/* Flush the QAF input stream. */
static int qaf_out_flush(struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = 0;

    DEBUG_MSG("Output Stream %p", out);
    lock_output_stream(out);

    if (!out->standby) {
        //If QAF passthrough is active then block the flush on module input streams.
        if (p_qaf->passthrough_out) {
            pthread_mutex_lock(&p_qaf->lock);
            //If flush is received for the QAF passthrough stream then call the primary HAL api.
            if (p_qaf->passthrough_in == out) {
                status = p_qaf->passthrough_out->stream.flush(
                        (struct audio_stream_out *)p_qaf->passthrough_out);
                out->offload_state = OFFLOAD_STATE_IDLE;
            }
            pthread_mutex_unlock(&p_qaf->lock);
        } else {
            //Flush the module input stream.
            status = audio_extn_qaf_stream_flush(out);
        }
    }
    unlock_output_stream(out);
    DEBUG_MSG("Exit");
    return status;
}

static uint32_t qaf_out_get_latency(const struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    uint32_t latency = 0;
    struct qaf_module *qaf_mod = NULL;
    DEBUG_MSG_VV("Output Stream %p", out);

    qaf_mod = get_qaf_module_for_input_stream(out);
    if (!qaf_mod) {
        return 0;
    }

    //If QAF passthrough is active then block the get latency on module input streams.
    if (p_qaf->passthrough_out) {
        pthread_mutex_lock(&p_qaf->lock);
        //If get latency is called for the QAF passthrough stream then call the primary HAL api.
        if (p_qaf->passthrough_in == out) {
            latency = p_qaf->passthrough_out->stream.get_latency(
                    (struct audio_stream_out *)p_qaf->passthrough_out);
        }
        pthread_mutex_unlock(&p_qaf->lock);
    } else {
        if (is_offload_usecase(out->usecase)) {
            latency = COMPRESS_OFFLOAD_PLAYBACK_LATENCY;
        } else {
            uint32_t sample_rate = 0;
            latency = QAF_MODULE_PCM_INPUT_BUFFER_LATENCY; //Input latency

            if (qaf_mod->stream_out[QAF_OUT_OFFLOAD])
                sample_rate = qaf_mod->stream_out[QAF_OUT_OFFLOAD]->sample_rate;
            else if (qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH])
                sample_rate = qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]->sample_rate;

            if (sample_rate) {
                latency += (get_pcm_output_buffer_size_samples(qaf_mod) * 1000) / out->sample_rate;
            }
        }

        if ( audio_extn_bt_hal_get_output_stream(qaf_mod->bt_hdl) != NULL) {
            if (is_offload_usecase(out->usecase)) {
                latency = audio_extn_bt_hal_get_latency(qaf_mod->bt_hdl) +
                QAF_COMPRESS_OFFLOAD_PROCESSING_LATENCY;
            } else {
                latency = audio_extn_bt_hal_get_latency(qaf_mod->bt_hdl) +
                QAF_PCM_OFFLOAD_PROCESSING_LATENCY;
            }
        }
    }

    DEBUG_MSG_VV("Latency %d", latency);
    return latency;
}

static bool check_and_get_compressed_device_format(int device, int *format)
{
    switch (device) {
        case (AUDIO_DEVICE_OUT_AUX_DIGITAL | AUDIO_COMPRESSED_OUT_DD):
            *format = AUDIO_FORMAT_AC3;
            return true;
        case (AUDIO_DEVICE_OUT_AUX_DIGITAL | AUDIO_COMPRESSED_OUT_DDP):
            *format = AUDIO_FORMAT_E_AC3;
            return true;
        case (AUDIO_DEVICE_OUT_AUX_DIGITAL | AUDIO_FORMAT_DTS):
            *format = AUDIO_FORMAT_DTS;
            return true;
        default:
            return false;
    }
}

static void set_out_stream_channel_map(struct stream_out *out, audio_qaf_media_format_t *media_fmt)
{
    if (media_fmt == NULL || out == NULL) {
        return;
    }
    struct audio_out_channel_map_param chmap = {0,{0}};
    int i = 0;
    chmap.channels = media_fmt->channels;
    for (i = 0; i < chmap.channels && i < AUDIO_CHANNEL_COUNT_MAX && i < AUDIO_QAF_MAX_CHANNELS;
            i++) {
        chmap.channel_map[i] = media_fmt->ch_map[i];
    }
    audio_extn_utils_set_channel_map(out, &chmap);
}

/* Call back function for mm module. */
static void notify_event_callback(audio_session_handle_t session_handle __unused,
                                  void *prv_data,
                                  void *buf,
                                  audio_event_id_t event_id,
                                  int size,
                                  int device) //TODO: add media format as well.
{

    /*
     For SPKR:
     1. Open pcm device if device_id passed to it SPKR and write the data to
     pcm device

     For HDMI
     1.Open compress device for HDMI(PCM or AC3) based on current hdmi o/p format and write
     data to the HDMI device.
     */
    int ret;
    audio_output_flags_t flags;
    struct qaf_module* qaf_mod = (struct qaf_module*)prv_data;
    struct audio_stream_out *bt_stream = NULL;
    int format;
    int8_t *data_buffer_p = NULL;
    uint32_t buffer_size = 0;
    bool need_to_recreate_stream = false;
    struct audio_config config;
    audio_qaf_media_format_t *media_fmt = NULL;

    if (qaf_mod->is_session_closing) {
        DEBUG_MSG("Dropping event as session is closing."
                "Device 0x%X, Event = 0x%X, Bytes to write %d", device, event_id, size);
        return;
    }

    DEBUG_MSG_VV("Device 0x%X, Event = 0x%X, Bytes to write %d", device, event_id, size);


    /* Default config initialization. */
    config.sample_rate = config.offload_info.sample_rate = QAF_OUTPUT_SAMPLING_RATE;
    config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
    config.offload_info.size = AUDIO_INFO_INITIALIZER.size;
    config.format = config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
    config.offload_info.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    config.offload_info.channel_mask = config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;

    if (event_id == AUDIO_SEC_FAIL_EVENT) {
        DEBUG_MSG("%s Security failed, closing session", __func__);
        qaf_session_close(qaf_mod);
        return;
    }

    pthread_mutex_lock(&p_qaf->lock);

    if (event_id == AUDIO_DATA_EVENT) {
        data_buffer_p = (int8_t*)buf;
        buffer_size = size;
    } else if (event_id == AUDIO_DATA_EVENT_V2) {
        audio_qaf_out_buffer_t *buf_payload = (audio_qaf_out_buffer_t*)buf;
        int index = -1;

        if ((uint32_t)size < sizeof(audio_qaf_out_buffer_t)) {
            ERROR_MSG("AUDIO_DATA_EVENT_V2 payload size is not sufficient.");
            return;
        }

        data_buffer_p = (int8_t*)buf_payload->data + buf_payload->offset;
        buffer_size = buf_payload->size - buf_payload->offset;

        index = get_media_fmt_array_index_for_output_id(qaf_mod, buf_payload->output_id);

        if (index < 0) {
            /*If media format is not received then switch to default values.*/
            event_id = AUDIO_DATA_EVENT;
        } else {
            media_fmt = &qaf_mod->out_stream_fmt[index];
            need_to_recreate_stream = qaf_mod->is_media_fmt_changed[index];
            qaf_mod->is_media_fmt_changed[index] = false;

            config.sample_rate = config.offload_info.sample_rate = media_fmt->sample_rate;
            config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
            config.offload_info.size = AUDIO_INFO_INITIALIZER.size;
            config.format = config.offload_info.format = media_fmt->format;
            config.offload_info.bit_width = media_fmt->bit_width;

            if (media_fmt->format == AUDIO_FORMAT_PCM) {
                if (media_fmt->bit_width == 16)
                    config.format = config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
                else if (media_fmt->bit_width == 24)
                    config.format = config.offload_info.format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
                else
                    config.format = config.offload_info.format = AUDIO_FORMAT_PCM_32_BIT;
            }

            device |= (media_fmt->format & AUDIO_FORMAT_MAIN_MASK);

            config.channel_mask = audio_channel_out_mask_from_count(media_fmt->channels);
            config.offload_info.channel_mask = config.channel_mask;
        }
    }

    if (event_id == AUDIO_OUTPUT_MEDIA_FORMAT_EVENT) {
        audio_qaf_media_format_t *p_fmt = (audio_qaf_media_format_t*)buf;
        audio_qaf_media_format_t *p_cached_fmt = NULL;
        int index = -1;

        if ( (uint32_t)size < sizeof(audio_qaf_media_format_t)) {
            ERROR_MSG("Size is not proper for the event AUDIO_OUTPUT_MEDIA_FORMAT_EVENT.");
            return ;
        }

        index = get_media_fmt_array_index_for_output_id(qaf_mod, p_fmt->output_id);

        if (index >= 0) {
            p_cached_fmt = &qaf_mod->out_stream_fmt[index];
        } else if (index < 0 && qaf_mod->new_out_format_index < MAX_QAF_MODULE_OUT) {
            index = qaf_mod->new_out_format_index;
            p_cached_fmt = &qaf_mod->out_stream_fmt[index];
            qaf_mod->new_out_format_index++;
        }

        if (p_cached_fmt == NULL) {
            ERROR_MSG("Maximum output from a QAF module is reached. Can not process new output.");
            return ;
        }

        if (memcmp(p_cached_fmt, p_fmt, sizeof(audio_qaf_media_format_t)) != 0) {
            memcpy(p_cached_fmt, p_fmt, sizeof(audio_qaf_media_format_t));
            qaf_mod->is_media_fmt_changed[index] = true;
        }
    } else if (event_id == AUDIO_DATA_EVENT || event_id == AUDIO_DATA_EVENT_V2) {

        if (p_qaf->passthrough_out != NULL) {
            //If QAF passthrough is active then all the module output will be dropped.
            pthread_mutex_unlock(&p_qaf->lock);
            DEBUG_MSG("QAF-PSTH is active, DROPPING DATA!");
            return;
        }

        if (check_and_get_compressed_device_format(device, &format)) {
            /*
             * CASE 1: Transcoded output of mm module.
             * If HDMI is not connected then drop the data.
             * Only one HDMI output can be supported from all the mm modules of QAF.
             * Multi-Channel PCM HDMI output streams will be closed from all the mm modules.
             * If transcoded output of other module is already enabled then this data will be dropped.
             */

            if (!p_qaf->hdmi_connect) {
                DEBUG_MSG("HDMI not connected, DROPPING DATA!");
                pthread_mutex_unlock(&p_qaf->lock);
                return;
            }

            //Closing all the PCM HDMI output stream from QAF.
            close_all_pcm_hdmi_output();

            /* If Media format was changed for this stream then need to re-create the stream. */
            if (need_to_recreate_stream && qaf_mod->stream_out[QAF_OUT_TRANSCODE_PASSTHROUGH]) {
                adev_close_output_stream((struct audio_hw_device *)p_qaf->adev,
                                         (struct audio_stream_out *)(qaf_mod->stream_out[QAF_OUT_TRANSCODE_PASSTHROUGH]));
                qaf_mod->stream_out[QAF_OUT_TRANSCODE_PASSTHROUGH] = NULL;
                p_qaf->passthrough_enabled = false;
            }

            if (!p_qaf->passthrough_enabled
                && !(qaf_mod->stream_out[QAF_OUT_TRANSCODE_PASSTHROUGH])) {

                audio_devices_t devices;

                config.format = config.offload_info.format = format;
                config.offload_info.channel_mask = config.channel_mask = AUDIO_CHANNEL_OUT_5POINT1;

                flags = (AUDIO_OUTPUT_FLAG_NON_BLOCKING
                         | AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD
                         | AUDIO_OUTPUT_FLAG_DIRECT
                         | AUDIO_OUTPUT_FLAG_COMPRESS_PASSTHROUGH);
                devices = AUDIO_DEVICE_OUT_AUX_DIGITAL;

                ret = adev_open_output_stream((struct audio_hw_device *)p_qaf->adev,
                                              QAF_DEFAULT_COMPR_PASSTHROUGH_HANDLE,
                                              devices,
                                              flags,
                                              &config,
                                              (struct audio_stream_out **)&(qaf_mod->stream_out[QAF_OUT_TRANSCODE_PASSTHROUGH]),
                                              NULL);
                if (ret < 0) {
                    ERROR_MSG("adev_open_output_stream failed with ret = %d!", ret);
                    pthread_mutex_unlock(&p_qaf->lock);
                    return;
                }

                if (format == AUDIO_FORMAT_E_AC3) {
                    qaf_mod->stream_out[QAF_OUT_TRANSCODE_PASSTHROUGH]->compr_config.fragment_size =
                            COMPRESS_PASSTHROUGH_DDP_FRAGMENT_SIZE;
                }
                qaf_mod->stream_out[QAF_OUT_TRANSCODE_PASSTHROUGH]->compr_config.fragments =
                        COMPRESS_OFFLOAD_NUM_FRAGMENTS;

                p_qaf->passthrough_enabled = true;
            }

            if (qaf_mod->stream_out[QAF_OUT_TRANSCODE_PASSTHROUGH]) {
                ret = qaf_mod->stream_out[QAF_OUT_TRANSCODE_PASSTHROUGH]->stream.write(
                        (struct audio_stream_out *)qaf_mod->stream_out[QAF_OUT_TRANSCODE_PASSTHROUGH],
                        data_buffer_p,
                        buffer_size);
            }
        }
        else if ((device & AUDIO_DEVICE_OUT_AUX_DIGITAL)
                   && (p_qaf->hdmi_connect)
                   && (p_qaf->hdmi_sink_channels > 2)) {

            /* CASE 2: Multi-Channel PCM output to HDMI.
             * If any other HDMI output is already enabled then this has to be dropped.
             */

            if (p_qaf->passthrough_enabled) {
                //Closing all the multi-Channel PCM HDMI output stream from QAF.
                close_all_pcm_hdmi_output();

                //If passthrough is active then pcm hdmi output has to be dropped.
                pthread_mutex_unlock(&p_qaf->lock);
                DEBUG_MSG("Compressed passthrough enabled, DROPPING DATA!");
                return;
            }

            /* If Media format was changed for this stream then need to re-create the stream. */
            if (need_to_recreate_stream && qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]) {
                adev_close_output_stream((struct audio_hw_device *)p_qaf->adev,
                                         (struct audio_stream_out *)(qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]));
                qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH] = NULL;
                p_qaf->mch_pcm_hdmi_enabled = false;
            }

            if (!p_qaf->mch_pcm_hdmi_enabled && !(qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH])) {
                audio_devices_t devices;

                if (event_id == AUDIO_DATA_EVENT) {
                    config.offload_info.format = config.format = AUDIO_FORMAT_PCM_16_BIT;

                    if (p_qaf->hdmi_sink_channels == 8) {
                        config.offload_info.channel_mask = config.channel_mask =
                                AUDIO_CHANNEL_OUT_7POINT1;
                    } else if (p_qaf->hdmi_sink_channels == 6) {
                        config.offload_info.channel_mask = config.channel_mask =
                                AUDIO_CHANNEL_OUT_5POINT1;
                    } else {
                        config.offload_info.channel_mask = config.channel_mask =
                                AUDIO_CHANNEL_OUT_STEREO;
                    }
                }

                devices = AUDIO_DEVICE_OUT_AUX_DIGITAL;
                flags = AUDIO_OUTPUT_FLAG_DIRECT;

                ret = adev_open_output_stream((struct audio_hw_device *)p_qaf->adev,
                                              QAF_DEFAULT_COMPR_AUDIO_HANDLE,
                                              devices,
                                              flags,
                                              &config,
                                              (struct audio_stream_out **)&(qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]),
                                              NULL);
                if (ret < 0) {
                    ERROR_MSG("adev_open_output_stream failed with ret = %d!", ret);
                    pthread_mutex_unlock(&p_qaf->lock);
                    return;
                }
                set_out_stream_channel_map(qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH], media_fmt);

                qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]->compr_config.fragments =
                        COMPRESS_OFFLOAD_NUM_FRAGMENTS;
                qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]->compr_config.fragment_size =
                        qaf_get_pcm_offload_output_buffer_size(qaf_mod, &config.offload_info);

                p_qaf->mch_pcm_hdmi_enabled = true;

                if ((qaf_mod->stream_in[QAF_IN_MAIN]
                    && qaf_mod->stream_in[QAF_IN_MAIN]->client_callback != NULL) ||
                    (qaf_mod->stream_in[QAF_IN_MAIN_2]
                    && qaf_mod->stream_in[QAF_IN_MAIN_2]->client_callback != NULL)) {

                    if (qaf_mod->stream_in[QAF_IN_MAIN]) {
                        qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]->stream.set_callback(
                            (struct audio_stream_out *)qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH],
                            qaf_mod->stream_in[QAF_IN_MAIN]->client_callback,
                            qaf_mod->stream_in[QAF_IN_MAIN]->client_cookie);
                    }
                    if (qaf_mod->stream_in[QAF_IN_MAIN_2]) {
                        qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]->stream.set_callback(
                            (struct audio_stream_out *)qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH],
                            qaf_mod->stream_in[QAF_IN_MAIN_2]->client_callback,
                            qaf_mod->stream_in[QAF_IN_MAIN_2]->client_cookie);
                    }
                } else if (qaf_mod->stream_in[QAF_IN_PCM]
                           && qaf_mod->stream_in[QAF_IN_PCM]->client_callback != NULL) {

                    qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]->stream.set_callback(
                            (struct audio_stream_out *)qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH],
                            qaf_mod->stream_in[QAF_IN_PCM]->client_callback,
                            qaf_mod->stream_in[QAF_IN_PCM]->client_cookie);
                }

                int index = -1;
                if (qaf_mod->adsp_hdlr_config[QAF_IN_MAIN].adsp_hdlr_config_valid)
                    index = (int) QAF_IN_MAIN;
                else if (qaf_mod->adsp_hdlr_config[QAF_IN_MAIN_2].adsp_hdlr_config_valid)
                    index = (int) QAF_IN_MAIN_2;
                else if (qaf_mod->adsp_hdlr_config[QAF_IN_PCM].adsp_hdlr_config_valid)
                    index = (int) QAF_IN_PCM;

                if (index >= 0) {
                    if (qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]->standby)
                        qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]->stream.write(
                                (struct audio_stream_out *)qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH], NULL, 0);

                    lock_output_stream(qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]);
                    ret = audio_extn_out_set_param_data(
                            qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH],
                            AUDIO_EXTN_PARAM_ADSP_STREAM_CMD,
                            (audio_extn_param_payload *)&qaf_mod->adsp_hdlr_config[index].event_params);
                    unlock_output_stream(qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]);

                }
            }

            if (qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]) {
                ret = qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH]->stream.write(
                        (struct audio_stream_out *)qaf_mod->stream_out[QAF_OUT_OFFLOAD_MCH],
                        data_buffer_p,
                        buffer_size);
            }
        }
        else {
            /* CASE 3: PCM output.
             */

            /* If Media format was changed for this stream then need to re-create the stream. */
            if (need_to_recreate_stream && qaf_mod->stream_out[QAF_OUT_OFFLOAD]) {
                adev_close_output_stream((struct audio_hw_device *)p_qaf->adev,
                                         (struct audio_stream_out *)(qaf_mod->stream_out[QAF_OUT_OFFLOAD]));
                qaf_mod->stream_out[QAF_OUT_OFFLOAD] = NULL;
            }

            bt_stream = audio_extn_bt_hal_get_output_stream(qaf_mod->bt_hdl);
            if (bt_stream != NULL) {
                if (qaf_mod->stream_out[QAF_OUT_OFFLOAD]) {
                    adev_close_output_stream((struct audio_hw_device *)p_qaf->adev,
                                             (struct audio_stream_out *)(qaf_mod->stream_out[QAF_OUT_OFFLOAD]));
                    qaf_mod->stream_out[QAF_OUT_OFFLOAD] = NULL;
                }

                audio_extn_bt_hal_out_write(p_qaf->bt_hdl, data_buffer_p, buffer_size);
            } else if (NULL == qaf_mod->stream_out[QAF_OUT_OFFLOAD]) {
                audio_devices_t devices;

                if (qaf_mod->stream_in[QAF_IN_MAIN])
                    devices = get_device_types(&qaf_mod->stream_in[QAF_IN_MAIN]->device_list);
                else
                    devices = get_device_types(&qaf_mod->stream_in[QAF_IN_PCM]->device_list);

                //If multi channel pcm or passthrough is already enabled then remove the hdmi flag from device.
                if (p_qaf->mch_pcm_hdmi_enabled || p_qaf->passthrough_enabled) {
                    if (devices & AUDIO_DEVICE_OUT_AUX_DIGITAL)
                        devices ^= AUDIO_DEVICE_OUT_AUX_DIGITAL;
                }
                if (devices == 0) {
                    devices = device;
                }

                flags = AUDIO_OUTPUT_FLAG_DIRECT;

                /* TODO:: Need to Propagate errors to framework */
                ret = adev_open_output_stream((struct audio_hw_device *)p_qaf->adev,
                                              QAF_DEFAULT_COMPR_AUDIO_HANDLE,
                                              devices,
                                              flags,
                                              &config,
                                              (struct audio_stream_out **)&(qaf_mod->stream_out[QAF_OUT_OFFLOAD]),
                                              NULL);
                if (ret < 0) {
                    ERROR_MSG("adev_open_output_stream failed with ret = %d!", ret);
                    pthread_mutex_unlock(&p_qaf->lock);
                    return;
                }
                set_out_stream_channel_map(qaf_mod->stream_out[QAF_OUT_OFFLOAD], media_fmt);

                if ((qaf_mod->stream_in[QAF_IN_MAIN]
                    && qaf_mod->stream_in[QAF_IN_MAIN]->client_callback != NULL) ||
                    (qaf_mod->stream_in[QAF_IN_MAIN_2]
                    && qaf_mod->stream_in[QAF_IN_MAIN_2]->client_callback != NULL)) {

                    if (qaf_mod->stream_in[QAF_IN_MAIN]) {
                        qaf_mod->stream_out[QAF_OUT_OFFLOAD]->stream.set_callback(
                            (struct audio_stream_out *)qaf_mod->stream_out[QAF_OUT_OFFLOAD],
                            qaf_mod->stream_in[QAF_IN_MAIN]->client_callback,
                            qaf_mod->stream_in[QAF_IN_MAIN]->client_cookie);
                    }
                    if (qaf_mod->stream_in[QAF_IN_MAIN_2]) {
                        qaf_mod->stream_out[QAF_OUT_OFFLOAD]->stream.set_callback(
                            (struct audio_stream_out *)qaf_mod->stream_out[QAF_OUT_OFFLOAD],
                            qaf_mod->stream_in[QAF_IN_MAIN_2]->client_callback,
                            qaf_mod->stream_in[QAF_IN_MAIN_2]->client_cookie);
                    }
                } else if (qaf_mod->stream_in[QAF_IN_PCM]
                           && qaf_mod->stream_in[QAF_IN_PCM]->client_callback != NULL) {

                    qaf_mod->stream_out[QAF_OUT_OFFLOAD]->stream.set_callback(
                                                (struct audio_stream_out *)qaf_mod->stream_out[QAF_OUT_OFFLOAD],
                                                qaf_mod->stream_in[QAF_IN_PCM]->client_callback,
                                                qaf_mod->stream_in[QAF_IN_PCM]->client_cookie);
                }

                qaf_mod->stream_out[QAF_OUT_OFFLOAD]->compr_config.fragments =
                        COMPRESS_OFFLOAD_NUM_FRAGMENTS;
                qaf_mod->stream_out[QAF_OUT_OFFLOAD]->compr_config.fragment_size =
                        qaf_get_pcm_offload_output_buffer_size(qaf_mod, &config.offload_info);

                if (qaf_mod->is_vol_set) {
                    DEBUG_MSG("Setting Volume Left[%f], Right[%f]", qaf_mod->vol_left, qaf_mod->vol_right);
                    qaf_mod->stream_out[QAF_OUT_OFFLOAD]->stream.set_volume(
                            (struct audio_stream_out *)qaf_mod->stream_out[QAF_OUT_OFFLOAD],
                            qaf_mod->vol_left,
                            qaf_mod->vol_right);
                }

                int index = -1;
                if (qaf_mod->adsp_hdlr_config[QAF_IN_MAIN].adsp_hdlr_config_valid)
                    index = (int) QAF_IN_MAIN;
                else if (qaf_mod->adsp_hdlr_config[QAF_IN_MAIN_2].adsp_hdlr_config_valid)
                    index = (int) QAF_IN_MAIN_2;
                else if (qaf_mod->adsp_hdlr_config[QAF_IN_PCM].adsp_hdlr_config_valid)
                    index = (int) QAF_IN_PCM;
                if (index >= 0) {
                    if (qaf_mod->stream_out[QAF_OUT_OFFLOAD]->standby) {
                        qaf_mod->stream_out[QAF_OUT_OFFLOAD]->stream.write(
                                (struct audio_stream_out *)qaf_mod->stream_out[QAF_OUT_OFFLOAD], NULL, 0);
                    }

                    lock_output_stream(qaf_mod->stream_out[QAF_OUT_OFFLOAD]);
                    ret = audio_extn_out_set_param_data(
                            qaf_mod->stream_out[QAF_OUT_OFFLOAD],
                            AUDIO_EXTN_PARAM_ADSP_STREAM_CMD,
                            (audio_extn_param_payload *)&qaf_mod->adsp_hdlr_config[index].event_params);
                    unlock_output_stream(qaf_mod->stream_out[QAF_OUT_OFFLOAD]);
                }
            }

            /*
             * TODO:: Since this is mixed data,
             * need to identify to which stream the error should be sent
             */
            if (qaf_mod->stream_out[QAF_OUT_OFFLOAD]) {
                ret = qaf_mod->stream_out[QAF_OUT_OFFLOAD]->stream.write(
                        (struct audio_stream_out *)qaf_mod->stream_out[QAF_OUT_OFFLOAD],
                        data_buffer_p,
                        buffer_size);
            }
        }
        DEBUG_MSG_VV("Bytes written = %d", ret);
    }
    else if (event_id == AUDIO_EOS_EVENT
               || event_id == AUDIO_EOS_MAIN_DD_DDP_EVENT
               || event_id == AUDIO_EOS_MAIN_2_DD_DDP_EVENT
               || event_id == AUDIO_EOS_MAIN_AAC_EVENT
               || event_id == AUDIO_EOS_MAIN_AC4_EVENT
               || event_id == AUDIO_EOS_ASSOC_DD_DDP_EVENT
               || event_id == AUDIO_EOS_ASSOC_AAC_EVENT
               || event_id == AUDIO_EOS_ASSOC_AC4_EVENT) {
        struct stream_out *out = qaf_mod->stream_in[QAF_IN_MAIN];
        struct stream_out *out_pcm = qaf_mod->stream_in[QAF_IN_PCM];
        struct stream_out *out_main2 = qaf_mod->stream_in[QAF_IN_MAIN_2];
        struct stream_out *out_assoc = qaf_mod->stream_in[QAF_IN_ASSOC];

        /**
         * TODO:: Only DD/DDP Associate Eos is handled, need to add support
         * for other formats.
         */
        if (event_id == AUDIO_EOS_EVENT
                && (out_pcm != NULL)
                && (check_stream_state(out_pcm, STOPPING))) {

            lock_output_stream(out_pcm);
            out_pcm->client_callback(STREAM_CBK_EVENT_DRAIN_READY, NULL, out_pcm->client_cookie);
            set_stream_state(out_pcm, STOPPED);
            unlock_output_stream(out_pcm);
            DEBUG_MSG("sent pcm DRAIN_READY");
        } else if ( (event_id == AUDIO_EOS_ASSOC_DD_DDP_EVENT
                || event_id == AUDIO_EOS_ASSOC_AAC_EVENT
                || event_id == AUDIO_EOS_ASSOC_AC4_EVENT)
                && (out_assoc != NULL)
                && (check_stream_state(out_assoc, STOPPING))) {

            lock_output_stream(out_assoc);
            out_assoc->client_callback(STREAM_CBK_EVENT_DRAIN_READY, NULL, out_assoc->client_cookie);
            set_stream_state(out_assoc, STOPPED);
            unlock_output_stream(out_assoc);
            DEBUG_MSG("sent associated DRAIN_READY");
        } else if (event_id == AUDIO_EOS_MAIN_2_DD_DDP_EVENT
                && (out_main2 != NULL)
                && (check_stream_state(out_main2, STOPPING))) {

            lock_output_stream(out_main2);
            out_main2->client_callback(STREAM_CBK_EVENT_DRAIN_READY, NULL, out_main2->client_cookie);
            set_stream_state(out_main2, STOPPED);
            unlock_output_stream(out_main2);
            DEBUG_MSG("sent main2 DRAIN_READY");
        } else if ((out != NULL) && (check_stream_state(out, STOPPING))) {
            lock_output_stream(out);
            out->client_callback(STREAM_CBK_EVENT_DRAIN_READY, NULL, out->client_cookie);
            set_stream_state(out, STOPPED);
            unlock_output_stream(out);
            DEBUG_MSG("sent main DRAIN_READY");
        }
    }
    else if (event_id == AUDIO_MAIN_EOS_EVENT || event_id == AUDIO_ASSOC_EOS_EVENT) {
        struct stream_out *out = NULL;

        if (event_id == AUDIO_MAIN_EOS_EVENT) {
            out = qaf_mod->stream_in[QAF_IN_MAIN];
        } else {
            out = qaf_mod->stream_in[QAF_IN_ASSOC];
        }

        if ((out != NULL) && (check_stream_state(out, STOPPING))) {
            lock_output_stream(out);
            out->client_callback(STREAM_CBK_EVENT_DRAIN_READY, NULL, out->client_cookie);
            set_stream_state(out, STOPPED);
            unlock_output_stream(out);
            DEBUG_MSG("sent DRAIN_READY");
        }
    }

    pthread_mutex_unlock(&p_qaf->lock);
    return;
}

/* Close the mm module session. */
static int qaf_session_close(struct qaf_module* qaf_mod)
{
    int j;

    DEBUG_MSG("Closing Session.");

    //Check if all streams are closed or not.
    for (j = 0; j < MAX_QAF_MODULE_IN; j++) {
        if (qaf_mod->stream_in[j] != NULL) {
            break;
        }
    }
    if (j != MAX_QAF_MODULE_IN) {
        return 0; //Some stream is already active, Can not close session.
    }

    qaf_mod->is_session_closing = true;
    pthread_mutex_lock(&p_qaf->lock);

    if (qaf_mod->session_handle != NULL && qaf_mod->qaf_audio_session_close) {
#ifdef AUDIO_EXTN_IP_HDLR_ENABLED
        if (qaf_mod == &p_qaf->qaf_mod[MS12]) {
            audio_extn_ip_hdlr_intf_close(qaf_mod->ip_hdlr_hdl, false, qaf_mod->session_handle);
        }
#endif
        qaf_mod->qaf_audio_session_close(qaf_mod->session_handle);
        qaf_mod->session_handle = NULL;
        qaf_mod->is_vol_set = false;
        memset(qaf_mod->stream_state, 0, sizeof(qaf_mod->stream_state));
    }

    for (j = 0; j < MAX_QAF_MODULE_OUT; j++) {
        if (qaf_mod->stream_out[j]) {
            adev_close_output_stream((struct audio_hw_device *)p_qaf->adev,
                                     (struct audio_stream_out *)(qaf_mod->stream_out[j]));
            qaf_mod->stream_out[j] = NULL;
        }
        memset(&qaf_mod->out_stream_fmt[j], 0, sizeof(audio_qaf_media_format_t));
        qaf_mod->is_media_fmt_changed[j] = false;
    }
    qaf_mod->new_out_format_index = 0;

    pthread_mutex_unlock(&p_qaf->lock);
    qaf_mod->is_session_closing = false;
    DEBUG_MSG("Session Closed.");

    return 0;
}

/* Close the stream of QAF module. */
static int qaf_stream_close(struct stream_out *out)
{
    int ret = -EINVAL;
    struct qaf_module *qaf_mod = NULL;
    int index = -1;
    DEBUG_MSG("Flag [0x%x], Stream handle [%p]", out->flags, out->qaf_stream_handle);

    qaf_mod = get_qaf_module_for_input_stream(out);
    index = get_input_stream_index(out);

    if (!qaf_mod || !qaf_mod->qaf_audio_stream_close || index < 0) {
        return -EINVAL;
    }

    pthread_mutex_lock(&p_qaf->lock);

    set_stream_state(out,STOPPED);
    qaf_mod->stream_in[index] = NULL;
    memset(&qaf_mod->adsp_hdlr_config[index], 0, sizeof(struct qaf_adsp_hdlr_config_state));

    lock_output_stream(out);
    if (out->qaf_stream_handle) {
        ret = qaf_mod->qaf_audio_stream_close(out->qaf_stream_handle);
        out->qaf_stream_handle = NULL;
    }
    unlock_output_stream(out);

    pthread_mutex_unlock(&p_qaf->lock);

    //If all streams are closed then close the session.
    qaf_session_close(qaf_mod);

    DEBUG_MSG();
    return ret;
}

/* Open a MM module session with QAF. */
static int audio_extn_qaf_session_open(mm_module_type mod_type, struct stream_out *out)
{
    ALOGV("%s %d", __func__, __LINE__);
    unsigned char* license_data = NULL;
    device_license_config_t lic_config = {NULL, 0, 0};
    int ret = -ENOSYS;

    struct qaf_module *qaf_mod = NULL;

    if (mod_type >= MAX_MM_MODULE_TYPE || !(p_qaf->qaf_mod[mod_type].qaf_audio_session_open))
        return -ENOTSUP; //Not supported by QAF module.

    pthread_mutex_lock(&p_qaf->lock);

    qaf_mod = &(p_qaf->qaf_mod[mod_type]);

    //If session is already opened then return.
    if (qaf_mod->session_handle) {
        DEBUG_MSG("Session is already opened.");
        pthread_mutex_unlock(&p_qaf->lock);
        return 0;
    }

#ifndef AUDIO_EXTN_IP_HDLR_ENABLED
 {
    int size=0;
    char value[PROPERTY_VALUE_MAX] = {0};
    if (mod_type == MS12) {
        //Getting the license
        license_data = platform_get_license((struct audio_hw_device *)(p_qaf->adev->platform),
                                            &size);
        if (!license_data) {
            ERROR_MSG("License data is not present.");
            pthread_mutex_unlock(&p_qaf->lock);
            return -EINVAL;
        }

        lic_config.p_license = (unsigned char*)calloc(1, size);
        if (lic_config.p_license == NULL) {
            ERROR_MSG("Out of Memory");
            ret = -ENOMEM;
            goto exit;
        }

        lic_config.l_size = size;
        memcpy(lic_config.p_license, license_data, size);

        if (property_get("vendor.audio.qaf.manufacturer", value, "") && atoi(value)) {
            lic_config.manufacturer_id = (unsigned long)atoi(value);
        } else {
            ERROR_MSG("vendor.audio.qaf.manufacturer id is not set");
            ret = -EINVAL;
            goto exit;
        }
    }
}
#endif

    ret = qaf_mod->qaf_audio_session_open(&qaf_mod->session_handle,
                                          AUDIO_SESSION_BROADCAST,
                                          (void *)(qaf_mod),
                                          (void *)&lic_config);
    if (ret < 0) {
        ERROR_MSG("Error in session open %d", ret);
        goto exit;
    }

    if (qaf_mod->session_handle == NULL) {
        ERROR_MSG("Session handle is NULL.");
        ret = -ENOMEM;
        goto exit;
    }

    if (qaf_mod->qaf_register_event_callback)
        qaf_mod->qaf_register_event_callback(qaf_mod->session_handle,
                                             qaf_mod,
                                             &notify_event_callback,
                                             AUDIO_DATA_EVENT_V2);
    if(p_qaf->bt_connect)
         set_bt_configuration_to_module();
    else
         set_hdmi_configuration_to_module();

#ifdef AUDIO_EXTN_IP_HDLR_ENABLED
    if (mod_type == MS12) {
        ret = audio_extn_ip_hdlr_intf_open(qaf_mod->ip_hdlr_hdl, false, qaf_mod->session_handle, out->usecase);
        if (ret < 0) {
            ERROR_MSG("%s audio_extn_ip_hdlr_intf_open failed, ret = %d", __func__, ret);
            goto exit;
        }
    }
#endif

exit:
    if (license_data != NULL) {
        free(license_data);
        license_data = NULL;
    }
    if (lic_config.p_license != NULL) {
        free(lic_config.p_license);
        lic_config.p_license = NULL;
    }

    pthread_mutex_unlock(&p_qaf->lock);
    return ret;
}

/* opens a stream in QAF module. */
static int qaf_stream_open(struct stream_out *out,
                           struct audio_config *config,
                           audio_output_flags_t flags,
                           audio_devices_t devices)
{
    int status = -EINVAL;
    mm_module_type mmtype = get_mm_module_for_format(config->format);
    struct qaf_module* qaf_mod = NULL;
    DEBUG_MSG("Flags 0x%x, Device 0x%x", flags, devices);

    if (mmtype >= MAX_MM_MODULE_TYPE) {
        ERROR_MSG("Unsupported Stream");
        return -ENOTSUP;
    }

    if (p_qaf->qaf_mod[mmtype].qaf_audio_session_open == NULL ||
        p_qaf->qaf_mod[mmtype].qaf_audio_stream_open == NULL) {
        ERROR_MSG("Session or Stream is NULL");
        return -ENOTSUP;
    }
    //Open the module session, if not opened already.
    status = audio_extn_qaf_session_open(mmtype, out);
    qaf_mod = &(p_qaf->qaf_mod[mmtype]);

    if ((status != 0) || (qaf_mod->session_handle == NULL)) {
        ERROR_MSG("Failed to open session.");
        return status;
    }

    audio_stream_config_t input_config;
    input_config.sample_rate = config->sample_rate;
    input_config.channels = popcount(config->channel_mask);
    input_config.format = config->format;

    if (input_config.format != AUDIO_FORMAT_PCM_16_BIT) {
        input_config.format &= AUDIO_FORMAT_MAIN_MASK;
    }

    DEBUG_MSG("stream_open sample_rate(%d) channels(%d) devices(%#x) flags(%#x) format(%#x)",
              input_config.sample_rate, input_config.channels, devices, flags, input_config.format);

    if (input_config.format == AUDIO_FORMAT_PCM_16_BIT) {
        //If PCM stream is already opened then fail this stream open.
        if (qaf_mod->stream_in[QAF_IN_PCM]) {
            ERROR_MSG("PCM input is already active.");
            return -ENOTSUP;
        }

        //TODO: Flag can be system tone or external associated PCM.
        status = qaf_mod->qaf_audio_stream_open(qaf_mod->session_handle,
                                                &out->qaf_stream_handle,
                                                input_config,
                                                devices,
                                                AUDIO_STREAM_SYSTEM_TONE);
        if (status == 0) {
            qaf_mod->stream_in[QAF_IN_PCM] = out;
        } else {
            ERROR_MSG("System tone stream open failed with QAF module !!!");
        }
    } else if ((flags & AUDIO_OUTPUT_FLAG_MAIN) && (flags & AUDIO_OUTPUT_FLAG_ASSOCIATED)) {
        if (is_main_active(qaf_mod) || is_dual_main_active(qaf_mod)) {
            ERROR_MSG("Dual Main or Main already active. So, Cannot open main and associated stream");
            return -EINVAL;
        } else {
            status = qaf_mod->qaf_audio_stream_open(qaf_mod->session_handle, &out->qaf_stream_handle, input_config, devices, /*flags*/AUDIO_STREAM_MAIN);
            if (status == 0) {
                DEBUG_MSG("Open stream for Input with both Main and Associated stream contents with flag(%x) and stream_handle(%p)", flags, out->qaf_stream_handle);
                qaf_mod->stream_in[QAF_IN_MAIN] = out;
            } else {
                ERROR_MSG("Stream Open FAILED !!!");
            }
        }
    } else if ((flags & AUDIO_OUTPUT_FLAG_MAIN) || ((!(flags & AUDIO_OUTPUT_FLAG_MAIN)) && (!(flags & AUDIO_OUTPUT_FLAG_ASSOCIATED)))) {
        /* Assume Main if no flag is set */
        if (is_dual_main_active(qaf_mod)) {
            ERROR_MSG("Dual Main already active. So, Cannot open main stream");
            return -EINVAL;
        } else if (is_main_active(qaf_mod) && qaf_mod->stream_in[QAF_IN_ASSOC]) {
            ERROR_MSG("Main and Associated already active. So, Cannot open main stream");
            return -EINVAL;
        } else if (is_main_active(qaf_mod) && (mmtype != MS12)) {
            ERROR_MSG("Main already active and Not an MS12 format. So, Cannot open another main stream");
            return -EINVAL;
        } else {
            status = qaf_mod->qaf_audio_stream_open(qaf_mod->session_handle, &out->qaf_stream_handle, input_config, devices, /*flags*/AUDIO_STREAM_MAIN);
            if (status == 0) {
                DEBUG_MSG("Open stream for Input with only Main flag(%x) stream_handle(%p)", flags, out->qaf_stream_handle);
                if(qaf_mod->stream_in[QAF_IN_MAIN]) {
                    qaf_mod->stream_in[QAF_IN_MAIN_2] = out;
                } else {
                    qaf_mod->stream_in[QAF_IN_MAIN] = out;
                }
            } else {
                ERROR_MSG("Stream Open FAILED !!!");
            }
        }
    } else if ((flags & AUDIO_OUTPUT_FLAG_ASSOCIATED)) {
        if (is_dual_main_active(qaf_mod)) {
            ERROR_MSG("Dual Main already active. So, Cannot open associated stream");
            return -EINVAL;
        } else if (!is_main_active(qaf_mod)) {
            ERROR_MSG("Main not active. So, Cannot open associated stream");
            return -EINVAL;
        } else if (qaf_mod->stream_in[QAF_IN_ASSOC]) {
            ERROR_MSG("Associated already active. So, Cannot open associated stream");
            return -EINVAL;
        }
        status = qaf_mod->qaf_audio_stream_open(qaf_mod->session_handle, &out->qaf_stream_handle, input_config, devices, /*flags*/AUDIO_STREAM_ASSOCIATED);
        if (status == 0) {
            DEBUG_MSG("Open stream for Input with only Associated flag(%x) stream handle(%p)", flags, out->qaf_stream_handle);
            qaf_mod->stream_in[QAF_IN_ASSOC] = out;
        } else {
            ERROR_MSG("Stream Open FAILED !!!");
        }
    }

    if (status != 0) {
        //If no stream is active then close the session.
        qaf_session_close(qaf_mod);
        return 0;
    }

    //If Device is HDMI, QAF passthrough is enabled and there is no previous QAF passthrough input stream.
    if ((!p_qaf->passthrough_in)
        && (devices & AUDIO_DEVICE_OUT_AUX_DIGITAL)
        && audio_extn_qaf_passthrough_enabled(out)) {
        //Assign the QAF passthrough input stream.
        p_qaf->passthrough_in = out;

        //If HDMI is connected and format is supported by HDMI then create QAF passthrough output stream.
        if (p_qaf->hdmi_connect
            && platform_is_edid_supported_format(p_qaf->adev->platform, out->format)) {
            status = create_qaf_passthrough_stream();
            if (status < 0) {
                qaf_stream_close(out);
                ERROR_MSG("QAF passthrough stream creation failed with error %d", status);
                return status;
            }
        }
        /*Else: since QAF passthrough input stream is already initialized,
         * when hdmi is connected
         * then qaf passthrough output stream will be created.
         */
    }

    DEBUG_MSG();
    return status;
}

/* Resume a QAF stream. */
static int qaf_out_resume(struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = 0;
    DEBUG_MSG("Output Stream %p", out);


    lock_output_stream(out);

    //If QAF passthrough is active then block the resume on module input streams.
    if (p_qaf->passthrough_out) {
        //If resume is received for the QAF passthrough stream then call the primary HAL api.
        pthread_mutex_lock(&p_qaf->lock);
        if (p_qaf->passthrough_in == out) {
            status = p_qaf->passthrough_out->stream.resume(
                    (struct audio_stream_out*)p_qaf->passthrough_out);
            if (!status) out->offload_state = OFFLOAD_STATE_PLAYING;
        }
        pthread_mutex_unlock(&p_qaf->lock);
    } else {
        //Flush the module input stream.
        status = qaf_stream_start(out);
    }

    unlock_output_stream(out);

    DEBUG_MSG();
    return status;
}

/* Offload thread for QAF output streams. */
static void *qaf_offload_thread_loop(void *context)
{
    struct stream_out *out = (struct stream_out *)context;
    struct listnode *item;
    int ret = 0;
    struct str_parms *parms = NULL;
    int value = 0;
    char* kvpairs = NULL;
    struct qaf_module *qaf_mod = NULL;

    qaf_mod = get_qaf_module_for_input_stream(out);

    if (!qaf_mod) return NULL;

    setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_AUDIO);
    set_sched_policy(0, SP_FOREGROUND);
    prctl(PR_SET_NAME, (unsigned long)"Offload Callback", 0, 0, 0);

    lock_output_stream(out);

    DEBUG_MSG();
    for (;;) {
        struct offload_cmd *cmd = NULL;
        stream_callback_event_t event;
        bool send_callback = false;

        DEBUG_MSG_VV("List Empty %d (1:TRUE, 0:FALSE)", list_empty(&out->qaf_offload_cmd_list));
        if (list_empty(&out->qaf_offload_cmd_list)) {
            DEBUG_MSG_VV("SLEEPING");
            pthread_cond_wait(&out->qaf_offload_cond, &out->lock);
            DEBUG_MSG_VV("RUNNING");
            continue;
        }

        item = list_head(&out->qaf_offload_cmd_list);
        cmd = node_to_item(item, struct offload_cmd, node);
        list_remove(item);

        if (cmd->cmd == OFFLOAD_CMD_EXIT) {
            free(cmd);
            break;
        }

        unlock_output_stream(out);

        send_callback = false;
        switch (cmd->cmd) {
            case OFFLOAD_CMD_WAIT_FOR_BUFFER: {
                DEBUG_MSG_VV("wait for buffer availability");

                while (1) {
                    kvpairs = qaf_mod->qaf_audio_stream_get_param(out->qaf_stream_handle,
                                                                  "buf_available");
                    if (kvpairs) {
                        parms = str_parms_create_str(kvpairs);
                        ret = str_parms_get_int(parms, "buf_available", &value);
                        if (ret >= 0) {
                            if (value > 0) {
                                DEBUG_MSG_VV("buffer available");
                                str_parms_destroy(parms);
                                parms = NULL;
                                break;
                            } else {
                                DEBUG_MSG_VV("sleep");
                                str_parms_destroy(parms);
                                parms = NULL;
                                usleep(10000);
                            }
                        }
                        free(kvpairs);
                        kvpairs = NULL;
                    }
                }
                send_callback = true;
                event = STREAM_CBK_EVENT_WRITE_READY;
                break;
            }
            default:
                DEBUG_MSG("unknown command received: %d", cmd->cmd);
            break;
        }

        lock_output_stream(out);

        if (send_callback && out->client_callback) {
            out->client_callback(event, NULL, out->client_cookie);
        }

        free(cmd);
    }

    while (!list_empty(&out->qaf_offload_cmd_list)) {
        item = list_head(&out->qaf_offload_cmd_list);
        list_remove(item);
        free (node_to_item( item, struct offload_cmd, node));
    }
    unlock_output_stream(out);

    return NULL;
}

/* Create the offload callback thread for QAF output stream. */
static int qaf_create_offload_callback_thread(struct stream_out *out)
{
    DEBUG_MSG("Output Stream %p", out);
    lock_output_stream(out);
    pthread_cond_init(&out->qaf_offload_cond, (const pthread_condattr_t *)NULL);
    list_init(&out->qaf_offload_cmd_list);
    pthread_create(&out->qaf_offload_thread,
                   (const pthread_attr_t *)NULL,
                   qaf_offload_thread_loop,
                   out);
    unlock_output_stream(out);
    return 0;
}

/* Destroy the offload callback thread of QAF output stream. */
static int qaf_destroy_offload_callback_thread(struct stream_out *out)
{
    DEBUG_MSG("Output Stream %p", out);
    qaf_send_offload_cmd_l(out, OFFLOAD_CMD_EXIT);

    pthread_join(out->qaf_offload_thread, (void **)NULL);
    pthread_cond_destroy(&out->qaf_offload_cond);
    return 0;
}

/* Sets the stream set parameters (device routing information). */
static int qaf_out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct str_parms *parms;
    char value[32];
    int val = 0;
    struct stream_out *out = (struct stream_out *)stream;
    int ret = 0;
    int err = 0;
    struct qaf_module *qaf_mod = NULL;

    DEBUG_MSG("usecase(%d: %s) kvpairs: %s", out->usecase, use_case_table[out->usecase], kvpairs);

    parms = str_parms_create_str(kvpairs);
    err = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (err < 0)
        return err;
    val = atoi(value);

    qaf_mod = get_qaf_module_for_input_stream(out);
    if (!qaf_mod) return (-EINVAL);

    //TODO: HDMI is connected but user doesn't want HDMI output, close both HDMI outputs.

    /* Setting new device information to the mm module input streams.
     * This is needed if QAF module output streams are not created yet.
     */
    reassign_device_list(&out->device_list, val, "");

#ifndef A2DP_OFFLOAD_ENABLED
    if (val == AUDIO_DEVICE_OUT_BLUETOOTH_A2DP) {
        //If device is BT then open the BT stream if not already opened.
        if ( audio_extn_bt_hal_get_output_stream(qaf_mod->bt_hdl) == NULL
             && audio_extn_bt_hal_get_device(qaf_mod->bt_hdl) != NULL) {
            ret = audio_extn_bt_hal_open_output_stream(qaf_mod->bt_hdl,
                                                       QAF_OUTPUT_SAMPLING_RATE,
                                                       AUDIO_CHANNEL_OUT_STEREO,
                                                       CODEC_BACKEND_DEFAULT_BIT_WIDTH);
            if (ret != 0) {
                ERROR_MSG("BT Output stream open failure!");
            }
        }
    } else if (val != 0) {
        //If device is not BT then close the BT stream if already opened.
        if ( audio_extn_bt_hal_get_output_stream(qaf_mod->bt_hdl) != NULL) {
            audio_extn_bt_hal_close_output_stream(qaf_mod->bt_hdl);
        }
    }
#endif

    if (p_qaf->passthrough_in == out) { //Device routing is received for QAF passthrough stream.

        if (!(val & AUDIO_DEVICE_OUT_AUX_DIGITAL)) { //HDMI route is disabled.

            //If QAF pasthrough output is enabled. Close it.
            close_qaf_passthrough_stream();

            //Send the routing information to mm module pcm output.
            if (qaf_mod->stream_out[QAF_OUT_OFFLOAD]) {
                ret = qaf_mod->stream_out[QAF_OUT_OFFLOAD]->stream.common.set_parameters(
                        (struct audio_stream *)qaf_mod->stream_out[QAF_OUT_OFFLOAD], kvpairs);
            }
            //else: device info is updated in the input streams.
        } else { //HDMI route is enabled.

            //create the QAf passthrough stream, if not created already.
            ret = create_qaf_passthrough_stream();

            if (p_qaf->passthrough_out != NULL) { //If QAF passthrough out is enabled then send routing information.
                ret = p_qaf->passthrough_out->stream.common.set_parameters(
                        (struct audio_stream *)p_qaf->passthrough_out, kvpairs);
            }
        }
    } else {
        //Send the routing information to mm module pcm output.
        if (qaf_mod->stream_out[QAF_OUT_OFFLOAD]) {
            ret = qaf_mod->stream_out[QAF_OUT_OFFLOAD]->stream.common.set_parameters(
                    (struct audio_stream *)qaf_mod->stream_out[QAF_OUT_OFFLOAD], kvpairs);
        }
        //else: device info is updated in the input streams.
    }
    str_parms_destroy(parms);

    return ret;
}

/* Checks if a stream is QAF stream or not. */
bool audio_extn_is_qaf_stream(struct stream_out *out)
{
    struct qaf_module *qaf_mod = get_qaf_module_for_input_stream(out);

    if (qaf_mod) {
        return true;
    }
    return false;
}

/* API to send playback stream specific config parameters */
int audio_extn_qaf_out_set_param_data(struct stream_out *out,
                                       audio_extn_param_id param_id,
                                       audio_extn_param_payload *payload)
{
    int ret = -EINVAL;
    int index;
    struct stream_out *new_out = NULL;
    struct audio_adsp_event *adsp_event;
    struct qaf_module *qaf_mod = get_qaf_module_for_input_stream(out);

    if (!out || !qaf_mod || !payload) {
        ERROR_MSG("Invalid Param");
        return ret;
    }

    /* In qaf output render session may not be opened at this time.
     to handle it store adsp_hdlr param info so that it can be
     applied later after opening render session from ms12 callback
     */
    if (param_id == AUDIO_EXTN_PARAM_ADSP_STREAM_CMD) {
        index = get_input_stream_index(out);
        if (index < 0) {
            ERROR_MSG("Invalid stream");
            return ret;
        }
        adsp_event = (struct audio_adsp_event *)payload;

        if (payload->adsp_event_params.payload_length <= AUDIO_MAX_ADSP_STREAM_CMD_PAYLOAD_LEN) {
            pthread_mutex_lock(&p_qaf->lock);
            memcpy(qaf_mod->adsp_hdlr_config[index].event_payload,
                   adsp_event->payload,
                   adsp_event->payload_length);
            qaf_mod->adsp_hdlr_config[index].event_params.payload =
                    qaf_mod->adsp_hdlr_config[index].event_payload;
            qaf_mod->adsp_hdlr_config[index].event_params.payload_length =
                    adsp_event->payload_length;
            qaf_mod->adsp_hdlr_config[index].adsp_hdlr_config_valid = true;
            pthread_mutex_unlock(&p_qaf->lock);
        } else {
            ERROR_MSG("Invalid adsp event length %d", adsp_event->payload_length);
            return ret;
        }
        ret = 0;
    }

    /* apply param for all active out sessions */
    for (index = 0; index < MAX_QAF_MODULE_OUT; index++) {
        new_out = qaf_mod->stream_out[index];
        if (!new_out) continue;

        /*ADSP event is not supported for passthrough*/
        if ((param_id == AUDIO_EXTN_PARAM_ADSP_STREAM_CMD)
            && !(new_out->flags == AUDIO_OUTPUT_FLAG_DIRECT)) continue;
        if (new_out->standby)
            new_out->stream.write((struct audio_stream_out *)new_out, NULL, 0);
        lock_output_stream(new_out);
        ret = audio_extn_out_set_param_data(new_out, param_id, payload);
        if (ret)
            ERROR_MSG("audio_extn_out_set_param_data error %d", ret);
        unlock_output_stream(new_out);
    }
    return ret;
}

int audio_extn_qaf_out_get_param_data(struct stream_out *out,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload)
{
    int ret = -EINVAL, i;
    struct stream_out *new_out = NULL;
    struct qaf_module *qaf_mod = get_qaf_module_for_input_stream(out);

    if (!out || !qaf_mod || !payload) {
        ERROR_MSG("Invalid Param");
        return ret;
    }

    if (!p_qaf->hdmi_connect) {
        ERROR_MSG("hdmi not connected");
        return ret;
    }

    /* get session which is routed to hdmi*/
    if (p_qaf->passthrough_out)
        new_out = p_qaf->passthrough_out;
    else {
        for (i = 0; i < MAX_QAF_MODULE_OUT; i++) {
            if (qaf_mod->stream_out[i]) {
                new_out = qaf_mod->stream_out[i];
                break;
            }
        }
    }

    if (!new_out) {
        ERROR_MSG("No stream active.");
        return ret;
    }

    if (new_out->standby)
        new_out->stream.write((struct audio_stream_out *)new_out, NULL, 0);

    lock_output_stream(new_out);
    ret = audio_extn_out_get_param_data(new_out, param_id, payload);
    if (ret)
        ERROR_MSG("audio_extn_out_get_param_data error %d", ret);
    unlock_output_stream(new_out);

    return ret;
}

/* To open a stream with QAF. */
int audio_extn_qaf_open_output_stream(struct audio_hw_device *dev,
                                      audio_io_handle_t handle,
                                      audio_devices_t devices,
                                      audio_output_flags_t flags,
                                      struct audio_config *config,
                                      struct audio_stream_out **stream_out,
                                      const char *address)
{
    int ret = 0;
    struct stream_out *out;

    ret = adev_open_output_stream(dev, handle, devices, flags, config, stream_out, address);
    if (*stream_out == NULL) {
        ERROR_MSG("Stream open failed %d", ret);
        return ret;
    }

#ifndef LINUX_ENABLED
//Bypass QAF for dummy PCM session opened by APM during boot time
    if(flags == 0) {
        ALOGD("bypassing QAF for flags is equal to none");
        return ret;
    }
#endif

    out = (struct stream_out *)*stream_out;

    ret = qaf_stream_open(out, config, flags, devices);
    if (ret < 0) {
        ERROR_MSG("Error opening QAF stream err[%d]! QAF bypassed.", ret);
        //Stream not supported by QAF, Bypass QAF.
        return 0;
    }

    /* Override function pointers based on qaf definitions */
    out->stream.set_volume = qaf_out_set_volume;
    out->stream.pause = qaf_out_pause;
    out->stream.resume = qaf_out_resume;
    out->stream.drain = qaf_out_drain;
    out->stream.flush = qaf_out_flush;

    out->stream.common.standby = qaf_out_standby;
    out->stream.common.set_parameters = qaf_out_set_parameters;
    out->stream.get_latency = qaf_out_get_latency;
    out->stream.get_render_position = qaf_out_get_render_position;
    out->stream.write = qaf_out_write;
    out->stream.get_presentation_position = qaf_out_get_presentation_position;
    out->platform_latency = 0;

    /*TODO: Need to handle this for DTS*/
    if (out->usecase == USECASE_AUDIO_PLAYBACK_LOW_LATENCY) {
        out->usecase = USECASE_AUDIO_PLAYBACK_DEEP_BUFFER;
        out->config.period_size = QAF_DEEP_BUFFER_OUTPUT_PERIOD_SIZE;
        out->config.period_count = DEEP_BUFFER_OUTPUT_PERIOD_COUNT;
        out->config.start_threshold = QAF_DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4;
        out->config.avail_min = QAF_DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4;
    } else if(out->flags == AUDIO_OUTPUT_FLAG_DIRECT) {
        out->compr_config.fragment_size = qaf_get_pcm_offload_input_buffer_size(&(config->offload_info));
    }

    *stream_out = &out->stream;
    if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
        qaf_create_offload_callback_thread(out);
    }

    DEBUG_MSG("Exit");
    return 0;
}

/* Close a QAF stream. */
void audio_extn_qaf_close_output_stream(struct audio_hw_device *dev,
                                        struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct qaf_module* qaf_mod = get_qaf_module_for_input_stream(out);

    if (!qaf_mod) {
        DEBUG_MSG("qaf module is NULL, bypassing qaf on close output stream");
        /*closing non-MS12/default output stream opened with qaf */
        adev_close_output_stream(dev, stream);
        return;
    }

    DEBUG_MSG("stream_handle(%p) format = %x", out, out->format);

    //If close is received for QAF passthrough stream then close the QAF passthrough output.
    if (p_qaf->passthrough_in == out) {
        if (p_qaf->passthrough_out) {
            ALOGD("%s %d closing stream handle %p", __func__, __LINE__, p_qaf->passthrough_out);
            pthread_mutex_lock(&p_qaf->lock);
            adev_close_output_stream((struct audio_hw_device *)p_qaf->adev,
                                     (struct audio_stream_out *)(p_qaf->passthrough_out));
            pthread_mutex_unlock(&p_qaf->lock);
            p_qaf->passthrough_out = NULL;
        }

        p_qaf->passthrough_in = NULL;
    }

    if (out->flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
        qaf_destroy_offload_callback_thread(out);
    }

    qaf_stream_close(out);

    adev_close_output_stream(dev, stream);

    DEBUG_MSG("Exit");
}

/* Check if QAF is supported or not. */
bool audio_extn_qaf_is_enabled()
{
    bool prop_enabled = false;
    char value[PROPERTY_VALUE_MAX] = {0};
    property_get("vendor.audio.qaf.enabled", value, NULL);
    prop_enabled = atoi(value) || !strncmp("true", value, 4);
    return (prop_enabled);
}

void set_bt_configuration_to_module()
{
    if (!p_qaf) {
        return;
    }

    if (!p_qaf->bt_connect) {
        DEBUG_MSG("BT is not connected.");
        return;
    }

    struct str_parms *qaf_params;
    char *format_params = NULL;

    qaf_params = str_parms_create();
    if (qaf_params) {
        //ms12 wrapper don't support bt, treat this as speaker and routign to bt
        //will take care as a part of data callback notifier
        str_parms_add_str(qaf_params,
            AUDIO_QAF_PARAMETER_KEY_DEVICE,
            AUDIO_QAF_PARAMETER_VALUE_DEVICE_SPEAKER);

        str_parms_add_str(qaf_params,
                          AUDIO_QAF_PARAMETER_KEY_RENDER_FORMAT,
                          AUDIO_QAF_PARAMETER_VALUE_PCM);
        format_params = str_parms_to_str(qaf_params);

        if (p_qaf->qaf_mod[MS12].session_handle && p_qaf->qaf_mod[MS12].qaf_audio_session_set_param) {
            ALOGE(" Configuring BT/speaker for MS12 wrapper");
            p_qaf->qaf_mod[MS12].qaf_audio_session_set_param(p_qaf->qaf_mod[MS12].session_handle,
                                                         format_params);
        }
        if (p_qaf->qaf_mod[DTS_M8].session_handle
                && p_qaf->qaf_mod[DTS_M8].qaf_audio_session_set_param) {
            ALOGE(" Configuring BT/speaker for MS12 wrapper");
            p_qaf->qaf_mod[DTS_M8].qaf_audio_session_set_param(p_qaf->qaf_mod[DTS_M8].session_handle,
                                                           format_params);
        }
    }
    str_parms_destroy(qaf_params);

}


/* Query HDMI EDID and sets module output accordingly.*/
void set_hdmi_configuration_to_module()
{
    int channels = 0;
    char *format_params;
    struct str_parms *qaf_params;
    char prop_value[PROPERTY_VALUE_MAX];
    bool passth_support = false;

    DEBUG_MSG("Entry");

    if (!p_qaf) {
        return;
    }

    if (!p_qaf->hdmi_connect) {
        DEBUG_MSG("HDMI is not connected.");
        return;
    }

    p_qaf->hdmi_sink_channels = 0;

    //QAF re-encoding and DSP offload passthrough is supported.
    if (property_get_bool("vendor.audio.offload.passthrough", false)
            && property_get_bool("vendor.audio.qaf.reencode", false)) {

        //If MS12 session is active.
        if (p_qaf->qaf_mod[MS12].session_handle && p_qaf->qaf_mod[MS12].qaf_audio_session_set_param) {

            bool do_setparam = false;
            qaf_params = str_parms_create();
            property_get("vendor.audio.qaf.hdmi.out", prop_value, NULL);

            if (platform_is_edid_supported_format(p_qaf->adev->platform, AUDIO_FORMAT_E_AC3)
                    && (strncmp(prop_value, "ddp", 3) == 0)) {
                do_setparam = true;
                if (qaf_params) {
                    str_parms_add_str(qaf_params,
                                      AUDIO_QAF_PARAMETER_KEY_RENDER_FORMAT,
                                      AUDIO_QAF_PARAMETER_VALUE_REENCODE_EAC3);
                }
            } else if (platform_is_edid_supported_format(p_qaf->adev->platform, AUDIO_FORMAT_AC3)) {
                do_setparam = true;
                if (qaf_params) {
                    str_parms_add_str(qaf_params,
                                      AUDIO_QAF_PARAMETER_KEY_RENDER_FORMAT,
                                      AUDIO_QAF_PARAMETER_VALUE_REENCODE_AC3);
                }
            }

            if (do_setparam) {
                if (p_qaf->qaf_msmd_enabled) {
                    str_parms_add_str(qaf_params,
                                      AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                      AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI_AND_SPK); //TODO: Need enhancement.
                } else {
                    str_parms_add_str(qaf_params,
                                      AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                      AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI);
                }
                format_params = str_parms_to_str(qaf_params);

                p_qaf->qaf_mod[MS12].qaf_audio_session_set_param(p_qaf->qaf_mod[MS12].session_handle,
                                                                 format_params);

                passth_support = true;
            }
            str_parms_destroy(qaf_params);
        }

        //DTS_M8 session is active.
        if (p_qaf->qaf_mod[DTS_M8].session_handle
                && p_qaf->qaf_mod[DTS_M8].qaf_audio_session_set_param) {

            bool do_setparam = false;
            qaf_params = str_parms_create();
            if (platform_is_edid_supported_format(p_qaf->adev->platform, AUDIO_FORMAT_DTS)) {
                do_setparam = true;
                if (qaf_params) {
                    str_parms_add_str(qaf_params,
                                      AUDIO_QAF_PARAMETER_KEY_RENDER_FORMAT,
                                      AUDIO_QAF_PARAMETER_VALUE_REENCODE_DTS);
                }
            }

            if (do_setparam) {
                if (p_qaf->qaf_msmd_enabled) {
                    str_parms_add_str(qaf_params,
                                      AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                      AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI_AND_SPK); //TODO: Need enhancement.
                } else {
                    str_parms_add_str(qaf_params,
                                      AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                      AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI);
                }
                format_params = str_parms_to_str(qaf_params);

                p_qaf->qaf_mod[DTS_M8].qaf_audio_session_set_param(p_qaf->qaf_mod[DTS_M8].session_handle,
                                                                   format_params);

                passth_support = true;
            }
            str_parms_destroy(qaf_params);
        }
    }

    //Compressed passthrough is not enabled.
    if (!passth_support) {

        channels = platform_edid_get_max_channels(p_qaf->adev->platform);

        qaf_params = str_parms_create();

        str_parms_add_str(qaf_params,
                          AUDIO_QAF_PARAMETER_KEY_RENDER_FORMAT,
                          AUDIO_QAF_PARAMETER_VALUE_PCM);
        switch (channels) {
            case 8:
                DEBUG_MSG("Switching Qaf output to 7.1 channels");
                str_parms_add_str(qaf_params,
                                  AUDIO_QAF_PARAMETER_KEY_CHANNELS,
                                  AUDIO_QAF_PARAMETER_VALUE_8_CHANNELS);
                if (p_qaf->qaf_msmd_enabled) {
                    str_parms_add_str(qaf_params,
                                      AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                      AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI_AND_SPK);
                } else {
                    str_parms_add_str(qaf_params,
                                      AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                      AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI);
                }
                p_qaf->hdmi_sink_channels = channels;
                break;
            case 6:
                DEBUG_MSG("Switching Qaf output to 5.1 channels");
                str_parms_add_str(qaf_params,
                                  AUDIO_QAF_PARAMETER_KEY_CHANNELS,
                                  AUDIO_QAF_PARAMETER_VALUE_6_CHANNELS);
                if (p_qaf->qaf_msmd_enabled) {
                    str_parms_add_str(qaf_params,
                                      AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                      AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI_AND_SPK);
                } else {
                    str_parms_add_str(qaf_params,
                                      AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                      AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI);
                }
                p_qaf->hdmi_sink_channels = channels;
                break;
            default:
                DEBUG_MSG("Switching Qaf output to default channels");
                str_parms_add_str(qaf_params,
                                  AUDIO_QAF_PARAMETER_KEY_CHANNELS,
                                  AUDIO_QAF_PARAMETER_VALUE_DEFAULT_CHANNELS);
                if (p_qaf->qaf_msmd_enabled) {
                    str_parms_add_str(qaf_params,
                                      AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                      AUDIO_QAF_PARAMETER_VALUE_DEVICE_HDMI_AND_SPK);
                } else {
                    str_parms_add_str(qaf_params,
                                      AUDIO_QAF_PARAMETER_KEY_DEVICE,
                                      AUDIO_QAF_PARAMETER_VALUE_DEVICE_SPEAKER);
                }
                p_qaf->hdmi_sink_channels = 2;
                break;
        }

        format_params = str_parms_to_str(qaf_params);

        if (p_qaf->qaf_mod[MS12].session_handle && p_qaf->qaf_mod[MS12].qaf_audio_session_set_param) {
            p_qaf->qaf_mod[MS12].qaf_audio_session_set_param(p_qaf->qaf_mod[MS12].session_handle,
                                                             format_params);
        }
        if (p_qaf->qaf_mod[DTS_M8].session_handle
                && p_qaf->qaf_mod[DTS_M8].qaf_audio_session_set_param) {
            p_qaf->qaf_mod[DTS_M8].qaf_audio_session_set_param(p_qaf->qaf_mod[DTS_M8].session_handle,
                                                               format_params);
        }

        str_parms_destroy(qaf_params);
    }
    DEBUG_MSG("Exit");
}

/* QAF set parameter function. For Device connect and disconnect. */
int audio_extn_qaf_set_parameters(struct audio_device *adev, struct str_parms *parms)
{
    int status = 0, val = 0, k;
    char *format_params, *kv_parirs;
    struct str_parms *qaf_params;

    DEBUG_MSG("Entry");

    if (!p_qaf) {
        return -EINVAL;
    }

    status = str_parms_get_int(parms, AUDIO_PARAMETER_DEVICE_CONNECT, &val);

    if ((status >= 0) && audio_is_output_device(val)) {
        if (val & AUDIO_DEVICE_OUT_AUX_DIGITAL) { //HDMI is connected.

            p_qaf->hdmi_connect = 1;
            p_qaf->hdmi_sink_channels = 0;

            if (p_qaf->passthrough_in) { //If QAF passthrough is already initialized.
                lock_output_stream(p_qaf->passthrough_in);
                if (platform_is_edid_supported_format(adev->platform,
                                                      p_qaf->passthrough_in->format)) {
                    //If passthrough format is supported by HDMI then create the QAF passthrough output if not created already.
                    create_qaf_passthrough_stream();
                    //Ignoring the returned error, If error then QAF passthrough is disabled.
                } else {
                    //If passthrough format is not supported by HDMI then close the QAF passthrough output if already created.
                    close_qaf_passthrough_stream();
                }
                unlock_output_stream(p_qaf->passthrough_in);
            }

            set_hdmi_configuration_to_module();

        } else if (val & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP) {
            p_qaf->bt_connect = 1;
            set_bt_configuration_to_module();
#ifndef A2DP_OFFLOAD_ENABLED
            for (k = 0; k < MAX_MM_MODULE_TYPE; k++) {
                if (!p_qaf->qaf_mod[k].bt_hdl) {
                    DEBUG_MSG("Opening a2dp output...");
                    status = audio_extn_bt_hal_load(&p_qaf->qaf_mod[k].bt_hdl);
                    if (status != 0) {
                        ERROR_MSG("Error opening BT module");
                        return status;
                    }
                }
            }
#endif
        }
        //TODO else if: Need to consider other devices.
    }

    status = str_parms_get_int(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, &val);
    if ((status >= 0) && audio_is_output_device(val)) {
        if (val & AUDIO_DEVICE_OUT_AUX_DIGITAL) { //HDMI is disconnected.

            qaf_params = str_parms_create();
            str_parms_add_str(qaf_params,
                              AUDIO_QAF_PARAMETER_KEY_DEVICE,
                              AUDIO_QAF_PARAMETER_VALUE_DEVICE_SPEAKER);
            str_parms_add_str(qaf_params,
                              AUDIO_QAF_PARAMETER_KEY_RENDER_FORMAT,
                              AUDIO_QAF_PARAMETER_VALUE_PCM);
            p_qaf->hdmi_sink_channels = 0;

            p_qaf->passthrough_enabled = 0;
            p_qaf->mch_pcm_hdmi_enabled = 0;
            p_qaf->hdmi_connect = 0;

            format_params = str_parms_to_str(qaf_params);

            for (k = 0; k < MAX_MM_MODULE_TYPE; k++) {
                if (p_qaf->qaf_mod[k].session_handle
                    && p_qaf->qaf_mod[k].qaf_audio_session_set_param) {
                    p_qaf->qaf_mod[k].qaf_audio_session_set_param(
                            p_qaf->qaf_mod[k].session_handle, format_params);
                }
            }
            close_all_hdmi_output();

            str_parms_destroy(qaf_params);
            close_qaf_passthrough_stream();
        } else if (val & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP) {
        p_qaf->bt_connect = 0;
        //reconfig HDMI as end device (if connected)
        if(p_qaf->hdmi_connect)
            set_hdmi_configuration_to_module();
#ifndef A2DP_OFFLOAD_ENABLED
            DEBUG_MSG("Closing a2dp output...");
            for (k = 0; k < MAX_MM_MODULE_TYPE; k++) {
                if (p_qaf->qaf_mod[k].bt_hdl) {
                    audio_extn_bt_hal_unload(p_qaf->qaf_mod[k].bt_hdl);
                    p_qaf->qaf_mod[k].bt_hdl = NULL;
                }
            }
#endif
        }
        //TODO else if: Need to consider other devices.
    }

    for (k = 0; k < MAX_MM_MODULE_TYPE; k++) {
        kv_parirs = str_parms_to_str(parms);
        if (p_qaf->qaf_mod[k].session_handle && p_qaf->qaf_mod[k].qaf_audio_session_set_param) {
            p_qaf->qaf_mod[k].qaf_audio_session_set_param(
                    p_qaf->qaf_mod[k].session_handle, kv_parirs);
        }
    }

    DEBUG_MSG("Exit");
    return status;
}

/* Create the QAF. */
int audio_extn_qaf_init(struct audio_device *adev)
{
    DEBUG_MSG("Entry");

    p_qaf = calloc(1, sizeof(struct qaf));
    if (p_qaf == NULL) {
        ERROR_MSG("Out of memory");
        return -ENOMEM;
    }

    p_qaf->adev = adev;

    if (property_get_bool("vendor.audio.qaf.msmd", false)) {
        p_qaf->qaf_msmd_enabled = 1;
    }
    pthread_mutex_init(&p_qaf->lock, (const pthread_mutexattr_t *) NULL);

    int i = 0;

    for (i = 0; i < MAX_MM_MODULE_TYPE; i++) {
        char value[PROPERTY_VALUE_MAX] = {0};
        char lib_name[PROPERTY_VALUE_MAX] = {0};
        struct qaf_module *qaf_mod = &(p_qaf->qaf_mod[i]);

        if (i == MS12) {
            property_get("vendor.audio.qaf.library", value, NULL);
            snprintf(lib_name, PROPERTY_VALUE_MAX, "%s", value);
#ifdef AUDIO_EXTN_IP_HDLR_ENABLED
{
        int ret = 0;
        ret = audio_extn_ip_hdlr_intf_init(&qaf_mod->ip_hdlr_hdl, lib_name, &qaf_mod->qaf_lib,
                                           adev, USECASE_AUDIO_PLAYBACK_OFFLOAD);
        if (ret < 0) {
            ERROR_MSG("audio_extn_ip_hdlr_intf_init failed, ret = %d", ret);
            continue;
        }
        if (qaf_mod->qaf_lib == NULL) {
            ERROR_MSG("failed to get library handle");
            continue;
        }
}
#else
       qaf_mod->qaf_lib = dlopen(lib_name, RTLD_NOW);
        if (qaf_mod->qaf_lib == NULL) {
            ERROR_MSG("DLOPEN failed for %s", lib_name);
            continue;
        }
        DEBUG_MSG("DLOPEN successful for %s", lib_name);
#endif
        } else if (i == DTS_M8) {
            property_get("vendor.audio.qaf.m8.library", value, NULL);
            snprintf(lib_name, PROPERTY_VALUE_MAX, "%s", value);
            qaf_mod->qaf_lib = dlopen(lib_name, RTLD_NOW);
            if (qaf_mod->qaf_lib == NULL) {
                ERROR_MSG("DLOPEN failed for %s", lib_name);
                continue;
            }
            DEBUG_MSG("DLOPEN successful for %s", lib_name);
        } else {
            continue;
        }

        qaf_mod->qaf_audio_session_open =
                    (int (*)(audio_session_handle_t* session_handle, audio_session_type_t s_type,
                                  void *p_data, void* license_data))dlsym(qaf_mod->qaf_lib,
                                                                     "audio_session_open");
        qaf_mod->qaf_audio_session_close =
                    (int (*)(audio_session_handle_t session_handle))dlsym(qaf_mod->qaf_lib,
                                                                     "audio_session_close");
        qaf_mod->qaf_audio_stream_open =
                    (int (*)(audio_session_handle_t session_handle, audio_stream_handle_t* stream_handle,
                     audio_stream_config_t input_config, audio_devices_t devices, stream_type_t flags))dlsym(qaf_mod->qaf_lib,
                                                                     "audio_stream_open");
        qaf_mod->qaf_audio_stream_close =
                    (int (*)(audio_stream_handle_t stream_handle))dlsym(qaf_mod->qaf_lib,
                                                                     "audio_stream_close");
        qaf_mod->qaf_audio_stream_set_param =
                    (int (*)(audio_stream_handle_t stream_handle, const char* kv_pairs))dlsym(qaf_mod->qaf_lib,
                                                                     "audio_stream_set_param");
        qaf_mod->qaf_audio_session_set_param =
                    (int (*)(audio_session_handle_t handle, const char* kv_pairs))dlsym(qaf_mod->qaf_lib,
                                                                     "audio_session_set_param");
        qaf_mod->qaf_audio_stream_get_param =
                    (char* (*)(audio_stream_handle_t stream_handle, const char* key))dlsym(qaf_mod->qaf_lib,
                                                                     "audio_stream_get_param");
        qaf_mod->qaf_audio_session_get_param =
                    (char* (*)(audio_session_handle_t handle, const char* key))dlsym(qaf_mod->qaf_lib,
                                                                     "audio_session_get_param");
        qaf_mod->qaf_audio_stream_start =
                    (int (*)(audio_stream_handle_t stream_handle))dlsym(qaf_mod->qaf_lib,
                                                                     "audio_stream_start");
        qaf_mod->qaf_audio_stream_stop =
                    (int (*)(audio_stream_handle_t stream_handle))dlsym(qaf_mod->qaf_lib,
                                                                     "audio_stream_stop");
        qaf_mod->qaf_audio_stream_pause =
                    (int (*)(audio_stream_handle_t stream_handle))dlsym(qaf_mod->qaf_lib,
                                                                     "audio_stream_pause");
        qaf_mod->qaf_audio_stream_flush =
                    (int (*)(audio_stream_handle_t stream_handle))dlsym(qaf_mod->qaf_lib,
                                                                     "audio_stream_flush");
        qaf_mod->qaf_audio_stream_write =
                    (int (*)(audio_stream_handle_t stream_handle, const void* buf, int size))dlsym(qaf_mod->qaf_lib,
                                                                     "audio_stream_write");
        qaf_mod->qaf_register_event_callback =
                    (void (*)(audio_session_handle_t session_handle, void *priv_data, notify_event_callback_t event_callback,
                     audio_event_id_t event_id))dlsym(qaf_mod->qaf_lib,
                                                                     "register_event_callback");
    }

    DEBUG_MSG("Exit");
    return 0;
}

/* Tear down the qaf extension. */
void audio_extn_qaf_deinit()
{
    int i;
    DEBUG_MSG("Entry");

    if (p_qaf != NULL) {
        for (i = 0; i < MAX_MM_MODULE_TYPE; i++) {
            qaf_session_close(&p_qaf->qaf_mod[i]);

            if (p_qaf->qaf_mod[i].qaf_lib != NULL) {
                if (i == MS12) {
#ifdef AUDIO_EXTN_IP_HDLR_ENABLED
                    audio_extn_ip_hdlr_intf_deinit(p_qaf->qaf_mod[i].ip_hdlr_hdl);
#else
                    dlclose(p_qaf->qaf_mod[i].qaf_lib);
#endif
                    p_qaf->qaf_mod[i].qaf_lib = NULL;
                } else {
                    dlclose(p_qaf->qaf_mod[i].qaf_lib);
                    p_qaf->qaf_mod[i].qaf_lib = NULL;
                }
            }
        }
        if (p_qaf->passthrough_out) {
            adev_close_output_stream((struct audio_hw_device *)p_qaf->adev,
                                     (struct audio_stream_out *)(p_qaf->passthrough_out));
            p_qaf->passthrough_out = NULL;
        }

        pthread_mutex_destroy(&p_qaf->lock);
        free(p_qaf);
        p_qaf = NULL;
    }
    DEBUG_MSG("Exit");
}
