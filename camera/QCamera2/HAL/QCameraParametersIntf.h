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

#ifndef ANDROID_HARDWARE_QCAMERA_PARAMETERS_INTF_H
#define ANDROID_HARDWARE_QCAMERA_PARAMETERS_INTF_H

#include <utils/String8.h>
#include <utils/Mutex.h>
#include "cam_intf.h"
#include "cam_types.h"
#include "QCameraThermalAdapter.h"

extern "C" {
#include <mm_camera_interface.h>
#include <mm_jpeg_interface.h>
}

using namespace android;

namespace qcamera {

typedef cam_manual_capture_type QCameraManualCaptureModes;

class QCameraAdjustFPS
{
public:
    virtual int recalcFPSRange(int &minFPS, int &maxFPS,
            const float &minVideoFPS, const float &maxVideoFPs,
            cam_fps_range_t &adjustedRange, bool bRecordingHint) = 0;
    virtual ~QCameraAdjustFPS() {}
};

class QCameraParameters;

class QCameraParametersIntf
{
public:

    // member variables
    QCameraParametersIntf();
    ~QCameraParametersIntf();

    int32_t allocate();
    int32_t init(cam_capability_t *capabilities,
                 mm_camera_vtbl_t *mmOps,
                 QCameraAdjustFPS *adjustFPS);

    void deinit();
    int32_t updateParameters(const String8& params, bool &needRestart);
    int32_t commitParameters();

    char* getParameters();
    void getPreviewFpsRange(int *min_fps, int *max_fps) const;
#ifdef TARGET_TS_MAKEUP
    bool getTsMakeupInfo(int &whiteLevel, int &cleanLevel) const;
#endif

    int getPreviewHalPixelFormat();
    int32_t getStreamRotation(cam_stream_type_t streamType,
            cam_pp_feature_config_t &featureConfig,
            cam_dimension_t &dim);
    int32_t getStreamFormat(cam_stream_type_t streamType,
            cam_format_t &format);

    int32_t getStreamSubFormat(
      cam_stream_type_t streamType, cam_sub_format_type_t &sub_format);


    int32_t getStreamDimension(cam_stream_type_t streamType,
            cam_dimension_t &dim);

    void getThumbnailSize(int *width, int *height) const;
    uint8_t getZSLBurstInterval();
    uint8_t getZSLQueueDepth();
    uint8_t getZSLBackLookCount();
    uint8_t getMaxUnmatchedFramesInQueue();
    bool isZSLMode();
    bool isRdiMode();
    bool isSecureMode();
    bool isNoDisplayMode();
    bool isWNREnabled();
    bool isTNRSnapshotEnabled();
    int32_t getCDSMode();
    bool isLTMForSeeMoreEnabled();
    bool isHfrMode();
    void getHfrFps(cam_fps_range_t &pFpsRange);
    uint8_t getNumOfSnapshots();
    uint8_t getNumOfRetroSnapshots();
    uint8_t getNumOfExtraHDRInBufsIfNeeded();
    uint8_t getNumOfExtraHDROutBufsIfNeeded();

    bool getRecordingHintValue();
    uint32_t getJpegQuality();
    uint32_t getRotation();
    uint32_t getDeviceRotation();
    uint32_t getJpegExifRotation();
    bool useJpegExifRotation();
    int32_t getEffectValue();
    bool isInstantAECEnabled();
    bool isInstantCaptureEnabled();
    uint8_t getAecFrameBoundValue();
    uint8_t getAecSkipDisplayFrameBound();

    int32_t getExifDateTime(String8 &dateTime, String8 &subsecTime);
    int32_t getExifFocalLength(rat_t *focalLenght);
    uint16_t getExifIsoSpeed();
    int32_t getExifGpsProcessingMethod(char *gpsProcessingMethod,
            uint32_t &count);
    int32_t getExifLatitude(rat_t *latitude, char *latRef);
    int32_t getExifLongitude(rat_t *longitude, char *lonRef);
    int32_t getExifAltitude(rat_t *altitude, char *altRef);
    int32_t getExifGpsDateTimeStamp(char *gpsDateStamp,
            uint32_t bufLen, rat_t *gpsTimeStamp);
    bool isVideoBuffersCached();
    int32_t updateFocusDistances(cam_focus_distances_info_t *focusDistances);

