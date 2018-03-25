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

#define LOG_TAG "QCamera3PostProc"

// To remove
#include <cutils/properties.h>

// System dependencies
#include <stdio.h>

// Camera dependencies
#include "QCamera3Channel.h"
#include "QCamera3HWI.h"
#include "QCamera3PostProc.h"
#include "QCamera3Stream.h"
#include "QCameraTrace.h"

extern "C" {
#include "mm_camera_dbg.h"
}

#define ENABLE_MODEL_INFO_EXIF

namespace qcamera {

static const char ExifAsciiPrefix[] =
    { 0x41, 0x53, 0x43, 0x49, 0x49, 0x0, 0x0, 0x0 };          // "ASCII\0\0\0"

__unused
static const char ExifUndefinedPrefix[] =
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };   // "\0\0\0\0\0\0\0\0"

#define EXIF_ASCII_PREFIX_SIZE           8   //(sizeof(ExifAsciiPrefix))
#define FOCAL_LENGTH_DECIMAL_PRECISION   1000

/*===========================================================================
 * FUNCTION   : QCamera3PostProcessor
 *
 * DESCRIPTION: constructor of QCamera3PostProcessor.
 *
 * PARAMETERS :
 *   @cam_ctrl : ptr to HWI object
 *
 * RETURN     : None
 *==========================================================================*/
QCamera3PostProcessor::QCamera3PostProcessor(QCamera3ProcessingChannel* ch_ctrl)
    : m_parent(ch_ctrl),
      mJpegCB(NULL),
      mJpegUserData(NULL),
      mJpegClientHandle(0),
      mJpegSessionId(0),
      m_bThumbnailNeeded(TRUE),
      m_pReprocChannel(NULL),
      m_inputPPQ(releasePPInputData, this),
      m_inputFWKPPQ(NULL, this),
      m_ongoingPPQ(releaseOngoingPPData, this),
      m_inputJpegQ(releaseJpegData, this),
      m_ongoingJpegQ(releaseJpegData, this),
      m_inputMetaQ(releaseMetadata, this),
      m_jpegSettingsQ(NULL, this)
{
    memset(&mJpegHandle, 0, sizeof(mJpegHandle));
    memset(&mJpegMetadata, 0, sizeof(mJpegMetadata));
    pthread_mutex_init(&mReprocJobLock, NULL);
}

/*===========================================================================
 * FUNCTION   : ~QCamera3PostProcessor
 *
 * DESCRIPTION: deconstructor of QCamera3PostProcessor.
 *
 * PARAMETERS : None
 *
 * RETURN     : None
 *==========================================================================*/
QCamera3PostProcessor::~QCamera3PostProcessor()
{
    pthread_mutex_destroy(&mReprocJobLock);
}

/*===========================================================================
 * FUNCTION   : init
 *
 * DESCRIPTION: initialization of postprocessor
 *
 * PARAMETERS :
 *   @memory              : output buffer memory
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3PostProcessor::init(QCamera3StreamMem *memory)
{
    ATRACE_CALL();
    mOutputMem = memory;
    m_dataProcTh.launch(dataProcessRoutine, this);

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
int32_t QCamera3PostProcessor::deinit()
{
    int rc = NO_ERROR;
    m_dataProcTh.exit();

    if (m_pReprocChannel != NULL) {
        m_pReprocChannel->stop();
        delete m_pReprocChannel;
        m_pReprocChannel = NULL;
    }

    if(mJpegClientHandle > 0) {
        rc = mJpegHandle.close(mJpegClientHandle);
        LOGH("Jpeg closed, rc = %d, mJpegClientHandle = %x",
               rc, mJpegClientHandle);
        mJpegClientHandle = 0;
        memset(&mJpegHandle, 0, sizeof(mJpegHandle));
    }

    mOutputMem = NULL;
    return rc;
}

/*===========================================================================
 * FUNCTION   : initJpeg
 *
 * DESCRIPTION: initialization of jpeg through postprocessor
 *
 * PARAMETERS :
 *   @jpeg_cb      : callback to handle jpeg event from mm-camera-interface
 *   @max_pic_dim  : max picture dimensions
 *   @user_data    : user data ptr for jpeg callback
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3PostProcessor::initJpeg(jpeg_encode_callback_t jpeg_cb,
        cam_dimension_t* max_pic_dim,
        void *user_data)
{
    ATRACE_CALL();
    mJpegCB = jpeg_cb;
    mJpegUserData = user_data;
    mm_dimension max_size;

    if ((0 > max_pic_dim->width) || (0 > max_pic_dim->height)) {
        LOGE("Negative dimension %dx%d",
                max_pic_dim->width, max_pic_dim->height);
        return BAD_VALUE;
    }

    // set max pic size
    memset(&max_size, 0, sizeof(mm_dimension));
    max_size.w =  max_pic_dim->width;
    max_size.h =  max_pic_dim->height;

    // Pass OTP calibration data to JPEG
    QCamera3HardwareInterface* hal_obj = (QCamera3HardwareInterface*)m_parent->mUserData;
    mJpegMetadata.default_sensor_flip = FLIP_NONE;
    mJpegMetadata.sensor_mount_angle = hal_obj->getSensorMountAngle();
    memcpy(&mJpegMetadata.otp_calibration_data,
            hal_obj->getRelatedCalibrationData(),
            sizeof(cam_related_system_calibration_data_t));
    mJpegClientHandle = jpeg_open(&mJpegHandle, NULL, max_size, &mJpegMetadata);

    if (!mJpegClientHandle) {
        LOGE("jpeg_open did not work");
        return UNKNOWN_ERROR;
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
 *   @config        : reprocess configuration
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *
 * NOTE       : if any reprocess is needed, a reprocess channel/stream
 *              will be started.
 *==========================================================================*/
int32_t QCamera3PostProcessor::start(const reprocess_config_t &config)
{
    int32_t rc = NO_ERROR;
    QCamera3HardwareInterface* hal_obj = (QCamera3HardwareInterface*)m_parent->mUserData;

    if (config.reprocess_type != REPROCESS_TYPE_NONE) {
        if (m_pReprocChannel != NULL) {
            m_pReprocChannel->stop();
            delete m_pReprocChannel;
            m_pReprocChannel = NULL;
        }

        // if reprocess is needed, start reprocess channel
        LOGD("Setting input channel as pInputChannel");
        m_pReprocChannel = hal_obj->addOfflineReprocChannel(config, m_parent);
        if (m_pReprocChannel == NULL) {
            LOGE("cannot add reprocess channel");
            return UNKNOWN_ERROR;
        }
        /*start the reprocess channel only if buffers are already allocated, thus
          only start it in an intermediate reprocess type, defer it for others*/
        if (config.reprocess_type == REPROCESS_TYPE_JPEG) {
            rc = m_pReprocChannel->start();
            if (rc != 0) {
                LOGE("cannot start reprocess channel");
                delete m_pReprocChannel;
                m_pReprocChannel = NULL;
                return rc;
            }
        }
    }
    m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_START_DATA_PROC, TRUE, FALSE);

    return rc;
}

/*===========================================================================
 * FUNCTION   : flush
 *
 * DESCRIPTION: stop ongoing postprocess and jpeg jobs
 *
 * PARAMETERS : None
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *
 *==========================================================================*/
