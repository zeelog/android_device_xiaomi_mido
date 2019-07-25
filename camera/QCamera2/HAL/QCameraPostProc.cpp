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

#define LOG_TAG "QCameraPostProc"

// System dependencies
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <utils/Errors.h>

// Camera dependencies
#include "QCamera2HWI.h"
#include "QCameraPostProc.h"
#include "QCameraTrace.h"

extern "C" {
#include "mm_camera_dbg.h"
}

namespace qcamera {

const char *QCameraPostProcessor::STORE_LOCATION = "/sdcard/img_%d.jpg";

#define FREE_JPEG_OUTPUT_BUFFER(ptr,cnt)     \
    int jpeg_bufs; \
    for (jpeg_bufs = 0; jpeg_bufs < (int)cnt; jpeg_bufs++)  { \
      if (ptr[jpeg_bufs] != NULL) { \
          free(ptr[jpeg_bufs]); \
          ptr[jpeg_bufs] = NULL; \
      } \
    }

/*===========================================================================
 * FUNCTION   : QCameraPostProcessor
 *
 * DESCRIPTION: constructor of QCameraPostProcessor.
 *
 * PARAMETERS :
 *   @cam_ctrl : ptr to HWI object
 *
 * RETURN     : None
 *==========================================================================*/
QCameraPostProcessor::QCameraPostProcessor(QCamera2HardwareInterface *cam_ctrl)
    : m_parent(cam_ctrl),
      mJpegCB(NULL),
      mJpegUserData(NULL),
      mJpegClientHandle(0),
      mJpegSessionId(0),
      m_pJpegExifObj(NULL),
      m_bThumbnailNeeded(TRUE),
      mPPChannelCount(0),
      m_bInited(FALSE),
      m_inputPPQ(releaseOngoingPPData, this),
      m_ongoingPPQ(releaseOngoingPPData, this),
      m_inputJpegQ(releaseJpegData, this),
      m_ongoingJpegQ(releaseJpegData, this),
      m_inputRawQ(releaseRawData, this),
      mSaveFrmCnt(0),
      mUseSaveProc(false),
      mUseJpegBurst(false),
      mJpegMemOpt(true),
      m_JpegOutputMemCount(0),
      mNewJpegSessionNeeded(true),
      m_bufCountPPQ(0),
      m_PPindex(0)
{
    memset(&mJpegHandle, 0, sizeof(mJpegHandle));
    memset(&mJpegMpoHandle, 0, sizeof(mJpegMpoHandle));
    memset(&m_pJpegOutputMem, 0, sizeof(m_pJpegOutputMem));
    memset(mPPChannels, 0, sizeof(mPPChannels));
    m_DataMem = NULL;
    mOfflineDataBufs = NULL;
    pthread_mutex_init(&m_reprocess_lock,NULL);
}

/*===========================================================================
 * FUNCTION   : ~QCameraPostProcessor
 *
 * DESCRIPTION: deconstructor of QCameraPostProcessor.
 *
 * PARAMETERS : None
 *
 * RETURN     : None
 *==========================================================================*/
QCameraPostProcessor::~QCameraPostProcessor()
{
    FREE_JPEG_OUTPUT_BUFFER(m_pJpegOutputMem,m_JpegOutputMemCount);
    if (m_pJpegExifObj != NULL) {
        delete m_pJpegExifObj;
        m_pJpegExifObj = NULL;
    }
    for (int8_t i = 0; i < mPPChannelCount; i++) {
        QCameraChannel *pChannel = mPPChannels[i];
        if ( pChannel != NULL ) {
            pChannel->stop();
            delete pChannel;
            pChannel = NULL;
        }
    }
    mPPChannelCount = 0;
    pthread_mutex_destroy(&m_reprocess_lock);
}

/*===========================================================================
 * FUNCTION   : setJpegHandle
 *
 * DESCRIPTION: set JPEG client handles
 *
 * PARAMETERS :
 *   @pJpegHandle    : JPEG ops handle
 *   @pJpegMpoHandle    : MPO JPEG ops handle
 *   @clientHandle    : JPEG client handle
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraPostProcessor::setJpegHandle(mm_jpeg_ops_t *pJpegHandle,
    mm_jpeg_mpo_ops_t *pJpegMpoHandle, uint32_t clientHandle)
{
    LOGH("E mJpegClientHandle: %d, clientHandle: %d",
             mJpegClientHandle, clientHandle);

    if(pJpegHandle) {
        memcpy(&mJpegHandle, pJpegHandle, sizeof(mm_jpeg_ops_t));
    }

    if(pJpegMpoHandle) {
        memcpy(&mJpegMpoHandle, pJpegMpoHandle, sizeof(mm_jpeg_mpo_ops_t));
    }
    mJpegClientHandle = clientHandle;
    LOGH("X mJpegClientHandle: %d, clientHandle: %d",
             mJpegClientHandle, clientHandle);
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : init
 *
 * DESCRIPTION: initialization of postprocessor
 *
 * PARAMETERS :
 *   @jpeg_cb      : callback to handle jpeg event from mm-camera-interface
 *   @user_data    : user data ptr for jpeg callback
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraPostProcessor::init(jpeg_encode_callback_t jpeg_cb, void *user_data)
{
    mJpegCB = jpeg_cb;
    mJpegUserData = user_data;
    m_dataProcTh.launch(dataProcessRoutine, this);
    m_saveProcTh.launch(dataSaveRoutine, this);
    m_parent->mParameters.setReprocCount();
    m_bInited = TRUE;
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : deinit
 *
 * DESCRIPTION: de-initialization of postprocessor
 *
 * PARAMETERS : None
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraPostProcessor::deinit()
{
    if (m_bInited == TRUE) {
        m_dataProcTh.exit();
        m_saveProcTh.exit();
        m_bInited = FALSE;
    }
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : start
 *
 * DESCRIPTION: start postprocessor. Data process thread and data notify thread
 *              will be launched.
 *
 * PARAMETERS :
 *   @pSrcChannel : source channel obj ptr that possibly needs reprocess
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *
 * NOTE       : if any reprocess is needed, a reprocess channel/stream
 *              will be started.
 *==========================================================================*/
int32_t QCameraPostProcessor::start(QCameraChannel *pSrcChannel)
{
    char prop[PROPERTY_VALUE_MAX];
    int32_t rc = NO_ERROR;
    QCameraChannel *pInputChannel = pSrcChannel;

    LOGH("E ");
    if (m_bInited == FALSE) {
        LOGE("postproc not initialized yet");
        return UNKNOWN_ERROR;
    }

    if (m_DataMem != NULL) {
        m_DataMem->release(m_DataMem);
        m_DataMem = NULL;
    }

    if (pInputChannel == NULL) {
        LOGE("Input Channel for pproc is NULL.");
        return UNKNOWN_ERROR;
    }

    if ( m_parent->needReprocess() ) {
        for (int8_t i = 0; i < mPPChannelCount; i++) {
            // Delete previous reproc channel
            QCameraReprocessChannel *pChannel = mPPChannels[i];
            if (pChannel != NULL) {
                pChannel->stop();
                delete pChannel;
                pChannel = NULL;
            }
        }
        mPPChannelCount = 0;

        m_bufCountPPQ = 0;
        if (!m_parent->isLongshotEnabled()) {
            m_parent->mParameters.setReprocCount();
        }

        if (m_parent->mParameters.getManualCaptureMode() >=
                CAM_MANUAL_CAPTURE_TYPE_3) {
            mPPChannelCount = m_parent->mParameters.getReprocCount() - 1;
        } else {
            mPPChannelCount = m_parent->mParameters.getReprocCount();
        }

        // Create all reproc channels and start channel
        for (int8_t i = 0; i < mPPChannelCount; i++) {
            mPPChannels[i] = m_parent->addReprocChannel(pInputChannel, i);
            if (mPPChannels[i] == NULL) {
                LOGE("cannot add multi reprocess channel i = %d", i);
                return UNKNOWN_ERROR;
            }
            rc = mPPChannels[i]->start();
            if (rc != 0) {
                LOGE("cannot start multi reprocess channel i = %d", i);
                delete mPPChannels[i];
                mPPChannels[i] = NULL;
                return UNKNOWN_ERROR;
            }
            pInputChannel = static_cast<QCameraChannel *>(mPPChannels[i]);
        }
    }

    property_get("persist.camera.longshot.save", prop, "0");
    mUseSaveProc = atoi(prop) > 0 ? true : false;

    m_PPindex = 0;
    m_InputMetadata.clear();
    m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_START_DATA_PROC, TRUE, FALSE);
    m_parent->m_cbNotifier.startSnapshots();
    LOGH("X rc = %d", rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : stop
 *
 * DESCRIPTION: stop postprocessor. Data process and notify thread will be stopped.
 *
 * PARAMETERS : None
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *
 * NOTE       : reprocess channel will be stopped and deleted if there is any
 *==========================================================================*/
int32_t QCameraPostProcessor::stop()
{
    if (m_bInited == TRUE) {
        m_parent->m_cbNotifier.stopSnapshots();

        if (m_DataMem != NULL) {
            m_DataMem->release(m_DataMem);
            m_DataMem = NULL;
        }

        // dataProc Thread need to process "stop" as sync call because abort jpeg job should be a sync call
        m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_STOP_DATA_PROC, TRUE, TRUE);
    }
    // stop reproc channel if exists
    for (int8_t i = 0; i < mPPChannelCount; i++) {
        QCameraReprocessChannel *pChannel = mPPChannels[i];
        if (pChannel != NULL) {
            pChannel->stop();
            delete pChannel;
            pChannel = NULL;
        }
    }
    mPPChannelCount = 0;
    m_PPindex = 0;
    m_InputMetadata.clear();

    if (mOfflineDataBufs != NULL) {
        mOfflineDataBufs->deallocate();
        delete mOfflineDataBufs;
        mOfflineDataBufs = NULL;
    }
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : createJpegSession
 *
 * DESCRIPTION: start JPEG session in parallel to reproces to reduce the KPI
 *
 * PARAMETERS :
 *   @pSrcChannel : source channel obj ptr that possibly needs reprocess
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraPostProcessor::createJpegSession(QCameraChannel *pSrcChannel)
{
    int32_t rc = NO_ERROR;

    LOGH("E ");
    if (m_bInited == FALSE) {
        LOGE("postproc not initialized yet");
        return UNKNOWN_ERROR;
    }

    if (pSrcChannel == NULL) {
        LOGE("Input Channel for pproc is NULL.");
        return UNKNOWN_ERROR;
    }

    if (mPPChannelCount > 0) {
        QCameraChannel *pChannel = NULL;
        int ppChannel_idx = mPPChannelCount - 1;
        pChannel = m_parent->needReprocess() ? mPPChannels[ppChannel_idx] :
                pSrcChannel;
        QCameraStream *pSnapshotStream = NULL;
        QCameraStream *pThumbStream = NULL;
        bool thumb_stream_needed = ((!m_parent->isZSLMode() ||
            (m_parent->mParameters.getFlipMode(CAM_STREAM_TYPE_SNAPSHOT) ==
             m_parent->mParameters.getFlipMode(CAM_STREAM_TYPE_PREVIEW))) &&
            !m_parent->mParameters.generateThumbFromMain());

        if (pChannel == NULL) {
            LOGE("Input Channel for pproc is NULL for index %d.",
                     ppChannel_idx);
            return UNKNOWN_ERROR;
        }

        for (uint32_t i = 0; i < pChannel->getNumOfStreams(); ++i) {
            QCameraStream *pStream = pChannel->getStreamByIndex(i);

            if ( NULL == pStream ) {
                break;
            }

            if (pStream->isTypeOf(CAM_STREAM_TYPE_SNAPSHOT) ||
                    pStream->isOrignalTypeOf(CAM_STREAM_TYPE_SNAPSHOT)) {
                pSnapshotStream = pStream;
            }

            if ((thumb_stream_needed) &&
                   (pStream->isTypeOf(CAM_STREAM_TYPE_PREVIEW) ||
                    pStream->isTypeOf(CAM_STREAM_TYPE_POSTVIEW) ||
                    pStream->isOrignalTypeOf(CAM_STREAM_TYPE_PREVIEW) ||
                    pStream->isOrignalTypeOf(CAM_STREAM_TYPE_POSTVIEW))) {
                pThumbStream = pStream;
            }
        }

        // If thumbnail is not part of the reprocess channel, then
        // try to get it from the source channel
        if ((thumb_stream_needed) && (NULL == pThumbStream) &&
                (pChannel == mPPChannels[ppChannel_idx])) {
            for (uint32_t i = 0; i < pSrcChannel->getNumOfStreams(); ++i) {
                QCameraStream *pStream = pSrcChannel->getStreamByIndex(i);

                if ( NULL == pStream ) {
                    break;
                }

                if (pStream->isTypeOf(CAM_STREAM_TYPE_POSTVIEW) ||
                        pStream->isOrignalTypeOf(CAM_STREAM_TYPE_POSTVIEW) ||
                        pStream->isTypeOf(CAM_STREAM_TYPE_PREVIEW) ||
                        pStream->isOrignalTypeOf(CAM_STREAM_TYPE_PREVIEW)) {
                    pThumbStream = pStream;
                }
            }
        }

        if ( NULL != pSnapshotStream ) {
            mm_jpeg_encode_params_t encodeParam;
            memset(&encodeParam, 0, sizeof(mm_jpeg_encode_params_t));
            rc = getJpegEncodingConfig(encodeParam, pSnapshotStream, pThumbStream);
            if (rc != NO_ERROR) {
                LOGE("error getting encoding config");
                return rc;
            }
            LOGH("[KPI Perf] : call jpeg create_session");

            rc = mJpegHandle.create_session(mJpegClientHandle,
                    &encodeParam,
                    &mJpegSessionId);
            if (rc != NO_ERROR) {
                LOGE("error creating a new jpeg encoding session");
                return rc;
            }
            mNewJpegSessionNeeded = false;
        }
    }
    LOGH("X ");
    return rc;
}

/*===========================================================================
 * FUNCTION   : getJpegEncodingConfig
 *
 * DESCRIPTION: function to prepare encoding job information
 *
 * PARAMETERS :
 *   @encode_parm   : param to be filled with encoding configuration
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraPostProcessor::getJpegEncodingConfig(mm_jpeg_encode_params_t& encode_parm,
                                                    QCameraStream *main_stream,
                                                    QCameraStream *thumb_stream)
{
    LOGD("E");
    int32_t ret = NO_ERROR;
    size_t out_size;

    char prop[PROPERTY_VALUE_MAX];
    property_get("persist.camera.jpeg_burst", prop, "0");
    mUseJpegBurst = (atoi(prop) > 0) && !mUseSaveProc;
    encode_parm.burst_mode = mUseJpegBurst;

    cam_rect_t crop;
    memset(&crop, 0, sizeof(cam_rect_t));
    main_stream->getCropInfo(crop);

    cam_dimension_t src_dim, dst_dim;
    memset(&src_dim, 0, sizeof(cam_dimension_t));
    memset(&dst_dim, 0, sizeof(cam_dimension_t));
    main_stream->getFrameDimension(src_dim);

    bool hdr_output_crop = m_parent->mParameters.isHDROutputCropEnabled();
    if (hdr_output_crop && crop.height) {
        dst_dim.height = crop.height;
    } else {
        dst_dim.height = src_dim.height;
    }
    if (hdr_output_crop && crop.width) {
        dst_dim.width = crop.width;
    } else {
        dst_dim.width = src_dim.width;
    }

    // set rotation only when no online rotation or offline pp rotation is done before
    if (!m_parent->needRotationReprocess()) {
        encode_parm.rotation = m_parent->mParameters.getJpegRotation();
    }

    encode_parm.main_dim.src_dim = src_dim;
    encode_parm.main_dim.dst_dim = dst_dim;

    m_dst_dim = dst_dim;

    encode_parm.jpeg_cb = mJpegCB;
    encode_parm.userdata = mJpegUserData;

    m_bThumbnailNeeded = TRUE; // need encode thumbnail by default
    // system property to disable the thumbnail encoding in order to reduce the power
    // by default thumbnail encoding is set to TRUE and explicitly set this property to
    // disable the thumbnail encoding
    property_get("persist.camera.tn.disable", prop, "0");
    if (atoi(prop) == 1) {
        m_bThumbnailNeeded = FALSE;
        LOGH("m_bThumbnailNeeded is %d", m_bThumbnailNeeded);
    }
    cam_dimension_t thumbnailSize;
    memset(&thumbnailSize, 0, sizeof(cam_dimension_t));
    m_parent->getThumbnailSize(thumbnailSize);
    if (thumbnailSize.width == 0 || thumbnailSize.height == 0) {
        // (0,0) means no thumbnail
        m_bThumbnailNeeded = FALSE;
    }
    encode_parm.encode_thumbnail = m_bThumbnailNeeded;

    // get color format
    cam_format_t img_fmt = CAM_FORMAT_YUV_420_NV12;
    main_stream->getFormat(img_fmt);
    encode_parm.color_format = getColorfmtFromImgFmt(img_fmt);

    // get jpeg quality
    uint32_t val = m_parent->getJpegQuality();
    if (0U < val) {
        encode_parm.quality = val;
    } else {
        LOGH("Using default JPEG quality");
        encode_parm.quality = 85;
    }
    cam_frame_len_offset_t main_offset;
    memset(&main_offset, 0, sizeof(cam_frame_len_offset_t));
    main_stream->getFrameOffset(main_offset);

    // src buf config
    QCameraMemory *pStreamMem = main_stream->getStreamBufs();
    if (pStreamMem == NULL) {
        LOGE("cannot get stream bufs from main stream");
        ret = BAD_VALUE;
        goto on_error;
    }
    encode_parm.num_src_bufs = pStreamMem->getCnt();
    for (uint32_t i = 0; i < encode_parm.num_src_bufs; i++) {
        camera_memory_t *stream_mem = pStreamMem->getMemory(i, false);
        if (stream_mem != NULL) {
            encode_parm.src_main_buf[i].index = i;
            encode_parm.src_main_buf[i].buf_size = stream_mem->size;
            encode_parm.src_main_buf[i].buf_vaddr = (uint8_t *)stream_mem->data;
            encode_parm.src_main_buf[i].fd = pStreamMem->getFd(i);
            encode_parm.src_main_buf[i].format = MM_JPEG_FMT_YUV;
            encode_parm.src_main_buf[i].offset = main_offset;
        }
    }
    LOGI("Src Buffer cnt = %d, res = %dX%d len = %d rot = %d "
            "src_dim = %dX%d dst_dim = %dX%d",
            encode_parm.num_src_bufs,
            main_offset.mp[0].width, main_offset.mp[0].height,
            main_offset.frame_len, encode_parm.rotation,
            src_dim.width, src_dim.height,
            dst_dim.width, dst_dim.height);

    if (m_bThumbnailNeeded == TRUE) {
        m_parent->getThumbnailSize(encode_parm.thumb_dim.dst_dim);

        if (thumb_stream == NULL) {
            thumb_stream = main_stream;
        }
        if (((90 == m_parent->mParameters.getJpegRotation())
                || (270 == m_parent->mParameters.getJpegRotation()))
                && (m_parent->needRotationReprocess())) {
            // swap thumbnail dimensions
            cam_dimension_t tmp_dim = encode_parm.thumb_dim.dst_dim;
            encode_parm.thumb_dim.dst_dim.width = tmp_dim.height;
            encode_parm.thumb_dim.dst_dim.height = tmp_dim.width;
        }
        pStreamMem = thumb_stream->getStreamBufs();
        if (pStreamMem == NULL) {
            LOGE("cannot get stream bufs from thumb stream");
            ret = BAD_VALUE;
            goto on_error;
        }
        cam_frame_len_offset_t thumb_offset;
        memset(&thumb_offset, 0, sizeof(cam_frame_len_offset_t));
        thumb_stream->getFrameOffset(thumb_offset);
        encode_parm.num_tmb_bufs =  pStreamMem->getCnt();
        for (uint32_t i = 0; i < pStreamMem->getCnt(); i++) {
            camera_memory_t *stream_mem = pStreamMem->getMemory(i, false);
            if (stream_mem != NULL) {
                encode_parm.src_thumb_buf[i].index = i;
                encode_parm.src_thumb_buf[i].buf_size = stream_mem->size;
                encode_parm.src_thumb_buf[i].buf_vaddr = (uint8_t *)stream_mem->data;
                encode_parm.src_thumb_buf[i].fd = pStreamMem->getFd(i);
                encode_parm.src_thumb_buf[i].format = MM_JPEG_FMT_YUV;
                encode_parm.src_thumb_buf[i].offset = thumb_offset;
            }
        }
        cam_format_t img_fmt_thumb = CAM_FORMAT_YUV_420_NV12;
        thumb_stream->getFormat(img_fmt_thumb);
        encode_parm.thumb_color_format = getColorfmtFromImgFmt(img_fmt_thumb);

        // crop is the same if frame is the same
        if (thumb_stream != main_stream) {
            memset(&crop, 0, sizeof(cam_rect_t));
            thumb_stream->getCropInfo(crop);
        }

        memset(&src_dim, 0, sizeof(cam_dimension_t));
        thumb_stream->getFrameDimension(src_dim);
        encode_parm.thumb_dim.src_dim = src_dim;

        if (!m_parent->needRotationReprocess()) {
            encode_parm.thumb_rotation = m_parent->mParameters.getJpegRotation();
        }
        encode_parm.thumb_dim.crop = crop;
        encode_parm.thumb_from_postview =
            !m_parent->mParameters.generateThumbFromMain() &&
            (img_fmt_thumb != CAM_FORMAT_YUV_420_NV12_UBWC) &&
            (m_parent->mParameters.useJpegExifRotation() ||
            m_parent->mParameters.getJpegRotation() == 0);

        if (encode_parm.thumb_from_postview &&
          m_parent->mParameters.useJpegExifRotation()){
          encode_parm.thumb_rotation =
            m_parent->mParameters.getJpegExifRotation();
        }

        LOGI("Src THUMB buf_cnt = %d, res = %dX%d len = %d rot = %d "
            "src_dim = %dX%d, dst_dim = %dX%d",
            encode_parm.num_tmb_bufs,
            thumb_offset.mp[0].width, thumb_offset.mp[0].height,
            thumb_offset.frame_len, encode_parm.thumb_rotation,
            encode_parm.thumb_dim.src_dim.width,
            encode_parm.thumb_dim.src_dim.height,
            encode_parm.thumb_dim.dst_dim.width,
            encode_parm.thumb_dim.dst_dim.height);
    }

    encode_parm.num_dst_bufs = 1;
    if (mUseJpegBurst) {
        encode_parm.num_dst_bufs = MAX_JPEG_BURST;
    }
    encode_parm.get_memory = NULL;
    out_size = main_offset.frame_len;
    if (mJpegMemOpt) {
        encode_parm.get_memory = getJpegMemory;
        encode_parm.put_memory = releaseJpegMemory;
        out_size = sizeof(omx_jpeg_ouput_buf_t);
        encode_parm.num_dst_bufs = encode_parm.num_src_bufs;
    }
    m_JpegOutputMemCount = (uint32_t)encode_parm.num_dst_bufs;
    for (uint32_t i = 0; i < m_JpegOutputMemCount; i++) {
        if (m_pJpegOutputMem[i] != NULL)
          free(m_pJpegOutputMem[i]);
        omx_jpeg_ouput_buf_t omx_out_buf;
        memset(&omx_out_buf, 0, sizeof(omx_jpeg_ouput_buf_t));
        omx_out_buf.handle = this;
        // allocate output buf for jpeg encoding
        m_pJpegOutputMem[i] = malloc(out_size);

        if (NULL == m_pJpegOutputMem[i]) {
          ret = NO_MEMORY;
          LOGE("initHeapMem for jpeg, ret = NO_MEMORY");
          goto on_error;
        }

        if (mJpegMemOpt) {
            memcpy(m_pJpegOutputMem[i], &omx_out_buf, sizeof(omx_out_buf));
        }

        encode_parm.dest_buf[i].index = i;
        encode_parm.dest_buf[i].buf_size = main_offset.frame_len;
        encode_parm.dest_buf[i].buf_vaddr = (uint8_t *)m_pJpegOutputMem[i];
        encode_parm.dest_buf[i].fd = -1;
        encode_parm.dest_buf[i].format = MM_JPEG_FMT_YUV;
        encode_parm.dest_buf[i].offset = main_offset;
    }

    LOGD("X");
    return NO_ERROR;

on_error:
    FREE_JPEG_OUTPUT_BUFFER(m_pJpegOutputMem, m_JpegOutputMemCount);

    LOGD("X with error %d", ret);
    return ret;
}

/*===========================================================================
 * FUNCTION   : sendEvtNotify
 *
 * DESCRIPTION: send event notify through notify callback registered by upper layer
 *
 * PARAMETERS :
 *   @msg_type: msg type of notify
 *   @ext1    : extension
 *   @ext2    : extension
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraPostProcessor::sendEvtNotify(int32_t msg_type,
                                            int32_t ext1,
                                            int32_t ext2)
{
    return m_parent->sendEvtNotify(msg_type, ext1, ext2);
}

/*===========================================================================
 * FUNCTION   : sendDataNotify
 *
 * DESCRIPTION: enqueue data into dataNotify thread
 *
 * PARAMETERS :
 *   @msg_type: data callback msg type
 *   @data    : ptr to data memory struct
 *   @index   : index to data buffer
 *   @metadata: ptr to meta data buffer if there is any
 *   @release_data : ptr to struct indicating if data need to be released
 *                   after notify
 *   @super_buf_frame_idx : super buffer frame index
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraPostProcessor::sendDataNotify(int32_t msg_type,
                                             camera_memory_t *data,
                                             uint8_t index,
                                             camera_frame_metadata_t *metadata,
                                             qcamera_release_data_t *release_data,
                                             uint32_t super_buf_frame_idx)
{
    qcamera_data_argm_t *data_cb = (qcamera_data_argm_t *)malloc(sizeof(qcamera_data_argm_t));
    if (NULL == data_cb) {
        LOGE("no mem for acamera_data_argm_t");
        return NO_MEMORY;
    }
    memset(data_cb, 0, sizeof(qcamera_data_argm_t));
    data_cb->msg_type = msg_type;
    data_cb->data = data;
    data_cb->index = index;
    data_cb->metadata = metadata;
    if (release_data != NULL) {
        data_cb->release_data = *release_data;
    }

    qcamera_callback_argm_t cbArg;
    memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
    cbArg.cb_type = QCAMERA_DATA_SNAPSHOT_CALLBACK;
    cbArg.msg_type = msg_type;
    cbArg.data = data;
    cbArg.metadata = metadata;
    cbArg.user_data = data_cb;
    cbArg.cookie = this;
    cbArg.release_cb = releaseNotifyData;
    cbArg.frame_index = super_buf_frame_idx;
    int rc = m_parent->m_cbNotifier.notifyCallback(cbArg);
    if ( NO_ERROR != rc ) {
        LOGE("Error enqueuing jpeg data into notify queue");
        releaseNotifyData(data_cb, this, UNKNOWN_ERROR);
        return UNKNOWN_ERROR;
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : validatePostProcess
 *
 * DESCRIPTION: Verify output buffer count of pp module
 *
 * PARAMETERS :
 *   @frame   : process frame received from mm-camera-interface
 *
 * RETURN     : bool type of status
 *              TRUE  -- success
 *              FALSE     failure
 *==========================================================================*/
bool QCameraPostProcessor::validatePostProcess(mm_camera_super_buf_t *frame)
{
    bool status = TRUE;
    QCameraChannel *pChannel = NULL;
    QCameraReprocessChannel *m_pReprocChannel = NULL;

    if (frame == NULL) {
        return status;
    }

    pChannel = m_parent->getChannelByHandle(frame->ch_id);
    for (int8_t i = 0; i < mPPChannelCount; i++) {
        if (pChannel == mPPChannels[i]->getSrcChannel()) {
            m_pReprocChannel = mPPChannels[i];
            break;
        }
    }

    if ((m_pReprocChannel != NULL) && (pChannel == m_pReprocChannel->getSrcChannel())) {
        QCameraStream *pStream = NULL;
        for (uint8_t i = 0; i < m_pReprocChannel->getNumOfStreams(); i++) {
            pStream = m_pReprocChannel->getStreamByIndex(i);
            if (pStream && (m_inputPPQ.getCurrentSize() > 0) &&
                    (m_ongoingPPQ.getCurrentSize() >=  pStream->getNumQueuedBuf())) {
                LOGW("Out of PP Buffer PPQ = %d ongoingQ = %d Jpeg = %d onJpeg = %d",
                        m_inputPPQ.getCurrentSize(), m_ongoingPPQ.getCurrentSize(),
                        m_inputJpegQ.getCurrentSize(), m_ongoingJpegQ.getCurrentSize());
                status = FALSE;
                break;
            }
        }
    }
    return status;
}

/*===========================================================================
 * FUNCTION   : getOfflinePPInputBuffer
 *
 * DESCRIPTION: Function to generate offline post proc buffer
 *
 * PARAMETERS :
 * @src_frame : process frame received from mm-camera-interface
 *
 * RETURN     : Buffer pointer if successfull
 *            : NULL in case of failures
 *==========================================================================*/
mm_camera_buf_def_t *QCameraPostProcessor::getOfflinePPInputBuffer(
        mm_camera_super_buf_t *src_frame)
{
    mm_camera_buf_def_t *mBufDefs = NULL;
    QCameraChannel *pChannel = NULL;
    QCameraStream *src_pStream = NULL;
    mm_camera_buf_def_t *data_frame = NULL;
    mm_camera_buf_def_t *meta_frame = NULL;

    if (mOfflineDataBufs == NULL) {
        LOGE("Offline Buffer not allocated");
        return NULL;
    }

    uint32_t num_bufs = mOfflineDataBufs->getCnt();
    size_t bufDefsSize = num_bufs * sizeof(mm_camera_buf_def_t);
    mBufDefs = (mm_camera_buf_def_t *)malloc(bufDefsSize);
    if (mBufDefs == NULL) {
        LOGE("No memory");
        return NULL;
    }
    memset(mBufDefs, 0, bufDefsSize);

    pChannel = m_parent->getChannelByHandle(src_frame->ch_id);
    for (uint32_t i = 0; i < src_frame->num_bufs; i++) {
        src_pStream = pChannel->getStreamByHandle(
                src_frame->bufs[i]->stream_id);
        if (src_pStream != NULL) {
            if (src_pStream->getMyType() == CAM_STREAM_TYPE_RAW) {
                LOGH("Found RAW input stream");
                data_frame = src_frame->bufs[i];
            } else if (src_pStream->getMyType() == CAM_STREAM_TYPE_METADATA){
                LOGH("Found Metada input stream");
                meta_frame = src_frame->bufs[i];
            }
        }
    }

    if ((src_pStream != NULL) && (data_frame != NULL)) {
        cam_frame_len_offset_t offset;
        memset(&offset, 0, sizeof(cam_frame_len_offset_t));
        src_pStream->getFrameOffset(offset);
        for (uint32_t i = 0; i < num_bufs; i++) {
            mBufDefs[i] = *data_frame;
            mOfflineDataBufs->getBufDef(offset, mBufDefs[i], i);

            LOGD("Dumping RAW data on offline buffer");
            /*Actual data memcpy just for verification*/
            memcpy(mBufDefs[i].buffer, data_frame->buffer,
                    mBufDefs[i].frame_len);
        }
        releaseSuperBuf(src_frame, CAM_STREAM_TYPE_RAW);
    } else {
        free(mBufDefs);
        mBufDefs = NULL;
    }

    LOGH("mBufDefs = %p", mBufDefs);
    return mBufDefs;
}

/*===========================================================================
 * FUNCTION   : processData
 *
 * DESCRIPTION: enqueue data into dataProc thread
 *
 * PARAMETERS :
 *   @frame   : process frame received from mm-camera-interface
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *
 * NOTE       : depends on if offline reprocess is needed, received frame will
 *              be sent to either input queue of postprocess or jpeg encoding
 *==========================================================================*/
int32_t QCameraPostProcessor::processData(mm_camera_super_buf_t *frame)
{
    if (m_bInited == FALSE) {
        LOGE("postproc not initialized yet");
        return UNKNOWN_ERROR;
    }

    if (frame == NULL) {
        LOGE("Invalid parameter");
        return UNKNOWN_ERROR;
    }

    mm_camera_buf_def_t *meta_frame = NULL;
    for (uint32_t i = 0; i < frame->num_bufs; i++) {
        // look through input superbuf
        if (frame->bufs[i]->stream_type == CAM_STREAM_TYPE_METADATA) {
            meta_frame = frame->bufs[i];
            break;
        }
    }
    if (meta_frame != NULL) {
        //Function to upadte metadata for frame based parameter
        m_parent->updateMetadata((metadata_buffer_t *)meta_frame->buffer);
    }

    if (m_parent->needReprocess()) {
        if ((!m_parent->isLongshotEnabled() &&
             !m_parent->m_stateMachine.isNonZSLCaptureRunning()) ||
            (m_parent->isLongshotEnabled() &&
             m_parent->isCaptureShutterEnabled())) {
            //play shutter sound
            m_parent->playShutter();
        }

        ATRACE_INT("Camera:Reprocess", 1);
        LOGH("need reprocess");

        // enqueu to post proc input queue
        qcamera_pp_data_t *pp_request_job =
                (qcamera_pp_data_t *)malloc(sizeof(qcamera_pp_data_t));
        if (pp_request_job == NULL) {
            LOGE("No memory for pproc job");
            return NO_MEMORY;
        }
        memset(pp_request_job, 0, sizeof(qcamera_pp_data_t));
        pp_request_job->src_frame = frame;
        pp_request_job->src_reproc_frame = frame;
        pp_request_job->reprocCount = 0;
        pp_request_job->ppChannelIndex = 0;

        if ((NULL != frame) &&
                (0 < frame->num_bufs)
                && (m_parent->isRegularCapture())) {
            /*Regular capture. Source stream will be deleted*/
            mm_camera_buf_def_t *bufs = NULL;
            uint32_t num_bufs = frame->num_bufs;
            bufs = new mm_camera_buf_def_t[num_bufs];
            if (NULL == bufs) {
                LOGE("Unable to allocate cached buffers");
                return NO_MEMORY;
            }

            for (uint32_t i = 0; i < num_bufs; i++) {
                bufs[i] = *frame->bufs[i];
                frame->bufs[i] = &bufs[i];
            }
            pp_request_job->src_reproc_bufs = bufs;

            // Don't release source frame after encoding
            // at this point the source channel will not exist.
            pp_request_job->reproc_frame_release = true;
        }

        if (mOfflineDataBufs != NULL) {
            pp_request_job->offline_reproc_buf =
                    getOfflinePPInputBuffer(frame);
            if (pp_request_job->offline_reproc_buf != NULL) {
                pp_request_job->offline_buffer = true;
            }
        }

        if (false == m_inputPPQ.enqueue((void *)pp_request_job)) {
            LOGW("Input PP Q is not active!!!");
            releaseSuperBuf(frame);
            free(frame);
            free(pp_request_job);
            frame = NULL;
            pp_request_job = NULL;
            return NO_ERROR;
        }
        if (m_parent->mParameters.isAdvCamFeaturesEnabled()
                && (meta_frame != NULL)) {
            m_InputMetadata.add(meta_frame);
        }
    } else if (m_parent->mParameters.isNV16PictureFormat() ||
        m_parent->mParameters.isNV21PictureFormat()) {
        //check if raw frame information is needed.
        if(m_parent->mParameters.isYUVFrameInfoNeeded())
            setYUVFrameInfo(frame);

        processRawData(frame);
    } else {
        //play shutter sound
        if(!m_parent->m_stateMachine.isNonZSLCaptureRunning() &&
           !m_parent->mLongshotEnabled)
           m_parent->playShutter();

        LOGH("no need offline reprocess, sending to jpeg encoding");
        qcamera_jpeg_data_t *jpeg_job =
            (qcamera_jpeg_data_t *)malloc(sizeof(qcamera_jpeg_data_t));
        if (jpeg_job == NULL) {
            LOGE("No memory for jpeg job");
            return NO_MEMORY;
        }

        memset(jpeg_job, 0, sizeof(qcamera_jpeg_data_t));
        jpeg_job->src_frame = frame;

        if (meta_frame != NULL) {
            // fill in meta data frame ptr
            jpeg_job->metadata = (metadata_buffer_t *)meta_frame->buffer;
        }

        // enqueu to jpeg input queue
        if (!m_inputJpegQ.enqueue((void *)jpeg_job)) {
            LOGW("Input Jpeg Q is not active!!!");
            releaseJpegJobData(jpeg_job);
            free(jpeg_job);
            jpeg_job = NULL;
            return NO_ERROR;
        }
    }

    m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : processRawData
 *
 * DESCRIPTION: enqueue raw data into dataProc thread
 *
 * PARAMETERS :
 *   @frame   : process frame received from mm-camera-interface
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraPostProcessor::processRawData(mm_camera_super_buf_t *frame)
{
    if (m_bInited == FALSE) {
        LOGE("postproc not initialized yet");
        return UNKNOWN_ERROR;
    }

    // enqueu to raw input queue
    if (m_inputRawQ.enqueue((void *)frame)) {
        m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);
    } else {
        LOGW("m_inputRawQ is not active!!!");
        releaseSuperBuf(frame);
        free(frame);
        frame = NULL;
    }
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : processJpegEvt
 *
 * DESCRIPTION: process jpeg event from mm-jpeg-interface.
 *
 * PARAMETERS :
 *   @evt     : payload of jpeg event, including information about jpeg encoding
 *              status, jpeg size and so on.
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *
 * NOTE       : This event will also trigger DataProc thread to move to next job
 *              processing (i.e., send a new jpeg encoding job to mm-jpeg-interface
 *              if there is any pending job in jpeg input queue)
 *==========================================================================*/
int32_t QCameraPostProcessor::processJpegEvt(qcamera_jpeg_evt_payload_t *evt)
{
    if (m_bInited == FALSE) {
        LOGE("postproc not initialized yet");
        return UNKNOWN_ERROR;
    }

    int32_t rc = NO_ERROR;
    camera_memory_t *jpeg_mem = NULL;
    omx_jpeg_ouput_buf_t *jpeg_out = NULL;
    void *jpegData = NULL;
    if (mUseSaveProc && m_parent->isLongshotEnabled()) {
        qcamera_jpeg_evt_payload_t *saveData = ( qcamera_jpeg_evt_payload_t * ) malloc(sizeof(qcamera_jpeg_evt_payload_t));
        if ( NULL == saveData ) {
            LOGE("Can not allocate save data message!");
            return NO_MEMORY;
        }
        *saveData = *evt;
        if (m_inputSaveQ.enqueue((void *) saveData)) {
            m_saveProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);
        } else {
            LOGD("m_inputSaveQ PP Q is not active!!!");
            free(saveData);
            saveData = NULL;
            return rc;
        }
    } else {
        /* To be removed later when ISP Frame sync feature is available
                qcamera_jpeg_data_t *jpeg_job =
                    (qcamera_jpeg_data_t *)m_ongoingJpegQ.dequeue(matchJobId,
                    (void*)&evt->jobId);
                    uint32_t frame_idx = jpeg_job->src_frame->bufs[0]->frame_idx;*/
        uint32_t frame_idx = 75;
        LOGH("FRAME INDEX %d", frame_idx);
        // Release jpeg job data
        m_ongoingJpegQ.flushNodes(matchJobId, (void*)&evt->jobId);

        if (m_inputPPQ.getCurrentSize() > 0) {
            m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);
        }
        LOGH("[KPI Perf] : jpeg job %d", evt->jobId);

        if ((false == m_parent->m_bIntJpegEvtPending) &&
             (m_parent->mDataCb == NULL ||
              m_parent->msgTypeEnabledWithLock(CAMERA_MSG_COMPRESSED_IMAGE) == 0 )) {
            LOGW("No dataCB or CAMERA_MSG_COMPRESSED_IMAGE not enabled");
            rc = NO_ERROR;
            goto end;
        }

        if(evt->status == JPEG_JOB_STATUS_ERROR) {
            LOGE("Error event handled from jpeg, status = %d",
                   evt->status);
            rc = FAILED_TRANSACTION;
            goto end;
        }
        if (!mJpegMemOpt) {
            jpegData = evt->out_data.buf_vaddr;
        }
        else {
            jpeg_out  = (omx_jpeg_ouput_buf_t*) evt->out_data.buf_vaddr;
            if (jpeg_out != NULL) {
                jpeg_mem = (camera_memory_t *)jpeg_out->mem_hdl;
                if (jpeg_mem != NULL) {
                    jpegData = jpeg_mem->data;
                }
            }
        }
        m_parent->dumpJpegToFile(jpegData,
                                  evt->out_data.buf_filled_len,
                                  evt->jobId);
        LOGH("Dump jpeg_size=%d", evt->out_data.buf_filled_len);
        if(true == m_parent->m_bIntJpegEvtPending) {
              //Sending JPEG snapshot taken notification to HAL
              pthread_mutex_lock(&m_parent->m_int_lock);
              pthread_cond_signal(&m_parent->m_int_cond);
              pthread_mutex_unlock(&m_parent->m_int_lock);
              m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);
              return rc;
        }
        if (!mJpegMemOpt) {
            // alloc jpeg memory to pass to upper layer
            jpeg_mem = m_parent->mGetMemory(-1, evt->out_data.buf_filled_len,
                1, m_parent->mCallbackCookie);
            if (NULL == jpeg_mem) {
                rc = NO_MEMORY;
                LOGE("getMemory for jpeg, ret = NO_MEMORY");
                goto end;
            }
            memcpy(jpeg_mem->data, evt->out_data.buf_vaddr, evt->out_data.buf_filled_len);
        }
        LOGH("Calling upperlayer callback to store JPEG image");
        qcamera_release_data_t release_data;
        memset(&release_data, 0, sizeof(qcamera_release_data_t));
        release_data.data = jpeg_mem;
        LOGI("[KPI Perf]: PROFILE_JPEG_CB ");
        rc = sendDataNotify(CAMERA_MSG_COMPRESSED_IMAGE,
                jpeg_mem,
                0,
                NULL,
                &release_data,
                frame_idx);
        m_parent->setOutputImageCount(m_parent->getOutputImageCount() + 1);

end:
        if (rc != NO_ERROR) {
            // send error msg to upper layer
            LOGE("Jpeg Encoding failed. Notify Application");
            sendEvtNotify(CAMERA_MSG_ERROR,
                          UNKNOWN_ERROR,
                          0);

            if (NULL != jpeg_mem) {
                jpeg_mem->release(jpeg_mem);
                jpeg_mem = NULL;
            }
        }

        /* check whether to send callback for depth map */
        if (m_parent->mParameters.isUbiRefocus() &&
                (m_parent->getOutputImageCount() + 1 ==
                        m_parent->mParameters.getRefocusOutputCount())) {
            m_parent->setOutputImageCount(m_parent->getOutputImageCount() + 1);

            jpeg_mem = m_DataMem;
            release_data.data = jpeg_mem;
            m_DataMem = NULL;
            LOGH("[KPI Perf]: send jpeg callback for depthmap ");
            rc = sendDataNotify(CAMERA_MSG_COMPRESSED_IMAGE,
                    jpeg_mem,
                    0,
                    NULL,
                    &release_data,
                    frame_idx);
            if (rc != NO_ERROR) {
                // send error msg to upper layer
                sendEvtNotify(CAMERA_MSG_ERROR,
                        UNKNOWN_ERROR,
                        0);
                if (NULL != jpeg_mem) {
                    jpeg_mem->release(jpeg_mem);
                    jpeg_mem = NULL;
                }
            }
            m_DataMem = NULL;
        }
    }

