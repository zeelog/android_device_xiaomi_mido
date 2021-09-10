/*--------------------------------------------------------------------------
Copyright (c) 2013 - 2017, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of The Linux Foundation nor
      the names of its contributors may be used to endorse or promote
      products derived from this software without specific prior written
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/

#ifndef __VIDC_DEBUG_H__
#define __VIDC_DEBUG_H__

#ifdef _ANDROID_
#include <cstdio>
#include <pthread.h>
#include <sys/mman.h>
#include <media/msm_media_info.h>

enum {
   PRIO_ERROR=0x1,
   PRIO_INFO=0x1,
   PRIO_HIGH=0x2,
   PRIO_LOW=0x4,
   PRIO_TRACE_HIGH = 0x10,
   PRIO_TRACE_LOW = 0x20,
};

extern int debug_level;

#undef DEBUG_PRINT_ERROR
#define DEBUG_PRINT_ERROR(fmt, args...) ({ \
      if (debug_level & PRIO_ERROR) \
          ALOGE(fmt,##args); \
      })
#undef DEBUG_PRINT_INFO
#define DEBUG_PRINT_INFO(fmt, args...) ({ \
      if (debug_level & PRIO_INFO) \
          ALOGI(fmt,##args); \
      })
#undef DEBUG_PRINT_LOW
#define DEBUG_PRINT_LOW(fmt, args...) ({ \
      if (debug_level & PRIO_LOW) \
          ALOGD(fmt,##args); \
      })
#undef DEBUG_PRINT_HIGH
#define DEBUG_PRINT_HIGH(fmt, args...) ({ \
      if (debug_level & PRIO_HIGH) \
          ALOGD(fmt,##args); \
      })
#else
#define DEBUG_PRINT_ERROR printf
#define DEBUG_PRINT_INFO printf
#define DEBUG_PRINT_LOW printf
#define DEBUG_PRINT_HIGH printf
#endif

#define VALIDATE_OMX_PARAM_DATA(ptr, paramType)                                \
    {                                                                          \
        if (ptr == NULL) { return OMX_ErrorBadParameter; }                     \
        paramType *p = reinterpret_cast<paramType *>(ptr);                     \
        if (p->nSize < sizeof(paramType)) {                                    \
            ALOGE("Insufficient object size(%u) v/s expected(%zu) for type %s",\
                    (unsigned int)p->nSize, sizeof(paramType), #paramType);    \
            return OMX_ErrorBadParameter;                                      \
        }                                                                      \
    }                                                                          \

/*
 * Validate OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE type param
 * *assumes* VALIDATE_OMX_PARAM_DATA checks have passed
 * Checks for nParamCount cannot be generalized here. it is imperative that
 *  the calling code handles it.
 */
#define VALIDATE_OMX_VENDOR_EXTENSION_PARAM_DATA(ext)                                             \
    {                                                                                             \
        if (ext->nParamSizeUsed < 1 || ext->nParamSizeUsed > OMX_MAX_ANDROID_VENDOR_PARAMCOUNT) { \
            ALOGE("VendorExtension: sub-params(%u) not in expected range(%u - %u)",               \
                    ext->nParamSizeUsed, 1, OMX_MAX_ANDROID_VENDOR_PARAMCOUNT);                   \
            return OMX_ErrorBadParameter;                                                         \
        }                                                                                         \
        OMX_U32 expectedSize = (OMX_U32)sizeof(OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE) +         \
                ((ext->nParamSizeUsed - 1) * (OMX_U32)sizeof(OMX_CONFIG_ANDROID_VENDOR_PARAMTYPE));\
        if (ext->nSize < expectedSize) {                                                          \
            ALOGE("VendorExtension: Insifficient size(%u) v/s expected(%u)",                      \
                    ext->nSize, expectedSize);                                                    \
            return OMX_ErrorBadParameter;                                                         \
        }                                                                                         \
    }                                                                                             \

class auto_lock {
    public:
        auto_lock(pthread_mutex_t &lock)
            : mLock(lock) {
                pthread_mutex_lock(&mLock);
            }
        ~auto_lock() {
            pthread_mutex_unlock(&mLock);
        }
    private:
        pthread_mutex_t &mLock;
};

class AutoUnmap {
    void *vaddr;
    int size;

    public:
        AutoUnmap(void *vaddr, int size) {
            this->vaddr = vaddr;
            this->size = size;
        }

