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
///////////////////////////////////////////////////////////////////////////////
// THIS FILE IS IMMUTABLE. DO NOT EDIT IN ANY CASE.                          //
///////////////////////////////////////////////////////////////////////////////

// This file is a snapshot of an AIDL file. Do not edit it manually. There are
// two cases:
// 1). this is a frozen version file - do not edit this in any case.
// 2). this is a 'current' file. If you make a backwards compatible change to
//     the interface (from the latest frozen version), the build system will
//     prompt you to update this file with `m <name>-update-api`.
//
// You must not make a backward incompatible change to any AIDL file built
// with the aidl_interface module type with versions property set. The module
// type is used to build AIDL files in a way that they can be used across
// independently updatable components of the system. If a device is shipped
// with such a backward incompatible change, it has a high risk of breaking
// later when a module using the interface is updated, e.g., Mainline modules.

package vendor.qti.hardware.display.config;
@VintfStability
interface IDisplayConfig {
  boolean isDisplayConnected(in vendor.qti.hardware.display.config.DisplayType dpy);
  void setDisplayStatus(in vendor.qti.hardware.display.config.DisplayType dpy, in vendor.qti.hardware.display.config.ExternalStatus status);
  void configureDynRefreshRate(in vendor.qti.hardware.display.config.DynRefreshRateOp op, in int refrestRate);
  int getConfigCount(in vendor.qti.hardware.display.config.DisplayType dpy);
  int getActiveConfig(in vendor.qti.hardware.display.config.DisplayType dpy);
  void setActiveConfig(in vendor.qti.hardware.display.config.DisplayType dpy, in int config);
  vendor.qti.hardware.display.config.Attributes getDisplayAttributes(in int configIndex, in vendor.qti.hardware.display.config.DisplayType dpy);
  void setPanelBrightness(in int level);
  int getPanelBrightness();
  void minHdcpEncryptionLevelChanged(in vendor.qti.hardware.display.config.DisplayType dpy, in int minEncLevel);
  void refreshScreen();
  void controlPartialUpdate(in vendor.qti.hardware.display.config.DisplayType dpy, in boolean enable);
  void toggleScreenUpdate(in boolean on);
  void setIdleTimeout(in int value);
  vendor.qti.hardware.display.config.HDRCapsParams getHDRCapabilities(in vendor.qti.hardware.display.config.DisplayType dpy);
  void setCameraLaunchStatus(in int on);
  boolean displayBWTransactionPending();
  void setDisplayAnimating(in long displayId, in boolean animating);
  void controlIdlePowerCollapse(in boolean enable, in boolean synchronous);
  boolean getWriteBackCapabilities();
  void setDisplayDppsAdROI(in int displayId, in int hStart, in int hEnd, in int vStart, in int vEnd, in int factorIn, in int factorOut);
  void updateVSyncSourceOnPowerModeOff();
  void updateVSyncSourceOnPowerModeDoze();
  void setPowerMode(in int dispId, in vendor.qti.hardware.display.config.PowerMode powerMode);
  boolean isPowerModeOverrideSupported(in int dispId);
  boolean isHDRSupported(in int dispId);
  boolean isWCGSupported(in int dispId);
  void setLayerAsMask(in int dispId, in long layerId);
  String getDebugProperty(in String propName);
  vendor.qti.hardware.display.config.Attributes getActiveBuiltinDisplayAttributes();
  void setPanelLuminanceAttributes(in int dispId, in float minLum, in float maxLum);
  boolean isBuiltInDisplay(in int dispId);
  boolean isAsyncVDSCreationSupported();
  void createVirtualDisplay(in int width, in int height, in int format);
  long[] getSupportedDSIBitClks(in int dispId);
  long getDSIClk(in int dispId);
  void setDSIClk(in int dispId, in long bitClk);
  void setCWBOutputBuffer(in vendor.qti.hardware.display.config.IDisplayConfigCallback callback, in int dispId, in vendor.qti.hardware.display.config.Rect rect, in boolean postProcessed, in android.hardware.common.NativeHandle buffer);
  void setQsyncMode(in int dispId, in vendor.qti.hardware.display.config.QsyncMode mode);
  boolean isSmartPanelConfig(in int dispId, in int configId);
  boolean isRotatorSupportedFormat(in int halFormat, in boolean ubwc);
  void controlQsyncCallback(in boolean enable);
  void sendTUIEvent(in vendor.qti.hardware.display.config.DisplayType dpy, in vendor.qti.hardware.display.config.TUIEventType eventType);
  int getDisplayHwId(in int dispId);
  int[] getSupportedDisplayRefreshRates(in vendor.qti.hardware.display.config.DisplayType dpy);
  boolean isRCSupported(in int dispId);
  void controlIdleStatusCallback(in boolean enable);
  boolean isSupportedConfigSwitch(in int dispId, in int config);
  vendor.qti.hardware.display.config.DisplayType getDisplayType(in long physicalDispId);
  void setCameraSmoothInfo(in vendor.qti.hardware.display.config.CameraSmoothOp op, in int fps);
  long registerCallback(in vendor.qti.hardware.display.config.IDisplayConfigCallback callback);
  void unRegisterCallback(in long handle);
}
