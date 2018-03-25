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

#ifndef __QCAMERA3_CHANNEL_H__
#define __QCAMERA3_CHANNEL_H__

// System dependencies
#include <utils/List.h>
#include <utils/Mutex.h>
#include <utils/Vector.h>
#include "gralloc_priv.h"

// Camera dependencies
#include "cam_intf.h"
#include "cam_types.h"
#include "hardware/camera3.h"
#include "QCamera3HALHeader.h"
#include "QCamera3Mem.h"
#include "QCamera3PostProc.h"
#include "QCamera3Stream.h"
#include "QCamera3StreamMem.h"

extern "C" {
#include "mm_camera_interface.h"
#include "mm_jpeg_interface.h"
}

using namespace android;

#define MIN_STREAMING_BUFFER_NUM 7+11

#define QCAMERA_DUMP_FRM_PREVIEW          1
#define QCAMERA_DUMP_FRM_VIDEO            (1<<1)
#define QCAMERA_DUMP_FRM_SNAPSHOT         (1<<2)
#define QCAMERA_DUMP_FRM_CALLBACK         (1<<3)
#define QCAMERA_DUMP_FRM_INPUT_REPROCESS  (1<<6)

typedef int64_t nsecs_t;

namespace qcamera {

typedef void (*channel_cb_routine)(mm_camera_super_buf_t *metadata,
                                camera3_stream_buffer_t *buffer,
                                uint32_t frame_number, bool isInputBuffer,
                                void *userdata);
class QCamera3Channel
{
public:
    QCamera3Channel(uint32_t cam_handle,
                   uint32_t channel_handle,
                   mm_camera_ops_t *cam_ops,
                   channel_cb_routine cb_routine,
                   cam_padding_info_t *paddingInfo,
                   cam_feature_mask_t postprocess_mask,
                   void *userData, uint32_t numBuffers);
    virtual ~QCamera3Channel();

    virtual int32_t start();
    virtual int32_t stop();
    virtual int32_t setBatchSize(uint32_t);
    virtual int32_t queueBatchBuf();
    virtual int32_t setPerFrameMapUnmap(bool enable);
    int32_t bufDone(mm_camera_super_buf_t *recvd_frame);
    int32_t setBundleInfo(const cam_bundle_config_t &bundleInfo);

    virtual uint32_t getStreamTypeMask();
    uint32_t getStreamID(uint32_t streamMask);
    void destroy();
    virtual int32_t initialize(cam_is_type_t isType) = 0;
    virtual int32_t request(buffer_handle_t * /*buffer*/,
                uint32_t /*frameNumber*/){ return 0;};
    virtual int32_t request(buffer_handle_t * /*buffer*/,
                uint32_t /*frameNumber*/,
                camera3_stream_buffer_t* /*pInputBuffer*/,
                metadata_buffer_t* /*metadata*/){ return 0;};
    virtual void streamCbRoutine(mm_camera_super_buf_t *super_frame,
                            QCamera3Stream *stream) = 0;

    virtual int32_t registerBuffer(buffer_handle_t *buffer, cam_is_type_t isType) = 0;
    virtual QCamera3StreamMem *getStreamBufs(uint32_t len) = 0;
    virtual void putStreamBufs() = 0;
    virtual int32_t flush();

    QCamera3Stream *getStreamByHandle(uint32_t streamHandle);
    uint32_t getMyHandle() const {return m_handle;};
    uint32_t getNumOfStreams() const {return m_numStreams;};
    uint32_t getNumBuffers() const {return mNumBuffers;};
    QCamera3Stream *getStreamByIndex(uint32_t index);

    static void streamCbRoutine(mm_camera_super_buf_t *super_frame,
                QCamera3Stream *stream, void *userdata);
    void dumpYUV(mm_camera_buf_def_t *frame, cam_dimension_t dim,
            cam_frame_len_offset_t offset, uint8_t name);
    bool isUBWCEnabled();
    cam_format_t getStreamDefaultFormat(cam_stream_type_t type);

    void *mUserData;
    cam_padding_info_t mPaddingInfo;
    QCamera3Stream *mStreams[MAX_STREAM_NUM_IN_BUNDLE];
    uint32_t m_numStreams;
protected:

