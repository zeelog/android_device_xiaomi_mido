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

/**
 * This class define FM native interface, will description FM native interface
 */
public class FmNative {
    static {
        System.loadLibrary("qcomfmjni");
    }

    /**
     * Open FM device, call before power up
     *
     * @return (true,success; false, failed)
     */
    static native boolean openDev();

    /**
     * Close FM device, call after power down
     *
     * @return (true, success; false, failed)
     */
    static native boolean closeDev();

    /**
     * power up FM with frequency use long antenna
     *
     * @param frequency frequency(50KHZ, 87.55; 100KHZ, 87.5)
     *
     * @return (true, success; false, failed)
     */
    static native boolean powerUp(float frequency);

    /**
     * Power down FM
     *
     * @param type (0, FMRadio; 1, FMTransimitter)
     *
     * @return (true, success; false, failed)
     */
    static native boolean powerDown(int type);

    /**
     * tune to frequency
     *
     * @param frequency frequency(50KHZ, 87.55; 100KHZ, 87.5)
     *
     * @return (true, success; false, failed)
     */
    static native boolean tune(float frequency);

    /**
     * seek with frequency in direction
     *
     * @param frequency frequency(50KHZ, 87.55; 100KHZ, 87.5)
     * @param isUp (true, next station; false previous station)
     *
     * @return frequency(float)
     */
    static native float seek(float frequency, boolean isUp);

    /**
     * Auto scan(from 87.50-108.00)
     *
     * @return The scan station array(short)
     */
    static native short[] autoScan();

    /**
     * Stop scan, also can stop seek, other native when scan should call stop
     * scan first, else will execute wait auto scan finish
     *
     * @return (true, can stop scan process; false, can't stop scan process)
     */
    static native boolean stopScan();

    /**
     * Open or close rds fuction
     *
     * @param rdson The rdson (true, open; false, close)
     *
     * @return rdsset
     */
    static native int setRds(boolean rdson);

    /**
     * Read rds events
     *
     * @return rds event type
     */
    static native short readRds();

    /**
     * Get program service(program name)
     *
     * @return The program name
     */
    static native byte[] getPs();

    /**
     * Get radio text, RDS standard does not support Chinese character
     *
     * @return The LRT (Last Radio Text) bytes
     */
    static native byte[] getLrText();

    /**
     * Active alternative frequencies
     *
     * @return The frequency(float)
     */
    static native short activeAf();

    /**
     * Mute or unmute FM voice
     *
     * @param mute (true, mute; false, unmute)
     *
     * @return (true, success; false, failed)
     */
    static native int setMute(boolean mute);

    /**
     * Inquiry if RDS is support in driver
     *
     * @return (1, support; 0, NOT support; -1, error)
     */
    static native int isRdsSupport();

    /**
     * Switch antenna
     *
     * @param antenna antenna (0, long antenna, 1 short antenna)
     *
     * @return (0, success; 1 failed; 2 not support)
     */
    static native int switchAntenna(int antenna);

    /**
     * Set the driver to low power mode.
     *
     * @return (true, success; false, failed)
     */
    static native boolean setLowPowerMode();

    /**
     * Set the driver to normal power mode.
     *
     * @return (true, success; false, failed)
     */
    static native boolean setNormalPowerMode();
}
