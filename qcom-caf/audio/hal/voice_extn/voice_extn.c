/*
 * Copyright (c) 2013-2020, The Linux Foundation. All rights reserved.
 * Not a contribution.
 *
 * Copyright (C) 2013 The Android Open Source Project
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

#define LOG_TAG "voice_extn"
/*#define LOG_NDEBUG 0*/
#define LOG_NDDEBUG 0

#include <errno.h>
#include <stdlib.h>
#include <math.h>
#include <log/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>
#include <sys/ioctl.h>
#include <time.h>
#include <sound/voice_params.h>

#include "audio_hw.h"
#include "voice.h"
#include "platform.h"
#include "platform_api.h"
#include "voice_extn.h"

#ifdef DYNAMIC_LOG_ENABLED
#include <log_xml_parser.h>
#define LOG_MASK HAL_MOD_FILE_VOICE_EXTN
#include <log_utils.h>
#endif

#define AUDIO_PARAMETER_KEY_VSID                "vsid"
#define AUDIO_PARAMETER_KEY_CALL_STATE          "call_state"
#define AUDIO_PARAMETER_KEY_AUDIO_MODE          "audio_mode"
#define AUDIO_PARAMETER_KEY_ALL_CALL_STATES     "all_call_states"
#define AUDIO_PARAMETER_KEY_DEVICE_MUTE         "device_mute"
#define AUDIO_PARAMETER_KEY_DIRECTION           "direction"
#define AUDIO_PARAMETER_KEY_CALL_TYPE           "call_type"

#define VOICE_EXTN_PARAMETER_VALUE_MAX_LEN 256

#define VOICE2_VSID              0x10DC1000
#define VOLTE_VSID               0x10C02000
#define QCHAT_VSID               0x10803000
#define VOWLAN_VSID              0x10002000
#define VOICEMMODE1_VSID         0x11C05000
#define VOICEMMODE2_VSID         0x11DC5000
#define ALL_VSID                 0xFFFFFFFF

/* Voice Session Indices */
#define VOICE2_SESS_IDX    (VOICE_SESS_IDX + 1)
#define VOLTE_SESS_IDX     (VOICE_SESS_IDX + 2)
#define QCHAT_SESS_IDX     (VOICE_SESS_IDX + 3)
#define VOWLAN_SESS_IDX    (VOICE_SESS_IDX + 4)
#define MMODE1_SESS_IDX    (VOICE_SESS_IDX + 5)
#define MMODE2_SESS_IDX    (VOICE_SESS_IDX + 6)

/* Call States */
#define CALL_HOLD           (BASE_CALL_STATE + 2)
#define CALL_LOCAL_HOLD     (BASE_CALL_STATE + 3)

struct pcm_config pcm_config_incall_music = {
    .channels = 1,
    .rate = DEFAULT_OUTPUT_SAMPLING_RATE,
    .period_size = LOW_LATENCY_OUTPUT_PERIOD_SIZE,
    .period_count = LOW_LATENCY_OUTPUT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
    .stop_threshold = INT_MAX,
    .avail_min = LOW_LATENCY_OUTPUT_PERIOD_SIZE / 4,
};

static bool voice_extn_compress_voip_enabled = false;
static bool voice_extn_dynamic_ecns_feature_enabled = false;
static bool voice_extn_incall_music_enabled = false;
static bool voice_extn_multi_session_enabled = false;
static bool voice_extn_power_mode_enabled = false;

int voice_extn_is_call_state_active(struct audio_device *adev, bool *is_call_active);

int compress_voip_set_parameters(struct audio_device *adev,
                                             struct str_parms *parms);
void compress_voip_get_parameters(struct str_parms *query,
                                             struct str_parms *reply);
void compress_voip_out_get_parameters(struct stream_out *out,
                                                 struct str_parms *query,
                                                 struct str_parms *reply);
void compress_voip_in_get_parameters(struct stream_in *in,
                                                struct str_parms *query,
                                                struct str_parms *reply);