    bool isAEBracketEnabled();
    int32_t setAEBracketing();
    bool isFpsDebugEnabled();
    bool isHistogramEnabled();
    bool isSceneSelectionEnabled();
    bool isSmallJpegSizeEnabled();
    int32_t setSelectedScene(cam_scene_mode_type scene);
    cam_scene_mode_type getSelectedScene();
    bool isFaceDetectionEnabled();
    int32_t setFaceDetectionOption(bool enabled);
    int32_t setHistogram(bool enabled);
    int32_t setFaceDetection(bool enabled, bool initCommit);
    int32_t setFrameSkip(enum msm_vfe_frame_skip_pattern pattern);
    qcamera_thermal_mode getThermalMode();
    int32_t updateRecordingHintValue(int32_t value);
    int32_t setHDRAEBracket(cam_exp_bracketing_t hdrBracket);
    bool isHDREnabled();
    bool isAutoHDREnabled();
    int32_t stopAEBracket();
    int32_t updateRAW(cam_dimension_t max_dim);
    bool isDISEnabled();
    bool isAVTimerEnabled();
    int32_t setISType();
    cam_is_type_t getVideoISType();
    cam_is_type_t getPreviewISType();
    uint8_t getMobicatMask();

    cam_focus_mode_type getFocusMode() const;
    int32_t setNumOfSnapshot();
    int32_t adjustPreviewFpsRange(cam_fps_range_t *fpsRange);
    bool isJpegPictureFormat();
    bool isNV16PictureFormat();
    bool isNV21PictureFormat();
    cam_denoise_process_type_t getDenoiseProcessPlate(cam_intf_parm_type_t type);
    int32_t getMaxPicSize(cam_dimension_t &dim);
    int getFlipMode(cam_stream_type_t streamType);
    bool isSnapshotFDNeeded();

    bool isHDR1xFrameEnabled();
    bool isYUVFrameInfoNeeded();
    const char*getFrameFmtString(cam_format_t fmt);
    bool isHDR1xExtraBufferNeeded();
    bool isHDROutputCropEnabled();

    bool isPreviewFlipChanged();
    bool isVideoFlipChanged();
    bool isSnapshotFlipChanged();
    void setHDRSceneEnable(bool bflag);
    int32_t updateAWBParams(cam_awb_params_t &awb_params);

    const char *getASDStateString(cam_auto_scene_t scene);
    bool isHDRThumbnailProcessNeeded();
    void setMinPpMask(cam_feature_mask_t min_pp_mask);
    bool setStreamConfigure(bool isCapture,
            bool previewAsPostview, bool resetConfig, uint32_t* sessionId);
    int32_t addOnlineRotation(uint32_t rotation, uint32_t streamId,
            int32_t device_rotation);
    uint8_t getNumOfExtraBuffersForImageProc();
    uint8_t getNumOfExtraBuffersForVideo();
    uint8_t getNumOfExtraBuffersForPreview();
    uint32_t getExifBufIndex(uint32_t captureIndex);
    bool needThumbnailReprocess(cam_feature_mask_t *pFeatureMask);
    bool isUbiFocusEnabled();
    bool isChromaFlashEnabled();
    bool isHighQualityNoiseReductionMode();
    bool isTruePortraitEnabled();
    size_t getTPMaxMetaSize();
    bool isSeeMoreEnabled();
    bool isStillMoreEnabled();
    bool isOptiZoomEnabled();

    int32_t commitAFBracket(cam_af_bracketing_t afBracket);
    int32_t set3ALock(bool lock3A);
    int32_t setAndCommitZoom(int zoom_level);
    uint8_t getBurstCountForAdvancedCapture();
    uint32_t getNumberInBufsForSingleShot();
    uint32_t getNumberOutBufsForSingleShot();
    int32_t setLongshotEnable(bool enable);
    String8 dump();
    bool isUbiRefocus();
    uint32_t getRefocusMaxMetaSize();
    uint8_t getRefocusOutputCount();
    bool generateThumbFromMain();
    void updateCurrentFocusPosition(cam_focus_pos_info_t &cur_pos_info);
    void updateAEInfo(cam_3a_params_t &ae_params);
    bool isDisplayFrameNeeded();
    bool isAdvCamFeaturesEnabled();
    int32_t setAecLock(const char *aecStr);
    int32_t updateDebugLevel();
    bool is4k2kVideoResolution();
    bool isUBWCEnabled();