    // wait up data proc thread to do next job,
    // if previous request is blocked due to ongoing jpeg job
    m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);

    return rc;
}

/*===========================================================================
 * FUNCTION   : processPPData
 *
 * DESCRIPTION: process received frame after reprocess.
 *
 * PARAMETERS :
 *   @frame   : received frame from reprocess channel.
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *
 * NOTE       : The frame after reprocess need to send to jpeg encoding.
 *==========================================================================*/
int32_t QCameraPostProcessor::processPPData(mm_camera_super_buf_t *frame)
{
    bool triggerEvent = TRUE;

    LOGD("QCameraPostProcessor::processPPData");
    bool needSuperBufMatch = m_parent->mParameters.generateThumbFromMain();
    if (m_bInited == FALSE) {
        LOGE("postproc not initialized yet");
        return UNKNOWN_ERROR;
    }

    qcamera_pp_data_t *job = (qcamera_pp_data_t *)m_ongoingPPQ.dequeue();
    if (NULL == job) {
        LOGE("Cannot find reprocess job");
        return BAD_VALUE;
    }

    if (!needSuperBufMatch && (job->src_frame == NULL
            || job->src_reproc_frame == NULL) ) {
        LOGE("Invalid reprocess job");
        return BAD_VALUE;
    }

    if (!needSuperBufMatch && (m_parent->mParameters.isNV16PictureFormat() ||
        m_parent->mParameters.isNV21PictureFormat())) {
        releaseOngoingPPData(job, this);
        free(job);

        if(m_parent->mParameters.isYUVFrameInfoNeeded())
            setYUVFrameInfo(frame);
        return processRawData(frame);
    }
#ifdef TARGET_TS_MAKEUP
    // find snapshot frame frame
    mm_camera_buf_def_t *pReprocFrame = NULL;
    QCameraStream * pSnapshotStream = NULL;
    QCameraChannel *pChannel = m_parent->getChannelByHandle(frame->ch_id);
    if (pChannel == NULL) {
        for (int8_t i = 0; i < mPPChannelCount; i++) {
            if ((mPPChannels[i] != NULL) &&
                    (mPPChannels[i]->getMyHandle() == frame->ch_id)) {
                pChannel = mPPChannels[i];
                break;
            }
        }
    }
    if (pChannel == NULL) {
        LOGE("No corresponding channel (ch_id = %d) exist, return here",
                frame->ch_id);
        return BAD_VALUE;
    }

    for (uint32_t i = 0; i < frame->num_bufs; i++) {
        pSnapshotStream = pChannel->getStreamByHandle(frame->bufs[i]->stream_id);
        if (pSnapshotStream != NULL) {
            if (pSnapshotStream->isOrignalTypeOf(CAM_STREAM_TYPE_SNAPSHOT)) {
                pReprocFrame = frame->bufs[i];
                break;
            }
        }
    }
    if (pReprocFrame != NULL && m_parent->mParameters.isFaceDetectionEnabled()) {
        m_parent->TsMakeupProcess_Snapshot(pReprocFrame,pSnapshotStream);
    } else {
        LOGH("pReprocFrame == NULL || isFaceDetectionEnabled = %d",
                m_parent->mParameters.isFaceDetectionEnabled());
    }
#endif
    int8_t mCurReprocCount = job->reprocCount;
    if ((m_parent->isLongshotEnabled()
            && (!(m_parent->mParameters.getQuadraCfa())|| (mCurReprocCount == 2)))
            && (!m_parent->isCaptureShutterEnabled())
            && (!m_parent->mCACDoneReceived)) {
        // play shutter sound for longshot
        // after reprocess is done
        m_parent->playShutter();
    }
    m_parent->mCACDoneReceived = FALSE;

    int8_t mCurChannelIndex = job->ppChannelIndex;
    if ( mCurReprocCount > 1 ) {
        //In case of pp 2nd pass, we can release input of 2nd pass
        releaseSuperBuf(job->src_frame);
        free(job->src_frame);
        job->src_frame = NULL;
    }

    LOGD("mCurReprocCount = %d mCurChannelIndex = %d mTotalNumReproc = %d",
             mCurReprocCount, mCurChannelIndex,
            m_parent->mParameters.getReprocCount());
    if (mCurReprocCount < m_parent->mParameters.getReprocCount()) {
        //More pp pass needed. Push frame back to pp queue.
        qcamera_pp_data_t *pp_request_job = job;
        pp_request_job->src_frame = frame;

        if ((mPPChannels[mCurChannelIndex]->getReprocCount()
                == mCurReprocCount) &&
                (mPPChannels[mCurChannelIndex + 1] != NULL)) {
            pp_request_job->ppChannelIndex++;
        }

        // enqueu to post proc input queue
        if (false == m_inputPPQ.enqueue((void *)pp_request_job)) {
            LOGW("m_input PP Q is not active!!!");
            releaseOngoingPPData(pp_request_job,this);
            free(pp_request_job);
            pp_request_job = NULL;
            triggerEvent = FALSE;
        }
    } else {
        //Done with post processing. Send frame to Jpeg
        qcamera_jpeg_data_t *jpeg_job =
                (qcamera_jpeg_data_t *)malloc(sizeof(qcamera_jpeg_data_t));
        if (jpeg_job == NULL) {
            LOGE("No memory for jpeg job");
            return NO_MEMORY;
        }

        memset(jpeg_job, 0, sizeof(qcamera_jpeg_data_t));
        jpeg_job->src_frame = frame;
        jpeg_job->src_reproc_frame = job ? job->src_reproc_frame : NULL;
        jpeg_job->src_reproc_bufs = job ? job->src_reproc_bufs : NULL;
        jpeg_job->reproc_frame_release = job ? job->reproc_frame_release : false;
        jpeg_job->offline_reproc_buf = job ? job->offline_reproc_buf : NULL;
        jpeg_job->offline_buffer = job ? job->offline_buffer : false;

        // find meta data frame
        mm_camera_buf_def_t *meta_frame = NULL;
        if (m_parent->mParameters.isAdvCamFeaturesEnabled()) {
            size_t meta_idx = m_parent->mParameters.getExifBufIndex(m_PPindex);
            if (m_InputMetadata.size() >= (meta_idx + 1)) {
                meta_frame = m_InputMetadata.itemAt(meta_idx);
            } else {
                LOGW("Input metadata vector contains %d entries, index required %d",
                         m_InputMetadata.size(), meta_idx);
            }
            m_PPindex++;
        } else {
            for (uint32_t i = 0; job && job->src_reproc_frame &&
                    (i < job->src_reproc_frame->num_bufs); i++) {
                // look through input superbuf
                if (job->src_reproc_frame->bufs[i]->stream_type == CAM_STREAM_TYPE_METADATA) {
                    meta_frame = job->src_reproc_frame->bufs[i];
                    break;
                }
            }

            if (meta_frame == NULL) {
                // look through reprocess superbuf
                for (uint32_t i = 0; i < frame->num_bufs; i++) {
                    if (frame->bufs[i]->stream_type == CAM_STREAM_TYPE_METADATA) {
                        meta_frame = frame->bufs[i];
                        break;
                    }
                }
            }
        }
        if (meta_frame != NULL) {
            // fill in meta data frame ptr
            jpeg_job->metadata = (metadata_buffer_t *)meta_frame->buffer;
        }

        if (m_parent->mParameters.getQuadraCfa()) {
            // find offline metadata frame for quadra CFA
            mm_camera_buf_def_t *pOfflineMetaFrame = NULL;
            QCameraStream * pOfflineMetadataStream = NULL;
            QCameraChannel *pChannel = m_parent->getChannelByHandle(frame->ch_id);
            if (pChannel == NULL) {
                for (int8_t i = 0; i < mPPChannelCount; i++) {
                    if ((mPPChannels[i] != NULL) &&
                            (mPPChannels[i]->getMyHandle() == frame->ch_id)) {
                        pChannel = mPPChannels[i];
                        break;
                    }
                }
            }
            if (pChannel == NULL) {
                LOGE("No corresponding channel (ch_id = %d) exist, return here",
                        frame->ch_id);
                return BAD_VALUE;
            }

            for (uint32_t i = 0; i < frame->num_bufs; i++) {
                pOfflineMetadataStream = pChannel->getStreamByHandle(frame->bufs[i]->stream_id);
                if (pOfflineMetadataStream != NULL) {
                    if (pOfflineMetadataStream->isOrignalTypeOf(CAM_STREAM_TYPE_METADATA)) {
                        pOfflineMetaFrame = frame->bufs[i];
                        break;
                    }
                }
            }
            if (pOfflineMetaFrame != NULL) {
                // fill in meta data frame ptr
                jpeg_job->metadata = (metadata_buffer_t *)pOfflineMetaFrame->buffer;

                // Dump offline metadata for Tuning
                char value[PROPERTY_VALUE_MAX];
                property_get("persist.camera.dumpmetadata", value, "0");
                int32_t enabled = atoi(value);
                if (enabled && jpeg_job->metadata->is_tuning_params_valid) {
                    m_parent->dumpMetadataToFile(pOfflineMetadataStream,pOfflineMetaFrame,
                                                 (char *)"Offline_isp_meta");
                }
            }
        }

        // enqueu reprocessed frame to jpeg input queue
        if (false == m_inputJpegQ.enqueue((void *)jpeg_job)) {
            LOGW("Input Jpeg Q is not active!!!");
            releaseJpegJobData(jpeg_job);
            free(jpeg_job);
            jpeg_job = NULL;
            triggerEvent = FALSE;
        }

        // free pp job buf
        pthread_mutex_lock(&m_reprocess_lock);
        if (job) {
            free(job);
        }
        pthread_mutex_unlock(&m_reprocess_lock);
    }

    LOGD("");
    // wait up data proc thread

    if (triggerEvent) {
        m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);
    }

    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : findJpegJobByJobId
 *
 * DESCRIPTION: find a jpeg job from ongoing Jpeg queue by its job ID
 *
 * PARAMETERS :
 *   @jobId   : job Id of the job
 *
 * RETURN     : ptr to a jpeg job struct. NULL if not found.
 *
 * NOTE       : Currently only one job is sending to mm-jpeg-interface for jpeg
 *              encoding. Therefore simply dequeue from the ongoing Jpeg Queue
 *              will serve the purpose to find the jpeg job.
 *==========================================================================*/
