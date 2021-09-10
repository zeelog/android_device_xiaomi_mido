/*--------------------------------------------------------------------------
Copyright (c) 2010-2018, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of The Linux Foundation nor
      the names of its contributors may be used to endorse or promote
      products derived from this software without specific prior written
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/
#include "omx_video_encoder.h"
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <dlfcn.h>
#ifdef _ANDROID_ICS_
#include <media/hardware/HardwareAPI.h>
#endif
#ifdef _ANDROID_
#include <cutils/properties.h>
#endif
#ifdef _USE_GLIB_
#include <glib.h>
#define strlcpy g_strlcpy
#endif

static int bframes;
static int entropy;
static int perfmode;
static int lowlatency;
// factory function executed by the core to create instances
void *get_omx_component_factory_fn(void)
{
    return(new omx_venc);
}

omx_venc::perf_control::perf_control()
{
    m_perf_lib = NULL;
    m_perf_lock_acquire = NULL;
    m_perf_lock_release = NULL;
    m_perf_handle = 0;
}

omx_venc::perf_control::~perf_control()
{
    if (m_perf_handle != 0 && m_perf_lock_release) {
        m_perf_lock_release(m_perf_handle);
    }
    if (m_perf_lib) {
        dlclose(m_perf_lib);
    }
}

void omx_venc::perf_control::send_hint_to_mpctl(bool state)
{
    if (load_lib() == false) {
        return;
    }
    /* 0x4601 maps to video encode callback in
     * perflock, 46 is the enum number, 01 is
     * the state being sent when perflock
     * acquire succeeds
     */
    int arg = 0x4601;

    if (m_perf_lock_acquire && state == true) {
        m_perf_handle = m_perf_lock_acquire(0, 0, &arg, sizeof(arg) / sizeof(int));
        DEBUG_PRINT_INFO("Video encode perflock acquired,handle=%d",m_perf_handle);
    } else if (m_perf_lock_release && state == false) {
        m_perf_lock_release(m_perf_handle);
        DEBUG_PRINT_INFO("Video encode perflock released");
    }
}

bool omx_venc::perf_control::load_lib()
{
    char perf_lib_path[PROPERTY_VALUE_MAX] = {0};
    if (m_perf_lib)
        return true;

    if ((property_get("ro.vendor.extension_library", perf_lib_path, NULL) <= 0)) {
        DEBUG_PRINT_ERROR("vendor library not set in ro.vendor.extension_library");
        goto handle_err;
    }
    if ((m_perf_lib = dlopen(perf_lib_path, RTLD_NOW)) == NULL) {
        DEBUG_PRINT_ERROR("Failed to open %s : %s",perf_lib_path, dlerror());
        goto handle_err;
    } else {
        m_perf_lock_acquire = (perf_lock_acquire_t)dlsym(m_perf_lib, "perf_lock_acq");
        if (m_perf_lock_acquire == NULL) {
            DEBUG_PRINT_ERROR("Failed to load symbol: perf_lock_acq");
            goto handle_err;
        }
        m_perf_lock_release = (perf_lock_release_t)dlsym(m_perf_lib, "perf_lock_rel");
        if (m_perf_lock_release == NULL) {
            DEBUG_PRINT_ERROR("Failed to load symbol: perf_lock_rel");
            goto handle_err;
        }
    }
    return true;

handle_err:
    if(m_perf_lib != NULL) {
        dlclose(m_perf_lib);
    }
    return false;
}

//constructor

omx_venc::omx_venc()
{
#ifdef _ANDROID_ICS_
    meta_mode_enable = false;
    memset(meta_buffer_hdr,0,sizeof(meta_buffer_hdr));
    memset(meta_buffers,0,sizeof(meta_buffers));
    memset(opaque_buffer_hdr,0,sizeof(opaque_buffer_hdr));
    mUseProxyColorFormat = false;
    get_syntaxhdr_enable = false;
#endif
    bframes = entropy = 0;
    char property_value[PROPERTY_VALUE_MAX] = {0};
    property_get("vendor.vidc.debug.level", property_value, "1");
    debug_level = strtoul(property_value, NULL, 16);
    property_value[0] = '\0';
    property_get("vendor.vidc.debug.bframes", property_value, "0");
    bframes = atoi(property_value);
    property_value[0] = '\0';
    property_get("vendor.vidc.debug.entropy", property_value, "1");
    entropy = !!atoi(property_value);
    property_value[0] = '\0';
    property_get("vendor.vidc.debug.perf.mode", property_value, "0");
    perfmode = atoi(property_value);
    property_value[0] = '\0';
    handle = NULL;
    property_get("vendor.vidc.debug.lowlatency", property_value, "0");
    lowlatency = atoi(property_value);
    property_value[0] = '\0';
    m_perf_control.send_hint_to_mpctl(true);
}

omx_venc::~omx_venc()
{
    get_syntaxhdr_enable = false;
    m_perf_control.send_hint_to_mpctl(false);
    //nothing to do
}

/* ======================================================================
   FUNCTION
   omx_venc::ComponentInit

   DESCRIPTION
   Initialize the component.

   PARAMETERS
   ctxt -- Context information related to the self.
   id   -- Event identifier. This could be any of the following:
   1. Command completion event
   2. Buffer done callback event
   3. Frame done callback event

   RETURN VALUE
   None.

   ========================================================================== */