    int getBrightness();
    int32_t updateOisValue(bool oisValue);
    int32_t setIntEvent(cam_int_evt_params_t params);
    bool getofflineRAW();
    bool getQuadraCfa();
    int32_t updatePpFeatureMask(cam_stream_type_t stream_type);
    int32_t getStreamPpMask(cam_stream_type_t stream_type, cam_feature_mask_t &pp_mask);
    int32_t getSharpness();
    int32_t getEffect();
    int32_t updateFlashMode(cam_flash_mode_t flash_mode);
    int32_t configureAEBracketing(cam_capture_frame_config_t &frame_config);
    int32_t configureHDRBracketing(cam_capture_frame_config_t &frame_config);
    int32_t configFrameCapture(bool commitSettings);
    int32_t resetFrameCapture(bool commitSettings, bool lowLightEnabled);
    cam_still_more_t getStillMoreSettings();
    void setStillMoreSettings(cam_still_more_t stillmore_config);
    cam_still_more_t getStillMoreCapability();
    cam_dyn_img_data_t getDynamicImgData();
    void setDynamicImgData(cam_dyn_img_data_t d);

    int32_t getParmZoomLevel();
    int8_t getReprocCount();
    int8_t getCurPPCount();
    void setReprocCount();
    bool isPostProcScaling();
    bool isLLNoiseEnabled();
    void setCurPPCount(int8_t count);
    int32_t setQuadraCfaMode(uint32_t value, bool initCommit);
    int32_t setToneMapMode(uint32_t value, bool initCommit);
    void setTintless(bool enable);
    uint8_t getLongshotStages();
    int8_t getBufBatchCount();
    int8_t getVideoBatchSize();

    int32_t setManualCaptureMode(
            QCameraManualCaptureModes value = CAM_MANUAL_CAPTURE_TYPE_OFF);
    QCameraManualCaptureModes getManualCaptureMode();
    int64_t getExposureTime();

    cam_capture_frame_config_t getCaptureFrameConfig();
    void setJpegRotation(int rotation);
    uint32_t getJpegRotation();

    void setLowLightLevel(cam_low_light_mode_t value);
    cam_low_light_mode_t getLowLightLevel();
    bool getLowLightCapture();
    bool isLinkPreviewForLiveShot();

    /* Dual camera specific */
    bool getDcrf();
    int32_t setRelatedCamSyncInfo(
            cam_sync_related_sensors_event_info_t* info);
    const cam_sync_related_sensors_event_info_t*
            getRelatedCamSyncInfo(void);
    int32_t setFrameSyncEnabled(bool enable);
    bool isFrameSyncEnabled(void);
    int32_t getRelatedCamCalibration(
            cam_related_system_calibration_data_t* calib);
    int32_t bundleRelatedCameras(bool sync, uint32_t sessionid);
    uint8_t fdModeInVideo();
    bool isOEMFeatEnabled();

    int32_t setZslMode(bool value);
    int32_t updateZSLModeValue(bool value);

    bool isReprocScaleEnabled();
    bool isUnderReprocScaling();
    int32_t getPicSizeFromAPK(int &width, int &height);

    int32_t checkFeatureConcurrency();
    int32_t setInstantAEC(uint8_t enable, bool initCommit);

    int32_t getAnalysisInfo(
        bool fdVideoEnabled,
        bool hal3,
        cam_feature_mask_t featureMask,
        cam_analysis_info_t *pAnalysisInfo);
    int32_t updateDtVc(int32_t *dt, int32_t *vc);

private:
    QCameraParameters *mImpl;
    mutable Mutex mLock;
};

}; // namespace qcamera

#endif
