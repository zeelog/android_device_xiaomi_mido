/*--------------------------------------------------------------------------
Copyright (c) 2018, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/

#define LOG_TAG "SidebandStreamHandle"

#include <string.h>
#include <utils/Log.h>

#include "SidebandStreamHandle.h"

namespace android {

SidebandStreamHandle::SidebandStreamHandle(){
    mLibHandle = NULL;
    mHandleProducer = NULL;
    mHandleConsumer = NULL;
}

bool SidebandStreamHandle::init(){
    bool status = true;
    if (mLibHandle || mHandleProducer || mHandleConsumer) {
        ALOGE("SidebandStreamHandle::init called twice\n");
        status = false;
    }

    if(status){
        mLibHandle = dlopen(SIDEBAND_LIBRARY, RTLD_NOW);

        if(mLibHandle) {
            mHandleProducer = (CreateSidebandStreamHandleProducer_t *)
                dlsym(mLibHandle, "CreateSidebandStreamHandleProducer");
            mHandleConsumer = (CreateSidebandStreamHandleConsumer_t *)
                dlsym(mLibHandle, "CreateSidebandStreamHandleConsumer");
            if (!mHandleProducer || !mHandleConsumer)
                status = false;
        }else
            status = false;
        }

    if (!status && mLibHandle) {
        dlclose(mLibHandle);
        mLibHandle = NULL;
        mHandleProducer = NULL;
        mHandleConsumer = NULL;
    }
    return status;
}

void SidebandStreamHandle::destroy()
{
    ALOGI("SidebandStreamHandle::destroy\n");
    if (mLibHandle) {
        dlclose(mLibHandle);
    }

    mLibHandle = NULL;
    mHandleProducer = NULL;
    mHandleConsumer = NULL;
}

};  //namespace android