OMX_ERRORTYPE omx_venc::component_init(OMX_STRING role)
{

    OMX_ERRORTYPE eRet = OMX_ErrorNone;

    int fds[2];
    int r;

    OMX_VIDEO_CODINGTYPE codec_type;

    DEBUG_PRINT_HIGH("omx_venc(): Inside component_init()");
    // Copy the role information which provides the decoder m_nkind
    strlcpy((char *)m_nkind,role,OMX_MAX_STRINGNAME_SIZE);
    secure_session = false;

    if (!strncmp((char *)m_nkind,"OMX.qcom.video.encoder.mpeg4",\
                OMX_MAX_STRINGNAME_SIZE)) {
        strlcpy((char *)m_cRole, "video_encoder.mpeg4",\
                OMX_MAX_STRINGNAME_SIZE);
        codec_type = OMX_VIDEO_CodingMPEG4;
    } else if (!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.h263",\
                OMX_MAX_STRINGNAME_SIZE)) {
        strlcpy((char *)m_cRole, "video_encoder.h263",OMX_MAX_STRINGNAME_SIZE);
        codec_type = OMX_VIDEO_CodingH263;
    } else if (!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.avc",\
                OMX_MAX_STRINGNAME_SIZE)) {
        strlcpy((char *)m_cRole, "video_encoder.avc",OMX_MAX_STRINGNAME_SIZE);
        codec_type = OMX_VIDEO_CodingAVC;
    } else if(!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.avc.secure",\
                OMX_MAX_STRINGNAME_SIZE)) {
        strlcpy((char *)m_cRole, "video_encoder.avc",OMX_MAX_STRINGNAME_SIZE);
        codec_type = OMX_VIDEO_CodingAVC;
        secure_session = true;
    }
#ifdef _MSM8974_
    else if (!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.vp8",    \
                OMX_MAX_STRINGNAME_SIZE)) {
        strlcpy((char *)m_cRole, "video_encoder.vp8",OMX_MAX_STRINGNAME_SIZE);
        codec_type = OMX_VIDEO_CodingVP8;
    }
#endif
    else if (!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.hevc",    \
                OMX_MAX_STRINGNAME_SIZE)) {
        strlcpy((char *)m_cRole, "video_encoder.hevc", OMX_MAX_STRINGNAME_SIZE);
        codec_type = OMX_VIDEO_CodingHEVC;
    } else if (!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.hevc.secure",    \
                OMX_MAX_STRINGNAME_SIZE)) {
        strlcpy((char *)m_cRole, "video_encoder.hevc", OMX_MAX_STRINGNAME_SIZE);
        codec_type = OMX_VIDEO_CodingHEVC;
        secure_session = true;
    } else {
        DEBUG_PRINT_ERROR("ERROR: Unknown Component");
        eRet = OMX_ErrorInvalidComponentName;
    }

    if (eRet != OMX_ErrorNone) {
        return eRet;
    }
#ifdef ENABLE_GET_SYNTAX_HDR
    get_syntaxhdr_enable = true;
    DEBUG_PRINT_HIGH("Get syntax header enabled");
#endif

    handle = new venc_dev(this);

    if (handle == NULL) {
        DEBUG_PRINT_ERROR("ERROR: handle is NULL");
        return OMX_ErrorInsufficientResources;
    }

    if (handle->venc_open(codec_type) != true) {
        DEBUG_PRINT_ERROR("ERROR: venc_open failed");
        eRet = OMX_ErrorInsufficientResources;
        goto init_error;
    }

    //Intialise the OMX layer variables
    memset(&m_pCallbacks,0,sizeof(OMX_CALLBACKTYPE));

    OMX_INIT_STRUCT(&m_sPortParam, OMX_PORT_PARAM_TYPE);
    m_sPortParam.nPorts = 0x2;
    m_sPortParam.nStartPortNumber = (OMX_U32) PORT_INDEX_IN;

    OMX_INIT_STRUCT(&m_sPortParam_audio, OMX_PORT_PARAM_TYPE);
    m_sPortParam_audio.nPorts = 0;
    m_sPortParam_audio.nStartPortNumber = 0;

    OMX_INIT_STRUCT(&m_sPortParam_img, OMX_PORT_PARAM_TYPE);
    m_sPortParam_img.nPorts = 0;
    m_sPortParam_img.nStartPortNumber = 0;

    OMX_INIT_STRUCT(&m_sParamBitrate, OMX_VIDEO_PARAM_BITRATETYPE);
    m_sParamBitrate.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sParamBitrate.eControlRate = OMX_Video_ControlRateVariableSkipFrames;
    m_sParamBitrate.nTargetBitrate = 64000;

    OMX_INIT_STRUCT(&m_sConfigBitrate, OMX_VIDEO_CONFIG_BITRATETYPE);
    m_sConfigBitrate.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sConfigBitrate.nEncodeBitrate = 64000;

    OMX_INIT_STRUCT(&m_sConfigFramerate, OMX_CONFIG_FRAMERATETYPE);
    m_sConfigFramerate.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sConfigFramerate.xEncodeFramerate = 30 << 16;

    OMX_INIT_STRUCT(&m_sConfigIntraRefreshVOP, OMX_CONFIG_INTRAREFRESHVOPTYPE);
    m_sConfigIntraRefreshVOP.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sConfigIntraRefreshVOP.IntraRefreshVOP = OMX_FALSE;

    OMX_INIT_STRUCT(&m_sConfigFrameRotation, OMX_CONFIG_ROTATIONTYPE);
    m_sConfigFrameRotation.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sConfigFrameRotation.nRotation = 0;

    OMX_INIT_STRUCT(&m_sConfigAVCIDRPeriod, OMX_VIDEO_CONFIG_AVCINTRAPERIOD);
    m_sConfigAVCIDRPeriod.nPortIndex = (OMX_U32) PORT_INDEX_OUT;

    OMX_INIT_STRUCT(&m_sPrependSPSPPS, PrependSPSPPSToIDRFramesParams);
    m_sPrependSPSPPS.bEnable = OMX_FALSE;

    OMX_INIT_STRUCT(&m_sSessionQuantization, OMX_VIDEO_PARAM_QUANTIZATIONTYPE);
    m_sSessionQuantization.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sSessionQuantization.nQpI = 9;
    m_sSessionQuantization.nQpP = 6;
    m_sSessionQuantization.nQpB = 2;

    OMX_INIT_STRUCT(&m_sSessionQPRange, OMX_QCOM_VIDEO_PARAM_QPRANGETYPE);
    OMX_INIT_STRUCT(&m_sSessionIPBQPRange, OMX_QCOM_VIDEO_PARAM_IPB_QPRANGETYPE);
    m_sSessionQPRange.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sSessionIPBQPRange.nPortIndex = (OMX_U32)PORT_INDEX_OUT;
    m_sSessionQPRange.minQP = 2;
    if (codec_type == OMX_VIDEO_CodingAVC) {
        m_sSessionQPRange.maxQP = 51;
    } else {
        m_sSessionQPRange.maxQP = 31;
    }
    m_sSessionIPBQPRange.minIQP =
                m_sSessionIPBQPRange.minPQP =
                m_sSessionIPBQPRange.minBQP = m_sSessionQPRange.minQP;
    m_sSessionIPBQPRange.maxIQP =
                m_sSessionIPBQPRange.maxPQP =
                m_sSessionIPBQPRange.maxBQP = m_sSessionQPRange.maxQP;
    OMX_INIT_STRUCT(&m_sAVCSliceFMO, OMX_VIDEO_PARAM_AVCSLICEFMO);
    m_sAVCSliceFMO.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sAVCSliceFMO.eSliceMode = OMX_VIDEO_SLICEMODE_AVCDefault;
    m_sAVCSliceFMO.nNumSliceGroups = 0;
    m_sAVCSliceFMO.nSliceGroupMapType = 0;
    OMX_INIT_STRUCT(&m_sParamProfileLevel, OMX_VIDEO_PARAM_PROFILELEVELTYPE);
    m_sParamProfileLevel.nPortIndex = (OMX_U32) PORT_INDEX_OUT;

    OMX_INIT_STRUCT(&m_sIntraperiod, QOMX_VIDEO_INTRAPERIODTYPE);
    m_sIntraperiod.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sIntraperiod.nPFrames = (m_sConfigFramerate.xEncodeFramerate * 2) - 1;

    OMX_INIT_STRUCT(&m_sErrorCorrection, OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE);
    m_sErrorCorrection.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sErrorCorrection.bEnableDataPartitioning = OMX_FALSE;
    m_sErrorCorrection.bEnableHEC = OMX_FALSE;
    m_sErrorCorrection.bEnableResync = OMX_FALSE;
    m_sErrorCorrection.bEnableRVLC = OMX_FALSE;
    m_sErrorCorrection.nResynchMarkerSpacing = 0;

    OMX_INIT_STRUCT(&m_sSliceSpacing, QOMX_VIDEO_PARAM_SLICE_SPACING_TYPE);
    m_sSliceSpacing.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sSliceSpacing.eSliceMode = QOMX_SLICEMODE_MB_COUNT;

    OMX_INIT_STRUCT(&m_sIntraRefresh, OMX_VIDEO_PARAM_INTRAREFRESHTYPE);
    m_sIntraRefresh.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sIntraRefresh.eRefreshMode = OMX_VIDEO_IntraRefreshMax;

#ifdef SUPPORT_CONFIG_INTRA_REFRESH
    OMX_INIT_STRUCT(&m_sConfigIntraRefresh, OMX_VIDEO_CONFIG_ANDROID_INTRAREFRESHTYPE);
    m_sConfigIntraRefresh.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sConfigIntraRefresh.nRefreshPeriod = 0;
#endif

    OMX_INIT_STRUCT(&m_sConfigColorAspects, DescribeColorAspectsParams);
    m_sConfigColorAspects.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sConfigColorAspects.sAspects.mRange =  ColorAspects::RangeUnspecified;
    m_sConfigColorAspects.sAspects.mPrimaries = ColorAspects::PrimariesUnspecified;
    m_sConfigColorAspects.sAspects.mMatrixCoeffs = ColorAspects::MatrixUnspecified;
    m_sConfigColorAspects.sAspects.mTransfer = ColorAspects::TransferUnspecified;

    if (codec_type == OMX_VIDEO_CodingMPEG4) {
        m_sParamProfileLevel.eProfile = (OMX_U32) OMX_VIDEO_MPEG4ProfileSimple;
        m_sParamProfileLevel.eLevel = (OMX_U32) OMX_VIDEO_MPEG4Level0;
    } else if (codec_type == OMX_VIDEO_CodingH263) {
        m_sParamProfileLevel.eProfile = (OMX_U32) OMX_VIDEO_H263ProfileBaseline;
        m_sParamProfileLevel.eLevel = (OMX_U32) OMX_VIDEO_H263Level10;
    } else if (codec_type == OMX_VIDEO_CodingAVC) {
        m_sParamProfileLevel.eProfile = (OMX_U32) OMX_VIDEO_AVCProfileBaseline;
        m_sParamProfileLevel.eLevel = (OMX_U32) OMX_VIDEO_AVCLevel1;
    } else if (codec_type == OMX_VIDEO_CodingVP8) {
        m_sParamProfileLevel.eProfile = (OMX_U32) OMX_VIDEO_VP8ProfileMain;
        m_sParamProfileLevel.eLevel = (OMX_U32) OMX_VIDEO_VP8Level_Version0;
    } else if (codec_type == OMX_VIDEO_CodingHEVC) {
        m_sParamProfileLevel.eProfile = (OMX_U32) OMX_VIDEO_HEVCProfileMain;
        m_sParamProfileLevel.eLevel = (OMX_U32) OMX_VIDEO_HEVCMainTierLevel1;
    }

    OMX_INIT_STRUCT(&m_sParamEntropy,  QOMX_VIDEO_H264ENTROPYCODINGTYPE);
    m_sParamEntropy.bCabac = OMX_FALSE;

    // Initialize the video parameters for input port
    OMX_INIT_STRUCT(&m_sInPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
    m_sInPortDef.nPortIndex= (OMX_U32) PORT_INDEX_IN;
    m_sInPortDef.bEnabled = OMX_TRUE;
    m_sInPortDef.bPopulated = OMX_FALSE;
    m_sInPortDef.eDomain = OMX_PortDomainVideo;
    m_sInPortDef.eDir = OMX_DirInput;
    m_sInPortDef.format.video.cMIMEType = (char *)"YUV420";
    m_sInPortDef.format.video.nFrameWidth = OMX_CORE_QCIF_WIDTH;
    m_sInPortDef.format.video.nFrameHeight = OMX_CORE_QCIF_HEIGHT;
    m_sInPortDef.format.video.nStride = OMX_CORE_QCIF_WIDTH;
    m_sInPortDef.format.video.nSliceHeight = OMX_CORE_QCIF_HEIGHT;
    m_sInPortDef.format.video.nBitrate = 64000;
    m_sInPortDef.format.video.xFramerate = 15 << 16;
    m_sInPortDef.format.video.eColorFormat = (OMX_COLOR_FORMATTYPE)
        QOMX_DEFAULT_COLOR_FMT;
    m_sInPortDef.format.video.eCompressionFormat =  OMX_VIDEO_CodingUnused;

    if (dev_get_buf_req(&m_sInPortDef.nBufferCountMin,
                &m_sInPortDef.nBufferCountActual,
                &m_sInPortDef.nBufferSize,
                m_sInPortDef.nPortIndex) != true) {
        eRet = OMX_ErrorUndefined;
        goto init_error;
    }

    // Initialize the video parameters for output port
    OMX_INIT_STRUCT(&m_sOutPortDef, OMX_PARAM_PORTDEFINITIONTYPE);
    m_sOutPortDef.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sOutPortDef.bEnabled = OMX_TRUE;
    m_sOutPortDef.bPopulated = OMX_FALSE;
    m_sOutPortDef.eDomain = OMX_PortDomainVideo;
    m_sOutPortDef.eDir = OMX_DirOutput;
    m_sOutPortDef.format.video.nFrameWidth = OMX_CORE_QCIF_WIDTH;
    m_sOutPortDef.format.video.nFrameHeight = OMX_CORE_QCIF_HEIGHT;
    m_sOutPortDef.format.video.nBitrate = 64000;
    m_sOutPortDef.format.video.xFramerate = 15 << 16;
    m_sOutPortDef.format.video.eColorFormat =  OMX_COLOR_FormatUnused;
    if (codec_type == OMX_VIDEO_CodingMPEG4) {
        m_sOutPortDef.format.video.eCompressionFormat =  OMX_VIDEO_CodingMPEG4;
    } else if (codec_type == OMX_VIDEO_CodingH263) {
        m_sOutPortDef.format.video.eCompressionFormat =  OMX_VIDEO_CodingH263;
    } else if (codec_type == OMX_VIDEO_CodingAVC) {
        m_sOutPortDef.format.video.eCompressionFormat =  OMX_VIDEO_CodingAVC;
    } else if (codec_type == OMX_VIDEO_CodingVP8) {
        m_sOutPortDef.format.video.eCompressionFormat =  OMX_VIDEO_CodingVP8;
    } else if (codec_type == OMX_VIDEO_CodingHEVC) {
        m_sOutPortDef.format.video.eCompressionFormat =  OMX_VIDEO_CodingHEVC;
    }

    if (dev_get_buf_req(&m_sOutPortDef.nBufferCountMin,
                &m_sOutPortDef.nBufferCountActual,
                &m_sOutPortDef.nBufferSize,
                m_sOutPortDef.nPortIndex) != true) {
        eRet = OMX_ErrorUndefined;
        goto init_error;
    }

    // Initialize the video color format for input port
    OMX_INIT_STRUCT(&m_sInPortFormat, OMX_VIDEO_PARAM_PORTFORMATTYPE);
    m_sInPortFormat.nPortIndex = (OMX_U32) PORT_INDEX_IN;
    m_sInPortFormat.nIndex = 0;
    m_sInPortFormat.eColorFormat = (OMX_COLOR_FORMATTYPE)
        QOMX_DEFAULT_COLOR_FMT;
    m_sInPortFormat.eCompressionFormat = OMX_VIDEO_CodingUnused;


    // Initialize the compression format for output port
    OMX_INIT_STRUCT(&m_sOutPortFormat, OMX_VIDEO_PARAM_PORTFORMATTYPE);
    m_sOutPortFormat.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sOutPortFormat.nIndex = 0;
    m_sOutPortFormat.eColorFormat = OMX_COLOR_FormatUnused;
    if (codec_type == OMX_VIDEO_CodingMPEG4) {
        m_sOutPortFormat.eCompressionFormat =  OMX_VIDEO_CodingMPEG4;
    } else if (codec_type == OMX_VIDEO_CodingH263) {
        m_sOutPortFormat.eCompressionFormat =  OMX_VIDEO_CodingH263;
    } else if (codec_type == OMX_VIDEO_CodingAVC) {
        m_sOutPortFormat.eCompressionFormat =  OMX_VIDEO_CodingAVC;
    } else if (codec_type == OMX_VIDEO_CodingVP8) {
        m_sOutPortFormat.eCompressionFormat =  OMX_VIDEO_CodingVP8;
    } else if (codec_type == OMX_VIDEO_CodingHEVC) {
        m_sOutPortFormat.eCompressionFormat =  OMX_VIDEO_CodingHEVC;
    }

    // mandatory Indices for kronos test suite
    OMX_INIT_STRUCT(&m_sPriorityMgmt, OMX_PRIORITYMGMTTYPE);

    OMX_INIT_STRUCT(&m_sInBufSupplier, OMX_PARAM_BUFFERSUPPLIERTYPE);
    m_sInBufSupplier.nPortIndex = (OMX_U32) PORT_INDEX_IN;

    OMX_INIT_STRUCT(&m_sOutBufSupplier, OMX_PARAM_BUFFERSUPPLIERTYPE);
    m_sOutBufSupplier.nPortIndex = (OMX_U32) PORT_INDEX_OUT;

    OMX_INIT_STRUCT(&m_sParamInitqp, QOMX_EXTNINDEX_VIDEO_INITIALQP);
    m_sParamInitqp.nPortIndex = (OMX_U32) PORT_INDEX_OUT;

    // mp4 specific init
    OMX_INIT_STRUCT(&m_sParamMPEG4, OMX_VIDEO_PARAM_MPEG4TYPE);
    m_sParamMPEG4.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sParamMPEG4.eProfile = OMX_VIDEO_MPEG4ProfileSimple;
    m_sParamMPEG4.eLevel = OMX_VIDEO_MPEG4Level0;
    m_sParamMPEG4.nSliceHeaderSpacing = 0;
    m_sParamMPEG4.bSVH = OMX_FALSE;
    m_sParamMPEG4.bGov = OMX_FALSE;
    m_sParamMPEG4.nPFrames = (m_sOutPortFormat.xFramerate * 2 - 1);  // 2 second intra period for default outport fps
    m_sParamMPEG4.bACPred = OMX_TRUE;
    m_sParamMPEG4.nTimeIncRes = 30; // delta = 2 @ 15 fps
    m_sParamMPEG4.nAllowedPictureTypes = 2; // pframe and iframe
    m_sParamMPEG4.nHeaderExtension = 1; // number of video packet headers per vop
    m_sParamMPEG4.bReversibleVLC = OMX_FALSE;

    // h263 specific init
    OMX_INIT_STRUCT(&m_sParamH263, OMX_VIDEO_PARAM_H263TYPE);
    m_sParamH263.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sParamH263.nPFrames = (m_sOutPortFormat.xFramerate * 2 - 1); // 2 second intra period for default outport fps
    m_sParamH263.nBFrames = 0;
    m_sParamH263.eProfile = OMX_VIDEO_H263ProfileBaseline;
    m_sParamH263.eLevel = OMX_VIDEO_H263Level10;
    m_sParamH263.bPLUSPTYPEAllowed = OMX_FALSE;
    m_sParamH263.nAllowedPictureTypes = 2;
    m_sParamH263.bForceRoundingTypeToZero = OMX_TRUE;
    m_sParamH263.nPictureHeaderRepetition = 0;
    m_sParamH263.nGOBHeaderInterval = 1;

    // h264 specific init
    OMX_INIT_STRUCT(&m_sParamAVC, OMX_VIDEO_PARAM_AVCTYPE);
    m_sParamAVC.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sParamAVC.nSliceHeaderSpacing = 0;
    m_sParamAVC.nPFrames = (m_sOutPortFormat.xFramerate * 2 - 1); // 2 second intra period for default outport fps
    m_sParamAVC.nBFrames = 0;
    m_sParamAVC.bUseHadamard = OMX_FALSE;
    m_sParamAVC.nRefIdx10ActiveMinus1 = 1;
    m_sParamAVC.nRefIdx11ActiveMinus1 = 0;
    m_sParamAVC.bEnableUEP = OMX_FALSE;
    m_sParamAVC.bEnableFMO = OMX_FALSE;
    m_sParamAVC.bEnableASO = OMX_FALSE;
    m_sParamAVC.bEnableRS = OMX_FALSE;
    m_sParamAVC.eProfile = OMX_VIDEO_AVCProfileBaseline;
    m_sParamAVC.eLevel = OMX_VIDEO_AVCLevel1;
    m_sParamAVC.nAllowedPictureTypes = 2;
    m_sParamAVC.bFrameMBsOnly = OMX_FALSE;
    m_sParamAVC.bMBAFF = OMX_FALSE;
    m_sParamAVC.bEntropyCodingCABAC = OMX_FALSE;
    m_sParamAVC.bWeightedPPrediction = OMX_FALSE;
    m_sParamAVC.nWeightedBipredicitonMode = 0;
    m_sParamAVC.bconstIpred = OMX_FALSE;
    m_sParamAVC.bDirect8x8Inference = OMX_FALSE;
    m_sParamAVC.bDirectSpatialTemporal = OMX_FALSE;
    m_sParamAVC.nCabacInitIdc = 0;
    m_sParamAVC.eLoopFilterMode = OMX_VIDEO_AVCLoopFilterEnable;

    // VP8 specific init
    OMX_INIT_STRUCT(&m_sParamVP8, OMX_VIDEO_PARAM_VP8TYPE);
    m_sParamVP8.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sParamVP8.eProfile = OMX_VIDEO_VP8ProfileMain;
    m_sParamVP8.eLevel = OMX_VIDEO_VP8Level_Version0;
    m_sParamVP8.nDCTPartitions = 0;
    m_sParamVP8.bErrorResilientMode = OMX_FALSE;

    // HEVC specific init
    OMX_INIT_STRUCT(&m_sParamHEVC, OMX_VIDEO_PARAM_HEVCTYPE);
    m_sParamHEVC.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sParamHEVC.eProfile =  OMX_VIDEO_HEVCProfileMain;
    m_sParamHEVC.eLevel =  OMX_VIDEO_HEVCMainTierLevel1;

    OMX_INIT_STRUCT(&m_sParamLTRMode, QOMX_VIDEO_PARAM_LTRMODE_TYPE);
    m_sParamLTRMode.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sParamLTRMode.eLTRMode = QOMX_VIDEO_LTRMode_Disable;

    OMX_INIT_STRUCT(&m_sParamLTRCount, QOMX_VIDEO_PARAM_LTRCOUNT_TYPE);
    m_sParamLTRCount.nPortIndex = (OMX_U32) PORT_INDEX_OUT;

#ifdef LTR_SUPPORT
    m_sParamLTRCount.nCount = 3; //Non zero count indicated LTR feature support
#else
    m_sParamLTRCount.nCount = 0;
#endif

    OMX_INIT_STRUCT(&m_sConfigDeinterlace, OMX_VIDEO_CONFIG_DEINTERLACE);
    m_sConfigDeinterlace.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sConfigDeinterlace.nEnable = OMX_FALSE;

    OMX_INIT_STRUCT(&m_sHierLayers, QOMX_VIDEO_HIERARCHICALLAYERS);
    m_sHierLayers.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sHierLayers.nNumLayers = 0;
    m_sHierLayers.eHierarchicalCodingType = QOMX_HIERARCHICALCODING_P;

    OMX_INIT_STRUCT(&m_sMBIStatistics, OMX_QOMX_VIDEO_MBI_STATISTICS);
    m_sMBIStatistics.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sMBIStatistics.eMBIStatisticsType = QOMX_MBI_STATISTICS_MODE_DEFAULT;

    OMX_INIT_STRUCT(&m_slowLatencyMode, QOMX_EXTNINDEX_VIDEO_VENC_LOW_LATENCY_MODE);
    m_slowLatencyMode.bLowLatencyMode = OMX_FALSE;

    OMX_INIT_STRUCT(&m_sParamTemporalLayers, OMX_VIDEO_PARAM_ANDROID_TEMPORALLAYERINGTYPE);
    m_sParamTemporalLayers.eSupportedPatterns = OMX_VIDEO_AndroidTemporalLayeringPatternAndroid;

    OMX_INIT_STRUCT(&m_sConfigTemporalLayers, OMX_VIDEO_CONFIG_ANDROID_TEMPORALLAYERINGTYPE);

    OMX_INIT_STRUCT(&m_sParamAVTimerTimestampMode, QOMX_ENABLETYPE);
    m_sParamAVTimerTimestampMode.bEnable = OMX_FALSE;

    OMX_INIT_STRUCT(&m_sConfigQP, OMX_SKYPE_VIDEO_CONFIG_QP);
    m_sConfigQP.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sConfigQP.nQP = 30;

    OMX_INIT_STRUCT(&m_sParamControlInputQueue , QOMX_ENABLETYPE);
    m_sParamControlInputQueue.bEnable = OMX_FALSE;

    OMX_INIT_STRUCT(&m_sConfigInputTrigTS, OMX_TIME_CONFIG_TIMESTAMPTYPE);

    m_state                   = OMX_StateLoaded;
    m_sExtraData = 0;

    if (eRet == OMX_ErrorNone) {
        msg_thread_created = true;
        r = pthread_create(&msg_thread_id,0, message_thread_enc, this);
        if (r < 0) {
            DEBUG_PRINT_ERROR("ERROR: message_thread_enc thread creation failed");
            eRet = OMX_ErrorInsufficientResources;
            msg_thread_created = false;
            goto init_error;
        } else {
            async_thread_created = true;
            r = pthread_create(&async_thread_id,0, venc_dev::async_venc_message_thread, this);
            if (r < 0) {
                DEBUG_PRINT_ERROR("ERROR: venc_dev::async_venc_message_thread thread creation failed");
                eRet = OMX_ErrorInsufficientResources;
                async_thread_created = false;
                msg_thread_stop = true;
                pthread_join(msg_thread_id,NULL);
                msg_thread_created = false;
                goto init_error;
            } else
                dev_set_message_thread_id(async_thread_id);
        }
    }

    if (lowlatency)
    {
        QOMX_ENABLETYPE low_latency;
        low_latency.bEnable = OMX_TRUE;
        DEBUG_PRINT_LOW("Enable lowlatency mode");
        if (!handle->venc_set_param(&low_latency,
               (OMX_INDEXTYPE)OMX_QcomIndexConfigVideoVencLowLatencyMode)) {
            DEBUG_PRINT_ERROR("Failed enabling low latency mode");
        }
    }
    DEBUG_PRINT_INFO("Component_init : %s : return = 0x%x", m_nkind, eRet);

    {
        VendorExtensionStore *extStore = const_cast<VendorExtensionStore *>(&mVendorExtensionStore);
        init_vendor_extensions(*extStore);
        mVendorExtensionStore.dumpExtensions((const char *)m_nkind);
    }

    return eRet;
init_error:
    handle->venc_close();
    delete handle;
    handle = NULL;
    return eRet;
}


/* ======================================================================
   FUNCTION
   omx_venc::Setparameter

   DESCRIPTION
   OMX Set Parameter method implementation.

   PARAMETERS
   <TBD>.

   RETURN VALUE
   OMX Error None if successful.

   ========================================================================== */
OMX_ERRORTYPE  omx_venc::set_parameter(OMX_IN OMX_HANDLETYPE     hComp,
        OMX_IN OMX_INDEXTYPE paramIndex,
        OMX_IN OMX_PTR        paramData)
{
    (void)hComp;
    OMX_ERRORTYPE eRet = OMX_ErrorNone;


    if (m_state == OMX_StateInvalid) {
        DEBUG_PRINT_ERROR("ERROR: Set Param in Invalid State");
        return OMX_ErrorInvalidState;
    }
    if (paramData == NULL) {
        DEBUG_PRINT_ERROR("ERROR: Get Param in Invalid paramData");
        return OMX_ErrorBadParameter;
    }

    /*set_parameter can be called in loaded state
      or disabled port */
    if (m_state == OMX_StateLoaded
            || m_sInPortDef.bEnabled == OMX_FALSE
            || m_sOutPortDef.bEnabled == OMX_FALSE) {
        DEBUG_PRINT_LOW("Set Parameter called in valid state");
    } else {
        DEBUG_PRINT_ERROR("ERROR: Set Parameter called in Invalid State");
        return OMX_ErrorIncorrectStateOperation;
    }

    switch ((int)paramIndex) {
        case OMX_IndexParamPortDefinition:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_PARAM_PORTDEFINITIONTYPE);
                OMX_PARAM_PORTDEFINITIONTYPE *portDefn;
                portDefn = (OMX_PARAM_PORTDEFINITIONTYPE *) paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamPortDefinition H= %d, W = %d",
                        (int)portDefn->format.video.nFrameHeight,
                        (int)portDefn->format.video.nFrameWidth);

                if (PORT_INDEX_IN == portDefn->nPortIndex) {
                    if (!dev_is_video_session_supported(portDefn->format.video.nFrameWidth,
                                portDefn->format.video.nFrameHeight)) {
                        DEBUG_PRINT_ERROR("video session not supported");
                        omx_report_unsupported_setting();
                        return OMX_ErrorUnsupportedSetting;
                    }
                    DEBUG_PRINT_LOW("i/p actual cnt requested = %u", (unsigned int)portDefn->nBufferCountActual);
                    DEBUG_PRINT_LOW("i/p min cnt requested = %u", (unsigned int)portDefn->nBufferCountMin);
                    DEBUG_PRINT_LOW("i/p buffersize requested = %u", (unsigned int)portDefn->nBufferSize);
                    if (portDefn->nBufferCountActual > MAX_NUM_INPUT_BUFFERS) {
                        DEBUG_PRINT_ERROR("ERROR: (In_PORT) actual count (%u) exceeds max(%u)",
                                (unsigned int)portDefn->nBufferCountActual, (unsigned int)MAX_NUM_INPUT_BUFFERS);
                        return OMX_ErrorUnsupportedSetting;
                    }
                    if (m_inp_mem_ptr &&
                            (portDefn->nBufferCountActual != m_sInPortDef.nBufferCountActual ||
                            portDefn->nBufferSize != m_sInPortDef.nBufferSize)) {
                        DEBUG_PRINT_ERROR("ERROR: (In_PORT) buffer count/size can change only if port is unallocated !");
                        return OMX_ErrorInvalidState;
                    }
                    if (portDefn->nBufferCountMin > portDefn->nBufferCountActual) {
                        DEBUG_PRINT_ERROR("ERROR: (In_PORT) Min buffers (%u) > actual count (%u)",
                                (unsigned int)portDefn->nBufferCountMin, (unsigned int)portDefn->nBufferCountActual);
                        return OMX_ErrorUnsupportedSetting;
                    }
                    if (handle->venc_set_param(paramData,OMX_IndexParamPortDefinition) != true) {
                        DEBUG_PRINT_ERROR("ERROR: venc_set_param input failed");
                        return handle->hw_overload ? OMX_ErrorInsufficientResources :
                                OMX_ErrorUnsupportedSetting;
                    }

                    DEBUG_PRINT_LOW("i/p previous actual cnt = %u", (unsigned int)m_sInPortDef.nBufferCountActual);
                    DEBUG_PRINT_LOW("i/p previous min cnt = %u", (unsigned int)m_sInPortDef.nBufferCountMin);
                    memcpy(&m_sInPortDef, portDefn,sizeof(OMX_PARAM_PORTDEFINITIONTYPE));

#ifdef _ANDROID_ICS_
                    if (portDefn->format.video.eColorFormat ==
                            (OMX_COLOR_FORMATTYPE)QOMX_COLOR_FormatAndroidOpaque) {
                        m_sInPortDef.format.video.eColorFormat = (OMX_COLOR_FORMATTYPE)
                            QOMX_DEFAULT_COLOR_FMT;
                        if (!mUseProxyColorFormat) {
                            if (!c2d_conv.init()) {
                                DEBUG_PRINT_ERROR("C2D init failed");
                                return OMX_ErrorUnsupportedSetting;
                            }
                            DEBUG_PRINT_HIGH("C2D init is successful");
                        }
                        mUseProxyColorFormat = true;
                        m_input_msg_id = OMX_COMPONENT_GENERATE_ETB_OPQ;
                    } else
                        mUseProxyColorFormat = false;
#endif
                    /*Query Input Buffer Requirements*/
                    dev_get_buf_req   (&m_sInPortDef.nBufferCountMin,
                            &m_sInPortDef.nBufferCountActual,
                            &m_sInPortDef.nBufferSize,
                            m_sInPortDef.nPortIndex);

                    /*Query ouput Buffer Requirements*/
                    dev_get_buf_req   (&m_sOutPortDef.nBufferCountMin,
                            &m_sOutPortDef.nBufferCountActual,
                            &m_sOutPortDef.nBufferSize,
                            m_sOutPortDef.nPortIndex);
                    m_sInPortDef.nBufferCountActual = portDefn->nBufferCountActual;
                } else if (PORT_INDEX_OUT == portDefn->nPortIndex) {
                    DEBUG_PRINT_LOW("o/p actual cnt requested = %u", (unsigned int)portDefn->nBufferCountActual);
                    DEBUG_PRINT_LOW("o/p min cnt requested = %u", (unsigned int)portDefn->nBufferCountMin);
                    DEBUG_PRINT_LOW("o/p buffersize requested = %u", (unsigned int)portDefn->nBufferSize);

                    if (portDefn->nBufferCountActual > MAX_NUM_OUTPUT_BUFFERS) {
                        DEBUG_PRINT_ERROR("ERROR: (Out_PORT) actual count (%u) exceeds max(%u)",
                                (unsigned int)portDefn->nBufferCountActual, (unsigned int)MAX_NUM_OUTPUT_BUFFERS);
                        return OMX_ErrorUnsupportedSetting;
                    }
                    if (m_out_mem_ptr &&
                            (portDefn->nBufferCountActual != m_sOutPortDef.nBufferCountActual ||
                            portDefn->nBufferSize != m_sOutPortDef.nBufferSize)) {
                        DEBUG_PRINT_ERROR("ERROR: (Out_PORT) buffer count/size can change only if port is unallocated !");
                        return OMX_ErrorInvalidState;
                    }

                    if (portDefn->nBufferCountMin > portDefn->nBufferCountActual) {
                        DEBUG_PRINT_ERROR("ERROR: (Out_PORT) Min buffers (%u) > actual count (%u)",
                                (unsigned int)portDefn->nBufferCountMin, (unsigned int)portDefn->nBufferCountActual);
                        return OMX_ErrorUnsupportedSetting;
                    }
                    if (handle->venc_set_param(paramData,OMX_IndexParamPortDefinition) != true) {
                        DEBUG_PRINT_ERROR("ERROR: venc_set_param output failed");
                        return OMX_ErrorUnsupportedSetting;
                    }
                    memcpy(&m_sOutPortDef,portDefn,sizeof(struct OMX_PARAM_PORTDEFINITIONTYPE));
#ifdef _MSM8974_
                    /*Query ouput Buffer Requirements*/
                    dev_get_buf_req(&m_sOutPortDef.nBufferCountMin,
                            &m_sOutPortDef.nBufferCountActual,
                            &m_sOutPortDef.nBufferSize,
                            m_sOutPortDef.nPortIndex);
#endif
                    update_profile_level(); //framerate , bitrate

                    DEBUG_PRINT_LOW("o/p previous actual cnt = %u", (unsigned int)m_sOutPortDef.nBufferCountActual);
                    DEBUG_PRINT_LOW("o/p previous min cnt = %u", (unsigned int)m_sOutPortDef.nBufferCountMin);
                    m_sOutPortDef.nBufferCountActual = portDefn->nBufferCountActual;
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Set_parameter: Bad Port idx %d",
                            (int)portDefn->nPortIndex);
                    eRet = OMX_ErrorBadPortIndex;
                }
                m_sConfigFramerate.xEncodeFramerate = portDefn->format.video.xFramerate;
                m_sConfigBitrate.nEncodeBitrate = portDefn->format.video.nBitrate;
                m_sParamBitrate.nTargetBitrate = portDefn->format.video.nBitrate;
            }
            break;

        case OMX_IndexParamVideoPortFormat:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_VIDEO_PARAM_PORTFORMATTYPE);
                OMX_VIDEO_PARAM_PORTFORMATTYPE *portFmt =
                    (OMX_VIDEO_PARAM_PORTFORMATTYPE *)paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoPortFormat %d",
                        portFmt->eColorFormat);
                //set the driver with the corresponding values
                if (PORT_INDEX_IN == portFmt->nPortIndex) {
                    if (handle->venc_set_param(paramData,OMX_IndexParamVideoPortFormat) != true) {
                        return OMX_ErrorUnsupportedSetting;
                    }

                    DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoPortFormat %d",
                            portFmt->eColorFormat);
                    update_profile_level(); //framerate

