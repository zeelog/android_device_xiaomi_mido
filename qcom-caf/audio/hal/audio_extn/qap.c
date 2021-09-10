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

#define LOG_TAG "audio_hw_qap"
#define LOG_NDEBUG 0
#define VERY_VERY_VERBOSE_LOGGING
#ifdef VERY_VERY_VERBOSE_LOGGING
#define DEBUG_MSG_VV DEBUG_MSG
#else
#define DEBUG_MSG_VV(a...) do { } while(0)
#endif

#define DEBUG_MSG(arg,...) ALOGE("%s: %d:  " arg, __func__, __LINE__, ##__VA_ARGS__)
#define ERROR_MSG(arg,...) ALOGE("%s: %d:  " arg, __func__, __LINE__, ##__VA_ARGS__)

#define COMPRESS_OFFLOAD_NUM_FRAGMENTS 2
#define COMPRESS_PASSTHROUGH_DDP_FRAGMENT_SIZE 4608

#define QAP_DEFAULT_COMPR_AUDIO_HANDLE 1001
#define QAP_DEFAULT_COMPR_PASSTHROUGH_HANDLE 1002
#define QAP_DEFAULT_PASSTHROUGH_HANDLE 1003

#define COMPRESS_OFFLOAD_PLAYBACK_LATENCY 300

#define MIN_PCM_OFFLOAD_FRAGMENT_SIZE 512
#define MAX_PCM_OFFLOAD_FRAGMENT_SIZE (240 * 1024)

#define DIV_ROUND_UP(x, y) (((x) + (y) - 1)/(y))
#define ALIGN(x, y) ((y) * DIV_ROUND_UP((x), (y)))

/* Pcm input node buffer size is 6144 bytes, i.e, 32msec for 48000 samplerate */
#define QAP_MODULE_PCM_INPUT_BUFFER_LATENCY 32

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
 * QAP Latency to process buffers since out_write from primary HAL
 */
#define QAP_COMPRESS_OFFLOAD_PROCESSING_LATENCY 18
#define QAP_PCM_OFFLOAD_PROCESSING_LATENCY 48

//TODO: Need to handle for DTS
#define QAP_DEEP_BUFFER_OUTPUT_PERIOD_SIZE 1536

#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <cutils/properties.h>
#include <cutils/str_parms.h>
#include <cutils/log.h>
#include <cutils/atomic.h>
#include "audio_utils/primitives.h"
#include "audio_hw.h"
#include "platform_api.h"
#include <platform.h>
#include <system/thread_defs.h>
#include <cutils/sched_policy.h>
#include "audio_extn.h"
#include <qti_audio.h>
#include <qap_api.h>
#include "sound/compress_params.h"
#include "ip_hdlr_intf.h"
#include "dolby_ms12.h"

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_QAF
#include <log_utils.h>
#endif

//TODO: Need to remove this.
#define QAP_OUTPUT_SAMPLING_RATE 48000

#ifdef QAP_DUMP_ENABLED
FILE *fp_output_writer_hdmi = NULL;
#endif

//Types of MM module, currently supported by QAP.
typedef enum {
    MS12,
    DTS_M8,
    MAX_MM_MODULE_TYPE,
    INVALID_MM_MODULE
} mm_module_type;

typedef enum {
    QAP_OUT_TRANSCODE_PASSTHROUGH = 0, /* Transcode passthrough via MM module*/
    QAP_OUT_OFFLOAD_MCH, /* Multi-channel PCM offload*/
    QAP_OUT_OFFLOAD, /* PCM offload */

    MAX_QAP_MODULE_OUT
} mm_module_output_type;

typedef enum {
    QAP_IN_MAIN = 0, /* Single PID Main/Primary or Dual-PID stream */
    QAP_IN_ASSOC,    /* Associated/Secondary stream */
    QAP_IN_PCM,      /* PCM stream. */
    QAP_IN_MAIN_2,   /* Single PID Main2 stream */
    MAX_QAP_MODULE_IN
} mm_module_input_type;

typedef enum {
    STOPPED,    /*Stream is in stop state. */
    STOPPING,   /*Stream is stopping, waiting for EOS. */
    RUN,        /*Stream is in run state. */
    MAX_STATES
} qap_stream_state;

struct qap_module {
    audio_session_handle_t session_handle;
    void *qap_lib;
    void *qap_handle;

    /*Input stream of MM module */
    struct stream_out *stream_in[MAX_QAP_MODULE_IN];
    /*Output Stream from MM module */
    struct stream_out *stream_out[MAX_QAP_MODULE_OUT];

    /*Media format associated with each output id raised by mm module. */
    qap_session_outputs_config_t session_outputs_config;
    /*Flag is set if media format is changed for an mm module output. */
    bool is_media_fmt_changed[MAX_QAP_MODULE_OUT];
    /*Index to be updated in session_outputs_config array for a new mm module output. */
    int new_out_format_index;

    //BT session handle.
    void *bt_hdl;

    float vol_left;
    float vol_right;
    bool is_vol_set;
    qap_stream_state stream_state[MAX_QAP_MODULE_IN];
    bool is_session_closing;
    bool is_session_output_active;
    pthread_cond_t session_output_cond;
    pthread_mutex_t session_output_lock;

};

struct qap {
    struct audio_device *adev;

    pthread_mutex_t lock;

    bool bt_connect;
    bool hdmi_connect;
    int hdmi_sink_channels;

    //Flag to indicate if QAP transcode output stream is enabled from any mm module.
    bool passthrough_enabled;
    //Flag to indicate if QAP mch pcm output stream is enabled from any mm module.
    bool mch_pcm_hdmi_enabled;

    //Flag to indicate if msmd is supported.
    bool qap_msmd_enabled;

    bool qap_output_block_handling;
    //Handle of QAP input stream, which is routed as QAP passthrough.
    struct stream_out *passthrough_in;
    //Handle of QAP passthrough stream.
    struct stream_out *passthrough_out;

    struct qap_module qap_mod[MAX_MM_MODULE_TYPE];
};

//Global handle of QAP. Access to this should be protected by mutex lock.
static struct qap *p_qap = NULL;

/* Gets the pointer to qap module for the qap input stream. */
static struct qap_module* get_qap_module_for_input_stream_l(struct stream_out *out)
{
    struct qap_module *qap_mod = NULL;
    int i, j;
    if (!p_qap) return NULL;

    for (i = 0; i < MAX_MM_MODULE_TYPE; i++) {
        for (j = 0; j < MAX_QAP_MODULE_IN; j++) {
            if (p_qap->qap_mod[i].stream_in[j] == out) {
                qap_mod = &(p_qap->qap_mod[i]);
                break;
            }
        }
    }

    return qap_mod;
}

/* Finds the mm module input stream index for the QAP input stream. */
static int get_input_stream_index_l(struct stream_out *out)
{
    int index = -1, j;
    struct qap_module* qap_mod = NULL;

    qap_mod = get_qap_module_for_input_stream_l(out);
    if (!qap_mod) return index;

    for (j = 0; j < MAX_QAP_MODULE_IN; j++) {
        if (qap_mod->stream_in[j] == out) {
            index = j;
            break;
        }
    }

    return index;
}

static void set_stream_state_l(struct stream_out *out, int state)
{
    struct qap_module *qap_mod = get_qap_module_for_input_stream_l(out);
    int index = get_input_stream_index_l(out);
    if (qap_mod && index >= 0) qap_mod->stream_state[index] = state;
}

static bool check_stream_state_l(struct stream_out *out, int state)
{
    struct qap_module *qap_mod = get_qap_module_for_input_stream_l(out);
    int index = get_input_stream_index_l(out);
    if (qap_mod && index >= 0) return ((int)qap_mod->stream_state[index] == state);
    return false;
}

