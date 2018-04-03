/* Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
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
*
*/

#define LOG_TAG "QCamera2Factory"

// System dependencies
#include <stdlib.h>
#include <utils/Errors.h>
#include <cutils/properties.h>

// Camera dependencies
#ifdef QCAMERA_HAL1_SUPPORT
#include "hardware/camera.h"
#include "HAL/QCamera2HWI.h"
#include "QCameraMuxer.h"
#endif

#include "hardware/camera3.h"
#include "HAL3/QCamera3HWI.h"
#include "util/QCameraFlash.h"
#include "QCamera2Factory.h"
extern "C" {
#include "mm_camera_dbg.h"
}

using namespace android;

namespace qcamera {

QCamera2Factory *gQCamera2Factory = NULL;
pthread_mutex_t gCamLock = PTHREAD_MUTEX_INITIALIZER;
#ifdef QCAMERA_HAL1_SUPPORT
QCameraMuxer *gQCameraMuxer = NULL;
#endif

//Total number of cameras opened simultaneously.
//This variable updation is protected by gCamLock.
uint8_t gNumCameraSessions = 0;

volatile uint32_t gKpiDebugLevel = 1;

/*===========================================================================
 * FUNCTION   : QCamera2Factory
 *
 * DESCRIPTION: default constructor of QCamera2Factory
 *
 * PARAMETERS : none
 *
 * RETURN     : None
 *==========================================================================*/
QCamera2Factory::QCamera2Factory()
{
    mHalDescriptors = NULL;
    mCallbacks = NULL;
    mNumOfCameras = get_num_of_cameras();
    int bDualCamera = 0;
    char propDefault[PROPERTY_VALUE_MAX];
    char prop[PROPERTY_VALUE_MAX];
    property_get("persist.camera.HAL3.enabled", prop, "1");
    int isHAL3Enabled = atoi(prop);
#ifndef QCAMERA_HAL1_SUPPORT
    isHAL3Enabled = 1;
#endif

    // Signifies whether system has to enable dual camera mode
    snprintf(propDefault, PROPERTY_VALUE_MAX, "%d", isDualCamAvailable(isHAL3Enabled));
    property_get("persist.camera.dual.camera", prop, propDefault);
    bDualCamera = atoi(prop);
    LOGH("dualCamera:%d ", bDualCamera);
#ifndef QCAMERA_HAL1_SUPPORT
    bDualCamera = 0;
#endif

    if(bDualCamera) {
        LOGI("Enabling QCamera Muxer");
#ifdef QCAMERA_HAL1_SUPPORT
        if (!gQCameraMuxer) {
            QCameraMuxer::getCameraMuxer(&gQCameraMuxer, mNumOfCameras);
            if (!gQCameraMuxer) {
                LOGE("Error !! Failed to get QCameraMuxer");
            }
        }
#endif
    }
#ifdef QCAMERA_HAL1_SUPPORT
    if (!gQCameraMuxer && (mNumOfCameras > 0) &&(mNumOfCameras <= MM_CAMERA_MAX_NUM_SENSORS)) {
#else
    if ((mNumOfCameras > 0) &&(mNumOfCameras <= MM_CAMERA_MAX_NUM_SENSORS)) {
#endif
        mHalDescriptors = new hal_desc[mNumOfCameras];
        if ( NULL != mHalDescriptors) {
            uint32_t cameraId = 0;

            for (int i = 0; i < mNumOfCameras ; i++, cameraId++) {
                mHalDescriptors[i].cameraId = cameraId;
                // Set Device version to 3.x when both HAL3 is enabled & its BAYER sensor
                if (isHAL3Enabled && !(is_yuv_sensor(cameraId))) {
                    mHalDescriptors[i].device_version =
                            CAMERA_DEVICE_API_VERSION_3_0;
                } else {
                    mHalDescriptors[i].device_version =
                            CAMERA_DEVICE_API_VERSION_1_0;
                }
            }
        } else {
            LOGE("Not enough resources to allocate HAL descriptor table!");
        }
    } else {
        LOGI("%d camera devices detected!", mNumOfCameras);
    }
}

/*===========================================================================
 * FUNCTION   : ~QCamera2Factory
 *
 * DESCRIPTION: deconstructor of QCamera2Factory
 *
 * PARAMETERS : none
 *
 * RETURN     : None
 *==========================================================================*/
QCamera2Factory::~QCamera2Factory()
{
    if ( NULL != mHalDescriptors ) {
        delete [] mHalDescriptors;
    }
#ifdef QCAMERA_HAL1_SUPPORT
    if (gQCameraMuxer) {
        delete gQCameraMuxer;
        gQCameraMuxer = NULL;
    }
#endif
}

/*===========================================================================
 * FUNCTION   : get_number_of_cameras
 *
 * DESCRIPTION: static function to query number of cameras detected
 *
 * PARAMETERS : none
 *
 * RETURN     : number of cameras detected
 *==========================================================================*/
int QCamera2Factory::get_number_of_cameras()
{
    int numCameras = 0;

    if (!gQCamera2Factory) {
        gQCamera2Factory = new QCamera2Factory();
        if (!gQCamera2Factory) {
            LOGE("Failed to allocate Camera2Factory object");
            return 0;
        }
    }
#ifdef QCAMERA_HAL1_SUPPORT
    if(gQCameraMuxer)
        numCameras = gQCameraMuxer->get_number_of_cameras();
    else
#endif
        numCameras = gQCamera2Factory->getNumberOfCameras();

    LOGH("num of cameras: %d", numCameras);
    return numCameras;
}

/*===========================================================================
 * FUNCTION   : get_camera_info
 *
 * DESCRIPTION: static function to query camera information with its ID
 *
 * PARAMETERS :
 *   @camera_id : camera ID
 *   @info      : ptr to camera info struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2Factory::get_camera_info(int camera_id, struct camera_info *info)
{
    int rc = NO_ERROR;
#ifdef QCAMERA_HAL1_SUPPORT
    if(gQCameraMuxer)
        rc = gQCameraMuxer->get_camera_info(camera_id, info);
    else
#endif
        rc =  gQCamera2Factory->getCameraInfo(camera_id, info);

    return rc;
}

/*===========================================================================
 * FUNCTION   : set_callbacks
 *
 * DESCRIPTION: static function to set callbacks function to camera module
 *
 * PARAMETERS :
 *   @callbacks : ptr to callback functions
 *
 * RETURN     : NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2Factory::set_callbacks(const camera_module_callbacks_t *callbacks)
{
    int rc = NO_ERROR;
#ifdef QCAMERA_HAL1_SUPPORT
    if(gQCameraMuxer)
        rc = gQCameraMuxer->set_callbacks(callbacks);
    else
#endif
        rc =  gQCamera2Factory->setCallbacks(callbacks);

    return rc;
}

/*===========================================================================
 * FUNCTION   : open_legacy
 *
 * DESCRIPTION: Function to open older hal version implementation
 *
 * PARAMETERS :
 *   @hw_device : ptr to struct storing camera hardware device info
 *   @camera_id : camera ID
 *   @halVersion: Based on camera_module_t.common.module_api_version
 *
 * RETURN     : 0  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2Factory::open_legacy(const struct hw_module_t* module,
            const char* id, uint32_t halVersion, struct hw_device_t** device)
{
    int rc = NO_ERROR;
    if (module != &HAL_MODULE_INFO_SYM.common) {
        LOGE("Invalid module. Trying to open %p, expect %p",
            module, &HAL_MODULE_INFO_SYM.common);
        return INVALID_OPERATION;
    }
    if (!id) {
        LOGE("Invalid camera id");
        return BAD_VALUE;
    }
#ifdef QCAMERA_HAL1_SUPPORT
    if(gQCameraMuxer)
        rc =  gQCameraMuxer->open_legacy(module, id, halVersion, device);
    else
#endif
        rc =  gQCamera2Factory->openLegacy(atoi(id), halVersion, device);

    return rc;
}

/*===========================================================================
 * FUNCTION   : set_torch_mode
 *
 * DESCRIPTION: Attempt to turn on or off the torch mode of the flash unit.
 *
 * PARAMETERS :
 *   @camera_id : camera ID
 *   @on        : Indicates whether to turn the flash on or off
 *
 * RETURN     : 0  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2Factory::set_torch_mode(const char* camera_id, bool on)
{
    return gQCamera2Factory->setTorchMode(camera_id, on);
}

/*===========================================================================
 * FUNCTION   : getNumberOfCameras
 *
 * DESCRIPTION: query number of cameras detected
 *
 * PARAMETERS : none
 *
 * RETURN     : number of cameras detected
 *==========================================================================*/
int QCamera2Factory::getNumberOfCameras()
{
    return mNumOfCameras;
}

/*===========================================================================
 * FUNCTION   : getCameraInfo
 *
 * DESCRIPTION: query camera information with its ID
 *
 * PARAMETERS :
 *   @camera_id : camera ID
 *   @info      : ptr to camera info struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2Factory::getCameraInfo(int camera_id, struct camera_info *info)
{
    int rc;

    if (!mNumOfCameras || camera_id >= mNumOfCameras || !info ||
        (camera_id < 0)) {
        LOGE("Error getting camera info!! mNumOfCameras = %d,"
                "camera_id = %d, info = %p",
                 mNumOfCameras, camera_id, info);
        return -ENODEV;
    }

    if ( NULL == mHalDescriptors ) {
        LOGE("Hal descriptor table is not initialized!");
        return NO_INIT;
    }

    LOGI("Camera id %d API version %d",
            camera_id, mHalDescriptors[camera_id].device_version);

    // Need ANDROID_FLASH_INFO_AVAILABLE property for flashlight widget to
    // work and so get the static data regardless of HAL version
    rc = QCamera3HardwareInterface::getCamInfo(
            mHalDescriptors[camera_id].cameraId, info);
    if (mHalDescriptors[camera_id].device_version ==
            CAMERA_DEVICE_API_VERSION_1_0) {
        info->device_version = CAMERA_DEVICE_API_VERSION_1_0;
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : setCallbacks
 *
 * DESCRIPTION: set callback functions to send asynchronous notifications to
 *              frameworks.
 *
 * PARAMETERS :
 *   @callbacks : callback function pointer
 *
 * RETURN     :
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2Factory::setCallbacks(const camera_module_callbacks_t *callbacks)
{
    int rc = NO_ERROR;
    mCallbacks = callbacks;

    rc = QCameraFlash::getInstance().registerCallbacks(callbacks);
    if (rc != 0) {
        LOGE("Failed to register callbacks with flash module!");
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : cameraDeviceOpen
 *
 * DESCRIPTION: open a camera device with its ID
 *
 * PARAMETERS :
 *   @camera_id : camera ID
 *   @hw_device : ptr to struct storing camera hardware device info
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2Factory::cameraDeviceOpen(int camera_id,
                    struct hw_device_t **hw_device)
{
    int rc = NO_ERROR;
    if (camera_id < 0 || camera_id >= mNumOfCameras)
        return -ENODEV;

    if ( NULL == mHalDescriptors ) {
        LOGE("Hal descriptor table is not initialized!");
        return NO_INIT;
    }

    LOGI("Open camera id %d API version %d",
            camera_id, mHalDescriptors[camera_id].device_version);

    if ( mHalDescriptors[camera_id].device_version == CAMERA_DEVICE_API_VERSION_3_0 ) {
        QCamera3HardwareInterface *hw = new QCamera3HardwareInterface(mHalDescriptors[camera_id].cameraId,
                mCallbacks);
        if (!hw) {
            LOGE("Allocation of hardware interface failed");
            return NO_MEMORY;
        }
        rc = hw->openCamera(hw_device);
        if (rc != 0) {
            delete hw;
        }
    }
#ifdef QCAMERA_HAL1_SUPPORT
    else if (mHalDescriptors[camera_id].device_version == CAMERA_DEVICE_API_VERSION_1_0) {
        QCamera2HardwareInterface *hw = new QCamera2HardwareInterface((uint32_t)camera_id);
        if (!hw) {
            LOGE("Allocation of hardware interface failed");
            return NO_MEMORY;
        }
        rc = hw->openCamera(hw_device);
        if (rc != NO_ERROR) {
            delete hw;
        }
    }
#endif
    else {
        LOGE("Device version for camera id %d invalid %d",
              camera_id,
              mHalDescriptors[camera_id].device_version);
        return BAD_VALUE;
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : camera_device_open
 *
 * DESCRIPTION: static function to open a camera device by its ID
 *
 * PARAMETERS :
 *   @camera_id : camera ID
 *   @hw_device : ptr to struct storing camera hardware device info
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2Factory::camera_device_open(
    const struct hw_module_t *module, const char *id,
    struct hw_device_t **hw_device)
{
    int rc = NO_ERROR;
    if (module != &HAL_MODULE_INFO_SYM.common) {
        LOGE("Invalid module. Trying to open %p, expect %p",
            module, &HAL_MODULE_INFO_SYM.common);
        return INVALID_OPERATION;
    }
    if (!id) {
        LOGE("Invalid camera id");
        return BAD_VALUE;
    }
#ifdef QCAMERA_HAL1_SUPPORT
    if(gQCameraMuxer)
        rc =  gQCameraMuxer->camera_device_open(module, id, hw_device);
    else
#endif
        rc = gQCamera2Factory->cameraDeviceOpen(atoi(id), hw_device);
    return rc;
}

struct hw_module_methods_t QCamera2Factory::mModuleMethods = {
    .open = QCamera2Factory::camera_device_open,
};

/*===========================================================================
 * FUNCTION   : openLegacy
 *
 * DESCRIPTION: Function to open older hal version implementation
 *
 * PARAMETERS :
 *   @camera_id : camera ID
 *   @halVersion: Based on camera_module_t.common.module_api_version
 *   @hw_device : ptr to struct storing camera hardware device info
 *
 * RETURN     : 0  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2Factory::openLegacy(
        int32_t cameraId, uint32_t halVersion, struct hw_device_t** hw_device)
{
    int rc = NO_ERROR;

    LOGI("openLegacy halVersion: %d", halVersion);
    //Assumption: all cameras can support legacy API version
    if (cameraId < 0 || cameraId >= gQCamera2Factory->getNumberOfCameras())
        return -ENODEV;

    switch(halVersion)
    {
#ifdef QCAMERA_HAL1_SUPPORT
        case CAMERA_DEVICE_API_VERSION_1_0:
        {
            QCamera2HardwareInterface *hw =
                new QCamera2HardwareInterface((uint32_t)cameraId);
            if (!hw) {
                LOGE("Allocation of hardware interface failed");
                return NO_MEMORY;
            }
            rc = hw->openCamera(hw_device);
            if (rc != NO_ERROR) {
                delete hw;
            }
            break;
        }
#endif
        default:
            LOGE("Device API version: %d for camera id %d invalid",
                 halVersion, cameraId);
            return BAD_VALUE;
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : setTorchMode
 *
 * DESCRIPTION: Attempt to turn on or off the torch mode of the flash unit.
 *
 * PARAMETERS :
 *   @camera_id : camera ID
 *   @on        : Indicates whether to turn the flash on or off
 *
 * RETURN     : 0  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2Factory::setTorchMode(const char* camera_id, bool on)
{
    int retVal(0);
    long cameraIdLong(-1);
    int cameraIdInt(-1);
    char* endPointer = NULL;
    errno = 0;
    QCameraFlash& flash = QCameraFlash::getInstance();

    cameraIdLong = strtol(camera_id, &endPointer, 10);

    if ((errno == ERANGE) ||
            (cameraIdLong < 0) ||
            (cameraIdLong >= static_cast<long>(get_number_of_cameras())) ||
            (endPointer == camera_id) ||
            (*endPointer != '\0')) {
        retVal = -EINVAL;
    } else if (on) {
        cameraIdInt = static_cast<int>(cameraIdLong);
        retVal = flash.initFlash(cameraIdInt);

        if (retVal == 0) {
            retVal = flash.setFlashMode(cameraIdInt, on);
            if ((retVal == 0) && (mCallbacks != NULL)) {
                mCallbacks->torch_mode_status_change(mCallbacks,
                        camera_id,
                        TORCH_MODE_STATUS_AVAILABLE_ON);
            } else if (retVal == -EALREADY) {
                // Flash is already on, so treat this as a success.
                retVal = 0;
            }
        }
    } else {
        cameraIdInt = static_cast<int>(cameraIdLong);
        retVal = flash.setFlashMode(cameraIdInt, on);

        if (retVal == 0) {
            retVal = flash.deinitFlash(cameraIdInt);
            if ((retVal == 0) && (mCallbacks != NULL)) {
                mCallbacks->torch_mode_status_change(mCallbacks,
                        camera_id,
                        TORCH_MODE_STATUS_AVAILABLE_OFF);
            }
        } else if (retVal == -EALREADY) {
            // Flash is already off, so treat this as a success.
            retVal = 0;
        }
    }

    return retVal;
}

/*===========================================================================
 * FUNCTION   : isDualCamAvailable
 *
 * DESCRIPTION: Function to check whether we have dual Camera HW available
 *
 * PARAMETERS :
 *   @hal3Enabled : HAL3 enable flag
 *
 * RETURN     : bool - true : have Dual Camera HW available
 *                           false : not have Dual Camera HW available
 *==========================================================================*/
bool QCamera2Factory::isDualCamAvailable(int hal3Enabled)
{
    bool rc = false;
    int i = 0;
    camera_info info;
    cam_sync_type_t cam_type = CAM_TYPE_MAIN;

    for (i = 0; i < mNumOfCameras; i++) {
        if (!hal3Enabled) {
#ifdef QCAMERA_HAL1_SUPPORT
            QCamera2HardwareInterface::getCapabilities(i, &info, &cam_type);
#endif
        }

        if(cam_type == CAM_TYPE_AUX) {
            LOGH("Have Dual Camera HW Avaiable.");
            rc = true;
            break;
        }
    }
#ifdef QCAMERA_HAL1_SUPPORT
    return rc;
#else
    return false;
#endif
}

}; // namespace qcamera