int32_t QCamera3PostProcessor::flush()
{
    int32_t rc = NO_ERROR;
    qcamera_hal3_jpeg_data_t *jpeg_job =
            (qcamera_hal3_jpeg_data_t *)m_ongoingJpegQ.dequeue();
    while (jpeg_job != NULL) {
        rc = mJpegHandle.abort_job(jpeg_job->jobId);
        releaseJpegJobData(jpeg_job);
        free(jpeg_job);

        jpeg_job = (qcamera_hal3_jpeg_data_t *)m_ongoingJpegQ.dequeue();
    }
    rc = releaseOfflineBuffers(true);
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
int32_t QCamera3PostProcessor::stop()
{
    m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_STOP_DATA_PROC, TRUE, TRUE);

    if (m_pReprocChannel != NULL) {
        m_pReprocChannel->stop();
        delete m_pReprocChannel;
        m_pReprocChannel = NULL;
    }

    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : getFWKJpegEncodeConfig
 *
 * DESCRIPTION: function to prepare encoding job information
 *
 * PARAMETERS :
 *   @encode_parm   : param to be filled with encoding configuration
 *   @frame         : framework input buffer
 *   @jpeg_settings : jpeg settings to be applied for encoding
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3PostProcessor::getFWKJpegEncodeConfig(
        mm_jpeg_encode_params_t& encode_parm,
        qcamera_fwk_input_pp_data_t *frame,
        jpeg_settings_t *jpeg_settings)
{
    LOGD("E");
    int32_t ret = NO_ERROR;

    if ((NULL == frame) || (NULL == jpeg_settings)) {
        return BAD_VALUE;
    }

    ssize_t bufSize = mOutputMem->getSize(jpeg_settings->out_buf_index);
    if (BAD_INDEX == bufSize) {
        LOGE("cannot retrieve buffer size for buffer %u",
                jpeg_settings->out_buf_index);
        return BAD_VALUE;
    }

    encode_parm.jpeg_cb = mJpegCB;
    encode_parm.userdata = mJpegUserData;

    if (jpeg_settings->thumbnail_size.width > 0 &&
            jpeg_settings->thumbnail_size.height > 0)
        m_bThumbnailNeeded = TRUE;
    else
        m_bThumbnailNeeded = FALSE;
    encode_parm.encode_thumbnail = m_bThumbnailNeeded;

    // get color format
    cam_format_t img_fmt = frame->reproc_config.stream_format;
    encode_parm.color_format = getColorfmtFromImgFmt(img_fmt);

    // get jpeg quality
    encode_parm.quality = jpeg_settings->jpeg_quality;
    if (encode_parm.quality <= 0) {
        encode_parm.quality = 85;
    }

    // get jpeg thumbnail quality
    encode_parm.thumb_quality = jpeg_settings->jpeg_thumb_quality;

    cam_frame_len_offset_t main_offset =
            frame->reproc_config.input_stream_plane_info.plane_info;

    encode_parm.num_src_bufs = 1;
    encode_parm.src_main_buf[0].index = 0;
    encode_parm.src_main_buf[0].buf_size = frame->input_buffer.frame_len;
    encode_parm.src_main_buf[0].buf_vaddr = (uint8_t *) frame->input_buffer.buffer;
    encode_parm.src_main_buf[0].fd = frame->input_buffer.fd;
    encode_parm.src_main_buf[0].format = MM_JPEG_FMT_YUV;
    encode_parm.src_main_buf[0].offset = main_offset;

    //Pass input thumbnail buffer info to encoder.
    //Note: Use main buffer to encode thumbnail
    if (m_bThumbnailNeeded == TRUE) {
        encode_parm.num_tmb_bufs = 1;
        encode_parm.src_thumb_buf[0] = encode_parm.src_main_buf[0];
    }

    //Pass output jpeg buffer info to encoder.
    //mOutputMem is allocated by framework.
    encode_parm.num_dst_bufs = 1;
    encode_parm.dest_buf[0].index = 0;
    encode_parm.dest_buf[0].buf_size = (size_t)bufSize;
    encode_parm.dest_buf[0].buf_vaddr = (uint8_t *)mOutputMem->getPtr(
            jpeg_settings->out_buf_index);
    encode_parm.dest_buf[0].fd = mOutputMem->getFd(
            jpeg_settings->out_buf_index);
    encode_parm.dest_buf[0].format = MM_JPEG_FMT_YUV;
    encode_parm.dest_buf[0].offset = main_offset;

    LOGD("X");
    return NO_ERROR;

    LOGD("X with error %d", ret);
    return ret;
}

/*===========================================================================
 * FUNCTION   : getJpegEncodeConfig
 *
 * DESCRIPTION: function to prepare encoding job information
 *
 * PARAMETERS :
 *   @encode_parm   : param to be filled with encoding configuration
 *   #main_stream   : stream object where the input buffer comes from
 *   @jpeg_settings : jpeg settings to be applied for encoding
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3PostProcessor::getJpegEncodeConfig(
                mm_jpeg_encode_params_t& encode_parm,
                QCamera3Stream *main_stream,
                jpeg_settings_t *jpeg_settings)
{
    LOGD("E");
    int32_t ret = NO_ERROR;
    ssize_t bufSize = 0;

    encode_parm.jpeg_cb = mJpegCB;
    encode_parm.userdata = mJpegUserData;

    if (jpeg_settings->thumbnail_size.width > 0 &&
            jpeg_settings->thumbnail_size.height > 0)
        m_bThumbnailNeeded = TRUE;
    else
        m_bThumbnailNeeded = FALSE;
    encode_parm.encode_thumbnail = m_bThumbnailNeeded;

    // get color format
    cam_format_t img_fmt = CAM_FORMAT_YUV_420_NV12;  //default value
    main_stream->getFormat(img_fmt);
    encode_parm.color_format = getColorfmtFromImgFmt(img_fmt);

    // get jpeg quality
    encode_parm.quality = jpeg_settings->jpeg_quality;
    if (encode_parm.quality <= 0) {
        encode_parm.quality = 85;
    }

    // get jpeg thumbnail quality
    encode_parm.thumb_quality = jpeg_settings->jpeg_thumb_quality;

    cam_frame_len_offset_t main_offset;
    memset(&main_offset, 0, sizeof(cam_frame_len_offset_t));
    main_stream->getFrameOffset(main_offset);

    // src buf config
    //Pass input main image buffer info to encoder.
    QCamera3StreamMem *pStreamMem = main_stream->getStreamBufs();
    if (pStreamMem == NULL) {
        LOGE("cannot get stream bufs from main stream");
        ret = BAD_VALUE;
        goto on_error;
    }
    encode_parm.num_src_bufs = MIN(pStreamMem->getCnt(), MM_JPEG_MAX_BUF);
    for (uint32_t i = 0; i < encode_parm.num_src_bufs; i++) {
        if (pStreamMem != NULL) {
            encode_parm.src_main_buf[i].index = i;
            bufSize = pStreamMem->getSize(i);
            if (BAD_INDEX == bufSize) {
                LOGE("cannot retrieve buffer size for buffer %u", i);
                ret = BAD_VALUE;
                goto on_error;
            }
            encode_parm.src_main_buf[i].buf_size = (size_t)bufSize;
            encode_parm.src_main_buf[i].buf_vaddr = (uint8_t *)pStreamMem->getPtr(i);
            encode_parm.src_main_buf[i].fd = pStreamMem->getFd(i);
            encode_parm.src_main_buf[i].format = MM_JPEG_FMT_YUV;
            encode_parm.src_main_buf[i].offset = main_offset;
        }
    }

    //Pass input thumbnail buffer info to encoder.
    //Note: Use main buffer to encode thumbnail
    if (m_bThumbnailNeeded == TRUE) {
        pStreamMem = main_stream->getStreamBufs();
        if (pStreamMem == NULL) {
            LOGE("cannot get stream bufs from thumb stream");
            ret = BAD_VALUE;
            goto on_error;
        }
        cam_frame_len_offset_t thumb_offset;
        memset(&thumb_offset, 0, sizeof(cam_frame_len_offset_t));
        main_stream->getFrameOffset(thumb_offset);
        encode_parm.num_tmb_bufs = MIN(pStreamMem->getCnt(), MM_JPEG_MAX_BUF);
        for (uint32_t i = 0; i < encode_parm.num_tmb_bufs; i++) {
            if (pStreamMem != NULL) {
                encode_parm.src_thumb_buf[i].index = i;
                bufSize = pStreamMem->getSize(i);
                if (BAD_INDEX == bufSize) {
                    LOGE("cannot retrieve buffer size for buffer %u", i);
                    ret = BAD_VALUE;
                    goto on_error;
                }
                encode_parm.src_thumb_buf[i].buf_size = (uint32_t)bufSize;
                encode_parm.src_thumb_buf[i].buf_vaddr = (uint8_t *)pStreamMem->getPtr(i);
                encode_parm.src_thumb_buf[i].fd = pStreamMem->getFd(i);
                encode_parm.src_thumb_buf[i].format = MM_JPEG_FMT_YUV;
                encode_parm.src_thumb_buf[i].offset = thumb_offset;
            }
        }
    }

    //Pass output jpeg buffer info to encoder.
    //mJpegMem is allocated by framework.
    bufSize = mOutputMem->getSize(jpeg_settings->out_buf_index);
    if (BAD_INDEX == bufSize) {
        LOGE("cannot retrieve buffer size for buffer %u",
                jpeg_settings->out_buf_index);
        ret = BAD_VALUE;
        goto on_error;
    }
    encode_parm.num_dst_bufs = 1;
    encode_parm.dest_buf[0].index = 0;
    encode_parm.dest_buf[0].buf_size = (size_t)bufSize;
    encode_parm.dest_buf[0].buf_vaddr = (uint8_t *)mOutputMem->getPtr(
            jpeg_settings->out_buf_index);
    encode_parm.dest_buf[0].fd = mOutputMem->getFd(
            jpeg_settings->out_buf_index);
    encode_parm.dest_buf[0].format = MM_JPEG_FMT_YUV;
    encode_parm.dest_buf[0].offset = main_offset;

    LOGD("X");
    return NO_ERROR;

on_error:
    LOGD("X with error %d", ret);
    return ret;
}

int32_t QCamera3PostProcessor::processData(mm_camera_super_buf_t *input) {
    return processData(input, NULL, 0);
}

/*===========================================================================
 * FUNCTION   : processData
 *
 * DESCRIPTION: enqueue data into dataProc thread
 *
 * PARAMETERS :
 *   @frame   : process input frame
 *   @output  : process output frame
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *
 * NOTE       : depends on if offline reprocess is needed, received frame will
 *              be sent to either input queue of postprocess or jpeg encoding
 *==========================================================================*/
int32_t QCamera3PostProcessor::processData(mm_camera_super_buf_t *input,
        buffer_handle_t *output, uint32_t frameNumber)
{
    LOGD("E");
    pthread_mutex_lock(&mReprocJobLock);

    // enqueue to post proc input queue
    qcamera_hal3_pp_buffer_t *pp_buffer = (qcamera_hal3_pp_buffer_t *)malloc(
            sizeof(qcamera_hal3_pp_buffer_t));
    if (NULL == pp_buffer) {
        LOGE("out of memory");
        return NO_MEMORY;
    }
    memset(pp_buffer, 0, sizeof(*pp_buffer));
    pp_buffer->input = input;
    pp_buffer->output = output;
    pp_buffer->frameNumber = frameNumber;
    m_inputPPQ.enqueue((void *)pp_buffer);
    if (!(m_inputMetaQ.isEmpty())) {
        LOGD("meta queue is not empty, do next job");
        m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);
    } else
        LOGD("metadata queue is empty");
    pthread_mutex_unlock(&mReprocJobLock);

    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : needsReprocess
 *
 * DESCRIPTION: Determine if reprocess is needed.
 *
 * PARAMETERS :
 *   @frame   : process frame
 *
 * RETURN     :
 *  TRUE if frame needs to be reprocessed
 *  FALSE is frame does not need to be reprocessed
 *
 *==========================================================================*/
bool QCamera3PostProcessor::needsReprocess(qcamera_fwk_input_pp_data_t *frame)
{
    metadata_buffer_t* meta = (metadata_buffer_t *) frame->metadata_buffer.buffer;
    bool edgeModeOn = FALSE;
    bool noiseRedModeOn = FALSE;
    bool reproNotDone = TRUE;

    if (frame->reproc_config.reprocess_type == REPROCESS_TYPE_NONE) {
        return FALSE;
    }

    // edge detection
    IF_META_AVAILABLE(cam_edge_application_t, edgeMode,
            CAM_INTF_META_EDGE_MODE, meta) {
        edgeModeOn = (CAM_EDGE_MODE_OFF != edgeMode->edge_mode);
    }

    // noise reduction
    IF_META_AVAILABLE(uint32_t, noiseRedMode,
            CAM_INTF_META_NOISE_REDUCTION_MODE, meta) {
        noiseRedModeOn = (CAM_NOISE_REDUCTION_MODE_OFF != *noiseRedMode);
    }

    IF_META_AVAILABLE(uint8_t, reprocess_flags,
            CAM_INTF_META_REPROCESS_FLAGS, meta) {
        reproNotDone = FALSE;
    }

    return (edgeModeOn || noiseRedModeOn || reproNotDone);
}

/*===========================================================================
 * FUNCTION   : processData
 *
 * DESCRIPTION: enqueue data into dataProc thread
 *
 * PARAMETERS :
 *   @frame   : process frame
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *
 * NOTE       : depends on if offline reprocess is needed, received frame will
 *              be sent to either input queue of postprocess or jpeg encoding
 *==========================================================================*/
int32_t QCamera3PostProcessor::processData(qcamera_fwk_input_pp_data_t *frame)
{
    if (needsReprocess(frame)) {
        ATRACE_INT("Camera:Reprocess", 1);
        LOGH("scheduling framework reprocess");
        pthread_mutex_lock(&mReprocJobLock);
        // enqueu to post proc input queue
        m_inputFWKPPQ.enqueue((void *)frame);
        m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);
        pthread_mutex_unlock(&mReprocJobLock);
    } else {
        jpeg_settings_t *jpeg_settings = (jpeg_settings_t *)m_jpegSettingsQ.dequeue();

        if (jpeg_settings == NULL) {
            LOGE("Cannot find jpeg settings");
            return BAD_VALUE;
        }

        LOGH("no need offline reprocess, sending to jpeg encoding");
        qcamera_hal3_jpeg_data_t *jpeg_job =
            (qcamera_hal3_jpeg_data_t *)malloc(sizeof(qcamera_hal3_jpeg_data_t));
        if (jpeg_job == NULL) {
            LOGE("No memory for jpeg job");
            return NO_MEMORY;
        }

        memset(jpeg_job, 0, sizeof(qcamera_hal3_jpeg_data_t));
        jpeg_job->fwk_frame = frame;
        jpeg_job->jpeg_settings = jpeg_settings;
        jpeg_job->metadata =
                (metadata_buffer_t *) frame->metadata_buffer.buffer;

        // enqueu to jpeg input queue
        m_inputJpegQ.enqueue((void *)jpeg_job);
        m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);
    }

    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : processPPMetadata
 *
 * DESCRIPTION: enqueue data into dataProc thread
 *
 * PARAMETERS :
 *   @frame   : process metadata frame received from pic channel
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *
 *==========================================================================*/
int32_t QCamera3PostProcessor::processPPMetadata(mm_camera_super_buf_t *reproc_meta)
{
    LOGD("E");
    pthread_mutex_lock(&mReprocJobLock);
    // enqueue to metadata input queue
    m_inputMetaQ.enqueue((void *)reproc_meta);
    if (!(m_inputPPQ.isEmpty())) {
       LOGD("pp queue is not empty, do next job");
       m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);
    } else {
       LOGD("pp queue is empty, not calling do next job");
    }
    pthread_mutex_unlock(&mReprocJobLock);
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : processJpegSettingData
 *
 * DESCRIPTION: enqueue jpegSetting into dataProc thread
 *
 * PARAMETERS :
 *   @jpeg_settings : jpeg settings data received from pic channel
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *
 *==========================================================================*/