int compress_voip_out_get_buffer_size(struct stream_out *out);
int compress_voip_in_get_buffer_size(struct stream_in *in);
int compress_voip_start_output_stream(struct stream_out *out);
int compress_voip_start_input_stream(struct stream_in *in);
int compress_voip_close_output_stream(struct audio_stream *stream);
int compress_voip_open_output_stream(struct stream_out *out);
int compress_voip_close_input_stream(struct audio_stream *stream);
int compress_voip_open_input_stream(struct stream_in *in);
int compress_voip_set_volume(struct audio_device *adev, float volume);
int compress_voip_set_mic_mute(struct audio_device *adev, bool state);
bool compress_voip_pcm_prop_check();
bool compress_voip_is_active(const struct audio_device *adev);
bool compress_voip_is_format_supported(audio_format_t format);
bool compress_voip_is_config_supported(struct audio_config *config);
bool compress_voip_is_started(struct audio_device *adev);

static bool is_valid_call_state(int call_state)
{
    if (call_state < CALL_INACTIVE || call_state > CALL_LOCAL_HOLD)
        return false;
    else
        return true;
}

static bool is_valid_vsid(uint32_t vsid)
{
    if (vsid == VOICE_VSID ||
        vsid == VOICE2_VSID ||
        vsid == VOLTE_VSID ||
        vsid == QCHAT_VSID ||
        vsid == VOICEMMODE1_VSID ||
        vsid == VOICEMMODE2_VSID ||
        vsid == VOWLAN_VSID)
        return true;
    else
        return false;
}

static audio_usecase_t voice_extn_get_usecase_for_session_idx(const int index)
{
    audio_usecase_t usecase_id = -1;

    switch(index) {
    case VOICE_SESS_IDX:
        usecase_id = USECASE_VOICE_CALL;
        break;

    case VOICE2_SESS_IDX:
        usecase_id = USECASE_VOICE2_CALL;
        break;

    case VOLTE_SESS_IDX:
        usecase_id = USECASE_VOLTE_CALL;
        break;

    case QCHAT_SESS_IDX:
        usecase_id = USECASE_QCHAT_CALL;
        break;

    case VOWLAN_SESS_IDX:
        usecase_id = USECASE_VOWLAN_CALL;
        break;

    case MMODE1_SESS_IDX:
        usecase_id = USECASE_VOICEMMODE1_CALL;
        break;

    case MMODE2_SESS_IDX:
        usecase_id = USECASE_VOICEMMODE2_CALL;
        break;

    default:
        ALOGE("%s: Invalid voice session index\n", __func__);
    }

    return usecase_id;
}

static uint32_t get_session_id_with_state(struct audio_device *adev,
                                          int call_state)
{
    struct voice_session *session = NULL;
    int i = 0;
    uint32_t session_id = 0;
    int max_voice_sessions = MAX_VOICE_SESSIONS;

    if (!voice_extn_is_multi_session_supported())
        max_voice_sessions = 1;

    for (i = 0; i < max_voice_sessions; i++) {
        session = &adev->voice.session[i];
        if (session->state.current == call_state){
            session_id = session->vsid;
            break;
        }
    }

    return session_id;
}

