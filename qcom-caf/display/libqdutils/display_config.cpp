/*
* Copyright (c) 2013-2014, 2016, 2018-2021, The Linux Foundation. All rights reserved.
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

#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <display_config.h>
#include <QServiceUtils.h>
#include <qd_utils.h>

using namespace android;
using namespace qService;

namespace qdutils {

//=============================================================================
// The functions below run in the client process and wherever necessary
// do a binder call to HWC to get/set data.

int isExternalConnected(void) {
    int ret;
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;
    if(binder != NULL) {
        err = binder->dispatch(IQService::CHECK_EXTERNAL_STATUS,
                &inParcel , &outParcel);
    }
    if(err) {
        ALOGE("%s: Failed to get external status err=%d", __FUNCTION__, err);
        ret = err;
    } else {
        ret = outParcel.readInt32();
    }
    return ret;
}

int getDisplayAttributes(int dpy, DisplayAttributes_t& dpyattr) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;
    inParcel.writeInt32(dpy);
    if(binder != NULL) {
        err = binder->dispatch(IQService::GET_DISPLAY_ATTRIBUTES,
                &inParcel, &outParcel);
    }
    if(!err) {
        dpyattr.vsync_period = outParcel.readInt32();
        dpyattr.xres = outParcel.readInt32();
        dpyattr.yres = outParcel.readInt32();
        dpyattr.xdpi = outParcel.readFloat();
        dpyattr.ydpi = outParcel.readFloat();
        dpyattr.panel_type = outParcel.readInt32();
    } else {
        ALOGE("%s() failed with err %d", __FUNCTION__, err);
    }
    return err;
}

int getDisplayVisibleRegion(int dpy, hwc_rect_t &rect) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;
    inParcel.writeInt32(dpy);
    if(binder != NULL) {
        err = binder->dispatch(IQService::GET_DISPLAY_VISIBLE_REGION,
                &inParcel, &outParcel);
    }
    if(!err) {
        rect.left = outParcel.readInt32();
        rect.top = outParcel.readInt32();
        rect.right = outParcel.readInt32();
        rect.bottom = outParcel.readInt32();
    } else {
        ALOGE("%s: Failed to getVisibleRegion for dpy =%d: err = %d",
              __FUNCTION__, dpy, err);
    }
    return err;
}

int setViewFrame(int dpy, int l, int t, int r, int b) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;
    inParcel.writeInt32(dpy);
    inParcel.writeInt32(l);
    inParcel.writeInt32(t);
    inParcel.writeInt32(r);
    inParcel.writeInt32(b);

    if(binder != NULL) {
        err = binder->dispatch(IQService::SET_VIEW_FRAME,
                &inParcel, &outParcel);
    }
    if(err)
        ALOGE("%s: Failed to set view frame for dpy %d err=%d",
                            __FUNCTION__, dpy, err);

    return err;
}

int setSecondaryDisplayStatus(int dpy, uint32_t status) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;
    inParcel.writeInt32(dpy);
    inParcel.writeInt32(status);

    if(binder != NULL) {
        err = binder->dispatch(IQService::SET_SECONDARY_DISPLAY_STATUS,
                &inParcel, &outParcel);
    }
    if(err)
        ALOGE("%s: Failed for dpy %d status = %d err=%d", __FUNCTION__, dpy,
                                                        status, err);

    return err;
}

int configureDynRefreshRate(uint32_t op, uint32_t refreshRate) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;
    inParcel.writeInt32(op);
    inParcel.writeInt32(refreshRate);

    if(binder != NULL) {
        err = binder->dispatch(IQService::CONFIGURE_DYN_REFRESH_RATE,
                               &inParcel, &outParcel);
    }

    if(err)
        ALOGE("%s: Failed setting op %d err=%d", __FUNCTION__, op, err);

    return err;
}

int getConfigCount(int /*dpy*/) {
    int numConfigs = -1;
    sp<IQService> binder = getBinder();
    if(binder != NULL) {
        Parcel inParcel, outParcel;
        inParcel.writeInt32(DISPLAY_PRIMARY);
        status_t err = binder->dispatch(IQService::GET_CONFIG_COUNT,
                &inParcel, &outParcel);
        if(!err) {
            numConfigs = outParcel.readInt32();
            ALOGI("%s() Received num configs %d", __FUNCTION__, numConfigs);
        } else {
            ALOGE("%s() failed with err %d", __FUNCTION__, err);
        }
    }
    return numConfigs;
}

