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

#define LOG_TAG "QCamera3StreamMem"

// System dependencies
#include "gralloc_priv.h"

// Camera dependencies
#include "QCamera3StreamMem.h"

using namespace android;

namespace qcamera {

/*===========================================================================
 * FUNCTION   : QCamera3StreamMem
 *
 * DESCRIPTION: default constructor of QCamera3StreamMem
 *
 * PARAMETERS : none
 *
 * RETURN     : None
 *==========================================================================*/
QCamera3StreamMem::QCamera3StreamMem(uint32_t maxHeapBuffer, bool queueHeapBuffers) :
        mHeapMem(maxHeapBuffer),
        mGrallocMem(maxHeapBuffer),
        mMaxHeapBuffers(maxHeapBuffer),
        mQueueHeapBuffers(queueHeapBuffers)
{
}

/*===========================================================================
 * FUNCTION   : QCamera3StreamMem
 *
 * DESCRIPTION: destructor of QCamera3StreamMem
 *
 * PARAMETERS : none
 *
 * RETURN     : None
 *==========================================================================*/
QCamera3StreamMem::~QCamera3StreamMem()
{
    clear();
}

/*===========================================================================
 * FUNCTION   : getCnt
 *
 * DESCRIPTION: query number of buffers allocated/registered
 *
 * PARAMETERS : none
 *
 * RETURN     : number of buffers allocated
 *==========================================================================*/
uint32_t QCamera3StreamMem::getCnt()
{
    Mutex::Autolock lock(mLock);

    return (mHeapMem.getCnt() + mGrallocMem.getCnt());
}

/*===========================================================================
 * FUNCTION   : getRegFlags
 *
 * DESCRIPTION: query initial reg flags
 *
 * PARAMETERS :
 *   @regFlags: initial reg flags of the allocated/registered buffers
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera3StreamMem::getRegFlags(uint8_t * regFlags)
{
    // Assume that all buffers allocated can be queued.
    for (uint32_t i = 0; i < mHeapMem.getCnt(); i ++)
        regFlags[i] = (mQueueHeapBuffers ? 1 : 0);
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : getFd
 *
 * DESCRIPTION: return file descriptor of the indexed buffer
 *
 * PARAMETERS :
 *   @index   : index of the buffer
 *
 * RETURN     : file descriptor
 *==========================================================================*/
int QCamera3StreamMem::getFd(uint32_t index)
{
    Mutex::Autolock lock(mLock);

    if (index < mMaxHeapBuffers)
        return mHeapMem.getFd(index);
    else
        return mGrallocMem.getFd(index);
}

/*===========================================================================
 * FUNCTION   : getSize
 *
 * DESCRIPTION: return buffer size of the indexed buffer
 *
 * PARAMETERS :
 *   @index   : index of the buffer
 *
 * RETURN     : buffer size
 *==========================================================================*/
ssize_t QCamera3StreamMem::getSize(uint32_t index)
{
    Mutex::Autolock lock(mLock);

    if (index < mMaxHeapBuffers)
        return mHeapMem.getSize(index);
    else
        return mGrallocMem.getSize(index);
}

/*===========================================================================
 * FUNCTION   : invalidateCache
 *
 * DESCRIPTION: invalidate the cache of the indexed buffer
 *
 * PARAMETERS :
 *   @index   : index of the buffer
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera3StreamMem::invalidateCache(uint32_t index)
{
    Mutex::Autolock lock(mLock);

    if (index < mMaxHeapBuffers)
        return mHeapMem.invalidateCache(index);
    else
        return mGrallocMem.invalidateCache(index);
}

/*===========================================================================
 * FUNCTION   : cleanInvalidateCache
 *
 * DESCRIPTION: clean and invalidate the cache of the indexed buffer
 *
 * PARAMETERS :
 *   @index   : index of the buffer
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera3StreamMem::cleanInvalidateCache(uint32_t index)
{
    Mutex::Autolock lock(mLock);

    if (index < mMaxHeapBuffers)
        return mHeapMem.cleanInvalidateCache(index);
    else
        return mGrallocMem.cleanInvalidateCache(index);
}

/*===========================================================================
 * FUNCTION   : getBufDef
 *
 * DESCRIPTION: query detailed buffer information
 *
 * PARAMETERS :
 *   @offset  : [input] frame buffer offset
 *   @bufDef  : [output] reference to struct to store buffer definition
 *   @index   : [input] index of the buffer
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3StreamMem::getBufDef(const cam_frame_len_offset_t &offset,
        mm_camera_buf_def_t &bufDef, uint32_t index)
{
    int32_t ret = NO_ERROR;

    if (index < mMaxHeapBuffers)
        ret = mHeapMem.getBufDef(offset, bufDef, index);
    else
        ret = mGrallocMem.getBufDef(offset, bufDef, index);

    bufDef.mem_info = (void *)this;

    return ret;
}

/*===========================================================================
 * FUNCTION   : getPtr
 *
 * DESCRIPTION: return virtual address of the indexed buffer
 *
 * PARAMETERS :
 *   @index   : index of the buffer
 *
 * RETURN     : virtual address
 *==========================================================================*/
void* QCamera3StreamMem::getPtr(uint32_t index)
{
    Mutex::Autolock lock(mLock);

    if (index < mMaxHeapBuffers)
        return mHeapMem.getPtr(index);
    else
        return mGrallocMem.getPtr(index);
}

/*===========================================================================
 * FUNCTION   : valid
 *
 * DESCRIPTION: return whether there is a valid buffer at the current index
 *
 * PARAMETERS :
 *   @index   : index of the buffer
 *
 * RETURN     : true if there is a buffer, false otherwise
 *==========================================================================*/
bool QCamera3StreamMem::valid(uint32_t index)
{
    Mutex::Autolock lock(mLock);

    if (index < mMaxHeapBuffers)
        return (mHeapMem.getSize(index) > 0);
    else
        return (mGrallocMem.getSize(index) > 0);
}

/*===========================================================================
 * FUNCTION   : registerBuffer
 *
 * DESCRIPTION: registers frameworks-allocated gralloc buffer_handle_t
 *
 * PARAMETERS :
 *   @buffers : buffer_handle_t pointer
 *   @type :    cam_stream_type_t
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera3StreamMem::registerBuffer(buffer_handle_t *buffer,
        cam_stream_type_t type)
{
    Mutex::Autolock lock(mLock);
    return mGrallocMem.registerBuffer(buffer, type);
}


/*===========================================================================
 * FUNCTION   : unregisterBuffer
 *
 * DESCRIPTION: unregister buffer
 *
 * PARAMETERS :
 *   @idx     : unregister buffer at index 'idx'
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3StreamMem::unregisterBuffer(size_t idx)
{
    Mutex::Autolock lock(mLock);
    return mGrallocMem.unregisterBuffer(idx);
}

/*===========================================================================
 * FUNCTION   : getMatchBufIndex
 *
 * DESCRIPTION: query buffer index by object ptr
 *
 * PARAMETERS :
 *   @opaque  : opaque ptr
 *
 * RETURN     : buffer index if match found,
 *              -1 if failed
 *==========================================================================*/
int QCamera3StreamMem::getMatchBufIndex(void *object)
{
    Mutex::Autolock lock(mLock);
    return mGrallocMem.getMatchBufIndex(object);
}

/*===========================================================================
 * FUNCTION   : getBufferHandle
 *
 * DESCRIPTION: return framework pointer
 *
 * PARAMETERS :
 *   @index   : index of the buffer
 *
 * RETURN     : buffer ptr if match found
                NULL if failed
 *==========================================================================*/
void *QCamera3StreamMem::getBufferHandle(uint32_t index)
{
    Mutex::Autolock lock(mLock);
    return mGrallocMem.getBufferHandle(index);
}

/*===========================================================================
 * FUNCTION   : unregisterBuffers
 *
 * DESCRIPTION: unregister buffers
 *
 * PARAMETERS : none
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera3StreamMem::unregisterBuffers()
{
    Mutex::Autolock lock(mLock);
    mGrallocMem.unregisterBuffers();
}


/*===========================================================================
 * FUNCTION   : allocate
 *
 * DESCRIPTION: allocate requested number of buffers of certain size
 *
 * PARAMETERS :
 *   @count   : number of buffers to be allocated
 *   @size    : lenght of the buffer to be allocated
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCamera3StreamMem::allocateAll(size_t size)
{
    Mutex::Autolock lock(mLock);
    return mHeapMem.allocate(size);
}

int QCamera3StreamMem::allocateOne(size_t size)
{
    Mutex::Autolock lock(mLock);
    return mHeapMem.allocateOne(size);
}

/*===========================================================================
 * FUNCTION   : deallocate
 *
 * DESCRIPTION: deallocate heap buffers
 *
 * PARAMETERS : none
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera3StreamMem::deallocate()
{
    Mutex::Autolock lock(mLock);
    mHeapMem.deallocate();
}

/*===========================================================================
 * FUNCTION   : markFrameNumber
 *
 * DESCRIPTION: We use this function from the request call path to mark the
 *              buffers with the frame number they are intended for this info
 *              is used later when giving out callback & it is duty of PP to
 *              ensure that data for that particular frameNumber/Request is
 *              written to this buffer.
 * PARAMETERS :
 *   @index   : index of the buffer
 *   @frame#  : Frame number from the framework
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3StreamMem::markFrameNumber(uint32_t index, uint32_t frameNumber)
{
    Mutex::Autolock lock(mLock);
    if (index < mMaxHeapBuffers)
        return mHeapMem.markFrameNumber(index, frameNumber);
    else
        return mGrallocMem.markFrameNumber(index, frameNumber);
}

/*===========================================================================
 * FUNCTION   : getFrameNumber
 *
 * DESCRIPTION: We use this to fetch the frameNumber for the request with which
 *              this buffer was given to HAL
 *
 *
 * PARAMETERS :
 *   @index   : index of the buffer
 *
 * RETURN     : int32_t frameNumber
 *              positive/zero  -- success
 *              negative failure
 *==========================================================================*/
int32_t QCamera3StreamMem::getFrameNumber(uint32_t index)
{
    Mutex::Autolock lock(mLock);
    if (index < mMaxHeapBuffers)
        return mHeapMem.getFrameNumber(index);
    else
        return mGrallocMem.getFrameNumber(index);
}

/*===========================================================================
 * FUNCTION   : getGrallocBufferIndex
 *
 * DESCRIPTION: We use this to fetch the gralloc buffer index based on frameNumber
 *
 * PARAMETERS :
 *   @frameNumber : frame Number
 *
 * RETURN     : int32_t buffer index
 *              positive/zero  -- success
 *              negative failure
 *==========================================================================*/
int32_t QCamera3StreamMem::getGrallocBufferIndex(uint32_t frameNumber)
{
    Mutex::Autolock lock(mLock);
    int32_t index = mGrallocMem.getBufferIndex(frameNumber);
    return index;
}

/*===========================================================================
 * FUNCTION   : getHeapBufferIndex
 *
 * DESCRIPTION: We use this to fetch the heap buffer index based on frameNumber
 *
 * PARAMETERS :
 *   @frameNumber : frame Number
 *
 * RETURN     : int32_t buffer index
 *              positive/zero  -- success
 *              negative failure
 *==========================================================================*/
int32_t QCamera3StreamMem::getHeapBufferIndex(uint32_t frameNumber)
{
    Mutex::Autolock lock(mLock);
    int32_t index = mHeapMem.getBufferIndex(frameNumber);
    return index;
}

}; //namespace qcamera