qcamera_jpeg_data_t *QCameraPostProcessor::findJpegJobByJobId(uint32_t jobId)
{
    qcamera_jpeg_data_t * job = NULL;
    if (jobId == 0) {
        LOGE("not a valid jpeg jobId");
        return NULL;
    }

    // currely only one jpeg job ongoing, so simply dequeue the head
    job = (qcamera_jpeg_data_t *)m_ongoingJpegQ.dequeue();
    return job;
}

/*===========================================================================
 * FUNCTION   : releasePPInputData
 *
 * DESCRIPTION: callback function to release post process input data node
 *
 * PARAMETERS :
 *   @data      : ptr to post process input data
 *   @user_data : user data ptr (QCameraReprocessor)
 *
 * RETURN     : None
 *==========================================================================*/
void QCameraPostProcessor::releasePPInputData(void *data, void *user_data)
{
    QCameraPostProcessor *pme = (QCameraPostProcessor *)user_data;
    if (NULL != pme) {
        qcamera_pp_request_t *pp_job = (qcamera_pp_request_t *)data;
        if (NULL != pp_job->src_frame) {
            pme->releaseSuperBuf(pp_job->src_frame);
            if (pp_job->src_frame == pp_job->src_reproc_frame)
                pp_job->src_reproc_frame = NULL;
            free(pp_job->src_frame);
            pp_job->src_frame = NULL;
        }
        if (NULL != pp_job->src_reproc_frame) {
            pme->releaseSuperBuf(pp_job->src_reproc_frame);
            free(pp_job->src_reproc_frame);
            pp_job->src_reproc_frame = NULL;
        }
        pp_job->reprocCount = 0;
    }
}

