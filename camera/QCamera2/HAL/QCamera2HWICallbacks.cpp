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

#define LOG_TAG "QCamera2HWI"

// System dependencies
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#define STAT_H <SYSTEM_HEADER_PREFIX/stat.h>
#include STAT_H
#include <utils/Errors.h>

// Camera dependencies
#include "QCamera2HWI.h"
#include "QCameraTrace.h"

extern "C" {
#include "mm_camera_dbg.h"
}

namespace qcamera {

/*===========================================================================
 * FUNCTION   : zsl_channel_cb
 *
 * DESCRIPTION: helper function to handle ZSL superbuf callback directly from
 *              mm-camera-interface
 *
 * PARAMETERS :
 *   @recvd_frame : received super buffer
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : recvd_frame will be released after this call by caller, so if
 *             async operation needed for recvd_frame, it's our responsibility
 *             to save a copy for this variable to be used later.
 *==========================================================================*/
void QCamera2HardwareInterface::zsl_channel_cb(mm_camera_super_buf_t *recvd_frame,
                                               void *userdata)
{
    ATRACE_CALL();
    LOGH("[KPI Perf]: E");
    char value[PROPERTY_VALUE_MAX];
    bool dump_raw = false;
    bool log_matching = false;
    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;
    if (pme == NULL ||
        pme->mCameraHandle == NULL ||
        pme->mCameraHandle->camera_handle != recvd_frame->camera_handle){
       LOGE("camera obj not valid");
       return;
    }

    QCameraChannel *pChannel = pme->m_channels[QCAMERA_CH_TYPE_ZSL];
    if (pChannel == NULL ||
        pChannel->getMyHandle() != recvd_frame->ch_id) {
        LOGE("ZSL channel doesn't exist, return here");
        return;
    }

    if(pme->mParameters.isSceneSelectionEnabled() &&
            !pme->m_stateMachine.isCaptureRunning()) {
        pme->selectScene(pChannel, recvd_frame);
        pChannel->bufDone(recvd_frame);
        return;
    }

    LOGD("Frame CB Unlock : %d, is AEC Locked: %d",
           recvd_frame->bUnlockAEC, pme->m_bLedAfAecLock);
    if(recvd_frame->bUnlockAEC && pme->m_bLedAfAecLock) {
        qcamera_sm_internal_evt_payload_t *payload =
                (qcamera_sm_internal_evt_payload_t *)malloc(
                        sizeof(qcamera_sm_internal_evt_payload_t));
        if (NULL != payload) {
            memset(payload, 0, sizeof(qcamera_sm_internal_evt_payload_t));
            payload->evt_type = QCAMERA_INTERNAL_EVT_RETRO_AEC_UNLOCK;
            int32_t rc = pme->processEvt(QCAMERA_SM_EVT_EVT_INTERNAL, payload);
            if (rc != NO_ERROR) {
                LOGE("processEvt for retro AEC unlock failed");
                free(payload);
                payload = NULL;
            }
        } else {
            LOGE("No memory for retro AEC event");
        }
    }

    // Check if retro-active frames are completed and camera is
    // ready to go ahead with LED estimation for regular frames
    if (recvd_frame->bReadyForPrepareSnapshot) {
        // Send an event
        LOGD("Ready for Prepare Snapshot, signal ");
        qcamera_sm_internal_evt_payload_t *payload =
                    (qcamera_sm_internal_evt_payload_t *)malloc(
                    sizeof(qcamera_sm_internal_evt_payload_t));
        if (NULL != payload) {
            memset(payload, 0, sizeof(qcamera_sm_internal_evt_payload_t));
            payload->evt_type = QCAMERA_INTERNAL_EVT_READY_FOR_SNAPSHOT;
            int32_t rc = pme->processEvt(QCAMERA_SM_EVT_EVT_INTERNAL, payload);
            if (rc != NO_ERROR) {
                LOGW("processEvt Ready for Snaphot failed");
                free(payload);
                payload = NULL;
            }
        } else {
            LOGE("No memory for prepare signal event detect"
                    " qcamera_sm_internal_evt_payload_t");
        }
    }

    /* indicate the parent that capture is done */
    pme->captureDone();

    // save a copy for the superbuf
    mm_camera_super_buf_t* frame =
               (mm_camera_super_buf_t *)malloc(sizeof(mm_camera_super_buf_t));
    if (frame == NULL) {
        LOGE("Error allocating memory to save received_frame structure.");
        pChannel->bufDone(recvd_frame);
        return;
    }
    *frame = *recvd_frame;

    if (recvd_frame->num_bufs > 0) {
        LOGI("[KPI Perf]: superbuf frame_idx %d",
            recvd_frame->bufs[0]->frame_idx);
    }

    // DUMP RAW if available
    property_get("persist.camera.zsl_raw", value, "0");
    dump_raw = atoi(value) > 0 ? true : false;
    if (dump_raw) {
        for (uint32_t i = 0; i < recvd_frame->num_bufs; i++) {
            if (recvd_frame->bufs[i]->stream_type == CAM_STREAM_TYPE_RAW) {
                mm_camera_buf_def_t * raw_frame = recvd_frame->bufs[i];
                QCameraStream *pStream = pChannel->getStreamByHandle(raw_frame->stream_id);
                if (NULL != pStream) {
                    pme->dumpFrameToFile(pStream, raw_frame, QCAMERA_DUMP_FRM_RAW);
                }
                break;
            }
        }
    }

    for (uint32_t i = 0; i < recvd_frame->num_bufs; i++) {
        if (recvd_frame->bufs[i]->stream_type == CAM_STREAM_TYPE_SNAPSHOT) {
            mm_camera_buf_def_t * yuv_frame = recvd_frame->bufs[i];
            QCameraStream *pStream = pChannel->getStreamByHandle(yuv_frame->stream_id);
            if (NULL != pStream) {
                pme->dumpFrameToFile(pStream, yuv_frame, QCAMERA_DUMP_FRM_INPUT_REPROCESS);
            }
            break;
        }
    }
    //
    // whether need FD Metadata along with Snapshot frame in ZSL mode
    if(pme->needFDMetadata(QCAMERA_CH_TYPE_ZSL)){
        //Need Face Detection result for snapshot frames
        //Get the Meta Data frames
        mm_camera_buf_def_t *pMetaFrame = NULL;
        for (uint32_t i = 0; i < frame->num_bufs; i++) {
            QCameraStream *pStream = pChannel->getStreamByHandle(frame->bufs[i]->stream_id);
            if (pStream != NULL) {
                if (pStream->isTypeOf(CAM_STREAM_TYPE_METADATA)) {
                    pMetaFrame = frame->bufs[i]; //find the metadata
                    break;
                }
            }
        }

        if(pMetaFrame != NULL){
            metadata_buffer_t *pMetaData = (metadata_buffer_t *)pMetaFrame->buffer;
            //send the face detection info
            cam_faces_data_t faces_data;
            pme->fillFacesData(faces_data, pMetaData);
            //HARD CODE here before MCT can support
            faces_data.detection_data.fd_type = QCAMERA_FD_SNAPSHOT;

            qcamera_sm_internal_evt_payload_t *payload =
                (qcamera_sm_internal_evt_payload_t *)malloc(sizeof(qcamera_sm_internal_evt_payload_t));
            if (NULL != payload) {
                memset(payload, 0, sizeof(qcamera_sm_internal_evt_payload_t));
                payload->evt_type = QCAMERA_INTERNAL_EVT_FACE_DETECT_RESULT;
                payload->faces_data = faces_data;
                int32_t rc = pme->processEvt(QCAMERA_SM_EVT_EVT_INTERNAL, payload);
                if (rc != NO_ERROR) {
                    LOGW("processEvt face_detection_result failed");
                    free(payload);
                    payload = NULL;
                }
            } else {
                LOGE("No memory for face_detection_result qcamera_sm_internal_evt_payload_t");
            }
        }
    }

    property_get("persist.camera.dumpmetadata", value, "0");
    int32_t enabled = atoi(value);
    if (enabled) {
        mm_camera_buf_def_t *pMetaFrame = NULL;
        QCameraStream *pStream = NULL;
        for (uint32_t i = 0; i < frame->num_bufs; i++) {
            pStream = pChannel->getStreamByHandle(frame->bufs[i]->stream_id);
            if (pStream != NULL) {
                if (pStream->isTypeOf(CAM_STREAM_TYPE_METADATA)) {
                    pMetaFrame = frame->bufs[i];
                    if (pMetaFrame != NULL &&
                            ((metadata_buffer_t *)pMetaFrame->buffer)->is_tuning_params_valid) {
                        pme->dumpMetadataToFile(pStream, pMetaFrame, (char *) "ZSL_Snapshot");
                    }
                    break;
                }
            }
        }
    }

    property_get("persist.camera.zsl_matching", value, "0");
    log_matching = atoi(value) > 0 ? true : false;
    if (log_matching) {
        LOGH("ZSL super buffer contains:");
        QCameraStream *pStream = NULL;
        for (uint32_t i = 0; i < frame->num_bufs; i++) {
            pStream = pChannel->getStreamByHandle(frame->bufs[i]->stream_id);
            if (pStream != NULL ) {
                LOGH("Buffer with V4L index %d frame index %d of type %d Timestamp: %ld %ld ",
                        frame->bufs[i]->buf_idx,
                        frame->bufs[i]->frame_idx,
                        pStream->getMyType(),
                        frame->bufs[i]->ts.tv_sec,
                        frame->bufs[i]->ts.tv_nsec);
            }
        }
    }

    // Wait on Postproc initialization if needed
    // then send to postprocessor
    if ((NO_ERROR != pme->waitDeferredWork(pme->mReprocJob)) ||
            (NO_ERROR != pme->m_postprocessor.processData(frame))) {
        LOGE("Failed to trigger process data");
        pChannel->bufDone(recvd_frame);
        free(frame);
        frame = NULL;
        return;
    }

    LOGH("[KPI Perf]: X");
}

/*===========================================================================
 * FUNCTION   : selectScene
 *
 * DESCRIPTION: send a preview callback when a specific selected scene is applied
 *
 * PARAMETERS :
 *   @pChannel: Camera channel
 *   @frame   : Bundled super buffer
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::selectScene(QCameraChannel *pChannel,
        mm_camera_super_buf_t *frame)
{
    mm_camera_buf_def_t *pMetaFrame = NULL;
    QCameraStream *pStream = NULL;
    int32_t rc = NO_ERROR;

    if ((NULL == frame) || (NULL == pChannel)) {
        LOGE("Invalid scene select input");
        return BAD_VALUE;
    }

    cam_scene_mode_type selectedScene = mParameters.getSelectedScene();
    if (CAM_SCENE_MODE_MAX == selectedScene) {
        LOGL("No selected scene");
        return NO_ERROR;
    }

    for (uint32_t i = 0; i < frame->num_bufs; i++) {
        pStream = pChannel->getStreamByHandle(frame->bufs[i]->stream_id);
        if (pStream != NULL) {
            if (pStream->isTypeOf(CAM_STREAM_TYPE_METADATA)) {
                pMetaFrame = frame->bufs[i];
                break;
            }
        }
    }

    if (NULL == pMetaFrame) {
        LOGE("No metadata buffer found in scene select super buffer");
        return NO_INIT;
    }

    metadata_buffer_t *pMetaData = (metadata_buffer_t *)pMetaFrame->buffer;

    IF_META_AVAILABLE(cam_scene_mode_type, scene, CAM_INTF_META_CURRENT_SCENE, pMetaData) {
        if ((*scene == selectedScene) &&
                (mDataCb != NULL) &&
                (msgTypeEnabledWithLock(CAMERA_MSG_PREVIEW_FRAME) > 0)) {
            mm_camera_buf_def_t *preview_frame = NULL;
            for (uint32_t i = 0; i < frame->num_bufs; i++) {
                pStream = pChannel->getStreamByHandle(frame->bufs[i]->stream_id);
                if (pStream != NULL) {
                    if (pStream->isTypeOf(CAM_STREAM_TYPE_PREVIEW)) {
                        preview_frame = frame->bufs[i];
                        break;
                    }
                }
            }
            if (preview_frame) {
                QCameraGrallocMemory *memory = (QCameraGrallocMemory *)preview_frame->mem_info;
                uint32_t idx = preview_frame->buf_idx;
                rc = sendPreviewCallback(pStream, memory, idx);
                if (NO_ERROR != rc) {
                    LOGE("Error triggering scene select preview callback");
                } else {
                    mParameters.setSelectedScene(CAM_SCENE_MODE_MAX);
                }
            } else {
                LOGE("No preview buffer found in scene select super buffer");
                return NO_INIT;
            }
        }
    } else {
        LOGE("No current scene metadata!");
        rc = NO_INIT;
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : capture_channel_cb_routine
 *
 * DESCRIPTION: helper function to handle snapshot superbuf callback directly from
 *              mm-camera-interface
 *
 * PARAMETERS :
 *   @recvd_frame : received super buffer
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : recvd_frame will be released after this call by caller, so if
 *             async operation needed for recvd_frame, it's our responsibility
 *             to save a copy for this variable to be used later.
*==========================================================================*/
void QCamera2HardwareInterface::capture_channel_cb_routine(mm_camera_super_buf_t *recvd_frame,
                                                           void *userdata)
{
    KPI_ATRACE_CALL();
    char value[PROPERTY_VALUE_MAX];
    LOGH("[KPI Perf]: E PROFILE_YUV_CB_TO_HAL");
    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;
    if (pme == NULL ||
        pme->mCameraHandle == NULL ||
        pme->mCameraHandle->camera_handle != recvd_frame->camera_handle){
        LOGE("camera obj not valid");
        return;
    }

    QCameraChannel *pChannel = pme->m_channels[QCAMERA_CH_TYPE_CAPTURE];
    if (pChannel == NULL ||
        pChannel->getMyHandle() != recvd_frame->ch_id) {
        LOGE("Capture channel doesn't exist, return here");
        return;
    }

    // save a copy for the superbuf
    mm_camera_super_buf_t* frame =
               (mm_camera_super_buf_t *)malloc(sizeof(mm_camera_super_buf_t));
    if (frame == NULL) {
        LOGE("Error allocating memory to save received_frame structure.");
        pChannel->bufDone(recvd_frame);
        return;
    }
    *frame = *recvd_frame;

    if (recvd_frame->num_bufs > 0) {
        LOGI("[KPI Perf]: superbuf frame_idx %d",
                recvd_frame->bufs[0]->frame_idx);
    }

    for ( uint32_t i= 0 ; i < recvd_frame->num_bufs ; i++ ) {
        if ( recvd_frame->bufs[i]->stream_type == CAM_STREAM_TYPE_SNAPSHOT ) {
            mm_camera_buf_def_t * yuv_frame = recvd_frame->bufs[i];
            QCameraStream *pStream = pChannel->getStreamByHandle(yuv_frame->stream_id);
            if ( NULL != pStream ) {
                pme->dumpFrameToFile(pStream, yuv_frame, QCAMERA_DUMP_FRM_INPUT_REPROCESS);
            }
            break;
        }
    }

    property_get("persist.camera.dumpmetadata", value, "0");
    int32_t enabled = atoi(value);
    if (enabled) {
        mm_camera_buf_def_t *pMetaFrame = NULL;
        QCameraStream *pStream = NULL;
        for (uint32_t i = 0; i < frame->num_bufs; i++) {
            pStream = pChannel->getStreamByHandle(frame->bufs[i]->stream_id);
            if (pStream != NULL) {
                if (pStream->isTypeOf(CAM_STREAM_TYPE_METADATA)) {
                    pMetaFrame = frame->bufs[i]; //find the metadata
                    if (pMetaFrame != NULL &&
                            ((metadata_buffer_t *)pMetaFrame->buffer)->is_tuning_params_valid) {
                        pme->dumpMetadataToFile(pStream, pMetaFrame, (char *) "Snapshot");
                    }
                    break;
                }
            }
        }
    }

    // Wait on Postproc initialization if needed
    // then send to postprocessor
    if ((NO_ERROR != pme->waitDeferredWork(pme->mReprocJob)) ||
            (NO_ERROR != pme->m_postprocessor.processData(frame))) {
        LOGE("Failed to trigger process data");
        pChannel->bufDone(recvd_frame);
        free(frame);
        frame = NULL;
        return;
    }

/* START of test register face image for face authentication */
#ifdef QCOM_TEST_FACE_REGISTER_FACE
    static uint8_t bRunFaceReg = 1;

    if (bRunFaceReg > 0) {
        // find snapshot frame
        QCameraStream *main_stream = NULL;
        mm_camera_buf_def_t *main_frame = NULL;
        for (int i = 0; i < recvd_frame->num_bufs; i++) {
            QCameraStream *pStream =
                pChannel->getStreamByHandle(recvd_frame->bufs[i]->stream_id);
            if (pStream != NULL) {
                if (pStream->isTypeOf(CAM_STREAM_TYPE_SNAPSHOT)) {
                    main_stream = pStream;
                    main_frame = recvd_frame->bufs[i];
                    break;
                }
            }
        }
        if (main_stream != NULL && main_frame != NULL) {
            int32_t faceId = -1;
            cam_pp_offline_src_config_t config;
            memset(&config, 0, sizeof(cam_pp_offline_src_config_t));
            config.num_of_bufs = 1;
            main_stream->getFormat(config.input_fmt);
            main_stream->getFrameDimension(config.input_dim);
            main_stream->getFrameOffset(config.input_buf_planes.plane_info);
            LOGH("DEBUG: registerFaceImage E");
            int32_t rc = pme->registerFaceImage(main_frame->buffer, &config, faceId);
            LOGH("DEBUG: registerFaceImage X, ret=%d, faceId=%d", rc, faceId);
            bRunFaceReg = 0;
        }
    }

#endif
/* END of test register face image for face authentication */

    LOGH("[KPI Perf]: X");
}
#ifdef TARGET_TS_MAKEUP
bool QCamera2HardwareInterface::TsMakeupProcess_Preview(mm_camera_buf_def_t *pFrame,
        QCameraStream * pStream) {
    LOGD("begin");
    bool bRet = false;
    if (pStream == NULL || pFrame == NULL) {
        bRet = false;
        LOGH("pStream == NULL || pFrame == NULL");
    } else {
        bRet = TsMakeupProcess(pFrame, pStream, mFaceRect);
    }
    LOGD("end bRet = %d ",bRet);
    return bRet;
}

bool QCamera2HardwareInterface::TsMakeupProcess_Snapshot(mm_camera_buf_def_t *pFrame,
        QCameraStream * pStream) {
    LOGD("begin");
    bool bRet = false;
    if (pStream == NULL || pFrame == NULL) {
        bRet = false;
        LOGH("pStream == NULL || pFrame == NULL");
    } else {
        cam_frame_len_offset_t offset;
        memset(&offset, 0, sizeof(cam_frame_len_offset_t));
        pStream->getFrameOffset(offset);

        cam_dimension_t dim;
        pStream->getFrameDimension(dim);

        unsigned char *yBuf  = (unsigned char*)pFrame->buffer;
        unsigned char *uvBuf = yBuf + offset.mp[0].len;
        TSMakeupDataEx inMakeupData;
        inMakeupData.frameWidth  = dim.width;
        inMakeupData.frameHeight = dim.height;
        inMakeupData.yBuf  = yBuf;
        inMakeupData.uvBuf = uvBuf;
        inMakeupData.yStride  = offset.mp[0].stride;
        inMakeupData.uvStride = offset.mp[1].stride;
        LOGD("detect begin");
        TSHandle fd_handle = ts_detectface_create_context();
        if (fd_handle != NULL) {
            cam_format_t fmt;
            pStream->getFormat(fmt);
            int iret = ts_detectface_detectEx(fd_handle, &inMakeupData);
            LOGD("ts_detectface_detect iret = %d",iret);
            if (iret <= 0) {
                bRet = false;
            } else {
                TSRect faceRect;
                memset(&faceRect,-1,sizeof(TSRect));
                iret = ts_detectface_get_face_info(fd_handle, 0, &faceRect, NULL,NULL,NULL);
                LOGD("ts_detectface_get_face_info iret=%d,faceRect.left=%ld,"
                        "faceRect.top=%ld,faceRect.right=%ld,faceRect.bottom=%ld"
                        ,iret,faceRect.left,faceRect.top,faceRect.right,faceRect.bottom);
                bRet = TsMakeupProcess(pFrame,pStream,faceRect);
            }
            ts_detectface_destroy_context(&fd_handle);
            fd_handle = NULL;
        } else {
            LOGH("fd_handle == NULL");
        }
        LOGD("detect end");
    }
    LOGD("end bRet = %d ",bRet);
    return bRet;
}

bool QCamera2HardwareInterface::TsMakeupProcess(mm_camera_buf_def_t *pFrame,
        QCameraStream * pStream,TSRect& faceRect) {
    bool bRet = false;
    LOGD("begin");
    if (pStream == NULL || pFrame == NULL) {
        LOGH("pStream == NULL || pFrame == NULL ");
        return false;
    }

    int whiteLevel, cleanLevel;
    bool enableMakeup = (faceRect.left > -1) &&
            (mParameters.getTsMakeupInfo(whiteLevel, cleanLevel));
    if (enableMakeup) {
        cam_dimension_t dim;
        cam_frame_len_offset_t offset;
        pStream->getFrameDimension(dim);
        pStream->getFrameOffset(offset);
        unsigned char *tempOriBuf = NULL;

        tempOriBuf = (unsigned char*)pFrame->buffer;
        unsigned char *yBuf = tempOriBuf;
        unsigned char *uvBuf = tempOriBuf + offset.mp[0].len;
        unsigned char *tmpBuf = new unsigned char[offset.frame_len];
        if (tmpBuf == NULL) {
            LOGH("tmpBuf == NULL ");
            return false;
        }
        TSMakeupDataEx inMakeupData, outMakeupData;
        whiteLevel =  whiteLevel <= 0 ? 0 : (whiteLevel >= 100 ? 100 : whiteLevel);
        cleanLevel =  cleanLevel <= 0 ? 0 : (cleanLevel >= 100 ? 100 : cleanLevel);
        inMakeupData.frameWidth = dim.width;  // NV21 Frame width  > 0
        inMakeupData.frameHeight = dim.height; // NV21 Frame height > 0
        inMakeupData.yBuf =  yBuf; //  Y buffer pointer
        inMakeupData.uvBuf = uvBuf; // VU buffer pointer
        inMakeupData.yStride  = offset.mp[0].stride;
        inMakeupData.uvStride = offset.mp[1].stride;
        outMakeupData.frameWidth = dim.width; // NV21 Frame width  > 0
        outMakeupData.frameHeight = dim.height; // NV21 Frame height > 0
        outMakeupData.yBuf =  tmpBuf; //  Y buffer pointer
        outMakeupData.uvBuf = tmpBuf + offset.mp[0].len; // VU buffer pointer
        outMakeupData.yStride  = offset.mp[0].stride;
        outMakeupData.uvStride = offset.mp[1].stride;
        LOGD("faceRect:left 2:%ld,,right:%ld,,top:%ld,,bottom:%ld,,Level:%dx%d",
            faceRect.left,faceRect.right,faceRect.top,faceRect.bottom,cleanLevel,whiteLevel);
        ts_makeup_skin_beautyEx(&inMakeupData, &outMakeupData, &(faceRect),cleanLevel,whiteLevel);
        memcpy((unsigned char*)pFrame->buffer, tmpBuf, offset.frame_len);
        QCameraMemory *memory = (QCameraMemory *)pFrame->mem_info;
        memory->cleanCache(pFrame->buf_idx);
        if (tmpBuf != NULL) {
            delete[] tmpBuf;
            tmpBuf = NULL;
        }
    }
    LOGD("end bRet = %d ",bRet);
    return bRet;
}
#endif
/*===========================================================================
 * FUNCTION   : postproc_channel_cb_routine
 *
 * DESCRIPTION: helper function to handle postprocess superbuf callback directly from
 *              mm-camera-interface
 *
 * PARAMETERS :
 *   @recvd_frame : received super buffer
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : recvd_frame will be released after this call by caller, so if
 *             async operation needed for recvd_frame, it's our responsibility
 *             to save a copy for this variable to be used later.
*==========================================================================*/
void QCamera2HardwareInterface::postproc_channel_cb_routine(mm_camera_super_buf_t *recvd_frame,
                                                            void *userdata)
{
    ATRACE_CALL();
    LOGH("[KPI Perf]: E");
    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;
    if (pme == NULL ||
        pme->mCameraHandle == NULL ||
        pme->mCameraHandle->camera_handle != recvd_frame->camera_handle){
        LOGE("camera obj not valid");
        return;
    }

    // save a copy for the superbuf
    mm_camera_super_buf_t* frame =
               (mm_camera_super_buf_t *)malloc(sizeof(mm_camera_super_buf_t));
    if (frame == NULL) {
        LOGE("Error allocating memory to save received_frame structure.");
        return;
    }
    *frame = *recvd_frame;

    if (recvd_frame->num_bufs > 0) {
        LOGI("[KPI Perf]: frame_idx %d", recvd_frame->bufs[0]->frame_idx);
    }
    // Wait on JPEG create session
    pme->waitDeferredWork(pme->mJpegJob);

    // send to postprocessor
    pme->m_postprocessor.processPPData(frame);

    ATRACE_INT("Camera:Reprocess", 0);
    LOGH("[KPI Perf]: X");
}

/*===========================================================================
 * FUNCTION   : synchronous_stream_cb_routine
 *
 * DESCRIPTION: Function to handle STREAM SYNC CALLBACKS
 *
 * PARAMETERS :
 *   @super_frame : received super buffer
 *   @stream      : stream object
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : This Function is excecuted in mm-interface context.
 *             Avoid adding latency on this thread.
 *==========================================================================*/
void QCamera2HardwareInterface::synchronous_stream_cb_routine(
        mm_camera_super_buf_t *super_frame, QCameraStream * stream,
        void *userdata)
{
    nsecs_t frameTime = 0, mPreviewTimestamp = 0;
    int err = NO_ERROR;

    ATRACE_CALL();
    LOGH("[KPI Perf] : BEGIN");
    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;

    if (pme == NULL) {
        LOGE("Invalid hardware object");
        return;
    }
    if (super_frame == NULL) {
        LOGE("Invalid super buffer");
        return;
    }
    mm_camera_buf_def_t *frame = super_frame->bufs[0];
    if (NULL == frame) {
        LOGE("Frame is NULL");
        return;
    }

    if (stream->getMyType() != CAM_STREAM_TYPE_PREVIEW) {
        LOGE("This is only for PREVIEW stream for now");
        return;
    }

    if(pme->m_bPreviewStarted) {
        LOGI("[KPI Perf] : PROFILE_FIRST_PREVIEW_FRAME");
        pme->m_bPreviewStarted = false;
    }

    QCameraGrallocMemory *memory = (QCameraGrallocMemory *) frame->mem_info;
    if (!pme->needProcessPreviewFrame(frame->frame_idx)) {
        pthread_mutex_lock(&pme->mGrallocLock);
        pme->mLastPreviewFrameID = frame->frame_idx;
        memory->setBufferStatus(frame->buf_idx, STATUS_SKIPPED);
        pthread_mutex_unlock(&pme->mGrallocLock);
        LOGH("preview is not running, no need to process");
        return;
    }

    if (pme->needDebugFps()) {
        pme->debugShowPreviewFPS();
    }

    frameTime = nsecs_t(frame->ts.tv_sec) * 1000000000LL + frame->ts.tv_nsec;
    // Convert Boottime from camera to Monotime for display if needed.
    // Otherwise, mBootToMonoTimestampOffset value will be 0.
    frameTime = frameTime - pme->mBootToMonoTimestampOffset;
    // Calculate the future presentation time stamp for displaying frames at regular interval
    /*if (pme->getRecordingHintValue() == true) {
        mPreviewTimestamp = pme->mCameraDisplay.computePresentationTimeStamp(frameTime);
    }*/
    stream->mStreamTimestamp = frameTime;

#ifdef TARGET_TS_MAKEUP
    pme->TsMakeupProcess_Preview(frame,stream);
#endif

    // Enqueue  buffer to gralloc.
    uint32_t idx = frame->buf_idx;
    LOGD("%p Enqueue Buffer to display %d frame Time = %lld Display Time = %lld",
            pme, idx, frameTime, mPreviewTimestamp);
    err = memory->enqueueBuffer(idx, mPreviewTimestamp);

    if (err == NO_ERROR) {
        pthread_mutex_lock(&pme->mGrallocLock);
        pme->mLastPreviewFrameID = frame->frame_idx;
        pme->mEnqueuedBuffers++;
        pthread_mutex_unlock(&pme->mGrallocLock);
    } else {
        LOGE("Enqueue Buffer failed");
    }

    LOGH("[KPI Perf] : END");
    return;
}

/*===========================================================================
 * FUNCTION   : preview_stream_cb_routine
 *
 * DESCRIPTION: helper function to handle preview frame from preview stream in
 *              normal case with display.
 *
 * PARAMETERS :
 *   @super_frame : received super buffer
 *   @stream      : stream object
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : caller passes the ownership of super_frame, it's our
 *             responsibility to free super_frame once it's done. The new
 *             preview frame will be sent to display, and an older frame
 *             will be dequeued from display and needs to be returned back
 *             to kernel for future use.
 *==========================================================================*/
void QCamera2HardwareInterface::preview_stream_cb_routine(mm_camera_super_buf_t *super_frame,
                                                          QCameraStream * stream,
                                                          void *userdata)
{
    KPI_ATRACE_CALL();
    LOGH("[KPI Perf] : BEGIN");
    int err = NO_ERROR;
    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;
    QCameraGrallocMemory *memory = (QCameraGrallocMemory *)super_frame->bufs[0]->mem_info;
    uint8_t dequeueCnt = 0;

    if (pme == NULL) {
        LOGE("Invalid hardware object");
        free(super_frame);
        return;
    }
    if (memory == NULL) {
        LOGE("Invalid memory object");
        free(super_frame);
        return;
    }

    mm_camera_buf_def_t *frame = super_frame->bufs[0];
    if (NULL == frame) {
        LOGE("preview frame is NLUL");
        free(super_frame);
        return;
    }

    // For instant capture and for instant AEC, keep track of the frame counter.
    // This count will be used to check against the corresponding bound values.
    if (pme->mParameters.isInstantAECEnabled() ||
            pme->mParameters.isInstantCaptureEnabled()) {
        pme->mInstantAecFrameCount++;
    }

    pthread_mutex_lock(&pme->mGrallocLock);
    if (!stream->isSyncCBEnabled()) {
        pme->mLastPreviewFrameID = frame->frame_idx;
    }
    bool discardFrame = false;
    if (!stream->isSyncCBEnabled() &&
            !pme->needProcessPreviewFrame(frame->frame_idx))
    {
        discardFrame = true;
    } else if (stream->isSyncCBEnabled() &&
            memory->isBufSkipped(frame->buf_idx)) {
        discardFrame = true;
        memory->setBufferStatus(frame->buf_idx, STATUS_IDLE);
    }
    pthread_mutex_unlock(&pme->mGrallocLock);

    if (discardFrame) {
        LOGH("preview is not running, no need to process");
        stream->bufDone(frame->buf_idx);
    }

    uint32_t idx = frame->buf_idx;

    pme->dumpFrameToFile(stream, frame, QCAMERA_DUMP_FRM_PREVIEW);

    if(pme->m_bPreviewStarted) {
       LOGI("[KPI Perf] : PROFILE_FIRST_PREVIEW_FRAME");
       pme->m_bPreviewStarted = false ;
    }

    if (!stream->isSyncCBEnabled() && !discardFrame) {

        if (pme->needDebugFps()) {
            pme->debugShowPreviewFPS();
        }

        LOGD("Enqueue Buffer to display %d", idx);
#ifdef TARGET_TS_MAKEUP
        pme->TsMakeupProcess_Preview(frame,stream);
#endif
        err = memory->enqueueBuffer(idx);

        if (err == NO_ERROR) {
            pthread_mutex_lock(&pme->mGrallocLock);
            pme->mEnqueuedBuffers++;
            dequeueCnt = pme->mEnqueuedBuffers;
            pthread_mutex_unlock(&pme->mGrallocLock);
        } else {
            LOGE("Enqueue Buffer failed");
        }
    } else {
        pthread_mutex_lock(&pme->mGrallocLock);
        dequeueCnt = pme->mEnqueuedBuffers;
        pthread_mutex_unlock(&pme->mGrallocLock);
    }

    uint8_t numMapped = memory->getMappable();
    LOGD("EnqueuedCnt %d numMapped %d", dequeueCnt, numMapped);

    for (uint8_t i = 0; i < dequeueCnt; i++) {
        int dequeuedIdx = memory->dequeueBuffer();
        LOGD("dequeuedIdx %d numMapped %d Loop running for %d", dequeuedIdx, numMapped, i);
        if (dequeuedIdx < 0 || dequeuedIdx >= memory->getCnt()) {
            LOGE("Invalid dequeued buffer index %d from display",
                   dequeuedIdx);
            break;
        } else {
            pthread_mutex_lock(&pme->mGrallocLock);
            pme->mEnqueuedBuffers--;
            pthread_mutex_unlock(&pme->mGrallocLock);
            if (dequeuedIdx >= numMapped) {
                // This buffer has not yet been mapped to the backend
                err = stream->mapNewBuffer((uint32_t)dequeuedIdx);
                if (memory->checkIfAllBuffersMapped()) {
                    // check if mapping is done for all the buffers
                    // Signal the condition for create jpeg session
                    Mutex::Autolock l(pme->mMapLock);
                    pme->mMapCond.signal();
                    LOGH("Mapping done for all bufs");
                } else {
                    LOGH("All buffers are not yet mapped");
                }
            }
        }
        // Get the updated mappable buffer count since it's modified in dequeueBuffer()
        numMapped = memory->getMappable();
        if (err < 0) {
            LOGE("buffer mapping failed %d", err);
        } else {
            // Return dequeued buffer back to driver
            err = stream->bufDone((uint32_t)dequeuedIdx);
            if ( err < 0) {
                LOGW("stream bufDone failed %d", err);
            }
        }
    }

    // Handle preview data callback
    if (pme->m_channels[QCAMERA_CH_TYPE_CALLBACK] == NULL) {
        if (pme->needSendPreviewCallback() && !discardFrame &&
                (!pme->mParameters.isSceneSelectionEnabled())) {
            int32_t rc = pme->sendPreviewCallback(stream, memory, idx);
            if (NO_ERROR != rc) {
                LOGW("Preview callback was not sent succesfully");
            }
        }
    }

    free(super_frame);
    LOGH("[KPI Perf] : END");
    return;
}

/*===========================================================================
 * FUNCTION   : sendPreviewCallback
 *
 * DESCRIPTION: helper function for triggering preview callbacks
 *
 * PARAMETERS :
 *   @stream    : stream object
 *   @memory    : Stream memory allocator
 *   @idx       : buffer index
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::sendPreviewCallback(QCameraStream *stream,
        QCameraMemory *memory, uint32_t idx)
{
    camera_memory_t *previewMem = NULL;
    camera_memory_t *data = NULL;
    camera_memory_t *dataToApp = NULL;
    size_t previewBufSize = 0;
    size_t previewBufSizeFromCallback = 0;
    cam_dimension_t preview_dim;
    cam_format_t previewFmt;
    int32_t rc = NO_ERROR;
    int32_t yStride = 0;
    int32_t yScanline = 0;
    int32_t uvStride = 0;
    int32_t uvScanline = 0;
    int32_t uStride = 0;
    int32_t uScanline = 0;
    int32_t vStride = 0;
    int32_t vScanline = 0;
    int32_t yStrideToApp = 0;
    int32_t uvStrideToApp = 0;
    int32_t yScanlineToApp = 0;
    int32_t uvScanlineToApp = 0;
    int32_t srcOffset = 0;
    int32_t dstOffset = 0;
    int32_t srcBaseOffset = 0;
    int32_t dstBaseOffset = 0;
    int i;

    if ((NULL == stream) || (NULL == memory)) {
        LOGE("Invalid preview callback input");
        return BAD_VALUE;
    }

    cam_stream_info_t *streamInfo =
            reinterpret_cast<cam_stream_info_t *>(stream->getStreamInfoBuf()->getPtr(0));
    if (NULL == streamInfo) {
        LOGE("Invalid streamInfo");
        return BAD_VALUE;
    }

    stream->getFrameDimension(preview_dim);
    stream->getFormat(previewFmt);

    yStrideToApp = preview_dim.width;
    yScanlineToApp = preview_dim.height;
    uvStrideToApp = yStrideToApp;
    uvScanlineToApp = yScanlineToApp / 2;

    /* The preview buffer size in the callback should be
     * (width*height*bytes_per_pixel). As all preview formats we support,
     * use 12 bits per pixel, buffer size = previewWidth * previewHeight * 3/2.
     * We need to put a check if some other formats are supported in future. */
    if ((previewFmt == CAM_FORMAT_YUV_420_NV21) ||
        (previewFmt == CAM_FORMAT_YUV_420_NV12) ||
        (previewFmt == CAM_FORMAT_YUV_420_YV12) ||
        (previewFmt == CAM_FORMAT_YUV_420_NV12_VENUS) ||
        (previewFmt == CAM_FORMAT_YUV_420_NV21_VENUS) ||
        (previewFmt == CAM_FORMAT_YUV_420_NV21_ADRENO)) {
        if(previewFmt == CAM_FORMAT_YUV_420_YV12) {
            yStride = streamInfo->buf_planes.plane_info.mp[0].stride;
            yScanline = streamInfo->buf_planes.plane_info.mp[0].scanline;
            uStride = streamInfo->buf_planes.plane_info.mp[1].stride;
            uScanline = streamInfo->buf_planes.plane_info.mp[1].scanline;
            vStride = streamInfo->buf_planes.plane_info.mp[2].stride;
            vScanline = streamInfo->buf_planes.plane_info.mp[2].scanline;

            previewBufSize = (size_t)
                    (yStride * yScanline + uStride * uScanline + vStride * vScanline);
            previewBufSizeFromCallback = previewBufSize;
        } else {
            yStride = streamInfo->buf_planes.plane_info.mp[0].stride;
            yScanline = streamInfo->buf_planes.plane_info.mp[0].scanline;
            uvStride = streamInfo->buf_planes.plane_info.mp[1].stride;
            uvScanline = streamInfo->buf_planes.plane_info.mp[1].scanline;

            previewBufSize = (size_t)
                    ((yStrideToApp * yScanlineToApp) + (uvStrideToApp * uvScanlineToApp));

            previewBufSizeFromCallback = (size_t)
                    ((yStride * yScanline) + (uvStride * uvScanline));
        }
        if(previewBufSize == previewBufSizeFromCallback) {
            previewMem = mGetMemory(memory->getFd(idx),
                       previewBufSize, 1, mCallbackCookie);
            if (!previewMem || !previewMem->data) {
                LOGE("mGetMemory failed.\n");
                return NO_MEMORY;
            } else {
                data = previewMem;
            }
        } else {
            data = memory->getMemory(idx, false);
            dataToApp = mGetMemory(-1, previewBufSize, 1, mCallbackCookie);
            if (!dataToApp || !dataToApp->data) {
                LOGE("mGetMemory failed.\n");
                return NO_MEMORY;
            }

            for (i = 0; i < preview_dim.height; i++) {
                srcOffset = i * yStride;
                dstOffset = i * yStrideToApp;

                memcpy((unsigned char *) dataToApp->data + dstOffset,
                        (unsigned char *) data->data + srcOffset,
                        (size_t)yStrideToApp);
            }

            srcBaseOffset = yStride * yScanline;
            dstBaseOffset = yStrideToApp * yScanlineToApp;

            for (i = 0; i < preview_dim.height/2; i++) {
                srcOffset = i * uvStride + srcBaseOffset;
                dstOffset = i * uvStrideToApp + dstBaseOffset;

                memcpy((unsigned char *) dataToApp->data + dstOffset,
                        (unsigned char *) data->data + srcOffset,
                        (size_t)yStrideToApp);
            }
        }
    } else {
        /*Invalid Buffer content. But can be used as a first preview frame trigger in
        framework/app */
        previewBufSize = (size_t)
                    ((yStrideToApp * yScanlineToApp) +
                    (uvStrideToApp * uvScanlineToApp));
        previewBufSizeFromCallback = 0;
        LOGW("Invalid preview format. Buffer content cannot be processed size = %d",
                previewBufSize);
        dataToApp = mGetMemory(-1, previewBufSize, 1, mCallbackCookie);
        if (!dataToApp || !dataToApp->data) {
            LOGE("mGetMemory failed.\n");
            return NO_MEMORY;
        }
    }
    qcamera_callback_argm_t cbArg;
    memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
    cbArg.cb_type = QCAMERA_DATA_CALLBACK;
    cbArg.msg_type = CAMERA_MSG_PREVIEW_FRAME;
    if (previewBufSize != 0 && previewBufSizeFromCallback != 0 &&
            previewBufSize == previewBufSizeFromCallback) {
        cbArg.data = data;
    } else {
        cbArg.data = dataToApp;
    }
    if ( previewMem ) {
        cbArg.user_data = previewMem;
        cbArg.release_cb = releaseCameraMemory;
    } else if (dataToApp) {
        cbArg.user_data = dataToApp;
        cbArg.release_cb = releaseCameraMemory;
    }
    cbArg.cookie = this;
    rc = m_cbNotifier.notifyCallback(cbArg);
    if (rc != NO_ERROR) {
        LOGW("fail sending notification");
        if (previewMem) {
            previewMem->release(previewMem);
        } else if (dataToApp) {
            dataToApp->release(dataToApp);
        }
    }

