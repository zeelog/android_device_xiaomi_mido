/*
 * Copyright (C) 2022 The Android Open Source Project
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

import java.text.DecimalFormat;
import java.lang.Math;

public class Utils {
    /**
     * Set the alpha component of {@code color} to be {@code alpha}.
     *
     * @see android/support/v7/graphics/ColorUtils.java
     */
    public static int setColorAlphaComponent(int color, int alpha) {
        // Convert percentage to HEX representation which is in range 0-255 (0-0xFF)
        int alpha_hex = (int) (0xFF * (alpha / 100f));
        return (color & 0x00ffffff) | (alpha_hex << 24);
    }

    /**
     * Convert size in bytes to human-readable size
     */
    public static String getHumanReadableSize(long size) {
        final String[] units = new String[] { "B", "KB", "MB", "GB", "TB" };
        if (size <= 0) return "0 " + units[0];
        int digitGroups = (int) (Math.log10(size) / Math.log10(1024));
        return new DecimalFormat("#,##0.#")
            .format(size / Math.pow(1024, digitGroups)) + " " +
            units[digitGroups];
    }
}
