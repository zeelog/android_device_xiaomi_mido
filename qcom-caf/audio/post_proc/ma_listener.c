/*
 * Copyright (C) 2018 The Android Open Source Project
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

#define LOG_TAG "ma_listener"
/*#define LOG_NDEBUG 0*/

#include <stdlib.h>
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <cutils/list.h>
#include <log/log.h>
#include <hardware/audio_effect.h>
#include <audio-base.h>

#define MA_FLAG ( EFFECT_FLAG_TYPE_INSERT | \
                   EFFECT_FLAG_VOLUME_MONITOR | \
                   EFFECT_FLAG_DEVICE_IND | \
                   EFFECT_FLAG_OFFLOAD_SUPPORTED | \
                   EFFECT_FLAG_NO_PROCESS)

#define PRINT_STREAM_TYPE(i) ALOGV("descriptor found and is of stream type %s ",\
                                                            i == AUDIO_STREAM_MUSIC ? "MUSIC": \
                                                            i == AUDIO_STREAM_RING ? "RING": \
                                                            i == AUDIO_STREAM_ALARM ? "ALARM": \
                                                            i == AUDIO_STREAM_VOICE_CALL ? "Voice_call": \
                                                            i == AUDIO_STREAM_SYSTEM ? "SYSTEM":       \
                                                            i == AUDIO_STREAM_NOTIFICATION ? "Notification":\
                                                            "--INVALID--"); \


#define MA_SET_STATE "audio_hw_send_qdsp_parameter"
#define HAL_VENDOR_PATH "/vendor/lib/hw"

enum {
    MA_LISTENER_STATE_UNINITIALIZED,
    MA_LISTENER_STATE_INITIALIZED,
    MA_LISTENER_STATE_ACTIVE,
};

typedef struct ma_listener_context_s ma_listener_context_t;
static const struct effect_interface_s effect_interface;

struct ma_state {
    float vol;
    bool active;
};

static const audio_stream_type_t MIN_STREAM_TYPES = AUDIO_STREAM_VOICE_CALL;
static const audio_stream_type_t MAX_STREAM_TYPES = AUDIO_STREAM_NOTIFICATION;
static struct ma_state g_cur_state[MAX_STREAM_TYPES + 1];