    return rc;
}

/*===========================================================================
 * FUNCTION   : nodisplay_preview_stream_cb_routine
 *
 * DESCRIPTION: helper function to handle preview frame from preview stream in
 *              no-display case
 *
 * PARAMETERS :
 *   @super_frame : received super buffer
 *   @stream      : stream object
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : caller passes the ownership of super_frame, it's our
 *             responsibility to free super_frame once it's done.
 *==========================================================================*/
void QCamera2HardwareInterface::nodisplay_preview_stream_cb_routine(
                                                          mm_camera_super_buf_t *super_frame,
                                                          QCameraStream *stream,
                                                          void * userdata)
{
    ATRACE_CALL();
    LOGH("[KPI Perf] E");
    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;
    if (pme == NULL ||
        pme->mCameraHandle == NULL ||
        pme->mCameraHandle->camera_handle != super_frame->camera_handle){
        LOGE("camera obj not valid");
        // simply free super frame
        free(super_frame);
        return;
    }
    mm_camera_buf_def_t *frame = super_frame->bufs[0];
    if (NULL == frame) {
        LOGE("preview frame is NULL");
        free(super_frame);
        return;
    }

    if (!pme->needProcessPreviewFrame(frame->frame_idx)) {
        LOGH("preview is not running, no need to process");
        stream->bufDone(frame->buf_idx);
        free(super_frame);
        return;
    }

    if (pme->needDebugFps()) {
        pme->debugShowPreviewFPS();
    }

    QCameraMemory *previewMemObj = (QCameraMemory *)frame->mem_info;
    camera_memory_t *preview_mem = NULL;
    if (previewMemObj != NULL) {
        preview_mem = previewMemObj->getMemory(frame->buf_idx, false);
    }
    if (NULL != previewMemObj && NULL != preview_mem) {
        pme->dumpFrameToFile(stream, frame, QCAMERA_DUMP_FRM_PREVIEW);

        if ((pme->needProcessPreviewFrame(frame->frame_idx)) &&
                pme->needSendPreviewCallback() &&
                (pme->getRelatedCamSyncInfo()->mode != CAM_MODE_SECONDARY)) {
            qcamera_callback_argm_t cbArg;
            memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
            cbArg.cb_type = QCAMERA_DATA_CALLBACK;
            cbArg.msg_type = CAMERA_MSG_PREVIEW_FRAME;
            cbArg.data = preview_mem;
            cbArg.user_data = (void *) &frame->buf_idx;
            cbArg.cookie = stream;
            cbArg.release_cb = returnStreamBuffer;
            int32_t rc = pme->m_cbNotifier.notifyCallback(cbArg);
            if (rc != NO_ERROR) {
                LOGE ("fail sending data notify");
                stream->bufDone(frame->buf_idx);
            }
        } else {
            stream->bufDone(frame->buf_idx);
        }
    }
    free(super_frame);
    LOGH("[KPI Perf] X");
}

