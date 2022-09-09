/*
 * Copyright (c) 2015-2017, 2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
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

#define LOG_TAG "volume_listener"
//#define LOG_NDEBUG 0
#include <stdlib.h>
#include <dlfcn.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>

#include <cutils/list.h>
#include <log/log.h>
#include <hardware/audio_effect.h>
#include <cutils/properties.h>
#include <platform_api.h>

#define PRIMARY_HAL_PATH XSTR(LIB_AUDIO_HAL)
#define XSTR(x) STR(x)
#define STR(x) #x

#define VOL_FLAG ( EFFECT_FLAG_TYPE_INSERT | \
                   EFFECT_FLAG_VOLUME_IND | \
                   EFFECT_FLAG_DEVICE_IND | \
                   EFFECT_FLAG_OFFLOAD_SUPPORTED | \
                   EFFECT_FLAG_NO_PROCESS)

#define PRINT_STREAM_TYPE(i) ALOGV("descriptor found and is of stream type %s ",\
                                                            i == MUSIC?"MUSIC": \
                                                            i == RING?"RING": \
                                                            i == ALARM?"ALARM": \
                                                            i == VC_CALL?"Voice_call": \
                                                            i == NOTIFICATION?"Notification":\
                                                            "--INVALID--"); \

#define MAX_GAIN_LEVELS 5

#define AHAL_GAIN_DEPENDENT_INTERFACE_FUNCTION "audio_hw_send_gain_dep_calibration"
#define AHAL_GAIN_GET_MAPPING_TABLE "audio_hw_get_gain_level_mapping"
#define DEFAULT_CAL_STEP 0

#ifdef AUDIO_FEATURE_ENABLED_GCOV
extern void  __gcov_flush();
static void enable_gcov()
{
    __gcov_flush();
}
#else
static void enable_gcov()
{
}
#endif

enum {
    VOL_LISTENER_STATE_UNINITIALIZED,
    VOL_LISTENER_STATE_INITIALIZED,
    VOL_LISTENER_STATE_ACTIVE,
};

typedef struct vol_listener_context_s vol_listener_context_t;
static const struct effect_interface_s effect_interface;

/* flag to avoid multiple initialization */
static bool initialized = false;

/* current gain dep cal level that was pushed succesfully */
static int current_gain_dep_cal_level = -1;

enum STREAM_TYPE {
    MUSIC,
    RING,
    ALARM,
    VC_CALL,
    NOTIFICATION,
    MAX_STREAM_TYPES,
};

struct vol_listener_context_s {
    const struct effect_interface_s *itfe;
    struct listnode effect_list_node;
    effect_config_t config;
    const effect_descriptor_t *desc;
    uint32_t stream_type;
    uint32_t session_id;
    uint32_t state;
    uint32_t dev_id;
    float left_vol;
    float right_vol;
};

/* volume listener, music UUID: 08b8b058-0590-11e5-ac71-0025b32654a0 */
const effect_descriptor_t vol_listener_music_descriptor = {
    { 0x08b8b058, 0x0590, 0x11e5, 0xac71, { 0x00, 0x25, 0xb3, 0x26, 0x54, 0xa0 } },  // type
    { 0x08b8b058, 0x0590, 0x11e5, 0xac71, { 0x00, 0x25, 0xb3, 0x26, 0x54, 0xa0 } },  // uuid
    EFFECT_CONTROL_API_VERSION,
    VOL_FLAG,
    0, /* TODO */
    1,
    "Volume listener for Music",
    "Qualcomm Technologies Inc.",
};

/* volume listener, ring UUID: 0956df94-0590-11e5-bdbe-0025b32654a0 */
const effect_descriptor_t vol_listener_ring_descriptor = {
    { 0x0956df94, 0x0590, 0x11e5, 0xbdbe, { 0x00, 0x25, 0xb3, 0x26, 0x54, 0xa0 } }, // type
    { 0x0956df94, 0x0590, 0x11e5, 0xbdbe, { 0x00, 0x25, 0xb3, 0x26, 0x54, 0xa0 } }, // uuid
    EFFECT_CONTROL_API_VERSION,
    VOL_FLAG,
    0, /* TODO */
    1,
    "Volume listener for ring",
    "Qualcomm Technologies Inc",
};

