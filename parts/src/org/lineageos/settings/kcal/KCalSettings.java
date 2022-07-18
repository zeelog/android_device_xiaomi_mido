/*
 * Copyright (C) 2018 The Xiaomi-SDM660 Project
 *               2017-2021 The LineageOS Project
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

package org.lineageos.settings.kcal;

import android.os.Bundle;
import android.provider.Settings;
import androidx.preference.PreferenceFragment;
import androidx.preference.Preference;

import org.lineageos.settings.preferences.FileUtils;
import org.lineageos.settings.preferences.SecureSettingCustomSeekBarPreference;
import org.lineageos.settings.preferences.SecureSettingSwitchPreference;

import org.lineageos.settings.R;

public class KCalSettings extends PreferenceFragment implements
        Preference.OnPreferenceChangeListener, Utils {

    private SecureSettingSwitchPreference mEnabled;
    private SecureSettingSwitchPreference mSetOnBoot;
    private SecureSettingCustomSeekBarPreference mRed;
    private SecureSettingCustomSeekBarPreference mGreen;
    private SecureSettingCustomSeekBarPreference mBlue;
    private SecureSettingCustomSeekBarPreference mSaturation;
    private SecureSettingCustomSeekBarPreference mValue;
    private SecureSettingCustomSeekBarPreference mContrast;
    private SecureSettingCustomSeekBarPreference mHue;
    private SecureSettingCustomSeekBarPreference mMin;
    private SecureSettingSwitchPreference mGrayscale;

    @Override
    public void onActivityCreated(Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);
    }

    @Override
    public void onCreatePreferences(Bundle savedInstanceState, String rootKey) {
        setPreferencesFromResource(R.xml.preferences_kcal, rootKey);

        boolean enabled = Settings.Secure.getInt(getContext().getContentResolver(), PREF_ENABLED,
                0) == 1;

        mEnabled = (SecureSettingSwitchPreference) findPreference(PREF_ENABLED);
        mEnabled.setOnPreferenceChangeListener(this);
        mEnabled.setTitle(enabled ? R.string.kcal_enabled : R.string.kcal_disabled);

        mSetOnBoot = (SecureSettingSwitchPreference) findPreference(PREF_SETONBOOT);
        mSetOnBoot.setOnPreferenceChangeListener(this);

        mMin = (SecureSettingCustomSeekBarPreference) findPreference(PREF_MINIMUM);
        mMin.setOnPreferenceChangeListener(this);

        mRed = (SecureSettingCustomSeekBarPreference) findPreference(PREF_RED);
        mRed.setOnPreferenceChangeListener(this);

        mGreen = (SecureSettingCustomSeekBarPreference) findPreference(PREF_GREEN);
        mGreen.setOnPreferenceChangeListener(this);

        mBlue = (SecureSettingCustomSeekBarPreference) findPreference(PREF_BLUE);
        mBlue.setOnPreferenceChangeListener(this);

        mSaturation = (SecureSettingCustomSeekBarPreference) findPreference(PREF_SATURATION);
        mSaturation.setEnabled((Settings.Secure.getInt(getContext().getContentResolver(),
                PREF_GRAYSCALE, 0) == 0));
        mSaturation.setOnPreferenceChangeListener(this);

        mValue = (SecureSettingCustomSeekBarPreference) findPreference(PREF_VALUE);
        mValue.setOnPreferenceChangeListener(this);

        mContrast = (SecureSettingCustomSeekBarPreference) findPreference(PREF_CONTRAST);
        mContrast.setOnPreferenceChangeListener(this);

        mHue = (SecureSettingCustomSeekBarPreference) findPreference(PREF_HUE);
        mHue.setOnPreferenceChangeListener(this);

        mGrayscale = (SecureSettingSwitchPreference) findPreference(PREF_GRAYSCALE);
        mGrayscale.setOnPreferenceChangeListener(this);
    }

    @Override
    public boolean onPreferenceChange(Preference preference, Object value) {
        final String key = preference.getKey();

        String rgbString;

        switch (key) {
            case PREF_ENABLED:
                FileUtils.setValue(KCAL_ENABLE, (boolean) value);
                mEnabled.setTitle((boolean) value ? R.string.kcal_enabled : R.string.kcal_disabled);
                break;

            case PREF_MINIMUM:
                FileUtils.setValue(KCAL_MIN, (int) value);
                break;

            case PREF_RED:
                rgbString = value + " " + mGreen.getValue() + " " + mBlue.getValue();
                FileUtils.setValue(KCAL_RGB, rgbString);
                break;

            case PREF_GREEN:
                rgbString = mRed.getValue() + " " + value + " " + mBlue.getValue();
                FileUtils.setValue(KCAL_RGB, rgbString);
                break;

            case PREF_BLUE:
                rgbString = mRed.getValue() + " " + mGreen.getValue() + " " + value;
                FileUtils.setValue(KCAL_RGB, rgbString);
                break;

            case PREF_SATURATION:
                if (!(Settings.Secure.getInt(getContext().getContentResolver(), PREF_GRAYSCALE, 0) == 1)) {
                    FileUtils.setValue(KCAL_SAT, (int) value + SATURATION_OFFSET);
                }
                break;

            case PREF_VALUE:
                FileUtils.setValue(KCAL_VAL, (int) value + VALUE_OFFSET);
                break;

            case PREF_CONTRAST:
                FileUtils.setValue(KCAL_CONT, (int) value + CONTRAST_OFFSET);
                break;

            case PREF_HUE:
                FileUtils.setValue(KCAL_HUE, (int) value);
                break;

            case PREF_GRAYSCALE:
                setmGrayscale((boolean) value);
                break;

            default:
                break;
        }
        return true;
    }

    void applyValues(String preset) {
        String[] values = preset.split(" ");
        int red = Integer.parseInt(values[0]);
        int green = Integer.parseInt(values[1]);
        int blue = Integer.parseInt(values[2]);
        int min = Integer.parseInt(values[3]);
        int sat = Integer.parseInt(values[4]);
        int value = Integer.parseInt(values[5]);
        int contrast = Integer.parseInt(values[6]);
        int hue = Integer.parseInt(values[7]);

        mRed.refresh(red);
        mGreen.refresh(green);
        mBlue.refresh(blue);
        mMin.refresh(min);
        mSaturation.refresh(sat);
        mValue.refresh(value);
        mContrast.refresh(contrast);
        mHue.refresh(hue);
    }

    void setmSetOnBoot(boolean checked) {
        mSetOnBoot.setChecked(checked);
    }

    void setmGrayscale(boolean checked) {
        mGrayscale.setChecked(checked);
        mSaturation.setEnabled(!checked);
        FileUtils.setValue(KCAL_SAT, checked ? 128 :
                Settings.Secure.getInt(getContext().getContentResolver(), PREF_SATURATION,
                        SATURATION_DEFAULT) + SATURATION_OFFSET);
    }
}