/*===========================================================================
 * FUNCTION   : rdi_mode_stream_cb_routine
 *
 * DESCRIPTION: helper function to handle RDI frame from preview stream in
 *              rdi mode case
 *
 * PARAMETERS :
 *   @super_frame : received super buffer
 *   @stream      : stream object
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : caller passes the ownership of super_frame, it's our
 *             responsibility to free super_frame once it's done.
 *==========================================================================*/
void QCamera2HardwareInterface::rdi_mode_stream_cb_routine(
  mm_camera_super_buf_t *super_frame,
  QCameraStream *stream,
  void * userdata)
{
    ATRACE_CALL();
    LOGH("RDI_DEBUG Enter");
    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;
    if (pme == NULL ||
        pme->mCameraHandle == NULL ||
        pme->mCameraHandle->camera_handle != super_frame->camera_handle){
        LOGE("camera obj not valid");
        free(super_frame);
        return;
    }
    mm_camera_buf_def_t *frame = super_frame->bufs[0];
    if (NULL == frame) {
        LOGE("preview frame is NLUL");
        goto end;
    }
    if (!pme->needProcessPreviewFrame(frame->frame_idx)) {
        LOGE("preview is not running, no need to process");
        stream->bufDone(frame->buf_idx);
        goto end;
    }
    if (pme->needDebugFps()) {
        pme->debugShowPreviewFPS();
    }
    // Non-secure Mode
    if (!pme->isSecureMode()) {
        QCameraMemory *previewMemObj = (QCameraMemory *)frame->mem_info;
        if (NULL == previewMemObj) {
            LOGE("previewMemObj is NULL");
            stream->bufDone(frame->buf_idx);
            goto end;
        }

        camera_memory_t *preview_mem = previewMemObj->getMemory(frame->buf_idx, false);
        if (NULL != preview_mem) {
            previewMemObj->cleanCache(frame->buf_idx);
            // Dump RAW frame
            pme->dumpFrameToFile(stream, frame, QCAMERA_DUMP_FRM_RAW);
            // Notify Preview callback frame
            if (pme->needProcessPreviewFrame(frame->frame_idx) &&
                    pme->mDataCb != NULL &&
                    pme->msgTypeEnabledWithLock(CAMERA_MSG_PREVIEW_FRAME) > 0) {
                qcamera_callback_argm_t cbArg;
                memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
                cbArg.cb_type    = QCAMERA_DATA_CALLBACK;
                cbArg.msg_type   = CAMERA_MSG_PREVIEW_FRAME;
                cbArg.data       = preview_mem;
                cbArg.user_data = (void *) &frame->buf_idx;
                cbArg.cookie     = stream;
                cbArg.release_cb = returnStreamBuffer;
                pme->m_cbNotifier.notifyCallback(cbArg);
            } else {
                LOGE("preview_mem is NULL");
                stream->bufDone(frame->buf_idx);
            }
        }
        else {
            LOGE("preview_mem is NULL");
            stream->bufDone(frame->buf_idx);
        }
    } else {
        // Secure Mode
        // We will do QCAMERA_NOTIFY_CALLBACK and share FD in case of secure mode
        QCameraMemory *previewMemObj = (QCameraMemory *)frame->mem_info;
        if (NULL == previewMemObj) {
            LOGE("previewMemObj is NULL");
            stream->bufDone(frame->buf_idx);
            goto end;
        }

        int fd = previewMemObj->getFd(frame->buf_idx);
        LOGD("Preview frame fd =%d for index = %d ", fd, frame->buf_idx);
        if (pme->needProcessPreviewFrame(frame->frame_idx) &&
                pme->mDataCb != NULL &&
                pme->msgTypeEnabledWithLock(CAMERA_MSG_PREVIEW_FRAME) > 0) {
            // Prepare Callback structure
            qcamera_callback_argm_t cbArg;
            memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
            cbArg.cb_type    = QCAMERA_NOTIFY_CALLBACK;
            cbArg.msg_type   = CAMERA_MSG_PREVIEW_FRAME;
#ifndef VANILLA_HAL
            cbArg.ext1       = CAMERA_FRAME_DATA_FD;
            cbArg.ext2       = fd;
#endif
            cbArg.user_data  = (void *) &frame->buf_idx;
            cbArg.cookie     = stream;
            cbArg.release_cb = returnStreamBuffer;
            pme->m_cbNotifier.notifyCallback(cbArg);
        } else {
            LOGH("No need to process preview frame, return buffer");
            stream->bufDone(frame->buf_idx);
        }
    }
end:
    free(super_frame);
    LOGH("RDI_DEBUG Exit");
    return;
}

/*===========================================================================
 * FUNCTION   : postview_stream_cb_routine
 *
 * DESCRIPTION: helper function to handle post frame from postview stream
 *
 * PARAMETERS :
 *   @super_frame : received super buffer
 *   @stream      : stream object
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : caller passes the ownership of super_frame, it's our
 *             responsibility to free super_frame once it's done.
 *==========================================================================*/
void QCamera2HardwareInterface::postview_stream_cb_routine(mm_camera_super_buf_t *super_frame,
                                                           QCameraStream *stream,
                                                           void *userdata)
{
    ATRACE_CALL();
    int err = NO_ERROR;
    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;
    QCameraGrallocMemory *memory = (QCameraGrallocMemory *)super_frame->bufs[0]->mem_info;

    if (pme == NULL) {
        LOGE("Invalid hardware object");
        free(super_frame);
        return;
    }
    if (memory == NULL) {
        LOGE("Invalid memory object");
        free(super_frame);
        return;
    }

    LOGH("[KPI Perf] : BEGIN");

    mm_camera_buf_def_t *frame = super_frame->bufs[0];
    if (NULL == frame) {
        LOGE("preview frame is NULL");
        free(super_frame);
        return;
    }

    QCameraMemory *memObj = (QCameraMemory *)frame->mem_info;
    if (NULL != memObj) {
        pme->dumpFrameToFile(stream, frame, QCAMERA_DUMP_FRM_THUMBNAIL);
    }

    // Return buffer back to driver
    err = stream->bufDone(frame->buf_idx);
    if ( err < 0) {
        LOGE("stream bufDone failed %d", err);
    }

    free(super_frame);
    LOGH("[KPI Perf] : END");
    return;
}