int32_t QCamera3PostProcessor::processJpegSettingData(
        jpeg_settings_t *jpeg_settings)
{
    if (!jpeg_settings) {
        LOGE("invalid jpeg settings pointer");
        return -EINVAL;
    }
    return m_jpegSettingsQ.enqueue((void *)jpeg_settings);
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
int32_t QCamera3PostProcessor::processPPData(mm_camera_super_buf_t *frame)
{
    qcamera_hal3_pp_data_t *job = (qcamera_hal3_pp_data_t *)m_ongoingPPQ.dequeue();
    ATRACE_INT("Camera:Reprocess", 0);
    if (job == NULL || ((NULL == job->src_frame) && (NULL == job->fwk_src_frame))) {
        LOGE("Cannot find reprocess job");
        return BAD_VALUE;
    }
    if (job->jpeg_settings == NULL) {
        LOGE("Cannot find jpeg settings");
        return BAD_VALUE;
    }

    qcamera_hal3_jpeg_data_t *jpeg_job =
        (qcamera_hal3_jpeg_data_t *)malloc(sizeof(qcamera_hal3_jpeg_data_t));
    if (jpeg_job == NULL) {
        LOGE("No memory for jpeg job");
        return NO_MEMORY;
    }

    memset(jpeg_job, 0, sizeof(qcamera_hal3_jpeg_data_t));
    jpeg_job->src_frame = frame;
    if(frame != job->src_frame)
        jpeg_job->src_reproc_frame = job->src_frame;
    if (NULL == job->fwk_src_frame) {
        jpeg_job->metadata = job->metadata;
    } else {
        jpeg_job->metadata =
                (metadata_buffer_t *) job->fwk_src_frame->metadata_buffer.buffer;
        jpeg_job->fwk_src_buffer = job->fwk_src_frame;
    }
    jpeg_job->src_metadata = job->src_metadata;
    jpeg_job->jpeg_settings = job->jpeg_settings;

    // free pp job buf
    free(job);

    // enqueu reprocessed frame to jpeg input queue
    m_inputJpegQ.enqueue((void *)jpeg_job);

    // wait up data proc thread
    m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);

    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : dequeuePPJob
 *
 * DESCRIPTION: find a postprocessing job from ongoing pp queue by frame number
 *
 * PARAMETERS :
 *   @frameNumber : frame number for the pp job
 *
 * RETURN     : ptr to a pp job struct. NULL if not found.
 *==========================================================================*/
qcamera_hal3_pp_data_t *QCamera3PostProcessor::dequeuePPJob(uint32_t frameNumber) {
    qcamera_hal3_pp_data_t *pp_job = NULL;
    pp_job = (qcamera_hal3_pp_data_t *)m_ongoingPPQ.dequeue();

    if (pp_job == NULL) {
        LOGE("Fatal: ongoing PP queue is empty");
        return NULL;
    }
    if (pp_job->fwk_src_frame &&
            (pp_job->fwk_src_frame->frameNumber != frameNumber)) {
        LOGE("head of pp queue doesn't match requested frame number");
    }
    return pp_job;
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
qcamera_hal3_jpeg_data_t *QCamera3PostProcessor::findJpegJobByJobId(uint32_t jobId)
{
    qcamera_hal3_jpeg_data_t * job = NULL;
    if (jobId == 0) {
        LOGE("not a valid jpeg jobId");
        return NULL;
    }

    // currely only one jpeg job ongoing, so simply dequeue the head
    job = (qcamera_hal3_jpeg_data_t *)m_ongoingJpegQ.dequeue();
    return job;
}

/*===========================================================================
 * FUNCTION   : releasePPInputData
 *
 * DESCRIPTION: callback function to release post process input data node
 *
 * PARAMETERS :
 *   @data      : ptr to post process input data
 *   @user_data : user data ptr (QCamera3Reprocessor)
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera3PostProcessor::releasePPInputData(void *data, void *user_data)
{
    QCamera3PostProcessor *pme = (QCamera3PostProcessor *)user_data;
    if (NULL != pme) {
        qcamera_hal3_pp_buffer_t *buf = (qcamera_hal3_pp_buffer_t *)data;
        if (NULL != buf) {
            if (buf->input) {
                pme->releaseSuperBuf(buf->input);
                free(buf->input);
                buf->input = NULL;
            }
        }
    }
}

/*===========================================================================
 * FUNCTION   : releaseMetaData
 *
 * DESCRIPTION: callback function to release metadata camera buffer
 *
 * PARAMETERS :
 *   @data      : ptr to post process input data
 *   @user_data : user data ptr (QCamera3Reprocessor)
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera3PostProcessor::releaseMetadata(void *data, void *user_data)
{
    QCamera3PostProcessor *pme = (QCamera3PostProcessor *)user_data;
    if (NULL != pme) {
        pme->m_parent->metadataBufDone((mm_camera_super_buf_t *)data);
    }
}

/*===========================================================================
 * FUNCTION   : releaseJpegData
 *
 * DESCRIPTION: callback function to release jpeg job node
 *
 * PARAMETERS :
 *   @data      : ptr to ongoing jpeg job data
 *   @user_data : user data ptr (QCamera3Reprocessor)
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera3PostProcessor::releaseJpegData(void *data, void *user_data)
{
    QCamera3PostProcessor *pme = (QCamera3PostProcessor *)user_data;
    if (NULL != pme) {
        pme->releaseJpegJobData((qcamera_hal3_jpeg_data_t *)data);
    }
}

/*===========================================================================
 * FUNCTION   : releaseOngoingPPData
 *
 * DESCRIPTION: callback function to release ongoing postprocess job node
 *
 * PARAMETERS :
 *   @data      : ptr to onging postprocess job
 *   @user_data : user data ptr (QCamera3Reprocessor)
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera3PostProcessor::releaseOngoingPPData(void *data, void *user_data)
{
    QCamera3PostProcessor *pme = (QCamera3PostProcessor *)user_data;
    if (NULL != pme) {
        qcamera_hal3_pp_data_t *pp_data = (qcamera_hal3_pp_data_t *)data;

        if (pp_data && pp_data->src_frame)
          pme->releaseSuperBuf(pp_data->src_frame);

        pme->releasePPJobData(pp_data);

    }
}

/*===========================================================================
 * FUNCTION   : releaseSuperBuf
 *
 * DESCRIPTION: function to release a superbuf frame by returning back to kernel
 *
 * PARAMETERS :
 *   @super_buf : ptr to the superbuf frame
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera3PostProcessor::releaseSuperBuf(mm_camera_super_buf_t *super_buf)
{
    if (NULL != super_buf) {
        if (m_parent != NULL) {
            m_parent->bufDone(super_buf);
        }
    }
}

/*===========================================================================
 * FUNCTION   : releaseOfflineBuffers
 *
 * DESCRIPTION: function to release/unmap offline buffers if any
 *
 * PARAMETERS :
 * @allBuffers : flag that asks to release all buffers or only one
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera3PostProcessor::releaseOfflineBuffers(bool allBuffers)
{
    int32_t rc = NO_ERROR;

    if(NULL != m_pReprocChannel) {
        rc = m_pReprocChannel->unmapOfflineBuffers(allBuffers);
    }

    return rc;
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
void QCamera3PostProcessor::releaseJpegJobData(qcamera_hal3_jpeg_data_t *job)
{
    ATRACE_CALL();
    int32_t rc = NO_ERROR;
    LOGD("E");
    if (NULL != job) {
        if (NULL != job->src_reproc_frame) {
            free(job->src_reproc_frame);
            job->src_reproc_frame = NULL;
        }

        if (NULL != job->src_frame) {
            if (NULL != m_pReprocChannel) {
                rc = m_pReprocChannel->bufDone(job->src_frame);
                if (NO_ERROR != rc)
                    LOGE("bufDone error: %d", rc);
            }
            free(job->src_frame);
            job->src_frame = NULL;
        }

        if (NULL != job->fwk_src_buffer) {
            free(job->fwk_src_buffer);
            job->fwk_src_buffer = NULL;
        } else if (NULL != job->src_metadata) {
            m_parent->metadataBufDone(job->src_metadata);
            free(job->src_metadata);
            job->src_metadata = NULL;
        }

        if (NULL != job->fwk_frame) {
            free(job->fwk_frame);
            job->fwk_frame = NULL;
        }

        if (NULL != job->pJpegExifObj) {
            delete job->pJpegExifObj;
            job->pJpegExifObj = NULL;
        }

        if (NULL != job->jpeg_settings) {
            free(job->jpeg_settings);
            job->jpeg_settings = NULL;
        }
    }
    /* Additional trigger to process any pending jobs in the input queue */
    m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);
    LOGD("X");
}

/*===========================================================================
 * FUNCTION   : releasePPJobData
 *
 * DESCRIPTION: function to release internal resources in p pjob struct
 *
 * PARAMETERS :
 *   @job     : ptr to pp job struct
 *
 * RETURN     : None
 *
 * NOTE       : Original source metadata buffer needs to be released and
 *              queued back to kernel for future use. src_frame, src_metadata,
 *              and fwk_src_frame structures need to be freed.
 *==========================================================================*/
