/*--------------------------------------------------------------------------
Copyright (c) 2014-2017,2020 The Linux Foundation. All rights reserved.

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
#include "omx_swvenc_mpeg4.h"

/* def: StoreMetaDataInBuffersParams */
#include <media/hardware/HardwareAPI.h>

/* def: VENUS_BUFFER_SIZE, VENUS_Y_STRIDE etc */
#include <media/msm_media_info.h>

/* def: private_handle_t*/
#include <gralloc_priv.h>


/*----------------------------------------------------------------------------
 * Preprocessor Definitions and Constants
 * -------------------------------------------------------------------------*/
#define OMX_SPEC_VERSION 0x00000101
#define OMX_INIT_STRUCT(_s_, _name_)             \
    memset((_s_), 0x0, sizeof(_name_));          \
    (_s_)->nSize = sizeof(_name_);               \
    (_s_)->nVersion.nVersion = OMX_SPEC_VERSION

#define ENTER_FUNC() DEBUG_PRINT_HIGH("ENTERING: %s",__FUNCTION__)
#define EXIT_FUNC()  DEBUG_PRINT_HIGH("EXITING: %s",__FUNCTION__)
#define RETURN(x)    EXIT_FUNC(); return x;
#define ALIGN(value,alignment) (((value) + (alignment-1)) & (~(alignment-1)))

#define BUFFER_LOG_LOC "/data/vendor/media"

/* factory function executed by the core to create instances */
void *get_omx_component_factory_fn(void)
{
    RETURN((new omx_venc));
}

omx_venc::omx_venc()
{
    ENTER_FUNC();

    char property_value[PROPERTY_VALUE_MAX] = {0};

    memset(&m_debug,0,sizeof(m_debug));

    property_value[0] = '\0';
    property_get("vendor.vidc.debug.level", property_value, "1");
    debug_level = atoi(property_value);

    property_value[0] = '\0';
    property_get("vendor.vidc.enc.log.in", property_value, "0");
    m_debug.in_buffer_log = atoi(property_value);

    property_value[0] = '\0';
    property_get("vendor.vidc.enc.log.out", property_value, "0");
    m_debug.out_buffer_log = atoi(property_value);

    snprintf(m_debug.log_loc, PROPERTY_VALUE_MAX, "%s", BUFFER_LOG_LOC);
    property_value[0] = '\0';
    property_get("vendor.vidc.log.loc", property_value, "");
    if (*property_value)
    {
       strlcpy(m_debug.log_loc, property_value, PROPERTY_VALUE_MAX);
    }

    memset(meta_buffer_hdr,0,sizeof(meta_buffer_hdr));
    meta_mode_enable = false;
    memset(meta_buffer_hdr,0,sizeof(meta_buffer_hdr));
    memset(meta_buffers,0,sizeof(meta_buffers));
    memset(opaque_buffer_hdr,0,sizeof(opaque_buffer_hdr));
    mUseProxyColorFormat = false;
    get_syntaxhdr_enable = false;
    m_bSeqHdrRequested = false;
    set_format = false;

    EXIT_FUNC();
}

omx_venc::~omx_venc()
{
    ENTER_FUNC();
    get_syntaxhdr_enable = false;
    EXIT_FUNC();
}