/*===========================================================================
 * FUNCTION   : video_stream_cb_routine
 *
 * DESCRIPTION: helper function to handle video frame from video stream
 *
 * PARAMETERS :
 *   @super_frame : received super buffer
 *   @stream      : stream object
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : caller passes the ownership of super_frame, it's our
 *             responsibility to free super_frame once it's done. video
 *             frame will be sent to video encoder. Once video encoder is
 *             done with the video frame, it will call another API
 *             (release_recording_frame) to return the frame back
 *==========================================================================*/
void QCamera2HardwareInterface::video_stream_cb_routine(mm_camera_super_buf_t *super_frame,
                                                        QCameraStream *stream,
                                                        void *userdata)
{
    ATRACE_CALL();
    QCameraVideoMemory *videoMemObj = NULL;
    camera_memory_t *video_mem = NULL;
    nsecs_t timeStamp = 0;
    bool triggerTCB = FALSE;

    LOGD("[KPI Perf] : BEGIN");
    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;
    if (pme == NULL ||
        pme->mCameraHandle == NULL ||
        pme->mCameraHandle->camera_handle != super_frame->camera_handle){
        LOGE("camera obj not valid");
        // simply free super frame
        free(super_frame);
        return;
    }

    mm_camera_buf_def_t *frame = super_frame->bufs[0];

    if (pme->needDebugFps()) {
        pme->debugShowVideoFPS();
    }
    if(pme->m_bRecordStarted) {
       LOGI("[KPI Perf] : PROFILE_FIRST_RECORD_FRAME");
       pme->m_bRecordStarted = false ;
    }
    LOGD("Stream(%d), Timestamp: %ld %ld",
          frame->stream_id,
          frame->ts.tv_sec,
          frame->ts.tv_nsec);

    if (frame->buf_type == CAM_STREAM_BUF_TYPE_MPLANE) {
        if (pme->mParameters.getVideoBatchSize() == 0) {
            timeStamp = nsecs_t(frame->ts.tv_sec) * 1000000000LL
                    + frame->ts.tv_nsec;
            pme->dumpFrameToFile(stream, frame, QCAMERA_DUMP_FRM_VIDEO);
            videoMemObj = (QCameraVideoMemory *)frame->mem_info;
            video_mem = NULL;
            if (NULL != videoMemObj) {
                video_mem = videoMemObj->getMemory(frame->buf_idx,
                        (pme->mStoreMetaDataInFrame > 0)? true : false);
                triggerTCB = TRUE;
                LOGH("Video frame TimeStamp : %lld batch = 0 index = %d",
                        timeStamp, frame->buf_idx);
            }
        } else {
            //Handle video batch callback
            native_handle_t *nh = NULL;
            pme->dumpFrameToFile(stream, frame, QCAMERA_DUMP_FRM_VIDEO);
            QCameraVideoMemory *videoMemObj = (QCameraVideoMemory *)frame->mem_info;
            if ((stream->mCurMetaMemory == NULL)
                    || (stream->mCurBufIndex == -1)) {
                //get Free metadata available
                for (int i = 0; i < CAMERA_MIN_VIDEO_BATCH_BUFFERS; i++) {
                    if (stream->mStreamMetaMemory[i].consumerOwned == 0) {
                        stream->mCurMetaMemory = videoMemObj->getMemory(i,true);
                        stream->mCurBufIndex = 0;
                        stream->mCurMetaIndex = i;
                        stream->mStreamMetaMemory[i].numBuffers = 0;
                        break;
                    }
                }
            }
            video_mem = stream->mCurMetaMemory;
            nh = videoMemObj->getNativeHandle(stream->mCurMetaIndex);
            if (video_mem == NULL || nh == NULL) {
                LOGE("No Free metadata. Drop this frame");
                stream->mCurBufIndex = -1;
                stream->bufDone(frame->buf_idx);
                free(super_frame);
                return;
            }

            int index = stream->mCurBufIndex;
            int fd_cnt = pme->mParameters.getVideoBatchSize();
            nsecs_t frame_ts = nsecs_t(frame->ts.tv_sec) * 1000000000LL
                    + frame->ts.tv_nsec;
            if (index == 0) {
                stream->mFirstTimeStamp = frame_ts;
            }

            stream->mStreamMetaMemory[stream->mCurMetaIndex].buf_index[index]
                    = (uint8_t)frame->buf_idx;
            stream->mStreamMetaMemory[stream->mCurMetaIndex].numBuffers++;
            stream->mStreamMetaMemory[stream->mCurMetaIndex].consumerOwned
                    = TRUE;
            /*
            * data[0] => FD
            * data[mNumFDs + 1] => OFFSET
            * data[mNumFDs + 2] => SIZE
            * data[mNumFDs + 3] => Usage Flag (Color format/Compression)
            * data[mNumFDs + 4] => TIMESTAMP
            * data[mNumFDs + 5] => FORMAT
            */
            nh->data[index] = videoMemObj->getFd(frame->buf_idx);
            nh->data[index + fd_cnt] = 0;
            nh->data[index + (fd_cnt * 2)] = (int)videoMemObj->getSize(frame->buf_idx);
            nh->data[index + (fd_cnt * 3)] = videoMemObj->getUsage();
            nh->data[index + (fd_cnt * 4)] = (int)(frame_ts - stream->mFirstTimeStamp);
            nh->data[index + (fd_cnt * 5)] = videoMemObj->getFormat();
            stream->mCurBufIndex++;
            if (stream->mCurBufIndex == fd_cnt) {
                timeStamp = stream->mFirstTimeStamp;
                LOGH("Video frame to encoder TimeStamp : %lld batch = %d Buffer idx = %d",
                        timeStamp, fd_cnt,
                        nh->data[nh->numFds + nh->numInts - VIDEO_METADATA_NUM_COMMON_INTS]);
                stream->mCurBufIndex = -1;
                stream->mCurMetaIndex = -1;
                stream->mCurMetaMemory = NULL;
                triggerTCB = TRUE;
            }
        }
    } else {
        videoMemObj = (QCameraVideoMemory *)frame->mem_info;
        video_mem = NULL;
        native_handle_t *nh = NULL;
        int fd_cnt = frame->user_buf.bufs_used;
        if (NULL != videoMemObj) {
            video_mem = videoMemObj->getMemory(frame->buf_idx, true);
            nh = videoMemObj->getNativeHandle(frame->buf_idx);
        } else {
            LOGE("videoMemObj NULL");
        }

        if (nh != NULL) {
            timeStamp = nsecs_t(frame->ts.tv_sec) * 1000000000LL
                    + frame->ts.tv_nsec;

            for (int i = 0; i < fd_cnt; i++) {
                if (frame->user_buf.buf_idx[i] >= 0) {
                    mm_camera_buf_def_t *plane_frame =
                            &frame->user_buf.plane_buf[frame->user_buf.buf_idx[i]];
                    QCameraVideoMemory *frameobj =
                            (QCameraVideoMemory *)plane_frame->mem_info;
                    int usage = frameobj->getUsage();
                    nsecs_t frame_ts = nsecs_t(plane_frame->ts.tv_sec) * 1000000000LL
                            + plane_frame->ts.tv_nsec;
                    /*
                       data[0] => FD
                       data[mNumFDs + 1] => OFFSET
                       data[mNumFDs + 2] => SIZE
                       data[mNumFDs + 3] => Usage Flag (Color format/Compression)
                       data[mNumFDs + 4] => TIMESTAMP
                       data[mNumFDs + 5] => FORMAT
                    */
                    nh->data[i] = frameobj->getFd(plane_frame->buf_idx);
                    nh->data[fd_cnt + i] = 0;
                    nh->data[(2 * fd_cnt) + i] = (int)frameobj->getSize(plane_frame->buf_idx);
                    nh->data[(3 * fd_cnt) + i] = usage;
                    nh->data[(4 * fd_cnt) + i] = (int)(frame_ts - timeStamp);
                    nh->data[(5 * fd_cnt) + i] = frameobj->getFormat();
                    LOGD("Send Video frames to services/encoder delta : %lld FD = %d index = %d",
                            (frame_ts - timeStamp), plane_frame->fd, plane_frame->buf_idx);
                    pme->dumpFrameToFile(stream, plane_frame, QCAMERA_DUMP_FRM_VIDEO);
                }
            }
            triggerTCB = TRUE;
            LOGH("Batch buffer TimeStamp : %lld FD = %d index = %d fd_cnt = %d",
                    timeStamp, frame->fd, frame->buf_idx, fd_cnt);
        } else {
            LOGE("No Video Meta Available. Return Buffer");
            stream->bufDone(super_frame->bufs[0]->buf_idx);
        }
    }

    if ((NULL != video_mem) && (triggerTCB == TRUE)) {
        if ((pme->mDataCbTimestamp != NULL) &&
            pme->msgTypeEnabledWithLock(CAMERA_MSG_VIDEO_FRAME) > 0) {
            qcamera_callback_argm_t cbArg;
            memset(&cbArg, 0, sizeof(qcamera_callback_argm_t));
            cbArg.cb_type = QCAMERA_DATA_TIMESTAMP_CALLBACK;
            cbArg.msg_type = CAMERA_MSG_VIDEO_FRAME;
            cbArg.data = video_mem;

            // For VT usecase, ISP uses AVtimer not CLOCK_BOOTTIME as time source.
            // So do not change video timestamp.
            if (!pme->mParameters.isAVTimerEnabled()) {
                // Convert Boottime from camera to Monotime for video if needed.
                // Otherwise, mBootToMonoTimestampOffset value will be 0.
                timeStamp = timeStamp - pme->mBootToMonoTimestampOffset;
            }
            LOGD("Final video buffer TimeStamp : %lld ", timeStamp);
            cbArg.timestamp = timeStamp;
            int32_t rc = pme->m_cbNotifier.notifyCallback(cbArg);
            if (rc != NO_ERROR) {
                LOGE("fail sending data notify");
                stream->bufDone(frame->buf_idx);
            }
        }
    }

    free(super_frame);
    LOGD("[KPI Perf] : END");
}

/*===========================================================================
 * FUNCTION   : snapshot_channel_cb_routine
 *
 * DESCRIPTION: helper function to handle snapshot frame from snapshot channel
 *
 * PARAMETERS :
 *   @super_frame : received super buffer
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : recvd_frame will be released after this call by caller, so if
 *             async operation needed for recvd_frame, it's our responsibility
 *             to save a copy for this variable to be used later.
 *==========================================================================*/
void QCamera2HardwareInterface::snapshot_channel_cb_routine(mm_camera_super_buf_t *super_frame,
       void *userdata)
{
    ATRACE_CALL();
    char value[PROPERTY_VALUE_MAX];
    QCameraChannel *pChannel = NULL;

    LOGH("[KPI Perf]: E");
    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;
    if (pme == NULL ||
        pme->mCameraHandle == NULL ||
        pme->mCameraHandle->camera_handle != super_frame->camera_handle){
        LOGE("camera obj not valid");
        // simply free super frame
        free(super_frame);
        return;
    }

    if (pme->isLowPowerMode()) {
        pChannel = pme->m_channels[QCAMERA_CH_TYPE_VIDEO];
    } else {
        pChannel = pme->m_channels[QCAMERA_CH_TYPE_SNAPSHOT];
    }

    if ((pChannel == NULL) || (pChannel->getMyHandle() != super_frame->ch_id)) {
        LOGE("Snapshot channel doesn't exist, return here");
        return;
    }

    property_get("persist.camera.dumpmetadata", value, "0");
    int32_t enabled = atoi(value);
    if (enabled) {
        if (pChannel == NULL ||
            pChannel->getMyHandle() != super_frame->ch_id) {
            LOGE("Capture channel doesn't exist, return here");
            return;
        }
        mm_camera_buf_def_t *pMetaFrame = NULL;
        QCameraStream *pStream = NULL;
        for (uint32_t i = 0; i < super_frame->num_bufs; i++) {
            pStream = pChannel->getStreamByHandle(super_frame->bufs[i]->stream_id);
            if (pStream != NULL) {
                if (pStream->isTypeOf(CAM_STREAM_TYPE_METADATA)) {
                    pMetaFrame = super_frame->bufs[i]; //find the metadata
                    if (pMetaFrame != NULL &&
                            ((metadata_buffer_t *)pMetaFrame->buffer)->is_tuning_params_valid) {
                        pme->dumpMetadataToFile(pStream, pMetaFrame, (char *) "Snapshot");
                    }
                    break;
                }
            }
        }
    }

    // save a copy for the superbuf
    mm_camera_super_buf_t* frame = (mm_camera_super_buf_t *)malloc(sizeof(mm_camera_super_buf_t));
    if (frame == NULL) {
        LOGE("Error allocating memory to save received_frame structure.");
        pChannel->bufDone(super_frame);
        return;
    }
    *frame = *super_frame;

    if (frame->num_bufs > 0) {
        LOGI("[KPI Perf]: superbuf frame_idx %d",
                frame->bufs[0]->frame_idx);
    }

    if ((NO_ERROR != pme->waitDeferredWork(pme->mReprocJob)) ||
            (NO_ERROR != pme->m_postprocessor.processData(frame))) {
        LOGE("Failed to trigger process data");
        pChannel->bufDone(super_frame);
        free(frame);
        frame = NULL;
        return;
    }

    LOGH("[KPI Perf]: X");
}

/*===========================================================================
 * FUNCTION   : raw_stream_cb_routine
 *
 * DESCRIPTION: helper function to handle raw dump frame from raw stream
 *
 * PARAMETERS :
 *   @super_frame : received super buffer
 *   @stream      : stream object
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : caller passes the ownership of super_frame, it's our
 *             responsibility to free super_frame once it's done. For raw
 *             frame, there is no need to send to postprocessor for jpeg
 *             encoding. this function will play shutter and send the data
 *             callback to upper layer. Raw frame buffer will be returned
 *             back to kernel, and frame will be free after use.
 *==========================================================================*/
void QCamera2HardwareInterface::raw_stream_cb_routine(mm_camera_super_buf_t * super_frame,
                                                      QCameraStream * /*stream*/,
                                                      void * userdata)
{
    ATRACE_CALL();
    LOGH("[KPI Perf] : BEGIN");
    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;
    if (pme == NULL ||
        pme->mCameraHandle == NULL ||
        pme->mCameraHandle->camera_handle != super_frame->camera_handle){
        LOGE("camera obj not valid");
        // simply free super frame
        free(super_frame);
        return;
    }

    pme->m_postprocessor.processRawData(super_frame);
    LOGH("[KPI Perf] : END");
}

/*===========================================================================
 * FUNCTION   : raw_channel_cb_routine
 *
 * DESCRIPTION: helper function to handle RAW  superbuf callback directly from
 *              mm-camera-interface
 *
 * PARAMETERS :
 *   @super_frame : received super buffer
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : recvd_frame will be released after this call by caller, so if
 *             async operation needed for recvd_frame, it's our responsibility
 *             to save a copy for this variable to be used later.
*==========================================================================*/
void QCamera2HardwareInterface::raw_channel_cb_routine(mm_camera_super_buf_t *super_frame,
        void *userdata)

{
    ATRACE_CALL();
    char value[PROPERTY_VALUE_MAX];

    LOGH("[KPI Perf]: E");
    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;
    if (pme == NULL ||
        pme->mCameraHandle == NULL ||
        pme->mCameraHandle->camera_handle != super_frame->camera_handle){
        LOGE("camera obj not valid");
        // simply free super frame
        free(super_frame);
        return;
    }

    QCameraChannel *pChannel = pme->m_channels[QCAMERA_CH_TYPE_RAW];
    if (pChannel == NULL) {
        LOGE("RAW channel doesn't exist, return here");
        return;
    }

    if (pChannel->getMyHandle() != super_frame->ch_id) {
        LOGE("Invalid Input super buffer");
        pChannel->bufDone(super_frame);
        return;
    }

    property_get("persist.camera.dumpmetadata", value, "0");
    int32_t enabled = atoi(value);
    if (enabled) {
        mm_camera_buf_def_t *pMetaFrame = NULL;
        QCameraStream *pStream = NULL;
        for (uint32_t i = 0; i < super_frame->num_bufs; i++) {
            pStream = pChannel->getStreamByHandle(super_frame->bufs[i]->stream_id);
            if (pStream != NULL) {
                if (pStream->isTypeOf(CAM_STREAM_TYPE_METADATA)) {
                    pMetaFrame = super_frame->bufs[i]; //find the metadata
                    if (pMetaFrame != NULL &&
                            ((metadata_buffer_t *)pMetaFrame->buffer)->is_tuning_params_valid) {
                        pme->dumpMetadataToFile(pStream, pMetaFrame, (char *) "raw");
                    }
                    break;
                }
            }
        }
    }

    // save a copy for the superbuf
    mm_camera_super_buf_t* frame = (mm_camera_super_buf_t *)malloc(sizeof(mm_camera_super_buf_t));
    if (frame == NULL) {
        LOGE("Error allocating memory to save received_frame structure.");
        pChannel->bufDone(super_frame);
        return;
    }
    *frame = *super_frame;

    if (frame->num_bufs > 0) {
        LOGI("[KPI Perf]: superbuf frame_idx %d",
                frame->bufs[0]->frame_idx);
    }

    // Wait on Postproc initialization if needed
    // then send to postprocessor
    if ((NO_ERROR != pme->waitDeferredWork(pme->mReprocJob)) ||
            (NO_ERROR != pme->m_postprocessor.processData(frame))) {
        LOGE("Failed to trigger process data");
        pChannel->bufDone(super_frame);
        free(frame);
        frame = NULL;
        return;
    }

    LOGH("[KPI Perf]: X");

}