        ~AutoUnmap() {
            if (vaddr)
                munmap(vaddr, size);
        }
};

class Signal {
    bool signalled;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
public:
    Signal() {
        signalled = false;
        pthread_cond_init(&condition, NULL);
        pthread_mutex_init(&mutex, NULL);
    }

    ~Signal() {
            pthread_cond_destroy(&condition);
            pthread_mutex_destroy(&mutex);
    }

    void signal() {
        pthread_mutex_lock(&mutex);
        signalled = true;
        pthread_cond_signal(&condition);
        pthread_mutex_unlock(&mutex);
    }

    int wait(uint64_t timeout_nsec) {
        struct timespec ts;

        pthread_mutex_lock(&mutex);
        if (signalled) {
            signalled = false;
            pthread_mutex_unlock(&mutex);
            return 0;
        }
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_nsec / 1000000000;
        ts.tv_nsec += timeout_nsec % 1000000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_nsec -= 1000000000;
            ts.tv_sec  += 1;
        }
        int ret = pthread_cond_timedwait(&condition, &mutex, &ts);
        signalled = false;
        pthread_mutex_unlock(&mutex);
        return ret;
    }
};

#ifdef _ANDROID_
#define ATRACE_TAG ATRACE_TAG_VIDEO
#include <utils/Trace.h>

class AutoTracer {
    int mPrio;
public:
    AutoTracer(int prio, const char* msg)
        : mPrio(prio) {
        if (debug_level & prio) {
            ATRACE_BEGIN(msg);
        }
    }
    ~AutoTracer() {
        if (debug_level & mPrio) {
            ATRACE_END();
        }
    }
};

struct __attribute__((packed)) IvfFileHeader {
    uint8_t signature[4];
    uint16_t version;
    uint16_t size;
    uint8_t fourCC[4];
    uint16_t width;
    uint16_t height;
    uint32_t rate;
    uint32_t scale;
    uint32_t frameCount;
    uint32_t unused;

    IvfFileHeader();
    IvfFileHeader(bool isVp9, int width, int height,
                int rate, int scale, int nFrameCount);
};

struct __attribute__((packed)) IvfFrameHeader {
    uint32_t filledLen;
    uint64_t timeStamp;

    IvfFrameHeader();
    IvfFrameHeader(uint32_t size, uint64_t timeStamp);
};

inline int getYuvSize(int colorFormat, int width, int height) {
    int yStride = VENUS_Y_STRIDE(colorFormat, width);
    int uvStride = VENUS_UV_STRIDE(colorFormat, width);
    int yScanlines = VENUS_Y_SCANLINES(colorFormat, height);
    int uvScanlines = VENUS_UV_SCANLINES(colorFormat, height);
    int yMetaStride = VENUS_Y_META_STRIDE(colorFormat, width);
    int yMetaScanlines = VENUS_Y_META_SCANLINES(colorFormat, height);
    int uvMetaStride = VENUS_UV_META_STRIDE(colorFormat, width);
    int uvMetaScanlines = VENUS_UV_META_SCANLINES(colorFormat, height);
    int yPlane = (yStride * yScanlines + 4095) & ~4095;
    int uvPlane = (uvStride * uvScanlines + 4095) & ~4095;
    int yMetaPlane = (yMetaStride * yMetaScanlines + 4095) & ~4095;
    int uvMetaPlane = (uvMetaStride * uvMetaScanlines + 4095) & ~4095;
    return yPlane + uvPlane + yMetaPlane + uvMetaPlane;
}

#define VIDC_TRACE_NAME_LOW(_name) AutoTracer _tracer(PRIO_TRACE_LOW, _name);
#define VIDC_TRACE_NAME_HIGH(_name) AutoTracer _tracer(PRIO_TRACE_HIGH, _name);

#define VIDC_TRACE_INT_LOW(_name, _int) \
    if (debug_level & PRIO_TRACE_LOW) { \
        ATRACE_INT(_name, _int);        \
    }

#define VIDC_TRACE_INT_HIGH(_name, _int) \
    if (debug_level & PRIO_TRACE_HIGH) { \
        ATRACE_INT(_name, _int);        \
    }

#else // _ANDROID_

#define VIDC_TRACE_NAME_LOW(_name)
#define VIDC_TRACE_NAME_HIGH(_name)
#define VIDC_TRACE_INT_LOW(_name, _int)
#define VIDC_TRACE_INT_HIGH(_name, _int)

#endif // !_ANDROID_

#endif
