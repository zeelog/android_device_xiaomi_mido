/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "audio_hw_ffv"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0
/*#define VERY_VERY_VERBOSE_LOGGING*/
#ifdef VERY_VERY_VERBOSE_LOGGING
#define ALOGVV ALOGV
#else
#define ALOGVV(a...) do { } while(0)
#endif

#include <errno.h>
#include <cutils/properties.h>
#include <cutils/str_parms.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <log/log.h>
#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>
#include <system/thread_defs.h>

#include "audio_hw.h"
#include "audio_extn.h"
#include "platform.h"
#include "platform_api.h"

#include "ffv_interface.h"

#define AUDIO_PARAMETER_FFV_MODE_ON "ffvOn"
#define AUDIO_PARAMETER_FFV_SPLIT_EC_REF_DATA "ffv_split_ec_ref_data"
#define AUDIO_PARAMETER_FFV_EC_REF_CHANNEL_COUNT "ffv_ec_ref_channel_count"
#define AUDIO_PARAMETER_FFV_EC_REF_DEVICE "ffv_ec_ref_dev"
#define AUDIO_PARAMETER_FFV_CHANNEL_INDEX "ffv_channel_index"


#define FFV_CONFIG_FILE_NAME "BF_1out.cfg"
#define FFV_LIB_NAME "libffv.so"

#define FFV_SAMPLING_RATE_16000 16000
#define FFV_EC_REF_LOOPBACK_DEVICE_MONO "ec-ref-loopback-mono"
#define FFV_EC_REF_LOOPBACK_DEVICE_STEREO "ec-ref-loopback-stereo"

#define FFV_CHANNEL_MODE_MONO 1
#define FFV_CHANNEL_MODE_STEREO 2
#define FFV_CHANNEL_MODE_QUAD 6
#define FFV_CHANNEL_MODE_HEX 6
#define FFV_CHANNEL_MODE_OCT 8

#define FFV_PCM_BUFFER_DURATION_MS 160
#define FFV_PCM_PERIOD_COUNT (8)
#define FFV_PCM_PERIOD_SIZE \
    ((((FFV_SAMPLING_RATE_16000 * FFV_PCM_BUFFER_DURATION_MS) \
       /(FFV_PCM_PERIOD_COUNT * 1000)) + 0x1f) & ~0x1f)

#define ALIGN(number, align) \
        ((number + align - 1) & ~(align - 1))
#define CALCULATE_PERIOD_SIZE(duration_ms, sample_rate, period_cnt, align) \
       (ALIGN(((sample_rate * duration_ms) /(period_cnt * 1000)), align))

#define FFV_PCM_MAX_RETRY 10
#define FFV_PCM_SLEEP_WAIT 1000