OMX_ERRORTYPE omx_venc::component_init(OMX_STRING role)
{
    ENTER_FUNC();

    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    SWVENC_STATUS Ret = SWVENC_S_SUCCESS;
    SWVENC_CALLBACK callBackInfo;
    OMX_VIDEO_CODINGTYPE codec_type;
    SWVENC_PROPERTY Prop;
    int fds[2];

    strlcpy((char *)m_nkind,role,OMX_MAX_STRINGNAME_SIZE);
    secure_session = false;

    if (!strncmp( (char *)m_nkind,"OMX.qcom.video.encoder.mpeg4sw",
                  OMX_MAX_STRINGNAME_SIZE))
    {
        strlcpy((char *)m_cRole, "video_encoder.mpeg4",\
                OMX_MAX_STRINGNAME_SIZE);
        codec_type = OMX_VIDEO_CodingMPEG4;
        m_codec = SWVENC_CODEC_MPEG4;
    }
    else if (!strncmp( (char *)m_nkind,"OMX.qcom.video.encoder.h263sw",
                  OMX_MAX_STRINGNAME_SIZE))
    {
        strlcpy((char *)m_cRole, "video_encoder.h263",\
                OMX_MAX_STRINGNAME_SIZE);
        codec_type = OMX_VIDEO_CodingH263;
        m_codec = SWVENC_CODEC_H263;
    }
    else
    {
        DEBUG_PRINT_ERROR("ERROR: Unknown Component");
        eRet = OMX_ErrorInvalidComponentName;
        RETURN(eRet);
    }

#ifdef ENABLE_GET_SYNTAX_HDR
    get_syntaxhdr_enable = true;
    DEBUG_PRINT_HIGH("Get syntax header enabled");
#endif

    callBackInfo.pfn_empty_buffer_done    = swvenc_empty_buffer_done_cb;
    callBackInfo.pfn_fill_buffer_done     = swvenc_fill_buffer_done_cb;
    callBackInfo.pfn_event_notification   = swvenc_handle_event_cb;
    callBackInfo.p_client                 = (void*)this;

    SWVENC_STATUS sRet = swvenc_init(&m_hSwVenc, m_codec, &callBackInfo);
    if (sRet != SWVENC_S_SUCCESS)
    {
        DEBUG_PRINT_ERROR("swvenc_init returned %d, ret insufficient resources",
         sRet);
        RETURN(OMX_ErrorInsufficientResources);
    }

    sRet = swvenc_check_inst_load(m_hSwVenc);
    if (sRet != SWVENC_S_SUCCESS)
    {
        DEBUG_PRINT_ERROR("swvenc_init returned %d, ret insufficient resources",
         sRet);
        RETURN(OMX_ErrorInsufficientResources);
    }

    m_stopped = true;

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
    m_sConfigFrameRotation.nPortIndex = (OMX_U32) PORT_INDEX_IN;
    m_sConfigFrameRotation.nRotation = 0;

    OMX_INIT_STRUCT(&m_sSessionQuantization, OMX_VIDEO_PARAM_QUANTIZATIONTYPE);
    m_sSessionQuantization.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sSessionQuantization.nQpI = 9;
    m_sSessionQuantization.nQpP = 6;
    m_sSessionQuantization.nQpB = 2;

    OMX_INIT_STRUCT(&m_sSessionQPRange, OMX_QCOM_VIDEO_PARAM_QPRANGETYPE);
    m_sSessionQPRange.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sSessionQPRange.minQP = 2;

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

    OMX_INIT_STRUCT(&m_sIntraRefresh, OMX_VIDEO_PARAM_INTRAREFRESHTYPE);
    m_sIntraRefresh.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sIntraRefresh.eRefreshMode = OMX_VIDEO_IntraRefreshMax;

    if (codec_type == OMX_VIDEO_CodingMPEG4)
    {
        m_sParamProfileLevel.eProfile = (OMX_U32) OMX_VIDEO_MPEG4ProfileSimple;
        m_sParamProfileLevel.eLevel = (OMX_U32) OMX_VIDEO_MPEG4Level0;
    } else if (codec_type == OMX_VIDEO_CodingH263)
    {
        m_sParamProfileLevel.eProfile = (OMX_U32) OMX_VIDEO_H263ProfileBaseline;
        m_sParamProfileLevel.eLevel = (OMX_U32) OMX_VIDEO_H263Level10;
    }

    /* set the profile and level */
    Ret = swvenc_set_profile_level(m_sParamProfileLevel.eProfile,
                m_sParamProfileLevel.eLevel);
    if (Ret != SWVENC_S_SUCCESS)
    {
       DEBUG_PRINT_ERROR("%s, swvenc_set_rc_mode failed (%d)",
         __FUNCTION__, Ret);
       RETURN(OMX_ErrorUndefined);
    }

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
        QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m;
    m_sInPortDef.format.video.eCompressionFormat =  OMX_VIDEO_CodingUnused;

    /* set the frame size */
    Prop.id = SWVENC_PROPERTY_ID_FRAME_SIZE;
    Prop.info.frame_size.height = m_sInPortDef.format.video.nFrameHeight;
    Prop.info.frame_size.width  = m_sInPortDef.format.video.nFrameWidth;

    Ret = swvenc_setproperty(m_hSwVenc, &Prop);
    if (Ret != SWVENC_S_SUCCESS)
    {
       DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
         __FUNCTION__, Ret);
       RETURN(OMX_ErrorUnsupportedSetting);
    }

    /* set the frame attributes */
    Prop.id = SWVENC_PROPERTY_ID_FRAME_ATTRIBUTES;
    Prop.info.frame_attributes.stride_luma = m_sInPortDef.format.video.nStride;
    Prop.info.frame_attributes.stride_chroma = m_sInPortDef.format.video.nStride;
    Prop.info.frame_attributes.offset_luma = 0;
    Prop.info.frame_attributes.offset_chroma =
      (m_sInPortDef.format.video.nSliceHeight * m_sInPortDef.format.video.nStride);
    Prop.info.frame_attributes.size = (Prop.info.frame_attributes.offset_chroma * 3) >> 1;

    Ret = swvenc_setproperty(m_hSwVenc, &Prop);
    if (Ret != SWVENC_S_SUCCESS)
    {
       DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
         __FUNCTION__, Ret);
       RETURN(OMX_ErrorUndefined);
    }

    Ret = swvenc_get_buffer_req(&m_sInPortDef.nBufferCountMin,
              &m_sInPortDef.nBufferCountActual,
              &m_sInPortDef.nBufferSize,
              &m_sInPortDef.nBufferAlignment,
              PORT_INDEX_IN);
    if (Ret != SWVENC_S_SUCCESS)
    {
       DEBUG_PRINT_ERROR("ERROR: %s, swvenc_get_buffer_req failed (%d)", __FUNCTION__,
          Ret);
       RETURN(OMX_ErrorUndefined);
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
    if (codec_type == OMX_VIDEO_CodingMPEG4)
    {
        m_sOutPortDef.format.video.eCompressionFormat =  OMX_VIDEO_CodingMPEG4;
    }
    else if (codec_type == OMX_VIDEO_CodingH263)
    {
        m_sOutPortDef.format.video.eCompressionFormat =  OMX_VIDEO_CodingH263;
    }

    Ret = swvenc_get_buffer_req(&m_sOutPortDef.nBufferCountMin,
              &m_sOutPortDef.nBufferCountActual,
              &m_sOutPortDef.nBufferSize,
              &m_sOutPortDef.nBufferAlignment,
              PORT_INDEX_OUT);
    if (Ret != SWVENC_S_SUCCESS)
    {
       DEBUG_PRINT_ERROR("ERROR: %s, swvenc_get_buffer_req failed (%d)", __FUNCTION__,
          Ret);
       RETURN(OMX_ErrorUndefined);
    }

    // Initialize the video color format for input port
    OMX_INIT_STRUCT(&m_sInPortFormat, OMX_VIDEO_PARAM_PORTFORMATTYPE);
    m_sInPortFormat.nPortIndex = (OMX_U32) PORT_INDEX_IN;
    m_sInPortFormat.nIndex = 0;
    m_sInPortFormat.eColorFormat = (OMX_COLOR_FORMATTYPE)
        QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m;
    m_sInPortFormat.eCompressionFormat = OMX_VIDEO_CodingUnused;

    // Initialize the compression format for output port
    OMX_INIT_STRUCT(&m_sOutPortFormat, OMX_VIDEO_PARAM_PORTFORMATTYPE);
    m_sOutPortFormat.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    m_sOutPortFormat.nIndex = 0;
    m_sOutPortFormat.eColorFormat = OMX_COLOR_FormatUnused;
    if (codec_type == OMX_VIDEO_CodingMPEG4)
    {
        m_sOutPortFormat.eCompressionFormat =  OMX_VIDEO_CodingMPEG4;
    } else if (codec_type == OMX_VIDEO_CodingH263)
    {
        m_sOutPortFormat.eCompressionFormat =  OMX_VIDEO_CodingH263;
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
    // 2 second intra period for default outport fps
    m_sParamMPEG4.nPFrames = (m_sOutPortFormat.xFramerate * 2 - 1);
    m_sParamMPEG4.bACPred = OMX_TRUE;
    // delta = 2 @ 15 fps
    m_sParamMPEG4.nTimeIncRes = 30;
    // pframe and iframe
    m_sParamMPEG4.nAllowedPictureTypes = 2;
    // number of video packet headers per vop
    m_sParamMPEG4.nHeaderExtension = 1;
    m_sParamMPEG4.bReversibleVLC = OMX_FALSE;

    // h263 specific init
    OMX_INIT_STRUCT(&m_sParamH263, OMX_VIDEO_PARAM_H263TYPE);
    m_sParamH263.nPortIndex = (OMX_U32) PORT_INDEX_OUT;
    // 2 second intra period for default outport fps
    m_sParamH263.nPFrames = (m_sOutPortFormat.xFramerate * 2 - 1);
    m_sParamH263.nBFrames = 0;
    m_sParamH263.eProfile = OMX_VIDEO_H263ProfileBaseline;
    m_sParamH263.eLevel = OMX_VIDEO_H263Level10;
    m_sParamH263.bPLUSPTYPEAllowed = OMX_FALSE;
    m_sParamH263.nAllowedPictureTypes = 2;
    m_sParamH263.bForceRoundingTypeToZero = OMX_TRUE;
    m_sParamH263.nPictureHeaderRepetition = 0;
    m_sParamH263.nGOBHeaderInterval = 1;

    m_state                   = OMX_StateLoaded;
    m_sExtraData = 0;

    m_capability.max_height = OMX_CORE_FWVGA_HEIGHT;
    m_capability.max_width = OMX_CORE_FWVGA_WIDTH;
    m_capability.min_height = 32;
    m_capability.min_width = 32;

    if (eRet == OMX_ErrorNone)
    {
        if (pipe(fds))
        {
            DEBUG_PRINT_ERROR("ERROR: pipe creation failed");
            eRet = OMX_ErrorInsufficientResources;
        }
        else
        {
            if ((fds[0] == 0) || (fds[1] == 0))
            {
                if (pipe(fds))
                {
                    DEBUG_PRINT_ERROR("ERROR: pipe creation failed");
                    eRet = OMX_ErrorInsufficientResources;
                }
            }
            if (eRet == OMX_ErrorNone)
            {
                m_pipe_in = fds[0];
                m_pipe_out = fds[1];

                if (pthread_create(&msg_thread_id,0, message_thread_enc, this) < 0) {
                    eRet = OMX_ErrorInsufficientResources;
                    msg_thread_created = false;
                }
                else {
                    msg_thread_created = true;
                }
            }
        }
    }

    DEBUG_PRINT_HIGH("Component_init return value = 0x%x", eRet);

    EXIT_FUNC();

    RETURN(eRet);
}

OMX_ERRORTYPE  omx_venc::set_parameter
(
    OMX_IN OMX_HANDLETYPE hComp,
    OMX_IN OMX_INDEXTYPE  paramIndex,
    OMX_IN OMX_PTR        paramData
)
{
    ENTER_FUNC();

    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    SWVENC_STATUS Ret  = SWVENC_S_SUCCESS;
    SWVENC_PROPERTY Prop;
    bool bResult;
    unsigned int stride, scanlines;

    (void)hComp;

    if (m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("ERROR: Set Param in Invalid State");
        RETURN(OMX_ErrorInvalidState);
    }
    if (paramData == NULL)
    {
        DEBUG_PRINT_ERROR("ERROR: Get Param in Invalid paramData");
        RETURN(OMX_ErrorBadParameter);
    }

    /* set_parameter can be called in loaded state or disabled port */
    if ( (m_state == OMX_StateLoaded) ||
         (m_sInPortDef.bEnabled == OMX_FALSE) ||
         (m_sOutPortDef.bEnabled == OMX_FALSE)
       )
    {
        DEBUG_PRINT_LOW("Set Parameter called in valid state");
    }
    else
    {
        DEBUG_PRINT_ERROR("ERROR: Set Parameter called in Invalid State");
        RETURN(OMX_ErrorIncorrectStateOperation);
    }

    switch ((int)paramIndex)
    {
        case OMX_IndexParamPortDefinition:
        {
            OMX_PARAM_PORTDEFINITIONTYPE *portDefn;
            portDefn = (OMX_PARAM_PORTDEFINITIONTYPE *) paramData;
            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamPortDefinition H= %d, W = %d",
                    (int)portDefn->format.video.nFrameHeight,
                    (int)portDefn->format.video.nFrameWidth);

            if (PORT_INDEX_IN == portDefn->nPortIndex)
            {
                if (!dev_is_video_session_supported(portDefn->format.video.nFrameWidth,
                            portDefn->format.video.nFrameHeight))
                {
                    DEBUG_PRINT_ERROR("video session not supported");
                    omx_report_unsupported_setting();
                    RETURN(OMX_ErrorUnsupportedSetting);
                }
                DEBUG_PRINT_LOW("i/p actual cnt requested = %u", portDefn->nBufferCountActual);
                DEBUG_PRINT_LOW("i/p min cnt requested = %u", portDefn->nBufferCountMin);
                DEBUG_PRINT_LOW("i/p buffersize requested = %u", portDefn->nBufferSize);
                if (portDefn->nBufferCountMin > portDefn->nBufferCountActual)
                {
                    DEBUG_PRINT_ERROR("ERROR: (In_PORT) Min buffers (%u) > actual count (%u)",
                            portDefn->nBufferCountMin, portDefn->nBufferCountActual);
                    RETURN(OMX_ErrorUnsupportedSetting);
                }

                /* set the frame size */
                Prop.id = SWVENC_PROPERTY_ID_FRAME_SIZE;
                Prop.info.frame_size.height = portDefn->format.video.nFrameHeight;
                Prop.info.frame_size.width  = portDefn->format.video.nFrameWidth;

                Ret = swvenc_setproperty(m_hSwVenc, &Prop);
                if (Ret != SWVENC_S_SUCCESS)
                {
                   DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
                     __FUNCTION__, Ret);
                   RETURN(OMX_ErrorUnsupportedSetting);
                }

                /* set the input frame-rate */
                if (portDefn->format.video.xFramerate != 0)
                {
                   Ret = swvenc_set_frame_rate(portDefn->format.video.xFramerate >> 16);
                   if (Ret != SWVENC_S_SUCCESS)
                   {
                      DEBUG_PRINT_ERROR("%s, swvenc_set_frame_rate failed (%d)",
                        __FUNCTION__, Ret);
                      RETURN(OMX_ErrorUnsupportedSetting);
                   }
                }

                /* set the frame attributes */
                stride = VENUS_Y_STRIDE(COLOR_FMT_NV12, portDefn->format.video.nFrameWidth);
                scanlines = VENUS_Y_SCANLINES(COLOR_FMT_NV12, portDefn->format.video.nSliceHeight);
                Prop.id = SWVENC_PROPERTY_ID_FRAME_ATTRIBUTES;
                Prop.info.frame_attributes.stride_luma = stride;
                Prop.info.frame_attributes.stride_chroma = stride;
                Prop.info.frame_attributes.offset_luma = 0;
                Prop.info.frame_attributes.offset_chroma = scanlines * stride;
                Prop.info.frame_attributes.size =
                  VENUS_BUFFER_SIZE(COLOR_FMT_NV12,
                     portDefn->format.video.nFrameWidth,
                     portDefn->format.video.nFrameHeight);

                Ret = swvenc_setproperty(m_hSwVenc, &Prop);
                if (Ret != SWVENC_S_SUCCESS)
                {
                   DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
                     __FUNCTION__, Ret);
                   RETURN(OMX_ErrorUnsupportedSetting);
                }

                DEBUG_PRINT_LOW("i/p previous actual cnt = %u", m_sInPortDef.nBufferCountActual);
                DEBUG_PRINT_LOW("i/p previous min cnt = %u", m_sInPortDef.nBufferCountMin);
                DEBUG_PRINT_LOW("i/p previous buffersize = %u", m_sInPortDef.nBufferSize);

                memcpy(&m_sInPortDef, portDefn,sizeof(OMX_PARAM_PORTDEFINITIONTYPE));

                /* update the input buffer requirement */
                Ret = swvenc_get_buffer_req(&m_sInPortDef.nBufferCountMin,
                        &m_sInPortDef.nBufferCountActual,
                        &m_sInPortDef.nBufferSize,
                        &m_sInPortDef.nBufferAlignment,
                        portDefn->nPortIndex);
                if (Ret != SWVENC_S_SUCCESS)
                {
                   DEBUG_PRINT_ERROR("ERROR: %s, swvenc_get_buffer_req failed (%d)", __FUNCTION__,
                      Ret);
                   RETURN(OMX_ErrorUndefined);
                }

                if (portDefn->nBufferCountActual > m_sInPortDef.nBufferCountActual)
                {
                   m_sInPortDef.nBufferCountActual = portDefn->nBufferCountActual;
                }
                if (portDefn->nBufferSize > m_sInPortDef.nBufferSize)
                {
                   m_sInPortDef.nBufferSize = portDefn->nBufferSize;
                }

                DEBUG_PRINT_LOW("i/p new actual cnt = %u", m_sInPortDef.nBufferCountActual);
                DEBUG_PRINT_LOW("i/p new min cnt = %u", m_sInPortDef.nBufferCountMin);
                DEBUG_PRINT_LOW("i/p new buffersize = %u", m_sInPortDef.nBufferSize);
            }
            else if (PORT_INDEX_OUT == portDefn->nPortIndex)
            {
                DEBUG_PRINT_LOW("o/p actual cnt requested = %u", portDefn->nBufferCountActual);
                DEBUG_PRINT_LOW("o/p min cnt requested = %u", portDefn->nBufferCountMin);
                DEBUG_PRINT_LOW("o/p buffersize requested = %u", portDefn->nBufferSize);
                if (portDefn->nBufferCountMin > portDefn->nBufferCountActual)
                {
                    DEBUG_PRINT_ERROR("ERROR: (Out_PORT) Min buffers (%u) > actual count (%u)",
                            portDefn->nBufferCountMin, portDefn->nBufferCountActual);
                    RETURN(OMX_ErrorUnsupportedSetting);
                }

                /* set the output bit-rate */
                Ret = swvenc_set_bit_rate(portDefn->format.video.nBitrate);
                if (Ret != SWVENC_S_SUCCESS)
                {
                   DEBUG_PRINT_ERROR("%s, swvenc_set_bit_rate failed (%d)",
                     __FUNCTION__, Ret);
                   RETURN(OMX_ErrorUnsupportedSetting);
                }

                DEBUG_PRINT_LOW("o/p previous actual cnt = %u", m_sOutPortDef.nBufferCountActual);
                DEBUG_PRINT_LOW("o/p previous min cnt = %u", m_sOutPortDef.nBufferCountMin);
                DEBUG_PRINT_LOW("o/p previous buffersize = %u", m_sOutPortDef.nBufferSize);

                /* set the buffer requirement */
                bResult = dev_set_buf_req(&portDefn->nBufferCountMin,
                  &portDefn->nBufferCountActual,
                  &portDefn->nBufferSize,
                  portDefn->nPortIndex);
                if (bResult != true)
                {
                   DEBUG_PRINT_ERROR("%s, dev_set_buf_req failed",
                     __FUNCTION__);
                   RETURN(OMX_ErrorUnsupportedSetting);
                }
                memcpy(&m_sOutPortDef, portDefn,sizeof(OMX_PARAM_PORTDEFINITIONTYPE));

                /* update the output buffer requirement */
                Ret = swvenc_get_buffer_req(&m_sOutPortDef.nBufferCountMin,
                        &m_sOutPortDef.nBufferCountActual,
                        &m_sOutPortDef.nBufferSize,
                        &m_sOutPortDef.nBufferAlignment,
                        portDefn->nPortIndex);
                if (Ret != SWVENC_S_SUCCESS)
                {
                   DEBUG_PRINT_ERROR("ERROR: %s, swvenc_get_buffer_req failed (%d)", __FUNCTION__,
                      Ret);
                   RETURN(OMX_ErrorUndefined);
                }

                if (portDefn->nBufferCountActual > m_sOutPortDef.nBufferCountActual)
                {
                   m_sOutPortDef.nBufferCountActual = portDefn->nBufferCountActual;
                }
                if (portDefn->nBufferSize > m_sOutPortDef.nBufferSize)
                {
                   m_sOutPortDef.nBufferSize = portDefn->nBufferSize;
                }

                DEBUG_PRINT_LOW("o/p new actual cnt = %u", m_sOutPortDef.nBufferCountActual);
                DEBUG_PRINT_LOW("o/p new min cnt = %u", m_sOutPortDef.nBufferCountMin);
                DEBUG_PRINT_LOW("o/p new buffersize = %u", m_sOutPortDef.nBufferSize);
            }
            else
            {
                DEBUG_PRINT_ERROR("ERROR: Set_parameter: Bad Port idx %d",
                        (int)portDefn->nPortIndex);
                eRet = OMX_ErrorBadPortIndex;
            }
            m_sConfigFramerate.xEncodeFramerate = portDefn->format.video.xFramerate;
            m_sConfigBitrate.nEncodeBitrate = portDefn->format.video.nBitrate;
            m_sParamBitrate.nTargetBitrate = portDefn->format.video.nBitrate;
            break;
        }

        case OMX_IndexParamVideoPortFormat:
        {
            OMX_VIDEO_PARAM_PORTFORMATTYPE *portFmt =
                (OMX_VIDEO_PARAM_PORTFORMATTYPE *)paramData;
            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoPortFormat %d",
                    portFmt->eColorFormat);
            SWVENC_COLOR_FORMAT color_format;

            /* set the driver with the corresponding values */
            if (PORT_INDEX_IN == portFmt->nPortIndex)
            {
                if (portFmt->eColorFormat ==
                    ((OMX_COLOR_FORMATTYPE) QOMX_COLOR_FormatAndroidOpaque))
                {
                    /* meta_mode = 2 (kMetadataBufferTypeGrallocSource) */
                    m_sInPortFormat.eColorFormat =
                        (OMX_COLOR_FORMATTYPE) QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m;
                    color_format = SWVENC_COLOR_FORMAT_NV12;
                    if (!mUseProxyColorFormat)
                    {
                       if (!c2d_conv.init())
                       {
                          DEBUG_PRINT_ERROR("C2D init failed");
                          return OMX_ErrorUnsupportedSetting;
                       }
                       DEBUG_PRINT_ERROR("C2D init is successful");
                    }
                    mUseProxyColorFormat = true;
                    m_input_msg_id = OMX_COMPONENT_GENERATE_ETB_OPQ;
                }
                else
                {
                    m_sInPortFormat.eColorFormat = portFmt->eColorFormat;
                    if ((portFmt->eColorFormat == OMX_COLOR_FormatYUV420SemiPlanar) ||
                        (portFmt->eColorFormat ==
                         ((OMX_COLOR_FORMATTYPE) QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m)))
                    {
                        color_format = SWVENC_COLOR_FORMAT_NV12;
                    }
                    else if (portFmt->eColorFormat ==
                             ((OMX_COLOR_FORMATTYPE) QOMX_COLOR_FormatYVU420SemiPlanar))
                    {
                        color_format = SWVENC_COLOR_FORMAT_NV21;
                    }
                    else
                    {
                        DEBUG_PRINT_ERROR("%s: OMX_IndexParamVideoPortFormat %d invalid",
                                          __FUNCTION__,
                                          portFmt->eColorFormat);
                        RETURN(OMX_ErrorBadParameter);
                    }
                    m_input_msg_id = OMX_COMPONENT_GENERATE_ETB;
                    mUseProxyColorFormat = false;
                }
                m_sInPortDef.format.video.eColorFormat = m_sInPortFormat.eColorFormat;
                /* set the input color format */
                Prop.id = SWVENC_PROPERTY_ID_COLOR_FORMAT;
                Prop.info.color_format = color_format;
                Ret = swvenc_setproperty(m_hSwVenc, &Prop);
                if (Ret != SWVENC_S_SUCCESS)
                {
                   DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
                     __FUNCTION__, Ret);
                   RETURN(OMX_ErrorUnsupportedSetting);
                }

                /* set the input frame-rate */
                if (portFmt->xFramerate != 0)
                {
                   Ret = swvenc_set_frame_rate(portFmt->xFramerate >> 16);
                   if (Ret != SWVENC_S_SUCCESS)
                   {
                      DEBUG_PRINT_ERROR("%s, swvenc_set_frame_rate failed (%d)",
                        __FUNCTION__, Ret);
                      //RETURN(OMX_ErrorUnsupportedSetting);
                   }
                   m_sInPortFormat.xFramerate = portFmt->xFramerate;
                }
            }
            break;
        }

        case OMX_IndexParamVideoInit:
        {
            OMX_PORT_PARAM_TYPE* pParam = (OMX_PORT_PARAM_TYPE*)(paramData);
            DEBUG_PRINT_LOW("Set OMX_IndexParamVideoInit called");
            break;
        }

        case OMX_IndexParamVideoBitrate:
        {
            OMX_VIDEO_PARAM_BITRATETYPE* pParam = (OMX_VIDEO_PARAM_BITRATETYPE*)paramData;
            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoBitrate");

            if (m_max_allowed_bitrate_check)
            {
               //TBD: to add bitrate check
            }

            /* set the output bit-rate */
            Ret = swvenc_set_bit_rate(pParam->nTargetBitrate);
            if (Ret != SWVENC_S_SUCCESS)
            {
               DEBUG_PRINT_ERROR("%s, swvenc_set_bit_rate failed (%d)",
                 __FUNCTION__, Ret);
               RETURN(OMX_ErrorUnsupportedSetting);
            }

            /* set the RC-mode */
            Ret = swvenc_set_rc_mode(pParam->eControlRate);
            if (Ret != SWVENC_S_SUCCESS)
            {
               DEBUG_PRINT_ERROR("%s, swvenc_set_rc_mode failed (%d)",
                 __FUNCTION__, Ret);
               RETURN(OMX_ErrorUnsupportedSetting);
            }

            m_sParamBitrate.nTargetBitrate = pParam->nTargetBitrate;
            m_sParamBitrate.eControlRate = pParam->eControlRate;
            m_sConfigBitrate.nEncodeBitrate = pParam->nTargetBitrate;
            m_sInPortDef.format.video.nBitrate = pParam->nTargetBitrate;
            m_sOutPortDef.format.video.nBitrate = pParam->nTargetBitrate;
            DEBUG_PRINT_LOW("bitrate = %u", m_sOutPortDef.format.video.nBitrate);
            break;
        }

        case OMX_IndexParamVideoMpeg4:
        {
            OMX_VIDEO_PARAM_MPEG4TYPE* pParam = (OMX_VIDEO_PARAM_MPEG4TYPE*)paramData;

            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoMpeg4");

            if (pParam->nBFrames)
            {
                DEBUG_PRINT_ERROR("Warning: B frames not supported");
            }

            /* set the intra period */
            Ret = swvenc_set_intra_period(pParam->nPFrames,pParam->nBFrames);
            if (Ret != SWVENC_S_SUCCESS)
            {
               DEBUG_PRINT_ERROR("%s, swvenc_set_intra_period failed (%d)",
                 __FUNCTION__, Ret);
               RETURN(OMX_ErrorUnsupportedSetting);
            }

            memcpy(&m_sParamMPEG4,pParam, sizeof(struct OMX_VIDEO_PARAM_MPEG4TYPE));
            m_sIntraperiod.nPFrames = m_sParamMPEG4.nPFrames;
            m_sIntraperiod.nBFrames = m_sParamMPEG4.nBFrames;
            break;
        }

        case OMX_IndexParamVideoH263:
        {
            OMX_VIDEO_PARAM_H263TYPE* pParam = (OMX_VIDEO_PARAM_H263TYPE*)paramData;

            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoH263");

            /* set the intra period */
            Ret = swvenc_set_intra_period(pParam->nPFrames,pParam->nBFrames);
            if (Ret != SWVENC_S_SUCCESS)
            {
               DEBUG_PRINT_ERROR("%s, swvenc_set_intra_period failed (%d)",
                 __FUNCTION__, Ret);
               RETURN(OMX_ErrorUnsupportedSetting);
            }

            memcpy(&m_sParamH263,pParam, sizeof(struct OMX_VIDEO_PARAM_H263TYPE));
            m_sIntraperiod.nPFrames = m_sParamH263.nPFrames;
            m_sIntraperiod.nBFrames = m_sParamH263.nBFrames;
            break;
        }

        case OMX_IndexParamVideoProfileLevelCurrent:
        {
            OMX_VIDEO_PARAM_PROFILELEVELTYPE* pParam = (OMX_VIDEO_PARAM_PROFILELEVELTYPE*)paramData;

            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoProfileLevelCurrent");

            /* set the profile and level */
            Ret = swvenc_set_profile_level(pParam->eProfile,pParam->eLevel);
            if (Ret != SWVENC_S_SUCCESS)
            {
               DEBUG_PRINT_ERROR("%s, swvenc_set_rc_mode failed (%d)",
                 __FUNCTION__, Ret);
               RETURN(OMX_ErrorUnsupportedSetting);
            }


            m_sParamProfileLevel.eProfile = pParam->eProfile;
            m_sParamProfileLevel.eLevel = pParam->eLevel;

            if (SWVENC_CODEC_MPEG4 == m_codec)
            {
                m_sParamMPEG4.eProfile = (OMX_VIDEO_MPEG4PROFILETYPE)m_sParamProfileLevel.eProfile;
                m_sParamMPEG4.eLevel = (OMX_VIDEO_MPEG4LEVELTYPE)m_sParamProfileLevel.eLevel;
                DEBUG_PRINT_LOW("MPEG4 profile = %d, level = %d", m_sParamMPEG4.eProfile,
                        m_sParamMPEG4.eLevel);
            }
            else if (SWVENC_CODEC_H263 == m_codec)
            {
                m_sParamH263.eProfile = (OMX_VIDEO_H263PROFILETYPE)m_sParamProfileLevel.eProfile;
                m_sParamH263.eLevel = (OMX_VIDEO_H263LEVELTYPE)m_sParamProfileLevel.eLevel;
                DEBUG_PRINT_LOW("H263 profile = %d, level = %d", m_sParamH263.eProfile,
                        m_sParamH263.eLevel);
            }
            break;
        }

        case OMX_IndexParamStandardComponentRole:
        {
            OMX_PARAM_COMPONENTROLETYPE *comp_role;
            comp_role = (OMX_PARAM_COMPONENTROLETYPE *) paramData;
            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamStandardComponentRole %s",
                    comp_role->cRole);

            if ((m_state == OMX_StateLoaded)&&
                    !BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
            {
                DEBUG_PRINT_LOW("Set Parameter called in valid state");
            }
            else
            {
                DEBUG_PRINT_ERROR("Set Parameter called in Invalid State");
                RETURN(OMX_ErrorIncorrectStateOperation);
            }

            if (SWVENC_CODEC_MPEG4 == m_codec)
            {
                if (!strncmp((const char*)comp_role->cRole,"video_encoder.mpeg4",OMX_MAX_STRINGNAME_SIZE))
                {
                    strlcpy((char*)m_cRole,"video_encoder.mpeg4",OMX_MAX_STRINGNAME_SIZE);
                }
                else
                {
                    DEBUG_PRINT_ERROR("ERROR: Setparameter: unknown Index %s", comp_role->cRole);
                    eRet = OMX_ErrorUnsupportedSetting;
                }
            }
            else if (SWVENC_CODEC_H263 == m_codec)
            {
                if (!strncmp((const char*)comp_role->cRole,"video_encoder.h263",OMX_MAX_STRINGNAME_SIZE))
                {
                    strlcpy((char*)m_cRole,"video_encoder.h263",OMX_MAX_STRINGNAME_SIZE);
                }
                else
                {
                    DEBUG_PRINT_ERROR("ERROR: Setparameter: unknown Index %s", comp_role->cRole);
                    eRet =OMX_ErrorUnsupportedSetting;
                }
            }
            else
            {
                DEBUG_PRINT_ERROR("ERROR: Setparameter: unknown param %s", m_nkind);
                eRet = OMX_ErrorInvalidComponentName;
            }
            break;
        }

        case OMX_IndexParamPriorityMgmt:
        {
            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamPriorityMgmt");
            if (m_state != OMX_StateLoaded) {
                DEBUG_PRINT_ERROR("ERROR: Set Parameter called in Invalid State");
                RETURN(OMX_ErrorIncorrectStateOperation);
            }
            OMX_PRIORITYMGMTTYPE *priorityMgmtype = (OMX_PRIORITYMGMTTYPE*) paramData;
            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamPriorityMgmt %u",
                    priorityMgmtype->nGroupID);

            DEBUG_PRINT_LOW("set_parameter: priorityMgmtype %u",
                    priorityMgmtype->nGroupPriority);

            m_sPriorityMgmt.nGroupID = priorityMgmtype->nGroupID;
            m_sPriorityMgmt.nGroupPriority = priorityMgmtype->nGroupPriority;

            break;
        }

        case OMX_IndexParamCompBufferSupplier:
        {
            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamCompBufferSupplier");
            OMX_PARAM_BUFFERSUPPLIERTYPE *bufferSupplierType = (OMX_PARAM_BUFFERSUPPLIERTYPE*) paramData;
            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamCompBufferSupplier %d",
                    bufferSupplierType->eBufferSupplier);
            if ( (bufferSupplierType->nPortIndex == 0) ||
                 (bufferSupplierType->nPortIndex ==1)
               )
            {
                m_sInBufSupplier.eBufferSupplier = bufferSupplierType->eBufferSupplier;
            }
            else
            {
                eRet = OMX_ErrorBadPortIndex;
            }

            break;

        }

        case OMX_IndexParamVideoQuantization:
        {
            // this is applicable only for RC-off case
            DEBUG_PRINT_LOW("set_parameter: OMX_IndexParamVideoQuantization");
            OMX_VIDEO_PARAM_QUANTIZATIONTYPE *session_qp = (OMX_VIDEO_PARAM_QUANTIZATIONTYPE*) paramData;
            if (session_qp->nPortIndex == PORT_INDEX_OUT)
            {
                Prop.id = SWVENC_PROPERTY_ID_QP;
                Prop.info.qp.qp_i = session_qp->nQpI;
                Prop.info.qp.qp_p = session_qp->nQpP;
                Prop.info.qp.qp_b = session_qp->nQpB;

                Ret = swvenc_setproperty(m_hSwVenc, &Prop);
                if (Ret != SWVENC_S_SUCCESS)
                {
                   DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
                     __FUNCTION__, Ret);
                   RETURN(OMX_ErrorUnsupportedSetting);
                }

                m_sSessionQuantization.nQpI = session_qp->nQpI;
                m_sSessionQuantization.nQpP = session_qp->nQpP;
                m_sSessionQuantization.nQpB = session_qp->nQpB;
            }
            else
            {
                DEBUG_PRINT_ERROR("ERROR: Unsupported port Index for Session QP setting");
                eRet = OMX_ErrorBadPortIndex;
            }
            break;
        }

        case OMX_QcomIndexParamVideoQPRange:
        {
            DEBUG_PRINT_LOW("set_parameter: OMX_QcomIndexParamVideoQPRange");
            OMX_QCOM_VIDEO_PARAM_QPRANGETYPE *qp_range = (OMX_QCOM_VIDEO_PARAM_QPRANGETYPE*) paramData;
            if (qp_range->nPortIndex == PORT_INDEX_OUT)
            {
                if ( (qp_range->minQP > 255) ||
                     (qp_range->maxQP > 255)
                   )
                {
                   DEBUG_PRINT_ERROR("ERROR: Out of range QP");
                   eRet = OMX_ErrorBadParameter;
                }

                Prop.id = SWVENC_PROPERTY_ID_QP_RANGE;
                Prop.info.qp_range.min_qp_packed =
                 (qp_range->minQP << 16) | (qp_range->minQP) | (qp_range->minQP << 8);
                Prop.info.qp_range.max_qp_packed =
                 (qp_range->maxQP << 16) | (qp_range->maxQP) | (qp_range->maxQP << 8);

                Ret = swvenc_setproperty(m_hSwVenc, &Prop);
                if (Ret != SWVENC_S_SUCCESS)
                {
                   DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
                     __FUNCTION__, Ret);
                   RETURN(OMX_ErrorUnsupportedSetting);
                }

                m_sSessionQPRange.minQP= qp_range->minQP;
                m_sSessionQPRange.maxQP= qp_range->maxQP;
            }
            else
            {
                DEBUG_PRINT_ERROR("ERROR: Unsupported port Index for QP range setting");
                eRet = OMX_ErrorBadPortIndex;
            }
            break;
        }

        case OMX_QcomIndexPortDefn:
        {
            OMX_QCOM_PARAM_PORTDEFINITIONTYPE* pParam =
                (OMX_QCOM_PARAM_PORTDEFINITIONTYPE*)paramData;
            DEBUG_PRINT_LOW("set_parameter: OMX_QcomIndexPortDefn");
            if (pParam->nPortIndex == (OMX_U32)PORT_INDEX_IN)
            {
                if (pParam->nMemRegion > OMX_QCOM_MemRegionInvalid &&
                        pParam->nMemRegion < OMX_QCOM_MemRegionMax)
                {
                    m_use_input_pmem = OMX_TRUE;
                }
                else
                {
                    m_use_input_pmem = OMX_FALSE;
                }
            }
            else if (pParam->nPortIndex == (OMX_U32)PORT_INDEX_OUT)
            {
                if (pParam->nMemRegion > OMX_QCOM_MemRegionInvalid &&
                        pParam->nMemRegion < OMX_QCOM_MemRegionMax)
                {
                    m_use_output_pmem = OMX_TRUE;
                }
                else
                {
                    m_use_output_pmem = OMX_FALSE;
                }
            }
            else
            {
                DEBUG_PRINT_ERROR("ERROR: SetParameter called on unsupported Port Index for QcomPortDefn");
                RETURN(OMX_ErrorBadPortIndex);
            }
            break;
        }

        case OMX_IndexParamVideoErrorCorrection:
        {
            DEBUG_PRINT_LOW("OMX_IndexParamVideoErrorCorrection");
            OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE* pParam =
                (OMX_VIDEO_PARAM_ERRORCORRECTIONTYPE*)paramData;

            /* HEC */
            if (m_codec == SWVENC_CODEC_MPEG4)
            {
               Prop.id = SWVENC_PROPERTY_ID_MPEG4_HEC;
               Prop.info.mpeg4_hec = pParam->bEnableHEC;

               Ret = swvenc_setproperty(m_hSwVenc, &Prop);
               if (Ret != SWVENC_S_SUCCESS)
               {
                  DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
                    __FUNCTION__, Ret);
                  RETURN(OMX_ErrorUndefined);
               }

               /* Data partitioning */
               Prop.id = SWVENC_PROPERTY_ID_MPEG4_DP;
               Prop.info.mpeg4_dp = pParam->bEnableDataPartitioning;

               Ret = swvenc_setproperty(m_hSwVenc, &Prop);
               if (Ret != SWVENC_S_SUCCESS)
               {
                  DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
                    __FUNCTION__, Ret);
                  RETURN(OMX_ErrorUndefined);
               }
            }

            /* RVLC */
            if (pParam->bEnableRVLC)
            {
               DEBUG_PRINT_ERROR("%s, RVLC not support", __FUNCTION__);
            }

            /* Re-sync Marker */
            Prop.id = SWVENC_PROPERTY_ID_SLICE_CONFIG;
            if ( (m_codec != SWVENC_CODEC_H263) && (pParam->bEnableDataPartitioning) )
            {
               DEBUG_PRINT_ERROR("DataPartioning are not Supported for this codec");
               break;
            }
            if ( (m_codec != SWVENC_CODEC_H263) && (pParam->nResynchMarkerSpacing) )
            {
               Prop.info.slice_config.mode = SWVENC_SLICE_MODE_BYTE;
               Prop.info.slice_config.size = pParam->nResynchMarkerSpacing;
            }
            else if ( (SWVENC_CODEC_H263 == m_codec) && (pParam->bEnableResync) )
            {
               Prop.info.slice_config.mode = SWVENC_SLICE_MODE_GOB;
               Prop.info.slice_config.size = 0;
               Ret = swvenc_setproperty(m_hSwVenc, &Prop);
               if (Ret != SWVENC_S_SUCCESS)
               {
                  DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
                    __FUNCTION__, Ret);
                  RETURN(OMX_ErrorUndefined);
               }
            }
            else
            {
               Prop.info.slice_config.mode = SWVENC_SLICE_MODE_OFF;
               Prop.info.slice_config.size = 0;
            }

            memcpy(&m_sErrorCorrection,pParam, sizeof(m_sErrorCorrection));
            break;
        }

        case OMX_IndexParamVideoIntraRefresh:
        {
            DEBUG_PRINT_LOW("set_param:OMX_IndexParamVideoIntraRefresh");
            OMX_VIDEO_PARAM_INTRAREFRESHTYPE* pParam =
                (OMX_VIDEO_PARAM_INTRAREFRESHTYPE*)paramData;

            Ret = swvenc_set_intra_refresh(pParam);
            if (Ret != SWVENC_S_SUCCESS)
            {
               DEBUG_PRINT_ERROR("%s, swvenc_set_intra_refresh failed (%d)",
                 __FUNCTION__, Ret);
               RETURN(OMX_ErrorUnsupportedSetting);
            }

            memcpy(&m_sIntraRefresh, pParam, sizeof(m_sIntraRefresh));
            break;
        }

        case OMX_QcomIndexParamVideoMetaBufferMode:
        {
            StoreMetaDataInBuffersParams *pParam =
                (StoreMetaDataInBuffersParams*)paramData;
            DEBUG_PRINT_HIGH("set_parameter:OMX_QcomIndexParamVideoMetaBufferMode: "
                    "port_index = %u, meta_mode = %d", pParam->nPortIndex, pParam->bStoreMetaData);

            if (pParam->nPortIndex == PORT_INDEX_IN)
            {
                if (pParam->bStoreMetaData != meta_mode_enable)
                {
                    meta_mode_enable = pParam->bStoreMetaData;
                    if (!meta_mode_enable)
                    {
                        Ret = swvenc_get_buffer_req(&m_sOutPortDef.nBufferCountMin,
                                 &m_sOutPortDef.nBufferCountActual,
                                 &m_sOutPortDef.nBufferSize,
                                 &m_sOutPortDef.nBufferAlignment,
                                 m_sOutPortDef.nPortIndex);
                        if (Ret != SWVENC_S_SUCCESS)
                        {
                           DEBUG_PRINT_ERROR("ERROR: %s, swvenc_get_buffer_req failed (%d)", __FUNCTION__,
                              Ret);
                           eRet = OMX_ErrorUndefined;
                           break;
                        }
                    }
                }
            }
            else if (pParam->nPortIndex == PORT_INDEX_OUT && secure_session)
            {
                if (pParam->bStoreMetaData != meta_mode_enable)
                {
                    meta_mode_enable = pParam->bStoreMetaData;
                }
            }
            else
            {
                if (pParam->bStoreMetaData)
                {
                    DEBUG_PRINT_ERROR("set_parameter: metamode is "
                            "valid for input port only");
                    eRet = OMX_ErrorUnsupportedIndex;
                }
            }
        }
        break;

        case OMX_QcomIndexParamIndexExtraDataType:
        {
            DEBUG_PRINT_HIGH("set_parameter: OMX_QcomIndexParamIndexExtraDataType");
            QOMX_INDEXEXTRADATATYPE *pParam = (QOMX_INDEXEXTRADATATYPE *)paramData;
            OMX_U32 mask = 0;

            if (pParam->nIndex == (OMX_INDEXTYPE)OMX_ExtraDataVideoEncoderSliceInfo)
            {
                if (pParam->nPortIndex == PORT_INDEX_OUT)
                {
                    mask = VEN_EXTRADATA_SLICEINFO;

                    DEBUG_PRINT_HIGH("SliceInfo extradata %s",
                            ((pParam->bEnabled == OMX_TRUE) ? "enabled" : "disabled"));
                }
                else
                {
                    DEBUG_PRINT_ERROR("set_parameter: Slice information is "
                            "valid for output port only");
                    eRet = OMX_ErrorUnsupportedIndex;
                    break;
                }
            }
            else if (pParam->nIndex == (OMX_INDEXTYPE)OMX_ExtraDataVideoEncoderMBInfo)
            {
                if (pParam->nPortIndex == PORT_INDEX_OUT)
                {
                    mask = VEN_EXTRADATA_MBINFO;

                    DEBUG_PRINT_HIGH("MBInfo extradata %s",
                            ((pParam->bEnabled == OMX_TRUE) ? "enabled" : "disabled"));
                }
                else
                {
                    DEBUG_PRINT_ERROR("set_parameter: MB information is "
                            "valid for output port only");
                    eRet = OMX_ErrorUnsupportedIndex;
                    break;
                }
            }
            else
            {
                DEBUG_PRINT_ERROR("set_parameter: unsupported extrdata index (%x)",
                        pParam->nIndex);
                eRet = OMX_ErrorUnsupportedIndex;
                break;
            }


            if (pParam->bEnabled == OMX_TRUE)
            {
                m_sExtraData |= mask;
            }
            else
            {
                m_sExtraData &= ~mask;
            }

            #if 0
            // TBD: add setprop to swvenc once the support is added
            if (handle->venc_set_param((OMX_PTR)!!(m_sExtraData & mask),
                        (OMX_INDEXTYPE)pParam->nIndex) != true)
            {
                DEBUG_PRINT_ERROR("ERROR: Setting Extradata (%x) failed", pParam->nIndex);
                RETURN(OMX_ErrorUnsupportedSetting);
            }
            else
            #endif
            {
                m_sOutPortDef.nPortIndex = PORT_INDEX_OUT;
                bResult = dev_get_buf_req(&m_sOutPortDef.nBufferCountMin,
                        &m_sOutPortDef.nBufferCountActual,
                        &m_sOutPortDef.nBufferSize,
                        m_sOutPortDef.nPortIndex);
                if (false == bResult)
                {
                   DEBUG_PRINT_ERROR("dev_get_buf_req failed");
                   eRet = OMX_ErrorUndefined;
                   break;
                }

                DEBUG_PRINT_HIGH("updated out_buf_req: buffer cnt=%u, "
                        "count min=%u, buffer size=%u",
                        m_sOutPortDef.nBufferCountActual,
                        m_sOutPortDef.nBufferCountMin,
                        m_sOutPortDef.nBufferSize);
            }
            break;
        }

        case OMX_QcomIndexParamVideoMaxAllowedBitrateCheck:
        {
            QOMX_EXTNINDEX_PARAMTYPE* pParam =
                (QOMX_EXTNINDEX_PARAMTYPE*)paramData;
            if (pParam->nPortIndex == PORT_INDEX_OUT)
            {
                m_max_allowed_bitrate_check =
                    ((pParam->bEnable == OMX_TRUE) ? true : false);
                DEBUG_PRINT_HIGH("set_parameter: max allowed bitrate check %s",
                        ((pParam->bEnable == OMX_TRUE) ? "enabled" : "disabled"));
            }
            else
            {
                DEBUG_PRINT_ERROR("ERROR: OMX_QcomIndexParamVideoMaxAllowedBitrateCheck "
                        " called on wrong port(%u)", pParam->nPortIndex);
                RETURN(OMX_ErrorBadPortIndex);
            }
            break;
        }

        case OMX_QcomIndexEnableSliceDeliveryMode:
        {
            QOMX_EXTNINDEX_PARAMTYPE* pParam =
                (QOMX_EXTNINDEX_PARAMTYPE*)paramData;
            if (pParam->nPortIndex == PORT_INDEX_OUT)
            {
                //TBD: add setprop to swvenc once the support is added
                #if 0
                if (!handle->venc_set_param(paramData,
                            (OMX_INDEXTYPE)OMX_QcomIndexEnableSliceDeliveryMode)) {
                    DEBUG_PRINT_ERROR("ERROR: Request for setting slice delivery mode failed");
                    RETURN( OMX_ErrorUnsupportedSetting;
                }
                #endif
                {
                    DEBUG_PRINT_ERROR("ERROR: Request for setting slice delivery mode failed");
                    RETURN(OMX_ErrorUnsupportedSetting);
                }
            }
            else
            {
                DEBUG_PRINT_ERROR("ERROR: OMX_QcomIndexEnableSliceDeliveryMode "
                        "called on wrong port(%u)", pParam->nPortIndex);
                RETURN(OMX_ErrorBadPortIndex);
            }
            break;
        }

        case OMX_QcomIndexEnableH263PlusPType:
        {
            QOMX_EXTNINDEX_PARAMTYPE* pParam =
                (QOMX_EXTNINDEX_PARAMTYPE*)paramData;
            DEBUG_PRINT_LOW("OMX_QcomIndexEnableH263PlusPType");
            if (pParam->nPortIndex == PORT_INDEX_OUT)
            {
                DEBUG_PRINT_ERROR("ERROR: Request for setting PlusPType failed");
                RETURN(OMX_ErrorUnsupportedSetting);
            }
            else
            {
                DEBUG_PRINT_ERROR("ERROR: OMX_QcomIndexEnableH263PlusPType "
                        "called on wrong port(%u)", pParam->nPortIndex);
                RETURN(OMX_ErrorBadPortIndex);
            }
            break;
        }

        case OMX_QcomIndexParamPeakBitrate:
        {
            DEBUG_PRINT_ERROR("ERROR: Setting peak bitrate");
            RETURN(OMX_ErrorUnsupportedSetting);
            break;
        }

        case QOMX_IndexParamVideoInitialQp:
        {
            // TBD: applicable to RC-on case only
            DEBUG_PRINT_ERROR("ERROR: Setting Initial QP for RC-on case");
            RETURN(OMX_ErrorNone);
            break;
        }


        case OMX_QcomIndexParamSetMVSearchrange:
        {
            DEBUG_PRINT_ERROR("ERROR: Setting Searchrange");
            RETURN(OMX_ErrorUnsupportedSetting);
            break;
        }

        default:
        {
            DEBUG_PRINT_ERROR("ERROR: Setparameter: unknown param %d", paramIndex);
            eRet = OMX_ErrorUnsupportedIndex;
            break;
        }
    }

    RETURN(eRet);
}