int getActiveConfig(int dpy) {
    int configIndex = -1;
    sp<IQService> binder = getBinder();
    if(binder != NULL) {
        Parcel inParcel, outParcel;
        inParcel.writeInt32(dpy);
        status_t err = binder->dispatch(IQService::GET_ACTIVE_CONFIG,
                &inParcel, &outParcel);
        if(!err) {
            configIndex = outParcel.readInt32();
            ALOGI("%s() Received active config index %d", __FUNCTION__,
                    configIndex);
        } else {
            ALOGE("%s() failed with err %d", __FUNCTION__, err);
        }
    }
    return configIndex;
}

int setActiveConfig(int configIndex, int /*dpy*/) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    if(binder != NULL) {
        Parcel inParcel, outParcel;
        inParcel.writeInt32(configIndex);
        inParcel.writeInt32(DISPLAY_PRIMARY);
        err = binder->dispatch(IQService::SET_ACTIVE_CONFIG,
                &inParcel, &outParcel);
        if(!err) {
            ALOGI("%s() Successfully set active config index %d", __FUNCTION__,
                    configIndex);
        } else {
            ALOGE("%s() failed with err %d", __FUNCTION__, err);
        }
    }
    return err;
}

DisplayAttributes getDisplayAttributes(int configIndex, int dpy) {
    DisplayAttributes dpyattr = {};
    sp<IQService> binder = getBinder();
    if(binder != NULL) {
        Parcel inParcel, outParcel;
        inParcel.writeInt32(configIndex);
        inParcel.writeInt32(dpy);
        status_t err = binder->dispatch(
                IQService::GET_DISPLAY_ATTRIBUTES_FOR_CONFIG, &inParcel,
                &outParcel);
        if(!err) {
            dpyattr.vsync_period = outParcel.readInt32();
            dpyattr.xres = outParcel.readInt32();
            dpyattr.yres = outParcel.readInt32();
            dpyattr.xdpi = outParcel.readFloat();
            dpyattr.ydpi = outParcel.readFloat();
            dpyattr.panel_type = outParcel.readInt32();
            dpyattr.is_yuv = outParcel.readInt32();
            ALOGI("%s() Received attrs for index %d: xres %d, yres %d",
                    __FUNCTION__, configIndex, dpyattr.xres, dpyattr.yres);
        } else {
            ALOGE("%s() failed with err %d", __FUNCTION__, err);
        }
    }
    return dpyattr;
}

int setPanelMode(int mode) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    if(binder != NULL) {
        Parcel inParcel, outParcel;
        inParcel.writeInt32(mode);
        err = binder->dispatch(IQService::SET_DISPLAY_MODE,
                               &inParcel, &outParcel);
        if(!err) {
            ALOGI("%s() Successfully set the display mode to %d", __FUNCTION__,
                  mode);
        } else {
            ALOGE("%s() failed with err %d", __FUNCTION__, err);
        }
    }
    return err;
}

int setPanelBrightness(int level) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;

    if(binder != NULL) {
        inParcel.writeInt32(level);
        status_t err = binder->dispatch(IQService::SET_PANEL_BRIGHTNESS,
                &inParcel, &outParcel);
        if(err) {
            ALOGE("%s() failed with err %d", __FUNCTION__, err);
        }
    }
    return err;
}

int getPanelBrightness() {
    int panel_brightness = -1;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;

    if(binder != NULL) {
        status_t err = binder->dispatch(IQService::GET_PANEL_BRIGHTNESS,
                &inParcel, &outParcel);
        if(!err) {
            panel_brightness = outParcel.readInt32();
            ALOGI("%s() Current panel brightness value %d", __FUNCTION__,
                    panel_brightness);
        } else {
            ALOGE("%s() failed with err %d", __FUNCTION__, err);
        }
    }
    return panel_brightness;
}

int setDsiClk(int dpy, uint64_t bitClk) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;

    if(binder != NULL) {
        inParcel.writeInt32(dpy);
        inParcel.writeUint64(bitClk);
        status_t err = binder->dispatch(IQService::SET_DSI_CLK, &inParcel, &outParcel);
        if(err) {
            ALOGE("%s() failed with err %d", __FUNCTION__, err);
        }
    }
    return err;
}

