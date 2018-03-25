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

#define LOG_TAG "QCamera3Stream"

// Camera dependencies
#include "QCamera3HWI.h"
#include "QCamera3Stream.h"

extern "C" {
#include "mm_camera_dbg.h"
}

using namespace android;

namespace qcamera {
#define MAX_BATCH_SIZE   32

const char* QCamera3Stream::mStreamNames[] = {
        "CAM_DEFAULT",
        "CAM_PREVIEW",
        "CAM_POSTVIEW",
        "CAM_SNAPSHOT",
        "CAM_VIDEO",
        "CAM_CALLBACK",
        "CAM_IMPL_DEFINED",
        "CAM_METADATA",
        "CAM_RAW",
        "CAM_OFFLINE_PROC",
        "CAM_PARM",
        "CAM_ANALYSIS"
        "CAM_MAX" };

/*===========================================================================
 * FUNCTION   : get_bufs
 *
 * DESCRIPTION: static function entry to allocate stream buffers
 *
 * PARAMETERS :
 *   @offset     : offset info of stream buffers
 *   @num_bufs   : number of buffers allocated
 *   @initial_reg_flag: flag to indicate if buffer needs to be registered
 *                      at kernel initially
 *   @bufs       : output of allocated buffers
 *   @ops_tbl    : ptr to buf mapping/unmapping ops
 *   @user_data  : user data ptr of ops_tbl
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::get_bufs(
                     cam_frame_len_offset_t *offset,
                     uint8_t *num_bufs,
                     uint8_t **initial_reg_flag,
                     mm_camera_buf_def_t **bufs,
                     mm_camera_map_unmap_ops_tbl_t *ops_tbl,
                     void *user_data)
{
    int32_t rc = NO_ERROR;
    QCamera3Stream *stream = reinterpret_cast<QCamera3Stream *>(user_data);
    if (!stream) {
        LOGE("getBufs invalid stream pointer");
        return NO_MEMORY;
    }
    rc = stream->getBufs(offset, num_bufs, initial_reg_flag, bufs, ops_tbl);
    if (NO_ERROR != rc) {
        LOGE("stream->getBufs failed");
        return NO_MEMORY;
    }
    if (stream->mBatchSize) {
        //Allocate batch buffers if mBatchSize is non-zero. All the output
        //arguments correspond to batch containers and not image buffers
        rc = stream->getBatchBufs(num_bufs, initial_reg_flag,
                bufs, ops_tbl);
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : put_bufs
 *
 * DESCRIPTION: static function entry to deallocate stream buffers
 *
 * PARAMETERS :
 *   @ops_tbl    : ptr to buf mapping/unmapping ops
 *   @user_data  : user data ptr of ops_tbl
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::put_bufs(
                     mm_camera_map_unmap_ops_tbl_t *ops_tbl,
                     void *user_data)
{
    int32_t rc = NO_ERROR;
    QCamera3Stream *stream = reinterpret_cast<QCamera3Stream *>(user_data);
    if (!stream) {
        LOGE("putBufs invalid stream pointer");
        return NO_MEMORY;
    }

    if (stream->mBatchSize) {
        rc = stream->putBatchBufs(ops_tbl);
        if (NO_ERROR != rc) {
            LOGE("stream->putBatchBufs failed");
        }
    }
    rc = stream->putBufs(ops_tbl);
    return rc;
}

/*===========================================================================
 * FUNCTION   : invalidate_buf
 *
 * DESCRIPTION: static function entry to invalidate a specific stream buffer
 *
 * PARAMETERS :
 *   @index      : index of the stream buffer to invalidate
 *   @user_data  : user data ptr of ops_tbl
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::invalidate_buf(uint32_t index, void *user_data)
{
    int32_t rc = NO_ERROR;

    QCamera3Stream *stream = reinterpret_cast<QCamera3Stream *>(user_data);
    if (!stream) {
        LOGE("invalid stream pointer");
        return NO_MEMORY;
    }
    if (stream->mBatchSize) {
        int32_t retVal = NO_ERROR;
        for (size_t i = 0;
                i < stream->mBatchBufDefs[index].user_buf.bufs_used; i++) {
            uint32_t buf_idx = stream->mBatchBufDefs[index].user_buf.buf_idx[i];
            retVal = stream->invalidateBuf(buf_idx);
            if (NO_ERROR != retVal) {
                LOGE("invalidateBuf failed for buf_idx: %d err: %d",
                         buf_idx, retVal);
            }
            rc |= retVal;
        }
    } else {
        rc = stream->invalidateBuf(index);
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : clean_invalidate_buf
 *
 * DESCRIPTION: static function entry to clean and invalidate a specific stream buffer
 *
 * PARAMETERS :
 *   @index      : index of the stream buffer to invalidate
 *   @user_data  : user data ptr of ops_tbl
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::clean_invalidate_buf(uint32_t index, void *user_data)
{
    int32_t rc = NO_ERROR;

    QCamera3Stream *stream = reinterpret_cast<QCamera3Stream *>(user_data);
    if (!stream) {
        LOGE("invalid stream pointer");
        return NO_MEMORY;
    }
    if (stream->mBatchSize) {
        int32_t retVal = NO_ERROR;
        for (size_t i = 0;
                i < stream->mBatchBufDefs[index].user_buf.bufs_used; i++) {
            uint32_t buf_idx = stream->mBatchBufDefs[index].user_buf.buf_idx[i];
            retVal = stream->cleanInvalidateBuf(buf_idx);
            if (NO_ERROR != retVal) {
                LOGE("invalidateBuf failed for buf_idx: %d err: %d",
                         buf_idx, retVal);
            }
            rc |= retVal;
        }
    } else {
        rc = stream->cleanInvalidateBuf(index);
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : QCamera3Stream
 *
 * DESCRIPTION: constructor of QCamera3Stream
 *
 * PARAMETERS :
 *   @allocator  : memory allocator obj
 *   @camHandle  : camera handle
 *   @chId       : channel handle
 *   @camOps     : ptr to camera ops table
 *   @paddingInfo: ptr to padding info
 *
 * RETURN     : None
 *==========================================================================*/
QCamera3Stream::QCamera3Stream(uint32_t camHandle,
                             uint32_t chId,
                             mm_camera_ops_t *camOps,
                             cam_padding_info_t *paddingInfo,
                             QCamera3Channel *channel) :
        mCamHandle(camHandle),
        mChannelHandle(chId),
        mHandle(0),
        mCamOps(camOps),
        mStreamInfo(NULL),
        mMemOps(NULL),
        mNumBufs(0),
        mDataCB(NULL),
        mUserData(NULL),
        mDataQ(releaseFrameData, this),
        mStreamInfoBuf(NULL),
        mStreamBufs(NULL),
        mBufDefs(NULL),
        mChannel(channel),
        mBatchSize(0),
        mNumBatchBufs(0),
        mStreamBatchBufs(NULL),
        mBatchBufDefs(NULL),
        mCurrentBatchBufDef(NULL),
        mBufsStaged(0),
        mFreeBatchBufQ(NULL, this)
{
    mMemVtbl.user_data = this;
    mMemVtbl.get_bufs = get_bufs;
    mMemVtbl.put_bufs = put_bufs;
    mMemVtbl.invalidate_buf = invalidate_buf;
    mMemVtbl.clean_invalidate_buf = clean_invalidate_buf;
    mMemVtbl.set_config_ops = NULL;
    memset(&mFrameLenOffset, 0, sizeof(mFrameLenOffset));
    memcpy(&mPaddingInfo, paddingInfo, sizeof(cam_padding_info_t));
}

/*===========================================================================
 * FUNCTION   : ~QCamera3Stream
 *
 * DESCRIPTION: deconstructor of QCamera3Stream
 *
 * PARAMETERS : None
 *
 * RETURN     : None
 *==========================================================================*/
QCamera3Stream::~QCamera3Stream()
{
    if (mStreamInfoBuf != NULL) {
        int rc = mCamOps->unmap_stream_buf(mCamHandle,
                    mChannelHandle, mHandle, CAM_MAPPING_BUF_TYPE_STREAM_INFO, 0, -1);
        if (rc < 0) {
            LOGE("Failed to un-map stream info buffer");
        }
        mStreamInfoBuf->deallocate();
        delete mStreamInfoBuf;
        mStreamInfoBuf = NULL;
    }
    // delete stream
    if (mHandle > 0) {
        mCamOps->delete_stream(mCamHandle, mChannelHandle, mHandle);
        mHandle = 0;
    }
}

/*===========================================================================
 * FUNCTION   : init
 *
 * DESCRIPTION: initialize stream obj
 *
 * PARAMETERS :
 *   @streamType     : stream type
 *   @streamFormat   : stream format
 *   @streamDim      : stream dimension
 *   @reprocess_config: reprocess stream input configuration
 *   @minNumBuffers  : minimal buffer count for particular stream type
 *   @postprocess_mask: PP mask
 *   @is_type  : Image stabilization type, cam_is_type_t
 *   @batchSize  : Number of image buffers in a batch.
 *                 0: No batch. N: container with N image buffers
 *   @stream_cb      : callback handle
 *   @userdata       : user data
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::init(cam_stream_type_t streamType,
                            cam_format_t streamFormat,
                            cam_dimension_t streamDim,
                            cam_rotation_t streamRotation,
                            cam_stream_reproc_config_t* reprocess_config,
                            uint8_t minNumBuffers,
                            cam_feature_mask_t postprocess_mask,
                            cam_is_type_t is_type,
                            uint32_t batchSize,
                            hal3_stream_cb_routine stream_cb,
                            void *userdata)
{
    int32_t rc = OK;
    ssize_t bufSize = BAD_INDEX;
    mm_camera_stream_config_t stream_config;
    LOGD("batch size is %d", batchSize);

    mHandle = mCamOps->add_stream(mCamHandle, mChannelHandle);
    if (!mHandle) {
        LOGE("add_stream failed");
        rc = UNKNOWN_ERROR;
        goto done;
    }

    // allocate and map stream info memory
    mStreamInfoBuf = new QCamera3HeapMemory(1);
    if (mStreamInfoBuf == NULL) {
        LOGE("no memory for stream info buf obj");
        rc = -ENOMEM;
        goto err1;
    }
    rc = mStreamInfoBuf->allocate(sizeof(cam_stream_info_t));
    if (rc < 0) {
        LOGE("no memory for stream info");
        rc = -ENOMEM;
        goto err2;
    }

    mStreamInfo =
        reinterpret_cast<cam_stream_info_t *>(mStreamInfoBuf->getPtr(0));
    memset(mStreamInfo, 0, sizeof(cam_stream_info_t));
    mStreamInfo->stream_type = streamType;
    mStreamInfo->fmt = streamFormat;
    mStreamInfo->dim = streamDim;
    mStreamInfo->num_bufs = minNumBuffers;
    mStreamInfo->pp_config.feature_mask = postprocess_mask;
    mStreamInfo->is_type = is_type;
    mStreamInfo->pp_config.rotation = streamRotation;
    LOGD("stream_type is %d, feature_mask is %Ld",
           mStreamInfo->stream_type, mStreamInfo->pp_config.feature_mask);

    bufSize = mStreamInfoBuf->getSize(0);
    if (BAD_INDEX != bufSize) {
        rc = mCamOps->map_stream_buf(mCamHandle,
                mChannelHandle, mHandle, CAM_MAPPING_BUF_TYPE_STREAM_INFO,
                0, -1, mStreamInfoBuf->getFd(0), (size_t)bufSize,
                mStreamInfoBuf->getPtr(0));
        if (rc < 0) {
            LOGE("Failed to map stream info buffer");
            goto err3;
        }
    } else {
        LOGE("Failed to retrieve buffer size (bad index)");
        goto err3;
    }

    mNumBufs = minNumBuffers;
    if (reprocess_config != NULL) {
        mStreamInfo->reprocess_config = *reprocess_config;
        mStreamInfo->streaming_mode = CAM_STREAMING_MODE_BURST;
        //mStreamInfo->num_of_burst = reprocess_config->offline.num_of_bufs;
        mStreamInfo->num_of_burst = 1;
    } else if (batchSize) {
        if (batchSize > MAX_BATCH_SIZE) {
            LOGE("batchSize:%d is very large", batchSize);
            rc = BAD_VALUE;
            goto err4;
        }
        else {
            mNumBatchBufs = MAX_INFLIGHT_HFR_REQUESTS / batchSize;
            mStreamInfo->streaming_mode = CAM_STREAMING_MODE_BATCH;
            mStreamInfo->user_buf_info.frame_buf_cnt = batchSize;
            mStreamInfo->user_buf_info.size =
                    (uint32_t)(sizeof(msm_camera_user_buf_cont_t));
            mStreamInfo->num_bufs = mNumBatchBufs;
            //Frame interval is irrelavent since time stamp calculation is not
            //required from the mCamOps
            mStreamInfo->user_buf_info.frameInterval = 0;
            LOGD("batch size is %d", batchSize);
        }
    } else {
        mStreamInfo->streaming_mode = CAM_STREAMING_MODE_CONTINUOUS;
    }

    // Configure the stream
    stream_config.stream_info = mStreamInfo;
    stream_config.mem_vtbl = mMemVtbl;
    stream_config.padding_info = mPaddingInfo;
    stream_config.userdata = this;
    stream_config.stream_cb = dataNotifyCB;
    stream_config.stream_cb_sync = NULL;

    rc = mCamOps->config_stream(mCamHandle,
            mChannelHandle, mHandle, &stream_config);
    if (rc < 0) {
        LOGE("Failed to config stream, rc = %d", rc);
        goto err4;
    }

    mDataCB = stream_cb;
    mUserData = userdata;
    mBatchSize = batchSize;
    return 0;

err4:
    mCamOps->unmap_stream_buf(mCamHandle,
            mChannelHandle, mHandle, CAM_MAPPING_BUF_TYPE_STREAM_INFO, 0, -1);
err3:
    mStreamInfoBuf->deallocate();
err2:
    delete mStreamInfoBuf;
    mStreamInfoBuf = NULL;
    mStreamInfo = NULL;
err1:
    mCamOps->delete_stream(mCamHandle, mChannelHandle, mHandle);
    mHandle = 0;
    mNumBufs = 0;
done:
    return rc;
}

/*===========================================================================
 * FUNCTION   : start
 *
 * DESCRIPTION: start stream. Will start main stream thread to handle stream
 *              related ops.
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::start()
{
    int32_t rc = 0;

    mDataQ.init();
    if (mBatchSize)
        mFreeBatchBufQ.init();
    rc = mProcTh.launch(dataProcRoutine, this);
    return rc;
}

/*===========================================================================
 * FUNCTION   : stop
 *
 * DESCRIPTION: stop stream. Will stop main stream thread
 *
 * PARAMETERS : none
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::stop()
{
    int32_t rc = 0;
    rc = mProcTh.exit();
    return rc;
}

/*===========================================================================
 * FUNCTION   : processDataNotify
 *
 * DESCRIPTION: process stream data notify
 *
 * PARAMETERS :
 *   @frame   : stream frame received
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::processDataNotify(mm_camera_super_buf_t *frame)
{
    LOGD("E\n");
    int32_t rc;
    if (mDataQ.enqueue((void *)frame)) {
        rc = mProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);
    } else {
        LOGD("Stream thread is not active, no ops here");
        bufDone(frame->bufs[0]->buf_idx);
        free(frame);
        rc = NO_ERROR;
    }
    LOGD("X\n");
    return rc;
}

/*===========================================================================
 * FUNCTION   : dataNotifyCB
 *
 * DESCRIPTION: callback for data notify. This function is registered with
 *              mm-camera-interface to handle data notify
 *
 * PARAMETERS :
 *   @recvd_frame   : stream frame received
 *   userdata       : user data ptr
 *
 * RETURN     : none
 *==========================================================================*/
void QCamera3Stream::dataNotifyCB(mm_camera_super_buf_t *recvd_frame,
                                 void *userdata)
{
    LOGD("E\n");
    QCamera3Stream* stream = (QCamera3Stream *)userdata;
    if (stream == NULL ||
        recvd_frame == NULL ||
        recvd_frame->bufs[0] == NULL ||
        recvd_frame->bufs[0]->stream_id != stream->getMyHandle()) {
        LOGE("Not a valid stream to handle buf");
        return;
    }

    mm_camera_super_buf_t *frame =
        (mm_camera_super_buf_t *)malloc(sizeof(mm_camera_super_buf_t));
    if (frame == NULL) {
        LOGE("No mem for mm_camera_buf_def_t");
        stream->bufDone(recvd_frame->bufs[0]->buf_idx);
        return;
    }
    *frame = *recvd_frame;
    stream->processDataNotify(frame);
    return;
}

/*===========================================================================
 * FUNCTION   : dataProcRoutine
 *
 * DESCRIPTION: function to process data in the main stream thread
 *
 * PARAMETERS :
 *   @data    : user data ptr
 *
 * RETURN     : none
 *==========================================================================*/
void *QCamera3Stream::dataProcRoutine(void *data)
{
    int running = 1;
    int ret;
    QCamera3Stream *pme = (QCamera3Stream *)data;
    QCameraCmdThread *cmdThread = &pme->mProcTh;

    cmdThread->setName(mStreamNames[pme->mStreamInfo->stream_type]);

    LOGD("E");
    do {
        do {
            ret = cam_sem_wait(&cmdThread->cmd_sem);
            if (ret != 0 && errno != EINVAL) {
                LOGE("cam_sem_wait error (%s)",
                       strerror(errno));
                return NULL;
            }
        } while (ret != 0);

        // we got notified about new cmd avail in cmd queue
        camera_cmd_type_t cmd = cmdThread->getCmd();
        switch (cmd) {
        case CAMERA_CMD_TYPE_DO_NEXT_JOB:
            {
                LOGD("Do next job");
                mm_camera_super_buf_t *frame =
                    (mm_camera_super_buf_t *)pme->mDataQ.dequeue();
                if (NULL != frame) {
                    if (UNLIKELY(frame->bufs[0]->buf_type ==
                            CAM_STREAM_BUF_TYPE_USERPTR)) {
                        pme->handleBatchBuffer(frame);
                    } else if (pme->mDataCB != NULL) {
                        pme->mDataCB(frame, pme, pme->mUserData);
                    } else {
                        // no data cb routine, return buf here
                        pme->bufDone(frame->bufs[0]->buf_idx);
                    }
                }
            }
            break;
        case CAMERA_CMD_TYPE_EXIT:
            LOGH("Exit");
            /* flush data buf queue */
            pme->mDataQ.flush();
            pme->flushFreeBatchBufQ();
            running = 0;
            break;
        default:
            break;
        }
    } while (running);
    LOGD("X");
    return NULL;
}

/*===========================================================================
 * FUNCTION   : bufDone
 *
 * DESCRIPTION: return stream buffer to kernel
 *
 * PARAMETERS :
 *   @index   : index of buffer to be returned
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::bufDone(uint32_t index)
{
    int32_t rc = NO_ERROR;
    Mutex::Autolock lock(mLock);

    if ((index >= mNumBufs) || (mBufDefs == NULL)) {
        LOGE("index; %d, mNumBufs: %d", index, mNumBufs);
        return BAD_INDEX;
    }
    if (mStreamBufs == NULL)
    {
        LOGE("putBufs already called");
        return INVALID_OPERATION;
    }

    if( NULL == mBufDefs[index].mem_info) {
        if (NULL == mMemOps) {
            LOGE("Camera operations not initialized");
            return NO_INIT;
        }

        ssize_t bufSize = mStreamBufs->getSize(index);

        if (BAD_INDEX != bufSize) {
            LOGD("Map streamBufIdx: %d", index);
            rc = mMemOps->map_ops(index, -1, mStreamBufs->getFd(index),
                    (size_t)bufSize, mStreamBufs->getPtr(index),
                    CAM_MAPPING_BUF_TYPE_STREAM_BUF, mMemOps->userdata);
            if (rc < 0) {
                LOGE("Failed to map camera buffer %d", index);
                return rc;
            }

            rc = mStreamBufs->getBufDef(mFrameLenOffset, mBufDefs[index], index);
            if (NO_ERROR != rc) {
                LOGE("Couldn't find camera buffer definition");
                mMemOps->unmap_ops(index, -1, CAM_MAPPING_BUF_TYPE_STREAM_BUF, mMemOps->userdata);
                return rc;
            }
        } else {
            LOGE("Failed to retrieve buffer size (bad index)");
            return INVALID_OPERATION;
        }
    }

    if (UNLIKELY(mBatchSize)) {
        rc = aggregateBufToBatch(mBufDefs[index]);
    } else {
        rc = mCamOps->qbuf(mCamHandle, mChannelHandle, &mBufDefs[index]);
        if (rc < 0) {
            return FAILED_TRANSACTION;
        }
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : bufRelease
 *
 * DESCRIPTION: release all resources associated with this buffer
 *
 * PARAMETERS :
 *   @index   : index of buffer to be released
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::bufRelease(int32_t index)
{
    int32_t rc = NO_ERROR;
    Mutex::Autolock lock(mLock);

    if ((index >= mNumBufs) || (mBufDefs == NULL)) {
        return BAD_INDEX;
    }

    if (NULL != mBufDefs[index].mem_info) {
        if (NULL == mMemOps) {
            LOGE("Camera operations not initialized");
            return NO_INIT;
        }

        rc = mMemOps->unmap_ops(index, -1, CAM_MAPPING_BUF_TYPE_STREAM_BUF,
                mMemOps->userdata);
        if (rc < 0) {
            LOGE("Failed to un-map camera buffer %d", index);
            return rc;
        }

        mBufDefs[index].mem_info = NULL;
    } else {
        LOGE("Buffer at index %d not registered");
        return BAD_INDEX;
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : getBufs
 *
 * DESCRIPTION: allocate stream buffers
 *
 * PARAMETERS :
 *   @offset     : offset info of stream buffers
 *   @num_bufs   : number of buffers allocated
 *   @initial_reg_flag: flag to indicate if buffer needs to be registered
 *                      at kernel initially
 *   @bufs       : output of allocated buffers
 *   @ops_tbl    : ptr to buf mapping/unmapping ops
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::getBufs(cam_frame_len_offset_t *offset,
                     uint8_t *num_bufs,
                     uint8_t **initial_reg_flag,
                     mm_camera_buf_def_t **bufs,
                     mm_camera_map_unmap_ops_tbl_t *ops_tbl)
{
    int rc = NO_ERROR;
    uint8_t *regFlags;
    Mutex::Autolock lock(mLock);

    if (!ops_tbl) {
        LOGE("ops_tbl is NULL");
        return INVALID_OPERATION;
    }

    mFrameLenOffset = *offset;
    mMemOps = ops_tbl;

    if (mStreamBufs != NULL) {
       LOGE("Failed getBufs being called twice in a row without a putBufs call");
       return INVALID_OPERATION;
    }
    mStreamBufs = mChannel->getStreamBufs(mFrameLenOffset.frame_len);
    if (!mStreamBufs) {
        LOGE("Failed to allocate stream buffers");
        return NO_MEMORY;
    }

    for (uint32_t i = 0; i < mNumBufs; i++) {
        if (mStreamBufs->valid(i)) {
            ssize_t bufSize = mStreamBufs->getSize(i);
            if (BAD_INDEX != bufSize) {
                rc = ops_tbl->map_ops(i, -1, mStreamBufs->getFd(i),
                        (size_t)bufSize, mStreamBufs->getPtr(i),
                        CAM_MAPPING_BUF_TYPE_STREAM_BUF,
                        ops_tbl->userdata);
                if (rc < 0) {
                    LOGE("map_stream_buf failed: %d", rc);
                    for (uint32_t j = 0; j < i; j++) {
                        if (mStreamBufs->valid(j)) {
                            ops_tbl->unmap_ops(j, -1,
                                    CAM_MAPPING_BUF_TYPE_STREAM_BUF,
                                    ops_tbl->userdata);
                        }
                    }
                    return INVALID_OPERATION;
                }
            } else {
                LOGE("Failed to retrieve buffer size (bad index)");
                return INVALID_OPERATION;
            }
        }
    }

    //regFlags array is allocated by us, but consumed and freed by mm-camera-interface
    regFlags = (uint8_t *)malloc(sizeof(uint8_t) * mNumBufs);
    if (!regFlags) {
        LOGE("Out of memory");
        for (uint32_t i = 0; i < mNumBufs; i++) {
            if (mStreamBufs->valid(i)) {
                ops_tbl->unmap_ops(i, -1, CAM_MAPPING_BUF_TYPE_STREAM_BUF,
                        ops_tbl->userdata);
            }
        }
        return NO_MEMORY;
    }
    memset(regFlags, 0, sizeof(uint8_t) * mNumBufs);

    mBufDefs = (mm_camera_buf_def_t *)malloc(mNumBufs * sizeof(mm_camera_buf_def_t));
    if (mBufDefs == NULL) {
        LOGE("Failed to allocate mm_camera_buf_def_t %d", rc);
        for (uint32_t i = 0; i < mNumBufs; i++) {
            if (mStreamBufs->valid(i)) {
                ops_tbl->unmap_ops(i, -1, CAM_MAPPING_BUF_TYPE_STREAM_BUF,
                        ops_tbl->userdata);
            }
        }
        free(regFlags);
        regFlags = NULL;
        return INVALID_OPERATION;
    }
    memset(mBufDefs, 0, mNumBufs * sizeof(mm_camera_buf_def_t));
    for (uint32_t i = 0; i < mNumBufs; i++) {
        if (mStreamBufs->valid(i)) {
            mStreamBufs->getBufDef(mFrameLenOffset, mBufDefs[i], i);
        }
    }

    rc = mStreamBufs->getRegFlags(regFlags);
    if (rc < 0) {
        LOGE("getRegFlags failed %d", rc);
        for (uint32_t i = 0; i < mNumBufs; i++) {
            if (mStreamBufs->valid(i)) {
                ops_tbl->unmap_ops(i, -1, CAM_MAPPING_BUF_TYPE_STREAM_BUF,
                        ops_tbl->userdata);
            }
        }
        free(mBufDefs);
        mBufDefs = NULL;
        free(regFlags);
        regFlags = NULL;
        return INVALID_OPERATION;
    }

    *num_bufs = mNumBufs;
    *initial_reg_flag = regFlags;
    *bufs = mBufDefs;
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : putBufs
 *
 * DESCRIPTION: deallocate stream buffers
 *
 * PARAMETERS :
 *   @ops_tbl    : ptr to buf mapping/unmapping ops
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::putBufs(mm_camera_map_unmap_ops_tbl_t *ops_tbl)
{
    int rc = NO_ERROR;
    Mutex::Autolock lock(mLock);

    for (uint32_t i = 0; i < mNumBufs; i++) {
        if (mStreamBufs->valid(i) && NULL != mBufDefs[i].mem_info) {
            rc = ops_tbl->unmap_ops(i, -1, CAM_MAPPING_BUF_TYPE_STREAM_BUF, ops_tbl->userdata);
            if (rc < 0) {
                LOGE("un-map stream buf failed: %d", rc);
            }
        }
    }
    mBufDefs = NULL; // mBufDefs just keep a ptr to the buffer
                     // mm-camera-interface own the buffer, so no need to free
    memset(&mFrameLenOffset, 0, sizeof(mFrameLenOffset));

    if (mStreamBufs == NULL) {
        LOGE("getBuf failed previously, or calling putBufs twice");
    }

    mChannel->putStreamBufs();

    //need to set mStreamBufs to null because putStreamBufs deletes that memory
    mStreamBufs = NULL;

    return rc;
}

/*===========================================================================
 * FUNCTION   : invalidateBuf
 *
 * DESCRIPTION: invalidate a specific stream buffer
 *
 * PARAMETERS :
 *   @index   : index of the buffer to invalidate
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::invalidateBuf(uint32_t index)
{
    if (mStreamBufs == NULL) {
       LOGE("putBufs already called");
       return INVALID_OPERATION;
    } else
       return mStreamBufs->invalidateCache(index);
}

/*===========================================================================
 * FUNCTION   : cleanInvalidateBuf
 *
 * DESCRIPTION: clean and invalidate a specific stream buffer
 *
 * PARAMETERS :
 *   @index   : index of the buffer to invalidate
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::cleanInvalidateBuf(uint32_t index)
{
    if (mStreamBufs == NULL) {
        LOGE("putBufs already called");
        return INVALID_OPERATION;
    } else
        return mStreamBufs->cleanInvalidateCache(index);
}

/*===========================================================================
 * FUNCTION   : getFrameOffset
 *
 * DESCRIPTION: query stream buffer frame offset info
 *
 * PARAMETERS :
 *   @offset  : reference to struct to store the queried frame offset info
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::getFrameOffset(cam_frame_len_offset_t &offset)
{
    offset = mFrameLenOffset;
    return 0;
}

/*===========================================================================
 * FUNCTION   : getFrameDimension
 *
 * DESCRIPTION: query stream frame dimension info
 *
 * PARAMETERS :
 *   @dim     : reference to struct to store the queried frame dimension
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::getFrameDimension(cam_dimension_t &dim)
{
    if (mStreamInfo != NULL) {
        dim = mStreamInfo->dim;
        return 0;
    }
    return -1;
}

/*===========================================================================
 * FUNCTION   : getFormat
 *
 * DESCRIPTION: query stream format
 *
 * PARAMETERS :
 *   @fmt     : reference to stream format
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::getFormat(cam_format_t &fmt)
{
    if (mStreamInfo != NULL) {
        fmt = mStreamInfo->fmt;
        return 0;
    }
    return -1;
}

/*===========================================================================
 * FUNCTION   : getMyServerID
 *
 * DESCRIPTION: query server stream ID
 *
 * PARAMETERS : None
 *
 * RETURN     : stream ID from server
 *==========================================================================*/
uint32_t QCamera3Stream::getMyServerID() {
    if (mStreamInfo != NULL) {
        return mStreamInfo->stream_svr_id;
    } else {
        return 0;
    }
}

/*===========================================================================
 * FUNCTION   : getMyType
 *
 * DESCRIPTION: query stream type
 *
 * PARAMETERS : None
 *
 * RETURN     : type of stream
 *==========================================================================*/
cam_stream_type_t QCamera3Stream::getMyType() const
{
    if (mStreamInfo != NULL) {
        return mStreamInfo->stream_type;
    } else {
        return CAM_STREAM_TYPE_MAX;
    }
}

/*===========================================================================
 * FUNCTION   : mapBuf
 *
 * DESCRIPTION: map stream related buffer to backend server
 *
 * PARAMETERS :
 *   @buf_type : mapping type of buffer
 *   @buf_idx  : index of buffer
 *   @plane_idx: plane index
 *   @fd       : fd of the buffer
 *   @buffer : buffer ptr
 *   @size     : lenght of the buffer
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::mapBuf(uint8_t buf_type, uint32_t buf_idx,
        int32_t plane_idx, int fd, void *buffer, size_t size)
{
    return mCamOps->map_stream_buf(mCamHandle, mChannelHandle,
                                   mHandle, buf_type,
                                   buf_idx, plane_idx,
                                   fd, size, buffer);

}

/*===========================================================================
 * FUNCTION   : unmapBuf
 *
 * DESCRIPTION: unmap stream related buffer to backend server
 *
 * PARAMETERS :
 *   @buf_type : mapping type of buffer
 *   @buf_idx  : index of buffer
 *   @plane_idx: plane index
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::unmapBuf(uint8_t buf_type, uint32_t buf_idx, int32_t plane_idx)
{
    return mCamOps->unmap_stream_buf(mCamHandle, mChannelHandle,
                                     mHandle, buf_type,
                                     buf_idx, plane_idx);
}

/*===========================================================================
 * FUNCTION   : setParameter
 *
 * DESCRIPTION: set stream based parameters
 *
 * PARAMETERS :
 *   @param   : ptr to parameters to be set
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::setParameter(cam_stream_parm_buffer_t &param)
{
    int32_t rc = NO_ERROR;
    mStreamInfo->parm_buf = param;
    rc = mCamOps->set_stream_parms(mCamHandle,
                                   mChannelHandle,
                                   mHandle,
                                   &mStreamInfo->parm_buf);
    if (rc == NO_ERROR) {
        param = mStreamInfo->parm_buf;
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : releaseFrameData
 *
 * DESCRIPTION: callback function to release frame data node
 *
 * PARAMETERS :
 *   @data      : ptr to post process input data
 *   @user_data : user data ptr (QCameraReprocessor)
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera3Stream::releaseFrameData(void *data, void *user_data)
{
    QCamera3Stream *pme = (QCamera3Stream *)user_data;
    mm_camera_super_buf_t *frame = (mm_camera_super_buf_t *)data;
    if (NULL != pme) {
        if (UNLIKELY(pme->mBatchSize)) {
            /* For batch mode, the batch buffer is added to empty list */
            if(!pme->mFreeBatchBufQ.enqueue((void*) frame->bufs[0])) {
                LOGE("batchBuf.buf_idx: %d enqueue failed",
                        frame->bufs[0]->buf_idx);
            }
        } else {
            pme->bufDone(frame->bufs[0]->buf_idx);
        }
    }
}