OMX_ERRORTYPE  omx_venc::set_config
(
   OMX_IN OMX_HANDLETYPE      hComp,
   OMX_IN OMX_INDEXTYPE configIndex,
   OMX_IN OMX_PTR        configData
)
{
    ENTER_FUNC();

    SWVENC_STATUS SwStatus;

    (void)hComp;

    if (configData == NULL)
    {
        DEBUG_PRINT_ERROR("ERROR: param is null");
        RETURN(OMX_ErrorBadParameter);
    }

    if (m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("ERROR: config called in Invalid state");
        RETURN(OMX_ErrorIncorrectStateOperation);
    }

    switch ((int)configIndex)
    {
        case OMX_IndexConfigVideoBitrate:
        {
            OMX_VIDEO_CONFIG_BITRATETYPE* pParam =
                reinterpret_cast<OMX_VIDEO_CONFIG_BITRATETYPE*>(configData);
            DEBUG_PRINT_HIGH("set_config(): OMX_IndexConfigVideoBitrate (%u)", pParam->nEncodeBitrate);

            if (pParam->nPortIndex == PORT_INDEX_OUT)
            {
                SwStatus = swvenc_set_bit_rate(pParam->nEncodeBitrate);
                if (SwStatus != SWVENC_S_SUCCESS)
                {
                   DEBUG_PRINT_ERROR("%s, swvenc_set_bit_rate failed (%d)",
                     __FUNCTION__, SwStatus);
                   RETURN(OMX_ErrorUnsupportedSetting);
                }

                m_sConfigBitrate.nEncodeBitrate = pParam->nEncodeBitrate;
                m_sParamBitrate.nTargetBitrate = pParam->nEncodeBitrate;
                m_sOutPortDef.format.video.nBitrate = pParam->nEncodeBitrate;
            }
            else
            {
                DEBUG_PRINT_ERROR("ERROR: Unsupported port index: %u", pParam->nPortIndex);
                RETURN(OMX_ErrorBadPortIndex);
            }
            break;
        }
        case OMX_IndexConfigVideoFramerate:
        {
            OMX_CONFIG_FRAMERATETYPE* pParam =
                reinterpret_cast<OMX_CONFIG_FRAMERATETYPE*>(configData);
            DEBUG_PRINT_HIGH("set_config(): OMX_IndexConfigVideoFramerate (0x%x)", pParam->xEncodeFramerate);

            if (pParam->nPortIndex == PORT_INDEX_OUT)
            {
                SwStatus = swvenc_set_frame_rate(pParam->xEncodeFramerate >> 16);
                if (SwStatus != SWVENC_S_SUCCESS)
                {
                   DEBUG_PRINT_ERROR("%s, swvenc_set_frame_rate failed (%d)",
                     __FUNCTION__, SwStatus);
                   RETURN(OMX_ErrorUnsupportedSetting);
                }

                m_sConfigFramerate.xEncodeFramerate = pParam->xEncodeFramerate;
                m_sOutPortDef.format.video.xFramerate = pParam->xEncodeFramerate;
                m_sOutPortFormat.xFramerate = pParam->xEncodeFramerate;
            }
            else
            {
                DEBUG_PRINT_ERROR("ERROR: Unsupported port index: %u", pParam->nPortIndex);
                RETURN(OMX_ErrorBadPortIndex);
            }
            break;
        }
        case QOMX_IndexConfigVideoIntraperiod:
        {
            QOMX_VIDEO_INTRAPERIODTYPE* pParam =
                reinterpret_cast<QOMX_VIDEO_INTRAPERIODTYPE*>(configData);
            DEBUG_PRINT_HIGH("set_config(): QOMX_IndexConfigVideoIntraperiod");

            if (pParam->nPortIndex == PORT_INDEX_OUT)
            {
                if (pParam->nBFrames > 0)
                {
                    DEBUG_PRINT_ERROR("B frames not supported");
                    RETURN(OMX_ErrorUnsupportedSetting);
                }

                DEBUG_PRINT_HIGH("Old: P/B frames = %u/%u, New: P/B frames = %u/%u",
                        m_sIntraperiod.nPFrames, m_sIntraperiod.nBFrames,
                        pParam->nPFrames, pParam->nBFrames);
                if (m_sIntraperiod.nBFrames != pParam->nBFrames)
                {
                    DEBUG_PRINT_HIGH("Dynamically changing B-frames not supported");
                    RETURN(OMX_ErrorUnsupportedSetting);
                }

                /* set the intra period */
                SwStatus = swvenc_set_intra_period(pParam->nPFrames,pParam->nBFrames);
                if (SwStatus != SWVENC_S_SUCCESS)
                {
                   DEBUG_PRINT_ERROR("%s, swvenc_set_intra_period failed (%d)",
                     __FUNCTION__, SwStatus);
                   RETURN(OMX_ErrorUnsupportedSetting);
                }

                m_sIntraperiod.nPFrames = pParam->nPFrames;
                m_sIntraperiod.nBFrames = pParam->nBFrames;
                m_sIntraperiod.nIDRPeriod = pParam->nIDRPeriod;

                if (m_sOutPortFormat.eCompressionFormat == OMX_VIDEO_CodingMPEG4)
                {
                    m_sParamMPEG4.nPFrames = pParam->nPFrames;
                    if (m_sParamMPEG4.eProfile != OMX_VIDEO_MPEG4ProfileSimple)
                    {
                        m_sParamMPEG4.nBFrames = pParam->nBFrames;
                    }
                    else
                    {
                        m_sParamMPEG4.nBFrames = 0;
                    }
                }
                else if (m_sOutPortFormat.eCompressionFormat == OMX_VIDEO_CodingH263)
                {
                    m_sParamH263.nPFrames = pParam->nPFrames;
                }
            }
            else
            {
                DEBUG_PRINT_ERROR("ERROR: (QOMX_IndexConfigVideoIntraperiod) Unsupported port index: %u", pParam->nPortIndex);
                RETURN(OMX_ErrorBadPortIndex);
            }

            break;
        }
        case OMX_IndexConfigVideoIntraVOPRefresh:
        {
            OMX_CONFIG_INTRAREFRESHVOPTYPE* pParam =
                reinterpret_cast<OMX_CONFIG_INTRAREFRESHVOPTYPE*>(configData);
            DEBUG_PRINT_HIGH("set_config(): OMX_IndexConfigVideoIntraVOPRefresh");

            if (pParam->nPortIndex == PORT_INDEX_OUT)
            {

                SWVENC_PROPERTY Prop;

                Prop.id = SWVENC_PROPERTY_ID_IFRAME_REQUEST;

                SwStatus = swvenc_setproperty(m_hSwVenc, &Prop);
                if (SwStatus != SWVENC_S_SUCCESS)
                {
                   DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
                     __FUNCTION__, SwStatus);
                   RETURN(OMX_ErrorUnsupportedSetting);
                }

                m_sConfigIntraRefreshVOP.IntraRefreshVOP = pParam->IntraRefreshVOP;
            }
            else
            {
                DEBUG_PRINT_ERROR("ERROR: Unsupported port index: %u", pParam->nPortIndex);
                RETURN(OMX_ErrorBadPortIndex);
            }
            break;
        }
        case OMX_IndexConfigCommonRotate:
        {
            DEBUG_PRINT_ERROR("ERROR: OMX_IndexConfigCommonRotate not supported");
            RETURN(OMX_ErrorUnsupportedSetting);
            break;
        }
        default:
            DEBUG_PRINT_ERROR("ERROR: unsupported index %d", (int) configIndex);
            RETURN(OMX_ErrorUnsupportedSetting);
            break;
    }

    EXIT_FUNC();

    RETURN(OMX_ErrorNone);
}