void QCamera3PostProcessor::releasePPJobData(qcamera_hal3_pp_data_t *pp_job)
{
    ATRACE_CALL();
    LOGD("E");
    if (NULL != pp_job) {
        if (NULL != pp_job->src_frame) {
            free(pp_job->src_frame);
            if (NULL != pp_job->src_metadata) {
                m_parent->metadataBufDone(pp_job->src_metadata);
                free(pp_job->src_metadata);
            }
            pp_job->src_frame = NULL;
            pp_job->metadata = NULL;
        }

        if (NULL != pp_job->fwk_src_frame) {
            free(pp_job->fwk_src_frame);
            pp_job->fwk_src_frame = NULL;
        }
    }

    /* Additional trigger to process any pending jobs in the input queue */
    m_dataProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);
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
mm_jpeg_color_format QCamera3PostProcessor::getColorfmtFromImgFmt(cam_format_t img_fmt)
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
mm_jpeg_format_t QCamera3PostProcessor::getJpegImgTypeFromImgFmt(cam_format_t img_fmt)
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
 * FUNCTION   : encodeFWKData
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
int32_t QCamera3PostProcessor::encodeFWKData(qcamera_hal3_jpeg_data_t *jpeg_job_data,
        uint8_t &needNewSess)
{
    LOGD("E");
    int32_t ret = NO_ERROR;
    mm_jpeg_job_t jpg_job;
    uint32_t jobId = 0;
    qcamera_fwk_input_pp_data_t *recvd_frame = NULL;
    metadata_buffer_t *metadata = NULL;
    jpeg_settings_t *jpeg_settings = NULL;
    QCamera3HardwareInterface* hal_obj = NULL;
    mm_jpeg_debug_exif_params_t *exif_debug_params = NULL;
    bool needJpegExifRotation = false;

    if (NULL == jpeg_job_data) {
        LOGE("Invalid jpeg job");
        return BAD_VALUE;
    }

    recvd_frame = jpeg_job_data->fwk_frame;
    if (NULL == recvd_frame) {
        LOGE("Invalid input buffer");
        return BAD_VALUE;
    }

    metadata = jpeg_job_data->metadata;
    if (NULL == metadata) {
        LOGE("Invalid metadata buffer");
        return BAD_VALUE;
    }

    jpeg_settings = jpeg_job_data->jpeg_settings;
    if (NULL == jpeg_settings) {
        LOGE("Invalid jpeg settings buffer");
        return BAD_VALUE;
    }

    if ((NULL != jpeg_job_data->src_frame) && (NULL != jpeg_job_data->src_frame)) {
        LOGE("Unsupported case both framework and camera source buffers are invalid!");
        return BAD_VALUE;
    }

    hal_obj = (QCamera3HardwareInterface*)m_parent->mUserData;
    if (hal_obj == NULL) {
        LOGE("hal_obj is NULL, Error");
        return BAD_VALUE;
    }

    if (mJpegClientHandle <= 0) {
        LOGE("Error: bug here, mJpegClientHandle is 0");
        return UNKNOWN_ERROR;
    }

    cam_dimension_t src_dim;
    memset(&src_dim, 0, sizeof(cam_dimension_t));
    src_dim.width = recvd_frame->reproc_config.input_stream_dim.width;
    src_dim.height = recvd_frame->reproc_config.input_stream_dim.height;

    cam_dimension_t dst_dim;
    memset(&dst_dim, 0, sizeof(cam_dimension_t));
    dst_dim.width = recvd_frame->reproc_config.output_stream_dim.width;
    dst_dim.height = recvd_frame->reproc_config.output_stream_dim.height;

    cam_rect_t crop;
    memset(&crop, 0, sizeof(cam_rect_t));
    //TBD_later - Zoom event removed in stream
    //main_stream->getCropInfo(crop);

    // Set JPEG encode crop in reprocess frame metadata
    // If this JPEG crop info exist, encoder should do cropping
    IF_META_AVAILABLE(cam_stream_crop_info_t, jpeg_crop,
            CAM_INTF_PARM_JPEG_ENCODE_CROP, metadata) {
        memcpy(&crop, &(jpeg_crop->crop), sizeof(cam_rect_t));
    }

    // Set JPEG encode crop in reprocess frame metadata
    // If this JPEG scale info exist, encoder should do scaling
    IF_META_AVAILABLE(cam_dimension_t, scale_dim,
            CAM_INTF_PARM_JPEG_SCALE_DIMENSION, metadata) {
        if (scale_dim->width != 0 && scale_dim->height != 0) {
            dst_dim.width = scale_dim->width;
            dst_dim.height = scale_dim->height;
        }
    }

    needJpegExifRotation = (hal_obj->needJpegExifRotation() || !needsReprocess(recvd_frame));

    // If EXIF rotation metadata is added and used to match the JPEG orientation,
    // it means CPP rotation is not involved, whether it is because CPP does not
    // support rotation, or the reprocessed frame is not sent to CPP.
    // Override CAM_INTF_PARM_ROTATION to 0 to avoid wrong CPP rotation info
    // to be filled in to JPEG metadata.
    if (needJpegExifRotation) {
        cam_rotation_info_t rotation_info;
        memset(&rotation_info, 0, sizeof(rotation_info));
        rotation_info.rotation = ROTATE_0;
        rotation_info.streamId = 0;
        ADD_SET_PARAM_ENTRY_TO_BATCH(metadata, CAM_INTF_PARM_ROTATION, rotation_info);
    }

    LOGH("Need new session?:%d", needNewSess);
    if (needNewSess) {
        //creating a new session, so we must destroy the old one
        if ( 0 < mJpegSessionId ) {
            ret = mJpegHandle.destroy_session(mJpegSessionId);
            if (ret != NO_ERROR) {
                LOGE("Error destroying an old jpeg encoding session, id = %d",
                       mJpegSessionId);
                return ret;
            }
            mJpegSessionId = 0;
        }
        // create jpeg encoding session
        mm_jpeg_encode_params_t encodeParam;
        memset(&encodeParam, 0, sizeof(mm_jpeg_encode_params_t));
        getFWKJpegEncodeConfig(encodeParam, recvd_frame, jpeg_settings);
        LOGH("#src bufs:%d # tmb bufs:%d #dst_bufs:%d",
                     encodeParam.num_src_bufs,encodeParam.num_tmb_bufs,encodeParam.num_dst_bufs);
        if (!needJpegExifRotation &&
            (jpeg_settings->jpeg_orientation == 90 ||
            jpeg_settings->jpeg_orientation == 270)) {
            // swap src width and height, stride and scanline due to rotation
            encodeParam.main_dim.src_dim.width = src_dim.height;
            encodeParam.main_dim.src_dim.height = src_dim.width;
            encodeParam.thumb_dim.src_dim.width = src_dim.height;
            encodeParam.thumb_dim.src_dim.height = src_dim.width;

            int32_t temp = encodeParam.src_main_buf[0].offset.mp[0].stride;
            encodeParam.src_main_buf[0].offset.mp[0].stride =
                encodeParam.src_main_buf[0].offset.mp[0].scanline;
            encodeParam.src_main_buf[0].offset.mp[0].scanline = temp;

            temp = encodeParam.src_thumb_buf[0].offset.mp[0].stride;
            encodeParam.src_thumb_buf[0].offset.mp[0].stride =
                encodeParam.src_thumb_buf[0].offset.mp[0].scanline;
            encodeParam.src_thumb_buf[0].offset.mp[0].scanline = temp;
        } else {
            encodeParam.main_dim.src_dim = src_dim;
            encodeParam.thumb_dim.src_dim = src_dim;
        }
        encodeParam.main_dim.dst_dim = dst_dim;
        encodeParam.thumb_dim.dst_dim = jpeg_settings->thumbnail_size;

        LOGI("Src Buffer cnt = %d, res = %dX%d len = %d rot = %d "
            "src_dim = %dX%d dst_dim = %dX%d",
            encodeParam.num_src_bufs,
            encodeParam.src_main_buf[0].offset.mp[0].stride,
            encodeParam.src_main_buf[0].offset.mp[0].scanline,
            encodeParam.src_main_buf[0].offset.frame_len,
            encodeParam.rotation,
            src_dim.width, src_dim.height,
            dst_dim.width, dst_dim.height);
        LOGI("Src THUMB buf_cnt = %d, res = %dX%d len = %d rot = %d "
            "src_dim = %dX%d, dst_dim = %dX%d",
            encodeParam.num_tmb_bufs,
            encodeParam.src_thumb_buf[0].offset.mp[0].stride,
            encodeParam.src_thumb_buf[0].offset.mp[0].scanline,
            encodeParam.src_thumb_buf[0].offset.frame_len,
            encodeParam.thumb_rotation,
            encodeParam.thumb_dim.src_dim.width,
            encodeParam.thumb_dim.src_dim.height,
            encodeParam.thumb_dim.dst_dim.width,
            encodeParam.thumb_dim.dst_dim.height);

        LOGH("#src bufs:%d # tmb bufs:%d #dst_bufs:%d",
                     encodeParam.num_src_bufs,encodeParam.num_tmb_bufs,encodeParam.num_dst_bufs);

        ret = mJpegHandle.create_session(mJpegClientHandle, &encodeParam, &mJpegSessionId);
        if (ret != NO_ERROR) {
            LOGE("Error creating a new jpeg encoding session, ret = %d", ret);
            return ret;
        }
        needNewSess = FALSE;
    }

    // Fill in new job
    memset(&jpg_job, 0, sizeof(mm_jpeg_job_t));
    jpg_job.job_type = JPEG_JOB_TYPE_ENCODE;
    jpg_job.encode_job.session_id = mJpegSessionId;
    jpg_job.encode_job.src_index = 0;
    jpg_job.encode_job.dst_index = 0;

    // Set main dim job parameters and handle rotation
    if (!needJpegExifRotation && (jpeg_settings->jpeg_orientation == 90 ||
            jpeg_settings->jpeg_orientation == 270)) {

        jpg_job.encode_job.main_dim.src_dim.width = src_dim.height;
        jpg_job.encode_job.main_dim.src_dim.height = src_dim.width;

        jpg_job.encode_job.main_dim.dst_dim.width = dst_dim.height;
        jpg_job.encode_job.main_dim.dst_dim.height = dst_dim.width;

        jpg_job.encode_job.main_dim.crop.width = crop.height;
        jpg_job.encode_job.main_dim.crop.height = crop.width;
        jpg_job.encode_job.main_dim.crop.left = crop.top;
        jpg_job.encode_job.main_dim.crop.top = crop.left;
    } else {
        jpg_job.encode_job.main_dim.src_dim = src_dim;
        jpg_job.encode_job.main_dim.dst_dim = dst_dim;
        jpg_job.encode_job.main_dim.crop = crop;
    }

    // get 3a sw version info
    cam_q3a_version_t sw_version;
    memset(&sw_version, 0, sizeof(sw_version));
    if (hal_obj)
        hal_obj->get3AVersion(sw_version);

    // get exif data
    QCamera3Exif *pJpegExifObj = getExifData(metadata, jpeg_settings, needJpegExifRotation);
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

    // thumbnail dim
    LOGH("Thumbnail needed:%d", m_bThumbnailNeeded);
    if (m_bThumbnailNeeded == TRUE) {
        jpg_job.encode_job.thumb_dim.dst_dim =
                jpeg_settings->thumbnail_size;

        if (!needJpegExifRotation && (jpeg_settings->jpeg_orientation == 90 ||
                jpeg_settings->jpeg_orientation == 270)) {
            //swap the thumbnail destination width and height if it has
            //already been rotated
            int temp = jpg_job.encode_job.thumb_dim.dst_dim.width;
            jpg_job.encode_job.thumb_dim.dst_dim.width =
                    jpg_job.encode_job.thumb_dim.dst_dim.height;
            jpg_job.encode_job.thumb_dim.dst_dim.height = temp;

            jpg_job.encode_job.thumb_dim.src_dim.width = src_dim.height;
            jpg_job.encode_job.thumb_dim.src_dim.height = src_dim.width;

            jpg_job.encode_job.thumb_dim.crop.width = crop.height;
            jpg_job.encode_job.thumb_dim.crop.height = crop.width;
            jpg_job.encode_job.thumb_dim.crop.left = crop.top;
            jpg_job.encode_job.thumb_dim.crop.top = crop.left;
        } else {
        jpg_job.encode_job.thumb_dim.src_dim = src_dim;
        jpg_job.encode_job.thumb_dim.crop = crop;
        }
        jpg_job.encode_job.thumb_index = 0;
    }

    jpg_job.encode_job.cam_exif_params = hal_obj->get3AExifParams();
    exif_debug_params = jpg_job.encode_job.cam_exif_params.debug_params;
    // Fill in exif debug data
    // Allocate for a local copy of debug parameters
    jpg_job.encode_job.cam_exif_params.debug_params =
            (mm_jpeg_debug_exif_params_t *) malloc (sizeof(mm_jpeg_debug_exif_params_t));
    if (!jpg_job.encode_job.cam_exif_params.debug_params) {
        LOGE("Out of Memory. Allocation failed for 3A debug exif params");
        return NO_MEMORY;
    }

    jpg_job.encode_job.mobicat_mask = hal_obj->getMobicatMask();

    if (metadata != NULL) {
        // Fill in the metadata passed as parameter
        jpg_job.encode_job.p_metadata = metadata;

       jpg_job.encode_job.p_metadata->is_mobicat_aec_params_valid =
                jpg_job.encode_job.cam_exif_params.cam_3a_params_valid;

       if (jpg_job.encode_job.cam_exif_params.cam_3a_params_valid) {
            jpg_job.encode_job.p_metadata->mobicat_aec_params =
                jpg_job.encode_job.cam_exif_params.cam_3a_params;
       }

        if (exif_debug_params) {
            // Copy debug parameters locally.
           memcpy(jpg_job.encode_job.cam_exif_params.debug_params,
                   exif_debug_params, (sizeof(mm_jpeg_debug_exif_params_t)));
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
    } else {
       LOGW("Metadata is null");
    }

    // Multi image info
    if (hal_obj->isDeviceLinked() == TRUE) {
        jpg_job.encode_job.multi_image_info.type = MM_JPEG_TYPE_JPEG;
        jpg_job.encode_job.multi_image_info.num_of_images = 1;
        jpg_job.encode_job.multi_image_info.enable_metadata = 1;
        if (hal_obj->isMainCamera() == TRUE) {
            jpg_job.encode_job.multi_image_info.is_primary = 1;
        } else {
            jpg_job.encode_job.multi_image_info.is_primary = 0;
        }
    }

    jpg_job.encode_job.hal_version = CAM_HAL_V3;

    //Start jpeg encoding
    ret = mJpegHandle.start_job(&jpg_job, &jobId);
    if (jpg_job.encode_job.cam_exif_params.debug_params) {
        free(jpg_job.encode_job.cam_exif_params.debug_params);
    }
    if (ret == NO_ERROR) {
        // remember job info
        jpeg_job_data->jobId = jobId;
    }

    LOGD("X");
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
int32_t QCamera3PostProcessor::encodeData(qcamera_hal3_jpeg_data_t *jpeg_job_data,
                          uint8_t &needNewSess)
{
    ATRACE_CALL();
    LOGD("E");
    int32_t ret = NO_ERROR;
    mm_jpeg_job_t jpg_job;
    uint32_t jobId = 0;
    QCamera3Stream *main_stream = NULL;
    mm_camera_buf_def_t *main_frame = NULL;
    QCamera3Channel *srcChannel = NULL;
    mm_camera_super_buf_t *recvd_frame = NULL;
    metadata_buffer_t *metadata = NULL;
    jpeg_settings_t *jpeg_settings = NULL;
    QCamera3HardwareInterface* hal_obj = NULL;
    mm_jpeg_debug_exif_params_t *exif_debug_params = NULL;
    if (m_parent != NULL) {
        hal_obj = (QCamera3HardwareInterface*)m_parent->mUserData;
        if (hal_obj == NULL) {
            LOGE("hal_obj is NULL, Error");
            return BAD_VALUE;
        }
    } else {
        LOGE("m_parent is NULL, Error");
        return BAD_VALUE;
    }
    bool needJpegExifRotation = false;

    recvd_frame = jpeg_job_data->src_frame;
    metadata = jpeg_job_data->metadata;
    jpeg_settings = jpeg_job_data->jpeg_settings;

    LOGD("encoding bufIndex: %u",
        jpeg_job_data->src_frame->bufs[0]->buf_idx);

    QCamera3Channel *pChannel = NULL;
    // first check picture channel
    if (m_parent->getMyHandle() == recvd_frame->ch_id) {
        pChannel = m_parent;
    }
    // check reprocess channel if not found
    if (pChannel == NULL) {
        if (m_pReprocChannel != NULL &&
            m_pReprocChannel->getMyHandle() == recvd_frame->ch_id) {
            pChannel = m_pReprocChannel;
        }
    }

    srcChannel = pChannel;

    if (srcChannel == NULL) {
        LOGE("No corresponding channel (ch_id = %d) exist, return here",
               recvd_frame->ch_id);
        return BAD_VALUE;
    }

    // find snapshot frame and thumnail frame
    //Note: In this version we will receive only snapshot frame.
    for (uint32_t i = 0; i < recvd_frame->num_bufs; i++) {
        QCamera3Stream *srcStream =
            srcChannel->getStreamByHandle(recvd_frame->bufs[i]->stream_id);
        if (srcStream != NULL) {
            switch (srcStream->getMyType()) {
            case CAM_STREAM_TYPE_SNAPSHOT:
            case CAM_STREAM_TYPE_OFFLINE_PROC:
                main_stream = srcStream;
                main_frame = recvd_frame->bufs[i];
                break;
            default:
                break;
            }
        }
    }

    if(NULL == main_frame){
       LOGE("Main frame is NULL");
       return BAD_VALUE;
    }

    QCamera3StreamMem *memObj = (QCamera3StreamMem *)main_frame->mem_info;
    if (NULL == memObj) {
        LOGE("Memeory Obj of main frame is NULL");
        return NO_MEMORY;
    }

    // clean and invalidate cache ops through mem obj of the frame
    memObj->cleanInvalidateCache(main_frame->buf_idx);

    if (mJpegClientHandle <= 0) {
        LOGE("Error: bug here, mJpegClientHandle is 0");
        return UNKNOWN_ERROR;
    }
    cam_dimension_t src_dim;
    memset(&src_dim, 0, sizeof(cam_dimension_t));
    main_stream->getFrameDimension(src_dim);

    cam_dimension_t dst_dim;
    memset(&dst_dim, 0, sizeof(cam_dimension_t));
    if (NO_ERROR != m_parent->getStreamSize(dst_dim)) {
        LOGE("Failed to get size of the JPEG stream");
        return UNKNOWN_ERROR;
    }

    needJpegExifRotation = hal_obj->needJpegExifRotation();
    IF_META_AVAILABLE(cam_rotation_info_t, rotation_info, CAM_INTF_PARM_ROTATION, metadata) {
        if (jpeg_settings->jpeg_orientation != 0 && rotation_info->rotation == ROTATE_0) {
            needJpegExifRotation = TRUE;
            LOGH("Need EXIF JPEG ROTATION");
        }
    }

    // Although in HAL3, legacy flip mode is not advertised
    // default value of CAM_INTF_PARM_FLIP is still added here
    // for jpge metadata
    int32_t flipMode = 0; // no flip
    ADD_SET_PARAM_ENTRY_TO_BATCH(metadata, CAM_INTF_PARM_FLIP, flipMode);

    LOGH("Need new session?:%d", needNewSess);
    if (needNewSess) {
        //creating a new session, so we must destroy the old one
        if ( 0 < mJpegSessionId ) {
            ret = mJpegHandle.destroy_session(mJpegSessionId);
            if (ret != NO_ERROR) {
                LOGE("Error destroying an old jpeg encoding session, id = %d",
                       mJpegSessionId);
                return ret;
            }
            mJpegSessionId = 0;
        }
        // create jpeg encoding session
        mm_jpeg_encode_params_t encodeParam;
        memset(&encodeParam, 0, sizeof(mm_jpeg_encode_params_t));
        getJpegEncodeConfig(encodeParam, main_stream, jpeg_settings);
        LOGH("#src bufs:%d # tmb bufs:%d #dst_bufs:%d",
                     encodeParam.num_src_bufs,encodeParam.num_tmb_bufs,encodeParam.num_dst_bufs);
        if (!needJpegExifRotation &&
            (jpeg_settings->jpeg_orientation == 90 ||
            jpeg_settings->jpeg_orientation == 270)) {
           //swap src width and height, stride and scanline due to rotation
           encodeParam.main_dim.src_dim.width = src_dim.height;
           encodeParam.main_dim.src_dim.height = src_dim.width;
           encodeParam.thumb_dim.src_dim.width = src_dim.height;
           encodeParam.thumb_dim.src_dim.height = src_dim.width;

           int32_t temp = encodeParam.src_main_buf[0].offset.mp[0].stride;
           encodeParam.src_main_buf[0].offset.mp[0].stride =
              encodeParam.src_main_buf[0].offset.mp[0].scanline;
           encodeParam.src_main_buf[0].offset.mp[0].scanline = temp;

           temp = encodeParam.src_thumb_buf[0].offset.mp[0].stride;
           encodeParam.src_thumb_buf[0].offset.mp[0].stride =
              encodeParam.src_thumb_buf[0].offset.mp[0].scanline;
           encodeParam.src_thumb_buf[0].offset.mp[0].scanline = temp;
        } else {
           encodeParam.main_dim.src_dim  = src_dim;
           encodeParam.thumb_dim.src_dim = src_dim;
        }
        encodeParam.main_dim.dst_dim = dst_dim;
        encodeParam.thumb_dim.dst_dim = jpeg_settings->thumbnail_size;

        LOGI("Src Buffer cnt = %d, res = %dX%d len = %d rot = %d "
            "src_dim = %dX%d dst_dim = %dX%d",
            encodeParam.num_src_bufs,
            encodeParam.src_main_buf[0].offset.mp[0].stride,
            encodeParam.src_main_buf[0].offset.mp[0].scanline,
            encodeParam.src_main_buf[0].offset.frame_len,
            encodeParam.rotation,
            src_dim.width, src_dim.height,
            dst_dim.width, dst_dim.height);
        LOGI("Src THUMB buf_cnt = %d, res = %dX%d len = %d rot = %d "
            "src_dim = %dX%d, dst_dim = %dX%d",
            encodeParam.num_tmb_bufs,
            encodeParam.src_thumb_buf[0].offset.mp[0].stride,
            encodeParam.src_thumb_buf[0].offset.mp[0].scanline,
            encodeParam.src_thumb_buf[0].offset.frame_len,
            encodeParam.thumb_rotation,
            encodeParam.thumb_dim.src_dim.width,
            encodeParam.thumb_dim.src_dim.height,
            encodeParam.thumb_dim.dst_dim.width,
            encodeParam.thumb_dim.dst_dim.height);
        ret = mJpegHandle.create_session(mJpegClientHandle, &encodeParam, &mJpegSessionId);
        if (ret != NO_ERROR) {
            LOGE("Error creating a new jpeg encoding session, ret = %d", ret);
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

    cam_rect_t crop;
    memset(&crop, 0, sizeof(cam_rect_t));
    //TBD_later - Zoom event removed in stream
    //main_stream->getCropInfo(crop);

    // Set main dim job parameters and handle rotation
    if (!needJpegExifRotation && (jpeg_settings->jpeg_orientation == 90 ||
            jpeg_settings->jpeg_orientation == 270)) {

        jpg_job.encode_job.main_dim.src_dim.width = src_dim.height;
        jpg_job.encode_job.main_dim.src_dim.height = src_dim.width;

        jpg_job.encode_job.main_dim.dst_dim.width = dst_dim.height;
        jpg_job.encode_job.main_dim.dst_dim.height = dst_dim.width;

        jpg_job.encode_job.main_dim.crop.width = crop.height;
        jpg_job.encode_job.main_dim.crop.height = crop.width;
        jpg_job.encode_job.main_dim.crop.left = crop.top;
        jpg_job.encode_job.main_dim.crop.top = crop.left;
    } else {
        jpg_job.encode_job.main_dim.src_dim = src_dim;
        jpg_job.encode_job.main_dim.dst_dim = dst_dim;
        jpg_job.encode_job.main_dim.crop = crop;
    }

    // get 3a sw version info
    cam_q3a_version_t sw_version;
    memset(&sw_version, 0, sizeof(sw_version));

    if (hal_obj)
        hal_obj->get3AVersion(sw_version);

    // get exif data
    QCamera3Exif *pJpegExifObj = getExifData(metadata, jpeg_settings, needJpegExifRotation);
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

    // thumbnail dim
    LOGH("Thumbnail needed:%d", m_bThumbnailNeeded);
    if (m_bThumbnailNeeded == TRUE) {
        jpg_job.encode_job.thumb_dim.dst_dim =
                jpeg_settings->thumbnail_size;

      if (!needJpegExifRotation &&
          (jpeg_settings->jpeg_orientation  == 90 ||
           jpeg_settings->jpeg_orientation == 270)) {
            //swap the thumbnail destination width and height if it has
            //already been rotated
            int temp = jpg_job.encode_job.thumb_dim.dst_dim.width;
            jpg_job.encode_job.thumb_dim.dst_dim.width =
                    jpg_job.encode_job.thumb_dim.dst_dim.height;
            jpg_job.encode_job.thumb_dim.dst_dim.height = temp;

            jpg_job.encode_job.thumb_dim.src_dim.width = src_dim.height;
            jpg_job.encode_job.thumb_dim.src_dim.height = src_dim.width;

            jpg_job.encode_job.thumb_dim.crop.width = crop.height;
            jpg_job.encode_job.thumb_dim.crop.height = crop.width;
            jpg_job.encode_job.thumb_dim.crop.left = crop.top;
            jpg_job.encode_job.thumb_dim.crop.top = crop.left;
        } else {
           jpg_job.encode_job.thumb_dim.src_dim = src_dim;
           jpg_job.encode_job.thumb_dim.crop = crop;
        }
        jpg_job.encode_job.thumb_index = main_frame->buf_idx;
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

    jpg_job.encode_job.cam_exif_params = hal_obj->get3AExifParams();
    exif_debug_params = jpg_job.encode_job.cam_exif_params.debug_params;

    // Allocate for a local copy of debug parameters
    jpg_job.encode_job.cam_exif_params.debug_params =
            (mm_jpeg_debug_exif_params_t *) malloc (sizeof(mm_jpeg_debug_exif_params_t));
    if (!jpg_job.encode_job.cam_exif_params.debug_params) {
        LOGE("Out of Memory. Allocation failed for 3A debug exif params");
        return NO_MEMORY;
    }

    jpg_job.encode_job.mobicat_mask = hal_obj->getMobicatMask();

    if (metadata != NULL) {
       //Fill in the metadata passed as parameter
       jpg_job.encode_job.p_metadata = metadata;

       jpg_job.encode_job.p_metadata->is_mobicat_aec_params_valid =
                jpg_job.encode_job.cam_exif_params.cam_3a_params_valid;

       if (jpg_job.encode_job.cam_exif_params.cam_3a_params_valid) {
            jpg_job.encode_job.p_metadata->mobicat_aec_params =
                jpg_job.encode_job.cam_exif_params.cam_3a_params;
       }

       if (exif_debug_params) {
            // Copy debug parameters locally.
           memcpy(jpg_job.encode_job.cam_exif_params.debug_params,
                   exif_debug_params, (sizeof(mm_jpeg_debug_exif_params_t)));
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
    } else {
       LOGW("Metadata is null");
    }

    // Multi image info
    if (hal_obj->isDeviceLinked() == TRUE) {
        jpg_job.encode_job.multi_image_info.type = MM_JPEG_TYPE_JPEG;
        jpg_job.encode_job.multi_image_info.num_of_images = 1;
        jpg_job.encode_job.multi_image_info.enable_metadata = 1;
        if (hal_obj->isMainCamera() == TRUE) {
            jpg_job.encode_job.multi_image_info.is_primary = 1;
        } else {
            jpg_job.encode_job.multi_image_info.is_primary = 0;
        }
    }

    jpg_job.encode_job.hal_version = CAM_HAL_V3;

    //Start jpeg encoding
    ret = mJpegHandle.start_job(&jpg_job, &jobId);
    if (jpg_job.encode_job.cam_exif_params.debug_params) {
        free(jpg_job.encode_job.cam_exif_params.debug_params);
    }
    if (ret == NO_ERROR) {
        // remember job info
        jpeg_job_data->jobId = jobId;
    }

    LOGD("X");
    return ret;
}

/*===========================================================================
 * FUNCTION   : dataProcessRoutine
 *
 * DESCRIPTION: data process routine that handles input data either from input
 *              Jpeg Queue to do jpeg encoding, or from input PP Queue to do
 *              reprocess.
 *
 * PARAMETERS :
 *   @data    : user data ptr (QCamera3PostProcessor)
 *
 * RETURN     : None
 *==========================================================================*/
void *QCamera3PostProcessor::dataProcessRoutine(void *data)
{
    int running = 1;
    int ret;
    uint8_t is_active = FALSE;
    uint8_t needNewSess = TRUE;
    mm_camera_super_buf_t *meta_buffer = NULL;
    LOGD("E");
    QCamera3PostProcessor *pme = (QCamera3PostProcessor *)data;
    QCameraCmdThread *cmdThread = &pme->m_dataProcTh;
    cmdThread->setName("cam_data_proc");

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
            needNewSess = TRUE;

            pme->m_ongoingPPQ.init();
            pme->m_inputJpegQ.init();
            pme->m_inputPPQ.init();
            pme->m_inputFWKPPQ.init();
            pme->m_inputMetaQ.init();
            cam_sem_post(&cmdThread->sync_sem);

            break;
        case CAMERA_CMD_TYPE_STOP_DATA_PROC:
            {
                LOGH("stop data proc");
                is_active = FALSE;

                // cancel all ongoing jpeg jobs
                qcamera_hal3_jpeg_data_t *jpeg_job =
                    (qcamera_hal3_jpeg_data_t *)pme->m_ongoingJpegQ.dequeue();
                while (jpeg_job != NULL) {
                    pme->mJpegHandle.abort_job(jpeg_job->jobId);

                    pme->releaseJpegJobData(jpeg_job);
                    free(jpeg_job);

                    jpeg_job = (qcamera_hal3_jpeg_data_t *)pme->m_ongoingJpegQ.dequeue();
                }

                // destroy jpeg encoding session
                if ( 0 < pme->mJpegSessionId ) {
                    pme->mJpegHandle.destroy_session(pme->mJpegSessionId);
                    pme->mJpegSessionId = 0;
                }

                needNewSess = TRUE;

                // flush ongoing postproc Queue
                pme->m_ongoingPPQ.flush();

                // flush input jpeg Queue
                pme->m_inputJpegQ.flush();

                // flush input Postproc Queue
                pme->m_inputPPQ.flush();

                // flush framework input Postproc Queue
                pme->m_inputFWKPPQ.flush();

                pme->m_inputMetaQ.flush();

                // signal cmd is completed
                cam_sem_post(&cmdThread->sync_sem);
            }
            break;
        case CAMERA_CMD_TYPE_DO_NEXT_JOB:
            {
                LOGH("Do next job, active is %d", is_active);
                /* needNewSess is set to TRUE as postproc is not re-STARTed
                 * anymore for every captureRequest */
                needNewSess = TRUE;
                if (is_active == TRUE) {
                    // check if there is any ongoing jpeg jobs
                    if (pme->m_ongoingJpegQ.isEmpty()) {
                        LOGD("ongoing jpeg queue is empty so doing the jpeg job");
                        // no ongoing jpeg job, we are fine to send jpeg encoding job
                        qcamera_hal3_jpeg_data_t *jpeg_job =
                            (qcamera_hal3_jpeg_data_t *)pme->m_inputJpegQ.dequeue();

                        if (NULL != jpeg_job) {
                            // add into ongoing jpeg job Q
                            pme->m_ongoingJpegQ.enqueue((void *)jpeg_job);

                            if (jpeg_job->fwk_frame) {
                                ret = pme->encodeFWKData(jpeg_job, needNewSess);
                            } else {
                                ret = pme->encodeData(jpeg_job, needNewSess);
                            }
                            if (NO_ERROR != ret) {
                                // dequeue the last one
                                pme->m_ongoingJpegQ.dequeue(false);

                                pme->releaseJpegJobData(jpeg_job);
                                free(jpeg_job);
                            }
                        }
                    }

                    // check if there are any framework pp jobs
                    if (!pme->m_inputFWKPPQ.isEmpty()) {
                        qcamera_fwk_input_pp_data_t *fwk_frame =
                                (qcamera_fwk_input_pp_data_t *) pme->m_inputFWKPPQ.dequeue();
                        if (NULL != fwk_frame) {
                            qcamera_hal3_pp_data_t *pp_job =
                                    (qcamera_hal3_pp_data_t *)malloc(sizeof(qcamera_hal3_pp_data_t));
                            jpeg_settings_t *jpeg_settings =
                                    (jpeg_settings_t *)pme->m_jpegSettingsQ.dequeue();
                            if (pp_job != NULL) {
                                memset(pp_job, 0, sizeof(qcamera_hal3_pp_data_t));
                                pp_job->jpeg_settings = jpeg_settings;
                                if (pme->m_pReprocChannel != NULL) {
                                    if (NO_ERROR != pme->m_pReprocChannel->overrideFwkMetadata(fwk_frame)) {
                                        LOGE("Failed to extract output crop");
                                    }
                                    // add into ongoing PP job Q
                                    pp_job->fwk_src_frame = fwk_frame;
                                    pme->m_ongoingPPQ.enqueue((void *)pp_job);
                                    ret = pme->m_pReprocChannel->doReprocessOffline(fwk_frame);
                                    if (NO_ERROR != ret) {
                                        // remove from ongoing PP job Q
                                        pme->m_ongoingPPQ.dequeue(false);
                                    }
                                } else {
                                    LOGE("Reprocess channel is NULL");
                                    ret = -1;
                                }
                            } else {
                                LOGE("no mem for qcamera_hal3_pp_data_t");
                                ret = -1;
                            }

                            if (0 != ret) {
                                // free pp_job
                                if (pp_job != NULL) {
                                    free(pp_job);
                                }
                                // free frame
                                if (fwk_frame != NULL) {
                                    free(fwk_frame);
                                }
                            }
                        }
                    }

                    LOGH("dequeuing pp frame");
                    pthread_mutex_lock(&pme->mReprocJobLock);
                    if(!pme->m_inputPPQ.isEmpty() && !pme->m_inputMetaQ.isEmpty()) {
                        qcamera_hal3_pp_buffer_t *pp_buffer =
                            (qcamera_hal3_pp_buffer_t *)pme->m_inputPPQ.dequeue();
                        meta_buffer =
                            (mm_camera_super_buf_t *)pme->m_inputMetaQ.dequeue();
                        jpeg_settings_t *jpeg_settings =
                           (jpeg_settings_t *)pme->m_jpegSettingsQ.dequeue();
                        pthread_mutex_unlock(&pme->mReprocJobLock);
                        qcamera_hal3_pp_data_t *pp_job =
                            (qcamera_hal3_pp_data_t *)malloc(sizeof(qcamera_hal3_pp_data_t));
                        if (pp_job == NULL) {
                            LOGE("no mem for qcamera_hal3_pp_data_t");
                            ret = -1;
                        } else if (meta_buffer == NULL) {
                            LOGE("failed to dequeue from m_inputMetaQ");
                            ret = -1;
                        } else if (pp_buffer == NULL) {
                            LOGE("failed to dequeue from m_inputPPQ");
                            ret = -1;
                        } else if (pp_buffer != NULL){
                            memset(pp_job, 0, sizeof(qcamera_hal3_pp_data_t));
                            pp_job->src_frame = pp_buffer->input;
                            pp_job->src_metadata = meta_buffer;
                            if (meta_buffer->bufs[0] != NULL) {
                                pp_job->metadata = (metadata_buffer_t *)
                                        meta_buffer->bufs[0]->buffer;
                            }
                            pp_job->jpeg_settings = jpeg_settings;
                            pme->m_ongoingPPQ.enqueue((void *)pp_job);
                            if (pme->m_pReprocChannel != NULL) {
                                mm_camera_buf_def_t *meta_buffer_arg = NULL;
                                meta_buffer_arg = meta_buffer->bufs[0];
                                qcamera_fwk_input_pp_data_t fwk_frame;
                                memset(&fwk_frame, 0, sizeof(qcamera_fwk_input_pp_data_t));
                                fwk_frame.frameNumber = pp_buffer->frameNumber;
                                ret = pme->m_pReprocChannel->overrideMetadata(
                                        pp_buffer, meta_buffer_arg,
                                        pp_job->jpeg_settings,
                                        fwk_frame);
                                if (NO_ERROR == ret) {
                                    // add into ongoing PP job Q
                                    ret = pme->m_pReprocChannel->doReprocessOffline(
                                            &fwk_frame, true);
                                    if (NO_ERROR != ret) {
                                        // remove from ongoing PP job Q
                                        pme->m_ongoingPPQ.dequeue(false);
                                    }
                                }
                            } else {
                                LOGE("No reprocess. Calling processPPData directly");
                                ret = pme->processPPData(pp_buffer->input);
                            }
                        }

                        if (0 != ret) {
                            // free pp_job
                            if (pp_job != NULL) {
                                free(pp_job);
                            }
                            // free frame
                            if (pp_buffer != NULL) {
                                if (pp_buffer->input) {
                                    pme->releaseSuperBuf(pp_buffer->input);
                                    free(pp_buffer->input);
                                }
                                free(pp_buffer);
                            }
                            //free metadata
                            if (NULL != meta_buffer) {
                                pme->m_parent->metadataBufDone(meta_buffer);
                                free(meta_buffer);
                            }
                        } else {
                            if (pp_buffer != NULL) {
                                free(pp_buffer);
                            }
                        }
                    } else {
                        pthread_mutex_unlock(&pme->mReprocJobLock);
                    }
                } else {
                    // not active, simply return buf and do no op
                    qcamera_hal3_jpeg_data_t *jpeg_job =
                        (qcamera_hal3_jpeg_data_t *)pme->m_inputJpegQ.dequeue();
                    if (NULL != jpeg_job) {
                        free(jpeg_job);
                    }

                    qcamera_hal3_pp_buffer_t* pp_buf =
                            (qcamera_hal3_pp_buffer_t *)pme->m_inputPPQ.dequeue();
                    if (NULL != pp_buf) {
                        if (pp_buf->input) {
                            pme->releaseSuperBuf(pp_buf->input);
                            free(pp_buf->input);
                            pp_buf->input = NULL;
                        }
                        free(pp_buf);
                    }
                    mm_camera_super_buf_t *metadata = (mm_camera_super_buf_t *)pme->m_inputMetaQ.dequeue();
                    if (metadata != NULL) {
                        pme->m_parent->metadataBufDone(metadata);
                        free(metadata);
                    }
                    qcamera_fwk_input_pp_data_t *fwk_frame =
                            (qcamera_fwk_input_pp_data_t *) pme->m_inputFWKPPQ.dequeue();
                    if (NULL != fwk_frame) {
                        free(fwk_frame);
                    }
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
    LOGD("X");
    return NULL;
}

/* EXIF related helper methods */

/*===========================================================================
 * FUNCTION   : getRational
 *
 * DESCRIPTION: compose rational struct
 *
 * PARAMETERS :
 *   @rat     : ptr to struct to store rational info
 *   @num     :num of the rational
 *   @denom   : denom of the rational
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t getRational(rat_t *rat, int num, int denom)
{
    if ((0 > num) || (0 >= denom)) {
        LOGE("Negative values");
        return BAD_VALUE;
    }
    if (NULL == rat) {
        LOGE("NULL rat input");
        return BAD_VALUE;
    }
    rat->num = (uint32_t)num;
    rat->denom = (uint32_t)denom;
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : parseGPSCoordinate
 *
 * DESCRIPTION: parse GPS coordinate string
 *
 * PARAMETERS :
 *   @coord_str : [input] coordinate string
 *   @coord     : [output]  ptr to struct to store coordinate
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int parseGPSCoordinate(const char *coord_str, rat_t* coord)
{
    if(coord == NULL) {
        LOGE("error, invalid argument coord == NULL");
        return BAD_VALUE;
    }
    double degF = atof(coord_str);
    if (degF < 0) {
        degF = -degF;
    }
    double minF = (degF - (int) degF) * 60;
    double secF = (minF - (int) minF) * 60;

    getRational(&coord[0], (int)degF, 1);
    getRational(&coord[1], (int)minF, 1);
    getRational(&coord[2], (int)(secF * 10000), 10000);
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : getExifDateTime
 *
 * DESCRIPTION: query exif date time
 *
 * PARAMETERS :
 *   @dateTime   : string to store exif date time
 *   @subsecTime : string to store exif subsec time
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t getExifDateTime(String8 &dateTime, String8 &subsecTime)
{
    int32_t ret = NO_ERROR;

    //get time and date from system
    struct timeval tv;
    struct tm timeinfo_data;

    int res = gettimeofday(&tv, NULL);
    if (0 == res) {
        struct tm *timeinfo = localtime_r(&tv.tv_sec, &timeinfo_data);
        if (NULL != timeinfo) {
            //Write datetime according to EXIF Spec
            //"YYYY:MM:DD HH:MM:SS" (20 chars including \0)
            dateTime = String8::format("%04d:%02d:%02d %02d:%02d:%02d",
                    timeinfo->tm_year + 1900, timeinfo->tm_mon + 1,
                    timeinfo->tm_mday, timeinfo->tm_hour,
                    timeinfo->tm_min, timeinfo->tm_sec);
            //Write subsec according to EXIF Sepc
            subsecTime = String8::format("%06ld", tv.tv_usec);
        } else {
            LOGE("localtime_r() error");
            ret = UNKNOWN_ERROR;
        }
    } else if (-1 == res) {
        LOGE("gettimeofday() error: %s", strerror(errno));
        ret = UNKNOWN_ERROR;
    } else {
        LOGE("gettimeofday() unexpected return code: %d", res);
        ret = UNKNOWN_ERROR;
    }

    return ret;
}

/*===========================================================================
 * FUNCTION   : getExifFocalLength
 *
 * DESCRIPTION: get exif focal length
 *
 * PARAMETERS :
 *   @focalLength : ptr to rational struct to store focal length
 *   @value       : focal length value
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t getExifFocalLength(rat_t *focalLength, float value)
{
    int focalLengthValue =
        (int)(value * FOCAL_LENGTH_DECIMAL_PRECISION);
    return getRational(focalLength, focalLengthValue, FOCAL_LENGTH_DECIMAL_PRECISION);
}

/*===========================================================================
  * FUNCTION   : getExifExpTimeInfo
  *
  * DESCRIPTION: get exif exposure time information
  *
  * PARAMETERS :
  *   @expoTimeInfo     : rational exposure time value
  *   @value            : exposure time value
  * RETURN     : nt32_t type of status
  *              NO_ERROR  -- success
  *              none-zero failure code
  *==========================================================================*/
int32_t getExifExpTimeInfo(rat_t *expoTimeInfo, int64_t value)
{

    int64_t cal_exposureTime;
    if (value != 0)
        cal_exposureTime = value;
    else
        cal_exposureTime = 60;

    return getRational(expoTimeInfo, 1, (int)cal_exposureTime);
}

/*===========================================================================
 * FUNCTION   : getExifGpsProcessingMethod
 *
 * DESCRIPTION: get GPS processing method
 *
 * PARAMETERS :
 *   @gpsProcessingMethod : string to store GPS process method
 *   @count               : length of the string
 *   @value               : the value of the processing method
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t getExifGpsProcessingMethod(char *gpsProcessingMethod,
        uint32_t &count, char* value)
{
    if(value != NULL) {
        memcpy(gpsProcessingMethod, ExifAsciiPrefix, EXIF_ASCII_PREFIX_SIZE);
        count = EXIF_ASCII_PREFIX_SIZE;
        strlcpy(gpsProcessingMethod + EXIF_ASCII_PREFIX_SIZE,
                value,
                GPS_PROCESSING_METHOD_SIZE);
        count += (uint32_t)strlen(value);
        gpsProcessingMethod[count++] = '\0'; // increase 1 for the last NULL char
        return NO_ERROR;
    } else {
        return BAD_VALUE;
    }
}

/*===========================================================================
 * FUNCTION   : getExifLatitude
 *
 * DESCRIPTION: get exif latitude
 *
 * PARAMETERS :
 *   @latitude : ptr to rational struct to store latitude info
 *   @latRef   : character to indicate latitude reference
 *   @value    : value of the latitude
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t getExifLatitude(rat_t *latitude, char *latRef, double value)
{
    char str[30];
    snprintf(str, sizeof(str), "%f", value);
    if(str[0] != '\0') {
        parseGPSCoordinate(str, latitude);

        //set Latitude Ref
        float latitudeValue = strtof(str, 0);
        if(latitudeValue < 0.0f) {
            latRef[0] = 'S';
        } else {
            latRef[0] = 'N';
        }
        latRef[1] = '\0';
        return NO_ERROR;
    }else{
        return BAD_VALUE;
    }
}

/*===========================================================================
 * FUNCTION   : getExifLongitude
 *
 * DESCRIPTION: get exif longitude
 *
 * PARAMETERS :
 *   @longitude : ptr to rational struct to store longitude info
 *   @lonRef    : character to indicate longitude reference
 *   @value     : value of the longitude
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t getExifLongitude(rat_t *longitude, char *lonRef, double value)
{
    char str[30];
    snprintf(str, sizeof(str), "%f", value);
    if(str[0] != '\0') {
        parseGPSCoordinate(str, longitude);

        //set Longitude Ref
        float longitudeValue = strtof(str, 0);
        if(longitudeValue < 0.0f) {
            lonRef[0] = 'W';
        } else {
            lonRef[0] = 'E';
        }
        lonRef[1] = '\0';
        return NO_ERROR;
    }else{
        return BAD_VALUE;
    }
}

/*===========================================================================
 * FUNCTION   : getExifAltitude
 *
 * DESCRIPTION: get exif altitude
 *
 * PARAMETERS :
 *   @altitude : ptr to rational struct to store altitude info
 *   @altRef   : character to indicate altitude reference
 *   @argValue : altitude value
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t getExifAltitude(rat_t *altitude, char *altRef, double argValue)
{
    char str[30];
    snprintf(str, sizeof(str), "%f", argValue);
    if (str[0] != '\0') {
        double value = atof(str);
        *altRef = 0;
        if(value < 0){
            *altRef = 1;
            value = -value;
        }
        return getRational(altitude, (int)(value * 1000), 1000);
    } else {
        return BAD_VALUE;
    }
}

/*===========================================================================
 * FUNCTION   : getExifGpsDateTimeStamp
 *
 * DESCRIPTION: get exif GPS date time stamp
 *
 * PARAMETERS :
 *   @gpsDateStamp : GPS date time stamp string
 *   @bufLen       : length of the string
 *   @gpsTimeStamp : ptr to rational struct to store time stamp info
 *   @value        : timestamp value
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t getExifGpsDateTimeStamp(char *gpsDateStamp, uint32_t bufLen,
        rat_t *gpsTimeStamp, int64_t value)
{
    char str[30];
    snprintf(str, sizeof(str), "%lld", (long long int)value);
    if(str[0] != '\0') {
        time_t unixTime = (time_t)atol(str);
        struct tm *UTCTimestamp = gmtime(&unixTime);
        if (UTCTimestamp != NULL && gpsDateStamp != NULL
                && gpsTimeStamp != NULL) {
            strftime(gpsDateStamp, bufLen, "%Y:%m:%d", UTCTimestamp);

            getRational(&gpsTimeStamp[0], UTCTimestamp->tm_hour, 1);
            getRational(&gpsTimeStamp[1], UTCTimestamp->tm_min, 1);
            getRational(&gpsTimeStamp[2], UTCTimestamp->tm_sec, 1);
            return NO_ERROR;
        } else {
            LOGE("Could not get the timestamp");
            return BAD_VALUE;
        }
    } else {
        return BAD_VALUE;
    }
}

/*===========================================================================
 * FUNCTION   : getExifExposureValue
 *
 * DESCRIPTION: get exif GPS date time stamp
 *
 * PARAMETERS :
 *   @exposure_val        : rational exposure value
 *   @exposure_comp       : exposure compensation
 *   @step                : exposure step
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t getExifExposureValue(srat_t* exposure_val, int32_t exposure_comp,
        cam_rational_type_t step)
{
    exposure_val->num = exposure_comp * step.numerator;
    exposure_val->denom = step.denominator;
    return 0;
}

/*===========================================================================
 * FUNCTION   : getExifData
 *
 * DESCRIPTION: get exif data to be passed into jpeg encoding
 *
 * PARAMETERS :
 * @metadata      : metadata of the encoding request
 * @jpeg_settings : jpeg_settings for encoding
 * @needJpegExifRotation: check if rotation need to added in EXIF
 *
 * RETURN     : exif data from user setting and GPS
 *==========================================================================*/
QCamera3Exif *QCamera3PostProcessor::getExifData(metadata_buffer_t *metadata,
        jpeg_settings_t *jpeg_settings, bool needJpegExifRotation)
{
    QCamera3Exif *exif = new QCamera3Exif();
    if (exif == NULL) {
        LOGE("No memory for QCamera3Exif");
        return NULL;
    }
    QCamera3HardwareInterface* hal_obj = NULL;
    if (m_parent != NULL) {
        hal_obj = (QCamera3HardwareInterface*)m_parent->mUserData;
    } else {
        LOGE("m_parent is NULL, Error");
        return NULL;
    }

    int32_t rc = NO_ERROR;
    uint32_t count = 0;

    // add exif entries
    String8 dateTime;
    String8 subsecTime;
    rc = getExifDateTime(dateTime, subsecTime);
    if (rc == NO_ERROR) {
        exif->addEntry(EXIFTAGID_DATE_TIME, EXIF_ASCII,
                (uint32_t)(dateTime.length() + 1), (void *)dateTime.string());
        exif->addEntry(EXIFTAGID_EXIF_DATE_TIME_ORIGINAL, EXIF_ASCII,
                (uint32_t)(dateTime.length() + 1), (void *)dateTime.string());
        exif->addEntry(EXIFTAGID_EXIF_DATE_TIME_DIGITIZED, EXIF_ASCII,
                (uint32_t)(dateTime.length() + 1), (void *)dateTime.string());
        exif->addEntry(EXIFTAGID_SUBSEC_TIME, EXIF_ASCII,
                (uint32_t)(subsecTime.length() + 1), (void *)subsecTime.string());
        exif->addEntry(EXIFTAGID_SUBSEC_TIME_ORIGINAL, EXIF_ASCII,
                (uint32_t)(subsecTime.length() + 1), (void *)subsecTime.string());
        exif->addEntry(EXIFTAGID_SUBSEC_TIME_DIGITIZED, EXIF_ASCII,
                (uint32_t)(subsecTime.length() + 1), (void *)subsecTime.string());
    } else {
        LOGW("getExifDateTime failed");
    }


    if (metadata != NULL) {
        IF_META_AVAILABLE(float, focal_length, CAM_INTF_META_LENS_FOCAL_LENGTH, metadata) {
            rat_t focalLength;
            rc = getExifFocalLength(&focalLength, *focal_length);
            if (rc == NO_ERROR) {
                exif->addEntry(EXIFTAGID_FOCAL_LENGTH,
                        EXIF_RATIONAL,
                        1,
                        (void *)&(focalLength));
            } else {
                LOGW("getExifFocalLength failed");
            }
        }

        IF_META_AVAILABLE(int32_t, isoSpeed, CAM_INTF_META_SENSOR_SENSITIVITY, metadata) {
            int16_t fwk_isoSpeed = (int16_t) *isoSpeed;
            exif->addEntry(EXIFTAGID_ISO_SPEED_RATING, EXIF_SHORT, 1, (void *) &(fwk_isoSpeed));
        }


        IF_META_AVAILABLE(int64_t, sensor_exposure_time,
                CAM_INTF_META_SENSOR_EXPOSURE_TIME, metadata) {
            rat_t sensorExpTime;
            rc = getExifExpTimeInfo(&sensorExpTime, *sensor_exposure_time);
            if (rc == NO_ERROR){
                exif->addEntry(EXIFTAGID_EXPOSURE_TIME,
                        EXIF_RATIONAL,
                        1,
                        (void *)&(sensorExpTime));
            } else {
                LOGW("getExifExpTimeInfo failed");
            }
        }

        char* jpeg_gps_processing_method = jpeg_settings->gps_processing_method;
        if (strlen(jpeg_gps_processing_method) > 0) {
            char gpsProcessingMethod[EXIF_ASCII_PREFIX_SIZE +
                    GPS_PROCESSING_METHOD_SIZE];
            count = 0;
            rc = getExifGpsProcessingMethod(gpsProcessingMethod,
                    count,
                    jpeg_gps_processing_method);
            if(rc == NO_ERROR) {
                exif->addEntry(EXIFTAGID_GPS_PROCESSINGMETHOD,
                        EXIF_ASCII,
                        count,
                        (void *)gpsProcessingMethod);
            } else {
                LOGW("getExifGpsProcessingMethod failed");
            }
        }

        if (jpeg_settings->gps_coordinates_valid) {

            //latitude
            rat_t latitude[3];
            char latRef[2];
            rc = getExifLatitude(latitude, latRef,
                    jpeg_settings->gps_coordinates[0]);
            if(rc == NO_ERROR) {
                exif->addEntry(EXIFTAGID_GPS_LATITUDE,
                        EXIF_RATIONAL,
                        3,
                        (void *)latitude);
                exif->addEntry(EXIFTAGID_GPS_LATITUDE_REF,
                        EXIF_ASCII,
                        2,
                        (void *)latRef);
            } else {
                LOGW("getExifLatitude failed");
            }

            //longitude
            rat_t longitude[3];
            char lonRef[2];
            rc = getExifLongitude(longitude, lonRef,
                    jpeg_settings->gps_coordinates[1]);
            if(rc == NO_ERROR) {
                exif->addEntry(EXIFTAGID_GPS_LONGITUDE,
                        EXIF_RATIONAL,
                        3,
                        (void *)longitude);

                exif->addEntry(EXIFTAGID_GPS_LONGITUDE_REF,
                        EXIF_ASCII,
                        2,
                        (void *)lonRef);
            } else {
                LOGW("getExifLongitude failed");
            }

            //altitude
            rat_t altitude;
            char altRef;
            rc = getExifAltitude(&altitude, &altRef,
                    jpeg_settings->gps_coordinates[2]);
            if(rc == NO_ERROR) {
                exif->addEntry(EXIFTAGID_GPS_ALTITUDE,
                        EXIF_RATIONAL,
                        1,
                        (void *)&(altitude));

                exif->addEntry(EXIFTAGID_GPS_ALTITUDE_REF,
                        EXIF_BYTE,
                        1,
                        (void *)&altRef);
            } else {
                LOGW("getExifAltitude failed");
            }
        }

        if (jpeg_settings->gps_timestamp_valid) {

            char gpsDateStamp[20];
            rat_t gpsTimeStamp[3];
            rc = getExifGpsDateTimeStamp(gpsDateStamp, 20, gpsTimeStamp,
                    jpeg_settings->gps_timestamp);
            if(rc == NO_ERROR) {
                exif->addEntry(EXIFTAGID_GPS_DATESTAMP, EXIF_ASCII,
                        (uint32_t)(strlen(gpsDateStamp) + 1),
                        (void *)gpsDateStamp);

                exif->addEntry(EXIFTAGID_GPS_TIMESTAMP,
                        EXIF_RATIONAL,
                        3,
                        (void *)gpsTimeStamp);
            } else {
                LOGW("getExifGpsDataTimeStamp failed");
            }
        }

        IF_META_AVAILABLE(int32_t, exposure_comp, CAM_INTF_PARM_EXPOSURE_COMPENSATION, metadata) {
            IF_META_AVAILABLE(cam_rational_type_t, comp_step, CAM_INTF_PARM_EV_STEP, metadata) {
                srat_t exposure_val;
                rc = getExifExposureValue(&exposure_val, *exposure_comp, *comp_step);
                if(rc == NO_ERROR) {
                    exif->addEntry(EXIFTAGID_EXPOSURE_BIAS_VALUE,
                            EXIF_SRATIONAL,
                            1,
                            (void *)(&exposure_val));
                } else {
                    LOGW("getExifExposureValue failed ");
                }
            }
        }
    } else {
        LOGW("no metadata provided ");
    }

#ifdef ENABLE_MODEL_INFO_EXIF

    char value[PROPERTY_VALUE_MAX];
    if (property_get("ro.product.manufacturer", value, "QCOM-AA") > 0) {
        exif->addEntry(EXIFTAGID_MAKE, EXIF_ASCII,
                (uint32_t)(strlen(value) + 1), (void *)value);
    } else {
        LOGW("getExifMaker failed");
    }

    if (property_get("ro.product.model", value, "QCAM-AA") > 0) {
        exif->addEntry(EXIFTAGID_MODEL, EXIF_ASCII,
                (uint32_t)(strlen(value) + 1), (void *)value);
    } else {
        LOGW("getExifModel failed");
    }

    if (property_get("ro.build.description", value, "QCAM-AA") > 0) {
        exif->addEntry(EXIFTAGID_SOFTWARE, EXIF_ASCII,
                (uint32_t)(strlen(value) + 1), (void *)value);
    } else {
        LOGW("getExifSoftware failed");
    }

#endif

    if (jpeg_settings->image_desc_valid) {
        if (exif->addEntry(EXIFTAGID_IMAGE_DESCRIPTION, EXIF_ASCII,
                strlen(jpeg_settings->image_desc)+1,
                (void *)jpeg_settings->image_desc)) {
            LOGW("Adding IMAGE_DESCRIPTION tag failed");
        }
    }

    if (needJpegExifRotation) {
        int16_t orientation;
        switch (jpeg_settings->jpeg_orientation) {
            case 0:
                orientation = 1;
                break;
            case 90:
                orientation = 6;
                break;
            case 180:
                orientation = 3;
                break;
            case 270:
                orientation = 8;
                break;
            default:
                orientation = 1;
                break;
        }
        exif->addEntry(EXIFTAGID_ORIENTATION,
                       EXIF_SHORT,
                       1,
                       (void *)&orientation);
        exif->addEntry(EXIFTAGID_TN_ORIENTATION,
                       EXIF_SHORT,
                       1,
                       (void *)&orientation);

    }

    return exif;
}

/*===========================================================================
 * FUNCTION   : QCamera3Exif
 *
 * DESCRIPTION: constructor of QCamera3Exif
 *
 * PARAMETERS : None
 *
 * RETURN     : None
 *==========================================================================*/
QCamera3Exif::QCamera3Exif()
    : m_nNumEntries(0)
{
    memset(m_Entries, 0, sizeof(m_Entries));
}

/*===========================================================================
 * FUNCTION   : ~QCamera3Exif
 *
 * DESCRIPTION: deconstructor of QCamera3Exif. Will release internal memory ptr.
 *
 * PARAMETERS : None
 *
 * RETURN     : None
 *==========================================================================*/
QCamera3Exif::~QCamera3Exif()
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
            default:
                LOGW("Error, Unknown type");
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
int32_t QCamera3Exif::addEntry(exif_tag_id_t tagid,
                              exif_tag_type_t type,
                              uint32_t count,
                              void *data)
{
    int32_t rc = NO_ERROR;
    if(m_nNumEntries >= MAX_HAL3_EXIF_TABLE_ENTRIES) {
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
                    m_Entries[m_nNumEntries].tag_entry.data._byte =
                        *(uint8_t *)data;
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
                    uint16_t *values =
                        (uint16_t *)malloc(count * sizeof(uint16_t));
                    if (values == NULL) {
                        LOGE("No memory for short array");
                        rc = NO_MEMORY;
                    } else {
                        memcpy(values, exif_data, count * sizeof(uint16_t));
                        m_Entries[m_nNumEntries].tag_entry.data._shorts = values;
                    }
                } else {
                    m_Entries[m_nNumEntries].tag_entry.data._short =
                        *(uint16_t *)data;
                }
            }
            break;
        case EXIF_LONG:
            {
                uint32_t *exif_data = (uint32_t *)data;
                if (count > 1) {
                    uint32_t *values =
                        (uint32_t *)malloc(count * sizeof(uint32_t));
                    if (values == NULL) {
                        LOGE("No memory for long array");
                        rc = NO_MEMORY;
                    } else {
                        memcpy(values, exif_data, count * sizeof(uint32_t));
                        m_Entries[m_nNumEntries].tag_entry.data._longs = values;
                    }
                } else {
                    m_Entries[m_nNumEntries].tag_entry.data._long =
                        *(uint32_t *)data;
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
                    m_Entries[m_nNumEntries].tag_entry.data._rat =
                        *(rat_t *)data;
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
                int32_t *exif_data = (int32_t *)data;
                if (count > 1) {
                    int32_t *values =
                        (int32_t *)malloc(count * sizeof(int32_t));
                    if (values == NULL) {
                        LOGE("No memory for signed long array");
                        rc = NO_MEMORY;
                    } else {
                        memcpy(values, exif_data, count * sizeof(int32_t));
                        m_Entries[m_nNumEntries].tag_entry.data._slongs =values;
                    }
                } else {
                    m_Entries[m_nNumEntries].tag_entry.data._slong =
                        *(int32_t *)data;
                }
            }
            break;
        case EXIF_SRATIONAL:
            {
                srat_t *exif_data = (srat_t *)data;
                if (count > 1) {
                    srat_t *values = (srat_t *)malloc(count * sizeof(srat_t));
                    if (values == NULL) {
                        LOGE("No memory for sign rational array");
                        rc = NO_MEMORY;
                    } else {
                        memcpy(values, exif_data, count * sizeof(srat_t));
                        m_Entries[m_nNumEntries].tag_entry.data._srats = values;
                    }
                } else {
                    m_Entries[m_nNumEntries].tag_entry.data._srat =
                        *(srat_t *)data;
                }
            }
            break;
        default:
            LOGE("Error, Unknown type");
            break;
    }

    // Increase number of entries
    m_nNumEntries++;
    return rc;
}

}; // namespace qcamera
