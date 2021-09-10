/**
 * @copyright
 *
 *   Copyright (c) 2015-2018,2020 The Linux Foundation. All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE DISCLAIMED.
 *   IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY
 *   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *   OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *   DAMAGE.
 *
 * @file
 *
 *   omx_swvdec.cpp
 *
 * @brief
 *
 *   OMX software video decoder component source.
 */

#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <cutils/properties.h>

#include <media/hardware/HardwareAPI.h>
#include <gralloc_priv.h>

#include "OMX_QCOMExtns.h"

#include "omx_swvdec.h"

#include "swvdec_api.h"

static unsigned int split_buffer_mpeg4(unsigned int         *offset_array,
                                       OMX_BUFFERHEADERTYPE *p_buffer_hdr);

/**
 * ----------------
 * PUBLIC FUNCTIONS
 * ----------------
 */

/**
 * @brief Create & return component class instance.
 *
 * @retval Pointer to new component class instance.
 */
void *get_omx_component_factory_fn(void)
{
    return new omx_swvdec;
}

/**
 * @brief Component constructor.
 */
omx_swvdec::omx_swvdec():
    m_state(OMX_StateInvalid),
    m_status_flags(0),
    m_swvdec_codec(SWVDEC_CODEC_INVALID),
    m_swvdec_handle(NULL),
    m_swvdec_created(false),
    m_omx_video_codingtype(OMX_VIDEO_CodingUnused),
    m_omx_color_formattype(OMX_COLOR_FormatUnused),
    m_sync_frame_decoding_mode(false),
    m_android_native_buffers(false),
    m_meta_buffer_mode_disabled(false),
    m_meta_buffer_mode(false),
    m_adaptive_playback_mode(false),
    m_arbitrary_bytes_mode(false),
    m_port_reconfig_inprogress(false),
    m_dimensions_update_inprogress(false),
    m_buffer_array_ip(NULL),
    m_buffer_array_op(NULL),
    m_meta_buffer_array(NULL)
{
    // memset all member variables that are composite structures
    memset(&m_cmp,                     0, sizeof(m_cmp)); // part of base class
    memset(&m_cmp_name[0],             0, sizeof(m_cmp_name));
    memset(&m_role_name[0],            0, sizeof(m_role_name));
    memset(&m_frame_dimensions,        0, sizeof(m_frame_dimensions));
    memset(&m_frame_attributes,        0, sizeof(m_frame_attributes));
    memset(&m_frame_dimensions_max,    0, sizeof(m_frame_dimensions_max));
    memset(&m_async_thread,            0, sizeof(m_async_thread));
    memset(&m_port_ip,                 0, sizeof(m_port_ip));
    memset(&m_port_op,                 0, sizeof(m_port_op));
    memset(&m_callback,                0, sizeof(m_callback));
    memset(&m_app_data,                0, sizeof(m_app_data));
    memset(&m_prio_mgmt,               0, sizeof(m_prio_mgmt));
    memset(&m_sem_cmd,                 0, sizeof(m_sem_cmd));
    memset(&m_meta_buffer_array_mutex, 0, sizeof(m_meta_buffer_array_mutex));

    // null-terminate component name & role name strings
    m_cmp_name[0]  = '\0';
    m_role_name[0] = '\0';

    // ports are enabled & unpopulated by default
    m_port_ip.enabled     = OMX_TRUE;
    m_port_op.enabled     = OMX_TRUE;
    m_port_ip.unpopulated = OMX_TRUE;
    m_port_op.unpopulated = OMX_TRUE;
}

/**
 * @brief Component destructor.
 */
omx_swvdec::~omx_swvdec()
{
}

/**
 * @brief Initialize component.
 *
 * @param[in] cmp_name: Component name string.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::component_init(OMX_STRING cmp_name)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    OMX_SWVDEC_LOG_API("'%s', version date: %s",
                       cmp_name,
                       OMX_SWVDEC_VERSION_DATE);

    omx_swvdec_log_init();

    {
        char property_value[PROPERTY_VALUE_MAX] = {0};

        if (property_get("vendor.omx_swvdec.meta_buffer.disable",
                         property_value,
                         NULL))
        {
            m_meta_buffer_mode_disabled = (bool) atoi(property_value);

            OMX_SWVDEC_LOG_LOW("omx_swvdec.meta_buffer.disable: %d",
                               m_meta_buffer_mode_disabled ? 1 : 0);
        }
    }

    if (m_state != OMX_StateInvalid)
    {
        OMX_SWVDEC_LOG_ERROR("disallowed in state %s",
                             OMX_STATETYPE_STRING(m_state));

        retval = OMX_ErrorIncorrectStateOperation;
        goto component_init_exit;
    }

    if (!strncmp(cmp_name,
                 "OMX.qti.video.decoder.mpeg4sw",
                 OMX_MAX_STRINGNAME_SIZE))
    {
        OMX_SWVDEC_LOG_LOW("'video_decoder.mpeg4'");

        strlcpy(m_cmp_name,               cmp_name, OMX_MAX_STRINGNAME_SIZE);
        strlcpy(m_role_name, "video_decoder.mpeg4", OMX_MAX_STRINGNAME_SIZE);

        m_swvdec_codec         = SWVDEC_CODEC_MPEG4;
        m_omx_video_codingtype = OMX_VIDEO_CodingMPEG4;
    }
    else if (!strncmp(cmp_name,
                      "OMX.qti.video.decoder.h263sw",
                      OMX_MAX_STRINGNAME_SIZE))
    {
        OMX_SWVDEC_LOG_LOW("video_decoder.h263");

        strlcpy(m_cmp_name,              cmp_name, OMX_MAX_STRINGNAME_SIZE);
        strlcpy(m_role_name, "video_decoder.h263", OMX_MAX_STRINGNAME_SIZE);

        m_swvdec_codec         = SWVDEC_CODEC_H263;
        m_omx_video_codingtype = OMX_VIDEO_CodingH263;
    }
#ifdef _ANDROID_O_MR1_DIVX_CHANGES
   else if (!strncmp(cmp_name,"OMX.qti.video.decoder.divxsw",OMX_MAX_STRINGNAME_SIZE)){
        OMX_SWVDEC_LOG_LOW("video_decoder.divx");

        strlcpy(m_cmp_name,              cmp_name, OMX_MAX_STRINGNAME_SIZE);
        strlcpy(m_role_name, "video_decoder.divx", OMX_MAX_STRINGNAME_SIZE);

        m_swvdec_codec         = SWVDEC_CODEC_MPEG4;
        m_omx_video_codingtype = ((OMX_VIDEO_CODINGTYPE) QOMX_VIDEO_CodingDivx);
   }else if (!strncmp(cmp_name,"OMX.qti.video.decoder.divx4sw",OMX_MAX_STRINGNAME_SIZE)){
         OMX_SWVDEC_LOG_LOW("video_decoder.divx4");

        strlcpy(m_cmp_name,              cmp_name, OMX_MAX_STRINGNAME_SIZE);
        strlcpy(m_role_name, "video_decoder.divx4", OMX_MAX_STRINGNAME_SIZE);

        m_swvdec_codec         = SWVDEC_CODEC_MPEG4;
        m_omx_video_codingtype = ((OMX_VIDEO_CODINGTYPE) QOMX_VIDEO_CodingDivx);
   }
#else
    else if (((!strncmp(cmp_name,
                        "OMX.qti.video.decoder.divxsw",
                        OMX_MAX_STRINGNAME_SIZE))) ||
             ((!strncmp(cmp_name,
                        "OMX.qti.video.decoder.divx4sw",
                        OMX_MAX_STRINGNAME_SIZE))))
    {
        OMX_SWVDEC_LOG_LOW("video_decoder.divx");

        strlcpy(m_cmp_name,              cmp_name, OMX_MAX_STRINGNAME_SIZE);
        strlcpy(m_role_name, "video_decoder.divx", OMX_MAX_STRINGNAME_SIZE);

        m_swvdec_codec         = SWVDEC_CODEC_MPEG4;
        m_omx_video_codingtype = ((OMX_VIDEO_CODINGTYPE) QOMX_VIDEO_CodingDivx);
    }
#endif
    else
    {
        OMX_SWVDEC_LOG_ERROR("'%s': invalid component name", cmp_name);

        retval = OMX_ErrorInvalidComponentName;
        goto component_init_exit;
    }

    {
        SWVDEC_CALLBACK callback;

        SWVDEC_STATUS retval_swvdec;

        callback.pfn_empty_buffer_done  = swvdec_empty_buffer_done_callback;
        callback.pfn_fill_buffer_done   = swvdec_fill_buffer_done_callback;
        callback.pfn_event_notification = swvdec_event_handler_callback;
        callback.p_client               = this;

        if ((retval_swvdec = swvdec_init(&m_swvdec_handle,
                                         m_swvdec_codec,
                                         &callback)) !=
            SWVDEC_STATUS_SUCCESS)
        {
            retval = retval_swvdec2omx(retval_swvdec);
            goto component_init_exit;
        }

        if ((retval_swvdec = swvdec_check_inst_load(m_swvdec_handle)) !=
            SWVDEC_STATUS_SUCCESS)
        {
            retval = retval_swvdec2omx(retval_swvdec);
            goto component_init_exit;
        }
        m_swvdec_created = true;

        if ((retval = set_frame_dimensions(DEFAULT_FRAME_WIDTH,
                                           DEFAULT_FRAME_HEIGHT)) !=
            OMX_ErrorNone)
        {
            goto component_init_exit;
        }

        m_omx_color_formattype =
            ((OMX_COLOR_FORMATTYPE)
             OMX_QCOM_COLOR_FormatYUV420PackedSemiPlanar32m);

        if ((retval = set_frame_attributes(m_omx_color_formattype)) !=
            OMX_ErrorNone)
        {
            goto component_init_exit;
        }
    }

    if ((retval = get_buffer_requirements_swvdec(OMX_CORE_PORT_INDEX_IP)) !=
        OMX_ErrorNone)
    {
        goto component_init_exit;
    }

    if ((retval = get_buffer_requirements_swvdec(OMX_CORE_PORT_INDEX_OP)) !=
        OMX_ErrorNone)
    {
        goto component_init_exit;
    }

    if ((retval = async_thread_create()) != OMX_ErrorNone)
    {
        goto component_init_exit;
    }

    if (sem_init(&m_sem_cmd, 0, 0))
    {
        OMX_SWVDEC_LOG_ERROR("failed to create command processing semaphore");

        retval = OMX_ErrorInsufficientResources;
        goto component_init_exit;
    }

    if (pthread_mutex_init(&m_meta_buffer_array_mutex, NULL))
    {
        OMX_SWVDEC_LOG_ERROR("failed to create meta buffer info array mutex");

        retval = OMX_ErrorInsufficientResources;
        goto component_init_exit;
    }

    OMX_SWVDEC_LOG_HIGH("OMX_StateInvalid -> OMX_StateLoaded");

    m_state = OMX_StateLoaded;

component_init_exit:
    return retval;
}

/**
 * @brief De-initialize component.
 *
 * @param[in] cmp_handle: Component handle.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::component_deinit(OMX_HANDLETYPE cmp_handle)
{
    OMX_SWVDEC_LOG_API("");

    if (cmp_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");
    }

    pthread_mutex_destroy(&m_meta_buffer_array_mutex);

    sem_destroy(&m_sem_cmd);

    async_thread_destroy();

    if (m_swvdec_created)
    {
        swvdec_deinit(m_swvdec_handle);

        m_swvdec_handle = NULL;
    }

    OMX_SWVDEC_LOG_HIGH("all done, goodbye!");

    return OMX_ErrorNone;
}

/**
 * @brief Get component version.
 *
 * @param[in]     cmp_handle:     Component handle.
 * @param[in]     cmp_name:       Component name string.
 * @param[in,out] p_cmp_version:  Pointer to component version variable.
 * @param[in,out] p_spec_version: Pointer to OMX spec version variable.
 * @param[in,out] p_cmp_UUID:     Pointer to component UUID variable.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::get_component_version(OMX_HANDLETYPE   cmp_handle,
                                                OMX_STRING       cmp_name,
                                                OMX_VERSIONTYPE *p_cmp_version,
                                                OMX_VERSIONTYPE *p_spec_version,
                                                OMX_UUIDTYPE    *p_cmp_UUID)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    (void) p_cmp_UUID;

    OMX_SWVDEC_LOG_API("");

    if (m_state == OMX_StateInvalid)
    {
        OMX_SWVDEC_LOG_ERROR("in invalid state");

        retval = OMX_ErrorInvalidState;
    }
    else if (cmp_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");

        retval = OMX_ErrorInvalidComponent;
    }
    else if (strncmp(cmp_name, m_cmp_name, sizeof(m_cmp_name)))
    {
        OMX_SWVDEC_LOG_ERROR("'%s': invalid component name", cmp_name);

        retval = OMX_ErrorInvalidComponentName;
    }
    else if (p_cmp_version == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_cmp_version = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else if (p_spec_version == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_spec_version = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else
    {
        p_spec_version->nVersion = OMX_SPEC_VERSION;
    }

    return retval;
}

/**
 * @brief Send command to component.
 *
 * @param[in] cmp_handle: Component handle.
 * @param[in] cmd:        Command.
 * @param[in] param:      Command parameter.
 * @param[in] p_cmd_data: Pointer to command data.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::send_command(OMX_HANDLETYPE  cmp_handle,
                                       OMX_COMMANDTYPE cmd,
                                       OMX_U32         param,
                                       OMX_PTR         p_cmd_data)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    (void) p_cmd_data; // prevent warning for unused function argument

    if (m_state == OMX_StateInvalid)
    {
        OMX_SWVDEC_LOG_ERROR("in invalid state");

        retval = OMX_ErrorInvalidState;
        goto send_command_exit;
    }
    else if (cmp_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");

        retval = OMX_ErrorInvalidComponent;
        goto send_command_exit;
    }

    switch (cmd)
    {

    case OMX_CommandStateSet:
    {
        OMX_SWVDEC_LOG_API("%s, %s",
                           OMX_COMMANDTYPE_STRING(cmd),
                           OMX_STATETYPE_STRING((OMX_STATETYPE) param));
        break;
    }

    case OMX_CommandFlush:
    case OMX_CommandPortDisable:
    case OMX_CommandPortEnable:
    {
        OMX_SWVDEC_LOG_API("%s, port index %d",
                           OMX_COMMANDTYPE_STRING(cmd),
                           param);

        if ((param != OMX_CORE_PORT_INDEX_IP) &&
            (param != OMX_CORE_PORT_INDEX_OP) &&
            (param != OMX_ALL))
        {
            OMX_SWVDEC_LOG_ERROR("port index '%d' invalid", param);

            retval = OMX_ErrorBadPortIndex;
        }

        break;
    }

    default:
    {
        OMX_SWVDEC_LOG_API("cmd %d, param %d", cmd, param);

        OMX_SWVDEC_LOG_ERROR("cmd '%d' invalid", cmd);

        retval = OMX_ErrorBadParameter;
        break;
    }

    } // switch (cmd)

    if (retval == OMX_ErrorNone)
    {
        if (cmp_handle == NULL)
        {
            OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");

            retval = OMX_ErrorInvalidComponent;
        }
        else if (m_state == OMX_StateInvalid)
        {
            OMX_SWVDEC_LOG_ERROR("in invalid state");

            retval = OMX_ErrorInvalidState;
        }
    }

    if (retval != OMX_ErrorNone)
    {
        async_post_event(OMX_SWVDEC_EVENT_ERROR, retval, 0);
    }
    else
    {
        async_post_event(OMX_SWVDEC_EVENT_CMD, cmd, param);

        sem_wait(&m_sem_cmd);
    }

send_command_exit:
    return retval;
}

/**
 * @brief Get a parameter from component.
 *
 * @param[in]     cmp_handle:   Component handle.
 * @param[in]     param_index:  Parameter index.
 * @param[in,out] p_param_data: Pointer to parameter data.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::get_parameter(OMX_HANDLETYPE cmp_handle,
                                        OMX_INDEXTYPE  param_index,
                                        OMX_PTR        p_param_data)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (m_state == OMX_StateInvalid)
    {
        OMX_SWVDEC_LOG_ERROR("in invalid state");

        retval = OMX_ErrorInvalidState;
    }
    else if (cmp_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");

        retval = OMX_ErrorInvalidComponent;
    }
    else if (p_param_data == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_param_data = NULL");

        retval = OMX_ErrorBadParameter;
    }

    if (retval != OMX_ErrorNone)
    {
        goto get_parameter_exit;
    }

    switch (param_index)
    {

    case OMX_IndexParamAudioInit:
    {
        OMX_PORT_PARAM_TYPE *p_port_param =
            (OMX_PORT_PARAM_TYPE *) p_param_data;

        p_port_param->nPorts           = 0;
        p_port_param->nStartPortNumber = 0;

        OMX_SWVDEC_LOG_API("OMX_IndexParamAudioInit: "
                           "%d port(s), start port index %d",
                           p_port_param->nPorts,
                           p_port_param->nStartPortNumber);
        break;
    }

    case OMX_IndexParamImageInit:
    {
        OMX_PORT_PARAM_TYPE *p_port_param =
            (OMX_PORT_PARAM_TYPE *) p_param_data;

        p_port_param->nPorts           = 0;
        p_port_param->nStartPortNumber = 0;

        OMX_SWVDEC_LOG_API("OMX_IndexParamImageInit: "
                           "%d port(s), start port index %d",
                           p_port_param->nPorts,
                           p_port_param->nStartPortNumber);
        break;
    }

    case OMX_IndexParamVideoInit:
    {
        OMX_PORT_PARAM_TYPE *p_port_param =
            (OMX_PORT_PARAM_TYPE *) p_param_data;

        p_port_param->nPorts           = 2;
        p_port_param->nStartPortNumber = 0;

        OMX_SWVDEC_LOG_API("OMX_IndexParamVideoInit: "
                           "%d port(s), start port index %d",
                           p_port_param->nPorts,
                           p_port_param->nStartPortNumber);
        break;
    }

    case OMX_IndexParamOtherInit:
    {
        OMX_PORT_PARAM_TYPE *p_port_param =
            (OMX_PORT_PARAM_TYPE *) p_param_data;

        p_port_param->nPorts           = 0;
        p_port_param->nStartPortNumber = 0;

        OMX_SWVDEC_LOG_API("OMX_IndexParamOtherInit: "
                           "%d port(s), start port index %d",
                           p_port_param->nPorts,
                           p_port_param->nStartPortNumber);
        break;
    }

    case OMX_IndexConfigPriorityMgmt:
    {
        OMX_PRIORITYMGMTTYPE *p_prio_mgmt =
            (OMX_PRIORITYMGMTTYPE *) p_param_data;

        OMX_SWVDEC_LOG_API("OMX_IndexConfigPriorityMgmt");

        memcpy(p_prio_mgmt, &m_prio_mgmt, sizeof(OMX_PRIORITYMGMTTYPE));
        break;
    }

    case OMX_IndexParamStandardComponentRole:
    {
        OMX_PARAM_COMPONENTROLETYPE *p_cmp_role =
            (OMX_PARAM_COMPONENTROLETYPE *) p_param_data;

        strlcpy((char *) p_cmp_role->cRole,
                m_role_name,
                OMX_MAX_STRINGNAME_SIZE);

        OMX_SWVDEC_LOG_API("OMX_IndexParamStandardComponentRole: %s",
                           p_cmp_role->cRole);
        break;
    }

    case OMX_IndexParamPortDefinition:
    {
        OMX_PARAM_PORTDEFINITIONTYPE *p_port_def =
            (OMX_PARAM_PORTDEFINITIONTYPE *) p_param_data;

        OMX_SWVDEC_LOG_API("OMX_IndexParamPortDefinition, port index %d",
                           p_port_def->nPortIndex);

        retval = get_port_definition(p_port_def);
        break;
    }

    case OMX_IndexParamCompBufferSupplier:
    {
        OMX_PARAM_BUFFERSUPPLIERTYPE *p_buffer_supplier =
            (OMX_PARAM_BUFFERSUPPLIERTYPE *) p_param_data;

        OMX_SWVDEC_LOG_API("OMX_IndexParamCompBufferSupplier, port index %d",
                           p_buffer_supplier->nPortIndex);

        if ((p_buffer_supplier->nPortIndex == OMX_CORE_PORT_INDEX_IP) ||
            (p_buffer_supplier->nPortIndex == OMX_CORE_PORT_INDEX_OP))
        {
            p_buffer_supplier->eBufferSupplier = OMX_BufferSupplyUnspecified;
        }
        else
        {
            OMX_SWVDEC_LOG_ERROR("port index '%d' invalid",
                                 p_buffer_supplier->nPortIndex);

            retval = OMX_ErrorBadPortIndex;
        }

        break;
    }

    case OMX_IndexParamVideoPortFormat:
    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE *p_port_format =
            (OMX_VIDEO_PARAM_PORTFORMATTYPE *) p_param_data;

        OMX_SWVDEC_LOG_API("OMX_IndexParamVideoPortFormat, "
                           "port index %d, index %d",
                           p_port_format->nPortIndex,
                           p_port_format->nIndex);

        retval = get_video_port_format(p_port_format);
        break;
    }

    case OMX_IndexParamVideoMpeg2:
    {
        OMX_SWVDEC_LOG_ERROR("OMX_IndexParamVideoMpeg2: unsupported");

        retval = OMX_ErrorUnsupportedIndex;
        break;
    }

    case OMX_IndexParamVideoMpeg4:
    {
        OMX_SWVDEC_LOG_API("OMX_IndexParamVideoMpeg4: unsupported");

        retval = OMX_ErrorUnsupportedIndex;
        break;
    }

    case OMX_IndexParamVideoAvc:
    {
        OMX_SWVDEC_LOG_API("OMX_IndexParamVideoAvc: unsupported");

        retval = OMX_ErrorUnsupportedIndex;
        break;
    }

    case OMX_IndexParamVideoH263:
    {
        OMX_SWVDEC_LOG_API("OMX_IndexParamVideoH263: unsupported");

        retval = OMX_ErrorUnsupportedIndex;
        break;
    }

    case OMX_IndexParamVideoProfileLevelQuerySupported:
    {
        OMX_VIDEO_PARAM_PROFILELEVELTYPE *p_profilelevel =
            (OMX_VIDEO_PARAM_PROFILELEVELTYPE *) p_param_data;

        OMX_SWVDEC_LOG_API("OMX_IndexParamVideoProfileLevelQuerySupported, "
                           "port index %d, profile index %d",
                           p_profilelevel->nPortIndex,
                           p_profilelevel->nProfileIndex);

        retval = get_supported_profilelevel(p_profilelevel);
        break;
    }

    default:
    {
        /**
         * Vendor-specific extension indices checked here since they are not
         * part of the OMX_INDEXTYPE enumerated type.
         */

        switch ((OMX_QCOM_EXTN_INDEXTYPE) param_index)
        {

        case OMX_GoogleAndroidIndexGetAndroidNativeBufferUsage:
        {
            GetAndroidNativeBufferUsageParams *p_buffer_usage =
                (GetAndroidNativeBufferUsageParams *) p_param_data;

            OMX_SWVDEC_LOG_API(
                "OMX_GoogleAndroidIndexGetAndroidNativeBufferUsage, "
                "port index %d", p_buffer_usage->nPortIndex);

            if (p_buffer_usage->nPortIndex == OMX_CORE_PORT_INDEX_OP)
            {
                p_buffer_usage->nUsage = (static_cast<uint32_t>(GRALLOC_USAGE_PRIVATE_IOMMU_HEAP |
                                          GRALLOC_USAGE_SW_READ_OFTEN |
                                          GRALLOC_USAGE_SW_WRITE_OFTEN));
            }
            else
            {
                OMX_SWVDEC_LOG_ERROR("port index '%d' invalid",
                                     p_buffer_usage->nPortIndex);

                retval = OMX_ErrorBadPortIndex;
            }
            break;
        }

        case OMX_QcomIndexFlexibleYUVDescription:
        {
            OMX_SWVDEC_LOG_API("OMX_QcomIndexFlexibleYUVDescription");

            retval = describe_color_format((DescribeColorFormatParams *)
                                           p_param_data);
            break;
        }

        default:
        {
            OMX_SWVDEC_LOG_ERROR("param index '0x%08x' invalid",
                                 (OMX_QCOM_EXTN_INDEXTYPE) param_index);

            retval = OMX_ErrorBadParameter;
            break;
        }

        } // switch ((OMX_QCOM_EXTN_INDEXTYPE) param_index)

    } // default case

    } // switch (param_index)