/* Finds the right mm module for the QAP input stream format. */
static mm_module_type get_mm_module_for_format_l(audio_format_t format)
{
    int j;

    DEBUG_MSG("Format 0x%x", format);

    if (format == AUDIO_FORMAT_PCM_16_BIT) {
        //If dts is not supported then alway support pcm with MS12
        if (!property_get_bool("vendor.audio.qap.dts_m8", false)) { //TODO: Need to add this property for DTS.
            return MS12;
        }

        //If QAP passthrough is active then send the PCM stream to primary HAL.
        if (!p_qap->passthrough_out) {
            /* Iff any stream is active in MS12 module then route PCM stream to it. */
            for (j = 0; j < MAX_QAP_MODULE_IN; j++) {
                if (p_qap->qap_mod[MS12].stream_in[j]) {
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

static bool is_main_active_l(struct qap_module* qap_mod)
{
   return (qap_mod->stream_in[QAP_IN_MAIN] || qap_mod->stream_in[QAP_IN_MAIN_2]);
}

static bool is_dual_main_active_l(struct qap_module* qap_mod)
{
   return (qap_mod->stream_in[QAP_IN_MAIN] && qap_mod->stream_in[QAP_IN_MAIN_2]);
}

//Checks if any main or pcm stream is running in the session.
static bool is_any_stream_running_l(struct qap_module* qap_mod)
{
    //Not checking associated stream.
    struct stream_out *out = qap_mod->stream_in[QAP_IN_MAIN];
    struct stream_out *out_pcm = qap_mod->stream_in[QAP_IN_PCM];
    struct stream_out *out_main2 = qap_mod->stream_in[QAP_IN_MAIN_2];

    if ((out == NULL || (out != NULL && check_stream_state_l(out, STOPPED)))
        && (out_main2 == NULL || (out_main2 != NULL && check_stream_state_l(out_main2, STOPPED)))
        && (out_pcm == NULL || (out_pcm != NULL && check_stream_state_l(out_pcm, STOPPED)))) {
        return false;
    }
    return true;
}

/* Gets the pcm output buffer size(in samples) for the mm module. */
static uint32_t get_pcm_output_buffer_size_samples_l(struct qap_module *qap_mod)
{
    uint32_t pcm_output_buffer_size = 0;

    if (qap_mod == &p_qap->qap_mod[MS12]) {
        pcm_output_buffer_size = MS12_PCM_OUT_FRAGMENT_SIZE;
    } else if (qap_mod == &p_qap->qap_mod[DTS_M8]) {
        pcm_output_buffer_size = DTS_PCM_OUT_FRAGMENT_SIZE;
    }

    return pcm_output_buffer_size;
}

static int get_media_fmt_array_index_for_output_id_l(
        struct qap_module* qap_mod,
        uint32_t output_id)
{
    int i;
    for (i = 0; i < MAX_SUPPORTED_OUTPUTS; i++) {
        if (qap_mod->session_outputs_config.output_config[i].id == output_id) {
            return i;
        }
    }
    return -1;
}

/* Acquire Mutex lock on output stream */
static void lock_output_stream_l(struct stream_out *out)
{
    pthread_mutex_lock(&out->pre_lock);
    pthread_mutex_lock(&out->lock);
    pthread_mutex_unlock(&out->pre_lock);
}

/* Release Mutex lock on output stream */
static void unlock_output_stream_l(struct stream_out *out)
{
    pthread_mutex_unlock(&out->lock);
}

/* Checks if stream can be routed as QAP passthrough or not. */
static bool audio_extn_qap_passthrough_enabled(struct stream_out *out)
{
    DEBUG_MSG("Format 0x%x", out->format);
    bool is_enabled = false;

    if (!p_qap) return false;

    if ((!property_get_bool("vendor.audio.qap.reencode", false))
        && property_get_bool("vendor.audio.qap.passthrough", false)) {

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

/*Closes all pcm hdmi output from QAP. */
static void close_all_pcm_hdmi_output_l()
{
    int i;
    //Closing all the PCM HDMI output stream from QAP.
    for (i = 0; i < MAX_MM_MODULE_TYPE; i++) {
        if (p_qap->qap_mod[i].stream_out[QAP_OUT_OFFLOAD_MCH]) {
            adev_close_output_stream((struct audio_hw_device *)p_qap->adev,
                                     (struct audio_stream_out *)(p_qap->qap_mod[i].stream_out[QAP_OUT_OFFLOAD_MCH]));
            p_qap->qap_mod[i].stream_out[QAP_OUT_OFFLOAD_MCH] = NULL;
        }

        if ((p_qap->qap_mod[i].stream_out[QAP_OUT_OFFLOAD])
            && compare_device_type(
                    &p_qap->qap_mod[i].stream_out[QAP_OUT_OFFLOAD]->device_list,
                    AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
            adev_close_output_stream((struct audio_hw_device *)p_qap->adev,
                                     (struct audio_stream_out *)(p_qap->qap_mod[i].stream_out[QAP_OUT_OFFLOAD]));
            p_qap->qap_mod[i].stream_out[QAP_OUT_OFFLOAD] = NULL;
        }
    }

    p_qap->mch_pcm_hdmi_enabled = 0;
}

static void close_all_hdmi_output_l()
{
    int k;
    for (k = 0; k < MAX_MM_MODULE_TYPE; k++) {
        if (p_qap->qap_mod[k].stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH]) {
            adev_close_output_stream((struct audio_hw_device *)p_qap->adev,
                                     (struct audio_stream_out *)(p_qap->qap_mod[k].stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH]));
            p_qap->qap_mod[k].stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH] = NULL;
        }
    }
    p_qap->passthrough_enabled = 0;

    close_all_pcm_hdmi_output_l();
}

static int qap_out_callback(stream_callback_event_t event, void *param __unused, void *cookie)
{
    struct stream_out *out = (struct stream_out *)cookie;

    out->client_callback(event, NULL, out->client_cookie);
    return 0;
}

/* Creates the QAP passthrough output stream. */
static int create_qap_passthrough_stream_l()
{
    DEBUG_MSG("Entry");

    int ret = 0;
    struct stream_out *out = p_qap->passthrough_in;

    if (!out) return -EINVAL;

    pthread_mutex_lock(&p_qap->lock);
    lock_output_stream_l(out);

    //Creating QAP passthrough output stream.
    if (NULL == p_qap->passthrough_out) {
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

        //Device is copied from the QAP passthrough input stream.
        devices = get_device_types(&out->device_list);
        flags = out->flags;

        ret = adev_open_output_stream((struct audio_hw_device *)p_qap->adev,
                                      QAP_DEFAULT_PASSTHROUGH_HANDLE,
                                      devices,
                                      flags,
                                      &config,
                                      (struct audio_stream_out **)&(p_qap->passthrough_out),
                                      NULL);
        if (ret < 0) {
            ERROR_MSG("adev_open_output_stream failed with ret = %d!", ret);
            unlock_output_stream_l(out);
            return ret;
        }
        p_qap->passthrough_in = out;
        p_qap->passthrough_out->stream.set_callback((struct audio_stream_out *)p_qap->passthrough_out,
                                                    (stream_callback_t) qap_out_callback, out);
    }

    unlock_output_stream_l(out);

    //Since QAP-Passthrough is created, close other HDMI outputs.
    close_all_hdmi_output_l();

    pthread_mutex_unlock(&p_qap->lock);
    return ret;
}


/* Stops a QAP module stream.*/
static int audio_extn_qap_stream_stop(struct stream_out *out)
{
    int ret = 0;
    DEBUG_MSG("Output Stream 0x%x", (int)out);

    if (!check_stream_state_l(out, RUN)) return ret;

    struct qap_module *qap_mod = get_qap_module_for_input_stream_l(out);

    if (!qap_mod || !qap_mod->session_handle|| !out->qap_stream_handle) {
        ERROR_MSG("Wrong state to process qap_mod(%p) sess_hadl(%p) strm hndl(%p)",
                                qap_mod, qap_mod->session_handle, out->qap_stream_handle);
        return -EINVAL;
    }

    ret = qap_module_cmd(out->qap_stream_handle,
                            QAP_MODULE_CMD_STOP,
                            sizeof(QAP_MODULE_CMD_STOP),
                            NULL,
                            NULL,
                            NULL);
    if (QAP_STATUS_OK != ret) {
        ERROR_MSG("stop failed %d", ret);
        return -EINVAL;
    }

    return ret;
}

static int qap_out_drain(struct audio_stream_out* stream, audio_drain_type_t type)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = 0;
    struct qap_module *qap_mod = NULL;

    qap_mod = get_qap_module_for_input_stream_l(out);
    DEBUG_MSG("Output Stream %p", out);

    lock_output_stream_l(out);

    //If QAP passthrough is enabled then block the drain on module stream.
    if (p_qap->passthrough_out) {
        pthread_mutex_lock(&p_qap->lock);
        //If drain is received for QAP passthorugh stream then call the primary HAL api.
        if (p_qap->passthrough_in == out) {
            status = p_qap->passthrough_out->stream.drain(
                    (struct audio_stream_out *)p_qap->passthrough_out, type);
        }
        pthread_mutex_unlock(&p_qap->lock);
    } else if (!is_any_stream_running_l(qap_mod)) {
        //If stream is already stopped then send the drain ready.
        out->client_callback(STREAM_CBK_EVENT_DRAIN_READY, NULL, out->client_cookie);
        set_stream_state_l(out, STOPPED);
    } else {
        qap_audio_buffer_t *buffer;
        buffer = (qap_audio_buffer_t *) calloc(1, sizeof(qap_audio_buffer_t));
        buffer->common_params.offset = 0;
        buffer->common_params.data = buffer;
        buffer->common_params.size = 0;
        buffer->buffer_parms.input_buf_params.flags = QAP_BUFFER_EOS;
        DEBUG_MSG("Queing EOS buffer %p flags %d size %d", buffer, buffer->buffer_parms.input_buf_params.flags, buffer->common_params.size);
        status = qap_module_process(out->qap_stream_handle, buffer);
        if (QAP_STATUS_OK != status) {
            ERROR_MSG("EOS buffer queing failed%d", status);
            return -EINVAL;
        }

        //Drain the module input stream.
        /* Stream stop will trigger EOS and on EOS_EVENT received
         from callback DRAIN_READY command is sent */
        status = audio_extn_qap_stream_stop(out);

        if (status == 0) {
            //Setting state to stopping as client is expecting drain_ready event.
            set_stream_state_l(out, STOPPING);
        }
    }

    unlock_output_stream_l(out);
    return status;
}


/* Flush the QAP module input stream. */
static int audio_extn_qap_stream_flush(struct stream_out *out)
{
    DEBUG_MSG("Output Stream %p", out);
    int ret = -EINVAL;
    struct qap_module *qap_mod = NULL;

    qap_mod = get_qap_module_for_input_stream_l(out);
    if (!qap_mod || !qap_mod->session_handle|| !out->qap_stream_handle) {
        ERROR_MSG("Wrong state to process qap_mod(%p) sess_hadl(%p) strm hndl(%p)",
                                qap_mod, qap_mod->session_handle, out->qap_stream_handle);
        return -EINVAL;
    }

    ret = qap_module_cmd(out->qap_stream_handle,
                            QAP_MODULE_CMD_FLUSH,
                            sizeof(QAP_MODULE_CMD_FLUSH),
                            NULL,
                            NULL,
                            NULL);
    if (QAP_STATUS_OK != ret) {
        ERROR_MSG("flush failed %d", ret);
        return -EINVAL;
    }

    return ret;
}


/* Pause the QAP module input stream. */
static int qap_stream_pause_l(struct stream_out *out)
{
    struct qap_module *qap_mod = NULL;
    int ret = -EINVAL;

    qap_mod = get_qap_module_for_input_stream_l(out);
    if (!qap_mod || !qap_mod->session_handle|| !out->qap_stream_handle) {
        ERROR_MSG("Wrong state to process qap_mod(%p) sess_hadl(%p) strm hndl(%p)",
            qap_mod, qap_mod->session_handle, out->qap_stream_handle);
        return -EINVAL;
    }

    ret = qap_module_cmd(out->qap_stream_handle,
                            QAP_MODULE_CMD_PAUSE,
                            sizeof(QAP_MODULE_CMD_PAUSE),
                            NULL,
                            NULL,
                            NULL);
    if (QAP_STATUS_OK != ret) {
        ERROR_MSG("pause failed %d", ret);
        return -EINVAL;
    }

    return ret;
}


/* Flush the QAP input stream. */
static int qap_out_flush(struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = 0;

    DEBUG_MSG("Output Stream %p", out);
    lock_output_stream_l(out);

    if (!out->standby) {
        //If QAP passthrough is active then block the flush on module input streams.
        if (p_qap->passthrough_out) {
            pthread_mutex_lock(&p_qap->lock);
            //If flush is received for the QAP passthrough stream then call the primary HAL api.
            if (p_qap->passthrough_in == out) {
                status = p_qap->passthrough_out->stream.flush(
                        (struct audio_stream_out *)p_qap->passthrough_out);
                out->offload_state = OFFLOAD_STATE_IDLE;
            }
            pthread_mutex_unlock(&p_qap->lock);
        } else {
            //Flush the module input stream.
            status = audio_extn_qap_stream_flush(out);
        }
    }
    unlock_output_stream_l(out);
    DEBUG_MSG("Exit");
    return status;
}


/* Pause a QAP input stream. */
static int qap_out_pause(struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = 0;
    DEBUG_MSG("Output Stream %p", out);

    lock_output_stream_l(out);

    //If QAP passthrough is enabled then block the pause on module stream.
    if (p_qap->passthrough_out) {
        pthread_mutex_lock(&p_qap->lock);
        //If pause is received for QAP passthorugh stream then call the primary HAL api.
        if (p_qap->passthrough_in == out) {
            status = p_qap->passthrough_out->stream.pause(
                    (struct audio_stream_out *)p_qap->passthrough_out);
            out->offload_state = OFFLOAD_STATE_PAUSED;
        }
        pthread_mutex_unlock(&p_qap->lock);
    } else {
        //Pause the module input stream.
        status = qap_stream_pause_l(out);
    }

    unlock_output_stream_l(out);
    return status;
}

static void close_qap_passthrough_stream_l()
{
    if (p_qap->passthrough_out != NULL) { //QAP pasthroug is enabled. Close it.
        pthread_mutex_lock(&p_qap->lock);
        adev_close_output_stream((struct audio_hw_device *)p_qap->adev,
                                 (struct audio_stream_out *)(p_qap->passthrough_out));
        p_qap->passthrough_out = NULL;
        pthread_mutex_unlock(&p_qap->lock);

        if (p_qap->passthrough_in->qap_stream_handle) {
            qap_out_pause((struct audio_stream_out*)p_qap->passthrough_in);
            qap_out_flush((struct audio_stream_out*)p_qap->passthrough_in);
            qap_out_drain((struct audio_stream_out*)p_qap->passthrough_in,
                          (audio_drain_type_t)STREAM_CBK_EVENT_DRAIN_READY);
        }
    }
}

static int qap_out_standby(struct audio_stream *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct qap_module *qap_mod = NULL;
    int status = 0;
    int i;

    ALOGD("%s: enter: stream (%p) usecase(%d: %s)", __func__,
          stream, out->usecase, use_case_table[out->usecase]);

    lock_output_stream_l(out);

    //If QAP passthrough is active then block standby on all the input streams of QAP mm modules.
    if (p_qap->passthrough_out) {
        //If standby is received on QAP passthrough stream then forward it to primary HAL.
        if (p_qap->passthrough_in == out) {
            status = p_qap->passthrough_out->stream.common.standby(
                    (struct audio_stream *)p_qap->passthrough_out);
        }
    } else if (check_stream_state_l(out, RUN)) {
        //If QAP passthrough stream is not active then stop the QAP module stream.
        status = audio_extn_qap_stream_stop(out);

        if (status == 0) {
            //Setting state to stopped as client not expecting drain_ready event.
            set_stream_state_l(out, STOPPED);
        }
        if(p_qap->qap_output_block_handling) {
            qap_mod = get_qap_module_for_input_stream_l(out);
            for (i = 0; i < MAX_QAP_MODULE_IN; i++) {
                if (qap_mod->stream_in[i] != NULL &&
                    check_stream_state_l(qap_mod->stream_in[i], RUN)) {
                    break;
                }
            }

            if (i != MAX_QAP_MODULE_IN) {
                DEBUG_MSG("[%s] stream is still active.", use_case_table[qap_mod->stream_in[i]->usecase]);
            } else {
                pthread_mutex_lock(&qap_mod->session_output_lock);
                qap_mod->is_session_output_active = false;
                pthread_mutex_unlock(&qap_mod->session_output_lock);
                DEBUG_MSG(" all the input streams are either closed or stopped(standby) block the MM module output");
            }
        }
    }

    if (!out->standby) {
        out->standby = true;
    }

    unlock_output_stream_l(out);
    return status;
}

/* Sets the volume to PCM output stream. */
static int qap_out_set_volume(struct audio_stream_out *stream, float left, float right)
{
    int ret = 0;
    struct stream_out *out = (struct stream_out *)stream;
    struct qap_module *qap_mod = NULL;

    DEBUG_MSG("Left %f, Right %f", left, right);

    qap_mod = get_qap_module_for_input_stream_l(out);
    if (!qap_mod) {
        return -EINVAL;
    }

    pthread_mutex_lock(&p_qap->lock);
    qap_mod->vol_left = left;
    qap_mod->vol_right = right;
    qap_mod->is_vol_set = true;
    pthread_mutex_unlock(&p_qap->lock);

    if (qap_mod->stream_out[QAP_OUT_OFFLOAD] != NULL) {
        ret = qap_mod->stream_out[QAP_OUT_OFFLOAD]->stream.set_volume(
                (struct audio_stream_out *)qap_mod->stream_out[QAP_OUT_OFFLOAD], left, right);
    }

    return ret;
}

/* Starts a QAP module stream. */
static int qap_stream_start_l(struct stream_out *out)
{
    int ret = 0;
    struct qap_module *qap_mod = NULL;

    DEBUG_MSG("Output Stream = %p", out);

    qap_mod = get_qap_module_for_input_stream_l(out);
    if ((!qap_mod) || (!qap_mod->session_handle)) {
        ERROR_MSG("QAP mod is not inited (%p) or session is not yet opened (%p) ",
            qap_mod, qap_mod->session_handle);
        return -EINVAL;
    }
    if (out->qap_stream_handle) {
        ret = qap_module_cmd(out->qap_stream_handle,
                             QAP_MODULE_CMD_START,
                             sizeof(QAP_MODULE_CMD_START),
                             NULL,
                             NULL,
                             NULL);
        if (ret != QAP_STATUS_OK) {
            ERROR_MSG("start failed");
            ret = -EINVAL;
        }
    } else
        ERROR_MSG("QAP stream not yet opened, drop this cmd");

    DEBUG_MSG("exit");
    return ret;

}

static int qap_start_output_stream(struct stream_out *out)
{
    int ret = 0;
    struct audio_device *adev = out->dev;

    if ((out->usecase < 0) || (out->usecase >= AUDIO_USECASE_MAX)) {
        ret = -EINVAL;
        DEBUG_MSG("Use case out of bounds sleeping for 500ms");
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

    return qap_stream_start_l(out);
}

/* Sends input buffer to the QAP MM module. */
static int qap_module_write_input_buffer(struct stream_out *out, const void *buffer, int bytes)
{
    int ret = -EINVAL;
    struct qap_module *qap_mod = NULL;
    qap_audio_buffer_t buff;

    qap_mod = get_qap_module_for_input_stream_l(out);
    if ((!qap_mod) || (!qap_mod->session_handle) || (!out->qap_stream_handle)) {
        return ret;
    }

    //If data received on associated stream when all other stream are stopped then drop the data.
    if (out == qap_mod->stream_in[QAP_IN_ASSOC] && !is_any_stream_running_l(qap_mod))
        return bytes;

    memset(&buff, 0, sizeof(buff));
    buff.common_params.offset = 0;
    buff.common_params.size = bytes;
    buff.common_params.data = (void *) buffer;
    buff.common_params.timestamp = QAP_BUFFER_NO_TSTAMP;
    buff.buffer_parms.input_buf_params.flags = QAP_BUFFER_NO_TSTAMP;
    DEBUG_MSG("calling module process with bytes %d %p", bytes, buffer);
    ret  = qap_module_process(out->qap_stream_handle, &buff);

    if(ret > 0) set_stream_state_l(out, RUN);

    return ret;
}

static ssize_t qap_out_write(struct audio_stream_out *stream, const void *buffer, size_t bytes)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct audio_device *adev = out->dev;
    ssize_t ret = 0;
    struct qap_module *qap_mod = NULL;

    DEBUG_MSG_VV("bytes = %d, usecase[%s] and flags[%x] for handle[%p]",
          (int)bytes, use_case_table[out->usecase], out->flags, out);

    lock_output_stream_l(out);

    // If QAP passthrough is active then block writing data to QAP mm module.
    if (p_qap->passthrough_out) {
        //If write is received for the QAP passthrough stream then send the buffer to primary HAL.
        if (p_qap->passthrough_in == out) {
            ret = p_qap->passthrough_out->stream.write(
                    (struct audio_stream_out *)(p_qap->passthrough_out),
                    buffer,
                    bytes);
            if (ret > 0) out->standby = false;
        }
        unlock_output_stream_l(out);
        return ret;
    } else if (out->standby) {
        pthread_mutex_lock(&adev->lock);
        ret = qap_start_output_stream(out);
        pthread_mutex_unlock(&adev->lock);
        if (ret == 0) {
            out->standby = false;
            if(p_qap->qap_output_block_handling) {
                qap_mod = get_qap_module_for_input_stream_l(out);

                pthread_mutex_lock(&qap_mod->session_output_lock);
                if (qap_mod->is_session_output_active == false) {
                    qap_mod->is_session_output_active = true;
                    pthread_cond_signal(&qap_mod->session_output_cond);
                    DEBUG_MSG("Wake up MM module output thread");
                }
                pthread_mutex_unlock(&qap_mod->session_output_lock);
            }
        } else {
            goto exit;
        }
    }

    if ((adev->is_channel_status_set == false) &&
         compare_device_type(&out->device_list, AUDIO_DEVICE_OUT_AUX_DIGITAL)) {
        audio_utils_set_hdmi_channel_status(out, (char *)buffer, bytes);
        adev->is_channel_status_set = true;
    }

    ret = qap_module_write_input_buffer(out, buffer, bytes);
    DEBUG_MSG_VV("Bytes consumed [%d] by MM Module", (int)ret);

    if (ret >= 0) {
        out->written += ret / ((popcount(out->channel_mask) * sizeof(short)));
    }


exit:
    unlock_output_stream_l(out);

    if (ret < 0) {
        if (ret == -EAGAIN) {
            DEBUG_MSG_VV("No space available to consume bytes, post msg to cb thread");
            bytes = 0;
        } else if (ret == -ENOMEM || ret == -EPERM) {
            if (out->pcm)
                ERROR_MSG("error %d, %s", (int)ret, pcm_get_error(out->pcm));
            qap_out_standby(&out->stream.common);
            DEBUG_MSG("SLEEP for 100sec");
            usleep(bytes * 1000000
                   / audio_stream_out_frame_size(stream)
                   / out->stream.common.get_sample_rate(&out->stream.common));
        }
    } else if (ret < (ssize_t)bytes) {
        //partial buffer copied to the module.
        DEBUG_MSG_VV("Not enough space available to consume all the bytes");
        bytes = ret;
    }
    return bytes;
}

/* Gets PCM offload buffer size for a given config. */
static uint32_t qap_get_pcm_offload_buffer_size(audio_offload_info_t* info,
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

    ALOGI("Qap PCM offload Fragment size is %d bytes", fragment_size);

    return fragment_size;
}

static uint32_t qap_get_pcm_offload_input_buffer_size(audio_offload_info_t* info)
{
    return qap_get_pcm_offload_buffer_size(info, MS12_PCM_IN_FRAGMENT_SIZE);
}

static uint32_t qap_get_pcm_offload_output_buffer_size(struct qap_module *qap_mod,
                                                audio_offload_info_t* info)
{
    return qap_get_pcm_offload_buffer_size(info, get_pcm_output_buffer_size_samples_l(qap_mod));
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
static int qap_get_rendered_frames(struct stream_out *out, uint64_t *frames)
{
    int ret = 0, i;
    struct str_parms *parms;
//    int value = 0;
    int module_latency = 0;
    uint32_t kernel_latency = 0;
    uint32_t dsp_latency = 0;
    int signed_frames = 0;
    char* kvpairs = NULL;
    struct qap_module *qap_mod = NULL;

    DEBUG_MSG("Output Format %d", out->format);

    qap_mod = get_qap_module_for_input_stream_l(out);
    if (!qap_mod || !qap_mod->session_handle|| !out->qap_stream_handle) {
        ERROR_MSG("Wrong state to process qap_mod(%p) sess_hadl(%p) strm hndl(%p)",
            qap_mod, qap_mod->session_handle, out->qap_stream_handle);
        return -EINVAL;
    }

    //Get MM module latency.
/* Tobeported
    kvpairs = qap_mod->qap_audio_stream_get_param(out->qap_stream_handle, "get_latency");
*/
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
    for (i = MAX_QAP_MODULE_OUT - 1; i >= 0; i--) {
        if (qap_mod->stream_out[i] == NULL) {
            continue;
        } else {
            unsigned int num_fragments = qap_mod->stream_out[i]->compr_config.fragments;
            uint32_t fragment_size = qap_mod->stream_out[i]->compr_config.fragment_size;
            uint32_t kernel_buffer_size = num_fragments * fragment_size;
            get_buffer_latency(qap_mod->stream_out[i], kernel_buffer_size, &kernel_latency);
            break;
        }
    }

    //Get DSP latency
    if ((qap_mod->stream_out[QAP_OUT_OFFLOAD] != NULL)
        || (qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH] != NULL)) {
        unsigned int sample_rate = 0;
        audio_usecase_t platform_latency = 0;

        if (qap_mod->stream_out[QAP_OUT_OFFLOAD])
            sample_rate = qap_mod->stream_out[QAP_OUT_OFFLOAD]->sample_rate;
        else if (qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH])
            sample_rate = qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]->sample_rate;

        if (qap_mod->stream_out[QAP_OUT_OFFLOAD])
            platform_latency =
                platform_render_latency(qap_mod->stream_out[QAP_OUT_OFFLOAD]->usecase);
        else
            platform_latency =
                platform_render_latency(qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]->usecase);

        dsp_latency = (platform_latency * sample_rate) / 1000000LL;
    } else if (qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH] != NULL) {
        unsigned int sample_rate = 0;

        sample_rate = qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH]->sample_rate; //TODO: How this sample rate can be used?
        dsp_latency = (COMPRESS_OFFLOAD_PLAYBACK_LATENCY * sample_rate) / 1000;
    }

    // MM Module Latency + Kernel Latency + DSP Latency
    if ( audio_extn_bt_hal_get_output_stream(qap_mod->bt_hdl) != NULL) {
        out->platform_latency = module_latency + audio_extn_bt_hal_get_latency(qap_mod->bt_hdl);
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
/* Tobeported
        }
        else {

        kvpairs = qap_mod->qap_audio_stream_get_param(out->qap_stream_handle, "position");
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
*/
    } else {
        ret = -EINVAL;
    }

    return ret;
}