/*===========================================================================
 * FUNCTION   : preview_raw_stream_cb_routine
 *
 * DESCRIPTION: helper function to handle raw frame during standard preview
 *
 * PARAMETERS :
 *   @super_frame : received super buffer
 *   @stream      : stream object
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : caller passes the ownership of super_frame, it's our
 *             responsibility to free super_frame once it's done.
 *==========================================================================*/
void QCamera2HardwareInterface::preview_raw_stream_cb_routine(mm_camera_super_buf_t * super_frame,
                                                              QCameraStream * stream,
                                                              void * userdata)
{
    ATRACE_CALL();
    LOGH("[KPI Perf] : BEGIN");
    char value[PROPERTY_VALUE_MAX];
    bool dump_preview_raw = false, dump_video_raw = false;

    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;
    if (pme == NULL ||
        pme->mCameraHandle == NULL ||
        pme->mCameraHandle->camera_handle != super_frame->camera_handle){
        LOGE("camera obj not valid");
        // simply free super frame
        free(super_frame);
        return;
    }

    mm_camera_buf_def_t *raw_frame = super_frame->bufs[0];

    if (raw_frame != NULL) {
        property_get("persist.camera.preview_raw", value, "0");
        dump_preview_raw = atoi(value) > 0 ? true : false;
        property_get("persist.camera.video_raw", value, "0");
        dump_video_raw = atoi(value) > 0 ? true : false;
        if (dump_preview_raw || (pme->mParameters.getRecordingHintValue()
                && dump_video_raw)) {
            pme->dumpFrameToFile(stream, raw_frame, QCAMERA_DUMP_FRM_RAW);
        }
        stream->bufDone(raw_frame->buf_idx);
    }
    free(super_frame);

    LOGH("[KPI Perf] : END");
}

/*===========================================================================
 * FUNCTION   : snapshot_raw_stream_cb_routine
 *
 * DESCRIPTION: helper function to handle raw frame during standard capture
 *
 * PARAMETERS :
 *   @super_frame : received super buffer
 *   @stream      : stream object
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : caller passes the ownership of super_frame, it's our
 *             responsibility to free super_frame once it's done.
 *==========================================================================*/
void QCamera2HardwareInterface::snapshot_raw_stream_cb_routine(mm_camera_super_buf_t * super_frame,
                                                               QCameraStream * stream,
                                                               void * userdata)
{
    ATRACE_CALL();
    LOGH("[KPI Perf] : BEGIN");
    char value[PROPERTY_VALUE_MAX];
    bool dump_raw = false;

    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;
    if (pme == NULL ||
        pme->mCameraHandle == NULL ||
        pme->mCameraHandle->camera_handle != super_frame->camera_handle){
        LOGE("camera obj not valid");
        // simply free super frame
        free(super_frame);
        return;
    }

    property_get("persist.camera.snapshot_raw", value, "0");
    dump_raw = atoi(value) > 0 ? true : false;

    for (uint32_t i = 0; i < super_frame->num_bufs; i++) {
        if (super_frame->bufs[i]->stream_type == CAM_STREAM_TYPE_RAW) {
            mm_camera_buf_def_t * raw_frame = super_frame->bufs[i];
            if (NULL != stream) {
                if (dump_raw) {
                    pme->dumpFrameToFile(stream, raw_frame, QCAMERA_DUMP_FRM_RAW);
                }
                stream->bufDone(super_frame->bufs[i]->buf_idx);
            }
            break;
        }
    }

    free(super_frame);

    LOGH("[KPI Perf] : END");
}

/*===========================================================================
 * FUNCTION   : updateMetadata
 *
 * DESCRIPTION: Frame related parameter can be updated here
 *
 * PARAMETERS :
 *   @pMetaData : pointer to metadata buffer
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCamera2HardwareInterface::updateMetadata(metadata_buffer_t *pMetaData)
{
    int32_t rc = NO_ERROR;

    if (pMetaData == NULL) {
        LOGE("Null Metadata buffer");
        return rc;
    }

    // Sharpness
    cam_edge_application_t edge_application;
    memset(&edge_application, 0x00, sizeof(cam_edge_application_t));
    edge_application.sharpness = mParameters.getSharpness();
    if (edge_application.sharpness != 0) {
        edge_application.edge_mode = CAM_EDGE_MODE_FAST;
    } else {
        edge_application.edge_mode = CAM_EDGE_MODE_OFF;
    }
    ADD_SET_PARAM_ENTRY_TO_BATCH(pMetaData,
            CAM_INTF_META_EDGE_MODE, edge_application);

    //Effect
    int32_t prmEffect = mParameters.getEffect();
    ADD_SET_PARAM_ENTRY_TO_BATCH(pMetaData, CAM_INTF_PARM_EFFECT, prmEffect);

    //flip
    int32_t prmFlip = mParameters.getFlipMode(CAM_STREAM_TYPE_SNAPSHOT);
    ADD_SET_PARAM_ENTRY_TO_BATCH(pMetaData, CAM_INTF_PARM_FLIP, prmFlip);

    //denoise
    uint8_t prmDenoise = (uint8_t)mParameters.isWNREnabled();
    ADD_SET_PARAM_ENTRY_TO_BATCH(pMetaData,
            CAM_INTF_META_NOISE_REDUCTION_MODE, prmDenoise);

    //rotation & device rotation
    uint32_t prmRotation = mParameters.getJpegRotation();
    cam_rotation_info_t rotation_info;
    memset(&rotation_info, 0, sizeof(cam_rotation_info_t));
    if (prmRotation == 0) {
       rotation_info.rotation = ROTATE_0;
    } else if (prmRotation == 90) {
       rotation_info.rotation = ROTATE_90;
    } else if (prmRotation == 180) {
       rotation_info.rotation = ROTATE_180;
    } else if (prmRotation == 270) {
       rotation_info.rotation = ROTATE_270;
    }

    uint32_t device_rotation = mParameters.getDeviceRotation();
    if (device_rotation == 0) {
        rotation_info.device_rotation = ROTATE_0;
    } else if (device_rotation == 90) {
        rotation_info.device_rotation = ROTATE_90;
    } else if (device_rotation == 180) {
        rotation_info.device_rotation = ROTATE_180;
    } else if (device_rotation == 270) {
        rotation_info.device_rotation = ROTATE_270;
    } else {
        rotation_info.device_rotation = ROTATE_0;
    }

    ADD_SET_PARAM_ENTRY_TO_BATCH(pMetaData, CAM_INTF_PARM_ROTATION, rotation_info);

    // Imglib Dynamic Scene Data
    cam_dyn_img_data_t dyn_img_data = mParameters.getDynamicImgData();
    if (mParameters.isStillMoreEnabled()) {
        cam_still_more_t stillmore_cap = mParameters.getStillMoreSettings();
        dyn_img_data.input_count = stillmore_cap.burst_count;
    }
    ADD_SET_PARAM_ENTRY_TO_BATCH(pMetaData,
            CAM_INTF_META_IMG_DYN_FEAT, dyn_img_data);

    //CPP CDS
    int32_t prmCDSMode = mParameters.getCDSMode();
    ADD_SET_PARAM_ENTRY_TO_BATCH(pMetaData,
            CAM_INTF_PARM_CDS_MODE, prmCDSMode);

    return rc;
}

/*===========================================================================
 * FUNCTION   : metadata_stream_cb_routine
 *
 * DESCRIPTION: helper function to handle metadata frame from metadata stream
 *
 * PARAMETERS :
 *   @super_frame : received super buffer
 *   @stream      : stream object
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : caller passes the ownership of super_frame, it's our
 *             responsibility to free super_frame once it's done. Metadata
 *             could have valid entries for face detection result or
 *             histogram statistics information.
 *==========================================================================*/