/*===========================================================================
 * FUNCTION   : getBatchBufs
 *
 * DESCRIPTION: allocate batch containers for the stream
 *
 * PARAMETERS :
 *   @num_bufs   : number of buffers allocated
 *   @initial_reg_flag: flag to indicate if buffer needs to be registered
 *                      at kernel initially
 *   @bufs       : output of allocated buffers
  *  @ops_tbl    : ptr to buf mapping/unmapping ops
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::getBatchBufs(
        uint8_t *num_bufs, uint8_t **initial_reg_flag,
        mm_camera_buf_def_t **bufs,
        mm_camera_map_unmap_ops_tbl_t *ops_tbl)
{
    int rc = NO_ERROR;
    uint8_t *regFlags;

    if (!ops_tbl || !num_bufs || !initial_reg_flag || !bufs) {
        LOGE("input args NULL");
        return INVALID_OPERATION;
    }
    LOGH("Batch container allocation stream type = %d",
             getMyType());

    Mutex::Autolock lock(mLock);

    mMemOps = ops_tbl;

    //Allocate batch containers
    mStreamBatchBufs = new QCamera3HeapMemory(1);
    if (!mStreamBatchBufs) {
        LOGE("unable to create batch container memory");
        return NO_MEMORY;
    }
    // Allocating single buffer file-descriptor for all batch containers,
    // mStreamBatchBufs considers all the container bufs as a single buffer. But
    // QCamera3Stream manages that single buffer as multiple batch buffers
    LOGD("Allocating batch container memory. numBatch: %d size: %d",
             mNumBatchBufs, mStreamInfo->user_buf_info.size);
    rc = mStreamBatchBufs->allocate(
            mNumBatchBufs * mStreamInfo->user_buf_info.size);
    if (rc < 0) {
        LOGE("unable to allocate batch container memory");
        rc = NO_MEMORY;
        goto err1;
    }

    /* map batch buffers. getCnt here returns 1 because of single FD across
     * batch bufs */
    for (uint32_t i = 0; i < mStreamBatchBufs->getCnt(); i++) {
        if (mNumBatchBufs) {
            //For USER_BUF, size = number_of_container bufs instead of the total
            //buf size
            rc = ops_tbl->map_ops(i, -1, mStreamBatchBufs->getFd(i),
                    (size_t)mNumBatchBufs, mStreamBatchBufs->getPtr(i),
                    CAM_MAPPING_BUF_TYPE_STREAM_USER_BUF,
                    ops_tbl->userdata);
            if (rc < 0) {
                LOGE("Failed to map stream container buffer: %d",
                         rc);
                //Unmap all the buffers that were successfully mapped before
                //this buffer mapping failed
                for (size_t j = 0; j < i; j++) {
                    ops_tbl->unmap_ops(j, -1,
                            CAM_MAPPING_BUF_TYPE_STREAM_USER_BUF,
                            ops_tbl->userdata);
                }
                goto err2;
            }
        } else {
            LOGE("Failed to retrieve buffer size (bad index)");
            return INVALID_OPERATION;
        }
    }

    LOGD("batch bufs successfully mmapped = %d",
             mNumBatchBufs);

    /* regFlags array is allocated here, but consumed and freed by
     * mm-camera-interface */
    regFlags = (uint8_t *)malloc(sizeof(uint8_t) * mNumBatchBufs);
    if (!regFlags) {
        LOGE("Out of memory");
        rc = NO_MEMORY;
        goto err3;
    }
    memset(regFlags, 0, sizeof(uint8_t) * mNumBatchBufs);
    /* Do not queue the container buffers as the image buffers are not yet
     * queued. mStreamBatchBufs->getRegFlags is not called as mStreamBatchBufs
     * considers single buffer is allocated */
    for (uint32_t i = 0; i < mNumBatchBufs; i++) {
        regFlags[i] = 0;
    }

    mBatchBufDefs = (mm_camera_buf_def_t *)
            malloc(mNumBatchBufs * sizeof(mm_camera_buf_def_t));
    if (mBatchBufDefs == NULL) {
        LOGE("mBatchBufDefs memory allocation failed");
        rc = INVALID_OPERATION;
        goto err4;
    }
    memset(mBatchBufDefs, 0, mNumBatchBufs * sizeof(mm_camera_buf_def_t));

    //Populate bufDef and queue to free batchBufQ
    for (uint32_t i = 0; i < mNumBatchBufs; i++) {
        getBatchBufDef(mBatchBufDefs[i], i);
        if(mFreeBatchBufQ.enqueue((void*) &mBatchBufDefs[i])) {
            LOGD("mBatchBufDefs[%d]: 0x%p", i, &mBatchBufDefs[i]);
        } else {
            LOGE("enqueue mBatchBufDefs[%d] failed", i);
        }
    }

    *num_bufs = mNumBatchBufs;
    *initial_reg_flag = regFlags;
    *bufs = mBatchBufDefs;
    LOGH("stream type: %d, numBufs(batch): %d",
             mStreamInfo->stream_type, mNumBatchBufs);

    return NO_ERROR;
