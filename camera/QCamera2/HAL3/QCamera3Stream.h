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

#ifndef __QCAMERA3_STREAM_H__
#define __QCAMERA3_STREAM_H__

// System dependencies
#include <utils/Mutex.h>

// Camera dependencies
#include "QCamera3Mem.h"
#include "QCamera3StreamMem.h"
#include "QCameraCmdThread.h"
#include "QCameraQueue.h"

extern "C" {
#include "mm_camera_interface.h"
}

namespace qcamera {

class QCamera3Channel;
class QCamera3Stream;

typedef void (*hal3_stream_cb_routine)(mm_camera_super_buf_t *frame,
                                  QCamera3Stream *stream,
                                  void *userdata);

class QCamera3Stream
{
public:
    QCamera3Stream(uint32_t camHandle,
                  uint32_t chId,
                  mm_camera_ops_t *camOps,
                  cam_padding_info_t *paddingInfo,
                  QCamera3Channel *channel);
    virtual ~QCamera3Stream();
    virtual int32_t init(cam_stream_type_t streamType,
                         cam_format_t streamFormat,
                         cam_dimension_t streamDim,
                         cam_rotation_t streamRotation,
                         cam_stream_reproc_config_t* reprocess_config,
                         uint8_t minStreamBufNum,
                         cam_feature_mask_t postprocess_mask,
                         cam_is_type_t is_type,
                         uint32_t batchSize,
                         hal3_stream_cb_routine stream_cb,
                         void *userdata);
    virtual int32_t bufDone(uint32_t index);
    virtual int32_t bufRelease(int32_t index);
    virtual int32_t processDataNotify(mm_camera_super_buf_t *bufs);
    virtual int32_t start();
    virtual int32_t stop();
    virtual int32_t queueBatchBuf();

    static void dataNotifyCB(mm_camera_super_buf_t *recvd_frame, void *userdata);
    static void *dataProcRoutine(void *data);
    uint32_t getMyHandle() const {return mHandle;}
    cam_stream_type_t getMyType() const;
    int32_t getFrameOffset(cam_frame_len_offset_t &offset);
    int32_t getFrameDimension(cam_dimension_t &dim);
    int32_t getFormat(cam_format_t &fmt);
    QCamera3StreamMem *getStreamBufs() {return mStreamBufs;};
    uint32_t getMyServerID();

    int32_t mapBuf(uint8_t buf_type, uint32_t buf_idx,
            int32_t plane_idx, int fd, void *buffer, size_t size);
    int32_t unmapBuf(uint8_t buf_type, uint32_t buf_idx, int32_t plane_idx);
    int32_t setParameter(cam_stream_parm_buffer_t &param);
    cam_stream_info_t* getStreamInfo() const {return mStreamInfo; };

    static void releaseFrameData(void *data, void *user_data);

private:
    uint32_t mCamHandle;
    uint32_t mChannelHandle;
    uint32_t mHandle; // stream handle from mm-camera-interface
    mm_camera_ops_t *mCamOps;
    cam_stream_info_t *mStreamInfo; // ptr to stream info buf
    mm_camera_stream_mem_vtbl_t mMemVtbl;
    mm_camera_map_unmap_ops_tbl_t *mMemOps;
    uint8_t mNumBufs;
    hal3_stream_cb_routine mDataCB;
    void *mUserData;

    QCameraQueue     mDataQ;
    QCameraCmdThread mProcTh; // thread for dataCB

    QCamera3HeapMemory *mStreamInfoBuf;
    QCamera3StreamMem *mStreamBufs;
    mm_camera_buf_def_t *mBufDefs;
    cam_frame_len_offset_t mFrameLenOffset;
    cam_padding_info_t mPaddingInfo;
    QCamera3Channel *mChannel;
    Mutex mLock;    //Lock controlling access to 'mBufDefs'

    uint32_t mBatchSize; // 0: No batch, non-0: Number of imaage bufs in a batch
    uint8_t mNumBatchBufs; //Number of batch buffers which can hold image bufs
    QCamera3HeapMemory *mStreamBatchBufs; //Pointer to batch buffers memory
    mm_camera_buf_def_t *mBatchBufDefs; //Pointer to array of batch bufDefs
    mm_camera_buf_def_t *mCurrentBatchBufDef; //batch buffer in progress during
                                              //aggregation
    uint32_t    mBufsStaged; //Number of image buffers aggregated into
                             //currentBatchBufDef
    QCameraQueue mFreeBatchBufQ; //Buffer queue containing empty batch buffers

    static int32_t get_bufs(
                     cam_frame_len_offset_t *offset,
                     uint8_t *num_bufs,
                     uint8_t **initial_reg_flag,
                     mm_camera_buf_def_t **bufs,
                     mm_camera_map_unmap_ops_tbl_t *ops_tbl,
                     void *user_data);
    static int32_t put_bufs(
                     mm_camera_map_unmap_ops_tbl_t *ops_tbl,
                     void *user_data);
    static int32_t invalidate_buf(uint32_t index, void *user_data);
    static int32_t clean_invalidate_buf(uint32_t index, void *user_data);

    int32_t getBufs(cam_frame_len_offset_t *offset,
                     uint8_t *num_bufs,
                     uint8_t **initial_reg_flag,
                     mm_camera_buf_def_t **bufs,
                     mm_camera_map_unmap_ops_tbl_t *ops_tbl);
    int32_t putBufs(mm_camera_map_unmap_ops_tbl_t *ops_tbl);
    int32_t invalidateBuf(uint32_t index);
    int32_t cleanInvalidateBuf(uint32_t index);
    int32_t getBatchBufs(
            uint8_t *num_bufs, uint8_t **initial_reg_flag,
            mm_camera_buf_def_t **bufs,
            mm_camera_map_unmap_ops_tbl_t *ops_tbl);
    int32_t putBatchBufs(mm_camera_map_unmap_ops_tbl_t *ops_tbl);
    int32_t getBatchBufDef(mm_camera_buf_def_t& batchBufDef,
            int32_t index);
    int32_t aggregateBufToBatch(mm_camera_buf_def_t& bufDef);
    int32_t handleBatchBuffer(mm_camera_super_buf_t *superBuf);

    static const char* mStreamNames[CAM_STREAM_TYPE_MAX];
    void flushFreeBatchBufQ();
};

}; // namespace qcamera

#endif /* __QCAMERA3_STREAM_H__ */