/*===========================================================================
 * FUNCTION   : releaseJpegData
 *
 * DESCRIPTION: callback function to release jpeg job node
 *
 * PARAMETERS :
 *   @data      : ptr to ongoing jpeg job data
 *   @user_data : user data ptr (QCameraReprocessor)
 *
 * RETURN     : None
 *==========================================================================*/
void QCameraPostProcessor::releaseJpegData(void *data, void *user_data)
{
    QCameraPostProcessor *pme = (QCameraPostProcessor *)user_data;
    if (NULL != pme) {
        pme->releaseJpegJobData((qcamera_jpeg_data_t *)data);
        LOGH("Rleased job ID %u",
            ((qcamera_jpeg_data_t *)data)->jobId);
    }
}

/*===========================================================================
 * FUNCTION   : releaseOngoingPPData
 *
 * DESCRIPTION: callback function to release ongoing postprocess job node
 *
 * PARAMETERS :
 *   @data      : ptr to onging postprocess job
 *   @user_data : user data ptr (QCameraReprocessor)
 *
 * RETURN     : None
 *==========================================================================*/
void QCameraPostProcessor::releaseOngoingPPData(void *data, void *user_data)
{
    QCameraPostProcessor *pme = (QCameraPostProcessor *)user_data;
    if (NULL != pme) {
        qcamera_pp_data_t *pp_job = (qcamera_pp_data_t *)data;
        if (NULL != pp_job->src_frame) {
            if (!pp_job->reproc_frame_release) {
                pme->releaseSuperBuf(pp_job->src_frame);
            }
            if (pp_job->src_frame == pp_job->src_reproc_frame)
                pp_job->src_reproc_frame = NULL;

            free(pp_job->src_frame);
            pp_job->src_frame = NULL;
        }
        if (NULL != pp_job->src_reproc_frame) {
            pme->releaseSuperBuf(pp_job->src_reproc_frame);
            free(pp_job->src_reproc_frame);
            pp_job->src_reproc_frame = NULL;
        }
        if ((pp_job->offline_reproc_buf != NULL)
                && (pp_job->offline_buffer)) {
            free(pp_job->offline_reproc_buf);
            pp_job->offline_buffer = false;
            pp_job->offline_reproc_buf = NULL;
        }
        pp_job->reprocCount = 0;
    }
}

/*===========================================================================
 * FUNCTION   : releaseNotifyData
 *
 * DESCRIPTION: function to release internal resources in notify data struct
 *
 * PARAMETERS :
 *   @user_data  : ptr user data
 *   @cookie     : callback cookie
 *   @cb_status  : callback status
 *
 * RETURN     : None
 *
 * NOTE       : deallocate jpeg heap memory if it's not NULL
 *==========================================================================*/
void QCameraPostProcessor::releaseNotifyData(void *user_data,
                                             void *cookie,
                                             int32_t cb_status)
{
    LOGD("releaseNotifyData release_data %p", user_data);

    qcamera_data_argm_t *app_cb = ( qcamera_data_argm_t * ) user_data;
    QCameraPostProcessor *postProc = ( QCameraPostProcessor * ) cookie;
    if ( ( NULL != app_cb ) && ( NULL != postProc ) ) {

        if ( postProc->mUseSaveProc &&
             app_cb->release_data.unlinkFile &&
             ( NO_ERROR != cb_status ) ) {

            String8 unlinkPath((const char *) app_cb->release_data.data->data,
                                app_cb->release_data.data->size);
            int rc = unlink(unlinkPath.string());
            LOGH("Unlinking stored file rc = %d",
                  rc);
        }

        if (app_cb && NULL != app_cb->release_data.data) {
            app_cb->release_data.data->release(app_cb->release_data.data);
            app_cb->release_data.data = NULL;
        }
        if (app_cb && NULL != app_cb->release_data.frame) {
            postProc->releaseSuperBuf(app_cb->release_data.frame);
            free(app_cb->release_data.frame);
            app_cb->release_data.frame = NULL;
        }
        if (app_cb && NULL != app_cb->release_data.streamBufs) {
            app_cb->release_data.streamBufs->deallocate();
            delete app_cb->release_data.streamBufs;
            app_cb->release_data.streamBufs = NULL;
        }
        free(app_cb);
    }
}

/*===========================================================================
 * FUNCTION   : releaseSuperBuf
 *
 * DESCRIPTION: function to release a superbuf frame by returning back to kernel
 *
 * PARAMETERS :
 * @super_buf : ptr to the superbuf frame
 *
 * RETURN     : None
 *==========================================================================*/
void QCameraPostProcessor::releaseSuperBuf(mm_camera_super_buf_t *super_buf)
{
    QCameraChannel *pChannel = NULL;

    if (NULL != super_buf) {
        pChannel = m_parent->getChannelByHandle(super_buf->ch_id);

        if ( NULL == pChannel ) {
            for (int8_t i = 0; i < mPPChannelCount; i++) {
                if ((mPPChannels[i] != NULL) &&
                        (mPPChannels[i]->getMyHandle() == super_buf->ch_id)) {
                    pChannel = mPPChannels[i];
                    break;
                }
            }
        }

        if (pChannel != NULL) {
            pChannel->bufDone(super_buf);
        } else {
            LOGE("Channel id %d not found!!",
                  super_buf->ch_id);
        }
    }
}

/*===========================================================================
 * FUNCTION    : releaseSuperBuf
 *
 * DESCRIPTION : function to release a superbuf frame by returning back to kernel
 *
 * PARAMETERS  :
 * @super_buf  : ptr to the superbuf frame
 * @stream_type: Type of stream to be released
 *
 * RETURN      : None
 *==========================================================================*/
void QCameraPostProcessor::releaseSuperBuf(mm_camera_super_buf_t *super_buf,
        cam_stream_type_t stream_type)
{
    QCameraChannel *pChannel = NULL;

    if (NULL != super_buf) {
        pChannel = m_parent->getChannelByHandle(super_buf->ch_id);
        if (pChannel == NULL) {
            for (int8_t i = 0; i < mPPChannelCount; i++) {
                if ((mPPChannels[i] != NULL) &&
                        (mPPChannels[i]->getMyHandle() == super_buf->ch_id)) {
                    pChannel = mPPChannels[i];
                    break;
                }
            }
        }

        if (pChannel != NULL) {
            for (uint32_t i = 0; i < super_buf->num_bufs; i++) {
                if (super_buf->bufs[i] != NULL) {
                    QCameraStream *pStream =
                            pChannel->getStreamByHandle(super_buf->bufs[i]->stream_id);
                    if ((pStream != NULL) && ((pStream->getMyType() == stream_type)
                            || (pStream->getMyOriginalType() == stream_type))) {
                        pChannel->bufDone(super_buf, super_buf->bufs[i]->stream_id);
                        break;
                    }
                }
            }
        } else {
            LOGE("Channel id %d not found!!",
                   super_buf->ch_id);
        }
    }
}

/*===========================================================================
 * FUNCTION   : releaseJpegJobData
 *
 * DESCRIPTION: function to release internal resources in jpeg job struct
 *
 * PARAMETERS :
 *   @job     : ptr to jpeg job struct
 *
 * RETURN     : None
 *
 * NOTE       : original source frame need to be queued back to kernel for
 *              future use. Output buf of jpeg job need to be released since
 *              it's allocated for each job. Exif object need to be deleted.
 *==========================================================================*/
void QCameraPostProcessor::releaseJpegJobData(qcamera_jpeg_data_t *job)
{
    LOGD("E");
    if (NULL != job) {
        if (NULL != job->src_reproc_frame) {
            if (!job->reproc_frame_release) {
                releaseSuperBuf(job->src_reproc_frame);
            }
            free(job->src_reproc_frame);
            job->src_reproc_frame = NULL;
        }

        if (NULL != job->src_frame) {
            releaseSuperBuf(job->src_frame);
            free(job->src_frame);
            job->src_frame = NULL;
        }

        if (NULL != job->pJpegExifObj) {
            delete job->pJpegExifObj;
            job->pJpegExifObj = NULL;
        }

        if (NULL != job->src_reproc_bufs) {
            delete [] job->src_reproc_bufs;
        }

        if ((job->offline_reproc_buf != NULL)
                && (job->offline_buffer)) {
            free(job->offline_reproc_buf);
            job->offline_buffer = false;
        }
    }
    LOGD("X");
}

/*===========================================================================
 * FUNCTION   : releaseSaveJobData
 *
 * DESCRIPTION: function to release internal resources in store jobs
 *
 * PARAMETERS :
 *   @job     : ptr to save job struct
 *
 * RETURN     : None
 *
 *==========================================================================*/
void QCameraPostProcessor::releaseSaveJobData(void *data, void *user_data)
{
    LOGD("E");

    QCameraPostProcessor *pme = (QCameraPostProcessor *) user_data;
    if (NULL == pme) {
        LOGE("Invalid postproc handle");
        return;
    }

    qcamera_jpeg_evt_payload_t *job_data = (qcamera_jpeg_evt_payload_t *) data;
    if (job_data == NULL) {
        LOGE("Invalid jpeg event data");
        return;
    }

    // find job by jobId
    qcamera_jpeg_data_t *job = pme->findJpegJobByJobId(job_data->jobId);

    if (NULL != job) {
        pme->releaseJpegJobData(job);
        free(job);
    } else {
        LOGE("Invalid jpeg job");
    }

    LOGD("X");
}

/*===========================================================================
 * FUNCTION   : releaseRawData
 *
 * DESCRIPTION: function to release internal resources in store jobs
 *
 * PARAMETERS :
 *   @job     : ptr to save job struct
 *
 * RETURN     : None
 *
 *==========================================================================*/
void QCameraPostProcessor::releaseRawData(void *data, void *user_data)
{
    LOGD("E");

    QCameraPostProcessor *pme = (QCameraPostProcessor *) user_data;
    if (NULL == pme) {
        LOGE("Invalid postproc handle");
        return;
    }
    mm_camera_super_buf_t *super_buf = (mm_camera_super_buf_t *) data;
    pme->releaseSuperBuf(super_buf);

    LOGD("X");
}


/*===========================================================================
 * FUNCTION   : getColorfmtFromImgFmt
 *
 * DESCRIPTION: function to return jpeg color format based on its image format
 *
 * PARAMETERS :
 *   @img_fmt : image format
 *
 * RETURN     : jpeg color format that can be understandable by omx lib
 *==========================================================================*/
mm_jpeg_color_format QCameraPostProcessor::getColorfmtFromImgFmt(cam_format_t img_fmt)
{
    switch (img_fmt) {
    case CAM_FORMAT_YUV_420_NV21:
    case CAM_FORMAT_YUV_420_NV21_VENUS:
        return MM_JPEG_COLOR_FORMAT_YCRCBLP_H2V2;
    case CAM_FORMAT_YUV_420_NV21_ADRENO:
        return MM_JPEG_COLOR_FORMAT_YCRCBLP_H2V2;
    case CAM_FORMAT_YUV_420_NV12:
    case CAM_FORMAT_YUV_420_NV12_VENUS:
        return MM_JPEG_COLOR_FORMAT_YCBCRLP_H2V2;
    case CAM_FORMAT_YUV_420_YV12:
        return MM_JPEG_COLOR_FORMAT_YCBCRLP_H2V2;
    case CAM_FORMAT_YUV_422_NV61:
        return MM_JPEG_COLOR_FORMAT_YCRCBLP_H2V1;
    case CAM_FORMAT_YUV_422_NV16:
        return MM_JPEG_COLOR_FORMAT_YCBCRLP_H2V1;
    default:
        return MM_JPEG_COLOR_FORMAT_YCRCBLP_H2V2;
    }
}

