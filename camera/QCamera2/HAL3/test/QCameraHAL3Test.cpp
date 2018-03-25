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


#include "QCameraHAL3Test.h"
#include "QCameraHAL3Base.h"

namespace qcamera {
hal3_camera_lib_test *CamObj_handle;
extern bool thread_exit;
extern int test_case_end;
buffer_thread_t thread;
extern pthread_cond_t mRequestAppCond;
extern pthread_mutex_t TestAppLock, mCaptureRequestLock;

camera3_stream_t *QCameraHAL3Test::initStream(int streamtype,
        int camid, int w, int h, int usage, int format, int dataspace)
{
    requested_stream =  new camera3_stream_t;
    memset(requested_stream, 0, sizeof(camera3_stream_t));

    requested_stream->stream_type = streamtype;
    requested_stream->width = w;
    requested_stream->height = h;
    requested_stream->format = format;
    requested_stream->usage = usage;
    requested_stream->data_space = (android_dataspace_t)dataspace;
    requested_stream->rotation = CAMERA3_STREAM_ROTATION_0;
    return requested_stream;
}

QCameraHAL3Test::QCameraHAL3Test(int id)
{
    mCamId = id;
}

camera3_stream_configuration QCameraHAL3Test::configureStream(
        int opmode, int num_streams)
{
    int i;
    camera3_stream_configuration requested_config;
    requested_config.operation_mode  = opmode;
    requested_config.num_streams = num_streams;
    requested_config.streams = new camera3_stream_t *[num_streams];
    return requested_config;
}


camera3_stream_buffer_t QCameraHAL3Test::hal3appGetStreamBuffs(camera3_stream_t *req_stream)
{
    camera3_stream_buffer_t stream_buffs;
    stream_buffs.stream = req_stream;
    stream_buffs.release_fence = -1;
    stream_buffs.acquire_fence = -1;
    return stream_buffs;
}

camera3_capture_request QCameraHAL3Test::hal3appGetRequestSettings(
        camera3_stream_buffer_t *stream_buffs, int num_buffer)
{
    camera3_capture_request request_settings;
    request_settings.input_buffer = NULL;
    request_settings.num_output_buffers = 1;
    request_settings.output_buffers = stream_buffs;
    return request_settings;
}

native_handle_t *QCameraHAL3Test::allocateBuffers(int width, int height,
        hal3_camtest_meminfo_t *req_meminfo)
{
    struct ion_handle_data handle_data;
    struct ion_allocation_data alloc;
    struct ion_fd_data ion_info_fd;
    int main_ion_fd = -1, rc;
    size_t buf_size;
    void *data = NULL;
    native_handle_t *nh_test;
    main_ion_fd = open("/dev/ion", O_RDONLY);
    if (main_ion_fd <= 0) {
        LOGE("Ion dev open failed %s\n", strerror(errno));
        return NULL;
    }
    memset(&alloc, 0, sizeof(alloc));
    buf_size = (size_t)(width * height *2);
    alloc.len = (size_t)(buf_size);
    alloc.len = (alloc.len + 4095U) & (~4095U);
    alloc.align = 4096;
    alloc.flags = ION_FLAG_CACHED;
    alloc.heap_id_mask = ION_HEAP(ION_SYSTEM_HEAP_ID);
    rc = ioctl(main_ion_fd, ION_IOC_ALLOC, &alloc);
    if (rc < 0) {
        LOGE("ION allocation failed %s with rc = %d \n", strerror(errno), rc);
        return NULL;
    }
    memset(&ion_info_fd, 0, sizeof(ion_info_fd));
    ion_info_fd.handle = alloc.handle;
    rc = ioctl(main_ion_fd, ION_IOC_SHARE, &ion_info_fd);
    if (rc < 0) {
        LOGE("ION map failed %s\n", strerror(errno));
        return NULL;
    }
    req_meminfo->ion_fd = main_ion_fd;
    req_meminfo->ion_handle = ion_info_fd.handle;
    LOGD("%s ION FD %d len %d\n", __func__, ion_info_fd.fd, alloc.len);
    nh_test = native_handle_create(2, 4);
    nh_test->data[0] = ion_info_fd.fd;
    nh_test->data[1] = 0;
    nh_test->data[2] = 0;
    nh_test->data[3] = 0;
    nh_test->data[4] = alloc.len;
    nh_test->data[5] = 0;
    return nh_test;
}

void QCameraHAL3Test::captureRequestRepeat(
        hal3_camera_lib_test *my_hal3test_obj, int camid, int testcase)
{

}

bool QCameraHAL3Test::processThreadCreate(
        void *obj, int testcase)
{
    int32_t ret = 0;
    pthread_attr_t attr;
    if (pipe(pfd) < 0) {
        LOGE("%s: Error in creating the pipe", __func__);
    }
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_mutex_init(&thread.mutex, NULL);
    pthread_cond_init(&thread.cond, NULL);
    pthread_cond_init(&mRequestAppCond, NULL);
    thread.is_thread_started = 0;
    thread.readfd = pfd[0];
    thread.writefd = pfd[1];
    thread.data_obj = obj;
    thread.testcase = testcase;
    pthread_mutex_lock(&thread.mutex);
    ret = pthread_create(&thread.td, &attr, processBuffers, &thread );
    pthread_setname_np(thread.td, "TestApp_Thread");
    if (ret < 0) {
        LOGE("Failed to create status thread");
        return 0;
    }
    pthread_mutex_unlock(&thread.mutex);
    return 1;
}

void * processBuffers(void *data) {
    buffer_thread_t *thread = (buffer_thread_t*)data;
    int32_t readfd, writefd;
    int testcase;
    hal3_camera_lib_test *hal3_test_handle;
    struct timespec ts1;
    thread->is_thread_started = 1;
    readfd = thread->readfd;
    writefd = thread->writefd;
    testcase = thread->testcase;
    QCameraHAL3Test *obj;
    obj = (QCameraHAL3Test *)thread->data_obj;
    struct pollfd pollfds;
    int32_t num_of_fds = 1;
    int32_t ready = 0;
    while(!thread_exit) {
        pthread_mutex_lock(&thread->mutex);
        clock_gettime(CLOCK_REALTIME, &ts1);
        ts1.tv_nsec += 10000000L;
        pthread_cond_timedwait(&mRequestAppCond, &thread->mutex, &ts1);
        pthread_mutex_unlock(&thread->mutex);
        hal3_test_handle = CamObj_handle;
        if (test_case_end == 0) {
            obj->captureRequestRepeat(hal3_test_handle, 0, testcase);
        }
    }
    LOGD("Sensor thread is exiting");
    close(readfd);
    close(writefd);
    pthread_cond_destroy(&mRequestAppCond);
    pthread_mutex_unlock(&TestAppLock);
    pthread_exit(0);
    return NULL;
}

QCameraHAL3Test::~QCameraHAL3Test()
{

}

}
