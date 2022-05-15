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

import vendor.qti.hardware.display.config.Attributes;
import vendor.qti.hardware.display.config.DisplayType;
import vendor.qti.hardware.display.config.DynRefreshRateOp;
import vendor.qti.hardware.display.config.ExternalStatus;
import vendor.qti.hardware.display.config.HDRCapsParams;
import vendor.qti.hardware.display.config.IDisplayConfigCallback;
import vendor.qti.hardware.display.config.PowerMode;
import vendor.qti.hardware.display.config.QsyncMode;
import vendor.qti.hardware.display.config.Rect;
import vendor.qti.hardware.display.config.TUIEventType;
import vendor.qti.hardware.display.config.CameraSmoothOp;

@VintfStability
interface IDisplayConfig {
    /*
     * Query whether a given display type is connected.
     *
     * @param dpy display type
     *
     * @return true when connected, false when disconnected
     */
    boolean isDisplayConnected(in DisplayType dpy);

    /*
     * Set the secondary display status (pause/resume/offline) etc.
     *
     * @param dpy display type
     * @param status next status to be set
     *
     * @return error is NONE upon success
     */
    void setDisplayStatus(in DisplayType dpy, in ExternalStatus status);

    /*
     * Enable/Disable/Set refresh rate dynamically.
     *
     * @param op operation code defined in DisplayDynRefreshRateOp
     * @param refreshRate refresh rate value
     *
     * @return error is NONE upon success
     */
    void configureDynRefreshRate(in DynRefreshRateOp op, in int refrestRate);

    /*
     * Query the number of configurations a given display can support.
     *
     * @param dpy display type
     *
     * @return number of configurations
     */
    int getConfigCount(in DisplayType dpy);

    /*
     * Query the config index of a given display type.
     *
     * @param[in] dpy display type
     *
     * @return config index of the display type
     */
    int getActiveConfig(in DisplayType dpy);

    /*
     * Set a new display configuration pointed by the index.
     *
     * @param dpy display type
     * @param config config index
     *
     * @return error is NONE upon success
     */
    void setActiveConfig(in DisplayType dpy, in int config);

    /*
     * Query the display attributes of the specified config index.
     *
     * @param configIndex config index
     * @param dpy display type
     *
     * @return attributes display attributes
     */
    Attributes getDisplayAttributes(in int configIndex, in DisplayType dpy);

    /*
     * Set the panel brightness of the primary display.
     *
     * @param level brightness level
     *
     * @return error is NONE upon success
     */
    void setPanelBrightness(in int level);

    /*
     * Query the panel brightness of the primary display.
     *
     * @return level brightness level
     */
    int getPanelBrightness();

    /*
     * Indicates display about a change in minimum encryption level.
     *
     * @param dpy display type
     * @param minEncLevel encryption level
     *
     * @return error is NONE upon success
     */
    void minHdcpEncryptionLevelChanged (in DisplayType dpy, in int minEncLevel);

    /*
     * Requests display to recompose and invalidate the display pipeline.
     *
     * @return error is NONE upon success
     */
    void refreshScreen();

    /*
     * Enable/Disable partial update.
     *
     * @param dpy display type
     * @param enable enable/disable
     *
     * @return error is NONE upon success
     */
    void controlPartialUpdate(in DisplayType dpy, in boolean enable);

    /*
     * Toggle screen update.
     *
     * @param on if true, pause display and drop incoming draw cycles
     *
     * @return error is NONE upon success
     */
    void toggleScreenUpdate(in boolean on);

    /*
     * Set idle timeout value for video mode panels.
     *
     * @param value idle timeout value
     *
     * @return error is NONE upon success
     */
    void setIdleTimeout(in int value);

    /*
     * Query the HDR capabilities of a given display type.
     *
     * @param dpy display type.
     *
     * @return HDR capabilities
     */
    HDRCapsParams getHDRCapabilities(in DisplayType dpy);

    /*
     * Set the camera application's status (start/stop).
     *
     * @param on if true, camera is started
     *
     * @return error is NONE upon success
     */
    void setCameraLaunchStatus(in int on);

    /*
     * Query the bandwidth transaction status.
     *
     * @return true if transaction is still pending
     */
    boolean displayBWTransactionPending();

    /*
     * Set display animating property.
     *
     * @param displayId display Id
     * @param animating if true, the display is animating
     *
     * @return error is NONE upon success
     */
    void setDisplayAnimating(in long displayId, in boolean animating);