void QCamera2HardwareInterface::metadata_stream_cb_routine(mm_camera_super_buf_t * super_frame,
                                                           QCameraStream * stream,
                                                           void * userdata)
{
    ATRACE_CALL();
    LOGD("[KPI Perf] : BEGIN");
    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;
    if (pme == NULL ||
        pme->mCameraHandle == NULL ||
        pme->mCameraHandle->camera_handle != super_frame->camera_handle){
        LOGE("camera obj not valid");
        // simply free super frame
        free(super_frame);
        return;
    }

    mm_camera_buf_def_t *frame = super_frame->bufs[0];
    metadata_buffer_t *pMetaData = (metadata_buffer_t *)frame->buffer;
    if(pme->m_stateMachine.isNonZSLCaptureRunning()&&
       !pme->mLongshotEnabled) {
       //Make shutter call back in non ZSL mode once raw frame is received from VFE.
       pme->playShutter();
    }

    if (pMetaData->is_tuning_params_valid && pme->mParameters.getRecordingHintValue() == true) {
        //Dump Tuning data for video
        pme->dumpMetadataToFile(stream,frame,(char *)"Video");
    }

    IF_META_AVAILABLE(cam_hist_stats_t, stats_data, CAM_INTF_META_HISTOGRAM, pMetaData) {
        // process histogram statistics info
        qcamera_sm_internal_evt_payload_t *payload =
            (qcamera_sm_internal_evt_payload_t *)
                malloc(sizeof(qcamera_sm_internal_evt_payload_t));
        if (NULL != payload) {
            memset(payload, 0, sizeof(qcamera_sm_internal_evt_payload_t));
            payload->evt_type = QCAMERA_INTERNAL_EVT_HISTOGRAM_STATS;
            payload->stats_data = *stats_data;
            int32_t rc = pme->processEvt(QCAMERA_SM_EVT_EVT_INTERNAL, payload);
            if (rc != NO_ERROR) {
                LOGW("processEvt histogram failed");
                free(payload);
                payload = NULL;

            }
        } else {
            LOGE("No memory for histogram qcamera_sm_internal_evt_payload_t");
        }
    }

    IF_META_AVAILABLE(cam_face_detection_data_t, detection_data,
            CAM_INTF_META_FACE_DETECTION, pMetaData) {

        cam_faces_data_t faces_data;
        pme->fillFacesData(faces_data, pMetaData);
        faces_data.detection_data.fd_type = QCAMERA_FD_PREVIEW; //HARD CODE here before MCT can support

        qcamera_sm_internal_evt_payload_t *payload = (qcamera_sm_internal_evt_payload_t *)
            malloc(sizeof(qcamera_sm_internal_evt_payload_t));
        if (NULL != payload) {
            memset(payload, 0, sizeof(qcamera_sm_internal_evt_payload_t));
            payload->evt_type = QCAMERA_INTERNAL_EVT_FACE_DETECT_RESULT;
            payload->faces_data = faces_data;
            int32_t rc = pme->processEvt(QCAMERA_SM_EVT_EVT_INTERNAL, payload);
            if (rc != NO_ERROR) {
                LOGW("processEvt face detection failed");
                free(payload);
                payload = NULL;
            }
        } else {
            LOGE("No memory for face detect qcamera_sm_internal_evt_payload_t");
        }
    }

    IF_META_AVAILABLE(uint32_t, afState, CAM_INTF_META_AF_STATE, pMetaData) {
        uint8_t forceAFUpdate = FALSE;
        //1. Earlier HAL used to rely on AF done flags set in metadata to generate callbacks to
        //upper layers. But in scenarios where metadata drops especially which contain important
        //AF information, APP will wait indefinitely for focus result resulting in capture hang.
        //2. HAL can check for AF state transitions to generate AF state callbacks to upper layers.
        //This will help overcome metadata drop issue with the earlier approach.
        //3. But sometimes AF state transitions can happen so fast within same metadata due to
        //which HAL will receive only the final AF state. HAL may perceive this as no change in AF
        //state depending on the state transitions happened (for example state A -> B -> A).
        //4. To overcome the drawbacks of both the approaches, we go for a hybrid model in which
        //we check state transition at both HAL level and AF module level. We rely on
        //'state transition' meta field set by AF module for the state transition detected by it.
        IF_META_AVAILABLE(uint8_t, stateChange, CAM_INTF_AF_STATE_TRANSITION, pMetaData) {
            forceAFUpdate = *stateChange;
        }
        //This is a special scenario in which when scene modes like landscape are selected, AF mode
        //gets changed to INFINITY at backend, but HAL will not be aware of it. Also, AF state in
        //such cases will be set to CAM_AF_STATE_INACTIVE by backend. So, detect the AF mode
        //change here and trigger AF callback @ processAutoFocusEvent().
        IF_META_AVAILABLE(uint32_t, afFocusMode, CAM_INTF_PARM_FOCUS_MODE, pMetaData) {
            if (((cam_focus_mode_type)(*afFocusMode) == CAM_FOCUS_MODE_INFINITY) &&
                    pme->mActiveAF){
                forceAFUpdate = TRUE;
            }
        }
        if ((pme->m_currentFocusState != (*afState)) || forceAFUpdate) {
            cam_af_state_t prevFocusState = pme->m_currentFocusState;
            pme->m_currentFocusState = (cam_af_state_t)(*afState);
            qcamera_sm_internal_evt_payload_t *payload = (qcamera_sm_internal_evt_payload_t *)
                    malloc(sizeof(qcamera_sm_internal_evt_payload_t));
            if (NULL != payload) {
                memset(payload, 0, sizeof(qcamera_sm_internal_evt_payload_t));
                payload->evt_type = QCAMERA_INTERNAL_EVT_FOCUS_UPDATE;
                payload->focus_data.focus_state = (cam_af_state_t)(*afState);
                //Need to flush ZSL Q only if we are transitioning from scanning state
                //to focused/not focused state.
                payload->focus_data.flush_info.needFlush =
                        ((prevFocusState == CAM_AF_STATE_PASSIVE_SCAN) ||
                        (prevFocusState == CAM_AF_STATE_ACTIVE_SCAN)) &&
                        ((pme->m_currentFocusState == CAM_AF_STATE_FOCUSED_LOCKED) ||
                        (pme->m_currentFocusState == CAM_AF_STATE_NOT_FOCUSED_LOCKED));
                payload->focus_data.flush_info.focused_frame_idx = frame->frame_idx;

                IF_META_AVAILABLE(float, focusDistance,
                        CAM_INTF_META_LENS_FOCUS_DISTANCE, pMetaData) {
                    payload->focus_data.focus_dist.
                    focus_distance[CAM_FOCUS_DISTANCE_OPTIMAL_INDEX] = *focusDistance;
                }
                IF_META_AVAILABLE(float, focusRange, CAM_INTF_META_LENS_FOCUS_RANGE, pMetaData) {
                    payload->focus_data.focus_dist.
                            focus_distance[CAM_FOCUS_DISTANCE_NEAR_INDEX] = focusRange[0];
                    payload->focus_data.focus_dist.
                            focus_distance[CAM_FOCUS_DISTANCE_FAR_INDEX] = focusRange[1];
                }
                IF_META_AVAILABLE(uint32_t, focusMode, CAM_INTF_PARM_FOCUS_MODE, pMetaData) {
                    payload->focus_data.focus_mode = (cam_focus_mode_type)(*focusMode);
                }
                IF_META_AVAILABLE(uint8_t, isDepthFocus,
                        CAM_INTF_META_FOCUS_DEPTH_INFO, pMetaData) {
                    payload->focus_data.isDepth = *isDepthFocus;
                }
                int32_t rc = pme->processEvt(QCAMERA_SM_EVT_EVT_INTERNAL, payload);
                if (rc != NO_ERROR) {
                    LOGW("processEvt focus failed");
                    free(payload);
                    payload = NULL;
                }
            } else {
                LOGE("No memory for focus qcamera_sm_internal_evt_payload_t");
            }
        }
    }

    IF_META_AVAILABLE(cam_crop_data_t, crop_data, CAM_INTF_META_CROP_DATA, pMetaData) {
        if (crop_data->num_of_streams > MAX_NUM_STREAMS) {
            LOGE("Invalid num_of_streams %d in crop_data",
                crop_data->num_of_streams);
        } else {
            qcamera_sm_internal_evt_payload_t *payload =
                (qcamera_sm_internal_evt_payload_t *)
                    malloc(sizeof(qcamera_sm_internal_evt_payload_t));
            if (NULL != payload) {
                memset(payload, 0, sizeof(qcamera_sm_internal_evt_payload_t));
                payload->evt_type = QCAMERA_INTERNAL_EVT_CROP_INFO;
                payload->crop_data = *crop_data;
                int32_t rc = pme->processEvt(QCAMERA_SM_EVT_EVT_INTERNAL, payload);
                if (rc != NO_ERROR) {
                    LOGE("processEvt crop info failed");
                    free(payload);
                    payload = NULL;
                }
            } else {
                LOGE("No memory for prep_snapshot qcamera_sm_internal_evt_payload_t");
            }
        }
    }

    IF_META_AVAILABLE(int32_t, prep_snapshot_done_state,
            CAM_INTF_META_PREP_SNAPSHOT_DONE, pMetaData) {
        qcamera_sm_internal_evt_payload_t *payload =
        (qcamera_sm_internal_evt_payload_t *)malloc(sizeof(qcamera_sm_internal_evt_payload_t));
        if (NULL != payload) {
            memset(payload, 0, sizeof(qcamera_sm_internal_evt_payload_t));
            payload->evt_type = QCAMERA_INTERNAL_EVT_PREP_SNAPSHOT_DONE;
            payload->prep_snapshot_state = (cam_prep_snapshot_state_t)*prep_snapshot_done_state;
            int32_t rc = pme->processEvt(QCAMERA_SM_EVT_EVT_INTERNAL, payload);
            if (rc != NO_ERROR) {
                LOGW("processEvt prep_snapshot failed");
                free(payload);
                payload = NULL;
            }
        } else {
            LOGE("No memory for prep_snapshot qcamera_sm_internal_evt_payload_t");
        }
    }

    IF_META_AVAILABLE(cam_asd_hdr_scene_data_t, hdr_scene_data,
            CAM_INTF_META_ASD_HDR_SCENE_DATA, pMetaData) {
        LOGH("hdr_scene_data: %d %f\n",
                hdr_scene_data->is_hdr_scene, hdr_scene_data->hdr_confidence);
        //Handle this HDR meta data only if capture is not in process
        if (!pme->m_stateMachine.isCaptureRunning()) {
            qcamera_sm_internal_evt_payload_t *payload =
                    (qcamera_sm_internal_evt_payload_t *)
                    malloc(sizeof(qcamera_sm_internal_evt_payload_t));
            if (NULL != payload) {
                memset(payload, 0, sizeof(qcamera_sm_internal_evt_payload_t));
                payload->evt_type = QCAMERA_INTERNAL_EVT_HDR_UPDATE;
                payload->hdr_data = *hdr_scene_data;
                int32_t rc = pme->processEvt(QCAMERA_SM_EVT_EVT_INTERNAL, payload);
                if (rc != NO_ERROR) {
                    LOGW("processEvt hdr update failed");
                    free(payload);
                    payload = NULL;
                }
            } else {
                LOGE("No memory for hdr update qcamera_sm_internal_evt_payload_t");
            }
        }
    }

    IF_META_AVAILABLE(cam_asd_decision_t, cam_asd_info,
            CAM_INTF_META_ASD_SCENE_INFO, pMetaData) {
        qcamera_sm_internal_evt_payload_t *payload =
            (qcamera_sm_internal_evt_payload_t *)malloc(sizeof(qcamera_sm_internal_evt_payload_t));
        if (NULL != payload) {
            memset(payload, 0, sizeof(qcamera_sm_internal_evt_payload_t));
            payload->evt_type = QCAMERA_INTERNAL_EVT_ASD_UPDATE;
            payload->asd_data = (cam_asd_decision_t)*cam_asd_info;
            int32_t rc = pme->processEvt(QCAMERA_SM_EVT_EVT_INTERNAL, payload);
            if (rc != NO_ERROR) {
                LOGW("processEvt asd_update failed");
                free(payload);
                payload = NULL;
            }
        } else {
            LOGE("No memory for asd_update qcamera_sm_internal_evt_payload_t");
        }
    }

    IF_META_AVAILABLE(cam_awb_params_t, awb_params, CAM_INTF_META_AWB_INFO, pMetaData) {
        LOGH(", metadata for awb params.");
        qcamera_sm_internal_evt_payload_t *payload =
                (qcamera_sm_internal_evt_payload_t *)
                malloc(sizeof(qcamera_sm_internal_evt_payload_t));
        if (NULL != payload) {
            memset(payload, 0, sizeof(qcamera_sm_internal_evt_payload_t));
            payload->evt_type = QCAMERA_INTERNAL_EVT_AWB_UPDATE;
            payload->awb_data = *awb_params;
            int32_t rc = pme->processEvt(QCAMERA_SM_EVT_EVT_INTERNAL, payload);
            if (rc != NO_ERROR) {
                LOGW("processEvt awb_update failed");
                free(payload);
                payload = NULL;
            }
        } else {
            LOGE("No memory for awb_update qcamera_sm_internal_evt_payload_t");
        }
    }

    IF_META_AVAILABLE(uint32_t, flash_mode, CAM_INTF_META_FLASH_MODE, pMetaData) {
        pme->mExifParams.sensor_params.flash_mode = (cam_flash_mode_t)*flash_mode;
    }

    IF_META_AVAILABLE(int32_t, flash_state, CAM_INTF_META_FLASH_STATE, pMetaData) {
        pme->mExifParams.sensor_params.flash_state = (cam_flash_state_t) *flash_state;
    }

    IF_META_AVAILABLE(float, aperture_value, CAM_INTF_META_LENS_APERTURE, pMetaData) {
        pme->mExifParams.sensor_params.aperture_value = *aperture_value;
    }

    IF_META_AVAILABLE(cam_3a_params_t, ae_params, CAM_INTF_META_AEC_INFO, pMetaData) {
        pme->mExifParams.cam_3a_params = *ae_params;
        pme->mExifParams.cam_3a_params_valid = TRUE;
        pme->mFlashNeeded = ae_params->flash_needed;
        pme->mExifParams.cam_3a_params.brightness = (float) pme->mParameters.getBrightness();
        qcamera_sm_internal_evt_payload_t *payload =
                (qcamera_sm_internal_evt_payload_t *)
                malloc(sizeof(qcamera_sm_internal_evt_payload_t));
        if (NULL != payload) {
            memset(payload, 0, sizeof(qcamera_sm_internal_evt_payload_t));
            payload->evt_type = QCAMERA_INTERNAL_EVT_AE_UPDATE;
            payload->ae_data = *ae_params;
            int32_t rc = pme->processEvt(QCAMERA_SM_EVT_EVT_INTERNAL, payload);
            if (rc != NO_ERROR) {
                LOGW("processEvt ae_update failed");
                free(payload);
                payload = NULL;
            }
        } else {
            LOGE("No memory for ae_update qcamera_sm_internal_evt_payload_t");
        }
    }

    IF_META_AVAILABLE(int32_t, wb_mode, CAM_INTF_PARM_WHITE_BALANCE, pMetaData) {
        pme->mExifParams.cam_3a_params.wb_mode = (cam_wb_mode_type) *wb_mode;
    }

    IF_META_AVAILABLE(cam_sensor_params_t, sensor_params, CAM_INTF_META_SENSOR_INFO, pMetaData) {
        pme->mExifParams.sensor_params = *sensor_params;
    }

    IF_META_AVAILABLE(cam_ae_exif_debug_t, ae_exif_debug_params,
            CAM_INTF_META_EXIF_DEBUG_AE, pMetaData) {
        if (pme->mExifParams.debug_params) {
            pme->mExifParams.debug_params->ae_debug_params = *ae_exif_debug_params;
            pme->mExifParams.debug_params->ae_debug_params_valid = TRUE;
        }
    }

    IF_META_AVAILABLE(cam_awb_exif_debug_t, awb_exif_debug_params,
            CAM_INTF_META_EXIF_DEBUG_AWB, pMetaData) {
        if (pme->mExifParams.debug_params) {
            pme->mExifParams.debug_params->awb_debug_params = *awb_exif_debug_params;
            pme->mExifParams.debug_params->awb_debug_params_valid = TRUE;
        }
    }

    IF_META_AVAILABLE(cam_af_exif_debug_t, af_exif_debug_params,
            CAM_INTF_META_EXIF_DEBUG_AF, pMetaData) {
        if (pme->mExifParams.debug_params) {
            pme->mExifParams.debug_params->af_debug_params = *af_exif_debug_params;
            pme->mExifParams.debug_params->af_debug_params_valid = TRUE;
        }
    }

    IF_META_AVAILABLE(cam_asd_exif_debug_t, asd_exif_debug_params,
            CAM_INTF_META_EXIF_DEBUG_ASD, pMetaData) {
        if (pme->mExifParams.debug_params) {
            pme->mExifParams.debug_params->asd_debug_params = *asd_exif_debug_params;
            pme->mExifParams.debug_params->asd_debug_params_valid = TRUE;
        }
    }

    IF_META_AVAILABLE(cam_stats_buffer_exif_debug_t, stats_exif_debug_params,
            CAM_INTF_META_EXIF_DEBUG_STATS, pMetaData) {
        if (pme->mExifParams.debug_params) {
            pme->mExifParams.debug_params->stats_debug_params = *stats_exif_debug_params;
            pme->mExifParams.debug_params->stats_debug_params_valid = TRUE;
        }
    }

    IF_META_AVAILABLE(cam_bestats_buffer_exif_debug_t, bestats_exif_debug_params,
            CAM_INTF_META_EXIF_DEBUG_BESTATS, pMetaData) {
        if (pme->mExifParams.debug_params) {
            pme->mExifParams.debug_params->bestats_debug_params = *bestats_exif_debug_params;
            pme->mExifParams.debug_params->bestats_debug_params_valid = TRUE;
        }
    }

    IF_META_AVAILABLE(cam_bhist_buffer_exif_debug_t, bhist_exif_debug_params,
            CAM_INTF_META_EXIF_DEBUG_BHIST, pMetaData) {
        if (pme->mExifParams.debug_params) {
            pme->mExifParams.debug_params->bhist_debug_params = *bhist_exif_debug_params;
            pme->mExifParams.debug_params->bhist_debug_params_valid = TRUE;
        }
    }

    IF_META_AVAILABLE(cam_q3a_tuning_info_t, q3a_tuning_exif_debug_params,
            CAM_INTF_META_EXIF_DEBUG_3A_TUNING, pMetaData) {
        if (pme->mExifParams.debug_params) {
            pme->mExifParams.debug_params->q3a_tuning_debug_params = *q3a_tuning_exif_debug_params;
            pme->mExifParams.debug_params->q3a_tuning_debug_params_valid = TRUE;
        }
    }

    IF_META_AVAILABLE(uint32_t, led_mode, CAM_INTF_META_LED_MODE_OVERRIDE, pMetaData) {
        qcamera_sm_internal_evt_payload_t *payload =
                (qcamera_sm_internal_evt_payload_t *)
                malloc(sizeof(qcamera_sm_internal_evt_payload_t));
        if (NULL != payload) {
            memset(payload, 0, sizeof(qcamera_sm_internal_evt_payload_t));
            payload->evt_type = QCAMERA_INTERNAL_EVT_LED_MODE_OVERRIDE;
            payload->led_data = (cam_flash_mode_t)*led_mode;
            int32_t rc = pme->processEvt(QCAMERA_SM_EVT_EVT_INTERNAL, payload);
            if (rc != NO_ERROR) {
                LOGW("processEvt led mode override failed");
                free(payload);
                payload = NULL;
            }
        } else {
            LOGE("No memory for focus qcamera_sm_internal_evt_payload_t");
        }
    }

    cam_edge_application_t edge_application;
    memset(&edge_application, 0x00, sizeof(cam_edge_application_t));
    edge_application.sharpness = pme->mParameters.getSharpness();
    if (edge_application.sharpness != 0) {
        edge_application.edge_mode = CAM_EDGE_MODE_FAST;
    } else {
        edge_application.edge_mode = CAM_EDGE_MODE_OFF;
    }
    ADD_SET_PARAM_ENTRY_TO_BATCH(pMetaData, CAM_INTF_META_EDGE_MODE, edge_application);

    IF_META_AVAILABLE(cam_focus_pos_info_t, cur_pos_info,
            CAM_INTF_META_FOCUS_POSITION, pMetaData) {
        qcamera_sm_internal_evt_payload_t *payload =
            (qcamera_sm_internal_evt_payload_t *)malloc(sizeof(qcamera_sm_internal_evt_payload_t));
        if (NULL != payload) {
            memset(payload, 0, sizeof(qcamera_sm_internal_evt_payload_t));
            payload->evt_type = QCAMERA_INTERNAL_EVT_FOCUS_POS_UPDATE;
            payload->focus_pos = *cur_pos_info;
            int32_t rc = pme->processEvt(QCAMERA_SM_EVT_EVT_INTERNAL, payload);
            if (rc != NO_ERROR) {
                LOGW("processEvt focus_pos_update failed");
                free(payload);
                payload = NULL;
            }
        } else {
            LOGE("No memory for focus_pos_update qcamera_sm_internal_evt_payload_t");
        }
    }

    if (pme->mParameters.getLowLightCapture()) {
        IF_META_AVAILABLE(cam_low_light_mode_t, low_light_level,
                CAM_INTF_META_LOW_LIGHT, pMetaData) {
            pme->mParameters.setLowLightLevel(*low_light_level);
        }
    }

    IF_META_AVAILABLE(cam_dyn_img_data_t, dyn_img_data,
            CAM_INTF_META_IMG_DYN_FEAT, pMetaData) {
        pme->mParameters.setDynamicImgData(*dyn_img_data);
    }

    IF_META_AVAILABLE(int32_t, touch_ae_status, CAM_INTF_META_TOUCH_AE_RESULT, pMetaData) {
      LOGD("touch_ae_status: %d", *touch_ae_status);
    }

    stream->bufDone(frame->buf_idx);
    free(super_frame);

    LOGD("[KPI Perf] : END");
}

/*===========================================================================
 * FUNCTION   : reprocess_stream_cb_routine
 *
 * DESCRIPTION: helper function to handle reprocess frame from reprocess stream
                (after reprocess, e.g., ZSL snapshot frame after WNR if
 *              WNR is enabled)
 *
 * PARAMETERS :
 *   @super_frame : received super buffer
 *   @stream      : stream object
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *
 * NOTE      : caller passes the ownership of super_frame, it's our
 *             responsibility to free super_frame once it's done. In this
 *             case, reprocessed frame need to be passed to postprocessor
 *             for jpeg encoding.
 *==========================================================================*/
void QCamera2HardwareInterface::reprocess_stream_cb_routine(mm_camera_super_buf_t * super_frame,
                                                            QCameraStream * /*stream*/,
                                                            void * userdata)
{
    ATRACE_CALL();
    LOGH("[KPI Perf]: E");
    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;
    if (pme == NULL ||
        pme->mCameraHandle == NULL ||
        pme->mCameraHandle->camera_handle != super_frame->camera_handle){
        LOGE("camera obj not valid");
        // simply free super frame
        free(super_frame);
        return;
    }

    pme->m_postprocessor.processPPData(super_frame);

    LOGH("[KPI Perf]: X");
}

/*===========================================================================
 * FUNCTION   : callback_stream_cb_routine
 *
 * DESCRIPTION: function to process CALBACK stream data
                           Frame will processed and sent to framework
 *
 * PARAMETERS :
 *   @super_frame : received super buffer
 *   @stream      : stream object
 *   @userdata    : user data ptr
 *
 * RETURN    : None
 *==========================================================================*/
void QCamera2HardwareInterface::callback_stream_cb_routine(mm_camera_super_buf_t *super_frame,
        QCameraStream *stream, void *userdata)
{
    ATRACE_CALL();
    LOGH("[KPI Perf]: E");
    QCamera2HardwareInterface *pme = (QCamera2HardwareInterface *)userdata;

    if (pme == NULL ||
            pme->mCameraHandle == NULL ||
            pme->mCameraHandle->camera_handle != super_frame->camera_handle) {
        LOGE("camera obj not valid");
        // simply free super frame
        free(super_frame);
        return;
    }

    mm_camera_buf_def_t *frame = super_frame->bufs[0];
    if (NULL == frame) {
        LOGE("preview callback frame is NULL");
        free(super_frame);
        return;
    }

    if (!pme->needProcessPreviewFrame(frame->frame_idx)) {
        LOGH("preview is not running, no need to process");
        stream->bufDone(frame->buf_idx);
        free(super_frame);
        return;
    }

    QCameraMemory *previewMemObj = (QCameraMemory *)frame->mem_info;
    // Handle preview data callback
    if (pme->mDataCb != NULL &&
            (pme->msgTypeEnabledWithLock(CAMERA_MSG_PREVIEW_FRAME) > 0) &&
            (!pme->mParameters.isSceneSelectionEnabled())) {
        int32_t rc = pme->sendPreviewCallback(stream, previewMemObj, frame->buf_idx);
        if (NO_ERROR != rc) {
            LOGE("Preview callback was not sent succesfully");
        }
    }
    stream->bufDone(frame->buf_idx);
    free(super_frame);
    LOGH("[KPI Perf]: X");
}