/* volume listener, alarm UUID: 09f303e2-0590-11e5-8fdb-0025b32654a0 */
const effect_descriptor_t vol_listener_alarm_descriptor = {
    { 0x09f303e2, 0x0590, 0x11e5, 0x8fdb, { 0x00, 0x25, 0xb3, 0x26, 0x54, 0xa0 } }, // type
    { 0x09f303e2, 0x0590, 0x11e5, 0x8fdb, { 0x00, 0x25, 0xb3, 0x26, 0x54, 0xa0 } }, // uuid
    EFFECT_CONTROL_API_VERSION,
    VOL_FLAG,
    0, /* TODO */
    1,
    "Volume listener for alarm",
    "Qualcomm Technologies Inc",
};

/* volume listener, voice call UUID: 0ace5c08-0590-11e5-ae9e-0025b32654a0 */
const effect_descriptor_t vol_listener_voice_call_descriptor = {
    { 0x0ace5c08, 0x0590, 0x11e5, 0xae9e, { 0x00, 0x25, 0xb3, 0x26, 0x54, 0xa0 } }, // type
    { 0x0ace5c08, 0x0590, 0x11e5, 0xae9e, { 0x00, 0x25, 0xb3, 0x26, 0x54, 0xa0 } }, // uuid
    EFFECT_CONTROL_API_VERSION,
    VOL_FLAG,
    0, /* TODO */
    1,
    "Volume listener for voice call",
    "Qualcomm Technologies Inc",
};

/* volume listener, notification UUID: 0b776dde-0590-11e5-81ba-0025b32654a0 */
const effect_descriptor_t vol_listener_notification_descriptor = {
    { 0x0b776dde, 0x0590, 0x11e5, 0x81ba, { 0x00, 0x25, 0xb3, 0x26, 0x54, 0xa0 } }, // type
    { 0x0b776dde, 0x0590, 0x11e5, 0x81ba, { 0x00, 0x25, 0xb3, 0x26, 0x54, 0xa0 } }, // uuid
    EFFECT_CONTROL_API_VERSION,
    VOL_FLAG,
    0, /* TODO */
    1,
    "Volume listener for notification",
    "Qualcomm Technologies Inc",
};

static int total_volume_cal_step = MAX_GAIN_LEVELS;

// using gain level for non-drc volume curve
struct amp_db_and_gain_table  volume_curve_gain_mapping_table[MAX_VOLUME_CAL_STEPS] = {
    /* Level 0 in the calibration database contains default calibration */
    { 0.001774, -55, 5 },
    { 0.501187,  -6, 4 },
    { 0.630957,  -4, 3 },
    { 0.794328,  -2, 2 },
    { 1.0,        0, 1 },
    { 0, 0, -1 },
    { 0, 0, -1 },
    { 0, 0, -1 },
    { 0, 0, -1 },
    { 0, 0, -1 },
    { 0, 0, -1 },
    { 0, 0, -1 },
    { 0, 0, -1 },
    { 0, 0, -1 },
    { 0, 0, -1 },
};

static const effect_descriptor_t *descriptors[] = {
    &vol_listener_music_descriptor,
    &vol_listener_ring_descriptor,
    &vol_listener_alarm_descriptor,
    &vol_listener_voice_call_descriptor,
    &vol_listener_notification_descriptor,
    NULL,
};

pthread_once_t once = PTHREAD_ONCE_INIT;
/* flag to indicate if init was success */
static int init_status;

/* current volume level for which gain dep cal level was selected */
static float current_vol = 0.0;

/* HAL interface to send calibration */
static bool (*send_gain_dep_cal)(int);

static int (*get_custom_gain_table)(struct amp_db_and_gain_table *, int);

/* if dumping allowed */
static bool dumping_enabled = false;

/* list of created effects. */
struct listnode vol_effect_list;

/* lock must be held when modifying or accessing created_effects_list */
pthread_mutex_t vol_listner_init_lock;

static bool headset_cal_enabled;

/*
 *  Local functions
 */