OMX_ERRORTYPE  omx_venc::component_deinit(OMX_IN OMX_HANDLETYPE hComp)
{
    ENTER_FUNC();

    OMX_U32 i = 0;
    DEBUG_PRINT_HIGH("omx_venc(): Inside component_deinit()");

    (void)hComp;

    if (OMX_StateLoaded != m_state)
    {
        DEBUG_PRINT_ERROR("WARNING:Rxd DeInit,OMX not in LOADED state %d",
                m_state);
    }
    if (m_out_mem_ptr)
    {
        DEBUG_PRINT_LOW("Freeing the Output Memory");
        for (i=0; i< m_sOutPortDef.nBufferCountActual; i++ )
        {
            free_output_buffer (&m_out_mem_ptr[i]);
        }
        free(m_out_mem_ptr);
        m_out_mem_ptr = NULL;
    }

    /* Check if the input buffers have to be cleaned up */
    if ( m_inp_mem_ptr && !meta_mode_enable )
    {
        DEBUG_PRINT_LOW("Freeing the Input Memory");
        for (i=0; i<m_sInPortDef.nBufferCountActual; i++)
        {
            free_input_buffer (&m_inp_mem_ptr[i]);
        }

        free(m_inp_mem_ptr);
        m_inp_mem_ptr = NULL;
    }

    /* Reset counters in msg queues */
    m_ftb_q.m_size=0;
    m_cmd_q.m_size=0;
    m_etb_q.m_size=0;
    m_ftb_q.m_read = m_ftb_q.m_write =0;
    m_cmd_q.m_read = m_cmd_q.m_write =0;
    m_etb_q.m_read = m_etb_q.m_write =0;

    /* Clear the strong reference */
    DEBUG_PRINT_HIGH("Calling swvenc_deinit()");
    swvenc_deinit(m_hSwVenc);

    if (msg_thread_created) {
        msg_thread_created = false;
        msg_thread_stop = true;
        post_message(this, OMX_COMPONENT_CLOSE_MSG);
        DEBUG_PRINT_HIGH("omx_video: Waiting on Msg Thread exit");
        pthread_join(msg_thread_id,NULL);
    }
    DEBUG_PRINT_HIGH("OMX_Venc:Component Deinit");

    RETURN(OMX_ErrorNone);
}