    int32_t addStream(cam_stream_type_t streamType,
                      cam_format_t streamFormat,
                      cam_dimension_t streamDim,
                      cam_rotation_t streamRotation,
                      uint8_t minStreamBufnum,
                      cam_feature_mask_t postprocessMask,
                      cam_is_type_t isType,
                      uint32_t batchSize = 0);

    int32_t allocateStreamInfoBuf(camera3_stream_t *stream);

    uint32_t m_camHandle;
    mm_camera_ops_t *m_camOps;
    bool m_bIsActive;

    uint32_t m_handle;


    mm_camera_buf_notify_t mDataCB;


    QCamera3HeapMemory *mStreamInfoBuf;
    channel_cb_routine mChannelCB;
    //cam_padding_info_t *mPaddingInfo;
    cam_feature_mask_t mPostProcMask;
    uint32_t mYUVDump;
    cam_is_type_t mIsType;
    uint32_t mNumBuffers;
    /* Enable unmapping of buffer before issuing buffer callback. Default value
     * for this flag is true and is selectively set to false for the usecases
     * such as HFR to avoid any performance hit due to mapping/unmapping */
    bool    mPerFrameMapUnmapEnable;
    uint32_t mFrmNum;
    uint32_t mDumpFrmCnt;
    uint32_t mSkipMode;
    uint32_t mDumpSkipCnt;
};

/* QCamera3ProcessingChannel is used to handle all streams that are directly
 * generated by hardware and given to frameworks without any postprocessing at HAL.
 * It also handles input streams that require reprocessing by hardware and then
 * returned to frameworks. */
class QCamera3ProcessingChannel : public QCamera3Channel
{
public:
   QCamera3ProcessingChannel(uint32_t cam_handle,
           uint32_t channel_handle,
           mm_camera_ops_t *cam_ops,
           channel_cb_routine cb_routine,
           cam_padding_info_t *paddingInfo,
           void *userData,
           camera3_stream_t *stream,
           cam_stream_type_t stream_type,
           cam_feature_mask_t postprocess_mask,
           QCamera3Channel *metadataChannel,
           uint32_t numBuffers = MAX_INFLIGHT_REQUESTS);

    ~QCamera3ProcessingChannel();

    virtual int32_t initialize(cam_is_type_t isType);
    virtual int32_t request(buffer_handle_t *buffer,
            uint32_t frameNumber,
            camera3_stream_buffer_t* pInputBuffer,
            metadata_buffer_t* metadata);
    virtual void streamCbRoutine(mm_camera_super_buf_t *super_frame,
            QCamera3Stream *stream);
    virtual QCamera3StreamMem *getStreamBufs(uint32_t len);
    virtual void putStreamBufs();
    virtual int32_t registerBuffer(buffer_handle_t *buffer, cam_is_type_t isType);

    virtual int32_t stop();

    virtual reprocess_type_t getReprocessType() = 0;

    virtual void reprocessCbRoutine(buffer_handle_t *resultBuffer,
            uint32_t resultFrameNumber);

    int32_t queueReprocMetadata(mm_camera_super_buf_t *metadata);
    int32_t metadataBufDone(mm_camera_super_buf_t *recvd_frame);
    int32_t translateStreamTypeAndFormat(camera3_stream_t *stream,
            cam_stream_type_t &streamType,
            cam_format_t &streamFormat);
    int32_t setReprocConfig(reprocess_config_t &reproc_cfg,
            camera3_stream_buffer_t *pInputBuffer,
            metadata_buffer_t *metadata,
            cam_format_t streamFormat, cam_dimension_t dim);
    int32_t setFwkInputPPData(qcamera_fwk_input_pp_data_t *src_frame,
            camera3_stream_buffer_t *pInputBuffer,
            reprocess_config_t *reproc_cfg,
            metadata_buffer_t *metadata,
            buffer_handle_t *output_buffer,
            uint32_t frameNumber);
    int32_t checkStreamCbErrors(mm_camera_super_buf_t *super_frame,
            QCamera3Stream *stream);
    int32_t getStreamSize(cam_dimension_t &dim);