err4:
    free(regFlags);
err3:
    for (size_t i = 0; i < mStreamBatchBufs->getCnt(); i++) {
        ops_tbl->unmap_ops(i, -1, CAM_MAPPING_BUF_TYPE_STREAM_USER_BUF,
                ops_tbl->userdata);
    }
err2:
    mStreamBatchBufs->deallocate();
err1:
    delete mStreamBatchBufs;
    mStreamBatchBufs = NULL;
    return rc;
}

/*===========================================================================
 * FUNCTION   : putBatchBufs
 *
 * DESCRIPTION: deallocate stream batch buffers
 *
 * PARAMETERS :
 *   @ops_tbl    : ptr to buf mapping/unmapping ops
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::putBatchBufs(mm_camera_map_unmap_ops_tbl_t *ops_tbl)
{
    int rc = NO_ERROR;
    Mutex::Autolock lock(mLock);

    if (mStreamBatchBufs) {
        for (uint32_t i = 0; i < mStreamBatchBufs->getCnt(); i++) {
            rc = ops_tbl->unmap_ops(i, -1, CAM_MAPPING_BUF_TYPE_STREAM_USER_BUF,
                    ops_tbl->userdata);
            if (rc < 0) {
                LOGE("un-map batch buf failed: %d", rc);
            }
        }
        mStreamBatchBufs->deallocate();
        delete mStreamBatchBufs;
        mStreamBatchBufs = NULL;
    }
    // mm-camera-interface frees bufDefs even though bufDefs are allocated by
    // QCamera3Stream. Don't free here
    mBatchBufDefs = NULL;

    return rc;
}

/*===========================================================================
 * FUNCTION   : getBatchBufDef
 *
 * DESCRIPTION: query detailed buffer information of batch buffer
 *
 * PARAMETERS :
 *   @bufDef  : [output] reference to struct to store buffer definition
 *   @@index  : [input] index of the buffer
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::getBatchBufDef(mm_camera_buf_def_t& batchBufDef,
        int32_t index)
{
    int rc = NO_ERROR;
    memset(&batchBufDef, 0, sizeof(mm_camera_buf_def_t));
    if (mStreamBatchBufs) {
        //Single file descriptor for all batch buffers
        batchBufDef.fd          = mStreamBatchBufs->getFd(0);
        batchBufDef.buf_type    = CAM_STREAM_BUF_TYPE_USERPTR;
        batchBufDef.frame_len   = mStreamInfo->user_buf_info.size;
        batchBufDef.mem_info    = mStreamBatchBufs;
        batchBufDef.buffer      = (uint8_t *)mStreamBatchBufs->getPtr(0) +
                                    (index * mStreamInfo->user_buf_info.size);
        batchBufDef.buf_idx     = index;
        batchBufDef.user_buf.num_buffers = mBatchSize;
        batchBufDef.user_buf.bufs_used = 0;
        batchBufDef.user_buf.plane_buf = mBufDefs;
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : aggregateBufToBatch
 *
 * DESCRIPTION: queue batch container to downstream.
 *
 * PARAMETERS :
 *   @bufDef : image buffer to be aggregated into batch
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success always
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::aggregateBufToBatch(mm_camera_buf_def_t& bufDef)
{
    int32_t rc = NO_ERROR;

    if (UNLIKELY(!mBatchSize)) {
        LOGE("Batch mod is not enabled");
        return INVALID_OPERATION;
    }
    if (!mCurrentBatchBufDef) {
        mCurrentBatchBufDef = (mm_camera_buf_def_t *)mFreeBatchBufQ.dequeue();
        if (!mCurrentBatchBufDef) {
            LOGE("No empty batch buffers is available");
            return NO_MEMORY;
        }
        LOGD("batch buffer: %d dequeued from empty buffer list",
                mCurrentBatchBufDef->buf_idx);
    }
    if (mBufsStaged == mCurrentBatchBufDef->user_buf.num_buffers) {
        LOGE("batch buffer is already full");
        return NO_MEMORY;
    }

    mCurrentBatchBufDef->user_buf.buf_idx[mBufsStaged] = bufDef.buf_idx;
    mBufsStaged++;
    LOGD("buffer id: %d aggregated into batch buffer id: %d",
             bufDef.buf_idx, mCurrentBatchBufDef->buf_idx);
    return rc;
}

/*===========================================================================
 * FUNCTION   : queueBatchBuf
 *
 * DESCRIPTION: queue batch container to downstream.
 *
 * PARAMETERS : None
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success always
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::queueBatchBuf()
{
    int32_t rc = NO_ERROR;

    if (!mCurrentBatchBufDef) {
        LOGE("No buffers were queued into batch");
        return INVALID_OPERATION;
    }
    //bufs_used: number of valid buffers in the batch buffers
    mCurrentBatchBufDef->user_buf.bufs_used = mBufsStaged;

    //if mBufsStaged < num_buffers, initialize the buf_idx to -1 for rest of the
    //buffers
    for (size_t i = mBufsStaged; i < mCurrentBatchBufDef->user_buf.num_buffers;
            i++) {
        mCurrentBatchBufDef->user_buf.buf_idx[i] = -1;
    }

    rc = mCamOps->qbuf(mCamHandle, mChannelHandle, mCurrentBatchBufDef);
    if (rc < 0) {
        LOGE("queueing of batch buffer: %d failed with err: %d",
                mCurrentBatchBufDef->buf_idx, rc);
        return FAILED_TRANSACTION;
    }
    LOGD("Batch buf id: %d queued. bufs_used: %d",
            mCurrentBatchBufDef->buf_idx,
            mCurrentBatchBufDef->user_buf.bufs_used);

    mCurrentBatchBufDef = NULL;
    mBufsStaged = 0;

    return rc;
}

/*===========================================================================
 * FUNCTION   : handleBatchBuffer
 *
 * DESCRIPTION: separate individual buffers from the batch and issue callback
 *
 * PARAMETERS :
 *   @superBuf : Received superbuf containing batch buffer
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success always
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3Stream::handleBatchBuffer(mm_camera_super_buf_t *superBuf)
{
    int32_t rc = NO_ERROR;
    mm_camera_super_buf_t *frame;
    mm_camera_buf_def_t batchBuf;

    if (LIKELY(!mBatchSize)) {
        LOGE("Stream: %d not in batch mode, but batch buffer received",
                 getMyType());
        return INVALID_OPERATION;
    }
    if (!mDataCB) {
        LOGE("Data callback not set for batch mode");
        return BAD_VALUE;
    }
    if (!superBuf->bufs[0]) {
        LOGE("superBuf->bufs[0] is NULL!!");
        return BAD_VALUE;
    }

    /* Copy the batch buffer to local and queue the batch buffer to  empty queue
     * to handle the new requests received while callbacks are in progress */
    batchBuf = *superBuf->bufs[0];
    if (!mFreeBatchBufQ.enqueue((void*) superBuf->bufs[0])) {
        LOGE("batchBuf.buf_idx: %d enqueue failed",
                batchBuf.buf_idx);
        free(superBuf);
        return NO_MEMORY;
    }
    LOGD("Received batch buffer: %d bufs_used: %d",
            batchBuf.buf_idx, batchBuf.user_buf.bufs_used);
    //dummy local bufDef to issue multiple callbacks
    mm_camera_buf_def_t buf;
    memset(&buf, 0, sizeof(mm_camera_buf_def_t));

    for (size_t i = 0; i < batchBuf.user_buf.bufs_used; i++) {
        int32_t buf_idx = batchBuf.user_buf.buf_idx[i];
        buf = mBufDefs[buf_idx];

        /* this memory is freed inside dataCB. Should not be freed here */
        frame = (mm_camera_super_buf_t *)malloc(sizeof(mm_camera_super_buf_t));
        if (!frame) {
            LOGE("malloc failed. Buffers will be dropped");
            break;
        } else {
            memcpy(frame, superBuf, sizeof(mm_camera_super_buf_t));
            frame->bufs[0] = &buf;

            mDataCB(frame, this, mUserData);
        }
    }
    LOGD("batch buffer: %d callbacks done",
            batchBuf.buf_idx);

    free(superBuf);
    return rc;
}

/*===========================================================================
 * FUNCTION   : flushFreeBatchBufQ
 *
 * DESCRIPTION: dequeue all the entries of mFreeBatchBufQ and call flush.
 *              QCameraQueue::flush calls 'free(node->data)' which should be
 *              avoided for mFreeBatchBufQ as the entries are not allocated
 *              during each enqueue
 *
 * PARAMETERS : None
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera3Stream::flushFreeBatchBufQ()
{
    while (!mFreeBatchBufQ.isEmpty()) {
        mFreeBatchBufQ.dequeue();
    }
    mFreeBatchBufQ.flush();
}

}; // namespace qcamera
