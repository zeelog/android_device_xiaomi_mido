///////////////////////////////////////////////////////////////////////////////
// THIS FILE IS IMMUTABLE. DO NOT EDIT IN ANY CASE.                          //
///////////////////////////////////////////////////////////////////////////////

// This file is a snapshot of an AIDL interface (or parcelable). Do not try to
// edit this file. It looks like you are doing that because you have modified
// an AIDL interface in a backward-incompatible way, e.g., deleting a function
// from an interface or a field from a parcelable and it broke the build. That
// breakage is intended.
//
// You must not make a backward incompatible changes to the AIDL files built
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
}