    QCamera3PostProcessor m_postprocessor; // post processor
    void showDebugFPS(int32_t streamType);

protected:
    uint8_t mDebugFPS;
    int mFrameCount;
    int mLastFrameCount;
    nsecs_t mLastFpsTime;
    bool isWNREnabled() {return m_bWNROn;};
    void startPostProc(const reprocess_config_t &reproc_cfg);
    void issueChannelCb(buffer_handle_t *resultBuffer,
            uint32_t resultFrameNumber);
    int32_t releaseOfflineMemory(uint32_t resultFrameNumber);

    QCamera3StreamMem mMemory; //output buffer allocated by fwk
    camera3_stream_t *mCamera3Stream;
    uint32_t mNumBufs;
    cam_stream_type_t mStreamType;
    cam_format_t mStreamFormat;
    uint8_t mIntent;

    bool mPostProcStarted;
    bool mInputBufferConfig;   // Set when the processing channel is configured
                               // for processing input(framework) buffers

    QCamera3Channel *m_pMetaChannel;
    mm_camera_super_buf_t *mMetaFrame;
    QCamera3StreamMem mOfflineMemory;      //reprocessing input buffer
    QCamera3StreamMem mOfflineMetaMemory; //reprocessing metadata buffer
    List<uint32_t> mFreeOfflineMetaBuffersList;
    Mutex mFreeOfflineMetaBuffersLock;

private:

    bool m_bWNROn;
};

/* QCamera3RegularChannel is used to handle all streams that are directly
 * generated by hardware and given to frameworks without any postprocessing at HAL.
 * Examples are: all IMPLEMENTATION_DEFINED streams, CPU_READ streams. */
class QCamera3RegularChannel : public QCamera3ProcessingChannel
{
public:
    QCamera3RegularChannel(uint32_t cam_handle,
                    uint32_t channel_handle,
                    mm_camera_ops_t *cam_ops,
                    channel_cb_routine cb_routine,
                    cam_padding_info_t *paddingInfo,
                    void *userData,
                    camera3_stream_t *stream,
                    cam_stream_type_t stream_type,
                    cam_feature_mask_t postprocess_mask,
                    QCamera3Channel *metadataChannel,
                    uint32_t numBuffers = MAX_INFLIGHT_REQUESTS);

    virtual ~QCamera3RegularChannel();

    virtual int32_t setBatchSize(uint32_t batchSize);
    virtual uint32_t getStreamTypeMask();
    virtual int32_t queueBatchBuf();
    virtual int32_t initialize(cam_is_type_t isType);
    using QCamera3ProcessingChannel::request;
    virtual int32_t request(buffer_handle_t *buffer, uint32_t frameNumber);
    virtual reprocess_type_t getReprocessType();

private:
    int32_t initialize(struct private_handle_t *priv_handle);

    uint32_t mBatchSize;
    cam_rotation_t mRotation;
};

/* QCamera3MetadataChannel is for metadata stream generated by camera daemon. */
class QCamera3MetadataChannel : public QCamera3Channel
{
public:
    QCamera3MetadataChannel(uint32_t cam_handle,
                    uint32_t channel_handle,
                    mm_camera_ops_t *cam_ops,
                    channel_cb_routine cb_routine,
                    cam_padding_info_t *paddingInfo,
                    cam_feature_mask_t postprocess_mask,
                    void *userData,
                    uint32_t numBuffers = MIN_STREAMING_BUFFER_NUM);
    virtual ~QCamera3MetadataChannel();

    virtual int32_t initialize(cam_is_type_t isType);

    virtual int32_t request(buffer_handle_t *buffer, uint32_t frameNumber);
    virtual void streamCbRoutine(mm_camera_super_buf_t *super_frame,
                            QCamera3Stream *stream);

