/* Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
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

#ifndef __QCAMERA3HWI_MEM_H__
#define __QCAMERA3HWI_MEM_H__

// System dependencies
#include <linux/msm_ion.h>
#include <utils/Mutex.h>

// Camera dependencies
#include "hardware/camera3.h"

extern "C" {
#include "mm_camera_interface.h"
}

using namespace android;

namespace qcamera {

// Base class for all memory types. Abstract.
class QCamera3Memory {

public:
    int cleanCache(uint32_t index)
    {
        return cacheOps(index, ION_IOC_CLEAN_CACHES);
    }
    int invalidateCache(uint32_t index)
    {
        return cacheOps(index, ION_IOC_INV_CACHES);
    }
    int cleanInvalidateCache(uint32_t index)
    {
        return cacheOps(index, ION_IOC_CLEAN_INV_CACHES);
    }
    int getFd(uint32_t index);
    ssize_t getSize(uint32_t index);
    uint32_t getCnt();

    virtual int cacheOps(uint32_t index, unsigned int cmd) = 0;
    virtual int getMatchBufIndex(void *object) = 0;
    virtual void *getPtr(uint32_t index) = 0;

    virtual int32_t markFrameNumber(uint32_t index, uint32_t frameNumber) = 0;
    virtual int32_t getFrameNumber(uint32_t index) = 0;
    virtual int32_t getBufferIndex(uint32_t frameNumber) = 0;

    QCamera3Memory();
    virtual ~QCamera3Memory();

    int32_t getBufDef(const cam_frame_len_offset_t &offset,
            mm_camera_buf_def_t &bufDef, uint32_t index);

protected:
    struct QCamera3MemInfo {
        int fd;
        int main_ion_fd;
        ion_user_handle_t handle;
        size_t size;
    };

    int cacheOpsInternal(uint32_t index, unsigned int cmd, void *vaddr);
    virtual void *getPtrLocked(uint32_t index) = 0;

    uint32_t mBufferCount;
    struct QCamera3MemInfo mMemInfo[MM_CAMERA_MAX_NUM_FRAMES];
    void *mPtr[MM_CAMERA_MAX_NUM_FRAMES];
    int32_t mCurrentFrameNumbers[MM_CAMERA_MAX_NUM_FRAMES];
    Mutex mLock;
};

// Internal heap memory is used for memories used internally
// They are allocated from /dev/ion. Examples are: capabilities,
// parameters, metadata, and internal YUV data for jpeg encoding.
class QCamera3HeapMemory : public QCamera3Memory {
public:
    QCamera3HeapMemory(uint32_t maxCnt);
    virtual ~QCamera3HeapMemory();

    int allocate(size_t size);
    int allocateOne(size_t size);
    void deallocate();

    virtual int cacheOps(uint32_t index, unsigned int cmd);
    virtual int getMatchBufIndex(void *object);
    virtual void *getPtr(uint32_t index);

    virtual int32_t markFrameNumber(uint32_t index, uint32_t frameNumber);
    virtual int32_t getFrameNumber(uint32_t index);
    virtual int32_t getBufferIndex(uint32_t frameNumber);

protected:
    virtual void *getPtrLocked(uint32_t index);
private:
    int allocOneBuffer(struct QCamera3MemInfo &memInfo,
            unsigned int heap_id, size_t size);
    void deallocOneBuffer(struct QCamera3MemInfo &memInfo);
    uint32_t mMaxCnt;
};

// Gralloc Memory shared with frameworks
class QCamera3GrallocMemory : public QCamera3Memory {
public:
    QCamera3GrallocMemory(uint32_t startIdx);
    virtual ~QCamera3GrallocMemory();

    int registerBuffer(buffer_handle_t *buffer, cam_stream_type_t type);
    int32_t unregisterBuffer(size_t idx);
    void unregisterBuffers();
    virtual int cacheOps(uint32_t index, unsigned int cmd);
    virtual int getMatchBufIndex(void *object);
    virtual void *getPtr(uint32_t index);

    virtual int32_t markFrameNumber(uint32_t index, uint32_t frameNumber);
    virtual int32_t getFrameNumber(uint32_t index);
    virtual int32_t getBufferIndex(uint32_t frameNumber);

    void *getBufferHandle(uint32_t index);
protected:
    virtual void *getPtrLocked(uint32_t index);
private:
    int32_t unregisterBufferLocked(size_t idx);
    int32_t getFreeIndexLocked();
    buffer_handle_t *mBufferHandle[MM_CAMERA_MAX_NUM_FRAMES];
    struct private_handle_t *mPrivateHandle[MM_CAMERA_MAX_NUM_FRAMES];

    uint32_t mStartIdx;
};
};
#endif