OMX_U32 omx_venc::dev_stop(void)
{
    ENTER_FUNC();

    SWVENC_STATUS Ret;

    if (false == m_stopped)
    {
       Ret = swvenc_stop(m_hSwVenc);
       if (Ret != SWVENC_S_SUCCESS)
       {
          DEBUG_PRINT_ERROR("%s, swvenc_stop failed (%d)",
            __FUNCTION__, Ret);
          RETURN(-1);
       }
       set_format = false;
       m_stopped = true;

       /* post STOP_DONE event as start is synchronus */
       post_event (0, OMX_ErrorNone, OMX_COMPONENT_GENERATE_STOP_DONE);
    }

    RETURN(0);
}

OMX_U32 omx_venc::dev_pause(void)
{
    ENTER_FUNC();
    // nothing to be done for sw encoder

    RETURN(true);
}

OMX_U32 omx_venc::dev_resume(void)
{
    ENTER_FUNC();
    // nothing to be done for sw encoder

    RETURN(true);
}

OMX_U32 omx_venc::dev_start(void)
{
   ENTER_FUNC();
   SWVENC_STATUS Ret;
   Ret = swvenc_start(m_hSwVenc);
   if (Ret != SWVENC_S_SUCCESS)
   {
      DEBUG_PRINT_ERROR("%s, swvenc_start failed (%d)",
        __FUNCTION__, Ret);
      RETURN(-1);
   }

   m_stopped = false;

   RETURN(0);
}

OMX_U32 omx_venc::dev_flush(unsigned port)
{
   ENTER_FUNC();
   SWVENC_STATUS Ret;

   (void)port;
   Ret = swvenc_flush(m_hSwVenc);
   if (Ret != SWVENC_S_SUCCESS)
   {
      DEBUG_PRINT_ERROR("%s, swvenc_flush failed (%d)",
        __FUNCTION__, Ret);
      RETURN(-1);
   }

   RETURN(0);
}

OMX_U32 omx_venc::dev_start_done(void)
{
   ENTER_FUNC();

   /* post START_DONE event as start is synchronus */
   post_event (0, OMX_ErrorNone, OMX_COMPONENT_GENERATE_START_DONE);

   RETURN(0);
}

OMX_U32 omx_venc::dev_set_message_thread_id(pthread_t tid)
{
    ENTER_FUNC();

    // nothing to be done for sw encoder
    (void)tid;

    RETURN(true);
}

bool omx_venc::dev_handle_empty_eos_buffer(void)
{
    ENTER_FUNC();
    SWVENC_STATUS Ret;
    SWVENC_IPBUFFER ipbuffer;
    ipbuffer.p_buffer = NULL;
    ipbuffer.filled_length = 0;
    ipbuffer.flags = SWVENC_FLAG_EOS;
    Ret = swvenc_emptythisbuffer(m_hSwVenc, &ipbuffer);
    if (Ret != SWVENC_S_SUCCESS)
    {
        DEBUG_PRINT_ERROR("%s, swvenc_emptythisbuffer failed (%d)",
                __FUNCTION__, Ret);
        RETURN(false);
    }
    RETURN(true);
}

bool omx_venc::dev_use_buf(void *buf_addr,unsigned port,unsigned index)
{
    ENTER_FUNC();

    (void)buf_addr;
    (void)port;
    (void)index;

    RETURN(true);
}

bool omx_venc::dev_free_buf(void *buf_addr,unsigned port)
{
    ENTER_FUNC();

    (void)buf_addr;
    (void)port;

    RETURN(true);
}