get_parameter_exit:
    return retval;
}

/**
 * @brief Set a parameter to component.
 *
 * @param[in] cmp_handle:   Component handle.
 * @param[in] param_index:  Parameter index.
 * @param[in] p_param_data: Pointer to parameter data.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::set_parameter(OMX_HANDLETYPE cmp_handle,
                                        OMX_INDEXTYPE  param_index,
                                        OMX_PTR        p_param_data)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (m_state == OMX_StateInvalid)
    {
        OMX_SWVDEC_LOG_ERROR("in invalid state");

        retval = OMX_ErrorInvalidState;
    }
    else if (cmp_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");

        retval = OMX_ErrorInvalidComponent;
    }
    else if (p_param_data == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_param_data = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else if ((m_state != OMX_StateLoaded) &&
             (m_port_reconfig_inprogress == false))
    {
        OMX_SWVDEC_LOG_ERROR("disallowed in state %s",
                             OMX_STATETYPE_STRING(m_state));

        retval = OMX_ErrorIncorrectStateOperation;
    }

    if (retval != OMX_ErrorNone)
    {
        goto set_parameter_exit;
    }

    switch (param_index)
    {

    case OMX_IndexParamPriorityMgmt:
    {
        OMX_PRIORITYMGMTTYPE *p_prio_mgmt =
            (OMX_PRIORITYMGMTTYPE *) p_param_data;

        OMX_SWVDEC_LOG_API("OMX_IndexConfigPriorityMgmt: "
                           "group ID %d, group priority %d",
                           p_prio_mgmt->nGroupID,
                           p_prio_mgmt->nGroupPriority);

        if (m_state != OMX_StateLoaded)
        {
            OMX_SWVDEC_LOG_ERROR("'%d' state invalid; "
                                 "should be in loaded state",
                                 m_state);

            retval = OMX_ErrorIncorrectStateOperation;
        }
        else
        {
            memcpy(&m_prio_mgmt, p_prio_mgmt, sizeof(OMX_PRIORITYMGMTTYPE));
        }

        break;
    }

    case OMX_IndexParamStandardComponentRole:
    {
        OMX_PARAM_COMPONENTROLETYPE *p_cmp_role =
            (OMX_PARAM_COMPONENTROLETYPE *) p_param_data;

        OMX_SWVDEC_LOG_API("OMX_IndexParamStandardComponentRole '%s'",
                           p_cmp_role->cRole);

        if (m_state != OMX_StateLoaded)
        {
            OMX_SWVDEC_LOG_ERROR("'%d' state invalid; "
                                 "should be in loaded state",
                                 m_state);

            retval = OMX_ErrorIncorrectStateOperation;
        }
        else
        {
            if (strncmp((char *) p_cmp_role->cRole,
                        m_role_name,
                        OMX_MAX_STRINGNAME_SIZE))
            {
                OMX_SWVDEC_LOG_ERROR("'%s': invalid component role name",
                                     p_cmp_role->cRole);

                retval = OMX_ErrorBadParameter;
            }
        }

        break;
    }

    case OMX_IndexParamPortDefinition:
    {
        OMX_PARAM_PORTDEFINITIONTYPE *p_port_def =
            (OMX_PARAM_PORTDEFINITIONTYPE *) p_param_data;

        OMX_SWVDEC_LOG_API("OMX_IndexParamPortDefinition, port index %d",
                           p_port_def->nPortIndex);

        if ((m_state != OMX_StateLoaded) &&
            (((p_port_def->nPortIndex == OMX_CORE_PORT_INDEX_IP) &&
              (m_port_ip.enabled      == OMX_TRUE) &&
              (m_port_ip.populated    == OMX_TRUE)) ||
             ((p_port_def->nPortIndex == OMX_CORE_PORT_INDEX_OP) &&
              (m_port_op.enabled      == OMX_TRUE) &&
              (m_port_op.populated    == OMX_TRUE))))
        {
            OMX_SWVDEC_LOG_ERROR("OMX_IndexParamPortDefinition "
                                 "disallowed in state %s "
                                 "while port index %d is enabled & populated",
                                 OMX_STATETYPE_STRING(m_state),
                                 p_port_def->nPortIndex);

            retval = OMX_ErrorIncorrectStateOperation;
        }
        else
        {
            retval = set_port_definition(p_port_def);
        }

        break;
    }

    case OMX_IndexParamCompBufferSupplier:
    {
        OMX_PARAM_BUFFERSUPPLIERTYPE *p_buffer_supplier =
            (OMX_PARAM_BUFFERSUPPLIERTYPE *) p_param_data;

        OMX_SWVDEC_LOG_API("OMX_IndexParamCompBufferSupplier: "
                           "port index %d, buffer supplier %d",
                           p_buffer_supplier->nPortIndex,
                           (int) p_buffer_supplier->eBufferSupplier);

        if ((p_buffer_supplier->nPortIndex != OMX_CORE_PORT_INDEX_IP) &&
            (p_buffer_supplier->nPortIndex != OMX_CORE_PORT_INDEX_OP))
        {
            OMX_SWVDEC_LOG_ERROR("port index '%d' invalid",
                                 p_buffer_supplier->nPortIndex);

            retval = OMX_ErrorBadPortIndex;
        }

        break;
    }

    case OMX_IndexParamVideoPortFormat:
    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE *p_port_format =
            (OMX_VIDEO_PARAM_PORTFORMATTYPE *) p_param_data;

        OMX_SWVDEC_LOG_API("OMX_IndexParamVideoPortFormat, port index %d",
                           p_port_format->nPortIndex);

        if ((m_state != OMX_StateLoaded) &&
            (((p_port_format->nPortIndex == OMX_CORE_PORT_INDEX_IP) &&
              (m_port_ip.enabled         == OMX_TRUE) &&
              (m_port_ip.populated       == OMX_TRUE)) ||
             ((p_port_format->nPortIndex == OMX_CORE_PORT_INDEX_OP) &&
              (m_port_op.enabled         == OMX_TRUE) &&
              (m_port_op.populated       == OMX_TRUE))))
        {
            OMX_SWVDEC_LOG_ERROR("OMX_IndexParamVideoPortFormat "
                                 "disallowed in state %s "
                                 "while port index %d is enabled & populated",
                                 OMX_STATETYPE_STRING(m_state),
                                 p_port_format->nPortIndex);

            retval = OMX_ErrorIncorrectStateOperation;
        }
        else
        {
            retval = set_video_port_format(p_port_format);
        }

        break;
    }

    case OMX_IndexParamVideoMpeg2:
    {
        OMX_SWVDEC_LOG_ERROR("OMX_IndexParamVideoMpeg2 unsupported");

        retval = OMX_ErrorUnsupportedIndex;
        break;
    }

    case OMX_IndexParamVideoMpeg4:
    {
        OMX_SWVDEC_LOG_API("OMX_IndexParamVideoMpeg4 unsupported");

        retval = OMX_ErrorUnsupportedIndex;
        break;
    }

    case OMX_IndexParamVideoAvc:
    {
        OMX_SWVDEC_LOG_API("OMX_IndexParamVideoAvc unsupported");

        retval = OMX_ErrorUnsupportedIndex;
        break;
    }

    case OMX_IndexParamVideoH263:
    {
        OMX_SWVDEC_LOG_API("OMX_IndexParamVideoH263 unsupported");

        retval = OMX_ErrorUnsupportedIndex;
        break;
    }

    default:
    {
        /**
         * Vendor-specific extension indices checked here since they are not
         * part of the OMX_INDEXTYPE enumerated type.
         */

        switch ((OMX_QCOM_EXTN_INDEXTYPE) param_index)
        {

        case OMX_QcomIndexPortDefn:
        {
            OMX_QCOM_PARAM_PORTDEFINITIONTYPE *p_port_def =
                (OMX_QCOM_PARAM_PORTDEFINITIONTYPE *) p_param_data;

            OMX_SWVDEC_LOG_API("OMX_QcomIndexPortDefn, port index %d",
                               p_port_def->nPortIndex);

            if ((m_state != OMX_StateLoaded) &&
                (((p_port_def->nPortIndex == OMX_CORE_PORT_INDEX_IP) &&
                  (m_port_ip.enabled      == OMX_TRUE) &&
                  (m_port_ip.populated    == OMX_TRUE)) ||
                 ((p_port_def->nPortIndex == OMX_CORE_PORT_INDEX_OP) &&
                  (m_port_op.enabled      == OMX_TRUE) &&
                  (m_port_op.populated    == OMX_TRUE))))
            {
                OMX_SWVDEC_LOG_ERROR("OMX_QcomIndexPortDefn "
                                     "disallowed in state %s "
                                     "while port index %d "
                                     "is enabled & populated",
                                     OMX_STATETYPE_STRING(m_state),
                                     p_port_def->nPortIndex);

                retval = OMX_ErrorIncorrectStateOperation;
            }
            else
            {
                retval = set_port_definition_qcom(p_port_def);
            }

            break;
        }

        case OMX_QcomIndexParamVideoDivx:
        {
            OMX_SWVDEC_LOG_API("OMX_QcomIndexParamVideoDivx");

            break;
        }

        case OMX_QcomIndexParamVideoSyncFrameDecodingMode:
        {
            OMX_SWVDEC_LOG_API("OMX_QcomIndexParamVideoSyncFrameDecodingMode");

            m_sync_frame_decoding_mode = true;
            break;
        }

        case OMX_QcomIndexParamVideoDecoderPictureOrder:
        {
            QOMX_VIDEO_DECODER_PICTURE_ORDER *p_picture_order =
                (QOMX_VIDEO_DECODER_PICTURE_ORDER *) p_param_data;

            switch (p_picture_order->eOutputPictureOrder)
            {

            case QOMX_VIDEO_DISPLAY_ORDER:
            {
                OMX_SWVDEC_LOG_API(
                    "OMX_QcomIndexParamVideoDecoderPictureOrder, "
                    "QOMX_VIDEO_DISPLAY_ORDER");

                break;
            }

            case QOMX_VIDEO_DECODE_ORDER:
            {
                OMX_SWVDEC_LOG_API(
                    "OMX_QcomIndexParamVideoDecoderPictureOrder, "
                    "QOMX_VIDEO_DECODE_ORDER");

                OMX_SWVDEC_LOG_ERROR(
                    "OMX_QcomIndexParamVideoDecoderPictureOrder, "
                    "QOMX_VIDEO_DECODE_ORDER; unsupported");

                retval = OMX_ErrorUnsupportedSetting;
                break;
            }

            default:
            {
                OMX_SWVDEC_LOG_ERROR(
                    "OMX_QcomIndexParamVideoDecoderPictureOrder, %d; invalid",
                    p_picture_order->eOutputPictureOrder);

                retval = OMX_ErrorBadParameter;
                break;
            }

            }

            break;
        }

        case OMX_GoogleAndroidIndexEnableAndroidNativeBuffers:
        {
            OMX_SWVDEC_LOG_API(
                "OMX_GoogleAndroidIndexEnableAndroidNativeBuffers, %s",
                (((EnableAndroidNativeBuffersParams *) p_param_data)->enable ?
                 "enable" :
                 "disable"));

            m_android_native_buffers =
                (bool) (((EnableAndroidNativeBuffersParams *)
                         p_param_data)->enable);

            break;
        }

        case OMX_GoogleAndroidIndexUseAndroidNativeBuffer:
        {
            OMX_SWVDEC_LOG_ERROR("OMX_GoogleAndroidIndexUseAndroidNativeBuffer "
                                 "unsupported");

            retval = OMX_ErrorUnsupportedIndex;
            break;
        }

        case OMX_QcomIndexParamEnableTimeStampReorder:
        {
            OMX_SWVDEC_LOG_API(
                "OMX_QcomIndexParamEnableTimeStampReorder, %s",
                (((QOMX_INDEXTIMESTAMPREORDER *) p_param_data)->bEnable ?
                 "enable" :
                 "disable"));

            break;
        }

        case OMX_QcomIndexParamVideoMetaBufferMode:
        {
            StoreMetaDataInBuffersParams *p_meta_data =
                (StoreMetaDataInBuffersParams *) p_param_data;

            OMX_SWVDEC_LOG_API("OMX_QcomIndexParamVideoMetaBufferMode, "
                               "port index %d, %s",
                               p_meta_data->nPortIndex,
                               (p_meta_data->bStoreMetaData ?
                                "enable" :
                                "disable"));

            if (p_meta_data->nPortIndex == OMX_CORE_PORT_INDEX_OP)
            {
                if (p_meta_data->bStoreMetaData && m_meta_buffer_mode_disabled)
                {
                    OMX_SWVDEC_LOG_ERROR("meta buffer mode disabled "
                                         "via ADB setprop: "
                                         "'omx_swvdec.meta_buffer.disable'");

                    retval = OMX_ErrorBadParameter;
                }
                else
                {
                    m_meta_buffer_mode = (bool) p_meta_data->bStoreMetaData;
                }
            }
            else
            {
                OMX_SWVDEC_LOG_ERROR("port index '%d' invalid",
                                     p_meta_data->nPortIndex);

                retval = OMX_ErrorBadPortIndex;
            }

            break;
        }

        case OMX_QcomIndexParamVideoAdaptivePlaybackMode:
        {
            PrepareForAdaptivePlaybackParams *p_adaptive_playback_params =
                (PrepareForAdaptivePlaybackParams *) p_param_data;

            OMX_SWVDEC_LOG_API("OMX_QcomIndexParamVideoAdaptivePlaybackMode, "
                               "port index %d, %s, max dimensions: %d x %d",
                               p_adaptive_playback_params->nPortIndex,
                               (p_adaptive_playback_params->bEnable ?
                                "enable" :
                                "disable"),
                               p_adaptive_playback_params->nMaxFrameWidth,
                               p_adaptive_playback_params->nMaxFrameHeight);

            if (p_adaptive_playback_params->nPortIndex ==
                OMX_CORE_PORT_INDEX_OP)
            {
                if (p_adaptive_playback_params->bEnable)
                {
                    m_adaptive_playback_mode = true;

                    retval =
                        set_adaptive_playback(
                            p_adaptive_playback_params->nMaxFrameWidth,
                            p_adaptive_playback_params->nMaxFrameHeight);
                }
            }
            else
            {
                OMX_SWVDEC_LOG_ERROR("port index '%d' invalid",
                                     p_adaptive_playback_params->nPortIndex);

                retval = OMX_ErrorBadPortIndex;
            }

            break;
        }

        default:
        {
            OMX_SWVDEC_LOG_ERROR("param index '0x%08x' invalid",
                                 (OMX_QCOM_EXTN_INDEXTYPE) param_index);

            retval = OMX_ErrorBadParameter;
            break;
        }

        } // switch ((OMX_QCOM_EXTN_INDEXTYPE) param_index)

        break;
    } // default case

    } // switch (param_index)

set_parameter_exit:
    return retval;
}

/**
 * @brief Get a configuration from component.
 *
 * @param[in] cmp_handle:    Component handle.
 * @param[in] config_index:  Configuration index.
 * @param[in] p_config_data: Pointer to configuration data.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::get_config(OMX_HANDLETYPE cmp_handle,
                                     OMX_INDEXTYPE  config_index,
                                     OMX_PTR        p_config_data)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (m_state == OMX_StateInvalid)
    {
        OMX_SWVDEC_LOG_ERROR("in invalid state");

        retval = OMX_ErrorInvalidState;
    }
    else if (cmp_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");

        retval = OMX_ErrorInvalidComponent;
    }
    else if (p_config_data == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_config_data = NULL");

        retval = OMX_ErrorBadParameter;
    }

    if (retval != OMX_ErrorNone)
    {
        goto get_config_exit;
    }

    switch (config_index)
    {

    case OMX_IndexConfigCommonOutputCrop:
    {
        OMX_CONFIG_RECTTYPE *p_recttype = (OMX_CONFIG_RECTTYPE *) p_config_data;

        OMX_SWVDEC_LOG_API("OMX_IndexConfigCommonOutputCrop, port index %d",
                           p_recttype->nPortIndex);

        if (p_recttype->nPortIndex == OMX_CORE_PORT_INDEX_OP)
        {
            if (m_dimensions_update_inprogress)
            {
                retval = get_frame_dimensions_swvdec();

                m_dimensions_update_inprogress = false;
            }

            if (retval == OMX_ErrorNone)
            {
                p_recttype->nLeft   = 0;
                p_recttype->nTop    = 0;
                p_recttype->nWidth  = m_frame_dimensions.width;
                p_recttype->nHeight = m_frame_dimensions.height;
            }
        }
        else
        {
            OMX_SWVDEC_LOG_ERROR("port index '%d' invalid",
                                 p_recttype->nPortIndex);

            retval = OMX_ErrorBadPortIndex;
        }

        break;
    }

    default:
    {
        switch ((OMX_QCOM_EXTN_INDEXTYPE) config_index)
        {

        case OMX_QcomIndexConfigInterlaced:
        {
            OMX_QCOM_CONFIG_INTERLACETYPE *p_config_interlacetype =
                (OMX_QCOM_CONFIG_INTERLACETYPE *) p_config_data;

            OMX_SWVDEC_LOG_API("OMX_QcomIndexConfigInterlaced, "
                               "port index %d, index %d",
                               p_config_interlacetype->nPortIndex,
                               p_config_interlacetype->nIndex);

            if (p_config_interlacetype->nPortIndex == OMX_CORE_PORT_INDEX_OP)
            {
                if (p_config_interlacetype->nIndex == 0)
                {
                    p_config_interlacetype->eInterlaceType =
                        OMX_QCOM_InterlaceFrameProgressive;
                }
                else if (p_config_interlacetype->nIndex == 1)
                {
                    p_config_interlacetype->eInterlaceType =
                        OMX_QCOM_InterlaceInterleaveFrameTopFieldFirst;
                }
                else if (p_config_interlacetype->nIndex == 2)
                {
                    p_config_interlacetype->eInterlaceType =
                        OMX_QCOM_InterlaceInterleaveFrameBottomFieldFirst;
                }
                else
                {
                    OMX_SWVDEC_LOG_ERROR("index '%d' unsupported; "
                                         "no more interlaced types",
                                         p_config_interlacetype->nIndex);

                    retval = OMX_ErrorNoMore;
                }
            }
            else
            {
                OMX_SWVDEC_LOG_ERROR("port index '%d' invalid",
                                     p_config_interlacetype->nPortIndex);

                retval = OMX_ErrorBadPortIndex;
            }

            break;
        }

        case OMX_QcomIndexQueryNumberOfVideoDecInstance:
        {
            QOMX_VIDEO_QUERY_DECODER_INSTANCES *p_decoder_instances =
                (QOMX_VIDEO_QUERY_DECODER_INSTANCES *) p_config_data;

            OMX_SWVDEC_LOG_API("OMX_QcomIndexQueryNumberOfVideoDecInstance");

            p_decoder_instances->nNumOfInstances = OMX_SWVDEC_NUM_INSTANCES;
            break;
        }

        case OMX_QcomIndexConfigVideoFramePackingArrangement:
        {
            OMX_SWVDEC_LOG_API(
                "OMX_QcomIndexConfigVideoFramePackingArrangement");

            OMX_SWVDEC_LOG_ERROR(
                "OMX_QcomIndexConfigVideoFramePackingArrangement unsupported");

            retval = OMX_ErrorUnsupportedIndex;
            break;
        }

        default:
        {
            OMX_SWVDEC_LOG_ERROR("config index '0x%08x' invalid", config_index);

            retval = OMX_ErrorBadParameter;
            break;
        }

        } // switch ((OMX_QCOM_EXTN_INDEXTYPE) config_index)

        break;
    }

    } // switch (config_index)

get_config_exit:
    return retval;
}

/**
 * @brief Set a configuration to component.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::set_config(OMX_HANDLETYPE cmp_handle,
                                     OMX_INDEXTYPE  config_index,
                                     OMX_PTR        p_config_data)
{
    (void) cmp_handle;
    (void) p_config_data;

    OMX_SWVDEC_LOG_API("config index 0x%08x", config_index);

    OMX_SWVDEC_LOG_ERROR("not implemented");

    return OMX_ErrorNotImplemented;
}

/**
 * @brief Translate a vendor-specific extension string to a standard index type.
 *
 * @param[in]     cmp_handle:   Component handle.
 * @param[in]     param_name:   Parameter name (extension string).
 * @param[in,out] p_index_type: Pointer to extension string's index type.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::get_extension_index(OMX_HANDLETYPE cmp_handle,
                                              OMX_STRING     param_name,
                                              OMX_INDEXTYPE *p_index_type)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (m_state == OMX_StateInvalid)
    {
        OMX_SWVDEC_LOG_ERROR("in invalid state");

        retval = OMX_ErrorInvalidState;
    }
    else if (cmp_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");

        retval = OMX_ErrorInvalidComponent;
    }
    else if (p_index_type == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_index_type = NULL");

        retval = OMX_ErrorBadParameter;
    }

    if (retval != OMX_ErrorNone)
    {
        goto get_extension_index_exit;
    }

    OMX_SWVDEC_LOG_API("'%s'", param_name);

    if (!strncmp(param_name,
                 "OMX.QCOM.index.param.video.SyncFrameDecodingMode",
                 OMX_MAX_STRINGNAME_SIZE))
    {
        *p_index_type =
            (OMX_INDEXTYPE) OMX_QcomIndexParamVideoSyncFrameDecodingMode;
    }
    else if (!strncmp(param_name,
                      "OMX.QCOM.index.param.IndexExtraData",
                      OMX_MAX_STRINGNAME_SIZE))
    {
        *p_index_type = (OMX_INDEXTYPE) OMX_QcomIndexParamIndexExtraDataType;
    }
    else if (!strncmp(param_name,
                      "OMX.google.android.index.enableAndroidNativeBuffers",
                      OMX_MAX_STRINGNAME_SIZE))
    {
        *p_index_type =
            (OMX_INDEXTYPE) OMX_GoogleAndroidIndexEnableAndroidNativeBuffers;
    }
    else if (!strncmp(param_name,
                      "OMX.google.android.index.useAndroidNativeBuffer2",
                      OMX_MAX_STRINGNAME_SIZE))
    {
        *p_index_type =
            (OMX_INDEXTYPE) OMX_GoogleAndroidIndexUseAndroidNativeBuffer2;
    }
    else if (!strncmp(param_name,
                      "OMX.google.android.index.useAndroidNativeBuffer",
                      OMX_MAX_STRINGNAME_SIZE))
    {
        *p_index_type =
            (OMX_INDEXTYPE) OMX_GoogleAndroidIndexUseAndroidNativeBuffer;
    }
    else if (!strncmp(param_name,
                      "OMX.google.android.index.getAndroidNativeBufferUsage",
                      OMX_MAX_STRINGNAME_SIZE))
    {
        *p_index_type =
            (OMX_INDEXTYPE) OMX_GoogleAndroidIndexGetAndroidNativeBufferUsage;
    }
    else if (!strncmp(param_name,
                      "OMX.google.android.index.storeMetaDataInBuffers",
                      OMX_MAX_STRINGNAME_SIZE))
    {
        *p_index_type = (OMX_INDEXTYPE) OMX_QcomIndexParamVideoMetaBufferMode;
    }
    else if (!strncmp(param_name,
                      "OMX.google.android.index.describeColorFormat",
                      OMX_MAX_STRINGNAME_SIZE))
    {
        *p_index_type = (OMX_INDEXTYPE) OMX_QcomIndexFlexibleYUVDescription;
    }
    else if (!strncmp(param_name,
                      "OMX.google.android.index.prepareForAdaptivePlayback",
                      OMX_MAX_STRINGNAME_SIZE))
    {
        *p_index_type =
            (OMX_INDEXTYPE) OMX_QcomIndexParamVideoAdaptivePlaybackMode;
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("'%s': not implemented", param_name);

        retval = OMX_ErrorNotImplemented;
    }

get_extension_index_exit:
    return retval;
}

/**
 * @brief Get component state.
 *
 * @param[in]     cmp_handle: Component handle.
 * @param[in,out] p_state:    Pointer to state variable.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::get_state(OMX_HANDLETYPE cmp_handle,
                                    OMX_STATETYPE *p_state)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (cmp_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");

        retval = OMX_ErrorInvalidComponent;
    }
    else
    {
        OMX_SWVDEC_LOG_API("%s", OMX_STATETYPE_STRING(m_state));

        *p_state = m_state;
    }

    return retval;
}

/**
 * @brief Component tunnel request.
 *
 * @retval OMX_ErrorNotImplemented
 */
