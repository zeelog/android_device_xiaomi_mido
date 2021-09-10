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

#ifndef ANDROID_SIDEBAND_HANDLEBASE__H
#define ANDROID_SIDEBAND_HANDLEBASE__H

#include <utils/Errors.h>
#include <cutils/native_handle.h>
#include <cutils/log.h>

// ----------------------------------------------------------------------------
namespace android {
// ----------------------------------------------------------------------------
#define BUF_QUEUE_FULL (android::UNKNOWN_ERROR + 9)
#define BUF_QUEUE_EMPTY (android::UNKNOWN_ERROR + 10)
#define BUF_QUEUE_NO_MORE_DATA (android::UNKNOWN_ERROR + 11)
#define SETTING_NO_DATA_CHANGE (android::UNKNOWN_ERROR + 12)
// ----------------------------------------------------------------------------

typedef struct color_data {
    uint32_t flags;
    float hue;
    float saturation;
    float tone_cb;
    float tone_cr;
    float contrast;
    float brightness;
}color_data_t;

struct SidebandHandleBase : public native_handle {
public:
    virtual ~SidebandHandleBase() { };

    /*Producer use, send captured buffer fd to Consumer*/
    virtual status_t queueBuffer(int idx) = 0;

    /*Producer use, get return buffer fd from Consumer */
    virtual status_t dequeueBuffer(int *idx, uint32_t msInterval) = 0;

    /*Producer use, set setting_data to Consumer */
    virtual status_t setColorData(color_data_t &color_data) = 0;

    /*Consumer use, get captured buffer Number */
    virtual status_t acquireBufferNumb(int *captured_unRead_bufNum) = 0;

    /*Consumer use, get captured buffer fd from Producer*/
    virtual status_t acquireBuffer(int *idx, uint32_t msInterval) = 0;

    /*Consumer use, send used buffer fd to Producer*/
    virtual status_t releaseBuffer(int idx) = 0;

    /*Consumer use, get setting_data from Producer*/
    virtual status_t getColorData(color_data_t *color_data) = 0;

public:
    /*Consumer use, get Buffer width*/
    virtual int getBufferWidth() = 0;

    /*Consumer use, get Buffer height*/
    virtual int getBufferHeight() = 0;

    /*Consumer use, get Color Format*/
    virtual int getColorFormat() = 0;

    /*Consumer use, get CompressedUsage*/
    virtual int getCompressedUsage() = 0;

    /*Consumer use, get BufferFd info*/
    virtual int getBufferFd(int idx) = 0;

    /*Consumer use, set BufferFd info*/
    virtual int setBufferFd(int idx, int fd) = 0;

    /*Consumer use, get Buffer Size according color format*/
    virtual int getBufferSize() = 0;

    /*Consumer use, validate native_handle*/
    static int validate(const native_handle* h)
    {
        const SidebandHandleBase* hnd = (const SidebandHandleBase*)h;
        if (!hnd || hnd->version != sizeof(native_handle) ||
                hnd->numInts != sNumInts || hnd->numFds != sNumFds ||
                hnd->data[hnd->numFds] != sMagic)
        {
            ALOGE("invalid sideband handle (at %p)", hnd);
            return -EINVAL;
        }
        return 0;
    }

    /*Consumer use, get sidebandhandle id*/
    int getSidebandHandleId() const
    {
        return data[numFds + 2];
    }

protected:
    static const int sNumFds = 7;
    static const int sNumInts = 9;
    static const int sMagic = 0x53424e48; /*SBNH*/
};

// ----------------------------------------------------------------------------
}; // namespace android
#endif // ANDROID_SIDEBAND_HANDLEBASE__H
