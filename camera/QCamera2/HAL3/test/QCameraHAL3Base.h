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
#ifndef __HAL3APP_H__
#define __HAL3APP_H__

#include <hardware/hardware.h>
#include <dlfcn.h>
#include <hardware/camera3.h>
#include <sys/stat.h>
#include <ctype.h>
#include <list>
#include <camera/CameraMetadata.h>
#include <linux/msm_ion.h>
#include <errno.h>
#include <fcntl.h>
#include <system/window.h>
#include <stdio.h>
#include <pthread.h>
#include <poll.h>

extern "C" {
#include "mm_camera_dbg.h"
}

#define HAL3_DATASPACE_UNKNOWN 0x0
#define HAL3_DATASPACE_ARBITRARY 0x1
#define HAL3_DATASPACE_JFIF 0x101
#define FLAGS_VIDEO_ENCODER 0x00010000

#define PREVIEW_WIDTH 1440
#define PREVIEW_HEIGHT 1080

#define VIDEO_WIDTH 1920
#define VIDEO_HEIGHT 1080

#define SNAPSHOT_CAPTURE_WIDTH 5344
#define SNAPSHOT_CAPTURE_HEIGHT 4008

#define RAWSNAPSHOT_CAPTURE_WIDTH 5344
#define RAWSNAPSHOT_CAPTURE_HEIGHT 4016

namespace qcamera {

typedef enum {
    HAL3_CAM_OK,
    HAL3_CAM_E_GENERAL,
    HAL3_CAM_E_NO_MEMORY,
    HAL3_CAM_E_NOT_SUPPORTED,
    HAL3_CAM_E_INVALID_INPUT,
    HAL3_CAM_E_INVALID_OPERATION,
    HAL3_CAM_E_ENCODE,
    HAL3_CAM_E_BUFFER_REG,
    HAL3_CAM_E_PMEM_ALLOC,
    HAL3_CAM_E_CAPTURE_FAILED,
    HAL3_CAM_E_CAPTURE_TIMEOUT,
} hal3_camera_status_type_t;

typedef enum {
    PROCESS_BUFFER,
} buffer_thread_msg_type_t;

typedef struct {
    camera3_device_t *device;
    camera3_callback_ops callback_ops;
    struct camera_info cam_info;
    camera_module_callbacks_t module_cb;
} hal3_camera_test_obj_t;

typedef struct {
    int fd;
    int ion_fd;
    ion_user_handle_t ion_handle;
    size_t size;
} hal3_camtest_meminfo_t;

typedef struct {
    buffer_thread_msg_type_t msg;
    bool stop_thread;
} buffer_thread_msg_t;

typedef struct {
    void *ptr;
    camera_module_t* halModule_t;
} hal3_interface_lib_t;

typedef struct {
    uint8_t num_cameras;
    vendor_tag_ops_t mVendorTagOps;
    hal3_interface_lib_t hal3_lib;
} hal3_camera_app_t;

typedef struct {
    hal3_camera_app_t app_obj;
    hal3_camera_test_obj_t test_obj;
} hal3_camera_lib_test;

typedef struct {
    pthread_t td;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int32_t readfd;
    int32_t writefd;
    void *data_obj;
    bool is_thread_started;
    int testcase;
} buffer_thread_t;

extern int32_t pfd[2];
typedef hal3_camera_lib_test hal3_camera_lib_handle;

class CameraHAL3Base
{
    friend class MainTestContext;
protected:
    int mCameraIndex;
public:
    CameraHAL3Base();
    CameraHAL3Base(int cameraIndex);
    hal3_camera_lib_test *mLibHandle;
    int mFrameCount;
    int fps;
    int mSecElapsed;
    int mTestCaseSelected;
    int mPreviewRunning;
    int mVideoRunning;
    int mSnapShotRunning;

    int hal3appCamInitialize(int camid, hal3_camera_test_obj_t *my_test_obj);
    void hal3appCamCapabilityGet(hal3_camera_lib_test *handle,int camid);
    int hal3appCameraLibOpen(int );
    int hal3appTestLoad(hal3_camera_app_t *);
    int hal3appCamOpen(hal3_camera_app_t *,
             int,
             hal3_camera_test_obj_t *);
    int hal3appCameraPreviewInit(int, int, int, int);
    int hal3appCameraVideoInit(int, int camid, int w, int h);
    int hal3appCameraCaptureInit(hal3_camera_lib_test *, int, int);
    int hal3appRawCaptureInit(hal3_camera_lib_test *handle, int camid, int );
    int hal3appCameraTestLoad();
    void hal3appCheckStream(int testcase, int camid);
};

}
#endif