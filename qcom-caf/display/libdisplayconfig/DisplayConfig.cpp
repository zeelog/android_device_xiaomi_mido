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

#include <vendor/display/config/1.0/IDisplayConfig.h>

#include "DisplayConfig.h"

namespace display {

using vendor::display::config::V1_0::IDisplayConfig;

//=============================================================================
// The functions below run in the client process and wherever necessary
// do a binder call to HWC to get/set data.

IDisplayConfig::DisplayType MapDisplayType(int dpy) {
    switch (dpy) {
        case DISPLAY_PRIMARY:
            return IDisplayConfig::DisplayType::DISPLAY_PRIMARY;

        case DISPLAY_EXTERNAL:
            return IDisplayConfig::DisplayType::DISPLAY_EXTERNAL;

        case DISPLAY_VIRTUAL:
            return IDisplayConfig::DisplayType::DISPLAY_VIRTUAL;

        default:
            break;
    }

    return IDisplayConfig::DisplayType::INVALID;
}

IDisplayConfig::DisplayExternalStatus MapExternalStatus(uint32_t status) {
    switch (status) {
        case EXTERNAL_OFFLINE:
            return IDisplayConfig::DisplayExternalStatus::EXTERNAL_OFFLINE;

        case EXTERNAL_ONLINE:
            return IDisplayConfig::DisplayExternalStatus::EXTERNAL_ONLINE;

        case EXTERNAL_PAUSE:
            return IDisplayConfig::DisplayExternalStatus::EXTERNAL_PAUSE;

        case EXTERNAL_RESUME:
            return IDisplayConfig::DisplayExternalStatus::EXTERNAL_RESUME;

        default:
            break;
    }

    return IDisplayConfig::DisplayExternalStatus::INVALID;
}

IDisplayConfig::DisplayDynRefreshRateOp MapDynRefreshRateOp(uint32_t op) {
    switch (op) {
        case DISABLE_METADATA_DYN_REFRESH_RATE:
            return IDisplayConfig::DisplayDynRefreshRateOp::DISABLE_METADATA_DYN_REFRESH_RATE;

        case ENABLE_METADATA_DYN_REFRESH_RATE:
            return IDisplayConfig::DisplayDynRefreshRateOp::ENABLE_METADATA_DYN_REFRESH_RATE;

        case SET_BINDER_DYN_REFRESH_RATE:
            return IDisplayConfig::DisplayDynRefreshRateOp::SET_BINDER_DYN_REFRESH_RATE;

        default:
            break;
    }

    return IDisplayConfig::DisplayDynRefreshRateOp::INVALID;
}

int MapDisplayPortType(IDisplayConfig::DisplayPortType panelType) {
    switch (panelType) {
        case IDisplayConfig::DisplayPortType::DISPLAY_PORT_DEFAULT:
            return DISPLAY_PORT_DEFAULT;

        case IDisplayConfig::DisplayPortType::DISPLAY_PORT_DSI:
            return DISPLAY_PORT_DSI;

        case IDisplayConfig::DisplayPortType::DISPLAY_PORT_DTV:
            return DISPLAY_PORT_DTV;

        case IDisplayConfig::DisplayPortType::DISPLAY_PORT_WRITEBACK:
            return DISPLAY_PORT_WRITEBACK;

        case IDisplayConfig::DisplayPortType::DISPLAY_PORT_LVDS:
            return DISPLAY_PORT_LVDS;

        case IDisplayConfig::DisplayPortType::DISPLAY_PORT_EDP:
            return DISPLAY_PORT_EDP;

        case IDisplayConfig::DisplayPortType::DISPLAY_PORT_DP:
            return DISPLAY_PORT_DP;

        default:
            break;
    }

    return -1;
}

int isExternalConnected() {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return 0;
    }

    int connected = 0;
    intf->isDisplayConnected(IDisplayConfig::DisplayType::DISPLAY_EXTERNAL,
        [&](const auto &tmpError, const auto &tmpStatus) {
            if (tmpError) {
                return;
            }

            connected = tmpStatus;
        });

    return connected;
}

int setSecondayDisplayStatus(int dpy, uint32_t status) {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return -1;
    }

    return intf->setSecondayDisplayStatus(MapDisplayType(dpy), MapExternalStatus(status));
}