static int qap_out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    struct stream_out *out = (struct stream_out *)stream;
    int ret = 0;
    uint64_t frames=0;
    struct qap_module* qap_mod = NULL;
    ALOGV("%s, Output Stream %p,dsp frames %d",__func__, stream, (int)dsp_frames);

    qap_mod = get_qap_module_for_input_stream_l(out);
    if (!qap_mod) {
        ret = out->stream.get_render_position(stream, dsp_frames);
        ALOGV("%s, non qap_MOD DSP FRAMES %d",__func__, (int)dsp_frames);
        return ret;
    }

    if (p_qap->passthrough_out) {
        pthread_mutex_lock(&p_qap->lock);
        ret = p_qap->passthrough_out->stream.get_render_position((struct audio_stream_out *)p_qap->passthrough_out, dsp_frames);
        pthread_mutex_unlock(&p_qap->lock);
        ALOGV("%s, PASS THROUGH DSP FRAMES %p",__func__, dsp_frames);
        return ret;
        }
    frames=*dsp_frames;
    ret = qap_get_rendered_frames(out, &frames);
    *dsp_frames = (uint32_t)frames;
    ALOGV("%s, DSP FRAMES %d",__func__, (int)dsp_frames);
    return ret;
}

static int qap_out_get_presentation_position(const struct audio_stream_out *stream,
                                             uint64_t *frames,
                                             struct timespec *timestamp)
{
    struct stream_out *out = (struct stream_out *)stream;
    int ret = 0;

//    DEBUG_MSG_VV("Output Stream %p", stream);

    //If QAP passthorugh output stream is active.
    if (p_qap->passthrough_out) {
        if (p_qap->passthrough_in == out) {
            //If api is called for QAP passthorugh stream then call the primary HAL api to get the position.
            pthread_mutex_lock(&p_qap->lock);
            ret = p_qap->passthrough_out->stream.get_presentation_position(
                    (struct audio_stream_out *)p_qap->passthrough_out,
                    frames,
                    timestamp);
            pthread_mutex_unlock(&p_qap->lock);
        } else {
            //If api is called for other stream then return zero frames.
            *frames = 0;
            clock_gettime(CLOCK_MONOTONIC, timestamp);
        }
        return ret;
    }

    ret = qap_get_rendered_frames(out, frames);
    clock_gettime(CLOCK_MONOTONIC, timestamp);

    return ret;
}

static uint32_t qap_out_get_latency(const struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    uint32_t latency = 0;
    struct qap_module *qap_mod = NULL;
    DEBUG_MSG_VV("Output Stream %p", out);

    qap_mod = get_qap_module_for_input_stream_l(out);
    if (!qap_mod) {
        return 0;
    }

    //If QAP passthrough is active then block the get latency on module input streams.
    if (p_qap->passthrough_out) {
        pthread_mutex_lock(&p_qap->lock);
        //If get latency is called for the QAP passthrough stream then call the primary HAL api.
        if (p_qap->passthrough_in == out) {
            latency = p_qap->passthrough_out->stream.get_latency(
                    (struct audio_stream_out *)p_qap->passthrough_out);
        }
        pthread_mutex_unlock(&p_qap->lock);
    } else {
        if (is_offload_usecase(out->usecase)) {
            latency = COMPRESS_OFFLOAD_PLAYBACK_LATENCY;
        } else {
            uint32_t sample_rate = 0;
            latency = QAP_MODULE_PCM_INPUT_BUFFER_LATENCY; //Input latency

            if (qap_mod->stream_out[QAP_OUT_OFFLOAD])
                sample_rate = qap_mod->stream_out[QAP_OUT_OFFLOAD]->sample_rate;
            else if (qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH])
                sample_rate = qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]->sample_rate;

            if (sample_rate) {
                latency += (get_pcm_output_buffer_size_samples_l(qap_mod) * 1000) / out->sample_rate;
            }
        }

        if ( audio_extn_bt_hal_get_output_stream(qap_mod->bt_hdl) != NULL) {
            if (is_offload_usecase(out->usecase)) {
                latency = audio_extn_bt_hal_get_latency(qap_mod->bt_hdl) +
                QAP_COMPRESS_OFFLOAD_PROCESSING_LATENCY;
            } else {
                latency = audio_extn_bt_hal_get_latency(qap_mod->bt_hdl) +
                QAP_PCM_OFFLOAD_PROCESSING_LATENCY;
            }
        }
    }

    DEBUG_MSG_VV("Latency %d", latency);
    return latency;
}

static bool qap_check_and_get_compressed_device_format(int device, int *format)
{
    switch (device) {
        case (AUDIO_DEVICE_OUT_AUX_DIGITAL | QAP_AUDIO_FORMAT_AC3):
            *format = AUDIO_FORMAT_AC3;
            return true;
        case (AUDIO_DEVICE_OUT_AUX_DIGITAL | QAP_AUDIO_FORMAT_EAC3):
            *format = AUDIO_FORMAT_E_AC3;
            return true;
        case (AUDIO_DEVICE_OUT_AUX_DIGITAL | QAP_AUDIO_FORMAT_DTS):
            *format = AUDIO_FORMAT_DTS;
            return true;
        default:
            return false;
    }
}

static void set_out_stream_channel_map(struct stream_out *out, qap_output_config_t * media_fmt)
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

bool audio_extn_is_qap_enabled()
{
    bool prop_enabled = false;
    char value[PROPERTY_VALUE_MAX] = {0};
    property_get("vendor.audio.qap.enabled", value, NULL);
    prop_enabled = atoi(value) || !strncmp("true", value, 4);
    DEBUG_MSG("%d", prop_enabled);
    return (prop_enabled);
}

void static qap_close_all_output_streams(struct qap_module *qap_mod)
{
    int i =0;
    struct stream_out *stream_out = NULL;
    DEBUG_MSG("Entry");

    for (i = 0; i < MAX_QAP_MODULE_OUT; i++) {
        stream_out = qap_mod->stream_out[i];
        if (stream_out != NULL) {
            adev_close_output_stream((struct audio_hw_device *)p_qap->adev, (struct audio_stream_out *)stream_out);
            DEBUG_MSG("Closed outputenum=%d session 0x%x %s",
                    i, (int)stream_out, use_case_table[stream_out->usecase]);
            qap_mod->stream_out[i] = NULL;
        }
        memset(&qap_mod->session_outputs_config.output_config[i], 0, sizeof(qap_session_outputs_config_t));
        qap_mod->is_media_fmt_changed[i] = false;
    }
    DEBUG_MSG("exit");
}