OMX_ERRORTYPE omx_swvdec::component_tunnel_request(
    OMX_HANDLETYPE       cmp_handle,
    OMX_U32              port,
    OMX_HANDLETYPE       peer_component,
    OMX_U32              peer_port,
    OMX_TUNNELSETUPTYPE *p_tunnel_setup)
{
    (void) cmp_handle;
    (void) port;
    (void) peer_component;
    (void) peer_port;
    (void) p_tunnel_setup;

    OMX_SWVDEC_LOG_API("");

    OMX_SWVDEC_LOG_ERROR("not implemented");

    return OMX_ErrorNotImplemented;
}

/**
 * @brief Use buffer.
 *
 * @param[in]     cmp_handle:    Component handle.
 * @param[in,out] pp_buffer_hdr: Pointer to pointer to buffer header type
 *                               structure.
 * @param[in]     port:          Port index.
 * @param[in]     p_app_data:    Pointer to IL client app data.
 * @param[in]     bytes:         Size of buffer to be allocated in bytes.
 * @param[in]     p_buffer:      Pointer to buffer to be used.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::use_buffer(OMX_HANDLETYPE         cmp_handle,
                                     OMX_BUFFERHEADERTYPE **pp_buffer_hdr,
                                     OMX_U32                port,
                                     OMX_PTR                p_app_data,
                                     OMX_U32                bytes,
                                     OMX_U8                *p_buffer)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (m_state == OMX_StateInvalid)
    {
        OMX_SWVDEC_LOG_ERROR("in invalid state");

        retval = OMX_ErrorInvalidState;
    }
    else if (cmp_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");

        retval = OMX_ErrorInvalidComponent;
    }
    else if (pp_buffer_hdr == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("pp_buffer_hdr = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else
    {
        OMX_SWVDEC_LOG_API("port index %d, %p", port, p_buffer);

        if (port == OMX_CORE_PORT_INDEX_OP)
        {
            retval = buffer_use_op(pp_buffer_hdr, p_app_data, bytes, p_buffer);

            if (retval == OMX_ErrorNone)
            {
                SWVDEC_STATUS retval_swvdec;

                if ((m_status_flags & (1 << PENDING_STATE_LOADED_TO_IDLE)) &&
                    (m_port_ip.populated == OMX_TRUE) &&
                    (m_port_op.populated == OMX_TRUE))
                {
                    if ((retval_swvdec = swvdec_start(m_swvdec_handle)) !=
                        SWVDEC_STATUS_SUCCESS)
                    {
                        OMX_SWVDEC_LOG_ERROR("failed to start SwVdec");

                        retval = retval_swvdec2omx(retval_swvdec);
                        goto use_buffer_exit;
                    }

                    m_status_flags &= ~(1 << PENDING_STATE_LOADED_TO_IDLE);

                    async_post_event(OMX_SWVDEC_EVENT_CMD_ACK,
                                     OMX_CommandStateSet,
                                     OMX_StateIdle);
                }

                if ((m_status_flags & (1 << PENDING_PORT_ENABLE_OP)) &&
                    (m_port_op.populated == OMX_TRUE))
                {
                    if (m_port_reconfig_inprogress)
                    {
                        if ((retval_swvdec = swvdec_start(m_swvdec_handle)) !=
                            SWVDEC_STATUS_SUCCESS)
                        {
                            OMX_SWVDEC_LOG_ERROR("failed to start SwVdec");

                            retval = retval_swvdec2omx(retval_swvdec);
                        }
                    }

                    m_status_flags &= ~(1 << PENDING_PORT_ENABLE_OP);

                    async_post_event(OMX_SWVDEC_EVENT_CMD_ACK,
                                     OMX_CommandPortEnable,
                                     OMX_CORE_PORT_INDEX_OP);
                }
            }
        }
        else
        {
            OMX_SWVDEC_LOG_ERROR("port index '%d' invalid", port);

            retval = OMX_ErrorBadPortIndex;
        }
    }

use_buffer_exit:
    return retval;
}

/**
 * @brief Allocate new buffer & associated header.
 *
 * @param[in]     cmp_handle:    Component handle.
 * @param[in,out] pp_buffer_hdr: Pointer to pointer to buffer header type
 *                               structure.
 * @param[in]     port:          Port index.
 * @param[in]     p_app_data:    Pointer to IL client app data.
 * @param[in]     bytes:         Size of buffer to be allocated in bytes.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::allocate_buffer(OMX_HANDLETYPE         cmp_handle,
                                          OMX_BUFFERHEADERTYPE **pp_buffer_hdr,
                                          OMX_U32                port,
                                          OMX_PTR                p_app_data,
                                          OMX_U32                bytes)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (m_state == OMX_StateInvalid)
    {
        OMX_SWVDEC_LOG_ERROR("in invalid state");

        retval = OMX_ErrorInvalidState;
    }
    else if (cmp_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");

        retval = OMX_ErrorInvalidComponent;
    }
    else if (pp_buffer_hdr == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("pp_buffer_hdr = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else
    {
        OMX_SWVDEC_LOG_API("port index %d, %d bytes", port, bytes);

        if (port == OMX_CORE_PORT_INDEX_IP)
        {
            retval = buffer_allocate_ip(pp_buffer_hdr,
                                        p_app_data,
                                        bytes);
        }
        else if (port == OMX_CORE_PORT_INDEX_OP)
        {
            if (m_meta_buffer_mode == true)
            {
                OMX_SWVDEC_LOG_ERROR("'meta buffer mode' enabled");

                retval = OMX_ErrorBadParameter;
            }
            else if (m_android_native_buffers == true)
            {
                OMX_SWVDEC_LOG_ERROR("'android native buffers' enabled");

                retval = OMX_ErrorBadParameter;
            }
            else
            {
                retval = buffer_allocate_op(pp_buffer_hdr,
                                            p_app_data,
                                            bytes);
            }
        }
        else
        {
            OMX_SWVDEC_LOG_ERROR("port index %d invalid", port);

            retval = OMX_ErrorBadPortIndex;
        }

        if (retval == OMX_ErrorNone)
        {
            SWVDEC_STATUS retval_swvdec;

            if ((m_status_flags & (1 << PENDING_STATE_LOADED_TO_IDLE)) &&
                (m_port_ip.populated == OMX_TRUE) &&
                (m_port_op.populated == OMX_TRUE))
            {
                if ((retval_swvdec = swvdec_start(m_swvdec_handle)) !=
                    SWVDEC_STATUS_SUCCESS)
                {
                    OMX_SWVDEC_LOG_ERROR("failed to start SwVdec");

                    retval = retval_swvdec2omx(retval_swvdec);
                    goto allocate_buffer_exit;
                }

                m_status_flags &= ~(1 << PENDING_STATE_LOADED_TO_IDLE);

                async_post_event(OMX_SWVDEC_EVENT_CMD_ACK,
                                 OMX_CommandStateSet,
                                 OMX_StateIdle);
            }

            if ((m_status_flags & (1 << PENDING_PORT_ENABLE_IP)) &&
                (m_port_ip.populated == OMX_TRUE))
            {
                m_status_flags &= ~(1 << PENDING_PORT_ENABLE_IP);

                async_post_event(OMX_SWVDEC_EVENT_CMD_ACK,
                                 OMX_CommandPortEnable,
                                 OMX_CORE_PORT_INDEX_IP);
            }

            if ((m_status_flags & (1 << PENDING_PORT_ENABLE_OP)) &&
                (m_port_op.populated == OMX_TRUE))
            {
                if (m_port_reconfig_inprogress)
                {
                    if ((retval_swvdec = swvdec_start(m_swvdec_handle)) !=
                        SWVDEC_STATUS_SUCCESS)
                    {
                        OMX_SWVDEC_LOG_ERROR("failed to start SwVdec");

                        retval = retval_swvdec2omx(retval_swvdec);
                    }
                }

                m_status_flags &= ~(1 << PENDING_PORT_ENABLE_OP);

                async_post_event(OMX_SWVDEC_EVENT_CMD_ACK,
                                 OMX_CommandPortEnable,
                                 OMX_CORE_PORT_INDEX_OP);
            }
        }
    }

allocate_buffer_exit:
    return retval;
}

/**
 * @brief Release buffer & associated header.
 *
 * @param[in] cmp_handle:   Component handle.
 * @param[in] port:         Port index.
 * @param[in] p_buffer_hdr: Pointer to buffer's buffer header.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::free_buffer(OMX_HANDLETYPE        cmp_handle,
                                      OMX_U32               port,
                                      OMX_BUFFERHEADERTYPE *p_buffer_hdr)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (cmp_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");

        retval = OMX_ErrorInvalidComponent;
    }
    else if (p_buffer_hdr == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_buffer_hdr = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else if ((port != OMX_CORE_PORT_INDEX_IP) &&
             (port != OMX_CORE_PORT_INDEX_OP))
    {
        OMX_SWVDEC_LOG_ERROR("port index '%d' invalid", port);

        retval = OMX_ErrorBadPortIndex;
    }
    else if (m_state != OMX_StateIdle)
    {
        if (m_state != OMX_StateExecuting)
        {
            OMX_SWVDEC_LOG_ERROR("disallowed in state %s",
                                 OMX_STATETYPE_STRING(m_state));

            retval = OMX_ErrorIncorrectStateOperation;
        }
        else
        {
            if (((port == OMX_CORE_PORT_INDEX_IP) && m_port_ip.enabled) ||
                ((port == OMX_CORE_PORT_INDEX_OP) && m_port_op.enabled))
            {
                OMX_SWVDEC_LOG_ERROR("port index %d not disabled", port);

                retval = OMX_ErrorBadPortIndex;
            }
        }
    }

    if (retval == OMX_ErrorNone)
    {
        OMX_SWVDEC_LOG_API("port index %d, %p", port, p_buffer_hdr);

        if (port == OMX_CORE_PORT_INDEX_IP)
        {
            retval = buffer_deallocate_ip(p_buffer_hdr);
        }
        else
        {
            retval = buffer_deallocate_op(p_buffer_hdr);
        }
    }

    if ((retval == OMX_ErrorNone) &&
        (m_status_flags & (1 << PENDING_STATE_IDLE_TO_LOADED)))
    {
        if ((m_port_ip.unpopulated == OMX_TRUE) &&
            (m_port_op.unpopulated == OMX_TRUE))
        {
            SWVDEC_STATUS retval_swvdec;

            if ((retval_swvdec = swvdec_stop(m_swvdec_handle)) ==
                SWVDEC_STATUS_SUCCESS)
            {
                m_status_flags &= ~(1 << PENDING_STATE_IDLE_TO_LOADED);

                async_post_event(OMX_SWVDEC_EVENT_CMD_ACK,
                                 OMX_CommandStateSet,
                                 OMX_StateLoaded);
            }
            else
            {
                OMX_SWVDEC_LOG_ERROR("failed to stop SwVdec");

                retval = retval_swvdec2omx(retval_swvdec);
            }
        }
    }

    if ((retval == OMX_ErrorNone) &&
        (m_status_flags & (1 << PENDING_PORT_DISABLE_IP)) &&
        m_port_ip.unpopulated)
    {
        m_status_flags &= ~(1 << PENDING_PORT_DISABLE_IP);

        async_post_event(OMX_SWVDEC_EVENT_CMD_ACK,
                         OMX_CommandPortDisable,
                         OMX_CORE_PORT_INDEX_IP);
    }

    if ((retval == OMX_ErrorNone) &&
        (m_status_flags & (1 << PENDING_PORT_DISABLE_OP)) &&
        m_port_op.unpopulated)
    {
        if (m_port_reconfig_inprogress)
        {
            SWVDEC_STATUS retval_swvdec;

            if ((retval_swvdec = swvdec_stop(m_swvdec_handle)) !=
                SWVDEC_STATUS_SUCCESS)
            {
                OMX_SWVDEC_LOG_ERROR("failed to stop SwVdec");

                retval = retval_swvdec2omx(retval_swvdec);
            }
        }

        m_status_flags &= ~(1 << PENDING_PORT_DISABLE_OP);

        async_post_event(OMX_SWVDEC_EVENT_CMD_ACK,
                         OMX_CommandPortDisable,
                         OMX_CORE_PORT_INDEX_OP);
    }

    return retval;
}

/**
 * @brief Send a buffer to component's input port to be emptied.
 *
 * @param[in] cmp_handle:   Component handle.
 * @param[in] p_buffer_hdr: Pointer to buffer's buffer header.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::empty_this_buffer(OMX_HANDLETYPE        cmp_handle,
                                            OMX_BUFFERHEADERTYPE *p_buffer_hdr)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    unsigned int ii;

    if (m_state == OMX_StateInvalid)
    {
        OMX_SWVDEC_LOG_ERROR("in invalid state");

        retval = OMX_ErrorInvalidState;
    }
    else if (cmp_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");

        retval = OMX_ErrorInvalidComponent;
    }
    else if (p_buffer_hdr == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_buffer_hdr = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else if (p_buffer_hdr->pBuffer == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_buffer_hdr->pBuffer = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else if (p_buffer_hdr->pInputPortPrivate == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_buffer_hdr->pInputPortPrivate = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else if (m_port_ip.enabled == OMX_FALSE)
    {
        OMX_SWVDEC_LOG_ERROR("ip port disabled");

        retval = OMX_ErrorIncorrectStateOperation;
    }
    else if (p_buffer_hdr->nInputPortIndex != OMX_CORE_PORT_INDEX_IP)
    {
        OMX_SWVDEC_LOG_ERROR("port index '%d' invalid",
                             p_buffer_hdr->nInputPortIndex);

        retval = OMX_ErrorBadPortIndex;
    }

    if (retval != OMX_ErrorNone)
    {
        goto empty_this_buffer_exit;
    }

    for (ii = 0; ii < m_port_ip.def.nBufferCountActual; ii++)
    {
        if (p_buffer_hdr == &(m_buffer_array_ip[ii].buffer_header))
        {
            OMX_SWVDEC_LOG_LOW("ip buffer %p has index %d",
                               p_buffer_hdr->pBuffer,
                               ii);
            break;
        }
    }

    if (ii == m_port_ip.def.nBufferCountActual)
    {
        OMX_SWVDEC_LOG_ERROR("ip buffer %p not found",
                             p_buffer_hdr->pBuffer);

        retval = OMX_ErrorBadParameter;
        goto empty_this_buffer_exit;
    }

    OMX_SWVDEC_LOG_API("%p: buffer %p, flags 0x%08x, filled length %d, "
                       "timestamp %lld",
                       p_buffer_hdr,
                       p_buffer_hdr->pBuffer,
                       p_buffer_hdr->nFlags,
                       p_buffer_hdr->nFilledLen,
                       p_buffer_hdr->nTimeStamp);

    async_post_event(OMX_SWVDEC_EVENT_ETB,
                     (unsigned long) p_buffer_hdr,
                     (unsigned long) ii);

empty_this_buffer_exit:
    return retval;
}

/**
 * @brief Send a buffer to component's output port to be filled.
 *
 * @param[in] cmp_handle:   Component handle.
 * @param[in] p_buffer_hdr: Pointer to buffer's buffer header.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::fill_this_buffer(OMX_HANDLETYPE        cmp_handle,
                                           OMX_BUFFERHEADERTYPE *p_buffer_hdr)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    unsigned int ii;

    SWVDEC_BUFFER *p_buffer_swvdec;

    if (m_state == OMX_StateInvalid)
    {
        OMX_SWVDEC_LOG_ERROR("in invalid state");

        retval = OMX_ErrorInvalidState;
    }
    else if (cmp_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");

        retval = OMX_ErrorInvalidComponent;
    }
    else if (p_buffer_hdr == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_buffer_hdr = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else if (p_buffer_hdr->pBuffer == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_buffer_hdr->pBuffer = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else if (p_buffer_hdr->pOutputPortPrivate == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_buffer_hdr->pOutputPortPrivate = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else if (m_port_op.enabled == OMX_FALSE)
    {
        OMX_SWVDEC_LOG_ERROR("op port disabled");

        retval = OMX_ErrorIncorrectStateOperation;
    }
    else if (p_buffer_hdr->nOutputPortIndex != OMX_CORE_PORT_INDEX_OP)
    {
        OMX_SWVDEC_LOG_ERROR("port index '%d' invalid",
                             p_buffer_hdr->nOutputPortIndex);

        retval = OMX_ErrorBadPortIndex;
    }

    if (retval != OMX_ErrorNone)
    {
        goto fill_this_buffer_exit;
    }

    OMX_SWVDEC_LOG_API("%p", p_buffer_hdr);

    for (ii = 0; ii < m_port_op.def.nBufferCountActual; ii++)
    {
        if (p_buffer_hdr == &(m_buffer_array_op[ii].buffer_header))
        {
            OMX_SWVDEC_LOG_LOW("op buffer %p has index %d",
                               p_buffer_hdr->pBuffer,
                               ii);
            break;
        }
    }

    if (ii == m_port_op.def.nBufferCountActual)
    {
        OMX_SWVDEC_LOG_ERROR("op buffer %p not found",
                             p_buffer_hdr->pBuffer);

        retval = OMX_ErrorBadParameter;
        goto fill_this_buffer_exit;
    }

    p_buffer_swvdec = &m_buffer_array_op[ii].buffer_swvdec;

    if (m_meta_buffer_mode)
    {
        struct VideoDecoderOutputMetaData *p_meta_data;

        private_handle_t *p_private_handle;

        struct vdec_bufferpayload *p_buffer_payload;

        p_meta_data =
            (struct VideoDecoderOutputMetaData *) p_buffer_hdr->pBuffer;

        p_private_handle = (private_handle_t *) (p_meta_data->pHandle);

        p_buffer_payload = &m_buffer_array_op[ii].buffer_payload;

        if (p_private_handle == NULL)
        {
            OMX_SWVDEC_LOG_ERROR(
                "p_buffer_hdr->pBuffer->pHandle = NULL");

            retval = OMX_ErrorBadParameter;
            goto fill_this_buffer_exit;
        }

        pthread_mutex_lock(&m_meta_buffer_array_mutex);

        if (m_meta_buffer_array[ii].ref_count == 0)
        {
            unsigned char *bufferaddr;

            bufferaddr = (unsigned char *) mmap(NULL,
                                                m_port_op.def.nBufferSize,
                                                PROT_READ | PROT_WRITE,
                                                MAP_SHARED,
                                                p_private_handle->fd,
                                                0);

            if (bufferaddr == MAP_FAILED)
            {
                OMX_SWVDEC_LOG_ERROR("mmap() failed for "
                                     "fd %d of size %d",
                                     p_private_handle->fd,
                                     m_port_op.def.nBufferSize);

                pthread_mutex_unlock(&m_meta_buffer_array_mutex);

                retval = OMX_ErrorInsufficientResources;
                goto fill_this_buffer_exit;
            }

            p_buffer_payload->bufferaddr  = bufferaddr;
            p_buffer_payload->pmem_fd     = p_private_handle->fd;
            p_buffer_payload->buffer_len  = m_port_op.def.nBufferSize;
            p_buffer_payload->mmaped_size = m_port_op.def.nBufferSize;

            p_buffer_swvdec->p_buffer      = bufferaddr;
            p_buffer_swvdec->size          = m_port_op.def.nBufferSize;
            p_buffer_swvdec->p_client_data = (void *) ((unsigned long) ii);
        }

        meta_buffer_ref_add(ii, p_buffer_payload->pmem_fd);

        pthread_mutex_unlock(&m_meta_buffer_array_mutex);
    }

    OMX_SWVDEC_LOG_LOW("%p: buffer %p",
                       p_buffer_hdr,
                       p_buffer_swvdec->p_buffer);

    async_post_event(OMX_SWVDEC_EVENT_FTB,
                     (unsigned long) p_buffer_hdr,
                     (unsigned long) ii);

fill_this_buffer_exit:
    return retval;
}

/**
 * @brief Set component's callback structure.
 *
 * @param[in] cmp_handle:  Component handle.
 * @param[in] p_callbacks: Pointer to callback structure.
 * @param[in] p_app_data:  Pointer to IL client app data.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::set_callbacks(OMX_HANDLETYPE    cmp_handle,
                                        OMX_CALLBACKTYPE *p_callbacks,
                                        OMX_PTR           p_app_data)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    OMX_SWVDEC_LOG_API("");

    if (m_state == OMX_StateInvalid)
    {
        OMX_SWVDEC_LOG_ERROR("in invalid state");

        retval = OMX_ErrorInvalidState;
    }
    else if (cmp_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");

        retval = OMX_ErrorInvalidComponent;
    }
    else if (p_callbacks->EventHandler == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_callbacks->EventHandler = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else if (p_callbacks->EmptyBufferDone == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_callbacks->EmptyBufferDone = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else if (p_callbacks->FillBufferDone == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_callbacks->FillBufferDone = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else
    {
        m_callback = *p_callbacks;
        m_app_data = p_app_data;
    }

    return retval;
}

/**
 * @brief Use EGL image.
 *
 * @retval OMX_ErrorNotImplemented
 */
OMX_ERRORTYPE omx_swvdec::use_EGL_image(OMX_HANDLETYPE         cmp_handle,
                                        OMX_BUFFERHEADERTYPE **pp_buffer_hdr,
                                        OMX_U32                port,
                                        OMX_PTR                p_app_data,
                                        void                  *egl_image)
{
    (void) cmp_handle;
    (void) pp_buffer_hdr;
    (void) port;
    (void) p_app_data;
    (void) egl_image;

    OMX_SWVDEC_LOG_API("");

    OMX_SWVDEC_LOG_ERROR("not implemented");

    return OMX_ErrorNotImplemented;
}

/**
 * @brief Enumerate component role.
 *
 * @param[in]     cmp_handle: Component handle.
 * @param[in,out] p_role:     Pointer to component role string.
 * @param[in]     index:      Role index being queried.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::component_role_enum(OMX_HANDLETYPE cmp_handle,
                                              OMX_U8        *p_role,
                                              OMX_U32        index)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (m_state == OMX_StateInvalid)
    {
        OMX_SWVDEC_LOG_ERROR("in invalid state");

        retval = OMX_ErrorInvalidState;
    }
    else if (cmp_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("cmp_handle = NULL");

        retval = OMX_ErrorInvalidComponent;
    }
    else if (index > 0)
    {
        OMX_SWVDEC_LOG_HIGH("index '%d' unsupported; no more roles", index);

        retval = OMX_ErrorNoMore;
    }
    else
    {
        memcpy(p_role, m_role_name, OMX_MAX_STRINGNAME_SIZE);

        OMX_SWVDEC_LOG_API("index '%d': '%s'", index, p_role);
    }

    return retval;
}

/**
 * -------------------------
 * SwVdec callback functions
 * -------------------------
 */