int configureDynRefeshRate(uint32_t op, uint32_t refreshRate) {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return -1;
    }

    return intf->configureDynRefeshRate(MapDynRefreshRateOp(op), refreshRate);
}

int getConfigCount(int dpy) {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return -1;
    }

    int count = 0;
    intf->getConfigCount(MapDisplayType(dpy),
        [&](const auto &tmpError, const auto &tmpCount) {
            if (tmpError) {
                return;
            }

            count = tmpCount;
        });

    return count;
}

int getActiveConfig(int dpy) {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return -1;
    }

    int config = 0;
    intf->getActiveConfig(MapDisplayType(dpy),
        [&](const auto &tmpError, const auto &tmpConfig) {
            if (tmpError) {
                return;
            }

            config = tmpConfig;
        });

    return config;
}

int setActiveConfig(int dpy, uint32_t config) {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return -1;
    }

    return intf->setActiveConfig(MapDisplayType(dpy), config);
}

DisplayAttributes getDisplayAttributes(uint32_t configIndex, int dpy) {
    DisplayAttributes attributes;

    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return attributes;
    }

    intf->getDisplayAttributes(configIndex, MapDisplayType(dpy),
        [&](const auto &tmpError, const auto &tmpAttributes) {
            if (tmpError) {
                return;
            }

            attributes.vsync_period = tmpAttributes.vsyncPeriod;
            attributes.xres = tmpAttributes.xRes;
            attributes.yres = tmpAttributes.yRes;
            attributes.xdpi = tmpAttributes.xDpi;
            attributes.ydpi = tmpAttributes.yDpi;
            attributes.panel_type = MapDisplayPortType(tmpAttributes.panelType);
            attributes.is_yuv = tmpAttributes.isYuv;
        });

    return attributes;
}

int setPanelBrightness(uint32_t level) {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return -1;
    }

    return intf->setPanelBrightness(level);
}

uint32_t getPanelBrightness() {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return 0;
    }

    int level = 0;
    intf->getPanelBrightness(
        [&](const auto &tmpError, const auto &tmpLevel) {
            if (tmpError) {
                return;
            }

            level = tmpLevel;
        });

    return level;
}

int minHdcpEncryptionLevelChanged(int dpy, uint32_t min_enc_level) {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return -1;
    }

    return intf->minHdcpEncryptionLevelChanged(MapDisplayType(dpy), min_enc_level);
}

int refreshScreen() {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return -1;
    }

    return intf->refreshScreen();
}

int controlPartialUpdate(int dpy, bool enable) {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return -1;
    }

    return intf->controlPartialUpdate(MapDisplayType(dpy), enable);
}

int toggleScreenUpdate(uint32_t on) {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return -1;
    }

    return intf->toggleScreenUpdate(on == 1);
}

int setIdleTimeout(uint32_t value) {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return -1;
    }

    return intf->setIdleTimeout(value);
}

int getHDRCapabilities(int dpy, DisplayHDRCapabilities *caps) {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL || caps == NULL) {
        return -1;
    }

    int error = -1;
    intf->getHDRCapabilities(MapDisplayType(dpy),
        [&](const auto &tmpError, const auto &tmpCaps) {
            error = tmpError;
            if (error) {
                return;
            }

            caps->supported_hdr_types = tmpCaps.supportedHdrTypes;
            caps->max_luminance = tmpCaps.maxLuminance;
            caps->max_avg_luminance = tmpCaps.maxAvgLuminance;
            caps->min_luminance = tmpCaps.minLuminance;
        });

    return error;
}

int setCameraLaunchStatus(uint32_t on) {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return -1;
    }

    return intf->setCameraLaunchStatus(on);
}

bool displayBWTransactionPending() {
    android::sp<IDisplayConfig> intf = IDisplayConfig::getService();
    if (intf == NULL) {
        return 0;
    }

    int status = 0;
    intf->displayBWTransactionPending(
        [&](const auto &tmpError, const auto &tmpStatus) {
            if (tmpError) {
                return;
            }

            status = tmpStatus;
        });

    return status;
}

} // namespace display