struct ma_listener_context_s {
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

/* voice UUID: 4ece09c2-3728-11e8-a9f9-fc4dd4486b6d */
const effect_descriptor_t ma_listener_voice_descriptor = {
    { 0x46f924ed, 0x25c3, 0x4272, 0x85b1, { 0x41, 0x78, 0x3f, 0x0d, 0xc6, 0x38 } },  // type
    { 0x4ece09c2, 0x3728, 0x11e8, 0xa9f9, { 0xfc, 0x4d, 0xd4, 0x48, 0x6b, 0x6d } },  // uuid
    EFFECT_CONTROL_API_VERSION,
    MA_FLAG,
    0, /* TODO */
    1,
    "MAXXAUDIO listener for Voice",
    "The Android Open Source Project",
};

/* system UUID: 4f705ff6-3728-11e8-a0c6-fc4dd4486b6d */
const effect_descriptor_t ma_listener_system_descriptor = {
    { 0x8bd0f979, 0x5266, 0x4791, 0x9213, { 0x11, 0x3a, 0xbc, 0xf7, 0xd3, 0xdc } },  // type
    { 0x4f705ff6, 0x3728, 0x11e8, 0xa0c6, { 0xfc, 0x4d, 0xd4, 0x48, 0x6b, 0x6d } },  // uuid
    EFFECT_CONTROL_API_VERSION,
    MA_FLAG,
    0, /* TODO */
    1,
    "MAXXAUDIO listener for System",
    "The Android Open Source Project",
};

/* ring UUID: 4fd6e5c8-3728-11e8-8303-fc4dd4486b6d */
const effect_descriptor_t ma_listener_ring_descriptor = {
    { 0x5380692a, 0x872e, 0x4697, 0x8e38, { 0xcd, 0xd1, 0x09, 0xf6, 0xcb, 0x87 } },  // type
    { 0x4fd6e5c8, 0x3728, 0x11e8, 0x8303, { 0xfc, 0x4d, 0xd4, 0x48, 0x6b, 0x6d } },  // uuid
    EFFECT_CONTROL_API_VERSION,
    MA_FLAG,
    0, /* TODO */
    1,
    "MAXXAUDIO listener for Ring",
    "The Android Open Source Project",
};
/* music UUID: 5036194e-3728-11e8-8db9-fc4dd4486b6d */
const effect_descriptor_t ma_listener_music_descriptor = {
    { 0x3a3a19b2, 0x62b1, 0x4785, 0xb55e, { 0xb2, 0x8f, 0xd4, 0x1b, 0x83, 0x58 } },  // type
    { 0x5036194e, 0x3728, 0x11e8, 0x8db9, { 0xfc, 0x4d, 0xd4, 0x48, 0x6b, 0x6d } },  // uuid
    EFFECT_CONTROL_API_VERSION,
    MA_FLAG,
    0, /* TODO */
    1,
    "MAXXAUDIO listener for Music",
    "The Android Open Source Project",
};
/* alarm UUID: 50b9f084-3728-11e8-9225-fc4dd4486b6d */
const effect_descriptor_t ma_listener_alarm_descriptor = {
    { 0x8d08d30f, 0xb4c3, 0x4600, 0x8f99, { 0xfc, 0xbb, 0x5d, 0x05, 0x8b, 0x60 } },  // type
    { 0x50b9f084, 0x3728, 0x11e8, 0x9225, { 0xfc, 0x4d, 0xd4, 0x48, 0x6b, 0x6d } },  // uuid
    EFFECT_CONTROL_API_VERSION,
    MA_FLAG,
    0, /* TODO */
    1,
    "MAXXAUDIO listener for Alarm",
    "The Android Open Source Project",
};
/* notification UUID: 50fe4d56-3728-11e8-ac73-fc4dd4486b6d */
const effect_descriptor_t ma_listener_notification_descriptor = {
    { 0x513d09f5, 0xae7f, 0x483d, 0x922a, { 0x5c, 0x72, 0xc5, 0xe5, 0x68, 0x4c } },  // type
    { 0x50fe4d56, 0x3728, 0x11e8, 0xac73, { 0xfc, 0x4d, 0xd4, 0x48, 0x6b, 0x6d } },  // uuid
    EFFECT_CONTROL_API_VERSION,
    MA_FLAG,
    0, /* TODO */
    1,
    "MAXXAUDIO listener for Notification",
    "The Android Open Source Project",
};

static const effect_descriptor_t *descriptors[] = {
    &ma_listener_voice_descriptor,
    &ma_listener_system_descriptor,
    &ma_listener_ring_descriptor,
    &ma_listener_music_descriptor,
    &ma_listener_alarm_descriptor,
    &ma_listener_notification_descriptor,
    NULL,
};

/* function of sending state to HAL */
static bool (*send_ma_parameter)(int, float, bool);

static int init_state = -1;
pthread_once_t once = PTHREAD_ONCE_INIT;

struct listnode ma_effect_list;
pthread_mutex_t ma_listner_init_lock;

/*
 *  Local functions
 */
static inline bool valid_dev_in_context(struct ma_listener_context_s *context)
{
    /* check device */
    if ((context->dev_id & AUDIO_DEVICE_OUT_SPEAKER) ||
        (context->dev_id & AUDIO_DEVICE_OUT_SPEAKER_SAFE) ||
        /* TODO: it should be dynamic if hybird split A2SP is implemented. */
        (context->dev_id & AUDIO_DEVICE_OUT_ALL_A2DP) ||
        (context->dev_id & AUDIO_DEVICE_OUT_ALL_USB)) {
        return true;
    }

    return false;
}

static void check_and_set_ma_parameter(uint32_t stream_type)
{
    bool active = false;
    float temp_vol = 0.0;
    float max_vol = 0.0;
    struct listnode *node = NULL;
    ma_listener_context_t *context = NULL;

    ALOGV("%s .. called ..", __func__);
    // get maximum volume for the active session with same strem type
    list_for_each(node, &ma_effect_list) {
        context = node_to_item(node, struct ma_listener_context_s, effect_list_node);
        if (context->stream_type == stream_type &&
            valid_dev_in_context(context)) {
            if (context->state == MA_LISTENER_STATE_ACTIVE) {
                active = true;
                temp_vol = fmax(context->left_vol, context->right_vol);
                if (max_vol < temp_vol)
                    max_vol = temp_vol;
                ALOGV("%s: check session(%d) volume(%f) for stream(%d)",
                       __func__, context->session_id, temp_vol, stream_type);
            }
        }
    }

    // check volume
    if (max_vol < 0.0) max_vol = 0;
    else if (max_vol > 1.0) max_vol = 1.0;

    if (send_ma_parameter != NULL &&
        stream_type >= MIN_STREAM_TYPES &&
        stream_type <= MAX_STREAM_TYPES &&
        (g_cur_state[stream_type].vol != max_vol ||
         g_cur_state[stream_type].active != active)) {

        ALOGV("%s: set stream(%d) active(%s->%s) volume(%f->%f)",
              __func__, stream_type,
              g_cur_state[stream_type].active ? "T" : "F", active ? "T" : "F",
              g_cur_state[stream_type].vol, max_vol);

        // update changes to hal
        send_ma_parameter(stream_type, max_vol, active);
        g_cur_state[stream_type].vol = max_vol;
        g_cur_state[stream_type].active = active;
    }

    return;
}

/*
 * Effect Control Interface Implementation
 */
static int ma_effect_command(effect_handle_t self,
                              uint32_t cmd_code, uint32_t cmd_size,
                              void *p_cmd_data, uint32_t *reply_size,
                              void *p_reply_data)
{
    ma_listener_context_t *context = (ma_listener_context_t *)self;
    int status = 0;

    ALOGV("%s .. called ..", __func__);
    pthread_mutex_lock(&ma_listner_init_lock);

    if (context == NULL || context->state == MA_LISTENER_STATE_UNINITIALIZED) {
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
            status = -EINVAL;
            goto exit;
        }
        *(int *)p_reply_data = 0;
        break;

    case EFFECT_CMD_SET_CONFIG:
        ALOGV("%s :: cmd called EFFECT_CMD_SET_CONFIG", __func__);
        if (p_cmd_data == NULL || cmd_size != sizeof(effect_config_t)
                || p_reply_data == NULL || reply_size == NULL || *reply_size != sizeof(int)) {
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

        if (context->state != MA_LISTENER_STATE_INITIALIZED) {
            ALOGE("%s: EFFECT_CMD_ENABLE : state not INITIALIZED", __func__);
            status = -ENOSYS;
            goto exit;
        }

        context->state = MA_LISTENER_STATE_ACTIVE;
        *(int *)p_reply_data = 0;

        // After changing the state and if device is valid
        // check and send state
        if (valid_dev_in_context(context)) {
            check_and_set_ma_parameter(context->stream_type);
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

        if (context->state != MA_LISTENER_STATE_ACTIVE) {
            ALOGE("%s: EFFECT_CMD_ENABLE : state not ACTIVE", __func__);
            status = -ENOSYS;
            goto exit;
        }

        context->state = MA_LISTENER_STATE_INITIALIZED;
        *(int *)p_reply_data = 0;

        // After changing the state and if device is valid
        // check and send state
        if (valid_dev_in_context(context)) {
            check_and_set_ma_parameter(context->stream_type);
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

        if (p_cmd_data == NULL) {
            ALOGE("%s: EFFECT_CMD_SET_DEVICE: cmd data NULL", __func__);
            status = -EINVAL;
            goto exit;
        }

        new_device = *(uint32_t *)p_cmd_data;
        ALOGV("%s :: EFFECT_CMD_SET_DEVICE: (current/new) device (0x%x / 0x%x)",
               __func__, context->dev_id, new_device);

        context->dev_id = new_device;
        // After changing the state and if device is valid
        // check and send parameter
        if (valid_dev_in_context(context)) {
            check_and_set_ma_parameter(context->stream_type);
        }
    }
    break;

    case EFFECT_CMD_SET_VOLUME:
    {
        float left_vol = 0, right_vol = 0;

        ALOGV("cmd called EFFECT_CMD_SET_VOLUME");
        if (p_cmd_data == NULL || cmd_size != 2 * sizeof(uint32_t)) {
            ALOGE("%s: EFFECT_CMD_SET_VOLUME: %s", __func__, (p_cmd_data == NULL) ?
                  "p_cmd_data is NULL" : "cmd_size issue");
            status = -EINVAL;
            goto exit;
        }

        left_vol = (float)(*(uint32_t *)p_cmd_data) / (1 << 24);
        right_vol = (float)(*((uint32_t *)p_cmd_data + 1)) / (1 << 24);
        ALOGV("Current Volume (%f / %f ) new Volume (%f / %f)", context->left_vol,
              context->right_vol, left_vol, right_vol);

        context->left_vol = left_vol;
        context->right_vol = right_vol;

        // After changing the state and if device is valid
        // check and send volume
        if (valid_dev_in_context(context)) {
            check_and_set_ma_parameter(context->stream_type);
        }
    }
    break;

    default:
        ALOGW("%s: unknow command %d", __func__, cmd_code);
        status = -ENOSYS;
        break;
    }

exit:
    pthread_mutex_unlock(&ma_listner_init_lock);
    return status;
}

/* Effect Control Interface Implementation: get_descriptor */
static int ma_effect_get_descriptor(effect_handle_t   self,
                                     effect_descriptor_t *descriptor)
{
    ma_listener_context_t *context = (ma_listener_context_t *)self;
    ALOGV("%s .. called ..", __func__);

    if (descriptor == NULL) {
        ALOGE("%s: descriptor is NULL", __func__);
        return -EINVAL;
    }

    *descriptor = *context->desc;
    return 0;
}

static void init_once()
{
    int ret = 0;
    void *handle = NULL;
    char lib_path[PATH_MAX] = {0};

    if (init_state == 0) {
        ALOGD("%s : already init ... do nothing", __func__);
        return;
    }

    ALOGV("%s .. called ..", __func__);

    send_ma_parameter = NULL;

    ret = snprintf(lib_path, PATH_MAX, "%s/%s", HAL_VENDOR_PATH, HAL_LIB_NAME);
    if (ret < 0) {
        ALOGE("%s: snprintf failed for lib %s ret %d", __func__, HAL_LIB_NAME, ret);
        return;
    }

    handle = dlopen(lib_path, RTLD_NOW);
    if (handle == NULL) {
        ALOGE("%s: DLOPEN failed for %s", __func__, HAL_LIB_NAME);
        return;
    } else {
        ALOGV("%s: DLOPEN successful for %s", __func__, HAL_LIB_NAME);
        send_ma_parameter = (bool (*)(int, float, bool))dlsym(handle, MA_SET_STATE);

        if (!send_ma_parameter) {
            ALOGE("%s: dlsym error %s for send_ma_parameter", __func__, dlerror());
            return;
        }
    }

    pthread_mutex_init(&ma_listner_init_lock, NULL);
    list_init(&ma_effect_list);
    init_state = 0;

    ALOGD("%s: exit ret %d", __func__, init_state);
}

static bool lib_init()
{
    pthread_once(&once, init_once);
    return init_state;
}

static int ma_prc_lib_create(const effect_uuid_t *uuid,
                              int32_t session_id,
                              int32_t io_id __unused,
                              effect_handle_t *p_handle)
{
    int itt = 0;
    ma_listener_context_t *context = NULL;

    ALOGV("%s .. called ..", __func__);

    if (lib_init() != 0) {
        return init_state;
    }

    if (p_handle == NULL || uuid == NULL) {
        ALOGE("%s: %s is NULL", __func__, (p_handle == NULL) ? "p_handle" : "uuid");
        return -EINVAL;
    }

    context = (ma_listener_context_t *)calloc(1, sizeof(ma_listener_context_t));

    if (context == NULL) {
        ALOGE("%s: failed to allocate for context .. oops !!", __func__);
        return -EINVAL;
    }

    // check if UUID is supported
    for (itt = 0; descriptors[itt] != NULL; itt++) {
        if (memcmp(uuid, &descriptors[itt]->uuid, sizeof(effect_uuid_t)) == 0) {
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
    context->state = MA_LISTENER_STATE_INITIALIZED;
    context->dev_id = AUDIO_DEVICE_NONE;
    context->session_id = session_id;

    // Add this to master list
    pthread_mutex_lock(&ma_listner_init_lock);
    list_add_tail(&ma_effect_list, &context->effect_list_node);

    pthread_mutex_unlock(&ma_listner_init_lock);

    *p_handle = (effect_handle_t)context;
    return 0;
}

static int ma_prc_lib_release(effect_handle_t handle)
{
    struct listnode *node, *temp_node_next;
    ma_listener_context_t *context = NULL;
    ma_listener_context_t *recv_contex = (ma_listener_context_t *)handle;
    int status = -EINVAL;

    ALOGV("%s: context %p", __func__, handle);

    if (recv_contex == NULL) {
        return status;
    }

    pthread_mutex_lock(&ma_listner_init_lock);
    // check if the handle/context provided is valid
    list_for_each_safe(node, temp_node_next, &ma_effect_list) {
        context = node_to_item(node, struct ma_listener_context_s, effect_list_node);
        if (context == recv_contex) {
            ALOGV("--- Found something to remove ---");
            list_remove(node);
            PRINT_STREAM_TYPE(context->stream_type);
            free(context);
            status = 0;
        }
    }

    if (status != 0) {
        ALOGE("%s: nothing to remove, ret %d", __func__, status);
        pthread_mutex_unlock(&ma_listner_init_lock);
        return status;
    }

    pthread_mutex_unlock(&ma_listner_init_lock);
    return status;
}

static int ma_prc_lib_get_descriptor(const effect_uuid_t *uuid,
                                      effect_descriptor_t *descriptor)
{
    int i = 0;

    ALOGV("%s .. called ..", __func__);
    if (lib_init() != 0) {
        return init_state;
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
    return -EINVAL;
}


/* effect_handle_t interface implementation for volume listener effect */
static const struct effect_interface_s effect_interface = {
    NULL,
    ma_effect_command,
    ma_effect_get_descriptor,
    NULL,
};

__attribute__((visibility("default")))
audio_effect_library_t AUDIO_EFFECT_LIBRARY_INFO_SYM = {
    .tag = AUDIO_EFFECT_LIBRARY_TAG,
    .version = EFFECT_LIBRARY_API_VERSION,
    .name = "MAXXAUDIO Listener Effect Library",
    .implementor = "The Android Open Source Project",
    .create_effect = ma_prc_lib_create,
    .release_effect = ma_prc_lib_release,
    .get_descriptor = ma_prc_lib_get_descriptor,
};