/*===========================================================================
 * FUNCTION   : dumpFrameToFile
 *
 * DESCRIPTION: helper function to dump jpeg into file for debug purpose.
 *
 * PARAMETERS :
 *    @data : data ptr
 *    @size : length of data buffer
 *    @index : identifier for data
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera2HardwareInterface::dumpJpegToFile(const void *data,
        size_t size, uint32_t index)
{
    char value[PROPERTY_VALUE_MAX];
    property_get("persist.camera.dumpimg", value, "0");
    uint32_t enabled = (uint32_t) atoi(value);
    uint32_t frm_num = 0;
    uint32_t skip_mode = 0;

    char buf[32];
    cam_dimension_t dim;
    memset(buf, 0, sizeof(buf));
    memset(&dim, 0, sizeof(dim));

    if(((enabled & QCAMERA_DUMP_FRM_JPEG) && data) ||
        ((true == m_bIntJpegEvtPending) && data)) {
        frm_num = ((enabled & 0xffff0000) >> 16);
        if(frm_num == 0) {
            frm_num = 10; //default 10 frames
        }
        if(frm_num > 256) {
            frm_num = 256; //256 buffers cycle around
        }
        skip_mode = ((enabled & 0x0000ff00) >> 8);
        if(skip_mode == 0) {
            skip_mode = 1; //no-skip
        }

        if( mDumpSkipCnt % skip_mode == 0) {
            if((frm_num == 256) && (mDumpFrmCnt >= frm_num)) {
                // reset frame count if cycling
                mDumpFrmCnt = 0;
            }
            if (mDumpFrmCnt <= frm_num) {
                snprintf(buf, sizeof(buf), QCAMERA_DUMP_FRM_LOCATION "%d_%d.jpg",
                        mDumpFrmCnt, index);
                if (true == m_bIntJpegEvtPending) {
                    strlcpy(m_BackendFileName, buf, QCAMERA_MAX_FILEPATH_LENGTH);
                    mBackendFileSize = size;
                }

                int file_fd = open(buf, O_RDWR | O_CREAT, 0777);
                if (file_fd >= 0) {
                    ssize_t written_len = write(file_fd, data, size);
                    fchmod(file_fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                    LOGH("written number of bytes %zd\n",
                             written_len);
                    close(file_fd);
                } else {
                    LOGE("fail to open file for image dumping");
                }
                if (false == m_bIntJpegEvtPending) {
                    mDumpFrmCnt++;
                }
            }
        }
        mDumpSkipCnt++;
    }
}


void QCamera2HardwareInterface::dumpMetadataToFile(QCameraStream *stream,
                                                   mm_camera_buf_def_t *frame,char *type)
{
    char value[PROPERTY_VALUE_MAX];
    uint32_t frm_num = 0;
    metadata_buffer_t *metadata = (metadata_buffer_t *)frame->buffer;
    property_get("persist.camera.dumpmetadata", value, "0");
    uint32_t enabled = (uint32_t) atoi(value);
    if (stream == NULL) {
        LOGH("No op");
        return;
    }

    uint32_t dumpFrmCnt = stream->mDumpMetaFrame;
    if(enabled){
        frm_num = ((enabled & 0xffff0000) >> 16);
        if (frm_num == 0) {
            frm_num = 10; //default 10 frames
        }
        if (frm_num > 256) {
            frm_num = 256; //256 buffers cycle around
        }
        if ((frm_num == 256) && (dumpFrmCnt >= frm_num)) {
            // reset frame count if cycling
            dumpFrmCnt = 0;
        }
        LOGH("dumpFrmCnt= %u, frm_num = %u", dumpFrmCnt, frm_num);
        if (dumpFrmCnt < frm_num) {
            char timeBuf[128];
            char buf[32];
            memset(buf, 0, sizeof(buf));
            memset(timeBuf, 0, sizeof(timeBuf));
            time_t current_time;
            struct tm * timeinfo;
            time (&current_time);
            timeinfo = localtime (&current_time);
            if (NULL != timeinfo) {
                strftime(timeBuf, sizeof(timeBuf),
                        QCAMERA_DUMP_FRM_LOCATION "%Y%m%d%H%M%S", timeinfo);
            }
            String8 filePath(timeBuf);
            snprintf(buf, sizeof(buf), "%um_%s_%d.bin", dumpFrmCnt, type, frame->frame_idx);
            filePath.append(buf);
            int file_fd = open(filePath.string(), O_RDWR | O_CREAT, 0777);
            if (file_fd >= 0) {
                ssize_t written_len = 0;
                metadata->tuning_params.tuning_data_version = TUNING_DATA_VERSION;
                void *data = (void *)((uint8_t *)&metadata->tuning_params.tuning_data_version);
                written_len += write(file_fd, data, sizeof(uint32_t));
                data = (void *)((uint8_t *)&metadata->tuning_params.tuning_sensor_data_size);
                LOGH("tuning_sensor_data_size %d",(int)(*(int *)data));
                written_len += write(file_fd, data, sizeof(uint32_t));
                data = (void *)((uint8_t *)&metadata->tuning_params.tuning_vfe_data_size);
                LOGH("tuning_vfe_data_size %d",(int)(*(int *)data));
                written_len += write(file_fd, data, sizeof(uint32_t));
                data = (void *)((uint8_t *)&metadata->tuning_params.tuning_cpp_data_size);
                LOGH("tuning_cpp_data_size %d",(int)(*(int *)data));
                written_len += write(file_fd, data, sizeof(uint32_t));
                data = (void *)((uint8_t *)&metadata->tuning_params.tuning_cac_data_size);
                LOGH("tuning_cac_data_size %d",(int)(*(int *)data));
                written_len += write(file_fd, data, sizeof(uint32_t));
                data = (void *)((uint8_t *)&metadata->tuning_params.tuning_cac_data_size2);
                LOGH("< skrajago >tuning_cac_data_size %d",(int)(*(int *)data));
                written_len += write(file_fd, data, sizeof(uint32_t));
                size_t total_size = metadata->tuning_params.tuning_sensor_data_size;
                data = (void *)((uint8_t *)&metadata->tuning_params.data);
                written_len += write(file_fd, data, total_size);
                total_size = metadata->tuning_params.tuning_vfe_data_size;
                data = (void *)((uint8_t *)&metadata->tuning_params.data[TUNING_VFE_DATA_OFFSET]);
                written_len += write(file_fd, data, total_size);
                total_size = metadata->tuning_params.tuning_cpp_data_size;
                data = (void *)((uint8_t *)&metadata->tuning_params.data[TUNING_CPP_DATA_OFFSET]);
                written_len += write(file_fd, data, total_size);
                total_size = metadata->tuning_params.tuning_cac_data_size;
                data = (void *)((uint8_t *)&metadata->tuning_params.data[TUNING_CAC_DATA_OFFSET]);
                written_len += write(file_fd, data, total_size);
                close(file_fd);
            }else {
                LOGE("fail t open file for image dumping");
            }
            dumpFrmCnt++;
        }
    }
    stream->mDumpMetaFrame = dumpFrmCnt;
}
/*===========================================================================
 * FUNCTION   : dumpFrameToFile
 *
 * DESCRIPTION: helper function to dump frame into file for debug purpose.
 *
 * PARAMETERS :
 *    @data : data ptr
 *    @size : length of data buffer
 *    @index : identifier for data
 *    @dump_type : type of the frame to be dumped. Only such
 *                 dump type is enabled, the frame will be
 *                 dumped into a file.
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera2HardwareInterface::dumpFrameToFile(QCameraStream *stream,
        mm_camera_buf_def_t *frame, uint32_t dump_type, const char *misc)
{
    char value[PROPERTY_VALUE_MAX];
    property_get("persist.camera.dumpimg", value, "0");
    uint32_t enabled = (uint32_t) atoi(value);
    uint32_t frm_num = 0;
    uint32_t skip_mode = 0;

    if (NULL == stream) {
        LOGE("stream object is null");
        return;
    }

    uint32_t dumpFrmCnt = stream->mDumpFrame;

    if (true == m_bIntRawEvtPending) {
        enabled = QCAMERA_DUMP_FRM_RAW;
    }

    if((enabled & QCAMERA_DUMP_FRM_MASK_ALL)) {
        if((enabled & dump_type) && stream && frame) {
            frm_num = ((enabled & 0xffff0000) >> 16);
            if(frm_num == 0) {
                frm_num = 10; //default 10 frames
            }
            if(frm_num > 256) {
                frm_num = 256; //256 buffers cycle around
            }
            skip_mode = ((enabled & 0x0000ff00) >> 8);
            if(skip_mode == 0) {
                skip_mode = 1; //no-skip
            }
            if(stream->mDumpSkipCnt == 0)
                stream->mDumpSkipCnt = 1;

            if( stream->mDumpSkipCnt % skip_mode == 0) {
                if((frm_num == 256) && (dumpFrmCnt >= frm_num)) {
                    // reset frame count if cycling
                    dumpFrmCnt = 0;
                }
                if (dumpFrmCnt <= frm_num) {
                    char buf[32];
                    char timeBuf[128];
                    time_t current_time;
                    struct tm * timeinfo;

                    memset(timeBuf, 0, sizeof(timeBuf));

                    time (&current_time);
                    timeinfo = localtime (&current_time);
                    memset(buf, 0, sizeof(buf));

                    cam_dimension_t dim;
                    memset(&dim, 0, sizeof(dim));
                    stream->getFrameDimension(dim);

                    cam_frame_len_offset_t offset;
                    memset(&offset, 0, sizeof(cam_frame_len_offset_t));
                    stream->getFrameOffset(offset);

                    if (NULL != timeinfo) {
                        strftime(timeBuf, sizeof(timeBuf),
                                QCAMERA_DUMP_FRM_LOCATION "%Y%m%d%H%M%S", timeinfo);
                    }
                    String8 filePath(timeBuf);
                    switch (dump_type) {
                    case QCAMERA_DUMP_FRM_PREVIEW:
                        {
                            snprintf(buf, sizeof(buf), "%dp_%dx%d_%d.yuv",
                                    dumpFrmCnt, dim.width, dim.height, frame->frame_idx);
                        }
                        break;
                    case QCAMERA_DUMP_FRM_THUMBNAIL:
                        {
                            snprintf(buf, sizeof(buf), "%dt_%dx%d_%d.yuv",
                                    dumpFrmCnt, dim.width, dim.height, frame->frame_idx);
                        }
                        break;
                    case QCAMERA_DUMP_FRM_SNAPSHOT:
                        {
                            if (!mParameters.isPostProcScaling()) {
                                mParameters.getStreamDimension(CAM_STREAM_TYPE_SNAPSHOT, dim);
                            } else {
                                stream->getFrameDimension(dim);
                            }
                            if (misc != NULL) {
                                snprintf(buf, sizeof(buf), "%ds_%dx%d_%d_%s.yuv",
                                        dumpFrmCnt, dim.width, dim.height, frame->frame_idx, misc);
                            } else {
                                snprintf(buf, sizeof(buf), "%ds_%dx%d_%d.yuv",
                                        dumpFrmCnt, dim.width, dim.height, frame->frame_idx);
                            }
                        }
                        break;
                    case QCAMERA_DUMP_FRM_INPUT_REPROCESS:
                        {
                            stream->getFrameDimension(dim);
                            if (misc != NULL) {
                                snprintf(buf, sizeof(buf), "%dir_%dx%d_%d_%s.yuv",
                                        dumpFrmCnt, dim.width, dim.height, frame->frame_idx, misc);
                            } else {
                                snprintf(buf, sizeof(buf), "%dir_%dx%d_%d.yuv",
                                        dumpFrmCnt, dim.width, dim.height, frame->frame_idx);
                            }
                        }
                        break;
                    case QCAMERA_DUMP_FRM_VIDEO:
                        {
                            snprintf(buf, sizeof(buf), "%dv_%dx%d_%d.yuv",
                                    dumpFrmCnt, dim.width, dim.height, frame->frame_idx);
                        }
                        break;
                    case QCAMERA_DUMP_FRM_RAW:
                        {
                            mParameters.getStreamDimension(CAM_STREAM_TYPE_RAW, dim);
                            snprintf(buf, sizeof(buf), "%dr_%dx%d_%d.raw",
                                    dumpFrmCnt, dim.width, dim.height, frame->frame_idx);
                        }
                        break;
                    case QCAMERA_DUMP_FRM_JPEG:
                        {
                            mParameters.getStreamDimension(CAM_STREAM_TYPE_SNAPSHOT, dim);
                            snprintf(buf, sizeof(buf), "%dj_%dx%d_%d.yuv",
                                    dumpFrmCnt, dim.width, dim.height, frame->frame_idx);
                        }
                        break;
                    default:
                        LOGE("Not supported for dumping stream type %d",
                               dump_type);
                        return;
                    }

                    filePath.append(buf);
                    int file_fd = open(filePath.string(), O_RDWR | O_CREAT, 0777);
                    ssize_t written_len = 0;
                    if (file_fd >= 0) {
                        void *data = NULL;

                        fchmod(file_fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                        for (uint32_t i = 0; i < offset.num_planes; i++) {
                            uint32_t index = offset.mp[i].offset;
                            if (i > 0) {
                                index += offset.mp[i-1].len;
                            }

                            if (offset.mp[i].meta_len != 0) {
                                data = (void *)((uint8_t *)frame->buffer + index);
                                written_len += write(file_fd, data,
                                        (size_t)offset.mp[i].meta_len);
                                index += (uint32_t)offset.mp[i].meta_len;
                            }

                            for (int j = 0; j < offset.mp[i].height; j++) {
                                data = (void *)((uint8_t *)frame->buffer + index);
                                written_len += write(file_fd, data,
                                        (size_t)offset.mp[i].width);
                                index += (uint32_t)offset.mp[i].stride;
                            }
                        }

                        LOGH("written number of bytes %ld\n",
                             written_len);
                        close(file_fd);
                    } else {
                        LOGE("fail to open file for image dumping");
                    }
                    if (true == m_bIntRawEvtPending) {
                        strlcpy(m_BackendFileName, filePath.string(), QCAMERA_MAX_FILEPATH_LENGTH);
                        mBackendFileSize = (size_t)written_len;
                    } else {
                        dumpFrmCnt++;
                    }
                }
            }
            stream->mDumpSkipCnt++;
        }
    } else {
        dumpFrmCnt = 0;
    }
    stream->mDumpFrame = dumpFrmCnt;
}

/*===========================================================================
 * FUNCTION   : debugShowVideoFPS
 *
 * DESCRIPTION: helper function to log video frame FPS for debug purpose.
 *
 * PARAMETERS : None
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera2HardwareInterface::debugShowVideoFPS()
{
    mVFrameCount++;
    nsecs_t now = systemTime();
    nsecs_t diff = now - mVLastFpsTime;
    if (diff > ms2ns(250)) {
        mVFps = (((double)(mVFrameCount - mVLastFrameCount)) *
                (double)(s2ns(1))) / (double)diff;
        LOGI("[KPI Perf]: PROFILE_VIDEO_FRAMES_PER_SECOND: %.4f Cam ID = %d",
                mVFps, mCameraId);
        mVLastFpsTime = now;
        mVLastFrameCount = mVFrameCount;
    }
}

/*===========================================================================
 * FUNCTION   : debugShowPreviewFPS
 *
 * DESCRIPTION: helper function to log preview frame FPS for debug purpose.
 *
 * PARAMETERS : None
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera2HardwareInterface::debugShowPreviewFPS()
{
    mPFrameCount++;
    nsecs_t now = systemTime();
    nsecs_t diff = now - mPLastFpsTime;
    if (diff > ms2ns(250)) {
        mPFps = (((double)(mPFrameCount - mPLastFrameCount)) *
                (double)(s2ns(1))) / (double)diff;
        LOGI("[KPI Perf]: PROFILE_PREVIEW_FRAMES_PER_SECOND : %.4f Cam ID = %d",
                 mPFps, mCameraId);
        mPLastFpsTime = now;
        mPLastFrameCount = mPFrameCount;
    }
}

/*===========================================================================
 * FUNCTION   : fillFacesData
 *
 * DESCRIPTION: helper function to fill in face related metadata into a struct.
 *
 * PARAMETERS :
 *   @faces_data : face features data to be filled
 *   @metadata   : metadata structure to read face features from
 *
 * RETURN     : None
 *==========================================================================*/