#ifdef _ANDROID_ICS_
                    if (portFmt->eColorFormat ==
                            (OMX_COLOR_FORMATTYPE)QOMX_COLOR_FormatAndroidOpaque) {
                        m_sInPortFormat.eColorFormat = (OMX_COLOR_FORMATTYPE)
                            QOMX_DEFAULT_COLOR_FMT;
                        if (!mUseProxyColorFormat) {
                            if (!c2d_conv.init()) {
                                DEBUG_PRINT_ERROR("C2D init failed");
                                return OMX_ErrorUnsupportedSetting;
                            }
                            DEBUG_PRINT_HIGH("C2D init is successful");
                        }
                        mUseProxyColorFormat = true;
                        m_input_msg_id = OMX_COMPONENT_GENERATE_ETB_OPQ;
                    } else
#endif
                    {
                        m_sInPortFormat.eColorFormat = portFmt->eColorFormat;
                        m_sInPortDef.format.video.eColorFormat = portFmt->eColorFormat;
                        m_input_msg_id = OMX_COMPONENT_GENERATE_ETB;
                        mUseProxyColorFormat = false;
                    }
                    m_sInPortFormat.xFramerate = portFmt->xFramerate;
                }
                //TODO if no use case for O/P port,delet m_sOutPortFormat
            }
            break;
        case OMX_IndexParamVideoInit:
            { //TODO, do we need this index set param
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_PORT_PARAM_TYPE);
                OMX_PORT_PARAM_TYPE* pParam = (OMX_PORT_PARAM_TYPE*)(paramData);
                DEBUG_PRINT_LOW("Set OMX_IndexParamVideoInit called");
                break;
            }

        case OMX_IndexParamVideoBitrate:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_VIDEO_PARAM_BITRATETYPE);
                OMX_VIDEO_PARAM_BITRATETYPE* pParam = (OMX_VIDEO_PARAM_BITRATETYPE*)paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoBitrate");
                if (handle->venc_set_param(paramData,OMX_IndexParamVideoBitrate) != true) {
                    return OMX_ErrorUnsupportedSetting;
                }
                m_sParamBitrate.nTargetBitrate = pParam->nTargetBitrate;
                m_sParamBitrate.eControlRate = pParam->eControlRate;
                update_profile_level(); //bitrate
                m_sConfigBitrate.nEncodeBitrate = pParam->nTargetBitrate;
                m_sInPortDef.format.video.nBitrate = pParam->nTargetBitrate;
                m_sOutPortDef.format.video.nBitrate = pParam->nTargetBitrate;
                /* RC mode chan chage buffer requirements on Input port */
                dev_get_buf_req(&m_sInPortDef.nBufferCountMin,
                        &m_sInPortDef.nBufferCountActual,
                        &m_sInPortDef.nBufferSize,
                        m_sInPortDef.nPortIndex);
                DEBUG_PRINT_LOW("bitrate = %u", (unsigned int)m_sOutPortDef.format.video.nBitrate);
                break;
            }
        case OMX_IndexParamVideoMpeg4:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_VIDEO_PARAM_MPEG4TYPE);
                OMX_VIDEO_PARAM_MPEG4TYPE* pParam = (OMX_VIDEO_PARAM_MPEG4TYPE*)paramData;
                OMX_VIDEO_PARAM_MPEG4TYPE mp4_param;
                memcpy(&mp4_param, pParam, sizeof(struct OMX_VIDEO_PARAM_MPEG4TYPE));
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoMpeg4");
                if (pParam->eProfile == OMX_VIDEO_MPEG4ProfileAdvancedSimple) {
#ifdef _MSM8974_
                    if (pParam->nBFrames || bframes)
                        mp4_param.nBFrames = 1;
                    else
                        mp4_param.nBFrames = 0;
                    DEBUG_PRINT_HIGH("MPEG4: %u BFrames are being set", (unsigned int)mp4_param.nBFrames);
#endif
                } else {
                    if (pParam->nBFrames) {
                        DEBUG_PRINT_ERROR("Warning: B frames not supported");
                        mp4_param.nBFrames = 0;
                    }
                }
                if (handle->venc_set_param(&mp4_param,OMX_IndexParamVideoMpeg4) != true) {
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sParamMPEG4,pParam, sizeof(struct OMX_VIDEO_PARAM_MPEG4TYPE));
                m_sIntraperiod.nPFrames = m_sParamMPEG4.nPFrames;
                if (pParam->nBFrames || bframes)
                    m_sIntraperiod.nBFrames = m_sParamMPEG4.nBFrames = mp4_param.nBFrames;
                else
                    m_sIntraperiod.nBFrames = m_sParamMPEG4.nBFrames;
                break;
            }
        case OMX_IndexParamVideoH263:
            {
                OMX_VIDEO_PARAM_H263TYPE* pParam = (OMX_VIDEO_PARAM_H263TYPE*)paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoH263");
                if (handle->venc_set_param(paramData,OMX_IndexParamVideoH263) != true) {
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sParamH263,pParam, sizeof(struct OMX_VIDEO_PARAM_H263TYPE));
                m_sIntraperiod.nPFrames = m_sParamH263.nPFrames;
                m_sIntraperiod.nBFrames = m_sParamH263.nBFrames;
                break;
            }
        case OMX_IndexParamVideoAvc:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_VIDEO_PARAM_AVCTYPE);
                OMX_VIDEO_PARAM_AVCTYPE* pParam = (OMX_VIDEO_PARAM_AVCTYPE*)paramData;
                OMX_VIDEO_PARAM_AVCTYPE avc_param;
                memcpy(&avc_param, pParam, sizeof( struct OMX_VIDEO_PARAM_AVCTYPE));
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoAvc");

                avc_param.nBFrames = 0;
                if ((pParam->eProfile == OMX_VIDEO_AVCProfileHigh)||
                        (pParam->eProfile == OMX_VIDEO_AVCProfileMain)) {

                    if (pParam->nBFrames) {
                        avc_param.nBFrames = pParam->nBFrames;
                        DEBUG_PRINT_LOW("B frames set using Client setparam to %d",
                            avc_param.nBFrames);
                    }

                    if (bframes ) {
                        avc_param.nBFrames = bframes;
                        DEBUG_PRINT_LOW("B frames set using setprop to %d",
                            avc_param.nBFrames);
                    }

                    DEBUG_PRINT_HIGH("AVC: BFrames: %u", (unsigned int)avc_param.nBFrames);
                    avc_param.bEntropyCodingCABAC = (OMX_BOOL)(avc_param.bEntropyCodingCABAC && entropy);
                    avc_param.nCabacInitIdc = entropy ? avc_param.nCabacInitIdc : 0;
                } else {
                    if (pParam->nBFrames) {
                        DEBUG_PRINT_ERROR("Warning: B frames not supported");
                    }
                }

                if (handle->venc_set_param(&avc_param,OMX_IndexParamVideoAvc) != true) {
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sParamAVC,pParam, sizeof(struct OMX_VIDEO_PARAM_AVCTYPE));
                m_sIntraperiod.nPFrames = m_sParamAVC.nPFrames;
                if (pParam->nBFrames || bframes)
                    m_sIntraperiod.nBFrames = m_sParamAVC.nBFrames = avc_param.nBFrames;
                else
                    m_sIntraperiod.nBFrames = m_sParamAVC.nBFrames;
                break;
            }
        case (OMX_INDEXTYPE)OMX_IndexParamVideoVp8:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_VIDEO_PARAM_VP8TYPE);
                OMX_VIDEO_PARAM_VP8TYPE* pParam = (OMX_VIDEO_PARAM_VP8TYPE*)paramData;
                OMX_VIDEO_PARAM_VP8TYPE vp8_param;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoVp8");
                if (pParam->nDCTPartitions != m_sParamVP8.nDCTPartitions ||
                    pParam->bErrorResilientMode != m_sParamVP8.bErrorResilientMode) {
                    DEBUG_PRINT_ERROR("VP8 doesn't support nDCTPartitions or bErrorResilientMode");
                }
                memcpy(&vp8_param, pParam, sizeof( struct OMX_VIDEO_PARAM_VP8TYPE));
                if (handle->venc_set_param(&vp8_param, (OMX_INDEXTYPE)OMX_IndexParamVideoVp8) != true) {
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sParamVP8,pParam, sizeof(struct OMX_VIDEO_PARAM_VP8TYPE));
                break;
            }
        case (OMX_INDEXTYPE)OMX_IndexParamVideoHevc:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_VIDEO_PARAM_HEVCTYPE);
                OMX_VIDEO_PARAM_HEVCTYPE* pParam = (OMX_VIDEO_PARAM_HEVCTYPE*)paramData;
                OMX_VIDEO_PARAM_HEVCTYPE hevc_param;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoHevc");
                memcpy(&hevc_param, pParam, sizeof( struct OMX_VIDEO_PARAM_HEVCTYPE));
                if (handle->venc_set_param(&hevc_param, (OMX_INDEXTYPE)OMX_IndexParamVideoHevc) != true) {
                    DEBUG_PRINT_ERROR("Failed : set_parameter: OMX_IndexParamVideoHevc");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sParamHEVC, pParam, sizeof(struct OMX_VIDEO_PARAM_HEVCTYPE));
                break;
            }
        case OMX_IndexParamVideoProfileLevelCurrent:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_VIDEO_PARAM_PROFILELEVELTYPE);
                OMX_VIDEO_PARAM_PROFILELEVELTYPE* pParam = (OMX_VIDEO_PARAM_PROFILELEVELTYPE*)paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoProfileLevelCurrent");
                if (handle->venc_set_param(pParam,OMX_IndexParamVideoProfileLevelCurrent) != true) {
                    DEBUG_PRINT_ERROR("set_parameter: OMX_IndexParamVideoProfileLevelCurrent failed for Profile: %u "
                            "Level :%u", (unsigned int)pParam->eProfile, (unsigned int)pParam->eLevel);
                    return OMX_ErrorUnsupportedSetting;
                }
                m_sParamProfileLevel.eProfile = pParam->eProfile;
                m_sParamProfileLevel.eLevel = pParam->eLevel;

                if (!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.mpeg4",\
                            OMX_MAX_STRINGNAME_SIZE)) {
                    m_sParamMPEG4.eProfile = (OMX_VIDEO_MPEG4PROFILETYPE)m_sParamProfileLevel.eProfile;
                    m_sParamMPEG4.eLevel = (OMX_VIDEO_MPEG4LEVELTYPE)m_sParamProfileLevel.eLevel;
                    DEBUG_PRINT_LOW("MPEG4 profile = %d, level = %d", m_sParamMPEG4.eProfile,
                            m_sParamMPEG4.eLevel);
                } else if (!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.h263",\
                            OMX_MAX_STRINGNAME_SIZE)) {
                    m_sParamH263.eProfile = (OMX_VIDEO_H263PROFILETYPE)m_sParamProfileLevel.eProfile;
                    m_sParamH263.eLevel = (OMX_VIDEO_H263LEVELTYPE)m_sParamProfileLevel.eLevel;
                    DEBUG_PRINT_LOW("H263 profile = %d, level = %d", m_sParamH263.eProfile,
                            m_sParamH263.eLevel);
                } else if (!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.avc",\
                            OMX_MAX_STRINGNAME_SIZE)) {
                    m_sParamAVC.eProfile = (OMX_VIDEO_AVCPROFILETYPE)m_sParamProfileLevel.eProfile;
                    m_sParamAVC.eLevel = (OMX_VIDEO_AVCLEVELTYPE)m_sParamProfileLevel.eLevel;
                    DEBUG_PRINT_LOW("AVC profile = %d, level = %d", m_sParamAVC.eProfile,
                            m_sParamAVC.eLevel);
                } else if (!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.avc.secure",\
                            OMX_MAX_STRINGNAME_SIZE)) {
                    m_sParamAVC.eProfile = (OMX_VIDEO_AVCPROFILETYPE)m_sParamProfileLevel.eProfile;
                    m_sParamAVC.eLevel = (OMX_VIDEO_AVCLEVELTYPE)m_sParamProfileLevel.eLevel;
                    DEBUG_PRINT_LOW("\n AVC profile = %d, level = %d", m_sParamAVC.eProfile,
                            m_sParamAVC.eLevel);
                }
                else if (!strncmp((char*)m_nkind, "OMX.qcom.video.encoder.vp8",\
                            OMX_MAX_STRINGNAME_SIZE)) {
                    m_sParamVP8.eProfile = (OMX_VIDEO_VP8PROFILETYPE)m_sParamProfileLevel.eProfile;
                    m_sParamVP8.eLevel = (OMX_VIDEO_VP8LEVELTYPE)m_sParamProfileLevel.eLevel;
                    DEBUG_PRINT_LOW("VP8 profile = %d, level = %d", m_sParamVP8.eProfile,
                            m_sParamVP8.eLevel);
                }
                else if (!strncmp((char*)m_nkind, "OMX.qcom.video.encoder.hevc",\
                            OMX_MAX_STRINGNAME_SIZE)) {
                    m_sParamHEVC.eProfile = (OMX_VIDEO_HEVCPROFILETYPE)m_sParamProfileLevel.eProfile;
                    m_sParamHEVC.eLevel = (OMX_VIDEO_HEVCLEVELTYPE)m_sParamProfileLevel.eLevel;
                    DEBUG_PRINT_LOW("HEVC profile = %d, level = %d", m_sParamHEVC.eProfile,
                            m_sParamHEVC.eLevel);
                }

                break;
            }
        case OMX_IndexParamStandardComponentRole:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_PARAM_COMPONENTROLETYPE);
                OMX_PARAM_COMPONENTROLETYPE *comp_role;
                comp_role = (OMX_PARAM_COMPONENTROLETYPE *) paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamStandardComponentRole %s",
                        comp_role->cRole);

                if ((m_state == OMX_StateLoaded)&&
                        !BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING)) {
                    DEBUG_PRINT_LOW("Set Parameter called in valid state");
                } else {
                    DEBUG_PRINT_ERROR("Set Parameter called in Invalid State");
                    return OMX_ErrorIncorrectStateOperation;
                }

                if (!strncmp((char*)m_nkind, "OMX.qcom.video.encoder.avc",OMX_MAX_STRINGNAME_SIZE)) {
                    if (!strncmp((char*)comp_role->cRole,"video_encoder.avc",OMX_MAX_STRINGNAME_SIZE)) {
                        strlcpy((char*)m_cRole,"video_encoder.avc",OMX_MAX_STRINGNAME_SIZE);
                    } else {
                        DEBUG_PRINT_ERROR("ERROR: Setparameter: unknown Index %s", comp_role->cRole);
                        eRet =OMX_ErrorUnsupportedSetting;
                    }
                } else if (!strncmp((char*)m_nkind, "OMX.qcom.video.encoder.avc.secure",OMX_MAX_STRINGNAME_SIZE)) {
                    if (!strncmp((char*)comp_role->cRole,"video_encoder.avc",OMX_MAX_STRINGNAME_SIZE)) {
                        strlcpy((char*)m_cRole,"video_encoder.avc",OMX_MAX_STRINGNAME_SIZE);
                    } else {
                        DEBUG_PRINT_ERROR("ERROR: Setparameter: unknown Index %s\n", comp_role->cRole);
                        eRet =OMX_ErrorUnsupportedSetting;
                    }
                } else if (!strncmp((char*)m_nkind, "OMX.qcom.video.encoder.mpeg4",OMX_MAX_STRINGNAME_SIZE)) {
                    if (!strncmp((const char*)comp_role->cRole,"video_encoder.mpeg4",OMX_MAX_STRINGNAME_SIZE)) {
                        strlcpy((char*)m_cRole,"video_encoder.mpeg4",OMX_MAX_STRINGNAME_SIZE);
                    } else {
                        DEBUG_PRINT_ERROR("ERROR: Setparameter: unknown Index %s", comp_role->cRole);
                        eRet = OMX_ErrorUnsupportedSetting;
                    }
                } else if (!strncmp((char*)m_nkind, "OMX.qcom.video.encoder.h263",OMX_MAX_STRINGNAME_SIZE)) {
                    if (!strncmp((const char*)comp_role->cRole,"video_encoder.h263",OMX_MAX_STRINGNAME_SIZE)) {
                        strlcpy((char*)m_cRole,"video_encoder.h263",OMX_MAX_STRINGNAME_SIZE);
                    } else {
                        DEBUG_PRINT_ERROR("ERROR: Setparameter: unknown Index %s", comp_role->cRole);
                        eRet =OMX_ErrorUnsupportedSetting;
                    }
                }