/*===========================================================================
 * FUNCTION   : getJpegImgTypeFromImgFmt
 *
 * DESCRIPTION: function to return jpeg encode image type based on its image format
 *
 * PARAMETERS :
 *   @img_fmt : image format
 *
 * RETURN     : return jpeg source image format (YUV or Bitstream)
 *==========================================================================*/
mm_jpeg_format_t QCameraPostProcessor::getJpegImgTypeFromImgFmt(cam_format_t img_fmt)
{
    switch (img_fmt) {
    case CAM_FORMAT_YUV_420_NV21:
    case CAM_FORMAT_YUV_420_NV21_ADRENO:
    case CAM_FORMAT_YUV_420_NV12:
    case CAM_FORMAT_YUV_420_NV12_VENUS:
    case CAM_FORMAT_YUV_420_NV21_VENUS:
    case CAM_FORMAT_YUV_420_YV12:
    case CAM_FORMAT_YUV_422_NV61:
    case CAM_FORMAT_YUV_422_NV16:
        return MM_JPEG_FMT_YUV;
    default:
        return MM_JPEG_FMT_YUV;
    }
}

/*===========================================================================
 * FUNCTION   : queryStreams
 *
 * DESCRIPTION: utility method for retrieving main, thumbnail and reprocess
 *              streams and frame from bundled super buffer
 *
 * PARAMETERS :
 *   @main    : ptr to main stream if present
 *   @thumb   : ptr to thumbnail stream if present
 *   @reproc  : ptr to reprocess stream if present
 *   @main_image : ptr to main image if present
 *   @thumb_image: ptr to thumbnail image if present
 *   @frame   : bundled super buffer
 *   @reproc_frame : bundled source frame buffer
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraPostProcessor::queryStreams(QCameraStream **main,
        QCameraStream **thumb,
        QCameraStream **reproc,
        mm_camera_buf_def_t **main_image,
        mm_camera_buf_def_t **thumb_image,
        mm_camera_super_buf_t *frame,
        mm_camera_super_buf_t *reproc_frame)
{
    if (NULL == frame) {
        return NO_INIT;
    }

    QCameraChannel *pChannel = m_parent->getChannelByHandle(frame->ch_id);
    // check reprocess channel if not found
    if (pChannel == NULL) {
        for (int8_t i = 0; i < mPPChannelCount; i++) {
            if ((mPPChannels[i] != NULL) &&
                    (mPPChannels[i]->getMyHandle() == frame->ch_id)) {
                pChannel = mPPChannels[i];
                break;
            }
        }
    }
    if (pChannel == NULL) {
        LOGD("No corresponding channel (ch_id = %d) exist, return here",
               frame->ch_id);
        return BAD_VALUE;
    }

    // Use snapshot stream to create thumbnail if snapshot and preview
    // flip settings doesn't match in ZSL mode.
    bool thumb_stream_needed = ((!m_parent->isZSLMode() ||
        (m_parent->mParameters.getFlipMode(CAM_STREAM_TYPE_SNAPSHOT) ==
         m_parent->mParameters.getFlipMode(CAM_STREAM_TYPE_PREVIEW))) &&
        !m_parent->mParameters.generateThumbFromMain());

    *main = *thumb = *reproc = NULL;
    *main_image = *thumb_image = NULL;
    // find snapshot frame and thumnail frame
    for (uint32_t i = 0; i < frame->num_bufs; i++) {
        QCameraStream *pStream =
                pChannel->getStreamByHandle(frame->bufs[i]->stream_id);
        if (pStream != NULL) {
            if (pStream->isTypeOf(CAM_STREAM_TYPE_SNAPSHOT) ||
                    pStream->isOrignalTypeOf(CAM_STREAM_TYPE_SNAPSHOT) ||
                    pStream->isTypeOf(CAM_STREAM_TYPE_VIDEO) ||
                    pStream->isOrignalTypeOf(CAM_STREAM_TYPE_VIDEO) ||
                    pStream->isOrignalTypeOf(CAM_STREAM_TYPE_RAW)) {
                *main= pStream;
                *main_image = frame->bufs[i];
            } else if (thumb_stream_needed &&
                       (pStream->isTypeOf(CAM_STREAM_TYPE_PREVIEW) ||
                        pStream->isTypeOf(CAM_STREAM_TYPE_POSTVIEW) ||
                        pStream->isOrignalTypeOf(CAM_STREAM_TYPE_PREVIEW) ||
                        pStream->isOrignalTypeOf(CAM_STREAM_TYPE_POSTVIEW))) {
                *thumb = pStream;
                *thumb_image = frame->bufs[i];
            }
            if (pStream->isTypeOf(CAM_STREAM_TYPE_OFFLINE_PROC) ) {
                *reproc = pStream;
            }
        }
    }

    if (thumb_stream_needed && *thumb_image == NULL && reproc_frame != NULL) {
        QCameraChannel *pSrcReprocChannel = NULL;
        pSrcReprocChannel = m_parent->getChannelByHandle(reproc_frame->ch_id);
        if (pSrcReprocChannel != NULL) {
            // find thumbnail frame
            for (uint32_t i = 0; i < reproc_frame->num_bufs; i++) {
                QCameraStream *pStream =
                        pSrcReprocChannel->getStreamByHandle(
                                reproc_frame->bufs[i]->stream_id);
                if (pStream != NULL) {
                    if (pStream->isTypeOf(CAM_STREAM_TYPE_PREVIEW) ||
                        pStream->isTypeOf(CAM_STREAM_TYPE_POSTVIEW)) {
                        *thumb = pStream;
                        *thumb_image = reproc_frame->bufs[i];
                    }
                }
            }
        }
    }

    return NO_ERROR;
}

/*===========================================================================
* FUNCTION   : syncStreamParams
*
* DESCRIPTION: Query the runtime parameters of all streams included
*              in the main and reprocessed frames
*
* PARAMETERS :
*   @frame : Main image super buffer
*   @reproc_frame : Image supper buffer that got processed
*
* RETURN     : int32_t type of status
*              NO_ERROR  -- success
*              none-zero failure code
*==========================================================================*/
int32_t QCameraPostProcessor::syncStreamParams(mm_camera_super_buf_t *frame,
        mm_camera_super_buf_t *reproc_frame)
{
    QCameraStream *reproc_stream = NULL;
    QCameraStream *main_stream = NULL;
    QCameraStream *thumb_stream = NULL;
    mm_camera_buf_def_t *main_frame = NULL;
    mm_camera_buf_def_t *thumb_frame = NULL;
    int32_t ret = NO_ERROR;

    ret = queryStreams(&main_stream,
            &thumb_stream,
            &reproc_stream,
            &main_frame,
            &thumb_frame,
            frame,
            reproc_frame);
    if (NO_ERROR != ret) {
        LOGE("Camera streams query from input frames failed %d",
                ret);
        return ret;
    }

    if (NULL != main_stream) {
        ret = main_stream->syncRuntimeParams();
        if (NO_ERROR != ret) {
            LOGE("Syncing of main stream runtime parameters failed %d",
                    ret);
            return ret;
        }
    }

    if (NULL != thumb_stream) {
        ret = thumb_stream->syncRuntimeParams();
        if (NO_ERROR != ret) {
            LOGE("Syncing of thumb stream runtime parameters failed %d",
                    ret);
            return ret;
        }
    }

    if ((NULL != reproc_stream) && (reproc_stream != main_stream)) {
        ret = reproc_stream->syncRuntimeParams();
        if (NO_ERROR != ret) {
            LOGE("Syncing of reproc stream runtime parameters failed %d",
                    ret);
            return ret;
        }
    }

    return ret;
}