    virtual QCamera3StreamMem *getStreamBufs(uint32_t le);
    virtual void putStreamBufs();
    virtual int32_t registerBuffer(buffer_handle_t * /*buffer*/, cam_is_type_t /*isType*/)
            { return NO_ERROR; };

private:
    QCamera3StreamMem *mMemory;
};

/* QCamera3RawChannel is for opaqueu/cross-platform raw stream containing
 * vendor specific bayer data or 16-bit unpacked bayer data */
class QCamera3RawChannel : public QCamera3RegularChannel
{
public:
    QCamera3RawChannel(uint32_t cam_handle,
                    uint32_t channel_handle,
                    mm_camera_ops_t *cam_ops,
                    channel_cb_routine cb_routine,
                    cam_padding_info_t *paddingInfo,
                    void *userData,
                    camera3_stream_t *stream,
                    cam_feature_mask_t postprocess_mask,
                    QCamera3Channel *metadataChannel,
                    bool raw_16 = false,
                    uint32_t numBuffers = MAX_INFLIGHT_REQUESTS);

    virtual ~QCamera3RawChannel();

    virtual int32_t initialize(cam_is_type_t isType);

    virtual void streamCbRoutine(mm_camera_super_buf_t *super_frame,
                            QCamera3Stream *stream);

    virtual reprocess_type_t getReprocessType();

private:
    bool mRawDump;
    bool mIsRaw16;

    void dumpRawSnapshot(mm_camera_buf_def_t *frame);
    void convertLegacyToRaw16(mm_camera_buf_def_t *frame);
    void convertMipiToRaw16(mm_camera_buf_def_t *frame);
};

/*
 * QCamera3RawDumpChannel is for internal use only for Raw dump
 */

class QCamera3RawDumpChannel : public QCamera3Channel
{
public:
    QCamera3RawDumpChannel(uint32_t cam_handle,
                    uint32_t channel_handle,
                    mm_camera_ops_t *cam_ops,
                    cam_dimension_t rawDumpSize,
                    cam_padding_info_t *paddingInfo,
                    void *userData,
                    cam_feature_mask_t postprocess_mask, uint32_t numBuffers = 3U);
    virtual ~QCamera3RawDumpChannel();
    virtual int32_t initialize(cam_is_type_t isType);
    virtual void streamCbRoutine(mm_camera_super_buf_t *super_frame,
                            QCamera3Stream *stream);
    virtual QCamera3StreamMem *getStreamBufs(uint32_t le);
    virtual void putStreamBufs();
    virtual int32_t registerBuffer(buffer_handle_t * /*buffer*/, cam_is_type_t /*isType*/)
            { return NO_ERROR; };
    virtual int32_t request(buffer_handle_t *buffer, uint32_t frameNumber);
    void dumpRawSnapshot(mm_camera_buf_def_t *frame);

public:
    cam_dimension_t mDim;

private:
    bool mRawDump;
    QCamera3StreamMem *mMemory;
};

/* QCamera3YUVChannel is used to handle flexible YUV streams that are directly
 * generated by hardware and given to frameworks without any postprocessing at HAL.
 * It is also used to handle input buffers that generate YUV outputs */
class QCamera3YUVChannel : public QCamera3ProcessingChannel
{
public:
    QCamera3YUVChannel(uint32_t cam_handle,
            uint32_t channel_handle,
            mm_camera_ops_t *cam_ops,
            channel_cb_routine cb_routine,
            cam_padding_info_t *paddingInfo,
            void *userData,
            camera3_stream_t *stream,
            cam_stream_type_t stream_type,
            cam_feature_mask_t postprocess_mask,
            QCamera3Channel *metadataChannel);
    ~QCamera3YUVChannel();
    virtual int32_t initialize(cam_is_type_t isType);
    using QCamera3ProcessingChannel::request;
    virtual int32_t request(buffer_handle_t *buffer,
            uint32_t frameNumber,
            camera3_stream_buffer_t* pInputBuffer,
            metadata_buffer_t* metadata, bool &needMetadata);
    virtual reprocess_type_t getReprocessType();
    virtual void streamCbRoutine(mm_camera_super_buf_t *super_frame,
            QCamera3Stream *stream);
    virtual void putStreamBufs();
    virtual void reprocessCbRoutine(buffer_handle_t *resultBuffer,
        uint32_t resultFrameNumber);

private:
    typedef struct {
        uint32_t frameNumber;
        bool offlinePpFlag;
        buffer_handle_t *output;
        mm_camera_super_buf_t *callback_buffer;
    } PpInfo;

    // Whether offline postprocessing is required for this channel
    bool mBypass;
    uint32_t mFrameLen;