static bool verify_context(vol_listener_context_t *context)
{
    if (context->stream_type == VC_CALL &&
        headset_cal_enabled &&
        (context->dev_id == AUDIO_DEVICE_OUT_EARPIECE ||
        context->dev_id == AUDIO_DEVICE_OUT_WIRED_HEADSET ||
        context->dev_id == AUDIO_DEVICE_OUT_WIRED_HEADPHONE))
        return true;
    else {
        if (context->dev_id & AUDIO_DEVICE_OUT_SPEAKER ||
            context->dev_id & AUDIO_DEVICE_OUT_SPEAKER_SAFE)
            return true;
        else
            return false;
    }
}

static void dump_list_l()
{
    struct listnode *node;
    vol_listener_context_t *context;

    ALOGW("DUMP_START :: ===========");

    list_for_each(node, &vol_effect_list) {
        context = node_to_item(node, struct vol_listener_context_s, effect_list_node);
        // dump stream_type / Device / session_id / left / righ volume
        ALOGW("%s: streamType [%s] Device [%d] state [%d] sessionID [%d] volume (L/R) [%f / %f] ",
        __func__,
        context->stream_type == MUSIC ? "MUSIC" :
        context->stream_type == RING ? "RING" :
        context->stream_type == ALARM ? "ALARM" :
        context->stream_type == VC_CALL ? "VC_CALL" :
        context->stream_type == NOTIFICATION ? "NOTIFICATION" : "--INVALID--",
        context->dev_id, context->state, context->session_id, context->left_vol,context->right_vol);
    }

    ALOGW("DUMP_END :: ===========");
}

static void check_and_set_gain_dep_cal()
{
    // iterate through list and make decision to set new gain dep cal level for speaker device
    // 1. find all usecase active on speaker
    // 2. find energy sum for each usecase
    // 3. find the highest of all the active usecase
    // 4. if new value is different than the current value then load new calibration

    struct listnode *node = NULL;
    float new_vol = -1.0, sum_energy = 0.0, temp_vol = 0.0;
    bool sum_energy_used = false;
    int max_level = 0;
    vol_listener_context_t *context = NULL;
    if (dumping_enabled) {
        dump_list_l();
    }

    ALOGV("%s ==> Start ...", __func__);

    // compute energy sum for the active speaker device (pick loudest of both channels)
    list_for_each(node, &vol_effect_list) {
        context = node_to_item(node, struct vol_listener_context_s, effect_list_node);
        if ((context->state == VOL_LISTENER_STATE_ACTIVE) &&
            verify_context(context)) {
            sum_energy_used = true;
            temp_vol = fmax(context->left_vol, context->right_vol);
            sum_energy += temp_vol * temp_vol;
        }
    }

    if (sum_energy_used) {
        new_vol = fmin(sqrt(sum_energy), 1.0);
    }

    if (new_vol != current_vol) {
        ALOGV("%s:: Change in decision :: current volume is %f new volume is %f",
              __func__, current_vol, new_vol);

        if (send_gain_dep_cal != NULL) {
            // send Gain dep cal level
            int gain_dep_cal_level = -1;

            if (new_vol >= 1 && total_volume_cal_step > 0) { // max amplitude, use highest DRC level
                gain_dep_cal_level = volume_curve_gain_mapping_table[total_volume_cal_step - 1].level;
            } else if (new_vol == -1) {
                gain_dep_cal_level = DEFAULT_CAL_STEP;
            } else if (new_vol == 0) {
                gain_dep_cal_level = volume_curve_gain_mapping_table[0].level;
            } else {
                for (max_level = 0; max_level + 1 < total_volume_cal_step; max_level++) {
                    if (new_vol < volume_curve_gain_mapping_table[max_level + 1].amp &&
                        new_vol >= volume_curve_gain_mapping_table[max_level].amp) {
                        gain_dep_cal_level = volume_curve_gain_mapping_table[max_level].level;
                        ALOGV("%s: volume(%f), gain dep cal selcetd %d ",
                              __func__, current_vol, gain_dep_cal_level);
                        break;
                    }
                }
            }

            // check here if previous gain dep cal level was not same
            if (gain_dep_cal_level != -1) {
                if (gain_dep_cal_level != current_gain_dep_cal_level) {
                    // decision made .. send new level now
                    if (!send_gain_dep_cal(gain_dep_cal_level)) {
                        ALOGE("%s: Failed to set gain dep cal level", __func__);
                    } else {
                        // Success in setting the gain dep cal level, store new level and Volume
                        if (dumping_enabled) {
                            ALOGW("%s: (old/new) Volume (%f/%f) (old/new) level (%d/%d)",
                                  __func__, current_vol, new_vol, current_gain_dep_cal_level,
                                  gain_dep_cal_level);
                        } else {
                            ALOGV("%s: Change in Cal::(old/new) Volume (%f/%f) (old/new) level (%d/%d)",
                                  __func__, current_vol, new_vol, current_gain_dep_cal_level,
                                  gain_dep_cal_level);
                        }
                        current_gain_dep_cal_level = gain_dep_cal_level;
                        current_vol = new_vol;
                    }
                } else {
                    if (dumping_enabled) {
                        ALOGW("%s: volume changed but gain dep cal level is still the same: (old/new) Volume (%f/%f) (old/new) level (%d)",
                              __func__,current_vol, new_vol, current_gain_dep_cal_level);
                    } else {
                        ALOGV("%s: volume changed but gain dep cal level is still the same",
                              __func__);
                    }
                }
            } else {
                ALOGW("%s: Failed to find gain dep cal level for volume %f", __func__, new_vol);
            }
        } else {
            ALOGE("%s: not able to send calibration, NULL function pointer",
                  __func__);
        }
    } else {
        ALOGV("%s:: volume not changed, stick to same config ..... ", __func__);
    }

    ALOGV("check_and_set_gain_dep_cal ==> End ");
}