/**
 * @brief SwVdec empty buffer done callback.
 *
 * @param[in] swvdec_handle:   SwVdec handle.
 * @param[in] p_buffer_ip:     Pointer to input buffer structure.
 * @param[in] p_client_handle: Pointer to SwVdec's client handle.
 *
 * @retval SWVDEC_STATUS_SUCCESS
 * @retval SWVDEC_STATUS_NULL_POINTER
 * @retval SWVDEC_STATUS_INVALID_PARAMETERS
 */
SWVDEC_STATUS omx_swvdec::swvdec_empty_buffer_done_callback(
    SWVDEC_HANDLE  swvdec_handle,
    SWVDEC_BUFFER *p_buffer_ip,
    void          *p_client_handle)
{
    SWVDEC_STATUS retval = SWVDEC_STATUS_SUCCESS;

    if (p_buffer_ip == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_buffer_ip = NULL");

        retval = SWVDEC_STATUS_NULL_POINTER;
    }
    else if (p_client_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_client_handle = NULL");

        retval = SWVDEC_STATUS_NULL_POINTER;
    }
    else
    {
        omx_swvdec *p_omx_swvdec = (omx_swvdec *) p_client_handle;

        if (swvdec_handle != p_omx_swvdec->m_swvdec_handle)
        {
            OMX_SWVDEC_LOG_ERROR("invalid SwVdec handle");

            retval = SWVDEC_STATUS_INVALID_PARAMETERS;
        }
        else
        {
            p_omx_swvdec->swvdec_empty_buffer_done(p_buffer_ip);
        }
    }

    return retval;
}

/**
 * @brief SwVdec fill buffer done callback.
 *
 * @param[in] swvdec_handle:   SwVdec handle.
 * @param[in] p_buffer_op:     Pointer to output buffer structure.
 * @param[in] p_client_handle: Pointer to SwVdec's client handle.
 *
 * @retval SWVDEC_STATUS_SUCCESS
 * @retval SWVDEC_STATUS_NULL_POINTER
 * @retval SWVDEC_STATUS_INVALID_PARAMETERS
 */
SWVDEC_STATUS omx_swvdec::swvdec_fill_buffer_done_callback(
    SWVDEC_HANDLE  swvdec_handle,
    SWVDEC_BUFFER *p_buffer_op,
    void          *p_client_handle)
{
    SWVDEC_STATUS retval = SWVDEC_STATUS_SUCCESS;

    if (p_buffer_op == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_buffer_op = NULL");

        retval = SWVDEC_STATUS_NULL_POINTER;
    }
    else if (p_client_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_client_handle = NULL");

        retval = SWVDEC_STATUS_NULL_POINTER;
    }
    else
    {
        omx_swvdec *p_omx_swvdec = (omx_swvdec *) p_client_handle;

        if (swvdec_handle != p_omx_swvdec->m_swvdec_handle)
        {
            OMX_SWVDEC_LOG_ERROR("invalid SwVdec handle");

            retval = SWVDEC_STATUS_INVALID_PARAMETERS;
        }
        else
        {
            p_omx_swvdec->swvdec_fill_buffer_done(p_buffer_op);
        }
    }

    return retval;
}

/**
 * @brief SwVdec event handler callback.
 *
 * @param[in] swvdec_handle:   SwVdec handle.
 * @param[in] event:           Event.
 * @param[in] p_data:          Pointer to event-specific data.
 * @param[in] p_client_handle: Pointer to SwVdec's client handle.
 *
 * @retval SWVDEC_STATUS_SUCCESS
 * @retval SWVDEC_STATUS_NULL_POINTER
 * @retval SWVDEC_STATUS_INVALID_PARAMETERS
 */
SWVDEC_STATUS omx_swvdec::swvdec_event_handler_callback(
    SWVDEC_HANDLE swvdec_handle,
    SWVDEC_EVENT  event,
    void         *p_data,
    void         *p_client_handle)
{
    SWVDEC_STATUS retval = SWVDEC_STATUS_SUCCESS;

    if ((event == SWVDEC_EVENT_RELEASE_REFERENCE) && (p_data == NULL))
    {
        OMX_SWVDEC_LOG_ERROR("p_data = NULL");

        retval = SWVDEC_STATUS_NULL_POINTER;
    }
    else if (p_client_handle == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_client_handle = NULL");

        retval = SWVDEC_STATUS_NULL_POINTER;
    }
    else
    {
        omx_swvdec *p_omx_swvdec = (omx_swvdec *) p_client_handle;

        if (swvdec_handle != p_omx_swvdec->m_swvdec_handle)
        {
            OMX_SWVDEC_LOG_ERROR("invalid SwVdec handle");

            retval = SWVDEC_STATUS_INVALID_PARAMETERS;
        }
        else
        {
            p_omx_swvdec->swvdec_event_handler(event, p_data);
        }
    }

    return retval;
}

/**
 * -----------------
 * PRIVATE FUNCTIONS
 * -----------------
 */

/**
 * @brief Set frame dimensions for OMX component & SwVdec core.
 *
 * @param[in] width:  Frame width.
 * @param[in] height: Frame height.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::set_frame_dimensions(unsigned int width,
                                               unsigned int height)
{
    OMX_ERRORTYPE retval;

    m_frame_dimensions.width  = width;
    m_frame_dimensions.height = height;

    OMX_SWVDEC_LOG_HIGH("%d x %d",
                        m_frame_dimensions.width,
                        m_frame_dimensions.height);

    retval = set_frame_dimensions_swvdec();

    return retval;
}

/**
 * @brief Set frame attributes for OMX component & SwVdec core, based on
 *        frame dimensions & color format.
 *
 * @param[in] color_format: Color format.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::set_frame_attributes(
    OMX_COLOR_FORMATTYPE color_format)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    unsigned int width  = m_frame_dimensions.width;
    unsigned int height = m_frame_dimensions.height;

    unsigned int scanlines_uv;

    unsigned int plane_size_y;
    unsigned int plane_size_uv;

    switch (color_format)
    {

    case OMX_QCOM_COLOR_FormatYUV420PackedSemiPlanar32m:
    {
        /**
         * alignment factors:
         *
         * - stride:        128
         * - scanlines_y:    32
         * - scanlines_uv:   16
         * - size:         4096
         */

        m_frame_attributes.stride    = ALIGN(width, 128);
        m_frame_attributes.scanlines = ALIGN(height, 32);

        scanlines_uv = ALIGN(height / 2, 16);

        plane_size_y  = (m_frame_attributes.stride *
                         m_frame_attributes.scanlines);

        plane_size_uv = m_frame_attributes.stride * scanlines_uv;

        m_frame_attributes.size = ALIGN(plane_size_y + plane_size_uv, 4096);

        OMX_SWVDEC_LOG_HIGH("'OMX_QCOM_COLOR_FormatYUV420PackedSemiPlanar32m': "
                            "stride %d, scanlines %d, size %d",
                            m_frame_attributes.stride,
                            m_frame_attributes.scanlines,
                            m_frame_attributes.size);

        break;
    }

    case OMX_COLOR_FormatYUV420SemiPlanar:
    {
        /**
         * alignment factors:
         *
         * - stride:         16
         * - scanlines_y:    16
         * - scanlines_uv:   16
         * - size:         4096
         */

        m_frame_attributes.stride    = ALIGN(width,  16);
        m_frame_attributes.scanlines = ALIGN(height, 16);

        scanlines_uv = ALIGN(height / 2, 16);

        plane_size_y  = (m_frame_attributes.stride *
                         m_frame_attributes.scanlines);

        plane_size_uv = m_frame_attributes.stride * scanlines_uv;

        m_frame_attributes.size = ALIGN(plane_size_y + plane_size_uv, 4096);

        OMX_SWVDEC_LOG_HIGH("'OMX_COLOR_FormatYUV420SemiPlanar': "
                            "stride %d, scanlines %d, size %d",
                            m_frame_attributes.stride,
                            m_frame_attributes.scanlines,
                            m_frame_attributes.size);

        break;
    }

    default:
    {
        OMX_SWVDEC_LOG_ERROR("'0x%08x' color format invalid or unsupported",
                             color_format);

        retval = OMX_ErrorBadParameter;
        break;
    }

    } // switch (color_format)

    if (retval == OMX_ErrorNone)
    {
        m_omx_color_formattype = color_format;

        retval = set_frame_attributes_swvdec();
    }

    return retval;
}

/**
 * @brief Set maximum adaptive playback frame dimensions for OMX component &
 *        SwVdec core.
 *
 * @param[in] width:  Max adaptive playback frame width.
 * @param[in] height: Max adaptive playback frame height.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::set_adaptive_playback(unsigned int max_width,
                                                unsigned int max_height)
{
    OMX_ERRORTYPE retval;

    m_frame_dimensions_max.width  = max_width;
    m_frame_dimensions_max.height = max_height;

    OMX_SWVDEC_LOG_HIGH("%d x %d",
                        m_frame_dimensions_max.width,
                        m_frame_dimensions_max.height);

    retval = set_adaptive_playback_swvdec();

    if (retval == OMX_ErrorNone)
    {
        retval = set_frame_dimensions(max_width, max_height);
    }

    if (retval == OMX_ErrorNone)
    {
        retval = set_frame_attributes(m_omx_color_formattype);
    }

    return retval;
}

/**
 * @brief Get video port format for input or output port.
 *
 * @param[in,out] p_port_format: Pointer to video port format type.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::get_video_port_format(
    OMX_VIDEO_PARAM_PORTFORMATTYPE *p_port_format)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (p_port_format->nPortIndex == OMX_CORE_PORT_INDEX_IP)
    {
        if (p_port_format->nIndex == 0)
        {
            p_port_format->eColorFormat = OMX_COLOR_FormatUnused;

            p_port_format->eCompressionFormat = m_omx_video_codingtype;

            OMX_SWVDEC_LOG_HIGH("color format 0x%08x, "
                                "compression format 0x%08x",
                                p_port_format->eColorFormat,
                                p_port_format->eCompressionFormat);
        }
        else
        {
            OMX_SWVDEC_LOG_HIGH("index '%d' unsupported; "
                                "no more compression formats",
                                p_port_format->nIndex);

            retval = OMX_ErrorNoMore;
        }
    }
    else if (p_port_format->nPortIndex == OMX_CORE_PORT_INDEX_OP)
    {
        if (p_port_format->nIndex == 0)
        {
            p_port_format->eColorFormat =
                OMX_QCOM_COLOR_FormatYUV420PackedSemiPlanar32m;

            p_port_format->eCompressionFormat = OMX_VIDEO_CodingUnused;

            OMX_SWVDEC_LOG_HIGH("color format 0x%08x, "
                                "compression format 0x%08x",
                                p_port_format->eColorFormat,
                                p_port_format->eCompressionFormat);
        }
        else if (p_port_format->nIndex == 1)
        {
            p_port_format->eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;

            p_port_format->eCompressionFormat = OMX_VIDEO_CodingUnused;

            OMX_SWVDEC_LOG_HIGH("color format 0x%08x, "
                                "compression format 0x%08x",
                                p_port_format->eColorFormat,
                                p_port_format->eCompressionFormat);
        }
        else
        {
            OMX_SWVDEC_LOG_HIGH("index '%d' unsupported; no more color formats",
                                p_port_format->nIndex);

            retval = OMX_ErrorNoMore;
        }
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("port index '%d' invalid",
                             p_port_format->nPortIndex);

        retval = OMX_ErrorBadPortIndex;
    }

    return retval;
}

/**
 * @brief Set video port format for input or output port.
 *
 * @param[in] p_port_format: Pointer to video port format type.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::set_video_port_format(
    OMX_VIDEO_PARAM_PORTFORMATTYPE *p_port_format)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (p_port_format->nPortIndex == OMX_CORE_PORT_INDEX_IP)
    {
        OMX_SWVDEC_LOG_HIGH("OMX_IndexParamVideoPortFormat, port index 0; "
                            "doing nothing");
    }
    else if (p_port_format->nPortIndex == OMX_CORE_PORT_INDEX_OP)
    {
        retval = set_frame_attributes(p_port_format->eColorFormat);
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("port index '%d' invalid",
                             p_port_format->nPortIndex);

        retval = OMX_ErrorBadPortIndex;
    }

    return retval;
}

/**
 * @brief Get port definition for input or output port.
 *
 * @param[in,out] p_port_def: Pointer to port definition type.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::get_port_definition(
    OMX_PARAM_PORTDEFINITIONTYPE *p_port_def)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    p_port_def->eDomain = OMX_PortDomainVideo;

    if (p_port_def->nPortIndex == OMX_CORE_PORT_INDEX_IP)
    {
        if ((retval = get_buffer_requirements_swvdec(OMX_CORE_PORT_INDEX_IP)) !=
            OMX_ErrorNone)
        {
            goto get_port_definition_exit;
        }

        p_port_def->eDir               = OMX_DirInput;
        p_port_def->nBufferCountActual = m_port_ip.def.nBufferCountActual;
        p_port_def->nBufferCountMin    = m_port_ip.def.nBufferCountMin;
        p_port_def->nBufferSize        = m_port_ip.def.nBufferSize;
        p_port_def->bEnabled           = m_port_ip.enabled;
        p_port_def->bPopulated         = m_port_ip.populated;

        // VTS uses input port dimensions to set OP dimensions
        if ((retval = get_frame_dimensions_swvdec()) != OMX_ErrorNone)
        {
            goto get_port_definition_exit;
        }

        p_port_def->format.video.nFrameWidth  = m_frame_dimensions.width;
        p_port_def->format.video.nFrameHeight = m_frame_dimensions.height;

        OMX_SWVDEC_LOG_HIGH("port index %d: "
                            "count actual %d, count min %d, size %d, %d x %d",
                            p_port_def->nPortIndex,
                            p_port_def->nBufferCountActual,
                            p_port_def->nBufferCountMin,
                            p_port_def->nBufferSize,
                            p_port_def->format.video.nFrameWidth,
                            p_port_def->format.video.nFrameHeight);

        p_port_def->format.video.eColorFormat       = OMX_COLOR_FormatUnused;
        p_port_def->format.video.eCompressionFormat = m_omx_video_codingtype;
    }
    else if (p_port_def->nPortIndex == OMX_CORE_PORT_INDEX_OP)
    {
        if ((retval = get_frame_dimensions_swvdec()) != OMX_ErrorNone)
        {
            goto get_port_definition_exit;
        }

        p_port_def->format.video.nFrameWidth  = m_frame_dimensions.width;
        p_port_def->format.video.nFrameHeight = m_frame_dimensions.height;

        if (m_port_reconfig_inprogress)
        {
            if ((retval = set_frame_attributes(m_omx_color_formattype)) !=
                OMX_ErrorNone)
            {
                goto get_port_definition_exit;
            }
        }

        if ((retval = get_frame_attributes_swvdec()) != OMX_ErrorNone)
        {
            goto get_port_definition_exit;
        }

        p_port_def->format.video.nStride      = m_frame_attributes.stride;
        p_port_def->format.video.nSliceHeight = m_frame_attributes.scanlines;

        OMX_SWVDEC_LOG_HIGH("port index %d: "
                            "%d x %d, stride %d, sliceheight %d",
                            p_port_def->nPortIndex,
                            p_port_def->format.video.nFrameWidth,
                            p_port_def->format.video.nFrameHeight,
                            p_port_def->format.video.nStride,
                            p_port_def->format.video.nSliceHeight);

        /**
         * Query to SwVdec core for buffer requirements is not allowed in
         * executing state since it will overwrite the component's buffer
         * requirements updated via the most recent set_parameter().
         *
         * Buffer requirements communicated to component via set_parameter() are
         * not propagated to SwVdec core.
         *
         * The only execption is if port reconfiguration is in progress, in
         * which case the query to SwVdec core is required since buffer
         * requirements can change based on new dimensions.
         */
        if ((m_state != OMX_StateExecuting) || m_port_reconfig_inprogress)
        {
            if ((retval =
                 get_buffer_requirements_swvdec(OMX_CORE_PORT_INDEX_OP)) !=
                OMX_ErrorNone)
            {
                goto get_port_definition_exit;
            }
        }

        p_port_def->eDir               = OMX_DirOutput;
        p_port_def->nBufferCountActual = m_port_op.def.nBufferCountActual;
        p_port_def->nBufferCountMin    = m_port_op.def.nBufferCountMin;
        p_port_def->nBufferSize        = m_port_op.def.nBufferSize;
        p_port_def->bEnabled           = m_port_op.enabled;
        p_port_def->bPopulated         = m_port_op.populated;

        OMX_SWVDEC_LOG_HIGH("port index %d: "
                            "count actual %d, count min %d, size %d",
                            p_port_def->nPortIndex,
                            p_port_def->nBufferCountActual,
                            p_port_def->nBufferCountMin,
                            p_port_def->nBufferSize);

        p_port_def->format.video.eColorFormat       = m_omx_color_formattype;
        p_port_def->format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;

        if (m_omx_color_formattype ==
            OMX_QCOM_COLOR_FormatYUV420PackedSemiPlanar32m)
        {
            OMX_SWVDEC_LOG_HIGH(
                "port index %d: color format '0x%08x': "
                "OMX_QCOM_COLOR_FormatYUV420PackedSemiPlanar32m",
                p_port_def->nPortIndex,
                p_port_def->format.video.eColorFormat);
        }
        else if (m_omx_color_formattype == OMX_COLOR_FormatYUV420SemiPlanar)
        {
            OMX_SWVDEC_LOG_HIGH("port index %d: color format '0x%08x': "
                                "OMX_COLOR_FormatYUV420SemiPlanar",
                                p_port_def->nPortIndex,
                                p_port_def->format.video.eColorFormat);
        }
        else
        {
            assert(0);
            retval = OMX_ErrorUndefined;
        }
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("port index '%d' invalid", p_port_def->nPortIndex);

        retval = OMX_ErrorBadPortIndex;
    }

get_port_definition_exit:
    return retval;
}

/**
 * @brief Set port definition for input or output port.
 *
 * @param[in] p_port_def: Pointer to port definition type.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::set_port_definition(
    OMX_PARAM_PORTDEFINITIONTYPE *p_port_def)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    OMX_SWVDEC_LOG_HIGH("port index %d: "
                        "count actual %d, count min %d, size %d",
                        p_port_def->nPortIndex,
                        p_port_def->nBufferCountActual,
                        p_port_def->nBufferCountMin,
                        p_port_def->nBufferSize);

    if (p_port_def->nPortIndex == OMX_CORE_PORT_INDEX_IP)
    {
        m_port_ip.def.nBufferCountActual = p_port_def->nBufferCountActual;
        m_port_ip.def.nBufferCountMin    = p_port_def->nBufferCountMin;
        m_port_ip.def.nBufferSize        = p_port_def->nBufferSize;
    }
    else if (p_port_def->nPortIndex == OMX_CORE_PORT_INDEX_OP)
    {
        /**
         * OMX component's output port nBufferSize is not updated based on what
         * IL client sends; instead it is updated based on the possibly updated
         * frame attributes.
         *
         * This is because set_parameter() for output port definition only has
         * updates to buffer counts or frame dimensions.
         */

        m_port_op.def.nBufferCountActual = p_port_def->nBufferCountActual;
        m_port_op.def.nBufferCountMin    = p_port_def->nBufferCountMin;

        OMX_SWVDEC_LOG_HIGH("port index %d: %d x %d",
                            p_port_def->nPortIndex,
                            p_port_def->format.video.nFrameWidth,
                            p_port_def->format.video.nFrameHeight);

        /**
         * Update frame dimensions & attributes if:
         *
         * 1. not in adaptive playback mode
         *    OR
         * 2. new frame dimensions greater than adaptive playback mode's
         *    max frame dimensions
         */

        if ((m_adaptive_playback_mode == false) ||
            (p_port_def->format.video.nFrameWidth >
             m_frame_dimensions_max.width) ||
            (p_port_def->format.video.nFrameHeight >
             m_frame_dimensions_max.height))
        {
            OMX_SWVDEC_LOG_HIGH("updating frame dimensions & attributes");

            if ((retval =
                 set_frame_dimensions(p_port_def->format.video.nFrameWidth,
                                      p_port_def->format.video.nFrameHeight)) !=
                OMX_ErrorNone)
            {
                goto set_port_definition_exit;
            }

            if ((retval = set_frame_attributes(m_omx_color_formattype)) !=
                OMX_ErrorNone)
            {
                goto set_port_definition_exit;
            }

            // nBufferSize updated based on (possibly new) frame attributes

            m_port_op.def.nBufferSize = m_frame_attributes.size;
        }
        else
        {
            OMX_SWVDEC_LOG_HIGH("not updating frame dimensions & attributes");
        }
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("port index '%d' invalid", p_port_def->nPortIndex);

        retval = OMX_ErrorBadPortIndex;
    }

set_port_definition_exit:
    return retval;
}