    // Current edge, noise, and crop region setting
    cam_edge_application_t mEdgeMode;
    uint32_t mNoiseRedMode;
    cam_crop_region_t mCropRegion;

    // Mutex to protect mOfflinePpFlagMap and mFreeHeapBufferList
    Mutex mOfflinePpLock;
    // Map between free number and whether the request needs to be
    // postprocessed.
    List<PpInfo> mOfflinePpInfoList;
    // Heap buffer index list
    List<uint32_t> mFreeHeapBufferList;

private:
    bool needsFramePostprocessing(metadata_buffer_t* meta);
    int32_t handleOfflinePpCallback(uint32_t resultFrameNumber,
            Vector<mm_camera_super_buf_t *>& pendingCbs);
    mm_camera_super_buf_t* getNextPendingCbBuffer();
};

/* QCamera3PicChannel is for JPEG stream, which contains a YUV stream generated
 * by the hardware, and encoded to a JPEG stream */
class QCamera3PicChannel : public QCamera3ProcessingChannel
{
public:
    QCamera3PicChannel(uint32_t cam_handle,
            uint32_t channel_handle,
            mm_camera_ops_t *cam_ops,
            channel_cb_routine cb_routine,
            cam_padding_info_t *paddingInfo,
            void *userData,
            camera3_stream_t *stream,
            cam_feature_mask_t postprocess_mask,
            bool is4KVideo,
            bool isInputStreamConfigured,
            QCamera3Channel *metadataChannel,
            uint32_t numBuffers = MAX_INFLIGHT_REQUESTS);
    ~QCamera3PicChannel();

    virtual int32_t initialize(cam_is_type_t isType);
    virtual int32_t flush();
    virtual int32_t request(buffer_handle_t *buffer,
            uint32_t frameNumber,
            camera3_stream_buffer_t* pInputBuffer,
            metadata_buffer_t* metadata);
    virtual void streamCbRoutine(mm_camera_super_buf_t *super_frame,
            QCamera3Stream *stream);

    virtual QCamera3StreamMem *getStreamBufs(uint32_t le);
    virtual void putStreamBufs();
    virtual reprocess_type_t getReprocessType();

    QCamera3Exif *getExifData(metadata_buffer_t *metadata,
            jpeg_settings_t *jpeg_settings);
    void overrideYuvSize(uint32_t width, uint32_t height);
    static void jpegEvtHandle(jpeg_job_status_t status,
            uint32_t /*client_hdl*/,
            uint32_t jobId,
            mm_jpeg_output_t *p_output,
            void *userdata);
    static void dataNotifyCB(mm_camera_super_buf_t *recvd_frame,
            void *userdata);

private:
    int32_t queueJpegSetting(uint32_t out_buf_index, metadata_buffer_t *metadata);

public:
    cam_dimension_t m_max_pic_dim;

private:
    uint32_t mNumSnapshotBufs;
    uint32_t mYuvWidth, mYuvHeight;
    int32_t mCurrentBufIndex;
    bool mInputBufferHint;
    QCamera3StreamMem *mYuvMemory;
    // Keep a list of free buffers
    Mutex mFreeBuffersLock;
    List<uint32_t> mFreeBufferList;
    uint32_t mFrameLen;
};

// reprocess channel class
class QCamera3ReprocessChannel : public QCamera3Channel
{
public:
    QCamera3ReprocessChannel(uint32_t cam_handle,
                            uint32_t channel_handle,
                            mm_camera_ops_t *cam_ops,
                            channel_cb_routine cb_routine,
                            cam_padding_info_t *paddingInfo,
                            cam_feature_mask_t postprocess_mask,
                            void *userData, void *ch_hdl);
    QCamera3ReprocessChannel();
    virtual ~QCamera3ReprocessChannel();
    // offline reprocess
    virtual int32_t start();
    virtual int32_t stop();
    int32_t doReprocessOffline(qcamera_fwk_input_pp_data_t *frame,
            bool isPriorityFrame = false);
    int32_t doReprocess(int buf_fd,void *buffer, size_t buf_length, int32_t &ret_val,
                        mm_camera_super_buf_t *meta_buf);
    int32_t overrideMetadata(qcamera_hal3_pp_buffer_t *pp_buffer,
            mm_camera_buf_def_t *meta_buffer,
            jpeg_settings_t *jpeg_settings,
            qcamera_fwk_input_pp_data_t &fwk_frame);
    int32_t overrideFwkMetadata(qcamera_fwk_input_pp_data_t *frame);
    virtual QCamera3StreamMem *getStreamBufs(uint32_t len);
    virtual void putStreamBufs();
    virtual int32_t initialize(cam_is_type_t isType);
    int32_t unmapOfflineBuffers(bool all);
    int32_t bufDone(mm_camera_super_buf_t *recvd_frame);
    virtual void streamCbRoutine(mm_camera_super_buf_t *super_frame,
                            QCamera3Stream *stream);
    static void dataNotifyCB(mm_camera_super_buf_t *recvd_frame,
                                       void* userdata);
    int32_t addReprocStreamsFromSource(cam_pp_feature_config_t &pp_config,
           const reprocess_config_t &src_config,
           cam_is_type_t is_type,
           QCamera3Channel *pMetaChannel);
    QCamera3Stream *getStreamBySrcHandle(uint32_t srcHandle);
    QCamera3Stream *getSrcStreamBySrcHandle(uint32_t srcHandle);
    virtual int32_t registerBuffer(buffer_handle_t * buffer, cam_is_type_t isType);

public:
    void *inputChHandle;

private:
    typedef struct {
        QCamera3Stream *stream;
        cam_mapping_buf_type type;
        uint32_t index;
    } OfflineBuffer;