static int update_calls(struct audio_device *adev)
{
    int i = 0;
    audio_usecase_t usecase_id = 0;
    enum voice_lch_mode lch_mode;
    struct voice_session *session = NULL;
    int ret = 0;
    int max_voice_sessions = MAX_VOICE_SESSIONS;

    ALOGD("%s: enter:", __func__);
    if (!voice_extn_is_multi_session_supported())
        max_voice_sessions = 1;

    for (i = 0; i < max_voice_sessions; i++) {
        usecase_id = voice_extn_get_usecase_for_session_idx(i);
        session = &adev->voice.session[i];
        ALOGD("%s: cur_state=%d new_state=%d vsid=%x",
              __func__, session->state.current, session->state.new, session->vsid);

        switch(session->state.new)
        {
        case CALL_ACTIVE:
            switch(session->state.current)
            {
            case CALL_INACTIVE:
                ALOGD("%s: INACTIVE -> ACTIVE vsid:%x", __func__, session->vsid);
                ret = voice_start_usecase(adev, usecase_id);
                if (ret < 0) {
                    ALOGE("%s: voice_start_usecase() failed for usecase: %d\n",
                          __func__, usecase_id);
                } else {
                    session->state.current = session->state.new;
                }
                break;

            case CALL_HOLD:
                ALOGD("%s: HOLD -> ACTIVE vsid:%x", __func__, session->vsid);
                session->state.current = session->state.new;
                break;

            case CALL_LOCAL_HOLD:
                ALOGD("%s: LOCAL_HOLD -> ACTIVE vsid:%x", __func__, session->vsid);
                lch_mode = VOICE_LCH_STOP;
                ret = platform_update_lch(adev->platform, session, lch_mode);
                if (ret < 0)
                    ALOGE("%s: lch mode update failed, ret = %d", __func__, ret);
                else
                    session->state.current = session->state.new;
                break;

            default:
                ALOGV("%s: CALL_ACTIVE cannot be handled in state=%d vsid:%x",
                      __func__, session->state.current, session->vsid);
                break;
            }
            break;

        case CALL_INACTIVE:
            switch(session->state.current)
            {
            case CALL_ACTIVE:
            case CALL_HOLD:
            case CALL_LOCAL_HOLD:
                ALOGD("%s: ACTIVE/HOLD/LOCAL_HOLD -> INACTIVE vsid:%x", __func__, session->vsid);
                ret = voice_stop_usecase(adev, usecase_id);
                if (ret < 0) {
                    ALOGE("%s: voice_stop_usecase() failed for usecase: %d\n",
                          __func__, usecase_id);
                } else {
                    session->state.current = session->state.new;
                }
                break;

            default:
                ALOGV("%s: CALL_INACTIVE cannot be handled in state=%d vsid:%x",
                      __func__, session->state.current, session->vsid);
                break;
            }
            break;

        case CALL_HOLD:
            switch(session->state.current)
            {
            case CALL_ACTIVE:
                ALOGD("%s: CALL_ACTIVE -> HOLD vsid:%x", __func__, session->vsid);
                session->state.current = session->state.new;
                break;

            case CALL_LOCAL_HOLD:
                ALOGD("%s: CALL_LOCAL_HOLD -> HOLD vsid:%x", __func__, session->vsid);
                lch_mode = VOICE_LCH_STOP;
                ret = platform_update_lch(adev->platform, session, lch_mode);
                if (ret < 0)
                    ALOGE("%s: lch mode update failed, ret = %d", __func__, ret);
                else
                    session->state.current = session->state.new;
                break;

            default:
                ALOGV("%s: CALL_HOLD cannot be handled in state=%d vsid:%x",
                      __func__, session->state.current, session->vsid);
                break;
            }
            break;

        case CALL_LOCAL_HOLD:
            switch(session->state.current)
            {
            case CALL_ACTIVE:
            case CALL_HOLD:
                ALOGD("%s: ACTIVE/CALL_HOLD -> LOCAL_HOLD vsid:%x", __func__,
                      session->vsid);
                lch_mode = VOICE_LCH_START;
                ret = platform_update_lch(adev->platform, session, lch_mode);
                if (ret < 0)
                    ALOGE("%s: lch mode update failed, ret = %d", __func__, ret);
                else
                    session->state.current = session->state.new;
                break;

            default:
                ALOGV("%s: CALL_LOCAL_HOLD cannot be handled in state=%d vsid:%x",
                      __func__, session->state.current, session->vsid);
                break;
            }
            break;

        default:
            break;
        } //end out switch loop
    } //end for loop

    return ret;
}

static int update_call_states(struct audio_device *adev,
                                    const uint32_t vsid, const int call_state)
{
    struct voice_session *session = NULL;
    int i = 0;
    bool is_call_active;
    int max_voice_sessions = MAX_VOICE_SESSIONS;

    if (!voice_extn_is_multi_session_supported())
        max_voice_sessions = 1;

    for (i = 0; i < max_voice_sessions; i++) {
        if (vsid == adev->voice.session[i].vsid) {
            session = &adev->voice.session[i];
            break;
        }
    }

    if (session) {
        session->state.new = call_state;
        voice_extn_is_call_state_active(adev, &is_call_active);
        ALOGD("%s is_call_active:%d in_call:%d, mode:%d\n",
              __func__, is_call_active, adev->voice.in_call, adev->mode);
        /* Dont start voice call before device routing for voice usescases has
         * occured, otherwise voice calls will be started unintendedly on
         * speaker.
         */
        if (is_call_active || voice_is_in_call(adev)) {
            /* Device routing is not triggered for voice calls on the subsequent
             * subs, Hence update the call states if voice call is already
             * active on other sub.
             */
            update_calls(adev);
        }
    } else {
        return -EINVAL;
    }

    return 0;

}