bool omx_venc::dev_empty_buf
(
    void *buffer,
    void *pmem_data_buf,
    unsigned index,
    unsigned fd
)
{
    ENTER_FUNC();

    SWVENC_STATUS Ret;
    SWVENC_IPBUFFER ipbuffer;
    OMX_BUFFERHEADERTYPE *bufhdr = (OMX_BUFFERHEADERTYPE *)buffer;
    unsigned int size = 0, filled_length, offset = 0;
    SWVENC_COLOR_FORMAT color_format;
    SWVENC_PROPERTY prop;

    (void)pmem_data_buf;
    (void)index;

    if (meta_mode_enable)
    {
        LEGACY_CAM_METADATA_TYPE *meta_buf = NULL;
        meta_buf = (LEGACY_CAM_METADATA_TYPE *)bufhdr->pBuffer;
        if(m_sInPortDef.format.video.eColorFormat == ((OMX_COLOR_FORMATTYPE) QOMX_COLOR_FormatAndroidOpaque))
        {
            DEBUG_PRINT_LOW("dev_empty_buf: color_format is QOMX_COLOR_FormatAndroidOpaque");
            set_format = true;
        }
        if(!meta_buf)
        {
            if (!bufhdr->nFilledLen && (bufhdr->nFlags & OMX_BUFFERFLAG_EOS))
            {
                ipbuffer.p_buffer= bufhdr->pBuffer;
                ipbuffer.size = bufhdr->nAllocLen;
                ipbuffer.filled_length = bufhdr->nFilledLen;
                DEBUG_PRINT_LOW("dev_empty_buf: empty EOS buffer");
            }
            else
            {
                return false;
            }
        }
        else
        {
            if (meta_buf->buffer_type == LEGACY_CAM_SOURCE)
            {
                offset = meta_buf->meta_handle->data[1];
                size = meta_buf->meta_handle->data[2];
                if (set_format && (meta_buf->meta_handle->numFds + meta_buf->meta_handle->numInts > 5))
                {
                    m_sInPortFormat.eColorFormat = (OMX_COLOR_FORMATTYPE)meta_buf->meta_handle->data[5];
                }
                ipbuffer.p_buffer = (unsigned char *)mmap(NULL, size, PROT_READ|PROT_WRITE,MAP_SHARED, fd, offset);
                if (ipbuffer.p_buffer == MAP_FAILED)
                {
                    DEBUG_PRINT_ERROR("mmap() failed for fd %d of size %d",fd,size);
                    RETURN(OMX_ErrorBadParameter);
                }
                ipbuffer.size = size;
                ipbuffer.filled_length = size;
            }
            else if (meta_buf->buffer_type == kMetadataBufferTypeGrallocSource)
            {
                VideoGrallocMetadata *meta_buf = (VideoGrallocMetadata *)bufhdr->pBuffer;
                private_handle_t *handle = (private_handle_t *)meta_buf->pHandle;
                size = handle->size;
                if(set_format)
                {
                    DEBUG_PRINT_LOW("color format = 0x%x",handle->format);
                    if (((OMX_COLOR_FORMATTYPE)handle->format) != m_sInPortFormat.eColorFormat)
                    {
                        if(handle->format == HAL_PIXEL_FORMAT_NV12_ENCODEABLE)
                        {
                            m_sInPortFormat.eColorFormat = (OMX_COLOR_FORMATTYPE)
                                QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m;
                        }
                        else
                        {
                            DEBUG_PRINT_ERROR("%s: OMX_IndexParamVideoPortFormat 0x%x invalid",
                                              __FUNCTION__,handle->format);
                            RETURN(OMX_ErrorBadParameter);
                        }
                    }
                }
                ipbuffer.p_buffer = (unsigned char *)mmap(NULL, size, PROT_READ|PROT_WRITE,MAP_SHARED, fd, offset);
                if (ipbuffer.p_buffer == MAP_FAILED)
                {
                    DEBUG_PRINT_ERROR("mmap() failed for fd %d of size %d",fd,size);
                    RETURN(OMX_ErrorBadParameter);
                }
                ipbuffer.size = size;
                ipbuffer.filled_length = size;
            }
            else
            {
                //handles the use case for surface encode
                ipbuffer.p_buffer = bufhdr->pBuffer;
                ipbuffer.size = bufhdr->nAllocLen;
                ipbuffer.filled_length = bufhdr->nFilledLen;
            }
            if (set_format)
            {
                set_format = false;
                m_sInPortDef.format.video.eColorFormat = m_sInPortFormat.eColorFormat;
                Ret = swvenc_set_color_format(m_sInPortFormat.eColorFormat);
                if (Ret != SWVENC_S_SUCCESS)
                {
                    DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
                        __FUNCTION__, Ret);
                    RETURN(OMX_ErrorUnsupportedSetting);
                }
            }
        }
    }
    else
    {
        ipbuffer.p_buffer = bufhdr->pBuffer;
        ipbuffer.size = bufhdr->nAllocLen;
        ipbuffer.filled_length = bufhdr->nFilledLen;
    }
    ipbuffer.flags = 0;
    if (bufhdr->nFlags & OMX_BUFFERFLAG_EOS)
    {
      ipbuffer.flags |= SWVENC_FLAG_EOS;
    }
    ipbuffer.timestamp = bufhdr->nTimeStamp;
    ipbuffer.p_client_data = (unsigned char *)bufhdr;

    DEBUG_PRINT_LOW("ETB: p_buffer (%p) size (%d) filled_len (%d) flags (0x%X) timestamp (%lld) clientData (%p)",
      ipbuffer.p_buffer,
      ipbuffer.size,
      ipbuffer.filled_length,
      (unsigned int)ipbuffer.flags,
      ipbuffer.timestamp,
      ipbuffer.p_client_data);

    Ret = swvenc_emptythisbuffer(m_hSwVenc, &ipbuffer);
    if (Ret != SWVENC_S_SUCCESS)
    {
       DEBUG_PRINT_ERROR("%s, swvenc_emptythisbuffer failed (%d)",
         __FUNCTION__, Ret);
       RETURN(false);
    }

    if (m_debug.in_buffer_log)
    {
       swvenc_input_log_buffers((const char*)ipbuffer.p_buffer, ipbuffer.filled_length);
    }

    RETURN(true);
}

bool omx_venc::dev_fill_buf
(
    void *buffer,
    void *pmem_data_buf,
    unsigned index,
    unsigned fd
)
{
    ENTER_FUNC();

    SWVENC_STATUS Ret;

    SWVENC_OPBUFFER opbuffer;
    OMX_BUFFERHEADERTYPE *bufhdr = (OMX_BUFFERHEADERTYPE *)buffer;

    (void)pmem_data_buf;
    (void)index;
    (void)fd;

    opbuffer.p_buffer = bufhdr->pBuffer;
    opbuffer.size = bufhdr->nAllocLen;
    opbuffer.filled_length = bufhdr->nFilledLen;
    opbuffer.flags = bufhdr->nFlags;
    opbuffer.timestamp = bufhdr->nTimeStamp;
    opbuffer.p_client_data = (unsigned char *)bufhdr;
    opbuffer.frame_type = SWVENC_FRAME_TYPE_I;

    DEBUG_PRINT_LOW("FTB: p_buffer (%p) size (%d) filled_len (%d) flags (0x%X) timestamp (%lld) clientData (%p)",
      opbuffer.p_buffer,
      opbuffer.size,
      opbuffer.filled_length,
      opbuffer.flags,
      opbuffer.timestamp,
      opbuffer.p_client_data);

    if ( false == m_bSeqHdrRequested)
    {
      if (dev_get_seq_hdr(opbuffer.p_buffer, opbuffer.size, &opbuffer.filled_length) == 0)
      {
         bufhdr->nFilledLen = opbuffer.filled_length;
         bufhdr->nOffset = 0;
         bufhdr->nTimeStamp = 0;
         bufhdr->nFlags = OMX_BUFFERFLAG_CODECCONFIG;

         DEBUG_PRINT_LOW("sending FBD with codec config");
         m_bSeqHdrRequested = true;
         post_event ((unsigned long)bufhdr,0,OMX_COMPONENT_GENERATE_FBD);
      }
      else
      {
         DEBUG_PRINT_ERROR("ERROR: couldn't get sequence header");
         post_event(OMX_EventError,OMX_ErrorUndefined,OMX_COMPONENT_GENERATE_EVENT);
      }
    }
    else
    {
       Ret = swvenc_fillthisbuffer(m_hSwVenc, &opbuffer);
       if (Ret != SWVENC_S_SUCCESS)
       {
          DEBUG_PRINT_ERROR("%s, swvenc_fillthisbuffer failed (%d)",
            __FUNCTION__, Ret);
          RETURN(false);
       }
    }

    RETURN(true);
}

bool omx_venc::dev_get_seq_hdr
(
   void *buffer,
   unsigned size,
   unsigned *hdrlen
)
{
   ENTER_FUNC();

   SWVENC_STATUS Ret;
   SWVENC_OPBUFFER Buffer;

   Buffer.p_buffer = (unsigned char*) buffer;
   Buffer.size = size;

   Ret = swvenc_getsequenceheader(m_hSwVenc, &Buffer);
   if (Ret != SWVENC_S_SUCCESS)
   {
      DEBUG_PRINT_ERROR("%s, swvenc_flush failed (%d)",
        __FUNCTION__, Ret);
      RETURN(-1);
   }

   *hdrlen = Buffer.filled_length;

   RETURN(0);
}

bool omx_venc::dev_get_capability_ltrcount
(
   OMX_U32 *min,
   OMX_U32 *max,
   OMX_U32 *step_size
)
{
    ENTER_FUNC();

    (void)min;
    (void)max;
    (void)step_size;

    DEBUG_PRINT_ERROR("Get Capability LTR Count is not supported");

    RETURN(false);
}

bool omx_venc::dev_get_performance_level(OMX_U32 *perflevel)
{
    ENTER_FUNC();

    (void)perflevel;
    DEBUG_PRINT_ERROR("Get performance level is not supported");

    RETURN(false);
}

bool omx_venc::dev_get_vui_timing_info(OMX_U32 *enabled)
{
    ENTER_FUNC();

    (void)enabled;
    DEBUG_PRINT_ERROR("Get vui timing information is not supported");

    RETURN(false);
}

bool omx_venc::dev_get_vqzip_sei_info(OMX_U32 *enabled)
{
    ENTER_FUNC();

    (void)enabled;
    DEBUG_PRINT_ERROR("Get vqzip sei info is not supported");

    RETURN(false);
}

bool omx_venc::dev_get_peak_bitrate(OMX_U32 *peakbitrate)
{
    //TBD: store the peak bitrate in class and return here;
    ENTER_FUNC();

    (void)peakbitrate;
    DEBUG_PRINT_ERROR("Get peak bitrate is not supported");

    RETURN(false);
}

bool omx_venc::dev_get_batch_size(OMX_U32 *size)
{
    ENTER_FUNC();

    (void)size;

    DEBUG_PRINT_ERROR("Get batch size is not supported");

    RETURN(false);
}

bool omx_venc::dev_loaded_start()
{
   ENTER_FUNC();
   RETURN(true);
}

bool omx_venc::dev_loaded_stop()
{
   ENTER_FUNC();
   RETURN(true);
}

bool omx_venc::dev_loaded_start_done()
{
   ENTER_FUNC();
   RETURN(true);
}

bool omx_venc::dev_loaded_stop_done()
{
   ENTER_FUNC();
   RETURN(true);
}

bool omx_venc::dev_get_buf_req(OMX_U32 *min_buff_count,
        OMX_U32 *actual_buff_count,
        OMX_U32 *buff_size,
        OMX_U32 port)
{
   ENTER_FUNC();

   bool bRet = true;
   OMX_PARAM_PORTDEFINITIONTYPE *PortDef;

   if (PORT_INDEX_IN == port)
   {
     PortDef = &m_sInPortDef;
   }
   else if (PORT_INDEX_OUT == port)
   {
     PortDef = &m_sOutPortDef;
   }
   else
   {
     DEBUG_PRINT_ERROR("ERROR: %s, Unsupported parameter", __FUNCTION__);
     bRet = false;
   }

   if (true == bRet)
   {
      *min_buff_count = PortDef->nBufferCountMin;
      *actual_buff_count = PortDef->nBufferCountActual;
      *buff_size = PortDef->nBufferSize;
   }

   RETURN(true);
}

bool omx_venc::dev_set_buf_req
(
   OMX_U32 const *min_buff_count,
   OMX_U32 const *actual_buff_count,
   OMX_U32 const *buff_size,
   OMX_U32 port
)
{
   ENTER_FUNC();

   SWVENC_STATUS Ret;
   OMX_PARAM_PORTDEFINITIONTYPE *PortDef;

   (void)min_buff_count;
   if (PORT_INDEX_IN == port)
   {
     PortDef = &m_sInPortDef;
   }
   else if (PORT_INDEX_OUT == port)
   {
     PortDef = &m_sOutPortDef;
   }
   else
   {
     DEBUG_PRINT_ERROR("ERROR: %s, Unsupported parameter", __FUNCTION__);
     RETURN(false);
   }

   if (*actual_buff_count < PortDef->nBufferCountMin)
   {
      DEBUG_PRINT_ERROR("ERROR: %s, (actual,min) buffer count (%d, %d)",
         __FUNCTION__, *actual_buff_count, PortDef->nBufferCountMin);
      RETURN(false);
   }
   if (false == meta_mode_enable)
   {
      if (*buff_size < PortDef->nBufferSize)
      {
          DEBUG_PRINT_ERROR("ERROR: %s, (new,old) buffer count (%d, %d)",
             __FUNCTION__, *actual_buff_count, PortDef->nBufferCountMin);
          RETURN(false);
      }
   }

   RETURN(true);
}

bool omx_venc::dev_is_video_session_supported(OMX_U32 width, OMX_U32 height)
{
   ENTER_FUNC();

   if ( (width * height < m_capability.min_width *  m_capability.min_height) ||
        (width * height > m_capability.max_width *  m_capability.max_height)
      )
   {
       DEBUG_PRINT_ERROR(
         "Unsupported Resolution WxH = (%u)x(%u) Supported Range = min (%d)x(%d) - max (%d)x(%d)",
         width, height,
         m_capability.min_width, m_capability.min_height,
         m_capability.max_width, m_capability.max_height);
       RETURN(false);
   }

   RETURN(true);
}

bool omx_venc::dev_buffer_ready_to_queue(OMX_BUFFERHEADERTYPE *buffer)
{
   ENTER_FUNC();

   (void)buffer;
   RETURN(true);
}
int omx_venc::dev_handle_output_extradata(void *buffer, int fd)
{
   ENTER_FUNC();

   (void)buffer;
   (void)fd;

   RETURN(true);
}

int omx_venc::dev_handle_input_extradata(void *buffer, int fd, int index)
{
   ENTER_FUNC();

   (void)buffer;
   (void)fd;
   (void)index;

   RETURN(true);
}

void omx_venc::dev_set_extradata_cookie(void *buffer)
{
   ENTER_FUNC();

   (void)buffer;
}