void QCamera2HardwareInterface::fillFacesData(cam_faces_data_t &faces_data,
        metadata_buffer_t *metadata)
{
    memset(&faces_data, 0, sizeof(cam_faces_data_t));

    IF_META_AVAILABLE(cam_face_detection_data_t, p_detection_data,
            CAM_INTF_META_FACE_DETECTION, metadata) {
        faces_data.detection_data = *p_detection_data;
        if (faces_data.detection_data.num_faces_detected > MAX_ROI) {
            faces_data.detection_data.num_faces_detected = MAX_ROI;
        }

        LOGH("[KPI Perf] PROFILE_NUMBER_OF_FACES_DETECTED %d",
                faces_data.detection_data.num_faces_detected);

        IF_META_AVAILABLE(cam_face_recog_data_t, p_recog_data,
                CAM_INTF_META_FACE_RECOG, metadata) {
            faces_data.recog_valid = true;
            faces_data.recog_data = *p_recog_data;
        }

        IF_META_AVAILABLE(cam_face_blink_data_t, p_blink_data,
                CAM_INTF_META_FACE_BLINK, metadata) {
            faces_data.blink_valid = true;
            faces_data.blink_data = *p_blink_data;
        }

        IF_META_AVAILABLE(cam_face_gaze_data_t, p_gaze_data,
                CAM_INTF_META_FACE_GAZE, metadata) {
            faces_data.gaze_valid = true;
            faces_data.gaze_data = *p_gaze_data;
        }

        IF_META_AVAILABLE(cam_face_smile_data_t, p_smile_data,
                CAM_INTF_META_FACE_SMILE, metadata) {
            faces_data.smile_valid = true;
            faces_data.smile_data = *p_smile_data;
        }

        IF_META_AVAILABLE(cam_face_landmarks_data_t, p_landmarks,
                CAM_INTF_META_FACE_LANDMARK, metadata) {
            faces_data.landmark_valid = true;
            faces_data.landmark_data = *p_landmarks;
        }

        IF_META_AVAILABLE(cam_face_contour_data_t, p_contour,
                CAM_INTF_META_FACE_CONTOUR, metadata) {
            faces_data.contour_valid = true;
            faces_data.contour_data = *p_contour;
        }
    }
}

/*===========================================================================
 * FUNCTION   : ~QCameraCbNotifier
 *
 * DESCRIPTION: Destructor for exiting the callback context.
 *
 * PARAMETERS : None
 *
 * RETURN     : None
 *==========================================================================*/
QCameraCbNotifier::~QCameraCbNotifier()
{
}

/*===========================================================================
 * FUNCTION   : exit
 *
 * DESCRIPTION: exit notify thread.
 *
 * PARAMETERS : None
 *
 * RETURN     : None
 *==========================================================================*/
void QCameraCbNotifier::exit()
{
    mActive = false;
    mProcTh.exit();
}

/*===========================================================================
 * FUNCTION   : releaseNotifications
 *
 * DESCRIPTION: callback for releasing data stored in the callback queue.
 *
 * PARAMETERS :
 *   @data      : data to be released
 *   @user_data : context data
 *
 * RETURN     : None
 *==========================================================================*/
void QCameraCbNotifier::releaseNotifications(void *data, void *user_data)
{
    qcamera_callback_argm_t *arg = ( qcamera_callback_argm_t * ) data;

    if ( ( NULL != arg ) && ( NULL != user_data ) ) {
        if ( arg->release_cb ) {
            arg->release_cb(arg->user_data, arg->cookie, FAILED_TRANSACTION);
        }
    }
}

/*===========================================================================
 * FUNCTION   : matchSnapshotNotifications
 *
 * DESCRIPTION: matches snapshot data callbacks
 *
 * PARAMETERS :
 *   @data      : data to match
 *   @user_data : context data
 *
 * RETURN     : bool match
 *              true - match found
 *              false- match not found
 *==========================================================================*/
bool QCameraCbNotifier::matchSnapshotNotifications(void *data,
                                                   void */*user_data*/)
{
    qcamera_callback_argm_t *arg = ( qcamera_callback_argm_t * ) data;
    if ( NULL != arg ) {
        if ( QCAMERA_DATA_SNAPSHOT_CALLBACK == arg->cb_type ) {
            return true;
        }
    }

    return false;
}

/*===========================================================================
 * FUNCTION   : matchPreviewNotifications
 *
 * DESCRIPTION: matches preview data callbacks
 *
 * PARAMETERS :
 *   @data      : data to match
 *   @user_data : context data
 *
 * RETURN     : bool match
 *              true - match found
 *              false- match not found
 *==========================================================================*/
bool QCameraCbNotifier::matchPreviewNotifications(void *data,
        void */*user_data*/)
{
    qcamera_callback_argm_t *arg = ( qcamera_callback_argm_t * ) data;
    if (NULL != arg) {
        if ((QCAMERA_DATA_CALLBACK == arg->cb_type) &&
                (CAMERA_MSG_PREVIEW_FRAME == arg->msg_type)) {
            return true;
        }
    }

    return false;
}

/*===========================================================================
 * FUNCTION   : matchTimestampNotifications
 *
 * DESCRIPTION: matches timestamp data callbacks
 *
 * PARAMETERS :
 *   @data      : data to match
 *   @user_data : context data
 *
 * RETURN     : bool match
 *              true - match found
 *              false- match not found
 *==========================================================================*/
bool QCameraCbNotifier::matchTimestampNotifications(void *data,
        void */*user_data*/)
{
    qcamera_callback_argm_t *arg = ( qcamera_callback_argm_t * ) data;
    if (NULL != arg) {
        if ((QCAMERA_DATA_TIMESTAMP_CALLBACK == arg->cb_type) &&
                (CAMERA_MSG_VIDEO_FRAME == arg->msg_type)) {
            return true;
        }
    }

    return false;
}

/*===========================================================================
 * FUNCTION   : cbNotifyRoutine
 *
 * DESCRIPTION: callback thread which interfaces with the upper layers
 *              given input commands.
 *
 * PARAMETERS :
 *   @data    : context data
 *
 * RETURN     : None
 *==========================================================================*/
void * QCameraCbNotifier::cbNotifyRoutine(void * data)
{
    int running = 1;
    int ret;
    QCameraCbNotifier *pme = (QCameraCbNotifier *)data;
    QCameraCmdThread *cmdThread = &pme->mProcTh;
    cmdThread->setName("CAM_cbNotify");
    uint8_t isSnapshotActive = FALSE;
    bool longShotEnabled = false;
    uint32_t numOfSnapshotExpected = 0;
    uint32_t numOfSnapshotRcvd = 0;
    int32_t cbStatus = NO_ERROR;

    LOGD("E");
    do {
        do {
            ret = cam_sem_wait(&cmdThread->cmd_sem);
            if (ret != 0 && errno != EINVAL) {
                LOGD("cam_sem_wait error (%s)",
                            strerror(errno));
                return NULL;
            }
        } while (ret != 0);

        camera_cmd_type_t cmd = cmdThread->getCmd();
        LOGD("get cmd %d", cmd);
        switch (cmd) {
        case CAMERA_CMD_TYPE_START_DATA_PROC:
            {
                isSnapshotActive = TRUE;
                numOfSnapshotExpected = pme->mParent->numOfSnapshotsExpected();
                longShotEnabled = pme->mParent->isLongshotEnabled();
                LOGD("Num Snapshots Expected = %d",
                       numOfSnapshotExpected);
                numOfSnapshotRcvd = 0;
            }
            break;
        case CAMERA_CMD_TYPE_STOP_DATA_PROC:
            {
                pme->mDataQ.flushNodes(matchSnapshotNotifications);
                isSnapshotActive = FALSE;

                numOfSnapshotExpected = 0;
                numOfSnapshotRcvd = 0;
            }
            break;
        case CAMERA_CMD_TYPE_DO_NEXT_JOB:
            {
                qcamera_callback_argm_t *cb =
                    (qcamera_callback_argm_t *)pme->mDataQ.dequeue();
                cbStatus = NO_ERROR;
                if (NULL != cb) {
                    LOGD("cb type %d received",
                              cb->cb_type);

                    if (pme->mParent->msgTypeEnabledWithLock(cb->msg_type)) {
                        switch (cb->cb_type) {
                        case QCAMERA_NOTIFY_CALLBACK:
                            {
                                if (cb->msg_type == CAMERA_MSG_FOCUS) {
                                    KPI_ATRACE_INT("Camera:AutoFocus", 0);
                                    LOGH("[KPI Perf] : PROFILE_SENDING_FOCUS_EVT_TO APP");
                                }
                                if (pme->mNotifyCb) {
                                    pme->mNotifyCb(cb->msg_type,
                                                  cb->ext1,
                                                  cb->ext2,
                                                  pme->mCallbackCookie);
                                } else {
                                    LOGW("notify callback not set!");
                                }
                                if (cb->release_cb) {
                                    cb->release_cb(cb->user_data, cb->cookie,
                                            cbStatus);
                                }
                            }
                            break;
                        case QCAMERA_DATA_CALLBACK:
                            {
                                if (pme->mDataCb) {
                                    pme->mDataCb(cb->msg_type,
                                                 cb->data,
                                                 cb->index,
                                                 cb->metadata,
                                                 pme->mCallbackCookie);
                                } else {
                                    LOGW("data callback not set!");
                                }
                                if (cb->release_cb) {
                                    cb->release_cb(cb->user_data, cb->cookie,
                                            cbStatus);
                                }
                            }
                            break;
                        case QCAMERA_DATA_TIMESTAMP_CALLBACK:
                            {
                                if(pme->mDataCbTimestamp) {
                                    pme->mDataCbTimestamp(cb->timestamp,
                                                          cb->msg_type,
                                                          cb->data,
                                                          cb->index,
                                                          pme->mCallbackCookie);
                                } else {
                                    LOGE("Timestamp data callback not set!");
                                }
                                if (cb->release_cb) {
                                    cb->release_cb(cb->user_data, cb->cookie,
                                            cbStatus);
                                }
                            }
                            break;
                        case QCAMERA_DATA_SNAPSHOT_CALLBACK:
                            {
                                if (TRUE == isSnapshotActive && pme->mDataCb ) {
                                    if (!longShotEnabled) {
                                        numOfSnapshotRcvd++;
                                        LOGI("Num Snapshots Received = %d Expected = %d",
                                                numOfSnapshotRcvd, numOfSnapshotExpected);
                                        if (numOfSnapshotExpected > 0 &&
                                           (numOfSnapshotExpected == numOfSnapshotRcvd)) {
                                            LOGI("Received all snapshots");
                                            // notify HWI that snapshot is done
                                            pme->mParent->processSyncEvt(QCAMERA_SM_EVT_SNAPSHOT_DONE,
                                                                         NULL);
                                        }
                                    }
                                    if (pme->mJpegCb) {
                                        LOGI("Calling JPEG Callback!! for camera %d"
                                                "release_data %p",
                                                "frame_idx %d",
                                                 pme->mParent->getCameraId(),
                                                cb->user_data,
                                                cb->frame_index);
                                        pme->mJpegCb(cb->msg_type, cb->data,
                                                cb->index, cb->metadata,
                                                pme->mJpegCallbackCookie,
                                                cb->frame_index, cb->release_cb,
                                                cb->cookie, cb->user_data);
                                        // incase of non-null Jpeg cb we transfer
                                        // ownership of buffer to muxer. hence
                                        // release_cb should not be called
                                        // muxer will release after its done with
                                        // processing the buffer
                                    } else if(pme->mDataCb){
                                        pme->mDataCb(cb->msg_type, cb->data, cb->index,
                                                cb->metadata, pme->mCallbackCookie);
                                        if (cb->release_cb) {
                                            cb->release_cb(cb->user_data, cb->cookie,
                                                    cbStatus);
                                        }
                                    }
                                }
                            }
                            break;
                        default:
                            {
                                LOGE("invalid cb type %d",
                                          cb->cb_type);
                                cbStatus = BAD_VALUE;
                                if (cb->release_cb) {
                                    cb->release_cb(cb->user_data, cb->cookie,
                                            cbStatus);
                                }
                            }
                            break;
                        };
                    } else {
                        LOGW("cb message type %d not enabled!",
                                  cb->msg_type);
                        cbStatus = INVALID_OPERATION;
                        if (cb->release_cb) {
                            cb->release_cb(cb->user_data, cb->cookie, cbStatus);
                        }
                    }
                    delete cb;
                } else {
                    LOGW("invalid cb type passed");
                }
            }
            break;
        case CAMERA_CMD_TYPE_EXIT:
            {
                running = 0;
                pme->mDataQ.flush();
            }
            break;
        default:
            break;
        }
    } while (running);
    LOGD("X");

    return NULL;
}

/*===========================================================================
 * FUNCTION   : notifyCallback
 *
 * DESCRIPTION: Enqueus pending callback notifications for the upper layers.
 *
 * PARAMETERS :
 *   @cbArgs  : callback arguments
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraCbNotifier::notifyCallback(qcamera_callback_argm_t &cbArgs)
{
    if (!mActive) {
        LOGE("notify thread is not active");
        return UNKNOWN_ERROR;
    }

    qcamera_callback_argm_t *cbArg = new qcamera_callback_argm_t();
    if (NULL == cbArg) {
        LOGE("no mem for qcamera_callback_argm_t");
        return NO_MEMORY;
    }
    memset(cbArg, 0, sizeof(qcamera_callback_argm_t));
    *cbArg = cbArgs;

    if (mDataQ.enqueue((void *)cbArg)) {
        return mProcTh.sendCmd(CAMERA_CMD_TYPE_DO_NEXT_JOB, FALSE, FALSE);
    } else {
        LOGE("Error adding cb data into queue");
        delete cbArg;
        return UNKNOWN_ERROR;
    }
}

/*===========================================================================
 * FUNCTION   : setCallbacks
 *
 * DESCRIPTION: Initializes the callback functions, which would be used for
 *              communication with the upper layers and launches the callback
 *              context in which the callbacks will occur.
 *
 * PARAMETERS :
 *   @notifyCb          : notification callback
 *   @dataCb            : data callback
 *   @dataCbTimestamp   : data with timestamp callback
 *   @callbackCookie    : callback context data
 *
 * RETURN     : None
 *==========================================================================*/
void QCameraCbNotifier::setCallbacks(camera_notify_callback notifyCb,
                                     camera_data_callback dataCb,
                                     camera_data_timestamp_callback dataCbTimestamp,
                                     void *callbackCookie)
{
    if ( ( NULL == mNotifyCb ) &&
         ( NULL == mDataCb ) &&
         ( NULL == mDataCbTimestamp ) &&
         ( NULL == mCallbackCookie ) ) {
        mNotifyCb = notifyCb;
        mDataCb = dataCb;
        mDataCbTimestamp = dataCbTimestamp;
        mCallbackCookie = callbackCookie;
        mActive = true;
        mProcTh.launch(cbNotifyRoutine, this);
    } else {
        LOGE("Camera callback notifier already initialized!");
    }
}

/*===========================================================================
 * FUNCTION   : setJpegCallBacks
 *
 * DESCRIPTION: Initializes the JPEG callback function, which would be used for
 *              communication with the upper layers and launches the callback
 *              context in which the callbacks will occur.
 *
 * PARAMETERS :
 *   @jpegCb          : notification callback
 *   @callbackCookie    : callback context data
 *
 * RETURN     : None
 *==========================================================================*/
void QCameraCbNotifier::setJpegCallBacks(
        jpeg_data_callback jpegCb, void *callbackCookie)
{
    LOGH("Setting JPEG Callback notifier");
    mJpegCb        = jpegCb;
    mJpegCallbackCookie  = callbackCookie;
}

/*===========================================================================
 * FUNCTION   : flushPreviewNotifications
 *
 * DESCRIPTION: flush all pending preview notifications
 *              from the notifier queue
 *
 * PARAMETERS : None
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraCbNotifier::flushPreviewNotifications()
{
    if (!mActive) {
        LOGE("notify thread is not active");
        return UNKNOWN_ERROR;
    }
    mDataQ.flushNodes(matchPreviewNotifications);
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : flushVideoNotifications
 *
 * DESCRIPTION: flush all pending video notifications
 *              from the notifier queue
 *
 * PARAMETERS : None
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraCbNotifier::flushVideoNotifications()
{
    if (!mActive) {
        LOGE("notify thread is not active");
        return UNKNOWN_ERROR;
    }
    mDataQ.flushNodes(matchTimestampNotifications);
    return NO_ERROR;
}

/*===========================================================================
 * FUNCTION   : startSnapshots
 *
 * DESCRIPTION: Enables snapshot mode
 *
 * PARAMETERS : None
 *
 * RETURN     : int32_t type of status
 *              NO_ERROR  -- success
 *              none-zero failure code
 *==========================================================================*/
int32_t QCameraCbNotifier::startSnapshots()
{
    return mProcTh.sendCmd(CAMERA_CMD_TYPE_START_DATA_PROC, FALSE, TRUE);
}

/*===========================================================================
 * FUNCTION   : stopSnapshots
 *
 * DESCRIPTION: Disables snapshot processing mode
 *
 * PARAMETERS : None
 *
 * RETURN     : None
 *==========================================================================*/
void QCameraCbNotifier::stopSnapshots()
{
    mProcTh.sendCmd(CAMERA_CMD_TYPE_STOP_DATA_PROC, FALSE, TRUE);
}

}; // namespace qcamera