int voice_extn_get_active_session_id(struct audio_device *adev,
                                     uint32_t *session_id)
{
    if (!voice_extn_is_multi_session_supported())
        return -ENOSYS;

    *session_id = get_session_id_with_state(adev, CALL_ACTIVE);
    return 0;
}

int voice_extn_is_call_state_active(struct audio_device *adev, bool *is_call_active)
{
    struct voice_session *session = NULL;
    int i = 0;

    if (!voice_extn_is_multi_session_supported())
        return -ENOSYS;

    *is_call_active = false;
    for (i = 0; i < MAX_VOICE_SESSIONS; i++) {
        session = &adev->voice.session[i];
        if (session->state.current != CALL_INACTIVE){
            *is_call_active = true;
            break;
        }
    }

    return 0;
}

void dynamic_ecns_feature_init(bool is_feature_enabled)
{
    voice_extn_dynamic_ecns_feature_enabled = is_feature_enabled;
    ALOGD(":: %s: ---- Feature DYNAMIC_ECNS is %s ----", __func__,
                            is_feature_enabled ? "ENABLED" : "NOT ENABLED");
}

bool voice_extn_is_dynamic_ecns_enabled()
{
    return voice_extn_dynamic_ecns_feature_enabled;
}

void incall_music_feature_init(bool is_feature_enabled)
{
    voice_extn_incall_music_enabled = is_feature_enabled;
    ALOGV("%s: ---- Feature INCALL_MUSIC is %s----", __func__,
                                is_feature_enabled ? "ENABLED" : "NOT ENABLED");
}

bool voice_extn_is_incall_music_enabled()
{
    return voice_extn_incall_music_enabled;
}

void compr_voip_feature_init(bool is_feature_enabled)
{
    voice_extn_compress_voip_enabled = is_feature_enabled;
    ALOGV("%s:: ---- Feature COMPRESS_VOIP is %s ----", __func__,
                                is_feature_enabled ? "ENABLED" : "NOT ENABLED");
}

bool voice_extn_is_compress_voip_supported()
{
    return voice_extn_compress_voip_enabled;
}

void multi_voice_session_feature_init(bool is_feature_enabled)
{
    voice_extn_multi_session_enabled = is_feature_enabled;
    ALOGV("%s:: ---- Feature MULTI VOICE SESSION is %s ----", __func__,
                                is_feature_enabled ? "ENABLED" : "NOT ENABLED");
}

bool voice_extn_is_multi_session_supported()
{
    return voice_extn_multi_session_enabled;
}

void voice_power_mode_feature_init(bool is_feature_enabled)
{
    voice_extn_power_mode_enabled = is_feature_enabled;
    ALOGV("%s:: ---- Feature POWER MODE is %s ----", __func__,
                                is_feature_enabled ? "ENABLED" : "NOT ENABLED");
}

bool voice_extn_is_voice_power_mode_supported()
{
    return voice_extn_power_mode_enabled;
}

void voice_extn_feature_init()
{
    // Register feature function here
    // every feature should have a feature flag
    compr_voip_feature_init(
       property_get_bool("vendor.audio.feature.compr_voip.enable",
                          false));
    dynamic_ecns_feature_init(
       property_get_bool("vendor.audio.feature.dynamic_ecns.enable",
                          false));
    incall_music_feature_init(
       property_get_bool("vendor.audio.feature.incall_music.enable",
                          false));
    multi_voice_session_feature_init(
       property_get_bool("vendor.audio.feature.multi_voice_session.enable",
                          false));
    voice_power_mode_feature_init(
       property_get_bool("vendor.audio.feature.power_mode.enable",
                          false));
}