/*===========================================================================
 * FUNCTION   : encodeData
 *
 * DESCRIPTION: function to prepare encoding job information and send to
 *              mm-jpeg-interface to do the encoding job
 *
 * PARAMETERS :
 *   @jpeg_job_data : ptr to a struct saving job related information
 *   @needNewSess   : flag to indicate if a new jpeg encoding session need
 *                    to be created. After creation, this flag will be toggled
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraPostProcessor::encodeData(qcamera_jpeg_data_t *jpeg_job_data,
                                         uint8_t &needNewSess)
{
    LOGD("E");
    int32_t ret = NO_ERROR;
    mm_jpeg_job_t jpg_job;
    uint32_t jobId = 0;
    QCameraStream *reproc_stream = NULL;
    QCameraStream *main_stream = NULL;
    mm_camera_buf_def_t *main_frame = NULL;
    QCameraStream *thumb_stream = NULL;
    mm_camera_buf_def_t *thumb_frame = NULL;
    mm_camera_super_buf_t *recvd_frame = jpeg_job_data->src_frame;
    cam_rect_t crop;
    cam_stream_parm_buffer_t param;
    cam_stream_img_prop_t imgProp;

    // find channel
    QCameraChannel *pChannel = m_parent->getChannelByHandle(recvd_frame->ch_id);
    // check reprocess channel if not found
    if (pChannel == NULL) {
        for (int8_t i = 0; i < mPPChannelCount; i++) {
            if ((mPPChannels[i] != NULL) &&
                    (mPPChannels[i]->getMyHandle() == recvd_frame->ch_id)) {
                pChannel = mPPChannels[i];
                break;
            }
        }
    }

    if (pChannel == NULL) {
        LOGE("No corresponding channel (ch_id = %d) exist, return here",
                recvd_frame->ch_id);
        return BAD_VALUE;
    }

    const uint32_t jpeg_rotation = m_parent->mParameters.getJpegRotation();

    ret = queryStreams(&main_stream,
            &thumb_stream,
            &reproc_stream,
            &main_frame,
            &thumb_frame,
            recvd_frame,
            jpeg_job_data->src_reproc_frame);
    if (NO_ERROR != ret) {
        return ret;
    }

    if(NULL == main_frame){
       LOGE("Main frame is NULL");
       return BAD_VALUE;
    }

    if(NULL == thumb_frame){
       LOGD("Thumbnail frame does not exist");
    }

    QCameraMemory *memObj = (QCameraMemory *)main_frame->mem_info;
    if (NULL == memObj) {
        LOGE("Memeory Obj of main frame is NULL");
        return NO_MEMORY;
    }

    // dump snapshot frame if enabled
    m_parent->dumpFrameToFile(main_stream, main_frame,
            QCAMERA_DUMP_FRM_SNAPSHOT, (char *)"CPP");

    // send upperlayer callback for raw image
    camera_memory_t *mem = memObj->getMemory(main_frame->buf_idx, false);
    if (NULL != m_parent->mDataCb &&
        m_parent->msgTypeEnabledWithLock(CAMERA_MSG_RAW_IMAGE) > 0) {
        qcamera_callback_argm_t cbArg;
        memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
        cbArg.cb_type = QCAMERA_DATA_CALLBACK;
        cbArg.msg_type = CAMERA_MSG_RAW_IMAGE;
        cbArg.data = mem;
        cbArg.index = 0;
        m_parent->m_cbNotifier.notifyCallback(cbArg);
    }
    if (NULL != m_parent->mNotifyCb &&
        m_parent->msgTypeEnabledWithLock(CAMERA_MSG_RAW_IMAGE_NOTIFY) > 0) {
        qcamera_callback_argm_t cbArg;
        memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
        cbArg.cb_type = QCAMERA_NOTIFY_CALLBACK;
        cbArg.msg_type = CAMERA_MSG_RAW_IMAGE_NOTIFY;
        cbArg.ext1 = 0;
        cbArg.ext2 = 0;
        m_parent->m_cbNotifier.notifyCallback(cbArg);
    }

    if (mJpegClientHandle <= 0) {
        LOGE("Error: bug here, mJpegClientHandle is 0");
        return UNKNOWN_ERROR;
    }

    if (needNewSess) {
        // create jpeg encoding session
        mm_jpeg_encode_params_t encodeParam;
        memset(&encodeParam, 0, sizeof(mm_jpeg_encode_params_t));
        ret = getJpegEncodingConfig(encodeParam, main_stream, thumb_stream);
        if (ret != NO_ERROR) {
            LOGE("error getting encoding config");
            return ret;
        }
        LOGH("[KPI Perf] : call jpeg create_session");
        ret = mJpegHandle.create_session(mJpegClientHandle, &encodeParam, &mJpegSessionId);
        if (ret != NO_ERROR) {
            LOGE("error creating a new jpeg encoding session");
            return ret;
        }
        needNewSess = FALSE;
    }
    // Fill in new job
    memset(&jpg_job, 0, sizeof(mm_jpeg_job_t));
    jpg_job.job_type = JPEG_JOB_TYPE_ENCODE;
    jpg_job.encode_job.session_id = mJpegSessionId;
    jpg_job.encode_job.src_index = (int32_t)main_frame->buf_idx;
    jpg_job.encode_job.dst_index = 0;

    if (mJpegMemOpt) {
        jpg_job.encode_job.dst_index = jpg_job.encode_job.src_index;
    } else if (mUseJpegBurst) {
        jpg_job.encode_job.dst_index = -1;
    }

    // use src to reproc frame as work buffer; if src buf is not available
    // jpeg interface will allocate work buffer
    if (jpeg_job_data->src_reproc_frame != NULL) {
        int32_t ret = NO_ERROR;
        QCameraStream *main_stream = NULL;
        mm_camera_buf_def_t *main_frame = NULL;
        QCameraStream *thumb_stream = NULL;
        mm_camera_buf_def_t *thumb_frame = NULL;
        QCameraStream *reproc_stream = NULL;
        mm_camera_buf_def_t *workBuf = NULL;
        // Call queryStreams to fetch source of reproc frame
        ret = queryStreams(&main_stream,
                &thumb_stream,
                &reproc_stream,
                &main_frame,
                &thumb_frame,
                jpeg_job_data->src_reproc_frame,
                NULL);

        if ((NO_ERROR == ret) && ((workBuf = main_frame) != NULL)
                && !m_parent->isLowPowerMode()) {
            camera_memory_t *camWorkMem = NULL;
            int workBufIndex = workBuf->buf_idx;
            QCameraMemory *workMem = (QCameraMemory *)workBuf->mem_info;
            if (workMem != NULL) {
                camWorkMem = workMem->getMemory(workBufIndex, false);
            }
            if (camWorkMem != NULL && workMem != NULL) {
                jpg_job.encode_job.work_buf.buf_size = camWorkMem->size;
                jpg_job.encode_job.work_buf.buf_vaddr = (uint8_t *)camWorkMem->data;
                jpg_job.encode_job.work_buf.fd = workMem->getFd(workBufIndex);
                workMem->invalidateCache(workBufIndex);
            }
        }
    }

    cam_dimension_t src_dim;
    memset(&src_dim, 0, sizeof(cam_dimension_t));
    main_stream->getFrameDimension(src_dim);

    bool hdr_output_crop = m_parent->mParameters.isHDROutputCropEnabled();
    bool img_feature_enabled =
            m_parent->mParameters.isUbiFocusEnabled() ||
            m_parent->mParameters.isUbiRefocus() ||
            m_parent->mParameters.isChromaFlashEnabled() ||
            m_parent->mParameters.isOptiZoomEnabled() ||
            m_parent->mParameters.isStillMoreEnabled();

    LOGH("Crop needed %d", img_feature_enabled);
    crop.left = 0;
    crop.top = 0;
    crop.height = src_dim.height;
    crop.width = src_dim.width;

    param = main_stream->getOutputCrop();
    for (int i = 0; i < param.outputCrop.num_of_streams; i++) {
       if (param.outputCrop.crop_info[i].stream_id
           == main_stream->getMyServerID()) {
               crop = param.outputCrop.crop_info[i].crop;
               main_stream->setCropInfo(crop);
       }
    }
    if (img_feature_enabled) {
        memset(&param, 0, sizeof(cam_stream_parm_buffer_t));

        param = main_stream->getImgProp();
        imgProp = param.imgProp;
        main_stream->setCropInfo(imgProp.crop);
        crop = imgProp.crop;
        thumb_stream = NULL; /* use thumbnail from main image */

        if ((reproc_stream != NULL) && (m_DataMem == NULL) &&
                m_parent->mParameters.isUbiRefocus()) {

            QCameraHeapMemory* miscBufHandler = reproc_stream->getMiscBuf();
            cam_misc_buf_t* refocusResult =
                    reinterpret_cast<cam_misc_buf_t *>(miscBufHandler->getPtr(0));
            uint32_t resultSize = refocusResult->header_size +
                    refocusResult->width * refocusResult->height;
            camera_memory_t *dataMem = m_parent->mGetMemory(-1, resultSize,
                    1, m_parent->mCallbackCookie);

            LOGH("Refocus result header %u dims %dx%d",
                    resultSize, refocusResult->width, refocusResult->height);

            if (dataMem && dataMem->data) {
                memcpy(dataMem->data, refocusResult->data, resultSize);
                //save mem pointer for depth map
                m_DataMem = dataMem;
            }
        }
    } else if ((reproc_stream != NULL) && (m_parent->mParameters.isTruePortraitEnabled())) {

        QCameraHeapMemory* miscBufHandler = reproc_stream->getMiscBuf();
        cam_misc_buf_t* tpResult =
                reinterpret_cast<cam_misc_buf_t *>(miscBufHandler->getPtr(0));
        uint32_t tpMetaSize = tpResult->header_size + tpResult->width * tpResult->height;

        LOGH("True portrait result header %d% dims dx%d",
                tpMetaSize, tpResult->width, tpResult->height);

        CAM_DUMP_TO_FILE(QCAMERA_DUMP_FRM_LOCATION"tp", "bm", -1, "y",
                &tpResult->data, tpMetaSize);
    }

    cam_dimension_t dst_dim;

    if (hdr_output_crop && crop.height) {
        dst_dim.height = crop.height;
    } else {
        dst_dim.height = src_dim.height;
    }
    if (hdr_output_crop && crop.width) {
        dst_dim.width = crop.width;
    } else {
        dst_dim.width = src_dim.width;
    }

    // main dim
    jpg_job.encode_job.main_dim.src_dim = src_dim;
    jpg_job.encode_job.main_dim.dst_dim = dst_dim;
    jpg_job.encode_job.main_dim.crop = crop;

    // get 3a sw version info
    cam_q3a_version_t sw_version =
        m_parent->getCamHalCapabilities()->q3a_version;

    // get exif data
    QCameraExif *pJpegExifObj = m_parent->getExifData();
    jpeg_job_data->pJpegExifObj = pJpegExifObj;
    if (pJpegExifObj != NULL) {
        jpg_job.encode_job.exif_info.exif_data = pJpegExifObj->getEntries();
        jpg_job.encode_job.exif_info.numOfEntries =
            pJpegExifObj->getNumOfEntries();
        jpg_job.encode_job.exif_info.debug_data.sw_3a_version[0] =
            sw_version.major_version;
        jpg_job.encode_job.exif_info.debug_data.sw_3a_version[1] =
            sw_version.minor_version;
        jpg_job.encode_job.exif_info.debug_data.sw_3a_version[2] =
            sw_version.patch_version;
        jpg_job.encode_job.exif_info.debug_data.sw_3a_version[3] =
            sw_version.new_feature_des;
    }

    // set rotation only when no online rotation or offline pp rotation is done before
    if (!m_parent->needRotationReprocess()) {
        jpg_job.encode_job.rotation = jpeg_rotation;
    }
    LOGH("jpeg rotation is set to %d", jpg_job.encode_job.rotation);

    // thumbnail dim
    if (m_bThumbnailNeeded == TRUE) {
        m_parent->getThumbnailSize(jpg_job.encode_job.thumb_dim.dst_dim);

        if (thumb_stream == NULL) {
            // need jpeg thumbnail, but no postview/preview stream exists
            // we use the main stream/frame to encode thumbnail
            thumb_stream = main_stream;
            thumb_frame = main_frame;
        }
        if (m_parent->needRotationReprocess() &&
                ((90 == jpeg_rotation) || (270 == jpeg_rotation))) {
            // swap thumbnail dimensions
            cam_dimension_t tmp_dim = jpg_job.encode_job.thumb_dim.dst_dim;
            jpg_job.encode_job.thumb_dim.dst_dim.width = tmp_dim.height;
            jpg_job.encode_job.thumb_dim.dst_dim.height = tmp_dim.width;
        }

        memset(&src_dim, 0, sizeof(cam_dimension_t));
        thumb_stream->getFrameDimension(src_dim);
        jpg_job.encode_job.thumb_dim.src_dim = src_dim;

        // crop is the same if frame is the same
        if (thumb_frame != main_frame) {
            crop.left = 0;
            crop.top = 0;
            crop.height = src_dim.height;
            crop.width = src_dim.width;

            param = thumb_stream->getOutputCrop();
            for (int i = 0; i < param.outputCrop.num_of_streams; i++) {
               if (param.outputCrop.crop_info[i].stream_id
                   == thumb_stream->getMyServerID()) {
                       crop = param.outputCrop.crop_info[i].crop;
                       thumb_stream->setCropInfo(crop);
               }
           }
        }


        jpg_job.encode_job.thumb_dim.crop = crop;
        if (thumb_frame != NULL) {
            jpg_job.encode_job.thumb_index = thumb_frame->buf_idx;
        }
        LOGI("Thumbnail idx = %d src w/h (%dx%d), dst w/h (%dx%d)",
            jpg_job.encode_job.thumb_index,
            jpg_job.encode_job.thumb_dim.src_dim.width,
            jpg_job.encode_job.thumb_dim.src_dim.height,
            jpg_job.encode_job.thumb_dim.dst_dim.width,
            jpg_job.encode_job.thumb_dim.dst_dim.height);
    }

    LOGI("Main image idx = %d src w/h (%dx%d), dst w/h (%dx%d)",
            jpg_job.encode_job.src_index,
            jpg_job.encode_job.main_dim.src_dim.width,
            jpg_job.encode_job.main_dim.src_dim.height,
            jpg_job.encode_job.main_dim.dst_dim.width,
            jpg_job.encode_job.main_dim.dst_dim.height);

    if (thumb_frame != NULL) {
        // dump thumbnail frame if enabled
        m_parent->dumpFrameToFile(thumb_stream, thumb_frame, QCAMERA_DUMP_FRM_THUMBNAIL);
    }

    if (jpeg_job_data->metadata != NULL) {
        // fill in meta data frame ptr
        jpg_job.encode_job.p_metadata = jpeg_job_data->metadata;
    }

    jpg_job.encode_job.hal_version = CAM_HAL_V1;
    m_parent->mExifParams.sensor_params.sens_type = m_parent->getSensorType();
    jpg_job.encode_job.cam_exif_params = m_parent->mExifParams;
    jpg_job.encode_job.cam_exif_params.debug_params =
            (mm_jpeg_debug_exif_params_t *) malloc (sizeof(mm_jpeg_debug_exif_params_t));
    if (!jpg_job.encode_job.cam_exif_params.debug_params) {
        LOGE("Out of Memory. Allocation failed for 3A debug exif params");
        return NO_MEMORY;
    }

    jpg_job.encode_job.mobicat_mask = m_parent->mParameters.getMobicatMask();


    if (NULL != jpg_job.encode_job.p_metadata && (jpg_job.encode_job.mobicat_mask > 0)) {

       if (m_parent->mExifParams.debug_params) {
           memcpy(jpg_job.encode_job.cam_exif_params.debug_params,
                   m_parent->mExifParams.debug_params, (sizeof(mm_jpeg_debug_exif_params_t)));

           /* Save a copy of mobicat params */
           jpg_job.encode_job.p_metadata->is_mobicat_aec_params_valid =
                    jpg_job.encode_job.cam_exif_params.cam_3a_params_valid;

           if (jpg_job.encode_job.cam_exif_params.cam_3a_params_valid) {
                    jpg_job.encode_job.p_metadata->mobicat_aec_params =
                    jpg_job.encode_job.cam_exif_params.cam_3a_params;
           }

           /* Save a copy of 3A debug params */
            jpg_job.encode_job.p_metadata->is_statsdebug_ae_params_valid =
                    jpg_job.encode_job.cam_exif_params.debug_params->ae_debug_params_valid;
            jpg_job.encode_job.p_metadata->is_statsdebug_awb_params_valid =
                    jpg_job.encode_job.cam_exif_params.debug_params->awb_debug_params_valid;
            jpg_job.encode_job.p_metadata->is_statsdebug_af_params_valid =
                    jpg_job.encode_job.cam_exif_params.debug_params->af_debug_params_valid;
            jpg_job.encode_job.p_metadata->is_statsdebug_asd_params_valid =
                    jpg_job.encode_job.cam_exif_params.debug_params->asd_debug_params_valid;
            jpg_job.encode_job.p_metadata->is_statsdebug_stats_params_valid =
                    jpg_job.encode_job.cam_exif_params.debug_params->stats_debug_params_valid;
            jpg_job.encode_job.p_metadata->is_statsdebug_bestats_params_valid =
                    jpg_job.encode_job.cam_exif_params.debug_params->bestats_debug_params_valid;
            jpg_job.encode_job.p_metadata->is_statsdebug_bhist_params_valid =
                    jpg_job.encode_job.cam_exif_params.debug_params->bhist_debug_params_valid;
            jpg_job.encode_job.p_metadata->is_statsdebug_3a_tuning_params_valid =
                    jpg_job.encode_job.cam_exif_params.debug_params->q3a_tuning_debug_params_valid;

            if (jpg_job.encode_job.cam_exif_params.debug_params->ae_debug_params_valid) {
                jpg_job.encode_job.p_metadata->statsdebug_ae_data =
                        jpg_job.encode_job.cam_exif_params.debug_params->ae_debug_params;
            }
            if (jpg_job.encode_job.cam_exif_params.debug_params->awb_debug_params_valid) {
                jpg_job.encode_job.p_metadata->statsdebug_awb_data =
                        jpg_job.encode_job.cam_exif_params.debug_params->awb_debug_params;
            }
            if (jpg_job.encode_job.cam_exif_params.debug_params->af_debug_params_valid) {
                jpg_job.encode_job.p_metadata->statsdebug_af_data =
                        jpg_job.encode_job.cam_exif_params.debug_params->af_debug_params;
            }
            if (jpg_job.encode_job.cam_exif_params.debug_params->asd_debug_params_valid) {
                jpg_job.encode_job.p_metadata->statsdebug_asd_data =
                        jpg_job.encode_job.cam_exif_params.debug_params->asd_debug_params;
            }
            if (jpg_job.encode_job.cam_exif_params.debug_params->stats_debug_params_valid) {
                jpg_job.encode_job.p_metadata->statsdebug_stats_buffer_data =
                        jpg_job.encode_job.cam_exif_params.debug_params->stats_debug_params;
            }
            if (jpg_job.encode_job.cam_exif_params.debug_params->bestats_debug_params_valid) {
                jpg_job.encode_job.p_metadata->statsdebug_bestats_buffer_data =
                        jpg_job.encode_job.cam_exif_params.debug_params->bestats_debug_params;
            }
            if (jpg_job.encode_job.cam_exif_params.debug_params->bhist_debug_params_valid) {
                jpg_job.encode_job.p_metadata->statsdebug_bhist_data =
                        jpg_job.encode_job.cam_exif_params.debug_params->bhist_debug_params;
            }
            if (jpg_job.encode_job.cam_exif_params.debug_params->q3a_tuning_debug_params_valid) {
                jpg_job.encode_job.p_metadata->statsdebug_3a_tuning_data =
                        jpg_job.encode_job.cam_exif_params.debug_params->q3a_tuning_debug_params;
            }
        }

    }

    /* Init the QTable */
    for (int i = 0; i < QTABLE_MAX; i++) {
        jpg_job.encode_job.qtable_set[i] = 0;
    }

    const cam_sync_related_sensors_event_info_t* related_cam_info =
            m_parent->getRelatedCamSyncInfo();
    if (related_cam_info->sync_control == CAM_SYNC_RELATED_SENSORS_ON &&
            m_parent->getMpoComposition()) {
        jpg_job.encode_job.multi_image_info.type = MM_JPEG_TYPE_MPO;
        if (related_cam_info->type == CAM_TYPE_MAIN ) {
            jpg_job.encode_job.multi_image_info.is_primary = TRUE;
            LOGD("Encoding MPO Primary JPEG");
        } else {
            jpg_job.encode_job.multi_image_info.is_primary = FALSE;
            LOGD("Encoding MPO Aux JPEG");
        }
        jpg_job.encode_job.multi_image_info.num_of_images = 2;
    } else {
        LOGD("Encoding Single JPEG");
        jpg_job.encode_job.multi_image_info.type = MM_JPEG_TYPE_JPEG;
        jpg_job.encode_job.multi_image_info.is_primary = FALSE;
        jpg_job.encode_job.multi_image_info.num_of_images = 1;
    }

    LOGI("[KPI Perf] : PROFILE_JPEG_JOB_START");
    ret = mJpegHandle.start_job(&jpg_job, &jobId);
    if (jpg_job.encode_job.cam_exif_params.debug_params) {
        free(jpg_job.encode_job.cam_exif_params.debug_params);
    }
    if (ret == NO_ERROR) {
        // remember job info
        jpeg_job_data->jobId = jobId;
    }

    return ret;
}

/*===========================================================================
 * FUNCTION   : processRawImageImpl
 *
 * DESCRIPTION: function to send raw image to upper layer
 *
 * PARAMETERS :
 *   @recvd_frame   : frame to be encoded
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraPostProcessor::processRawImageImpl(mm_camera_super_buf_t *recvd_frame)
{
    int32_t rc = NO_ERROR;

    QCameraChannel *pChannel = m_parent->getChannelByHandle(recvd_frame->ch_id);
    QCameraStream *pStream = NULL;
    mm_camera_buf_def_t *frame = NULL;
    // check reprocess channel if not found
    if (pChannel == NULL) {
        for (int8_t i = 0; i < mPPChannelCount; i++) {
            if ((mPPChannels[i] != NULL) &&
                    (mPPChannels[i]->getMyHandle() == recvd_frame->ch_id)) {
                pChannel = mPPChannels[i];
                break;
            }
        }
    }
    if (pChannel == NULL) {
        LOGE("No corresponding channel (ch_id = %d) exist, return here",
                recvd_frame->ch_id);
        return BAD_VALUE;
    }

    // find snapshot frame
    for (uint32_t i = 0; i < recvd_frame->num_bufs; i++) {
        QCameraStream *pCurStream =
            pChannel->getStreamByHandle(recvd_frame->bufs[i]->stream_id);
        if (pCurStream != NULL) {
            if (pCurStream->isTypeOf(CAM_STREAM_TYPE_SNAPSHOT) ||
                pCurStream->isTypeOf(CAM_STREAM_TYPE_RAW) ||
                pCurStream->isOrignalTypeOf(CAM_STREAM_TYPE_SNAPSHOT) ||
                pCurStream->isOrignalTypeOf(CAM_STREAM_TYPE_RAW)) {
                pStream = pCurStream;
                frame = recvd_frame->bufs[i];
                break;
            }
        }
    }

    if ( NULL == frame ) {
        LOGE("No valid raw buffer");
        return BAD_VALUE;
    }

    QCameraMemory *rawMemObj = (QCameraMemory *)frame->mem_info;
    bool zslChannelUsed = m_parent->isZSLMode() &&
            ( pChannel != mPPChannels[0] );
    camera_memory_t *raw_mem = NULL;

    if (rawMemObj != NULL) {
        if (zslChannelUsed) {
            raw_mem = rawMemObj->getMemory(frame->buf_idx, false);
        } else {
            raw_mem = m_parent->mGetMemory(-1,
                                           frame->frame_len,
                                           1,
                                           m_parent->mCallbackCookie);
            if (NULL == raw_mem) {
                LOGE("Not enough memory for RAW cb ");
                return NO_MEMORY;
            }
            memcpy(raw_mem->data, frame->buffer, frame->frame_len);
        }
    }

    if (NULL != rawMemObj && NULL != raw_mem) {
        // dump frame into file
        if (frame->stream_type == CAM_STREAM_TYPE_SNAPSHOT ||
            pStream->isOrignalTypeOf(CAM_STREAM_TYPE_SNAPSHOT)) {
            // for YUV422 NV16 case
            m_parent->dumpFrameToFile(pStream, frame, QCAMERA_DUMP_FRM_SNAPSHOT);
        } else {
            //Received RAW snapshot taken notification
            m_parent->dumpFrameToFile(pStream, frame, QCAMERA_DUMP_FRM_RAW);

            if(true == m_parent->m_bIntRawEvtPending) {
              //Sending RAW snapshot taken notification to HAL
              memset(&m_dst_dim, 0, sizeof(m_dst_dim));
              pStream->getFrameDimension(m_dst_dim);
              pthread_mutex_lock(&m_parent->m_int_lock);
              pthread_cond_signal(&m_parent->m_int_cond);
              pthread_mutex_unlock(&m_parent->m_int_lock);
              raw_mem->release(raw_mem);
              return rc;
            }
        }

        // send data callback / notify for RAW_IMAGE
        if (NULL != m_parent->mDataCb &&
            m_parent->msgTypeEnabledWithLock(CAMERA_MSG_RAW_IMAGE) > 0) {
            qcamera_callback_argm_t cbArg;
            memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
            cbArg.cb_type = QCAMERA_DATA_CALLBACK;
            cbArg.msg_type = CAMERA_MSG_RAW_IMAGE;
            cbArg.data = raw_mem;
            cbArg.index = 0;
            m_parent->m_cbNotifier.notifyCallback(cbArg);
        }
        if (NULL != m_parent->mNotifyCb &&
            m_parent->msgTypeEnabledWithLock(CAMERA_MSG_RAW_IMAGE_NOTIFY) > 0) {
            qcamera_callback_argm_t cbArg;
            memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
            cbArg.cb_type = QCAMERA_NOTIFY_CALLBACK;
            cbArg.msg_type = CAMERA_MSG_RAW_IMAGE_NOTIFY;
            cbArg.ext1 = 0;
            cbArg.ext2 = 0;
            m_parent->m_cbNotifier.notifyCallback(cbArg);
        }

        if ((m_parent->mDataCb != NULL) &&
            m_parent->msgTypeEnabledWithLock(CAMERA_MSG_COMPRESSED_IMAGE) > 0) {
            qcamera_release_data_t release_data;
            memset(&release_data, 0, sizeof(qcamera_release_data_t));
            if ( zslChannelUsed ) {
                release_data.frame = recvd_frame;
            } else {
                release_data.data = raw_mem;
            }
            rc = sendDataNotify(CAMERA_MSG_COMPRESSED_IMAGE,
                                raw_mem,
                                0,
                                NULL,
                                &release_data);
        } else {
            raw_mem->release(raw_mem);
        }
    } else {
        LOGE("Cannot get raw mem");
        rc = UNKNOWN_ERROR;
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : dataSaveRoutine
 *
 * DESCRIPTION: data saving routine
 *
 * PARAMETERS :
 *   @data    : user data ptr (QCameraPostProcessor)
 *
 * RETURN     : None
 *==========================================================================*/
