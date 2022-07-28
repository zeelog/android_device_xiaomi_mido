/*
 * Copyright (C) 2017-2022 The LineageOS Project
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

package org.lineageos.settings.vibration;

import android.app.ActionBar;
import android.content.Intent;
import android.os.Bundle;
import androidx.preference.PreferenceFragment;
import androidx.preference.Preference;
import androidx.preference.ListPreference;
import androidx.preference.PreferenceCategory;
import androidx.preference.PreferenceManager;
import androidx.preference.PreferenceScreen;
import androidx.preference.SwitchPreference;
import androidx.preference.TwoStatePreference;

import org.lineageos.settings.R;

public class VibratorSettings extends PreferenceFragment {

    public static final String PREF_VMAX_OVERRIDE_SWITCH = "vmax_override";
    public static final String VMAX_OVERRIDE_PATH = "/sys/module/qti_haptics/parameters/vmax_mv_override";

    private static TwoStatePreference mVmaxOverrideModeSwitch;

    @Override
    public void onActivityCreated(Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);
        final ActionBar actionBar = getActivity().getActionBar();
        if (actionBar != null) {
            actionBar.setDisplayHomeAsUpEnabled(true);
        }

    }

    @Override
    public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
        setPreferencesFromResource(R.xml.vibration_settings, rootKey);

        mVmaxOverrideModeSwitch = (TwoStatePreference) findPreference(PREF_VMAX_OVERRIDE_SWITCH);
        mVmaxOverrideModeSwitch.setEnabled(VibratorOverrideModeSwitch.isSupported());
        mVmaxOverrideModeSwitch.setChecked(VibratorOverrideModeSwitch.isCurrentlyEnabled(this.getContext()));
        mVmaxOverrideModeSwitch.setOnPreferenceChangeListener(new VibratorOverrideModeSwitch(getContext()));

        VibratorStrengthPreference mVibratorStrength = findPreference("vib_strength");
        mVibratorStrength.setEnabled(VibratorStrengthPreference.isSupported());
    }
}
