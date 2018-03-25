/*Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#include "QCameraHAL3MainTestContext.h"
#include "QCameraHAL3SnapshotTest.h"
#include "QCameraHAL3RawSnapshotTest.h"
#include "QCameraHAL3PreviewTest.h"

#ifdef QCAMERA_HAL3_SUPPORT
#define LIB_PATH /usr/lib/hw/camera.msm8953.so
#else
#define LIB_PATH /system/lib/hw/camera.msm8953.so
#endif

extern "C" {
extern int set_camera_metadata_vendor_ops(const vendor_tag_ops_t *query_ops);
}

/*#ifdef CAMERA_CHIPSET_8953
#define CHIPSET_LIB lib/hw/camera.msm8953.so
#else
#define CHIPSET_LIB lib/hw/camera.msm8937.so
#endif*/

#define CAM_LIB(s) STR_LIB_PATH(s)
#define STR_LIB_PATH(s) #s

namespace qcamera {

QCameraHAL3PreviewTest *mPreviewtestCase = NULL;
QCameraHAL3VideoTest *mVideotestCase = NULL;
QCameraHAL3SnapshotTest *mSnapshottestCase = NULL;
QCameraHAL3RawSnapshotTest *mRawSnapshottestCase = NULL;

struct timeval start_time;
int capture_received;
int pfd[2];
extern int test_case_end;
extern int snapshot_buffer;

pthread_cond_t mRequestAppCond;
std::list<uint32_t> PreviewQueue;
std::list<uint32_t> VideoQueue;

pthread_mutex_t TestAppLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mCaptureRequestLock = PTHREAD_MUTEX_INITIALIZER;



static void camera_device_status_change(
        const struct camera_module_callbacks* callbacks,
        int camera_id, int new_status)
{
    /* Stub function */
}

static void torch_mode_status_change(
        const struct camera_module_callbacks* callbacks,
        const char* camera_id, int new_status)
{
    /* Stub function */
}

static void Notify(
        const camera3_callback_ops *cb,
        const camera3_notify_msg *msg)
{
    /* Stub function */
}

static void ProcessCaptureResult(
        const camera3_callback_ops *cb,
        const camera3_capture_result *result)
{
    buffer_thread_msg_t msg;
    extern CameraHAL3Base *mCamHal3Base;
    int frame_num;
    extern int req_sent;
    extern int preview_buffer_allocated;
    extern int video_buffer_allocated;
    double elapsedTime;
    int num;
    struct timeval end_time;
    LOGD("Cam Capture Result Callback %d and %d",
            result->num_output_buffers, mCamHal3Base->mFrameCount);
    if (mCamHal3Base->mTestCaseSelected == MENU_START_PREVIEW ||
            mCamHal3Base->mTestCaseSelected == MENU_START_VIDEO) {
        if (result->num_output_buffers == 1) {
            frame_num = result->frame_number;
            LOGD("Frame width:%d and height:%d and format:%d",
                    result->output_buffers->stream->width,
                    result->output_buffers->stream->height,
                    result->output_buffers->stream->format);
            (mCamHal3Base->mFrameCount)++;
            LOGD("Preview/Video Capture Result %d and fcount: %d and req_Sent:%d and %d ",
            result->num_output_buffers, mCamHal3Base->mFrameCount, req_sent, result->frame_number);
            if (test_case_end == 0) {
                if (mCamHal3Base->mTestCaseSelected == MENU_START_PREVIEW) {
                    num = (result->frame_number)%preview_buffer_allocated;
                    PreviewQueue.push_back(num);
                }
                else {
                    num = (result->frame_number)%video_buffer_allocated;
                           VideoQueue.push_back(num);
                }
                pthread_cond_signal(&mRequestAppCond);
                memset(&msg, 0, sizeof(buffer_thread_msg_t));
            }
        }
    }
    else {
        extern int fcount_captured;
        if (result->num_output_buffers == 1) {
            LOGD("snapshot/Raw Capture1 Result Callback %d and %d",
                    result->num_output_buffers, fcount_captured);
            (mCamHal3Base->mFrameCount)++;
            fcount_captured++;
            LOGD("\n Capture %d done preparing for capture ", fcount_captured);
            memset(&msg, 0, sizeof(buffer_thread_msg_t));
            write(pfd[1], &msg, sizeof(buffer_thread_msg_t));
        }
    }
}

CameraHAL3Base::CameraHAL3Base(int cameraIndex) :
    mCameraIndex(cameraIndex),
    mLibHandle(NULL),
    mFrameCount(0),
    mSecElapsed(1),
    mTestCaseSelected(0),
    mPreviewRunning(0),
    mVideoRunning(0),
    mSnapShotRunning(0)
{

}


int CameraHAL3Base::hal3appCameraTestLoad()
{
    int rc = HAL3_CAM_OK;
    int numCam;
    int32_t res = 0;
    hal3_camera_test_obj_t *my_test_obj;
    mLibHandle = new hal3_camera_lib_test;
    memset(mLibHandle, 0, sizeof(hal3_camera_lib_handle));
    rc = hal3appTestLoad(&mLibHandle->app_obj);
    camera_module_t *my_if_handle = mLibHandle->app_obj.hal3_lib.halModule_t;
    if (HAL3_CAM_OK != rc) {
        LOGE("hal3 err\n");
        goto EXIT;
    }

    numCam = my_if_handle->get_number_of_cameras();
    printf("\n Number of Cameras are : %d ", numCam);
    if (my_if_handle->get_vendor_tag_ops) {
        mLibHandle->app_obj.mVendorTagOps = vendor_tag_ops_t();
        my_if_handle->get_vendor_tag_ops(&(mLibHandle->app_obj.mVendorTagOps));

        res = set_camera_metadata_vendor_ops(&(mLibHandle->app_obj.mVendorTagOps));
        if (0 != res) {
            printf("%s: Could not set vendor tag descriptor, "
                    "received error %s (%d). \n", __func__,
                    strerror(-res), res);
            goto EXIT;
        }
    }
    my_test_obj = &(mLibHandle->test_obj);
    my_test_obj->module_cb.torch_mode_status_change = &torch_mode_status_change;
    my_test_obj->module_cb.camera_device_status_change = &camera_device_status_change;
    my_if_handle->set_callbacks(&(my_test_obj->module_cb));
    my_if_handle->get_camera_info(0, &(mLibHandle->test_obj.cam_info));
    return numCam;
    EXIT:
    return rc;

}

int CameraHAL3Base::hal3appCameraLibOpen(int camid)
{
    int rc;
    rc = hal3appCamOpen(&mLibHandle->app_obj, (int)camid, &(mLibHandle->test_obj));
    if (rc != HAL3_CAM_OK) {
        LOGE("hal3appCamOpen() camidx=%d, err=%d\n",
                camid, rc);
        goto EXIT;
    }
    rc = hal3appCamInitialize((int)camid, &mLibHandle->test_obj);
    EXIT:
    return rc;
}

int CameraHAL3Base::hal3appTestLoad(hal3_camera_app_t *my_hal3_app)
{
    memset(&my_hal3_app->hal3_lib, 0, sizeof(hal3_interface_lib_t));
    printf("\nLibrary path is :%s", CAM_LIB(LIB_PATH));
    my_hal3_app->hal3_lib.ptr = dlopen(CAM_LIB(LIB_PATH), RTLD_NOW);

    if (!my_hal3_app->hal3_lib.ptr) {
        LOGE("Error opening HAL libraries %s\n",
                dlerror());
        return -HAL3_CAM_E_GENERAL;
    }
    my_hal3_app->hal3_lib.halModule_t =
        (camera_module_t*)dlsym(my_hal3_app->hal3_lib.ptr, HAL_MODULE_INFO_SYM_AS_STR);
    if (my_hal3_app->hal3_lib.halModule_t == NULL) {
        LOGE("Error opening HAL library %s\n",
                dlerror());
        return -HAL3_CAM_E_GENERAL;
    }
    return HAL3_CAM_OK;
}

int CameraHAL3Base::hal3appCamOpen(
        hal3_camera_app_t *my_hal3_app,
        int camid,
        hal3_camera_test_obj_t *my_test_obj)
{
    int rc = 0;
    int numCam;
    camera_module_t *my_if_handle = my_hal3_app->hal3_lib.halModule_t;
    my_if_handle->common.methods->open(&(my_if_handle->common), "0",
            reinterpret_cast<hw_device_t**>(&(my_test_obj->device)));
    printf("\n Camera ID %d Opened \n", camid);
    return HAL3_CAM_OK;
}

int CameraHAL3Base::hal3appCamInitialize(int camid, hal3_camera_test_obj_t *my_test_obj)
{
    int rc = 0;
    camera3_device_t *device_handle = my_test_obj->device;
    my_test_obj->callback_ops.notify = &Notify;
    my_test_obj->callback_ops.process_capture_result = &ProcessCaptureResult;
    rc = device_handle->ops->initialize(my_test_obj->device, &(my_test_obj->callback_ops));
    if (rc != HAL3_CAM_OK) {
        LOGE("hal3appCamInitialize() camidx=%d, err=%d\n",
                camid, rc);
        goto EXIT;
    }
    EXIT:
    return rc;
}


void CameraHAL3Base::hal3appCheckStream(int testcase, int camid)
{
    if (testcase != MENU_START_PREVIEW) {
        if (mPreviewtestCase != NULL) {
            mPreviewtestCase->previewTestEnd(mLibHandle, camid);
            delete mPreviewtestCase;
            mPreviewtestCase = NULL;
        }
    }
    if (testcase != MENU_START_VIDEO){
        if (mVideotestCase != NULL) {
            mVideotestCase->videoTestEnd(mLibHandle, camid);
            delete mVideotestCase;
            mVideotestCase = NULL;
        }
    }

    if (testcase != MENU_START_CAPTURE){
        if (mSnapshottestCase != NULL) {
            delete mSnapshottestCase;
            mSnapshottestCase = NULL;
        }
    }

    if (testcase != MENU_START_RAW_CAPTURE) {
        if (mRawSnapshottestCase != NULL) {
            delete mRawSnapshottestCase;
            mRawSnapshottestCase = NULL;
        }
    }
}


int CameraHAL3Base::hal3appCameraPreviewInit(int testcase, int camid, int w, int h)
{
    extern int req_sent;
    int testCaseEndComplete = 0;
    int CaptureRequestSent = 0;
    if (w == 0 || h == 0) {
        printf("\n Frame dimension is wrong");
        return -1;
    }
    if ( mPreviewtestCase != NULL) {
        return 0;
    }
    else {
        testCaseEndComplete = 0;
        do {
            if (mVideoRunning == 1) {
                hal3appCheckStream(MENU_START_PREVIEW, camid);
            }
            pthread_mutex_lock(&TestAppLock);
            mTestCaseSelected = MENU_START_PREVIEW;
            if (mVideoRunning != 1) {
                hal3appCheckStream(MENU_START_PREVIEW, camid);
            }
            mPreviewtestCase = new QCameraHAL3PreviewTest(0);
            printf("\n\n Testing the Resolution : %d X %d", w, h);
            req_sent = 0;
            PreviewQueue.clear();
            capture_received = 0; mSecElapsed = 1;
            snapshot_buffer = -1; mFrameCount = 0;
            mPreviewtestCase->width = w; mPreviewtestCase->height = h;
            mPreviewtestCase->initTest(mLibHandle,
                    (int) MENU_START_PREVIEW, camid, w, h);
            testCaseEndComplete = 1;
        }while(testCaseEndComplete != 1);
    }
    return 0;
}

int CameraHAL3Base::hal3appCameraVideoInit(int testcase, int camid, int w, int h)
{
    extern int req_sent;
    int testCaseEndComplete = 0;
    int CaptureRequestSent = 0;
    if (w == 0 || h == 0) {
        printf("\n Frame dimension is wrong");
        return -1;
    }

    if (mVideotestCase != NULL) {
            return 0;
    }
    else {
        testCaseEndComplete = 0;
        do {
            if (mPreviewRunning == 1) {
                hal3appCheckStream(MENU_START_VIDEO, camid);
            }
            pthread_mutex_lock(&TestAppLock);
            mTestCaseSelected = MENU_START_VIDEO;
            if (mPreviewRunning != 1) {
                hal3appCheckStream(MENU_START_VIDEO, camid);
            }
            mVideotestCase = new QCameraHAL3VideoTest(0);
            VideoQueue.clear();
            printf("\n\nTesting the Resolution : %d X %d", w, h);
            req_sent = 0;
            capture_received =0; mSecElapsed = 1; test_case_end = 0;
            mVideotestCase->width = w; mVideotestCase->height = h;
            snapshot_buffer = -1; mFrameCount = 0;
            mVideotestCase->initTest(mLibHandle,
                    (int) MENU_START_VIDEO, camid, w, h);
            testCaseEndComplete = 1;
        }while(testCaseEndComplete !=1);
    }
    return 0;
}


int CameraHAL3Base::hal3appRawCaptureInit(hal3_camera_lib_test *handle, int camid, int req_cap)
{
    int testCaseEndComplete = 0;
    if (mSnapShotRunning != 1) {
        hal3appCheckStream(MENU_START_RAW_CAPTURE, camid);
    }
    testCaseEndComplete = 0;
    do {
        pthread_mutex_lock(&TestAppLock);
        if (mSnapShotRunning == 1) {
            hal3appCheckStream(MENU_START_RAW_CAPTURE, camid);
        }
        printf("\n capture:%d", req_cap);
        mTestCaseSelected = MENU_START_RAW_CAPTURE;
        mRawSnapshottestCase = new QCameraHAL3RawSnapshotTest(req_cap);
        mRawSnapshottestCase->mRequestedCapture = req_cap;
        mRawSnapshottestCase->initTest(mLibHandle,
        (int) MENU_START_RAW_CAPTURE, camid, RAWSNAPSHOT_CAPTURE_WIDTH,
                RAWSNAPSHOT_CAPTURE_HEIGHT);
        testCaseEndComplete = 1;
    }while(testCaseEndComplete !=1);
    return 0;
}

int CameraHAL3Base::hal3appCameraCaptureInit(hal3_camera_lib_test *handle,
        int camid, int req_cap)
{
    int testCaseEndComplete = 0;
    if (mSnapShotRunning != 1) {
        hal3appCheckStream(MENU_START_CAPTURE, camid);
    }
    testCaseEndComplete = 0;
    do {
        pthread_mutex_lock(&TestAppLock);
        if (mSnapShotRunning == 1) {
            hal3appCheckStream(MENU_START_CAPTURE, camid);
        }
        printf("\n capture:%d", req_cap);
        mTestCaseSelected = MENU_START_CAPTURE;
        mSnapshottestCase = new QCameraHAL3SnapshotTest(req_cap);
        mSnapshottestCase->mRequestedCapture = req_cap;
        mSnapshottestCase->initTest(mLibHandle,
            (int) MENU_START_CAPTURE, camid, SNAPSHOT_CAPTURE_WIDTH, SNAPSHOT_CAPTURE_HEIGHT);
        testCaseEndComplete = 1;
    }while(testCaseEndComplete != 1);
    return 0;
}

void CameraHAL3Base::hal3appCamCapabilityGet(hal3_camera_lib_test *handle, int camid)
{
    camera_module_t *my_if_handle = ((handle->app_obj).hal3_lib).halModule_t;
    hal3_camera_test_obj_t *test_obj_handle = &(handle->test_obj);
    camera_metadata_entry entry_hal3app;
    camera_info info;
    int i = 0, count = 0, j = 0;
    long int num = 0;
    int32_t *available_hdr = NULL, *available_svhdr = NULL, *available_ir = NULL;
    android::CameraMetadata hal3_cam_settings;
    printf("\n Number of Cameras are : %d and %p", camid, &(test_obj_handle->cam_info));
    my_if_handle->get_camera_info(camid, &(test_obj_handle->cam_info));
    info = test_obj_handle->cam_info;
    hal3_cam_settings = (info.static_camera_characteristics);
}

}

