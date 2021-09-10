/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012-2014, 2016-2017, 2018, 2021, The Linux Foundation. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are
 * retained for attribution purposes only.

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

#ifndef ANDROID_IQSERVICE_H
#define ANDROID_IQSERVICE_H

#include <stdint.h>
#include <sys/types.h>
#include <utils/Errors.h>
#include <utils/RefBase.h>
#include <binder/IInterface.h>
#include <binder/IBinder.h>
#include <IQClient.h>
#include <IQHDMIClient.h>


namespace qService {
// ----------------------------------------------------------------------------

class IQService : public android::IInterface
{
public:
    DECLARE_META_INTERFACE(QService);
    enum {
        COMMAND_LIST_START = android::IBinder::FIRST_CALL_TRANSACTION,
        GET_PANEL_BRIGHTNESS = 2, // Provides ability to set the panel brightness
        SET_PANEL_BRIGHTNESS = 3, // Provides ability to get the panel brightness
        CONNECT_HWC_CLIENT = 4, // Connect to qservice
        SCREEN_REFRESH = 5,     // Refresh screen through SF invalidate
        EXTERNAL_ORIENTATION = 6,// Set external orientation
        BUFFER_MIRRORMODE = 7,  // Buffer mirrormode
        CHECK_EXTERNAL_STATUS = 8,// Check status of external display
        GET_DISPLAY_ATTRIBUTES = 9,// Get display attributes
        SET_HSIC_DATA = 10,     // Set HSIC on dspp
        GET_DISPLAY_VISIBLE_REGION = 11,// Get the visibleRegion for dpy
        SET_SECONDARY_DISPLAY_STATUS = 12,// Sets secondary display status
        SET_MAX_PIPES_PER_MIXER = 13,// Set max pipes per mixer for MDPComp
        SET_VIEW_FRAME = 14,    // Set view frame of display
        DYNAMIC_DEBUG = 15,     // Enable more logging on the fly
        SET_IDLE_TIMEOUT = 16,  // Set idle timeout for GPU fallback
        TOGGLE_BWC = 17,           // Toggle BWC On/Off on targets that support
        /* Enable/Disable/Set refresh rate dynamically */
        CONFIGURE_DYN_REFRESH_RATE = 18,
        CONTROL_PARTIAL_UPDATE = 19,   // Provides ability to enable/disable partial update
        TOGGLE_SCREEN_UPDATES = 20, // Provides ability to set the panel brightness
        SET_FRAME_DUMP_CONFIG = 21,  // Provides ability to set the frame dump config
        SET_S3D_MODE = 22, // Set the 3D mode as specified in msm_hdmi_modes.h
        CONNECT_HDMI_CLIENT = 23,  // Connect HDMI CEC HAL Client
        QDCM_SVC_CMDS = 24,        // request QDCM services.
        SET_ACTIVE_CONFIG = 25, //Set a specified display config
        GET_ACTIVE_CONFIG = 26, //Get the current config index
        GET_CONFIG_COUNT = 27, //Get the number of supported display configs
        GET_DISPLAY_ATTRIBUTES_FOR_CONFIG = 28, //Get attr for specified config
        SET_DISPLAY_MODE = 29, // Set display mode to command or video mode
        SET_CAMERA_STATUS = 30, // To notify display when camera is on and off
        MIN_HDCP_ENCRYPTION_LEVEL_CHANGED = 31,
        GET_BW_TRANSACTION_STATUS = 32, //Client can query BW transaction status.
        SET_LAYER_MIXER_RESOLUTION = 33, // Enables client to set layer mixer resolution.
        SET_COLOR_MODE = 34, // Overrides the QDCM mode on the display
        GET_HDR_CAPABILITIES = 35, // Get HDR capabilities for legacy HWC interface
        SET_DSI_CLK = 36, // Set DSI Clk.
        GET_DSI_CLK = 37, // Get DSI Clk.
        GET_SUPPORTED_DSI_CLK = 38, // Get supported DSI Clk.
        GET_COMPOSER_STATUS = 39, // Get composer init status-true if primary display init is done
	SET_STAND_BY_MODE = 40, //Set stand by mode for MDP hardware.
        GET_PANEL_RESOLUTION = 41, // Get Panel Resolution
        COMMAND_LIST_END = 400,
    };

    enum {
        END = 0,
        START,
    };

    enum {
        DEBUG_ALL,
        DEBUG_MDPCOMP,
        DEBUG_VSYNC,
        DEBUG_VD,
        DEBUG_PIPE_LIFECYCLE,
        DEBUG_DRIVER_CONFIG,
        DEBUG_ROTATOR,
        DEBUG_QDCM,
        DEBUG_SCALAR,
        DEBUG_CLIENT,
        DEBUG_DISPLAY,
        DEBUG_MAX_VAL = DEBUG_DISPLAY, // Used to check each bit of the debug command paramater.
        // Update DEBUG_MAX_VAL when adding new debug tag.
    };

    enum {
        PREF_POST_PROCESSING,
        PREF_PARTIAL_UPDATE,
        ENABLE_PARTIAL_UPDATE,
    };

    // Register a HWC client that can be notified
    // This client is generic and is intended to get
    // dispatches of all events calling into QService
    virtual void connect(const android::sp<qClient::IQClient>& client) = 0;
    // Register an HDMI client. This client gets notification of HDMI events
    // such as plug/unplug and CEC messages
    virtual void connect(const android::sp<qClient::IQHDMIClient>& client) = 0;
    // Generic function to dispatch binder commands
    // The type of command decides how the data is parceled
    virtual android::status_t dispatch(uint32_t command,
            const android::Parcel* inParcel,
            android::Parcel* outParcel) = 0;
};

// ----------------------------------------------------------------------------

class BnQService : public android::BnInterface<IQService>
{
public:
    virtual android::status_t onTransact( uint32_t code,
                                          const android::Parcel& data,
                                          android::Parcel* reply,
                                          uint32_t flags = 0);
};

// ----------------------------------------------------------------------------
}; // namespace qService

#endif // ANDROID_IQSERVICE_H
