/*
 * Copyright (C) 2015 The CyanogenMod Project
 *               2017-2018 The LineageOS Project
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

package org.lineageos.settings;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;
import android.provider.Settings;

import org.lineageos.settings.dirac.DiracUtils;
import org.lineageos.settings.doze.DozeUtils;
import org.lineageos.settings.preferences.FileUtils;
import org.lineageos.settings.soundcontrol.SoundControlSettings;
import org.lineageos.settings.torch.TorchSettings;

public class BootCompletedReceiver extends BroadcastReceiver {

    private static final boolean DEBUG = false;
     private static final String TAG = "XiaomiParts";

    @Override
    public void onReceive(final Context context, Intent intent) {
        if (DozeUtils.isDozeEnabled(context) && DozeUtils.sensorsEnabled(context)) {
            if (DEBUG) Log.d(TAG, "Starting Doze service");
            DozeUtils.startService(context);
        }
        new DiracUtils(context).onBootCompleted();

        FileUtils.setValue(TorchSettings.TORCH_1_BRIGHTNESS_PATH,
                Settings.Secure.getInt(context.getContentResolver(),
                        TorchSettings.KEY_WHITE_TORCH_BRIGHTNESS, 100));
        FileUtils.setValue(TorchSettings.TORCH_2_BRIGHTNESS_PATH,
                Settings.Secure.getInt(context.getContentResolver(),
                        TorchSettings.KEY_YELLOW_TORCH_BRIGHTNESS, 100));
    int gain = Settings.Secure.getInt(context.getContentResolver(),
                SoundControlSettings.PREF_HEADPHONE_GAIN, 4);
        FileUtils.setValue(SoundControlSettings.HEADPHONE_GAIN_PATH, gain + " " + gain);
        FileUtils.setValue(SoundControlSettings.MICROPHONE_GAIN_PATH, Settings.Secure.getInt(context.getContentResolver(),
                SoundControlSettings.PREF_MICROPHONE_GAIN, 0));
        FileUtils.setValue(SoundControlSettings.SPEAKER_GAIN_PATH, Settings.Secure.getInt(context.getContentResolver(),
                SoundControlSettings.PREF_SPEAKER_GAIN, 0));
    }
}