/**
 * @brief Get supported profile & level.
 *
 * The supported profiles & levels are not queried from SwVdec core, but
 * hard-coded. This should ideally be replaced with a query to SwVdec core.
 *
 * @param[in,out] p_profilelevel: Pointer to video profile & level type.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::get_supported_profilelevel(
    OMX_VIDEO_PARAM_PROFILELEVELTYPE *p_profilelevel)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (p_profilelevel == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_profilelevel = NULL");

        retval = OMX_ErrorBadParameter;
        goto get_supported_profilelevel_exit;
    }

    if (p_profilelevel->nPortIndex != OMX_CORE_PORT_INDEX_IP)
    {
        OMX_SWVDEC_LOG_ERROR("port index '%d' invalid",
                             p_profilelevel->nPortIndex);

        retval = OMX_ErrorBadPortIndex;
        goto get_supported_profilelevel_exit;
    }

    if (m_omx_video_codingtype == OMX_VIDEO_CodingH263)
    {
        if (p_profilelevel->nProfileIndex == 0)
        {
            p_profilelevel->eProfile = OMX_VIDEO_H263ProfileBaseline;
            p_profilelevel->eLevel   = OMX_VIDEO_H263Level70;

            OMX_SWVDEC_LOG_HIGH("H.263 baseline profile, level 70");
        }
        else
        {
            OMX_SWVDEC_LOG_HIGH("profile index '%d' unsupported; "
                                "no more profiles",
                                p_profilelevel->nProfileIndex);

            retval = OMX_ErrorNoMore;
        }
    }
    else if ((m_omx_video_codingtype == OMX_VIDEO_CodingMPEG4) ||
             (m_omx_video_codingtype ==
              ((OMX_VIDEO_CODINGTYPE) QOMX_VIDEO_CodingDivx)))
    {
        if (p_profilelevel->nProfileIndex == 0)
        {
            p_profilelevel->eProfile = OMX_VIDEO_MPEG4ProfileSimple;
            p_profilelevel->eLevel   = OMX_VIDEO_MPEG4Level5;

            OMX_SWVDEC_LOG_HIGH("MPEG-4 simple profile, level 5");
        }
        else if (p_profilelevel->nProfileIndex == 1)
        {
            p_profilelevel->eProfile = OMX_VIDEO_MPEG4ProfileAdvancedSimple;
            p_profilelevel->eLevel   = OMX_VIDEO_MPEG4Level5;

            OMX_SWVDEC_LOG_HIGH("MPEG-4 advanced simple profile, level 5");
        }
        else
        {
            OMX_SWVDEC_LOG_HIGH("profile index '%d' unsupported; "
                                "no more profiles",
                                p_profilelevel->nProfileIndex);

            retval = OMX_ErrorNoMore;
        }
    }
    else
    {
        assert(0);
        retval = OMX_ErrorUndefined;
    }

get_supported_profilelevel_exit:
    return retval;
}

/**
 * @brief Describe color format.
 *
 * @param[in,out] p_params: Pointer to 'DescribeColorFormatParams' structure.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::describe_color_format(
    DescribeColorFormatParams *p_params)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (p_params == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_params = NULL");

        retval = OMX_ErrorBadParameter;
    }
    else
    {
        MediaImage *p_img = &p_params->sMediaImage;

        switch (p_params->eColorFormat)
        {

        case OMX_QCOM_COLOR_FormatYUV420PackedSemiPlanar32m:
        {
            size_t stride, scanlines;

            p_img->mType = MediaImage::MEDIA_IMAGE_TYPE_YUV;
            p_img->mNumPlanes = 3;

            p_img->mWidth  = p_params->nFrameWidth;
            p_img->mHeight = p_params->nFrameHeight;

            /**
             * alignment factors:
             *
             * - stride:    128
             * - scanlines:  32
             */
            stride    = ALIGN(p_img->mWidth,  128);
            scanlines = ALIGN(p_img->mHeight,  32);

            p_img->mBitDepth = 8;

            // plane 0 (Y)
            p_img->mPlane[MediaImage::Y].mOffset = 0;
            p_img->mPlane[MediaImage::Y].mColInc = 1;
            p_img->mPlane[MediaImage::Y].mRowInc = stride;
            p_img->mPlane[MediaImage::Y].mHorizSubsampling = 1;
            p_img->mPlane[MediaImage::Y].mVertSubsampling  = 1;

            // plane 1 (U)
            p_img->mPlane[MediaImage::U].mOffset = stride * scanlines;
            p_img->mPlane[MediaImage::U].mColInc = 2;
            p_img->mPlane[MediaImage::U].mRowInc = stride;
            p_img->mPlane[MediaImage::U].mHorizSubsampling = 2;
            p_img->mPlane[MediaImage::U].mVertSubsampling  = 2;

            // plane 2 (V)
            p_img->mPlane[MediaImage::V].mOffset = stride * scanlines + 1;
            p_img->mPlane[MediaImage::V].mColInc = 2;
            p_img->mPlane[MediaImage::V].mRowInc = stride;
            p_img->mPlane[MediaImage::V].mHorizSubsampling = 2;
            p_img->mPlane[MediaImage::V].mVertSubsampling  = 2;

            break;
        }

        case OMX_COLOR_FormatYUV420SemiPlanar:
        {
            // do nothing; standard OMX color formats should not be described
            retval = OMX_ErrorUnsupportedSetting;
            break;
        }

        default:
        {
            OMX_SWVDEC_LOG_ERROR("color format '0x%08x' invalid/unsupported",
                                 p_params->eColorFormat);

            p_img->mType = MediaImage::MEDIA_IMAGE_TYPE_UNKNOWN;

            retval = OMX_ErrorBadParameter;
            break;
        }

        } // switch (p_params->eColorFormat)
    }

    return retval;
}

/**
 * @brief Set QTI vendor-specific port definition for input or output port.
 *
 * @param[in] p_port_def: Pointer to QTI vendor-specific port definition type.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::set_port_definition_qcom(
    OMX_QCOM_PARAM_PORTDEFINITIONTYPE *p_port_def)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (p_port_def->nPortIndex == OMX_CORE_PORT_INDEX_IP)
    {
        switch (p_port_def->nFramePackingFormat)
        {

        case OMX_QCOM_FramePacking_Arbitrary:
        {
            OMX_SWVDEC_LOG_HIGH("OMX_QCOM_FramePacking_Arbitrary");

            m_arbitrary_bytes_mode = true;

            break;
        }

        case OMX_QCOM_FramePacking_OnlyOneCompleteFrame:
        {
            OMX_SWVDEC_LOG_HIGH(
                "OMX_QCOM_FramePacking_OnlyOneCompleteFrame");

            break;
        }

        default:
        {
            OMX_SWVDEC_LOG_ERROR(
                "frame packing format '%d' unsupported",
                p_port_def->nFramePackingFormat);

            retval = OMX_ErrorUnsupportedSetting;
            break;
        }

        }
    }
    else if (p_port_def->nPortIndex == OMX_CORE_PORT_INDEX_OP)
    {
        OMX_SWVDEC_LOG_HIGH("nMemRegion %d, nCacheAttr %d",
                            p_port_def->nMemRegion,
                            p_port_def->nCacheAttr);
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("port index '%d' invalid",
                             p_port_def->nPortIndex);

        retval = OMX_ErrorBadPortIndex;
    }

    return retval;
}

/**
 * @brief Set SwVdec frame dimensions based on OMX component frame dimensions.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::set_frame_dimensions_swvdec()
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    SWVDEC_PROPERTY property;

    SWVDEC_STATUS retval_swvdec;

    property.id = SWVDEC_PROPERTY_ID_FRAME_DIMENSIONS;

    property.info.frame_dimensions.width  = m_frame_dimensions.width;
    property.info.frame_dimensions.height = m_frame_dimensions.height;

    if ((retval_swvdec = swvdec_setproperty(m_swvdec_handle, &property)) !=
        SWVDEC_STATUS_SUCCESS)
    {
        retval = retval_swvdec2omx(retval_swvdec);
    }

    return retval;
}

/**
 * @brief Set SwVdec frame attributes based on OMX component frame attributes.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::set_frame_attributes_swvdec()
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    SWVDEC_FRAME_ATTRIBUTES *p_frame_attributes;

    SWVDEC_PROPERTY property;

    SWVDEC_STATUS retval_swvdec;

    p_frame_attributes = &property.info.frame_attributes;

    property.id = SWVDEC_PROPERTY_ID_FRAME_ATTRIBUTES;

    p_frame_attributes->color_format = SWVDEC_COLOR_FORMAT_SEMIPLANAR_NV12;

    p_frame_attributes->stride    = m_frame_attributes.stride;
    p_frame_attributes->scanlines = m_frame_attributes.scanlines;
    p_frame_attributes->size      = m_frame_attributes.size;

    if ((retval_swvdec = swvdec_setproperty(m_swvdec_handle, &property)) !=
        SWVDEC_STATUS_SUCCESS)
    {
        retval = retval_swvdec2omx(retval_swvdec);
    }

    return retval;
}

/**
 * @brief Set maximum adaptive playback frame dimensions for SwVdec core.
 */
OMX_ERRORTYPE omx_swvdec::set_adaptive_playback_swvdec()
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    SWVDEC_PROPERTY property;

    SWVDEC_STATUS retval_swvdec;

    property.id = SWVDEC_PROPERTY_ID_ADAPTIVE_PLAYBACK;

    property.info.frame_dimensions.width  = m_frame_dimensions_max.width;
    property.info.frame_dimensions.height = m_frame_dimensions_max.height;

    if ((retval_swvdec = swvdec_setproperty(m_swvdec_handle, &property)) !=
        SWVDEC_STATUS_SUCCESS)
    {
        retval = retval_swvdec2omx(retval_swvdec);
    }

    return retval;
}

/**
 * @brief Get SwVdec frame dimensions and set OMX component frame dimensions.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::get_frame_dimensions_swvdec()
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    SWVDEC_PROPERTY property;

    SWVDEC_STATUS retval_swvdec;

    property.id = SWVDEC_PROPERTY_ID_FRAME_DIMENSIONS;

    if ((retval_swvdec = swvdec_getproperty(m_swvdec_handle, &property)) !=
        SWVDEC_STATUS_SUCCESS)
    {
        retval = retval_swvdec2omx(retval_swvdec);
    }
    else
    {
        m_frame_dimensions.width  = property.info.frame_dimensions.width;
        m_frame_dimensions.height = property.info.frame_dimensions.height;
    }

    return retval;
}

/**
 * @brief Get SwVdec frame attributes and set OMX component frame attributes.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::get_frame_attributes_swvdec()
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    SWVDEC_PROPERTY property;

    SWVDEC_STATUS retval_swvdec;

    property.id = SWVDEC_PROPERTY_ID_FRAME_ATTRIBUTES;

    if ((retval_swvdec = swvdec_getproperty(m_swvdec_handle, &property)) !=
        SWVDEC_STATUS_SUCCESS)
    {
        retval = retval_swvdec2omx(retval_swvdec);
    }
    else
    {
        m_frame_attributes.stride    = property.info.frame_attributes.stride;
        m_frame_attributes.scanlines = property.info.frame_attributes.scanlines;
        m_frame_attributes.size      = property.info.frame_attributes.size;
    }

    return retval;
}

/**
 * @brief Get SwVdec buffer requirements; set input or output port definitions.
 *
 * @param[in] port_index: Port index.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::get_buffer_requirements_swvdec(
    unsigned int port_index)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    SWVDEC_PROPERTY property;

    SWVDEC_STATUS retval_swvdec;

    SWVDEC_BUFFER_REQ *p_buffer_req;

    if (port_index == OMX_CORE_PORT_INDEX_IP)
    {
        property.id = SWVDEC_PROPERTY_ID_BUFFER_REQ_IP;

        p_buffer_req = &property.info.buffer_req_ip;

        if ((retval_swvdec = swvdec_getproperty(m_swvdec_handle, &property)) !=
            SWVDEC_STATUS_SUCCESS)
        {
            retval = retval_swvdec2omx(retval_swvdec);
            goto get_buffer_requirements_swvdec_exit;
        }

        m_port_ip.def.nBufferSize        = p_buffer_req->size;
        m_port_ip.def.nBufferCountMin    = p_buffer_req->mincount;
        m_port_ip.def.nBufferCountActual = MAX(p_buffer_req->mincount,
                                               OMX_SWVDEC_IP_BUFFER_COUNT_MIN);
        m_port_ip.def.nBufferAlignment   = p_buffer_req->alignment;

        OMX_SWVDEC_LOG_HIGH("ip port: %d bytes x %d, %d-byte aligned",
                            m_port_ip.def.nBufferSize,
                            m_port_ip.def.nBufferCountActual,
                            m_port_ip.def.nBufferAlignment);
    }
    else if (port_index == OMX_CORE_PORT_INDEX_OP)
    {
        property.id = SWVDEC_PROPERTY_ID_BUFFER_REQ_OP;

        p_buffer_req = &property.info.buffer_req_op;

        if ((retval_swvdec = swvdec_getproperty(m_swvdec_handle, &property)) !=
            SWVDEC_STATUS_SUCCESS)
        {
            retval = retval_swvdec2omx(retval_swvdec);
            goto get_buffer_requirements_swvdec_exit;
        }

        if (m_sync_frame_decoding_mode)
        {
            p_buffer_req->mincount = 1;
        }

        m_port_op.def.nBufferSize        = p_buffer_req->size;
        m_port_op.def.nBufferCountMin    = p_buffer_req->mincount;
        m_port_op.def.nBufferCountActual = MAX(p_buffer_req->mincount,
                                               m_port_op.def.nBufferCountActual);
        m_port_op.def.nBufferAlignment   = p_buffer_req->alignment;

        OMX_SWVDEC_LOG_HIGH("op port: %d bytes x %d, %d-byte aligned",
                            m_port_op.def.nBufferSize,
                            m_port_op.def.nBufferCountActual,
                            m_port_op.def.nBufferAlignment);
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("port index '%d' invalid", port_index);

        retval = OMX_ErrorBadPortIndex;
    }

get_buffer_requirements_swvdec_exit:
    return retval;
}

/**
 * @brief Allocate input buffer, and input buffer info array if ncessary.
 *
 * @param[in,out] pp_buffer_hdr: Pointer to pointer to buffer header type
 *                               structure.
 * @param[in]     p_app_data:    Pointer to IL client app data.
 * @param[in]     size:          Size of buffer to be allocated in bytes.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::buffer_allocate_ip(
    OMX_BUFFERHEADERTYPE **pp_buffer_hdr,
    OMX_PTR                p_app_data,
    OMX_U32                size)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    unsigned int ii;

    if (size != m_port_ip.def.nBufferSize)
    {
        OMX_SWVDEC_LOG_ERROR("requested size (%d bytes) not equal to "
                             "configured size (%d bytes)",
                             size,
                             m_port_ip.def.nBufferSize);

        retval = OMX_ErrorBadParameter;
        goto buffer_allocate_ip_exit;
    }

    if (m_buffer_array_ip == NULL)
    {
        OMX_SWVDEC_LOG_HIGH("allocating buffer info array, %d element%s",
                            m_port_ip.def.nBufferCountActual,
                            (m_port_ip.def.nBufferCountActual > 1) ? "s" : "");

        if ((retval = buffer_allocate_ip_info_array()) != OMX_ErrorNone)
        {
            goto buffer_allocate_ip_exit;
        }
    }

    for (ii = 0; ii < m_port_ip.def.nBufferCountActual; ii++)
    {
        if (m_buffer_array_ip[ii].buffer_populated == false)
        {
            OMX_SWVDEC_LOG_LOW("buffer %d not populated", ii);
            break;
        }
    }

    if (ii < m_port_ip.def.nBufferCountActual)
    {
        int pmem_fd = -1;

        unsigned char *bufferaddr;

        OMX_SWVDEC_LOG_HIGH("ip buffer %d: %d bytes being allocated",
                            ii,
                            size);

        m_buffer_array_ip[ii].ion_info.ion_fd_device =
            ion_memory_alloc_map(&m_buffer_array_ip[ii].ion_info.ion_alloc_data,
                                 &m_buffer_array_ip[ii].ion_info.ion_fd_data,
                                 size,
                                 m_port_ip.def.nBufferAlignment);

        if (m_buffer_array_ip[ii].ion_info.ion_fd_device < 0)
        {
            retval = OMX_ErrorInsufficientResources;
            goto buffer_allocate_ip_exit;
        }

        pmem_fd = m_buffer_array_ip[ii].ion_info.ion_fd_data.fd;

        bufferaddr = (unsigned char *) mmap(NULL,
                                            size,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED,
                                            pmem_fd,
                                            0);

        if (bufferaddr == MAP_FAILED)
        {
            OMX_SWVDEC_LOG_ERROR("mmap() failed for fd %d of size %d",
                                 pmem_fd,
                                 size);

            close(pmem_fd);
            ion_memory_free(&m_buffer_array_ip[ii].ion_info);

            retval = OMX_ErrorInsufficientResources;
            goto buffer_allocate_ip_exit;
        }

        *pp_buffer_hdr = &m_buffer_array_ip[ii].buffer_header;

        m_buffer_array_ip[ii].buffer_payload.bufferaddr  = bufferaddr;
        m_buffer_array_ip[ii].buffer_payload.pmem_fd     = pmem_fd;
        m_buffer_array_ip[ii].buffer_payload.buffer_len  = size;
        m_buffer_array_ip[ii].buffer_payload.mmaped_size = size;
        m_buffer_array_ip[ii].buffer_payload.offset      = 0;

        m_buffer_array_ip[ii].buffer_swvdec.p_buffer      = bufferaddr;
        m_buffer_array_ip[ii].buffer_swvdec.size          = size;
        m_buffer_array_ip[ii].buffer_swvdec.p_client_data =
            (void *) ((unsigned long) ii);

        m_buffer_array_ip[ii].buffer_populated = true;

        OMX_SWVDEC_LOG_HIGH("ip buffer %d: %p, %d bytes",
                            ii,
                            bufferaddr,
                            size);

        (*pp_buffer_hdr)->pBuffer           = (OMX_U8 *) bufferaddr;
        (*pp_buffer_hdr)->nSize             = sizeof(OMX_BUFFERHEADERTYPE);
        (*pp_buffer_hdr)->nVersion.nVersion = OMX_SPEC_VERSION;
        (*pp_buffer_hdr)->nAllocLen         = size;
        (*pp_buffer_hdr)->pAppPrivate       = p_app_data;
        (*pp_buffer_hdr)->nInputPortIndex   = OMX_CORE_PORT_INDEX_IP;
        (*pp_buffer_hdr)->pInputPortPrivate =
            (void *) &(m_buffer_array_ip[ii].buffer_payload);

        m_port_ip.populated   = port_ip_populated();
        m_port_ip.unpopulated = OMX_FALSE;
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("all %d ip buffers allocated",
                             m_port_ip.def.nBufferCountActual);

        retval = OMX_ErrorInsufficientResources;
    }

buffer_allocate_ip_exit:
    return retval;
}

/**
 * @brief Allocate output buffer, and output buffer info array if necessary.
 *
 * @param[in,out] pp_buffer_hdr: Pointer to pointer to buffer header type
 *                               structure.
 * @param[in]     p_app_data:    Pointer to IL client app data.
 * @param[in]     size:          Size of buffer to be allocated in bytes.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::buffer_allocate_op(
    OMX_BUFFERHEADERTYPE **pp_buffer_hdr,
    OMX_PTR                p_app_data,
    OMX_U32                size)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    unsigned int ii;

    if (size != m_port_op.def.nBufferSize)
    {
        OMX_SWVDEC_LOG_ERROR("requested size (%d bytes) not equal to "
                             "configured size (%d bytes)",
                             size,
                             m_port_op.def.nBufferSize);

        retval = OMX_ErrorBadParameter;
        goto buffer_allocate_op_exit;
    }

    if (m_buffer_array_op == NULL)
    {
        OMX_SWVDEC_LOG_HIGH("allocating buffer info array, %d element%s",
                            m_port_op.def.nBufferCountActual,
                            (m_port_op.def.nBufferCountActual > 1) ? "s" : "");

        if ((retval = buffer_allocate_op_info_array()) != OMX_ErrorNone)
        {
            goto buffer_allocate_op_exit;
        }
    }

    for (ii = 0; ii < m_port_op.def.nBufferCountActual; ii++)
    {
        if (m_buffer_array_op[ii].buffer_populated == false)
        {
            OMX_SWVDEC_LOG_LOW("buffer %d not populated", ii);
            break;
        }
    }

    if (ii < m_port_op.def.nBufferCountActual)
    {
        int pmem_fd = -1;

        unsigned char *bufferaddr;

        OMX_SWVDEC_LOG_HIGH("op buffer %d: %d bytes being allocated",
                            ii,
                            size);

        m_buffer_array_op[ii].ion_info.ion_fd_device =
            ion_memory_alloc_map(&m_buffer_array_op[ii].ion_info.ion_alloc_data,
                                 &m_buffer_array_op[ii].ion_info.ion_fd_data,
                                 size,
                                 m_port_op.def.nBufferAlignment);

        if (m_buffer_array_op[ii].ion_info.ion_fd_device < 0)
        {
            retval = OMX_ErrorInsufficientResources;
            goto buffer_allocate_op_exit;
        }

        pmem_fd = m_buffer_array_op[ii].ion_info.ion_fd_data.fd;

        bufferaddr = (unsigned char *) mmap(NULL,
                                            size,
                                            PROT_READ | PROT_WRITE,
                                            MAP_SHARED,
                                            pmem_fd,
                                            0);

        if (bufferaddr == MAP_FAILED)
        {
            OMX_SWVDEC_LOG_ERROR("mmap() failed for fd %d of size %d",
                                 pmem_fd,
                                 size);

            close(pmem_fd);
            ion_memory_free(&m_buffer_array_op[ii].ion_info);

            retval = OMX_ErrorInsufficientResources;
            goto buffer_allocate_op_exit;
        }

        *pp_buffer_hdr = &m_buffer_array_op[ii].buffer_header;

        m_buffer_array_op[ii].buffer_payload.bufferaddr  = bufferaddr;
        m_buffer_array_op[ii].buffer_payload.pmem_fd     = pmem_fd;
        m_buffer_array_op[ii].buffer_payload.buffer_len  = size;
        m_buffer_array_op[ii].buffer_payload.mmaped_size = size;
        m_buffer_array_op[ii].buffer_payload.offset      = 0;

        m_buffer_array_op[ii].buffer_swvdec.p_buffer      = bufferaddr;
        m_buffer_array_op[ii].buffer_swvdec.size          = size;
        m_buffer_array_op[ii].buffer_swvdec.p_client_data =
            (void *) ((unsigned long) ii);

        m_buffer_array_op[ii].buffer_populated = true;

        OMX_SWVDEC_LOG_HIGH("op buffer %d: %p, %d bytes",
                            ii,
                            bufferaddr,
                            size);

        (*pp_buffer_hdr)->pBuffer            = (OMX_U8 *) bufferaddr;
        (*pp_buffer_hdr)->nSize              = sizeof(OMX_BUFFERHEADERTYPE);
        (*pp_buffer_hdr)->nVersion.nVersion  = OMX_SPEC_VERSION;
        (*pp_buffer_hdr)->nAllocLen          = size;
        (*pp_buffer_hdr)->pAppPrivate        = p_app_data;
        (*pp_buffer_hdr)->nOutputPortIndex   = OMX_CORE_PORT_INDEX_OP;
        (*pp_buffer_hdr)->pOutputPortPrivate =
            (void *) &(m_buffer_array_op[ii].buffer_payload);

        m_port_op.populated   = port_op_populated();
        m_port_op.unpopulated = OMX_FALSE;
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("all %d op buffers allocated",
                             m_port_op.def.nBufferCountActual);

        retval = OMX_ErrorInsufficientResources;
    }

buffer_allocate_op_exit:
    return retval;
}

/**
 * @brief Allocate input buffer info array.
 */
OMX_ERRORTYPE omx_swvdec::buffer_allocate_ip_info_array()
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    unsigned int ii;

    OMX_BUFFERHEADERTYPE *p_buffer_hdr;

    if (m_buffer_array_ip != NULL)
    {
        OMX_SWVDEC_LOG_ERROR("buffer info array already allocated");

        retval = OMX_ErrorInsufficientResources;
        goto buffer_allocate_ip_info_array_exit;
    }

    OMX_SWVDEC_LOG_HIGH("allocating buffer info array, %d element%s",
                        m_port_ip.def.nBufferCountActual,
                        (m_port_ip.def.nBufferCountActual > 1) ? "s" : "");

    m_buffer_array_ip =
        (OMX_SWVDEC_BUFFER_INFO *) calloc(sizeof(OMX_SWVDEC_BUFFER_INFO),
                                          m_port_ip.def.nBufferCountActual);

    if (m_buffer_array_ip == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("failed to allocate buffer info array; "
                             "%d element%s, %zu bytes requested",
                             m_port_ip.def.nBufferCountActual,
                             (m_port_ip.def.nBufferCountActual > 1) ? "s" : "",
                             sizeof(OMX_SWVDEC_BUFFER_INFO) *
                             m_port_ip.def.nBufferCountActual);

        retval = OMX_ErrorInsufficientResources;
        goto buffer_allocate_ip_info_array_exit;
    }

    for (ii = 0; ii < m_port_ip.def.nBufferCountActual; ii++)
    {
        p_buffer_hdr = &m_buffer_array_ip[ii].buffer_header;

        // reset file descriptors

        m_buffer_array_ip[ii].buffer_payload.pmem_fd = -1;
        m_buffer_array_ip[ii].ion_info.ion_fd_device = -1;

        m_buffer_array_ip[ii].buffer_swvdec.p_client_data =
            (void *) ((unsigned long) ii);

        p_buffer_hdr->nSize             = sizeof(OMX_BUFFERHEADERTYPE);
        p_buffer_hdr->nVersion.nVersion = OMX_SPEC_VERSION;
        p_buffer_hdr->nInputPortIndex   = OMX_CORE_PORT_INDEX_IP;
        p_buffer_hdr->pInputPortPrivate =
            (void *) &(m_buffer_array_ip[ii].buffer_payload);
    }

buffer_allocate_ip_info_array_exit:
    return retval;
}

/**
 * @brief Allocate output buffer info array.
 */