#define DLSYM(handle, name, err) \
do {\
    const char* error; \
    *(void**)&name##_fn = dlsym(handle, #name);\
    if ((error = dlerror())) {\
        ALOGE("%s: dlsym failed for %s error %s", __func__, #name, error);\
        err = -ENODEV;\
    }\
} while(0)\

/* uncomment to collect pcm dumps */
//#define FFV_PCM_DUMP

static FfvStatusType (*ffv_init_fn)(void** handle, int num_tx_in_ch,
    int num_out_ch, int num_ec_ref_ch, int frame_len, int sample_rate,
    const char *config_file_name, char *svaModelBuffer,
    uint32_t svaModelSize, int* totMemSize,
    int product_id, const char* prduct_license);
static void (*ffv_deinit_fn)(void* handle);
static void (*ffv_process_fn)(void *handle, const int16_t *in_pcm,
    int16_t *out_pcm, const int16_t *ec_ref_pcm);
static int (*ffv_read_fn)(void* handle, int16_t *buf_pcm,
    int max_buf_len);
static FfvStatusType (*ffv_get_param_fn)(void *handle, char *params_buffer_ptr,
    int param_id, int buffer_size, int *param_size_ptr);
static FfvStatusType (*ffv_set_param_fn)(void *handle, char *params_buffer_ptr,
    int param_id, int param_size);
static FfvStatusType (*ffv_register_event_callback_fn)(void *handle,
    ffv_event_callback_fn_t *fun_ptr);

struct ffvmodule {
    void *ffv_lib_handle;
    unsigned char *in_buf;
    unsigned int in_buf_size;
    unsigned char *ec_ref_buf;
    unsigned int ec_ref_buf_size;
    unsigned char *split_in_buf;
    unsigned int split_in_buf_size;
    unsigned char *out_buf;
    unsigned int out_buf_size;

    struct pcm_config capture_config;
    struct pcm_config out_config;
    struct pcm_config ec_ref_config;

    int ec_ref_pcm_id;
    struct pcm *ec_ref_pcm;
    int ec_ref_ch_cnt;
    audio_devices_t ec_ref_dev;
    bool split_ec_ref_data;

    bool is_ffv_enabled;
    bool buffers_allocated;
    struct stream_in *in;
    bool is_ffvmode_on;
    void *handle;
    pthread_mutex_t init_lock;
    bool capture_started;
    int target_ch_idx;

#ifdef FFV_PCM_DUMP
    FILE *fp_input;
    FILE *fp_ecref;
    FILE *fp_split_input;
    FILE *fp_output;
#endif
};

static struct ffvmodule ffvmod = {
    .ffv_lib_handle = NULL,
    .in_buf = NULL,
    .in_buf_size = 0,
    .ec_ref_buf = NULL,
    .ec_ref_buf_size = 0,
    .split_in_buf = NULL,
    .split_in_buf_size = 0,
    .out_buf = NULL,
    .out_buf_size = 0,

    .ec_ref_pcm = NULL,
    .ec_ref_ch_cnt = 1,
    .ec_ref_dev = AUDIO_DEVICE_OUT_SPEAKER,
    .is_ffv_enabled = false,
    .buffers_allocated = false,
    .in = NULL,
    .is_ffvmode_on = false,
    .handle = NULL,
    .capture_started = false,
    .target_ch_idx = -1,
};

static struct pcm_config ffv_pcm_config = {
    .channels = FFV_CHANNEL_MODE_MONO,
    .rate = FFV_SAMPLING_RATE_16000,
    .period_size = FFV_PCM_PERIOD_SIZE,
    .period_count = FFV_PCM_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

void audio_get_lib_path(char* lib_path, int path_size)
{
#ifdef LINUX_ENABLED
#ifdef __LP64__
    /* libs are stored in /usr/lib64 */
    snprintf(lib_path, path_size, "%s", "/usr/lib64");
#else
    /* libs are stored in /usr/lib */
    snprintf(lib_path, path_size, "%s", "/usr/lib");
#endif
#else
#ifdef __LP64__
    /* libs are stored in /vendor/lib64 */
    snprintf(lib_path, path_size, "%s", "/vendor/lib64");
#else
    /* libs are stored in /vendor/lib */
    snprintf(lib_path, path_size, "%s", "/vendor/lib");
#endif
#endif
}

static int32_t ffv_init_lib()
{
    int status = 0;
    char lib_path[VENDOR_CONFIG_PATH_MAX_LENGTH];
    char lib_file[VENDOR_CONFIG_FILE_MAX_LENGTH];

    /* Get path for lib in vendor */
    audio_get_lib_path(lib_path, sizeof(lib_path));

    /* Get path for ffv_lib_file */
    snprintf(lib_file, sizeof(lib_file), "%s/%s", lib_path, FFV_LIB_NAME);
    if (ffvmod.ffv_lib_handle) {
        ALOGE("%s: FFV library is already initialized", __func__);
        return 0;
    }

    ffvmod.ffv_lib_handle = dlopen(lib_file, RTLD_NOW);
    if (!ffvmod.ffv_lib_handle) {
        ALOGE("%s: Unable to open %s, error %s", __func__, lib_file,
            dlerror());
        status = -ENOENT;
        goto exit;
    }

    dlerror(); /* clear errors */
    DLSYM(ffvmod.ffv_lib_handle, ffv_init, status);
    if (status)
        goto exit;
    DLSYM(ffvmod.ffv_lib_handle, ffv_deinit, status);
    if (status)
        goto exit;
    DLSYM(ffvmod.ffv_lib_handle, ffv_process, status);
    if (status)
        goto exit;
    DLSYM(ffvmod.ffv_lib_handle, ffv_read, status);
    if (status)
        goto exit;
    DLSYM(ffvmod.ffv_lib_handle, ffv_get_param, status);
    if (status)
        goto exit;
    DLSYM(ffvmod.ffv_lib_handle, ffv_set_param, status);
    if (status)
        goto exit;
    DLSYM(ffvmod.ffv_lib_handle, ffv_register_event_callback, status);
    if (status)
        goto exit;

    return status;

exit:
    if (ffvmod.ffv_lib_handle)
        dlclose(ffvmod.ffv_lib_handle);
    ffvmod.ffv_lib_handle = NULL;

    return status;
}

static int deallocate_buffers()
{
    if (ffvmod.in_buf) {
        free(ffvmod.in_buf);
        ffvmod.in_buf = NULL;
    }

    if (ffvmod.split_in_buf) {
        free(ffvmod.split_in_buf);
        ffvmod.split_in_buf = NULL;
    }

    if (ffvmod.ec_ref_buf) {
        free(ffvmod.ec_ref_buf);
        ffvmod.ec_ref_buf = NULL;
    }

    if (ffvmod.out_buf) {
        free(ffvmod.out_buf);
        ffvmod.out_buf = NULL;
    }

    ffvmod.buffers_allocated = false;
    return 0;
}

static int allocate_buffers()
{
    int status = 0;

    /* in_buf - buffer read from capture session */
    ffvmod.in_buf_size = ffvmod.capture_config.period_size * ffvmod.capture_config.channels *
                              (pcm_format_to_bits(ffvmod.capture_config.format) >> 3);
    ffvmod.in_buf = (unsigned char *)calloc(1, ffvmod.in_buf_size);
    if (!ffvmod.in_buf) {
        ALOGE("%s: ERROR. Can not allocate in buffer size %d", __func__, ffvmod.in_buf_size);
        status = -ENOMEM;
        goto error_exit;
    }
    ALOGD("%s: Allocated in buffer size bytes =%d",
          __func__, ffvmod.in_buf_size);

    /* ec_buf - buffer read from ec ref capture session */
    ffvmod.ec_ref_buf_size = ffvmod.ec_ref_config.period_size * ffvmod.ec_ref_config.channels *
                              (pcm_format_to_bits(ffvmod.ec_ref_config.format) >> 3);
    ffvmod.ec_ref_buf = (unsigned char *)calloc(1, ffvmod.ec_ref_buf_size);
    if (!ffvmod.ec_ref_buf) {
        ALOGE("%s: ERROR. Can not allocate ec ref buffer size %d",
               __func__, ffvmod.ec_ref_buf_size);
        status = -ENOMEM;
        goto error_exit;
    }
    ALOGD("%s: Allocated ec ref buffer size bytes =%d",
          __func__, ffvmod.ec_ref_buf_size);

    if (ffvmod.split_ec_ref_data) {
        ffvmod.split_in_buf_size = ffvmod.in_buf_size - ffvmod.ec_ref_buf_size;
        ffvmod.split_in_buf = (unsigned char *)calloc(1, ffvmod.split_in_buf_size);
        if (!ffvmod.split_in_buf) {
            ALOGE("%s: ERROR. Can not allocate split in buffer size %d",
                   __func__, ffvmod.split_in_buf_size);
            status = -ENOMEM;
            goto error_exit;
        }
        ALOGD("%s: Allocated split in buffer size bytes =%d",
               __func__, ffvmod.split_in_buf_size);
    }

    /* out_buf - output buffer from FFV + SVA library */
    ffvmod.out_buf_size = ffvmod.out_config.period_size * ffvmod.out_config.channels *
                              (pcm_format_to_bits(ffvmod.out_config.format) >> 3);
    ffvmod.out_buf = (unsigned char *)calloc(1, ffvmod.out_buf_size);
    if (!ffvmod.out_buf) {
        ALOGE("%s: ERROR. Can not allocate out buffer size %d", __func__, ffvmod.out_buf_size);
        status = -ENOMEM;
        goto error_exit;
    }
    ALOGD("%s: Allocated out buffer size bytes =%d",
          __func__, ffvmod.out_buf_size);

    ffvmod.buffers_allocated = true;
    return 0;

error_exit:
    deallocate_buffers();
    return status;
}

void audio_extn_ffv_update_enabled()
{
    char ffv_enabled[PROPERTY_VALUE_MAX] = "false";

    property_get("ro.vendor.audio.sdk.ffv", ffv_enabled, "0");
    if (!strncmp("true", ffv_enabled, 4)) {
        ALOGD("%s: ffv is supported", __func__);
        ffvmod.is_ffv_enabled = true;
    } else {
        ALOGD("%s: ffv is not supported", __func__);
        ffvmod.is_ffv_enabled = false;
    }
}

bool audio_extn_ffv_get_enabled()
{
    ALOGV("%s: is_ffv_enabled:%d is_ffvmode_on:%d ", __func__, ffvmod.is_ffv_enabled, ffvmod.is_ffvmode_on);

    if(ffvmod.is_ffv_enabled && ffvmod.is_ffvmode_on)
        return true;

    return false;
}

bool  audio_extn_ffv_check_usecase(struct stream_in *in) {
    int ret = false;
    int channel_count = audio_channel_count_from_in_mask(in->channel_mask);
    audio_source_t source = in->source;

    if ((audio_extn_ffv_get_enabled()) &&
            (channel_count == 1) &&
            (AUDIO_SOURCE_MIC == source) &&
            (is_single_device_type_equal(&in->device_list, AUDIO_DEVICE_IN_BUILTIN_MIC) ||
             is_single_device_type_equal(&in->device_list, AUDIO_DEVICE_IN_BACK_MIC)) &&
            (in->format == AUDIO_FORMAT_PCM_16_BIT) &&
            (in->sample_rate == FFV_SAMPLING_RATE_16000)) {
        in->config.channels = channel_count;
        in->config.period_count = FFV_PCM_PERIOD_COUNT;
        in->config.period_size = FFV_PCM_PERIOD_SIZE;
        ALOGD("%s: FFV enabled", __func__);
        ret = true;
    }
    return ret;
}

int audio_extn_ffv_set_usecase(struct stream_in *in, int ffv_key, char* ffv_lic)
{
    int ret = -EINVAL;

    if (audio_extn_ffv_check_usecase(in)) {
        if (!audio_extn_ffv_stream_init(in, ffv_key, ffv_lic)) {
            ALOGD("%s: Created FFV session succesfully", __func__);
            ret = 0;
        } else {
            ALOGE("%s: Unable to start FFV record session", __func__);
        }
    }
    return ret;
}

struct stream_in *audio_extn_ffv_get_stream()
{
    return ffvmod.in;
}

void audio_extn_ffv_update_pcm_config(struct pcm_config *config)
{
    config->channels = ffvmod.capture_config.channels;
    config->period_count = ffvmod.capture_config.period_count;
    config->period_size = ffvmod.capture_config.period_size;
}

int32_t audio_extn_ffv_init(struct audio_device *adev __unused)
{
    int ret = 0;

    ret = ffv_init_lib();
    if (ret)
        ALOGE("%s: ERROR. ffv_init_lib ret %d", __func__, ret);

    pthread_mutex_init(&ffvmod.init_lock, NULL);
    return ret;
}

int32_t audio_extn_ffv_deinit()
{
    pthread_mutex_destroy(&ffvmod.init_lock);
    if (ffvmod.ffv_lib_handle) {
        dlclose(ffvmod.ffv_lib_handle);
        ffvmod.ffv_lib_handle = NULL;
    }
    return 0;
}

int32_t audio_extn_ffv_stream_init(struct stream_in *in, int key, char* lic)
{
    uint32_t ret = -EINVAL;
    int num_tx_in_ch, num_out_ch, num_ec_ref_ch;
    int frame_len;
    int sample_rate;
    const char *config_file_path;
    char vendor_config_path[VENDOR_CONFIG_PATH_MAX_LENGTH];
    char platform_info_xml_path_file[VENDOR_CONFIG_FILE_MAX_LENGTH];
    int total_mem_size;
    FfvStatusType status_type;
    const char *sm_buffer = "DISABLE_KEYWORD_DETECTION";
    ffv_target_channel_index_param_t ch_index_param;
    char *params_buffer_ptr = NULL;
    int param_size = 0;
    int param_id;

    audio_get_vendor_config_path(vendor_config_path, sizeof(vendor_config_path));
    /* Get path for ffv_config_file_name in vendor */
    snprintf(platform_info_xml_path_file, sizeof(platform_info_xml_path_file),
            "%s/%s", vendor_config_path, FFV_CONFIG_FILE_NAME);
    config_file_path = platform_info_xml_path_file;
    if (!audio_extn_ffv_get_enabled()) {
        ALOGE("Rejecting FFV -- init is called without enabling FFV");
        goto fail;
    }

    if (ffvmod.handle != NULL) {
        ALOGV("%s: reinitializing ffv library", __func__);
        audio_extn_ffv_stream_deinit();
    }

    ffvmod.capture_config = ffv_pcm_config;
    ffvmod.ec_ref_config = ffv_pcm_config;
    ffvmod.out_config = ffv_pcm_config;
    /* configure capture session with 6/8 channels */
    ffvmod.capture_config.channels = ffvmod.split_ec_ref_data ?
        FFV_CHANNEL_MODE_OCT : FFV_CHANNEL_MODE_HEX;
    ffvmod.capture_config.period_size =
                   CALCULATE_PERIOD_SIZE(FFV_PCM_BUFFER_DURATION_MS,
                                         ffvmod.capture_config.rate,
                                         FFV_PCM_PERIOD_COUNT, 32);

    /* Update channels with ec ref channel count */
    ffvmod.ec_ref_config.channels = ffvmod.ec_ref_ch_cnt;
    ffvmod.ec_ref_config.period_size =
               CALCULATE_PERIOD_SIZE(FFV_PCM_BUFFER_DURATION_MS,
                                     ffvmod.ec_ref_config.rate,
                                     FFV_PCM_PERIOD_COUNT, 32);
    ret = allocate_buffers();
    if (ret)
        goto fail;

    num_ec_ref_ch = ffvmod.ec_ref_config.channels;
    num_tx_in_ch = ffvmod.split_ec_ref_data ?
        (ffvmod.capture_config.channels - num_ec_ref_ch) :
        ffvmod.capture_config.channels;
    num_out_ch = ffvmod.out_config.channels;
    frame_len = ffvmod.capture_config.period_size;
    sample_rate = ffvmod.capture_config.rate;

    ALOGD("%s: ec_ref_ch %d, tx_in_ch %d, out_ch %d, frame_len %d, sample_rate %d",
           __func__, num_ec_ref_ch, num_tx_in_ch, num_out_ch, frame_len, sample_rate);
    ALOGD("%s: config file path %s", __func__, config_file_path);
    status_type = ffv_init_fn(&ffvmod.handle, num_tx_in_ch, num_out_ch, num_ec_ref_ch,
                      frame_len, sample_rate, config_file_path, (char *)sm_buffer, 0,
                      &total_mem_size, key, lic);
    if (status_type) {
        ALOGE("%s: ERROR. ffv_init returned %d", __func__, status_type);
        ret = -EINVAL;
        goto fail;
    }
    ALOGD("%s: ffv_init success %p", __func__, ffvmod.handle);

    /* set target channel index if received as part of setparams */
    if (ffvmod.target_ch_idx != -1) {
        ALOGD("%s: target channel index %d", __func__, ffvmod.target_ch_idx);
        ch_index_param.target_chan_idx = ffvmod.target_ch_idx;
        params_buffer_ptr = (char *)&ch_index_param;
        param_size = sizeof(ch_index_param);
        param_id = FFV_TARGET_CHANNEL_INDEX_PARAM;
        status_type = ffv_set_param_fn(ffvmod.handle, params_buffer_ptr,
                                       param_id, param_size);
        if (status_type) {
            ALOGE("%s: ERROR. ffv_set_param_fn ret %d", __func__, status_type);
            ret = -EINVAL;
            goto fail;
        }
    }

    ffvmod.in = in;
#ifdef RUN_KEEP_ALIVE_IN_ARM_FFV
    audio_extn_keep_alive_start(KEEP_ALIVE_OUT_PRIMARY);
#endif
#ifdef FFV_PCM_DUMP
    if (!ffvmod.fp_input) {
        ALOGD("%s: Opening input dump file \n", __func__);
        ffvmod.fp_input = fopen("/data/misc/audio/ffv_input.pcm", "wb");
    }
    if (!ffvmod.fp_ecref) {
        ALOGD("%s: Opening ecref dump file \n", __func__);
        ffvmod.fp_ecref = fopen("/data/misc/audio/ffv_ecref.pcm", "wb");
    }
    if (!ffvmod.fp_split_input && ffvmod.split_ec_ref_data) {
        ALOGD("%s: Opening split input dump file \n", __func__);
        ffvmod.fp_split_input = fopen("/data/misc/audio/ffv_split_input.pcm", "wb");
    }
    if (!ffvmod.fp_output) {
        ALOGD("%s: Opening output dump file \n", __func__);
        ffvmod.fp_output = fopen("/data/misc/audio/ffv_output.pcm", "wb");
    }
#endif
    ALOGV("%s: exit", __func__);
    return 0;

fail:
    audio_extn_ffv_stream_deinit();
    return ret;
}

int32_t audio_extn_ffv_stream_deinit()
{
    ALOGV("%s: entry", __func__);

#ifdef FFV_PCM_DUMP
    if (ffvmod.fp_input)
        fclose(ffvmod.fp_input);

    if (ffvmod.fp_ecref)
        fclose(ffvmod.fp_ecref);

    if (ffvmod.fp_split_input)
        fclose(ffvmod.fp_split_input);

    if (ffvmod.fp_output)
        fclose(ffvmod.fp_output);
#endif

    if (ffvmod.handle)
        ffv_deinit_fn(ffvmod.handle);

    if (ffvmod.buffers_allocated)
        deallocate_buffers();
#ifdef RUN_KEEP_ALIVE_IN_ARM_FFV
    audio_extn_keep_alive_stop(KEEP_ALIVE_OUT_PRIMARY);
#endif
    ffvmod.handle = NULL;
    ffvmod.in = NULL;
    ALOGV("%s: exit", __func__);
    return 0;
}

snd_device_t audio_extn_ffv_get_capture_snd_device()
{
    if (ffvmod.capture_config.channels == FFV_CHANNEL_MODE_OCT) {
        return SND_DEVICE_IN_HANDSET_8MIC;
    } else if (ffvmod.capture_config.channels == FFV_CHANNEL_MODE_HEX) {
        return SND_DEVICE_IN_HANDSET_6MIC;
    } else if (ffvmod.capture_config.channels == FFV_CHANNEL_MODE_QUAD) {
        return SND_DEVICE_IN_HANDSET_QMIC;
    } else {
        ALOGE("%s: Invalid channels configured for capture", __func__);
        return SND_DEVICE_NONE;
    }
}

int audio_extn_ffv_init_ec_ref_loopback(struct audio_device *adev,
                                        snd_device_t snd_device __unused)
{
    struct audio_usecase *uc_info_tx = NULL;
    snd_device_t in_snd_device;
    char *params_buffer_ptr = NULL;
    int param_id = FFV_RESET_AEC_PARAM;
    int param_size = 0;
    FfvStatusType status_type;
    int ret = 0;
    ffv_quadrx_use_dwnmix_param_t quad_downmix;

    ALOGV("%s: entry", __func__);
    /* notify library to reset AEC during each start */
    status_type = ffv_set_param_fn(ffvmod.handle, params_buffer_ptr,
                      param_id, param_size);
    if (status_type) {
        ALOGE("%s: ERROR. ffv_set_param_fn ret %d", __func__, status_type);
        return -EINVAL;
    }

    if (ffvmod.split_ec_ref_data) {
        ALOGV("%s: Ignore ec ref loopback init", __func__);
        return 0;
    }

    in_snd_device = platform_get_ec_ref_loopback_snd_device(ffvmod.ec_ref_ch_cnt);
    uc_info_tx = (struct audio_usecase *)calloc(1, sizeof(struct audio_usecase));
    if (!uc_info_tx) {
        return -ENOMEM;
    }

    if (in_snd_device == SND_DEVICE_IN_EC_REF_LOOPBACK_QUAD) {
        quad_downmix.quadrx_dwnmix_enable = true;
        ALOGD("%s: set param for 4 ch ec, handle %p", __func__, ffvmod.handle);
        status_type = ffv_set_param_fn(ffvmod.handle,
            (char *)&quad_downmix,
            FFV_QUADRX_USE_DWNMIX_PARAM,
            sizeof(ffv_quadrx_use_dwnmix_param_t));
        if (status_type) {
            ALOGE("%s: ERROR. ffv_set_param_fn for quad channel ec ref %d",
                __func__, status_type);
            return -EINVAL;
        }
    }

    pthread_mutex_lock(&ffvmod.init_lock);
    uc_info_tx->id = USECASE_AUDIO_EC_REF_LOOPBACK;
    uc_info_tx->type = PCM_CAPTURE;
    uc_info_tx->in_snd_device = in_snd_device;
    uc_info_tx->out_snd_device = SND_DEVICE_NONE;
    ffvmod.ec_ref_pcm = NULL;
    list_add_tail(&adev->usecase_list, &uc_info_tx->list);
    enable_snd_device(adev, in_snd_device);
    enable_audio_route(adev, uc_info_tx);

    ffvmod.ec_ref_pcm_id = platform_get_pcm_device_id(uc_info_tx->id, PCM_CAPTURE);
    if (ffvmod.ec_ref_pcm_id < 0) {
        ALOGE("%s: Invalid pcm device for usecase (%d)",
              __func__, uc_info_tx->id);
        ret = -ENODEV;
        goto exit;
    }

    ALOGV("%s: Opening PCM device card_id(%d) device_id(%d), channels %d format %d",
          __func__, adev->snd_card, ffvmod.ec_ref_pcm_id, ffvmod.ec_ref_config.channels,
          ffvmod.ec_ref_config.format);
    ffvmod.ec_ref_pcm = pcm_open(adev->snd_card,
                             ffvmod.ec_ref_pcm_id,
                             PCM_IN, &ffvmod.ec_ref_config);
    if (ffvmod.ec_ref_pcm && !pcm_is_ready(ffvmod.ec_ref_pcm)) {
        ALOGE("%s: %s", __func__, pcm_get_error(ffvmod.ec_ref_pcm));
        ret = -EIO;
        goto exit;
    }

    ALOGV("%s: pcm_prepare", __func__);
    if (pcm_prepare(ffvmod.ec_ref_pcm) < 0) {
        ALOGE("%s: pcm prepare for ec ref loopback failed", __func__);
        ret = -EINVAL;
    }

    ffvmod.capture_started = false;
    pthread_mutex_unlock(&ffvmod.init_lock);
    ALOGV("%s: exit", __func__);
    return 0;

exit:
    if (ffvmod.ec_ref_pcm) {
        pcm_close(ffvmod.ec_ref_pcm);
        ffvmod.ec_ref_pcm = NULL;
    }
    list_remove(&uc_info_tx->list);
    disable_snd_device(adev, in_snd_device);
    disable_audio_route(adev, uc_info_tx);
    free(uc_info_tx);
    pthread_mutex_unlock(&ffvmod.init_lock);
    return ret;
}

void audio_extn_ffv_append_ec_ref_dev_name(char *device_name)
{
    if (ffvmod.ec_ref_dev == AUDIO_DEVICE_OUT_LINE)
        strlcat(device_name, " lineout",  DEVICE_NAME_MAX_SIZE);
    ALOGD("%s: ec ref dev name %s", __func__, device_name);
}

int audio_extn_ffv_deinit_ec_ref_loopback(struct audio_device *adev,
                                          snd_device_t snd_device __unused)
{
    struct audio_usecase *uc_info_tx = NULL;
    snd_device_t in_snd_device;
    int ret = 0;

    ALOGV("%s: entry", __func__);
    if (ffvmod.split_ec_ref_data) {
        ALOGV("%s: Ignore ec ref loopback init", __func__);
        return 0;
    }

    in_snd_device = platform_get_ec_ref_loopback_snd_device(ffvmod.ec_ref_ch_cnt);
    uc_info_tx = get_usecase_from_list(adev, USECASE_AUDIO_EC_REF_LOOPBACK);
    pthread_mutex_lock(&ffvmod.init_lock);
    if (ffvmod.ec_ref_pcm) {
        pcm_close(ffvmod.ec_ref_pcm);
        ffvmod.ec_ref_pcm = NULL;
    }
    disable_snd_device(adev, in_snd_device);
    if (uc_info_tx) {
        list_remove(&uc_info_tx->list);
        disable_audio_route(adev, uc_info_tx);
        free(uc_info_tx);
    }
    pthread_mutex_unlock(&ffvmod.init_lock);
    ALOGV("%s: exit", __func__);
    return ret;
}

int32_t audio_extn_ffv_read(struct audio_stream_in *stream __unused,
                       void *buffer, size_t bytes)
{
    int status = 0;
    int16_t *in_ptr = NULL, *process_in_ptr = NULL, *process_out_ptr = NULL;
    int16_t *process_ec_ref_ptr = NULL;
    size_t in_buf_size, out_buf_size, bytes_to_copy;
    int retry_num = 0;
    int i, ch;
    int total_in_ch, in_ch, ec_ref_ch;

    if (!ffvmod.ffv_lib_handle) {
        ALOGE("%s: ffv_lib_handle not initialized", __func__);
        return -EINVAL;
    }

    if (!ffvmod.handle) {
        ALOGE("%s: ffv module handle not initialized", __func__);
        return -EINVAL;
    }

    if (!ffvmod.in || !ffvmod.in->pcm) {
        ALOGE("%s: capture session not initiliazed", __func__);
        return -EINVAL;
    }

    if (!ffvmod.split_ec_ref_data && !ffvmod.ec_ref_pcm) {
        ALOGE("%s: ec ref session not initiliazed", __func__);
        return -EINVAL;
    }

    if (!ffvmod.capture_started) {
        /* pcm_start of capture and ec ref session before read to reduce drift */
        pcm_start(ffvmod.in->pcm);
        while (status && (retry_num < FFV_PCM_MAX_RETRY)) {
            usleep(FFV_PCM_SLEEP_WAIT);
            retry_num++;
            ALOGI("%s: pcm_start retrying..status %d errno %d, retry cnt %d",
                   __func__, status, errno, retry_num);
            status = pcm_start(ffvmod.in->pcm);
        }
        if (status) {
            ALOGE("%s: ERROR. pcm_start failed, returned status %d - %s",
                  __func__, status, pcm_get_error(ffvmod.in->pcm));
            return status;
        }
        retry_num = 0;

        if (!ffvmod.split_ec_ref_data) {
            pcm_start(ffvmod.ec_ref_pcm);
            while (status && (retry_num < FFV_PCM_MAX_RETRY)) {
                usleep(FFV_PCM_SLEEP_WAIT);
                retry_num++;
                ALOGI("%s: pcm_start retrying..status %d errno %d, retry cnt %d",
                      __func__, status, errno, retry_num);
                status = pcm_start(ffvmod.ec_ref_pcm);
            }
            if (status) {
                ALOGE("%s: ERROR. pcm_start failed, returned status %d - %s",
                       __func__, status, pcm_get_error(ffvmod.ec_ref_pcm));
                return status;
            }
        }
        audio_extn_set_cpu_affinity();
        setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_AUDIO);
        ffvmod.capture_started = true;
    }

    ALOGVV("%s: pcm_read reading bytes=%d", __func__, ffvmod.in_buf_size);
    status = pcm_read(ffvmod.in->pcm, ffvmod.in_buf, ffvmod.in_buf_size);
    if (status) {
        ALOGE("%s: pcm read failed status %d - %s", __func__, status,
              pcm_get_error(ffvmod.in->pcm));
        goto exit;
    }
    ALOGVV("%s: pcm_read done", __func__);

    if (!ffvmod.split_ec_ref_data) {
        /* read EC ref data */
        ALOGVV("%s: ec ref pcm_read reading bytes=%d", __func__, ffvmod.ec_ref_buf_size);
        status = pcm_read(ffvmod.ec_ref_pcm, ffvmod.ec_ref_buf, ffvmod.ec_ref_buf_size);
        if (status) {
            ALOGE("%s: ec ref pcm read failed status %d - %s", __func__, status,
                   pcm_get_error(ffvmod.ec_ref_pcm));
            goto exit;
        }
        ALOGVV("%s: ec ref pcm_read done", __func__);
        process_in_ptr = (int16_t *)ffvmod.in_buf;
        process_ec_ref_ptr = (int16_t *)ffvmod.ec_ref_buf;
        in_buf_size = ffvmod.in_buf_size;
    } else {
        /* split input buffer into actual input channels and EC ref channels */
        in_ptr = (int16_t *)ffvmod.in_buf;
        process_in_ptr = (int16_t *)ffvmod.split_in_buf;
        process_ec_ref_ptr = (int16_t *)ffvmod.ec_ref_buf;
        total_in_ch = ffvmod.capture_config.channels;
        ec_ref_ch = ffvmod.ec_ref_config.channels;
        in_ch = total_in_ch - ec_ref_ch;
        for (i = 0; i < (int)ffvmod.capture_config.period_size; i++) {
            for (ch = 0; ch < in_ch; ch++) {
                process_in_ptr[i*in_ch+ch] =
                          in_ptr[i*total_in_ch+ch];
            }
            for (ch = 0; ch < ec_ref_ch; ch++) {
                process_ec_ref_ptr[i*ec_ref_ch+ch] =
                          in_ptr[i*total_in_ch+in_ch+ch];
            }
        }
        in_buf_size = ffvmod.split_in_buf_size;
    }
    process_out_ptr = (int16_t *)ffvmod.out_buf;

    ffv_process_fn(ffvmod.handle, process_in_ptr,
            process_out_ptr, process_ec_ref_ptr);
    out_buf_size = ffvmod.out_buf_size;
    bytes_to_copy = (bytes <= out_buf_size) ? bytes : out_buf_size;
    memcpy(buffer, process_out_ptr, bytes_to_copy);
    if (bytes_to_copy != out_buf_size)
        ALOGD("%s: out buffer data dropped, copied %zu bytes",
               __func__, bytes_to_copy);

#ifdef FFV_PCM_DUMP
    if (ffvmod.fp_input)
        fwrite(ffvmod.in_buf, 1, ffvmod.in_buf_size, ffvmod.fp_input);
    if (ffvmod.fp_ecref)
        fwrite(ffvmod.ec_ref_buf, 1, ffvmod.ec_ref_buf_size, ffvmod.fp_ecref);
    if (ffvmod.fp_split_input)
        fwrite(ffvmod.split_in_buf, 1, ffvmod.split_in_buf_size, ffvmod.fp_split_input);
    if (ffvmod.fp_output)
        fwrite(process_out_ptr, 1, bytes_to_copy, ffvmod.fp_output);
#endif

exit:
    return status;
}

void audio_extn_ffv_set_parameters(struct audio_device *adev __unused,
                                   struct str_parms *parms)
{
    int val;
    int ret = 0;
    char value[128];

    /* FFV params are required to be set before start of recording */
    if (!ffvmod.handle) {
        ret = str_parms_get_str(parms, AUDIO_PARAMETER_FFV_MODE_ON, value,
                                sizeof(value));
        if (ret >= 0) {
            str_parms_del(parms, AUDIO_PARAMETER_FFV_MODE_ON);
            if (strcmp(value, "true") == 0) {
                ALOGD("%s: Setting FFV mode to true", __func__);
                ffvmod.is_ffvmode_on = true;
            } else {
                ALOGD("%s: Resetting FFV mode to false", __func__);
                ffvmod.is_ffvmode_on = false;
            }
        }

        ret = str_parms_get_str(parms, AUDIO_PARAMETER_FFV_SPLIT_EC_REF_DATA, value,
                                sizeof(value));
        if (ret >= 0) {
            str_parms_del(parms, AUDIO_PARAMETER_FFV_SPLIT_EC_REF_DATA);
            if (strcmp(value, "true") == 0) {
                ALOGD("%s: ec ref is packed with mic captured data", __func__);
                ffvmod.split_ec_ref_data = true;
            } else {
                ALOGD("%s: ec ref is captured separately", __func__);
                ffvmod.split_ec_ref_data = false;
            }
        }
        ret = str_parms_get_int(parms, AUDIO_PARAMETER_FFV_EC_REF_CHANNEL_COUNT, &val);
        if (ret >= 0) {
            str_parms_del(parms, AUDIO_PARAMETER_FFV_EC_REF_CHANNEL_COUNT);
            if (val == 1) {
                ALOGD("%s: mono ec ref", __func__);
                ffvmod.ec_ref_ch_cnt = FFV_CHANNEL_MODE_MONO;
            } else if (val == 2) {
                ALOGD("%s: stereo ec ref", __func__);
                ffvmod.ec_ref_ch_cnt = FFV_CHANNEL_MODE_STEREO;
            } else {
                ALOGE("%s: Invalid ec ref", __func__);
            }
        }
        ret = -1;
        if (str_parms_get_int(parms, AUDIO_PARAMETER_FFV_EC_REF_DEVICE, &val) >= 0) {
            ret = 1;
            str_parms_del(parms, AUDIO_PARAMETER_FFV_EC_REF_DEVICE);
        } else if (str_parms_get_int(parms, AUDIO_PARAMETER_DEVICE_CONNECT, &val) >= 0) {
            ret = 1;
        }
        if (ret == 1) {
            if (val & AUDIO_DEVICE_OUT_SPEAKER) {
                ALOGD("%s: capture ec ref from speaker", __func__);
                ffvmod.ec_ref_dev = AUDIO_DEVICE_OUT_SPEAKER;
            } else if (val & AUDIO_DEVICE_OUT_LINE) {
                ALOGD("%s: capture ec ref from line out", __func__);
                ffvmod.ec_ref_dev = AUDIO_DEVICE_OUT_LINE;
            }
        }

        ret = str_parms_get_int(parms, AUDIO_PARAMETER_DEVICE_DISCONNECT, &val);
        if (ret >= 0) {
            if (val & AUDIO_DEVICE_OUT_LINE) {
                ALOGD("%s: capture ec ref from speaker", __func__);
                ffvmod.ec_ref_dev = AUDIO_DEVICE_OUT_SPEAKER;
            }
        }

        ret = str_parms_get_int(parms, AUDIO_PARAMETER_FFV_CHANNEL_INDEX, &val);
        if (ret >= 0) {
            str_parms_del(parms, AUDIO_PARAMETER_FFV_CHANNEL_INDEX);
            ALOGD("%s: set target chan index %d", __func__, val);
            ffvmod.target_ch_idx = val;
        }
    }
}