void *QCameraPostProcessor::dataSaveRoutine(void *data)
{
    int running = 1;
    int ret;
    uint8_t is_active = FALSE;
    QCameraPostProcessor *pme = (QCameraPostProcessor *)data;
    QCameraCmdThread *cmdThread = &pme->m_saveProcTh;
    cmdThread->setName("CAM_JpegSave");
    char saveName[PROPERTY_VALUE_MAX];

    LOGH("E");
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
        case CAMERA_CMD_TYPE_START_DATA_PROC:
            LOGH("start data proc");
            is_active = TRUE;
            pme->m_inputSaveQ.init();
            break;
        case CAMERA_CMD_TYPE_STOP_DATA_PROC:
            {
                LOGH("stop data proc");
                is_active = FALSE;

                // flush input save Queue
                pme->m_inputSaveQ.flush();

                // signal cmd is completed
                cam_sem_post(&cmdThread->sync_sem);
            }
            break;
        case CAMERA_CMD_TYPE_DO_NEXT_JOB:
            {
                LOGH("Do next job, active is %d", is_active);

                qcamera_jpeg_evt_payload_t *job_data = (qcamera_jpeg_evt_payload_t *) pme->m_inputSaveQ.dequeue();
                if (job_data == NULL) {
                    LOGE("Invalid jpeg event data");
                    continue;
                }
                //qcamera_jpeg_data_t *jpeg_job =
                //        (qcamera_jpeg_data_t *)pme->m_ongoingJpegQ.dequeue(false);
                //uint32_t frame_idx = jpeg_job->src_frame->bufs[0]->frame_idx;
                uint32_t frame_idx = 75;

                pme->m_ongoingJpegQ.flushNodes(matchJobId, (void*)&job_data->jobId);

                LOGH("[KPI Perf] : jpeg job %d", job_data->jobId);

                if (is_active == TRUE) {
                    memset(saveName, '\0', sizeof(saveName));
                    snprintf(saveName,
                             sizeof(saveName),
                             QCameraPostProcessor::STORE_LOCATION,
                             pme->mSaveFrmCnt);

                    int file_fd = open(saveName, O_RDWR | O_CREAT, 0655);
                    if (file_fd >= 0) {
                        ssize_t written_len = write(file_fd, job_data->out_data.buf_vaddr,
                                job_data->out_data.buf_filled_len);
                        if ((ssize_t)job_data->out_data.buf_filled_len != written_len) {
                            LOGE("Failed save complete data %d bytes "
                                  "written instead of %d bytes!",
                                   written_len,
                                  job_data->out_data.buf_filled_len);
                        } else {
                            LOGH("written number of bytes %d\n",
                                 written_len);
                        }

                        close(file_fd);
                    } else {
                        LOGE("fail t open file for saving");
                    }
                    pme->mSaveFrmCnt++;

                    camera_memory_t* jpeg_mem = pme->m_parent->mGetMemory(-1,
                                                         strlen(saveName),
                                                         1,
                                                         pme->m_parent->mCallbackCookie);
                    if (NULL == jpeg_mem) {
                        ret = NO_MEMORY;
                        LOGE("getMemory for jpeg, ret = NO_MEMORY");
                        goto end;
                    }
                    memcpy(jpeg_mem->data, saveName, strlen(saveName));

                    LOGH("Calling upperlayer callback to store JPEG image");
                    qcamera_release_data_t release_data;
                    memset(&release_data, 0, sizeof(qcamera_release_data_t));
                    release_data.data = jpeg_mem;
                    release_data.unlinkFile = true;
                    LOGI("[KPI Perf]: PROFILE_JPEG_CB ");
                    ret = pme->sendDataNotify(CAMERA_MSG_COMPRESSED_IMAGE,
                            jpeg_mem,
                            0,
                            NULL,
                            &release_data,
                            frame_idx);
                }

end:
                free(job_data);
            }
            break;
        case CAMERA_CMD_TYPE_EXIT:
            LOGH("save thread exit");
            running = 0;
            break;
        default:
            break;
        }
    } while (running);
    LOGH("X");
    return NULL;
}

/*===========================================================================
 * FUNCTION   : dataProcessRoutine
 *
 * DESCRIPTION: data process routine that handles input data either from input
 *              Jpeg Queue to do jpeg encoding, or from input PP Queue to do
 *              reprocess.
 *
 * PARAMETERS :
 *   @data    : user data ptr (QCameraPostProcessor)
 *
 * RETURN     : None
 *==========================================================================*/
void *QCameraPostProcessor::dataProcessRoutine(void *data)
{
    int running = 1;
    int ret;
    uint8_t is_active = FALSE;
    QCameraPostProcessor *pme = (QCameraPostProcessor *)data;
    QCameraCmdThread *cmdThread = &pme->m_dataProcTh;
    cmdThread->setName("CAM_DataProc");

    LOGH("E");
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
        case CAMERA_CMD_TYPE_START_DATA_PROC:
            LOGH("start data proc");
            is_active = TRUE;

            pme->m_ongoingPPQ.init();
            pme->m_inputJpegQ.init();
            pme->m_inputPPQ.init();
            pme->m_inputRawQ.init();

            pme->m_saveProcTh.sendCmd(CAMERA_CMD_TYPE_START_DATA_PROC,
                                      FALSE,
                                      FALSE);

            // signal cmd is completed
            cam_sem_post(&cmdThread->sync_sem);

            break;
        case CAMERA_CMD_TYPE_STOP_DATA_PROC:
            {
                LOGH("stop data proc");
                is_active = FALSE;

                pme->m_saveProcTh.sendCmd(CAMERA_CMD_TYPE_STOP_DATA_PROC,
                                           TRUE,
                                           TRUE);
                // cancel all ongoing jpeg jobs
                qcamera_jpeg_data_t *jpeg_job =
                    (qcamera_jpeg_data_t *)pme->m_ongoingJpegQ.dequeue();
                while (jpeg_job != NULL) {
                    pme->mJpegHandle.abort_job(jpeg_job->jobId);

                    pme->releaseJpegJobData(jpeg_job);
                    free(jpeg_job);

                    jpeg_job = (qcamera_jpeg_data_t *)pme->m_ongoingJpegQ.dequeue();
                }

                // destroy jpeg encoding session
                if ( 0 < pme->mJpegSessionId ) {
                    pme->mJpegHandle.destroy_session(pme->mJpegSessionId);
                    pme->mJpegSessionId = 0;
                }

                // free jpeg out buf and exif obj
                FREE_JPEG_OUTPUT_BUFFER(pme->m_pJpegOutputMem,
                    pme->m_JpegOutputMemCount);

                if (pme->m_pJpegExifObj != NULL) {
                    delete pme->m_pJpegExifObj;
                    pme->m_pJpegExifObj = NULL;
                }

                // flush ongoing postproc Queue
                pme->m_ongoingPPQ.flush();

                // flush input jpeg Queue
                pme->m_inputJpegQ.flush();

                // flush input Postproc Queue
                pme->m_inputPPQ.flush();

                // flush input raw Queue
                pme->m_inputRawQ.flush();

                // signal cmd is completed
                cam_sem_post(&cmdThread->sync_sem);

                pme->mNewJpegSessionNeeded = true;
            }
            break;
        case CAMERA_CMD_TYPE_DO_NEXT_JOB:
            {
                LOGH("Do next job, active is %d", is_active);
                if (is_active == TRUE) {
                    qcamera_jpeg_data_t *jpeg_job =
                        (qcamera_jpeg_data_t *)pme->m_inputJpegQ.dequeue();

                    if (NULL != jpeg_job) {
                        // To avoid any race conditions,
                        // sync any stream specific parameters here.
                        if (pme->m_parent->mParameters.isAdvCamFeaturesEnabled()) {
                            // Sync stream params, only if advanced features configured
                            // Reduces the latency for normal snapshot.
                            pme->syncStreamParams(jpeg_job->src_frame, NULL);
                        }

                        // add into ongoing jpeg job Q
                        if (pme->m_ongoingJpegQ.enqueue((void *)jpeg_job)) {
                            ret = pme->encodeData(jpeg_job,
                                      pme->mNewJpegSessionNeeded);
                            if (NO_ERROR != ret) {
                                // dequeue the last one
                                pme->m_ongoingJpegQ.dequeue(false);
                                pme->releaseJpegJobData(jpeg_job);
                                free(jpeg_job);
                                jpeg_job = NULL;
                                pme->sendEvtNotify(CAMERA_MSG_ERROR, UNKNOWN_ERROR, 0);
                            }
                        } else {
                            LOGW("m_ongoingJpegQ is not active!!!");
                            pme->releaseJpegJobData(jpeg_job);
                            free(jpeg_job);
                            jpeg_job = NULL;
                        }
                    }


                    // process raw data if any
                    mm_camera_super_buf_t *super_buf =
                        (mm_camera_super_buf_t *)pme->m_inputRawQ.dequeue();

                    if (NULL != super_buf) {
                        //play shutter sound
                        pme->m_parent->playShutter();
                        ret = pme->processRawImageImpl(super_buf);
                        if (NO_ERROR != ret) {
                            pme->releaseSuperBuf(super_buf);
                            free(super_buf);
                            pme->sendEvtNotify(CAMERA_MSG_ERROR, UNKNOWN_ERROR, 0);
                        }
                    }

                    ret = pme->doReprocess();
                    if (NO_ERROR != ret) {
                        pme->sendEvtNotify(CAMERA_MSG_ERROR, UNKNOWN_ERROR, 0);
                    } else {
                        ret = pme->stopCapture();
                    }

                } else {
                    // not active, simply return buf and do no op
                    qcamera_jpeg_data_t *jpeg_data =
                        (qcamera_jpeg_data_t *)pme->m_inputJpegQ.dequeue();
                    if (NULL != jpeg_data) {
                        pme->releaseJpegJobData(jpeg_data);
                        free(jpeg_data);
                    }
                    mm_camera_super_buf_t *super_buf =
                        (mm_camera_super_buf_t *)pme->m_inputRawQ.dequeue();
                    if (NULL != super_buf) {
                        pme->releaseSuperBuf(super_buf);
                        free(super_buf);
                    }

                    // flush input Postproc Queue
                    pme->m_inputPPQ.flush();
                }
            }
            break;
        case CAMERA_CMD_TYPE_EXIT:
            running = 0;
            break;
        default:
            break;
        }
    } while (running);
    LOGH("X");
    return NULL;
}

/*===========================================================================
 * FUNCTION   : doReprocess
 *
 * DESCRIPTION: Trigger channel reprocessing
 *
 * PARAMETERS :None
 *
 * RETURN     : int32_t type of status
 *                    NO_ERROR  -- success
 *                    none-zero failure code
 *==========================================================================*/
int32_t QCameraPostProcessor::doReprocess()
{
    int32_t ret = NO_ERROR;
    QCameraChannel *m_pSrcChannel = NULL;
    QCameraStream *pMetaStream = NULL;
    uint8_t meta_buf_index = 0;
    mm_camera_buf_def_t *meta_buf = NULL;
    mm_camera_super_buf_t *ppInputFrame = NULL;

    qcamera_pp_data_t *ppreq_job = (qcamera_pp_data_t *)m_inputPPQ.peek();
    if ((ppreq_job == NULL) || (ppreq_job->src_frame == NULL)) {
        return ret;
    }

    if (!validatePostProcess(ppreq_job->src_frame)) {
        return ret;
    }

    ppreq_job = (qcamera_pp_data_t *)m_inputPPQ.dequeue();
    if (ppreq_job == NULL || ppreq_job->src_frame == NULL ||
            ppreq_job->src_reproc_frame == NULL) {
        return ret;
    }

    mm_camera_super_buf_t *src_frame = ppreq_job->src_frame;
    mm_camera_super_buf_t *src_reproc_frame = ppreq_job->src_reproc_frame;
    int8_t mCurReprocCount = ppreq_job->reprocCount;
    int8_t mCurChannelIdx = ppreq_job->ppChannelIndex;

    LOGD("frame = %p src_frame = %p mCurReprocCount = %d mCurChannelIdx = %d",
            src_frame,src_reproc_frame,mCurReprocCount, mCurChannelIdx);

    if ((m_parent->mParameters.getManualCaptureMode() >=
            CAM_MANUAL_CAPTURE_TYPE_3)  && (mCurChannelIdx == 0)) {
        ppInputFrame = src_reproc_frame;
    } else {
        ppInputFrame = src_frame;
    }

    if (mPPChannelCount >= CAM_PP_CHANNEL_MAX) {
        LOGE("invalid channel count");
        return UNKNOWN_ERROR;
    }

    // find meta data stream and index of meta data frame in the superbuf
    for (int8_t j = 0; j < mPPChannelCount; j++) {
        /*First search in src buffer for any offline metadata */
        for (uint32_t i = 0; i < src_frame->num_bufs; i++) {
            QCameraStream *pStream = mPPChannels[j]->getStreamByHandle(
                    src_frame->bufs[i]->stream_id);
            if (pStream != NULL && pStream->isOrignalTypeOf(CAM_STREAM_TYPE_METADATA)) {
                meta_buf_index = (uint8_t) src_frame->bufs[i]->buf_idx;
                pMetaStream = pStream;
                meta_buf = src_frame->bufs[i];
                break;
            }
        }

        if ((pMetaStream != NULL) && (meta_buf != NULL)) {
            LOGD("Found Offline stream metadata = %d",
                    (int)meta_buf_index);
            break;
        }
    }

    if ((pMetaStream == NULL) && (meta_buf == NULL)) {
        for (int8_t j = 0; j < mPPChannelCount; j++) {
            m_pSrcChannel = mPPChannels[j]->getSrcChannel();
            if (m_pSrcChannel == NULL)
                continue;
            for (uint32_t i = 0; i < src_reproc_frame->num_bufs; i++) {
                QCameraStream *pStream =
                        m_pSrcChannel->getStreamByHandle(
                        src_reproc_frame->bufs[i]->stream_id);
                if (pStream != NULL && pStream->isTypeOf(CAM_STREAM_TYPE_METADATA)) {
                    meta_buf_index = (uint8_t) src_reproc_frame->bufs[i]->buf_idx;
                    pMetaStream = pStream;
                    meta_buf = src_reproc_frame->bufs[i];
                    break;
                }
            }
            if ((pMetaStream != NULL) && (meta_buf != NULL)) {
                LOGD("Found Meta data info for reprocessing index = %d",
                        (int)meta_buf_index);
                break;
            }
        }
    }

    if (m_parent->mParameters.isAdvCamFeaturesEnabled()) {
        // No need to sync stream params, if none of the advanced features configured
        // Reduces the latency for normal snapshot.
        syncStreamParams(src_frame, src_reproc_frame);
    }
    if (mPPChannels[mCurChannelIdx] != NULL) {
        // add into ongoing PP job Q
        ppreq_job->reprocCount = (int8_t) (mCurReprocCount + 1);

        if ((m_parent->isRegularCapture()) || (ppreq_job->offline_buffer)) {
            m_bufCountPPQ++;
            if (m_ongoingPPQ.enqueue((void *)ppreq_job)) {
                pthread_mutex_lock(&m_reprocess_lock);
                ret = mPPChannels[mCurChannelIdx]->doReprocessOffline(ppInputFrame,
                        meta_buf, m_parent->mParameters);
                if (ret != NO_ERROR) {
                    m_ongoingPPQ.dequeue(false);
                    pthread_mutex_unlock(&m_reprocess_lock);
                    goto end;
                }

                if ((ppreq_job->offline_buffer) &&
                        (ppreq_job->offline_reproc_buf)) {
                    ret = mPPChannels[mCurChannelIdx]->doReprocessOffline(
                            ppreq_job->offline_reproc_buf, meta_buf);
                    if (ret != NO_ERROR) {
                        m_ongoingPPQ.dequeue(false);
                    }
                }
                pthread_mutex_unlock(&m_reprocess_lock);
            } else {
                LOGW("m_ongoingPPQ is not active!!!");
                ret = UNKNOWN_ERROR;
                goto end;
            }
        } else {
            m_bufCountPPQ++;
            if (!m_ongoingPPQ.enqueue((void *)ppreq_job)) {
                LOGW("m_ongoingJpegQ is not active!!!");
                ret = UNKNOWN_ERROR;
                goto end;
            }

            int32_t numRequiredPPQBufsForSingleOutput = (int32_t)
                    m_parent->mParameters.getNumberInBufsForSingleShot();

            if (m_bufCountPPQ % numRequiredPPQBufsForSingleOutput == 0) {
                int32_t extra_pp_job_count =
                        m_parent->mParameters.getNumberOutBufsForSingleShot() -
                        m_parent->mParameters.getNumberInBufsForSingleShot();

                for (int32_t i = 0; i < extra_pp_job_count; i++) {
                    qcamera_pp_data_t *extra_pp_job =
                            (qcamera_pp_data_t *)calloc(1, sizeof(qcamera_pp_data_t));
                    if (!extra_pp_job) {
                        LOGE("no mem for qcamera_pp_data_t");
                        ret = NO_MEMORY;
                        break;
                    }
                    extra_pp_job->reprocCount = ppreq_job->reprocCount;
                    if (!m_ongoingPPQ.enqueue((void *)extra_pp_job)) {
                        LOGW("m_ongoingJpegQ is not active!!!");
                        releaseOngoingPPData(extra_pp_job, this);
                        free(extra_pp_job);
                        extra_pp_job = NULL;
                        goto end;
                    }
                }
            }

            ret = mPPChannels[mCurChannelIdx]->doReprocess(ppInputFrame,
                    m_parent->mParameters, pMetaStream, meta_buf_index);
            if (ret != NO_ERROR) {
                m_ongoingPPQ.dequeue(false);
            }
        }
    } else {
        LOGE("Reprocess channel is NULL");
        ret = UNKNOWN_ERROR;
    }

end:
    if (ret != NO_ERROR) {
        releaseOngoingPPData(ppreq_job, this);
        if (ppreq_job != NULL) {
            free(ppreq_job);
            ppreq_job = NULL;
        }
    }
    return ret;
}