OMX_ERRORTYPE omx_swvdec::buffer_allocate_op_info_array()
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    unsigned int ii;

    OMX_BUFFERHEADERTYPE *p_buffer_hdr;

    if (m_buffer_array_op != NULL)
    {
        OMX_SWVDEC_LOG_ERROR("buffer info array already allocated");

        retval = OMX_ErrorInsufficientResources;
        goto buffer_allocate_op_info_array_exit;
    }

    OMX_SWVDEC_LOG_HIGH("allocating buffer info array, %d element%s",
                        m_port_op.def.nBufferCountActual,
                        (m_port_op.def.nBufferCountActual > 1) ? "s" : "");

    m_buffer_array_op =
        (OMX_SWVDEC_BUFFER_INFO *) calloc(sizeof(OMX_SWVDEC_BUFFER_INFO),
                                          m_port_op.def.nBufferCountActual);

    if (m_buffer_array_op == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("failed to allocate buffer info array; "
                             "%d element%s, %zu bytes requested",
                             m_port_op.def.nBufferCountActual,
                             (m_port_op.def.nBufferCountActual > 1) ? "s" : "",
                             sizeof(OMX_SWVDEC_BUFFER_INFO) *
                             m_port_op.def.nBufferCountActual);

        retval = OMX_ErrorInsufficientResources;
        goto buffer_allocate_op_info_array_exit;
    }

    for (ii = 0; ii < m_port_op.def.nBufferCountActual; ii++)
    {
        p_buffer_hdr = &m_buffer_array_op[ii].buffer_header;

        // reset file descriptors

        m_buffer_array_op[ii].buffer_payload.pmem_fd = -1;
        m_buffer_array_op[ii].ion_info.ion_fd_device = -1;

        m_buffer_array_op[ii].buffer_swvdec.p_client_data =
            (void *) ((unsigned long) ii);

        p_buffer_hdr->nSize              = sizeof(OMX_BUFFERHEADERTYPE);
        p_buffer_hdr->nVersion.nVersion  = OMX_SPEC_VERSION;
        p_buffer_hdr->nOutputPortIndex   = OMX_CORE_PORT_INDEX_OP;
        p_buffer_hdr->pOutputPortPrivate =
            (void *) &(m_buffer_array_op[ii].buffer_payload);
    }

buffer_allocate_op_info_array_exit:
    return retval;
}

/**
 * @brief Use buffer allocated by IL client; allocate output buffer info array
 *        if necessary.
 *
 * @param[in,out] pp_buffer_hdr: Pointer to pointer to buffer header type
 *                               structure.
 * @param[in]     p_app_data:    Pointer to IL client app data.
 * @param[in]     size:          Size of buffer to be allocated in bytes.
 * @param[in]     p_buffer:      Pointer to buffer to be used.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::buffer_use_op(
    OMX_BUFFERHEADERTYPE **pp_buffer_hdr,
    OMX_PTR                p_app_data,
    OMX_U32                size,
    OMX_U8                *p_buffer)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    unsigned int ii;

    (void) size;

    if (m_buffer_array_op == NULL)
    {
        OMX_SWVDEC_LOG_HIGH("allocating buffer info array, %d element%s",
                            m_port_op.def.nBufferCountActual,
                            (m_port_op.def.nBufferCountActual > 1) ? "s" : "");

        if ((retval = buffer_allocate_op_info_array()) != OMX_ErrorNone)
        {
            goto buffer_use_op_exit;
        }
    }

    if (m_meta_buffer_mode && (m_meta_buffer_array == NULL))
    {
        OMX_SWVDEC_LOG_HIGH("allocating meta buffer info array, %d element%s",
                            m_port_op.def.nBufferCountActual,
                            (m_port_op.def.nBufferCountActual > 1) ? "s" : "");

        if ((retval = meta_buffer_array_allocate()) != OMX_ErrorNone)
        {
            goto buffer_use_op_exit;
        }
    }

    for (ii = 0; ii < m_port_op.def.nBufferCountActual; ii++)
    {
        if (m_buffer_array_op[ii].buffer_populated == false)
        {
            OMX_SWVDEC_LOG_LOW("buffer %d not populated", ii);
            break;
        }
    }

    if (ii < m_port_op.def.nBufferCountActual)
    {
        struct vdec_bufferpayload *p_buffer_payload;

        SWVDEC_BUFFER *p_buffer_swvdec;

        *pp_buffer_hdr   = &m_buffer_array_op[ii].buffer_header;
        p_buffer_payload = &m_buffer_array_op[ii].buffer_payload;
        p_buffer_swvdec  = &m_buffer_array_op[ii].buffer_swvdec;

        if (m_meta_buffer_mode)
        {
            p_buffer_swvdec->size          = m_port_op.def.nBufferSize;
            p_buffer_swvdec->p_client_data = (void *) ((unsigned long) ii);

            m_buffer_array_op[ii].buffer_populated = true;

            (*pp_buffer_hdr)->pBuffer     = p_buffer;
            (*pp_buffer_hdr)->pAppPrivate = p_app_data;
            (*pp_buffer_hdr)->nAllocLen   =
                sizeof(struct VideoDecoderOutputMetaData);

            OMX_SWVDEC_LOG_HIGH("op buffer %d: %p (meta buffer)",
                                ii,
                                *pp_buffer_hdr);

            m_port_op.populated   = port_op_populated();
            m_port_op.unpopulated = OMX_FALSE;
        }
        else if (m_android_native_buffers)
        {
            private_handle_t *p_handle;

            OMX_U8 *p_buffer_mapped;

            p_handle = (private_handle_t *) p_buffer;

            if (((OMX_U32) p_handle->size) < m_port_op.def.nBufferSize)
            {
                OMX_SWVDEC_LOG_ERROR("requested size (%d bytes) not equal to "
                                     "configured size (%d bytes)",
                                     p_handle->size,
                                     m_port_op.def.nBufferSize);

                retval = OMX_ErrorBadParameter;
                goto buffer_use_op_exit;
            }

            m_port_op.def.nBufferSize = p_handle->size;

            p_buffer_mapped = (OMX_U8 *) mmap(NULL,
                                              p_handle->size,
                                              PROT_READ | PROT_WRITE,
                                              MAP_SHARED,
                                              p_handle->fd,
                                              0);

            if (p_buffer_mapped == MAP_FAILED)
            {
                OMX_SWVDEC_LOG_ERROR("mmap() failed for fd %d of size %d",
                                     p_handle->fd,
                                     p_handle->size);

                retval = OMX_ErrorInsufficientResources;
                goto buffer_use_op_exit;
            }

            p_buffer_payload->bufferaddr  = p_buffer_mapped;
            p_buffer_payload->pmem_fd     = p_handle->fd;
            p_buffer_payload->buffer_len  = p_handle->size;
            p_buffer_payload->mmaped_size = p_handle->size;
            p_buffer_payload->offset      = 0;

            p_buffer_swvdec->p_buffer      = p_buffer_mapped;
            p_buffer_swvdec->size          = m_port_op.def.nBufferSize;
            p_buffer_swvdec->p_client_data = (void *) ((unsigned long) ii);

            m_buffer_array_op[ii].buffer_populated = true;

            (*pp_buffer_hdr)->pBuffer     = (m_android_native_buffers ?
                                             ((OMX_U8 *) p_handle) :
                                             p_buffer_mapped);
            (*pp_buffer_hdr)->pAppPrivate = p_app_data;
            (*pp_buffer_hdr)->nAllocLen   = m_port_op.def.nBufferSize;

            m_buffer_array_op[ii].ion_info.ion_fd_data.fd = p_handle->fd;

            OMX_SWVDEC_LOG_HIGH("op buffer %d: %p",
                                ii,
                                *pp_buffer_hdr);

            m_port_op.populated   = port_op_populated();
            m_port_op.unpopulated = OMX_FALSE;
        }
        else
        {
            OMX_SWVDEC_LOG_ERROR("neither 'meta buffer mode' nor "
                                 "'android native buffers' enabled");

            retval = OMX_ErrorBadParameter;
        }
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("all %d op buffers populated",
                             m_port_op.def.nBufferCountActual);

        retval = OMX_ErrorInsufficientResources;
    }

buffer_use_op_exit:
    return retval;
}

/**
 * @brief De-allocate input buffer.
 *
 * @param[in] p_buffer_hdr: Pointer to buffer header structure.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::buffer_deallocate_ip(
    OMX_BUFFERHEADERTYPE *p_buffer_hdr)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    unsigned int ii;

    if (p_buffer_hdr == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_buffer_hdr = NULL");

        retval = OMX_ErrorBadParameter;
        goto buffer_deallocate_ip_exit;
    }
    else if (m_buffer_array_ip == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("ip buffer array not allocated");

        retval = OMX_ErrorBadParameter;
        goto buffer_deallocate_ip_exit;
    }

    for (ii = 0; ii < m_port_ip.def.nBufferCountActual; ii++)
    {
        if (p_buffer_hdr == &(m_buffer_array_ip[ii].buffer_header))
        {
            OMX_SWVDEC_LOG_LOW("%p has index %d",
                               p_buffer_hdr->pBuffer,
                               ii);
            break;
        }
    }

    if (ii < m_port_ip.def.nBufferCountActual)
    {
        if (m_buffer_array_ip[ii].buffer_payload.pmem_fd > 0)
        {
            m_buffer_array_ip[ii].buffer_populated = false;

            m_port_ip.populated = OMX_FALSE;

            munmap(m_buffer_array_ip[ii].buffer_payload.bufferaddr,
                   m_buffer_array_ip[ii].buffer_payload.mmaped_size);

            close(m_buffer_array_ip[ii].buffer_payload.pmem_fd);
            m_buffer_array_ip[ii].buffer_payload.pmem_fd = -1;

            ion_memory_free(&m_buffer_array_ip[ii].ion_info);

            for (ii = 0; ii < m_port_ip.def.nBufferCountActual; ii++)
            {
                if (m_buffer_array_ip[ii].buffer_populated)
                {
                    break;
                }
            }

            if (ii == m_port_ip.def.nBufferCountActual)
            {
                buffer_deallocate_ip_info_array();

                m_port_ip.unpopulated = OMX_TRUE;
            }
        }
        else
        {
            OMX_SWVDEC_LOG_ERROR("%p: pmem_fd %d",
                                 p_buffer_hdr->pBuffer,
                                 m_buffer_array_ip[ii].buffer_payload.pmem_fd);
        }
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("%p not found", p_buffer_hdr->pBuffer);

        retval = OMX_ErrorBadParameter;
    }

buffer_deallocate_ip_exit:
    return retval;
}

/**
 * @brief De-allocate output buffer.
 *
 * @param[in] p_buffer_hdr: Pointer to buffer header structure.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::buffer_deallocate_op(
    OMX_BUFFERHEADERTYPE *p_buffer_hdr)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    unsigned int ii;

    if (p_buffer_hdr == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_buffer_hdr = NULL");

        retval = OMX_ErrorBadParameter;
        goto buffer_deallocate_op_exit;
    }
    else if (m_buffer_array_op == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("op buffer array not allocated");

        retval = OMX_ErrorBadParameter;
        goto buffer_deallocate_op_exit;
    }

    for (ii = 0; ii < m_port_op.def.nBufferCountActual; ii++)
    {
        if (p_buffer_hdr == &(m_buffer_array_op[ii].buffer_header))
        {
            OMX_SWVDEC_LOG_LOW("%p has index %d",
                               p_buffer_hdr->pBuffer,
                               ii);
            break;
        }
    }

    if (ii < m_port_op.def.nBufferCountActual)
    {
        if (m_meta_buffer_mode)
        {
            // do nothing; munmap() & FD reset done in FBD or RR
        }
        else if (m_android_native_buffers)
        {
            munmap(m_buffer_array_op[ii].buffer_payload.bufferaddr,
                   m_buffer_array_op[ii].buffer_payload.mmaped_size);

            m_buffer_array_op[ii].buffer_payload.pmem_fd = -1;
        }
        else
        {
            munmap(m_buffer_array_op[ii].buffer_payload.bufferaddr,
                   m_buffer_array_op[ii].buffer_payload.mmaped_size);

            close(m_buffer_array_op[ii].buffer_payload.pmem_fd);

            m_buffer_array_op[ii].buffer_payload.pmem_fd = -1;

            ion_memory_free(&m_buffer_array_op[ii].ion_info);
        }

        m_buffer_array_op[ii].buffer_populated = false;

        m_port_op.populated = OMX_FALSE;

        for (ii = 0; ii < m_port_op.def.nBufferCountActual; ii++)
        {
            if (m_buffer_array_op[ii].buffer_populated)
            {
                break;
            }
        }

        if (ii == m_port_op.def.nBufferCountActual)
        {
            buffer_deallocate_op_info_array();

            m_port_op.unpopulated = OMX_TRUE;

            if (m_meta_buffer_mode)
            {
                meta_buffer_array_deallocate();
            }
        }
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("%p not found", p_buffer_hdr->pBuffer);

        retval = OMX_ErrorBadParameter;
    }

buffer_deallocate_op_exit:
    return retval;
}

/**
 * @brief De-allocate input buffer info array.
 */
void omx_swvdec::buffer_deallocate_ip_info_array()
{
    assert(m_buffer_array_ip != NULL);

    free(m_buffer_array_ip);

    m_buffer_array_ip = NULL;
}

/**
 * @brief De-allocate output buffer info array.
 */
void omx_swvdec::buffer_deallocate_op_info_array()
{
    assert(m_buffer_array_op != NULL);

    free(m_buffer_array_op);

    m_buffer_array_op = NULL;
}

/**
 * @brief Allocate meta buffer info array.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::meta_buffer_array_allocate()
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    m_meta_buffer_array = ((OMX_SWVDEC_META_BUFFER_INFO *)
                           calloc(sizeof(OMX_SWVDEC_META_BUFFER_INFO),
                                  m_port_op.def.nBufferCountActual));

    if (m_meta_buffer_array == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("failed to allocate meta_buffer info array; "
                             "%d element%s, %zu bytes requested",
                             m_port_op.def.nBufferCountActual,
                             (m_port_op.def.nBufferCountActual > 1) ? "s" : "",
                             sizeof(OMX_SWVDEC_META_BUFFER_INFO) *
                             m_port_op.def.nBufferCountActual);

        retval = OMX_ErrorInsufficientResources;
    }
    else
    {
        unsigned int ii;

        for (ii = 0; ii < m_port_op.def.nBufferCountActual; ii++)
        {
            m_meta_buffer_array[ii].fd = -1;
        }
    }

    return retval;
}

/**
 * @brief De-allocate meta buffer info array.
 */
void omx_swvdec::meta_buffer_array_deallocate()
{
    assert(m_meta_buffer_array != NULL);

    free(m_meta_buffer_array);

    m_meta_buffer_array = NULL;
}

/**
 * @brief Add meta buffer reference.
 *
 * @param[in] index: Buffer index.
 * @param[in] fd:    File descriptor.
 */
void omx_swvdec::meta_buffer_ref_add(unsigned int index, int fd)
{
    if (m_meta_buffer_array[index].ref_count == 0)
    {
        m_meta_buffer_array[index].fd = fd;
    }

    m_meta_buffer_array[index].ref_count++;
}

/**
 * @brief Remove meta buffer reference.
 *
 * @param[in] index: Buffer index.
 */
void omx_swvdec::meta_buffer_ref_remove(unsigned int index)
{
    pthread_mutex_lock(&m_meta_buffer_array_mutex);

    m_meta_buffer_array[index].ref_count--;

    if (m_meta_buffer_array[index].ref_count == 0)
    {
        m_meta_buffer_array[index].fd = -1;

        munmap(m_buffer_array_op[index].buffer_payload.bufferaddr,
               m_buffer_array_op[index].buffer_payload.mmaped_size);

        m_buffer_array_op[index].buffer_payload.bufferaddr  = NULL;
        m_buffer_array_op[index].buffer_payload.offset      = 0;
        m_buffer_array_op[index].buffer_payload.mmaped_size = 0;

        m_buffer_array_op[index].buffer_swvdec.p_buffer = NULL;
        m_buffer_array_op[index].buffer_swvdec.size     = 0;
    }

    pthread_mutex_unlock(&m_meta_buffer_array_mutex);
}

/**
 * @brief Split MPEG-4 bitstream buffer into multiple frames (if they exist).
 *
 * @param[in,out] offset_array: Array of offsets to frame headers.
 * @param[in]     p_buffer_hdr: Pointer to buffer header.
 *
 * @retval Number of frames in buffer.
 */
unsigned int split_buffer_mpeg4(unsigned int         *offset_array,
                                OMX_BUFFERHEADERTYPE *p_buffer_hdr)
{
    unsigned char *p_buffer = p_buffer_hdr->pBuffer;

    unsigned int byte_count = 0;

    unsigned int num_frame_headers = 0;

    unsigned int next_4bytes;

    while ((byte_count < p_buffer_hdr->nFilledLen) &&
           (num_frame_headers < OMX_SWVDEC_MAX_FRAMES_PER_ETB))
    {
        next_4bytes = *((unsigned int *) p_buffer);

        next_4bytes = __builtin_bswap32(next_4bytes);

        if (next_4bytes == 0x000001B6)
        {
            OMX_SWVDEC_LOG_HIGH("%p, buffer %p: "
                                "frame header at %d bytes offset",
                                p_buffer_hdr,
                                p_buffer_hdr->pBuffer,
                                byte_count);

            offset_array[num_frame_headers] = byte_count;

            num_frame_headers++;

            p_buffer   += 4;
            byte_count += 4;
        }
        else
        {
            p_buffer++;
            byte_count++;
        }
    }

    return num_frame_headers;
}

/**
 * @brief Check if ip port is populated, i.e., if all ip buffers are populated.
 *
 * @retval  true
 * @retval false
 */
OMX_BOOL omx_swvdec::port_ip_populated()
{
    OMX_BOOL retval = OMX_FALSE;

    if (m_buffer_array_ip != NULL)
    {
        unsigned int ii;

        for (ii = 0; ii < m_port_ip.def.nBufferCountActual; ii++)
        {
            if (m_buffer_array_ip[ii].buffer_populated == false)
            {
                break;
            }
        }

        if (ii == m_port_ip.def.nBufferCountActual)
        {
            retval = OMX_TRUE;
        }
    }

    return retval;
}

/**
 * @brief Check if op port is populated, i.e., if all op buffers are populated.
 *
 * @retval  true
 * @retval false
 */
OMX_BOOL omx_swvdec::port_op_populated()
{
    OMX_BOOL retval = OMX_FALSE;

    if (m_buffer_array_op != NULL)
    {
        unsigned int ii;

        for (ii = 0; ii < m_port_op.def.nBufferCountActual; ii++)
        {
            if (m_buffer_array_op[ii].buffer_populated == false)
            {
                break;
            }
        }

        if (ii == m_port_op.def.nBufferCountActual)
        {
            retval = OMX_TRUE;
        }
    }

    return retval;
}

/**
 * @brief Flush input, output, or both input & output ports.
 *
 * @param[in] port_index: Index of port to flush.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::flush(unsigned int port_index)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (((port_index == OMX_CORE_PORT_INDEX_IP) &&
         m_port_ip.flush_inprogress) ||
        ((port_index == OMX_CORE_PORT_INDEX_OP) &&
         m_port_op.flush_inprogress) ||
        ((port_index == OMX_ALL) &&
         m_port_ip.flush_inprogress &&
         m_port_op.flush_inprogress))
    {
        OMX_SWVDEC_LOG_HIGH("flush port index %d already in progress",
                            port_index);
    }
    else
    {
        SWVDEC_FLUSH_TYPE swvdec_flush_type;

        SWVDEC_STATUS retval_swvdec;

        if (port_index == OMX_CORE_PORT_INDEX_IP)
        {
            m_port_ip.flush_inprogress = OMX_TRUE;

            //for VTS test case IP flush , trigger flush all
            // for IP flush, similar behavior is for hwcodecs
            m_port_ip.flush_inprogress = OMX_TRUE;
            m_port_op.flush_inprogress = OMX_TRUE;

            swvdec_flush_type = SWVDEC_FLUSH_TYPE_ALL;

            if ((retval_swvdec = swvdec_flush(m_swvdec_handle,
                                              swvdec_flush_type)) !=
                SWVDEC_STATUS_SUCCESS)
            {
                retval = retval_swvdec2omx(retval_swvdec);
            }
        }
        else if (port_index == OMX_CORE_PORT_INDEX_OP)
        {
            m_port_op.flush_inprogress = OMX_TRUE;

            swvdec_flush_type = (m_port_ip.flush_inprogress ?
                                 SWVDEC_FLUSH_TYPE_ALL :
                                 SWVDEC_FLUSH_TYPE_OP);

            if ((retval_swvdec = swvdec_flush(m_swvdec_handle,
                                              swvdec_flush_type)) !=
                SWVDEC_STATUS_SUCCESS)
            {
                retval = retval_swvdec2omx(retval_swvdec);
            }
        }
        else if (port_index == OMX_ALL)
        {
            m_port_ip.flush_inprogress = OMX_TRUE;
            m_port_op.flush_inprogress = OMX_TRUE;

            swvdec_flush_type = SWVDEC_FLUSH_TYPE_ALL;

            if ((retval_swvdec = swvdec_flush(m_swvdec_handle,
                                              swvdec_flush_type)) !=
                SWVDEC_STATUS_SUCCESS)
            {
                retval = retval_swvdec2omx(retval_swvdec);
            }
        }
        else
        {
            assert(0);
        }
    }

    return retval;
}

/**
 * @brief Allocate & map ION memory.
 */
int omx_swvdec::ion_memory_alloc_map(struct ion_allocation_data *p_alloc_data,
                                     struct ion_fd_data         *p_fd_data,
                                     OMX_U32                     size,
                                     OMX_U32                     alignment)
{
    int fd = -EINVAL;
    int rc = -EINVAL;

    if ((p_alloc_data == NULL) || (p_fd_data == NULL) || (size == 0))
    {
        OMX_SWVDEC_LOG_ERROR("invalid arguments");
        goto ion_memory_alloc_map_exit;
    }

    if ((fd = open("/dev/ion", O_RDONLY)) < 0)
    {
        OMX_SWVDEC_LOG_ERROR("failed to open ion device; fd = %d", fd);
        goto ion_memory_alloc_map_exit;
    }

    p_alloc_data->len          = size;
    p_alloc_data->align        = (alignment < 4096) ? 4096 : alignment;
    p_alloc_data->heap_id_mask = ION_HEAP(ION_IOMMU_HEAP_ID);
    p_alloc_data->flags        = 0;

    OMX_SWVDEC_LOG_LOW("heap_id_mask 0x%08x, len %zu, align %zu",
                       p_alloc_data->heap_id_mask,
                       p_alloc_data->len,
                       p_alloc_data->align);

    rc = ioctl(fd, ION_IOC_ALLOC, p_alloc_data);

    if (rc || (p_alloc_data->handle == 0))
    {
        OMX_SWVDEC_LOG_ERROR("ioctl() for allocation failed");

        close(fd);
        fd = -ENOMEM;

        goto ion_memory_alloc_map_exit;
    }

    p_fd_data->handle = p_alloc_data->handle;

    if (ioctl(fd, ION_IOC_MAP, p_fd_data))
    {
        struct vdec_ion ion_buf_info;

        OMX_SWVDEC_LOG_ERROR("ioctl() for mapping failed");

        ion_buf_info.ion_alloc_data = *p_alloc_data;
        ion_buf_info.ion_fd_device  = fd;
        ion_buf_info.ion_fd_data    = *p_fd_data;

        ion_memory_free(&ion_buf_info);

        p_fd_data->fd = -1;

        close(fd);
        fd = -ENOMEM;

        goto ion_memory_alloc_map_exit;
    }

ion_memory_alloc_map_exit:
    return fd;
}

/**
 * @brief Free ION memory.
 */
