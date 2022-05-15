/*
 * Copyright (c) 2021 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *  * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *  * Neither the name of The Linux Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

package vendor.qti.hardware.display.config;

import android.hardware.common.NativeHandle;
import vendor.qti.hardware.display.config.CameraSmoothOp;
import vendor.qti.hardware.display.config.Attributes;
import vendor.qti.hardware.display.config.Concurrency;

@VintfStability
interface IDisplayConfigCallback {
    /*
     * Send notification when concurrent writeback process completes.
     *
     * @param error result of the CWB process
     * @param buffer buffer for concurrent writeback
     */
    oneway void notifyCWBBufferDone(in int error, in NativeHandle buffer);

    /*
     * Send notification when there are changes on the Qsync.
     *
     * @param qsyncEnabled Qsync status
     * @param refreshRate refresh rate
     * @param qsyncRefreshRate Qsync refresh rate
     */
    oneway void notifyQsyncChange(in boolean qsyncEnabled, in int refreshRate,
                                  in int qsyncRefreshRate);

    /*
     * Send notification whether the device is in idle state.
     *
     * @param isIdle idle status
     */
    oneway void notifyIdleStatus(in boolean isIdle);

    /*
     * Send notification about camera smooth info.
     *
     * @param op enable or disable camera smooth feature
     * @param fps camera frame rate
     */
    oneway void notifyCameraSmoothInfo(in CameraSmoothOp op, in int fps);

    /*
     * Send notification when display resolution is changed by composer.
     *
     * @param displayId the display on which resolution switch is done
     * @param attr Attributes of the new display resolution
     */
    oneway void notifyResolutionChange(in int displayId, in Attributes attr);
     /*
      * Send mitigated fps when new display concurrency added like primary + wfd
      * @param displayId the display on which resolution switch is done
      * @param attr Attributes of the new display resolution
      * @param concurrency Concurrency for display
      */
      oneway void notifyFpsMitigation(in int displayId, in Attributes attr, in Concurrency
                                      concurrency);
}