/*
 * Effect Control Interface Implementation
 */

static inline int16_t clamp16(int32_t sample)
{
    if ((sample>>15) ^ (sample>>31))
        sample = 0x7FFF ^ (sample>>31);
    return sample;
}

static int vol_effect_process(effect_handle_t self,
                              audio_buffer_t *in_buffer,
                              audio_buffer_t *out_buffer)
{
    int status = 0;
    ALOGV("%s Called ", __func__);

    vol_listener_context_t *context = (vol_listener_context_t *)self;
    pthread_mutex_lock(&vol_listner_init_lock);

    if (context->state != VOL_LISTENER_STATE_ACTIVE) {
        ALOGE("%s: state is not active .. return error", __func__);
        status = -EINVAL;
        goto exit;
    }

    // calculation based on channel count 2
    if (in_buffer->raw != out_buffer->raw) {
        if (context->config.outputCfg.accessMode == EFFECT_BUFFER_ACCESS_ACCUMULATE) {
            size_t i;
            for (i = 0; i < out_buffer->frameCount*2; i++) {
                out_buffer->s16[i] = clamp16(out_buffer->s16[i] + in_buffer->s16[i]);
            }
        } else {
            memcpy(out_buffer->raw, in_buffer->raw, out_buffer->frameCount * 2 * sizeof(int16_t));
        }
    } else {
        ALOGV("%s: something wrong, didn't handle in_buffer and out_buffer same address case",
              __func__);
    }

exit:
    pthread_mutex_unlock(&vol_listner_init_lock);
    return status;
}