uint64_t getDsiClk(int dpy) {
    uint64_t dsi_clk = 0;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;

    if(binder != NULL) {
        inParcel.writeInt32(dpy);
        status_t err = binder->dispatch(IQService::GET_DSI_CLK, &inParcel, &outParcel);
        if(!err) {
            dsi_clk = outParcel.readUint64();
        } else {
            ALOGE("%s() failed with err %d", __FUNCTION__, err);
        }
    }
    return dsi_clk;
}

int getSupportedBitClk(int dpy, std::vector<uint64_t>& bit_rates) {
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;

    if(binder != NULL) {
        inParcel.writeInt32(dpy);
        status_t err = binder->dispatch(IQService::GET_SUPPORTED_DSI_CLK, &inParcel, &outParcel);
        if(err) {
            ALOGE("%s() failed with err %d", __FUNCTION__, err);
            return err;
        }
    }

    int32_t clk_levels = outParcel.readInt32();
    while (clk_levels > 0) {
      bit_rates.push_back(outParcel.readUint64());
      clk_levels--;
    }
    return 0;
}

}// namespace

// ----------------------------------------------------------------------------
// Functions for linking dynamically to libqdutils
// ----------------------------------------------------------------------------
extern "C" int minHdcpEncryptionLevelChanged(int dpy, int min_enc_level) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;
    inParcel.writeInt32(dpy);
    inParcel.writeInt32(min_enc_level);

    if(binder != NULL) {
        err = binder->dispatch(IQService::MIN_HDCP_ENCRYPTION_LEVEL_CHANGED,
                &inParcel, &outParcel);
    }

    if(err) {
        ALOGE("%s: Failed for dpy %d err=%d", __FUNCTION__, dpy, err);
    } else {
        err = outParcel.readInt32();
    }

    return err;
}

extern "C" int refreshScreen() {
    int ret = 0;
    ret = screenRefresh();
    return ret;
}

extern "C" int controlPartialUpdate(int dpy, int mode) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    if(binder != NULL) {
        Parcel inParcel, outParcel;
        inParcel.writeInt32(dpy);
        inParcel.writeInt32(mode);
        err = binder->dispatch(IQService::CONTROL_PARTIAL_UPDATE, &inParcel, &outParcel);
        if(err != 0) {
            ALOGE_IF(getBinder(), "%s() failed with err %d", __FUNCTION__, err);
        } else {
            return outParcel.readInt32();
        }
    }

    return err;
}

// returns 0 if composer is up
extern "C" int waitForComposerInit() {
    int status = false;
    sp<IQService> binder = getBinder();
    if (binder == NULL) {
        sleep(2);
        binder = getBinder();
    }

    if (binder != NULL) {
        Parcel inParcel, outParcel;
        binder->dispatch(IQService::GET_COMPOSER_STATUS, &inParcel, &outParcel);
        status = !!outParcel.readInt32();
        if (!status) {
            sleep(2);
            binder->dispatch(IQService::GET_COMPOSER_STATUS, &inParcel, &outParcel);
            status = !!outParcel.readInt32();
        }
    }

    return !status;
}

extern "C" int setStandByMode(int mode) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;

    if(binder != NULL) {
        inParcel.writeInt32(mode);
        err = binder->dispatch(IQService::SET_STAND_BY_MODE,
              &inParcel, &outParcel);
        if(err) {
            ALOGE("%s() failed with err %d", __FUNCTION__, err);
        }
    }
    return err;
}

extern "C"  int getPanelResolution(int *width, int *height) {
    status_t err = (status_t) FAILED_TRANSACTION;
    sp<IQService> binder = getBinder();
    Parcel inParcel, outParcel;

    if(binder != NULL) {
        err = binder->dispatch(IQService::GET_PANEL_RESOLUTION,
              &inParcel, &outParcel);
        if(err != 0) {
            ALOGE_IF(getBinder(), "%s() failed with err %d", __FUNCTION__, err);
        } else {
            *width = outParcel.readInt32();
            *height = outParcel.readInt32();
        }
    }

    return err;
}