/* Call back function for mm module. */
static void qap_session_callback(qap_session_handle_t session_handle __unused,
                                  void *prv_data,
                                 qap_callback_event_t event_id,
                                  int size,
                                  void *data)
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
    struct qap_module* qap_mod = (struct qap_module*)prv_data;
    struct audio_stream_out *bt_stream = NULL;
    int format;
    int8_t *data_buffer_p = NULL;
    uint32_t buffer_size = 0;
    bool need_to_recreate_stream = false;
    struct audio_config config;
    qap_output_config_t *new_conf = NULL;
    qap_audio_buffer_t *buffer = (qap_audio_buffer_t *) data;
    uint32_t device = 0;

    if (qap_mod->is_session_closing) {
        DEBUG_MSG("Dropping event as session is closing."
                "Event = 0x%X, Bytes to write %d", event_id, size);
        return;
    }

    if(p_qap->qap_output_block_handling) {
        pthread_mutex_lock(&qap_mod->session_output_lock);
        if (!qap_mod->is_session_output_active) {
            qap_close_all_output_streams(qap_mod);
            DEBUG_MSG("disabling MM module output by blocking the output thread");
            pthread_cond_wait(&qap_mod->session_output_cond, &qap_mod->session_output_lock);
            DEBUG_MSG("MM module output Enabled, output thread active");
        }
        pthread_mutex_unlock(&qap_mod->session_output_lock);
    }

    /* Default config initialization. */
    config.sample_rate = config.offload_info.sample_rate = QAP_OUTPUT_SAMPLING_RATE;
    config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
    config.offload_info.size = AUDIO_INFO_INITIALIZER.size;
    config.format = config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
    config.offload_info.bit_width = CODEC_BACKEND_DEFAULT_BIT_WIDTH;
    config.offload_info.channel_mask = config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;

    pthread_mutex_lock(&p_qap->lock);

    if (event_id == QAP_CALLBACK_EVENT_OUTPUT_CFG_CHANGE) {
        new_conf = &buffer->buffer_parms.output_buf_params.output_config;
        qap_output_config_t *cached_conf = NULL;
        int index = -1;

        DEBUG_MSG("Received QAP_CALLBACK_EVENT_OUTPUT_CFG_CHANGE event for output id=0x%x",
                buffer->buffer_parms.output_buf_params.output_id);

        DEBUG_MSG("sample rate=%d bitwidth=%d format = %d channels =0x%x",
            new_conf->sample_rate,
            new_conf->bit_width,
            new_conf->format,
            new_conf->channels);

        if ( (uint32_t)size < sizeof(qap_output_config_t)) {
            ERROR_MSG("Size is not proper for the event AUDIO_OUTPUT_MEDIA_FORMAT_EVENT.");
            return ;
        }

        index = get_media_fmt_array_index_for_output_id_l(qap_mod, buffer->buffer_parms.output_buf_params.output_id);

        DEBUG_MSG("index = %d", index);

        if (index >= 0) {
            cached_conf = &qap_mod->session_outputs_config.output_config[index];
        } else if (index < 0 && qap_mod->new_out_format_index < MAX_QAP_MODULE_OUT) {
            index = qap_mod->new_out_format_index;
            cached_conf = &qap_mod->session_outputs_config.output_config[index];
            qap_mod->new_out_format_index++;
        }

        if (cached_conf == NULL) {
            ERROR_MSG("Maximum output from a QAP module is reached. Can not process new output.");
            return ;
        }

        if (memcmp(cached_conf, new_conf, sizeof(qap_output_config_t)) != 0) {
            memcpy(cached_conf, new_conf, sizeof(qap_output_config_t));
            qap_mod->is_media_fmt_changed[index] = true;
        }
    } else if (event_id == QAP_CALLBACK_EVENT_DATA) {
        data_buffer_p = (int8_t*)buffer->common_params.data+buffer->common_params.offset;
        buffer_size = buffer->common_params.size;
        device = buffer->buffer_parms.output_buf_params.output_id;

        DEBUG_MSG_VV("Received QAP_CALLBACK_EVENT_DATA event buff size(%d) for outputid=0x%x",
            buffer_size, buffer->buffer_parms.output_buf_params.output_id);

        if (buffer && buffer->common_params.data) {
            int index = -1;

            index = get_media_fmt_array_index_for_output_id_l(qap_mod, buffer->buffer_parms.output_buf_params.output_id);
            DEBUG_MSG("index = %d", index);
            if (index > -1 && qap_mod->is_media_fmt_changed[index]) {
                DEBUG_MSG("FORMAT changed, recreate stream");
                need_to_recreate_stream = true;
                qap_mod->is_media_fmt_changed[index] = false;

                qap_output_config_t *new_config = &qap_mod->session_outputs_config.output_config[index];

                config.sample_rate = config.offload_info.sample_rate = new_config->sample_rate;
                config.offload_info.version = AUDIO_INFO_INITIALIZER.version;
                config.offload_info.size = AUDIO_INFO_INITIALIZER.size;
                config.offload_info.bit_width = new_config->bit_width;

                if (new_config->format == QAP_AUDIO_FORMAT_PCM_16_BIT) {
                    if (new_config->bit_width == 16)
                        config.format = config.offload_info.format = AUDIO_FORMAT_PCM_16_BIT;
                    else if (new_config->bit_width == 24)
                        config.format = config.offload_info.format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
                    else
                        config.format = config.offload_info.format = AUDIO_FORMAT_PCM_32_BIT;
                } else if (new_config->format  == QAP_AUDIO_FORMAT_AC3)
                    config.format = config.offload_info.format = AUDIO_FORMAT_AC3;
                else if (new_config->format  == QAP_AUDIO_FORMAT_EAC3)
                    config.format = config.offload_info.format = AUDIO_FORMAT_E_AC3;
                else if (new_config->format  == QAP_AUDIO_FORMAT_DTS)
                    config.format = config.offload_info.format = AUDIO_FORMAT_DTS;

                device |= (new_config->format & AUDIO_FORMAT_MAIN_MASK);

                config.channel_mask = audio_channel_out_mask_from_count(new_config->channels);
                config.offload_info.channel_mask = config.channel_mask;
                DEBUG_MSG("sample rate=%d bitwidth=%d format = %d channels=%d channel_mask=%d device =0x%x",
                    config.sample_rate,
                    config.offload_info.bit_width,
                    config.offload_info.format,
                    new_config->channels,
                    config.channel_mask,
                    device);
            }
        }

        if (p_qap->passthrough_out != NULL) {
            //If QAP passthrough is active then all the module output will be dropped.
            pthread_mutex_unlock(&p_qap->lock);
            DEBUG_MSG("QAP-PSTH is active, DROPPING DATA!");
            return;
        }

        if (qap_check_and_get_compressed_device_format(device, &format)) {
            /*
             * CASE 1: Transcoded output of mm module.
             * If HDMI is not connected then drop the data.
             * Only one HDMI output can be supported from all the mm modules of QAP.
             * Multi-Channel PCM HDMI output streams will be closed from all the mm modules.
             * If transcoded output of other module is already enabled then this data will be dropped.
             */

            if (!p_qap->hdmi_connect) {
                DEBUG_MSG("HDMI not connected, DROPPING DATA!");
                pthread_mutex_unlock(&p_qap->lock);
                return;
            }

            //Closing all the PCM HDMI output stream from QAP.
            close_all_pcm_hdmi_output_l();

            /* If Media format was changed for this stream then need to re-create the stream. */
            if (need_to_recreate_stream && qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH]) {
                DEBUG_MSG("closing Transcode Passthrough session ox%x",
                    (int)qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH]);
                adev_close_output_stream((struct audio_hw_device *)p_qap->adev,
                                         (struct audio_stream_out *)(qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH]));
                qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH] = NULL;
                p_qap->passthrough_enabled = false;
            }

            if (!p_qap->passthrough_enabled
                && !(qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH])) {

                audio_devices_t devices;

                config.format = config.offload_info.format = format;
                config.offload_info.channel_mask = config.channel_mask = AUDIO_CHANNEL_OUT_5POINT1;

                flags = (AUDIO_OUTPUT_FLAG_NON_BLOCKING
                         | AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD
                         | AUDIO_OUTPUT_FLAG_DIRECT
                         | AUDIO_OUTPUT_FLAG_COMPRESS_PASSTHROUGH);
                devices = AUDIO_DEVICE_OUT_AUX_DIGITAL;

                DEBUG_MSG("Opening Transcode Passthrough out(outputenum=%d) session 0x%x with below params",
                        QAP_OUT_TRANSCODE_PASSTHROUGH,
                        (int)qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH]);

                DEBUG_MSG("sample rate=%d bitwidth=%d format = 0x%x channel mask=0x%x flags=0x%x device =0x%x",
                    config.sample_rate,
                    config.offload_info.bit_width,
                    config.offload_info.format,
                    config.offload_info.channel_mask,
                    flags,
                    devices);

                ret = adev_open_output_stream((struct audio_hw_device *)p_qap->adev,
                                              QAP_DEFAULT_COMPR_PASSTHROUGH_HANDLE,
                                              devices,
                                              flags,
                                              &config,
                                              (struct audio_stream_out **)&(qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH]),
                                              NULL);
                if (ret < 0) {
                    ERROR_MSG("Failed opening Transcode Passthrough out(outputenum=%d) session 0x%x",
                            QAP_OUT_TRANSCODE_PASSTHROUGH,
                            (int)qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH]);
                    pthread_mutex_unlock(&p_qap->lock);
                    return;
                } else
                    DEBUG_MSG("Opened Transcode Passthrough out(outputenum=%d) session 0x%x",
                            QAP_OUT_TRANSCODE_PASSTHROUGH,
                            (int)qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH]);


                if (format == AUDIO_FORMAT_E_AC3) {
                    qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH]->compr_config.fragment_size =
                            COMPRESS_PASSTHROUGH_DDP_FRAGMENT_SIZE;
                }
                qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH]->compr_config.fragments =
                        COMPRESS_OFFLOAD_NUM_FRAGMENTS;

                p_qap->passthrough_enabled = true;
            }

            if (qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH]) {
                DEBUG_MSG_VV("Writing Bytes(%d) to QAP_OUT_TRANSCODE_PASSTHROUGH output(%p) buff ptr(%p)",
                    buffer_size, qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH],
                    data_buffer_p);
                ret = qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH]->stream.write(
                        (struct audio_stream_out *)qap_mod->stream_out[QAP_OUT_TRANSCODE_PASSTHROUGH],
                        data_buffer_p,
                        buffer_size);
            }
        }
        else if ((device & AUDIO_DEVICE_OUT_AUX_DIGITAL)
                   && (p_qap->hdmi_connect)
                   && (p_qap->hdmi_sink_channels > 2)) {

            /* CASE 2: Multi-Channel PCM output to HDMI.
             * If any other HDMI output is already enabled then this has to be dropped.
             */

            if (p_qap->passthrough_enabled) {
                //Closing all the multi-Channel PCM HDMI output stream from QAP.
                close_all_pcm_hdmi_output_l();

                //If passthrough is active then pcm hdmi output has to be dropped.
                pthread_mutex_unlock(&p_qap->lock);
                DEBUG_MSG("Compressed passthrough enabled, DROPPING DATA!");
                return;
            }

            /* If Media format was changed for this stream then need to re-create the stream. */
            if (need_to_recreate_stream && qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]) {
                DEBUG_MSG("closing MCH PCM session ox%x", (int)qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]);
                adev_close_output_stream((struct audio_hw_device *)p_qap->adev,
                                         (struct audio_stream_out *)(qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]));
                qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH] = NULL;
                p_qap->mch_pcm_hdmi_enabled = false;
            }

            if (!p_qap->mch_pcm_hdmi_enabled && !(qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH])) {
                audio_devices_t devices;

                if (event_id == AUDIO_DATA_EVENT) {
                    config.offload_info.format = config.format = AUDIO_FORMAT_PCM_16_BIT;

                    if (p_qap->hdmi_sink_channels == 8) {
                        config.offload_info.channel_mask = config.channel_mask =
                                AUDIO_CHANNEL_OUT_7POINT1;
                    } else if (p_qap->hdmi_sink_channels == 6) {
                        config.offload_info.channel_mask = config.channel_mask =
                                AUDIO_CHANNEL_OUT_5POINT1;
                    } else {
                        config.offload_info.channel_mask = config.channel_mask =
                                AUDIO_CHANNEL_OUT_STEREO;
                    }
                }

                devices = AUDIO_DEVICE_OUT_AUX_DIGITAL;
                flags = AUDIO_OUTPUT_FLAG_DIRECT;

                DEBUG_MSG("Opening MCH PCM out(outputenum=%d) session ox%x with below params",
                    QAP_OUT_OFFLOAD_MCH,
                    (int)qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]);

                DEBUG_MSG("sample rate=%d bitwidth=%d format = 0x%x channel mask=0x%x flags=0x%x device =0x%x",
                    config.sample_rate,
                    config.offload_info.bit_width,
                    config.offload_info.format,
                    config.offload_info.channel_mask,
                    flags,
                    devices);

                ret = adev_open_output_stream((struct audio_hw_device *)p_qap->adev,
                                              QAP_DEFAULT_COMPR_AUDIO_HANDLE,
                                              devices,
                                              flags,
                                              &config,
                                              (struct audio_stream_out **)&(qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]),
                                              NULL);
                if (ret < 0) {
                    ERROR_MSG("Failed opening MCH PCM out(outputenum=%d) session ox%x",
                        QAP_OUT_OFFLOAD_MCH,
                        (int)qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]);
                    pthread_mutex_unlock(&p_qap->lock);
                    return;
                    } else
                        DEBUG_MSG("Opened MCH PCM out(outputenum=%d) session ox%x",
                            QAP_OUT_OFFLOAD_MCH,
                            (int)qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]);

                set_out_stream_channel_map(qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH], new_conf);

                qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]->compr_config.fragments =
                        COMPRESS_OFFLOAD_NUM_FRAGMENTS;
                qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]->compr_config.fragment_size =
                        qap_get_pcm_offload_output_buffer_size(qap_mod, &config.offload_info);

                p_qap->mch_pcm_hdmi_enabled = true;

                if ((qap_mod->stream_in[QAP_IN_MAIN]
                    && qap_mod->stream_in[QAP_IN_MAIN]->client_callback != NULL) ||
                    (qap_mod->stream_in[QAP_IN_MAIN_2]
                    && qap_mod->stream_in[QAP_IN_MAIN_2]->client_callback != NULL)) {

                    if (qap_mod->stream_in[QAP_IN_MAIN]) {
                        qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]->stream.set_callback(
                            (struct audio_stream_out *)qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH],
                            qap_mod->stream_in[QAP_IN_MAIN]->client_callback,
                            qap_mod->stream_in[QAP_IN_MAIN]->client_cookie);
                    }
                    if (qap_mod->stream_in[QAP_IN_MAIN_2]) {
                        qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]->stream.set_callback(
                            (struct audio_stream_out *)qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH],
                            qap_mod->stream_in[QAP_IN_MAIN_2]->client_callback,
                            qap_mod->stream_in[QAP_IN_MAIN_2]->client_cookie);
                    }
                } else if (qap_mod->stream_in[QAP_IN_PCM]
                           && qap_mod->stream_in[QAP_IN_PCM]->client_callback != NULL) {

                    qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]->stream.set_callback(
                            (struct audio_stream_out *)qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH],
                            qap_mod->stream_in[QAP_IN_PCM]->client_callback,
                            qap_mod->stream_in[QAP_IN_PCM]->client_cookie);
                }
            }
            if (qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]) {
                DEBUG_MSG_VV("Writing Bytes(%d) to QAP_OUT_OFFLOAD_MCH output(%p) buff ptr(%p)",
                    buffer_size, qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH],
                    data_buffer_p);
                ret = qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH]->stream.write(
                        (struct audio_stream_out *)qap_mod->stream_out[QAP_OUT_OFFLOAD_MCH],
                        data_buffer_p,
                        buffer_size);
            }
        }
        else {
            /* CASE 3: PCM output.
             */

            /* If Media format was changed for this stream then need to re-create the stream. */
            if (need_to_recreate_stream && qap_mod->stream_out[QAP_OUT_OFFLOAD]) {
                DEBUG_MSG("closing PCM session ox%x", (int)qap_mod->stream_out[QAP_OUT_OFFLOAD]);
                adev_close_output_stream((struct audio_hw_device *)p_qap->adev,
                                         (struct audio_stream_out *)(qap_mod->stream_out[QAP_OUT_OFFLOAD]));
                qap_mod->stream_out[QAP_OUT_OFFLOAD] = NULL;
            }

            bt_stream = audio_extn_bt_hal_get_output_stream(qap_mod->bt_hdl);
            if (bt_stream != NULL) {
                if (qap_mod->stream_out[QAP_OUT_OFFLOAD]) {
                    adev_close_output_stream((struct audio_hw_device *)p_qap->adev,
                                             (struct audio_stream_out *)(qap_mod->stream_out[QAP_OUT_OFFLOAD]));
                    qap_mod->stream_out[QAP_OUT_OFFLOAD] = NULL;
                }

                audio_extn_bt_hal_out_write(p_qap->bt_hdl, data_buffer_p, buffer_size);
            } else if (NULL == qap_mod->stream_out[QAP_OUT_OFFLOAD]) {
                audio_devices_t devices;

                if (qap_mod->stream_in[QAP_IN_MAIN])
                    devices = get_device_types(&qap_mod->stream_in[QAP_IN_MAIN]->device_list);
                else
                    devices = get_device_types(&qap_mod->stream_in[QAP_IN_PCM]->device_list);

                //If multi channel pcm or passthrough is already enabled then remove the hdmi flag from device.
                if (p_qap->mch_pcm_hdmi_enabled || p_qap->passthrough_enabled) {
                    if (devices & AUDIO_DEVICE_OUT_AUX_DIGITAL)
                        devices ^= AUDIO_DEVICE_OUT_AUX_DIGITAL;
                }
                if (devices == 0) {
                    devices = device;
                }

                flags = AUDIO_OUTPUT_FLAG_DIRECT;


                DEBUG_MSG("Opening Stereo PCM out(outputenum=%d) session ox%x with below params",
                    QAP_OUT_OFFLOAD,
                    (int)qap_mod->stream_out[QAP_OUT_OFFLOAD]);


                DEBUG_MSG("sample rate=%d bitwidth=%d format = 0x%x channel mask=0x%x flags=0x%x device =0x%x",
                    config.sample_rate,
                    config.offload_info.bit_width,
                    config.offload_info.format,
                    config.offload_info.channel_mask,
                    flags,
                    devices);


                /* TODO:: Need to Propagate errors to framework */
                ret = adev_open_output_stream((struct audio_hw_device *)p_qap->adev,
                                              QAP_DEFAULT_COMPR_AUDIO_HANDLE,
                                              devices,
                                              flags,
                                              &config,
                                              (struct audio_stream_out **)&(qap_mod->stream_out[QAP_OUT_OFFLOAD]),
                                              NULL);
                if (ret < 0) {
                    ERROR_MSG("Failed opening Stereo PCM out(outputenum=%d) session ox%x",
                        QAP_OUT_OFFLOAD,
                        (int)qap_mod->stream_out[QAP_OUT_OFFLOAD]);
                    pthread_mutex_unlock(&p_qap->lock);
                    return;
                } else
                    DEBUG_MSG("Opened Stereo PCM out(outputenum=%d) session ox%x",
                        QAP_OUT_OFFLOAD,
                        (int)qap_mod->stream_out[QAP_OUT_OFFLOAD]);

                set_out_stream_channel_map(qap_mod->stream_out[QAP_OUT_OFFLOAD], new_conf);

                if ((qap_mod->stream_in[QAP_IN_MAIN]
                    && qap_mod->stream_in[QAP_IN_MAIN]->client_callback != NULL) ||
                    (qap_mod->stream_in[QAP_IN_MAIN_2]
                    && qap_mod->stream_in[QAP_IN_MAIN_2]->client_callback != NULL)) {

                    if (qap_mod->stream_in[QAP_IN_MAIN]) {
                        qap_mod->stream_out[QAP_OUT_OFFLOAD]->stream.set_callback(
                            (struct audio_stream_out *)qap_mod->stream_out[QAP_OUT_OFFLOAD],
                            qap_mod->stream_in[QAP_IN_MAIN]->client_callback,
                            qap_mod->stream_in[QAP_IN_MAIN]->client_cookie);
                    }
                    if (qap_mod->stream_in[QAP_IN_MAIN_2]) {
                        qap_mod->stream_out[QAP_OUT_OFFLOAD]->stream.set_callback(
                            (struct audio_stream_out *)qap_mod->stream_out[QAP_OUT_OFFLOAD],
                            qap_mod->stream_in[QAP_IN_MAIN_2]->client_callback,
                            qap_mod->stream_in[QAP_IN_MAIN_2]->client_cookie);
                    }
                } else if (qap_mod->stream_in[QAP_IN_PCM]
                           && qap_mod->stream_in[QAP_IN_PCM]->client_callback != NULL) {

                    qap_mod->stream_out[QAP_OUT_OFFLOAD]->stream.set_callback(
                                                (struct audio_stream_out *)qap_mod->stream_out[QAP_OUT_OFFLOAD],
                                                qap_mod->stream_in[QAP_IN_PCM]->client_callback,
                                                qap_mod->stream_in[QAP_IN_PCM]->client_cookie);
                }

                qap_mod->stream_out[QAP_OUT_OFFLOAD]->compr_config.fragments =
                        COMPRESS_OFFLOAD_NUM_FRAGMENTS;
                qap_mod->stream_out[QAP_OUT_OFFLOAD]->compr_config.fragment_size =
                        qap_get_pcm_offload_output_buffer_size(qap_mod, &config.offload_info);

                if (qap_mod->is_vol_set) {
                    DEBUG_MSG("Setting Volume Left[%f], Right[%f]", qap_mod->vol_left, qap_mod->vol_right);
                    qap_mod->stream_out[QAP_OUT_OFFLOAD]->stream.set_volume(
                            (struct audio_stream_out *)qap_mod->stream_out[QAP_OUT_OFFLOAD],
                            qap_mod->vol_left,
                            qap_mod->vol_right);
                }
            }

            if (qap_mod->stream_out[QAP_OUT_OFFLOAD]) {
                DEBUG_MSG_VV("Writing Bytes(%d) to QAP_OUT_OFFLOAD output(%p) buff ptr(%p)",
                    buffer_size, qap_mod->stream_out[QAP_OUT_OFFLOAD],
                    data_buffer_p);
                ret = qap_mod->stream_out[QAP_OUT_OFFLOAD]->stream.write(
                        (struct audio_stream_out *)qap_mod->stream_out[QAP_OUT_OFFLOAD],
                        data_buffer_p,
                        buffer_size);
            }
        }
        DEBUG_MSG_VV("Bytes consumed [%d] by Audio HAL", ret);
    }
    else if (event_id == QAP_CALLBACK_EVENT_EOS
               || event_id == QAP_CALLBACK_EVENT_MAIN_2_EOS
               || event_id == QAP_CALLBACK_EVENT_EOS_ASSOC) {
        struct stream_out *out = qap_mod->stream_in[QAP_IN_MAIN];
        struct stream_out *out_pcm = qap_mod->stream_in[QAP_IN_PCM];
        struct stream_out *out_main2 = qap_mod->stream_in[QAP_IN_MAIN_2];
        struct stream_out *out_assoc = qap_mod->stream_in[QAP_IN_ASSOC];

        /**
         * TODO:: Only DD/DDP Associate Eos is handled, need to add support
         * for other formats.
         */
        if (event_id == QAP_CALLBACK_EVENT_EOS
                && (out_pcm != NULL)
                && (check_stream_state_l(out_pcm, STOPPING))) {

            lock_output_stream_l(out_pcm);
            out_pcm->client_callback(STREAM_CBK_EVENT_DRAIN_READY, NULL, out_pcm->client_cookie);
            set_stream_state_l(out_pcm, STOPPED);
            unlock_output_stream_l(out_pcm);
            DEBUG_MSG("sent pcm DRAIN_READY");
        } else if ( event_id == QAP_CALLBACK_EVENT_EOS_ASSOC
                && (out_assoc != NULL)
                && (check_stream_state_l(out_assoc, STOPPING))) {

            lock_output_stream_l(out_assoc);
            out_assoc->client_callback(STREAM_CBK_EVENT_DRAIN_READY, NULL, out_assoc->client_cookie);
            set_stream_state_l(out_assoc, STOPPED);
            unlock_output_stream_l(out_assoc);
            DEBUG_MSG("sent associated DRAIN_READY");
        } else if (event_id == QAP_CALLBACK_EVENT_MAIN_2_EOS
                && (out_main2 != NULL)
                && (check_stream_state_l(out_main2, STOPPING))) {

            lock_output_stream_l(out_main2);
            out_main2->client_callback(STREAM_CBK_EVENT_DRAIN_READY, NULL, out_main2->client_cookie);
            set_stream_state_l(out_main2, STOPPED);
            unlock_output_stream_l(out_main2);
            DEBUG_MSG("sent main2 DRAIN_READY");
        } else if ((out != NULL) && (check_stream_state_l(out, STOPPING))) {
            lock_output_stream_l(out);
            out->client_callback(STREAM_CBK_EVENT_DRAIN_READY, NULL, out->client_cookie);
            set_stream_state_l(out, STOPPED);
            unlock_output_stream_l(out);
            DEBUG_MSG("sent main DRAIN_READY");
        }
    }
    else if (event_id == QAP_CALLBACK_EVENT_EOS || event_id == QAP_CALLBACK_EVENT_EOS_ASSOC) {
        struct stream_out *out = NULL;

        if (event_id == QAP_CALLBACK_EVENT_EOS) {
            out = qap_mod->stream_in[QAP_IN_MAIN];
        } else {
            out = qap_mod->stream_in[QAP_IN_ASSOC];
        }

        if ((out != NULL) && (check_stream_state_l(out, STOPPING))) {
            lock_output_stream_l(out);
            out->client_callback(STREAM_CBK_EVENT_DRAIN_READY, NULL, out->client_cookie);
            set_stream_state_l(out, STOPPED);
            unlock_output_stream_l(out);
            DEBUG_MSG("sent DRAIN_READY");
        }
    }

    pthread_mutex_unlock(&p_qap->lock);
    return;
}