static int vol_effect_command(effect_handle_t self,
                              uint32_t cmd_code, uint32_t cmd_size,
                              void *p_cmd_data, uint32_t *reply_size,
                              void *p_reply_data)
{
    vol_listener_context_t *context = (vol_listener_context_t *)self;
    int status = 0;

    ALOGV("%s Called ", __func__);
    pthread_mutex_lock(&vol_listner_init_lock);

    if (context == NULL || context->state == VOL_LISTENER_STATE_UNINITIALIZED) {
        ALOGE("%s: %s is NULL", __func__, (context == NULL) ?
              "context" : "context->state");
        status = -EINVAL;
        goto exit;
    }

    switch (cmd_code) {
    case EFFECT_CMD_INIT:
        ALOGV("%s :: cmd called EFFECT_CMD_INIT", __func__);
        if (p_reply_data == NULL || *reply_size != sizeof(int)) {
            ALOGE("%s: EFFECT_CMD_INIT: %s, sending -EINVAL", __func__,
                  (p_reply_data == NULL) ? "p_reply_data is NULL" :
                  "*reply_size != sizeof(int)");
            android_errorWriteLog(0x534e4554, "32669549");
            status = -EINVAL;
            goto exit;
        }
        *(int *)p_reply_data = 0;
        break;

    case EFFECT_CMD_SET_CONFIG:
        ALOGV("%s :: cmd called EFFECT_CMD_SET_CONFIG", __func__);
        if (p_cmd_data == NULL || cmd_size != sizeof(effect_config_t)
                || p_reply_data == NULL || reply_size == NULL || *reply_size != sizeof(int)) {
            android_errorWriteLog(0x534e4554, "32669549");
            status = -EINVAL;
            goto exit;
        }
        context->config = *(effect_config_t *)p_cmd_data;
        *(int *)p_reply_data = 0;
        break;

    case EFFECT_CMD_GET_CONFIG:
        ALOGV("%s :: cmd called EFFECT_CMD_GET_CONFIG", __func__);
        break;

    case EFFECT_CMD_RESET:
        ALOGV("%s :: cmd called EFFECT_CMD_RESET", __func__);
        break;

    case EFFECT_CMD_SET_AUDIO_MODE:
        ALOGV("%s :: cmd called EFFECT_CMD_SET_AUDIO_MODE", __func__);
        break;

    case EFFECT_CMD_OFFLOAD:
        ALOGV("%s :: cmd called EFFECT_CMD_OFFLOAD", __func__);
        if (p_reply_data == NULL || *reply_size != sizeof(int)) {
            ALOGE("%s: EFFECT_CMD_OFFLOAD: %s, sending -EINVAL", __func__,
                  (p_reply_data == NULL) ? "p_reply_data is NULL" :
                  "*reply_size != sizeof(int)");
            android_errorWriteLog(0x534e4554, "32669549");
            status = -EINVAL;
            goto exit;
        }
        *(int *)p_reply_data = 0;
        break;

    case EFFECT_CMD_ENABLE:
        ALOGV("%s :: cmd called EFFECT_CMD_ENABLE", __func__);
        if (p_reply_data == NULL || *reply_size != sizeof(int)) {
            ALOGE("%s: EFFECT_CMD_ENABLE: %s, sending -EINVAL", __func__,
                   (p_reply_data == NULL) ? "p_reply_data is NULL" :
                   "*reply_size != sizeof(int)");
            status = -EINVAL;
            goto exit;
        }

        if (context->state != VOL_LISTENER_STATE_INITIALIZED) {
            ALOGE("%s: EFFECT_CMD_ENABLE : state not INITIALIZED", __func__);
            status = -ENOSYS;
            goto exit;
        }

        context->state = VOL_LISTENER_STATE_ACTIVE;
        *(int *)p_reply_data = 0;

        // After changing the state and if device is speaker
        // recalculate gain dep cal level
        if (verify_context(context)) {
            check_and_set_gain_dep_cal();
        }

        break;

    case EFFECT_CMD_DISABLE:
        ALOGV("%s :: cmd called EFFECT_CMD_DISABLE", __func__);
        if (p_reply_data == NULL || *reply_size != sizeof(int)) {
            ALOGE("%s: EFFECT_CMD_DISABLE: %s, sending -EINVAL", __func__,
                  (p_reply_data == NULL) ? "p_reply_data is NULL" :
                  "*reply_size != sizeof(int)");
            status = -EINVAL;
            goto exit;
        }

        if (context->state != VOL_LISTENER_STATE_ACTIVE) {
            ALOGE("%s: EFFECT_CMD_ENABLE : state not ACTIVE", __func__);
            status = -ENOSYS;
            goto exit;
        }

        context->state = VOL_LISTENER_STATE_INITIALIZED;
        *(int *)p_reply_data = 0;

        // After changing the state and if device is speaker
        // recalculate gain dep cal level
        if (verify_context(context)) {
            check_and_set_gain_dep_cal();
        }

        break;

    case EFFECT_CMD_GET_PARAM:
        ALOGV("%s :: cmd called EFFECT_CMD_GET_PARAM", __func__);
        break;

    case EFFECT_CMD_SET_PARAM:
        ALOGV("%s :: cmd called EFFECT_CMD_SET_PARAM", __func__);
        break;

    case EFFECT_CMD_SET_DEVICE:
        {
            uint32_t new_device;
            bool recompute_gain_dep_cal_Level = false;
            ALOGV("cmd called EFFECT_CMD_SET_DEVICE ");

            if (p_cmd_data == NULL || cmd_size < sizeof(uint32_t)) {
                ALOGE("%s: EFFECT_CMD_SET_DEVICE: cmd data NULL", __func__);
                status = -EINVAL;
                goto exit;
            }

            new_device = *(uint32_t *)p_cmd_data;
            ALOGV("%s :: EFFECT_CMD_SET_DEVICE: (current/new) device (0x%x / 0x%x)",
                   __func__, context->dev_id, new_device);

            // check if old or new device is speaker for playback usecase
            if (verify_context(context) ||
                new_device & AUDIO_DEVICE_OUT_SPEAKER ||
                new_device & AUDIO_DEVICE_OUT_SPEAKER_SAFE)
                recompute_gain_dep_cal_Level = true;

            context->dev_id = new_device;

            if (recompute_gain_dep_cal_Level) {
                check_and_set_gain_dep_cal();
            }
        }
        break;

    case EFFECT_CMD_SET_VOLUME:
        {
            float left_vol = 0, right_vol = 0;
            bool recompute_gain_dep_cal_Level = false;

            ALOGV("cmd called EFFECT_CMD_SET_VOLUME");
            if (p_cmd_data == NULL || cmd_size != 2 * sizeof(uint32_t)) {
                ALOGE("%s: EFFECT_CMD_SET_VOLUME: %s", __func__, (p_cmd_data == NULL) ?
                      "p_cmd_data is NULL" : "cmd_size issue");
                status = -EINVAL;
                goto exit;
            }

            if (verify_context(context)) {
                recompute_gain_dep_cal_Level = true;
            }

            left_vol = (float)(*(uint32_t *)p_cmd_data) / (1 << 24);
            right_vol = (float)(*((uint32_t *)p_cmd_data + 1)) / (1 << 24);
            ALOGV("Current Volume (%f / %f ) new Volume (%f / %f)", context->left_vol,
                  context->right_vol, left_vol, right_vol);

            context->left_vol = left_vol;
            context->right_vol = right_vol;

            // recompute gan dep cal level only if volume changed on speaker device
            if (recompute_gain_dep_cal_Level) {
                check_and_set_gain_dep_cal();
            }
        }
        break;

    default:
        ALOGW("volume_listener_command invalid command %d", cmd_code);
        status = -ENOSYS;
        break;
    }

exit:
    pthread_mutex_unlock(&vol_listner_init_lock);
    return status;
}