void omx_swvdec::ion_memory_free(struct vdec_ion *p_ion_buf_info)
{
    if (p_ion_buf_info == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_ion_buf_info = NULL");
        goto ion_memory_free_exit;
    }

    if (ioctl(p_ion_buf_info->ion_fd_device,
              ION_IOC_FREE,
              &p_ion_buf_info->ion_alloc_data.handle))
    {
        OMX_SWVDEC_LOG_ERROR("ioctl() for freeing failed");
    }

    close(p_ion_buf_info->ion_fd_device);

    p_ion_buf_info->ion_fd_device         = -1;
    p_ion_buf_info->ion_alloc_data.handle =  0;
    p_ion_buf_info->ion_fd_data.fd        = -1;

ion_memory_free_exit:
    return;
}

/**
 * @brief Flush cached ION output buffer.
 *
 * @param[in] index: Index of buffer in output buffer info array.
 */
void omx_swvdec::ion_flush_op(unsigned int index)
{
    if (index < m_port_op.def.nBufferCountActual)
    {
        struct vdec_bufferpayload *p_buffer_payload =
            &m_buffer_array_op[index].buffer_payload;

        if(p_buffer_payload)
        {
            if(p_buffer_payload->bufferaddr != NULL)
            {
                __builtin___clear_cache(reinterpret_cast<char*>((size_t*)p_buffer_payload->bufferaddr),
                    reinterpret_cast<char*>((size_t*)p_buffer_payload->bufferaddr +p_buffer_payload->buffer_len));
            }
        }
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("buffer index '%d' invalid", index);
    }

    return;
}

/**
 * ----------------------------
 * component callback functions
 * ----------------------------
 */

/**
 * @brief Empty buffer done callback.
 *
 * @param[in] p_buffer_ip: Pointer to input buffer structure.
 */
void omx_swvdec::swvdec_empty_buffer_done(SWVDEC_BUFFER *p_buffer_ip)
{
    unsigned long index = (unsigned long) p_buffer_ip->p_client_data;

    m_buffer_array_ip[index].buffer_header.nFilledLen =
        p_buffer_ip->filled_length;

    async_post_event(OMX_SWVDEC_EVENT_EBD,
                     (unsigned long) &m_buffer_array_ip[index].buffer_header,
                     index);
}

/**
 * @brief Fill buffer done callback.
 *
 * @param[in] p_buffer_op: Pointer to output buffer structure.
 */
void omx_swvdec::swvdec_fill_buffer_done(SWVDEC_BUFFER *p_buffer_op)
{
    unsigned long index = (unsigned long) p_buffer_op->p_client_data;

    OMX_BUFFERHEADERTYPE *p_buffer_hdr;

    if (index < ((unsigned long) m_port_op.def.nBufferCountActual))
    {
        p_buffer_hdr = &m_buffer_array_op[index].buffer_header;

        p_buffer_hdr->nFlags     = p_buffer_op->flags;
        p_buffer_hdr->nTimeStamp = p_buffer_op->timestamp;
        p_buffer_hdr->nFilledLen = ((m_meta_buffer_mode &&
                                     p_buffer_op->filled_length) ?
                                    p_buffer_hdr->nAllocLen :
                                    p_buffer_op->filled_length);
    }

    async_post_event(OMX_SWVDEC_EVENT_FBD,
                     (unsigned long) &m_buffer_array_op[index].buffer_header,
                     index);
}

/**
 * @brief Event handler callback.
 *
 * @param[in] event:  Event.
 * @param[in] p_data: Pointer to event-specific data.
 */
void omx_swvdec::swvdec_event_handler(SWVDEC_EVENT event, void *p_data)
{
    switch (event)
    {

    case SWVDEC_EVENT_FLUSH_ALL_DONE:
    {
        async_post_event(OMX_SWVDEC_EVENT_FLUSH_PORT_IP, 0, 0);
        async_post_event(OMX_SWVDEC_EVENT_FLUSH_PORT_OP, 0, 0);

        break;
    }

    case SWVDEC_EVENT_FLUSH_OP_DONE:
    {
        async_post_event(OMX_SWVDEC_EVENT_FLUSH_PORT_OP, 0, 0);

        break;
    }

    case SWVDEC_EVENT_RELEASE_REFERENCE:
    {
        SWVDEC_BUFFER *p_buffer_op = (SWVDEC_BUFFER *) p_data;

        unsigned long index = (unsigned long) p_buffer_op->p_client_data;

        OMX_SWVDEC_LOG_LOW("release reference: %p", p_buffer_op->p_buffer);

        assert(index < ((unsigned long) m_port_op.def.nBufferCountActual));

        if (m_meta_buffer_mode)
        {
            meta_buffer_ref_remove(index);
        }

        break;
    }

    case SWVDEC_EVENT_RECONFIG_REQUIRED:
    {
        async_post_event(OMX_SWVDEC_EVENT_PORT_RECONFIG, 0, 0);

        break;
    }

    case SWVDEC_EVENT_DIMENSIONS_UPDATED:
    {
        async_post_event(OMX_SWVDEC_EVENT_DIMENSIONS_UPDATED, 0, 0);

        break;
    }

    case SWVDEC_EVENT_FATAL_ERROR:
    default:
    {
        async_post_event(OMX_SWVDEC_EVENT_ERROR, OMX_ErrorHardware, 0);

        break;
    }

    }
}

/**
 * @brief Translate SwVdec status return value to OMX error type return value.
 *
 * @param[in] retval_swvdec: SwVdec status return value.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::retval_swvdec2omx(SWVDEC_STATUS retval_swvdec)
{
    OMX_ERRORTYPE retval_omx;

    switch (retval_swvdec) {
        case SWVDEC_STATUS_SUCCESS:
            retval_omx = OMX_ErrorNone;
            break;

        case SWVDEC_STATUS_FAILURE:
            retval_omx = OMX_ErrorUndefined;
            break;

        case SWVDEC_STATUS_NULL_POINTER:
        case SWVDEC_STATUS_INVALID_PARAMETERS:
            retval_omx = OMX_ErrorBadParameter;
            break;

        case SWVDEC_STATUS_INVALID_STATE:
            retval_omx = OMX_ErrorInvalidState;
            break;

        case SWVDEC_STATUS_INSUFFICIENT_RESOURCES:
            retval_omx = OMX_ErrorInsufficientResources;
            break;

        case SWVDEC_STATUS_UNSUPPORTED:
            retval_omx = OMX_ErrorUnsupportedSetting;
            break;

        case SWVDEC_STATUS_NOT_IMPLEMENTED:
            retval_omx = OMX_ErrorNotImplemented;
            break;

        default:
            retval_omx = OMX_ErrorUndefined;
            break;
    }

    return retval_omx;
}

/**
 * @brief Create asynchronous thread.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::async_thread_create()
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    pthread_attr_t thread_attributes;

    if (sem_init(&m_async_thread.sem_thread_created, 0, 0))
    {
        OMX_SWVDEC_LOG_ERROR("failed to create async thread created semaphore");

        retval = OMX_ErrorInsufficientResources;
    }
    else if (sem_init(&m_async_thread.sem_event, 0, 0))
    {
        OMX_SWVDEC_LOG_ERROR("failed to create async thread event semaphore");

        retval = OMX_ErrorInsufficientResources;
    }
    else if (pthread_attr_init(&thread_attributes))
    {
        OMX_SWVDEC_LOG_ERROR("failed to create thread attributes object");

        retval = OMX_ErrorInsufficientResources;
    }
    else if (pthread_attr_setdetachstate(&thread_attributes,
                                         PTHREAD_CREATE_JOINABLE))
    {
        OMX_SWVDEC_LOG_ERROR("failed to set detach state attribute");

        retval = OMX_ErrorInsufficientResources;

        pthread_attr_destroy(&thread_attributes);
    }
    else
    {
        m_async_thread.created = false;
        m_async_thread.exit    = false;

        if (pthread_create(&m_async_thread.handle,
                           &thread_attributes,
                           (void *(*)(void *)) async_thread,
                           this))
        {
            OMX_SWVDEC_LOG_ERROR("failed to create async thread");

            retval = OMX_ErrorInsufficientResources;

            pthread_attr_destroy(&thread_attributes);
        }
        else
        {
            if (pthread_setname_np(m_async_thread.handle, "swvdec_async"))
            {
                // don't return error
                OMX_SWVDEC_LOG_ERROR("failed to set async thread name");
            }

            sem_wait(&m_async_thread.sem_thread_created);

            m_async_thread.created = true;
        }
    }

    return retval;
}

/**
 * @brief Destroy asynchronous thread.
 */
void omx_swvdec::async_thread_destroy()
{
    if (m_async_thread.created)
    {
        m_async_thread.exit = true;

        sem_post(&m_async_thread.sem_event);

        pthread_join(m_async_thread.handle, NULL);

        m_async_thread.created = false;
    }

    m_async_thread.exit = false;

    sem_destroy(&m_async_thread.sem_event);
    sem_destroy(&m_async_thread.sem_thread_created);
}

/**
 * @brief Post event to appropriate queue.
 *
 * @param[in] event_id:     Event ID.
 * @param[in] event_param1: Event parameter 1.
 * @param[in] event_param2: Event parameter 2.
 */
void omx_swvdec::async_post_event(unsigned long event_id,
                                  unsigned long event_param1,
                                  unsigned long event_param2)
{
    OMX_SWVDEC_EVENT_INFO event_info;

    event_info.event_id     = event_id;
    event_info.event_param1 = event_param1;
    event_info.event_param2 = event_param2;

    switch (event_id)
    {

    case OMX_SWVDEC_EVENT_ETB:
    case OMX_SWVDEC_EVENT_EBD:
    {
        m_queue_port_ip.push(&event_info);
        break;
    }

    case OMX_SWVDEC_EVENT_FTB:
    case OMX_SWVDEC_EVENT_FBD:
    {
        m_queue_port_op.push(&event_info);
        break;
    }

    default:
    {
        m_queue_command.push(&event_info);
        break;
    }

    }

    sem_post(&m_async_thread.sem_event);
}

/**
 * @brief Asynchronous thread.
 *
 * @param[in] p_cmp: Pointer to OMX SwVdec component class.
 */
void omx_swvdec::async_thread(void *p_cmp)
{
    if (p_cmp == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_cmp = NULL");
    }
    else
    {
        omx_swvdec *p_omx_swvdec = (omx_swvdec *) p_cmp;

        ASYNC_THREAD *p_async_thread = &p_omx_swvdec->m_async_thread;

        OMX_SWVDEC_LOG_HIGH("created");

        sem_post(&p_async_thread->sem_thread_created);

        while (p_async_thread->exit == false)
        {
            sem_wait(&p_async_thread->sem_event);

            if (p_async_thread->exit == true)
            {
                break;
            }

            p_omx_swvdec->async_process_event(p_cmp);
        }
    }

    OMX_SWVDEC_LOG_HIGH("exiting");
}

/**
 * @brief Process event.
 *
 * @param[in] p_cmp: Pointer to OMX SwVdec component class.
 */
void omx_swvdec::async_process_event(void *p_cmp)
{
    omx_swvdec *p_omx_swvdec;

    OMX_SWVDEC_EVENT_INFO event_info;

    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (p_cmp == NULL)
    {
        OMX_SWVDEC_LOG_ERROR("p_cmp = NULL");

        goto async_process_event_exit;
    }

    p_omx_swvdec = (omx_swvdec *) p_cmp;

    // NOTE: queues popped in order of priority; do not change!

    if ((p_omx_swvdec->m_queue_command.pop(&event_info) == false) &&
        (p_omx_swvdec->m_queue_port_op.pop(&event_info) == false) &&
        (p_omx_swvdec->m_queue_port_ip.pop(&event_info) == false))
    {
        OMX_SWVDEC_LOG_LOW("no event popped");

        goto async_process_event_exit;
    }

    switch (event_info.event_id)
    {

    case OMX_SWVDEC_EVENT_CMD:
    {
        OMX_COMMANDTYPE cmd   = (OMX_COMMANDTYPE) event_info.event_param1;
        OMX_U32         param = (OMX_U32)         event_info.event_param2;

        retval = p_omx_swvdec->async_process_event_cmd(cmd, param);
        break;
    }

    case OMX_SWVDEC_EVENT_CMD_ACK:
    {
        OMX_COMMANDTYPE cmd   = (OMX_COMMANDTYPE) event_info.event_param1;
        OMX_U32         param = (OMX_U32)         event_info.event_param2;

        retval = p_omx_swvdec->async_process_event_cmd_ack(cmd, param);
        break;
    }

    case OMX_SWVDEC_EVENT_ERROR:
    {
        OMX_ERRORTYPE error_code = (OMX_ERRORTYPE) event_info.event_param1;

        retval = p_omx_swvdec->async_process_event_error(error_code);
        break;
    }

    case OMX_SWVDEC_EVENT_ETB:
    {
        OMX_BUFFERHEADERTYPE *p_buffer_hdr =
            (OMX_BUFFERHEADERTYPE *) event_info.event_param1;

        unsigned int index = event_info.event_param2;

        retval = p_omx_swvdec->async_process_event_etb(p_buffer_hdr, index);
        break;
    }

    case OMX_SWVDEC_EVENT_FTB:
    {
        OMX_BUFFERHEADERTYPE *p_buffer_hdr =
            (OMX_BUFFERHEADERTYPE *) event_info.event_param1;

        unsigned int index = event_info.event_param2;

        retval = p_omx_swvdec->async_process_event_ftb(p_buffer_hdr, index);
        break;
    }

    case OMX_SWVDEC_EVENT_EBD:
    {
        OMX_BUFFERHEADERTYPE *p_buffer_hdr =
            (OMX_BUFFERHEADERTYPE *) event_info.event_param1;

        unsigned int index = event_info.event_param2;

        retval = p_omx_swvdec->async_process_event_ebd(p_buffer_hdr, index);
        break;
    }

    case OMX_SWVDEC_EVENT_FBD:
    {
        OMX_BUFFERHEADERTYPE *p_buffer_hdr =
            (OMX_BUFFERHEADERTYPE *) event_info.event_param1;

        unsigned int index = event_info.event_param2;

        retval = p_omx_swvdec->async_process_event_fbd(p_buffer_hdr, index);
        break;
    }

    case OMX_SWVDEC_EVENT_EOS:
    {
        retval = p_omx_swvdec->async_process_event_eos();
        break;
    }

    case OMX_SWVDEC_EVENT_FLUSH_PORT_IP:
    {
        retval = p_omx_swvdec->async_process_event_flush_port_ip();
        break;
    }

    case OMX_SWVDEC_EVENT_FLUSH_PORT_OP:
    {
        retval = p_omx_swvdec->async_process_event_flush_port_op();
        break;
    }

    case OMX_SWVDEC_EVENT_PORT_RECONFIG:
    {
        retval = p_omx_swvdec->async_process_event_port_reconfig();
        break;
    }

    case OMX_SWVDEC_EVENT_DIMENSIONS_UPDATED:
    {
        retval = p_omx_swvdec->async_process_event_dimensions_updated();
        break;
    }

    default:
    {
        assert(0);

        retval = OMX_ErrorUndefined;
        break;
    }

    }

    if (retval != OMX_ErrorNone)
    {
        p_omx_swvdec->async_post_event(OMX_SWVDEC_EVENT_ERROR, retval, 0);
    }

async_process_event_exit:
    return;
}

/**
 * @brief Process command event.
 *
 * @param[in] cmd:   Command.
 * @param[in] param: Command parameter.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_cmd(OMX_COMMANDTYPE cmd,
                                                  OMX_U32         param)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    bool cmd_ack = false;

    switch (cmd)
    {

    case OMX_CommandStateSet:
    {
        retval = async_process_event_cmd_state_set(&cmd_ack,
                                                   (OMX_STATETYPE) param);
        break;
    }

    case OMX_CommandFlush:
    {
        retval = async_process_event_cmd_flush((unsigned int) param);
        break;
    }

    case OMX_CommandPortDisable:
    {
        retval = async_process_event_cmd_port_disable(&cmd_ack,
                                                      (unsigned int) param);
        break;
    }

    case OMX_CommandPortEnable:
    {
        retval = async_process_event_cmd_port_enable(&cmd_ack,
                                                     (unsigned int) param);
        break;
    }

    default:
    {
        OMX_SWVDEC_LOG_ERROR("cmd '%d' invalid", (int) cmd);

        retval = OMX_ErrorBadParameter;
        break;
    }

    } // switch (cmd)

    if (retval != OMX_ErrorNone)
    {
        async_post_event(OMX_SWVDEC_EVENT_ERROR, retval, 0);
    }
    else if (cmd_ack)
    {
        async_post_event(OMX_SWVDEC_EVENT_CMD_ACK, cmd, param);
    }

    sem_post(&m_sem_cmd);

    return retval;
}

/**
 * @brief Process command acknowledgement event.
 *
 * @param[in] cmd:   Command.
 * @param[in] param: Command parameter.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_cmd_ack(OMX_COMMANDTYPE cmd,
                                                      OMX_U32         param)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    switch (cmd)
    {

    case OMX_CommandStateSet:
    {
        OMX_SWVDEC_LOG_HIGH("%s -> %s",
                            OMX_STATETYPE_STRING(m_state),
                            OMX_STATETYPE_STRING((OMX_STATETYPE) param));

        m_state = (OMX_STATETYPE) param;

        OMX_SWVDEC_LOG_CALLBACK("EventHandler(): OMX_EventCmdComplete, "
                                "OMX_CommandStateSet, %s",
                                OMX_STATETYPE_STRING(m_state));

        m_callback.EventHandler(&m_cmp,
                                m_app_data,
                                OMX_EventCmdComplete,
                                OMX_CommandStateSet,
                                (OMX_U32) m_state,
                                NULL);
        break;
    }

    case OMX_CommandFlush:
    case OMX_CommandPortEnable:
    case OMX_CommandPortDisable:
    {
        if ((cmd == OMX_CommandPortEnable) && m_port_reconfig_inprogress)
        {
            m_port_reconfig_inprogress = false;
        }

        OMX_SWVDEC_LOG_CALLBACK("EventHandler(): OMX_EventCmdComplete, "
                                "%s, port index %d",
                                OMX_COMMANDTYPE_STRING(cmd),
                                param);

        m_callback.EventHandler(&m_cmp,
                                m_app_data,
                                OMX_EventCmdComplete,
                                cmd,
                                param,
                                NULL);
        break;
    }

    default:
    {
        OMX_SWVDEC_LOG_ERROR("cmd '%d' invalid", (int) cmd);

        retval = OMX_ErrorBadParameter;
        break;
    }

    } // switch (cmd)

    return retval;
}

/**
 * @brief Process error event.
 *
 * @param[in] error_code: Error code.
 *
 * @retval OMX_ErrorNone
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_error(OMX_ERRORTYPE error_code)
{
    if (error_code == OMX_ErrorInvalidState)
    {
        OMX_SWVDEC_LOG_HIGH("%s -> OMX_StateInvalid",
                            OMX_STATETYPE_STRING(m_state));

        m_state = OMX_StateInvalid;
    }

    OMX_SWVDEC_LOG_CALLBACK("EventHandler(): OMX_EventError, 0x%08x",
                            error_code);

    m_callback.EventHandler(&m_cmp,
                            m_app_data,
                            OMX_EventError,
                            (OMX_U32) error_code,
                            0,
                            NULL);

    return OMX_ErrorNone;
}

/**
 * @brief Process OMX_CommandStateSet.
 *
 * @param[in,out] p_cmd_ack: Pointer to 'command acknowledge' boolean variable.
 * @param[in]     state_new: New state to which transition is requested.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_cmd_state_set(
    bool         *p_cmd_ack,
    OMX_STATETYPE state_new)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    SWVDEC_STATUS retval_swvdec;

    OMX_SWVDEC_LOG_HIGH("'%s-to-%s' requested",
                        OMX_STATETYPE_STRING(m_state),
                        OMX_STATETYPE_STRING(state_new));

    /**
     * Only the following state transitions are allowed via CommandStateSet:
     *
     * LOADED -> IDLE -> EXECUTING
     * LOADED <- IDLE <- EXECUTING
     */

    if (m_state == OMX_StateInvalid)
    {
        OMX_SWVDEC_LOG_ERROR("in state %s", OMX_STATETYPE_STRING(m_state));

        retval = OMX_ErrorInvalidState;
    }
    else if (state_new == OMX_StateInvalid)
    {
        OMX_SWVDEC_LOG_ERROR("requested transition to state %s",
                             OMX_STATETYPE_STRING(state_new));

        retval = OMX_ErrorInvalidState;
    }
    else if ((m_state   == OMX_StateLoaded) &&
             (state_new == OMX_StateIdle))
    {
        if ((m_port_ip.populated == OMX_TRUE) &&
            (m_port_op.populated == OMX_TRUE))
        {
            if ((retval_swvdec = swvdec_start(m_swvdec_handle)) ==
                SWVDEC_STATUS_SUCCESS)
            {
                *p_cmd_ack = true;
            }
            else
            {
                OMX_SWVDEC_LOG_ERROR("failed to start SwVdec");

                retval = retval_swvdec2omx(retval_swvdec);
            }
        }
        else
        {
            m_status_flags |= (1 << PENDING_STATE_LOADED_TO_IDLE);

            OMX_SWVDEC_LOG_LOW("'loaded-to-idle' pending");
        }
    }
    else if ((m_state   == OMX_StateIdle) &&
             (state_new == OMX_StateExecuting))
    {
        *p_cmd_ack = true;
    }
    else if ((m_state   == OMX_StateExecuting) &&
             (state_new == OMX_StateIdle))
    {
        m_status_flags |= (1 << PENDING_STATE_EXECUTING_TO_IDLE);

        OMX_SWVDEC_LOG_LOW("'executing-to-idle' pending");

        retval = flush(OMX_ALL);
    }
    else if ((m_state   == OMX_StateIdle) &&
             (state_new == OMX_StateLoaded))
    {
        if ((m_port_ip.unpopulated == OMX_TRUE) &&
            (m_port_op.unpopulated == OMX_TRUE))
        {
            if ((retval_swvdec = swvdec_stop(m_swvdec_handle)) ==
                SWVDEC_STATUS_SUCCESS)
            {
                *p_cmd_ack = true;
            }
            else
            {
                OMX_SWVDEC_LOG_ERROR("failed to stop SwVdec");

                retval = retval_swvdec2omx(retval_swvdec);
            }
        }
        else
        {
            m_status_flags |= (1 << PENDING_STATE_IDLE_TO_LOADED);

            OMX_SWVDEC_LOG_LOW("'idle-to-loaded' pending");
        }
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("state transition '%s -> %s' illegal",
                             OMX_STATETYPE_STRING(m_state),
                             OMX_STATETYPE_STRING(state_new));

        retval = ((state_new == m_state) ?
                  OMX_ErrorSameState :
                  OMX_ErrorIncorrectStateTransition);
    }

    return retval;
}