#ifdef _MSM8974_
                else if (!strncmp((char*)m_nkind, "OMX.qcom.video.encoder.vp8",OMX_MAX_STRINGNAME_SIZE)) {
                    if (!strncmp((const char*)comp_role->cRole,"video_encoder.vp8",OMX_MAX_STRINGNAME_SIZE)) {
                        strlcpy((char*)m_cRole,"video_encoder.vp8",OMX_MAX_STRINGNAME_SIZE);
                    } else {
                        DEBUG_PRINT_ERROR("ERROR: Setparameter: unknown Index %s", comp_role->cRole);
                        eRet =OMX_ErrorUnsupportedSetting;
                    }
                }
#endif
                else if (!strncmp((char*)m_nkind, "OMX.qcom.video.encoder.hevc",OMX_MAX_STRINGNAME_SIZE)) {
                    if (!strncmp((const char*)comp_role->cRole,"video_encoder.hevc",OMX_MAX_STRINGNAME_SIZE)) {
                        strlcpy((char*)m_cRole,"video_encoder.hevc",OMX_MAX_STRINGNAME_SIZE);
                    } else {
                        DEBUG_PRINT_ERROR("ERROR: Setparameter: unknown Index %s", comp_role->cRole);
                        eRet = OMX_ErrorUnsupportedSetting;
                    }
                }

                else {
                    DEBUG_PRINT_ERROR("ERROR: Setparameter: unknown param %s", m_nkind);
                    eRet = OMX_ErrorInvalidComponentName;
                }
                break;
            }

        case OMX_IndexParamPriorityMgmt:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_PRIORITYMGMTTYPE);
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamPriorityMgmt");
                if (m_state != OMX_StateLoaded) {
                    DEBUG_PRINT_ERROR("ERROR: Set Parameter called in Invalid State");
                    return OMX_ErrorIncorrectStateOperation;
                }
                OMX_PRIORITYMGMTTYPE *priorityMgmtype = (OMX_PRIORITYMGMTTYPE*) paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamPriorityMgmt %u",
                        (unsigned int)priorityMgmtype->nGroupID);

                DEBUG_PRINT_LOW("set_parameter: priorityMgmtype %u",
                        (unsigned int)priorityMgmtype->nGroupPriority);

                m_sPriorityMgmt.nGroupID = priorityMgmtype->nGroupID;
                m_sPriorityMgmt.nGroupPriority = priorityMgmtype->nGroupPriority;

                break;
            }

        case OMX_IndexParamCompBufferSupplier:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_PARAM_BUFFERSUPPLIERTYPE);
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamCompBufferSupplier");
                OMX_PARAM_BUFFERSUPPLIERTYPE *bufferSupplierType = (OMX_PARAM_BUFFERSUPPLIERTYPE*) paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamCompBufferSupplier %d",
                        bufferSupplierType->eBufferSupplier);
                if (bufferSupplierType->nPortIndex == 0 || bufferSupplierType->nPortIndex ==1)
                    m_sInBufSupplier.eBufferSupplier = bufferSupplierType->eBufferSupplier;

                else

                    eRet = OMX_ErrorBadPortIndex;

                break;

            }
        case OMX_GoogleAndroidIndexAllocateNativeHandle:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, AllocateNativeHandleParams);

                AllocateNativeHandleParams* allocateNativeHandleParams = (AllocateNativeHandleParams *) paramData;

                if (!secure_session) {
                    DEBUG_PRINT_HIGH("Enable/Disable allocate-native-handle allowed only in secure session");
                    eRet = OMX_ErrorUnsupportedSetting;
                    break;
                } else if (allocateNativeHandleParams->nPortIndex != PORT_INDEX_OUT) {
                    DEBUG_PRINT_HIGH("Enable/Disable allocate-native-handle allowed only on Output port!");
                    eRet = OMX_ErrorUnsupportedSetting;
                    break;
                } else if (m_out_mem_ptr) {
                    DEBUG_PRINT_ERROR("Enable/Disable allocate-native-handle is not allowed since Output port is not free !");
                    eRet = OMX_ErrorInvalidState;
                    break;
                }

                if (allocateNativeHandleParams != NULL) {
                    allocate_native_handle = allocateNativeHandleParams->enable;
                }
                break;
            }
        case OMX_IndexParamVideoQuantization:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_VIDEO_PARAM_QUANTIZATIONTYPE);
                DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoQuantization");
                OMX_VIDEO_PARAM_QUANTIZATIONTYPE *session_qp = (OMX_VIDEO_PARAM_QUANTIZATIONTYPE*) paramData;
                if (session_qp->nPortIndex == PORT_INDEX_OUT) {
                    if (handle->venc_set_param(paramData, OMX_IndexParamVideoQuantization) != true) {
                        return OMX_ErrorUnsupportedSetting;
                    }
                    m_sSessionQuantization.nQpI = session_qp->nQpI;
                    m_sSessionQuantization.nQpP = session_qp->nQpP;
                    m_sSessionQuantization.nQpB = session_qp->nQpB;
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Unsupported port Index for Session QP setting");
                    eRet = OMX_ErrorBadPortIndex;
                }
                break;
            }

        case OMX_QcomIndexParamVideoQPRange:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_QCOM_VIDEO_PARAM_QPRANGETYPE);
                DEBUG_PRINT_LOW("set_parameter: OMX_QcomIndexParamVideoQPRange");
                OMX_QCOM_VIDEO_PARAM_QPRANGETYPE *qp_range = (OMX_QCOM_VIDEO_PARAM_QPRANGETYPE*) paramData;
                if (qp_range->nPortIndex == PORT_INDEX_OUT) {
                    if (handle->venc_set_param(paramData,
                                (OMX_INDEXTYPE)OMX_QcomIndexParamVideoQPRange) != true) {
                        return OMX_ErrorUnsupportedSetting;
                    }
                    m_sSessionQPRange.minQP= qp_range->minQP;
                    m_sSessionQPRange.maxQP= qp_range->maxQP;
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Unsupported port Index for QP range setting");
                    eRet = OMX_ErrorBadPortIndex;
                }
                break;
            }

        case OMX_QcomIndexParamVideoIPBQPRange:
            {
                DEBUG_PRINT_LOW("set_parameter: OMX_QCOM_VIDEO_PARAM_IPB_QPRANGETYPE");
                OMX_QCOM_VIDEO_PARAM_IPB_QPRANGETYPE *qp_range = (OMX_QCOM_VIDEO_PARAM_IPB_QPRANGETYPE*) paramData;
                if (qp_range->nPortIndex == PORT_INDEX_OUT) {
                    if (handle->venc_set_param(paramData,
                                (OMX_INDEXTYPE)OMX_QcomIndexParamVideoIPBQPRange) != true) {
                        return OMX_ErrorUnsupportedSetting;
                    }
                    m_sSessionIPBQPRange.minIQP = qp_range->minIQP;
                    m_sSessionIPBQPRange.maxIQP = qp_range->maxIQP;
                    m_sSessionIPBQPRange.minPQP = qp_range->minPQP;
                    m_sSessionIPBQPRange.maxPQP = qp_range->maxPQP;
                    m_sSessionIPBQPRange.minBQP = qp_range->minBQP;
                    m_sSessionIPBQPRange.maxBQP = qp_range->maxBQP;
                } else {
                    DEBUG_PRINT_ERROR("Unsupported port Index for IPB QP range setting");
                    eRet = OMX_ErrorBadPortIndex;
                }
                break;
            }

        case OMX_QcomIndexPortDefn:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_QCOM_PARAM_PORTDEFINITIONTYPE);
                OMX_QCOM_PARAM_PORTDEFINITIONTYPE* pParam =
                    (OMX_QCOM_PARAM_PORTDEFINITIONTYPE*)paramData;
                DEBUG_PRINT_LOW("set_parameter: OMX_QcomIndexPortDefn");
                if (pParam->nPortIndex == (OMX_U32)PORT_INDEX_IN) {
                    if (pParam->nMemRegion > OMX_QCOM_MemRegionInvalid &&
                            pParam->nMemRegion < OMX_QCOM_MemRegionMax) {
                        m_use_input_pmem = OMX_TRUE;
                    } else {
                        m_use_input_pmem = OMX_FALSE;
                    }
                } else if (pParam->nPortIndex == (OMX_U32)PORT_INDEX_OUT) {
                    if (pParam->nMemRegion > OMX_QCOM_MemRegionInvalid &&
                            pParam->nMemRegion < OMX_QCOM_MemRegionMax) {
                        m_use_output_pmem = OMX_TRUE;
                    } else {
                        m_use_output_pmem = OMX_FALSE;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: SetParameter called on unsupported Port Index for QcomPortDefn");
                    return OMX_ErrorBadPortIndex;
                }
                break;
            }

        case OMX_IndexParamVideoErrorCorrection:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE);
                DEBUG_PRINT_LOW("OMX_IndexParamVideoErrorCorrection");
                OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE* pParam =
                    (OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE*)paramData;
                if (!handle->venc_set_param(paramData, OMX_IndexParamVideoErrorCorrection)) {
                    DEBUG_PRINT_ERROR("ERROR: Request for setting Error Resilience failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sErrorCorrection,pParam, sizeof(m_sErrorCorrection));
                break;
            }
        case OMX_QcomIndexParamVideoSliceSpacing:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_VIDEO_PARAM_SLICE_SPACING_TYPE);
                DEBUG_PRINT_LOW("OMX_QcomIndexParamVideoSliceSpacing");
                QOMX_VIDEO_PARAM_SLICE_SPACING_TYPE* pParam =
                    (QOMX_VIDEO_PARAM_SLICE_SPACING_TYPE*)paramData;
                if (!handle->venc_set_param(paramData, (OMX_INDEXTYPE)OMX_QcomIndexParamVideoSliceSpacing)) {
                    DEBUG_PRINT_ERROR("ERROR: Request for setting slice spacing failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sSliceSpacing, pParam, sizeof(m_sSliceSpacing));
                break;
            }
        case OMX_IndexParamVideoIntraRefresh:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_VIDEO_PARAM_INTRAREFRESHTYPE);
                DEBUG_PRINT_LOW("set_param:OMX_IndexParamVideoIntraRefresh");
                OMX_VIDEO_PARAM_INTRAREFRESHTYPE* pParam =
                    (OMX_VIDEO_PARAM_INTRAREFRESHTYPE*)paramData;
                if (!handle->venc_set_param(paramData,OMX_IndexParamVideoIntraRefresh)) {
                    DEBUG_PRINT_ERROR("ERROR: Request for setting intra refresh failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sIntraRefresh, pParam, sizeof(m_sIntraRefresh));
                break;
            }
#ifdef _ANDROID_ICS_
        case OMX_QcomIndexParamVideoMetaBufferMode:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, StoreMetaDataInBuffersParams);
                StoreMetaDataInBuffersParams *pParam =
                    (StoreMetaDataInBuffersParams*)paramData;
                DEBUG_PRINT_HIGH("set_parameter:OMX_QcomIndexParamVideoMetaBufferMode: "
                        "port_index = %u, meta_mode = %d", (unsigned int)pParam->nPortIndex, pParam->bStoreMetaData);
                if (pParam->nPortIndex == PORT_INDEX_IN) {
                    if (pParam->bStoreMetaData != meta_mode_enable) {
                        if (!handle->venc_set_meta_mode(pParam->bStoreMetaData)) {
                            DEBUG_PRINT_ERROR("ERROR: set Metabuffer mode %d fail",
                                    pParam->bStoreMetaData);
                            return OMX_ErrorUnsupportedSetting;
                        }
                        meta_mode_enable = pParam->bStoreMetaData;
                        if (meta_mode_enable) {
                            m_sInPortDef.nBufferCountActual = m_sInPortDef.nBufferCountMin;
                            if (handle->venc_set_param(&m_sInPortDef,OMX_IndexParamPortDefinition) != true) {
                                DEBUG_PRINT_ERROR("ERROR: venc_set_param input failed");
                                return OMX_ErrorUnsupportedSetting;
                            }
                        } else {
                            /*TODO: reset encoder driver Meta mode*/
                            dev_get_buf_req   (&m_sOutPortDef.nBufferCountMin,
                                    &m_sOutPortDef.nBufferCountActual,
                                    &m_sOutPortDef.nBufferSize,
                                    m_sOutPortDef.nPortIndex);
                        }
                    }
                } else if (pParam->nPortIndex == PORT_INDEX_OUT && secure_session) {
                            DEBUG_PRINT_ERROR("set_parameter: metamode is "
                            "valid for input port only in secure session");
                            return OMX_ErrorUnsupportedSetting;
                } else {
                    DEBUG_PRINT_HIGH("set_parameter: metamode is "
                            "valid for input port only");
                    eRet = OMX_ErrorUnsupportedIndex;
                }
            }
            break;