/* Effect Control Interface Implementation: get_descriptor */
static int vol_effect_get_descriptor(effect_handle_t   self,
                                     effect_descriptor_t *descriptor)
{
    vol_listener_context_t *context = (vol_listener_context_t *)self;
    ALOGV("%s Called ", __func__);

    if (descriptor == NULL) {
        ALOGE("%s: descriptor is NULL", __func__);
        return -EINVAL;
    }

    *descriptor = *context->desc;
    return 0;
}

static void init_once()
{
    int max_table_ent = 0;
    if (initialized) {
        ALOGV("%s : already init .. do nothing", __func__);
        return;
    }

    ALOGD("%s Called ", __func__);
    send_gain_dep_cal = NULL;
    get_custom_gain_table = NULL;

    pthread_mutex_init(&vol_listner_init_lock, NULL);

    // get hal function pointer
    if (access(PRIMARY_HAL_PATH, R_OK) == 0) {
        void *hal_lib_pointer = dlopen(PRIMARY_HAL_PATH, RTLD_NOW);
        if (hal_lib_pointer == NULL) {
            ALOGE("%s: DLOPEN failed for %s", __func__, PRIMARY_HAL_PATH);
        } else {
            ALOGV("%s: DLOPEN of %s Succes .. next get HAL entry function", __func__, PRIMARY_HAL_PATH);
            send_gain_dep_cal = (bool (*)(int))dlsym(hal_lib_pointer, AHAL_GAIN_DEPENDENT_INTERFACE_FUNCTION);
            if (send_gain_dep_cal == NULL) {
                ALOGE("Couldnt able to get the function symbol");
            }

            get_custom_gain_table = (int (*) (struct amp_db_and_gain_table *, int))
               dlsym(hal_lib_pointer, AHAL_GAIN_GET_MAPPING_TABLE);
            if (get_custom_gain_table == NULL) {
                ALOGE("Couldnt able to get the function AHAL_GAIN_GET_MAPPING_TABLE  symbol");
            } else {
                max_table_ent = get_custom_gain_table(volume_curve_gain_mapping_table,
                   MAX_VOLUME_CAL_STEPS);
                // if number of entries is 0 use default
                // if number of entries > MAX_VOLUME_CAL_STEPS (this should never happen)
                // then in this case
                // use only default number of steps but this will result in
                // unexpected behaviour

                if (max_table_ent > 0 && max_table_ent <= MAX_VOLUME_CAL_STEPS) {
                    if (max_table_ent < total_volume_cal_step) {
                        for (int i = max_table_ent; i < total_volume_cal_step; i++ ) {
                            volume_curve_gain_mapping_table[i].amp = 0;
                            volume_curve_gain_mapping_table[i].db = 0;
                            volume_curve_gain_mapping_table[i].level = -1;
                        }
                    }
                    total_volume_cal_step = max_table_ent;
                    ALOGD("%s: using custome volume table", __func__);
                } else {
                    ALOGD("%s: using default volume table", __func__);
                }

                if (dumping_enabled) {
                    ALOGD("%s: dumping table here .. size of table received %d",
                           __func__, max_table_ent);
                    for (int i = 0; i < MAX_VOLUME_CAL_STEPS ; i++)
                        ALOGD("[%d]  %f %f %d", i, volume_curve_gain_mapping_table[i].amp,
                                                   volume_curve_gain_mapping_table[i].db,
                                                   volume_curve_gain_mapping_table[i].level);
                }
            }
        }
    } else {
        ALOGE("%s: not able to acces lib %s ", __func__, PRIMARY_HAL_PATH);
    }

    // check system property to see if dumping is required
    char check_dump_val[PROPERTY_VALUE_MAX];
    property_get("vendor.audio.volume.listener.dump", check_dump_val, "0");
    if (atoi(check_dump_val))
        dumping_enabled = true;

    headset_cal_enabled = property_get_bool(
                            "vendor.audio.volume.headset.gain.depcal", false);

    init_status = 0;
    list_init(&vol_effect_list);
    initialized = true;
}