static int qap_sess_close(struct qap_module* qap_mod)
{
    int j;
    int ret = -EINVAL;

    DEBUG_MSG("Closing Session.");

    //Check if all streams are closed or not.
    for (j = 0; j < MAX_QAP_MODULE_IN; j++) {
        if (qap_mod->stream_in[j] != NULL) {
            break;
        }
    }
    if (j != MAX_QAP_MODULE_IN) {
        DEBUG_MSG("Some stream is still active, Can not close session.");
        return 0;
    }

    qap_mod->is_session_closing = true;
    if(p_qap->qap_output_block_handling) {
        pthread_mutex_lock(&qap_mod->session_output_lock);
        if (qap_mod->is_session_output_active == false) {
            pthread_cond_signal(&qap_mod->session_output_cond);
            DEBUG_MSG("Wake up MM module output thread");
        }
        pthread_mutex_unlock(&qap_mod->session_output_lock);
    }
    pthread_mutex_lock(&p_qap->lock);

    if (!qap_mod || !qap_mod->session_handle) {
        ERROR_MSG("Wrong state to process qap_mod(%p) sess_hadl(%p)",
            qap_mod, qap_mod->session_handle);
        return -EINVAL;
    }

    ret = qap_session_close(qap_mod->session_handle);
    if (QAP_STATUS_OK != ret) {
        ERROR_MSG("close session failed %d", ret);
        return -EINVAL;
    } else
        DEBUG_MSG("Closed QAP session 0x%x", (int)qap_mod->session_handle);

    qap_mod->session_handle = NULL;
    qap_mod->is_vol_set = false;
    memset(qap_mod->stream_state, 0, sizeof(qap_mod->stream_state));

    qap_close_all_output_streams(qap_mod);

    qap_mod->new_out_format_index = 0;

    pthread_mutex_unlock(&p_qap->lock);
    qap_mod->is_session_closing = false;
    DEBUG_MSG("Exit.");

    return 0;
}

static int qap_stream_close(struct stream_out *out)
{
    int ret = -EINVAL;
    struct qap_module *qap_mod = NULL;
    int index = -1;
    DEBUG_MSG("Flag [0x%x], Stream handle [%p]", out->flags, out->qap_stream_handle);

    qap_mod = get_qap_module_for_input_stream_l(out);
    index = get_input_stream_index_l(out);

    if (!qap_mod || !qap_mod->session_handle || (index < 0) || !out->qap_stream_handle) {
        ERROR_MSG("Wrong state to process qap_mod(%p) sess_hadl(%p) strm hndl(%p), index %d",
            qap_mod, qap_mod->session_handle, out->qap_stream_handle, index);
        return -EINVAL;
    }

    pthread_mutex_lock(&p_qap->lock);

    set_stream_state_l(out,STOPPED);
    qap_mod->stream_in[index] = NULL;

    lock_output_stream_l(out);

    ret = qap_module_deinit(out->qap_stream_handle);
    if (QAP_STATUS_OK != ret) {
        ERROR_MSG("deinit failed %d", ret);
        return -EINVAL;
    } else
        DEBUG_MSG("module(ox%x) closed successfully", (int)out->qap_stream_handle);


    out->qap_stream_handle = NULL;
    unlock_output_stream_l(out);

    pthread_mutex_unlock(&p_qap->lock);

    //If all streams are closed then close the session.
    qap_sess_close(qap_mod);

    DEBUG_MSG("Exit");
    return ret;
}

#define MAX_INIT_PARAMS 6

static void update_qap_session_init_params(audio_session_handle_t session_handle) {
    DEBUG_MSG("Entry");
    qap_status_t ret = QAP_STATUS_OK;
    uint32_t cmd_data[MAX_INIT_PARAMS] = {0};

    /* all init params should be sent
     * together so gang them up.
     */
    cmd_data[0] = MS12_SESSION_CFG_MAX_CHS;
    cmd_data[1] = 6;/*5.1 channels*/

    cmd_data[2] = MS12_SESSION_CFG_BS_OUTPUT_MODE;
    cmd_data[3] = 3;/*DDP Re-encoding and DDP to DD Transcoding*/

    cmd_data[4] = MS12_SESSION_CFG_CHMOD_LOCKING;
    cmd_data[MAX_INIT_PARAMS - 1] = 1;/*Lock to 6 channel*/

    ret = qap_session_cmd(session_handle,
            QAP_SESSION_CMD_SET_PARAM,
            MAX_INIT_PARAMS * sizeof(uint32_t),
            &cmd_data[0],
            NULL,
            NULL);
    if (ret != QAP_STATUS_OK) {
        ERROR_MSG("session init params config failed");
    }
    DEBUG_MSG("Exit");
    return;
}

