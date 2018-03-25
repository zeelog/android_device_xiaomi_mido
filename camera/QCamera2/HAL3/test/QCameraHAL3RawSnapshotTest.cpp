/* Copyright (c) 2016, The Linux Foundation. All rights reserved.
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

#include "QCameraHAL3RawSnapshotTest.h"
#include "QCameraHAL3MainTestContext.h"

namespace qcamera {
hal3_camera_lib_test *RawSnapshot_CamObj_handle;
extern int fcount_captured;
extern pthread_mutex_t TestAppLock;

QCameraHAL3RawSnapshotTest::QCameraHAL3RawSnapshotTest(int req_cap) :
    QCameraHAL3Test(0),
    mRawCaptureHandle(NULL),
    mRawSnapshotStream(NULL),
    mRequestedCapture(req_cap)
{

}

void QCameraHAL3RawSnapshotTest::initTest(hal3_camera_lib_test *handle,
        int testcase, int camid, int w, int h)
{
    int i; fcount_captured = 0;
    RawSnapshot_CamObj_handle = handle;
    LOGD("\n Raw buffer thread created");
    configureRawSnapshotStream(&(handle->test_obj), camid, w, h);
    constructDefaultRequest(&(handle->test_obj), 0);
    LOGD("\n Raw Snapshot Default stream setting read");
    rawProcessThreadCreate(MENU_START_RAW_CAPTURE,
            rawProcessBuffers);
    mRequest.frame_number = 0;
    LOGD("\nRaw  Snapshot stream configured");
    rawProcessCaptureRequest(&(handle->test_obj), 0);
    LOGD("\nRaw  Snapshot Process Capture Request Sent");
}

void QCameraHAL3RawSnapshotTest::constructDefaultRequest(
        hal3_camera_test_obj_t *my_test_obj, int camid)
{
    camera3_device_t *device_handle = my_test_obj->device;
    mMetaDataPtr[0] = device_handle->ops->construct_default_request_settings(
            my_test_obj->device, CAMERA3_TEMPLATE_PREVIEW);
    mMetaDataPtr[1] = device_handle->ops->construct_default_request_settings(
            my_test_obj->device, CAMERA3_TEMPLATE_STILL_CAPTURE);
}

void QCameraHAL3RawSnapshotTest::configureRawSnapshotStream(hal3_camera_test_obj_t *my_test_obj,
                                    int camid, int w, int h)
{
    camera3_stream_t *r_stream, *p_stream;
    camera3_device_t *device_handle = my_test_obj->device;
    mPreviewStream = new camera3_stream_t;
    mRawSnapshotStream = new camera3_stream_t;

    memset(mPreviewStream, 0, sizeof(camera3_stream_t));
    memset(mRawSnapshotStream, 0, sizeof(camera3_stream_t));

    mPreviewStream = initStream(CAMERA3_STREAM_OUTPUT, camid, PREVIEW_WIDTH, PREVIEW_HEIGHT, 0,
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, HAL3_DATASPACE_UNKNOWN);
    mRawSnapshotStream = initStream(CAMERA3_STREAM_OUTPUT, camid, RAWSNAPSHOT_CAPTURE_WIDTH,
            RAWSNAPSHOT_CAPTURE_HEIGHT, 0, HAL_PIXEL_FORMAT_RAW16, HAL3_DATASPACE_ARBITRARY);
    mRawSnapshotConfig = configureStream(CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE, 2);

    mRawSnapshotConfig.streams[0] = mPreviewStream;
    mRawSnapshotConfig.streams[1] = mRawSnapshotStream;
    device_handle->ops->configure_streams(my_test_obj->device, &(mRawSnapshotConfig));
}


void QCameraHAL3RawSnapshotTest::rawProcessCaptureRequest(
        hal3_camera_test_obj_t *my_test_obj, int camid)
{
    int width, height;
    static int num = 1;

    camera3_device_t *device_handle = my_test_obj->device;
    width = mRawSnapshotStream->width;
    height = mRawSnapshotStream->height;
    rawAllocateBuffers(width, height);
    mRequest.frame_number = 0;
    mRequest.settings = mMetaDataPtr[1];
    mRequest.input_buffer = NULL;
    mRequest.num_output_buffers = 1;
    mRawSnapshotStreamBuffs.stream = mRawSnapshotStream;
    mRawSnapshotStreamBuffs.status = 0;
    mRawSnapshotStreamBuffs.buffer = (const native_handle_t**)&mRawCaptureHandle;
    mRawSnapshotStreamBuffs.release_fence = -1;
    mRawSnapshotStreamBuffs.acquire_fence = -1;
    mRequest.output_buffers = &(mRawSnapshotStreamBuffs);
    LOGD("Calling HAL3APP capture request ");
    device_handle->ops->process_capture_request(my_test_obj->device, &(mRequest));
}

void QCameraHAL3RawSnapshotTest::rawProcessCaptureRequestRepeat(
        hal3_camera_lib_test *my_hal3test_obj, int camid)
{
    hal3_camera_test_obj_t *my_test_obj = &(my_hal3test_obj->test_obj);
    LOGD("\nRaw Requested Capture : %d and Received Capture : %d",
            mRequestedCapture, fcount_captured);
    if (mRequestedCapture == fcount_captured) {
        LOGD("\n Raw Snapshot is running successfully Ending test");
        fflush(stdout);
        LOGD("\n Capture Done , Recieved Frame : %d", fcount_captured);
        rawTestEnd(my_hal3test_obj, camid);
    }
    else {
        camera3_device_t *device_handle = my_test_obj->device;
        (mRequest.frame_number)++;
        mRequest.settings = mMetaDataPtr[1];
        mRequest.input_buffer = NULL;
        mRequest.num_output_buffers = 1;
        mRawSnapshotStreamBuffs.stream = mRawSnapshotStream;
        mRawSnapshotStreamBuffs.buffer = (const native_handle_t**)&mRawCaptureHandle;
        mRawSnapshotStreamBuffs.release_fence = -1;
        mRawSnapshotStreamBuffs.acquire_fence = -1;
        mRequest.output_buffers = &(mRawSnapshotStreamBuffs);
        LOGD("Calling HAL3APP repeat capture request repeat ");
        device_handle->ops->process_capture_request(my_test_obj->device, &(mRequest));
    }
}

void QCameraHAL3RawSnapshotTest::rawTestEnd(
        hal3_camera_lib_test *my_hal3test_obj, int camid)
{
    buffer_thread_msg_t msg;
    extern pthread_mutex_t gCamLock;
    hal3_camera_test_obj_t *my_test_obj = &(my_hal3test_obj->test_obj);
    camera3_device_t *device_handle = my_test_obj->device;
    device_handle->ops->flush(my_test_obj->device);
    LOGD("%s Closing Camera", __func__);
    /* Free the Allocated ION Memory */
    ioctl(mRawCaptureMemInfo.ion_fd, ION_IOC_FREE, &mRawCaptureMemInfo.ion_handle);
    close(mRawCaptureMemInfo.ion_fd);
    mRawCaptureMemInfo.ion_fd = -1;
    /* Close the Thread */
    memset(&msg, 0, sizeof(buffer_thread_msg_t));
    msg.stop_thread = 1;
    write(pfd[1], &msg, sizeof(buffer_thread_msg_t));
}


