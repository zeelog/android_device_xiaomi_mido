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
#ifndef __HAL3APPTESTINIT_H__
#define __HAL3APPTESTINIT_H__

#include "QCameraHAL3MainTestContext.h"
#include "QCameraHAL3Base.h"

namespace qcamera {

class QCameraHAL3Test
{
    int mCamId;
public:
    QCameraHAL3Test(int cameraIndex);
    camera3_stream_t *requested_stream;
    camera3_stream_t *initStream(int streamtype,
            int camid, int w, int h, int format,int usage,int dataspace);

    camera3_stream_configuration configureStream(
            int opmode, int num_streams);
    virtual void captureRequestRepeat(hal3_camera_lib_test *, int, int);
    camera_metadata_t* hal3appGetDefaultRequest(int type);

    camera3_capture_request hal3appGetRequestSettings(
            camera3_stream_buffer_t *stream_buffs, int num_buffer);
    camera3_stream_buffer_t hal3appGetStreamBuffs(camera3_stream_t *req_stream);

    native_handle_t *allocateBuffers(int width, int height,
            hal3_camtest_meminfo_t *req_meminfo);
    bool processThreadCreate(void *obj, int testcase);
    virtual ~QCameraHAL3Test();
};

    void * processBuffers(void *data);
}
#endif