    int32_t resetToCamPerfNormal(uint32_t frameNumber);
    android::List<OfflineBuffer> mOfflineBuffers;
    android::List<OfflineBuffer> mOfflineMetaBuffers;
    int32_t mOfflineBuffersIndex;
    int32_t mOfflineMetaIndex;
    uint32_t mFrameLen;
    Mutex mFreeBuffersLock; // Lock for free heap buffers
    List<int32_t> mFreeBufferList; // Free heap buffers list
    reprocess_type_t mReprocessType;
    uint32_t mSrcStreamHandles[MAX_STREAM_NUM_IN_BUNDLE];
    QCamera3ProcessingChannel *m_pSrcChannel; // ptr to source channel for reprocess
    QCamera3Channel *m_pMetaChannel;
    QCamera3StreamMem *mMemory;
    QCamera3StreamMem mGrallocMemory;
    Vector<uint32_t> mPriorityFrames;
    Mutex            mPriorityFramesLock;
    bool             mReprocessPerfMode;
};


/* QCamera3SupportChannel is for HAL internal consumption only */
class QCamera3SupportChannel : public QCamera3Channel
{
public:
    QCamera3SupportChannel(uint32_t cam_handle,
                    uint32_t channel_handle,
                    mm_camera_ops_t *cam_ops,
                    cam_padding_info_t *paddingInfo,
                    cam_feature_mask_t postprocess_mask,
                    cam_stream_type_t streamType,
                    cam_dimension_t *dim,
                    cam_format_t streamFormat,
                    uint8_t hw_analysis_supported,
                    cam_color_filter_arrangement_t color_arrangement,
                    void *userData,
                    uint32_t numBuffers = MIN_STREAMING_BUFFER_NUM
                    );
    virtual ~QCamera3SupportChannel();

    virtual int32_t initialize(cam_is_type_t isType);

    virtual int32_t request(buffer_handle_t *buffer, uint32_t frameNumber);
    virtual void streamCbRoutine(mm_camera_super_buf_t *super_frame,
                            QCamera3Stream *stream);

    virtual QCamera3StreamMem *getStreamBufs(uint32_t le);
    virtual void putStreamBufs();
    virtual int32_t registerBuffer(buffer_handle_t * /*buffer*/, cam_is_type_t /*isType*/)
            { return NO_ERROR; };

    static cam_dimension_t kDim;
private:
    QCamera3StreamMem *mMemory;
    cam_dimension_t mDim;
    cam_stream_type_t mStreamType;
    cam_format_t mStreamFormat;
};

}; // namespace qcamera

#endif /* __QCAMERA_CHANNEL_H__ */
