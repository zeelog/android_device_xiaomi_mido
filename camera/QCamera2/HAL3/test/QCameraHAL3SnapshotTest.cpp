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
#include "QCameraHAL3SnapshotTest.h"
#include "QCameraHAL3MainTestContext.h"


namespace qcamera {

hal3_camera_lib_test *Snapshot_CamObj_handle;
int fcount_captured;
extern pthread_mutex_t TestAppLock;
QCameraHAL3SnapshotTest::QCameraHAL3SnapshotTest(int req_cap) :
    QCameraHAL3Test(0),
    mCaptureHandle(NULL),
    mSnapshotStream(NULL),
    mRequestedCapture(req_cap)
{

}

void QCameraHAL3SnapshotTest::initTest(hal3_camera_lib_test *handle,
        int testcase, int camid, int w, int h)
{
    int i;
    fcount_captured = 0;
    Snapshot_CamObj_handle = handle;
    configureSnapshotStream(&(handle->test_obj), camid, w, h);

    constructDefaultRequest(&(handle->test_obj), 0);
    LOGD("\n Snapshot Default stream setting read");

    LOGD("\n Snapshot stream configured");
    snapshotThreadCreate(MENU_START_CAPTURE, hal3appSnapshotProcessBuffers);
    (mRequest.frame_number) = 0;
    snapshotProcessCaptureRequest(&(handle->test_obj), 0);
    LOGD("\n Snapshot Process Capture Request Sent");
}

void QCameraHAL3SnapshotTest::constructDefaultRequest(
        hal3_camera_test_obj_t *my_test_obj, int camid)
{
    camera3_device_t *device_handle = my_test_obj->device;
    mMetaDataPtr[0]= device_handle->ops->construct_default_request_settings(my_test_obj->device,
            CAMERA3_TEMPLATE_PREVIEW);
    mMetaDataPtr[1] = device_handle->ops->construct_default_request_settings(my_test_obj->device,
            CAMERA3_TEMPLATE_STILL_CAPTURE);
}

void QCameraHAL3SnapshotTest::configureSnapshotStream(hal3_camera_test_obj_t *my_test_obj,
        int camid, int w, int h)
{
    camera3_stream_t *s_stream, *p_stream;
    camera3_device_t *device_handle = my_test_obj->device;
    mPreviewStream = new camera3_stream_t;
    mSnapshotStream = new camera3_stream_t;

    memset(mPreviewStream, 0, sizeof(camera3_stream_t));
    memset(mSnapshotStream, 0, sizeof(camera3_stream_t));
    mPreviewStream = initStream(CAMERA3_STREAM_OUTPUT, camid, PREVIEW_WIDTH, PREVIEW_HEIGHT, 0,
            HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, HAL3_DATASPACE_UNKNOWN);
    mSnapshotStream = initStream(CAMERA3_STREAM_OUTPUT, camid, SNAPSHOT_CAPTURE_WIDTH,
            SNAPSHOT_CAPTURE_HEIGHT, 0, HAL_PIXEL_FORMAT_BLOB, HAL3_DATASPACE_JFIF);

    mSnapshotConfig = configureStream(CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE, 2);
    mSnapshotConfig.streams[0] = mPreviewStream;
    mSnapshotConfig.streams[1] = mSnapshotStream;
    device_handle->ops->configure_streams(my_test_obj->device, &(mSnapshotConfig));
}

void QCameraHAL3SnapshotTest::snapshotProcessCaptureRequest(
                hal3_camera_test_obj_t *my_test_obj, int camid)
{
    int width, height;
    camera3_device_t *device_handle = my_test_obj->device;
    width = mSnapshotStream->width;
    height = mSnapshotStream->height;
    snapshotAllocateBuffers(width, height);
    mRequest.settings = mMetaDataPtr[1];
    mRequest.input_buffer = NULL;
    mRequest.num_output_buffers = 1;
    mSnapshotStreamBuffs.stream = mSnapshotStream;
    mSnapshotStreamBuffs.status = 0;
    mSnapshotStreamBuffs.buffer = (const native_handle_t**)&mCaptureHandle;
    mSnapshotStreamBuffs.release_fence = -1;
    mSnapshotStreamBuffs.acquire_fence = -1;
    mRequest.output_buffers = &(mSnapshotStreamBuffs);
    LOGD("Calling HAL3APP capture request ");
    device_handle->ops->process_capture_request(my_test_obj->device, &(mRequest));
    (mRequest.frame_number)++;
}

void QCameraHAL3SnapshotTest::snapshotProcessCaptureRequestRepeat(
                    hal3_camera_lib_test *my_hal3test_obj, int camid)
{
    hal3_camera_test_obj_t *my_test_obj = &(my_hal3test_obj->test_obj);
    LOGD("\nSnapshot Requested Capture : %d and Received Capture : %d",
            mRequestedCapture, fcount_captured);
    if (mRequestedCapture == fcount_captured) {
        LOGD("\n Snapshot is running successfully Ending test");
        fflush(stdout);
        LOGD("\n Capture Done , Recieved Frame : %d", fcount_captured);
        snapshotTestEnd(my_hal3test_obj, camid);
    }
    else {
        camera3_device_t *device_handle = my_test_obj->device;
        mRequest.settings = mMetaDataPtr[1];
        mRequest.input_buffer = NULL;
        mRequest.num_output_buffers = 1;
        mSnapshotStreamBuffs.stream = mSnapshotStream;
        mSnapshotStreamBuffs.buffer = (const native_handle_t**)&mCaptureHandle;
        mSnapshotStreamBuffs.release_fence = -1;
        mSnapshotStreamBuffs.acquire_fence = -1;
        mRequest.output_buffers = &(mSnapshotStreamBuffs);
        LOGD("Calling HAL3APP repeat capture request repeat %d and %d",
                mRequestedCapture, fcount_captured);
        device_handle->ops->process_capture_request(my_test_obj->device, &(mRequest));
        (mRequest.frame_number)++;
    }
}

void QCameraHAL3SnapshotTest::snapshotTestEnd(
        hal3_camera_lib_test *my_hal3test_obj, int camid)
{
    buffer_thread_msg_t msg;
    extern pthread_mutex_t gCamLock;
    hal3_camera_test_obj_t *my_test_obj = &(my_hal3test_obj->test_obj);
    camera3_device_t *device_handle = my_test_obj->device;
    device_handle->ops->flush(my_test_obj->device);
    LOGD("%s Closing Camera", __func__);
    ioctl(mCaptureMemInfo.ion_fd, ION_IOC_FREE, &mCaptureMemInfo.ion_handle);
    close(mCaptureMemInfo.ion_fd);
    mCaptureMemInfo.ion_fd = -1;
    memset(&msg, 0, sizeof(buffer_thread_msg_t));
    msg.stop_thread = 1;
    write(pfd[1], &msg, sizeof(buffer_thread_msg_t));
}

void QCameraHAL3SnapshotTest::snapshotAllocateBuffers(int width, int height)
{
    mCaptureHandle= allocateBuffers(width, height, &mCaptureMemInfo);
}

bool QCameraHAL3SnapshotTest::snapshotThreadCreate(int testcase_id,
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

void * hal3appSnapshotProcessBuffers(void *data)
{
    buffer_thread_t *thread = (buffer_thread_t*)data;
    int32_t readfd, writefd;
    hal3_camera_lib_test *hal3_test_handle;
    pthread_mutex_lock(&thread->mutex);
    thread->is_thread_started = 1;
    readfd = thread->readfd;
    writefd = thread->writefd;
    QCameraHAL3SnapshotTest *obj;
    obj = (QCameraHAL3SnapshotTest *)thread->data_obj;
    pthread_cond_signal(&thread->cond);
    pthread_mutex_unlock(&thread->mutex);
    struct pollfd pollfds;
    int32_t num_of_fds = 1;
    bool sthread_exit = 0;
    int32_t ready = 0;
    pollfds.fd = readfd;
    pollfds.events = POLLIN | POLLPRI;
    while(!sthread_exit) {
        ready = poll(&pollfds, (nfds_t)num_of_fds, -1);
        if (ready > 0) {
            LOGD("Got some events");
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
                hal3_test_handle = Snapshot_CamObj_handle;
                obj->snapshotProcessCaptureRequestRepeat(hal3_test_handle, 0);
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

void QCameraHAL3SnapshotTest::captureRequestRepeat(
        hal3_camera_lib_test *my_hal3test_obj, int camid, int testcase)
{
}

QCameraHAL3SnapshotTest::~QCameraHAL3SnapshotTest()
{

}

}
