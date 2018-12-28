/*
 * Copyright (C) 2018 The LineageOS Project
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

package org.lineageos.settings.dirac;

import android.content.Context;
import android.content.Intent;
import android.os.UserHandle;

public final class DiracUtils {

    protected static DiracSound mDiracSound;
    private static boolean mInitialized;

    public static void initialize() {
        if (!mInitialized) {
            mInitialized = true;
            mDiracSound = new DiracSound(0, 0);
            mDiracSound.setMusic(mDiracSound.getMusic());
            mDiracSound.setHeadsetType(mDiracSound.getHeadsetType());
            setLevel(getLevel());
        }
    }

    protected static void setMusic(boolean enable) {
        mDiracSound.setMusic(enable ? 1 : 0);
    }

    protected static boolean isDiracEnabled(Context context) {
        return mDiracSound.getMusic() == 1;
    }

    protected static void setLevel(String preset) {
        String[] level = preset.split("\\s*,\\s*");

        for (int band = 0; band <= level.length - 1; band++) {
            mDiracSound.setLevel(band, Float.valueOf(level[band]));
        }
    }

    protected static String getLevel() {
        String selected = "";
        for (int band = 0; band <= 6; band++) {
            int temp = (int) mDiracSound.getLevel(band);
            selected += String.valueOf(temp);
            if (band != 6) selected += ",";
        }
        return selected;
    }

    protected static void setHeadsetType(int paramInt) {
         mDiracSound.setHeadsetType(paramInt);
    }
}
