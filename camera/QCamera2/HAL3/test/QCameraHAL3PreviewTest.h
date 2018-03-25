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
#ifndef __HAL3APPPREVIEWTEST_H__
#define __HAL3APPPREVIEWTEST_H__

#include "QCameraHAL3Base.h"
#include "QCameraHAL3Test.h"

namespace qcamera {

class QCameraHAL3PreviewTest : public QCameraHAL3Test
{
private:
    hal3_camtest_meminfo_t mPreviewMeminfo;
    native_handle_t **mPreviewHandle;
    hal3_camtest_meminfo_t mCaptureMemInfo;
    native_handle_t *mCaptureHandle;
    const camera_metadata_t *mMetaDataPtr[3];
    camera3_stream_t *mPreviewStream;
    camera3_stream_t *mSnapshotStream;
    camera3_capture_request mRequest;
    camera3_stream_buffer_t mPreviewStreamBuffs;
    camera3_stream_buffer_t mSnapshotStreamBuffs;
    camera3_stream_configuration mPreviewConfig;
public:
    int width;
    int height;
    int nobuffer;
    int mPipelineDepthPreview;

    QCameraHAL3PreviewTest(int cameraIndex);
    void configurePreviewStream(hal3_camera_test_obj_t *my_test_obj, int camid,
            int w, int h);

    void snapshotCaptureRequest(hal3_camera_lib_test *handle,
            int testcase, int camid, int w, int h);
    void constructDefaultRequest(hal3_camera_test_obj_t *, int);
    void initTest(hal3_camera_lib_test *handle,
            int testcase, int camid, int w, int h);

    void captureRequestRepeat(hal3_camera_lib_test *, int, int);
    void previewAllocateBuffers(int,
            int, int);
    bool previewProcessThreadCreate(hal3_camera_lib_test *);
    void previewTestEnd(hal3_camera_lib_test *my_hal3test_obj, int camid);
    void snapshotAllocateBuffers(int, int );
    virtual ~QCameraHAL3PreviewTest();
};

    void * hal3appPreviewProcessBuffers(void *);
}
#endif