void voice_extn_init(struct audio_device *adev)
{
    if (!voice_extn_is_multi_session_supported())
        return;

    adev->voice.session[VOICE_SESS_IDX].vsid =  VOICE_VSID;
    adev->voice.session[VOICE2_SESS_IDX].vsid = VOICE2_VSID;
    adev->voice.session[VOLTE_SESS_IDX].vsid =  VOLTE_VSID;
    adev->voice.session[QCHAT_SESS_IDX].vsid =  QCHAT_VSID;
    adev->voice.session[VOWLAN_SESS_IDX].vsid = VOWLAN_VSID;
    adev->voice.session[MMODE1_SESS_IDX].vsid = VOICEMMODE1_VSID;
    adev->voice.session[MMODE2_SESS_IDX].vsid = VOICEMMODE2_VSID;
}

int voice_extn_get_session_from_use_case(struct audio_device *adev,
                                         const audio_usecase_t usecase_id,
                                         struct voice_session **session)
{
    if (!voice_extn_is_multi_session_supported())
        return -ENOSYS;

    switch(usecase_id)
    {
    case USECASE_VOICE_CALL:
        *session = &adev->voice.session[VOICE_SESS_IDX];
        break;

    case USECASE_VOICE2_CALL:
        *session = &adev->voice.session[VOICE2_SESS_IDX];
        break;

    case USECASE_VOLTE_CALL:
        *session = &adev->voice.session[VOLTE_SESS_IDX];
        break;

    case USECASE_QCHAT_CALL:
        *session = &adev->voice.session[QCHAT_SESS_IDX];
        break;

    case USECASE_VOWLAN_CALL:
        *session = &adev->voice.session[VOWLAN_SESS_IDX];
        break;

    case USECASE_VOICEMMODE1_CALL:
        *session = &adev->voice.session[MMODE1_SESS_IDX];
        break;

    case USECASE_VOICEMMODE2_CALL:
        *session = &adev->voice.session[MMODE2_SESS_IDX];
        break;

    default:
        ALOGE("%s: Invalid usecase_id:%d\n", __func__, usecase_id);
        *session = NULL;
        return -EINVAL;
    }

    return 0;
}

int voice_extn_start_call(struct audio_device *adev)
{
    /* Start voice calls on sessions whose call state has been
     * udpated.
     */
    ALOGV("%s: enter:", __func__);
    if (!voice_extn_is_multi_session_supported())
        return -ENOSYS;

    return update_calls(adev);
}

int voice_extn_stop_call(struct audio_device *adev)
{
    int i;
    int ret = 0;

    ALOGV("%s: enter:", __func__);

    if (!voice_extn_is_multi_session_supported())
        return -ENOSYS;

    /* If BT device is enabled and voice calls are ended, telephony will call
     * set_mode(AUDIO_MODE_NORMAL) which will trigger audio policy manager to
     * set routing with device BT A2DP profile. Hence end all voice calls when
     * set_mode(AUDIO_MODE_NORMAL) before BT A2DP profile is selected.
     */
    if (adev->mode == AUDIO_MODE_NORMAL) {
        ALOGD("%s: end all calls", __func__);
        for (i = 0; i < MAX_VOICE_SESSIONS; i++) {
            adev->voice.session[i].state.new = CALL_INACTIVE;
        }

        ret = update_calls(adev);
    }

    return ret;
}

int voice_extn_set_parameters(struct audio_device *adev,
                              struct str_parms *parms)
{
    int value;
    int ret = 0, err;
    char *kv_pairs = str_parms_to_str(parms);
    char str_value[256] = {0};

    ALOGV_IF(kv_pairs != NULL, "%s: enter: %s", __func__, kv_pairs);