#endif
#if !defined(MAX_RES_720P) || defined(_MSM8974_)
        case OMX_QcomIndexParamIndexExtraDataType:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_INDEXEXTRADATATYPE);
                DEBUG_PRINT_HIGH("set_parameter: OMX_QcomIndexParamIndexExtraDataType");
                QOMX_INDEXEXTRADATATYPE *pParam = (QOMX_INDEXEXTRADATATYPE *)paramData;
                bool enable = false;
                OMX_U32 mask = 0;

                if (pParam->nIndex == (OMX_INDEXTYPE)OMX_ExtraDataVideoEncoderSliceInfo) {
                    if (pParam->nPortIndex == PORT_INDEX_OUT) {
                        mask = VENC_EXTRADATA_SLICEINFO;

                        DEBUG_PRINT_HIGH("SliceInfo extradata %s",
                                ((pParam->bEnabled == OMX_TRUE) ? "enabled" : "disabled"));
                    } else {
                        DEBUG_PRINT_ERROR("set_parameter: Slice information is "
                                "valid for output port only");
                        eRet = OMX_ErrorUnsupportedIndex;
                        break;
                    }
                } else if (pParam->nIndex == (OMX_INDEXTYPE)OMX_ExtraDataVideoEncoderMBInfo) {
                    if (pParam->nPortIndex == PORT_INDEX_OUT) {
                        mask = VENC_EXTRADATA_MBINFO;

                        DEBUG_PRINT_HIGH("MBInfo extradata %s",
                                ((pParam->bEnabled == OMX_TRUE) ? "enabled" : "disabled"));
                    } else {
                        DEBUG_PRINT_ERROR("set_parameter: MB information is "
                                "valid for output port only");
                        eRet = OMX_ErrorUnsupportedIndex;
                        break;
                    }
                } else if (pParam->nIndex == (OMX_INDEXTYPE)OMX_ExtraDataFrameDimension) {
                    if (pParam->nPortIndex == PORT_INDEX_IN) {
                            mask = VENC_EXTRADATA_FRAMEDIMENSION;
                        DEBUG_PRINT_HIGH("Frame dimension extradata %s",
                                ((pParam->bEnabled == OMX_TRUE) ? "enabled" : "disabled"));
                    } else {
                        DEBUG_PRINT_ERROR("set_parameter: Frame Dimension is "
                                "valid for input port only");
                        eRet = OMX_ErrorUnsupportedIndex;
                        break;
                    }
                } else if (pParam->nIndex == (OMX_INDEXTYPE)OMX_QTIIndexParamVQZipSEIExtraData) {
                    if (pParam->nPortIndex == PORT_INDEX_IN) {
                        mask = VENC_EXTRADATA_VQZIP;
                        DEBUG_PRINT_HIGH("VQZIP extradata %s",
                                ((pParam->bEnabled == OMX_TRUE) ? "enabled" : "disabled"));
                    } else {
                        DEBUG_PRINT_ERROR("set_parameter: VQZIP is "
                                "valid for input port only");
                        eRet = OMX_ErrorUnsupportedIndex;
                        break;
                    }
                }
                else if (pParam->nIndex == (OMX_INDEXTYPE)OMX_ExtraDataVideoLTRInfo) {
                    if (pParam->nPortIndex == PORT_INDEX_OUT) {
                        if (pParam->bEnabled == OMX_TRUE)
#ifndef _TARGET_KERNEL_VERSION_49_
                            mask = VEN_EXTRADATA_LTRINFO;
#else
                            mask = VENC_EXTRADATA_LTRINFO;
#endif
                        DEBUG_PRINT_HIGH("LTRInfo extradata %s",
                                ((pParam->bEnabled == OMX_TRUE) ? "enabled" : "disabled"));
                    } else {
                        DEBUG_PRINT_ERROR("set_parameter: LTR information is "
                                "valid for output port only");
                        eRet = OMX_ErrorUnsupportedIndex;
                        break;
                    }
                }
                else {
                    DEBUG_PRINT_ERROR("set_parameter: unsupported extrdata index (%x)",
                            pParam->nIndex);
                    eRet = OMX_ErrorUnsupportedIndex;
                    break;
                }


                if (pParam->bEnabled == OMX_TRUE)
                    m_sExtraData |= mask;
                else
                    m_sExtraData &= ~mask;

                enable = !!(m_sExtraData & mask);
                if (handle->venc_set_param(&enable,
                            (OMX_INDEXTYPE)pParam->nIndex) != true) {
                    DEBUG_PRINT_ERROR("ERROR: Setting Extradata (%x) failed", pParam->nIndex);
                    return OMX_ErrorUnsupportedSetting;
                }

                if (pParam->nPortIndex == PORT_INDEX_IN) {
                    m_sInPortDef.nPortIndex = PORT_INDEX_IN;
                    dev_get_buf_req(&m_sInPortDef.nBufferCountMin,
                            &m_sInPortDef.nBufferCountActual,
                            &m_sInPortDef.nBufferSize,
                            m_sInPortDef.nPortIndex);
                    DEBUG_PRINT_HIGH("updated in_buf_req: buffer cnt=%u, "
                            "count min=%u, buffer size=%u",
                            (unsigned int)m_sOutPortDef.nBufferCountActual,
                            (unsigned int)m_sOutPortDef.nBufferCountMin,
                            (unsigned int)m_sOutPortDef.nBufferSize);

                } else {
                    m_sOutPortDef.nPortIndex = PORT_INDEX_OUT;
                    dev_get_buf_req(&m_sOutPortDef.nBufferCountMin,
                            &m_sOutPortDef.nBufferCountActual,
                            &m_sOutPortDef.nBufferSize,
                            m_sOutPortDef.nPortIndex);
                    DEBUG_PRINT_HIGH("updated out_buf_req: buffer cnt=%u, "
                            "count min=%u, buffer size=%u",
                            (unsigned int)m_sOutPortDef.nBufferCountActual,
                            (unsigned int)m_sOutPortDef.nBufferCountMin,
                            (unsigned int)m_sOutPortDef.nBufferSize);
                }
                break;
            }
        case QOMX_IndexParamVideoLTRMode:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_VIDEO_PARAM_LTRMODE_TYPE);
                QOMX_VIDEO_PARAM_LTRMODE_TYPE* pParam =
                    (QOMX_VIDEO_PARAM_LTRMODE_TYPE*)paramData;
                if (!handle->venc_set_param(paramData, (OMX_INDEXTYPE)QOMX_IndexParamVideoLTRMode)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting LTR mode failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sParamLTRMode, pParam, sizeof(m_sParamLTRMode));
                break;
            }
        case QOMX_IndexParamVideoLTRCount:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_VIDEO_PARAM_LTRCOUNT_TYPE);
                QOMX_VIDEO_PARAM_LTRCOUNT_TYPE* pParam =
                    (QOMX_VIDEO_PARAM_LTRCOUNT_TYPE*)paramData;
                if (!handle->venc_set_param(paramData, (OMX_INDEXTYPE)QOMX_IndexParamVideoLTRCount)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting LTR count failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sParamLTRCount, pParam, sizeof(m_sParamLTRCount));
                break;
            }
#endif
        case OMX_QcomIndexParamVideoMaxAllowedBitrateCheck:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_EXTNINDEX_PARAMTYPE);
                QOMX_EXTNINDEX_PARAMTYPE* pParam =
                    (QOMX_EXTNINDEX_PARAMTYPE*)paramData;
                if (pParam->nPortIndex == PORT_INDEX_OUT) {
                    handle->m_max_allowed_bitrate_check =
                        ((pParam->bEnable == OMX_TRUE) ? true : false);
                    DEBUG_PRINT_HIGH("set_parameter: max allowed bitrate check %s",
                            ((pParam->bEnable == OMX_TRUE) ? "enabled" : "disabled"));
                } else {
                    DEBUG_PRINT_ERROR("ERROR: OMX_QcomIndexParamVideoMaxAllowedBitrateCheck "
                            " called on wrong port(%u)", (unsigned int)pParam->nPortIndex);
                    return OMX_ErrorBadPortIndex;
                }
                break;
            }
#ifdef MAX_RES_1080P
        case OMX_QcomIndexEnableSliceDeliveryMode:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_EXTNINDEX_PARAMTYPE);
                QOMX_EXTNINDEX_PARAMTYPE* pParam =
                    (QOMX_EXTNINDEX_PARAMTYPE*)paramData;
                if (pParam->nPortIndex == PORT_INDEX_OUT) {
                    if (!handle->venc_set_param(paramData,
                                (OMX_INDEXTYPE)OMX_QcomIndexEnableSliceDeliveryMode)) {
                        DEBUG_PRINT_ERROR("ERROR: Request for setting slice delivery mode failed");
                        return OMX_ErrorUnsupportedSetting;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: OMX_QcomIndexEnableSliceDeliveryMode "
                            "called on wrong port(%u)", (unsigned int)pParam->nPortIndex);
                    return OMX_ErrorBadPortIndex;
                }
                break;
            }
#endif
        case OMX_QcomIndexEnableH263PlusPType:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_EXTNINDEX_PARAMTYPE);
                QOMX_EXTNINDEX_PARAMTYPE* pParam =
                    (QOMX_EXTNINDEX_PARAMTYPE*)paramData;
                DEBUG_PRINT_LOW("OMX_QcomIndexEnableH263PlusPType");
                if (pParam->nPortIndex == PORT_INDEX_OUT) {
                    if (!handle->venc_set_param(paramData,
                                (OMX_INDEXTYPE)OMX_QcomIndexEnableH263PlusPType)) {
                        DEBUG_PRINT_ERROR("ERROR: Request for setting PlusPType failed");
                        return OMX_ErrorUnsupportedSetting;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: OMX_QcomIndexEnableH263PlusPType "
                            "called on wrong port(%u)", (unsigned int)pParam->nPortIndex);
                    return OMX_ErrorBadPortIndex;
                }
                break;
            }
        case OMX_QcomIndexParamSequenceHeaderWithIDR:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, PrependSPSPPSToIDRFramesParams);
                if(!handle->venc_set_param(paramData,
                            (OMX_INDEXTYPE)OMX_QcomIndexParamSequenceHeaderWithIDR)) {
                    DEBUG_PRINT_ERROR("%s: %s",
                            "OMX_QComIndexParamSequenceHeaderWithIDR:",
                            "request for inband sps/pps failed.");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sPrependSPSPPS, paramData, sizeof(m_sPrependSPSPPS));
                break;
            }
        case OMX_QcomIndexParamAUDelimiter:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_QCOM_VIDEO_CONFIG_AUD);
                if(!handle->venc_set_param(paramData,
                            (OMX_INDEXTYPE)OMX_QcomIndexParamAUDelimiter)) {
                    DEBUG_PRINT_ERROR("%s: %s",
                            "OMX_QComIndexParamAUDelimiter:",
                            "request for AU Delimiters failed.");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_QcomIndexParamMBIStatisticsMode:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_QOMX_VIDEO_MBI_STATISTICS);
                if(!handle->venc_set_param(paramData,
                            (OMX_INDEXTYPE)OMX_QcomIndexParamMBIStatisticsMode)) {
                    DEBUG_PRINT_ERROR("%s: %s",
                            "OMX_QcomIndexParamMBIStatisticsMode:",
                            "MBI Statistics mode setting failed.");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
       case OMX_QcomIndexHierarchicalStructure:
           {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_VIDEO_HIERARCHICALLAYERS);
                QOMX_VIDEO_HIERARCHICALLAYERS* pParam =
                    (QOMX_VIDEO_HIERARCHICALLAYERS*)paramData;
                DEBUG_PRINT_LOW("OMX_QcomIndexHierarchicalStructure");
                if (pParam->nPortIndex == PORT_INDEX_OUT) {
                    if (!handle->venc_set_param(paramData,
                                (OMX_INDEXTYPE)OMX_QcomIndexHierarchicalStructure)) {
                        DEBUG_PRINT_ERROR("ERROR: Request for setting PlusPType failed");
                        return OMX_ErrorUnsupportedSetting;
                    }
                if((pParam->eHierarchicalCodingType == QOMX_HIERARCHICALCODING_B) && pParam->nNumLayers)
                    hier_b_enabled = true;
                    m_sHierLayers.nNumLayers = pParam->nNumLayers;
                    m_sHierLayers.eHierarchicalCodingType = pParam->eHierarchicalCodingType;
                } else {
                    DEBUG_PRINT_ERROR("ERROR: OMX_QcomIndexHierarchicalStructure called on wrong port(%u)",
                          (unsigned int)pParam->nPortIndex);
                    return OMX_ErrorBadPortIndex;
                }
                break;

           }
        case OMX_QcomIndexParamPerfLevel:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_QCOM_VIDEO_PARAM_PERF_LEVEL);
                if (!handle->venc_set_param(paramData,
                            (OMX_INDEXTYPE) OMX_QcomIndexParamPerfLevel)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting performance level");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_QcomIndexParamH264VUITimingInfo:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_QCOM_VIDEO_PARAM_VUI_TIMING_INFO);
                if (!handle->venc_set_param(paramData,
                            (OMX_INDEXTYPE) OMX_QcomIndexParamH264VUITimingInfo)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting VUI timing info");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_QcomIndexParamPeakBitrate:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_QCOM_VIDEO_PARAM_PEAK_BITRATE);
                if (!handle->venc_set_param(paramData,
                            (OMX_INDEXTYPE) OMX_QcomIndexParamPeakBitrate)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting peak bitrate");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
             }
       case QOMX_IndexParamVideoInitialQp:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_EXTNINDEX_VIDEO_INITIALQP);
                if(!handle->venc_set_param(paramData,
                            (OMX_INDEXTYPE)QOMX_IndexParamVideoInitialQp)) {
                    DEBUG_PRINT_ERROR("Request to Enable initial QP failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sParamInitqp, paramData, sizeof(m_sParamInitqp));
                break;
            }
        case OMX_QcomIndexParamSetMVSearchrange:
            {
                if (!handle->venc_set_param(paramData,
                            (OMX_INDEXTYPE) OMX_QcomIndexParamSetMVSearchrange)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting Searchrange");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_QcomIndexParamVideoHybridHierpMode:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_EXTNINDEX_VIDEO_HYBRID_HP_MODE);
               if(!handle->venc_set_param(paramData,
                         (OMX_INDEXTYPE)OMX_QcomIndexParamVideoHybridHierpMode)) {
                   DEBUG_PRINT_ERROR("Request to Enable Hybrid Hier-P failed");
                   return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_QcomIndexParamBatchSize:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_PARAM_U32TYPE);
                if(!handle->venc_set_param(paramData,
                         (OMX_INDEXTYPE)OMX_QcomIndexParamBatchSize)) {
                   DEBUG_PRINT_ERROR("Attempting to set batch size failed");
                   return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_QcomIndexConfigH264EntropyCodingCabac:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_VIDEO_H264ENTROPYCODINGTYPE);
                if(!handle->venc_set_param(paramData,
                         (OMX_INDEXTYPE)OMX_QcomIndexConfigH264EntropyCodingCabac)) {
                   DEBUG_PRINT_ERROR("Attempting to set Entropy failed");
                   return OMX_ErrorUnsupportedSetting;
                }
               break;
            }
        case OMX_QTIIndexParamVQZIPSEIType:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_QTI_VIDEO_PARAM_VQZIP_SEI_TYPE);
                if (!handle->venc_set_param(paramData,
                            (OMX_INDEXTYPE) OMX_QTIIndexParamVQZIPSEIType)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting VQZIP SEI type");
                    return OMX_ErrorUnsupportedSetting;
                }
                m_sExtraData |= VENC_EXTRADATA_VQZIP;
                break;
            }
        case OMX_QcomIndexParamVencAspectRatio:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_EXTNINDEX_VIDEO_VENC_SAR);
                if (!handle->venc_set_param(paramData,
                        (OMX_INDEXTYPE)OMX_QcomIndexParamVencAspectRatio)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting OMX_QcomIndexParamVencAspectRatio failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sSar, paramData, sizeof(m_sSar));
                break;
            }
        case OMX_QTIIndexParamLowLatencyMode:
            {
                if (!handle->venc_set_param(paramData,
                        (OMX_INDEXTYPE)OMX_QTIIndexParamLowLatencyMode)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting OMX_QTIIndexParamLowLatencyMode failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_slowLatencyMode, paramData, sizeof(m_slowLatencyMode));
                break;
            }
        case OMX_QcomIndexConfigVideoVencLowLatencyMode:
            {
                if(!handle->venc_set_param(paramData,
                            (OMX_INDEXTYPE)OMX_QcomIndexConfigVideoVencLowLatencyMode)) {
                    DEBUG_PRINT_ERROR("Request to Enable Low latency mode failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_QTIIndexParamVideoEnableRoiInfo:
            {
                if (!handle->venc_set_param(paramData,
                            (OMX_INDEXTYPE)OMX_QTIIndexParamVideoEnableRoiInfo)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting OMX_QTIIndexParamVideoEnableRoiInfo failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                m_sExtraData |= VENC_EXTRADATA_ROI;
                break;
            }
        case OMX_IndexParamAndroidVideoTemporalLayering:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, OMX_VIDEO_PARAM_ANDROID_TEMPORALLAYERINGTYPE);
                if (!handle->venc_set_param(paramData,
                        (OMX_INDEXTYPE)OMX_IndexParamAndroidVideoTemporalLayering)) {
                    DEBUG_PRINT_ERROR("Failed to configure temporal layers");
                    return OMX_ErrorUnsupportedSetting;
                }
                // save the actual configuration applied
                memcpy(&m_sParamTemporalLayers, paramData, sizeof(m_sParamTemporalLayers));
                // keep the config data in sync
                m_sConfigTemporalLayers.ePattern = m_sParamTemporalLayers.ePattern;
                m_sConfigTemporalLayers.nBLayerCountActual = m_sParamTemporalLayers.nBLayerCountActual;
                m_sConfigTemporalLayers.nPLayerCountActual = m_sParamTemporalLayers.nPLayerCountActual;
                m_sConfigTemporalLayers.bBitrateRatiosSpecified = m_sParamTemporalLayers.bBitrateRatiosSpecified;
                memcpy(&m_sConfigTemporalLayers.nBitrateRatios[0],
                        &m_sParamTemporalLayers.nBitrateRatios[0],
                        OMX_VIDEO_ANDROID_MAXTEMPORALLAYERS * sizeof(OMX_U32));
                break;
            }
        case OMX_QTIIndexParamDisablePQ:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_DISABLETYPE);
                handle->venc_set_param(paramData,
                        (OMX_INDEXTYPE)OMX_QTIIndexParamDisablePQ);
                break;
            }
        case OMX_QTIIndexParamIframeSizeType:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_VIDEO_IFRAMESIZE);
                if (!handle->venc_set_param(paramData,
                            (OMX_INDEXTYPE)OMX_QTIIndexParamIframeSizeType)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting OMX_QTIIndexParamIframeSizeType failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_QTIIndexParamEnableAVTimerTimestamps:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_ENABLETYPE);
                if (!handle->venc_set_param(paramData,
                            (OMX_INDEXTYPE)OMX_QTIIndexParamEnableAVTimerTimestamps)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting OMX_QTIIndexParamEnableAVTimerTimestamps failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sParamAVTimerTimestampMode, paramData, sizeof(QOMX_ENABLETYPE));
                break;
            }
        case OMX_QcomIndexParamVencControlInputQueue:
            {
                VALIDATE_OMX_PARAM_DATA(paramData, QOMX_ENABLETYPE);
                memcpy(&m_sParamControlInputQueue, paramData, sizeof(QOMX_ENABLETYPE));
                break;
            }
        default:
            {
                DEBUG_PRINT_ERROR("ERROR: Setparameter: unknown param %d", paramIndex);
                eRet = OMX_ErrorUnsupportedIndex;
                break;
            }
    }
    return eRet;
}

