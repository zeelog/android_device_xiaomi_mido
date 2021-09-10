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

#ifndef ANDROID_SIDEBAND_STREAMHANDLE__H
#define ANDROID_SIDEBAND_STREAMHANDLE__H


#include <utils/Errors.h>
#include <cutils/native_handle.h>
#include <dlfcn.h>

#include "SidebandHandleBase.h"

#define SIDEBAND_LIBRARY "libsideband.so"

// ----------------------------------------------------------------------------
namespace android {
// ----------------------------------------------------------------------------



// ----------------------------------------------------------------------------

typedef SidebandHandleBase * CreateSidebandStreamHandleProducer_t(int bufferWidth, int bufferHeight, int color_format, int compressed_usage);
typedef SidebandHandleBase * CreateSidebandStreamHandleConsumer_t(const native_handle *handle);

class SidebandStreamHandle {

public:
    SidebandStreamHandle();
    bool init();
    void destroy();

public:
    CreateSidebandStreamHandleProducer_t *mHandleProducer;
    CreateSidebandStreamHandleConsumer_t *mHandleConsumer;

private:
    void *mLibHandle;
};

// ----------------------------------------------------------------------------
}; // namespace android
#endif // ANDROID_SIDEBAND_STREAMHANDLE__H
