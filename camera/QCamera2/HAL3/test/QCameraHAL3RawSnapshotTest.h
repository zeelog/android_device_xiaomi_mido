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

#ifndef __HAL3APPRAWSNAPSHOT_H__
#define __HAL3APPRAWSNAPSHOT_H__

#include "QCameraHAL3SnapshotTest.h"

namespace qcamera
{
class QCameraHAL3RawSnapshotTest : public QCameraHAL3Test
{
private:
    hal3_camtest_meminfo_t mRawCaptureMemInfo;
    native_handle_t *mRawCaptureHandle;
    const camera_metadata_t *mMetaDataPtr[3];
    camera3_stream_t *mPreviewStream;
    camera3_stream_t *mRawSnapshotStream;
    camera3_capture_request mRequest;
    camera3_stream_buffer_t mRawSnapshotStreamBuffs;
    camera3_stream_configuration mRawSnapshotConfig;
public:
    int mRequestedCapture;
    QCameraHAL3RawSnapshotTest(int req_cap);
    void constructDefaultRequest(hal3_camera_test_obj_t *my_test_obj,
            int camid);
    void configureRawSnapshotStream(hal3_camera_test_obj_t *my_test_obj,
            int camid, int, int );
    void rawProcessCaptureRequest(hal3_camera_test_obj_t *my_test_obj,
            int camid);
    void rawProcessCaptureRequestRepeat(hal3_camera_lib_test *my_hal3test_obj,
            int camid);
    void initTest(hal3_camera_lib_test *handle, int testcase, int, int, int);
    bool rawProcessThreadCreate(int testcase_id,
            void *(*hal3_thread_ops)(void *));
    void rawAllocateBuffers(int height, int width);
    void rawTestEnd(hal3_camera_lib_test *my_hal3test_obj, int camid);
    void captureRequestRepeat(hal3_camera_lib_test *my_hal3test_obj, int camid, int testcase);
    virtual ~QCameraHAL3RawSnapshotTest();
};
    void * rawProcessBuffers(void *data);
}
#endif