/* Query HDMI EDID and sets module output accordingly.*/
static void qap_set_hdmi_configuration_to_module()
{
    int ret = 0;
    int channels = 0;
    char prop_value[PROPERTY_VALUE_MAX] = {0};
    bool passth_support = false;
    qap_session_outputs_config_t *session_outputs_config = NULL;


    DEBUG_MSG("Entry");

    if (!p_qap) {
        return;
    }

    if (!p_qap->hdmi_connect) {
        return;
    }

    p_qap->hdmi_sink_channels = 0;

    if (p_qap->qap_mod[MS12].session_handle)
        session_outputs_config = &p_qap->qap_mod[MS12].session_outputs_config;
    else if (p_qap->qap_mod[DTS_M8].session_handle)
        session_outputs_config = &p_qap->qap_mod[DTS_M8].session_outputs_config;
    else {
        DEBUG_MSG("HDMI connection comes even before session is setup");
        return;
    }

    session_outputs_config->num_output = 1;
    //QAP re-encoding and DSP offload passthrough is supported.
    if (property_get_bool("vendor.audio.offload.passthrough", false)
            && property_get_bool("vendor.audio.qap.reencode", false)) {

        if (p_qap->qap_mod[MS12].session_handle) {

            bool do_setparam = false;
            property_get("vendor.audio.qap.hdmi.out", prop_value, NULL);

            if (platform_is_edid_supported_format(p_qap->adev->platform, AUDIO_FORMAT_E_AC3)
                    && (strncmp(prop_value, "ddp", 3) == 0)) {
                do_setparam = true;
                session_outputs_config->output_config[0].format = QAP_AUDIO_FORMAT_EAC3;
                session_outputs_config->output_config[0].id = AUDIO_DEVICE_OUT_HDMI|QAP_AUDIO_FORMAT_EAC3;
            } else if (platform_is_edid_supported_format(p_qap->adev->platform, AUDIO_FORMAT_AC3)) {
                do_setparam = true;
                session_outputs_config->output_config[0].format = QAP_AUDIO_FORMAT_AC3;
                session_outputs_config->output_config[0].id = AUDIO_DEVICE_OUT_HDMI|QAP_AUDIO_FORMAT_AC3;
            }
            if (do_setparam) {
                DEBUG_MSG(" Enabling HDMI(Passthrough out) from MS12 wrapper outputid=0x%x",
                    session_outputs_config->output_config[0].id);
                ret = qap_session_cmd(p_qap->qap_mod[MS12].session_handle,
                                    QAP_SESSION_CMD_SET_OUTPUTS,
                                    sizeof(qap_session_outputs_config_t),
                                    session_outputs_config,
                                    NULL,
                                    NULL);
                if (QAP_STATUS_OK != ret) {
                    ERROR_MSG("Unable to register AUDIO_DEVICE_OUT_HDMI device with QAP %d", ret);
                    return;
                }
                passth_support = true;
            }
        }

        if (p_qap->qap_mod[DTS_M8].session_handle) {

            bool do_setparam = false;
            if (platform_is_edid_supported_format(p_qap->adev->platform, AUDIO_FORMAT_DTS)) {
                do_setparam = true;
                session_outputs_config->output_config[0].format = QAP_AUDIO_FORMAT_DTS;
                session_outputs_config->output_config[0].id = AUDIO_DEVICE_OUT_HDMI|QAP_AUDIO_FORMAT_DTS;
            }

            if (do_setparam) {
                ret = qap_session_cmd(p_qap->qap_mod[DTS_M8].session_handle,
                                    QAP_SESSION_CMD_SET_OUTPUTS,
                                    sizeof(qap_session_outputs_config_t),
                                    session_outputs_config,
                                    NULL,
                                    NULL);
                if (QAP_STATUS_OK != ret) {
                    ERROR_MSG("Unable to register AUDIO_DEVICE_OUT_HDMI device with QAP %d", ret);
                    return;
                }
                passth_support = true;
            }
        }
    }
    //Compressed passthrough is not enabled.
    if (!passth_support) {

        channels = platform_edid_get_max_channels(p_qap->adev->platform);
        session_outputs_config->output_config[0].format = QAP_AUDIO_FORMAT_PCM_16_BIT;

        switch (channels) {
            case 8:
                DEBUG_MSG("Switching Qap output to 7.1 channels");
                session_outputs_config->output_config[0].channels = 8;
                if (!p_qap->qap_msmd_enabled)
                    session_outputs_config->output_config[0].id = AUDIO_DEVICE_OUT_HDMI|QAP_AUDIO_FORMAT_PCM_16_BIT;
                p_qap->hdmi_sink_channels = channels;
                break;
            case 6:
                DEBUG_MSG("Switching Qap output to 5.1 channels");
                session_outputs_config->output_config[0].channels = 6;
                if (!p_qap->qap_msmd_enabled)
                    session_outputs_config->output_config[0].id = AUDIO_DEVICE_OUT_HDMI|QAP_AUDIO_FORMAT_PCM_16_BIT;
                p_qap->hdmi_sink_channels = channels;
                break;
            default:
                DEBUG_MSG("Switching Qap output to default channels");
                session_outputs_config->output_config[0].channels = 2;
                if (!p_qap->qap_msmd_enabled)
                    session_outputs_config->output_config[0].id = AUDIO_DEVICE_OUT_HDMI|QAP_AUDIO_FORMAT_PCM_16_BIT;
                p_qap->hdmi_sink_channels = 2;
                break;
        }

        if (p_qap->qap_mod[MS12].session_handle) {
            DEBUG_MSG(" Enabling HDMI(MCH PCM out) from MS12 wrapper outputid = %x", session_outputs_config->output_config[0].id);
            ret = qap_session_cmd(p_qap->qap_mod[MS12].session_handle,
                                QAP_SESSION_CMD_SET_OUTPUTS,
                                sizeof(qap_session_outputs_config_t),
                                session_outputs_config,
                                NULL,
                                NULL);
            if (QAP_STATUS_OK != ret) {
                ERROR_MSG("Unable to register AUDIO_DEVICE_OUT_HDMI device with QAP %d", ret);
                return;
            }
        }
        if (p_qap->qap_mod[DTS_M8].session_handle) {
                ret = qap_session_cmd(p_qap->qap_mod[MS12].session_handle,
                                    QAP_SESSION_CMD_SET_OUTPUTS,
                                    sizeof(qap_session_outputs_config_t),
                                    session_outputs_config,
                                    NULL,
                                    NULL);
                if (QAP_STATUS_OK != ret) {
                    ERROR_MSG("Unable to register AUDIO_DEVICE_OUT_HDMI device with QAP %d", ret);
                    return;
                }
            }

    }
    DEBUG_MSG("Exit");
}


static void qap_set_default_configuration_to_module()
{
    qap_session_outputs_config_t *session_outputs_config = NULL;
    int ret = 0;

    DEBUG_MSG("Entry");

    if (!p_qap) {
        return;
    }

    if (!p_qap->bt_connect) {
        DEBUG_MSG("BT is not connected.");
    }

    //ms12 wrapper don't support bt, treat this as speaker and routign to bt
    //will take care as a part of data callback notifier


    if (p_qap->qap_mod[MS12].session_handle)
        session_outputs_config = &p_qap->qap_mod[MS12].session_outputs_config;
    else if (p_qap->qap_mod[DTS_M8].session_handle)
        session_outputs_config = &p_qap->qap_mod[DTS_M8].session_outputs_config;

    session_outputs_config->num_output = 1;
    session_outputs_config->output_config[0].id = AUDIO_DEVICE_OUT_SPEAKER;
    session_outputs_config->output_config[0].format = QAP_AUDIO_FORMAT_PCM_16_BIT;


    if (p_qap->qap_mod[MS12].session_handle) {
        DEBUG_MSG(" Enabling speaker(PCM out) from MS12 wrapper outputid = %x", session_outputs_config->output_config[0].id);
        ret = qap_session_cmd(p_qap->qap_mod[MS12].session_handle,
                            QAP_SESSION_CMD_SET_OUTPUTS,
                            sizeof(qap_session_outputs_config_t),
                            session_outputs_config,
                            NULL,
                            NULL);
        if (QAP_STATUS_OK != ret) {
            ERROR_MSG("Unable to register AUDIO_DEVICE_OUT_SPEAKER device with QAP %d", ret);
            return;
        }
    }
    if (p_qap->qap_mod[DTS_M8].session_handle) {
        ret = qap_session_cmd(p_qap->qap_mod[DTS_M8].session_handle,
                            QAP_SESSION_CMD_SET_OUTPUTS,
                            sizeof(qap_session_outputs_config_t),
                            session_outputs_config,
                            NULL,
                            NULL);
        if (QAP_STATUS_OK != ret) {
            ERROR_MSG("Unable to register AUDIO_DEVICE_OUT_SPEAKER device with QAP %d", ret);
            return;
        }
    }
}


/* Open a MM module session with QAP. */
static int audio_extn_qap_session_open(mm_module_type mod_type, __unused struct stream_out *out)
{
    DEBUG_MSG("%s %d", __func__, __LINE__);
    int ret = 0;

    struct qap_module *qap_mod = NULL;

    if (mod_type >= MAX_MM_MODULE_TYPE)
        return -ENOTSUP; //Not supported by QAP module.

    pthread_mutex_lock(&p_qap->lock);

    qap_mod = &(p_qap->qap_mod[mod_type]);

    //If session is already opened then return.
    if (qap_mod->session_handle) {
        DEBUG_MSG("QAP Session is already opened.");
        pthread_mutex_unlock(&p_qap->lock);
        return 0;
    }

    if (MS12 == mod_type) {
        if (NULL == (qap_mod->session_handle = (void *)qap_session_open(QAP_SESSION_MS12_OTT, qap_mod->qap_lib))) {
            ERROR_MSG("Failed to open QAP session, lib_handle 0x%x", (int)qap_mod->qap_lib);
            ret = -EINVAL;
            goto exit;
        } else
            DEBUG_MSG("Opened QAP session 0x%x", (int)qap_mod->session_handle);

        update_qap_session_init_params(qap_mod->session_handle);
    }

    if (QAP_STATUS_OK != (qap_session_set_callback (qap_mod->session_handle, &qap_session_callback, (void *)qap_mod))) {
        ERROR_MSG("Failed to register QAP session callback");
        ret = -EINVAL;
        goto exit;
    }

    qap_mod->is_session_output_active = true;

    if(p_qap->hdmi_connect)
        qap_set_hdmi_configuration_to_module();
    else
        qap_set_default_configuration_to_module();

exit:
    pthread_mutex_unlock(&p_qap->lock);
    return ret;
}



static int qap_map_input_format(audio_format_t audio_format, qap_audio_format_t *format)
{
    if (audio_format == AUDIO_FORMAT_AC3) {
        *format = QAP_AUDIO_FORMAT_AC3;
        DEBUG_MSG( "File Format is AC3!");
    } else if (audio_format == AUDIO_FORMAT_E_AC3) {
        *format = QAP_AUDIO_FORMAT_EAC3;
        DEBUG_MSG( "File Format is E_AC3!");
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
        DEBUG_MSG( "File Format is AAC!");
    } else if (audio_format == AUDIO_FORMAT_DTS) {
        *format = QAP_AUDIO_FORMAT_DTS;
        DEBUG_MSG( "File Format is DTS!");
    } else if (audio_format == AUDIO_FORMAT_DTS_HD) {
        *format = QAP_AUDIO_FORMAT_DTS_HD;
        DEBUG_MSG( "File Format is DTS_HD!");
    } else if (audio_format == AUDIO_FORMAT_PCM_16_BIT) {
        *format = QAP_AUDIO_FORMAT_PCM_16_BIT;
        DEBUG_MSG( "File Format is PCM_16!");
    } else if (audio_format == AUDIO_FORMAT_PCM_32_BIT) {
        *format = QAP_AUDIO_FORMAT_PCM_32_BIT;
        DEBUG_MSG( "File Format is PCM_32!");
    } else if (audio_format == AUDIO_FORMAT_PCM_24_BIT_PACKED) {
        *format = QAP_AUDIO_FORMAT_PCM_24_BIT_PACKED;
        DEBUG_MSG( "File Format is PCM_24!");
    } else if ((audio_format == AUDIO_FORMAT_PCM_8_BIT) ||
               (audio_format == AUDIO_FORMAT_PCM_8_24_BIT)) {
        *format = QAP_AUDIO_FORMAT_PCM_8_24_BIT;
        DEBUG_MSG( "File Format is PCM_8_24!");
    } else {
        ERROR_MSG( "File Format not supported!");
        return -EINVAL;
    }
    return 0;
}


void qap_module_callback(__unused qap_module_handle_t module_handle,
                         void* priv_data,
                         qap_module_callback_event_t event_id,
                         __unused int size,
                         __unused void *data)
{
    struct stream_out *out=(struct stream_out *)priv_data;

    DEBUG_MSG("Entry");
    if (QAP_MODULE_CALLBACK_EVENT_SEND_INPUT_BUFFER == event_id) {
        DEBUG_MSG("QAP_MODULE_CALLBACK_EVENT_SEND_INPUT_BUFFER for (%p)", out);
        if (out->client_callback) {
            out->client_callback(STREAM_CBK_EVENT_WRITE_READY, NULL, out->client_cookie);
        }
        else
            DEBUG_MSG("client has no callback registered, no action needed for this event %d",
                event_id);
    }
    else
        DEBUG_MSG("Un Recognized event %d", event_id);

    DEBUG_MSG("exit");
    return;
}


