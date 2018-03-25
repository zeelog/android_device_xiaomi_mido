/* Copyright (c) 2012-2017, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "QCamera2HWI"

// To remove
#include <cutils/properties.h>

// System definitions
#include <utils/Errors.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include "gralloc_priv.h"
#include "native_handle.h"

// Camera definitions
#include "android/QCamera2External.h"
#include "QCamera2HWI.h"
#include "QCameraBufferMaps.h"
#include "QCameraFlash.h"
#include "QCameraTrace.h"

extern "C" {
#include "mm_camera_dbg.h"
}

#define MAP_TO_DRIVER_COORDINATE(val, base, scale, offset) \
    ((int32_t)val * (int32_t)scale / (int32_t)base + (int32_t)offset)
#define CAMERA_MIN_STREAMING_BUFFERS     3
#define EXTRA_ZSL_PREVIEW_STREAM_BUF     2
#define CAMERA_MIN_JPEG_ENCODING_BUFFERS 2
#define CAMERA_MIN_VIDEO_BUFFERS         9
#define CAMERA_MIN_CALLBACK_BUFFERS      5
#define CAMERA_LONGSHOT_STAGES           4
#define CAMERA_MIN_CAMERA_BATCH_BUFFERS  6
#define CAMERA_ISP_PING_PONG_BUFFERS     2
#define MIN_UNDEQUEUED_BUFFERS           1 // This is required if preview window is not set

#define HDR_CONFIDENCE_THRESHOLD 0.4

#define CAMERA_OPEN_PERF_TIME_OUT 500 // 500 milliseconds

// Very long wait, just to be sure we don't deadlock
#define CAMERA_DEFERRED_THREAD_TIMEOUT 5000000000 // 5 seconds
#define CAMERA_DEFERRED_MAP_BUF_TIMEOUT 2000000000 // 2 seconds
#define CAMERA_MIN_METADATA_BUFFERS 10 // Need at least 10 for ZSL snapshot
#define CAMERA_INITIAL_MAPPABLE_PREVIEW_BUFFERS 5
#define CAMERA_MAX_PARAM_APPLY_DELAY 3

namespace qcamera {

extern cam_capability_t *gCamCapability[MM_CAMERA_MAX_NUM_SENSORS];
extern pthread_mutex_t gCamLock;
volatile uint32_t gCamHalLogLevel = 1;
extern uint8_t gNumCameraSessions;
uint32_t QCamera2HardwareInterface::sNextJobId = 1;

camera_device_ops_t QCamera2HardwareInterface::mCameraOps = {
    .set_preview_window =        QCamera2HardwareInterface::set_preview_window,
    .set_callbacks =             QCamera2HardwareInterface::set_CallBacks,
    .enable_msg_type =           QCamera2HardwareInterface::enable_msg_type,
    .disable_msg_type =          QCamera2HardwareInterface::disable_msg_type,
    .msg_type_enabled =          QCamera2HardwareInterface::msg_type_enabled,

    .start_preview =             QCamera2HardwareInterface::start_preview,
    .stop_preview =              QCamera2HardwareInterface::stop_preview,
    .preview_enabled =           QCamera2HardwareInterface::preview_enabled,
    .store_meta_data_in_buffers= QCamera2HardwareInterface::store_meta_data_in_buffers,

    .start_recording =           QCamera2HardwareInterface::start_recording,
    .stop_recording =            QCamera2HardwareInterface::stop_recording,
    .recording_enabled =         QCamera2HardwareInterface::recording_enabled,
    .release_recording_frame =   QCamera2HardwareInterface::release_recording_frame,

    .auto_focus =                QCamera2HardwareInterface::auto_focus,
    .cancel_auto_focus =         QCamera2HardwareInterface::cancel_auto_focus,

    .take_picture =              QCamera2HardwareInterface::take_picture,
    .cancel_picture =            QCamera2HardwareInterface::cancel_picture,

    .set_parameters =            QCamera2HardwareInterface::set_parameters,
    .get_parameters =            QCamera2HardwareInterface::get_parameters,
    .put_parameters =            QCamera2HardwareInterface::put_parameters,
    .send_command =              QCamera2HardwareInterface::send_command,

    .release =                   QCamera2HardwareInterface::release,
    .dump =                      QCamera2HardwareInterface::dump,
};
uint32_t QCamera2HardwareInterface::sessionId[] = {0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF};
/*===========================================================================
 * FUNCTION   : set_preview_window
 *
 * DESCRIPTION: set preview window.
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *   @window  : window ops table
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::set_preview_window(struct camera_device *device,
        struct preview_stream_ops *window)
{
    ATRACE_CALL();
    int rc = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGD("E camera id %d window = %p", hw->getCameraId(), window);

    hw->lockAPI();
    qcamera_api_result_t apiResult;
    rc = hw->processAPI(QCAMERA_SM_EVT_SET_PREVIEW_WINDOW, (void *)window);
    if (rc == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_SET_PREVIEW_WINDOW, &apiResult);
        rc = apiResult.status;
    }
    hw->unlockAPI();
    LOGD("X camera id %d", hw->getCameraId());

    return rc;
}

/*===========================================================================
 * FUNCTION   : set_CallBacks
 *
 * DESCRIPTION: set callbacks for notify and data
 *
 * PARAMETERS :
 *   @device     : ptr to camera device struct
 *   @notify_cb  : notify cb
 *   @data_cb    : data cb
 *   @data_cb_timestamp  : video data cd with timestamp
 *   @get_memory : ops table for request gralloc memory
 *   @user       : user data ptr
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::set_CallBacks(struct camera_device *device,
        camera_notify_callback notify_cb,
        camera_data_callback data_cb,
        camera_data_timestamp_callback data_cb_timestamp,
        camera_request_memory get_memory,
        void *user)
{
    ATRACE_CALL();
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return;
    }
    LOGD("E camera id %d", hw->getCameraId());

    qcamera_sm_evt_setcb_payload_t payload;
    payload.notify_cb = notify_cb;
    payload.data_cb = data_cb;
    payload.data_cb_timestamp = data_cb_timestamp;
    payload.get_memory = get_memory;
    payload.user = user;

    hw->lockAPI();
    qcamera_api_result_t apiResult;
    int32_t rc = hw->processAPI(QCAMERA_SM_EVT_SET_CALLBACKS, (void *)&payload);
    if (rc == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_SET_CALLBACKS, &apiResult);
    }
    hw->unlockAPI();
    LOGD("X camera id %d", hw->getCameraId());

}

/*===========================================================================
 * FUNCTION   : enable_msg_type
 *
 * DESCRIPTION: enable certain msg type
 *
 * PARAMETERS :
 *   @device     : ptr to camera device struct
 *   @msg_type   : msg type mask
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::enable_msg_type(struct camera_device *device, int32_t msg_type)
{
    ATRACE_CALL();
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return;
    }
    LOGD("E camera id %d", hw->getCameraId());

    hw->lockAPI();
    qcamera_api_result_t apiResult;
    int32_t rc = hw->processAPI(QCAMERA_SM_EVT_ENABLE_MSG_TYPE, (void *)&msg_type);
    if (rc == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_ENABLE_MSG_TYPE, &apiResult);
    }
    hw->unlockAPI();
    LOGD("X camera id %d", hw->getCameraId());

}

/*===========================================================================
 * FUNCTION   : disable_msg_type
 *
 * DESCRIPTION: disable certain msg type
 *
 * PARAMETERS :
 *   @device     : ptr to camera device struct
 *   @msg_type   : msg type mask
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::disable_msg_type(struct camera_device *device, int32_t msg_type)
{
    ATRACE_CALL();
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return;
    }
    LOGD("E camera id %d", hw->getCameraId());

    hw->lockAPI();
    qcamera_api_result_t apiResult;
    int32_t rc = hw->processAPI(QCAMERA_SM_EVT_DISABLE_MSG_TYPE, (void *)&msg_type);
    if (rc == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_DISABLE_MSG_TYPE, &apiResult);
    }
    hw->unlockAPI();
    LOGD("X camera id %d", hw->getCameraId());

}

/*===========================================================================
 * FUNCTION   : msg_type_enabled
 *
 * DESCRIPTION: if certain msg type is enabled
 *
 * PARAMETERS :
 *   @device     : ptr to camera device struct
 *   @msg_type   : msg type mask
 *
 * RETURN     : 1 -- enabled
 *              0 -- not enabled
 *==========================================================================*/
int QCamera2HardwareInterface::msg_type_enabled(struct camera_device *device, int32_t msg_type)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGD("E camera id %d", hw->getCameraId());

    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_MSG_TYPE_ENABLED, (void *)&msg_type);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_MSG_TYPE_ENABLED, &apiResult);
        ret = apiResult.enabled;
    }
    hw->unlockAPI();
    LOGD("X camera id %d", hw->getCameraId());

   return ret;
}

/*===========================================================================
 * FUNCTION   : prepare_preview
 *
 * DESCRIPTION: prepare preview
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::prepare_preview(struct camera_device *device)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGH("[KPI Perf]: E PROFILE_PREPARE_PREVIEW camera id %d",
             hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    qcamera_sm_evt_enum_t evt = QCAMERA_SM_EVT_PREPARE_PREVIEW;
    ret = hw->processAPI(evt, NULL);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(evt, &apiResult);
        ret = apiResult.status;
    }
    hw->unlockAPI();
    LOGH("[KPI Perf]: X");
    return ret;
}


/*===========================================================================
 * FUNCTION   : start_preview
 *
 * DESCRIPTION: start preview
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::start_preview(struct camera_device *device)
{
    KPI_ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGI("[KPI Perf]: E PROFILE_START_PREVIEW camera id %d",
             hw->getCameraId());

    // Release the timed perf lock acquired in openCamera
    hw->m_perfLock.lock_rel_timed();

    hw->m_perfLock.lock_acq();
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    qcamera_sm_evt_enum_t evt = QCAMERA_SM_EVT_START_PREVIEW;
    if (hw->isNoDisplayMode()) {
        evt = QCAMERA_SM_EVT_START_NODISPLAY_PREVIEW;
    }
    ret = hw->processAPI(evt, NULL);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(evt, &apiResult);
        ret = apiResult.status;
    }
    hw->unlockAPI();
    hw->m_bPreviewStarted = true;
    LOGI("[KPI Perf]: X ret = %d", ret);
    return ret;
}

/*===========================================================================
 * FUNCTION   : stop_preview
 *
 * DESCRIPTION: stop preview
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::stop_preview(struct camera_device *device)
{
    KPI_ATRACE_CALL();
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return;
    }
    LOGI("[KPI Perf]: E PROFILE_STOP_PREVIEW camera id %d",
             hw->getCameraId());

    // Disable power Hint for preview
    hw->m_perfLock.powerHint(POWER_HINT_VIDEO_ENCODE, false);

    hw->m_perfLock.lock_acq();
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    int32_t ret = hw->processAPI(QCAMERA_SM_EVT_STOP_PREVIEW, NULL);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_STOP_PREVIEW, &apiResult);
    }
    hw->unlockAPI();
    LOGI("[KPI Perf]: X ret = %d", ret);
}

/*===========================================================================
 * FUNCTION   : preview_enabled
 *
 * DESCRIPTION: if preview is running
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : 1 -- running
 *              0 -- not running
 *==========================================================================*/
int QCamera2HardwareInterface::preview_enabled(struct camera_device *device)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGD("E camera id %d", hw->getCameraId());

    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_PREVIEW_ENABLED, NULL);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_PREVIEW_ENABLED, &apiResult);
        ret = apiResult.enabled;
    }

    //if preview enabled, can enable preview callback send
    if(apiResult.enabled) {
        hw->m_stateMachine.setPreviewCallbackNeeded(true);
    }
    hw->unlockAPI();
    LOGD("X camera id %d", hw->getCameraId());

    return ret;
}

/*===========================================================================
 * FUNCTION   : store_meta_data_in_buffers
 *
 * DESCRIPTION: if need to store meta data in buffers for video frame
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *   @enable  : flag if enable
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::store_meta_data_in_buffers(
                struct camera_device *device, int enable)
{
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGD("E camera id %d", hw->getCameraId());

    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_STORE_METADATA_IN_BUFS, (void *)&enable);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_STORE_METADATA_IN_BUFS, &apiResult);
        ret = apiResult.status;
    }
    hw->unlockAPI();
    LOGD("X camera id %d", hw->getCameraId());

    return ret;
}

/*===========================================================================
 * FUNCTION   : restart_start_preview
 *
 * DESCRIPTION: start preview as part of the restart preview
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::restart_start_preview(struct camera_device *device)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGI("E camera id %d", hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;

    if (hw->getRelatedCamSyncInfo()->sync_control == CAM_SYNC_RELATED_SENSORS_ON) {
        ret = hw->processAPI(QCAMERA_SM_EVT_RESTART_START_PREVIEW, NULL);
        if (ret == NO_ERROR) {
            hw->waitAPIResult(QCAMERA_SM_EVT_RESTART_START_PREVIEW, &apiResult);
            ret = apiResult.status;
        }
    } else {
        LOGE("This function is not supposed to be called in single-camera mode");
        ret = INVALID_OPERATION;
    }
    // Preview restart done, update the mPreviewRestartNeeded flag to false.
    hw->mPreviewRestartNeeded = false;
    hw->unlockAPI();
    LOGI("X camera id %d", hw->getCameraId());

    return ret;
}

/*===========================================================================
 * FUNCTION   : restart_stop_preview
 *
 * DESCRIPTION: stop preview as part of the restart preview
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::restart_stop_preview(struct camera_device *device)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGI("E camera id %d", hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;

    if (hw->getRelatedCamSyncInfo()->sync_control == CAM_SYNC_RELATED_SENSORS_ON) {
        ret = hw->processAPI(QCAMERA_SM_EVT_RESTART_STOP_PREVIEW, NULL);
        if (ret == NO_ERROR) {
            hw->waitAPIResult(QCAMERA_SM_EVT_RESTART_STOP_PREVIEW, &apiResult);
            ret = apiResult.status;
        }
    } else {
        LOGE("This function is not supposed to be called in single-camera mode");
        ret = INVALID_OPERATION;
    }

    hw->unlockAPI();
    LOGI("X camera id %d", hw->getCameraId());

    return ret;
}

/*===========================================================================
 * FUNCTION   : pre_start_recording
 *
 * DESCRIPTION: prepare for the start recording
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::pre_start_recording(struct camera_device *device)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGH("[KPI Perf]: E PROFILE_PRE_START_RECORDING camera id %d",
          hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_PRE_START_RECORDING, NULL);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_PRE_START_RECORDING, &apiResult);
        ret = apiResult.status;
    }
    hw->unlockAPI();
    LOGH("[KPI Perf]: X");
    return ret;
}

/*===========================================================================
 * FUNCTION   : start_recording
 *
 * DESCRIPTION: start recording
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::start_recording(struct camera_device *device)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGI("[KPI Perf]: E PROFILE_START_RECORDING camera id %d",
          hw->getCameraId());
    // Give HWI control to call pre_start_recording in single camera mode.
    // In dual-cam mode, this control belongs to muxer.
    if (hw->getRelatedCamSyncInfo()->sync_control != CAM_SYNC_RELATED_SENSORS_ON) {
        ret = pre_start_recording(device);
        if (ret != NO_ERROR) {
            LOGE("pre_start_recording failed with ret = %d", ret);
            return ret;
        }
    }

    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_START_RECORDING, NULL);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_START_RECORDING, &apiResult);
        ret = apiResult.status;
    }
    hw->unlockAPI();
    hw->m_bRecordStarted = true;
    LOGI("[KPI Perf]: X ret = %d", ret);

    return ret;
}

/*===========================================================================
 * FUNCTION   : stop_recording
 *
 * DESCRIPTION: stop recording
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::stop_recording(struct camera_device *device)
{
    ATRACE_CALL();
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return;
    }
    LOGI("[KPI Perf]: E PROFILE_STOP_RECORDING camera id %d",
             hw->getCameraId());

    hw->lockAPI();
    qcamera_api_result_t apiResult;
    int32_t ret = hw->processAPI(QCAMERA_SM_EVT_STOP_RECORDING, NULL);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_STOP_RECORDING, &apiResult);
    }
    hw->unlockAPI();
    LOGI("[KPI Perf]: X ret = %d", ret);
}

/*===========================================================================
 * FUNCTION   : recording_enabled
 *
 * DESCRIPTION: if recording is running
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : 1 -- running
 *              0 -- not running
 *==========================================================================*/
int QCamera2HardwareInterface::recording_enabled(struct camera_device *device)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGD("E camera id %d", hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_RECORDING_ENABLED, NULL);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_RECORDING_ENABLED, &apiResult);
        ret = apiResult.enabled;
    }
    hw->unlockAPI();
    LOGD("X camera id %d", hw->getCameraId());

    return ret;
}

/*===========================================================================
 * FUNCTION   : release_recording_frame
 *
 * DESCRIPTION: return recording frame back
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *   @opaque  : ptr to frame to be returned
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::release_recording_frame(
            struct camera_device *device, const void *opaque)
{
    ATRACE_CALL();
    int32_t ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return;
    }
    if (!opaque) {
        LOGE("Error!! Frame info is NULL");
        return;
    }
    LOGD("E camera id %d", hw->getCameraId());

    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_RELEASE_RECORIDNG_FRAME, (void *)opaque);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_RELEASE_RECORIDNG_FRAME, &apiResult);
    }
    hw->unlockAPI();
    LOGD("X camera id %d", hw->getCameraId());
}

/*===========================================================================
 * FUNCTION   : auto_focus
 *
 * DESCRIPTION: start auto focus
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::auto_focus(struct camera_device *device)
{
    KPI_ATRACE_INT("Camera:AutoFocus", 1);
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGH("[KPI Perf] : E PROFILE_AUTO_FOCUS camera id %d",
             hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_START_AUTO_FOCUS, NULL);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_START_AUTO_FOCUS, &apiResult);
        ret = apiResult.status;
    }
    hw->unlockAPI();
    LOGH("[KPI Perf] : X ret = %d", ret);

    return ret;
}

/*===========================================================================
 * FUNCTION   : cancel_auto_focus
 *
 * DESCRIPTION: cancel auto focus
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::cancel_auto_focus(struct camera_device *device)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGH("[KPI Perf] : E PROFILE_CANCEL_AUTO_FOCUS camera id %d",
             hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_STOP_AUTO_FOCUS, NULL);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_STOP_AUTO_FOCUS, &apiResult);
        ret = apiResult.status;
    }
    hw->unlockAPI();
    LOGH("[KPI Perf] : X ret = %d", ret);
    return ret;
}

/*===========================================================================
 * FUNCTION   : pre_take_picture
 *
 * DESCRIPTION: pre take picture, restart preview if necessary.
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::pre_take_picture(struct camera_device *device)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGH("[KPI Perf]: E PROFILE_PRE_TAKE_PICTURE camera id %d",
          hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_PRE_TAKE_PICTURE, NULL);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_PRE_TAKE_PICTURE, &apiResult);
        ret = apiResult.status;
    }
    hw->unlockAPI();
    LOGH("[KPI Perf]: X");
    return ret;
}

/*===========================================================================
 * FUNCTION   : take_picture
 *
 * DESCRIPTION: take picture
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::take_picture(struct camera_device *device)
{
    KPI_ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGI("[KPI Perf]: E PROFILE_TAKE_PICTURE camera id %d",
             hw->getCameraId());
    if (!hw->mLongshotEnabled) {
        hw->m_perfLock.lock_acq();
    }
    qcamera_api_result_t apiResult;

   /** Added support for Retro-active Frames:
     *  takePicture() is called before preparing Snapshot to indicate the
     *  mm-camera-channel to pick up legacy frames even
     *  before LED estimation is triggered.
     */

    LOGH("isLiveSnap %d, isZSL %d, isHDR %d longshot = %d",
           hw->isLiveSnapshot(), hw->isZSLMode(), hw->isHDRMode(),
           hw->isLongshotEnabled());

    // Check for Retro-active Frames
    if ((hw->mParameters.getNumOfRetroSnapshots() > 0) &&
        !hw->isLiveSnapshot() && hw->isZSLMode() &&
        !hw->isHDRMode() && !hw->isLongshotEnabled()) {
        // Set Retro Picture Mode
        hw->setRetroPicture(1);
        hw->m_bLedAfAecLock = 0;
        LOGL("Retro Enabled");

        // Give HWI control to call pre_take_picture in single camera mode.
        // In dual-cam mode, this control belongs to muxer.
        if (hw->getRelatedCamSyncInfo()->sync_control != CAM_SYNC_RELATED_SENSORS_ON) {
            ret = pre_take_picture(device);
            if (ret != NO_ERROR) {
                LOGE("pre_take_picture failed with ret = %d",ret);
                return ret;
            }
        }

        /* Call take Picture for total number of snapshots required.
             This includes the number of retro frames and normal frames */
        hw->lockAPI();
        ret = hw->processAPI(QCAMERA_SM_EVT_TAKE_PICTURE, NULL);
        if (ret == NO_ERROR) {
          // Wait for retro frames, before calling prepare snapshot
          LOGD("Wait for Retro frames to be done");
          hw->waitAPIResult(QCAMERA_SM_EVT_TAKE_PICTURE, &apiResult);
            ret = apiResult.status;
        }
        /* Unlock API since it is acquired in prepare snapshot seperately */
        hw->unlockAPI();

        /* Prepare snapshot in case LED needs to be flashed */
        LOGD("Start Prepare Snapshot");
        ret = hw->prepare_snapshot(device);
    }
    else {
        hw->setRetroPicture(0);
        // Check if prepare snapshot is done
        if (!hw->mPrepSnapRun) {
            // Ignore the status from prepare_snapshot
            hw->prepare_snapshot(device);
        }

        // Give HWI control to call pre_take_picture in single camera mode.
        // In dual-cam mode, this control belongs to muxer.
        if (hw->getRelatedCamSyncInfo()->sync_control != CAM_SYNC_RELATED_SENSORS_ON) {
            ret = pre_take_picture(device);
            if (ret != NO_ERROR) {
                LOGE("pre_take_picture failed with ret = %d",ret);
                return ret;
            }
        }

        // Regardless what the result value for prepare_snapshot,
        // go ahead with capture anyway. Just like the way autofocus
        // is handled in capture case
        /* capture */
        LOGL("Capturing normal frames");
        hw->lockAPI();
        ret = hw->processAPI(QCAMERA_SM_EVT_TAKE_PICTURE, NULL);
        if (ret == NO_ERROR) {
          hw->waitAPIResult(QCAMERA_SM_EVT_TAKE_PICTURE, &apiResult);
            ret = apiResult.status;
        }
        hw->unlockAPI();
        if (!hw->isLongshotEnabled()){
            // For longshot mode, we prepare snapshot only once
            hw->mPrepSnapRun = false;
         }
    }
    LOGI("[KPI Perf]: X ret = %d", ret);
    return ret;
}

/*===========================================================================
 * FUNCTION   : cancel_picture
 *
 * DESCRIPTION: cancel current take picture request
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::cancel_picture(struct camera_device *device)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGI("[KPI Perf]: E PROFILE_CANCEL_PICTURE camera id %d",
             hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_CANCEL_PICTURE, NULL);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_CANCEL_PICTURE, &apiResult);
        ret = apiResult.status;
    }
    hw->unlockAPI();
    LOGI("[KPI Perf]: X camera id %d ret = %d", hw->getCameraId(), ret);

    return ret;
}

/*===========================================================================
 * FUNCTION   : set_parameters
 *
 * DESCRIPTION: set camera parameters
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *   @parms   : string of packed parameters
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::set_parameters(struct camera_device *device,
                                              const char *parms)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGD("E camera id %d", hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_SET_PARAMS, (void *)parms);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_SET_PARAMS, &apiResult);
        ret = apiResult.status;
    }

    // Give HWI control to restart (if necessary) after set params
    // in single camera mode. In dual-cam mode, this control belongs to muxer.
    if (hw->getRelatedCamSyncInfo()->sync_control != CAM_SYNC_RELATED_SENSORS_ON) {
        if ((ret == NO_ERROR) && hw->getNeedRestart()) {
            LOGD("stopping after param change");
            ret = hw->processAPI(QCAMERA_SM_EVT_SET_PARAMS_STOP, NULL);
            if (ret == NO_ERROR) {
                hw->waitAPIResult(QCAMERA_SM_EVT_SET_PARAMS_STOP, &apiResult);
                ret = apiResult.status;
            }
        }

        if (ret == NO_ERROR) {
            LOGD("committing param change");
            ret = hw->processAPI(QCAMERA_SM_EVT_SET_PARAMS_COMMIT, NULL);
            if (ret == NO_ERROR) {
                hw->waitAPIResult(QCAMERA_SM_EVT_SET_PARAMS_COMMIT, &apiResult);
                ret = apiResult.status;
            }
        }

        if ((ret == NO_ERROR) && hw->getNeedRestart()) {
            LOGD("restarting after param change");
            ret = hw->processAPI(QCAMERA_SM_EVT_SET_PARAMS_RESTART, NULL);
            if (ret == NO_ERROR) {
                hw->waitAPIResult(QCAMERA_SM_EVT_SET_PARAMS_RESTART, &apiResult);
                ret = apiResult.status;
            }
        }
    }

    hw->unlockAPI();
    LOGD("X camera id %d ret %d", hw->getCameraId(), ret);

    return ret;
}

/*===========================================================================
 * FUNCTION   : stop_after_set_params
 *
 * DESCRIPTION: stop after a set param call, if necessary
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::stop_after_set_params(struct camera_device *device)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGD("E camera id %d", hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;

    if (hw->getRelatedCamSyncInfo()->sync_control == CAM_SYNC_RELATED_SENSORS_ON) {
        ret = hw->processAPI(QCAMERA_SM_EVT_SET_PARAMS_STOP, NULL);
        if (ret == NO_ERROR) {
            hw->waitAPIResult(QCAMERA_SM_EVT_SET_PARAMS_STOP, &apiResult);
            ret = apiResult.status;
        }
    } else {
        LOGE("is not supposed to be called in single-camera mode");
        ret = INVALID_OPERATION;
    }

    hw->unlockAPI();
    LOGD("X camera id %d", hw->getCameraId());

    return ret;
}

/*===========================================================================
 * FUNCTION   : commit_params
 *
 * DESCRIPTION: commit after a set param call
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::commit_params(struct camera_device *device)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGD("E camera id %d", hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;

    if (hw->getRelatedCamSyncInfo()->sync_control == CAM_SYNC_RELATED_SENSORS_ON) {
        ret = hw->processAPI(QCAMERA_SM_EVT_SET_PARAMS_COMMIT, NULL);
        if (ret == NO_ERROR) {
            hw->waitAPIResult(QCAMERA_SM_EVT_SET_PARAMS_COMMIT, &apiResult);
            ret = apiResult.status;
        }
    } else {
        LOGE("is not supposed to be called in single-camera mode");
        ret = INVALID_OPERATION;
    }

    hw->unlockAPI();
    LOGD("X camera id %d", hw->getCameraId());

    return ret;
}

/*===========================================================================
 * FUNCTION   : restart_after_set_params
 *
 * DESCRIPTION: restart after a set param call, if necessary
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::restart_after_set_params(struct camera_device *device)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGD("E camera id %d", hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;

    if (hw->getRelatedCamSyncInfo()->sync_control == CAM_SYNC_RELATED_SENSORS_ON) {
        ret = hw->processAPI(QCAMERA_SM_EVT_SET_PARAMS_RESTART, NULL);
        if (ret == NO_ERROR) {
            hw->waitAPIResult(QCAMERA_SM_EVT_SET_PARAMS_RESTART, &apiResult);
            ret = apiResult.status;
        }
    } else {
        LOGE("is not supposed to be called in single-camera mode");
        ret = INVALID_OPERATION;
    }

    hw->unlockAPI();
    LOGD("X camera id %d", hw->getCameraId());
    return ret;
}

/*===========================================================================
 * FUNCTION   : get_parameters
 *
 * DESCRIPTION: query camera parameters
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : packed parameters in a string
 *==========================================================================*/
char* QCamera2HardwareInterface::get_parameters(struct camera_device *device)
{
    ATRACE_CALL();
    char *ret = NULL;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return NULL;
    }
    LOGD("E camera id %d", hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    int32_t rc = hw->processAPI(QCAMERA_SM_EVT_GET_PARAMS, NULL);
    if (rc == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_GET_PARAMS, &apiResult);
        ret = apiResult.params;
    }
    hw->unlockAPI();
    LOGD("E camera id %d", hw->getCameraId());

    return ret;
}

/*===========================================================================
 * FUNCTION   : put_parameters
 *
 * DESCRIPTION: return camera parameters string back to HAL
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *   @parm    : ptr to parameter string to be returned
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::put_parameters(struct camera_device *device,
                                               char *parm)
{
    ATRACE_CALL();
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return;
    }
    LOGD("E camera id %d", hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    int32_t ret = hw->processAPI(QCAMERA_SM_EVT_PUT_PARAMS, (void *)parm);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_PUT_PARAMS, &apiResult);
    }
    hw->unlockAPI();
    LOGD("E camera id %d", hw->getCameraId());
}

/*===========================================================================
 * FUNCTION   : send_command
 *
 * DESCRIPTION: command to be executed
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *   @cmd     : cmd to be executed
 *   @arg1    : ptr to optional argument1
 *   @arg2    : ptr to optional argument2
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::send_command(struct camera_device *device,
                                            int32_t cmd,
                                            int32_t arg1,
                                            int32_t arg2)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGD("E camera id %d", hw->getCameraId());

    qcamera_sm_evt_command_payload_t payload;
    memset(&payload, 0, sizeof(qcamera_sm_evt_command_payload_t));
    payload.cmd = cmd;
    payload.arg1 = arg1;
    payload.arg2 = arg2;
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_SEND_COMMAND, (void *)&payload);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_SEND_COMMAND, &apiResult);
        ret = apiResult.status;
    }
    hw->unlockAPI();
    LOGD("E camera id %d", hw->getCameraId());

    return ret;
}

/*===========================================================================
 * FUNCTION   : send_command_restart
 *
 * DESCRIPTION: restart if necessary after a send_command
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *   @cmd     : cmd to be executed
 *   @arg1    : ptr to optional argument1
 *   @arg2    : ptr to optional argument2
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::send_command_restart(struct camera_device *device,
        int32_t cmd,
        int32_t arg1,
        int32_t arg2)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
            reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }

    qcamera_sm_evt_command_payload_t payload;
    memset(&payload, 0, sizeof(qcamera_sm_evt_command_payload_t));
    payload.cmd = cmd;
    payload.arg1 = arg1;
    payload.arg2 = arg2;
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_SEND_COMMAND_RESTART, (void *)&payload);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_SEND_COMMAND_RESTART, &apiResult);
        ret = apiResult.status;
    }
    hw->unlockAPI();
    LOGD("E camera id %d", hw->getCameraId());

    return ret;
}

/*===========================================================================
 * FUNCTION   : release
 *
 * DESCRIPTION: release camera resource
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::release(struct camera_device *device)
{
    ATRACE_CALL();
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return;
    }
    LOGD("E camera id %d", hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    int32_t ret = hw->processAPI(QCAMERA_SM_EVT_RELEASE, NULL);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_RELEASE, &apiResult);
    }
    hw->unlockAPI();
    LOGD("E camera id %d", hw->getCameraId());
}

/*===========================================================================
 * FUNCTION   : dump
 *
 * DESCRIPTION: dump camera status
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *   @fd      : fd for status to be dumped to
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::dump(struct camera_device *device, int fd)
{
    int ret = NO_ERROR;

    //Log level property is read when "adb shell dumpsys media.camera" is
    //called so that the log level can be controlled without restarting
    //media server
    getLogLevel();
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGD("E camera id %d", hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_DUMP, (void *)&fd);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_DUMP, &apiResult);
        ret = apiResult.status;
    }
    hw->unlockAPI();
    LOGD("E camera id %d", hw->getCameraId());

    return ret;
}

/*===========================================================================
 * FUNCTION   : close_camera_device
 *
 * DESCRIPTION: close camera device
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::close_camera_device(hw_device_t *hw_dev)
{
    KPI_ATRACE_CALL();
    int ret = NO_ERROR;

    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(
            reinterpret_cast<camera_device_t *>(hw_dev)->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGI("[KPI Perf]: E camera id %d", hw->getCameraId());
    delete hw;
    LOGI("[KPI Perf]: X");
    return ret;
}

/*===========================================================================
 * FUNCTION   : register_face_image
 *
 * DESCRIPTION: register a face image into imaging lib for face authenticatio/
 *              face recognition
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *   @img_ptr : ptr to image buffer
 *   @config  : ptr to config about input image, i.e., format, dimension, and etc.
 *
 * RETURN     : >=0 unique ID of face registerd.
 *              <0  failure.
 *==========================================================================*/
int QCamera2HardwareInterface::register_face_image(struct camera_device *device,
                                                   void *img_ptr,
                                                   cam_pp_offline_src_config_t *config)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    LOGD("E camera id %d", hw->getCameraId());
    qcamera_sm_evt_reg_face_payload_t payload;
    memset(&payload, 0, sizeof(qcamera_sm_evt_reg_face_payload_t));
    payload.img_ptr = img_ptr;
    payload.config = config;
    hw->lockAPI();
    qcamera_api_result_t apiResult;
    ret = hw->processAPI(QCAMERA_SM_EVT_REG_FACE_IMAGE, (void *)&payload);
    if (ret == NO_ERROR) {
        hw->waitAPIResult(QCAMERA_SM_EVT_REG_FACE_IMAGE, &apiResult);
        ret = apiResult.handle;
    }
    hw->unlockAPI();
    LOGD("E camera id %d", hw->getCameraId());

    return ret;
}

/*===========================================================================
 * FUNCTION   : prepare_snapshot
 *
 * DESCRIPTION: prepares hardware for snapshot
 *
 * PARAMETERS :
 *   @device  : ptr to camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::prepare_snapshot(struct camera_device *device)
{
    ATRACE_CALL();
    int ret = NO_ERROR;
    QCamera2HardwareInterface *hw =
        reinterpret_cast<QCamera2HardwareInterface *>(device->priv);
    if (!hw) {
        LOGE("NULL camera device");
        return BAD_VALUE;
    }
    if (hw->isLongshotEnabled() && hw->mPrepSnapRun == true) {
        // For longshot mode, we prepare snapshot only once
        LOGH("prepare snapshot only once ");
        return NO_ERROR;
    }
    LOGH("[KPI Perf]: E PROFILE_PREPARE_SNAPSHOT camera id %d",
             hw->getCameraId());
    hw->lockAPI();
    qcamera_api_result_t apiResult;

    /* Prepare snapshot in case LED needs to be flashed */
    if (hw->mFlashNeeded || hw->mParameters.isChromaFlashEnabled()) {
        /* Prepare snapshot in case LED needs to be flashed */
        ret = hw->processAPI(QCAMERA_SM_EVT_PREPARE_SNAPSHOT, NULL);
        if (ret == NO_ERROR) {
          hw->waitAPIResult(QCAMERA_SM_EVT_PREPARE_SNAPSHOT, &apiResult);
            ret = apiResult.status;
        }
        hw->mPrepSnapRun = true;
    }
    hw->unlockAPI();
    LOGH("[KPI Perf]: X, ret: %d", ret);
    return ret;
}

/*===========================================================================
 * FUNCTION   : QCamera2HardwareInterface
 *
 * DESCRIPTION: constructor of QCamera2HardwareInterface
 *
 * PARAMETERS :
 *   @cameraId  : camera ID
 *
 * RETURN     : none
 *==========================================================================*/
QCamera2HardwareInterface::QCamera2HardwareInterface(uint32_t cameraId)
    : mCameraId(cameraId),
      mCameraHandle(NULL),
      mCameraOpened(false),
      m_bRelCamCalibValid(false),
      mPreviewWindow(NULL),
      mMsgEnabled(0),
      mStoreMetaDataInFrame(0),
      mJpegCb(NULL),
      mCallbackCookie(NULL),
      mJpegCallbackCookie(NULL),
      m_bMpoEnabled(TRUE),
      m_stateMachine(this),
      m_smThreadActive(true),
      m_postprocessor(this),
      m_thermalAdapter(QCameraThermalAdapter::getInstance()),
      m_cbNotifier(this),
      m_bPreviewStarted(false),
      m_bRecordStarted(false),
      m_currentFocusState(CAM_AF_STATE_INACTIVE),
      mDumpFrmCnt(0U),
      mDumpSkipCnt(0U),
      mThermalLevel(QCAMERA_THERMAL_NO_ADJUSTMENT),
      mActiveAF(false),
      m_HDRSceneEnabled(false),
      mLongshotEnabled(false),
      mLiveSnapshotThread(0),
      mIntPicThread(0),
      mFlashNeeded(false),
      mFlashConfigured(false),
      mDeviceRotation(0U),
      mCaptureRotation(0U),
      mJpegExifRotation(0U),
      mUseJpegExifRotation(false),
      mIs3ALocked(false),
      mPrepSnapRun(false),
      mZoomLevel(0),
      mPreviewRestartNeeded(false),
      mVFrameCount(0),
      mVLastFrameCount(0),
      mVLastFpsTime(0),
      mVFps(0),
      mPFrameCount(0),
      mPLastFrameCount(0),
      mPLastFpsTime(0),
      mPFps(0),
      mLowLightConfigured(false),
      mInstantAecFrameCount(0),
      m_bIntJpegEvtPending(false),
      m_bIntRawEvtPending(false),
      mReprocJob(0),
      mJpegJob(0),
      mMetadataAllocJob(0),
      mInitPProcJob(0),
      mParamAllocJob(0),
      mParamInitJob(0),
      mOutputCount(0),
      mInputCount(0),
      mAdvancedCaptureConfigured(false),
      mHDRBracketingEnabled(false),
      mNumPreviewFaces(-1),
      mJpegClientHandle(0),
      mJpegHandleOwner(false),
      mMetadataMem(NULL),
      mCACDoneReceived(false),
      m_bNeedRestart(false),
      mBootToMonoTimestampOffset(0),
      bDepthAFCallbacks(true)
{
#ifdef TARGET_TS_MAKEUP
    memset(&mFaceRect, -1, sizeof(mFaceRect));
#endif
    getLogLevel();
    ATRACE_CALL();
    mCameraDevice.common.tag = HARDWARE_DEVICE_TAG;
    mCameraDevice.common.version = HARDWARE_DEVICE_API_VERSION(1, 0);
    mCameraDevice.common.close = close_camera_device;
    mCameraDevice.ops = &mCameraOps;
    mCameraDevice.priv = this;

    pthread_mutex_init(&m_lock, NULL);
    pthread_cond_init(&m_cond, NULL);

    m_apiResultList = NULL;

    pthread_mutex_init(&m_evtLock, NULL);
    pthread_cond_init(&m_evtCond, NULL);
    memset(&m_evtResult, 0, sizeof(qcamera_api_result_t));


    pthread_mutex_init(&m_int_lock, NULL);
    pthread_cond_init(&m_int_cond, NULL);

    memset(m_channels, 0, sizeof(m_channels));

    memset(&mExifParams, 0, sizeof(mm_jpeg_exif_params_t));

    memset(m_BackendFileName, 0, QCAMERA_MAX_FILEPATH_LENGTH);

    memset(mDefOngoingJobs, 0, sizeof(mDefOngoingJobs));
    memset(&mJpegMetadata, 0, sizeof(mJpegMetadata));
    memset(&mJpegHandle, 0, sizeof(mJpegHandle));
    memset(&mJpegMpoHandle, 0, sizeof(mJpegMpoHandle));

    mDeferredWorkThread.launch(deferredWorkRoutine, this);
    mDeferredWorkThread.sendCmd(CAMERA_CMD_TYPE_START_DATA_PROC, FALSE, FALSE);
    m_perfLock.lock_init();

    pthread_mutex_init(&mGrallocLock, NULL);
    mEnqueuedBuffers = 0;
    mFrameSkipStart = 0;
    mFrameSkipEnd = 0;
    mLastPreviewFrameID = 0;

    //Load and read GPU library.
    lib_surface_utils = NULL;
    LINK_get_surface_pixel_alignment = NULL;
    mSurfaceStridePadding = CAM_PAD_TO_32;
    lib_surface_utils = dlopen("libadreno_utils.so", RTLD_NOW);
    if (lib_surface_utils) {
        *(void **)&LINK_get_surface_pixel_alignment =
                dlsym(lib_surface_utils, "get_gpu_pixel_alignment");
         if (LINK_get_surface_pixel_alignment) {
             mSurfaceStridePadding = LINK_get_surface_pixel_alignment();
         }
         dlclose(lib_surface_utils);
    }
}

/*===========================================================================
 * FUNCTION   : ~QCamera2HardwareInterface
 *
 * DESCRIPTION: destructor of QCamera2HardwareInterface
 *
 * PARAMETERS : none
 *
 * RETURN     : none
 *==========================================================================*/
QCamera2HardwareInterface::~QCamera2HardwareInterface()
{
    LOGH("E");

    mDeferredWorkThread.sendCmd(CAMERA_CMD_TYPE_STOP_DATA_PROC, TRUE, TRUE);
    mDeferredWorkThread.exit();

    if (mMetadataMem != NULL) {
        delete mMetadataMem;
        mMetadataMem = NULL;
    }

    m_perfLock.lock_acq();
    lockAPI();
    m_smThreadActive = false;
    unlockAPI();
    m_stateMachine.releaseThread();
    closeCamera();
    m_perfLock.lock_rel();
    m_perfLock.lock_deinit();
    pthread_mutex_destroy(&m_lock);
    pthread_cond_destroy(&m_cond);
    pthread_mutex_destroy(&m_evtLock);
    pthread_cond_destroy(&m_evtCond);
    pthread_mutex_destroy(&m_int_lock);
    pthread_cond_destroy(&m_int_cond);
    pthread_mutex_destroy(&mGrallocLock);
    LOGH("X");
}

/*===========================================================================
 * FUNCTION   : deferPPInit
 *
 * DESCRIPTION: Queue postproc init task to deferred thread
 *
 * PARAMETERS : none
 *
 * RETURN     : uint32_t job id of pproc init job
 *              0  -- failure
 *==========================================================================*/
uint32_t QCamera2HardwareInterface::deferPPInit()
{
    // init pproc
    DeferWorkArgs args;
    DeferPProcInitArgs pprocInitArgs;

    memset(&args, 0, sizeof(DeferWorkArgs));
    memset(&pprocInitArgs, 0, sizeof(DeferPProcInitArgs));

    pprocInitArgs.jpeg_cb = jpegEvtHandle;
    pprocInitArgs.user_data = this;
    args.pprocInitArgs = pprocInitArgs;

    return queueDeferredWork(CMD_DEF_PPROC_INIT,
            args);
}

/*===========================================================================
 * FUNCTION   : openCamera
 *
 * DESCRIPTION: open camera
 *
 * PARAMETERS :
 *   @hw_device  : double ptr for camera device struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::openCamera(struct hw_device_t **hw_device)
{
    KPI_ATRACE_CALL();
    int rc = NO_ERROR;
    if (mCameraOpened) {
        *hw_device = NULL;
        LOGE("Permission Denied");
        return PERMISSION_DENIED;
    }
    LOGI("[KPI Perf]: E PROFILE_OPEN_CAMERA camera id %d",
            mCameraId);
    m_perfLock.lock_acq_timed(CAMERA_OPEN_PERF_TIME_OUT);
    rc = openCamera();
    if (rc == NO_ERROR){
        *hw_device = &mCameraDevice.common;
        if (m_thermalAdapter.init(this) != 0) {
          LOGW("Init thermal adapter failed");
        }
    }
    else
        *hw_device = NULL;

    LOGI("[KPI Perf]: X PROFILE_OPEN_CAMERA camera id %d, rc: %d",
            mCameraId, rc);

    return rc;
}

/*===========================================================================
 * FUNCTION   : openCamera
 *
 * DESCRIPTION: open camera
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::openCamera()
{
    int32_t rc = NO_ERROR;
    char value[PROPERTY_VALUE_MAX];

    if (mCameraHandle) {
        LOGE("Failure: Camera already opened");
        return ALREADY_EXISTS;
    }

    rc = QCameraFlash::getInstance().reserveFlashForCamera(mCameraId);
    if (rc < 0) {
        LOGE("Failed to reserve flash for camera id: %d",
                mCameraId);
        return UNKNOWN_ERROR;
    }

    // alloc param buffer
    DeferWorkArgs args;
    memset(&args, 0, sizeof(args));
    mParamAllocJob = queueDeferredWork(CMD_DEF_PARAM_ALLOC, args);
    if (mParamAllocJob == 0) {
        LOGE("Failed queueing PARAM_ALLOC job");
        return -ENOMEM;
    }

    if (gCamCapability[mCameraId] != NULL) {
        // allocate metadata buffers
        DeferWorkArgs args;
        DeferMetadataAllocArgs metadataAllocArgs;

        memset(&args, 0, sizeof(args));
        memset(&metadataAllocArgs, 0, sizeof(metadataAllocArgs));

        uint32_t padding =
                gCamCapability[mCameraId]->padding_info.plane_padding;
        metadataAllocArgs.size = PAD_TO_SIZE(sizeof(metadata_buffer_t),
                padding);
        metadataAllocArgs.bufferCnt = CAMERA_MIN_METADATA_BUFFERS;
        args.metadataAllocArgs = metadataAllocArgs;

        mMetadataAllocJob = queueDeferredWork(CMD_DEF_METADATA_ALLOC, args);
        if (mMetadataAllocJob == 0) {
            LOGE("Failed to allocate metadata buffer");
            rc = -ENOMEM;
            goto error_exit1;
        }

        rc = camera_open((uint8_t)mCameraId, &mCameraHandle);
        if (rc) {
            LOGE("camera_open failed. rc = %d, mCameraHandle = %p",
                     rc, mCameraHandle);
            goto error_exit2;
        }

        mCameraHandle->ops->register_event_notify(mCameraHandle->camera_handle,
                camEvtHandle,
                (void *) this);
    } else {
        LOGH("Capabilities not inited, initializing now.");

        rc = camera_open((uint8_t)mCameraId, &mCameraHandle);
        if (rc) {
            LOGE("camera_open failed. rc = %d, mCameraHandle = %p",
                     rc, mCameraHandle);
            goto error_exit2;
        }

        if(NO_ERROR != initCapabilities(mCameraId,mCameraHandle)) {
            LOGE("initCapabilities failed.");
            rc = UNKNOWN_ERROR;
            goto error_exit3;
        }

        mCameraHandle->ops->register_event_notify(mCameraHandle->camera_handle,
                camEvtHandle,
                (void *) this);
    }

    // Init params in the background
    // 1. It's safe to queue init job, even if alloc job is not yet complete.
    // It will be queued to the same thread, so the alloc is guaranteed to
    // finish first.
    // 2. However, it is not safe to begin param init until after camera is
    // open. That is why we wait until after camera open completes to schedule
    // this task.
    memset(&args, 0, sizeof(args));
    mParamInitJob = queueDeferredWork(CMD_DEF_PARAM_INIT, args);
    if (mParamInitJob == 0) {
        LOGE("Failed queuing PARAM_INIT job");
        rc = -ENOMEM;
        goto error_exit3;
    }

    mCameraOpened = true;

    //Notify display HAL that a camera session is active.
    //But avoid calling the same during bootup because camera service might open/close
    //cameras at boot time during its initialization and display service will also internally
    //wait for camera service to initialize first while calling this display API, resulting in a
    //deadlock situation. Since boot time camera open/close calls are made only to fetch
    //capabilities, no need of this display bw optimization.
    //Use "service.bootanim.exit" property to know boot status.
    property_get("service.bootanim.exit", value, "0");
    if (atoi(value) == 1) {
        pthread_mutex_lock(&gCamLock);
        if (gNumCameraSessions++ == 0) {
            setCameraLaunchStatus(true);
        }
        pthread_mutex_unlock(&gCamLock);
    }

    // Setprop to decide the time source (whether boottime or monotonic).
    // By default, use monotonic time.
    property_get("persist.camera.time.monotonic", value, "1");
    mBootToMonoTimestampOffset = 0;
    if (atoi(value) == 1) {
        // if monotonic is set, then need to use time in monotonic.
        // So, Measure the clock offset between BOOTTIME and MONOTONIC
        // The clock domain source for ISP is BOOTTIME and
        // for Video/display is MONOTONIC
        // The below offset is used to convert from clock domain of other subsystem
        // (video/hardware composer) to that of camera. Assumption is that this
        // offset won't change during the life cycle of the camera device. In other
        // words, camera device shouldn't be open during CPU suspend.
        mBootToMonoTimestampOffset = QCameraCommon::getBootToMonoTimeOffset();
    }
    LOGH("mBootToMonoTimestampOffset = %lld", mBootToMonoTimestampOffset);

    memset(value, 0, sizeof(value));
    property_get("persist.camera.depth.focus.cb", value, "1");
    bDepthAFCallbacks = atoi(value);
    mCameraHandle->ops->get_session_id(mCameraHandle->camera_handle,
        &sessionId[mCameraId]);
    return NO_ERROR;

error_exit3:
    if(mJpegClientHandle) {
        deinitJpegHandle();
    }
    mCameraHandle->ops->close_camera(mCameraHandle->camera_handle);
    mCameraHandle = NULL;
error_exit2:
    waitDeferredWork(mMetadataAllocJob);
error_exit1:
    waitDeferredWork(mParamAllocJob);
    return rc;

}

/*===========================================================================
 * FUNCTION   : bundleRelatedCameras
 *
 * DESCRIPTION: bundle cameras to enable syncing of cameras
 *
 * PARAMETERS :
 *   @sync        :indicates whether syncing is On or Off
 *   @sessionid  :session id for other camera session
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::bundleRelatedCameras(bool syncOn,
            uint32_t sessionid)
{
    LOGD("bundleRelatedCameras sync %d with sessionid %d",
            syncOn, sessionid);

    int32_t rc = mParameters.bundleRelatedCameras(syncOn, sessionid);
    if (rc != NO_ERROR) {
        LOGE("bundleRelatedCameras failed %d", rc);
        return rc;
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : getCameraSessionId
 *
 * DESCRIPTION: gets the backend session Id of this HWI instance
 *
 * PARAMETERS :
 *   @sessionid  : pointer to the output session id
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::getCameraSessionId(uint32_t* session_id)
{
    int32_t rc = NO_ERROR;

    if(session_id != NULL) {
        rc = mCameraHandle->ops->get_session_id(mCameraHandle->camera_handle,
                session_id);
        LOGD("Getting Camera Session Id %d", *session_id);
    } else {
        LOGE("Session Id is Null");
        return UNKNOWN_ERROR;
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : isFrameSyncEnabled
 *
 * DESCRIPTION: returns whether frame sync is enabled
 *
 * PARAMETERS : none
 *
 * RETURN     : bool indicating whether frame sync is enabled
 *==========================================================================*/
bool QCamera2HardwareInterface::isFrameSyncEnabled(void)
{
    return mParameters.isFrameSyncEnabled();
}

/*===========================================================================
 * FUNCTION   : setFrameSyncEnabled
 *
 * DESCRIPTION: sets whether frame sync is enabled
 *
 * PARAMETERS :
 *   @enable  : flag whether to enable or disable frame sync
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::setFrameSyncEnabled(bool enable)
{
    return mParameters.setFrameSyncEnabled(enable);
}

/*===========================================================================
 * FUNCTION   : getRelatedCamSyncInfo
 *
 * DESCRIPTION:returns the related cam sync info for this HWI instance
 *
 * PARAMETERS :none
 *
 * RETURN     : const pointer to cam_sync_related_sensors_event_info_t
 *==========================================================================*/
const cam_sync_related_sensors_event_info_t*
        QCamera2HardwareInterface::getRelatedCamSyncInfo(void)
{
    return mParameters.getRelatedCamSyncInfo();
}

/*===========================================================================
 * FUNCTION   : setRelatedCamSyncInfo
 *
 * DESCRIPTION:sets the related cam sync info for this HWI instance
 *
 * PARAMETERS :
 *   @info  : ptr to related cam info parameters
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::setRelatedCamSyncInfo(
        cam_sync_related_sensors_event_info_t* info)
{
    if(info) {
        return mParameters.setRelatedCamSyncInfo(info);
    } else {
        return BAD_TYPE;
    }
}

/*===========================================================================
 * FUNCTION   : getMpoComposition
 *
 * DESCRIPTION:function to retrieve whether Mpo composition should be enabled
 *                    or not
 *
 * PARAMETERS :none
 *
 * RETURN     : bool indicates whether mpo composition is enabled or not
 *==========================================================================*/
bool QCamera2HardwareInterface::getMpoComposition(void)
{
    LOGH("MpoComposition:%d ", m_bMpoEnabled);
    return m_bMpoEnabled;
}

/*===========================================================================
 * FUNCTION   : setMpoComposition
 *
 * DESCRIPTION:set if Mpo composition should be enabled for this HWI instance
 *
 * PARAMETERS :
 *   @enable  : indicates whether Mpo composition enabled or not
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::setMpoComposition(bool enable)
{
    // By default set Mpo composition to disable
    m_bMpoEnabled = false;

    // Enable Mpo composition only if
    // 1) frame sync is ON between two cameras and
    // 2) any advanced features are not enabled (AOST features) and
    // 3) not in recording mode (for liveshot case)
    // 4) flash is not needed
    if ((getRelatedCamSyncInfo()->sync_control == CAM_SYNC_RELATED_SENSORS_ON) &&
            !mParameters.isAdvCamFeaturesEnabled() &&
            !mParameters.getRecordingHintValue() &&
            !mFlashNeeded &&
            !isLongshotEnabled()) {
        m_bMpoEnabled = enable;
        LOGH("MpoComposition:%d ", m_bMpoEnabled);
        return NO_ERROR;
    } else {
        return BAD_TYPE;
    }
}

/*===========================================================================
 * FUNCTION   : getRecordingHintValue
 *
 * DESCRIPTION:function to retrieve recording hint value
 *
 * PARAMETERS :none
 *
 * RETURN     : bool indicates whether recording hint is enabled or not
 *==========================================================================*/
bool QCamera2HardwareInterface::getRecordingHintValue(void)
{
    return mParameters.getRecordingHintValue();
}

/*===========================================================================
 * FUNCTION   : setRecordingHintValue
 *
 * DESCRIPTION:set recording hint value
 *
 * PARAMETERS :
 *   @enable  : video hint value
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::setRecordingHintValue(int32_t value)
{
    return mParameters.updateRecordingHintValue(value);
}

/*===========================================================================
 * FUNCTION   : closeCamera
 *
 * DESCRIPTION: close camera
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::closeCamera()
{
    int rc = NO_ERROR;
    int i;
    char value[PROPERTY_VALUE_MAX];
    LOGI("E");
    if (!mCameraOpened) {
        return NO_ERROR;
    }
    LOGI("[KPI Perf]: E PROFILE_CLOSE_CAMERA camera id %d",
             mCameraId);

    // set open flag to false
    mCameraOpened = false;

    // Reset Stream config info
    mParameters.setStreamConfigure(false, false, true, sessionId);

    // deinit Parameters
    mParameters.deinit();

    // exit notifier
    m_cbNotifier.exit();

    // stop and deinit postprocessor
    waitDeferredWork(mReprocJob);
    // Close the JPEG session
    waitDeferredWork(mJpegJob);
    m_postprocessor.stop();
    deinitJpegHandle();
    m_postprocessor.deinit();
    mInitPProcJob = 0; // reset job id, so pproc can be reinited later

    m_thermalAdapter.deinit();

    // delete all channels if not already deleted
    for (i = 0; i < QCAMERA_CH_TYPE_MAX; i++) {
        if (m_channels[i] != NULL) {
            m_channels[i]->stop();
            delete m_channels[i];
            m_channels[i] = NULL;
        }
    }

    //free all pending api results here
    if(m_apiResultList != NULL) {
        api_result_list *apiResultList = m_apiResultList;
        api_result_list *apiResultListNext;
        while (apiResultList != NULL) {
            apiResultListNext = apiResultList->next;
            free(apiResultList);
            apiResultList = apiResultListNext;
        }
    }

    rc = mCameraHandle->ops->close_camera(mCameraHandle->camera_handle);
    mCameraHandle = NULL;

    //Notify display HAL that there is no active camera session
    //but avoid calling the same during bootup. Refer to openCamera
    //for more details.
    property_get("service.bootanim.exit", value, "0");
    if (atoi(value) == 1) {
        pthread_mutex_lock(&gCamLock);
        if (--gNumCameraSessions == 0) {
            setCameraLaunchStatus(false);
        }
        pthread_mutex_unlock(&gCamLock);
    }

    if (mExifParams.debug_params) {
        free(mExifParams.debug_params);
        mExifParams.debug_params = NULL;
    }

    if (QCameraFlash::getInstance().releaseFlashFromCamera(mCameraId) != 0) {
        LOGD("Failed to release flash for camera id: %d",
                mCameraId);
    }

    LOGI("[KPI Perf]: X PROFILE_CLOSE_CAMERA camera id %d, rc: %d",
         mCameraId, rc);

    return rc;
}

#define DATA_PTR(MEM_OBJ,INDEX) MEM_OBJ->getPtr( INDEX )

/*===========================================================================
 * FUNCTION   : initCapabilities
 *
 * DESCRIPTION: initialize camera capabilities in static data struct
 *
 * PARAMETERS :
 *   @cameraId  : camera Id
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::initCapabilities(uint32_t cameraId,
        mm_camera_vtbl_t *cameraHandle)
{
    ATRACE_CALL();
    int rc = NO_ERROR;
    QCameraHeapMemory *capabilityHeap = NULL;

    /* Allocate memory for capability buffer */
    capabilityHeap = new QCameraHeapMemory(QCAMERA_ION_USE_CACHE);
    rc = capabilityHeap->allocate(1, sizeof(cam_capability_t), NON_SECURE);
    if(rc != OK) {
        LOGE("No memory for cappability");
        goto allocate_failed;
    }

    /* Map memory for capability buffer */
    memset(DATA_PTR(capabilityHeap,0), 0, sizeof(cam_capability_t));

    cam_buf_map_type_list bufMapList;
    rc = QCameraBufferMaps::makeSingletonBufMapList(
            CAM_MAPPING_BUF_TYPE_CAPABILITY,
            0 /*stream id*/, 0 /*buffer index*/, -1 /*plane index*/,
            0 /*cookie*/, capabilityHeap->getFd(0), sizeof(cam_capability_t),
            bufMapList, capabilityHeap->getPtr(0));

    if (rc == NO_ERROR) {
        rc = cameraHandle->ops->map_bufs(cameraHandle->camera_handle,
                &bufMapList);
    }

    if(rc < 0) {
        LOGE("failed to map capability buffer");
        goto map_failed;
    }

    /* Query Capability */
    rc = cameraHandle->ops->query_capability(cameraHandle->camera_handle);
    if(rc < 0) {
        LOGE("failed to query capability");
        goto query_failed;
    }
    gCamCapability[cameraId] =
            (cam_capability_t *)malloc(sizeof(cam_capability_t));

    if (!gCamCapability[cameraId]) {
        LOGE("out of memory");
        goto query_failed;
    }
    memcpy(gCamCapability[cameraId], DATA_PTR(capabilityHeap,0),
                                        sizeof(cam_capability_t));

    int index;
    for (index = 0; index < CAM_ANALYSIS_INFO_MAX; index++) {
        cam_analysis_info_t *p_analysis_info =
                &gCamCapability[cameraId]->analysis_info[index];
        p_analysis_info->analysis_padding_info.offset_info.offset_x = 0;
        p_analysis_info->analysis_padding_info.offset_info.offset_y = 0;
    }

    rc = NO_ERROR;

query_failed:
    cameraHandle->ops->unmap_buf(cameraHandle->camera_handle,
                            CAM_MAPPING_BUF_TYPE_CAPABILITY);
map_failed:
    capabilityHeap->deallocate();
    delete capabilityHeap;
allocate_failed:
    return rc;
}

/*===========================================================================
 * FUNCTION   : getCapabilities
 *
 * DESCRIPTION: query camera capabilities
 *
 * PARAMETERS :
 *   @cameraId  : camera Id
 *   @info      : camera info struct to be filled in with camera capabilities
 *
 * RETURN     : int type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::getCapabilities(uint32_t cameraId,
        struct camera_info *info, cam_sync_type_t *p_cam_type)
{
    ATRACE_CALL();
    int rc = NO_ERROR;
    struct  camera_info *p_info = NULL;
    pthread_mutex_lock(&gCamLock);
    p_info = get_cam_info(cameraId, p_cam_type);
    p_info->device_version = CAMERA_DEVICE_API_VERSION_1_0;
    p_info->static_camera_characteristics = NULL;
    memcpy(info, p_info, sizeof (struct camera_info));
    pthread_mutex_unlock(&gCamLock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : getCamHalCapabilities
 *
 * DESCRIPTION: get the HAL capabilities structure
 *
 * PARAMETERS :
 *   @cameraId  : camera Id
 *
 * RETURN     : capability structure of respective camera
 *
 *==========================================================================*/
cam_capability_t* QCamera2HardwareInterface::getCamHalCapabilities()
{
    return gCamCapability[mCameraId];
}

/*===========================================================================
 * FUNCTION   : getBufNumRequired
 *
 * DESCRIPTION: return number of stream buffers needed for given stream type
 *
 * PARAMETERS :
 *   @stream_type  : type of stream
 *
 * RETURN     : number of buffers needed
 *==========================================================================*/
uint8_t QCamera2HardwareInterface::getBufNumRequired(cam_stream_type_t stream_type)
{
    int bufferCnt = 0;
    int minCaptureBuffers = mParameters.getNumOfSnapshots();
    char value[PROPERTY_VALUE_MAX];
    bool raw_yuv = false;
    int persist_cnt = 0;

    int zslQBuffers = mParameters.getZSLQueueDepth();

    int minCircularBufNum = mParameters.getMaxUnmatchedFramesInQueue() +
                            CAMERA_MIN_JPEG_ENCODING_BUFFERS;

    int maxStreamBuf = minCaptureBuffers + mParameters.getMaxUnmatchedFramesInQueue() +
                       mParameters.getNumOfExtraHDRInBufsIfNeeded() -
                       mParameters.getNumOfExtraHDROutBufsIfNeeded() +
                       mParameters.getNumOfExtraBuffersForImageProc() +
                       EXTRA_ZSL_PREVIEW_STREAM_BUF;

    int minUndequeCount = 0;
    if (!isNoDisplayMode()) {
        if(mPreviewWindow != NULL) {
            if (mPreviewWindow->get_min_undequeued_buffer_count(mPreviewWindow,&minUndequeCount)
                != 0) {
                LOGW("get_min_undequeued_buffer_count  failed");
                //TODO: hardcoded because MIN_UNDEQUEUED_BUFFERS not defined
                //minUndequeCount = BufferQueue::MIN_UNDEQUEUED_BUFFERS;
                minUndequeCount = MIN_UNDEQUEUED_BUFFERS;
            }
        } else {
            //preview window might not be set at this point. So, query directly
            //from BufferQueue implementation of gralloc buffers.
            //minUndequeCount = BufferQueue::MIN_UNDEQUEUED_BUFFERS;
            //hardcoded because MIN_UNDEQUEUED_BUFFERS not defined. REVISIT
            minUndequeCount = MIN_UNDEQUEUED_BUFFERS;
        }
        if (minUndequeCount != MIN_UNDEQUEUED_BUFFERS) {
            // minUndequeCount from valid preview window != hardcoded MIN_UNDEQUEUED_BUFFERS
            // and so change the MACRO as per minUndequeCount
            LOGW("WARNING : minUndequeCount(%d) != hardcoded value(%d)",
                     minUndequeCount, MIN_UNDEQUEUED_BUFFERS);
        }
    }

    LOGD("minCaptureBuffers = %d zslQBuffers = %d minCircularBufNum = %d"
            "maxStreamBuf = %d minUndequeCount = %d",
            minCaptureBuffers, zslQBuffers, minCircularBufNum,
            maxStreamBuf, minUndequeCount);
    // Get buffer count for the particular stream type
    switch (stream_type) {
    case CAM_STREAM_TYPE_PREVIEW:
        {
            if (mParameters.isZSLMode()) {
                // We need to add two extra streming buffers to add
                // flexibility in forming matched super buf in ZSL queue.
                // with number being 'zslQBuffers + minCircularBufNum'
                // we see preview buffers sometimes get dropped at CPP
                // and super buf is not forming in ZSL Q for long time.

                bufferCnt = zslQBuffers + minCircularBufNum +
                        mParameters.getNumOfExtraBuffersForImageProc() +
                        mParameters.getNumOfExtraBuffersForPreview() +
                        mParameters.getNumOfExtraHDRInBufsIfNeeded();
            } else {
                bufferCnt = CAMERA_MIN_STREAMING_BUFFERS +
                        mParameters.getMaxUnmatchedFramesInQueue() +
                        mParameters.getNumOfExtraBuffersForPreview();
            }
            // ISP allocates native preview buffers and so reducing same from HAL allocation
            if (bufferCnt > CAMERA_ISP_PING_PONG_BUFFERS )
                bufferCnt -= CAMERA_ISP_PING_PONG_BUFFERS;

            // Extra ZSL preview frames are not needed for HFR case.
            // Thumbnail will not be derived from preview for HFR live snapshot case.
            if ((mParameters.getRecordingHintValue() == true)
                    && (!mParameters.isHfrMode())) {
                bufferCnt += EXTRA_ZSL_PREVIEW_STREAM_BUF;
            }

            // Add the display minUndequeCount count on top of camera requirement
            bufferCnt += minUndequeCount;

            property_get("persist.camera.preview_yuv", value, "0");
            persist_cnt = atoi(value);
            if ((persist_cnt < CAM_MAX_NUM_BUFS_PER_STREAM)
                    && (bufferCnt < persist_cnt)) {
                bufferCnt = persist_cnt;
            }
        }
        break;
    case CAM_STREAM_TYPE_POSTVIEW:
        {
            bufferCnt = minCaptureBuffers +
                        mParameters.getMaxUnmatchedFramesInQueue() +
                        mParameters.getNumOfExtraHDRInBufsIfNeeded() -
                        mParameters.getNumOfExtraHDROutBufsIfNeeded() +
                        mParameters.getNumOfExtraBuffersForImageProc();

            if (bufferCnt > maxStreamBuf) {
                bufferCnt = maxStreamBuf;
            }
            bufferCnt += minUndequeCount;
        }
        break;
    case CAM_STREAM_TYPE_SNAPSHOT:
        {
            if (mParameters.isZSLMode() || mLongshotEnabled) {
                if ((minCaptureBuffers == 1 || mParameters.isUbiRefocus()) &&
                        !mLongshotEnabled) {
                    // Single ZSL snapshot case
                    bufferCnt = zslQBuffers + CAMERA_MIN_STREAMING_BUFFERS +
                            mParameters.getNumOfExtraBuffersForImageProc();
                }
                else {
                    // ZSL Burst or Longshot case
                    bufferCnt = zslQBuffers + minCircularBufNum +
                            mParameters.getNumOfExtraBuffersForImageProc();
                }
                if (getSensorType() == CAM_SENSOR_YUV && bufferCnt > CAMERA_ISP_PING_PONG_BUFFERS) {
                    //ISP allocates native buffers in YUV case
                    bufferCnt -= CAMERA_ISP_PING_PONG_BUFFERS;
                }
            } else {
                bufferCnt = minCaptureBuffers +
                            mParameters.getNumOfExtraHDRInBufsIfNeeded() -
                            mParameters.getNumOfExtraHDROutBufsIfNeeded() +
                            mParameters.getNumOfExtraBuffersForImageProc();

                if (bufferCnt > maxStreamBuf) {
                    bufferCnt = maxStreamBuf;
                }
            }
        }
        break;
    case CAM_STREAM_TYPE_RAW:
        property_get("persist.camera.raw_yuv", value, "0");
        raw_yuv = atoi(value) > 0 ? true : false;

        if (isRdiMode() || raw_yuv) {
            bufferCnt = zslQBuffers + minCircularBufNum;
        } else if (mParameters.isZSLMode()) {
            bufferCnt = zslQBuffers + minCircularBufNum;
            if (getSensorType() == CAM_SENSOR_YUV && bufferCnt > CAMERA_ISP_PING_PONG_BUFFERS) {
                //ISP allocates native buffers in YUV case
                bufferCnt -= CAMERA_ISP_PING_PONG_BUFFERS;
            }

        } else {
            bufferCnt = minCaptureBuffers +
                        mParameters.getNumOfExtraHDRInBufsIfNeeded() -
                        mParameters.getNumOfExtraHDROutBufsIfNeeded() +
                        mParameters.getNumOfExtraBuffersForImageProc();

            if (bufferCnt > maxStreamBuf) {
                bufferCnt = maxStreamBuf;
            }
        }

        property_get("persist.camera.preview_raw", value, "0");
        persist_cnt = atoi(value);
        if ((persist_cnt < CAM_MAX_NUM_BUFS_PER_STREAM)
                && (bufferCnt < persist_cnt)) {
            bufferCnt = persist_cnt;
        }
        property_get("persist.camera.video_raw", value, "0");
        persist_cnt = atoi(value);
        if ((persist_cnt < CAM_MAX_NUM_BUFS_PER_STREAM)
                && (bufferCnt < persist_cnt)) {
            bufferCnt = persist_cnt;
        }

        break;
    case CAM_STREAM_TYPE_VIDEO:
        {
            if (mParameters.getBufBatchCount()) {
                //Video Buffer in case of HFR or camera batching..
                bufferCnt = CAMERA_MIN_CAMERA_BATCH_BUFFERS;
            } else if (mParameters.getVideoBatchSize()) {
                //Video Buffer count only for HAL to HAL batching.
                bufferCnt = (CAMERA_MIN_VIDEO_BATCH_BUFFERS
                        * mParameters.getVideoBatchSize());
                if (bufferCnt < CAMERA_MIN_VIDEO_BUFFERS) {
                    bufferCnt = CAMERA_MIN_VIDEO_BUFFERS;
                }
            } else {
                // No batching enabled.
                bufferCnt = CAMERA_MIN_VIDEO_BUFFERS;
            }

            bufferCnt += mParameters.getNumOfExtraBuffersForVideo();
            //if its 4K encoding usecase, then add extra buffer
            cam_dimension_t dim;
            mParameters.getStreamDimension(CAM_STREAM_TYPE_VIDEO, dim);
            if (is4k2kResolution(&dim)) {
                 //get additional buffer count
                 property_get("vidc.enc.dcvs.extra-buff-count", value, "0");
                 persist_cnt = atoi(value);
                 if (persist_cnt >= 0 &&
                     persist_cnt < CAM_MAX_NUM_BUFS_PER_STREAM) {
                     bufferCnt += persist_cnt;
                 }
            }
        }
        break;
    case CAM_STREAM_TYPE_METADATA:
        {
            if (mParameters.isZSLMode()) {
                // MetaData buffers should be >= (Preview buffers-minUndequeCount)
                bufferCnt = zslQBuffers + minCircularBufNum +
                            mParameters.getNumOfExtraHDRInBufsIfNeeded() -
                            mParameters.getNumOfExtraHDROutBufsIfNeeded() +
                            mParameters.getNumOfExtraBuffersForImageProc() +
                            EXTRA_ZSL_PREVIEW_STREAM_BUF;
            } else {
                bufferCnt = minCaptureBuffers +
                            mParameters.getNumOfExtraHDRInBufsIfNeeded() -
                            mParameters.getNumOfExtraHDROutBufsIfNeeded() +
                            mParameters.getMaxUnmatchedFramesInQueue() +
                            CAMERA_MIN_STREAMING_BUFFERS +
                            mParameters.getNumOfExtraBuffersForImageProc();

                if (bufferCnt > zslQBuffers + minCircularBufNum) {
                    bufferCnt = zslQBuffers + minCircularBufNum;
                }
            }
            if (CAMERA_MIN_METADATA_BUFFERS > bufferCnt) {
                bufferCnt = CAMERA_MIN_METADATA_BUFFERS;
            }
        }
        break;
    case CAM_STREAM_TYPE_OFFLINE_PROC:
        {
            bufferCnt = minCaptureBuffers;
            // One of the ubifocus buffers is miscellaneous buffer
            if (mParameters.isUbiRefocus()) {
                bufferCnt -= 1;
            }
            if (mLongshotEnabled) {
                bufferCnt = mParameters.getLongshotStages();
            }
        }
        break;
    case CAM_STREAM_TYPE_CALLBACK:
        bufferCnt = CAMERA_MIN_CALLBACK_BUFFERS;
        break;
    case CAM_STREAM_TYPE_ANALYSIS:
    case CAM_STREAM_TYPE_DEFAULT:
    case CAM_STREAM_TYPE_MAX:
    default:
        bufferCnt = 0;
        break;
    }

    LOGH("Buffer count = %d for stream type = %d", bufferCnt, stream_type);
    if (bufferCnt < 0 || CAM_MAX_NUM_BUFS_PER_STREAM < bufferCnt) {
        LOGW("Buffer count %d for stream type %d exceeds limit %d",
                 bufferCnt, stream_type, CAM_MAX_NUM_BUFS_PER_STREAM);
        return CAM_MAX_NUM_BUFS_PER_STREAM;
    }

    return (uint8_t)bufferCnt;
}

/*===========================================================================
 * FUNCTION   : allocateStreamBuf
 *
 * DESCRIPTION: alocate stream buffers
 *
 * PARAMETERS :
 *   @stream_type  : type of stream
 *   @size         : size of buffer
 *   @stride       : stride of buffer
 *   @scanline     : scanline of buffer
 *   @bufferCnt    : [IN/OUT] minimum num of buffers to be allocated.
 *                   could be modified during allocation if more buffers needed
 *
 * RETURN     : ptr to a memory obj that holds stream buffers.
 *              NULL if failed
 *==========================================================================*/
QCameraMemory *QCamera2HardwareInterface::allocateStreamBuf(
        cam_stream_type_t stream_type, size_t size, int stride, int scanline,
        uint8_t &bufferCnt)
{
    int rc = NO_ERROR;
    QCameraMemory *mem = NULL;
    bool bCachedMem = QCAMERA_ION_USE_CACHE;
    bool bPoolMem = false;
    char value[PROPERTY_VALUE_MAX];
    property_get("persist.camera.mem.usepool", value, "1");
    if (atoi(value) == 1) {
        bPoolMem = true;
    }

    // Allocate stream buffer memory object
    switch (stream_type) {
    case CAM_STREAM_TYPE_PREVIEW:
        {
            if (isNoDisplayMode()) {
                mem = new QCameraStreamMemory(mGetMemory,
                        mCallbackCookie,
                        bCachedMem,
                        (bPoolMem) ? &m_memoryPool : NULL,
                        stream_type);
            } else {
                cam_dimension_t dim;
                int minFPS, maxFPS;
                QCameraGrallocMemory *grallocMemory = NULL;

                grallocMemory = new QCameraGrallocMemory(mGetMemory, mCallbackCookie);

                mParameters.getStreamDimension(stream_type, dim);
                /* we are interested only in maxfps here */
                mParameters.getPreviewFpsRange(&minFPS, &maxFPS);
                int usage = 0;
                if(mParameters.isUBWCEnabled()) {
                    cam_format_t fmt;
                    mParameters.getStreamFormat(CAM_STREAM_TYPE_PREVIEW,fmt);
                    if (fmt == CAM_FORMAT_YUV_420_NV12_UBWC) {
                        usage = GRALLOC_USAGE_PRIVATE_ALLOC_UBWC ;
                    }
                }
                if (grallocMemory) {
                    grallocMemory->setMappable(
                            CAMERA_INITIAL_MAPPABLE_PREVIEW_BUFFERS);
                    grallocMemory->setWindowInfo(mPreviewWindow,
                            dim.width,dim.height, stride, scanline,
                            mParameters.getPreviewHalPixelFormat(),
                            maxFPS, usage);
                    pthread_mutex_lock(&mGrallocLock);
                    if (bufferCnt > CAMERA_INITIAL_MAPPABLE_PREVIEW_BUFFERS) {
                        mEnqueuedBuffers = (bufferCnt -
                                CAMERA_INITIAL_MAPPABLE_PREVIEW_BUFFERS);
                    } else {
                        mEnqueuedBuffers = 0;
                    }
                    pthread_mutex_unlock(&mGrallocLock);
                }
                mem = grallocMemory;
            }
        }
        break;
    case CAM_STREAM_TYPE_POSTVIEW:
        {
            if (isNoDisplayMode() || isPreviewRestartEnabled()) {
                mem = new QCameraStreamMemory(mGetMemory, mCallbackCookie, bCachedMem);
            } else {
                cam_dimension_t dim;
                int minFPS, maxFPS;
                QCameraGrallocMemory *grallocMemory =
                        new QCameraGrallocMemory(mGetMemory, mCallbackCookie);

                mParameters.getStreamDimension(stream_type, dim);
                /* we are interested only in maxfps here */
                mParameters.getPreviewFpsRange(&minFPS, &maxFPS);
                if (grallocMemory) {
                    grallocMemory->setWindowInfo(mPreviewWindow, dim.width,
                            dim.height, stride, scanline,
                            mParameters.getPreviewHalPixelFormat(), maxFPS);
                }
                mem = grallocMemory;
            }
        }
        break;
    case CAM_STREAM_TYPE_ANALYSIS:
    case CAM_STREAM_TYPE_SNAPSHOT:
    case CAM_STREAM_TYPE_RAW:
    case CAM_STREAM_TYPE_OFFLINE_PROC:
        mem = new QCameraStreamMemory(mGetMemory,
                mCallbackCookie,
                bCachedMem,
                (bPoolMem) ? &m_memoryPool : NULL,
                stream_type);
        break;
    case CAM_STREAM_TYPE_METADATA:
        {
            if (mMetadataMem == NULL) {
                mem = new QCameraMetadataStreamMemory(QCAMERA_ION_USE_CACHE);
            } else {
                mem = mMetadataMem;
                mMetadataMem = NULL;

                int32_t numAdditionalBuffers = bufferCnt - mem->getCnt();
                if (numAdditionalBuffers > 0) {
                    rc = mem->allocateMore(numAdditionalBuffers, size);
                    if (rc != NO_ERROR) {
                        LOGE("Failed to allocate additional buffers, "
                                "but attempting to proceed.");
                    }
                }
                bufferCnt = mem->getCnt();
                // The memory is already allocated  and initialized, so
                // simply return here.
                return mem;
            }
        }
        break;
    case CAM_STREAM_TYPE_VIDEO:
        {
            //Use uncached allocation by default
            if (mParameters.isVideoBuffersCached() || mParameters.isSeeMoreEnabled() ||
                    mParameters.isHighQualityNoiseReductionMode()) {
                bCachedMem = QCAMERA_ION_USE_CACHE;
            }
            else {
                bCachedMem = QCAMERA_ION_USE_NOCACHE;
            }

            QCameraVideoMemory *videoMemory = NULL;
            if (mParameters.getVideoBatchSize()) {
                videoMemory = new QCameraVideoMemory(
                        mGetMemory, mCallbackCookie, FALSE, QCAMERA_MEM_TYPE_BATCH);
                if (videoMemory == NULL) {
                    LOGE("Out of memory for video batching obj");
                    return NULL;
                }
                /*
                *   numFDs = BATCH size
                *  numInts = 5  // OFFSET, SIZE, USAGE, TIMESTAMP, FORMAT
                */
                rc = videoMemory->allocateMeta(
                        CAMERA_MIN_VIDEO_BATCH_BUFFERS,
                        mParameters.getVideoBatchSize(),
                        VIDEO_METADATA_NUM_INTS);
                if (rc < 0) {
                    delete videoMemory;
                    return NULL;
                }
            } else {
                videoMemory =
                        new QCameraVideoMemory(mGetMemory, mCallbackCookie, bCachedMem);
                if (videoMemory == NULL) {
                    LOGE("Out of memory for video obj");
                    return NULL;
                }
            }

            int usage = 0;
            cam_format_t fmt;
            mParameters.getStreamFormat(CAM_STREAM_TYPE_VIDEO,fmt);
            if (mParameters.isUBWCEnabled() && (fmt == CAM_FORMAT_YUV_420_NV12_UBWC)) {
                usage = private_handle_t::PRIV_FLAGS_UBWC_ALIGNED;
            }
            videoMemory->setVideoInfo(usage, fmt);
            mem = videoMemory;
        }
        break;
    case CAM_STREAM_TYPE_CALLBACK:
        mem = new QCameraStreamMemory(mGetMemory,
                mCallbackCookie,
                bCachedMem,
                (bPoolMem) ? &m_memoryPool : NULL,
                stream_type);
        break;
    case CAM_STREAM_TYPE_DEFAULT:
    case CAM_STREAM_TYPE_MAX:
    default:
        break;
    }
    if (!mem) {
        return NULL;
    }

    if (bufferCnt > 0) {
        if (mParameters.isSecureMode() &&
            (stream_type == CAM_STREAM_TYPE_RAW) &&
            (mParameters.isRdiMode())) {
            LOGD("Allocating %d secure buffers of size %d ", bufferCnt, size);
            rc = mem->allocate(bufferCnt, size, SECURE);
        } else {
            rc = mem->allocate(bufferCnt, size, NON_SECURE);
        }
        if (rc < 0) {
            delete mem;
            return NULL;
        }
        bufferCnt = mem->getCnt();
    }
    LOGH("rc = %d type = %d count = %d size = %d cache = %d, pool = %d mEnqueuedBuffers = %d",
            rc, stream_type, bufferCnt, size, bCachedMem, bPoolMem, mEnqueuedBuffers);
    return mem;
}

/*===========================================================================
 * FUNCTION   : allocateMoreStreamBuf
 *
 * DESCRIPTION: alocate more stream buffers from the memory object
 *
 * PARAMETERS :
 *   @mem_obj      : memory object ptr
 *   @size         : size of buffer
 *   @bufferCnt    : [IN/OUT] additional number of buffers to be allocated.
 *                   output will be the number of total buffers
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::allocateMoreStreamBuf(
        QCameraMemory *mem_obj, size_t size, uint8_t &bufferCnt)
{
    int rc = NO_ERROR;

    if (bufferCnt > 0) {
        rc = mem_obj->allocateMore(bufferCnt, size);
        bufferCnt = mem_obj->getCnt();
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : allocateMiscBuf
 *
 * DESCRIPTION: alocate miscellaneous buffer
 *
 * PARAMETERS :
 *   @streamInfo  : stream info
 *
 * RETURN     : ptr to a memory obj that holds stream info buffer.
 *              NULL if failed
 *==========================================================================*/
QCameraHeapMemory *QCamera2HardwareInterface::allocateMiscBuf(
        cam_stream_info_t *streamInfo)
{
    int rc = NO_ERROR;
    uint8_t bufNum = 0;
    size_t bufSize = 0;
    QCameraHeapMemory *miscBuf = NULL;
    cam_feature_mask_t feature_mask =
            streamInfo->reprocess_config.pp_feature_config.feature_mask;

    switch (streamInfo->stream_type) {
    case CAM_STREAM_TYPE_OFFLINE_PROC:
        if (CAM_QCOM_FEATURE_TRUEPORTRAIT & feature_mask) {
            bufNum = 1;
            bufSize = mParameters.getTPMaxMetaSize();
        } else if (CAM_QCOM_FEATURE_REFOCUS & feature_mask) {
            bufNum = 1;
            bufSize = mParameters.getRefocusMaxMetaSize();
        }
        break;
    default:
        break;
    }

    if (bufNum && bufSize) {
        miscBuf = new QCameraHeapMemory(QCAMERA_ION_USE_CACHE);

        if (!miscBuf) {
            LOGE("Unable to allocate miscBuf object");
            return NULL;
        }

        rc = miscBuf->allocate(bufNum, bufSize, NON_SECURE);
        if (rc < 0) {
            LOGE("Failed to allocate misc buffer memory");
            delete miscBuf;
            return NULL;
        }
    }

    return miscBuf;
}

/*===========================================================================
 * FUNCTION   : allocateStreamInfoBuf
 *
 * DESCRIPTION: alocate stream info buffer
 *
 * PARAMETERS :
 *   @stream_type  : type of stream
 *
 * RETURN     : ptr to a memory obj that holds stream info buffer.
 *              NULL if failed
 *==========================================================================*/
QCameraHeapMemory *QCamera2HardwareInterface::allocateStreamInfoBuf(
        cam_stream_type_t stream_type)
{
    int rc = NO_ERROR;
    char value[PROPERTY_VALUE_MAX];
    bool raw_yuv = false;
    int32_t dt = 0;
    int32_t vc = 0;


    QCameraHeapMemory *streamInfoBuf = new QCameraHeapMemory(QCAMERA_ION_USE_CACHE);
    if (!streamInfoBuf) {
        LOGE("allocateStreamInfoBuf: Unable to allocate streamInfo object");
        return NULL;
    }

    rc = streamInfoBuf->allocate(1, sizeof(cam_stream_info_t), NON_SECURE);
    if (rc < 0) {
        LOGE("allocateStreamInfoBuf: Failed to allocate stream info memory");
        delete streamInfoBuf;
        return NULL;
    }

    cam_stream_info_t *streamInfo = (cam_stream_info_t *)streamInfoBuf->getPtr(0);
    memset(streamInfo, 0, sizeof(cam_stream_info_t));
    streamInfo->stream_type = stream_type;
    rc = mParameters.getStreamFormat(stream_type, streamInfo->fmt);
    rc = mParameters.getStreamDimension(stream_type, streamInfo->dim);
    rc = mParameters.getStreamRotation(stream_type, streamInfo->pp_config, streamInfo->dim);
    streamInfo->num_bufs = getBufNumRequired(stream_type);
    streamInfo->streaming_mode = CAM_STREAMING_MODE_CONTINUOUS;
    streamInfo->is_secure = NON_SECURE;

    switch (stream_type) {
    case CAM_STREAM_TYPE_SNAPSHOT:
        if ((mParameters.isZSLMode() && mParameters.getRecordingHintValue() != true) ||
            mLongshotEnabled) {
            streamInfo->streaming_mode = CAM_STREAMING_MODE_CONTINUOUS;
        } else {
            streamInfo->streaming_mode = CAM_STREAMING_MODE_BURST;
            streamInfo->num_of_burst = (uint8_t)
                    (mParameters.getNumOfSnapshots()
                        + mParameters.getNumOfExtraHDRInBufsIfNeeded()
                        - mParameters.getNumOfExtraHDROutBufsIfNeeded()
                        + mParameters.getNumOfExtraBuffersForImageProc());
        }
        break;
    case CAM_STREAM_TYPE_RAW:
        property_get("persist.camera.raw_yuv", value, "0");
        raw_yuv = atoi(value) > 0 ? true : false;
        if ((mParameters.isZSLMode()) || (isRdiMode()) || (raw_yuv)) {
            streamInfo->streaming_mode = CAM_STREAMING_MODE_CONTINUOUS;
        } else {
            streamInfo->streaming_mode = CAM_STREAMING_MODE_BURST;
            streamInfo->num_of_burst = mParameters.getNumOfSnapshots();
        }
        if (mParameters.isSecureMode() && mParameters.isRdiMode()) {
            streamInfo->is_secure = SECURE;
        } else {
            streamInfo->is_secure = NON_SECURE;
        }
        if (CAM_FORMAT_META_RAW_10BIT == streamInfo->fmt) {
            mParameters.updateDtVc(&dt, &vc);
            if (dt)
                streamInfo->dt = dt;
            streamInfo->vc = vc;
        }

        break;
    case CAM_STREAM_TYPE_POSTVIEW:
        if (mLongshotEnabled) {
            streamInfo->streaming_mode = CAM_STREAMING_MODE_CONTINUOUS;
        } else {
            streamInfo->streaming_mode = CAM_STREAMING_MODE_BURST;
            streamInfo->num_of_burst = (uint8_t)(mParameters.getNumOfSnapshots()
                + mParameters.getNumOfExtraHDRInBufsIfNeeded()
                - mParameters.getNumOfExtraHDROutBufsIfNeeded()
                + mParameters.getNumOfExtraBuffersForImageProc());
        }
        break;
    case CAM_STREAM_TYPE_VIDEO:
        streamInfo->dis_enable = mParameters.isDISEnabled();
        if (mParameters.getBufBatchCount()) {
            //Update stream info structure with batch mode info
            streamInfo->streaming_mode = CAM_STREAMING_MODE_BATCH;
            streamInfo->user_buf_info.frame_buf_cnt = mParameters.getBufBatchCount();
            streamInfo->user_buf_info.size =
                    (uint32_t)(sizeof(struct msm_camera_user_buf_cont_t));
            cam_fps_range_t pFpsRange;
            mParameters.getHfrFps(pFpsRange);
            streamInfo->user_buf_info.frameInterval =
                    (long)((1000/pFpsRange.video_max_fps) * 1000);
            LOGH("Video Batch Count = %d, interval = %d",
                    streamInfo->user_buf_info.frame_buf_cnt,
                    streamInfo->user_buf_info.frameInterval);
        }
        if (mParameters.getRecordingHintValue()) {
            if(mParameters.isDISEnabled()) {
                streamInfo->is_type = mParameters.getVideoISType();
            } else {
                streamInfo->is_type = IS_TYPE_NONE;
            }
        }
        if (mParameters.isSecureMode()) {
            streamInfo->is_secure = SECURE;
        }
        break;
    case CAM_STREAM_TYPE_PREVIEW:
        if (mParameters.getRecordingHintValue()) {
            if(mParameters.isDISEnabled()) {
                streamInfo->is_type = mParameters.getPreviewISType();
            } else {
                streamInfo->is_type = IS_TYPE_NONE;
            }
        }
        if (mParameters.isSecureMode()) {
            streamInfo->is_secure = SECURE;
        }
        break;
    case CAM_STREAM_TYPE_ANALYSIS:
        streamInfo->noFrameExpected = 1;
        break;
    default:
        break;
    }

    // Update feature mask
    mParameters.updatePpFeatureMask(stream_type);

    // Get feature mask
    mParameters.getStreamPpMask(stream_type, streamInfo->pp_config.feature_mask);

    // Update pp config
    if (streamInfo->pp_config.feature_mask & CAM_QCOM_FEATURE_FLIP) {
        int flipMode = mParameters.getFlipMode(stream_type);
        if (flipMode > 0) {
            streamInfo->pp_config.flip = (uint32_t)flipMode;
        }
    }
    if (streamInfo->pp_config.feature_mask & CAM_QCOM_FEATURE_SHARPNESS) {
        streamInfo->pp_config.sharpness = mParameters.getSharpness();
    }
    if (streamInfo->pp_config.feature_mask & CAM_QCOM_FEATURE_EFFECT) {
        streamInfo->pp_config.effect = mParameters.getEffectValue();
    }

    if (streamInfo->pp_config.feature_mask & CAM_QCOM_FEATURE_DENOISE2D) {
        streamInfo->pp_config.denoise2d.denoise_enable = 1;
        streamInfo->pp_config.denoise2d.process_plates =
                mParameters.getDenoiseProcessPlate(CAM_INTF_PARM_WAVELET_DENOISE);
    }

    if (!((needReprocess()) && (CAM_STREAM_TYPE_SNAPSHOT == stream_type ||
            CAM_STREAM_TYPE_RAW == stream_type))) {
        if (gCamCapability[mCameraId]->qcom_supported_feature_mask &
                CAM_QCOM_FEATURE_CROP)
            streamInfo->pp_config.feature_mask |= CAM_QCOM_FEATURE_CROP;
        if (gCamCapability[mCameraId]->qcom_supported_feature_mask &
                CAM_QCOM_FEATURE_SCALE)
            streamInfo->pp_config.feature_mask |= CAM_QCOM_FEATURE_SCALE;
    }

    LOGH("type %d, fmt %d, dim %dx%d, num_bufs %d mask = 0x%x is_type %d\n",
           stream_type, streamInfo->fmt, streamInfo->dim.width,
           streamInfo->dim.height, streamInfo->num_bufs,
           streamInfo->pp_config.feature_mask,
           streamInfo->is_type);

    return streamInfoBuf;
}

/*===========================================================================
 * FUNCTION   : allocateStreamUserBuf
 *
 * DESCRIPTION: allocate user ptr for stream buffers
 *
 * PARAMETERS :
 *   @streamInfo  : stream info structure
 *
 * RETURN     : ptr to a memory obj that holds stream info buffer.
 *                    NULL if failed

 *==========================================================================*/
QCameraMemory *QCamera2HardwareInterface::allocateStreamUserBuf(
        cam_stream_info_t *streamInfo)
{
    int rc = NO_ERROR;
    QCameraMemory *mem = NULL;
    int size = 0;

    if (streamInfo->streaming_mode != CAM_STREAMING_MODE_BATCH) {
        LOGE("Stream is not in BATCH mode. Invalid Stream");
        return NULL;
    }

    // Allocate stream user buffer memory object
    switch (streamInfo->stream_type) {
    case CAM_STREAM_TYPE_VIDEO: {
        QCameraVideoMemory *video_mem = new QCameraVideoMemory(
                mGetMemory, mCallbackCookie, FALSE, QCAMERA_MEM_TYPE_BATCH);
        if (video_mem == NULL) {
            LOGE("Out of memory for video obj");
            return NULL;
        }
        /*
        *   numFDs = BATCH size
        *  numInts = 5  // OFFSET, SIZE, USAGE, TIMESTAMP, FORMAT
        */
        rc = video_mem->allocateMeta(streamInfo->num_bufs,
                mParameters.getBufBatchCount(), VIDEO_METADATA_NUM_INTS);
        if (rc < 0) {
            LOGE("allocateMeta failed");
            delete video_mem;
            return NULL;
        }
        int usage = 0;
        cam_format_t fmt;
        mParameters.getStreamFormat(CAM_STREAM_TYPE_VIDEO, fmt);
        if(mParameters.isUBWCEnabled() && (fmt == CAM_FORMAT_YUV_420_NV12_UBWC)) {
            usage = private_handle_t::PRIV_FLAGS_UBWC_ALIGNED;
        }
        video_mem->setVideoInfo(usage, fmt);
        mem = static_cast<QCameraMemory *>(video_mem);
    }
    break;

    case CAM_STREAM_TYPE_PREVIEW:
    case CAM_STREAM_TYPE_POSTVIEW:
    case CAM_STREAM_TYPE_ANALYSIS:
    case CAM_STREAM_TYPE_SNAPSHOT:
    case CAM_STREAM_TYPE_RAW:
    case CAM_STREAM_TYPE_METADATA:
    case CAM_STREAM_TYPE_OFFLINE_PROC:
    case CAM_STREAM_TYPE_CALLBACK:
        LOGE("Stream type Not supported.for BATCH processing");
    break;

    case CAM_STREAM_TYPE_DEFAULT:
    case CAM_STREAM_TYPE_MAX:
    default:
        break;
    }
    if (!mem) {
        LOGE("Failed to allocate mem");
        return NULL;
    }

    /*Size of this buffer will be number of batch buffer */
    size = PAD_TO_SIZE((streamInfo->num_bufs * streamInfo->user_buf_info.size),
            CAM_PAD_TO_4K);

    LOGH("Allocating BATCH Buffer count = %d", streamInfo->num_bufs);

    if (size > 0) {
        // Allocating one buffer for all batch buffers
        rc = mem->allocate(1, size, NON_SECURE);
        if (rc < 0) {
            delete mem;
            return NULL;
        }
    }
    return mem;
}


/*===========================================================================
 * FUNCTION   : waitForDeferredAlloc
 *
 * DESCRIPTION: Wait for deferred allocation, if applicable
 *              (applicable only for metadata buffers so far)
 *
 * PARAMETERS :
 *   @stream_type  : type of stream to (possibly) wait for
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera2HardwareInterface::waitForDeferredAlloc(cam_stream_type_t stream_type)
{
    if (stream_type == CAM_STREAM_TYPE_METADATA) {
        waitDeferredWork(mMetadataAllocJob);
    }
}


/*===========================================================================
 * FUNCTION   : setPreviewWindow
 *
 * DESCRIPTION: set preview window impl
 *
 * PARAMETERS :
 *   @window  : ptr to window ops table struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::setPreviewWindow(
        struct preview_stream_ops *window)
{
    mPreviewWindow = window;
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : setCallBacks
 *
 * DESCRIPTION: set callbacks impl
 *
 * PARAMETERS :
 *   @notify_cb  : notify cb
 *   @data_cb    : data cb
 *   @data_cb_timestamp : data cb with time stamp
 *   @get_memory : request memory ops table
 *   @user       : user data ptr
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::setCallBacks(camera_notify_callback notify_cb,
                                            camera_data_callback data_cb,
                                            camera_data_timestamp_callback data_cb_timestamp,
                                            camera_request_memory get_memory,
                                            void *user)
{
    mNotifyCb        = notify_cb;
    mDataCb          = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mGetMemory       = get_memory;
    mCallbackCookie  = user;
    m_cbNotifier.setCallbacks(notify_cb, data_cb, data_cb_timestamp, user);
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : setJpegCallBacks
 *
 * DESCRIPTION: set JPEG callbacks impl
 *
 * PARAMETERS :
 *   @jpegCb  : Jpeg callback method
 *   @callbackCookie    : callback cookie
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
void QCamera2HardwareInterface::setJpegCallBacks(jpeg_data_callback jpegCb,
                                            void *callbackCookie)
{
    LOGH("camera id %d", getCameraId());
    mJpegCb        = jpegCb;
    mJpegCallbackCookie  = callbackCookie;
    m_cbNotifier.setJpegCallBacks(mJpegCb, mJpegCallbackCookie);
}

/*===========================================================================
 * FUNCTION   : enableMsgType
 *
 * DESCRIPTION: enable msg type impl
 *
 * PARAMETERS :
 *   @msg_type  : msg type mask to be enabled
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::enableMsgType(int32_t msg_type)
{
    int32_t rc = NO_ERROR;

    if (mParameters.isUBWCEnabled()) {
        /*Need Special CALLBACK stream incase application requesting for
              Preview callback  in UBWC case*/
        if (!(msgTypeEnabled(CAMERA_MSG_PREVIEW_FRAME)) &&
                (msg_type & CAMERA_MSG_PREVIEW_FRAME)) {
            // Start callback channel only when preview/zsl channel is active
            QCameraChannel* previewCh = NULL;
            if (isZSLMode() && (getRecordingHintValue() != true)) {
                previewCh = m_channels[QCAMERA_CH_TYPE_ZSL];
            } else {
                previewCh = m_channels[QCAMERA_CH_TYPE_PREVIEW];
            }
            QCameraChannel* callbackCh = m_channels[QCAMERA_CH_TYPE_CALLBACK];
            if ((callbackCh != NULL) &&
                    (previewCh != NULL) && previewCh->isActive()) {
                rc = startChannel(QCAMERA_CH_TYPE_CALLBACK);
                if (rc != NO_ERROR) {
                    LOGE("START Callback Channel failed");
                }
            }
        }
    }
    mMsgEnabled |= msg_type;
    LOGH("(0x%x) : mMsgEnabled = 0x%x rc = %d", msg_type , mMsgEnabled, rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : disableMsgType
 *
 * DESCRIPTION: disable msg type impl
 *
 * PARAMETERS :
 *   @msg_type  : msg type mask to be disabled
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::disableMsgType(int32_t msg_type)
{
    int32_t rc = NO_ERROR;

    if (mParameters.isUBWCEnabled()) {
        /*STOP CALLBACK STREAM*/
        if ((msgTypeEnabled(CAMERA_MSG_PREVIEW_FRAME)) &&
                (msg_type & CAMERA_MSG_PREVIEW_FRAME)) {
            // Stop callback channel only if it is active
            if ((m_channels[QCAMERA_CH_TYPE_CALLBACK] != NULL) &&
                   (m_channels[QCAMERA_CH_TYPE_CALLBACK]->isActive())) {
                rc = stopChannel(QCAMERA_CH_TYPE_CALLBACK);
                if (rc != NO_ERROR) {
                    LOGE("STOP Callback Channel failed");
                }
            }
        }
    }
    mMsgEnabled &= ~msg_type;
    LOGH("(0x%x) : mMsgEnabled = 0x%x rc = %d", msg_type , mMsgEnabled, rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : msgTypeEnabled
 *
 * DESCRIPTION: impl to determine if certain msg_type is enabled
 *
 * PARAMETERS :
 *   @msg_type  : msg type mask
 *
 * RETURN     : 0 -- not enabled
 *              none 0 -- enabled
 *==========================================================================*/
int QCamera2HardwareInterface::msgTypeEnabled(int32_t msg_type)
{
    return (mMsgEnabled & msg_type);
}

/*===========================================================================
 * FUNCTION   : msgTypeEnabledWithLock
 *
 * DESCRIPTION: impl to determine if certain msg_type is enabled with lock
 *
 * PARAMETERS :
 *   @msg_type  : msg type mask
 *
 * RETURN     : 0 -- not enabled
 *              none 0 -- enabled
 *==========================================================================*/
int QCamera2HardwareInterface::msgTypeEnabledWithLock(int32_t msg_type)
{
    int enabled = 0;
    lockAPI();
    enabled = mMsgEnabled & msg_type;
    unlockAPI();
    return enabled;
}

/*===========================================================================
 * FUNCTION   : startPreview
 *
 * DESCRIPTION: start preview impl
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::startPreview()
{
    KPI_ATRACE_CALL();
    int32_t rc = NO_ERROR;

    LOGI("E ZSL = %d Recording Hint = %d", mParameters.isZSLMode(),
            mParameters.getRecordingHintValue());

    m_perfLock.lock_acq();

    updateThermalLevel((void *)&mThermalLevel);

    setDisplayFrameSkip();

    // start preview stream
    if (mParameters.isZSLMode() && mParameters.getRecordingHintValue() != true) {
        rc = startChannel(QCAMERA_CH_TYPE_ZSL);
    } else {
        rc = startChannel(QCAMERA_CH_TYPE_PREVIEW);
    }

    if (rc != NO_ERROR) {
        LOGE("failed to start channels");
        m_perfLock.lock_rel();
        return rc;
    }

    if ((msgTypeEnabled(CAMERA_MSG_PREVIEW_FRAME))
            && (m_channels[QCAMERA_CH_TYPE_CALLBACK] != NULL)) {
        rc = startChannel(QCAMERA_CH_TYPE_CALLBACK);
        if (rc != NO_ERROR) {
            LOGE("failed to start callback stream");
            stopChannel(QCAMERA_CH_TYPE_ZSL);
            stopChannel(QCAMERA_CH_TYPE_PREVIEW);
            m_perfLock.lock_rel();
            return rc;
        }
    }

    updatePostPreviewParameters();
    m_stateMachine.setPreviewCallbackNeeded(true);

    // if job id is non-zero, that means the postproc init job is already
    // pending or complete
    if (mInitPProcJob == 0) {
        mInitPProcJob = deferPPInit();
        if (mInitPProcJob == 0) {
            LOGE("Unable to initialize postprocessor, mCameraHandle = %p",
                     mCameraHandle);
            rc = -ENOMEM;
            m_perfLock.lock_rel();
            return rc;
        }
    }
    m_perfLock.lock_rel();

    if (rc == NO_ERROR) {
        // Set power Hint for preview
        m_perfLock.powerHint(POWER_HINT_VIDEO_ENCODE, true);
    }

    LOGI("X rc = %d", rc);
    return rc;
}

int32_t QCamera2HardwareInterface::updatePostPreviewParameters() {
    // Enable OIS only in Camera mode and 4k2k camcoder mode
    int32_t rc = NO_ERROR;
    rc = mParameters.updateOisValue(1);
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : stopPreview
 *
 * DESCRIPTION: stop preview impl
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::stopPreview()
{
    KPI_ATRACE_CALL();
    LOGI("E");
    mNumPreviewFaces = -1;
    mActiveAF = false;

    // Disable power Hint for preview
    m_perfLock.powerHint(POWER_HINT_VIDEO_ENCODE, false);

    m_perfLock.lock_acq();

    // stop preview stream
    stopChannel(QCAMERA_CH_TYPE_CALLBACK);
    stopChannel(QCAMERA_CH_TYPE_ZSL);
    stopChannel(QCAMERA_CH_TYPE_PREVIEW);
    stopChannel(QCAMERA_CH_TYPE_RAW);

    m_cbNotifier.flushPreviewNotifications();
    //add for ts makeup
#ifdef TARGET_TS_MAKEUP
    ts_makeup_finish();
#endif
    // delete all channels from preparePreview
    unpreparePreview();

    m_perfLock.lock_rel();

    LOGI("X");
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : storeMetaDataInBuffers
 *
 * DESCRIPTION: enable store meta data in buffers for video frames impl
 *
 * PARAMETERS :
 *   @enable  : flag if need enable
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::storeMetaDataInBuffers(int enable)
{
    mStoreMetaDataInFrame = enable;
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : preStartRecording
 *
 * DESCRIPTION: Prepare start recording impl
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::preStartRecording()
{
    int32_t rc = NO_ERROR;
    LOGH("E");
    if (mParameters.getRecordingHintValue() == false) {

        // Give HWI control to restart preview only in single camera mode.
        // In dual-cam mode, this control belongs to muxer.
        if (getRelatedCamSyncInfo()->sync_control != CAM_SYNC_RELATED_SENSORS_ON) {
            LOGH("start recording when hint is false, stop preview first");
            stopPreview();

            // Set recording hint to TRUE
            mParameters.updateRecordingHintValue(TRUE);
            rc = preparePreview();
            if (rc == NO_ERROR) {
                rc = startPreview();
            }
        }
        else
        {
            // For dual cam mode, update the flag mPreviewRestartNeeded to true
            // Restart control will be handled by muxer.
            mPreviewRestartNeeded = true;
        }
    }

    LOGH("X rc = %d", rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : startRecording
 *
 * DESCRIPTION: start recording impl
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::startRecording()
{
    int32_t rc = NO_ERROR;

    LOGI("E");
    //link meta stream with video channel if low power mode.
    if (isLowPowerMode()) {
        // Find and try to link a metadata stream from preview channel
        QCameraChannel *pMetaChannel = NULL;
        QCameraStream *pMetaStream = NULL;
        QCameraChannel *pVideoChannel = m_channels[QCAMERA_CH_TYPE_VIDEO];

        if (m_channels[QCAMERA_CH_TYPE_PREVIEW] != NULL) {
            pMetaChannel = m_channels[QCAMERA_CH_TYPE_PREVIEW];
            uint32_t streamNum = pMetaChannel->getNumOfStreams();
            QCameraStream *pStream = NULL;
            for (uint32_t i = 0 ; i < streamNum ; i++ ) {
                pStream = pMetaChannel->getStreamByIndex(i);
                if ((NULL != pStream) &&
                        (CAM_STREAM_TYPE_METADATA == pStream->getMyType())) {
                    pMetaStream = pStream;
                    break;
                }
            }
        }

        if ((NULL != pMetaChannel) && (NULL != pMetaStream)) {
            rc = pVideoChannel->linkStream(pMetaChannel, pMetaStream);
            if (NO_ERROR != rc) {
                LOGW("Metadata stream link failed %d", rc);
            }
        }
    }

    if (rc == NO_ERROR) {
        rc = startChannel(QCAMERA_CH_TYPE_VIDEO);
    }

    if (mParameters.isTNRSnapshotEnabled() && !isLowPowerMode()) {
        QCameraChannel *pChannel = m_channels[QCAMERA_CH_TYPE_SNAPSHOT];
        if (!mParameters.is4k2kVideoResolution()) {
            // Find and try to link a metadata stream from preview channel
            QCameraChannel *pMetaChannel = NULL;
            QCameraStream *pMetaStream = NULL;

            if (m_channels[QCAMERA_CH_TYPE_PREVIEW] != NULL) {
                pMetaChannel = m_channels[QCAMERA_CH_TYPE_PREVIEW];
                uint32_t streamNum = pMetaChannel->getNumOfStreams();
                QCameraStream *pStream = NULL;
                for (uint32_t i = 0 ; i < streamNum ; i++ ) {
                    pStream = pMetaChannel->getStreamByIndex(i);
                    if ((NULL != pStream) &&
                            (CAM_STREAM_TYPE_METADATA ==
                            pStream->getMyType())) {
                        pMetaStream = pStream;
                        break;
                    }
                }
            }

            if ((NULL != pMetaChannel) && (NULL != pMetaStream)) {
                rc = pChannel->linkStream(pMetaChannel, pMetaStream);
                if (NO_ERROR != rc) {
                    LOGW("Metadata stream link failed %d", rc);
                }
            }
        }
        LOGH("START snapshot Channel for TNR processing");
        rc = pChannel->start();
    }

    if (rc == NO_ERROR) {
        // Set power Hint for video encoding
        m_perfLock.powerHint(POWER_HINT_VIDEO_ENCODE, true);
    }

    LOGI("X rc = %d", rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : stopRecording
 *
 * DESCRIPTION: stop recording impl
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::stopRecording()
{
    LOGI("E");
    // stop snapshot channel
    if (mParameters.isTNRSnapshotEnabled()) {
        LOGH("STOP snapshot Channel for TNR processing");
        stopChannel(QCAMERA_CH_TYPE_SNAPSHOT);
    }
    int rc = stopChannel(QCAMERA_CH_TYPE_VIDEO);

    m_cbNotifier.flushVideoNotifications();
    // Disable power hint for video encoding
    m_perfLock.powerHint(POWER_HINT_VIDEO_ENCODE, false);
    LOGI("X rc = %d", rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : releaseRecordingFrame
 *
 * DESCRIPTION: return video frame impl
 *
 * PARAMETERS :
 *   @opaque  : ptr to video frame to be returned
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::releaseRecordingFrame(const void * opaque)
{
    int32_t rc = UNKNOWN_ERROR;
    QCameraVideoChannel *pChannel =
            (QCameraVideoChannel *)m_channels[QCAMERA_CH_TYPE_VIDEO];
    LOGD("opaque data = %p",opaque);

    if(pChannel != NULL) {
        rc = pChannel->releaseFrame(opaque, mStoreMetaDataInFrame > 0);
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : autoFocus
 *
 * DESCRIPTION: start auto focus impl
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::autoFocus()
{
    int rc = NO_ERROR;
    cam_focus_mode_type focusMode = mParameters.getFocusMode();
    LOGH("E");

    switch (focusMode) {
    case CAM_FOCUS_MODE_AUTO:
    case CAM_FOCUS_MODE_MACRO:
    case CAM_FOCUS_MODE_CONTINOUS_VIDEO:
    case CAM_FOCUS_MODE_CONTINOUS_PICTURE:
        mActiveAF = true;
        LOGI("Send AUTO FOCUS event. focusMode=%d, m_currentFocusState=%d",
                focusMode, m_currentFocusState);
        rc = mCameraHandle->ops->do_auto_focus(mCameraHandle->camera_handle);
        break;
    case CAM_FOCUS_MODE_INFINITY:
    case CAM_FOCUS_MODE_FIXED:
    case CAM_FOCUS_MODE_EDOF:
    default:
        LOGI("No ops in focusMode (%d)", focusMode);
        rc = sendEvtNotify(CAMERA_MSG_FOCUS, true, 0);
        break;
    }

    if (NO_ERROR != rc) {
        mActiveAF = false;
    }
    LOGH("X rc = %d", rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : cancelAutoFocus
 *
 * DESCRIPTION: cancel auto focus impl
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::cancelAutoFocus()
{
    int rc = NO_ERROR;
    cam_focus_mode_type focusMode = mParameters.getFocusMode();

    switch (focusMode) {
    case CAM_FOCUS_MODE_AUTO:
    case CAM_FOCUS_MODE_MACRO:
    case CAM_FOCUS_MODE_CONTINOUS_VIDEO:
    case CAM_FOCUS_MODE_CONTINOUS_PICTURE:
        mActiveAF = false;
        rc = mCameraHandle->ops->cancel_auto_focus(mCameraHandle->camera_handle);
        break;
    case CAM_FOCUS_MODE_INFINITY:
    case CAM_FOCUS_MODE_FIXED:
    case CAM_FOCUS_MODE_EDOF:
    default:
        LOGD("No ops in focusMode (%d)", focusMode);
        break;
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : processUFDumps
 *
 * DESCRIPTION: process UF jpeg dumps for refocus support
 *
 * PARAMETERS :
 *   @evt     : payload of jpeg event, including information about jpeg encoding
 *              status, jpeg size and so on.
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *
 * NOTE       : none
 *==========================================================================*/
bool QCamera2HardwareInterface::processUFDumps(qcamera_jpeg_evt_payload_t *evt)
{
   bool ret = true;
   if (mParameters.isUbiRefocus()) {
       int index = (int)getOutputImageCount();
       bool allFocusImage = (index == ((int)mParameters.getRefocusOutputCount() - 1));
       char name[FILENAME_MAX];

       camera_memory_t *jpeg_mem = NULL;
       omx_jpeg_ouput_buf_t *jpeg_out = NULL;
       size_t dataLen;
       uint8_t *dataPtr;
       if (NO_ERROR != waitDeferredWork(mInitPProcJob)) {
           LOGE("Init PProc Deferred work failed");
           return false;
       }
       if (!m_postprocessor.getJpegMemOpt()) {
           dataLen = evt->out_data.buf_filled_len;
           dataPtr = evt->out_data.buf_vaddr;
       } else {
           jpeg_out  = (omx_jpeg_ouput_buf_t*) evt->out_data.buf_vaddr;
           if (!jpeg_out) {
              LOGE("Null pointer detected");
              return false;
           }
           jpeg_mem = (camera_memory_t *)jpeg_out->mem_hdl;
           if (!jpeg_mem) {
              LOGE("Null pointer detected");
              return false;
           }
           dataPtr = (uint8_t *)jpeg_mem->data;
           dataLen = jpeg_mem->size;
       }

       if (allFocusImage)  {
           snprintf(name, sizeof(name), "AllFocusImage");
           index = -1;
       } else {
           snprintf(name, sizeof(name), "%d", 0);
       }
       CAM_DUMP_TO_FILE(QCAMERA_DUMP_FRM_LOCATION"ubifocus", name, index, "jpg",
           dataPtr, dataLen);
       LOGD("Dump the image %d %d allFocusImage %d",
           getOutputImageCount(), index, allFocusImage);
       setOutputImageCount(getOutputImageCount() + 1);
       if (!allFocusImage) {
           ret = false;
       }
   }
   return ret;
}

/*===========================================================================
 * FUNCTION   : unconfigureAdvancedCapture
 *
 * DESCRIPTION: unconfigure Advanced Capture.
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::unconfigureAdvancedCapture()
{
    int32_t rc = NO_ERROR;

    /*Disable Quadra CFA mode*/
    LOGH("Disabling Quadra CFA mode");
    mParameters.setQuadraCfaMode(false, true);

    if (mAdvancedCaptureConfigured) {

        mAdvancedCaptureConfigured = false;

        if(mIs3ALocked) {
            mParameters.set3ALock(false);
            mIs3ALocked = false;
        }
        if (mParameters.isHDREnabled() || mParameters.isAEBracketEnabled()) {
            rc = mParameters.setToneMapMode(true, true);
            if (rc != NO_ERROR) {
                LOGW("Failed to enable tone map during HDR/AEBracketing");
            }
            mHDRBracketingEnabled = false;
            rc = mParameters.stopAEBracket();
        } else if ((mParameters.isChromaFlashEnabled())
                || (mFlashConfigured && !mLongshotEnabled)
                || (mLowLightConfigured == true)
                || (mParameters.getManualCaptureMode() >= CAM_MANUAL_CAPTURE_TYPE_2)) {
            rc = mParameters.resetFrameCapture(TRUE, mLowLightConfigured);
        } else if (mParameters.isUbiFocusEnabled() || mParameters.isUbiRefocus()) {
            rc = configureAFBracketing(false);
        } else if (mParameters.isOptiZoomEnabled()) {
            rc = mParameters.setAndCommitZoom(mZoomLevel);
            setDisplaySkip(FALSE, CAMERA_MAX_PARAM_APPLY_DELAY);
        } else if (mParameters.isStillMoreEnabled()) {
            cam_still_more_t stillmore_config = mParameters.getStillMoreSettings();
            stillmore_config.burst_count = 0;
            mParameters.setStillMoreSettings(stillmore_config);

            /* If SeeMore is running, it will handle re-enabling tone map */
            if (!mParameters.isSeeMoreEnabled() && !mParameters.isLTMForSeeMoreEnabled()) {
                rc = mParameters.setToneMapMode(true, true);
                if (rc != NO_ERROR) {
                    LOGW("Failed to enable tone map during StillMore");
                }
            }

            /* Re-enable Tintless */
            mParameters.setTintless(true);
        } else {
            LOGW("No Advanced Capture feature enabled!!");
            rc = BAD_VALUE;
        }
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : configureAdvancedCapture
 *
 * DESCRIPTION: configure Advanced Capture.
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::configureAdvancedCapture()
{
    LOGH("E");
    int32_t rc = NO_ERROR;

    rc = mParameters.checkFeatureConcurrency();
    if (rc != NO_ERROR) {
        LOGE("Cannot support Advanced capture modes");
        return rc;
    }
    /*Enable Quadra CFA mode*/
    LOGH("Enabling Quadra CFA mode");
    mParameters.setQuadraCfaMode(true, true);

    setOutputImageCount(0);
    mInputCount = 0;
    mAdvancedCaptureConfigured = true;
    /* Display should be disabled for advanced modes */
    bool bSkipDisplay = true;

    if (getRelatedCamSyncInfo()->mode == CAM_MODE_SECONDARY) {
        // no Advance capture settings for Aux camera
        LOGH("X Secondary Camera, no need to process!! ");
        return rc;
    }

    /* Do not stop display if in stillmore livesnapshot */
    if (mParameters.isStillMoreEnabled() &&
            mParameters.isSeeMoreEnabled()) {
        bSkipDisplay = false;
    }
    if (mParameters.isUbiFocusEnabled() || mParameters.isUbiRefocus()) {
        rc = configureAFBracketing();
    } else if (mParameters.isOptiZoomEnabled()) {
        rc = configureOptiZoom();
    } else if(mParameters.isHDREnabled()) {
        rc = configureHDRBracketing();
        if (mHDRBracketingEnabled) {
            rc = mParameters.setToneMapMode(false, true);
            if (rc != NO_ERROR) {
                LOGW("Failed to disable tone map during HDR");
            }
        }
    } else if (mParameters.isAEBracketEnabled()) {
        rc = mParameters.setToneMapMode(false, true);
        if (rc != NO_ERROR) {
            LOGW("Failed to disable tone map during AEBracketing");
        }
        rc = configureAEBracketing();
    } else if (mParameters.isStillMoreEnabled()) {
        bSkipDisplay = false;
        rc = configureStillMore();
    } else if ((mParameters.isChromaFlashEnabled())
            || (mParameters.getLowLightLevel() != CAM_LOW_LIGHT_OFF)
            || (mParameters.getManualCaptureMode() >= CAM_MANUAL_CAPTURE_TYPE_2)) {
        rc = mParameters.configFrameCapture(TRUE);
        if (mParameters.getLowLightLevel() != CAM_LOW_LIGHT_OFF) {
            mLowLightConfigured = true;
        }
    } else if (mFlashNeeded && !mLongshotEnabled) {
        rc = mParameters.configFrameCapture(TRUE);
        mFlashConfigured = true;
        bSkipDisplay = false;
    } else {
        LOGH("Advanced Capture feature not enabled!! ");
        mAdvancedCaptureConfigured = false;
        bSkipDisplay = false;
    }

    LOGH("Stop preview temporarily for advanced captures");
    setDisplaySkip(bSkipDisplay);

    LOGH("X rc = %d", rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : configureAFBracketing
 *
 * DESCRIPTION: configure AF Bracketing.
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::configureAFBracketing(bool enable)
{
    LOGH("E");
    int32_t rc = NO_ERROR;
    cam_af_bracketing_t *af_bracketing_need;

    if (mParameters.isUbiRefocus()) {
        af_bracketing_need =
                &gCamCapability[mCameraId]->refocus_af_bracketing_need;
    } else {
        af_bracketing_need =
                &gCamCapability[mCameraId]->ubifocus_af_bracketing_need;
    }

    //Enable AF Bracketing.
    cam_af_bracketing_t afBracket;
    memset(&afBracket, 0, sizeof(cam_af_bracketing_t));
    afBracket.enable = enable;
    afBracket.burst_count = af_bracketing_need->burst_count;

    for(int8_t i = 0; i < MAX_AF_BRACKETING_VALUES; i++) {
        afBracket.focus_steps[i] = af_bracketing_need->focus_steps[i];
        LOGH("focus_step[%d] = %d", i, afBracket.focus_steps[i]);
    }
    //Send cmd to backend to set AF Bracketing for Ubi Focus.
    rc = mParameters.commitAFBracket(afBracket);
    if ( NO_ERROR != rc ) {
        LOGE("cannot configure AF bracketing");
        return rc;
    }
    if (enable) {
        mParameters.set3ALock(true);
        mIs3ALocked = true;
    }
    LOGH("X rc = %d", rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : configureHDRBracketing
 *
 * DESCRIPTION: configure HDR Bracketing.
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::configureHDRBracketing()
{
    LOGH("E");
    int32_t rc = NO_ERROR;

    cam_hdr_bracketing_info_t& hdrBracketingSetting =
            gCamCapability[mCameraId]->hdr_bracketing_setting;

    // 'values' should be in "idx1,idx2,idx3,..." format
    uint32_t hdrFrameCount =
            hdrBracketingSetting.num_frames;
    LOGH("HDR values %d, %d frame count: %u",
          (int8_t) hdrBracketingSetting.exp_val.values[0],
          (int8_t) hdrBracketingSetting.exp_val.values[1],
          hdrFrameCount);

    // Enable AE Bracketing for HDR
    cam_exp_bracketing_t aeBracket;
    memset(&aeBracket, 0, sizeof(cam_exp_bracketing_t));
    aeBracket.mode =
        hdrBracketingSetting.exp_val.mode;

    if (aeBracket.mode == CAM_EXP_BRACKETING_ON) {
        mHDRBracketingEnabled = true;
    }

    String8 tmp;
    for (uint32_t i = 0; i < hdrFrameCount; i++) {
        tmp.appendFormat("%d",
            (int8_t) hdrBracketingSetting.exp_val.values[i]);
        tmp.append(",");
    }
    if (mParameters.isHDR1xFrameEnabled()
        && mParameters.isHDR1xExtraBufferNeeded()) {
            tmp.appendFormat("%d", 0);
            tmp.append(",");
    }

    if( !tmp.isEmpty() &&
        ( MAX_EXP_BRACKETING_LENGTH > tmp.length() ) ) {
        //Trim last comma
        memset(aeBracket.values, '\0', MAX_EXP_BRACKETING_LENGTH);
        memcpy(aeBracket.values, tmp.string(), tmp.length() - 1);
    }

    LOGH("HDR config values %s",
          aeBracket.values);
    rc = mParameters.setHDRAEBracket(aeBracket);
    if ( NO_ERROR != rc ) {
        LOGE("cannot configure HDR bracketing");
        return rc;
    }
    LOGH("X rc = %d", rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : configureAEBracketing
 *
 * DESCRIPTION: configure AE Bracketing.
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::configureAEBracketing()
{
    LOGH("E");
    int32_t rc = NO_ERROR;

    rc = mParameters.setAEBracketing();
    if ( NO_ERROR != rc ) {
        LOGE("cannot configure AE bracketing");
        return rc;
    }
    LOGH("X rc = %d", rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : configureOptiZoom
 *
 * DESCRIPTION: configure Opti Zoom.
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::configureOptiZoom()
{
    int32_t rc = NO_ERROR;

    //store current zoom level.
    mZoomLevel = mParameters.getParmZoomLevel();

    //set zoom level to 1x;
    mParameters.setAndCommitZoom(0);

    mParameters.set3ALock(true);
    mIs3ALocked = true;

    return rc;
}

/*===========================================================================
 * FUNCTION   : configureStillMore
 *
 * DESCRIPTION: configure StillMore.
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::configureStillMore()
{
    int32_t rc = NO_ERROR;
    uint8_t burst_cnt = 0;
    cam_still_more_t stillmore_config;
    cam_still_more_t stillmore_cap;

    /* Disable Tone Map. If seemore is enabled, it will handle disabling it. */
    if (!mParameters.isSeeMoreEnabled() && !mParameters.isLTMForSeeMoreEnabled()) {
        rc = mParameters.setToneMapMode(false, true);
        if (rc != NO_ERROR) {
            LOGW("Failed to disable tone map during StillMore");
        }
    }

    /* Lock 3A */
    mParameters.set3ALock(true);
    mIs3ALocked = true;

    /* Disable Tintless */
    mParameters.setTintless(false);

    /* Initialize burst count from capability */
    stillmore_cap = mParameters.getStillMoreCapability();
    burst_cnt = stillmore_cap.max_burst_count;

    /* Reconfigure burst count from dynamic scene data */
    cam_dyn_img_data_t dynamic_img_data = mParameters.getDynamicImgData();
    if (dynamic_img_data.input_count >= stillmore_cap.min_burst_count &&
            dynamic_img_data.input_count <= stillmore_cap.max_burst_count) {
        burst_cnt = dynamic_img_data.input_count;
    }

    /* Reconfigure burst count in the case of liveshot */
    if (mParameters.isSeeMoreEnabled()) {
        burst_cnt = 1;
    }

    /* Reconfigure burst count from user input */
    char prop[PROPERTY_VALUE_MAX];
    property_get("persist.camera.imglib.stillmore", prop, "0");
    uint8_t burst_setprop = (uint32_t)atoi(prop);
    if (burst_setprop != 0)  {
       if ((burst_setprop < stillmore_cap.min_burst_count) ||
               (burst_setprop > stillmore_cap.max_burst_count)) {
           burst_cnt = stillmore_cap.max_burst_count;
       } else {
           burst_cnt = burst_setprop;
       }
    }

    memset(&stillmore_config, 0, sizeof(cam_still_more_t));
    stillmore_config.burst_count = burst_cnt;
    mParameters.setStillMoreSettings(stillmore_config);

    LOGH("Stillmore burst %d", burst_cnt);

    return rc;
}

/*===========================================================================
 * FUNCTION   : stopAdvancedCapture
 *
 * DESCRIPTION: stops advanced capture based on capture type
 *
 * PARAMETERS :
 *   @pChannel : channel.
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::stopAdvancedCapture(
        QCameraPicChannel *pChannel)
{
    LOGH("stop bracketig");
    int32_t rc = NO_ERROR;

    if(mParameters.isUbiFocusEnabled() || mParameters.isUbiRefocus()) {
        rc = pChannel->stopAdvancedCapture(MM_CAMERA_AF_BRACKETING);
    } else if (mParameters.isChromaFlashEnabled()
            || (mFlashConfigured && !mLongshotEnabled)
            || (mLowLightConfigured == true)
            || (mParameters.getManualCaptureMode() >= CAM_MANUAL_CAPTURE_TYPE_2)) {
        rc = pChannel->stopAdvancedCapture(MM_CAMERA_FRAME_CAPTURE);
        mFlashConfigured = false;
        mLowLightConfigured = false;
    } else if(mParameters.isHDREnabled()
            || mParameters.isAEBracketEnabled()) {
        rc = pChannel->stopAdvancedCapture(MM_CAMERA_AE_BRACKETING);
    } else if (mParameters.isOptiZoomEnabled()) {
        rc = pChannel->stopAdvancedCapture(MM_CAMERA_ZOOM_1X);
    } else if (mParameters.isStillMoreEnabled()) {
        LOGH("stopAdvancedCapture not needed for StillMore");
    } else {
        LOGH("No Advanced Capture feature enabled!");
        rc = BAD_VALUE;
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : startAdvancedCapture
 *
 * DESCRIPTION: starts advanced capture based on capture type
 *
 * PARAMETERS :
 *   @pChannel : channel.
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::startAdvancedCapture(
        QCameraPicChannel *pChannel)
{
    LOGH("Start bracketing");
    int32_t rc = NO_ERROR;

    if(mParameters.isUbiFocusEnabled() || mParameters.isUbiRefocus()) {
        rc = pChannel->startAdvancedCapture(MM_CAMERA_AF_BRACKETING);
    } else if (mParameters.isOptiZoomEnabled()) {
        rc = pChannel->startAdvancedCapture(MM_CAMERA_ZOOM_1X);
    } else if (mParameters.isStillMoreEnabled()) {
        LOGH("startAdvancedCapture not needed for StillMore");
    } else if (mParameters.isHDREnabled()
            || mParameters.isAEBracketEnabled()) {
        rc = pChannel->startAdvancedCapture(MM_CAMERA_AE_BRACKETING);
    } else if (mParameters.isChromaFlashEnabled()
            || (mFlashNeeded && !mLongshotEnabled)
            || (mLowLightConfigured == true)
            || (mParameters.getManualCaptureMode() >= CAM_MANUAL_CAPTURE_TYPE_2)) {
        cam_capture_frame_config_t config = mParameters.getCaptureFrameConfig();
        rc = pChannel->startAdvancedCapture(MM_CAMERA_FRAME_CAPTURE, &config);
    } else {
        LOGE("No Advanced Capture feature enabled!");
        rc = BAD_VALUE;
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : preTakePicture
 *
 * DESCRIPTION: Prepare take picture impl, Restarts preview if necessary
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::preTakePicture()
{
    int32_t rc = NO_ERROR;
    LOGH("E");
    if (mParameters.getRecordingHintValue() == true) {

        // Give HWI control to restart preview only in single camera mode.
        // In dual-cam mode, this control belongs to muxer.
        if (getRelatedCamSyncInfo()->sync_control != CAM_SYNC_RELATED_SENSORS_ON) {
            LOGH("restart preview if rec hint is true and preview is running");
            stopPreview();
            mParameters.updateRecordingHintValue(FALSE);
            // start preview again
            rc = preparePreview();
            if (rc == NO_ERROR) {
                rc = startPreview();
                if (rc != NO_ERROR) {
                    unpreparePreview();
                }
            }
        }
        else
        {
            // For dual cam mode, update the flag mPreviewRestartNeeded to true
            // Restart control will be handled by muxer.
            mPreviewRestartNeeded = true;
        }
    }

    LOGH("X rc = %d", rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : takePicture
 *
 * DESCRIPTION: take picture impl
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::takePicture()
{
    int rc = NO_ERROR;

    // Get total number for snapshots (retro + regular)
    uint8_t numSnapshots = mParameters.getNumOfSnapshots();
    // Get number of retro-active snapshots
    uint8_t numRetroSnapshots = mParameters.getNumOfRetroSnapshots();
    LOGH("E");

    //Set rotation value from user settings as Jpeg rotation
    //to configure back-end modules.
    mParameters.setJpegRotation(mParameters.getRotation());

    // Check if retro-active snapshots are not enabled
    if (!isRetroPicture() || !mParameters.isZSLMode()) {
      numRetroSnapshots = 0;
      LOGH("Reset retro snaphot count to zero");
    }

    //Do special configure for advanced capture modes.
    rc = configureAdvancedCapture();
    if (rc != NO_ERROR) {
        LOGE("Unsupported capture call");
        return rc;
    }

    if (mAdvancedCaptureConfigured) {
        numSnapshots = mParameters.getBurstCountForAdvancedCapture();
    }
    LOGI("snap count = %d zsl = %d advanced = %d",
            numSnapshots, mParameters.isZSLMode(), mAdvancedCaptureConfigured);

    if (mParameters.isZSLMode()) {
        QCameraChannel *pChannel = m_channels[QCAMERA_CH_TYPE_ZSL];
        QCameraPicChannel *pPicChannel = (QCameraPicChannel *)pChannel;
        if (NULL != pPicChannel) {

            if (mParameters.getofflineRAW()) {
                startRAWChannel(pPicChannel);
                pPicChannel = (QCameraPicChannel *)m_channels[QCAMERA_CH_TYPE_RAW];
                if (pPicChannel == NULL) {
                    LOGE("RAW Channel is NULL in Manual capture mode");
                    stopRAWChannel();
                    return UNKNOWN_ERROR;
                }
            }

            rc = configureOnlineRotation(*pPicChannel);
            if (rc != NO_ERROR) {
                LOGE("online rotation failed");
                return rc;
            }

            // start postprocessor
            DeferWorkArgs args;
            memset(&args, 0, sizeof(DeferWorkArgs));

            args.pprocArgs = pPicChannel;

            // No need to wait for mInitPProcJob here, because it was
            // queued in startPreview, and will definitely be processed before
            // mReprocJob can begin.
            mReprocJob = queueDeferredWork(CMD_DEF_PPROC_START,
                    args);
            if (mReprocJob == 0) {
                LOGE("Failure: Unable to start pproc");
                return -ENOMEM;
            }

            // Check if all preview buffers are mapped before creating
            // a jpeg session as preview stream buffers are queried during the same
            uint8_t numStreams = pChannel->getNumOfStreams();
            QCameraStream *pStream = NULL;
            QCameraStream *pPreviewStream = NULL;
            for (uint8_t i = 0 ; i < numStreams ; i++ ) {
                pStream = pChannel->getStreamByIndex(i);
                if (!pStream)
                    continue;
                if (CAM_STREAM_TYPE_PREVIEW == pStream->getMyType()) {
                    pPreviewStream = pStream;
                    break;
                }
            }
            if (pPreviewStream != NULL) {
                Mutex::Autolock l(mMapLock);
                QCameraMemory *pMemory = pStream->getStreamBufs();
                if (!pMemory) {
                    LOGE("Error!! pMemory is NULL");
                    return -ENOMEM;
                }

                uint8_t waitCnt = 2;
                while (!pMemory->checkIfAllBuffersMapped() && (waitCnt > 0)) {
                    LOGL(" Waiting for preview buffers to be mapped");
                    mMapCond.waitRelative(
                            mMapLock, CAMERA_DEFERRED_MAP_BUF_TIMEOUT);
                    LOGL("Wait completed!!");
                    waitCnt--;
                }
                // If all buffers are not mapped after retries, assert
                assert(pMemory->checkIfAllBuffersMapped());
            } else {
                assert(pPreviewStream);
            }

            // Create JPEG session
            mJpegJob = queueDeferredWork(CMD_DEF_CREATE_JPEG_SESSION,
                    args);
            if (mJpegJob == 0) {
                LOGE("Failed to queue CREATE_JPEG_SESSION");
                if (NO_ERROR != waitDeferredWork(mReprocJob)) {
                        LOGE("Reprocess Deferred work was failed");
                }
                m_postprocessor.stop();
                return -ENOMEM;
            }

            if (mAdvancedCaptureConfigured) {
                rc = startAdvancedCapture(pPicChannel);
                if (rc != NO_ERROR) {
                    LOGE("cannot start zsl advanced capture");
                    return rc;
                }
            }
            if (mLongshotEnabled && mPrepSnapRun) {
                mCameraHandle->ops->start_zsl_snapshot(
                        mCameraHandle->camera_handle,
                        pPicChannel->getMyHandle());
            }
            // If frame sync is ON and it is a SECONDARY camera,
            // we do not need to send the take picture command to interface
            // It will be handled along with PRIMARY camera takePicture request
            mm_camera_req_buf_t buf;
            memset(&buf, 0x0, sizeof(buf));
            if ((!mParameters.isAdvCamFeaturesEnabled() &&
                    !mFlashNeeded &&
                    !isLongshotEnabled() &&
                    isFrameSyncEnabled()) &&
                    (getRelatedCamSyncInfo()->sync_control ==
                    CAM_SYNC_RELATED_SENSORS_ON)) {
                if (getRelatedCamSyncInfo()->mode == CAM_MODE_PRIMARY) {
                    buf.type = MM_CAMERA_REQ_FRAME_SYNC_BUF;
                    buf.num_buf_requested = numSnapshots;
                    rc = pPicChannel->takePicture(&buf);
                    if (rc != NO_ERROR) {
                        LOGE("FS_DBG cannot take ZSL picture, stop pproc");
                        if (NO_ERROR != waitDeferredWork(mReprocJob)) {
                            LOGE("Reprocess Deferred work failed");
                            return UNKNOWN_ERROR;
                        }
                        if (NO_ERROR != waitDeferredWork(mJpegJob)) {
                            LOGE("Jpeg Deferred work failed");
                            return UNKNOWN_ERROR;
                        }
                        m_postprocessor.stop();
                        return rc;
                    }
                    LOGI("PRIMARY camera: send frame sync takePicture!!");
                }
            } else {
                buf.type = MM_CAMERA_REQ_SUPER_BUF;
                buf.num_buf_requested = numSnapshots;
                buf.num_retro_buf_requested = numRetroSnapshots;
                rc = pPicChannel->takePicture(&buf);
                if (rc != NO_ERROR) {
                    LOGE("cannot take ZSL picture, stop pproc");
                        if (NO_ERROR != waitDeferredWork(mReprocJob)) {
                            LOGE("Reprocess Deferred work failed");
                            return UNKNOWN_ERROR;
                        }
                        if (NO_ERROR != waitDeferredWork(mJpegJob)) {
                            LOGE("Jpeg Deferred work failed");
                            return UNKNOWN_ERROR;
                        }
                    m_postprocessor.stop();
                    return rc;
                }
            }
        } else {
            LOGE("ZSL channel is NULL");
            return UNKNOWN_ERROR;
        }
    } else {

        // start snapshot
        if (mParameters.isJpegPictureFormat() ||
                mParameters.isNV16PictureFormat() ||
                mParameters.isNV21PictureFormat()) {

            //STOP Preview for Non ZSL use case
            stopPreview();

            //Config CAPTURE channels
            rc = declareSnapshotStreams();
            if (NO_ERROR != rc) {
                return rc;
            }

            rc = addCaptureChannel();
            if ((rc == NO_ERROR) &&
                    (NULL != m_channels[QCAMERA_CH_TYPE_CAPTURE])) {

                if (!mParameters.getofflineRAW()) {
                    rc = configureOnlineRotation(
                        *m_channels[QCAMERA_CH_TYPE_CAPTURE]);
                    if (rc != NO_ERROR) {
                        LOGE("online rotation failed");
                        delChannel(QCAMERA_CH_TYPE_CAPTURE);
                        return rc;
                    }
                }

                DeferWorkArgs args;
                memset(&args, 0, sizeof(DeferWorkArgs));

                args.pprocArgs = m_channels[QCAMERA_CH_TYPE_CAPTURE];

                // No need to wait for mInitPProcJob here, because it was
                // queued in startPreview, and will definitely be processed before
                // mReprocJob can begin.
                mReprocJob = queueDeferredWork(CMD_DEF_PPROC_START,
                        args);
                if (mReprocJob == 0) {
                    LOGE("Failure: Unable to start pproc");
                    return -ENOMEM;
                }

                // Create JPEG session
                mJpegJob = queueDeferredWork(CMD_DEF_CREATE_JPEG_SESSION,
                        args);
                if (mJpegJob == 0) {
                    LOGE("Failed to queue CREATE_JPEG_SESSION");
                    if (NO_ERROR != waitDeferredWork(mReprocJob)) {
                        LOGE("Reprocess Deferred work was failed");
                    }
                    m_postprocessor.stop();
                    return -ENOMEM;
                }

                // start catpure channel
                rc =  m_channels[QCAMERA_CH_TYPE_CAPTURE]->start();
                if (rc != NO_ERROR) {
                    LOGE("cannot start capture channel");
                    if (NO_ERROR != waitDeferredWork(mReprocJob)) {
                        LOGE("Reprocess Deferred work failed");
                        return UNKNOWN_ERROR;
                    }
                    if (NO_ERROR != waitDeferredWork(mJpegJob)) {
                        LOGE("Jpeg Deferred work failed");
                        return UNKNOWN_ERROR;
                    }
                    delChannel(QCAMERA_CH_TYPE_CAPTURE);
                    return rc;
                }

                QCameraPicChannel *pCapChannel =
                    (QCameraPicChannel *)m_channels[QCAMERA_CH_TYPE_CAPTURE];
                if (NULL != pCapChannel) {
                    if (mParameters.isUbiFocusEnabled() ||
                            mParameters.isUbiRefocus() ||
                            mParameters.isChromaFlashEnabled()) {
                        rc = startAdvancedCapture(pCapChannel);
                        if (rc != NO_ERROR) {
                            LOGE("cannot start advanced capture");
                            return rc;
                        }
                    }
                }
                if ( mLongshotEnabled ) {
                    rc = longShot();
                    if (NO_ERROR != rc) {
                        if (NO_ERROR != waitDeferredWork(mReprocJob)) {
                            LOGE("Reprocess Deferred work failed");
                            return UNKNOWN_ERROR;
                        }
                        if (NO_ERROR != waitDeferredWork(mJpegJob)) {
                            LOGE("Jpeg Deferred work failed");
                            return UNKNOWN_ERROR;
                        }
                        delChannel(QCAMERA_CH_TYPE_CAPTURE);
                        return rc;
                    }
                }
            } else {
                LOGE("cannot add capture channel");
                delChannel(QCAMERA_CH_TYPE_CAPTURE);
                return rc;
            }
        } else {
            // Stop Preview before taking NZSL snapshot
            stopPreview();

            rc = mParameters.updateRAW(gCamCapability[mCameraId]->raw_dim[0]);
            if (NO_ERROR != rc) {
                LOGE("Raw dimension update failed %d", rc);
                return rc;
            }

            rc = declareSnapshotStreams();
            if (NO_ERROR != rc) {
                LOGE("RAW stream info configuration failed %d", rc);
                return rc;
            }

            rc = addChannel(QCAMERA_CH_TYPE_RAW);
            if (rc == NO_ERROR) {
                // start postprocessor
                if (NO_ERROR != waitDeferredWork(mInitPProcJob)) {
                    LOGE("Reprocess Deferred work failed");
                    return UNKNOWN_ERROR;
                }

                rc = m_postprocessor.start(m_channels[QCAMERA_CH_TYPE_RAW]);
                if (rc != NO_ERROR) {
                    LOGE("cannot start postprocessor");
                    delChannel(QCAMERA_CH_TYPE_RAW);
                    return rc;
                }

                rc = startChannel(QCAMERA_CH_TYPE_RAW);
                if (rc != NO_ERROR) {
                    LOGE("cannot start raw channel");
                    m_postprocessor.stop();
                    delChannel(QCAMERA_CH_TYPE_RAW);
                    return rc;
                }
            } else {
                LOGE("cannot add raw channel");
                return rc;
            }
        }
    }

    //When take picture, stop sending preview callbacks to APP
    m_stateMachine.setPreviewCallbackNeeded(false);
    LOGI("X rc = %d", rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : configureOnlineRotation
 *
 * DESCRIPTION: Configure backend with expected rotation for snapshot stream
 *
 * PARAMETERS :
 *    @ch     : Channel containing a snapshot stream
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::configureOnlineRotation(QCameraChannel &ch)
{
    int rc = NO_ERROR;
    uint32_t streamId = 0;
    QCameraStream *pStream = NULL;

    for (uint8_t i = 0; i < ch.getNumOfStreams(); i++) {
        QCameraStream *stream = ch.getStreamByIndex(i);
        if ((NULL != stream) &&
                ((CAM_STREAM_TYPE_SNAPSHOT == stream->getMyType())
                || (CAM_STREAM_TYPE_RAW == stream->getMyType()))) {
            pStream = stream;
            break;
        }
    }

    if (NULL == pStream) {
        LOGE("No snapshot stream found!");
        return BAD_VALUE;
    }

    streamId = pStream->getMyServerID();
    // Update online rotation configuration
    rc = mParameters.addOnlineRotation(mParameters.getJpegRotation(), streamId,
            mParameters.getDeviceRotation());
    if (rc != NO_ERROR) {
        LOGE("addOnlineRotation failed %d", rc);
        return rc;
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : declareSnapshotStreams
 *
 * DESCRIPTION: Configure backend with expected snapshot streams
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::declareSnapshotStreams()
{
    int rc = NO_ERROR;

    // Update stream info configuration
    rc = mParameters.setStreamConfigure(true, mLongshotEnabled, false, sessionId);
    if (rc != NO_ERROR) {
        LOGE("setStreamConfigure failed %d", rc);
        return rc;
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : longShot
 *
 * DESCRIPTION: Queue one more ZSL frame
 *              in the longshot pipe.
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::longShot()
{
    int32_t rc = NO_ERROR;
    uint8_t numSnapshots = mParameters.getNumOfSnapshots();
    QCameraPicChannel *pChannel = NULL;

    if (mParameters.isZSLMode()) {
        pChannel = (QCameraPicChannel *)m_channels[QCAMERA_CH_TYPE_ZSL];
    } else {
        pChannel = (QCameraPicChannel *)m_channels[QCAMERA_CH_TYPE_CAPTURE];
    }

    if (NULL != pChannel) {
        mm_camera_req_buf_t buf;
        memset(&buf, 0x0, sizeof(buf));
        buf.type = MM_CAMERA_REQ_SUPER_BUF;
        buf.num_buf_requested = numSnapshots;
        rc = pChannel->takePicture(&buf);
    } else {
        LOGE("Capture channel not initialized!");
        rc = NO_INIT;
        goto end;
    }

end:
    return rc;
}

/*===========================================================================
 * FUNCTION   : stopCaptureChannel
 *
 * DESCRIPTION: Stops capture channel
 *
 * PARAMETERS :
 *   @destroy : Set to true to stop and delete camera channel.
 *              Set to false to only stop capture channel.
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::stopCaptureChannel(bool destroy)
{
    int rc = NO_ERROR;
    if (mParameters.isJpegPictureFormat() ||
        mParameters.isNV16PictureFormat() ||
        mParameters.isNV21PictureFormat()) {
        mParameters.setQuadraCfaMode(false, true);
        rc = stopChannel(QCAMERA_CH_TYPE_CAPTURE);
        if (destroy && (NO_ERROR == rc)) {
            // Destroy camera channel but dont release context
            waitDeferredWork(mJpegJob);
            rc = delChannel(QCAMERA_CH_TYPE_CAPTURE, false);
        }
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : cancelPicture
 *
 * DESCRIPTION: cancel picture impl
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::cancelPicture()
{
    waitDeferredWork(mReprocJob);
    waitDeferredWork(mJpegJob);

    //stop post processor
    m_postprocessor.stop();

    unconfigureAdvancedCapture();
    LOGH("Enable display frames again");
    setDisplaySkip(FALSE);

    if (!mLongshotEnabled) {
        m_perfLock.lock_rel();
    }

    if (mParameters.isZSLMode()) {
        QCameraPicChannel *pPicChannel = NULL;
        if (mParameters.getofflineRAW()) {
            pPicChannel = (QCameraPicChannel *)m_channels[QCAMERA_CH_TYPE_RAW];
        } else {
            pPicChannel = (QCameraPicChannel *)m_channels[QCAMERA_CH_TYPE_ZSL];
        }
        if (NULL != pPicChannel) {
            pPicChannel->cancelPicture();
            stopRAWChannel();
            stopAdvancedCapture(pPicChannel);
        }
    } else {

        // normal capture case
        if (mParameters.isJpegPictureFormat() ||
            mParameters.isNV16PictureFormat() ||
            mParameters.isNV21PictureFormat()) {
            stopChannel(QCAMERA_CH_TYPE_CAPTURE);
            delChannel(QCAMERA_CH_TYPE_CAPTURE);
        } else {
            stopChannel(QCAMERA_CH_TYPE_RAW);
            delChannel(QCAMERA_CH_TYPE_RAW);
        }
    }

    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : captureDone
 *
 * DESCRIPTION: Function called when the capture is completed before encoding
 *
 * PARAMETERS : none
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::captureDone()
{
    qcamera_sm_internal_evt_payload_t *payload =
       (qcamera_sm_internal_evt_payload_t *)
       malloc(sizeof(qcamera_sm_internal_evt_payload_t));
    if (NULL != payload) {
        memset(payload, 0, sizeof(qcamera_sm_internal_evt_payload_t));
        payload->evt_type = QCAMERA_INTERNAL_EVT_ZSL_CAPTURE_DONE;
        int32_t rc = processEvt(QCAMERA_SM_EVT_EVT_INTERNAL, payload);
        if (rc != NO_ERROR) {
            LOGE("processEvt ZSL capture done failed");
            free(payload);
            payload = NULL;
        }
    } else {
        LOGE("No memory for ZSL capture done event");
    }
}

/*===========================================================================
 * FUNCTION   : Live_Snapshot_thread
 *
 * DESCRIPTION: Seperate thread for taking live snapshot during recording
 *
 * PARAMETERS : @data - pointer to QCamera2HardwareInterface class object
 *
 * RETURN     : none
 *==========================================================================*/
void* Live_Snapshot_thread (void* data)
{

    QCamera2HardwareInterface *hw = reinterpret_cast<QCamera2HardwareInterface *>(data);
    if (!hw) {
        LOGE("take_picture_thread: NULL camera device");
        return (void *)BAD_VALUE;
    }
    if (hw->bLiveSnapshot) {
        hw->takeLiveSnapshot_internal();
    } else {
        hw->cancelLiveSnapshot_internal();
    }
    return (void* )NULL;
}

/*===========================================================================
 * FUNCTION   : Int_Pic_thread
 *
 * DESCRIPTION: Seperate thread for taking snapshot triggered by camera backend
 *
 * PARAMETERS : @data - pointer to QCamera2HardwareInterface class object
 *
 * RETURN     : none
 *==========================================================================*/
void* Int_Pic_thread (void* data)
{
    int rc = NO_ERROR;

    QCamera2HardwareInterface *hw = reinterpret_cast<QCamera2HardwareInterface *>(data);

    if (!hw) {
        LOGE("take_picture_thread: NULL camera device");
        return (void *)BAD_VALUE;
    }

    bool JpegMemOpt = false;
    char raw_format[PROPERTY_VALUE_MAX];

    memset(raw_format, 0, sizeof(raw_format));

    rc = hw->takeBackendPic_internal(&JpegMemOpt, &raw_format[0]);
    if (rc == NO_ERROR) {
        hw->checkIntPicPending(JpegMemOpt, &raw_format[0]);
    } else {
        //Snapshot attempt not successful, we need to do cleanup here
        hw->clearIntPendingEvents();
    }

    return (void* )NULL;
}

/*===========================================================================
 * FUNCTION   : takeLiveSnapshot
 *
 * DESCRIPTION: take live snapshot during recording
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::takeLiveSnapshot()
{
    int rc = NO_ERROR;
    if (mLiveSnapshotThread != 0) {
        pthread_join(mLiveSnapshotThread,NULL);
        mLiveSnapshotThread = 0;
    }
    bLiveSnapshot = true;
    rc= pthread_create(&mLiveSnapshotThread, NULL, Live_Snapshot_thread, (void *) this);
    if (!rc) {
        pthread_setname_np(mLiveSnapshotThread, "CAM_liveSnap");
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : takePictureInternal
 *
 * DESCRIPTION: take snapshot triggered by backend
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::takePictureInternal()
{
    int rc = NO_ERROR;
    rc= pthread_create(&mIntPicThread, NULL, Int_Pic_thread, (void *) this);
    if (!rc) {
        pthread_setname_np(mIntPicThread, "CAM_IntPic");
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : checkIntPicPending
 *
 * DESCRIPTION: timed wait for jpeg completion event, and send
 *                        back completion event to backend
 *
 * PARAMETERS : none
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::checkIntPicPending(bool JpegMemOpt, char *raw_format)
{
    bool bSendToBackend = true;
    cam_int_evt_params_t params;
    int rc = NO_ERROR;

    struct timespec   ts;
    struct timeval    tp;
    gettimeofday(&tp, NULL);
    ts.tv_sec  = tp.tv_sec + 5;
    ts.tv_nsec = tp.tv_usec * 1000;

    if (true == m_bIntJpegEvtPending ||
        (true == m_bIntRawEvtPending)) {
        //Waiting in HAL for snapshot taken notification
        pthread_mutex_lock(&m_int_lock);
        rc = pthread_cond_timedwait(&m_int_cond, &m_int_lock, &ts);
        if (ETIMEDOUT == rc || 0x0 == m_BackendFileName[0]) {
            //Hit a timeout, or some spurious activity
            bSendToBackend = false;
        }

        if (true == m_bIntJpegEvtPending) {
            params.event_type = 0;
            mParameters.getStreamFormat(CAM_STREAM_TYPE_SNAPSHOT, params.picture_format);
        } else if (true == m_bIntRawEvtPending) {
            params.event_type = 1;
            mParameters.getStreamFormat(CAM_STREAM_TYPE_RAW, params.picture_format);
        }
        pthread_mutex_unlock(&m_int_lock);

        if (true == m_bIntJpegEvtPending) {
            //Attempting to restart preview after taking JPEG snapshot
            lockAPI();
            rc = processAPI(QCAMERA_SM_EVT_SNAPSHOT_DONE, NULL);
            unlockAPI();
            m_postprocessor.setJpegMemOpt(JpegMemOpt);
        } else if (true == m_bIntRawEvtPending) {
            //Attempting to restart preview after taking RAW snapshot
            stopChannel(QCAMERA_CH_TYPE_RAW);
            delChannel(QCAMERA_CH_TYPE_RAW);
            //restoring the old raw format
            property_set("persist.camera.raw.format", raw_format);
        }

        if (true == bSendToBackend) {
            //send event back to server with the file path
            params.dim = m_postprocessor.m_dst_dim;
            memcpy(&params.path[0], &m_BackendFileName[0], QCAMERA_MAX_FILEPATH_LENGTH);
            memset(&m_BackendFileName[0], 0x0, QCAMERA_MAX_FILEPATH_LENGTH);
            params.size = mBackendFileSize;
            rc = mParameters.setIntEvent(params);
        }

        clearIntPendingEvents();
    }

    return;
}

/*===========================================================================
 * FUNCTION   : takeBackendPic_internal
 *
 * DESCRIPTION: take snapshot triggered by backend
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::takeBackendPic_internal(bool *JpegMemOpt, char *raw_format)
{
    int rc = NO_ERROR;
    qcamera_api_result_t apiResult;

    lockAPI();
    //Set rotation value from user settings as Jpeg rotation
    //to configure back-end modules.
    mParameters.setJpegRotation(mParameters.getRotation());

    setRetroPicture(0);
    /* Prepare snapshot in case LED needs to be flashed */
    if (mFlashNeeded == 1 || mParameters.isChromaFlashEnabled()) {
        // Start Preparing for normal Frames
        LOGH("Start Prepare Snapshot");
        /* Prepare snapshot in case LED needs to be flashed */
        rc = processAPI(QCAMERA_SM_EVT_PREPARE_SNAPSHOT, NULL);
        if (rc == NO_ERROR) {
            waitAPIResult(QCAMERA_SM_EVT_PREPARE_SNAPSHOT, &apiResult);
            rc = apiResult.status;
        }
        LOGH("Prep Snapshot done rc = %d", rc);
        mPrepSnapRun = true;
    }
    unlockAPI();

    if (true == m_bIntJpegEvtPending) {
        //Attempting to take JPEG snapshot
        if (NO_ERROR != waitDeferredWork(mInitPProcJob)) {
            LOGE("Init PProc Deferred work failed");
            return UNKNOWN_ERROR;
        }
        *JpegMemOpt = m_postprocessor.getJpegMemOpt();
        m_postprocessor.setJpegMemOpt(false);

        /* capture */
        lockAPI();
        LOGH("Capturing internal snapshot");
        rc = processAPI(QCAMERA_SM_EVT_TAKE_PICTURE, NULL);
        if (rc == NO_ERROR) {
            waitAPIResult(QCAMERA_SM_EVT_TAKE_PICTURE, &apiResult);
            rc = apiResult.status;
        }
        unlockAPI();
    } else if (true == m_bIntRawEvtPending) {
        //Attempting to take RAW snapshot
        (void)JpegMemOpt;
        stopPreview();

        //getting the existing raw format type
        property_get("persist.camera.raw.format", raw_format, "17");
        //setting it to a default know value for this task
        property_set("persist.camera.raw.format", "18");

        rc = addChannel(QCAMERA_CH_TYPE_RAW);
        if (rc == NO_ERROR) {
            // start postprocessor
            if (NO_ERROR != waitDeferredWork(mInitPProcJob)) {
                LOGE("Init PProc Deferred work failed");
                return UNKNOWN_ERROR;
            }
            rc = m_postprocessor.start(m_channels[QCAMERA_CH_TYPE_RAW]);
            if (rc != NO_ERROR) {
                LOGE("cannot start postprocessor");
                delChannel(QCAMERA_CH_TYPE_RAW);
                return rc;
            }

            rc = startChannel(QCAMERA_CH_TYPE_RAW);
            if (rc != NO_ERROR) {
                LOGE("cannot start raw channel");
                m_postprocessor.stop();
                delChannel(QCAMERA_CH_TYPE_RAW);
                return rc;
            }
        } else {
            LOGE("cannot add raw channel");
            return rc;
        }
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : clearIntPendingEvents
 *
 * DESCRIPTION: clear internal pending events pertaining to backend
 *                        snapshot requests
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
void QCamera2HardwareInterface::clearIntPendingEvents()
{
    int rc = NO_ERROR;

    if (true == m_bIntRawEvtPending) {
        preparePreview();
        startPreview();
    }
    if (true == m_bIntJpegEvtPending) {
        if (false == mParameters.isZSLMode()) {
            lockAPI();
            rc = processAPI(QCAMERA_SM_EVT_START_PREVIEW, NULL);
            unlockAPI();
        }
    }

    pthread_mutex_lock(&m_int_lock);
    if (true == m_bIntJpegEvtPending) {
        m_bIntJpegEvtPending = false;
    } else if (true == m_bIntRawEvtPending) {
        m_bIntRawEvtPending = false;
    }
    pthread_mutex_unlock(&m_int_lock);
    return;
}

/*===========================================================================
 * FUNCTION   : takeLiveSnapshot_internal
 *
 * DESCRIPTION: take live snapshot during recording
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::takeLiveSnapshot_internal()
{
    int rc = NO_ERROR;

    QCameraChannel *pChannel = NULL;
    QCameraChannel *pPreviewChannel = NULL;
    QCameraStream  *pPreviewStream = NULL;
    QCameraStream  *pStream = NULL;

    //Set rotation value from user settings as Jpeg rotation
    //to configure back-end modules.
    mParameters.setJpegRotation(mParameters.getRotation());

    // Configure advanced capture
    rc = configureAdvancedCapture();
    if (rc != NO_ERROR) {
        LOGE("Unsupported capture call");
        goto end;
    }

    if (isLowPowerMode()) {
        pChannel = m_channels[QCAMERA_CH_TYPE_VIDEO];
    } else {
        pChannel = m_channels[QCAMERA_CH_TYPE_SNAPSHOT];
    }

    if (NULL == pChannel) {
        LOGE("Snapshot/Video channel not initialized");
        rc = NO_INIT;
        goto end;
    }

    // Check if all preview buffers are mapped before creating
    // a jpeg session as preview stream buffers are queried during the same
    pPreviewChannel = m_channels[QCAMERA_CH_TYPE_PREVIEW];
    if (pPreviewChannel != NULL) {
        uint32_t numStreams = pPreviewChannel->getNumOfStreams();

        for (uint8_t i = 0 ; i < numStreams ; i++ ) {
            pStream = pPreviewChannel->getStreamByIndex(i);
            if (!pStream)
                continue;
            if (CAM_STREAM_TYPE_PREVIEW == pStream->getMyType()) {
                pPreviewStream = pStream;
                break;
            }
        }

        if (pPreviewStream != NULL) {
            Mutex::Autolock l(mMapLock);
            QCameraMemory *pMemory = pStream->getStreamBufs();
            if (!pMemory) {
                LOGE("Error!! pMemory is NULL");
                return -ENOMEM;
            }

            uint8_t waitCnt = 2;
            while (!pMemory->checkIfAllBuffersMapped() && (waitCnt > 0)) {
                LOGL(" Waiting for preview buffers to be mapped");
                mMapCond.waitRelative(
                        mMapLock, CAMERA_DEFERRED_MAP_BUF_TIMEOUT);
                LOGL("Wait completed!!");
                waitCnt--;
            }
            // If all buffers are not mapped after retries, assert
            assert(pMemory->checkIfAllBuffersMapped());
        } else {
            assert(pPreviewStream);
        }
    }

    DeferWorkArgs args;
    memset(&args, 0, sizeof(DeferWorkArgs));

    args.pprocArgs = pChannel;

    // No need to wait for mInitPProcJob here, because it was
    // queued in startPreview, and will definitely be processed before
    // mReprocJob can begin.
    mReprocJob = queueDeferredWork(CMD_DEF_PPROC_START,
            args);
    if (mReprocJob == 0) {
        LOGE("Failed to queue CMD_DEF_PPROC_START");
        rc = -ENOMEM;
        goto end;
    }

    // Create JPEG session
    mJpegJob = queueDeferredWork(CMD_DEF_CREATE_JPEG_SESSION,
            args);
    if (mJpegJob == 0) {
        LOGE("Failed to queue CREATE_JPEG_SESSION");
        if (NO_ERROR != waitDeferredWork(mReprocJob)) {
            LOGE("Reprocess Deferred work was failed");
        }
        m_postprocessor.stop();
        rc = -ENOMEM;
        goto end;
    }

    if (isLowPowerMode()) {
        mm_camera_req_buf_t buf;
        memset(&buf, 0x0, sizeof(buf));
        buf.type = MM_CAMERA_REQ_SUPER_BUF;
        buf.num_buf_requested = 1;
        rc = ((QCameraVideoChannel*)pChannel)->takePicture(&buf);
        goto end;
    }

    //Disable reprocess for 4K liveshot case
    if (!mParameters.is4k2kVideoResolution()) {
        rc = configureOnlineRotation(*m_channels[QCAMERA_CH_TYPE_SNAPSHOT]);
        if (rc != NO_ERROR) {
            LOGE("online rotation failed");
            if (NO_ERROR != waitDeferredWork(mReprocJob)) {
                LOGE("Reprocess Deferred work was failed");
            }
            if (NO_ERROR != waitDeferredWork(mJpegJob)) {
                LOGE("Jpeg Deferred work was failed");
            }
            m_postprocessor.stop();
            return rc;
        }
    }

    if ((NULL != pChannel) && (mParameters.isTNRSnapshotEnabled())) {
        QCameraStream *pStream = NULL;
        for (uint32_t i = 0 ; i < pChannel->getNumOfStreams(); i++ ) {
            pStream = pChannel->getStreamByIndex(i);
            if ((NULL != pStream) &&
                    (CAM_STREAM_TYPE_SNAPSHOT == pStream->getMyType())) {
                break;
            }
        }
        if (pStream != NULL) {
            LOGD("REQUEST_FRAMES event for TNR snapshot");
            cam_stream_parm_buffer_t param;
            memset(&param, 0, sizeof(cam_stream_parm_buffer_t));
            param.type = CAM_STREAM_PARAM_TYPE_REQUEST_FRAMES;
            param.frameRequest.enableStream = 1;
            rc = pStream->setParameter(param);
            if (rc != NO_ERROR) {
                LOGE("Stream Event REQUEST_FRAMES failed");
            }
            goto end;
        }
    }

    // start snapshot channel
    if ((rc == NO_ERROR) && (NULL != pChannel)) {
        // Do not link metadata stream for 4K2k resolution
        // as CPP processing would be done on snapshot stream and not
        // reprocess stream
        if (!mParameters.is4k2kVideoResolution()) {
            // Find and try to link a metadata stream from preview channel
            QCameraChannel *pMetaChannel = NULL;
            QCameraStream *pMetaStream = NULL;
            QCameraStream *pPreviewStream = NULL;

            if (m_channels[QCAMERA_CH_TYPE_PREVIEW] != NULL) {
                pMetaChannel = m_channels[QCAMERA_CH_TYPE_PREVIEW];
                uint32_t streamNum = pMetaChannel->getNumOfStreams();
                QCameraStream *pStream = NULL;
                for (uint32_t i = 0 ; i < streamNum ; i++ ) {
                    pStream = pMetaChannel->getStreamByIndex(i);
                    if (NULL != pStream) {
                        if (CAM_STREAM_TYPE_METADATA == pStream->getMyType()) {
                            pMetaStream = pStream;
                        } else if ((CAM_STREAM_TYPE_PREVIEW == pStream->getMyType())
                                && (!mParameters.isHfrMode())
                                && (mParameters.isLinkPreviewForLiveShot())) {
                            // Do not link preview stream for
                            // 1)HFR live snapshot,Thumbnail will not be derived from
                            //   preview for HFR live snapshot.
                            // 2)persist.camera.linkpreview is 0
                            pPreviewStream = pStream;
                        }
                    }
                }
            }

            if ((NULL != pMetaChannel) && (NULL != pMetaStream)) {
                rc = pChannel->linkStream(pMetaChannel, pMetaStream);
                if (NO_ERROR != rc) {
                    LOGE("Metadata stream link failed %d", rc);
                }
            }
            if ((NULL != pMetaChannel) && (NULL != pPreviewStream)) {
                rc = pChannel->linkStream(pMetaChannel, pPreviewStream);
                if (NO_ERROR != rc) {
                    LOGE("Preview stream link failed %d", rc);
                }
            }
        }
        rc = pChannel->start();
    }

end:
    if (rc != NO_ERROR) {
        rc = processAPI(QCAMERA_SM_EVT_CANCEL_PICTURE, NULL);
        rc = sendEvtNotify(CAMERA_MSG_ERROR, UNKNOWN_ERROR, 0);
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : cancelLiveSnapshot
 *
 * DESCRIPTION: cancel current live snapshot request
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::cancelLiveSnapshot()
{
    int rc = NO_ERROR;
    if (mLiveSnapshotThread != 0) {
        pthread_join(mLiveSnapshotThread,NULL);
        mLiveSnapshotThread = 0;
    }
    bLiveSnapshot = false;
    rc= pthread_create(&mLiveSnapshotThread, NULL, Live_Snapshot_thread, (void *) this);
    if (!rc) {
        pthread_setname_np(mLiveSnapshotThread, "CAM_cancel_liveSnap");
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : cancelLiveSnapshot_internal
 *
 * DESCRIPTION: cancel live snapshot during recording
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::cancelLiveSnapshot_internal() {
    int rc = NO_ERROR;

    unconfigureAdvancedCapture();
    LOGH("Enable display frames again");
    setDisplaySkip(FALSE);

    if (!mLongshotEnabled) {
        m_perfLock.lock_rel();
    }

    //wait for deferred (reprocess and jpeg) threads to finish
    waitDeferredWork(mReprocJob);
    waitDeferredWork(mJpegJob);

    //stop post processor
    m_postprocessor.stop();

    // stop snapshot channel
    if (!mParameters.isTNRSnapshotEnabled()) {
        rc = stopChannel(QCAMERA_CH_TYPE_SNAPSHOT);
    } else {
        QCameraChannel *pChannel = m_channels[QCAMERA_CH_TYPE_SNAPSHOT];
        if (NULL != pChannel) {
            QCameraStream *pStream = NULL;
            for (uint32_t i = 0 ; i < pChannel->getNumOfStreams(); i++ ) {
                pStream = pChannel->getStreamByIndex(i);
                if ((NULL != pStream) &&
                        (CAM_STREAM_TYPE_SNAPSHOT ==
                        pStream->getMyType())) {
                    break;
                }
            }
            if (pStream != NULL) {
                LOGD("REQUEST_FRAMES event for TNR snapshot");
                cam_stream_parm_buffer_t param;
                memset(&param, 0, sizeof(cam_stream_parm_buffer_t));
                param.type = CAM_STREAM_PARAM_TYPE_REQUEST_FRAMES;
                param.frameRequest.enableStream = 0;
                rc = pStream->setParameter(param);
                if (rc != NO_ERROR) {
                    LOGE("Stream Event REQUEST_FRAMES failed");
                }
            }
        }
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : putParameters
 *
 * DESCRIPTION: put parameters string impl
 *
 * PARAMETERS :
 *   @parms   : parameters string to be released
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::putParameters(char *parms)
{
    free(parms);
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : sendCommand
 *
 * DESCRIPTION: send command impl
 *
 * PARAMETERS :
 *   @command : command to be executed
 *   @arg1    : optional argument 1
 *   @arg2    : optional argument 2
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::sendCommand(int32_t command,
        __unused int32_t &arg1, __unused int32_t &arg2)
{
    int rc = NO_ERROR;

    switch (command) {
#ifndef VANILLA_HAL
    case CAMERA_CMD_LONGSHOT_ON:
        m_perfLock.lock_acq();
        arg1 = arg2 = 0;
        // Longshot can only be enabled when image capture
        // is not active.
        if ( !m_stateMachine.isCaptureRunning() ) {
            LOGI("Longshot Enabled");
            mLongshotEnabled = true;
            rc = mParameters.setLongshotEnable(mLongshotEnabled);

            // Due to recent buffer count optimizations
            // ZSL might run with considerably less buffers
            // when not in longshot mode. Preview needs to
            // restart in this case.
            if (isZSLMode() && m_stateMachine.isPreviewRunning()) {
                QCameraChannel *pChannel = NULL;
                QCameraStream *pSnapStream = NULL;
                pChannel = m_channels[QCAMERA_CH_TYPE_ZSL];
                if (NULL != pChannel) {
                    QCameraStream *pStream = NULL;
                    for (uint32_t i = 0; i < pChannel->getNumOfStreams(); i++) {
                        pStream = pChannel->getStreamByIndex(i);
                        if (pStream != NULL) {
                            if (pStream->isTypeOf(CAM_STREAM_TYPE_SNAPSHOT)) {
                                pSnapStream = pStream;
                                break;
                            }
                        }
                    }
                    if (NULL != pSnapStream) {
                        uint8_t required = 0;
                        required = getBufNumRequired(CAM_STREAM_TYPE_SNAPSHOT);
                        if (pSnapStream->getBufferCount() < required) {
                            // We restart here, to reset the FPS and no
                            // of buffers as per the requirement of longshot usecase.
                            arg1 = QCAMERA_SM_EVT_RESTART_PERVIEW;
                            if (getRelatedCamSyncInfo()->sync_control ==
                                    CAM_SYNC_RELATED_SENSORS_ON) {
                                arg2 = QCAMERA_SM_EVT_DELAYED_RESTART;
                            }
                        }
                    }
                }
            }
            //
            mPrepSnapRun = false;
            mCACDoneReceived = FALSE;
        } else {
            rc = NO_INIT;
        }
        break;
    case CAMERA_CMD_LONGSHOT_OFF:
        m_perfLock.lock_rel();
        if ( mLongshotEnabled && m_stateMachine.isCaptureRunning() ) {
            cancelPicture();
            processEvt(QCAMERA_SM_EVT_SNAPSHOT_DONE, NULL);
            QCameraChannel *pZSLChannel = m_channels[QCAMERA_CH_TYPE_ZSL];
            if (isZSLMode() && (NULL != pZSLChannel) && mPrepSnapRun) {
                mCameraHandle->ops->stop_zsl_snapshot(
                        mCameraHandle->camera_handle,
                        pZSLChannel->getMyHandle());
            }
        }
        mPrepSnapRun = false;
        LOGI("Longshot Disabled");
        mLongshotEnabled = false;
        rc = mParameters.setLongshotEnable(mLongshotEnabled);
        mCACDoneReceived = FALSE;
        break;
    case CAMERA_CMD_HISTOGRAM_ON:
    case CAMERA_CMD_HISTOGRAM_OFF:
        rc = setHistogram(command == CAMERA_CMD_HISTOGRAM_ON? true : false);
        LOGH("Histogram -> %s",
              mParameters.isHistogramEnabled() ? "Enabled" : "Disabled");
        break;
#endif
    case CAMERA_CMD_START_FACE_DETECTION:
    case CAMERA_CMD_STOP_FACE_DETECTION:
        mParameters.setFaceDetectionOption(command == CAMERA_CMD_START_FACE_DETECTION? true : false);
        rc = setFaceDetection(command == CAMERA_CMD_START_FACE_DETECTION? true : false);
        LOGH("FaceDetection -> %s",
              mParameters.isFaceDetectionEnabled() ? "Enabled" : "Disabled");
        break;
#ifndef VANILLA_HAL
    case CAMERA_CMD_HISTOGRAM_SEND_DATA:
#endif
    default:
        rc = NO_ERROR;
        break;
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : registerFaceImage
 *
 * DESCRIPTION: register face image impl
 *
 * PARAMETERS :
 *   @img_ptr : ptr to image buffer
 *   @config  : ptr to config struct about input image info
 *   @faceID  : [OUT] face ID to uniquely identifiy the registered face image
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::registerFaceImage(void *img_ptr,
                                                 cam_pp_offline_src_config_t *config,
                                                 int32_t &faceID)
{
    int rc = NO_ERROR;
    faceID = -1;

    if (img_ptr == NULL || config == NULL) {
        LOGE("img_ptr or config is NULL");
        return BAD_VALUE;
    }

    // allocate ion memory for source image
    QCameraHeapMemory *imgBuf = new QCameraHeapMemory(QCAMERA_ION_USE_CACHE);
    if (imgBuf == NULL) {
        LOGE("Unable to new heap memory obj for image buf");
        return NO_MEMORY;
    }

    rc = imgBuf->allocate(1, config->input_buf_planes.plane_info.frame_len, NON_SECURE);
    if (rc < 0) {
        LOGE("Unable to allocate heap memory for image buf");
        delete imgBuf;
        return NO_MEMORY;
    }

    void *pBufPtr = imgBuf->getPtr(0);
    if (pBufPtr == NULL) {
        LOGE("image buf is NULL");
        imgBuf->deallocate();
        delete imgBuf;
        return NO_MEMORY;
    }
    memcpy(pBufPtr, img_ptr, config->input_buf_planes.plane_info.frame_len);

    cam_pp_feature_config_t pp_feature;
    memset(&pp_feature, 0, sizeof(cam_pp_feature_config_t));
    pp_feature.feature_mask = CAM_QCOM_FEATURE_REGISTER_FACE;
    QCameraReprocessChannel *pChannel =
        addOfflineReprocChannel(*config, pp_feature, NULL, NULL);

    if (pChannel == NULL) {
        LOGE("fail to add offline reprocess channel");
        imgBuf->deallocate();
        delete imgBuf;
        return UNKNOWN_ERROR;
    }

    rc = pChannel->start();
    if (rc != NO_ERROR) {
        LOGE("Cannot start reprocess channel");
        imgBuf->deallocate();
        delete imgBuf;
        delete pChannel;
        return rc;
    }

    ssize_t bufSize = imgBuf->getSize(0);
    if (BAD_INDEX != bufSize) {
        rc = pChannel->doReprocess(imgBuf->getFd(0), imgBuf->getPtr(0),
                (size_t)bufSize, faceID);
    } else {
        LOGE("Failed to retrieve buffer size (bad index)");
        return UNKNOWN_ERROR;
    }

    // done with register face image, free imgbuf and delete reprocess channel
    imgBuf->deallocate();
    delete imgBuf;
    imgBuf = NULL;
    pChannel->stop();
    delete pChannel;
    pChannel = NULL;

    return rc;
}

/*===========================================================================
 * FUNCTION   : release
 *
 * DESCRIPTION: release camera resource impl
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::release()
{
    // stop and delete all channels
    for (int i = 0; i <QCAMERA_CH_TYPE_MAX ; i++) {
        if (m_channels[i] != NULL) {
            stopChannel((qcamera_ch_type_enum_t)i);
            delChannel((qcamera_ch_type_enum_t)i);
        }
    }

    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : dump
 *
 * DESCRIPTION: camera status dump impl
 *
 * PARAMETERS :
 *   @fd      : fd for the buffer to be dumped with camera status
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::dump(int fd)
{
    dprintf(fd, "\n Camera HAL information Begin \n");
    dprintf(fd, "Camera ID: %d \n", mCameraId);
    dprintf(fd, "StoreMetaDataInFrame: %d \n", mStoreMetaDataInFrame);
    dprintf(fd, "\n Configuration: %s", mParameters.dump().string());
    dprintf(fd, "\n State Information: %s", m_stateMachine.dump().string());
    dprintf(fd, "\n Camera HAL information End \n");

    /* send UPDATE_DEBUG_LEVEL to the backend so that they can read the
       debug level property */
    mParameters.updateDebugLevel();
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : processAPI
 *
 * DESCRIPTION: process API calls from upper layer
 *
 * PARAMETERS :
 *   @api         : API to be processed
 *   @api_payload : ptr to API payload if any
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::processAPI(qcamera_sm_evt_enum_t api, void *api_payload)
{
    int ret = DEAD_OBJECT;

    if (m_smThreadActive) {
        ret = m_stateMachine.procAPI(api, api_payload);
    }

    return ret;
}

/*===========================================================================
 * FUNCTION   : processEvt
 *
 * DESCRIPTION: process Evt from backend via mm-camera-interface
 *
 * PARAMETERS :
 *   @evt         : event type to be processed
 *   @evt_payload : ptr to event payload if any
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::processEvt(qcamera_sm_evt_enum_t evt, void *evt_payload)
{
    return m_stateMachine.procEvt(evt, evt_payload);
}

/*===========================================================================
 * FUNCTION   : processSyncEvt
 *
 * DESCRIPTION: process synchronous Evt from backend
 *
 * PARAMETERS :
 *   @evt         : event type to be processed
 *   @evt_payload : ptr to event payload if any
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::processSyncEvt(qcamera_sm_evt_enum_t evt, void *evt_payload)
{
    int rc = NO_ERROR;

    pthread_mutex_lock(&m_evtLock);
    rc =  processEvt(evt, evt_payload);
    if (rc == NO_ERROR) {
        memset(&m_evtResult, 0, sizeof(qcamera_api_result_t));
        while (m_evtResult.request_api != evt) {
            pthread_cond_wait(&m_evtCond, &m_evtLock);
        }
        rc =  m_evtResult.status;
    }
    pthread_mutex_unlock(&m_evtLock);

    return rc;
}

/*===========================================================================
 * FUNCTION   : evtHandle
 *
 * DESCRIPTION: Function registerd to mm-camera-interface to handle backend events
 *
 * PARAMETERS :
 *   @camera_handle : event type to be processed
 *   @evt           : ptr to event
 *   @user_data     : user data ptr
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::camEvtHandle(uint32_t /*camera_handle*/,
                                          mm_camera_event_t *evt,
                                          void *user_data)
{
    QCamera2HardwareInterface *obj = (QCamera2HardwareInterface *)user_data;
    if (obj && evt) {
        mm_camera_event_t *payload =
            (mm_camera_event_t *)malloc(sizeof(mm_camera_event_t));
        if (NULL != payload) {
            *payload = *evt;
            //peek into the event, if this is an eztune event from server,
            //then we don't need to post it to the SM Qs, we shud directly
            //spawn a thread and get the job done (jpeg or raw snapshot)
            switch (payload->server_event_type) {
                case CAM_EVENT_TYPE_INT_TAKE_JPEG:
                    //Received JPEG trigger from eztune
                    if (false == obj->m_bIntJpegEvtPending) {
                        pthread_mutex_lock(&obj->m_int_lock);
                        obj->m_bIntJpegEvtPending = true;
                        pthread_mutex_unlock(&obj->m_int_lock);
                        obj->takePictureInternal();
                    }
                    free(payload);
                    break;
                case CAM_EVENT_TYPE_INT_TAKE_RAW:
                    //Received RAW trigger from eztune
                    if (false == obj->m_bIntRawEvtPending) {
                        pthread_mutex_lock(&obj->m_int_lock);
                        obj->m_bIntRawEvtPending = true;
                        pthread_mutex_unlock(&obj->m_int_lock);
                        obj->takePictureInternal();
                    }
                    free(payload);
                    break;
                case CAM_EVENT_TYPE_DAEMON_DIED:
                    {
                        Mutex::Autolock l(obj->mDefLock);
                        obj->mDefCond.broadcast();
                        LOGH("broadcast mDefCond signal\n");
                    }
                default:
                    obj->processEvt(QCAMERA_SM_EVT_EVT_NOTIFY, payload);
                    break;
            }
        }
    } else {
        LOGE("NULL user_data");
    }
}

/*===========================================================================
 * FUNCTION   : jpegEvtHandle
 *
 * DESCRIPTION: Function registerd to mm-jpeg-interface to handle jpeg events
 *
 * PARAMETERS :
 *   @status    : status of jpeg job
 *   @client_hdl: jpeg client handle
 *   @jobId     : jpeg job Id
 *   @p_ouput   : ptr to jpeg output result struct
 *   @userdata  : user data ptr
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::jpegEvtHandle(jpeg_job_status_t status,
                                              uint32_t /*client_hdl*/,
                                              uint32_t jobId,
                                              mm_jpeg_output_t *p_output,
                                              void *userdata)
{
    QCamera2HardwareInterface *obj = (QCamera2HardwareInterface *)userdata;
    if (obj) {
        qcamera_jpeg_evt_payload_t *payload =
            (qcamera_jpeg_evt_payload_t *)malloc(sizeof(qcamera_jpeg_evt_payload_t));
        if (NULL != payload) {
            memset(payload, 0, sizeof(qcamera_jpeg_evt_payload_t));
            payload->status = status;
            payload->jobId = jobId;
            if (p_output != NULL) {
                payload->out_data = *p_output;
            }
            obj->processEvt(QCAMERA_SM_EVT_JPEG_EVT_NOTIFY, payload);
        }
    } else {
        LOGE("NULL user_data");
    }
}

/*===========================================================================
 * FUNCTION   : thermalEvtHandle
 *
 * DESCRIPTION: routine to handle thermal event notification
 *
 * PARAMETERS :
 *   @level      : thermal level
 *   @userdata   : userdata passed in during registration
 *   @data       : opaque data from thermal client
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::thermalEvtHandle(
        qcamera_thermal_level_enum_t *level, void *userdata, void *data)
{
    if (!mCameraOpened) {
        LOGH("Camera is not opened, no need to handle thermal evt");
        return NO_ERROR;
    }

    // Make sure thermal events are logged
    LOGH("level = %d, userdata = %p, data = %p",
         *level, userdata, data);
    //We don't need to lockAPI, waitAPI here. QCAMERA_SM_EVT_THERMAL_NOTIFY
    // becomes an aync call. This also means we can only pass payload
    // by value, not by address.
    return processAPI(QCAMERA_SM_EVT_THERMAL_NOTIFY, (void *)level);
}

/*===========================================================================
 * FUNCTION   : sendEvtNotify
 *
 * DESCRIPTION: send event notify to notify thread
 *
 * PARAMETERS :
 *   @msg_type: msg type to be sent
 *   @ext1    : optional extension1
 *   @ext2    : optional extension2
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::sendEvtNotify(int32_t msg_type,
                                                 int32_t ext1,
                                                 int32_t ext2)
{
    qcamera_callback_argm_t cbArg;
    memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
    cbArg.cb_type = QCAMERA_NOTIFY_CALLBACK;
    cbArg.msg_type = msg_type;
    cbArg.ext1 = ext1;
    cbArg.ext2 = ext2;
    return m_cbNotifier.notifyCallback(cbArg);
}

/*===========================================================================
 * FUNCTION   : processAEInfo
 *
 * DESCRIPTION: process AE updates
 *
 * PARAMETERS :
 *   @ae_params: current AE parameters
 *
 * RETURN     : None
 *==========================================================================*/
int32_t QCamera2HardwareInterface::processAEInfo(cam_3a_params_t &ae_params)
{
    mParameters.updateAEInfo(ae_params);
    if (mParameters.isInstantAECEnabled()) {
        // Reset Instant AEC info only if instant aec enabled.
        bool bResetInstantAec = false;
        if (ae_params.settled) {
            // If AEC settled, reset instant AEC
            bResetInstantAec = true;
        } else if ((mParameters.isInstantCaptureEnabled()) &&
                (mInstantAecFrameCount >= mParameters.getAecFrameBoundValue())) {
            // if AEC not settled, and instant capture enabled,
            // reset instant AEC only when frame count is
            // more or equal to AEC frame bound value.
            bResetInstantAec = true;
        } else if ((mParameters.isInstantAECEnabled()) &&
                (mInstantAecFrameCount >= mParameters.getAecSkipDisplayFrameBound())) {
            // if AEC not settled, and only instant AEC enabled,
            // reset instant AEC only when frame count is
            // more or equal to AEC skip display frame bound value.
            bResetInstantAec = true;
        }

        if (bResetInstantAec) {
            LOGD("setting instant AEC to false");
            mParameters.setInstantAEC(false, true);
            mInstantAecFrameCount = 0;
        }
    }
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : processFocusPositionInfo
 *
 * DESCRIPTION: process AF updates
 *
 * PARAMETERS :
 *   @cur_pos_info: current lens position
 *
 * RETURN     : None
 *==========================================================================*/
int32_t QCamera2HardwareInterface::processFocusPositionInfo(cam_focus_pos_info_t &cur_pos_info)
{
    mParameters.updateCurrentFocusPosition(cur_pos_info);
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : processAutoFocusEvent
 *
 * DESCRIPTION: process auto focus event
 *
 * PARAMETERS :
 *   @focus_data: struct containing auto focus result info
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::processAutoFocusEvent(cam_auto_focus_data_t &focus_data)
{
    int32_t ret = NO_ERROR;
    LOGH("E");

    if (getRelatedCamSyncInfo()->mode == CAM_MODE_SECONDARY) {
        // Ignore focus updates
        LOGH("X Secondary Camera, no need to process!! ");
        return ret;
    }
    cam_focus_mode_type focusMode = mParameters.getFocusMode();
    LOGH("[AF_DBG]  focusMode=%d, focusState=%d isDepth=%d",
             focusMode, focus_data.focus_state, focus_data.isDepth);

    switch (focusMode) {
    case CAM_FOCUS_MODE_AUTO:
    case CAM_FOCUS_MODE_MACRO:
        // ignore AF event if AF was already cancelled meanwhile
        if (!mActiveAF) {
            break;
        }
        // If the HAL focus mode is different from AF INFINITY focus mode, send event to app
        if ((focus_data.focus_mode == CAM_FOCUS_MODE_INFINITY) &&
                (focus_data.focus_state == CAM_AF_STATE_INACTIVE)) {
            ret = sendEvtNotify(CAMERA_MSG_FOCUS, true, 0);
            mActiveAF = false; // reset the mActiveAF in this special case
            break;
        }

        //while transitioning from CAF->Auto/Macro, we might receive CAF related
        //events (PASSIVE_*) due to timing. Ignore such events if any.
        if ((focus_data.focus_state == CAM_AF_STATE_PASSIVE_SCAN) ||
                (focus_data.focus_state == CAM_AF_STATE_PASSIVE_FOCUSED) ||
                (focus_data.focus_state == CAM_AF_STATE_PASSIVE_UNFOCUSED)) {
            break;
        }

        //This is just an intermediate update to HAL indicating focus is in progress. No need
        //to send this event to app. Same applies to INACTIVE state as well.
        if ((focus_data.focus_state == CAM_AF_STATE_ACTIVE_SCAN) ||
                (focus_data.focus_state == CAM_AF_STATE_INACTIVE)) {
            break;
        }
        // update focus distance
        mParameters.updateFocusDistances(&focus_data.focus_dist);

        //flush any old snapshot frames in ZSL Q which are not focused.
        if (mParameters.isZSLMode() && focus_data.flush_info.needFlush ) {
            QCameraPicChannel *pZSLChannel =
                    (QCameraPicChannel *)m_channels[QCAMERA_CH_TYPE_ZSL];
            if (NULL != pZSLChannel) {
                //flush the zsl-buffer
                uint32_t flush_frame_idx = focus_data.flush_info.focused_frame_idx;
                LOGD("flush the zsl-buffer before frame = %u.", flush_frame_idx);
                pZSLChannel->flushSuperbuffer(flush_frame_idx);
            }
        }

        //send event to app finally
        LOGI("Send AF DOne event to app");
        ret = sendEvtNotify(CAMERA_MSG_FOCUS,
                            (focus_data.focus_state == CAM_AF_STATE_FOCUSED_LOCKED), 0);
        break;
    case CAM_FOCUS_MODE_CONTINOUS_VIDEO:
    case CAM_FOCUS_MODE_CONTINOUS_PICTURE:

        // If the HAL focus mode is different from AF INFINITY focus mode, send event to app
        if ((focus_data.focus_mode == CAM_FOCUS_MODE_INFINITY) &&
                (focus_data.focus_state == CAM_AF_STATE_INACTIVE)) {
            ret = sendEvtNotify(CAMERA_MSG_FOCUS, false, 0);
            mActiveAF = false; // reset the mActiveAF in this special case
            break;
        }

        //If AutoFocus() is triggered while in CAF mode, ignore all CAF events (PASSIVE_*) and
        //process/wait for only ACTIVE_* events.
        if (((focus_data.focus_state == CAM_AF_STATE_PASSIVE_FOCUSED) ||
                (focus_data.focus_state == CAM_AF_STATE_PASSIVE_UNFOCUSED) ||
                (focus_data.focus_state == CAM_AF_STATE_PASSIVE_SCAN)) && mActiveAF) {
            break;
        }

        if (!bDepthAFCallbacks && focus_data.isDepth &&
                (focus_data.focus_state == CAM_AF_STATE_PASSIVE_SCAN)) {
            LOGD("Skip sending scan state to app, if depth focus");
            break;
        }

        //These are the AF states for which we need to send notification to app in CAF mode.
        //This includes both regular CAF (PASSIVE) events as well as ACTIVE events ( in case
        //AF is triggered while in CAF mode)
        if ((focus_data.focus_state == CAM_AF_STATE_PASSIVE_FOCUSED) ||
                (focus_data.focus_state == CAM_AF_STATE_PASSIVE_UNFOCUSED) ||
                (focus_data.focus_state == CAM_AF_STATE_FOCUSED_LOCKED) ||
                (focus_data.focus_state == CAM_AF_STATE_NOT_FOCUSED_LOCKED)) {

            // update focus distance
            mParameters.updateFocusDistances(&focus_data.focus_dist);

            if (mParameters.isZSLMode() && focus_data.flush_info.needFlush ) {
                QCameraPicChannel *pZSLChannel =
                        (QCameraPicChannel *)m_channels[QCAMERA_CH_TYPE_ZSL];
                if (NULL != pZSLChannel) {
                    //flush the zsl-buffer
                    uint32_t flush_frame_idx = focus_data.flush_info.focused_frame_idx;
                    LOGD("flush the zsl-buffer before frame = %u.", flush_frame_idx);
                    pZSLChannel->flushSuperbuffer(flush_frame_idx);
                }
            }

            if (mActiveAF) {
                LOGI("Send AF Done event to app");
            }
            ret = sendEvtNotify(CAMERA_MSG_FOCUS,
                    ((focus_data.focus_state == CAM_AF_STATE_PASSIVE_FOCUSED) ||
                    (focus_data.focus_state == CAM_AF_STATE_FOCUSED_LOCKED)), 0);
        }
        ret = sendEvtNotify(CAMERA_MSG_FOCUS_MOVE,
                (focus_data.focus_state == CAM_AF_STATE_PASSIVE_SCAN), 0);
        break;
    case CAM_FOCUS_MODE_INFINITY:
    case CAM_FOCUS_MODE_FIXED:
    case CAM_FOCUS_MODE_EDOF:
    default:
        LOGH("no ops for autofocus event in focusmode %d", focusMode);
        break;
    }

    //Reset mActiveAF once we receive focus done event
    if ((focus_data.focus_state == CAM_AF_STATE_FOCUSED_LOCKED) ||
            (focus_data.focus_state == CAM_AF_STATE_NOT_FOCUSED_LOCKED)) {
        mActiveAF = false;
    }

    LOGH("X");
    return ret;
}

/*===========================================================================
 * FUNCTION   : processZoomEvent
 *
 * DESCRIPTION: process zoom event
 *
 * PARAMETERS :
 *   @crop_info : crop info as a result of zoom operation
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::processZoomEvent(cam_crop_data_t &crop_info)
{
    int32_t ret = NO_ERROR;

    for (int i = 0; i < QCAMERA_CH_TYPE_MAX; i++) {
        if (m_channels[i] != NULL) {
            ret = m_channels[i]->processZoomDone(mPreviewWindow, crop_info);
        }
    }
    return ret;
}

/*===========================================================================
 * FUNCTION   : processZSLCaptureDone
 *
 * DESCRIPTION: process ZSL capture done events
 *
 * PARAMETERS : None
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::processZSLCaptureDone()
{
    int rc = NO_ERROR;

    if (++mInputCount >= mParameters.getBurstCountForAdvancedCapture()) {
        rc = unconfigureAdvancedCapture();
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : processRetroAECUnlock
 *
 * DESCRIPTION: process retro burst AEC unlock events
 *
 * PARAMETERS : None
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::processRetroAECUnlock()
{
    int rc = NO_ERROR;

    LOGH("LED assisted AF Release AEC Lock");
    rc = mParameters.setAecLock("false");
    if (NO_ERROR != rc) {
        LOGE("Error setting AEC lock");
        return rc;
    }

    rc = mParameters.commitParameters();
    if (NO_ERROR != rc) {
        LOGE("Error during camera parameter commit");
    } else {
        m_bLedAfAecLock = FALSE;
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : processHDRData
 *
 * DESCRIPTION: process HDR scene events
 *
 * PARAMETERS :
 *   @hdr_scene : HDR scene event data
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::processHDRData(
        __unused cam_asd_hdr_scene_data_t hdr_scene)
{
    int rc = NO_ERROR;

#ifndef VANILLA_HAL
    if (hdr_scene.is_hdr_scene &&
      (hdr_scene.hdr_confidence > HDR_CONFIDENCE_THRESHOLD) &&
      mParameters.isAutoHDREnabled()) {
        m_HDRSceneEnabled = true;
    } else {
        m_HDRSceneEnabled = false;
    }
    mParameters.setHDRSceneEnable(m_HDRSceneEnabled);

    if ( msgTypeEnabled(CAMERA_MSG_META_DATA) ) {

        size_t data_len = sizeof(int);
        size_t buffer_len = 1 *sizeof(int)       //meta type
                          + 1 *sizeof(int)       //data len
                          + 1 *sizeof(int);      //data
        camera_memory_t *hdrBuffer = mGetMemory(-1,
                                                 buffer_len,
                                                 1,
                                                 mCallbackCookie);
        if ( NULL == hdrBuffer ) {
            LOGE("Not enough memory for auto HDR data");
            return NO_MEMORY;
        }

        int *pHDRData = (int *)hdrBuffer->data;
        if (pHDRData == NULL) {
            LOGE("memory data ptr is NULL");
            return UNKNOWN_ERROR;
        }

        pHDRData[0] = CAMERA_META_DATA_HDR;
        pHDRData[1] = (int)data_len;
        pHDRData[2] = m_HDRSceneEnabled;

        qcamera_callback_argm_t cbArg;
        memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
        cbArg.cb_type = QCAMERA_DATA_CALLBACK;
        cbArg.msg_type = CAMERA_MSG_META_DATA;
        cbArg.data = hdrBuffer;
        cbArg.user_data = hdrBuffer;
        cbArg.cookie = this;
        cbArg.release_cb = releaseCameraMemory;
        rc = m_cbNotifier.notifyCallback(cbArg);
        if (rc != NO_ERROR) {
            LOGE("fail sending auto HDR notification");
            hdrBuffer->release(hdrBuffer);
        }
    }

    LOGH("hdr_scene_data: processHDRData: %d %f",
          hdr_scene.is_hdr_scene,
          hdr_scene.hdr_confidence);

#endif
  return rc;
}

/*===========================================================================
 * FUNCTION   : transAwbMetaToParams
 *
 * DESCRIPTION: translate awb params from metadata callback to QCameraParametersIntf
 *
 * PARAMETERS :
 *   @awb_params : awb params from metadata callback
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::transAwbMetaToParams(cam_awb_params_t &awb_params)
{
    mParameters.updateAWBParams(awb_params);
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : processPrepSnapshotDone
 *
 * DESCRIPTION: process prep snapshot done event
 *
 * PARAMETERS :
 *   @prep_snapshot_state  : state of prepare snapshot done. In other words,
 *                           i.e. whether need future frames for capture.
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::processPrepSnapshotDoneEvent(
                        cam_prep_snapshot_state_t prep_snapshot_state)
{
    int32_t ret = NO_ERROR;
    LOGI("[KPI Perf]: Received PREPARE SANSPHOT Done event state = %d",
            prep_snapshot_state);
    if (m_channels[QCAMERA_CH_TYPE_ZSL] &&
        prep_snapshot_state == NEED_FUTURE_FRAME) {
        LOGH("already handled in mm-camera-intf, no ops here");
        if (isRetroPicture()) {
            mParameters.setAecLock("true");
            mParameters.commitParameters();
            m_bLedAfAecLock = TRUE;
        }
    }
    return ret;
}

/*===========================================================================
 * FUNCTION   : processASDUpdate
 *
 * DESCRIPTION: process ASD update event
 *
 * PARAMETERS :
 *   @scene: selected scene mode
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::processASDUpdate(
        __unused cam_asd_decision_t asd_decision)
{

#ifndef VANILLA_HAL
    if ( msgTypeEnabled(CAMERA_MSG_META_DATA) ) {
        size_t data_len = sizeof(cam_auto_scene_t);
        size_t buffer_len = 1 *sizeof(int)       //meta type
                + 1 *sizeof(int)       //data len
                + data_len;            //data
        camera_memory_t *asdBuffer = mGetMemory(-1,
                buffer_len, 1, mCallbackCookie);
        if ( NULL == asdBuffer ) {
            LOGE("Not enough memory for histogram data");
            return NO_MEMORY;
        }

        int *pASDData = (int *)asdBuffer->data;
        if (pASDData == NULL) {
            LOGE("memory data ptr is NULL");
            return UNKNOWN_ERROR;
        }

        pASDData[0] = CAMERA_META_DATA_ASD;
        pASDData[1] = (int)data_len;
        pASDData[2] = asd_decision.detected_scene;

        qcamera_callback_argm_t cbArg;
        memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
        cbArg.cb_type = QCAMERA_DATA_CALLBACK;
        cbArg.msg_type = CAMERA_MSG_META_DATA;
        cbArg.data = asdBuffer;
        cbArg.user_data = asdBuffer;
        cbArg.cookie = this;
        cbArg.release_cb = releaseCameraMemory;
        int32_t rc = m_cbNotifier.notifyCallback(cbArg);
        if (rc != NO_ERROR) {
            LOGE("fail sending notification");
            asdBuffer->release(asdBuffer);
        }
    }
#endif
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : processJpegNotify
 *
 * DESCRIPTION: process jpeg event
 *
 * PARAMETERS :
 *   @jpeg_evt: ptr to jpeg event payload
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::processJpegNotify(qcamera_jpeg_evt_payload_t *jpeg_evt)
{
    return m_postprocessor.processJpegEvt(jpeg_evt);
}

/*===========================================================================
 * FUNCTION   : lockAPI
 *
 * DESCRIPTION: lock to process API
 *
 * PARAMETERS : none
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::lockAPI()
{
    pthread_mutex_lock(&m_lock);
}

/*===========================================================================
 * FUNCTION   : waitAPIResult
 *
 * DESCRIPTION: wait for API result coming back. This is a blocking call, it will
 *              return only cerntain API event type arrives
 *
 * PARAMETERS :
 *   @api_evt : API event type
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::waitAPIResult(qcamera_sm_evt_enum_t api_evt,
        qcamera_api_result_t *apiResult)
{
    LOGD("wait for API result of evt (%d)", api_evt);
    int resultReceived = 0;
    while  (!resultReceived) {
        pthread_cond_wait(&m_cond, &m_lock);
        if (m_apiResultList != NULL) {
            api_result_list *apiResultList = m_apiResultList;
            api_result_list *apiResultListPrevious = m_apiResultList;
            while (apiResultList != NULL) {
                if (apiResultList->result.request_api == api_evt) {
                    resultReceived = 1;
                    *apiResult = apiResultList->result;
                    apiResultListPrevious->next = apiResultList->next;
                    if (apiResultList == m_apiResultList) {
                        m_apiResultList = apiResultList->next;
                    }
                    free(apiResultList);
                    break;
                }
                else {
                    apiResultListPrevious = apiResultList;
                    apiResultList = apiResultList->next;
                }
            }
        }
    }
    LOGD("return (%d) from API result wait for evt (%d)",
           apiResult->status, api_evt);
}


/*===========================================================================
 * FUNCTION   : unlockAPI
 *
 * DESCRIPTION: API processing is done, unlock
 *
 * PARAMETERS : none
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::unlockAPI()
{
    pthread_mutex_unlock(&m_lock);
}

/*===========================================================================
 * FUNCTION   : signalAPIResult
 *
 * DESCRIPTION: signal condition viarable that cerntain API event type arrives
 *
 * PARAMETERS :
 *   @result  : API result
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::signalAPIResult(qcamera_api_result_t *result)
{

    pthread_mutex_lock(&m_lock);
    api_result_list *apiResult = (api_result_list *)malloc(sizeof(api_result_list));
    if (apiResult == NULL) {
        LOGE("ERROR: malloc for api result failed, Result will not be sent");
        goto malloc_failed;
    }
    apiResult->result = *result;
    apiResult->next = NULL;
    if (m_apiResultList == NULL) m_apiResultList = apiResult;
    else {
        api_result_list *apiResultList = m_apiResultList;
        while(apiResultList->next != NULL) apiResultList = apiResultList->next;
        apiResultList->next = apiResult;
    }
malloc_failed:
    pthread_cond_broadcast(&m_cond);
    pthread_mutex_unlock(&m_lock);
}

/*===========================================================================
 * FUNCTION   : signalEvtResult
 *
 * DESCRIPTION: signal condition variable that certain event was processed
 *
 * PARAMETERS :
 *   @result  : Event result
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::signalEvtResult(qcamera_api_result_t *result)
{
    pthread_mutex_lock(&m_evtLock);
    m_evtResult = *result;
    pthread_cond_signal(&m_evtCond);
    pthread_mutex_unlock(&m_evtLock);
}

int32_t QCamera2HardwareInterface::prepareRawStream(QCameraChannel *curChannel)
{
    int32_t rc = NO_ERROR;
    cam_dimension_t str_dim,max_dim;
    QCameraChannel *pChannel;

    max_dim.width = 0;
    max_dim.height = 0;

    for (int j = 0; j < QCAMERA_CH_TYPE_MAX; j++) {
        if (m_channels[j] != NULL) {
            pChannel = m_channels[j];
            for (uint8_t i = 0; i < pChannel->getNumOfStreams(); i++) {
                QCameraStream *pStream = pChannel->getStreamByIndex(i);
                if (pStream != NULL) {
                    if ((pStream->isTypeOf(CAM_STREAM_TYPE_METADATA))
                            || (pStream->isTypeOf(CAM_STREAM_TYPE_POSTVIEW))) {
                        continue;
                    }
                    pStream->getFrameDimension(str_dim);
                    if (str_dim.width > max_dim.width) {
                        max_dim.width = str_dim.width;
                    }
                    if (str_dim.height > max_dim.height) {
                        max_dim.height = str_dim.height;
                    }
                }
            }
        }
    }

    for (uint8_t i = 0; i < curChannel->getNumOfStreams(); i++) {
        QCameraStream *pStream = curChannel->getStreamByIndex(i);
        if (pStream != NULL) {
            if ((pStream->isTypeOf(CAM_STREAM_TYPE_METADATA))
                    || (pStream->isTypeOf(CAM_STREAM_TYPE_POSTVIEW))) {
                continue;
            }
            pStream->getFrameDimension(str_dim);
            if (str_dim.width > max_dim.width) {
                max_dim.width = str_dim.width;
            }
            if (str_dim.height > max_dim.height) {
                max_dim.height = str_dim.height;
            }
        }
    }
    rc = mParameters.updateRAW(max_dim);
    return rc;
}
/*===========================================================================
 * FUNCTION   : addStreamToChannel
 *
 * DESCRIPTION: add a stream into a channel
 *
 * PARAMETERS :
 *   @pChannel   : ptr to channel obj
 *   @streamType : type of stream to be added
 *   @streamCB   : callback of stream
 *   @userData   : user data ptr to callback
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::addStreamToChannel(QCameraChannel *pChannel,
                                                      cam_stream_type_t streamType,
                                                      stream_cb_routine streamCB,
                                                      void *userData)
{
    int32_t rc = NO_ERROR;

    if (streamType == CAM_STREAM_TYPE_RAW) {
        prepareRawStream(pChannel);
    }
    QCameraHeapMemory *pStreamInfo = allocateStreamInfoBuf(streamType);
    if (pStreamInfo == NULL) {
        LOGE("no mem for stream info buf");
        return NO_MEMORY;
    }
    uint8_t minStreamBufNum = getBufNumRequired(streamType);
    bool bDynAllocBuf = false;
    if (isZSLMode() && streamType == CAM_STREAM_TYPE_SNAPSHOT) {
        bDynAllocBuf = true;
    }

    cam_padding_info_t padding_info;

    if (streamType == CAM_STREAM_TYPE_ANALYSIS) {
        cam_analysis_info_t analysisInfo;
        cam_feature_mask_t featureMask;

        featureMask = 0;
        mParameters.getStreamPpMask(CAM_STREAM_TYPE_ANALYSIS, featureMask);
        rc = mParameters.getAnalysisInfo(
                ((mParameters.getRecordingHintValue() == true) &&
                 mParameters.fdModeInVideo()),
                FALSE,
                featureMask,
                &analysisInfo);
        if (rc != NO_ERROR) {
            LOGE("getAnalysisInfo failed, ret = %d", rc);
            return rc;
        }

        padding_info = analysisInfo.analysis_padding_info;
    } else {
        padding_info =
                gCamCapability[mCameraId]->padding_info;
        if (streamType == CAM_STREAM_TYPE_PREVIEW || streamType == CAM_STREAM_TYPE_POSTVIEW) {
            padding_info.width_padding = mSurfaceStridePadding;
            padding_info.height_padding = CAM_PAD_TO_2;
        }
        if((!needReprocess())
                || (streamType != CAM_STREAM_TYPE_SNAPSHOT)
                || (!mParameters.isLLNoiseEnabled())) {
            padding_info.offset_info.offset_x = 0;
            padding_info.offset_info.offset_y = 0;
        }
    }

    bool deferAllocation = needDeferred(streamType);
    LOGD("deferAllocation = %d bDynAllocBuf = %d, stream type = %d",
            deferAllocation, bDynAllocBuf, streamType);
    rc = pChannel->addStream(*this,
            pStreamInfo,
            NULL,
            minStreamBufNum,
            &padding_info,
            streamCB, userData,
            bDynAllocBuf,
            deferAllocation);

    if (rc != NO_ERROR) {
        LOGE("add stream type (%d) failed, ret = %d",
               streamType, rc);
        return rc;
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : addPreviewChannel
 *
 * DESCRIPTION: add a preview channel that contains a preview stream
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::addPreviewChannel()
{
    int32_t rc = NO_ERROR;
    QCameraChannel *pChannel = NULL;
    char value[PROPERTY_VALUE_MAX];
    bool raw_yuv = false;


    if (m_channels[QCAMERA_CH_TYPE_PREVIEW] != NULL) {
        // if we had preview channel before, delete it first
        delete m_channels[QCAMERA_CH_TYPE_PREVIEW];
        m_channels[QCAMERA_CH_TYPE_PREVIEW] = NULL;
    }

    pChannel = new QCameraChannel(mCameraHandle->camera_handle,
                                  mCameraHandle->ops);
    if (NULL == pChannel) {
        LOGE("no mem for preview channel");
        return NO_MEMORY;
    }

    // preview only channel, don't need bundle attr and cb
    rc = pChannel->init(NULL, NULL, NULL);
    if (rc != NO_ERROR) {
        LOGE("init preview channel failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    // meta data stream always coexists with preview if applicable
    rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_METADATA,
                            metadata_stream_cb_routine, this);
    if (rc != NO_ERROR) {
        LOGE("add metadata stream failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    if (isRdiMode()) {
        rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_RAW,
                                rdi_mode_stream_cb_routine, this);
    } else {
        if (isNoDisplayMode()) {
            rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_PREVIEW,
                                    nodisplay_preview_stream_cb_routine, this);
        } else {
            rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_PREVIEW,
                                    preview_stream_cb_routine, this);
#ifdef TARGET_TS_MAKEUP
            int whiteLevel, cleanLevel;
            if(mParameters.getTsMakeupInfo(whiteLevel, cleanLevel) == false)
#endif
            pChannel->setStreamSyncCB(CAM_STREAM_TYPE_PREVIEW,
                    synchronous_stream_cb_routine);
        }
    }

    if (rc != NO_ERROR) {
        LOGE("add raw/preview stream failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    if (((mParameters.fdModeInVideo())
            || (mParameters.getDcrf() == true)
            || (mParameters.getRecordingHintValue() != true))
            && (!mParameters.isSecureMode())) {
        rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_ANALYSIS,
                NULL, this);
        if (rc != NO_ERROR) {
            LOGE("add Analysis stream failed, ret = %d", rc);
            delete pChannel;
            return rc;
        }
    }

    property_get("persist.camera.raw_yuv", value, "0");
    raw_yuv = atoi(value) > 0 ? true : false;
    if ( raw_yuv ) {
        rc = addStreamToChannel(pChannel,CAM_STREAM_TYPE_RAW,
                preview_raw_stream_cb_routine,this);
        if ( rc != NO_ERROR ) {
            LOGE("add raw stream failed, ret = %d", __FUNCTION__, rc);
            delete pChannel;
            return rc;
        }
    }

    if (rc != NO_ERROR) {
        LOGE("add preview stream failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    m_channels[QCAMERA_CH_TYPE_PREVIEW] = pChannel;
    return rc;
}

/*===========================================================================
 * FUNCTION   : addVideoChannel
 *
 * DESCRIPTION: add a video channel that contains a video stream
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::addVideoChannel()
{
    int32_t rc = NO_ERROR;
    QCameraVideoChannel *pChannel = NULL;

    if (m_channels[QCAMERA_CH_TYPE_VIDEO] != NULL) {
        // if we had video channel before, delete it first
        delete m_channels[QCAMERA_CH_TYPE_VIDEO];
        m_channels[QCAMERA_CH_TYPE_VIDEO] = NULL;
    }

    pChannel = new QCameraVideoChannel(mCameraHandle->camera_handle,
                                       mCameraHandle->ops);
    if (NULL == pChannel) {
        LOGE("no mem for video channel");
        return NO_MEMORY;
    }

    if (isLowPowerMode()) {
        mm_camera_channel_attr_t attr;
        memset(&attr, 0, sizeof(mm_camera_channel_attr_t));
        attr.notify_mode = MM_CAMERA_SUPER_BUF_NOTIFY_BURST;
        attr.look_back = 0; //wait for future frame for liveshot
        attr.post_frame_skip = mParameters.getZSLBurstInterval();
        attr.water_mark = 1; //hold min buffers possible in Q
        attr.max_unmatched_frames = mParameters.getMaxUnmatchedFramesInQueue();
        rc = pChannel->init(&attr, snapshot_channel_cb_routine, this);
    } else {
        // preview only channel, don't need bundle attr and cb
        rc = pChannel->init(NULL, NULL, NULL);
    }

    if (rc != 0) {
        LOGE("init video channel failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_VIDEO,
                            video_stream_cb_routine, this);
    if (rc != NO_ERROR) {
        LOGE("add video stream failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    m_channels[QCAMERA_CH_TYPE_VIDEO] = pChannel;
    return rc;
}

/*===========================================================================
 * FUNCTION   : addSnapshotChannel
 *
 * DESCRIPTION: add a snapshot channel that contains a snapshot stream
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 * NOTE       : Add this channel for live snapshot usecase. Regular capture will
 *              use addCaptureChannel.
 *==========================================================================*/
int32_t QCamera2HardwareInterface::addSnapshotChannel()
{
    int32_t rc = NO_ERROR;
    QCameraChannel *pChannel = NULL;

    if (m_channels[QCAMERA_CH_TYPE_SNAPSHOT] != NULL) {
        // if we had ZSL channel before, delete it first
        delete m_channels[QCAMERA_CH_TYPE_SNAPSHOT];
        m_channels[QCAMERA_CH_TYPE_SNAPSHOT] = NULL;
    }

    pChannel = new QCameraChannel(mCameraHandle->camera_handle,
                                  mCameraHandle->ops);
    if (NULL == pChannel) {
        LOGE("no mem for snapshot channel");
        return NO_MEMORY;
    }

    mm_camera_channel_attr_t attr;
    memset(&attr, 0, sizeof(mm_camera_channel_attr_t));
    attr.notify_mode = MM_CAMERA_SUPER_BUF_NOTIFY_CONTINUOUS;
    attr.look_back = 0; //wait for future frame for liveshot
    attr.post_frame_skip = mParameters.getZSLBurstInterval();
    attr.water_mark = 1; //hold min buffers possible in Q
    attr.max_unmatched_frames = mParameters.getMaxUnmatchedFramesInQueue();
    attr.priority = MM_CAMERA_SUPER_BUF_PRIORITY_LOW;
    rc = pChannel->init(&attr, snapshot_channel_cb_routine, this);
    if (rc != NO_ERROR) {
        LOGE("init snapshot channel failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_SNAPSHOT,
            NULL, NULL);
    if (rc != NO_ERROR) {
        LOGE("add snapshot stream failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    m_channels[QCAMERA_CH_TYPE_SNAPSHOT] = pChannel;
    return rc;
}

/*===========================================================================
 * FUNCTION   : addRawChannel
 *
 * DESCRIPTION: add a raw channel that contains a raw image stream
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::addRawChannel()
{
    int32_t rc = NO_ERROR;
    QCameraChannel *pChannel = NULL;

    if (m_channels[QCAMERA_CH_TYPE_RAW] != NULL) {
        // if we had raw channel before, delete it first
        delete m_channels[QCAMERA_CH_TYPE_RAW];
        m_channels[QCAMERA_CH_TYPE_RAW] = NULL;
    }

    pChannel = new QCameraChannel(mCameraHandle->camera_handle,
                                  mCameraHandle->ops);
    if (NULL == pChannel) {
        LOGE("no mem for raw channel");
        return NO_MEMORY;
    }

    if (mParameters.getofflineRAW()) {
        mm_camera_channel_attr_t attr;
        memset(&attr, 0, sizeof(mm_camera_channel_attr_t));
        attr.notify_mode = MM_CAMERA_SUPER_BUF_NOTIFY_BURST;
        attr.look_back = mParameters.getZSLBackLookCount();
        attr.post_frame_skip = mParameters.getZSLBurstInterval();
        attr.water_mark = 1;
        attr.max_unmatched_frames = mParameters.getMaxUnmatchedFramesInQueue();
        rc = pChannel->init(&attr, raw_channel_cb_routine, this);
        if (rc != NO_ERROR) {
            LOGE("init RAW channel failed, ret = %d", rc);
            delete pChannel;
            return rc;
        }
    } else {
        rc = pChannel->init(NULL, NULL, NULL);
        if (rc != NO_ERROR) {
            LOGE("init raw channel failed, ret = %d", rc);
            delete pChannel;
            return rc;
        }
    }

    if (!mParameters.isZSLMode()) {
        // meta data stream always coexists with snapshot in regular RAW capture case
        rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_METADATA,
                metadata_stream_cb_routine, this);
        if (rc != NO_ERROR) {
            LOGE("add metadata stream failed, ret = %d", rc);
            delete pChannel;
            return rc;
        }
    }

    if (mParameters.getofflineRAW()) {
        rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_RAW,
                NULL, this);
    } else {
        rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_RAW,
                raw_stream_cb_routine, this);
    }
    if (rc != NO_ERROR) {
        LOGE("add snapshot stream failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }
    m_channels[QCAMERA_CH_TYPE_RAW] = pChannel;
    return rc;
}

/*===========================================================================
 * FUNCTION   : addZSLChannel
 *
 * DESCRIPTION: add a ZSL channel that contains a preview stream and
 *              a snapshot stream
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::addZSLChannel()
{
    int32_t rc = NO_ERROR;
    QCameraPicChannel *pChannel = NULL;
    char value[PROPERTY_VALUE_MAX];
    bool raw_yuv = false;

    if (m_channels[QCAMERA_CH_TYPE_ZSL] != NULL) {
        // if we had ZSL channel before, delete it first
        delete m_channels[QCAMERA_CH_TYPE_ZSL];
        m_channels[QCAMERA_CH_TYPE_ZSL] = NULL;
    }

    pChannel = new QCameraPicChannel(mCameraHandle->camera_handle,
                                     mCameraHandle->ops);
    if (NULL == pChannel) {
        LOGE("no mem for ZSL channel");
        return NO_MEMORY;
    }

    // ZSL channel, init with bundle attr and cb
    mm_camera_channel_attr_t attr;
    memset(&attr, 0, sizeof(mm_camera_channel_attr_t));
    if (mParameters.isSceneSelectionEnabled()) {
        attr.notify_mode = MM_CAMERA_SUPER_BUF_NOTIFY_CONTINUOUS;
    } else {
        attr.notify_mode = MM_CAMERA_SUPER_BUF_NOTIFY_BURST;
    }
    attr.look_back = mParameters.getZSLBackLookCount();
    attr.post_frame_skip = mParameters.getZSLBurstInterval();
    if (mParameters.isOEMFeatEnabled()) {
        attr.post_frame_skip++;
    }
    attr.water_mark = mParameters.getZSLQueueDepth();
    attr.max_unmatched_frames = mParameters.getMaxUnmatchedFramesInQueue();
    attr.user_expected_frame_id =
        mParameters.isInstantCaptureEnabled() ? (uint8_t)mParameters.getAecFrameBoundValue() : 0;

    //Enabled matched queue
    if (isFrameSyncEnabled()) {
        LOGH("Enabling frame sync for dual camera, camera Id: %d",
                 mCameraId);
        attr.enable_frame_sync = 1;
    }
    rc = pChannel->init(&attr,
                        zsl_channel_cb,
                        this);
    if (rc != 0) {
        LOGE("init ZSL channel failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    // meta data stream always coexists with preview if applicable
    rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_METADATA,
                            metadata_stream_cb_routine, this);
    if (rc != NO_ERROR) {
        LOGE("add metadata stream failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    if (isNoDisplayMode()) {
        rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_PREVIEW,
                                nodisplay_preview_stream_cb_routine, this);
    } else {
        rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_PREVIEW,
                                preview_stream_cb_routine, this);
#ifdef TARGET_TS_MAKEUP
        int whiteLevel, cleanLevel;
        if(mParameters.getTsMakeupInfo(whiteLevel, cleanLevel) == false)
#endif
        pChannel->setStreamSyncCB(CAM_STREAM_TYPE_PREVIEW,
                synchronous_stream_cb_routine);
    }
    if (rc != NO_ERROR) {
        LOGE("add preview stream failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_SNAPSHOT,
                            NULL, this);
    if (rc != NO_ERROR) {
        LOGE("add snapshot stream failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    if (!mParameters.isSecureMode()) {
        rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_ANALYSIS,
                NULL, this);
        if (rc != NO_ERROR) {
            LOGE("add Analysis stream failed, ret = %d", rc);
            delete pChannel;
            return rc;
        }
    }

    property_get("persist.camera.raw_yuv", value, "0");
    raw_yuv = atoi(value) > 0 ? true : false;
    if (raw_yuv) {
        rc = addStreamToChannel(pChannel,
                                CAM_STREAM_TYPE_RAW,
                                preview_raw_stream_cb_routine,
                                this);
        if (rc != NO_ERROR) {
            LOGE("add raw stream failed, ret = %d", rc);
            delete pChannel;
            return rc;
        }
    }

    m_channels[QCAMERA_CH_TYPE_ZSL] = pChannel;
    return rc;
}

/*===========================================================================
 * FUNCTION   : addCaptureChannel
 *
 * DESCRIPTION: add a capture channel that contains a snapshot stream
 *              and a postview stream
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 * NOTE       : Add this channel for regular capture usecase.
 *              For Live snapshot usecase, use addSnapshotChannel.
 *==========================================================================*/
int32_t QCamera2HardwareInterface::addCaptureChannel()
{
    int32_t rc = NO_ERROR;
    QCameraPicChannel *pChannel = NULL;
    char value[PROPERTY_VALUE_MAX];
    bool raw_yuv = false;

    if (m_channels[QCAMERA_CH_TYPE_CAPTURE] != NULL) {
        delete m_channels[QCAMERA_CH_TYPE_CAPTURE];
        m_channels[QCAMERA_CH_TYPE_CAPTURE] = NULL;
    }

    pChannel = new QCameraPicChannel(mCameraHandle->camera_handle,
                                  mCameraHandle->ops);
    if (NULL == pChannel) {
        LOGE("no mem for capture channel");
        return NO_MEMORY;
    }

    // Capture channel, only need snapshot and postview streams start together
    mm_camera_channel_attr_t attr;
    memset(&attr, 0, sizeof(mm_camera_channel_attr_t));
    if ( mLongshotEnabled ) {
        attr.notify_mode = MM_CAMERA_SUPER_BUF_NOTIFY_BURST;
        attr.look_back = mParameters.getZSLBackLookCount();
        attr.water_mark = mParameters.getZSLQueueDepth();
    } else {
        attr.notify_mode = MM_CAMERA_SUPER_BUF_NOTIFY_CONTINUOUS;
    }
    attr.max_unmatched_frames = mParameters.getMaxUnmatchedFramesInQueue();

    rc = pChannel->init(&attr,
                        capture_channel_cb_routine,
                        this);
    if (rc != NO_ERROR) {
        LOGE("init capture channel failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    // meta data stream always coexists with snapshot in regular capture case
    rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_METADATA,
                            metadata_stream_cb_routine, this);
    if (rc != NO_ERROR) {
        LOGE("add metadata stream failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    if (mLongshotEnabled) {
        rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_PREVIEW,
                                preview_stream_cb_routine, this);

        if (rc != NO_ERROR) {
            LOGE("add preview stream failed, ret = %d", rc);
            delete pChannel;
            return rc;
        }
#ifdef TARGET_TS_MAKEUP
        int whiteLevel, cleanLevel;
        if(mParameters.getTsMakeupInfo(whiteLevel, cleanLevel) == false)
#endif
        pChannel->setStreamSyncCB(CAM_STREAM_TYPE_PREVIEW,
                synchronous_stream_cb_routine);
    //Not adding the postview stream to the capture channel if Quadra CFA is enabled.
    } else if (!mParameters.getQuadraCfa()) {
        rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_POSTVIEW,
                                NULL, this);

        if (rc != NO_ERROR) {
            LOGE("add postview stream failed, ret = %d", rc);
            delete pChannel;
            return rc;
        }
    }

    if (!mParameters.getofflineRAW()) {
        rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_SNAPSHOT,
                NULL, this);
        if (rc != NO_ERROR) {
            LOGE("add snapshot stream failed, ret = %d", rc);
            delete pChannel;
            return rc;
        }
    }

    stream_cb_routine stream_cb = NULL;
    property_get("persist.camera.raw_yuv", value, "0");
    raw_yuv = atoi(value) > 0 ? true : false;

    if (raw_yuv) {
        stream_cb = snapshot_raw_stream_cb_routine;
    }

    if ((raw_yuv) || (mParameters.getofflineRAW())) {
        rc = addStreamToChannel(pChannel,
                CAM_STREAM_TYPE_RAW, stream_cb, this);
        if (rc != NO_ERROR) {
            LOGE("add raw stream failed, ret = %d", rc);
            delete pChannel;
            return rc;
        }
    }

    m_channels[QCAMERA_CH_TYPE_CAPTURE] = pChannel;
    return rc;
}

/*===========================================================================
 * FUNCTION   : addMetaDataChannel
 *
 * DESCRIPTION: add a meta data channel that contains a metadata stream
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::addMetaDataChannel()
{
    int32_t rc = NO_ERROR;
    QCameraChannel *pChannel = NULL;

    if (m_channels[QCAMERA_CH_TYPE_METADATA] != NULL) {
        delete m_channels[QCAMERA_CH_TYPE_METADATA];
        m_channels[QCAMERA_CH_TYPE_METADATA] = NULL;
    }

    pChannel = new QCameraChannel(mCameraHandle->camera_handle,
                                  mCameraHandle->ops);
    if (NULL == pChannel) {
        LOGE("no mem for metadata channel");
        return NO_MEMORY;
    }

    rc = pChannel->init(NULL,
                        NULL,
                        NULL);
    if (rc != NO_ERROR) {
        LOGE("init metadata channel failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_METADATA,
                            metadata_stream_cb_routine, this);
    if (rc != NO_ERROR) {
        LOGE("add metadata stream failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    m_channels[QCAMERA_CH_TYPE_METADATA] = pChannel;
    return rc;
}

/*===========================================================================
 * FUNCTION   : addCallbackChannel
 *
 * DESCRIPTION: add a callback channel that contains a callback stream
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::addCallbackChannel()
{
    int32_t rc = NO_ERROR;
    QCameraChannel *pChannel = NULL;

    if (m_channels[QCAMERA_CH_TYPE_CALLBACK] != NULL) {
        delete m_channels[QCAMERA_CH_TYPE_CALLBACK];
        m_channels[QCAMERA_CH_TYPE_CALLBACK] = NULL;
    }

    pChannel = new QCameraChannel(mCameraHandle->camera_handle,
            mCameraHandle->ops);
    if (NULL == pChannel) {
        LOGE("no mem for callback channel");
        return NO_MEMORY;
    }

    rc = pChannel->init(NULL, NULL, this);
    if (rc != NO_ERROR) {
        LOGE("init callback channel failed, ret = %d",
                 rc);
        delete pChannel;
        return rc;
    }

    rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_CALLBACK,
            callback_stream_cb_routine, this);
    if (rc != NO_ERROR) {
        LOGE("add callback stream failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    m_channels[QCAMERA_CH_TYPE_CALLBACK] = pChannel;
    return rc;
}


/*===========================================================================
 * FUNCTION   : addAnalysisChannel
 *
 * DESCRIPTION: add a analysis channel that contains a analysis stream
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::addAnalysisChannel()
{
    int32_t rc = NO_ERROR;
    QCameraChannel *pChannel = NULL;

    if (m_channels[QCAMERA_CH_TYPE_ANALYSIS] != NULL) {
        delete m_channels[QCAMERA_CH_TYPE_ANALYSIS];
        m_channels[QCAMERA_CH_TYPE_ANALYSIS] = NULL;
    }

    pChannel = new QCameraChannel(mCameraHandle->camera_handle,
                                  mCameraHandle->ops);
    if (NULL == pChannel) {
        LOGE("no mem for metadata channel");
        return NO_MEMORY;
    }

    rc = pChannel->init(NULL, NULL, this);
    if (rc != NO_ERROR) {
        LOGE("init Analysis channel failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    rc = addStreamToChannel(pChannel, CAM_STREAM_TYPE_ANALYSIS,
                            NULL, this);
    if (rc != NO_ERROR) {
        LOGE("add Analysis stream failed, ret = %d", rc);
        delete pChannel;
        return rc;
    }

    m_channels[QCAMERA_CH_TYPE_ANALYSIS] = pChannel;
    return rc;
}


/*===========================================================================
 * FUNCTION   : getPPConfig
 *
 * DESCRIPTION: get Post processing configaration data
 *
 * PARAMETERS :
 * @pp config:  pp config structure pointer,
 * @curIndex:  current pp channel index
 * @multipass: Flag if multipass prcessing enabled.
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::getPPConfig(cam_pp_feature_config_t &pp_config,
        int8_t curIndex, bool multipass)
{
    int32_t rc = NO_ERROR;
    int32_t feature_set = 0;

    if (multipass) {
        LOGW("Multi pass enabled. Total Pass = %d, cur index = %d",
                mParameters.getReprocCount(), curIndex);
    }

    LOGH("Supported pproc feature mask = %llx",
            gCamCapability[mCameraId]->qcom_supported_feature_mask);
    cam_feature_mask_t feature_mask = gCamCapability[mCameraId]->qcom_supported_feature_mask;
    int32_t zoomLevel = mParameters.getParmZoomLevel();
    uint32_t rotation = mParameters.getJpegRotation();
    int32_t effect = mParameters.getEffectValue();

    pp_config.cur_reproc_count = curIndex + 1;
    pp_config.total_reproc_count = mParameters.getReprocCount();

    //Checking what feature mask to enable
    if (curIndex == 0) {
        if (mParameters.getQuadraCfa()) {
            feature_set = 2;
        } else {
            feature_set = 0;
        }
    } else if (curIndex == 1) {
        if (mParameters.getQuadraCfa()) {
            feature_set = 0;
        } else {
            feature_set = 1;
        }
    }

    switch(feature_set) {
        case 0:
            //Configure feature mask for first pass of reprocessing
            //check if any effects are enabled
            if ((CAM_EFFECT_MODE_OFF != effect) &&
                (feature_mask & CAM_QCOM_FEATURE_EFFECT)) {
                pp_config.feature_mask |= CAM_QCOM_FEATURE_EFFECT;
                pp_config.effect = effect;
            }

            //check for features that need to be enabled by default like sharpness
            //(if supported by hw).
            if ((feature_mask & CAM_QCOM_FEATURE_SHARPNESS) &&
                !mParameters.isOptiZoomEnabled()) {
                pp_config.feature_mask |= CAM_QCOM_FEATURE_SHARPNESS;
                pp_config.sharpness = mParameters.getSharpness();
            }

            //check if zoom is enabled
            if ((zoomLevel > 0) && (feature_mask & CAM_QCOM_FEATURE_CROP)) {
                pp_config.feature_mask |= CAM_QCOM_FEATURE_CROP;
            }

            if (mParameters.isWNREnabled() &&
                (feature_mask & CAM_QCOM_FEATURE_DENOISE2D)) {
                pp_config.feature_mask |= CAM_QCOM_FEATURE_DENOISE2D;
                pp_config.denoise2d.denoise_enable = 1;
                pp_config.denoise2d.process_plates =
                        mParameters.getDenoiseProcessPlate(CAM_INTF_PARM_WAVELET_DENOISE);
            }

            if (isCACEnabled()) {
                pp_config.feature_mask |= CAM_QCOM_FEATURE_CAC;
            }

            //check if rotation is required
            if ((feature_mask & CAM_QCOM_FEATURE_ROTATION) && (rotation > 0)) {
                pp_config.feature_mask |= CAM_QCOM_FEATURE_ROTATION;
                if (rotation == 0) {
                    pp_config.rotation = ROTATE_0;
                } else if (rotation == 90) {
                    pp_config.rotation = ROTATE_90;
                } else if (rotation == 180) {
                    pp_config.rotation = ROTATE_180;
                } else if (rotation == 270) {
                    pp_config.rotation = ROTATE_270;
                }
            }

            if (mParameters.isHDREnabled()){
                pp_config.feature_mask |= CAM_QCOM_FEATURE_HDR;
                pp_config.hdr_param.hdr_enable = 1;
                pp_config.hdr_param.hdr_need_1x = mParameters.isHDR1xFrameEnabled();
                pp_config.hdr_param.hdr_mode = CAM_HDR_MODE_MULTIFRAME;
            } else {
                pp_config.feature_mask &= ~CAM_QCOM_FEATURE_HDR;
                pp_config.hdr_param.hdr_enable = 0;
            }

            //check if scaling is enabled
            if ((feature_mask & CAM_QCOM_FEATURE_SCALE) &&
                mParameters.isReprocScaleEnabled() &&
                mParameters.isUnderReprocScaling()){
                pp_config.feature_mask |= CAM_QCOM_FEATURE_SCALE;
                mParameters.getPicSizeFromAPK(
                        pp_config.scale_param.output_width,
                        pp_config.scale_param.output_height);
            }

            if(mParameters.isUbiFocusEnabled()) {
                pp_config.feature_mask |= CAM_QCOM_FEATURE_UBIFOCUS;
            } else {
                pp_config.feature_mask &= ~CAM_QCOM_FEATURE_UBIFOCUS;
            }

            if(mParameters.isUbiRefocus()) {
                pp_config.feature_mask |= CAM_QCOM_FEATURE_REFOCUS;
                pp_config.misc_buf_param.misc_buffer_index = 0;
            } else {
                pp_config.feature_mask &= ~CAM_QCOM_FEATURE_REFOCUS;
            }

            if(mParameters.isChromaFlashEnabled()) {
                pp_config.feature_mask |= CAM_QCOM_FEATURE_CHROMA_FLASH;
                pp_config.flash_value = CAM_FLASH_ON;
            } else {
                pp_config.feature_mask &= ~CAM_QCOM_FEATURE_CHROMA_FLASH;
            }

            if(mParameters.isOptiZoomEnabled() && (0 <= zoomLevel)) {
                pp_config.feature_mask |= CAM_QCOM_FEATURE_OPTIZOOM;
                pp_config.zoom_level = (uint8_t) zoomLevel;
            } else {
                pp_config.feature_mask &= ~CAM_QCOM_FEATURE_OPTIZOOM;
            }

            if (mParameters.getofflineRAW()) {
                pp_config.feature_mask |= CAM_QCOM_FEATURE_RAW_PROCESSING;
            }

            if (mParameters.isTruePortraitEnabled()) {
                pp_config.feature_mask |= CAM_QCOM_FEATURE_TRUEPORTRAIT;
                pp_config.misc_buf_param.misc_buffer_index = 0;
            } else {
                pp_config.feature_mask &= ~CAM_QCOM_FEATURE_TRUEPORTRAIT;
            }

            if(mParameters.isStillMoreEnabled()) {
                pp_config.feature_mask |= CAM_QCOM_FEATURE_STILLMORE;
            } else {
                pp_config.feature_mask &= ~CAM_QCOM_FEATURE_STILLMORE;
            }

            if (mParameters.isOEMFeatEnabled()) {
                pp_config.feature_mask |= CAM_OEM_FEATURE_1;
            }

            if (mParameters.getCDSMode() != CAM_CDS_MODE_OFF) {
                if (feature_mask & CAM_QCOM_FEATURE_DSDN) {
                    pp_config.feature_mask |= CAM_QCOM_FEATURE_DSDN;
                } else {
                    pp_config.feature_mask |= CAM_QCOM_FEATURE_CDS;
                }
            }

            if ((multipass) &&
                    (m_postprocessor.getPPChannelCount() > 1)
                    && (!mParameters.getQuadraCfa())) {
                pp_config.feature_mask &= ~CAM_QCOM_FEATURE_PP_PASS_2;
                pp_config.feature_mask &= ~CAM_QCOM_FEATURE_ROTATION;
                pp_config.feature_mask &= ~CAM_QCOM_FEATURE_CDS;
                pp_config.feature_mask &= ~CAM_QCOM_FEATURE_DSDN;
                pp_config.feature_mask |= CAM_QCOM_FEATURE_CROP;
            } else {
                pp_config.feature_mask |= CAM_QCOM_FEATURE_SCALE;
            }

            cam_dimension_t thumb_src_dim;
            cam_dimension_t thumb_dst_dim;
            mParameters.getThumbnailSize(&(thumb_dst_dim.width), &(thumb_dst_dim.height));
            mParameters.getStreamDimension(CAM_STREAM_TYPE_POSTVIEW,thumb_src_dim);
            if ((thumb_dst_dim.width != thumb_src_dim.width) ||
                    (thumb_dst_dim.height != thumb_src_dim.height)) {
                if (thumb_dst_dim.width != 0 && thumb_dst_dim.height != 0) {
                    pp_config.feature_mask |= CAM_QCOM_FEATURE_CROP;
                }
            }

            break;

        case 1:
            //Configure feature mask for second pass of reprocessing
            pp_config.feature_mask |= CAM_QCOM_FEATURE_PP_PASS_2;
            if ((feature_mask & CAM_QCOM_FEATURE_ROTATION) && (rotation > 0)) {
                pp_config.feature_mask |= CAM_QCOM_FEATURE_ROTATION;
                if (rotation == 0) {
                    pp_config.rotation = ROTATE_0;
                } else if (rotation == 90) {
                    pp_config.rotation = ROTATE_90;
                } else if (rotation == 180) {
                    pp_config.rotation = ROTATE_180;
                } else if (rotation == 270) {
                    pp_config.rotation = ROTATE_270;
                }
            }
            if (mParameters.getCDSMode() != CAM_CDS_MODE_OFF) {
                if (feature_mask & CAM_QCOM_FEATURE_DSDN) {
                    pp_config.feature_mask |= CAM_QCOM_FEATURE_DSDN;
                } else {
                    pp_config.feature_mask |= CAM_QCOM_FEATURE_CDS;
                }
            }
            pp_config.feature_mask &= ~CAM_QCOM_FEATURE_RAW_PROCESSING;
            pp_config.feature_mask &= ~CAM_QCOM_FEATURE_METADATA_PROCESSING;
            pp_config.feature_mask &= ~CAM_QCOM_FEATURE_METADATA_BYPASS;
            break;

        case 2:
            //Setting feature for Quadra CFA
            pp_config.feature_mask |= CAM_QCOM_FEATURE_QUADRA_CFA;
            break;

    }

    LOGH("pproc feature mask set = %llx pass count = %d",
        pp_config.feature_mask, curIndex);
    return rc;
}

/*===========================================================================
 * FUNCTION   : addReprocChannel
 *
 * DESCRIPTION: add a reprocess channel that will do reprocess on frames
 *              coming from input channel
 *
 * PARAMETERS :
 *   @pInputChannel : ptr to input channel whose frames will be post-processed
 *   @cur_channel_index : Current channel index in multipass
 *
 * RETURN     : Ptr to the newly created channel obj. NULL if failed.
 *==========================================================================*/
QCameraReprocessChannel *QCamera2HardwareInterface::addReprocChannel(
        QCameraChannel *pInputChannel, int8_t cur_channel_index)
{
    int32_t rc = NO_ERROR;
    QCameraReprocessChannel *pChannel = NULL;
    uint32_t burst_cnt = mParameters.getNumOfSnapshots();

    if (pInputChannel == NULL) {
        LOGE("input channel obj is NULL");
        return NULL;
    }

    pChannel = new QCameraReprocessChannel(mCameraHandle->camera_handle,
                                           mCameraHandle->ops);
    if (NULL == pChannel) {
        LOGE("no mem for reprocess channel");
        return NULL;
    }

    // Capture channel, only need snapshot and postview streams start together
    mm_camera_channel_attr_t attr;
    memset(&attr, 0, sizeof(mm_camera_channel_attr_t));
    attr.notify_mode = MM_CAMERA_SUPER_BUF_NOTIFY_CONTINUOUS;
    attr.max_unmatched_frames = mParameters.getMaxUnmatchedFramesInQueue();
    rc = pChannel->init(&attr,
                        postproc_channel_cb_routine,
                        this);
    if (rc != NO_ERROR) {
        LOGE("init reprocess channel failed, ret = %d", rc);
        delete pChannel;
        return NULL;
    }

    // pp feature config
    cam_pp_feature_config_t pp_config;
    memset(&pp_config, 0, sizeof(cam_pp_feature_config_t));

    rc = getPPConfig(pp_config, cur_channel_index,
            ((mParameters.getReprocCount() > 1) ? TRUE : FALSE));
    if (rc != NO_ERROR){
        LOGE("Error while creating PP config");
        delete pChannel;
        return NULL;
    }

    uint8_t minStreamBufNum = getBufNumRequired(CAM_STREAM_TYPE_OFFLINE_PROC);

    //WNR and HDR happen inline. No extra buffers needed.
    cam_feature_mask_t temp_feature_mask = pp_config.feature_mask;
    temp_feature_mask &= ~CAM_QCOM_FEATURE_HDR;
    if (temp_feature_mask && mParameters.isHDREnabled()) {
        minStreamBufNum = (uint8_t)(1 + mParameters.getNumOfExtraHDRInBufsIfNeeded());
    }

    if (mParameters.isStillMoreEnabled()) {
        cam_still_more_t stillmore_config = mParameters.getStillMoreSettings();
        pp_config.burst_cnt = stillmore_config.burst_count;
        LOGH("Stillmore burst %d", pp_config.burst_cnt);

        // getNumOfExtraBuffersForImageProc returns 1 less buffer assuming
        // number of capture is already added. In the case of liveshot,
        // stillmore burst is 1. This is to account for the premature decrement
        if (mParameters.getNumOfExtraBuffersForImageProc() == 0) {
            minStreamBufNum += 1;
        }
    }

    if (mParameters.getManualCaptureMode() >= CAM_MANUAL_CAPTURE_TYPE_3) {
        minStreamBufNum += mParameters.getReprocCount() - 1;
        burst_cnt = mParameters.getReprocCount();
        if (cur_channel_index == 0) {
            pChannel->setReprocCount(2);
        } else {
            pChannel->setReprocCount(1);
        }
    } else {
        pChannel->setReprocCount(1);
    }

    // Add non inplace image lib buffers only when ppproc is present,
    // becuase pproc is non inplace and input buffers for img lib
    // are output for pproc and this number of extra buffers is required
    // If pproc is not there, input buffers for imglib are from snapshot stream
    uint8_t imglib_extra_bufs = mParameters.getNumOfExtraBuffersForImageProc();
    if (temp_feature_mask && imglib_extra_bufs) {
        // 1 is added because getNumOfExtraBuffersForImageProc returns extra
        // buffers assuming number of capture is already added
        minStreamBufNum = (uint8_t)(minStreamBufNum + imglib_extra_bufs + 1);
    }

    //Mask out features that are already processed in snapshot stream.
    cam_feature_mask_t snapshot_feature_mask = 0;
    mParameters.getStreamPpMask(CAM_STREAM_TYPE_SNAPSHOT, snapshot_feature_mask);

    pp_config.feature_mask &= ~snapshot_feature_mask;
    LOGH("Snapshot feature mask: 0x%llx, reproc feature mask: 0x%llx",
            snapshot_feature_mask, pp_config.feature_mask);

    bool offlineReproc = isRegularCapture();
    if (m_postprocessor.mOfflineDataBufs != NULL) {
        offlineReproc = TRUE;
    }

    cam_padding_info_t paddingInfo = gCamCapability[mCameraId]->padding_info;
    paddingInfo.offset_info.offset_x = 0;
    paddingInfo.offset_info.offset_y = 0;
    rc = pChannel->addReprocStreamsFromSource(*this,
                                              pp_config,
                                              pInputChannel,
                                              minStreamBufNum,
                                              burst_cnt,
                                              &paddingInfo,
                                              mParameters,
                                              mLongshotEnabled,
                                              offlineReproc);
    if (rc != NO_ERROR) {
        delete pChannel;
        return NULL;
    }

    return pChannel;
}

/*===========================================================================
 * FUNCTION   : addOfflineReprocChannel
 *
 * DESCRIPTION: add a offline reprocess channel contains one reproc stream,
 *              that will do reprocess on frames coming from external images
 *
 * PARAMETERS :
 *   @img_config  : offline reporcess image info
 *   @pp_feature  : pp feature config
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
QCameraReprocessChannel *QCamera2HardwareInterface::addOfflineReprocChannel(
                                            cam_pp_offline_src_config_t &img_config,
                                            cam_pp_feature_config_t &pp_feature,
                                            stream_cb_routine stream_cb,
                                            void *userdata)
{
    int32_t rc = NO_ERROR;
    QCameraReprocessChannel *pChannel = NULL;

    pChannel = new QCameraReprocessChannel(mCameraHandle->camera_handle,
                                           mCameraHandle->ops);
    if (NULL == pChannel) {
        LOGE("no mem for reprocess channel");
        return NULL;
    }

    rc = pChannel->init(NULL, NULL, NULL);
    if (rc != NO_ERROR) {
        LOGE("init reprocess channel failed, ret = %d", rc);
        delete pChannel;
        return NULL;
    }

    QCameraHeapMemory *pStreamInfo = allocateStreamInfoBuf(CAM_STREAM_TYPE_OFFLINE_PROC);
    if (pStreamInfo == NULL) {
        LOGE("no mem for stream info buf");
        delete pChannel;
        return NULL;
    }

    cam_stream_info_t *streamInfoBuf = (cam_stream_info_t *)pStreamInfo->getPtr(0);
    memset(streamInfoBuf, 0, sizeof(cam_stream_info_t));
    streamInfoBuf->stream_type = CAM_STREAM_TYPE_OFFLINE_PROC;
    streamInfoBuf->fmt = img_config.input_fmt;
    streamInfoBuf->dim = img_config.input_dim;
    streamInfoBuf->buf_planes = img_config.input_buf_planes;
    streamInfoBuf->streaming_mode = CAM_STREAMING_MODE_BURST;
    streamInfoBuf->num_of_burst = img_config.num_of_bufs;

    streamInfoBuf->reprocess_config.pp_type = CAM_OFFLINE_REPROCESS_TYPE;
    streamInfoBuf->reprocess_config.offline = img_config;
    streamInfoBuf->reprocess_config.pp_feature_config = pp_feature;

    rc = pChannel->addStream(*this,
            pStreamInfo, NULL, img_config.num_of_bufs,
            &gCamCapability[mCameraId]->padding_info,
            stream_cb, userdata, false);

    if (rc != NO_ERROR) {
        LOGE("add reprocess stream failed, ret = %d", rc);
        delete pChannel;
        return NULL;
    }

    return pChannel;
}

/*===========================================================================
 * FUNCTION   : addChannel
 *
 * DESCRIPTION: add a channel by its type
 *
 * PARAMETERS :
 *   @ch_type : channel type
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::addChannel(qcamera_ch_type_enum_t ch_type)
{
    int32_t rc = UNKNOWN_ERROR;
    switch (ch_type) {
    case QCAMERA_CH_TYPE_ZSL:
        rc = addZSLChannel();
        break;
    case QCAMERA_CH_TYPE_CAPTURE:
        rc = addCaptureChannel();
        break;
    case QCAMERA_CH_TYPE_PREVIEW:
        rc = addPreviewChannel();
        break;
    case QCAMERA_CH_TYPE_VIDEO:
        rc = addVideoChannel();
        break;
    case QCAMERA_CH_TYPE_SNAPSHOT:
        rc = addSnapshotChannel();
        break;
    case QCAMERA_CH_TYPE_RAW:
        rc = addRawChannel();
        break;
    case QCAMERA_CH_TYPE_METADATA:
        rc = addMetaDataChannel();
        break;
    case QCAMERA_CH_TYPE_CALLBACK:
        rc = addCallbackChannel();
        break;
    case QCAMERA_CH_TYPE_ANALYSIS:
        rc = addAnalysisChannel();
        break;
    default:
        break;
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : delChannel
 *
 * DESCRIPTION: delete a channel by its type
 *
 * PARAMETERS :
 *   @ch_type : channel type
 *   @destroy : delete context as well
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::delChannel(qcamera_ch_type_enum_t ch_type,
                                              bool destroy)
{
    if (m_channels[ch_type] != NULL) {
        if (destroy) {
            delete m_channels[ch_type];
            m_channels[ch_type] = NULL;
        } else {
            m_channels[ch_type]->deleteChannel();
        }
    }

    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : startChannel
 *
 * DESCRIPTION: start a channel by its type
 *
 * PARAMETERS :
 *   @ch_type : channel type
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::startChannel(qcamera_ch_type_enum_t ch_type)
{
    int32_t rc = UNKNOWN_ERROR;
    if (m_channels[ch_type] != NULL) {
        rc = m_channels[ch_type]->start();
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : stopChannel
 *
 * DESCRIPTION: stop a channel by its type
 *
 * PARAMETERS :
 *   @ch_type : channel type
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::stopChannel(qcamera_ch_type_enum_t ch_type)
{
    int32_t rc = UNKNOWN_ERROR;
    if (m_channels[ch_type] != NULL) {
        rc = m_channels[ch_type]->stop();
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : preparePreview
 *
 * DESCRIPTION: add channels needed for preview
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::preparePreview()
{
    ATRACE_CALL();
    int32_t rc = NO_ERROR;

    LOGI("E");
    rc = mParameters.setStreamConfigure(false, false, false, sessionId);
    if (rc != NO_ERROR) {
        LOGE("setStreamConfigure failed %d", rc);
        return rc;
    }

    if (mParameters.isZSLMode() && mParameters.getRecordingHintValue() != true) {
        rc = addChannel(QCAMERA_CH_TYPE_ZSL);
        if (rc != NO_ERROR) {
            LOGE("failed!! rc = %d", rc);
            return rc;
        }

        if (mParameters.isUBWCEnabled()) {
            cam_format_t fmt;
            mParameters.getStreamFormat(CAM_STREAM_TYPE_PREVIEW, fmt);
            if (fmt == CAM_FORMAT_YUV_420_NV12_UBWC) {
                rc = addChannel(QCAMERA_CH_TYPE_CALLBACK);
                if (rc != NO_ERROR) {
                    delChannel(QCAMERA_CH_TYPE_ZSL);
                    LOGE("failed!! rc = %d", rc);
                    return rc;
                }
            }
        }

        if (mParameters.getofflineRAW() && !mParameters.getQuadraCfa()) {
            addChannel(QCAMERA_CH_TYPE_RAW);
        }
    } else {
        bool recordingHint = mParameters.getRecordingHintValue();
        if(!isRdiMode() && recordingHint) {
            //stop face detection,longshot,etc if turned ON in Camera mode
#ifndef VANILLA_HAL
            int32_t arg; //dummy arg
            if (isLongshotEnabled()) {
                sendCommand(CAMERA_CMD_LONGSHOT_OFF, arg, arg);
            }
            if (mParameters.isFaceDetectionEnabled()
                    && (!mParameters.fdModeInVideo())) {
                sendCommand(CAMERA_CMD_STOP_FACE_DETECTION, arg, arg);
            }
            if (mParameters.isHistogramEnabled()) {
                sendCommand(CAMERA_CMD_HISTOGRAM_OFF, arg, arg);
            }
#endif
            //Don't create snapshot channel for liveshot, if low power mode is set.
            //Use video stream instead.
            if (!isLowPowerMode()) {
               rc = addChannel(QCAMERA_CH_TYPE_SNAPSHOT);
               if (rc != NO_ERROR) {
                   return rc;
               }
            }

            rc = addChannel(QCAMERA_CH_TYPE_VIDEO);
            if (rc != NO_ERROR) {
                delChannel(QCAMERA_CH_TYPE_SNAPSHOT);
                LOGE("failed!! rc = %d", rc);
                return rc;
            }
        }

        rc = addChannel(QCAMERA_CH_TYPE_PREVIEW);
        if (!isRdiMode() && (rc != NO_ERROR)) {
            if (recordingHint) {
                delChannel(QCAMERA_CH_TYPE_SNAPSHOT);
                delChannel(QCAMERA_CH_TYPE_VIDEO);
            }
        }

        if (mParameters.isUBWCEnabled() && !recordingHint) {
            cam_format_t fmt;
            mParameters.getStreamFormat(CAM_STREAM_TYPE_PREVIEW, fmt);
            if (fmt == CAM_FORMAT_YUV_420_NV12_UBWC) {
                rc = addChannel(QCAMERA_CH_TYPE_CALLBACK);
                if (rc != NO_ERROR) {
                    delChannel(QCAMERA_CH_TYPE_PREVIEW);
                    if (!isRdiMode()) {
                        delChannel(QCAMERA_CH_TYPE_SNAPSHOT);
                        delChannel(QCAMERA_CH_TYPE_VIDEO);
                    }
                    LOGE("failed!! rc = %d", rc);
                    return rc;
                }
            }
        }

        if (NO_ERROR != rc) {
            delChannel(QCAMERA_CH_TYPE_PREVIEW);
            LOGE("failed!! rc = %d", rc);
        }
    }

    LOGI("X rc = %d", rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : unpreparePreview
 *
 * DESCRIPTION: delete channels for preview
 *
 * PARAMETERS : none
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::unpreparePreview()
{
    delChannel(QCAMERA_CH_TYPE_ZSL);
    delChannel(QCAMERA_CH_TYPE_PREVIEW);
    delChannel(QCAMERA_CH_TYPE_VIDEO);
    delChannel(QCAMERA_CH_TYPE_SNAPSHOT);
    delChannel(QCAMERA_CH_TYPE_CALLBACK);
    delChannel(QCAMERA_CH_TYPE_RAW);
}

/*===========================================================================
 * FUNCTION   : playShutter
 *
 * DESCRIPTION: send request to play shutter sound
 *
 * PARAMETERS : none
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::playShutter(){
     if (mNotifyCb == NULL ||
         msgTypeEnabledWithLock(CAMERA_MSG_SHUTTER) == 0){
         LOGD("shutter msg not enabled or NULL cb");
         return;
     }
     LOGH("CAMERA_MSG_SHUTTER ");
     qcamera_callback_argm_t cbArg;
     memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
     cbArg.cb_type = QCAMERA_NOTIFY_CALLBACK;
     cbArg.msg_type = CAMERA_MSG_SHUTTER;
     cbArg.ext1 = 0;
     cbArg.ext2 = false;
     m_cbNotifier.notifyCallback(cbArg);
}

/*===========================================================================
 * FUNCTION   : getChannelByHandle
 *
 * DESCRIPTION: return a channel by its handle
 *
 * PARAMETERS :
 *   @channelHandle : channel handle
 *
 * RETURN     : a channel obj if found, NULL if not found
 *==========================================================================*/
QCameraChannel *QCamera2HardwareInterface::getChannelByHandle(uint32_t channelHandle)
{
    for(int i = 0; i < QCAMERA_CH_TYPE_MAX; i++) {
        if (m_channels[i] != NULL &&
            m_channels[i]->getMyHandle() == channelHandle) {
            return m_channels[i];
        }
    }

    return NULL;
}
/*===========================================================================
 * FUNCTION   : needPreviewFDCallback
 *
 * DESCRIPTION: decides if needPreviewFDCallback
 *
 * PARAMETERS :
 *   @num_faces : number of faces
 *
 * RETURN     : bool type of status
 *              true  -- success
 *              fale -- failure code
 *==========================================================================*/
bool QCamera2HardwareInterface::needPreviewFDCallback(uint8_t num_faces)
{
    if (num_faces == 0 && mNumPreviewFaces == 0) {
        return false;
    }

    return true;
}

/*===========================================================================
 * FUNCTION   : processFaceDetectionReuslt
 *
 * DESCRIPTION: process face detection reuslt
 *
 * PARAMETERS :
 *   @faces_data : ptr to face processing result struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::processFaceDetectionResult(cam_faces_data_t *faces_data)
{
    if (!mParameters.isFaceDetectionEnabled()) {
        LOGH("FaceDetection not enabled, no ops here");
        return NO_ERROR;
    }

    qcamera_face_detect_type_t fd_type = faces_data->detection_data.fd_type;
    cam_face_detection_data_t *detect_data = &(faces_data->detection_data);
    if ((NULL == mDataCb) ||
        (fd_type == QCAMERA_FD_PREVIEW && !msgTypeEnabled(CAMERA_MSG_PREVIEW_METADATA)) ||
        (!needPreviewFDCallback(detect_data->num_faces_detected))
#ifndef VANILLA_HAL
        || (fd_type == QCAMERA_FD_SNAPSHOT && !msgTypeEnabled(CAMERA_MSG_META_DATA))
#endif
        ) {
        LOGH("metadata msgtype not enabled, no ops here");
        return NO_ERROR;
    }

    if ((fd_type == QCAMERA_FD_PREVIEW) && (detect_data->update_flag == FALSE)) {
        // Don't send callback to app if this is skipped by fd at backend
        return NO_ERROR;
    }

    cam_dimension_t display_dim;
    mParameters.getStreamDimension(CAM_STREAM_TYPE_PREVIEW, display_dim);
    if (display_dim.width <= 0 || display_dim.height <= 0) {
        LOGE("Invalid preview width or height (%d x %d)",
               display_dim.width, display_dim.height);
        return UNKNOWN_ERROR;
    }

    // process face detection result
    // need separate face detection in preview or snapshot type
    size_t faceResultSize = 0;
    size_t data_len = 0;
    if(fd_type == QCAMERA_FD_PREVIEW){
        //fd for preview frames
        faceResultSize = sizeof(camera_frame_metadata_t);
        faceResultSize += sizeof(camera_face_t) * MAX_ROI;
    }else if(fd_type == QCAMERA_FD_SNAPSHOT){
#ifndef VANILLA_HAL
        // fd for snapshot frames
        //check if face is detected in this frame
        if(detect_data->num_faces_detected > 0){
            data_len = sizeof(camera_frame_metadata_t) +
                    sizeof(camera_face_t) * detect_data->num_faces_detected;
        }else{
            //no face
            data_len = 0;
        }
#endif
        faceResultSize = 1 *sizeof(int)    //meta data type
                       + 1 *sizeof(int)    // meta data len
                       + data_len;         //data
    }

    camera_memory_t *faceResultBuffer = mGetMemory(-1,
                                                   faceResultSize,
                                                   1,
                                                   mCallbackCookie);
    if ( NULL == faceResultBuffer ) {
        LOGE("Not enough memory for face result data");
        return NO_MEMORY;
    }

    unsigned char *pFaceResult = ( unsigned char * ) faceResultBuffer->data;
    memset(pFaceResult, 0, faceResultSize);
    unsigned char *faceData = NULL;
    if(fd_type == QCAMERA_FD_PREVIEW){
        faceData = pFaceResult;
        mNumPreviewFaces = detect_data->num_faces_detected;
    }else if(fd_type == QCAMERA_FD_SNAPSHOT){
#ifndef VANILLA_HAL
        //need fill meta type and meta data len first
        int *data_header = (int* )pFaceResult;
        data_header[0] = CAMERA_META_DATA_FD;
        data_header[1] = (int)data_len;

        if(data_len <= 0){
            //if face is not valid or do not have face, return
            qcamera_callback_argm_t cbArg;
            memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
            cbArg.cb_type = QCAMERA_DATA_CALLBACK;
            cbArg.msg_type = CAMERA_MSG_META_DATA;
            cbArg.data = faceResultBuffer;
            cbArg.user_data = faceResultBuffer;
            cbArg.cookie = this;
            cbArg.release_cb = releaseCameraMemory;
            int32_t rc = m_cbNotifier.notifyCallback(cbArg);
            if (rc != NO_ERROR) {
                LOGE("fail sending notification");
                faceResultBuffer->release(faceResultBuffer);
            }
            return rc;
        }
#endif
        faceData = pFaceResult + 2 *sizeof(int); //skip two int length
    }

    camera_frame_metadata_t *roiData = (camera_frame_metadata_t * ) faceData;
    camera_face_t *faces = (camera_face_t *) ( faceData + sizeof(camera_frame_metadata_t) );

    roiData->number_of_faces = detect_data->num_faces_detected;
    roiData->faces = faces;
    if (roiData->number_of_faces > 0) {
        for (int i = 0; i < roiData->number_of_faces; i++) {
            faces[i].id = detect_data->faces[i].face_id;
            faces[i].score = detect_data->faces[i].score;

            // left
            faces[i].rect[0] = MAP_TO_DRIVER_COORDINATE(
                    detect_data->faces[i].face_boundary.left,
                    display_dim.width, 2000, -1000);

            // top
            faces[i].rect[1] = MAP_TO_DRIVER_COORDINATE(
                    detect_data->faces[i].face_boundary.top,
                    display_dim.height, 2000, -1000);

            // right
            faces[i].rect[2] = faces[i].rect[0] +
                    MAP_TO_DRIVER_COORDINATE(
                    detect_data->faces[i].face_boundary.width,
                    display_dim.width, 2000, 0);

             // bottom
            faces[i].rect[3] = faces[i].rect[1] +
                    MAP_TO_DRIVER_COORDINATE(
                    detect_data->faces[i].face_boundary.height,
                    display_dim.height, 2000, 0);

            if (faces_data->landmark_valid) {
                // Center of left eye
                faces[i].left_eye[0] = MAP_TO_DRIVER_COORDINATE(
                        faces_data->landmark_data.face_landmarks[i].left_eye_center.x,
                        display_dim.width, 2000, -1000);
                faces[i].left_eye[1] = MAP_TO_DRIVER_COORDINATE(
                        faces_data->landmark_data.face_landmarks[i].left_eye_center.y,
                        display_dim.height, 2000, -1000);

                // Center of right eye
                faces[i].right_eye[0] = MAP_TO_DRIVER_COORDINATE(
                        faces_data->landmark_data.face_landmarks[i].right_eye_center.x,
                        display_dim.width, 2000, -1000);
                faces[i].right_eye[1] = MAP_TO_DRIVER_COORDINATE(
                        faces_data->landmark_data.face_landmarks[i].right_eye_center.y,
                        display_dim.height, 2000, -1000);

                // Center of mouth
                faces[i].mouth[0] = MAP_TO_DRIVER_COORDINATE(
                        faces_data->landmark_data.face_landmarks[i].mouth_center.x,
                        display_dim.width, 2000, -1000);
                faces[i].mouth[1] = MAP_TO_DRIVER_COORDINATE(
                        faces_data->landmark_data.face_landmarks[i].mouth_center.y,
                        display_dim.height, 2000, -1000);
            } else {
                // return -2000 if invalid
                faces[i].left_eye[0] = -2000;
                faces[i].left_eye[1] = -2000;

                faces[i].right_eye[0] = -2000;
                faces[i].right_eye[1] = -2000;

                faces[i].mouth[0] = -2000;
                faces[i].mouth[1] = -2000;
            }

#ifndef VANILLA_HAL
#ifdef TARGET_TS_MAKEUP
            mFaceRect.left = detect_data->faces[i].face_boundary.left;
            mFaceRect.top = detect_data->faces[i].face_boundary.top;
            mFaceRect.right = detect_data->faces[i].face_boundary.width+mFaceRect.left;
            mFaceRect.bottom = detect_data->faces[i].face_boundary.height+mFaceRect.top;
#endif
            if (faces_data->smile_valid) {
                faces[i].smile_degree = faces_data->smile_data.smile[i].smile_degree;
                faces[i].smile_score = faces_data->smile_data.smile[i].smile_confidence;
            }
            if (faces_data->blink_valid) {
                faces[i].blink_detected = faces_data->blink_data.blink[i].blink_detected;
                faces[i].leye_blink = faces_data->blink_data.blink[i].left_blink;
                faces[i].reye_blink = faces_data->blink_data.blink[i].right_blink;
            }
            if (faces_data->recog_valid) {
                faces[i].face_recognised = faces_data->recog_data.face_rec[i].face_recognised;
            }
            if (faces_data->gaze_valid) {
                faces[i].gaze_angle = faces_data->gaze_data.gaze[i].gaze_angle;
                faces[i].updown_dir = faces_data->gaze_data.gaze[i].updown_dir;
                faces[i].leftright_dir = faces_data->gaze_data.gaze[i].leftright_dir;
                faces[i].roll_dir = faces_data->gaze_data.gaze[i].roll_dir;
                faces[i].left_right_gaze = faces_data->gaze_data.gaze[i].left_right_gaze;
                faces[i].top_bottom_gaze = faces_data->gaze_data.gaze[i].top_bottom_gaze;
            }
#endif

        }
    }
    else{
#ifdef TARGET_TS_MAKEUP
        memset(&mFaceRect,-1,sizeof(mFaceRect));
#endif
    }
    qcamera_callback_argm_t cbArg;
    memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
    cbArg.cb_type = QCAMERA_DATA_CALLBACK;
    if(fd_type == QCAMERA_FD_PREVIEW){
        cbArg.msg_type = CAMERA_MSG_PREVIEW_METADATA;
    }
#ifndef VANILLA_HAL
    else if(fd_type == QCAMERA_FD_SNAPSHOT){
        cbArg.msg_type = CAMERA_MSG_META_DATA;
    }
#endif
    cbArg.data = faceResultBuffer;
    cbArg.metadata = roiData;
    cbArg.user_data = faceResultBuffer;
    cbArg.cookie = this;
    cbArg.release_cb = releaseCameraMemory;
    int32_t rc = m_cbNotifier.notifyCallback(cbArg);
    if (rc != NO_ERROR) {
        LOGE("fail sending notification");
        faceResultBuffer->release(faceResultBuffer);
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : releaseCameraMemory
 *
 * DESCRIPTION: releases camera memory objects
 *
 * PARAMETERS :
 *   @data    : buffer to be released
 *   @cookie  : context data
 *   @cbStatus: callback status
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera2HardwareInterface::releaseCameraMemory(void *data,
                                                    void */*cookie*/,
                                                    int32_t /*cbStatus*/)
{
    camera_memory_t *mem = ( camera_memory_t * ) data;
    if ( NULL != mem ) {
        mem->release(mem);
    }
}

/*===========================================================================
 * FUNCTION   : returnStreamBuffer
 *
 * DESCRIPTION: returns back a stream buffer
 *
 * PARAMETERS :
 *   @data    : buffer to be released
 *   @cookie  : context data
 *   @cbStatus: callback status
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera2HardwareInterface::returnStreamBuffer(void *data,
                                                   void *cookie,
                                                   int32_t /*cbStatus*/)
{
    QCameraStream *stream = ( QCameraStream * ) cookie;
    int idx = *((int *)data);
    if ((NULL != stream) && (0 <= idx)) {
        stream->bufDone((uint32_t)idx);
    } else {
        LOGE("Cannot return buffer %d %p", idx, cookie);
    }
}

/*===========================================================================
 * FUNCTION   : processHistogramStats
 *
 * DESCRIPTION: process histogram stats
 *
 * PARAMETERS :
 *   @hist_data : ptr to histogram stats struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::processHistogramStats(
        __unused cam_hist_stats_t &stats_data)
{
#ifndef VANILLA_HAL
    if (!mParameters.isHistogramEnabled()) {
        LOGH("Histogram not enabled, no ops here");
        return NO_ERROR;
    }

    camera_memory_t *histBuffer = mGetMemory(-1,
                                             sizeof(cam_histogram_data_t),
                                             1,
                                             mCallbackCookie);
    if ( NULL == histBuffer ) {
        LOGE("Not enough memory for histogram data");
        return NO_MEMORY;
    }

    cam_histogram_data_t *pHistData = (cam_histogram_data_t *)histBuffer->data;
    if (pHistData == NULL) {
        LOGE("memory data ptr is NULL");
        return UNKNOWN_ERROR;
    }

    switch (stats_data.type) {
    case CAM_HISTOGRAM_TYPE_BAYER:
        switch (stats_data.bayer_stats.data_type) {
            case CAM_STATS_CHANNEL_Y:
            case CAM_STATS_CHANNEL_R:
                *pHistData = stats_data.bayer_stats.r_stats;
                break;
            case CAM_STATS_CHANNEL_GR:
                *pHistData = stats_data.bayer_stats.gr_stats;
                break;
            case CAM_STATS_CHANNEL_GB:
            case CAM_STATS_CHANNEL_ALL:
                *pHistData = stats_data.bayer_stats.gb_stats;
                break;
            case CAM_STATS_CHANNEL_B:
                *pHistData = stats_data.bayer_stats.b_stats;
                break;
            default:
                *pHistData = stats_data.bayer_stats.r_stats;
                break;
        }
        break;
    case CAM_HISTOGRAM_TYPE_YUV:
        *pHistData = stats_data.yuv_stats;
        break;
    }

    qcamera_callback_argm_t cbArg;
    memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
    cbArg.cb_type = QCAMERA_DATA_CALLBACK;
    cbArg.msg_type = CAMERA_MSG_STATS_DATA;
    cbArg.data = histBuffer;
    cbArg.user_data = histBuffer;
    cbArg.cookie = this;
    cbArg.release_cb = releaseCameraMemory;
    int32_t rc = m_cbNotifier.notifyCallback(cbArg);
    if (rc != NO_ERROR) {
        LOGE("fail sending notification");
        histBuffer->release(histBuffer);
    }
#endif
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : calcThermalLevel
 *
 * DESCRIPTION: Calculates the target fps range depending on
 *              the thermal level.
 *              Note that this function can be called from QCameraParametersIntf
 *              while mutex is held. So it should not call back into
 *              QCameraParametersIntf causing deadlock.
 *
 * PARAMETERS :
 *   @level      : received thermal level
 *   @minFPS     : minimum configured fps range
 *   @maxFPS     : maximum configured fps range
 *   @minVideoFps: minimum configured fps range
 *   @maxVideoFps: maximum configured fps range
 *   @adjustedRange : target fps range
 *   @skipPattern : target skip pattern
 *   @bRecordingHint : recording hint value
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::calcThermalLevel(
            qcamera_thermal_level_enum_t level,
            const int minFPSi,
            const int maxFPSi,
            const float &minVideoFps,
            const float &maxVideoFps,
            cam_fps_range_t &adjustedRange,
            enum msm_vfe_frame_skip_pattern &skipPattern,
            bool bRecordingHint)
{
    const float minFPS = (float)minFPSi;
    const float maxFPS = (float)maxFPSi;

    LOGH("level: %d, preview minfps %f, preview maxfpS %f, "
              "video minfps %f, video maxfpS %f",
             level, minFPS, maxFPS, minVideoFps, maxVideoFps);

    switch(level) {
    case QCAMERA_THERMAL_NO_ADJUSTMENT:
        {
            adjustedRange.min_fps = minFPS / 1000.0f;
            adjustedRange.max_fps = maxFPS / 1000.0f;
            adjustedRange.video_min_fps = minVideoFps / 1000.0f;
            adjustedRange.video_max_fps = maxVideoFps / 1000.0f;
            skipPattern = NO_SKIP;
        }
        break;
    case QCAMERA_THERMAL_SLIGHT_ADJUSTMENT:
        {
            adjustedRange.min_fps = minFPS / 1000.0f;
            adjustedRange.max_fps = maxFPS / 1000.0f;
            adjustedRange.min_fps -= 0.1f * adjustedRange.min_fps;
            adjustedRange.max_fps -= 0.1f * adjustedRange.max_fps;
            adjustedRange.video_min_fps = minVideoFps / 1000.0f;
            adjustedRange.video_max_fps = maxVideoFps / 1000.0f;
            adjustedRange.video_min_fps -= 0.1f * adjustedRange.video_min_fps;
            adjustedRange.video_max_fps -= 0.1f * adjustedRange.video_max_fps;
            if ( adjustedRange.min_fps < 1 ) {
                adjustedRange.min_fps = 1;
            }
            if ( adjustedRange.max_fps < 1 ) {
                adjustedRange.max_fps = 1;
            }
            if ( adjustedRange.video_min_fps < 1 ) {
                adjustedRange.video_min_fps = 1;
            }
            if ( adjustedRange.video_max_fps < 1 ) {
                adjustedRange.video_max_fps = 1;
            }
            skipPattern = EVERY_2FRAME;
        }
        break;
    case QCAMERA_THERMAL_BIG_ADJUSTMENT:
        {
            adjustedRange.min_fps = minFPS / 1000.0f;
            adjustedRange.max_fps = maxFPS / 1000.0f;
            adjustedRange.min_fps -= 0.2f * adjustedRange.min_fps;
            adjustedRange.max_fps -= 0.2f * adjustedRange.max_fps;
            adjustedRange.video_min_fps = minVideoFps / 1000.0f;
            adjustedRange.video_max_fps = maxVideoFps / 1000.0f;
            adjustedRange.video_min_fps -= 0.2f * adjustedRange.video_min_fps;
            adjustedRange.video_max_fps -= 0.2f * adjustedRange.video_max_fps;
            if ( adjustedRange.min_fps < 1 ) {
                adjustedRange.min_fps = 1;
            }
            if ( adjustedRange.max_fps < 1 ) {
                adjustedRange.max_fps = 1;
            }
            if ( adjustedRange.video_min_fps < 1 ) {
                adjustedRange.video_min_fps = 1;
            }
            if ( adjustedRange.video_max_fps < 1 ) {
                adjustedRange.video_max_fps = 1;
            }
            skipPattern = EVERY_4FRAME;
        }
        break;
    case QCAMERA_THERMAL_MAX_ADJUSTMENT:
        {
            // Stop Preview?
            // Set lowest min FPS for now
            adjustedRange.min_fps = minFPS/1000.0f;
            adjustedRange.max_fps = minFPS/1000.0f;
            cam_capability_t *capability = gCamCapability[mCameraId];
            for (size_t i = 0;
                     i < capability->fps_ranges_tbl_cnt;
                     i++) {
                if (capability->fps_ranges_tbl[i].min_fps <
                        adjustedRange.min_fps) {
                    adjustedRange.min_fps =
                            capability->fps_ranges_tbl[i].min_fps;
                    adjustedRange.max_fps = adjustedRange.min_fps;
                }
            }
            skipPattern = MAX_SKIP;
            adjustedRange.video_min_fps = adjustedRange.min_fps;
            adjustedRange.video_max_fps = adjustedRange.max_fps;
        }
        break;
    case QCAMERA_THERMAL_SHUTDOWN:
        {
            // send error notify
            LOGE("Received shutdown thermal level. Closing camera");
            sendEvtNotify(CAMERA_MSG_ERROR, CAMERA_ERROR_SERVER_DIED, 0);
        }
        break;
    default:
        {
            LOGW("Invalid thermal level %d", level);
            return BAD_VALUE;
        }
        break;
    }
    if (level >= QCAMERA_THERMAL_NO_ADJUSTMENT && level <= QCAMERA_THERMAL_MAX_ADJUSTMENT) {
        if (bRecordingHint) {
            adjustedRange.min_fps = minFPS / 1000.0f;
            adjustedRange.max_fps = maxFPS / 1000.0f;
            adjustedRange.video_min_fps = minVideoFps / 1000.0f;
            adjustedRange.video_max_fps = maxVideoFps / 1000.0f;
            skipPattern = NO_SKIP;
            LOGH("No FPS mitigation in camcorder mode");
        }
        LOGH("Thermal level %d, FPS [%3.2f,%3.2f, %3.2f,%3.2f], frameskip %d",
                  level, adjustedRange.min_fps, adjustedRange.max_fps,
                    adjustedRange.video_min_fps, adjustedRange.video_max_fps, skipPattern);
    }

    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : recalcFPSRange
 *
 * DESCRIPTION: adjust the configured fps range regarding
 *              the last thermal level.
 *
 * PARAMETERS :
 *   @minFPS      : minimum configured fps range
 *   @maxFPS      : maximum configured fps range
 *   @minVideoFPS : minimum configured video fps
 *   @maxVideoFPS : maximum configured video fps
 *   @adjustedRange : target fps range
 *   @bRecordingHint : recording hint value
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::recalcFPSRange(int &minFPS, int &maxFPS,
        const float &minVideoFPS, const float &maxVideoFPS,
        cam_fps_range_t &adjustedRange, bool bRecordingHint)
{
    enum msm_vfe_frame_skip_pattern skipPattern;
    calcThermalLevel(mThermalLevel,
                     minFPS,
                     maxFPS,
                     minVideoFPS,
                     maxVideoFPS,
                     adjustedRange,
                     skipPattern,
                     bRecordingHint);
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : updateThermalLevel
 *
 * DESCRIPTION: update thermal level depending on thermal events
 *
 * PARAMETERS :
 *   @level   : thermal level
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::updateThermalLevel(void *thermal_level)
{
    int ret = NO_ERROR;
    cam_fps_range_t adjustedRange;
    int minFPS, maxFPS;
    float minVideoFPS, maxVideoFPS;
    enum msm_vfe_frame_skip_pattern skipPattern;
    bool value;
    qcamera_thermal_level_enum_t level = *(qcamera_thermal_level_enum_t *)thermal_level;


    if (!mCameraOpened) {
        LOGH("Camera is not opened, no need to update camera parameters");
        return NO_ERROR;
    }

    mParameters.getPreviewFpsRange(&minFPS, &maxFPS);
    qcamera_thermal_mode thermalMode = mParameters.getThermalMode();
    if (mParameters.isHfrMode()) {
        cam_fps_range_t hfrFpsRange;
        mParameters.getHfrFps(hfrFpsRange);
        minVideoFPS = hfrFpsRange.video_min_fps;
        maxVideoFPS = hfrFpsRange.video_max_fps;
    } else {
        minVideoFPS = minFPS;
        maxVideoFPS = maxFPS;
    }

    value = mParameters.getRecordingHintValue();
    calcThermalLevel(level, minFPS, maxFPS, minVideoFPS, maxVideoFPS,
            adjustedRange, skipPattern, value );
    mThermalLevel = level;

    if (thermalMode == QCAMERA_THERMAL_ADJUST_FPS)
        ret = mParameters.adjustPreviewFpsRange(&adjustedRange);
    else if (thermalMode == QCAMERA_THERMAL_ADJUST_FRAMESKIP)
        ret = mParameters.setFrameSkip(skipPattern);
    else
        LOGW("Incorrect thermal mode %d", thermalMode);

    return ret;

}

/*===========================================================================
 * FUNCTION   : updateParameters
 *
 * DESCRIPTION: update parameters
 *
 * PARAMETERS :
 *   @parms       : input parameters string
 *   @needRestart : output, flag to indicate if preview restart is needed
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera2HardwareInterface::updateParameters(const char *parms, bool &needRestart)
{
    int rc = NO_ERROR;

    String8 str = String8(parms);
    rc =  mParameters.updateParameters(str, needRestart);
    setNeedRestart(needRestart);

    // update stream based parameter settings
    for (int i = 0; i < QCAMERA_CH_TYPE_MAX; i++) {
        if (m_channels[i] != NULL) {
            m_channels[i]->UpdateStreamBasedParameters(mParameters);
        }
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : commitParameterChanges
 *
 * DESCRIPTION: commit parameter changes to the backend to take effect
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 * NOTE       : This function must be called after updateParameters.
 *              Otherwise, no change will be passed to backend to take effect.
 *==========================================================================*/
int QCamera2HardwareInterface::commitParameterChanges()
{
    int rc = NO_ERROR;
    rc = mParameters.commitParameters();
    if (rc == NO_ERROR) {
        // update number of snapshot based on committed parameters setting
        rc = mParameters.setNumOfSnapshot();
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : needDebugFps
 *
 * DESCRIPTION: if fps log info need to be printed out
 *
 * PARAMETERS : none
 *
 * RETURN     : true: need print out fps log
 *              false: no need to print out fps log
 *==========================================================================*/
bool QCamera2HardwareInterface::needDebugFps()
{
    bool needFps = false;
    needFps = mParameters.isFpsDebugEnabled();
    return needFps;
}

/*===========================================================================
 * FUNCTION   : isCACEnabled
 *
 * DESCRIPTION: if CAC is enabled
 *
 * PARAMETERS : none
 *
 * RETURN     : true: needed
 *              false: no need
 *==========================================================================*/
bool QCamera2HardwareInterface::isCACEnabled()
{
    char prop[PROPERTY_VALUE_MAX];
    memset(prop, 0, sizeof(prop));
    property_get("persist.camera.feature.cac", prop, "0");
    int enableCAC = atoi(prop);
    return enableCAC == 1;
}

/*===========================================================================
 * FUNCTION   : is4k2kResolution
 *
 * DESCRIPTION: if resolution is 4k x 2k or true 4k x 2k
 *
 * PARAMETERS : none
 *
 * RETURN     : true: needed
 *              false: no need
 *==========================================================================*/
bool QCamera2HardwareInterface::is4k2kResolution(cam_dimension_t* resolution)
{
   bool enabled = false;
   if ((resolution->width == 4096 && resolution->height == 2160) ||
       (resolution->width == 3840 && resolution->height == 2160) ) {
      enabled = true;
   }
   return enabled;
}

/*===========================================================================
 * FUNCTION   : isPreviewRestartEnabled
 *
 * DESCRIPTION: Check whether preview should be restarted automatically
 *              during image capture.
 *
 * PARAMETERS : none
 *
 * RETURN     : true: needed
 *              false: no need
 *==========================================================================*/
bool QCamera2HardwareInterface::isPreviewRestartEnabled()
{
    char prop[PROPERTY_VALUE_MAX];
    memset(prop, 0, sizeof(prop));
    property_get("persist.camera.feature.restart", prop, "0");
    int earlyRestart = atoi(prop);
    return earlyRestart == 1;
}

/*===========================================================================
 * FUNCTION   : needReprocess
 *
 * DESCRIPTION: if reprocess is needed
 *
 * PARAMETERS : none
 *
 * RETURN     : true: needed
 *              false: no need
 *==========================================================================*/
bool QCamera2HardwareInterface::needReprocess()
{
    bool needReprocess = false;

    if (!mParameters.isJpegPictureFormat() &&
        !mParameters.isNV21PictureFormat()) {
        // RAW image, no need to reprocess
        return false;
    }

    //Disable reprocess for small jpeg size or 4K liveshot case but enable if lowpower mode
    if ((mParameters.is4k2kVideoResolution() && mParameters.getRecordingHintValue()
            && !isLowPowerMode()) || mParameters.isSmallJpegSizeEnabled()) {
        return false;
    }

    // pp feature config
    cam_pp_feature_config_t pp_config;
    memset(&pp_config, 0, sizeof(cam_pp_feature_config_t));

    //Decide whether to do reprocess or not based on
    //ppconfig obtained in the first pass.
    getPPConfig(pp_config);

    if (pp_config.feature_mask > 0) {
        needReprocess = true;
    }

    LOGH("needReprocess %s", needReprocess ? "true" : "false");
    return needReprocess;
}


/*===========================================================================
 * FUNCTION   : needRotationReprocess
 *
 * DESCRIPTION: if rotation needs to be done by reprocess in pp
 *
 * PARAMETERS : none
 *
 * RETURN     : true: needed
 *              false: no need
 *==========================================================================*/
bool QCamera2HardwareInterface::needRotationReprocess()
{
    if (!mParameters.isJpegPictureFormat() &&
        !mParameters.isNV21PictureFormat()) {
        // RAW image, no need to reprocess
        return false;
    }

    //Disable reprocess for 4K liveshot case
    if ((mParameters.is4k2kVideoResolution() && mParameters.getRecordingHintValue()
            && !isLowPowerMode()) || mParameters.isSmallJpegSizeEnabled()) {
        //Disable reprocess for 4K liveshot case or small jpeg size
         return false;
    }

    if ((gCamCapability[mCameraId]->qcom_supported_feature_mask &
            CAM_QCOM_FEATURE_ROTATION) > 0 &&
            (mParameters.getJpegRotation() > 0)) {
        // current rotation is not zero, and pp has the capability to process rotation
        LOGH("need to do reprocess for rotation=%d",
                 mParameters.getJpegRotation());
        return true;
    }

    return false;
}

/*===========================================================================
 * FUNCTION   : getThumbnailSize
 *
 * DESCRIPTION: get user set thumbnail size
 *
 * PARAMETERS :
 *   @dim     : output of thumbnail dimension
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera2HardwareInterface::getThumbnailSize(cam_dimension_t &dim)
{
    mParameters.getThumbnailSize(&dim.width, &dim.height);
}

/*===========================================================================
 * FUNCTION   : getJpegQuality
 *
 * DESCRIPTION: get user set jpeg quality
 *
 * PARAMETERS : none
 *
 * RETURN     : jpeg quality setting
 *==========================================================================*/
uint32_t QCamera2HardwareInterface::getJpegQuality()
{
    uint32_t quality = 0;
    quality =  mParameters.getJpegQuality();
    return quality;
}

/*===========================================================================
 * FUNCTION   : getExifData
 *
 * DESCRIPTION: get exif data to be passed into jpeg encoding
 *
 * PARAMETERS : none
 *
 * RETURN     : exif data from user setting and GPS
 *==========================================================================*/
QCameraExif *QCamera2HardwareInterface::getExifData()
{
    QCameraExif *exif = new QCameraExif();
    if (exif == NULL) {
        LOGE("No memory for QCameraExif");
        return NULL;
    }

    int32_t rc = NO_ERROR;

    // add exif entries
    String8 dateTime, subSecTime;
    rc = mParameters.getExifDateTime(dateTime, subSecTime);
    if(rc == NO_ERROR) {
        exif->addEntry(EXIFTAGID_DATE_TIME, EXIF_ASCII,
                (uint32_t)(dateTime.length() + 1), (void *)dateTime.string());
        exif->addEntry(EXIFTAGID_EXIF_DATE_TIME_ORIGINAL, EXIF_ASCII,
                (uint32_t)(dateTime.length() + 1), (void *)dateTime.string());
        exif->addEntry(EXIFTAGID_EXIF_DATE_TIME_DIGITIZED, EXIF_ASCII,
                (uint32_t)(dateTime.length() + 1), (void *)dateTime.string());
        exif->addEntry(EXIFTAGID_SUBSEC_TIME, EXIF_ASCII,
                (uint32_t)(subSecTime.length() + 1), (void *)subSecTime.string());
        exif->addEntry(EXIFTAGID_SUBSEC_TIME_ORIGINAL, EXIF_ASCII,
                (uint32_t)(subSecTime.length() + 1), (void *)subSecTime.string());
        exif->addEntry(EXIFTAGID_SUBSEC_TIME_DIGITIZED, EXIF_ASCII,
                (uint32_t)(subSecTime.length() + 1), (void *)subSecTime.string());
    } else {
        LOGW("getExifDateTime failed");
    }

    rat_t focalLength;
    rc = mParameters.getExifFocalLength(&focalLength);
    if (rc == NO_ERROR) {
        exif->addEntry(EXIFTAGID_FOCAL_LENGTH,
                       EXIF_RATIONAL,
                       1,
                       (void *)&(focalLength));
    } else {
        LOGW("getExifFocalLength failed");
    }

    uint16_t isoSpeed = mParameters.getExifIsoSpeed();
    if (getSensorType() != CAM_SENSOR_YUV) {
        exif->addEntry(EXIFTAGID_ISO_SPEED_RATING,
                       EXIF_SHORT,
                       1,
                       (void *)&(isoSpeed));
    }

    char gpsProcessingMethod[EXIF_ASCII_PREFIX_SIZE + GPS_PROCESSING_METHOD_SIZE];
    uint32_t count = 0;

    /*gps data might not be available */
    rc = mParameters.getExifGpsProcessingMethod(gpsProcessingMethod, count);
    if(rc == NO_ERROR) {
        exif->addEntry(EXIFTAGID_GPS_PROCESSINGMETHOD,
                       EXIF_ASCII,
                       count,
                       (void *)gpsProcessingMethod);
    } else {
        LOGW("getExifGpsProcessingMethod failed");
    }

    rat_t latitude[3];
    char latRef[2];
    rc = mParameters.getExifLatitude(latitude, latRef);
    if(rc == NO_ERROR) {
        exif->addEntry(EXIFTAGID_GPS_LATITUDE,
                       EXIF_RATIONAL,
                       3,
                       (void *)latitude);
        exif->addEntry(EXIFTAGID_GPS_LATITUDE_REF,
                       EXIF_ASCII,
                       2,
                       (void *)latRef);
    } else {
        LOGW("getExifLatitude failed");
    }

    rat_t longitude[3];
    char lonRef[2];
    rc = mParameters.getExifLongitude(longitude, lonRef);
    if(rc == NO_ERROR) {
        exif->addEntry(EXIFTAGID_GPS_LONGITUDE,
                       EXIF_RATIONAL,
                       3,
                       (void *)longitude);

        exif->addEntry(EXIFTAGID_GPS_LONGITUDE_REF,
                       EXIF_ASCII,
                       2,
                       (void *)lonRef);
    } else {
        LOGW("getExifLongitude failed");
    }

    rat_t altitude;
    char altRef;
    rc = mParameters.getExifAltitude(&altitude, &altRef);
    if(rc == NO_ERROR) {
        exif->addEntry(EXIFTAGID_GPS_ALTITUDE,
                       EXIF_RATIONAL,
                       1,
                       (void *)&(altitude));

        exif->addEntry(EXIFTAGID_GPS_ALTITUDE_REF,
                       EXIF_BYTE,
                       1,
                       (void *)&altRef);
    } else {
        LOGW("getExifAltitude failed");
    }

    char gpsDateStamp[20];
    rat_t gpsTimeStamp[3];
    rc = mParameters.getExifGpsDateTimeStamp(gpsDateStamp, 20, gpsTimeStamp);
    if(rc == NO_ERROR) {
        exif->addEntry(EXIFTAGID_GPS_DATESTAMP,
                       EXIF_ASCII,
                       (uint32_t)(strlen(gpsDateStamp) + 1),
                       (void *)gpsDateStamp);

        exif->addEntry(EXIFTAGID_GPS_TIMESTAMP,
                       EXIF_RATIONAL,
                       3,
                       (void *)gpsTimeStamp);
    } else {
        LOGW("getExifGpsDataTimeStamp failed");
    }

#ifdef ENABLE_MODEL_INFO_EXIF

    char value[PROPERTY_VALUE_MAX];
    if (property_get("persist.sys.exif.make", value, "") > 0 ||
            property_get("ro.product.manufacturer", value, "QCOM-AA") > 0) {
        exif->addEntry(EXIFTAGID_MAKE,
                EXIF_ASCII, strlen(value) + 1, (void *)value);
    } else {
        LOGW("getExifMaker failed");
    }

    if (property_get("persist.sys.exif.model", value, "") > 0 ||
            property_get("ro.product.model", value, "QCAM-AA") > 0) {
        exif->addEntry(EXIFTAGID_MODEL,
                EXIF_ASCII, strlen(value) + 1, (void *)value);
    } else {
        LOGW("getExifModel failed");
    }

    if (property_get("ro.build.description", value, "QCAM-AA") > 0) {
        exif->addEntry(EXIFTAGID_SOFTWARE, EXIF_ASCII,
                (uint32_t)(strlen(value) + 1), (void *)value);
    } else {
        LOGW("getExifSoftware failed");
    }

#endif

    if (mParameters.useJpegExifRotation()) {
        int16_t orientation;
        switch (mParameters.getJpegExifRotation()) {
        case 0:
            orientation = 1;
            break;
        case 90:
            orientation = 6;
            break;
        case 180:
            orientation = 3;
            break;
        case 270:
            orientation = 8;
            break;
        default:
            orientation = 1;
            break;
        }
        exif->addEntry(EXIFTAGID_ORIENTATION,
                EXIF_SHORT,
                1,
                (void *)&orientation);
        exif->addEntry(EXIFTAGID_TN_ORIENTATION,
                EXIF_SHORT,
                1,
                (void *)&orientation);
    }

    return exif;
}

/*===========================================================================
 * FUNCTION   : setHistogram
 *
 * DESCRIPTION: set if histogram should be enabled
 *
 * PARAMETERS :
 *   @histogram_en : bool flag if histogram should be enabled
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::setHistogram(bool histogram_en)
{
    return mParameters.setHistogram(histogram_en);
}

/*===========================================================================
 * FUNCTION   : setFaceDetection
 *
 * DESCRIPTION: set if face detection should be enabled
 *
 * PARAMETERS :
 *   @enabled : bool flag if face detection should be enabled
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::setFaceDetection(bool enabled)
{
    return mParameters.setFaceDetection(enabled, true);
}

/*===========================================================================
 * FUNCTION   : isCaptureShutterEnabled
 *
 * DESCRIPTION: Check whether shutter should be triggered immediately after
 *              capture
 *
 * PARAMETERS :
 *
 * RETURN     : true - regular capture
 *              false - other type of capture
 *==========================================================================*/
bool QCamera2HardwareInterface::isCaptureShutterEnabled()
{
    char prop[PROPERTY_VALUE_MAX];
    memset(prop, 0, sizeof(prop));
    property_get("persist.camera.feature.shutter", prop, "0");
    int enableShutter = atoi(prop);
    return enableShutter == 1;
}

/*===========================================================================
 * FUNCTION   : needProcessPreviewFrame
 *
 * DESCRIPTION: returns whether preview frame need to be displayed
 *
 * PARAMETERS :
 *   @frameID : frameID of frame to be processed
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
bool QCamera2HardwareInterface::needProcessPreviewFrame(uint32_t frameID)
{
    return (((m_stateMachine.isPreviewRunning()) &&
            (!isDisplayFrameToSkip(frameID)) &&
            (!mParameters.isInstantAECEnabled())) ||
            (isPreviewRestartEnabled()));
}

/*===========================================================================
 * FUNCTION   : needSendPreviewCallback
 *
 * DESCRIPTION: returns whether preview frame need to callback to APP
 *
 * PARAMETERS :
 *
 * RETURN     : true - need preview frame callbck
 *              false - not send preview frame callback
 *==========================================================================*/
bool QCamera2HardwareInterface::needSendPreviewCallback()
{
    return m_stateMachine.isPreviewRunning()
            && (mDataCb != NULL)
            && (msgTypeEnabledWithLock(CAMERA_MSG_PREVIEW_FRAME) > 0)
            && m_stateMachine.isPreviewCallbackNeeded();
};

/*===========================================================================
 * FUNCTION   : setDisplaySkip
 *
 * DESCRIPTION: set range of frames to skip for preview
 *
 * PARAMETERS :
 *   @enabled : TRUE to start skipping frame to display
                FALSE to stop skipping frame to display
 *   @skipCnt : Number of frame to skip. 0 by default
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera2HardwareInterface::setDisplaySkip(bool enabled, uint8_t skipCnt)
{
    pthread_mutex_lock(&mGrallocLock);
    if (enabled) {
        setDisplayFrameSkip();
        setDisplayFrameSkip(mLastPreviewFrameID + skipCnt + 1);
    } else {
        setDisplayFrameSkip(mFrameSkipStart, (mLastPreviewFrameID + skipCnt + 1));
    }
    pthread_mutex_unlock(&mGrallocLock);
}

/*===========================================================================
 * FUNCTION   : setDisplayFrameSkip
 *
 * DESCRIPTION: set range of frames to skip for preview
 *
 * PARAMETERS :
 *   @start   : frameId to start skip
 *   @end     : frameId to stop skip
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera2HardwareInterface::setDisplayFrameSkip(uint32_t start,
        uint32_t end)
{
    if (start == 0) {
        mFrameSkipStart = 0;
        mFrameSkipEnd = 0;
        return;
    }
    if ((mFrameSkipStart == 0) || (mFrameSkipStart > start)) {
        mFrameSkipStart = start;
    }
    if ((end == 0) || (end > mFrameSkipEnd)) {
        mFrameSkipEnd = end;
    }
}

/*===========================================================================
 * FUNCTION   : isDisplayFrameToSkip
 *
 * DESCRIPTION: function to determin if input frame falls under skip range
 *
 * PARAMETERS :
 *   @frameId : frameId to verify
 *
 * RETURN     : true : need to skip
 *              false: no need to skip
 *==========================================================================*/
bool QCamera2HardwareInterface::isDisplayFrameToSkip(uint32_t frameId)
{
    return ((mFrameSkipStart != 0) && (frameId >= mFrameSkipStart) &&
            (frameId <= mFrameSkipEnd || mFrameSkipEnd == 0)) ? TRUE : FALSE;
}

/*===========================================================================
 * FUNCTION   : prepareHardwareForSnapshot
 *
 * DESCRIPTION: prepare hardware for snapshot, such as LED
 *
 * PARAMETERS :
 *   @afNeeded: flag indicating if Auto Focus needs to be done during preparation
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::prepareHardwareForSnapshot(int32_t afNeeded)
{
    ATRACE_CALL();
    LOGI("[KPI Perf]: Send PREPARE SANSPHOT event");
    return mCameraHandle->ops->prepare_snapshot(mCameraHandle->camera_handle,
                                                afNeeded);
}

/*===========================================================================
 * FUNCTION   : needFDMetadata
 *
 * DESCRIPTION: check whether we need process Face Detection metadata in this chanel
 *
 * PARAMETERS :
 *   @channel_type: channel type
 *
  * RETURN     : true: needed
 *              false: no need
 *==========================================================================*/
bool QCamera2HardwareInterface::needFDMetadata(qcamera_ch_type_enum_t channel_type)
{
    //Note: Currently we only process ZSL channel
    bool value = false;
    if(channel_type == QCAMERA_CH_TYPE_ZSL){
        //check if FD requirement is enabled
        if(mParameters.isSnapshotFDNeeded() &&
           mParameters.isFaceDetectionEnabled()){
            value = true;
            LOGH("Face Detection metadata is required in ZSL mode.");
        }
    }

    return value;
}

/*===========================================================================
 * FUNCTION   : deferredWorkRoutine
 *
 * DESCRIPTION: data process routine that executes deferred tasks
 *
 * PARAMETERS :
 *   @data    : user data ptr (QCamera2HardwareInterface)
 *
 * RETURN     : None
 *==========================================================================*/
void *QCamera2HardwareInterface::deferredWorkRoutine(void *obj)
{
    int running = 1;
    int ret;
    uint8_t is_active = FALSE;
    int32_t job_status = 0;

    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)obj;
    QCameraCmdThread *cmdThread = &pme->mDeferredWorkThread;
    cmdThread->setName("CAM_defrdWrk");

    do {
        do {
            ret = cam_sem_wait(&cmdThread->cmd_sem);
            if (ret != 0 && errno != EINVAL) {
                LOGE("cam_sem_wait error (%s)",
                         strerror(errno));
                return NULL;
            }
        } while (ret != 0);

        // we got notified about new cmd avail in cmd queue
        camera_cmd_type_t cmd = cmdThread->getCmd();
        LOGD("cmd: %d", cmd);
        switch (cmd) {
        case CAMERA_CMD_TYPE_START_DATA_PROC:
            LOGH("start data proc");
            is_active = TRUE;
            break;
        case CAMERA_CMD_TYPE_STOP_DATA_PROC:
            LOGH("stop data proc");
            is_active = FALSE;
            // signal cmd is completed
            cam_sem_post(&cmdThread->sync_sem);
            break;
        case CAMERA_CMD_TYPE_DO_NEXT_JOB:
            {
                DefWork *dw =
                    reinterpret_cast<DefWork *>(pme->mCmdQueue.dequeue());

                if ( NULL == dw ) {
                    LOGE("Invalid deferred work");
                    break;
                }

                switch( dw->cmd ) {
                case CMD_DEF_ALLOCATE_BUFF:
                    {
                        QCameraChannel * pChannel = dw->args.allocArgs.ch;

                        if ( NULL == pChannel ) {
                            LOGE("Invalid deferred work channel");
                            job_status = BAD_VALUE;
                            break;
                        }

                        cam_stream_type_t streamType = dw->args.allocArgs.type;
                        LOGH("Deferred buffer allocation started for stream type: %d",
                                 streamType);

                        uint32_t iNumOfStreams = pChannel->getNumOfStreams();
                        QCameraStream *pStream = NULL;
                        for ( uint32_t i = 0; i < iNumOfStreams; ++i) {
                            pStream = pChannel->getStreamByIndex(i);

                            if ( NULL == pStream ) {
                                job_status = BAD_VALUE;
                                break;
                            }

                            if ( pStream->isTypeOf(streamType)) {
                                if ( pStream->allocateBuffers() ) {
                                    LOGE("Error allocating buffers !!!");
                                    job_status =  NO_MEMORY;
                                    pme->sendEvtNotify(CAMERA_MSG_ERROR,
                                            CAMERA_ERROR_UNKNOWN, 0);
                                }
                                break;
                            }
                        }
                    }
                    break;
                case CMD_DEF_PPROC_START:
                    {
                        int32_t ret = pme->getDefJobStatus(pme->mInitPProcJob);
                        if (ret != NO_ERROR) {
                            job_status = ret;
                            LOGE("PPROC Start failed");
                            pme->sendEvtNotify(CAMERA_MSG_ERROR,
                                    CAMERA_ERROR_UNKNOWN, 0);
                            break;
                        }
                        QCameraChannel * pChannel = dw->args.pprocArgs;
                        assert(pChannel);

                        if (pme->m_postprocessor.start(pChannel) != NO_ERROR) {
                            LOGE("cannot start postprocessor");
                            job_status = BAD_VALUE;
                            pme->sendEvtNotify(CAMERA_MSG_ERROR,
                                    CAMERA_ERROR_UNKNOWN, 0);
                        }
                    }
                    break;
                case CMD_DEF_METADATA_ALLOC:
                    {
                        int32_t ret = pme->getDefJobStatus(pme->mParamAllocJob);
                        if (ret != NO_ERROR) {
                            job_status = ret;
                            LOGE("Metadata alloc failed");
                            pme->sendEvtNotify(CAMERA_MSG_ERROR,
                                    CAMERA_ERROR_UNKNOWN, 0);
                            break;
                        }
                        pme->mMetadataMem = new QCameraMetadataStreamMemory(
                                QCAMERA_ION_USE_CACHE);

                        if (pme->mMetadataMem == NULL) {
                            LOGE("Unable to allocate metadata buffers");
                            job_status = BAD_VALUE;
                            pme->sendEvtNotify(CAMERA_MSG_ERROR,
                                    CAMERA_ERROR_UNKNOWN, 0);
                        } else {
                            int32_t rc = pme->mMetadataMem->allocate(
                                    dw->args.metadataAllocArgs.bufferCnt,
                                    dw->args.metadataAllocArgs.size,
                                    NON_SECURE);
                            if (rc < 0) {
                                delete pme->mMetadataMem;
                                pme->mMetadataMem = NULL;
                            }
                        }
                     }
                     break;
                case CMD_DEF_CREATE_JPEG_SESSION:
                    {
                        QCameraChannel * pChannel = dw->args.pprocArgs;
                        assert(pChannel);

                        int32_t ret = pme->getDefJobStatus(pme->mReprocJob);
                        if (ret != NO_ERROR) {
                            job_status = ret;
                            LOGE("Jpeg create failed");
                            break;
                        }

                        if (pme->m_postprocessor.createJpegSession(pChannel)
                            != NO_ERROR) {
                            LOGE("cannot create JPEG session");
                            job_status = UNKNOWN_ERROR;
                            pme->sendEvtNotify(CAMERA_MSG_ERROR,
                                    CAMERA_ERROR_UNKNOWN, 0);
                        }
                    }
                    break;
                case CMD_DEF_PPROC_INIT:
                    {
                        int32_t rc = NO_ERROR;

                        jpeg_encode_callback_t jpegEvtHandle =
                                dw->args.pprocInitArgs.jpeg_cb;
                        void* user_data = dw->args.pprocInitArgs.user_data;
                        QCameraPostProcessor *postProcessor =
                                &(pme->m_postprocessor);
                        uint32_t cameraId = pme->mCameraId;
                        cam_capability_t *capability =
                                gCamCapability[cameraId];
                        cam_padding_info_t padding_info;
                        cam_padding_info_t& cam_capability_padding_info =
                                capability->padding_info;

                        if(!pme->mJpegClientHandle) {
                            rc = pme->initJpegHandle();
                            if (rc != NO_ERROR) {
                                LOGE("Error!! creating JPEG handle failed");
                                job_status = UNKNOWN_ERROR;
                                pme->sendEvtNotify(CAMERA_MSG_ERROR,
                                        CAMERA_ERROR_UNKNOWN, 0);
                                break;
                            }
                        }
                        LOGH("mJpegClientHandle : %d", pme->mJpegClientHandle);

                        rc = postProcessor->setJpegHandle(&pme->mJpegHandle,
                                &pme->mJpegMpoHandle,
                                pme->mJpegClientHandle);
                        if (rc != 0) {
                            LOGE("Error!! set JPEG handle failed");
                            job_status = UNKNOWN_ERROR;
                            pme->sendEvtNotify(CAMERA_MSG_ERROR,
                                    CAMERA_ERROR_UNKNOWN, 0);
                            break;
                        }

                        /* get max pic size for jpeg work buf calculation*/
                        rc = postProcessor->init(jpegEvtHandle, user_data);

                        if (rc != NO_ERROR) {
                            LOGE("cannot init postprocessor");
                            job_status = UNKNOWN_ERROR;
                            pme->sendEvtNotify(CAMERA_MSG_ERROR,
                                    CAMERA_ERROR_UNKNOWN, 0);
                            break;
                        }

                        // update padding info from jpeg
                        postProcessor->getJpegPaddingReq(padding_info);
                        if (cam_capability_padding_info.width_padding <
                                padding_info.width_padding) {
                            cam_capability_padding_info.width_padding =
                                    padding_info.width_padding;
                        }
                        if (cam_capability_padding_info.height_padding <
                                padding_info.height_padding) {
                            cam_capability_padding_info.height_padding =
                                    padding_info.height_padding;
                        }
                        if (cam_capability_padding_info.plane_padding !=
                                padding_info.plane_padding) {
                            cam_capability_padding_info.plane_padding =
                                    mm_stream_calc_lcm(
                                    cam_capability_padding_info.plane_padding,
                                    padding_info.plane_padding);
                        }
                        if (cam_capability_padding_info.offset_info.offset_x
                                != padding_info.offset_info.offset_x) {
                            cam_capability_padding_info.offset_info.offset_x =
                                    mm_stream_calc_lcm (
                                    cam_capability_padding_info.offset_info.offset_x,
                                    padding_info.offset_info.offset_x);
                        }
                        if (cam_capability_padding_info.offset_info.offset_y
                                != padding_info.offset_info.offset_y) {
                            cam_capability_padding_info.offset_info.offset_y =
                            mm_stream_calc_lcm (
                                    cam_capability_padding_info.offset_info.offset_y,
                                    padding_info.offset_info.offset_y);
                        }
                    }
                    break;
                case CMD_DEF_PARAM_ALLOC:
                    {
                        int32_t rc = pme->mParameters.allocate();
                        // notify routine would not be initialized by this time.
                        // So, just update error job status
                        if (rc != NO_ERROR) {
                            job_status = rc;
                            LOGE("Param allocation failed");
                            break;
                        }
                    }
                    break;
                case CMD_DEF_PARAM_INIT:
                    {
                        int32_t rc = pme->getDefJobStatus(pme->mParamAllocJob);
                        if (rc != NO_ERROR) {
                            job_status = rc;
                            LOGE("Param init failed");
                            pme->sendEvtNotify(CAMERA_MSG_ERROR,
                                    CAMERA_ERROR_UNKNOWN, 0);
                            break;
                        }

                        uint32_t camId = pme->mCameraId;
                        cam_capability_t * cap = gCamCapability[camId];

                        if (pme->mCameraHandle == NULL) {
                            LOGE("Camera handle is null");
                            job_status = BAD_VALUE;
                            pme->sendEvtNotify(CAMERA_MSG_ERROR,
                                    CAMERA_ERROR_UNKNOWN, 0);
                            break;
                        }

                        // Now PostProc need calibration data as initialization
                        // time for jpeg_open and calibration data is a
                        // get param for now, so params needs to be initialized
                        // before postproc init
                        rc = pme->mParameters.init(cap,
                                pme->mCameraHandle,
                                pme);
                        if (rc != 0) {
                            job_status = UNKNOWN_ERROR;
                            LOGE("Parameter Initialization failed");
                            pme->sendEvtNotify(CAMERA_MSG_ERROR,
                                    CAMERA_ERROR_UNKNOWN, 0);
                            break;
                        }

                        // Get related cam calibration only in
                        // dual camera mode
                        if (pme->getRelatedCamSyncInfo()->sync_control ==
                                CAM_SYNC_RELATED_SENSORS_ON) {
                            rc = pme->mParameters.getRelatedCamCalibration(
                                &(pme->mJpegMetadata.otp_calibration_data));
                            LOGD("Dumping Calibration Data Version Id %f rc %d",
                                    pme->mJpegMetadata.otp_calibration_data.calibration_format_version,
                                    rc);
                            if (rc != 0) {
                                job_status = UNKNOWN_ERROR;
                                LOGE("getRelatedCamCalibration failed");
                                pme->sendEvtNotify(CAMERA_MSG_ERROR,
                                        CAMERA_ERROR_UNKNOWN, 0);
                                break;
                            }
                            pme->m_bRelCamCalibValid = true;
                        }

                        pme->mJpegMetadata.sensor_mount_angle =
                            cap->sensor_mount_angle;
                        pme->mJpegMetadata.default_sensor_flip = FLIP_NONE;

                        pme->mParameters.setMinPpMask(
                            cap->qcom_supported_feature_mask);
                        pme->mExifParams.debug_params =
                                (mm_jpeg_debug_exif_params_t *)
                                malloc(sizeof(mm_jpeg_debug_exif_params_t));
                        if (!pme->mExifParams.debug_params) {
                            LOGE("Out of Memory. Allocation failed for "
                                    "3A debug exif params");
                            job_status = NO_MEMORY;
                            pme->sendEvtNotify(CAMERA_MSG_ERROR,
                                    CAMERA_ERROR_UNKNOWN, 0);
                            break;
                        }
                        memset(pme->mExifParams.debug_params, 0,
                                sizeof(mm_jpeg_debug_exif_params_t));
                    }
                    break;
                case CMD_DEF_GENERIC:
                    {
                        BackgroundTask *bgTask = dw->args.genericArgs;
                        job_status = bgTask->bgFunction(bgTask->bgArgs);
                    }
                    break;
                default:
                    LOGE("Incorrect command : %d", dw->cmd);
                }

                pme->dequeueDeferredWork(dw, job_status);
            }
            break;
        case CAMERA_CMD_TYPE_EXIT:
            running = 0;
            break;
        default:
            break;
        }
    } while (running);

    return NULL;
}

/*===========================================================================
 * FUNCTION   : queueDeferredWork
 *
 * DESCRIPTION: function which queues deferred tasks
 *
 * PARAMETERS :
 *   @cmd     : deferred task
 *   @args    : deferred task arguments
 *
 * RETURN     : job id of deferred job
 *            : 0 in case of error
 *==========================================================================*/
uint32_t QCamera2HardwareInterface::queueDeferredWork(DeferredWorkCmd cmd,
                                                      DeferWorkArgs args)
{
    Mutex::Autolock l(mDefLock);
    for (int32_t i = 0; i < MAX_ONGOING_JOBS; ++i) {
        if (mDefOngoingJobs[i].mDefJobId == 0) {
            DefWork *dw = new DefWork(cmd, sNextJobId, args);
            if (!dw) {
                LOGE("out of memory.");
                return 0;
            }
            if (mCmdQueue.enqueue(dw)) {
                mDefOngoingJobs[i].mDefJobId = sNextJobId++;
                mDefOngoingJobs[i].mDefJobStatus = 0;
                if (sNextJobId == 0) { // handle overflow
                    sNextJobId = 1;
                }
                mDeferredWorkThread.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB,
                        FALSE,
                        FALSE);
                return mDefOngoingJobs[i].mDefJobId;
            } else {
                LOGD("Command queue not active! cmd = %d", cmd);
                delete dw;
                return 0;
            }
        }
    }
    return 0;
}

/*===========================================================================
 * FUNCTION   : initJpegHandle
 *
 * DESCRIPTION: Opens JPEG client and gets a handle.
 *                     Sends Dual cam calibration info if present
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::initJpegHandle() {
    // Check if JPEG client handle is present
    LOGH("E");
    if(!mJpegClientHandle) {
        mm_dimension max_size = {0, 0};
        cam_dimension_t size;

        mParameters.getMaxPicSize(size);
        max_size.w = size.width;
        max_size.h = size.height;

        if (getRelatedCamSyncInfo()->sync_control == CAM_SYNC_RELATED_SENSORS_ON) {
            if (m_bRelCamCalibValid) {
                mJpegClientHandle = jpeg_open(&mJpegHandle, &mJpegMpoHandle,
                        max_size, &mJpegMetadata);
            } else {
                mJpegClientHandle =  jpeg_open(&mJpegHandle, &mJpegMpoHandle,
                        max_size, NULL);
            }
        } else {
            mJpegClientHandle = jpeg_open(&mJpegHandle, NULL, max_size, NULL);
        }
        if (!mJpegClientHandle) {
            LOGE("Error !! jpeg_open failed!! ");
            return UNKNOWN_ERROR;
        }
        // Set JPEG initialized as true to signify that this camera
        // has initialized the handle
        mJpegHandleOwner = true;
    }
    LOGH("X mJpegHandleOwner: %d, mJpegClientHandle: %d camera id: %d",
             mJpegHandleOwner, mJpegClientHandle, mCameraId);
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : deinitJpegHandle
 *
 * DESCRIPTION: Closes JPEG client using handle
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::deinitJpegHandle() {
    int32_t rc = NO_ERROR;
    LOGH("E");
    // Check if JPEG client handle is present and inited by this camera
    if(mJpegHandleOwner && mJpegClientHandle) {
        rc = mJpegHandle.close(mJpegClientHandle);
        if (rc != NO_ERROR) {
            LOGE("Error!! Closing mJpegClientHandle: %d failed",
                     mJpegClientHandle);
        }
        memset(&mJpegHandle, 0, sizeof(mJpegHandle));
        memset(&mJpegMpoHandle, 0, sizeof(mJpegMpoHandle));
        mJpegHandleOwner = false;
    }
    mJpegClientHandle = 0;
    LOGH("X rc = %d", rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : setJpegHandleInfo
 *
 * DESCRIPTION: sets JPEG client handle info
 *
 * PARAMETERS:
 *                  @ops                    : JPEG ops
 *                  @mpo_ops             : Jpeg MPO ops
 *                  @pJpegClientHandle : o/p Jpeg Client Handle
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::setJpegHandleInfo(mm_jpeg_ops_t *ops,
        mm_jpeg_mpo_ops_t *mpo_ops, uint32_t pJpegClientHandle) {

    if (pJpegClientHandle && ops && mpo_ops) {
        LOGH("Setting JPEG client handle %d",
                pJpegClientHandle);
        memcpy(&mJpegHandle, ops, sizeof(mm_jpeg_ops_t));
        memcpy(&mJpegMpoHandle, mpo_ops, sizeof(mm_jpeg_mpo_ops_t));
        mJpegClientHandle = pJpegClientHandle;
        return NO_ERROR;
    }
    else {
        LOGE("Error!! No Handle found: %d",
                pJpegClientHandle);
        return BAD_VALUE;
    }
}

/*===========================================================================
 * FUNCTION   : getJpegHandleInfo
 *
 * DESCRIPTION: gets JPEG client handle info
 *
 * PARAMETERS:
 *                  @ops                    : JPEG ops
 *                  @mpo_ops             : Jpeg MPO ops
 *                  @pJpegClientHandle : o/p Jpeg Client Handle
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::getJpegHandleInfo(mm_jpeg_ops_t *ops,
        mm_jpeg_mpo_ops_t *mpo_ops, uint32_t *pJpegClientHandle) {

    if (NO_ERROR != waitDeferredWork(mInitPProcJob)) {
        LOGE("Init PProc Deferred work failed");
        return UNKNOWN_ERROR;
    }
    // Copy JPEG ops if present
    if (ops && mpo_ops && pJpegClientHandle) {
        memcpy(ops, &mJpegHandle, sizeof(mm_jpeg_ops_t));
        memcpy(mpo_ops, &mJpegMpoHandle, sizeof(mm_jpeg_mpo_ops_t));
        *pJpegClientHandle = mJpegClientHandle;
        LOGH("Getting JPEG client handle %d",
                pJpegClientHandle);
        return NO_ERROR;
    } else {
        return BAD_VALUE;
    }
}

/*===========================================================================
 * FUNCTION   : dequeueDeferredWork
 *
 * DESCRIPTION: function which dequeues deferred tasks
 *
 * PARAMETERS :
 *   @dw      : deferred work
 *   @jobStatus: deferred task job status
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
uint32_t QCamera2HardwareInterface::dequeueDeferredWork(DefWork* dw, int32_t jobStatus)
{
    Mutex::Autolock l(mDefLock);
    for (uint32_t i = 0; i < MAX_ONGOING_JOBS; i++) {
        if (mDefOngoingJobs[i].mDefJobId == dw->id) {
            if (jobStatus != NO_ERROR) {
                mDefOngoingJobs[i].mDefJobStatus = jobStatus;
                LOGH("updating job status %d for id %d",
                         jobStatus, dw->id);
            } else {
                mDefOngoingJobs[i].mDefJobId = 0;
                mDefOngoingJobs[i].mDefJobStatus = 0;
            }
            delete dw;
            mDefCond.broadcast();
            return NO_ERROR;
        }
    }

    return UNKNOWN_ERROR;
}

/*===========================================================================
 * FUNCTION   : getDefJobStatus
 *
 * DESCRIPTION: Gets if a deferred task is success/fail
 *
 * PARAMETERS :
 *   @job_id  : deferred task id
 *
 * RETURN     : NO_ERROR if the job success, otherwise false
 *
 * PRECONDITION : mDefLock is held by current thread
 *==========================================================================*/
int32_t QCamera2HardwareInterface::getDefJobStatus(uint32_t &job_id)
{
    for (uint32_t i = 0; i < MAX_ONGOING_JOBS; i++) {
        if (mDefOngoingJobs[i].mDefJobId == job_id) {
            if ( NO_ERROR != mDefOngoingJobs[i].mDefJobStatus ) {
                LOGE("job_id (%d) was failed", job_id);
                return mDefOngoingJobs[i].mDefJobStatus;
            }
            else
                return NO_ERROR;
        }
    }
    return NO_ERROR;
}


/*===========================================================================
 * FUNCTION   : checkDeferredWork
 *
 * DESCRIPTION: checks if a deferred task is in progress
 *
 * PARAMETERS :
 *   @job_id  : deferred task id
 *
 * RETURN     : true if the task exists, otherwise false
 *
 * PRECONDITION : mDefLock is held by current thread
 *==========================================================================*/
bool QCamera2HardwareInterface::checkDeferredWork(uint32_t &job_id)
{
    for (uint32_t i = 0; i < MAX_ONGOING_JOBS; i++) {
        if (mDefOngoingJobs[i].mDefJobId == job_id) {
            return (NO_ERROR == mDefOngoingJobs[i].mDefJobStatus);
        }
    }
    return false;
}

/*===========================================================================
 * FUNCTION   : waitDeferredWork
 *
 * DESCRIPTION: waits for a deferred task to finish
 *
 * PARAMETERS :
 *   @job_id  : deferred task id
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::waitDeferredWork(uint32_t &job_id)
{
    Mutex::Autolock l(mDefLock);

    if (job_id == 0) {
        LOGD("Invalid job id %d", job_id);
        return NO_ERROR;
    }

    while (checkDeferredWork(job_id) == true ) {
        mDefCond.waitRelative(mDefLock, CAMERA_DEFERRED_THREAD_TIMEOUT);
    }
    return getDefJobStatus(job_id);
}

/*===========================================================================
 * FUNCTION   : scheduleBackgroundTask
 *
 * DESCRIPTION: Run a requested task in the deferred thread
 *
 * PARAMETERS :
 *   @bgTask  : Task to perform in the background
 *
 * RETURN     : job id of deferred job
 *            : 0 in case of error
 *==========================================================================*/
uint32_t QCamera2HardwareInterface::scheduleBackgroundTask(BackgroundTask* bgTask)
{
    DeferWorkArgs args;
    memset(&args, 0, sizeof(DeferWorkArgs));
    args.genericArgs = bgTask;

    return queueDeferredWork(CMD_DEF_GENERIC, args);
}

/*===========================================================================
 * FUNCTION   : waitForBackgroundTask
 *
 * DESCRIPTION: Wait for a background task to complete
 *
 * PARAMETERS :
 *   @taskId  : Task id to wait for
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::waitForBackgroundTask(uint32_t& taskId)
{
    return waitDeferredWork(taskId);
}

/*===========================================================================
 * FUNCTION   : needDeferedAllocation
 *
 * DESCRIPTION: Function to decide background task for streams
 *
 * PARAMETERS :
 *   @stream_type  : stream type
 *
 * RETURN     : true - if background task is needed
 *              false -  if background task is NOT needed
 *==========================================================================*/
bool QCamera2HardwareInterface::needDeferred(cam_stream_type_t stream_type)
{
    if ((stream_type == CAM_STREAM_TYPE_PREVIEW && mPreviewWindow == NULL)
            || (stream_type == CAM_STREAM_TYPE_ANALYSIS)) {
        return FALSE;
    }

    if ((stream_type == CAM_STREAM_TYPE_RAW)
            && (mParameters.getofflineRAW())) {
        return FALSE;
    }

    if ((stream_type == CAM_STREAM_TYPE_SNAPSHOT)
            && (!mParameters.getRecordingHintValue())){
        return TRUE;
    }

    if ((stream_type == CAM_STREAM_TYPE_PREVIEW)
            || (stream_type == CAM_STREAM_TYPE_METADATA)
            || (stream_type == CAM_STREAM_TYPE_RAW)
            || (stream_type == CAM_STREAM_TYPE_POSTVIEW)) {
        return TRUE;
    }

    if (stream_type == CAM_STREAM_TYPE_VIDEO) {
        return FALSE;
    }
    return FALSE;
}

/*===========================================================================
 * FUNCTION   : isRegularCapture
 *
 * DESCRIPTION: Check configuration for regular catpure
 *
 * PARAMETERS :
 *
 * RETURN     : true - regular capture
 *              false - other type of capture
 *==========================================================================*/
bool QCamera2HardwareInterface::isRegularCapture()
{
    bool ret = false;

    if (numOfSnapshotsExpected() == 1 &&
        !isLongshotEnabled() &&
        !mParameters.isHDREnabled() &&
        !mParameters.getRecordingHintValue() &&
        !isZSLMode() && (!mParameters.getofflineRAW()|| mParameters.getQuadraCfa())) {
            ret = true;
    }
    return ret;
}

/*===========================================================================
 * FUNCTION   : getLogLevel
 *
 * DESCRIPTION: Reads the log level property into a variable
 *
 * PARAMETERS :
 *   None
 *
 * RETURN     :
 *   None
 *==========================================================================*/
void QCamera2HardwareInterface::getLogLevel()
{
    char prop[PROPERTY_VALUE_MAX];

    property_get("persist.camera.kpi.debug", prop, "1");
    gKpiDebugLevel = atoi(prop);
    return;
}

/*===========================================================================
 * FUNCTION   : getSensorType
 *
 * DESCRIPTION: Returns the type of sensor being used whether YUV or Bayer
 *
 * PARAMETERS :
 *   None
 *
 * RETURN     : Type of sensor - bayer or YUV
 *
 *==========================================================================*/
cam_sensor_t QCamera2HardwareInterface::getSensorType()
{
    return gCamCapability[mCameraId]->sensor_type.sens_type;
}

/*===========================================================================
 * FUNCTION   : startRAWChannel
 *
 * DESCRIPTION: start RAW Channel
 *
 * PARAMETERS :
 *   @pChannel  : Src channel to link this RAW channel.
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::startRAWChannel(QCameraChannel *pMetaChannel)
{
    int32_t rc = NO_ERROR;
    QCameraChannel *pChannel = m_channels[QCAMERA_CH_TYPE_RAW];
    if ((NULL != pChannel) && (mParameters.getofflineRAW())) {
        // Find and try to link a metadata stream from preview channel
        QCameraStream *pMetaStream = NULL;

        if (pMetaChannel != NULL) {
            uint32_t streamNum = pMetaChannel->getNumOfStreams();
            QCameraStream *pStream = NULL;
            for (uint32_t i = 0 ; i < streamNum ; i++ ) {
                pStream = pMetaChannel->getStreamByIndex(i);
                if ((NULL != pStream) &&
                        (CAM_STREAM_TYPE_METADATA == pStream->getMyType())) {
                    pMetaStream = pStream;
                    break;
                }
            }

            if (NULL != pMetaStream) {
                rc = pChannel->linkStream(pMetaChannel, pMetaStream);
                if (NO_ERROR != rc) {
                    LOGE("Metadata stream link failed %d", rc);
                }
            }
        }
        rc = pChannel->start();
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : startRecording
 *
 * DESCRIPTION: start recording impl
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::stopRAWChannel()
{
    int32_t rc = NO_ERROR;
    rc = stopChannel(QCAMERA_CH_TYPE_RAW);
    return rc;
}

/*===========================================================================
 * FUNCTION   : isLowPowerMode
 *
 * DESCRIPTION: Returns TRUE if low power mode settings are to be applied for video recording
 *
 * PARAMETERS :
 *   None
 *
 * RETURN     : TRUE/FALSE
 *
 *==========================================================================*/
bool QCamera2HardwareInterface::isLowPowerMode()
{
    cam_dimension_t dim;
    mParameters.getStreamDimension(CAM_STREAM_TYPE_VIDEO, dim);

    char prop[PROPERTY_VALUE_MAX];
    property_get("camera.lowpower.record.enable", prop, "0");
    int enable = atoi(prop);

    //Enable low power mode if :
    //1. Video resolution is 2k (2048x1080) or above and
    //2. camera.lowpower.record.enable is set

    bool isLowpower = mParameters.getRecordingHintValue() && enable
            && ((dim.width * dim.height) >= (2048 * 1080));
    return isLowpower;
}

}; // namespace qcamera
