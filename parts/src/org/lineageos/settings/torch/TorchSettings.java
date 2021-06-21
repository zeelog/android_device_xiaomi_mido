/*
 * Copyright (C) 2018 The Asus-SDM660 Project
 * Copyright (C) 2017-2021 The LineageOS Project
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
 * limitations under the License
 */

package org.lineageos.settings.torch;

import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import androidx.preference.PreferenceFragment;
import androidx.preference.Preference;
import androidx.preference.PreferenceCategory;

import org.lineageos.settings.preferences.CustomSeekBarPreference;
import org.lineageos.settings.preferences.FileUtils;

import org.lineageos.settings.R;

public class TorchSettings extends PreferenceFragment implements
        Preference.OnPreferenceChangeListener {

    public static final String KEY_YELLOW_TORCH_BRIGHTNESS = "yellow_torch_brightness";
    public static final String KEY_WHITE_TORCH_BRIGHTNESS = "white_torch_brightness";
    public static final String TORCH_1_BRIGHTNESS_PATH = "sys/devices/platform/soc/200f000.qcom,spmi/spmi-0/spmi0-03/200f000.qcom,spmi:qcom,pmi8950@3:qcom,leds@d300/leds/led:torch_0/max_brightness";
    public static final String TORCH_2_BRIGHTNESS_PATH = "sys/devices/platform/soc/200f000.qcom,spmi/spmi-0/spmi0-03/200f000.qcom,spmi:qcom,pmi8950@3:qcom,leds@d300/leds/led:torch_1/max_brightness";
    
    private CustomSeekBarPreference mWhiteTorchBrightness;
    private CustomSeekBarPreference mYellowTorchBrightness;

    @Override
    public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
        setPreferencesFromResource(R.xml.torch_settings, rootKey);

        mWhiteTorchBrightness = (CustomSeekBarPreference) findPreference(KEY_WHITE_TORCH_BRIGHTNESS);
        mWhiteTorchBrightness.setEnabled(FileUtils.fileWritable(TORCH_1_BRIGHTNESS_PATH));
        mWhiteTorchBrightness.setOnPreferenceChangeListener(this);

        mYellowTorchBrightness = (CustomSeekBarPreference) findPreference(KEY_YELLOW_TORCH_BRIGHTNESS);
        mYellowTorchBrightness.setEnabled(FileUtils.fileWritable(TORCH_2_BRIGHTNESS_PATH));
        mYellowTorchBrightness.setOnPreferenceChangeListener(this);
    }

    @Override
    public boolean onPreferenceChange(Preference preference, Object value) {
        final String key = preference.getKey();
        switch (key) {
            case KEY_WHITE_TORCH_BRIGHTNESS:
                FileUtils.setValue(TORCH_1_BRIGHTNESS_PATH, (int) value);
                break;

            case KEY_YELLOW_TORCH_BRIGHTNESS:
                FileUtils.setValue(TORCH_2_BRIGHTNESS_PATH, (int) value);
                break;
        }
        return true;
    }
}
