/*
 * Copyright (C) 2014 The Android Open Source Project
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

package com.android.fmradio;

import android.os.Bundle;

/**
 * Activity connect FMRadio service should implements this interface to update
 * ui or status
 */
public interface FmListener {
    /**
     * directly call back from service to activity
     */
    // FM RDS station changed
    int LISTEN_RDSSTATION_CHANGED = 0x00100010;

    // FM PS information changed
    int LISTEN_PS_CHANGED = 0x00100011;

    // FM RT information changed
    int LISTEN_RT_CHANGED = 0x00100100;

    // FM Record state changed
    int LISTEN_RECORDSTATE_CHANGED = 0x00100101; // 1048833

    // FM record error occur
    int LISTEN_RECORDERROR = 0x00100110; // 1048848

    // FM record mode change
    int LISTEN_RECORDMODE_CHANGED = 0x00100111; // 4018849

    // FM Record state changed
    int LISTEN_SPEAKER_MODE_CHANGED = 0x00101000; // 1052672

    // Bundle keys
    String SWITCH_ANTENNA_VALUE = "switch_antenna_value";
    String CALLBACK_FLAG = "callback_flag";
    String KEY_IS_SWITCH_ANTENNA = "key_is_switch_antenna";
    String KEY_IS_TUNE = "key_is_tune";
    String KEY_TUNE_TO_STATION = "key_tune_to_station";
    String KEY_IS_SEEK = "key_is_seek";
    String KEY_SEEK_TO_STATION = "key_seek_to_station";
    String KEY_IS_SCAN = "key_is_scan";
    String KEY_RDS_STATION = "key_rds_station";
    String KEY_PS_INFO = "key_ps_info";
    String KEY_RT_INFO = "key_rt_info";
    String KEY_STATION_NUM = "key_station_num";

    // Audio focus related
    String KEY_AUDIOFOCUS_CHANGED = "key_audiofocus_changed";

    // Recording
    String KEY_RECORDING_STATE = "key_is_recording_state";
    String KEY_RECORDING_ERROR_TYPE = "key_recording_error_type";
    String KEY_IS_RECORDING_MODE = "key_is_recording_mode";

    // For change speaker/earphone mode
    String KEY_IS_SPEAKER_MODE = "key_is_speaker_mode";

    // Headset events
    String KEY_HEADSET_HOOK_EVENT = "key_headset_hook_event";

    /**
     * handle message: call back from service to activity
     */
    // Message to handle
    int MSGID_UPDATE_RDS = 1;
    int MSGID_UPDATE_CURRENT_STATION = 2;
    int MSGID_ANTENNA_UNAVAILABE = 3;
    int MSGID_SWITCH_ANTENNA = 4;
    int MSGID_SET_RDS_FINISHED = 5;
    int MSGID_SET_CHANNEL_FINISHED = 6;
    int MSGID_SET_MUTE_FINISHED = 7;
    // Fm main
    int MSGID_POWERUP_FINISHED = 9;
    int MSGID_POWERDOWN_FINISHED = 10;
    int MSGID_FM_EXIT = 11;
    int MSGID_SCAN_CANCELED = 12;
    int MSGID_SCAN_FINISHED = 13;
    int MSGID_AUDIOFOCUS_FAILED = 14;
    int MSGID_TUNE_FINISHED = 15;
    int MSGID_SEEK_FINISHED = 16;
    int MSGID_ACTIVE_AF_FINISHED = 18;
    // Recording
    int MSGID_RECORD_STATE_CHANGED = 19;
    int MSGID_RECORD_ERROR = 20;
    int MSGID_RECORD_MODE_CHANED = 21;
    int MSGID_STARTRECORDING_FINISHED = 22;
    int MSGID_STOPRECORDING_FINISHED = 23;
    int MSGID_STARTPLAYBACK_FINISHED = 24;
    int MSGID_STOPPLAYBACK_FINISHED = 25;
    int MSGID_SAVERECORDING_FINISHED = 26;
    // Audio focus related
    int MSGID_AUDIOFOCUS_CHANGED = 30;

    int NOT_AUDIO_FOCUS = 33;

    // For refresh time
    int MSGID_REFRESH = 101;

    // Headset events
    int MSGID_HEADSET_HOOK_EVENT = 102;
    int MSGID_HEADSET_HOOK_MULTI_CLICK_TIMEOUT = 103;

    /**
     * Call back method to activity from service
     */
    void onCallBack(Bundle bundle);
}