static int lib_init()
{
    pthread_once(&once, init_once);
    ALOGV("%s Called ", __func__);
    return init_status;
}

static int vol_prc_lib_create(const effect_uuid_t *uuid,
                              int32_t session_id,
                              int32_t io_id __unused,
                              effect_handle_t *p_handle)
{
    int itt = 0;
    vol_listener_context_t *context = NULL;

    ALOGV("volume_prc_lib_create .. called ..");

    if (lib_init() != 0) {
        return init_status;
    }

    if (p_handle == NULL || uuid == NULL) {
        ALOGE("%s: %s is NULL", __func__, (p_handle == NULL) ? "p_handle" : "uuid");
        return -EINVAL;
    }

    context = (vol_listener_context_t *)calloc(1, sizeof(vol_listener_context_t));

    if (context == NULL) {
        ALOGE("%s: failed to allocate for context .. oops !!", __func__);
        return -EINVAL;
    }

    // check if UUID is supported
    for (itt = 0; descriptors[itt] != NULL; itt++) {
        if (memcmp(uuid, &descriptors[itt]->uuid, sizeof(effect_uuid_t)) == 0) {
            // check if this correct .. very imp
            context->desc = descriptors[itt];
            context->stream_type = itt;
            PRINT_STREAM_TYPE(itt)
            break;
        }
    }

    if (descriptors[itt] == NULL) {
        ALOGE("%s .. couldnt find passed uuid, something wrong", __func__);
        free(context);
        return -EINVAL;
    }

    ALOGV("%s CREATED_CONTEXT %p", __func__, context);

    context->itfe = &effect_interface;
    context->state = VOL_LISTENER_STATE_INITIALIZED;
    context->dev_id = AUDIO_DEVICE_NONE;
    context->session_id = session_id;

    // Add this to master list
    pthread_mutex_lock(&vol_listner_init_lock);
    list_add_tail(&vol_effect_list, &context->effect_list_node);
    if (dumping_enabled) {
        dump_list_l();
    }
    pthread_mutex_unlock(&vol_listner_init_lock);

    *p_handle = (effect_handle_t)context;
    enable_gcov();
    return 0;
}