int omx_venc::dev_set_format(int color)
{
   ENTER_FUNC();

   (void)color;

   RETURN(true);
    //return handle->venc_set_format(color);
}

bool omx_venc::dev_get_dimensions(OMX_U32 index, OMX_U32 *width, OMX_U32 *height)
{
   ENTER_FUNC();

   (void)index;
   (void)width;
   (void)height;

   RETURN(true);
}

bool omx_venc::dev_color_align(OMX_BUFFERHEADERTYPE *buffer,
                OMX_U32 width, OMX_U32 height)
{
    ENTER_FUNC();

    if(secure_session) {
        DEBUG_PRINT_ERROR("Cannot align colors in secure session.");
        RETURN(OMX_FALSE);
    }
    return swvenc_color_align(buffer, width,height);
}

bool omx_venc::is_secure_session()
{
    ENTER_FUNC();

    RETURN(secure_session);
}

bool omx_venc::dev_get_output_log_flag()
{
    ENTER_FUNC();

    RETURN(m_debug.out_buffer_log == 1);
}

int omx_venc::dev_output_log_buffers(const char *buffer, int bufferlen, uint64_t ts)
{
    (void) ts;
    ENTER_FUNC();

    if (m_debug.out_buffer_log && !m_debug.outfile)
    {
        int size = 0;
        int width = m_sInPortDef.format.video.nFrameWidth;
        int height = m_sInPortDef.format.video.nFrameHeight;
        if(SWVENC_CODEC_MPEG4 == m_codec)
        {
           size = snprintf(m_debug.outfile_name, PROPERTY_VALUE_MAX,
              "%s/output_enc_%d_%d_%p.m4v",
              m_debug.log_loc, width, height, this);
        }
        else if(SWVENC_CODEC_H263 == m_codec)
        {
           size = snprintf(m_debug.outfile_name, PROPERTY_VALUE_MAX,
              "%s/output_enc_%d_%d_%p.263",
              m_debug.log_loc, width, height, this);
        }
        if ((size > PROPERTY_VALUE_MAX) || (size < 0))
        {
           DEBUG_PRINT_ERROR("Failed to open output file: %s for logging as size:%d",
                              m_debug.outfile_name, size);
           RETURN(-1);
        }
        DEBUG_PRINT_LOW("output filename = %s", m_debug.outfile_name);
        m_debug.outfile = fopen(m_debug.outfile_name, "ab");
        if (!m_debug.outfile)
        {
           DEBUG_PRINT_ERROR("Failed to open output file: %s for logging errno:%d",
                             m_debug.outfile_name, errno);
           m_debug.outfile_name[0] = '\0';
           RETURN(-1);
        }
    }
    if (m_debug.outfile && buffer && bufferlen)
    {
        DEBUG_PRINT_LOW("%s buffer length: %d", __func__, bufferlen);
        fwrite(buffer, bufferlen, 1, m_debug.outfile);
    }

    RETURN(0);
}

int omx_venc::swvenc_input_log_buffers(const char *buffer, int bufferlen)
{
   int width = m_sInPortDef.format.video.nFrameWidth;
   int height = m_sInPortDef.format.video.nFrameHeight;
   int stride = VENUS_Y_STRIDE(COLOR_FMT_NV12, width);
   int scanlines = VENUS_Y_SCANLINES(COLOR_FMT_NV12, height);
   char *temp = (char*)buffer;

   if (!m_debug.infile)
   {
       int size = snprintf(m_debug.infile_name, PROPERTY_VALUE_MAX,
                      "%s/input_enc_%d_%d_%p.yuv",
                      m_debug.log_loc, width, height, this);
       if ((size > PROPERTY_VALUE_MAX) || (size < 0))
       {
           DEBUG_PRINT_ERROR("Failed to open input file: %s for logging size:%d",
                              m_debug.infile_name, size);
           RETURN(-1);
       }
       DEBUG_PRINT_LOW("input filename = %s", m_debug.infile_name);
       m_debug.infile = fopen (m_debug.infile_name, "ab");
       if (!m_debug.infile)
       {
           DEBUG_PRINT_HIGH("Failed to open input file: %s for logging",
              m_debug.infile_name);
           m_debug.infile_name[0] = '\0';
           RETURN(-1);
       }
   }
   if (m_debug.infile && buffer && bufferlen)
   {
       DEBUG_PRINT_LOW("%s buffer length: %d", __func__, bufferlen);
       for (int i = 0; i < height; i++)
       {
          fwrite(temp, width, 1, m_debug.infile);
          temp += stride;
       }
       temp = (char*)(buffer + (stride * scanlines));
       for(int i = 0; i < height/2; i++)
       {
          fwrite(temp, width, 1, m_debug.infile);
          temp += stride;
      }
   }

   RETURN(0);
}

int omx_venc::dev_extradata_log_buffers(char *buffer)
{
   ENTER_FUNC();

   (void)buffer;

   RETURN(true);
    //return handle->venc_extradata_log_buffers(buffer);
}

SWVENC_STATUS omx_venc::swvenc_get_buffer_req
(
   OMX_U32 *min_buff_count,
   OMX_U32 *actual_buff_count,
   OMX_U32 *buff_size,
   OMX_U32 *buff_alignment,
   OMX_U32 port
)
{
    ENTER_FUNC();

    SWVENC_PROPERTY Prop;
    SWVENC_STATUS Ret;
    OMX_PARAM_PORTDEFINITIONTYPE *PortDef;

    Prop.id = SWVENC_PROPERTY_ID_BUFFER_REQ;
    if (PORT_INDEX_IN == port)
    {
      Prop.info.buffer_req.type = SWVENC_BUFFER_INPUT;
    }
    else if (PORT_INDEX_OUT == port)
    {
      Prop.info.buffer_req.type = SWVENC_BUFFER_OUTPUT;
    }
    else
    {
      DEBUG_PRINT_ERROR("ERROR: %s, Unsupported parameter", __FUNCTION__);
      RETURN(SWVENC_S_INVALID_PARAMETERS);
    }

    Ret = swvenc_getproperty(m_hSwVenc, &Prop);
    if (Ret != SWVENC_S_SUCCESS)
    {
       DEBUG_PRINT_ERROR("ERROR: %s, swvenc_setproperty failed (%d)", __FUNCTION__,
          Ret);
       RETURN(SWVENC_S_INVALID_PARAMETERS);
    }

    *buff_size = Prop.info.buffer_req.size;
    *min_buff_count = Prop.info.buffer_req.mincount;
    *actual_buff_count = Prop.info.buffer_req.mincount;
    *buff_alignment = Prop.info.buffer_req.alignment;

    RETURN(Ret);
}

SWVENC_STATUS omx_venc::swvenc_empty_buffer_done_cb
(
    SWVENC_HANDLE    swvenc,
    SWVENC_IPBUFFER *p_ipbuffer,
    void            *p_client
)
{
    ENTER_FUNC();

    (void)swvenc;
    SWVENC_STATUS eRet = SWVENC_S_SUCCESS;
    omx_venc *omx = reinterpret_cast<omx_venc*>(p_client);

    if (p_ipbuffer == NULL)
    {
        eRet = SWVENC_S_FAILURE;
    }
    else
    {
        omx->swvenc_empty_buffer_done(p_ipbuffer);
    }
    return eRet;
}

SWVENC_STATUS omx_venc::swvenc_empty_buffer_done
(
    SWVENC_IPBUFFER *p_ipbuffer
)
{
    SWVENC_STATUS eRet = SWVENC_S_SUCCESS;
    OMX_ERRORTYPE error = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE* omxhdr = NULL;

    //omx_video *omx = reinterpret_cast<omx_video*>(p_client);
    omxhdr = (OMX_BUFFERHEADERTYPE*)p_ipbuffer->p_client_data;

    DEBUG_PRINT_LOW("EBD: clientData (%p)", p_ipbuffer->p_client_data);

    if ( (omxhdr == NULL) ||
         ( ((OMX_U32)(omxhdr - m_inp_mem_ptr) >m_sInPortDef.nBufferCountActual) &&
           ((OMX_U32)(omxhdr - meta_buffer_hdr) >m_sInPortDef.nBufferCountActual)
         )
       )
    {
        omxhdr = NULL;
        error = OMX_ErrorUndefined;
    }

    if (omxhdr != NULL)
    {
        // unmap the input buffer->pBuffer
        omx_release_meta_buffer(omxhdr);
#ifdef _ANDROID_ICS_
        if (meta_mode_enable)
        {
           LEGACY_CAM_METADATA_TYPE *meta_buf = NULL;
           unsigned int size = 0;
           meta_buf = (LEGACY_CAM_METADATA_TYPE *)omxhdr->pBuffer;
           if (meta_buf)
           {
              if (meta_buf->buffer_type == LEGACY_CAM_SOURCE)
              {
                  size = meta_buf->meta_handle->data[2];
              }
              else if (meta_buf->buffer_type == kMetadataBufferTypeGrallocSource)
              {
                  VideoGrallocMetadata *meta_buf = (VideoGrallocMetadata *)omxhdr->pBuffer;
                  private_handle_t *handle = (private_handle_t *)meta_buf->pHandle;
                  size = handle->size;
              }
           }
           int status = munmap(p_ipbuffer->p_buffer, size);
           DEBUG_PRINT_HIGH("Unmapped pBuffer <%p> size <%d> status <%d>", p_ipbuffer->p_buffer, size, status);
        }
#endif
        post_event ((unsigned long)omxhdr,error,OMX_COMPONENT_GENERATE_EBD);
    }

    RETURN(eRet);
}

SWVENC_STATUS omx_venc::swvenc_fill_buffer_done_cb
(
    SWVENC_HANDLE    swvenc,
    SWVENC_OPBUFFER *p_opbuffer,
    void            *p_client
)
{
    ENTER_FUNC();

    SWVENC_STATUS eRet = SWVENC_S_SUCCESS;
    OMX_ERRORTYPE error = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE* omxhdr = NULL;
    omx_video *omx = reinterpret_cast<omx_video*>(p_client);

    (void)swvenc;

    if (p_opbuffer != NULL)
    {
        omxhdr = (OMX_BUFFERHEADERTYPE*)p_opbuffer->p_client_data;
    }

    if ( (p_opbuffer != NULL) &&
         ((OMX_U32)(omxhdr - omx->m_out_mem_ptr)  < omx->m_sOutPortDef.nBufferCountActual)
       )
    {
        DEBUG_PRINT_LOW("FBD: clientData (%p) buffer (%p) filled_lengh (%d) flags (0x%x) ts (%lld)",
          p_opbuffer->p_client_data,
          p_opbuffer->p_buffer,
          p_opbuffer->filled_length,
          p_opbuffer->flags,
          p_opbuffer->timestamp);

        if (p_opbuffer->filled_length <=  omxhdr->nAllocLen)
        {
            omxhdr->pBuffer = p_opbuffer->p_buffer;
            omxhdr->nFilledLen = p_opbuffer->filled_length;
            omxhdr->nOffset = 0;
            omxhdr->nTimeStamp = p_opbuffer->timestamp;
            omxhdr->nFlags = 0;
            if (SWVENC_FRAME_TYPE_I == p_opbuffer->frame_type)
            {
               omxhdr->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;
            }
            if (SWVENC_FLAG_EOS & p_opbuffer->flags)
            {
               omxhdr->nFlags |= OMX_BUFFERFLAG_EOS;
            }
            if(omxhdr->nFilledLen)
            {
               omxhdr->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
            }
            DEBUG_PRINT_LOW("o/p flag = 0x%x", omxhdr->nFlags);

            /* Use buffer case */
            if (omx->output_use_buffer && !omx->m_use_output_pmem)
            {
                DEBUG_PRINT_LOW("memcpy() for o/p Heap UseBuffer");
                memcpy( omxhdr->pBuffer,
                        (p_opbuffer->p_buffer),
                        p_opbuffer->filled_length );
            }
        }
        else
        {
            omxhdr->nFilledLen = 0;
        }

    }
    else
    {
        omxhdr = NULL;
        error = OMX_ErrorUndefined;
    }

    omx->post_event ((unsigned long)omxhdr,error,OMX_COMPONENT_GENERATE_FBD);

    RETURN(eRet);
}

SWVENC_STATUS omx_venc::swvenc_handle_event_cb
(
    SWVENC_HANDLE swvenc,
    SWVENC_EVENT  event,
    void         *p_client
)
{
    ENTER_FUNC();

    SWVENC_STATUS eRet = SWVENC_S_SUCCESS;
    omx_video *omx = reinterpret_cast<omx_video*>(p_client);

    OMX_BUFFERHEADERTYPE* omxhdr = NULL;

    (void)swvenc;

    if (omx == NULL || p_client == NULL)
    {
        DEBUG_PRINT_ERROR("ERROR: %s invalid i/p params", __FUNCTION__);
        RETURN(SWVENC_S_NULL_POINTER);
    }

    DEBUG_PRINT_LOW("swvenc_handle_event_cb - event = %d", event);

    switch (event)
    {
        case SWVENC_EVENT_FLUSH_DONE:
        {
           DEBUG_PRINT_ERROR("SWVENC_EVENT_FLUSH_DONE input_flush_progress %d output_flush_progress %d",
            omx->input_flush_progress, omx->output_flush_progress);
           if (omx->input_flush_progress)
           {
               omx->post_event ((unsigned)NULL, SWVENC_S_SUCCESS,
                  OMX_COMPONENT_GENERATE_EVENT_INPUT_FLUSH);
           }
           if (omx->output_flush_progress)
           {
               omx->post_event ((unsigned)NULL, SWVENC_S_SUCCESS,
                  OMX_COMPONENT_GENERATE_EVENT_OUTPUT_FLUSH);
           }
           break;
        }

        case SWVENC_EVENT_FATAL_ERROR:
        {
           DEBUG_PRINT_ERROR("ERROR: SWVENC_EVENT_FATAL_ERROR");
           omx->omx_report_error();
           break;
        }

        default:
            DEBUG_PRINT_HIGH("Unknown event received : %d", event);
            break;
    }

    RETURN(eRet);
}

