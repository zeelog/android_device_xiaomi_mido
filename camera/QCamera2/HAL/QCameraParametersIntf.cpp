/* Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
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

#define LOG_TAG "QCameraParametersIntf"

// System dependencies
#include <utils/Mutex.h>

// Camera dependencies
#include "QCameraParameters.h"
#include "QCameraParametersIntf.h"
#include "QCameraTrace.h"

extern "C" {
#include "mm_camera_dbg.h"
}

namespace qcamera {

#define CHECK_PARAM_INTF(impl) LOG_ALWAYS_FATAL_IF(((impl) == NULL), "impl is NULL!")

QCameraParametersIntf::QCameraParametersIntf() :
        mImpl(NULL)
{
}

QCameraParametersIntf::~QCameraParametersIntf()
{
    {
        Mutex::Autolock lock(mLock);
        if (mImpl) {
            delete mImpl;
            mImpl = NULL;
        }
    }
}


int32_t QCameraParametersIntf::allocate()
{
    Mutex::Autolock lock(mLock);
    mImpl = new QCameraParameters();
    if (!mImpl) {
        LOGE("Out of memory");
        return NO_MEMORY;
    }

    return mImpl->allocate();
}

int32_t QCameraParametersIntf::init(cam_capability_t *capabilities,
                                mm_camera_vtbl_t *mmOps,
                                QCameraAdjustFPS *adjustFPS)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->init(capabilities, mmOps, adjustFPS);
}

void QCameraParametersIntf::deinit()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    mImpl->deinit();
}

int32_t QCameraParametersIntf::updateParameters(const String8& params, bool &needRestart)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->updateParameters(params, needRestart);
}

int32_t QCameraParametersIntf::commitParameters()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->commitParameters();
}

char* QCameraParametersIntf::QCameraParametersIntf::getParameters()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getParameters();
}

void QCameraParametersIntf::getPreviewFpsRange(int *min_fps, int *max_fps) const
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    mImpl->getPreviewFpsRange(min_fps, max_fps);
}

#ifdef TARGET_TS_MAKEUP
bool QCameraParametersIntf::getTsMakeupInfo(int &whiteLevel, int &cleanLevel) const
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getTsMakeupInfo(whiteLevel, cleanLevel);
}
#endif

int QCameraParametersIntf::getPreviewHalPixelFormat()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getPreviewHalPixelFormat();
}

int32_t QCameraParametersIntf::getStreamRotation(cam_stream_type_t streamType,
                                            cam_pp_feature_config_t &featureConfig,
                                            cam_dimension_t &dim)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getStreamRotation(streamType, featureConfig, dim);

}

int32_t QCameraParametersIntf::getStreamSubFormat(cam_stream_type_t streamType,
                                            cam_sub_format_type_t &sub_format)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getStreamSubFormat(streamType, sub_format);
}


int32_t QCameraParametersIntf::getStreamFormat(cam_stream_type_t streamType,
                                            cam_format_t &format)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getStreamFormat(streamType, format);
}

int32_t QCameraParametersIntf::getStreamDimension(cam_stream_type_t streamType,
                                               cam_dimension_t &dim)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getStreamDimension(streamType, dim);
}

void QCameraParametersIntf::getThumbnailSize(int *width, int *height) const
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    mImpl->getThumbnailSize(width, height);
}

uint8_t QCameraParametersIntf::getZSLBurstInterval()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getZSLBurstInterval();
}

uint8_t QCameraParametersIntf::getZSLQueueDepth()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getZSLQueueDepth();
}

uint8_t QCameraParametersIntf::getZSLBackLookCount()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getZSLBackLookCount();
}

uint8_t QCameraParametersIntf::getMaxUnmatchedFramesInQueue()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getMaxUnmatchedFramesInQueue();
}

bool QCameraParametersIntf::isZSLMode()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isZSLMode();
}

bool QCameraParametersIntf::isRdiMode()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isRdiMode();
}

bool QCameraParametersIntf::isSecureMode()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isSecureMode();
}

bool QCameraParametersIntf::isNoDisplayMode()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isNoDisplayMode();
}

bool QCameraParametersIntf::isWNREnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isWNREnabled();
}

bool QCameraParametersIntf::isTNRSnapshotEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isTNRSnapshotEnabled();
}

int32_t QCameraParametersIntf::getCDSMode()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getCDSMode();
}

bool QCameraParametersIntf::isLTMForSeeMoreEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isLTMForSeeMoreEnabled();
}

bool QCameraParametersIntf::isHfrMode()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isHfrMode();
}

void QCameraParametersIntf::getHfrFps(cam_fps_range_t &pFpsRange)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    mImpl->getHfrFps(pFpsRange);
}

uint8_t QCameraParametersIntf::getNumOfSnapshots()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getNumOfSnapshots();
}

uint8_t QCameraParametersIntf::getNumOfRetroSnapshots()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getNumOfRetroSnapshots();
}

uint8_t QCameraParametersIntf::getNumOfExtraHDRInBufsIfNeeded()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getNumOfExtraHDRInBufsIfNeeded();
}

uint8_t QCameraParametersIntf::getNumOfExtraHDROutBufsIfNeeded()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getNumOfExtraHDROutBufsIfNeeded();
}

bool QCameraParametersIntf::getRecordingHintValue()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getRecordingHintValue();
}

uint32_t QCameraParametersIntf::getJpegQuality()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getJpegQuality();
}

uint32_t QCameraParametersIntf::getRotation()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getRotation();
}

uint32_t QCameraParametersIntf::getDeviceRotation()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getDeviceRotation();
}

uint32_t QCameraParametersIntf::getJpegExifRotation()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getJpegExifRotation();
}

bool QCameraParametersIntf::useJpegExifRotation()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->useJpegExifRotation();
}

int32_t QCameraParametersIntf::getEffectValue()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getEffectValue();
}

bool QCameraParametersIntf::isInstantAECEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isInstantAECEnabled();
}

bool QCameraParametersIntf::isInstantCaptureEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isInstantCaptureEnabled();
}

uint8_t QCameraParametersIntf::getAecFrameBoundValue()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getAecFrameBoundValue();
}

uint8_t QCameraParametersIntf::getAecSkipDisplayFrameBound()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getAecSkipDisplayFrameBound();
}

int32_t QCameraParametersIntf::getExifDateTime(
        String8 &dateTime, String8 &subsecTime)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getExifDateTime(dateTime, subsecTime);
}

int32_t QCameraParametersIntf::getExifFocalLength(rat_t *focalLength)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getExifFocalLength(focalLength);
}

uint16_t QCameraParametersIntf::getExifIsoSpeed()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getExifIsoSpeed();
}

int32_t QCameraParametersIntf::getExifGpsProcessingMethod(char *gpsProcessingMethod, uint32_t &count)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getExifGpsProcessingMethod(gpsProcessingMethod, count);
}

int32_t QCameraParametersIntf::getExifLatitude(rat_t *latitude, char *latRef)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getExifLatitude(latitude, latRef);
}

int32_t QCameraParametersIntf::getExifLongitude(rat_t *longitude, char *lonRef)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getExifLongitude(longitude, lonRef);
}

int32_t QCameraParametersIntf::getExifAltitude(rat_t *altitude, char *altRef)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getExifAltitude(altitude, altRef);
}

int32_t QCameraParametersIntf::getExifGpsDateTimeStamp(char *gpsDateStamp, uint32_t bufLen, rat_t *gpsTimeStamp)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getExifGpsDateTimeStamp(gpsDateStamp, bufLen, gpsTimeStamp);
}

bool QCameraParametersIntf::isVideoBuffersCached()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isVideoBuffersCached();
}

int32_t QCameraParametersIntf::updateFocusDistances(cam_focus_distances_info_t *focusDistances)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->updateFocusDistances(focusDistances);
}

bool QCameraParametersIntf::isAEBracketEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isAEBracketEnabled();
}

int32_t QCameraParametersIntf::setAEBracketing()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setAEBracketing();
}

bool QCameraParametersIntf::isFpsDebugEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isFpsDebugEnabled();
}

bool QCameraParametersIntf::isHistogramEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isHistogramEnabled();
}

bool QCameraParametersIntf::isSceneSelectionEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isSceneSelectionEnabled();
}

bool QCameraParametersIntf::isSmallJpegSizeEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isSmallJpegSizeEnabled();
}

int32_t QCameraParametersIntf::setSelectedScene(cam_scene_mode_type scene)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setSelectedScene(scene);
}

cam_scene_mode_type QCameraParametersIntf::getSelectedScene()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getSelectedScene();
}

bool QCameraParametersIntf::isFaceDetectionEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isFaceDetectionEnabled();
}

int32_t QCameraParametersIntf::setFaceDetectionOption(bool enabled)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setFaceDetectionOption(enabled);
}

int32_t QCameraParametersIntf::setHistogram(bool enabled)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setHistogram(enabled);
}

int32_t QCameraParametersIntf::setFaceDetection(bool enabled, bool initCommit)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setFaceDetection(enabled, initCommit);
}

int32_t QCameraParametersIntf::setFrameSkip(enum msm_vfe_frame_skip_pattern pattern)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setFrameSkip(pattern);
}

qcamera_thermal_mode QCameraParametersIntf::getThermalMode()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getThermalMode();
}

int32_t QCameraParametersIntf::updateRecordingHintValue(int32_t value)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->updateRecordingHintValue(value);
}

int32_t QCameraParametersIntf::setHDRAEBracket(cam_exp_bracketing_t hdrBracket)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setHDRAEBracket(hdrBracket);
}

bool QCameraParametersIntf::isHDREnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isHDREnabled();
}

bool QCameraParametersIntf::isAutoHDREnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isAutoHDREnabled();
}

int32_t QCameraParametersIntf::stopAEBracket()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->stopAEBracket();
}

int32_t QCameraParametersIntf::updateRAW(cam_dimension_t max_dim)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->updateRAW(max_dim);
}

bool QCameraParametersIntf::isDISEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isDISEnabled();
}

bool QCameraParametersIntf::isAVTimerEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isAVTimerEnabled();
}


int32_t QCameraParametersIntf::setISType()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setISType();
}

cam_is_type_t QCameraParametersIntf::getVideoISType()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getVideoISType();
}

cam_is_type_t QCameraParametersIntf::getPreviewISType()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getPreviewISType();
}

uint8_t QCameraParametersIntf::getMobicatMask()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getMobicatMask();
}

cam_focus_mode_type QCameraParametersIntf::getFocusMode() const
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getFocusMode();
}

int32_t QCameraParametersIntf::setNumOfSnapshot()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setNumOfSnapshot();
}

int32_t QCameraParametersIntf::adjustPreviewFpsRange(cam_fps_range_t *fpsRange)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->adjustPreviewFpsRange(fpsRange);
}

bool QCameraParametersIntf::isJpegPictureFormat()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isJpegPictureFormat();
}

bool QCameraParametersIntf::isNV16PictureFormat()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isNV16PictureFormat();
}

bool QCameraParametersIntf::isNV21PictureFormat()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isNV21PictureFormat();
}

cam_denoise_process_type_t QCameraParametersIntf::getDenoiseProcessPlate(
        cam_intf_parm_type_t type)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getDenoiseProcessPlate(type);
}

int32_t QCameraParametersIntf::getMaxPicSize(cam_dimension_t &dim)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getMaxPicSize(dim);
}

int QCameraParametersIntf::getFlipMode(cam_stream_type_t streamType)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getFlipMode(streamType);
}

bool QCameraParametersIntf::isSnapshotFDNeeded()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isSnapshotFDNeeded();
}

bool QCameraParametersIntf::isHDR1xFrameEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isHDR1xFrameEnabled();
}

bool QCameraParametersIntf::isYUVFrameInfoNeeded()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isYUVFrameInfoNeeded();
}

const char* QCameraParametersIntf::getFrameFmtString(cam_format_t fmt)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getFrameFmtString(fmt);
}

bool QCameraParametersIntf::isHDR1xExtraBufferNeeded()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isHDR1xExtraBufferNeeded();
}

bool QCameraParametersIntf::isHDROutputCropEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isHDROutputCropEnabled();
}

bool QCameraParametersIntf::isPreviewFlipChanged()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isPreviewFlipChanged();
}

bool QCameraParametersIntf::isVideoFlipChanged()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isVideoFlipChanged();
}

bool QCameraParametersIntf::isSnapshotFlipChanged()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isSnapshotFlipChanged();
}

void QCameraParametersIntf::setHDRSceneEnable(bool bflag)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    mImpl->setHDRSceneEnable(bflag);
}

int32_t QCameraParametersIntf::updateAWBParams(cam_awb_params_t &awb_params)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->updateAWBParams(awb_params);
}

const char * QCameraParametersIntf::getASDStateString(cam_auto_scene_t scene)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getASDStateString(scene);
}

bool QCameraParametersIntf::isHDRThumbnailProcessNeeded()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isHDRThumbnailProcessNeeded();
}

void QCameraParametersIntf::setMinPpMask(cam_feature_mask_t min_pp_mask)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    mImpl->setMinPpMask(min_pp_mask);
}

bool QCameraParametersIntf::setStreamConfigure(bool isCapture,
        bool previewAsPostview, bool resetConfig, uint32_t* sessionId)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setStreamConfigure(isCapture,
            previewAsPostview, resetConfig, sessionId);
}

int32_t QCameraParametersIntf::addOnlineRotation(uint32_t rotation,
        uint32_t streamId, int32_t device_rotation)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->addOnlineRotation(rotation, streamId, device_rotation);
}

uint8_t QCameraParametersIntf::getNumOfExtraBuffersForImageProc()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getNumOfExtraBuffersForImageProc();
}

uint8_t QCameraParametersIntf::getNumOfExtraBuffersForVideo()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getNumOfExtraBuffersForVideo();
}

uint8_t QCameraParametersIntf::getNumOfExtraBuffersForPreview()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getNumOfExtraBuffersForPreview();
}

uint32_t QCameraParametersIntf::getExifBufIndex(uint32_t captureIndex)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getExifBufIndex(captureIndex);
}

bool QCameraParametersIntf::needThumbnailReprocess(cam_feature_mask_t *pFeatureMask)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->needThumbnailReprocess(pFeatureMask);
}

bool QCameraParametersIntf::isUbiFocusEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isUbiFocusEnabled();
}

bool QCameraParametersIntf::isChromaFlashEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isChromaFlashEnabled();
}

bool QCameraParametersIntf::isHighQualityNoiseReductionMode()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isHighQualityNoiseReductionMode();
}

bool QCameraParametersIntf::isTruePortraitEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isTruePortraitEnabled();
}

size_t QCameraParametersIntf::getTPMaxMetaSize()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getTPMaxMetaSize();
}

bool QCameraParametersIntf::isSeeMoreEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isSeeMoreEnabled();
}

bool QCameraParametersIntf::isStillMoreEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isStillMoreEnabled();
}

bool QCameraParametersIntf::isOptiZoomEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isOptiZoomEnabled();
}

int32_t QCameraParametersIntf::commitAFBracket(cam_af_bracketing_t afBracket)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->commitAFBracket(afBracket);
}


int32_t QCameraParametersIntf::set3ALock(bool lock3A)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->set3ALock(lock3A);
}

int32_t QCameraParametersIntf::setAndCommitZoom(int zoom_level)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setAndCommitZoom(zoom_level);
}
uint8_t QCameraParametersIntf::getBurstCountForAdvancedCapture()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getBurstCountForAdvancedCapture();
}
uint32_t QCameraParametersIntf::getNumberInBufsForSingleShot()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getNumberInBufsForSingleShot();
}
uint32_t QCameraParametersIntf::getNumberOutBufsForSingleShot()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getNumberOutBufsForSingleShot();
}
int32_t QCameraParametersIntf::setLongshotEnable(bool enable)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setLongshotEnable(enable);
}
String8 QCameraParametersIntf::dump()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->dump();
}
bool QCameraParametersIntf::isUbiRefocus()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isUbiRefocus();
}
uint32_t QCameraParametersIntf::getRefocusMaxMetaSize()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getRefocusMaxMetaSize();
}
uint8_t QCameraParametersIntf::getRefocusOutputCount()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getRefocusOutputCount();
}

bool QCameraParametersIntf::generateThumbFromMain()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->generateThumbFromMain();
}

void QCameraParametersIntf::updateCurrentFocusPosition(cam_focus_pos_info_t &cur_pos_info)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    mImpl->updateCurrentFocusPosition(cur_pos_info);
}

void QCameraParametersIntf::updateAEInfo(cam_3a_params_t &ae_params)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    mImpl->updateAEInfo(ae_params);
}

bool QCameraParametersIntf::isAdvCamFeaturesEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isAdvCamFeaturesEnabled();
}

int32_t QCameraParametersIntf::setAecLock(const char *aecStr)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setAecLock(aecStr);
}

int32_t QCameraParametersIntf::updateDebugLevel()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->updateDebugLevel();
}

bool QCameraParametersIntf::is4k2kVideoResolution()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->is4k2kVideoResolution();
}

bool QCameraParametersIntf::isUBWCEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isUBWCEnabled();
}
int QCameraParametersIntf::getBrightness()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getBrightness();
}

int32_t QCameraParametersIntf::updateOisValue(bool oisValue)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->updateOisValue(oisValue);
}

int32_t QCameraParametersIntf::setIntEvent(cam_int_evt_params_t params)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setIntEvent(params);
}

bool QCameraParametersIntf::getofflineRAW()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getofflineRAW();
}

bool QCameraParametersIntf::getQuadraCfa()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getQuadraCfa();
}

int32_t QCameraParametersIntf::updatePpFeatureMask(cam_stream_type_t stream_type)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->updatePpFeatureMask(stream_type);
}

int32_t QCameraParametersIntf::getStreamPpMask(cam_stream_type_t stream_type,
        cam_feature_mask_t &pp_mask)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getStreamPpMask(stream_type, pp_mask);
}

int32_t QCameraParametersIntf::getSharpness()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getSharpness();
}

int32_t QCameraParametersIntf::getEffect()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getEffect();
}

int32_t QCameraParametersIntf::updateFlashMode(cam_flash_mode_t flash_mode)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->updateFlashMode(flash_mode);
}

int32_t QCameraParametersIntf::configureAEBracketing(cam_capture_frame_config_t &frame_config)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->configureAEBracketing(frame_config);
}

int32_t QCameraParametersIntf::configureHDRBracketing(cam_capture_frame_config_t &frame_config)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->configureHDRBracketing(frame_config);
}

int32_t QCameraParametersIntf::configFrameCapture(bool commitSettings)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->configFrameCapture(commitSettings);
}

int32_t QCameraParametersIntf::resetFrameCapture(bool commitSettings, bool lowLightEnabled)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->resetFrameCapture(commitSettings,lowLightEnabled);
}

cam_still_more_t QCameraParametersIntf::getStillMoreSettings()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getStillMoreSettings();
}

void QCameraParametersIntf::setStillMoreSettings(cam_still_more_t stillmore_config)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    mImpl->setStillMoreSettings(stillmore_config);
}

cam_still_more_t QCameraParametersIntf::getStillMoreCapability()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getStillMoreCapability();
}

cam_dyn_img_data_t QCameraParametersIntf::getDynamicImgData()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getDynamicImgData();
}

void QCameraParametersIntf::setDynamicImgData(cam_dyn_img_data_t d)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    mImpl->setDynamicImgData(d);
}

int32_t QCameraParametersIntf::getParmZoomLevel()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getParmZoomLevel();
}


int8_t QCameraParametersIntf::getReprocCount()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getReprocCount();
}


int8_t QCameraParametersIntf::getCurPPCount()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getCurPPCount();
}


void QCameraParametersIntf::setReprocCount()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    mImpl->setReprocCount();
}


bool QCameraParametersIntf::isPostProcScaling()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isPostProcScaling();
}


bool QCameraParametersIntf::isLLNoiseEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isLLNoiseEnabled();
}


void QCameraParametersIntf::setCurPPCount(int8_t count)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    mImpl->setCurPPCount(count);
}

int32_t QCameraParametersIntf::setQuadraCfaMode(uint32_t value, bool initCommit)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setQuadraCfaMode(value, initCommit);
}

int32_t QCameraParametersIntf::setToneMapMode(uint32_t value, bool initCommit)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setToneMapMode(value, initCommit);
}

void QCameraParametersIntf::setTintless(bool enable)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    mImpl->setTintless(enable);
}

uint8_t QCameraParametersIntf::getLongshotStages()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getLongshotStages();
}

int8_t  QCameraParametersIntf::getBufBatchCount()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getBufBatchCount();
}

int8_t  QCameraParametersIntf::getVideoBatchSize()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getVideoBatchSize();
}

int32_t QCameraParametersIntf::setManualCaptureMode(
        QCameraManualCaptureModes value)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setManualCaptureMode(value);
}

QCameraManualCaptureModes QCameraParametersIntf::getManualCaptureMode()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getManualCaptureMode();
}

int64_t QCameraParametersIntf::getExposureTime()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getExposureTime();
}

cam_capture_frame_config_t QCameraParametersIntf::getCaptureFrameConfig()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getCaptureFrameConfig();
}

void QCameraParametersIntf::setJpegRotation(int rotation)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    mImpl->setJpegRotation(rotation);
}

uint32_t QCameraParametersIntf::getJpegRotation()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getJpegRotation();
}

void QCameraParametersIntf::setLowLightLevel(cam_low_light_mode_t value)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    mImpl->setLowLightLevel(value);
}

cam_low_light_mode_t QCameraParametersIntf::getLowLightLevel()
{
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getLowLightLevel();
}

bool QCameraParametersIntf::getLowLightCapture()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getLowLightCapture();
}

bool QCameraParametersIntf::getDcrf()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getDcrf();
}

int32_t QCameraParametersIntf::setRelatedCamSyncInfo(
	cam_sync_related_sensors_event_info_t* info)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setRelatedCamSyncInfo(info);
}

const cam_sync_related_sensors_event_info_t*
	QCameraParametersIntf::getRelatedCamSyncInfo(void)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getRelatedCamSyncInfo();
}

int32_t QCameraParametersIntf::setFrameSyncEnabled(
	bool enable)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setFrameSyncEnabled(enable);
}

bool QCameraParametersIntf::isFrameSyncEnabled(void)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isFrameSyncEnabled();
}

int32_t QCameraParametersIntf::getRelatedCamCalibration(
	cam_related_system_calibration_data_t* calib)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getRelatedCamCalibration(calib);
}

int32_t QCameraParametersIntf::bundleRelatedCameras(bool sync, uint32_t sessionid)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->bundleRelatedCameras(sync, sessionid);
}

uint8_t QCameraParametersIntf::fdModeInVideo()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->fdModeInVideo();
}

bool QCameraParametersIntf::isOEMFeatEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isOEMFeatEnabled();
}

int32_t QCameraParametersIntf::setZslMode(bool value)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setZslMode(value);
}

int32_t QCameraParametersIntf::updateZSLModeValue(bool value)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->updateZSLModeValue(value);
}

bool QCameraParametersIntf::isReprocScaleEnabled()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isReprocScaleEnabled();
}

bool QCameraParametersIntf::isUnderReprocScaling()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isUnderReprocScaling();
}

int32_t QCameraParametersIntf::getPicSizeFromAPK(int &width, int &height)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getPicSizeFromAPK(width, height);
}

int32_t QCameraParametersIntf::checkFeatureConcurrency()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->checkFeatureConcurrency();
}

int32_t QCameraParametersIntf::setInstantAEC(uint8_t enable, bool initCommit)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->setInstantAEC(enable, initCommit);
}

int32_t QCameraParametersIntf::getAnalysisInfo(
        bool fdVideoEnabled,
        bool hal3,
        cam_feature_mask_t featureMask,
        cam_analysis_info_t *pAnalysisInfo)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->getAnalysisInfo(fdVideoEnabled, hal3, featureMask, pAnalysisInfo);
}
int32_t QCameraParametersIntf::updateDtVc(int32_t *dt, int32_t *vc)
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->updateDtVc(dt, vc);
}

bool QCameraParametersIntf::isLinkPreviewForLiveShot()
{
    Mutex::Autolock lock(mLock);
    CHECK_PARAM_INTF(mImpl);
    return mImpl->isLinkPreviewForLiveShot();
}

}; // namespace qcamera