    /*
     * Enable/disable idle power collapse.
     *
     * @param enable enable/disable
     * @param synchronous commit
     *
     * @return error is NONE upon success
     */
    void controlIdlePowerCollapse(in boolean enable, in boolean synchronous);

    /*
     * Query whether UBWC writeback is supported.
     *
     * @return true if supported, false if not supported
     */
    boolean getWriteBackCapabilities();

    /*
     * Set the region of interest of display dpps ad4
     *
     * @param display_id ID of this display
     * @param hStart start in hotizontal direction
     * @param hEnd end in hotizontal direction
     * @param vStart start in vertical direction
     * @param vEnd end in vertical direction
     * @param factorIn the strength factor of inside ROI region
     * @param factorOut the strength factor of outside ROI region
     *
     * @return error is NONE upon success
     */
    void setDisplayDppsAdROI(in int displayId, in int hStart, in int hEnd, in int vStart,
                             in int vEnd, in int factorIn, in int factorOut);

    /*
     * Update vsync source to next active display upon the power state
     * change to off.
     *
     * @return error is NONE upon success
     */
    void updateVSyncSourceOnPowerModeOff();

    /*
     * Update vsync source to next active display upon the power state
     * change to doze.
     *
     * @return error is NONE upon success
     */
    void updateVSyncSourceOnPowerModeDoze();

    /*
     * Sets new power mode on the specificied display.
     *
     * @param dispId display identifier used between client & service
     * @param powerMode new power mode
     *
     * @return error is NONE upon success
     */
    void setPowerMode(in int dispId, in PowerMode powerMode);

    /*
     * Query if power mode override is supported by underlying implementation
     * for the specified display.
     *
     * @param dispId display identifier used between client & service
     *
     * @return true if supported, false if not supported
     */
    boolean isPowerModeOverrideSupported(in int dispId);

    /*
     * Query if hdr is supported by the underlying implementation for the specified display.
     *
     * @param dispId display identifier used between client & service
     *
     * @return true if supported, false if not supported
     */
    boolean isHDRSupported(in int dispId);

    /*
     * Query if wide color gamut is supported by the underlying implementation
     * for the specified display.
     *
     * @param dispId display identifier used between client & service
     *
     * @return true if supported, false if not supported
     */
    boolean isWCGSupported(in int dispId);

    /*
     * Set layer as a mask type (e.g. round corner) identified by the layer id.
     *
     * @param dispId display identifier used between client & service
     * @param layerId layer id used for communication with hwc
     *
     * @return error is NONE upon success
     */
    void setLayerAsMask(in int dispId, in long layerId);

    /*
     * Query the value corresponding to the specified property string from hwc.
     *
     * @param propName name of the property
     *
     * @return value corresponding to the property
     */
    String getDebugProperty(in String propName);

    /*
     * Query the attributes for the active builtin display. If all
     * builtin displays are active, it returns primary display attributes.
     *
     * @return active display attributes
     */
    Attributes getActiveBuiltinDisplayAttributes();

    /*
     * Set the min and max luminance attributes required for dynamic
     * tonemapping of external device.
     *
     * @param dispId display identifier used between client & service
     * @param minLum min luminance supported by external device
     * @param maxLum max luminance supported by external device
     *
     * @return error is NONE upon success
     */
    void setPanelLuminanceAttributes(in int dispId, in float minLum, in float maxLum);

    /*
     * Query if the underlying display is of Built-In Type.
     *
     * @param dispId display identifier used between client & service
     *
     * @return true if display is of Built-In type
     */
    boolean isBuiltInDisplay(in int dispId);

    /*
     * Query if asynchronous Virtual Display Creation is supported
     *
     * @return true if async Virtual display creation is supported
     */
    boolean isAsyncVDSCreationSupported();

    /*
     * Creates Virtual Display based on width, height, format parameters.
     *
     * @param width Width of the virtual display
     * @param height Height of the virtual display
     * @param format Pixel format of the virtual display
     *
     * @return error is NONE upon success
     */
    void createVirtualDisplay(in int width, in int height, in int format);

    /*
     * Query the supported bit clock values of a given display ID.
     *
     * @param dispId display id
     *
     * @return vector of bit clock values
     */
    long[] getSupportedDSIBitClks(in int dispId);

    /*
     * Query the current bit clock value of a given display ID.
     *
     * @param dispId display id.
     *
     * @return bit clock value
     */
    long getDSIClk(in int dispId);

    /*
     * Set the bit clock value of a given display ID.
     *
     * @param dispId display id
     * @param bitClk desired bit clock value
     *
     * @return error is NONE upon success
     */
    void setDSIClk(in int dispId, in long bitClk);

