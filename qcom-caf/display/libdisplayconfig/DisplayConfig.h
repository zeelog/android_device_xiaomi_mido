/*
* Copyright (c) 2017 The Linux Foundation. All rights reserved.
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

#ifndef __DISPLAY_CONFIG_H__
#define __DISPLAY_CONFIG_H__

#include <stdint.h>
#include <vector>

// This header is for clients to use to set/get global display configuration.

namespace display {

enum {
    DISPLAY_PRIMARY = 0,
    DISPLAY_EXTERNAL,
    DISPLAY_VIRTUAL,
};

enum {
    EXTERNAL_OFFLINE = 0,
    EXTERNAL_ONLINE,
    EXTERNAL_PAUSE,
    EXTERNAL_RESUME,
};

enum {
    DISABLE_METADATA_DYN_REFRESH_RATE = 0,
    ENABLE_METADATA_DYN_REFRESH_RATE,
    SET_BINDER_DYN_REFRESH_RATE,
};

enum {
    DISPLAY_PORT_DEFAULT = 0,
    DISPLAY_PORT_DSI,
    DISPLAY_PORT_DTV,
    DISPLAY_PORT_WRITEBACK,
    DISPLAY_PORT_LVDS,
    DISPLAY_PORT_EDP,
    DISPLAY_PORT_DP,
};

struct DisplayAttributes {
    uint32_t vsync_period = 0; //nanoseconds
    uint32_t xres = 0;
    uint32_t yres = 0;
    float xdpi = 0.0f;
    float ydpi = 0.0f;
    int panel_type = DISPLAY_PORT_DEFAULT;
    bool is_yuv = false;
};

struct DisplayHDRCapabilities {
    std::vector<int32_t> supported_hdr_types;
    float max_luminance = 0.0f;
    float max_avg_luminance = 0.0f;
    float min_luminance = 0.0f;
};

//=============================================================================
// The functions below run in the client pocess and wherever necessary
// do a binder call to HWC to get/set data.

int isExternalConnected();
int setSecondayDisplayStatus(int dpy, uint32_t status);
int configureDynRefeshRate(uint32_t op, uint32_t refreshRate);
int getConfigCount(int dpy);
int getActiveConfig(int dpy);
int setActiveConfig(int dpy, uint32_t config);
DisplayAttributes getDisplayAttributes(uint32_t configIndex, int dpy);
int setPanelBrightness(uint32_t level);
uint32_t getPanelBrightness();
int minHdcpEncryptionLevelChanged(int dpy, uint32_t min_enc_level);
int refreshScreen();
int controlPartialUpdate(int dpy, bool enable);
int toggleScreenUpdate(uint32_t on);
int setIdleTimeout(uint32_t value);
int getHDRCapabilities(int dpy, DisplayHDRCapabilities *caps);
int setCameraLaunchStatus(uint32_t on);
bool displayBWTransactionPending();

} // namespace display

#endif  // __DISPLAY_CONFIG_H__
