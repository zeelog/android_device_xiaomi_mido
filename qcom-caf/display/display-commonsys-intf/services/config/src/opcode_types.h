/*
* Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
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

#ifndef __OPCODE_TYPES_H__
#define __OPCODE_TYPES_H__

namespace DisplayConfig {

enum OpCode {
  kIsDisplayConnected = 0,
  kSetDisplayStatus = 1,
  kConfigureDynRefreshRate = 2,
  kGetConfigCount = 3,
  kGetActiveConfig = 4,
  kSetActiveConfig = 5,
  kGetDisplayAttributes = 6,
  kSetPanelBrightness = 7,
  kGetPanelBrightness =  8,
  kMinHdcpEncryptionLevelChanged = 9,
  kRefreshScreen = 10,
  kControlPartialUpdate = 11,
  kToggleScreenUpdate = 12,
  kSetIdleTimeout = 13,
  kGetHdrCapabilities = 14,
  kSetCameraLaunchStatus = 15,
  kDisplayBwTransactionPending = 16,
  kSetDisplayAnimating = 17,
  kControlIdlePowerCollapse = 18,
  kGetWritebackCapabilities = 19,
  kSetDisplayDppsAdRoi = 20,
  kUpdateVsyncSourceOnPowerModeOff = 21,
  kUpdateVsyncSourceOnPowerModeDoze = 22,
  kSetPowerMode = 23,
  kIsPowerModeOverrideSupported = 24,
  kIsHdrSupported = 25,
  kIsWcgSupported = 26,
  kSetLayerAsMask = 27,
  kGetDebugProperty = 28,
  kGetActiveBuiltinDisplayAttributes = 29,
  kSetPanelLuminanceAttributes = 30,
  kIsBuiltinDisplay = 31,
  kSetCwbOutputBuffer = 32,
  kGetSupportedDsiBitclks = 33,
  kGetDsiClk = 34,
  kSetDsiClk = 35,
  kSetQsyncMode = 36,
  kIsSmartPanelConfig = 37,
  kIsAsyncVdsSupported = 38,
  kCreateVirtualDisplay = 39,
  kIsRotatorSupportedFormat = 40,
  kControlQsyncCallback = 41,
  kSendTUIEvent = 42,
  kGetDisplayHwId = 43,
  kGetSupportedDisplayRefreshRates = 44,
  kIsRCSupported = 45,
  kControlIdleStatusCallback = 46,
  kIsSupportedConfigSwitch = 47,
  kGetDisplayType = 48,
  kAllowIdleFallback = 49,
  kGetDisplayTileCount = 50,
  kSetPowerModeTiled = 51,
  kSetPanelBrightnessTiled = 52,
  kSetWiderModePref = 53,

  kDestroy = 0xFFFF, // Destroy sequence execution
};

}  // namespace DisplayConfig

#endif  // __OPCODE_TYPES_H__