/* opens a stream in QAP module. */
static int qap_stream_open(struct stream_out *out,
                           struct audio_config *config,
                           audio_output_flags_t flags,
                           audio_devices_t devices)
{
    int status = -EINVAL;
    mm_module_type mmtype = get_mm_module_for_format_l(config->format);
    struct qap_module* qap_mod = NULL;
    qap_module_config_t input_config = {0};

    DEBUG_MSG("Flags 0x%x, Device 0x%x for use case %s out 0x%x", flags, devices, use_case_table[out->usecase], (int)out);

    if (mmtype >= MAX_MM_MODULE_TYPE) {
        ERROR_MSG("Unsupported Stream");
        return -ENOTSUP;
    }

    //Open the module session, if not opened already.
    status = audio_extn_qap_session_open(mmtype, out);
    qap_mod = &(p_qap->qap_mod[mmtype]);

    if ((status != 0) || (!qap_mod->session_handle ))
        return status;

    input_config.sample_rate = config->sample_rate;
    input_config.channels = popcount(config->channel_mask);
    if (input_config.format != AUDIO_FORMAT_PCM_16_BIT) {
        input_config.format &= AUDIO_FORMAT_MAIN_MASK;
    }
    input_config.module_type = QAP_MODULE_DECODER;
    status = qap_map_input_format(config->format, &input_config.format);
    if (status == -EINVAL)
        return -EINVAL;

    DEBUG_MSG("qap_stream_open sample_rate(%d) channels(%d) devices(%#x) flags(%#x) format(%#x)",
              input_config.sample_rate, input_config.channels, devices, flags, input_config.format);

    if (input_config.format == QAP_AUDIO_FORMAT_PCM_16_BIT) {
        //If PCM stream is already opened then fail this stream open.
        if (qap_mod->stream_in[QAP_IN_PCM]) {
            ERROR_MSG("PCM input is already active.");
            return -ENOTSUP;
        }
        input_config.flags = QAP_MODULE_FLAG_SYSTEM_SOUND;
        status = qap_module_init(qap_mod->session_handle, &input_config, &out->qap_stream_handle);
        if (QAP_STATUS_OK != status) {
            ERROR_MSG("Unable to open PCM(QAP_MODULE_FLAG_SYSTEM_SOUND) QAP module %d", status);
            return -EINVAL;
        } else
            DEBUG_MSG("QAP_MODULE_FLAG_SYSTEM_SOUND, module(ox%x) opened successfully", (int)out->qap_stream_handle);

        qap_mod->stream_in[QAP_IN_PCM] = out;
    } else if ((flags & AUDIO_OUTPUT_FLAG_MAIN) && (flags & AUDIO_OUTPUT_FLAG_ASSOCIATED)) {
        if (is_main_active_l(qap_mod) || is_dual_main_active_l(qap_mod)) {
            ERROR_MSG("Dual Main or Main already active. So, Cannot open main and associated stream");
            return -EINVAL;
        } else {
            input_config.flags = QAP_MODULE_FLAG_PRIMARY;
            status = qap_module_init(qap_mod->session_handle, &input_config, &out->qap_stream_handle);
            if (QAP_STATUS_OK != status) {
                ERROR_MSG("Unable to open QAP stream/module with QAP_MODULE_FLAG_PRIMARY flag %d", status);
                return -EINVAL;
                } else
                    DEBUG_MSG("QAP_MODULE_FLAG_PRIMARY, module opened successfully 0x%x", (int)out->qap_stream_handle);;

            qap_mod->stream_in[QAP_IN_MAIN] = out;
        }
    } else if ((flags & AUDIO_OUTPUT_FLAG_MAIN) || ((!(flags & AUDIO_OUTPUT_FLAG_MAIN)) && (!(flags & AUDIO_OUTPUT_FLAG_ASSOCIATED)))) {
        /* Assume Main if no flag is set */
        if (is_dual_main_active_l(qap_mod)) {
            ERROR_MSG("Dual Main already active. So, Cannot open main stream");
            return -EINVAL;
        } else if (is_main_active_l(qap_mod) && qap_mod->stream_in[QAP_IN_ASSOC]) {
            ERROR_MSG("Main and Associated already active. So, Cannot open main stream");
            return -EINVAL;
        } else if (is_main_active_l(qap_mod) && (mmtype != MS12)) {
            ERROR_MSG("Main already active and Not an MS12 format. So, Cannot open another main stream");
            return -EINVAL;
        } else {
            input_config.flags = QAP_MODULE_FLAG_PRIMARY;
            status = qap_module_init(qap_mod->session_handle, &input_config, &out->qap_stream_handle);
            if (QAP_STATUS_OK != status) {
                ERROR_MSG("Unable to open QAP stream/module with QAP_MODULE_FLAG_PRIMARY flag %d", status);
                return -EINVAL;
            } else
                DEBUG_MSG("QAP_MODULE_FLAG_PRIMARY, module opened successfully 0x%x", (int)out->qap_stream_handle);

            if(qap_mod->stream_in[QAP_IN_MAIN]) {
                qap_mod->stream_in[QAP_IN_MAIN_2] = out;
            } else {
                qap_mod->stream_in[QAP_IN_MAIN] = out;
            }
        }
    } else if ((flags & AUDIO_OUTPUT_FLAG_ASSOCIATED)) {
        if (is_dual_main_active_l(qap_mod)) {
            ERROR_MSG("Dual Main already active. So, Cannot open associated stream");
            return -EINVAL;
        } else if (!is_main_active_l(qap_mod)) {
            ERROR_MSG("Main not active. So, Cannot open associated stream");
            return -EINVAL;
        } else if (qap_mod->stream_in[QAP_IN_ASSOC]) {
            ERROR_MSG("Associated already active. So, Cannot open associated stream");
            return -EINVAL;
        }
        input_config.flags = QAP_MODULE_FLAG_SECONDARY;
        status = qap_module_init(qap_mod->session_handle, &input_config, &out->qap_stream_handle);
        if (QAP_STATUS_OK != status) {
            ERROR_MSG("Unable to open QAP stream/module with QAP_MODULE_FLAG_SECONDARY flag %d", status);
            return -EINVAL;
        } else
            DEBUG_MSG("QAP_MODULE_FLAG_SECONDARY, module opened successfully 0x%x", (int)out->qap_stream_handle);

        qap_mod->stream_in[QAP_IN_ASSOC] = out;
    }

    if (out->qap_stream_handle) {
        status = qap_module_set_callback(out->qap_stream_handle, &qap_module_callback, out);
        if (QAP_STATUS_OK != status) {
            ERROR_MSG("Unable to register module callback %d", status);
            return -EINVAL;
        } else
            DEBUG_MSG("Module call back registered 0x%x cookie 0x%x", (int)out->qap_stream_handle, (int)out);
    }

    if (status != 0) {
        //If no stream is active then close the session.
        qap_sess_close(qap_mod);
        return 0;
    }

    //If Device is HDMI, QAP passthrough is enabled and there is no previous QAP passthrough input stream.
    if ((!p_qap->passthrough_in)
        && (devices & AUDIO_DEVICE_OUT_AUX_DIGITAL)
        && audio_extn_qap_passthrough_enabled(out)) {
        //Assign the QAP passthrough input stream.
        p_qap->passthrough_in = out;

        //If HDMI is connected and format is supported by HDMI then create QAP passthrough output stream.
        if (p_qap->hdmi_connect
            && platform_is_edid_supported_format(p_qap->adev->platform, out->format)) {
            status = create_qap_passthrough_stream_l();
            if (status < 0) {
                qap_stream_close(out);
                ERROR_MSG("QAP passthrough stream creation failed with error %d", status);
                return status;
            }
        }
        /*Else: since QAP passthrough input stream is already initialized,
         * when hdmi is connected
         * then qap passthrough output stream will be created.
         */
    }

    DEBUG_MSG();
    return status;
}

static int qap_out_resume(struct audio_stream_out* stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    int status = 0;
    DEBUG_MSG("Output Stream %p", out);


    lock_output_stream_l(out);

    //If QAP passthrough is active then block the resume on module input streams.
    if (p_qap->passthrough_out) {
        //If resume is received for the QAP passthrough stream then call the primary HAL api.
        pthread_mutex_lock(&p_qap->lock);
        if (p_qap->passthrough_in == out) {
            status = p_qap->passthrough_out->stream.resume(
                    (struct audio_stream_out*)p_qap->passthrough_out);
            if (!status) out->offload_state = OFFLOAD_STATE_PLAYING;
        }
        pthread_mutex_unlock(&p_qap->lock);
    } else {
        //Flush the module input stream.
        status = qap_stream_start_l(out);
    }

    unlock_output_stream_l(out);

    DEBUG_MSG();
    return status;
}

static int qap_out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct str_parms *parms;
    char value[32];
    int val = 0;
    struct stream_out *out = (struct stream_out *)stream;
    int ret = 0;
    int err = 0;
    struct qap_module *qap_mod = NULL;
    char *address = "";

    DEBUG_MSG("usecase(%d: %s) kvpairs: %s", out->usecase, use_case_table[out->usecase], kvpairs);

    parms = str_parms_create_str(kvpairs);
    err = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (err < 0)
        return err;
    val = atoi(value);

    qap_mod = get_qap_module_for_input_stream_l(out);
    if (!qap_mod) return (-EINVAL);

    //TODO: HDMI is connected but user doesn't want HDMI output, close both HDMI outputs.

    /* Setting new device information to the mm module input streams.
     * This is needed if QAP module output streams are not created yet.
     */
    reassign_device_list(&out->device_list, val, address);

#ifndef SPLIT_A2DP_ENABLED
    if (val == AUDIO_DEVICE_OUT_BLUETOOTH_A2DP) {
        //If device is BT then open the BT stream if not already opened.
        if ( audio_extn_bt_hal_get_output_stream(qap_mod->bt_hdl) == NULL
             && audio_extn_bt_hal_get_device(qap_mod->bt_hdl) != NULL) {
            ret = audio_extn_bt_hal_open_output_stream(qap_mod->bt_hdl,
                                                       QAP_OUTPUT_SAMPLING_RATE,
                                                       AUDIO_CHANNEL_OUT_STEREO,
                                                       CODEC_BACKEND_DEFAULT_BIT_WIDTH);
            if (ret != 0) {
                ERROR_MSG("BT Output stream open failure!");
            }
        }
    } else if (val != 0) {
        //If device is not BT then close the BT stream if already opened.
        if ( audio_extn_bt_hal_get_output_stream(qap_mod->bt_hdl) != NULL) {
            audio_extn_bt_hal_close_output_stream(qap_mod->bt_hdl);
        }
    }
#endif

    if (p_qap->passthrough_in == out) { //Device routing is received for QAP passthrough stream.

        if (!(val & AUDIO_DEVICE_OUT_AUX_DIGITAL)) { //HDMI route is disabled.

            //If QAP pasthrough output is enabled. Close it.
            close_qap_passthrough_stream_l();

            //Send the routing information to mm module pcm output.
            if (qap_mod->stream_out[QAP_OUT_OFFLOAD]) {
                ret = qap_mod->stream_out[QAP_OUT_OFFLOAD]->stream.common.set_parameters(
                        (struct audio_stream *)qap_mod->stream_out[QAP_OUT_OFFLOAD], kvpairs);
            }
            //else: device info is updated in the input streams.
        } else { //HDMI route is enabled.

            //create the QAf passthrough stream, if not created already.
            ret = create_qap_passthrough_stream_l();

            if (p_qap->passthrough_out != NULL) { //If QAP passthrough out is enabled then send routing information.
                ret = p_qap->passthrough_out->stream.common.set_parameters(
                        (struct audio_stream *)p_qap->passthrough_out, kvpairs);
            }
        }
    } else {
        //Send the routing information to mm module pcm output.
        if (qap_mod->stream_out[QAP_OUT_OFFLOAD]) {
            ret = qap_mod->stream_out[QAP_OUT_OFFLOAD]->stream.common.set_parameters(
                    (struct audio_stream *)qap_mod->stream_out[QAP_OUT_OFFLOAD], kvpairs);
        }
        //else: device info is updated in the input streams.
    }
    str_parms_destroy(parms);

    return ret;
}

/* Checks if a stream is QAP stream or not. */
bool audio_extn_is_qap_stream(struct stream_out *out)
{
    struct qap_module *qap_mod = get_qap_module_for_input_stream_l(out);

    if (qap_mod) {
        return true;
    }
    return false;
}

#if 0
/* API to send playback stream specific config parameters */
int audio_extn_qap_out_set_param_data(struct stream_out *out,
                                       audio_extn_param_id param_id,
                                       audio_extn_param_payload *payload)
{
    int ret = -EINVAL;
    int index;
    struct stream_out *new_out = NULL;
    struct audio_adsp_event *adsp_event;
    struct qap_module *qap_mod = get_qap_module_for_input_stream_l(out);

    if (!out || !qap_mod || !payload) {
        ERROR_MSG("Invalid Param");
        return ret;
    }

    /* apply param for all active out sessions */
    for (index = 0; index < MAX_QAP_MODULE_OUT; index++) {
        new_out = qap_mod->stream_out[index];
        if (!new_out) continue;

        /*ADSP event is not supported for passthrough*/
        if ((param_id == AUDIO_EXTN_PARAM_ADSP_STREAM_CMD)
            && !(new_out->flags == AUDIO_OUTPUT_FLAG_DIRECT)) continue;
        if (new_out->standby)
            new_out->stream.write((struct audio_stream_out *)new_out, NULL, 0);
        lock_output_stream_l(new_out);
        ret = audio_extn_out_set_param_data(new_out, param_id, payload);
        if (ret)
            ERROR_MSG("audio_extn_out_set_param_data error %d", ret);
        unlock_output_stream_l(new_out);
    }
    return ret;
}