static int vol_prc_lib_release(effect_handle_t handle)
{
    struct listnode *node = NULL, *temp_node_next;
    vol_listener_context_t *context = NULL;
    vol_listener_context_t *recv_contex = (vol_listener_context_t *)handle;
    int status = -EINVAL;
    bool recompute_flag = false;
    int active_stream_count = 0;

    ALOGV("%s context %p", __func__, handle);

    if (recv_contex == NULL) {
        return status;
    }
    pthread_mutex_lock(&vol_listner_init_lock);

    // check if the handle/context provided is valid
    list_for_each_safe(node, temp_node_next, &vol_effect_list) {
        context = node_to_item(node, struct vol_listener_context_s, effect_list_node);
        if (context == recv_contex) {
            ALOGV("--- Found something to remove ---");
            PRINT_STREAM_TYPE(context->stream_type);
            if (verify_context(context)) {
                recompute_flag = true;
            }
            list_remove(&context->effect_list_node);
            free(context);
            status = 0;
            break;
        } else {
            ++active_stream_count;
        }
    }

    if (status != 0) {
        ALOGE("something wrong ... <<<--- Found NOTHING to remove ... ???? --->>>>>");
        pthread_mutex_unlock(&vol_listner_init_lock);
        return status;
    }

    // if there are no active streams, reset cal and volume level
    if (active_stream_count == 0) {
        current_gain_dep_cal_level = -1;
        current_vol = 0.0;
    }

    if (recompute_flag) {
        check_and_set_gain_dep_cal();
    }

    if (dumping_enabled) {
        dump_list_l();
    }
    pthread_mutex_unlock(&vol_listner_init_lock);
    enable_gcov();
    return status;
}

static int vol_prc_lib_get_descriptor(const effect_uuid_t *uuid,
                                      effect_descriptor_t *descriptor)
{
    int i = 0;
    ALOGV("%s Called ", __func__);
    if (lib_init() != 0) {
        return init_status;
    }

    if (descriptor == NULL || uuid == NULL) {
        ALOGE("%s: %s is NULL", __func__, (descriptor == NULL) ? "descriptor" : "uuid");
        return -EINVAL;
    }

    for (i = 0; descriptors[i] != NULL; i++) {
        if (memcmp(uuid, &descriptors[i]->uuid, sizeof(effect_uuid_t)) == 0) {
            *descriptor = *descriptors[i];
            return 0;
        }
    }

    ALOGE("%s: couldnt found uuid passed, oops", __func__);
    return  -EINVAL;
}


/* effect_handle_t interface implementation for volume listener effect */
static const struct effect_interface_s effect_interface = {
    vol_effect_process,
    vol_effect_command,
    vol_effect_get_descriptor,
    NULL,
};

__attribute__((visibility("default")))
audio_effect_library_t AUDIO_EFFECT_LIBRARY_INFO_SYM = {
    .tag = AUDIO_EFFECT_LIBRARY_TAG,
    .version = EFFECT_LIBRARY_API_VERSION,
    .name = "Volume Listener Effect Library",
    .implementor = "Qualcomm Technologies Inc.",
    .create_effect = vol_prc_lib_create,
    .release_effect = vol_prc_lib_release,
    .get_descriptor = vol_prc_lib_get_descriptor,
};