bool omx_venc::update_profile_level()
{
    OMX_U32 eProfile, eLevel;

    if (!handle->venc_get_profile_level(&eProfile,&eLevel)) {
        DEBUG_PRINT_ERROR("Failed to update the profile_level");
        return false;
    }

    m_sParamProfileLevel.eProfile = (OMX_VIDEO_MPEG4PROFILETYPE)eProfile;
    m_sParamProfileLevel.eLevel = (OMX_VIDEO_MPEG4LEVELTYPE)eLevel;

    if (!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.mpeg4",\
                OMX_MAX_STRINGNAME_SIZE)) {
        m_sParamMPEG4.eProfile = (OMX_VIDEO_MPEG4PROFILETYPE)eProfile;
        m_sParamMPEG4.eLevel = (OMX_VIDEO_MPEG4LEVELTYPE)eLevel;
        DEBUG_PRINT_LOW("MPEG4 profile = %d, level = %d", m_sParamMPEG4.eProfile,
                m_sParamMPEG4.eLevel);
    } else if (!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.h263",\
                OMX_MAX_STRINGNAME_SIZE)) {
        m_sParamH263.eProfile = (OMX_VIDEO_H263PROFILETYPE)eProfile;
        m_sParamH263.eLevel = (OMX_VIDEO_H263LEVELTYPE)eLevel;
        DEBUG_PRINT_LOW("H263 profile = %d, level = %d", m_sParamH263.eProfile,
                m_sParamH263.eLevel);
    } else if (!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.avc",\
                OMX_MAX_STRINGNAME_SIZE)) {
        m_sParamAVC.eProfile = (OMX_VIDEO_AVCPROFILETYPE)eProfile;
        m_sParamAVC.eLevel = (OMX_VIDEO_AVCLEVELTYPE)eLevel;
        DEBUG_PRINT_LOW("AVC profile = %d, level = %d", m_sParamAVC.eProfile,
                m_sParamAVC.eLevel);
    } else if (!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.avc.secure",\
                OMX_MAX_STRINGNAME_SIZE)) {
        m_sParamAVC.eProfile = (OMX_VIDEO_AVCPROFILETYPE)eProfile;
        m_sParamAVC.eLevel = (OMX_VIDEO_AVCLEVELTYPE)eLevel;
        DEBUG_PRINT_LOW("\n AVC profile = %d, level = %d", m_sParamAVC.eProfile,
                m_sParamAVC.eLevel);
    }
    else if (!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.vp8",\
                OMX_MAX_STRINGNAME_SIZE)) {
        m_sParamVP8.eProfile = (OMX_VIDEO_VP8PROFILETYPE)eProfile;
        m_sParamVP8.eLevel = (OMX_VIDEO_VP8LEVELTYPE)eLevel;
        DEBUG_PRINT_LOW("VP8 profile = %d, level = %d", m_sParamVP8.eProfile,
                m_sParamVP8.eLevel);
    }
    else if (!strncmp((char *)m_nkind, "OMX.qcom.video.encoder.hevc",\
                OMX_MAX_STRINGNAME_SIZE)) {
        m_sParamHEVC.eProfile = (OMX_VIDEO_HEVCPROFILETYPE)eProfile;
        m_sParamHEVC.eLevel = (OMX_VIDEO_HEVCLEVELTYPE)eLevel;
        DEBUG_PRINT_LOW("HEVC profile = %d, level = %d", m_sParamHEVC.eProfile,
                m_sParamHEVC.eLevel);
    }

    return true;
}
/* ======================================================================
   FUNCTION
   omx_video::SetConfig

   DESCRIPTION
   OMX Set Config method implementation

   PARAMETERS
   <TBD>.

   RETURN VALUE
   OMX Error None if successful.
   ========================================================================== */