int audio_extn_qap_out_get_param_data(struct stream_out *out,
                             audio_extn_param_id param_id,
                             audio_extn_param_payload *payload)
{
    int ret = -EINVAL, i;
    struct stream_out *new_out = NULL;
    struct qap_module *qap_mod = get_qap_module_for_input_stream_l(out);

    if (!out || !qap_mod || !payload) {
        ERROR_MSG("Invalid Param");
        return ret;
    }

    if (!p_qap->hdmi_connect) {
        ERROR_MSG("hdmi not connected");
        return ret;
    }

    /* get session which is routed to hdmi*/
    if (p_qap->passthrough_out)
        new_out = p_qap->passthrough_out;
    else {
        for (i = 0; i < MAX_QAP_MODULE_OUT; i++) {
            if (qap_mod->stream_out[i]) {
                new_out = qap_mod->stream_out[i];
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

    lock_output_stream_l(new_out);
    ret = audio_extn_out_get_param_data(new_out, param_id, payload);
    if (ret)
        ERROR_MSG("audio_extn_out_get_param_data error %d", ret);
    unlock_output_stream_l(new_out);

    return ret;
}
#endif

int audio_extn_qap_open_output_stream(struct audio_hw_device *dev,
                                      audio_io_handle_t handle,
                                      audio_devices_t devices,
                                      audio_output_flags_t flags,
                                      struct audio_config *config,
                                      struct audio_stream_out **stream_out,
                                      const char *address)
{
    int ret = 0;
    struct stream_out *out;

    DEBUG_MSG("Entry");
    ret = adev_open_output_stream(dev, handle, devices, flags, config, stream_out, address);
    if (*stream_out == NULL) {
        ERROR_MSG("Stream open failed %d", ret);
        return ret;
    }

#ifndef LINUX_ENABLED
//Bypass QAP for dummy PCM session opened by APM during boot time
    if(flags == 0) {
        ALOGD("bypassing QAP for flags is equal to none");
        return ret;
    }
#endif

    out = (struct stream_out *)*stream_out;

    DEBUG_MSG("%s 0x%x", use_case_table[out->usecase], (int)out);

    ret = qap_stream_open(out, config, flags, devices);
    if (ret < 0) {
        ERROR_MSG("Error opening QAP stream err[%d]", ret);
        //Stream not supported by QAP, Bypass QAP.
        return 0;
    }

    /* Override function pointers based on qap definitions */
    out->stream.set_volume = qap_out_set_volume;
    out->stream.pause = qap_out_pause;
    out->stream.resume = qap_out_resume;
    out->stream.drain = qap_out_drain;
    out->stream.flush = qap_out_flush;

    out->stream.common.standby = qap_out_standby;
    out->stream.common.set_parameters = qap_out_set_parameters;
    out->stream.get_latency = qap_out_get_latency;
    out->stream.get_render_position = qap_out_get_render_position;
    out->stream.write = qap_out_write;
    out->stream.get_presentation_position = qap_out_get_presentation_position;
    out->platform_latency = 0;

    /*TODO: Need to handle this for DTS*/
    if (out->usecase == USECASE_AUDIO_PLAYBACK_LOW_LATENCY) {
        out->usecase = USECASE_AUDIO_PLAYBACK_DEEP_BUFFER;
        out->config.period_size = QAP_DEEP_BUFFER_OUTPUT_PERIOD_SIZE;
        out->config.period_count = DEEP_BUFFER_OUTPUT_PERIOD_COUNT;
        out->config.start_threshold = QAP_DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4;
        out->config.avail_min = QAP_DEEP_BUFFER_OUTPUT_PERIOD_SIZE / 4;
    } else if(out->flags == AUDIO_OUTPUT_FLAG_DIRECT) {
        out->compr_config.fragment_size = qap_get_pcm_offload_input_buffer_size(&(config->offload_info));
    }

    *stream_out = &out->stream;

    DEBUG_MSG("Exit");
    return 0;
}

void audio_extn_qap_close_output_stream(struct audio_hw_device *dev,
                                        struct audio_stream_out *stream)
{
    struct stream_out *out = (struct stream_out *)stream;
    struct qap_module* qap_mod = get_qap_module_for_input_stream_l(out);

    DEBUG_MSG("%s 0x%x", use_case_table[out->usecase], (int)out);

    if (!qap_mod) {
        DEBUG_MSG("qap module is NULL, nothing to close");
        /*closing non-MS12/default output stream opened with qap */
        adev_close_output_stream(dev, stream);
        return;
    }

    DEBUG_MSG("stream_handle(%p) format = %x", out, out->format);

    //If close is received for QAP passthrough stream then close the QAP passthrough output.
    if (p_qap->passthrough_in == out) {
        if (p_qap->passthrough_out) {
            ALOGD("%s %d closing stream handle %p", __func__, __LINE__, p_qap->passthrough_out);
            pthread_mutex_lock(&p_qap->lock);
            adev_close_output_stream((struct audio_hw_device *)p_qap->adev,
                                     (struct audio_stream_out *)(p_qap->passthrough_out));
            pthread_mutex_unlock(&p_qap->lock);
            p_qap->passthrough_out = NULL;
        }

        p_qap->passthrough_in = NULL;
    }

    qap_stream_close(out);

    adev_close_output_stream(dev, stream);

    DEBUG_MSG("Exit");
}

/* Check if QAP is supported or not. */
bool audio_extn_qap_is_enabled()
{
    bool prop_enabled = false;
    char value[PROPERTY_VALUE_MAX] = {0};
    property_get("vendor.audio.qap.enabled", value, NULL);
    prop_enabled = atoi(value) || !strncmp("true", value, 4);
    return (prop_enabled);
}

/* QAP set parameter function. For Device connect and disconnect. */
int audio_extn_qap_set_parameters(struct audio_device *adev, struct str_parms *parms)
{
    int status = 0, val = 0;
    qap_session_outputs_config_t *session_outputs_config = NULL;

    if (!p_qap) {
        return -EINVAL;
    }

    DEBUG_MSG("Entry");

    status = str_parms_get_int(parms, AUDIO_PARAMETER_DEVICE_CONNECT, &val);

    if ((status >= 0) && audio_is_output_device(val)) {
        if (val & AUDIO_DEVICE_OUT_AUX_DIGITAL) { //HDMI is connected.
            DEBUG_MSG("AUDIO_DEVICE_OUT_AUX_DIGITAL connected");
            p_qap->hdmi_connect = 1;
            p_qap->hdmi_sink_channels = 0;

            if (p_qap->passthrough_in) { //If QAP passthrough is already initialized.
                lock_output_stream_l(p_qap->passthrough_in);
                if (platform_is_edid_supported_format(adev->platform,
                                                      p_qap->passthrough_in->format)) {
                    //If passthrough format is supported by HDMI then create the QAP passthrough output if not created already.
                    create_qap_passthrough_stream_l();
                    //Ignoring the returned error, If error then QAP passthrough is disabled.
                } else {
                    //If passthrough format is not supported by HDMI then close the QAP passthrough output if already created.
                    close_qap_passthrough_stream_l();
                }
                unlock_output_stream_l(p_qap->passthrough_in);
            }

            qap_set_hdmi_configuration_to_module();

        } else if (val & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP) {
            DEBUG_MSG("AUDIO_DEVICE_OUT_BLUETOOTH_A2DP connected");
            p_qap->bt_connect = 1;
            qap_set_default_configuration_to_module();
#ifndef SPLIT_A2DP_ENABLED
            for (k = 0; k < MAX_MM_MODULE_TYPE; k++) {
                if (!p_qap->qap_mod[k].bt_hdl) {
                    DEBUG_MSG("Opening a2dp output...");
                    status = audio_extn_bt_hal_load(&p_qap->qap_mod[k].bt_hdl);
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
        DEBUG_MSG("AUDIO_DEVICE_OUT_AUX_DIGITAL disconnected");
        if (val & AUDIO_DEVICE_OUT_AUX_DIGITAL) {

            p_qap->hdmi_sink_channels = 0;

            p_qap->passthrough_enabled = 0;
            p_qap->mch_pcm_hdmi_enabled = 0;
            p_qap->hdmi_connect = 0;

            if (p_qap->qap_mod[MS12].session_handle)
                session_outputs_config = &p_qap->qap_mod[MS12].session_outputs_config;
            else if (p_qap->qap_mod[DTS_M8].session_handle)
                session_outputs_config = &p_qap->qap_mod[DTS_M8].session_outputs_config;
            else {
                DEBUG_MSG("HDMI disconnection comes even before session is setup");
                return 0;
            }

            session_outputs_config->num_output = 1;

            session_outputs_config->output_config[0].id = AUDIO_DEVICE_OUT_SPEAKER;
            session_outputs_config->output_config[0].format = QAP_AUDIO_FORMAT_PCM_16_BIT;


            if (p_qap->qap_mod[MS12].session_handle) {
                DEBUG_MSG(" Enabling speaker(PCM out) from MS12 wrapper outputid = %x", session_outputs_config->output_config[0].id);
                status = qap_session_cmd(p_qap->qap_mod[MS12].session_handle,
                                    QAP_SESSION_CMD_SET_OUTPUTS,
                                    sizeof(qap_session_outputs_config_t),
                                    session_outputs_config,
                                    NULL,
                                    NULL);
                if (QAP_STATUS_OK != status) {
                    ERROR_MSG("Unable to register AUDIO_DEVICE_OUT_SPEAKER device with QAP %d",status);
                    return -EINVAL;
                }
            }
            if (p_qap->qap_mod[DTS_M8].session_handle) {
                status = qap_session_cmd(p_qap->qap_mod[MS12].session_handle,
                                    QAP_SESSION_CMD_SET_OUTPUTS,
                                    sizeof(qap_session_outputs_config_t),
                                    session_outputs_config,
                                    NULL,
                                    NULL);
                if (QAP_STATUS_OK != status) {
                    ERROR_MSG("Unable to register AUDIO_DEVICE_OUT_SPEAKER device with QAP %d", status);
                    return -EINVAL;
                }
            }

            close_all_hdmi_output_l();
            close_qap_passthrough_stream_l();
        } else if (val & AUDIO_DEVICE_OUT_BLUETOOTH_A2DP) {
            DEBUG_MSG("AUDIO_DEVICE_OUT_BLUETOOTH_A2DP disconnected");
            p_qap->bt_connect = 0;
            //reconfig HDMI as end device (if connected)
            if(p_qap->hdmi_connect)
                qap_set_hdmi_configuration_to_module();
#ifndef SPLIT_A2DP_ENABLED
            DEBUG_MSG("Closing a2dp output...");
            for (k = 0; k < MAX_MM_MODULE_TYPE; k++) {
                if (p_qap->qap_mod[k].bt_hdl) {
                    audio_extn_bt_hal_unload(p_qap->qap_mod[k].bt_hdl);
                    p_qap->qap_mod[k].bt_hdl = NULL;
                }
            }
#endif
        }
        //TODO else if: Need to consider other devices.
    }

#if 0
    /* does this need to be ported to QAP?*/
    for (k = 0; k < MAX_MM_MODULE_TYPE; k++) {
        kv_parirs = str_parms_to_str(parms);
        if (p_qap->qap_mod[k].session_handle) {
            p_qap->qap_mod[k].qap_audio_session_set_param(
                    p_qap->qap_mod[k].session_handle, kv_parirs);
        }
    }
#endif

    DEBUG_MSG("Exit");
    return status;
}

/* Create the QAP. */
int audio_extn_qap_init(struct audio_device *adev)
{
    DEBUG_MSG("Entry");

    p_qap = calloc(1, sizeof(struct qap));
    if (p_qap == NULL) {
        ERROR_MSG("Out of memory");
        return -ENOMEM;
    }

    p_qap->adev = adev;

    if (property_get_bool("vendor.audio.qap.msmd", false)) {
        DEBUG_MSG("MSMD enabled.");
        p_qap->qap_msmd_enabled = 1;
    }

    if (property_get_bool("vendor.audio.qap.output.block.handling", false)) {
        DEBUG_MSG("out put thread blocking handling enabled.");
        p_qap->qap_output_block_handling = 1;
    }
    pthread_mutex_init(&p_qap->lock, (const pthread_mutexattr_t *) NULL);

    int i = 0;

    for (i = 0; i < MAX_MM_MODULE_TYPE; i++) {
        char value[PROPERTY_VALUE_MAX] = {0};
        char lib_name[PROPERTY_VALUE_MAX] = {0};
        struct qap_module *qap_mod = &(p_qap->qap_mod[i]);

        if (i == MS12) {
            property_get("vendor.audio.qap.library", value, NULL);
            snprintf(lib_name, PROPERTY_VALUE_MAX, "%s", value);

            DEBUG_MSG("Opening Ms12 library at %s", lib_name);
           qap_mod->qap_lib = ( void *) qap_load_library(lib_name);
            if (qap_mod->qap_lib == NULL) {
                ERROR_MSG("qap load lib failed for MS12 %s", lib_name);
                continue;
            }
            DEBUG_MSG("Loaded QAP lib at %s", lib_name);
            pthread_mutex_init(&qap_mod->session_output_lock, (const pthread_mutexattr_t *) NULL);
            pthread_cond_init(&qap_mod->session_output_cond, (const pthread_condattr_t *)NULL);
        } else if (i == DTS_M8) {
            property_get("vendor.audio.qap.m8.library", value, NULL);
            snprintf(lib_name, PROPERTY_VALUE_MAX, "%s", value);
            qap_mod->qap_lib = dlopen(lib_name, RTLD_NOW);
            if (qap_mod->qap_lib == NULL) {
                ERROR_MSG("DLOPEN failed for DTS M8 %s", lib_name);
                continue;
            }
            DEBUG_MSG("DLOPEN successful for %s", lib_name);
            pthread_mutex_init(&qap_mod->session_output_lock, (const pthread_mutexattr_t *) NULL);
            pthread_cond_init(&qap_mod->session_output_cond, (const pthread_condattr_t *)NULL);
        } else {
            continue;
        }
    }

    DEBUG_MSG("Exit");
    return 0;
}

/* Tear down the qap extension. */
void audio_extn_qap_deinit()
{
    int i;
    DEBUG_MSG("Entry");
    char value[PROPERTY_VALUE_MAX] = {0};
    char lib_name[PROPERTY_VALUE_MAX] = {0};

    if (p_qap != NULL) {
        for (i = 0; i < MAX_MM_MODULE_TYPE; i++) {
            if (p_qap->qap_mod[i].session_handle != NULL)
                qap_sess_close(&p_qap->qap_mod[i]);

            if (p_qap->qap_mod[i].qap_lib != NULL) {
                if (i == MS12) {
                    property_get("vendor.audio.qap.library", value, NULL);
                    snprintf(lib_name, PROPERTY_VALUE_MAX, "%s", value);
                    DEBUG_MSG("lib_name %s", lib_name);
                    if (QAP_STATUS_OK != qap_unload_library(p_qap->qap_mod[i].qap_lib))
                        ERROR_MSG("Failed to unload MS12 library lib name %s", lib_name);
                    else
                        DEBUG_MSG("closed/unloaded QAP lib at %s", lib_name);
                    p_qap->qap_mod[i].qap_lib = NULL;
                } else {
                    dlclose(p_qap->qap_mod[i].qap_lib);
                    p_qap->qap_mod[i].qap_lib = NULL;
                }
                pthread_mutex_destroy(&p_qap->qap_mod[i].session_output_lock);
                pthread_cond_destroy(&p_qap->qap_mod[i].session_output_cond);
            }
        }

        if (p_qap->passthrough_out) {
            adev_close_output_stream((struct audio_hw_device *)p_qap->adev,
                                     (struct audio_stream_out *)(p_qap->passthrough_out));
            p_qap->passthrough_out = NULL;
        }

        pthread_mutex_destroy(&p_qap->lock);
        free(p_qap);
        p_qap = NULL;
    }
    DEBUG_MSG("Exit");
}