    /*
     * Set the output buffer to be filled with the contents of the next
     * composition performed for this display. Client can specify cropping
     * rectangle for the partial concurrent writeback.
     * Buffer must be ready for writeback before this API is called.
     * If hardware protected content is displayed in next composition cycle,
     * CWB output buffer will be returned as failure in callback and without
     * any change in buffer.
     *
     * @param dispId display id where concurrent writeback shall be captured
     * @param rect cropping rectangle which shall be applied on blended output
     * @param postProcessed whether to capture post processed or mixer output
     * @param buffer buffer where concurrent writeback output shall be written
     *
     * @return error is NONE upon success
     */
    void setCWBOutputBuffer(in IDisplayConfigCallback callback, in int dispId, in Rect rect,
                            in boolean postProcessed, in NativeHandle buffer);

    /*
     * Set the desired qsync mode which will ideally take effect from next
     * composition cycle. Mode change may take longer than one cycle if there
     * is a conflict with current operation mode.
     *
     * @param dispId display id
     * @param mode desired qsync mode
     *
     * @return error is NONE upon success
     */
    void setQsyncMode(in int dispId, in QsyncMode mode);

    /*
     * Query if the specified display config has smart panel.
     *
     * @param dispId display Id
     * @param configId display config index
     *
     * @return true if the DisplayConfig has Smart Panel
     */
    boolean isSmartPanelConfig(in int dispId, in int configId);

    /*
     * Query if the given format is supported by rotator.
     *
     * @param format pass HAL_PIXEL_FORMAT for the validation
     * @param ubwc true if the given format is ubwc format otherwise false
     *
     * @return true if supported or false
     */
    boolean isRotatorSupportedFormat(in int halFormat, in boolean ubwc);

    /*
     * Enable/Display qsync callback.
     *
     * @param enable true if enabling qsync callback
     *
     * @return error is NONE upon success
     */
    void controlQsyncCallback(in boolean enable);

    /*
     * Notify TUI transition events to HW Composer.
     *
     * @param dpy display type
     * @param eventType TUI Event Type
     *
     * @return error is NONE upon success
     */
    void sendTUIEvent(in DisplayType dpy, in TUIEventType eventType);

    /*
     * Query the display HW Id of a given display from HWC HAL
     *
     * @param dispId display Id
     *
     * @return display HW ID
     */
    int getDisplayHwId(in int dispId);

    /*
     * Query the supported refresh rates from Display HAL. The API would return a vector of
     * supported fps in the current config group.
     *
     * @param dpy display type
     *
     * @return supported refresh rates
     */
    int[] getSupportedDisplayRefreshRates(in DisplayType dpy);

    /*
     * Query if the rounded corner feature is supported.
     *
     * @param dispId display Id
     *
     * @return true if supported, false otherwise
     */
    boolean isRCSupported(in int dispId);

    /*
     * Control the idle status callback.
     *
     * @param enable true if idle callback is enabled
     *
     * @return error is NONE upon success
     */
    void controlIdleStatusCallback(in boolean enable);

    /*
     * Query if the config switch is supported on the display.
     *
     * @param dispId display ID
     * @param config config bit mask
     *
     * @return true if supported, false otherwise
     */
    boolean isSupportedConfigSwitch(in int dispId, in int config);

    /*
     * Query the display type information for a given physical display id.
     *
     * @param physicalDispId physical display ID
     *
     * @return display type
     */
    DisplayType getDisplayType(in long physicalDispId);

    /*
     * Set the camera smooth info
     *
     * @param op enable or disable camera smooth
     * @param fps camera fps
     *
     * @return error is NONE upon success
     */
    void setCameraSmoothInfo(in CameraSmoothOp op, in int fps);

    /**
     * Register a callback for any display config info.
     *
     * Registering a new callback must not unregister the old one; the old
     * callback remains registered until one of the following happens:
     * - A client explicitly calls {@link unregisterCallback} to unregister it.
     * - The client process that hosts the callback dies.
     *
     * @param callback the callback to register.
     * @return: returns callback handle if successful.
     */
    long registerCallback(in IDisplayConfigCallback callback);

    /**
     * Explicitly unregister a callback that is previously registered through
     * {@link registerCallback}.
     *
     * @param handle the callback handle to unregister
     * @return: returns ok if successful.
     */
    void unRegisterCallback(in long handle);

    /*
     * Notify idle state to HWC HAL for a given display ID
     *
     * @param dispId display ID
     *
     * @return error is NONE upon success
     */
    void notifyDisplayIdleState(in int[] dispId);
}