    err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_VSID, &value);
    if (err >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_VSID);
        uint32_t vsid = value;
        int call_state = -1;
        err = str_parms_get_int(parms, AUDIO_PARAMETER_KEY_CALL_STATE, &value);
        if (err >= 0) {
            call_state = value;
            str_parms_del(parms, AUDIO_PARAMETER_KEY_CALL_STATE);
        } else {
            ALOGE("%s: call_state key not found", __func__);
            ret = -EINVAL;
            goto done;
        }

        if (is_valid_vsid(vsid) && is_valid_call_state(call_state)) {
            ret = update_call_states(adev, vsid, call_state);
        } else {
            ALOGE("%s: invalid vsid:%x or call_state:%d",
                  __func__, vsid, call_state);
            ret = -EINVAL;
            goto done;
        }
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_DEVICE_MUTE, str_value,
                            sizeof(str_value));
    if (err >= 0) {
        str_parms_del(parms, AUDIO_PARAMETER_KEY_DEVICE_MUTE);
        bool mute = false;

        if (!strncmp("true", str_value, sizeof("true"))) {
            mute = true;
        }

        err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_DIRECTION, str_value,
                                sizeof(str_value));
        if (err >= 0) {
            str_parms_del(parms, AUDIO_PARAMETER_KEY_DIRECTION);
        } else {
            ALOGE("%s: direction key not found", __func__);
            ret = -EINVAL;
            goto done;
        }

        ret = platform_set_device_mute(adev->platform, mute, str_value);
        if (ret != 0) {
            ALOGE("%s: Failed to set mute err:%d", __func__, ret);
            ret = -EINVAL;
            goto done;
        }
    }

    err = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_CALL_TYPE, str_value,
                            sizeof(str_value));
    if (err >= 0) {
          str_parms_del(parms, AUDIO_PARAMETER_KEY_CALL_TYPE);
          ALOGD("%s: call type is %s",__func__,str_value);

           /* Expected call types are CDMA/GSM/WCDMA/LTE/TDSDMA/WLAN/UNKNOWN */
           if (!strncmp("GSM", str_value, sizeof("GSM"))) {
               platform_set_gsm_mode(adev->platform, true);
           } else {
               platform_set_gsm_mode(adev->platform, false);
           }
    }

done:
    ALOGV("%s: exit with code(%d)", __func__, ret);
    free(kv_pairs);
    return ret;
}

static int get_all_call_states_str(const struct audio_device *adev,
                            char *value)
{
    int ret = 0;
    char *cur_ptr = value;
    int i, len=0;
    int max_voice_sessions = MAX_VOICE_SESSIONS;

    if (!voice_extn_is_multi_session_supported())
        max_voice_sessions = 1;

    for (i = 0; i < max_voice_sessions; i++) {
        snprintf(cur_ptr, VOICE_EXTN_PARAMETER_VALUE_MAX_LEN - len,
                 "%d:%d,",adev->voice.session[i].vsid,
                 adev->voice.session[i].state.current);
        len = strlen(cur_ptr);
        cur_ptr = cur_ptr + len;
    }
    ALOGV("%s:value=%s", __func__, value);
    return ret;
}

void voice_extn_get_parameters(const struct audio_device *adev,
                               struct str_parms *query,
                               struct str_parms *reply)
{
    int ret;
    char value[VOICE_EXTN_PARAMETER_VALUE_MAX_LEN] = {0};
    char *str = str_parms_to_str(query);

    ALOGV_IF(str != NULL, "%s: enter %s", __func__, str);
    free(str);

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_AUDIO_MODE, value,
                            sizeof(value));
    if (ret >= 0) {
        str_parms_add_int(reply, AUDIO_PARAMETER_KEY_AUDIO_MODE, adev->mode);
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_KEY_ALL_CALL_STATES,
                            value, sizeof(value));
    if (ret >= 0) {
        ret = get_all_call_states_str(adev, value);
        if (ret) {
            ALOGE("%s: Error fetching call states, err:%d", __func__, ret);
            return;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_KEY_ALL_CALL_STATES, value);
    }
    if (voice_extn_compress_voip_enabled)
        voice_extn_compress_voip_get_parameters(query, reply);

    str = str_parms_to_str(reply);
    ALOGV_IF(str != NULL, "%s: exit: returns \"%s\"", __func__, str);
    free(str);
}

void voice_extn_out_get_parameters(struct stream_out *out,
                                   struct str_parms *query,
                                   struct str_parms *reply)
{
    if (voice_extn_compress_voip_enabled)
        voice_extn_compress_voip_out_get_parameters(out, query, reply);
}

void voice_extn_in_get_parameters(struct stream_in *in,
                                  struct str_parms *query,
                                  struct str_parms *reply)
{
    if (voice_extn_compress_voip_enabled)
        voice_extn_compress_voip_in_get_parameters(in, query, reply);
}