/*===========================================================================
 * FUNCTION   : getReprocChannel
 *
 * DESCRIPTION:  Returns reprocessing channel handle
 *
 * PARAMETERS : index for reprocessing array
 *
 * RETURN     : QCameraReprocessChannel * type of pointer
                       NULL if no reprocessing channel
 *==========================================================================*/
QCameraReprocessChannel * QCameraPostProcessor::getReprocChannel(uint8_t index)
{
    if (index >= mPPChannelCount) {
        LOGE("Invalid index value");
        return NULL;
    }
    return mPPChannels[index];
}

/*===========================================================================
 * FUNCTION   : stopCapture
 *
 * DESCRIPTION: Trigger image capture stop
 *
 * PARAMETERS :
 * None
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraPostProcessor::stopCapture()
{
     int rc = NO_ERROR;

     if (m_parent->isRegularCapture()) {
        rc = m_parent->processAPI(
                        QCAMERA_SM_EVT_STOP_CAPTURE_CHANNEL,
                        NULL);
     }

     return rc;
}

/*===========================================================================
 * FUNCTION   : getJpegPaddingReq
 *
 * DESCRIPTION: function to add an entry to exif data
 *
 * PARAMETERS :
 *   @padding_info : jpeg specific padding requirement
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraPostProcessor::getJpegPaddingReq(cam_padding_info_t &padding_info)
{
    // TODO: hardcode for now, needs to query from mm-jpeg-interface
    padding_info.width_padding  = CAM_PAD_NONE;
    padding_info.height_padding  = CAM_PAD_TO_16;
    padding_info.plane_padding  = CAM_PAD_TO_WORD;
    padding_info.offset_info.offset_x = 0;
    padding_info.offset_info.offset_y = 0;
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : setYUVFrameInfo
 *
 * DESCRIPTION: set Raw YUV frame data info for up-layer
 *
 * PARAMETERS :
 *   @frame   : process frame received from mm-camera-interface
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *
 * NOTE       : currently we return frame len, y offset, cbcr offset and frame format
 *==========================================================================*/
int32_t QCameraPostProcessor::setYUVFrameInfo(mm_camera_super_buf_t *recvd_frame)
{
    QCameraChannel *pChannel = m_parent->getChannelByHandle(recvd_frame->ch_id);
    // check reprocess channel if not found
    if (pChannel == NULL) {
        for (int8_t i = 0; i < mPPChannelCount; i++) {
            if ((mPPChannels[i] != NULL) &&
                    (mPPChannels[i]->getMyHandle() == recvd_frame->ch_id)) {
                pChannel = mPPChannels[i];
                break;
            }
        }
    }

    if (pChannel == NULL) {
        LOGE("No corresponding channel (ch_id = %d) exist, return here",
                recvd_frame->ch_id);
        return BAD_VALUE;
    }

    // find snapshot frame
    for (uint32_t i = 0; i < recvd_frame->num_bufs; i++) {
        QCameraStream *pStream =
            pChannel->getStreamByHandle(recvd_frame->bufs[i]->stream_id);
        if (pStream != NULL) {
            if (pStream->isTypeOf(CAM_STREAM_TYPE_SNAPSHOT) ||
                pStream->isOrignalTypeOf(CAM_STREAM_TYPE_SNAPSHOT)) {
                //get the main frame, use stream info
                cam_frame_len_offset_t frame_offset;
                cam_dimension_t frame_dim;
                cam_format_t frame_fmt;
                const char *fmt_string;
                pStream->getFrameDimension(frame_dim);
                pStream->getFrameOffset(frame_offset);
                pStream->getFormat(frame_fmt);
                fmt_string = m_parent->mParameters.getFrameFmtString(frame_fmt);

                int cbcr_offset = (int32_t)frame_offset.mp[0].len -
                        frame_dim.width * frame_dim.height;

                LOGH("frame width=%d, height=%d, yoff=%d, cbcroff=%d, fmt_string=%s",
                        frame_dim.width, frame_dim.height, frame_offset.mp[0].offset, cbcr_offset, fmt_string);
                return NO_ERROR;
            }
        }
    }

    return BAD_VALUE;
}

bool QCameraPostProcessor::matchJobId(void *data, void *, void *match_data)
{
  qcamera_jpeg_data_t * job = (qcamera_jpeg_data_t *) data;
  uint32_t job_id = *((uint32_t *) match_data);
  return job->jobId == job_id;
}

/*===========================================================================
 * FUNCTION   : getJpegMemory
 *
 * DESCRIPTION: buffer allocation function
 *   to pass to jpeg interface
 *
 * PARAMETERS :
 *   @out_buf : buffer descriptor struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCameraPostProcessor::getJpegMemory(omx_jpeg_ouput_buf_t *out_buf)
{
    LOGH("Allocating jpeg out buffer of size: %d", out_buf->size);
    QCameraPostProcessor *procInst = (QCameraPostProcessor *) out_buf->handle;
    camera_memory_t *cam_mem = procInst->m_parent->mGetMemory(out_buf->fd, out_buf->size, 1U,
            procInst->m_parent->mCallbackCookie);
    out_buf->mem_hdl = cam_mem;
    out_buf->vaddr = cam_mem->data;

    return 0;
}

/*===========================================================================
 * FUNCTION   : releaseJpegMemory
 *
 * DESCRIPTION: release jpeg memory function
 *   to pass to jpeg interface, in case of abort
 *
 * PARAMETERS :
 *   @out_buf : buffer descriptor struct
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int QCameraPostProcessor::releaseJpegMemory(omx_jpeg_ouput_buf_t *out_buf)
{
    if (out_buf && out_buf->mem_hdl) {
      LOGD("releasing jpeg out buffer of size: %d", out_buf->size);
      camera_memory_t *cam_mem = (camera_memory_t*)out_buf->mem_hdl;
      cam_mem->release(cam_mem);
      out_buf->mem_hdl = NULL;
      out_buf->vaddr = NULL;
      return NO_ERROR;
    }
    return -1;
}

/*===========================================================================
 * FUNCTION   : QCameraExif
 *
 * DESCRIPTION: constructor of QCameraExif
 *
 * PARAMETERS : None
 *
 * RETURN     : None
 *==========================================================================*/
QCameraExif::QCameraExif()
    : m_nNumEntries(0)
{
    memset(m_Entries, 0, sizeof(m_Entries));
}

/*===========================================================================
 * FUNCTION   : ~QCameraExif
 *
 * DESCRIPTION: deconstructor of QCameraExif. Will release internal memory ptr.
 *
 * PARAMETERS : None
 *
 * RETURN     : None
 *==========================================================================*/
QCameraExif::~QCameraExif()
{
    for (uint32_t i = 0; i < m_nNumEntries; i++) {
        switch (m_Entries[i].tag_entry.type) {
        case EXIF_BYTE:
            {
                if (m_Entries[i].tag_entry.count > 1 &&
                    m_Entries[i].tag_entry.data._bytes != NULL) {
                    free(m_Entries[i].tag_entry.data._bytes);
                    m_Entries[i].tag_entry.data._bytes = NULL;
                }
            }
            break;
        case EXIF_ASCII:
            {
                if (m_Entries[i].tag_entry.data._ascii != NULL) {
                    free(m_Entries[i].tag_entry.data._ascii);
                    m_Entries[i].tag_entry.data._ascii = NULL;
                }
            }
            break;
        case EXIF_SHORT:
            {
                if (m_Entries[i].tag_entry.count > 1 &&
                    m_Entries[i].tag_entry.data._shorts != NULL) {
                    free(m_Entries[i].tag_entry.data._shorts);
                    m_Entries[i].tag_entry.data._shorts = NULL;
                }
            }
            break;
        case EXIF_LONG:
            {
                if (m_Entries[i].tag_entry.count > 1 &&
                    m_Entries[i].tag_entry.data._longs != NULL) {
                    free(m_Entries[i].tag_entry.data._longs);
                    m_Entries[i].tag_entry.data._longs = NULL;
                }
            }
            break;
        case EXIF_RATIONAL:
            {
                if (m_Entries[i].tag_entry.count > 1 &&
                    m_Entries[i].tag_entry.data._rats != NULL) {
                    free(m_Entries[i].tag_entry.data._rats);
                    m_Entries[i].tag_entry.data._rats = NULL;
                }
            }
            break;
        case EXIF_UNDEFINED:
            {
                if (m_Entries[i].tag_entry.data._undefined != NULL) {
                    free(m_Entries[i].tag_entry.data._undefined);
                    m_Entries[i].tag_entry.data._undefined = NULL;
                }
            }
            break;
        case EXIF_SLONG:
            {
                if (m_Entries[i].tag_entry.count > 1 &&
                    m_Entries[i].tag_entry.data._slongs != NULL) {
                    free(m_Entries[i].tag_entry.data._slongs);
                    m_Entries[i].tag_entry.data._slongs = NULL;
                }
            }
            break;
        case EXIF_SRATIONAL:
            {
                if (m_Entries[i].tag_entry.count > 1 &&
                    m_Entries[i].tag_entry.data._srats != NULL) {
                    free(m_Entries[i].tag_entry.data._srats);
                    m_Entries[i].tag_entry.data._srats = NULL;
                }
            }
            break;
        }
    }
}

/*===========================================================================
 * FUNCTION   : addEntry
 *
 * DESCRIPTION: function to add an entry to exif data
 *
 * PARAMETERS :
 *   @tagid   : exif tag ID
 *   @type    : data type
 *   @count   : number of data in uint of its type
 *   @data    : input data ptr
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraExif::addEntry(exif_tag_id_t tagid,
                              exif_tag_type_t type,
                              uint32_t count,
                              void *data)
{
    int32_t rc = NO_ERROR;
    if(m_nNumEntries >= MAX_EXIF_TABLE_ENTRIES) {
        LOGE("Number of entries exceeded limit");
        return NO_MEMORY;
    }

    m_Entries[m_nNumEntries].tag_id = tagid;
    m_Entries[m_nNumEntries].tag_entry.type = type;
    m_Entries[m_nNumEntries].tag_entry.count = count;
    m_Entries[m_nNumEntries].tag_entry.copy = 1;
    switch (type) {
    case EXIF_BYTE:
        {
            if (count > 1) {
                uint8_t *values = (uint8_t *)malloc(count);
                if (values == NULL) {
                    LOGE("No memory for byte array");
                    rc = NO_MEMORY;
                } else {
                    memcpy(values, data, count);
                    m_Entries[m_nNumEntries].tag_entry.data._bytes = values;
                }
            } else {
                m_Entries[m_nNumEntries].tag_entry.data._byte = *(uint8_t *)data;
            }
        }
        break;
    case EXIF_ASCII:
        {
            char *str = NULL;
            str = (char *)malloc(count + 1);
            if (str == NULL) {
                LOGE("No memory for ascii string");
                rc = NO_MEMORY;
            } else {
                memset(str, 0, count + 1);
                memcpy(str, data, count);
                m_Entries[m_nNumEntries].tag_entry.data._ascii = str;
            }
        }
        break;
    case EXIF_SHORT:
        {
            uint16_t *exif_data = (uint16_t *)data;
            if (count > 1) {
                uint16_t *values = (uint16_t *)malloc(count * sizeof(uint16_t));
                if (values == NULL) {
                    LOGE("No memory for short array");
                    rc = NO_MEMORY;
                } else {
                    memcpy(values, exif_data, count * sizeof(uint16_t));
                    m_Entries[m_nNumEntries].tag_entry.data._shorts = values;
                }
            } else {
                m_Entries[m_nNumEntries].tag_entry.data._short = *(uint16_t *)data;
            }
        }
        break;
    case EXIF_LONG:
        {
            uint32_t *exif_data = (uint32_t *)data;
            if (count > 1) {
                uint32_t *values = (uint32_t *)malloc(count * sizeof(uint32_t));
                if (values == NULL) {
                    LOGE("No memory for long array");
                    rc = NO_MEMORY;
                } else {
                    memcpy(values, exif_data, count * sizeof(uint32_t));
                    m_Entries[m_nNumEntries].tag_entry.data._longs = values;
                }
            } else {
                m_Entries[m_nNumEntries].tag_entry.data._long = *(uint32_t *)data;
            }
        }
        break;
    case EXIF_RATIONAL:
        {
            rat_t *exif_data = (rat_t *)data;
            if (count > 1) {
                rat_t *values = (rat_t *)malloc(count * sizeof(rat_t));
                if (values == NULL) {
                    LOGE("No memory for rational array");
                    rc = NO_MEMORY;
                } else {
                    memcpy(values, exif_data, count * sizeof(rat_t));
                    m_Entries[m_nNumEntries].tag_entry.data._rats = values;
                }
            } else {
                m_Entries[m_nNumEntries].tag_entry.data._rat = *(rat_t *)data;
            }
        }
        break;
    case EXIF_UNDEFINED:
        {
            uint8_t *values = (uint8_t *)malloc(count);
            if (values == NULL) {
                LOGE("No memory for undefined array");
                rc = NO_MEMORY;
            } else {
                memcpy(values, data, count);
                m_Entries[m_nNumEntries].tag_entry.data._undefined = values;
            }
        }
        break;
    case EXIF_SLONG:
        {
            uint32_t *exif_data = (uint32_t *)data;
            if (count > 1) {
                int32_t *values = (int32_t *)malloc(count * sizeof(int32_t));
                if (values == NULL) {
                    LOGE("No memory for signed long array");
                    rc = NO_MEMORY;
                } else {
                    memcpy(values, exif_data, count * sizeof(int32_t));
                    m_Entries[m_nNumEntries].tag_entry.data._slongs = values;
                }
            } else {
                m_Entries[m_nNumEntries].tag_entry.data._slong = *(int32_t *)data;
            }
        }
        break;
    case EXIF_SRATIONAL:
        {
            srat_t *exif_data = (srat_t *)data;
            if (count > 1) {
                srat_t *values = (srat_t *)malloc(count * sizeof(srat_t));
                if (values == NULL) {
                    LOGE("No memory for signed rational array");
                    rc = NO_MEMORY;
                } else {
                    memcpy(values, exif_data, count * sizeof(srat_t));
                    m_Entries[m_nNumEntries].tag_entry.data._srats = values;
                }
            } else {
                m_Entries[m_nNumEntries].tag_entry.data._srat = *(srat_t *)data;
            }
        }
        break;
    }

    // Increase number of entries
    m_nNumEntries++;
    return rc;
}

}; // namespace qcamera