SWVENC_STATUS omx_venc::swvenc_set_rc_mode
(
    OMX_VIDEO_CONTROLRATETYPE eControlRate
)
{
    ENTER_FUNC();

    SWVENC_STATUS Ret = SWVENC_S_SUCCESS;
    SWVENC_RC_MODE rc_mode;
    SWVENC_PROPERTY Prop;

    switch (eControlRate)
    {
        case OMX_Video_ControlRateDisable:
            rc_mode = SWVENC_RC_MODE_NONE;
            break;
        case OMX_Video_ControlRateVariableSkipFrames:
            rc_mode = SWVENC_RC_MODE_VBR_VFR;
            break;
        case OMX_Video_ControlRateVariable:
            rc_mode = SWVENC_RC_MODE_VBR_CFR;
            break;
        case OMX_Video_ControlRateConstantSkipFrames:
            rc_mode = SWVENC_RC_MODE_CBR_VFR;
            break;
        case OMX_Video_ControlRateConstant:
            rc_mode = SWVENC_RC_MODE_CBR_CFR;
            break;
        default:
            DEBUG_PRINT_ERROR("ERROR: UNKNOWN RC MODE");
            Ret = SWVENC_S_FAILURE;
            break;
    }

    if (SWVENC_S_SUCCESS == Ret)
    {
        Prop.id = SWVENC_PROPERTY_ID_RC_MODE;
        Prop.info.rc_mode = rc_mode;
        Ret = swvenc_setproperty(m_hSwVenc, &Prop);
        if (Ret != SWVENC_S_SUCCESS)
        {
           DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
             __FUNCTION__, Ret);
           RETURN(SWVENC_S_FAILURE);
        }
    }

    RETURN(Ret);
}

SWVENC_STATUS omx_venc::swvenc_set_profile_level
(
    OMX_U32 eProfile,
    OMX_U32 eLevel
)
{
    ENTER_FUNC();

    SWVENC_STATUS Ret = SWVENC_S_SUCCESS;
    SWVENC_PROPERTY Prop;
    SWVENC_PROFILE Profile;
    SWVENC_LEVEL Level;

    /* set the profile */
    if (SWVENC_CODEC_MPEG4 == m_codec)
    {
       switch (eProfile)
       {
          case OMX_VIDEO_MPEG4ProfileSimple:
             Profile.mpeg4 = SWVENC_PROFILE_MPEG4_SIMPLE;
             break;
          case OMX_VIDEO_MPEG4ProfileAdvancedSimple:
             Profile.mpeg4 = SWVENC_PROFILE_MPEG4_ADVANCED_SIMPLE;
             break;
          default:
             DEBUG_PRINT_ERROR("ERROR: UNKNOWN PROFILE");
             Ret = SWVENC_S_FAILURE;
             break;
       }
       switch (eLevel)
       {
          case OMX_VIDEO_MPEG4Level0:
             Level.mpeg4 = SWVENC_LEVEL_MPEG4_0;
             break;
          case OMX_VIDEO_MPEG4Level0b:
             Level.mpeg4 = SWVENC_LEVEL_MPEG4_0B;
             break;
          case OMX_VIDEO_MPEG4Level1:
             Level.mpeg4 = SWVENC_LEVEL_MPEG4_1;
             break;
          case OMX_VIDEO_MPEG4Level2:
             Level.mpeg4 = SWVENC_LEVEL_MPEG4_2;
             break;
          case OMX_VIDEO_MPEG4Level3:
             Level.mpeg4 = SWVENC_LEVEL_MPEG4_3;
             break;
          case OMX_VIDEO_MPEG4Level4:
             Level.mpeg4 = SWVENC_LEVEL_MPEG4_4;
             break;
          case OMX_VIDEO_MPEG4Level4a:
             Level.mpeg4 = SWVENC_LEVEL_MPEG4_4A;
             break;
          case OMX_VIDEO_MPEG4Level5:
             Level.mpeg4 = SWVENC_LEVEL_MPEG4_5;
             break;
          default:
             DEBUG_PRINT_ERROR("ERROR: UNKNOWN LEVEL");
             Ret = SWVENC_S_FAILURE;
             break;
       }
    }
    else if (SWVENC_CODEC_H263 == m_codec)
    {
       switch (eProfile)
       {
          case OMX_VIDEO_H263ProfileBaseline:
             Profile.h263 = SWVENC_PROFILE_H263_BASELINE;
             break;
          default:
             DEBUG_PRINT_ERROR("ERROR: UNKNOWN PROFILE");
             Ret = SWVENC_S_FAILURE;
             break;
       }
       switch (eLevel)
       {
          case OMX_VIDEO_H263Level10:
             Level.h263 = SWVENC_LEVEL_H263_10;
             break;
          case OMX_VIDEO_H263Level20:
             Level.h263 = SWVENC_LEVEL_H263_20;
             break;
          case OMX_VIDEO_H263Level30:
             Level.h263 = SWVENC_LEVEL_H263_30;
             break;
          case OMX_VIDEO_H263Level40:
             Level.h263 = SWVENC_LEVEL_H263_40;
             break;
          case OMX_VIDEO_H263Level50:
             Level.h263 = SWVENC_LEVEL_H263_50;
             break;
          case OMX_VIDEO_H263Level60:
             Level.h263 = SWVENC_LEVEL_H263_60;
             break;
          case OMX_VIDEO_H263Level70:
             Level.h263 = SWVENC_LEVEL_H263_70;
             break;
          default:
             DEBUG_PRINT_ERROR("ERROR: UNKNOWN LEVEL");
             Ret = SWVENC_S_FAILURE;
             break;
       }
    }
    else
    {
      DEBUG_PRINT_ERROR("ERROR: UNSUPPORTED CODEC");
      Ret = SWVENC_S_FAILURE;
    }

    if (SWVENC_S_SUCCESS == Ret)
    {
       Prop.id = SWVENC_PROPERTY_ID_PROFILE;
       Prop.info.profile = Profile;

       /* set the profile */
       Ret = swvenc_setproperty(m_hSwVenc, &Prop);
       if (Ret != SWVENC_S_SUCCESS)
       {
          DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
            __FUNCTION__, Ret);
          RETURN(SWVENC_S_FAILURE);
       }

       /* set the level */
       Prop.id = SWVENC_PROPERTY_ID_LEVEL;
       Prop.info.level = Level;

       Ret = swvenc_setproperty(m_hSwVenc, &Prop);
       if (Ret != SWVENC_S_SUCCESS)
       {
          DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
            __FUNCTION__, Ret);
          RETURN(SWVENC_S_FAILURE);
       }
    }

    RETURN(Ret);
}

SWVENC_STATUS omx_venc::swvenc_set_intra_refresh
(
    OMX_VIDEO_PARAM_INTRAREFRESHTYPE *IntraRefresh
)
{
   ENTER_FUNC();

   SWVENC_STATUS Ret = SWVENC_S_SUCCESS;
   SWVENC_IR_CONFIG ir_config;
   SWVENC_PROPERTY Prop;

   switch (IntraRefresh->eRefreshMode)
   {
      case OMX_VIDEO_IntraRefreshCyclic:
        Prop.info.ir_config.mode = SWVENC_IR_MODE_CYCLIC;
        break;
      case OMX_VIDEO_IntraRefreshAdaptive:
         Prop.info.ir_config.mode = SWVENC_IR_MODE_ADAPTIVE;
        break;
      case OMX_VIDEO_IntraRefreshBoth:
         Prop.info.ir_config.mode = SWVENC_IR_MODE_CYCLIC_ADAPTIVE;
        break;
      case OMX_VIDEO_IntraRefreshRandom:
         Prop.info.ir_config.mode = SWVENC_IR_MODE_RANDOM;
        break;
      default:
         DEBUG_PRINT_ERROR("ERROR: UNKNOWN INTRA REFRESH MODE");
         Ret = SWVENC_S_FAILURE;
         break;
   }

   if (SWVENC_S_SUCCESS == Ret)
   {
       Prop.id = SWVENC_PROPERTY_ID_IR_CONFIG;
       Prop.info.ir_config.cir_mbs = IntraRefresh->nCirMBs;

       Ret = swvenc_setproperty(m_hSwVenc, &Prop);
       if (Ret != SWVENC_S_SUCCESS)
       {
          DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
            __FUNCTION__, Ret);
          Ret = SWVENC_S_FAILURE;
       }
   }

   RETURN(Ret);
}

SWVENC_STATUS omx_venc::swvenc_set_frame_rate
(
    OMX_U32 nFrameRate
)
{
   ENTER_FUNC();

   SWVENC_STATUS Ret = SWVENC_S_SUCCESS;
   SWVENC_PROPERTY Prop;

   Prop.id = SWVENC_PROPERTY_ID_FRAME_RATE;
   Prop.info.frame_rate = nFrameRate;

   Ret = swvenc_setproperty(m_hSwVenc, &Prop);
   if (Ret != SWVENC_S_SUCCESS)
   {
      DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
        __FUNCTION__, Ret);
      Ret = SWVENC_S_FAILURE;
   }

   RETURN(Ret);
}

SWVENC_STATUS omx_venc::swvenc_set_bit_rate
(
    OMX_U32 nTargetBitrate
)
{
   ENTER_FUNC();

   SWVENC_STATUS Ret = SWVENC_S_SUCCESS;
   SWVENC_PROPERTY Prop;

   Prop.id = SWVENC_PROPERTY_ID_TARGET_BITRATE;
   Prop.info.target_bitrate = nTargetBitrate;

   Ret = swvenc_setproperty(m_hSwVenc, &Prop);
   if (Ret != SWVENC_S_SUCCESS)
   {
      DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
        __FUNCTION__, Ret);
      Ret = SWVENC_S_FAILURE;
   }

   RETURN(Ret);
}

SWVENC_STATUS omx_venc::swvenc_set_intra_period
(
    OMX_U32 nPFrame,
    OMX_U32 nBFrame
)
{
   ENTER_FUNC();

   SWVENC_STATUS Ret = SWVENC_S_SUCCESS;
   SWVENC_PROPERTY Prop;

   Prop.id = SWVENC_PROPERTY_ID_INTRA_PERIOD;
   Prop.info.intra_period.pframes = nPFrame;
   Prop.info.intra_period.bframes = nBFrame;

   Ret = swvenc_setproperty(m_hSwVenc, &Prop);
   if (Ret != SWVENC_S_SUCCESS)
   {
      DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
        __FUNCTION__, Ret);
      Ret = SWVENC_S_FAILURE;
   }

   RETURN(Ret);
}

bool omx_venc::swvenc_color_align(OMX_BUFFERHEADERTYPE *buffer, OMX_U32 width,
                        OMX_U32 height)
{
     OMX_U32 y_stride = VENUS_Y_STRIDE(COLOR_FMT_NV12, width),
            y_scanlines = VENUS_Y_SCANLINES(COLOR_FMT_NV12, height),
            uv_stride = VENUS_UV_STRIDE(COLOR_FMT_NV12, width),
            uv_scanlines = VENUS_UV_SCANLINES(COLOR_FMT_NV12, height),
            src_chroma_offset = width * height;

    if (buffer->nAllocLen >= VENUS_BUFFER_SIZE(COLOR_FMT_NV12, width, height)) {
        OMX_U8* src_buf = buffer->pBuffer, *dst_buf = buffer->pBuffer;
        //Do chroma first, so that we can convert it in-place
        src_buf += width * height;
        dst_buf += y_stride * y_scanlines;
        for (int line = height / 2 - 1; line >= 0; --line) {
            memmove(dst_buf + line * uv_stride,
                    src_buf + line * width,
                    width);
        }

        dst_buf = src_buf = buffer->pBuffer;
        //Copy the Y next
        for (int line = height - 1; line > 0; --line) {
            memmove(dst_buf + line * y_stride,
                    src_buf + line * width,
                    width);
        }
    } else {
        DEBUG_PRINT_ERROR("Failed to align Chroma. from %u to %u : \
                Insufficient bufferLen=%u v/s Required=%u",
                (unsigned int)(width*height), (unsigned int)src_chroma_offset, (unsigned int)buffer->nAllocLen,
                VENUS_BUFFER_SIZE(COLOR_FMT_NV12, width, height));
        return false;
    }

    return true;
}

SWVENC_STATUS omx_venc::swvenc_set_color_format
(
   OMX_COLOR_FORMATTYPE color_format
)
{
    ENTER_FUNC();
    SWVENC_STATUS Ret = SWVENC_S_SUCCESS;
    SWVENC_COLOR_FORMAT swvenc_color_format;
    SWVENC_PROPERTY Prop;
    if ((color_format == OMX_COLOR_FormatYUV420SemiPlanar) ||
         (color_format == ((OMX_COLOR_FORMATTYPE) QOMX_COLOR_FORMATYUV420PackedSemiPlanar32m)))
    {
        swvenc_color_format = SWVENC_COLOR_FORMAT_NV12;
    }
    else if (color_format == ((OMX_COLOR_FORMATTYPE) QOMX_COLOR_FormatYVU420SemiPlanar))
    {
        swvenc_color_format = SWVENC_COLOR_FORMAT_NV21;
    }
    else
    {
        DEBUG_PRINT_ERROR("%s: color_format %d invalid",__FUNCTION__,color_format);
        RETURN(SWVENC_S_FAILURE);
    }
    /* set the input color format */
    Prop.id = SWVENC_PROPERTY_ID_COLOR_FORMAT;
    Prop.info.color_format = swvenc_color_format;
    Ret = swvenc_setproperty(m_hSwVenc, &Prop);
    if (Ret != SWVENC_S_SUCCESS)
    {
        DEBUG_PRINT_ERROR("%s, swvenc_setproperty failed (%d)",
            __FUNCTION__, Ret);
        Ret = SWVENC_S_FAILURE;
    }
    RETURN(Ret);
}