int voice_extn_check_and_set_incall_music_usecase(struct audio_device *adev,
                                                  struct stream_out *out)
{
    if (voice_extn_incall_music_enabled) {
        uint32_t session_id = get_session_id_with_state(adev, CALL_ACTIVE);
        if (session_id == VOICEMMODE1_VSID) {
            out->usecase = USECASE_INCALL_MUSIC_UPLINK;
        } else if (session_id == VOICEMMODE2_VSID) {
            out->usecase = USECASE_INCALL_MUSIC_UPLINK2;
        } else {
            ALOGE("%s: Invalid session id %x", __func__, session_id);
            out->usecase = USECASE_INCALL_MUSIC_UPLINK;
        }

        out->config = pcm_config_incall_music;
        //FIXME: add support for MONO stream configuration when audioflinger mixer supports it
        out->supported_channel_masks[0] = AUDIO_CHANNEL_OUT_MONO;
        out->supported_channel_masks[1] = AUDIO_CHANNEL_OUT_STEREO;
        out->config.rate = out->sample_rate;

        ALOGV("%s: mode=%d, usecase id=%d", __func__, adev->mode, out->usecase);
        return 0;
    }
    return -ENOSYS;
}


int voice_extn_compress_voip_set_parameters(struct audio_device *adev,
                                            struct str_parms *parms)
{
    int ret = -ENOSYS;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_set_parameters(adev, parms);
    return ret;
}

void voice_extn_compress_voip_get_parameters(struct str_parms *query,
                                             struct str_parms *reply)
{
    if (voice_extn_compress_voip_enabled)
        compress_voip_get_parameters(query, reply);
}


void voice_extn_compress_voip_out_get_parameters(struct stream_out *out,
                                                 struct str_parms *query,
                                                 struct str_parms *reply)
{
    if (voice_extn_compress_voip_enabled)
        compress_voip_out_get_parameters(out, query, reply);
}

void voice_extn_compress_voip_in_get_parameters(struct stream_in *in,
                                                struct str_parms *query,
                                                struct str_parms *reply)
{
    if (voice_extn_compress_voip_enabled)
        compress_voip_in_get_parameters(in, query, reply);
}

int voice_extn_compress_voip_out_get_buffer_size(struct stream_out *out)
{
    int ret = -1;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_out_get_buffer_size(out);
    return ret;
}

int voice_extn_compress_voip_in_get_buffer_size(struct stream_in *in)
{
    int ret = -1;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_in_get_buffer_size(in);
    return ret;
}


int voice_extn_compress_voip_start_output_stream(struct stream_out *out)
{
    int ret = -1;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_start_output_stream(out);
    return ret;
}


int voice_extn_compress_voip_start_input_stream(struct stream_in *in)
{
    int ret = -1;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_start_input_stream(in);
    return ret;
}

int voice_extn_compress_voip_close_output_stream(struct audio_stream *stream)
{
    int ret = -1;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_close_output_stream(stream);
    return ret;
}


int voice_extn_compress_voip_close_input_stream(struct audio_stream *stream)
{
    int ret = -1;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_close_input_stream(stream);
    return ret;
}

int voice_extn_compress_voip_open_output_stream(struct stream_out *out)
{
    int ret = -1;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_open_output_stream(out);
    return ret;
}


int voice_extn_compress_voip_open_input_stream(struct stream_in *in)
{
    int ret = -1;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_open_input_stream(in);
    return ret;
}

int voice_extn_compress_voip_set_volume(struct audio_device *adev, float volume)
{
    int ret = -1;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_set_volume(adev, volume);
    return ret;
}

int voice_extn_compress_voip_set_mic_mute(struct audio_device *adev, bool state)
{
    int ret = -1;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_set_mic_mute(adev, state);
    return ret;
}

bool voice_extn_compress_voip_pcm_prop_check()
{
    bool ret = false;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_pcm_prop_check();
    return ret;
}

bool voice_extn_compress_voip_is_active(const struct audio_device *adev)
{
    bool ret = false;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_is_active(adev);
    return ret;
}

bool voice_extn_compress_voip_is_format_supported(audio_format_t format)
{
    bool ret = false;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_is_format_supported(format);
    return ret;
}

bool voice_extn_compress_voip_is_config_supported(struct audio_config *config)
{
    bool ret = false;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_is_config_supported(config);
    return ret;
}

bool voice_extn_compress_voip_is_started(struct audio_device *adev)
{
    bool ret = false;
    if (voice_extn_compress_voip_enabled)
        ret = compress_voip_is_started(adev);
    return ret;
}