/**
 * @brief Process OMX_CommandFlush.
 *
 * @param[in] port_index: Index of port to flush.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_cmd_flush(unsigned int port_index)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    OMX_SWVDEC_LOG_HIGH("flush port index %d requested", port_index);

    if (port_index == OMX_CORE_PORT_INDEX_IP)
    {
        m_status_flags |= (1 << PENDING_PORT_FLUSH_IP);

        OMX_SWVDEC_LOG_LOW("ip port flush pending");
    }
    else if (port_index == OMX_CORE_PORT_INDEX_OP)
    {
        m_status_flags |= (1 << PENDING_PORT_FLUSH_OP);

        OMX_SWVDEC_LOG_LOW("op port flush pending");
    }
    else if (port_index == OMX_ALL)
    {
        m_status_flags |= (1 << PENDING_PORT_FLUSH_IP);
        m_status_flags |= (1 << PENDING_PORT_FLUSH_OP);

        OMX_SWVDEC_LOG_LOW("ip & op ports flush pending");
    }

    retval = flush(port_index);

    return retval;
}

/**
 * @brief Process OMX_CommandPortDisable.
 *
 * @param[in,out] p_cmd_ack:  Pointer to 'command acknowledge' boolean variable.
 * @param[in]     port_index: Index of port to disable.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_cmd_port_disable(
    bool         *p_cmd_ack,
    unsigned int  port_index)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    OMX_SWVDEC_LOG_HIGH("disable port index %d requested", port_index);

    if (port_index == OMX_CORE_PORT_INDEX_IP)
    {
        if (m_port_ip.enabled == OMX_FALSE)
        {
            OMX_SWVDEC_LOG_ERROR("ip port already disabled");

            retval = OMX_ErrorBadPortIndex;
        }
        else
        {
            m_port_ip.enabled = OMX_FALSE;

            if (m_port_ip.unpopulated)
            {
                *p_cmd_ack = true;
            }
            else
            {
                m_status_flags |= (1 << PENDING_PORT_DISABLE_IP);

                OMX_SWVDEC_LOG_LOW("ip port disable pending");

                if (m_port_ip.num_pending_buffers)
                {
                    retval = flush(port_index);
                }
            }
        }
    }
    else if (port_index == OMX_CORE_PORT_INDEX_OP)
    {
        if (m_port_op.enabled == OMX_FALSE)
        {
            OMX_SWVDEC_LOG_ERROR("op port already disabled");

            retval = OMX_ErrorBadPortIndex;
        }
        else
        {
            m_port_op.enabled = OMX_FALSE;

            if (m_port_op.unpopulated)
            {
                *p_cmd_ack = true;
            }
            else
            {
                m_status_flags |= (1 << PENDING_PORT_DISABLE_OP);

                OMX_SWVDEC_LOG_LOW("op port disable pending");

                if (m_port_op.num_pending_buffers)
                {
                    retval = flush(port_index);
                }
            }
        }
    }
    else if (port_index == OMX_ALL)
    {
        if (m_port_ip.enabled == OMX_FALSE)
        {
            OMX_SWVDEC_LOG_ERROR("ip port already disabled");

            retval = OMX_ErrorBadPortIndex;
        }
        else if (m_port_op.enabled == OMX_FALSE)
        {
            OMX_SWVDEC_LOG_ERROR("op port already disabled");

            retval = OMX_ErrorBadPortIndex;
        }
        else
        {
            if (m_port_ip.unpopulated && m_port_op.unpopulated)
            {
                *p_cmd_ack = true;
            }
            else
            {
                m_port_ip.enabled = OMX_FALSE;
                m_port_op.enabled = OMX_FALSE;

                if (m_port_ip.unpopulated == OMX_FALSE)
                {
                    m_status_flags |= (1 << PENDING_PORT_DISABLE_IP);

                    OMX_SWVDEC_LOG_LOW("ip port disable pending");

                    if (m_port_ip.num_pending_buffers)
                    {
                        retval = flush(port_index);
                    }
                }

                if ((retval == OMX_ErrorNone) &&
                    (m_port_op.unpopulated == OMX_FALSE))
                {
                    m_status_flags |= (1 << PENDING_PORT_DISABLE_OP);

                    OMX_SWVDEC_LOG_LOW("op port disable pending");

                    if (m_port_op.num_pending_buffers)
                    {
                        retval = flush(port_index);
                    }
                }
            }
        }
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("port index '%d' invalid",
                             port_index);

        retval = OMX_ErrorBadPortIndex;
    }

    return retval;
}

/**
 * @brief Process OMX_CommandPortEnable.
 *
 * @param[in,out] p_cmd_ack:  Pointer to 'command acknowledge' boolean variable.
 * @param[in]     port_index: Index of port to enable.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_cmd_port_enable(
    bool        *p_cmd_ack,
    unsigned int port_index)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    OMX_SWVDEC_LOG_HIGH("enable port index %d requested", port_index);

    if (port_index == OMX_CORE_PORT_INDEX_IP)
    {
        if (m_port_ip.enabled)
        {
            OMX_SWVDEC_LOG_ERROR("ip port already enabled");

            retval = OMX_ErrorBadPortIndex;
        }
        else
        {
            m_port_ip.enabled = OMX_TRUE;

            if (m_port_ip.populated)
            {
                *p_cmd_ack = true;
            }
            else
            {
                m_status_flags |= (1 << PENDING_PORT_ENABLE_IP);

                OMX_SWVDEC_LOG_LOW("ip port enable pending");
            }
        }
    }
    else if (port_index == OMX_CORE_PORT_INDEX_OP)
    {
        if (m_port_op.enabled)
        {
            OMX_SWVDEC_LOG_ERROR("op port already enabled");

            retval = OMX_ErrorBadPortIndex;
        }
        else
        {
            m_port_op.enabled = OMX_TRUE;

            if (m_port_op.populated)
            {
                *p_cmd_ack = true;
            }
            else
            {
                m_status_flags |= (1 << PENDING_PORT_ENABLE_OP);

                OMX_SWVDEC_LOG_LOW("op port enable pending");
            }
        }
    }
    else if (port_index == OMX_ALL)
    {
        if (m_port_ip.enabled)
        {
            OMX_SWVDEC_LOG_ERROR("ip port already enabled");

            retval = OMX_ErrorBadPortIndex;
        }
        else if (m_port_op.enabled)
        {
            OMX_SWVDEC_LOG_ERROR("op port already enabled");

            retval = OMX_ErrorBadPortIndex;
        }
        else
        {
            m_port_ip.enabled = OMX_TRUE;
            m_port_op.enabled = OMX_TRUE;

            if (m_port_ip.populated && m_port_op.populated)
            {
                *p_cmd_ack = true;
            }
            else if (m_port_ip.populated == false)
            {
                m_status_flags |= (1 << PENDING_PORT_ENABLE_IP);

                OMX_SWVDEC_LOG_LOW("ip port enable pending");
            }
            else if (m_port_op.populated == false)
            {
                m_status_flags |= (1 << PENDING_PORT_ENABLE_OP);

                OMX_SWVDEC_LOG_LOW("op port enable pending");
            }
        }
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("port index '%d' invalid",
                             port_index);

        retval = OMX_ErrorBadPortIndex;
    }

    return retval;
}

/**
 * @brief Process ETB event.
 *
 * @param[in] p_buffer_hdr: Pointer to buffer header.
 * @param[in] index:        Index of buffer in input buffer info array.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_etb(
    OMX_BUFFERHEADERTYPE *p_buffer_hdr,
    unsigned int          index)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    m_port_ip.num_pending_buffers++;

    if ((p_buffer_hdr->nFilledLen == 0) &&
        ((p_buffer_hdr->nFlags & OMX_BUFFERFLAG_EOS) == 0))
    {
        OMX_SWVDEC_LOG_HIGH("returning %p, buffer %p; "
                            "zero length & no EOS flag",
                            p_buffer_hdr,
                            p_buffer_hdr->pBuffer);

        async_post_event(OMX_SWVDEC_EVENT_EBD,
                         (unsigned long) p_buffer_hdr,
                         (unsigned long) index);
    }
    else if (m_port_ip.flush_inprogress)
    {
        OMX_SWVDEC_LOG_HIGH("returning %p, buffer %p; "
                            "ip port flush in progress",
                            p_buffer_hdr,
                            p_buffer_hdr->pBuffer);

        async_post_event(OMX_SWVDEC_EVENT_EBD,
                         (unsigned long) p_buffer_hdr,
                         (unsigned long) index);
    }
    else
    {
        SWVDEC_STATUS retval_swvdec;

        SWVDEC_BUFFER *p_buffer_swvdec =
            &(m_buffer_array_ip[index].buffer_swvdec);

        if (p_buffer_hdr->nFilledLen &&
            ((p_buffer_hdr->nFlags & OMX_BUFFERFLAG_CODECCONFIG) == 0))
        {
            m_queue_timestamp.push(p_buffer_hdr->nTimeStamp);
        }

        assert(p_buffer_swvdec->p_buffer == p_buffer_hdr->pBuffer);

        if (m_arbitrary_bytes_mode &&
            p_buffer_hdr->nFilledLen &&
            ((p_buffer_hdr->nFlags & OMX_BUFFERFLAG_CODECCONFIG) == 0))
        {
            unsigned int offset_array[OMX_SWVDEC_MAX_FRAMES_PER_ETB] = {0};

            unsigned int num_frame_headers = 1;

            if ((m_omx_video_codingtype ==
                 ((OMX_VIDEO_CODINGTYPE) QOMX_VIDEO_CodingDivx)) ||
                (m_omx_video_codingtype == OMX_VIDEO_CodingMPEG4))
            {
                num_frame_headers = split_buffer_mpeg4(offset_array,
                                                       p_buffer_hdr);
            }
            else
            {
                assert(0);
            }

            if(num_frame_headers > 1)
            {
                m_buffer_array_ip[index].split_count = num_frame_headers - 1;

                for (unsigned int ii = 0; ii < num_frame_headers; ii++)
                {
                    p_buffer_swvdec->flags     = p_buffer_hdr->nFlags;
                    p_buffer_swvdec->timestamp = p_buffer_hdr->nTimeStamp;

                    if (ii == 0)
                    {
                        p_buffer_swvdec->offset        = 0;
                        p_buffer_swvdec->filled_length = (offset_array[ii + 1] ?
                                                          offset_array[ii + 1] :
                                                          p_buffer_hdr->nFilledLen);
                    }
                    else
                    {
                        p_buffer_swvdec->offset        = offset_array[ii];
                        p_buffer_swvdec->filled_length =
                            p_buffer_hdr->nFilledLen - offset_array[ii];
                    }

                    m_diag.dump_ip(p_buffer_swvdec->p_buffer +
                                   p_buffer_swvdec->offset,
                                   p_buffer_swvdec->filled_length);

                    retval_swvdec = swvdec_emptythisbuffer(m_swvdec_handle,
                                                           p_buffer_swvdec);

                    if (retval_swvdec != SWVDEC_STATUS_SUCCESS)
                    {
                        retval = retval_swvdec2omx(retval_swvdec);
                        break;
                    }
                }
            }
            else
            {
                OMX_SWVDEC_LOG_HIGH("No frame detected for Buffer %p, with TS %lld",
                                    p_buffer_hdr->pBuffer, p_buffer_hdr->nTimeStamp );

                p_buffer_swvdec->flags         = p_buffer_hdr->nFlags;
                p_buffer_swvdec->offset        = 0;
                p_buffer_swvdec->timestamp     = p_buffer_hdr->nTimeStamp;
                p_buffer_swvdec->filled_length = p_buffer_hdr->nFilledLen;

                m_diag.dump_ip(p_buffer_swvdec->p_buffer + p_buffer_swvdec->offset,
                               p_buffer_swvdec->filled_length);

                retval_swvdec = swvdec_emptythisbuffer(m_swvdec_handle,
                                                       p_buffer_swvdec);

                if (retval_swvdec != SWVDEC_STATUS_SUCCESS)
                {
                    retval = retval_swvdec2omx(retval_swvdec);
                }
            }
        }
        else
        {
            p_buffer_swvdec->flags         = p_buffer_hdr->nFlags;
            p_buffer_swvdec->offset        = 0;
            p_buffer_swvdec->timestamp     = p_buffer_hdr->nTimeStamp;
            p_buffer_swvdec->filled_length = p_buffer_hdr->nFilledLen;

            m_diag.dump_ip(p_buffer_swvdec->p_buffer + p_buffer_swvdec->offset,
                           p_buffer_swvdec->filled_length);

            retval_swvdec = swvdec_emptythisbuffer(m_swvdec_handle,
                                                   p_buffer_swvdec);

            if (retval_swvdec != SWVDEC_STATUS_SUCCESS)
            {
                retval = retval_swvdec2omx(retval_swvdec);
            }
        }
    }
    return retval;
}

/**
 * @brief Process FTB event.
 *
 * @param[in] p_buffer_hdr: Pointer to buffer header.
 * @param[in] index:        Index of buffer in output buffer info array.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_ftb(
    OMX_BUFFERHEADERTYPE *p_buffer_hdr,
    unsigned int          index)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    m_port_op.num_pending_buffers++;

    if (m_port_op.flush_inprogress)
    {
        OMX_SWVDEC_LOG_HIGH("returning %p, buffer %p; "
                            "op port flush in progress",
                            p_buffer_hdr,
                            m_buffer_array_op[index].buffer_swvdec.p_buffer);

        async_post_event(OMX_SWVDEC_EVENT_FBD,
                         (unsigned long) p_buffer_hdr,
                         (unsigned long) index);
    }
    else
    {
        SWVDEC_STATUS retval_swvdec;

        SWVDEC_BUFFER *p_buffer_swvdec =
            &(m_buffer_array_op[index].buffer_swvdec);

        retval_swvdec = swvdec_fillthisbuffer(m_swvdec_handle, p_buffer_swvdec);

        if (retval_swvdec != SWVDEC_STATUS_SUCCESS)
        {
            retval = retval_swvdec2omx(retval_swvdec);
        }
    }

    return retval;
}

/**
 * @brief Process EBD event.
 *
 * @param[in] p_buffer_hdr: Pointer to buffer header.
 * @param[in] index:        Index of buffer in output buffer info array.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_ebd(
    OMX_BUFFERHEADERTYPE *p_buffer_hdr,
    unsigned int          index)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (index < m_port_ip.def.nBufferCountActual)
    {
        if (m_arbitrary_bytes_mode && m_buffer_array_ip[index].split_count)
        {
            m_buffer_array_ip[index].split_count--;
        }
        else
        {
            m_port_ip.num_pending_buffers--;

            OMX_SWVDEC_LOG_CALLBACK(
                "EmptyBufferDone(): %p, buffer %p",
                p_buffer_hdr,
                m_buffer_array_ip[index].buffer_swvdec.p_buffer);

            m_callback.EmptyBufferDone(&m_cmp, m_app_data, p_buffer_hdr);
        }
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("buffer index '%d' invalid", index);

        retval = OMX_ErrorBadParameter;
    }

    return retval;
}

/**
 * @brief Process FBD event.
 *
 * @param[in] p_buffer_hdr: Pointer to buffer header.
 * @param[in] index:        Index of buffer in output buffer info array.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_fbd(
    OMX_BUFFERHEADERTYPE *p_buffer_hdr,
    unsigned int          index)
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    static long long timestamp_prev = 0;

    if (index < m_port_op.def.nBufferCountActual)
    {
        OMX_U8 *p_buffer;

        p_buffer = m_buffer_array_op[index].buffer_swvdec.p_buffer;

        m_port_op.num_pending_buffers--;

        if (m_port_op.flush_inprogress)
        {
            p_buffer_hdr->nFilledLen = 0;
            p_buffer_hdr->nTimeStamp = 0;
            p_buffer_hdr->nFlags    &= ~OMX_BUFFERFLAG_DATACORRUPT;
        }

        if (p_buffer_hdr->nFilledLen)
        {
            if (m_sync_frame_decoding_mode)
            {
                OMX_SWVDEC_LOG_LOW("sync frame decoding mode; "
                                   "setting timestamp to zero");

                p_buffer_hdr->nTimeStamp = 0;
            }
            else
            {
                if (m_queue_timestamp.empty())
                {
                    OMX_SWVDEC_LOG_ERROR("timestamp queue empty; "
                                         "re-using previous timestamp %lld",
                                         timestamp_prev);

                    p_buffer_hdr->nTimeStamp = timestamp_prev;
                }
                else
                {
                    p_buffer_hdr->nTimeStamp = m_queue_timestamp.top();

                    m_queue_timestamp.pop();

                    timestamp_prev = p_buffer_hdr->nTimeStamp;
                }
            }

            ion_flush_op(index);

            if (m_meta_buffer_mode)
            {
                pthread_mutex_lock(&m_meta_buffer_array_mutex);
            }

            m_diag.dump_op(p_buffer,
                           m_frame_dimensions.width,
                           m_frame_dimensions.height,
                           m_frame_attributes.stride,
                           m_frame_attributes.scanlines);

            if (m_meta_buffer_mode)
            {
                pthread_mutex_unlock(&m_meta_buffer_array_mutex);
            }
        }
        else
        {
            OMX_SWVDEC_LOG_LOW("filled length zero; "
                               "setting timestamp to zero");

            p_buffer_hdr->nTimeStamp = 0;
        }

        if (p_buffer_hdr->nFlags & OMX_BUFFERFLAG_EOS)
        {
            async_post_event(OMX_SWVDEC_EVENT_EOS, 0, 0);

            OMX_SWVDEC_LOG_LOW("flushing %zu elements in timestamp queue",
                               m_queue_timestamp.size());

            while (m_queue_timestamp.empty() == false)
            {
                m_queue_timestamp.pop();
            }
        }

        if (m_meta_buffer_mode &&
            ((p_buffer_hdr->nFlags & OMX_BUFFERFLAG_READONLY)) == 0)
        {
            meta_buffer_ref_remove(index);
        }

        OMX_SWVDEC_LOG_CALLBACK(
            "FillBufferDone(): %p, buffer %p, "
            "flags 0x%08x, filled length %d, timestamp %lld",
            p_buffer_hdr,
            p_buffer,
            p_buffer_hdr->nFlags,
            p_buffer_hdr->nFilledLen,
            p_buffer_hdr->nTimeStamp);

        m_callback.FillBufferDone(&m_cmp, m_app_data, p_buffer_hdr);
    }
    else
    {
        OMX_SWVDEC_LOG_ERROR("buffer index '%d' invalid", index);

        retval = OMX_ErrorBadParameter;
    }

    return retval;
}

/**
 * @brief Process EOS event.
 *
 * @retval OMX_ErrorNone
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_eos()
{
    OMX_SWVDEC_LOG_CALLBACK("EventHandler(): "
                            "OMX_EventBufferFlag, port index %d, EOS",
                            OMX_CORE_PORT_INDEX_OP);

    m_callback.EventHandler(&m_cmp,
                            m_app_data,
                            OMX_EventBufferFlag,
                            OMX_CORE_PORT_INDEX_OP,
                            OMX_BUFFERFLAG_EOS,
                            NULL);

    return OMX_ErrorNone;
}

/**
 * @brief Process input port flush event.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_flush_port_ip()
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    OMX_SWVDEC_EVENT_INFO event_info;

    OMX_BUFFERHEADERTYPE *p_buffer_hdr;

    unsigned int index;

    while (m_queue_port_ip.pop(&event_info))
    {
        switch (event_info.event_id)
        {

        case OMX_SWVDEC_EVENT_ETB:
        {
            p_buffer_hdr = (OMX_BUFFERHEADERTYPE *) event_info.event_param1;

            index = event_info.event_param2;

            // compensate decrement in async_process_event_ebd()
            m_port_ip.num_pending_buffers++;

            retval = async_process_event_ebd(p_buffer_hdr, index);
            break;
        }

        case OMX_SWVDEC_EVENT_EBD:
        {
            p_buffer_hdr = (OMX_BUFFERHEADERTYPE *) event_info.event_param1;

            index = event_info.event_param2;

            retval = async_process_event_ebd(p_buffer_hdr, index);
            break;
        }

        default:
        {
            assert(0);
            break;
        }

        }
    }

    assert(m_port_ip.num_pending_buffers == 0);

    if ((retval == OMX_ErrorNone) &&
        (m_status_flags & (1 << PENDING_PORT_FLUSH_IP)))
    {
        m_status_flags &= ~(1 << PENDING_PORT_FLUSH_IP);

        async_post_event(OMX_SWVDEC_EVENT_CMD_ACK,
                         OMX_CommandFlush,
                         OMX_CORE_PORT_INDEX_IP);
    }

    m_port_ip.flush_inprogress = OMX_FALSE;

    return retval;
}

/**
 * @brief Process output port flush event.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_flush_port_op()
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    OMX_SWVDEC_EVENT_INFO event_info;

    OMX_BUFFERHEADERTYPE *p_buffer_hdr;

    unsigned int index;

    while (m_queue_port_op.pop(&event_info))
    {
        switch (event_info.event_id)
        {

        case OMX_SWVDEC_EVENT_FTB:
        {
            p_buffer_hdr = (OMX_BUFFERHEADERTYPE *) event_info.event_param1;

            index = event_info.event_param2;

            // compensate decrement in async_process_event_fbd()
            m_port_op.num_pending_buffers++;

            retval = async_process_event_fbd(p_buffer_hdr, index);
            break;
        }

        case OMX_SWVDEC_EVENT_FBD:
        {
            p_buffer_hdr = (OMX_BUFFERHEADERTYPE *) event_info.event_param1;

            index = event_info.event_param2;

            retval = async_process_event_fbd(p_buffer_hdr, index);
            break;
        }

        default:
        {
            assert(0);
            break;
        }

        }
    }

    assert(m_port_op.num_pending_buffers == 0);

    if ((retval == OMX_ErrorNone) &&
        (m_status_flags & (1 << PENDING_PORT_FLUSH_OP)))
    {
        m_status_flags &= ~(1 << PENDING_PORT_FLUSH_OP);

        async_post_event(OMX_SWVDEC_EVENT_CMD_ACK,
                         OMX_CommandFlush,
                         OMX_CORE_PORT_INDEX_OP);
    }

    if ((retval == OMX_ErrorNone) &&
        (m_status_flags & (1 << PENDING_STATE_EXECUTING_TO_IDLE)))
    {
        m_status_flags &= ~(1 << PENDING_STATE_EXECUTING_TO_IDLE);

        async_post_event(OMX_SWVDEC_EVENT_CMD_ACK,
                         OMX_CommandStateSet,
                         OMX_StateIdle);
    }

    if (m_port_reconfig_inprogress == false)
    {
        OMX_SWVDEC_LOG_LOW("flushing %zu elements in timestamp queue",
                           m_queue_timestamp.size());

        while (m_queue_timestamp.empty() == false)
        {
            m_queue_timestamp.pop();
        }
    }

    m_port_op.flush_inprogress = OMX_FALSE;

    return retval;
}

/**
 * @brief Process port reconfiguration event.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_port_reconfig()
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (m_port_reconfig_inprogress)
    {
        OMX_SWVDEC_LOG_ERROR("port reconfiguration already in progress");

        retval = OMX_ErrorIncorrectStateOperation;
    }
    else
    {
        m_port_reconfig_inprogress = true;

        OMX_SWVDEC_LOG_CALLBACK("EventHandler(): "
                                "OMX_EventPortSettingsChanged, port index %d",
                                OMX_CORE_PORT_INDEX_OP);

        m_callback.EventHandler(&m_cmp,
                                m_app_data,
                                OMX_EventPortSettingsChanged,
                                OMX_CORE_PORT_INDEX_OP,
                                0,
                                NULL);
    }

    return retval;
}

/**
 * @brief Process dimensions updated event.
 *
 * @retval OMX_ERRORTYPE
 */
OMX_ERRORTYPE omx_swvdec::async_process_event_dimensions_updated()
{
    OMX_ERRORTYPE retval = OMX_ErrorNone;

    if (m_dimensions_update_inprogress)
    {
        OMX_SWVDEC_LOG_ERROR("dimensions update already in progress");

        retval = OMX_ErrorIncorrectStateOperation;
    }
    else
    {
        m_dimensions_update_inprogress = true;

        OMX_SWVDEC_LOG_CALLBACK("EventHandler(): "
                                "OMX_EventPortSettingsChanged, port index %d, "
                                "OMX_IndexConfigCommonOutputCrop",
                                OMX_CORE_PORT_INDEX_OP);

        m_callback.EventHandler(&m_cmp,
                                m_app_data,
                                OMX_EventPortSettingsChanged,
                                OMX_CORE_PORT_INDEX_OP,
                                OMX_IndexConfigCommonOutputCrop,
                                NULL);
    }

    return retval;
}