OMX_ERRORTYPE  omx_venc::set_config(OMX_IN OMX_HANDLETYPE      hComp,
        OMX_IN OMX_INDEXTYPE configIndex,
        OMX_IN OMX_PTR        configData)
{
    (void)hComp;
    if (configData == NULL) {
        DEBUG_PRINT_ERROR("ERROR: param is null");
        return OMX_ErrorBadParameter;
    }

    if (m_state == OMX_StateInvalid) {
        DEBUG_PRINT_ERROR("ERROR: config called in Invalid state");
        return OMX_ErrorIncorrectStateOperation;
    }

    // params will be validated prior to venc_init
    switch ((int)configIndex) {
        case OMX_IndexConfigVideoBitrate:
            {
                VALIDATE_OMX_PARAM_DATA(configData, OMX_VIDEO_CONFIG_BITRATETYPE);
                OMX_VIDEO_CONFIG_BITRATETYPE* pParam =
                    reinterpret_cast<OMX_VIDEO_CONFIG_BITRATETYPE*>(configData);
                DEBUG_PRINT_HIGH("set_config(): OMX_IndexConfigVideoBitrate (%u)", (unsigned int)pParam->nEncodeBitrate);

                if (pParam->nPortIndex == PORT_INDEX_OUT) {
                    if (handle->venc_set_config(configData, OMX_IndexConfigVideoBitrate) != true) {
                        DEBUG_PRINT_ERROR("ERROR: Setting OMX_IndexConfigVideoBitrate failed");
                        return OMX_ErrorUnsupportedSetting;
                    }

                    m_sConfigBitrate.nEncodeBitrate = pParam->nEncodeBitrate;
                    m_sParamBitrate.nTargetBitrate = pParam->nEncodeBitrate;
                    m_sOutPortDef.format.video.nBitrate = pParam->nEncodeBitrate;
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Unsupported port index: %u", (unsigned int)pParam->nPortIndex);
                    return OMX_ErrorBadPortIndex;
                }
                break;
            }
        case OMX_IndexConfigVideoFramerate:
            {
                VALIDATE_OMX_PARAM_DATA(configData, OMX_CONFIG_FRAMERATETYPE);
                OMX_CONFIG_FRAMERATETYPE* pParam =
                    reinterpret_cast<OMX_CONFIG_FRAMERATETYPE*>(configData);
                DEBUG_PRINT_HIGH("set_config(): OMX_IndexConfigVideoFramerate (0x%x)", (unsigned int)pParam->xEncodeFramerate);

                if (pParam->nPortIndex == PORT_INDEX_OUT) {
                    if (handle->venc_set_config(configData, OMX_IndexConfigVideoFramerate) != true) {
                        DEBUG_PRINT_ERROR("ERROR: Setting OMX_IndexConfigVideoFramerate failed");
                        return OMX_ErrorUnsupportedSetting;
                    }

                    m_sConfigFramerate.xEncodeFramerate = pParam->xEncodeFramerate;
                    m_sOutPortDef.format.video.xFramerate = pParam->xEncodeFramerate;
                    m_sOutPortFormat.xFramerate = pParam->xEncodeFramerate;
                    /*
                     * Frame rate can change buffer requirements. If query is not allowed,
                     * failure is not FATAL here.
                     */
                    dev_get_buf_req(&m_sInPortDef.nBufferCountMin,
                            &m_sInPortDef.nBufferCountActual,
                            &m_sInPortDef.nBufferSize,
                            m_sInPortDef.nPortIndex);
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Unsupported port index: %u", (unsigned int)pParam->nPortIndex);
                    return OMX_ErrorBadPortIndex;
                }

                break;
            }
        case QOMX_IndexConfigVideoIntraperiod:
            {
                VALIDATE_OMX_PARAM_DATA(configData, QOMX_VIDEO_INTRAPERIODTYPE);
                QOMX_VIDEO_INTRAPERIODTYPE* pParam =
                    reinterpret_cast<QOMX_VIDEO_INTRAPERIODTYPE*>(configData);

                DEBUG_PRINT_HIGH("set_config(): QOMX_IndexConfigVideoIntraperiod");
                if (pParam->nPortIndex == PORT_INDEX_OUT) {
#ifdef MAX_RES_720P
                    if (pParam->nBFrames > 0) {
                        DEBUG_PRINT_ERROR("B frames not supported");
                        return OMX_ErrorUnsupportedSetting;
                    }
#endif
                    DEBUG_PRINT_HIGH("Old: P/B frames = %u/%u, New: P/B frames = %u/%u",
                            (unsigned int)m_sIntraperiod.nPFrames, (unsigned int)m_sIntraperiod.nBFrames,
                            (unsigned int)pParam->nPFrames, (unsigned int)pParam->nBFrames);
                    if (m_sIntraperiod.nBFrames != pParam->nBFrames) {
                        if(hier_b_enabled && m_state == OMX_StateLoaded) {
                            DEBUG_PRINT_INFO("B-frames setting is supported if HierB is enabled");
                        }
                    }
                    if (handle->venc_set_config(configData, (OMX_INDEXTYPE) QOMX_IndexConfigVideoIntraperiod) != true) {
                        DEBUG_PRINT_ERROR("ERROR: Setting QOMX_IndexConfigVideoIntraperiod failed");
                        return OMX_ErrorUnsupportedSetting;
                    }
                    m_sIntraperiod.nPFrames = pParam->nPFrames;
                    m_sIntraperiod.nBFrames = pParam->nBFrames;
                    m_sIntraperiod.nIDRPeriod = pParam->nIDRPeriod;

                    if (m_sOutPortFormat.eCompressionFormat == OMX_VIDEO_CodingMPEG4) {
                        m_sParamMPEG4.nPFrames = pParam->nPFrames;
                        if (m_sParamMPEG4.eProfile != OMX_VIDEO_MPEG4ProfileSimple)
                            m_sParamMPEG4.nBFrames = pParam->nBFrames;
                        else
                            m_sParamMPEG4.nBFrames = 0;
                    } else if (m_sOutPortFormat.eCompressionFormat == OMX_VIDEO_CodingH263) {
                        m_sParamH263.nPFrames = pParam->nPFrames;
                    } else {
                        m_sParamAVC.nPFrames = pParam->nPFrames;
                        if ((m_sParamAVC.eProfile != OMX_VIDEO_AVCProfileBaseline) &&
                            (m_sParamAVC.eProfile != (OMX_VIDEO_AVCPROFILETYPE) OMX_VIDEO_AVCProfileConstrainedBaseline) &&
                            (m_sParamAVC.eProfile != (OMX_VIDEO_AVCPROFILETYPE) QOMX_VIDEO_AVCProfileConstrainedBaseline))
                            m_sParamAVC.nBFrames = pParam->nBFrames;
                        else
                            m_sParamAVC.nBFrames = 0;
                    }
                } else {
                    DEBUG_PRINT_ERROR("ERROR: (QOMX_IndexConfigVideoIntraperiod) Unsupported port index: %u", (unsigned int)pParam->nPortIndex);
                    return OMX_ErrorBadPortIndex;
                }

                break;
            }

        case OMX_IndexConfigVideoIntraVOPRefresh:
            {
                VALIDATE_OMX_PARAM_DATA(configData, OMX_CONFIG_INTRAREFRESHVOPTYPE);
                OMX_CONFIG_INTRAREFRESHVOPTYPE* pParam =
                    reinterpret_cast<OMX_CONFIG_INTRAREFRESHVOPTYPE*>(configData);

                DEBUG_PRINT_HIGH("set_config(): OMX_IndexConfigVideoIntraVOPRefresh");
                if (pParam->nPortIndex == PORT_INDEX_OUT) {
                    if (handle->venc_set_config(configData,
                                OMX_IndexConfigVideoIntraVOPRefresh) != true) {
                        DEBUG_PRINT_ERROR("ERROR: Setting OMX_IndexConfigVideoIntraVOPRefresh failed");
                        return OMX_ErrorUnsupportedSetting;
                    }

                    m_sConfigIntraRefreshVOP.IntraRefreshVOP = pParam->IntraRefreshVOP;
                } else {
                    DEBUG_PRINT_ERROR("ERROR: Unsupported port index: %u", (unsigned int)pParam->nPortIndex);
                    return OMX_ErrorBadPortIndex;
                }

                break;
            }
        case OMX_IndexConfigCommonRotate:
            {
                VALIDATE_OMX_PARAM_DATA(configData, OMX_CONFIG_ROTATIONTYPE);
                OMX_CONFIG_ROTATIONTYPE *pParam =
                    reinterpret_cast<OMX_CONFIG_ROTATIONTYPE*>(configData);
                OMX_S32 nRotation;

                if (pParam->nPortIndex != PORT_INDEX_OUT) {
                    DEBUG_PRINT_ERROR("ERROR: Unsupported port index: %u", (unsigned int)pParam->nPortIndex);
                    return OMX_ErrorBadPortIndex;
                }
                if ( pParam->nRotation == 0   ||
                        pParam->nRotation == 90  ||
                        pParam->nRotation == 180 ||
                        pParam->nRotation == 270 ) {
                    DEBUG_PRINT_HIGH("set_config: Rotation Angle %u", (unsigned int)pParam->nRotation);
                } else {
                    DEBUG_PRINT_ERROR("ERROR: un supported Rotation %u", (unsigned int)pParam->nRotation);
                    return OMX_ErrorUnsupportedSetting;
                }
                nRotation = pParam->nRotation - m_sConfigFrameRotation.nRotation;
                if (nRotation < 0)
                    nRotation = -nRotation;

                DEBUG_PRINT_HIGH("set_config: updating device Dims");

                if (handle->venc_set_config(configData,
                    OMX_IndexConfigCommonRotate) != true) {
                        DEBUG_PRINT_ERROR("ERROR: Set OMX_IndexConfigCommonRotate failed");
                        return OMX_ErrorUnsupportedSetting;
                }
                m_sConfigFrameRotation.nRotation = pParam->nRotation;

                // Update output-port resolution (since it might have been flipped by rotation)
                if (handle->venc_get_dimensions(PORT_INDEX_OUT,
                        &m_sOutPortDef.format.video.nFrameWidth,
                        &m_sOutPortDef.format.video.nFrameHeight)) {
                    DEBUG_PRINT_HIGH("set Rotation: updated dimensions = %u x %u",
                            m_sOutPortDef.format.video.nFrameWidth,
                            m_sOutPortDef.format.video.nFrameHeight);
                }
                break;
            }
        case OMX_QcomIndexConfigVideoFramePackingArrangement:
            {
                DEBUG_PRINT_HIGH("set_config(): OMX_QcomIndexConfigVideoFramePackingArrangement");
                if (m_sOutPortFormat.eCompressionFormat == OMX_VIDEO_CodingAVC) {
                    VALIDATE_OMX_PARAM_DATA(configData, OMX_QCOM_FRAME_PACK_ARRANGEMENT);
                    OMX_QCOM_FRAME_PACK_ARRANGEMENT *configFmt =
                        (OMX_QCOM_FRAME_PACK_ARRANGEMENT *) configData;
                    extra_data_handle.set_frame_pack_data(configFmt);
                } else {
                    DEBUG_PRINT_ERROR("ERROR: FramePackingData not supported for non AVC compression");
                }
                break;
            }
        case QOMX_IndexConfigVideoLTRPeriod:
            {
                VALIDATE_OMX_PARAM_DATA(configData, QOMX_VIDEO_CONFIG_LTRPERIOD_TYPE);
                QOMX_VIDEO_CONFIG_LTRPERIOD_TYPE* pParam = (QOMX_VIDEO_CONFIG_LTRPERIOD_TYPE*)configData;
                if (!handle->venc_set_config(configData, (OMX_INDEXTYPE)QOMX_IndexConfigVideoLTRPeriod)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting LTR period failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sConfigLTRPeriod, pParam, sizeof(m_sConfigLTRPeriod));
                break;
            }

       case OMX_IndexConfigVideoVp8ReferenceFrame:
           {
                VALIDATE_OMX_PARAM_DATA(configData, OMX_VIDEO_VP8REFERENCEFRAMETYPE);
               OMX_VIDEO_VP8REFERENCEFRAMETYPE* pParam = (OMX_VIDEO_VP8REFERENCEFRAMETYPE*) configData;
               if (!handle->venc_set_config(pParam, (OMX_INDEXTYPE) OMX_IndexConfigVideoVp8ReferenceFrame)) {
                   DEBUG_PRINT_ERROR("ERROR: Setting VP8 reference frame");
                   return OMX_ErrorUnsupportedSetting;
               }
               memcpy(&m_sConfigVp8ReferenceFrame, pParam, sizeof(m_sConfigVp8ReferenceFrame));
               break;
           }

       case QOMX_IndexConfigVideoLTRUse:
            {
                VALIDATE_OMX_PARAM_DATA(configData, QOMX_VIDEO_CONFIG_LTRUSE_TYPE);
                QOMX_VIDEO_CONFIG_LTRUSE_TYPE* pParam = (QOMX_VIDEO_CONFIG_LTRUSE_TYPE*)configData;
                if (!handle->venc_set_config(pParam, (OMX_INDEXTYPE)QOMX_IndexConfigVideoLTRUse)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting LTR use failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sConfigLTRUse, pParam, sizeof(m_sConfigLTRUse));
                break;
            }
        case QOMX_IndexConfigVideoLTRMark:
            {
                VALIDATE_OMX_PARAM_DATA(configData, QOMX_VIDEO_CONFIG_LTRMARK_TYPE);
                QOMX_VIDEO_CONFIG_LTRMARK_TYPE* pParam = (QOMX_VIDEO_CONFIG_LTRMARK_TYPE*)configData;
                if (!handle->venc_set_config(pParam, (OMX_INDEXTYPE)QOMX_IndexConfigVideoLTRMark)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting LTR mark failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_IndexConfigVideoAVCIntraPeriod:
            {
                VALIDATE_OMX_PARAM_DATA(configData, OMX_VIDEO_CONFIG_AVCINTRAPERIOD);
                OMX_VIDEO_CONFIG_AVCINTRAPERIOD *pParam = (OMX_VIDEO_CONFIG_AVCINTRAPERIOD*) configData;
                DEBUG_PRINT_LOW("set_config: OMX_IndexConfigVideoAVCIntraPeriod");
                if (!handle->venc_set_config(pParam, (OMX_INDEXTYPE)OMX_IndexConfigVideoAVCIntraPeriod)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting OMX_IndexConfigVideoAVCIntraPeriod failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sConfigAVCIDRPeriod, pParam, sizeof(m_sConfigAVCIDRPeriod));
                break;
            }
        case OMX_IndexConfigCommonDeinterlace:
            {
                VALIDATE_OMX_PARAM_DATA(configData, OMX_VIDEO_CONFIG_DEINTERLACE);
                OMX_VIDEO_CONFIG_DEINTERLACE *pParam = (OMX_VIDEO_CONFIG_DEINTERLACE*) configData;
                DEBUG_PRINT_LOW("set_config: OMX_IndexConfigCommonDeinterlace");
                if (!handle->venc_set_config(pParam, (OMX_INDEXTYPE)OMX_IndexConfigCommonDeinterlace)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting OMX_IndexConfigCommonDeinterlace failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_sConfigDeinterlace, pParam, sizeof(m_sConfigDeinterlace));
                break;
            }
        case OMX_QcomIndexConfigVideoVencPerfMode:
            {
                VALIDATE_OMX_PARAM_DATA(configData, QOMX_EXTNINDEX_VIDEO_PERFMODE);
                QOMX_EXTNINDEX_VIDEO_PERFMODE* pParam = (QOMX_EXTNINDEX_VIDEO_PERFMODE*)configData;
                if (!handle->venc_set_config(pParam, (OMX_INDEXTYPE)OMX_QcomIndexConfigVideoVencPerfMode)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting OMX_QcomIndexConfigVideoVencPerfMode failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_QcomIndexConfigNumHierPLayers:
        {
            VALIDATE_OMX_PARAM_DATA(configData, QOMX_EXTNINDEX_VIDEO_HIER_P_LAYERS);
            QOMX_EXTNINDEX_VIDEO_HIER_P_LAYERS* pParam =
                (QOMX_EXTNINDEX_VIDEO_HIER_P_LAYERS*)configData;
            if (!handle->venc_set_config(pParam, (OMX_INDEXTYPE)OMX_QcomIndexConfigNumHierPLayers)) {
                DEBUG_PRINT_ERROR("ERROR: Setting OMX_QcomIndexConfigNumHierPLayers failed");
                return OMX_ErrorUnsupportedSetting;
            }
            memcpy(&m_sHPlayers, pParam, sizeof(m_sHPlayers));
            break;
        }
        case OMX_QcomIndexConfigBaseLayerId:
        {
            VALIDATE_OMX_PARAM_DATA(configData, OMX_SKYPE_VIDEO_CONFIG_BASELAYERPID);
            OMX_SKYPE_VIDEO_CONFIG_BASELAYERPID* pParam =
                (OMX_SKYPE_VIDEO_CONFIG_BASELAYERPID*) configData;
            if (!handle->venc_set_config(pParam, (OMX_INDEXTYPE)OMX_QcomIndexConfigBaseLayerId)) {
                DEBUG_PRINT_ERROR("ERROR: Setting OMX_QcomIndexConfigBaseLayerId failed");
                return OMX_ErrorUnsupportedSetting;
            }
            memcpy(&m_sBaseLayerID, pParam, sizeof(m_sBaseLayerID));
            break;
        }
        case OMX_QcomIndexConfigQp:
        {
            VALIDATE_OMX_PARAM_DATA(configData, OMX_SKYPE_VIDEO_CONFIG_QP);
            OMX_SKYPE_VIDEO_CONFIG_QP* pParam =
                (OMX_SKYPE_VIDEO_CONFIG_QP*) configData;
            if (!handle->venc_set_config(pParam, (OMX_INDEXTYPE)OMX_QcomIndexConfigQp)) {
                DEBUG_PRINT_ERROR("ERROR: Setting OMX_QcomIndexConfigQp failed");
                return OMX_ErrorUnsupportedSetting;
            }
            memcpy(&m_sConfigQP, pParam, sizeof(m_sConfigQP));
            break;
        }
        case OMX_IndexConfigPriority:
            {
                VALIDATE_OMX_PARAM_DATA(configData, OMX_PARAM_U32TYPE);
                if (!handle->venc_set_config(configData, (OMX_INDEXTYPE)OMX_IndexConfigPriority)) {
                    DEBUG_PRINT_ERROR("Failed to set OMX_IndexConfigPriority");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_IndexConfigOperatingRate:
            {
                VALIDATE_OMX_PARAM_DATA(configData, OMX_PARAM_U32TYPE);
                if (!handle->venc_set_config(configData, (OMX_INDEXTYPE)OMX_IndexConfigOperatingRate)) {
                    DEBUG_PRINT_ERROR("Failed to set OMX_IndexConfigOperatingRate");
                    return handle->hw_overload ? OMX_ErrorInsufficientResources :
                            OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_QTIIndexConfigVideoRoiInfo:
            {
                VALIDATE_OMX_PARAM_DATA(configData, OMX_QTI_VIDEO_CONFIG_ROIINFO);
                if (!handle->venc_set_config(configData, (OMX_INDEXTYPE)OMX_QTIIndexConfigVideoRoiInfo)) {
                    DEBUG_PRINT_ERROR("Failed to set OMX_QTIIndexConfigVideoRoiInfo");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_IndexConfigTimePosition:
            {
                OMX_TIME_CONFIG_TIMESTAMPTYPE* pParam =
                    (OMX_TIME_CONFIG_TIMESTAMPTYPE*) configData;
                unsigned long buf;
                unsigned long p2;
                unsigned long bufTime;

                pthread_mutex_lock(&m_TimeStampInfo.m_lock);
                m_TimeStampInfo.ts = (OMX_S64)pParam->nTimestamp;
                    while (m_TimeStampInfo.deferred_inbufq.m_size &&
                          !(m_TimeStampInfo.deferred_inbufq.get_q_msg_type() > m_TimeStampInfo.ts)) {
                        m_TimeStampInfo.deferred_inbufq.pop_entry(&buf,&p2,&bufTime);
                        DEBUG_PRINT_INFO("Queueing back pending buffer %lu timestamp %lu", buf, bufTime);
                        this->post_event((unsigned long)hComp, (unsigned long)buf, m_input_msg_id);
                   }
                pthread_mutex_unlock(&m_TimeStampInfo.m_lock);
                memcpy(&m_sConfigInputTrigTS, pParam, sizeof(m_sConfigInputTrigTS));
                break;
            }
#ifdef SUPPORT_CONFIG_INTRA_REFRESH
       case OMX_IndexConfigAndroidIntraRefresh:
           {
                VALIDATE_OMX_PARAM_DATA(configData, OMX_VIDEO_CONFIG_ANDROID_INTRAREFRESHTYPE);
                OMX_VIDEO_CONFIG_ANDROID_INTRAREFRESHTYPE* pParam =
                    (OMX_VIDEO_CONFIG_ANDROID_INTRAREFRESHTYPE*) configData;
                if (m_state == OMX_StateLoaded
                        || m_sInPortDef.bEnabled == OMX_FALSE
                        || m_sOutPortDef.bEnabled == OMX_FALSE) {
                    if (!handle->venc_set_config(configData, (OMX_INDEXTYPE)OMX_IndexConfigAndroidIntraRefresh)) {
                        DEBUG_PRINT_ERROR("Failed to set OMX_IndexConfigVideoIntraRefreshType");
                        return OMX_ErrorUnsupportedSetting;
                    }
                    m_sConfigIntraRefresh.nRefreshPeriod = pParam->nRefreshPeriod;
               } else {
                    DEBUG_PRINT_ERROR("ERROR: Setting OMX_IndexConfigAndroidIntraRefresh supported only at start of session");
                    return OMX_ErrorUnsupportedSetting;
                }
               break;
           }
#endif
        case OMX_QTIIndexConfigVideoBlurResolution:
           {
                VALIDATE_OMX_PARAM_DATA(configData, OMX_QTI_VIDEO_CONFIG_BLURINFO);
                OMX_QTI_VIDEO_CONFIG_BLURINFO* pParam =
                              (OMX_QTI_VIDEO_CONFIG_BLURINFO*) configData;
                if (!handle->venc_set_config(configData, (OMX_INDEXTYPE)OMX_QTIIndexConfigVideoBlurResolution)) {
                    DEBUG_PRINT_ERROR("Failed to set OMX_QTIIndexConfigVideoBlurResolution");
                    return OMX_ErrorUnsupportedSetting;
                }
                memcpy(&m_blurInfo, pParam, sizeof(m_blurInfo));
                break;
           }
        case OMX_QcomIndexConfigH264Transform8x8:
           {
                if (!handle->venc_set_config(configData, (OMX_INDEXTYPE)OMX_QcomIndexConfigH264Transform8x8)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting OMX_QcomIndexConfigH264Transform8x8 failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_QTIIndexConfigDescribeColorAspects:
           {
               VALIDATE_OMX_PARAM_DATA(configData, DescribeColorAspectsParams);
               DescribeColorAspectsParams *params = (DescribeColorAspectsParams *)configData;
               print_debug_color_aspects(&(params->sAspects), "set_config");
               if ((m_state != OMX_StateExecuting) && (!BITMASK_PRESENT(&m_flags,OMX_COMPONENT_EXECUTE_PENDING))) {
                   if (!handle->venc_set_config(configData, (OMX_INDEXTYPE)OMX_QTIIndexConfigDescribeColorAspects)) {
                       DEBUG_PRINT_ERROR("Failed to set OMX_QTIIndexConfigDescribeColorAspects");
                       return OMX_ErrorUnsupportedSetting;
                   }
               }
               else {
                   DEBUG_PRINT_LOW("Ignoring colorspace setting when flag set to OMX_COMPONENT_EXECUTE_PENDING or state is in execute");
               }
               memcpy(&m_sConfigColorAspects, configData, sizeof(m_sConfigColorAspects));
               break;
           }
        case OMX_IndexConfigAndroidVideoTemporalLayering:
            {
                VALIDATE_OMX_PARAM_DATA(configData, OMX_VIDEO_CONFIG_ANDROID_TEMPORALLAYERINGTYPE);
                OMX_VIDEO_CONFIG_ANDROID_TEMPORALLAYERINGTYPE* pParam =
                    (OMX_VIDEO_CONFIG_ANDROID_TEMPORALLAYERINGTYPE*)configData;
                if (!handle->venc_set_config(configData, (OMX_INDEXTYPE)OMX_IndexConfigAndroidVideoTemporalLayering)) {
                    DEBUG_PRINT_ERROR("ERROR: Setting OMX_IndexConfigAndroidVideoTemporalLayering failed");
                    return OMX_ErrorUnsupportedSetting;
                }
                // save the actual configuration applied
                memcpy(&m_sConfigTemporalLayers, pParam, sizeof(m_sConfigTemporalLayers));
                // keep the config data in sync
                m_sParamTemporalLayers.ePattern = m_sConfigTemporalLayers.ePattern;
                m_sParamTemporalLayers.nBLayerCountActual = m_sConfigTemporalLayers.nBLayerCountActual;
                m_sParamTemporalLayers.nPLayerCountActual = m_sConfigTemporalLayers.nPLayerCountActual;
                m_sParamTemporalLayers.bBitrateRatiosSpecified = m_sConfigTemporalLayers.bBitrateRatiosSpecified;
                memcpy(&m_sParamTemporalLayers.nBitrateRatios[0],
                        &m_sConfigTemporalLayers.nBitrateRatios[0],
                        OMX_VIDEO_ANDROID_MAXTEMPORALLAYERS * sizeof(OMX_U32));

                break;
            }
        case OMX_QcomIndexConfigPerfLevel:
            {
                VALIDATE_OMX_PARAM_DATA(configData, OMX_QCOM_VIDEO_CONFIG_PERF_LEVEL);
                if (!handle->venc_set_config(configData,
                        (OMX_INDEXTYPE)OMX_QcomIndexConfigPerfLevel)) {
                    DEBUG_PRINT_ERROR("Failed to set perf level");
                    return OMX_ErrorUnsupportedSetting;
                }
                break;
            }
        case OMX_IndexConfigAndroidVendorExtension:
            {
                VALIDATE_OMX_PARAM_DATA(configData, OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE);

                OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext =
                    reinterpret_cast<OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *>(configData);
                VALIDATE_OMX_VENDOR_EXTENSION_PARAM_DATA(ext);

                return set_vendor_extension_config(ext);
            }
        case OMX_IndexConfigVideoNalSize:
            {
                return OMX_ErrorUnsupportedIndex;
            }
        default:
            DEBUG_PRINT_ERROR("ERROR: unsupported index %d", (int) configIndex);
            break;
    }

    return OMX_ErrorNone;
}

/* ======================================================================
   FUNCTION
   omx_venc::ComponentDeInit

   DESCRIPTION
   Destroys the component and release memory allocated to the heap.

   PARAMETERS
   <TBD>.

   RETURN VALUE
   OMX Error None if everything successful.

   ========================================================================== */
OMX_ERRORTYPE  omx_venc::component_deinit(OMX_IN OMX_HANDLETYPE hComp)
{
    (void) hComp;
    OMX_U32 i = 0;
    DEBUG_PRINT_HIGH("omx_venc(): Inside component_deinit()");
    if (OMX_StateLoaded != m_state) {
        DEBUG_PRINT_ERROR("WARNING:Rxd DeInit,OMX not in LOADED state %d",\
                m_state);
    }

    auto_lock l(m_buf_lock);
    if (m_out_mem_ptr) {
        DEBUG_PRINT_LOW("Freeing the Output Memory");
        for (i=0; i< m_sOutPortDef.nBufferCountActual; i++ ) {
            if (BITMASK_PRESENT(&m_out_bm_count, i)) {
                BITMASK_CLEAR(&m_out_bm_count, i);
                if (BITMASK_PRESENT(&m_client_out_bm_count, i))
                    BITMASK_CLEAR(&m_client_out_bm_count, i);
                free_output_buffer (&m_out_mem_ptr[i]);
            }

            if (release_output_done()) {
                break;
            }
        }
        free(m_out_mem_ptr);
        m_out_mem_ptr = NULL;
    }

    /*Check if the input buffers have to be cleaned up*/
    if (m_inp_mem_ptr
#ifdef _ANDROID_ICS_
            && !meta_mode_enable
#endif
       ) {
        DEBUG_PRINT_LOW("Freeing the Input Memory");
        for (i=0; i<m_sInPortDef.nBufferCountActual; i++ ) {
            if (BITMASK_PRESENT(&m_inp_bm_count, i)) {
                BITMASK_CLEAR(&m_inp_bm_count, i);
                if (BITMASK_PRESENT(&m_client_in_bm_count, i))
                    BITMASK_CLEAR(&m_client_in_bm_count, i);
                free_input_buffer (&m_inp_mem_ptr[i]);
            }

            if (release_input_done()) {
                break;
            }
        }


        free(m_inp_mem_ptr);
        m_inp_mem_ptr = NULL;
    }

    // Reset counters in mesg queues
    m_ftb_q.m_size=0;
    m_cmd_q.m_size=0;
    m_etb_q.m_size=0;
    m_ftb_q.m_read = m_ftb_q.m_write =0;
    m_cmd_q.m_read = m_cmd_q.m_write =0;
    m_etb_q.m_read = m_etb_q.m_write =0;

    DEBUG_PRINT_HIGH("Calling venc_close()");
    if (handle) {
        handle->venc_close();
        DEBUG_PRINT_HIGH("Deleting HANDLE[%p]", handle);
        delete (handle);
        handle = NULL;
    }
    DEBUG_PRINT_INFO("Component Deinit");
    return OMX_ErrorNone;
}


OMX_U32 omx_venc::dev_stop( void)
{
    return handle->venc_stop();
}


OMX_U32 omx_venc::dev_pause(void)
{
    return handle->venc_pause();
}

OMX_U32 omx_venc::dev_start(void)
{
    return handle->venc_start();
}

OMX_U32 omx_venc::dev_flush(unsigned port)
{
    return handle->venc_flush(port);
}
OMX_U32 omx_venc::dev_resume(void)
{
    return handle->venc_resume();
}

OMX_U32 omx_venc::dev_start_done(void)
{
    return handle->venc_start_done();
}

OMX_U32 omx_venc::dev_set_message_thread_id(pthread_t tid)
{
    return handle->venc_set_message_thread_id(tid);
}

bool omx_venc::dev_handle_empty_eos_buffer(void)
{
    return true;
}

bool omx_venc::dev_use_buf(void *buf_addr,unsigned port,unsigned index)
{
    return handle->venc_use_buf(buf_addr,port,index);
}

bool omx_venc::dev_buffer_ready_to_queue(OMX_BUFFERHEADERTYPE *buffer)
{
    bool bRet = true;

    pthread_mutex_lock(&m_TimeStampInfo.m_lock);

    if ((!m_sParamControlInputQueue.bEnable) ||
        ((OMX_S64)buffer->nTimeStamp <= (OMX_S64)m_TimeStampInfo.ts)) {
        DEBUG_PRINT_LOW("ETB is ready to be queued");
    } else {
        DEBUG_PRINT_INFO("ETB is deferred due to timeStamp mismatch  buf_ts %lld timestamp.ts %lld",
                        (OMX_S64)buffer->nTimeStamp, (OMX_S64)m_TimeStampInfo.ts);
        m_TimeStampInfo.deferred_inbufq.insert_entry((unsigned long)buffer, 0, buffer->nTimeStamp);
        bRet = false;
    }
    pthread_mutex_unlock(&m_TimeStampInfo.m_lock);
    return bRet;
}

bool omx_venc::dev_free_buf(void *buf_addr,unsigned port)
{
    return handle->venc_free_buf(buf_addr,port);
}

bool omx_venc::dev_empty_buf(void *buffer, void *pmem_data_buf,unsigned index,unsigned fd)
{
    bool bret = false;
    bret = handle->venc_empty_buf(buffer, pmem_data_buf,index,fd);
    hw_overload = handle->hw_overload;
    return bret;
}

bool omx_venc::dev_fill_buf(void *buffer, void *pmem_data_buf,unsigned index,unsigned fd)
{
    return handle->venc_fill_buf(buffer, pmem_data_buf,index,fd);
}

bool omx_venc::dev_get_seq_hdr(void *buffer, unsigned size, unsigned *hdrlen)
{
    return handle->venc_get_seq_hdr(buffer, size, hdrlen);
}

bool omx_venc::dev_get_capability_ltrcount(OMX_U32 *min, OMX_U32 *max, OMX_U32 *step_size)
{
#ifdef _MSM8974_
    (void) min;
    (void) max;
    (void) step_size;
    DEBUG_PRINT_ERROR("Get Capability LTR Count is not supported");
    return false;
#else
    return handle->venc_get_capability_ltrcount(min, max, step_size);
#endif
}

bool omx_venc::dev_get_performance_level(OMX_U32 *perflevel)
{
#ifdef _MSM8974_
    return handle->venc_get_performance_level(perflevel);
#else
    DEBUG_PRINT_ERROR("Get performance level is not supported");
    return false;
#endif
}

bool omx_venc::dev_get_vui_timing_info(OMX_U32 *enabled)
{
#ifdef _MSM8974_
    return handle->venc_get_vui_timing_info(enabled);
#else
    DEBUG_PRINT_ERROR("Get vui timing information is not supported");
    return false;
#endif
}

bool omx_venc::dev_get_vqzip_sei_info(OMX_U32 *enabled)
{
    return handle->venc_get_vqzip_sei_info(enabled);
}

bool omx_venc::dev_get_peak_bitrate(OMX_U32 *peakbitrate)
{
#ifdef _MSM8974_
    return handle->venc_get_peak_bitrate(peakbitrate);
#else
    DEBUG_PRINT_ERROR("Get peak bitrate is not supported");
    return false;
#endif
}

bool omx_venc::dev_get_batch_size(OMX_U32 *size)
{
#ifdef _MSM8974_
    return handle->venc_get_batch_size(size);
#else
    DEBUG_PRINT_ERROR("Get batch size is not supported");
    return false;
#endif
}

bool omx_venc::dev_get_temporal_layer_caps(OMX_U32 *nMaxLayers,
        OMX_U32 *nMaxBLayers) {
    return handle->venc_get_temporal_layer_caps(nMaxLayers, nMaxBLayers);
}

bool omx_venc::dev_get_pq_status(OMX_BOOL *pq_status) {
    return handle->venc_get_pq_status(pq_status);
}

bool omx_venc::dev_loaded_start()
{
    return handle->venc_loaded_start();
}

bool omx_venc::dev_loaded_stop()
{
    return handle->venc_loaded_stop();
}

bool omx_venc::dev_loaded_start_done()
{
    return handle->venc_loaded_start_done();
}

bool omx_venc::dev_loaded_stop_done()
{
    return handle->venc_loaded_stop_done();
}

bool omx_venc::dev_get_buf_req(OMX_U32 *min_buff_count,
        OMX_U32 *actual_buff_count,
        OMX_U32 *buff_size,
        OMX_U32 port)
{
    return handle->venc_get_buf_req(min_buff_count,
            actual_buff_count,
            buff_size,
            port);

}

bool omx_venc::dev_get_dimensions(OMX_U32 port,
        OMX_U32 *width,
        OMX_U32 *height)
{
    return handle->venc_get_dimensions(port,
            width,
            height);
}

bool omx_venc::dev_set_buf_req(OMX_U32 *min_buff_count,
        OMX_U32 *actual_buff_count,
        OMX_U32 *buff_size,
        OMX_U32 port)
{
    return handle->venc_set_buf_req(min_buff_count,
            actual_buff_count,
            buff_size,
            port);

}

bool omx_venc::dev_is_video_session_supported(OMX_U32 width, OMX_U32 height)
{
#ifdef _MSM8974_
    return handle->venc_is_video_session_supported(width,height);
#else
    DEBUG_PRINT_LOW("Check against video capability not supported");
    return true;
#endif
}

int omx_venc::dev_handle_output_extradata(void *buffer, int index)
{
    return handle->handle_output_extradata(buffer, index);
}

int omx_venc::dev_set_format(int color)
{
    return handle->venc_set_format(color);
}

int omx_venc::async_message_process (void *context, void* message)
{
    omx_video* omx = NULL;
    struct venc_msg *m_sVenc_msg = NULL;
    OMX_BUFFERHEADERTYPE* omxhdr = NULL;
    struct venc_buffer *temp_buff = NULL;
    native_handle_t *nh = NULL;

    if (context == NULL || message == NULL) {
        DEBUG_PRINT_ERROR("ERROR: omx_venc::async_message_process invalid i/p params");
        return -1;
    }
    m_sVenc_msg = (struct venc_msg *)message;

    omx = reinterpret_cast<omx_video*>(context);

    if (m_sVenc_msg->statuscode != VEN_S_SUCCESS) {
        DEBUG_PRINT_ERROR("ERROR: async_msg_process() - Error statuscode = %lu",
                m_sVenc_msg->statuscode);
        if(m_sVenc_msg->msgcode == VEN_MSG_HW_OVERLOAD) {
            omx->post_event (0, m_sVenc_msg->statuscode,\
                    OMX_COMPONENT_GENERATE_HARDWARE_OVERLOAD);
        } else {
            omx->post_event (0, m_sVenc_msg->statuscode,\
                    OMX_COMPONENT_GENERATE_HARDWARE_ERROR);
        }
    }

    DEBUG_PRINT_LOW("omx_venc::async_message_process- msgcode = %lu",
            m_sVenc_msg->msgcode);
    switch (m_sVenc_msg->msgcode) {
        case VEN_MSG_START:
            omx->post_event (0,m_sVenc_msg->statuscode,\
                    OMX_COMPONENT_GENERATE_START_DONE);
            break;
        case VEN_MSG_STOP:
            omx->post_event (0,m_sVenc_msg->statuscode,\
                    OMX_COMPONENT_GENERATE_STOP_DONE);
            break;
        case VEN_MSG_RESUME:
            omx->post_event (0,m_sVenc_msg->statuscode,\
                    OMX_COMPONENT_GENERATE_RESUME_DONE);
            break;
        case VEN_MSG_PAUSE:
            omx->post_event (0,m_sVenc_msg->statuscode,\
                    OMX_COMPONENT_GENERATE_PAUSE_DONE);
            break;
        case VEN_MSG_FLUSH_INPUT_DONE:

            omx->post_event (0,m_sVenc_msg->statuscode,\
                    OMX_COMPONENT_GENERATE_EVENT_INPUT_FLUSH);
            break;
#ifndef _TARGET_KERNEL_VERSION_49_
        case VEN_MSG_FLUSH_OUPUT_DONE:
            omx->post_event (0,m_sVenc_msg->statuscode,\
                    OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH);
            break;
#else
        case VEN_MSG_FLUSH_OUTPUT_DONE:
            omx->post_event (0,m_sVenc_msg->statuscode,\
                    OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH);
            break;
#endif
        case VEN_MSG_INPUT_BUFFER_DONE:
            omxhdr = (OMX_BUFFERHEADERTYPE* )\
                     m_sVenc_msg->buf.clientdata;

            if (omxhdr == NULL ||
                    (((OMX_U32)(omxhdr - omx->m_inp_mem_ptr) > omx->m_sInPortDef.nBufferCountActual) &&
                     ((OMX_U32)(omxhdr - omx->meta_buffer_hdr) > omx->m_sInPortDef.nBufferCountActual))) {
                omxhdr = NULL;
                m_sVenc_msg->statuscode = VEN_S_EFAIL;
            }

#ifdef _ANDROID_ICS_
            omx->omx_release_meta_buffer(omxhdr);
#endif
            omx->post_event ((unsigned long)omxhdr,m_sVenc_msg->statuscode,
                    OMX_COMPONENT_GENERATE_EBD);
            break;
        case VEN_MSG_OUTPUT_BUFFER_DONE:
        {
            omxhdr = (OMX_BUFFERHEADERTYPE*)m_sVenc_msg->buf.clientdata;
            OMX_U32 bufIndex = (OMX_U32)(omxhdr - omx->m_out_mem_ptr);

            if ( (omxhdr != NULL) &&
                    (bufIndex  < omx->m_sOutPortDef.nBufferCountActual)) {
                auto_lock l(omx->m_buf_lock);
                if (BITMASK_ABSENT(&(omx->m_out_bm_count), bufIndex)) {
                    DEBUG_PRINT_ERROR("Recieved FBD for buffer that is already freed !");
                    break;
                }
                if (!omx->is_secure_session() && (m_sVenc_msg->buf.len <=  omxhdr->nAllocLen)) {
                    omxhdr->nFilledLen = m_sVenc_msg->buf.len;
                    omxhdr->nOffset = m_sVenc_msg->buf.offset;
                    omxhdr->nTimeStamp = m_sVenc_msg->buf.timestamp;
                    DEBUG_PRINT_LOW("o/p TS = %u", (unsigned int)m_sVenc_msg->buf.timestamp);
                    omxhdr->nFlags = m_sVenc_msg->buf.flags;

                    /*Use buffer case*/
                    if (BITMASK_PRESENT(&(omx->m_client_out_bm_count), bufIndex) &&
                        omx->output_use_buffer && !omx->m_use_output_pmem && !omx->is_secure_session()) {
                        DEBUG_PRINT_LOW("memcpy() for o/p Heap UseBuffer");
                        memcpy(omxhdr->pBuffer,
                                (m_sVenc_msg->buf.ptrbuffer),
                                m_sVenc_msg->buf.len);
                    }
                } else if (omx->is_secure_session()) {
                    if (omx->allocate_native_handle) {
                        native_handle_t *nh = (native_handle_t *)(omxhdr->pBuffer);
                        nh->data[1] = m_sVenc_msg->buf.offset;
                        nh->data[2] = m_sVenc_msg->buf.len;
                        omxhdr->nFilledLen = m_sVenc_msg->buf.len;
                        omxhdr->nTimeStamp = m_sVenc_msg->buf.timestamp;
                        omxhdr->nFlags = m_sVenc_msg->buf.flags;
                    } else {
                        output_metabuffer *meta_buf = (output_metabuffer *)(omxhdr->pBuffer);
                        native_handle_t *nh = meta_buf->nh;
                        nh->data[1] = m_sVenc_msg->buf.offset;
                        nh->data[2] = m_sVenc_msg->buf.len;
                        omxhdr->nFilledLen = sizeof(output_metabuffer);
                        omxhdr->nTimeStamp = m_sVenc_msg->buf.timestamp;
                        omxhdr->nFlags = m_sVenc_msg->buf.flags;
                    }
                } else {
                    omxhdr->nFilledLen = 0;
                }

            } else {
                omxhdr = NULL;
                m_sVenc_msg->statuscode = VEN_S_EFAIL;
            }
            omx->post_event ((unsigned long)omxhdr,m_sVenc_msg->statuscode,
                    OMX_COMPONENT_GENERATE_FBD);
            break;
        }
        case VEN_MSG_NEED_OUTPUT_BUFFER:
            //TBD what action needs to be done here??
            break;
#ifndef _MSM8974_
        case VEN_MSG_LTRUSE_FAILED:
            DEBUG_PRINT_ERROR("LTRUSE Failed!");
            omx->post_event (NULL,m_sVenc_msg->statuscode,
                    OMX_COMPONENT_GENERATE_LTRUSE_FAILED);
            break;
#endif
        default:
            DEBUG_PRINT_HIGH("Unknown msg received : %lu", m_sVenc_msg->msgcode);
            break;
    }
    return 0;
}

bool omx_venc::dev_color_align(OMX_BUFFERHEADERTYPE *buffer,
                OMX_U32 width, OMX_U32 height)
{
    if(secure_session) {
        DEBUG_PRINT_ERROR("Cannot align colors in secure session.");
        return OMX_FALSE;
    }
    return handle->venc_color_align(buffer, width,height);
}

bool omx_venc::is_secure_session()
{
    return secure_session;
}

bool omx_venc::dev_get_output_log_flag()
{
    return handle->venc_get_output_log_flag();
}

int omx_venc::dev_output_log_buffers(const char *buffer,
                  int bufferlen, uint64_t timestamp)
{
    return handle->venc_output_log_buffers(buffer, bufferlen, timestamp);
}

int omx_venc::dev_extradata_log_buffers(char *buffer)
{
    return handle->venc_extradata_log_buffers(buffer);
}