void QCameraHAL3RawSnapshotTest::rawAllocateBuffers(int width, int height)
{
    mRawCaptureHandle = allocateBuffers(width, height, &mRawCaptureMemInfo);
}

bool QCameraHAL3RawSnapshotTest::rawProcessThreadCreate(int testcase_id,
        void *(*hal3_thread_ops)(void *))
{
    int32_t ret = 0;
    buffer_thread_t thread;
    pthread_attr_t attr;
    if (pipe(pfd) < 0) {
        LOGE("%s: Error in creating the pipe", __func__);
    }
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_mutex_init(&thread.mutex, NULL);
    pthread_cond_init(&thread.cond, NULL);
    thread.is_thread_started = 0;
    thread.readfd = pfd[0];
    thread.writefd = pfd[1];
    thread.data_obj = this;
    ret = pthread_create(&thread.td, &attr, hal3_thread_ops, &thread );
    pthread_setname_np(thread.td, "TestApp_Thread");
    if (ret < 0) {
        LOGE("Failed to create status thread");
        return 0;
    }
    pthread_mutex_lock(&thread.mutex);
    while(thread.is_thread_started == 0) {
        pthread_cond_wait(&thread.cond, &thread.mutex);
    }
    pthread_mutex_unlock(&thread.mutex);
    return 1;
}

void QCameraHAL3RawSnapshotTest::captureRequestRepeat(
        hal3_camera_lib_test *my_hal3test_obj, int camid, int testcase)
{
}

void * rawProcessBuffers(void *data) {
    buffer_thread_t *thread = (buffer_thread_t*)data;
    int32_t readfd, writefd;
    hal3_camera_lib_test *hal3_test_handle;
    pthread_mutex_lock(&thread->mutex);
    thread->is_thread_started = 1;
    readfd = thread->readfd;
    writefd = thread->writefd;
    QCameraHAL3RawSnapshotTest *obj;
    obj = (QCameraHAL3RawSnapshotTest *)thread->data_obj;
    pthread_cond_signal(&thread->cond);
    pthread_mutex_unlock(&thread->mutex);
    struct pollfd pollfds;
    int32_t num_of_fds = 1;
    bool rthread_exit = 0;
    int32_t ready = 0;
    pollfds.fd = readfd;
    pollfds.events = POLLIN | POLLPRI;
    while(!rthread_exit) {
        ready = poll(&pollfds, (nfds_t)num_of_fds, -1);
        if (ready > 0) {
            if (pollfds.revents & (POLLIN | POLLPRI)) {
                ssize_t nread = 0;
                buffer_thread_msg_t msg;
                nread = read(pollfds.fd, &msg, sizeof(buffer_thread_msg_t));
                if (nread < 0) {
                    LOGE("Unable to read the message");
                }
                if (msg.stop_thread) {
                    break;
                }
                hal3_test_handle = RawSnapshot_CamObj_handle;
                obj->rawProcessCaptureRequestRepeat(hal3_test_handle, 0);
            }
        }
        else {
            LOGE("Unable to poll exiting the thread");
            break;
        }
    }
    LOGD("Sensor thread is exiting");
    close(readfd);
    close(writefd);
    pthread_mutex_unlock(&TestAppLock);
    pthread_exit(0);
    return NULL;
}

QCameraHAL3RawSnapshotTest::~QCameraHAL3RawSnapshotTest()
{

}

}
