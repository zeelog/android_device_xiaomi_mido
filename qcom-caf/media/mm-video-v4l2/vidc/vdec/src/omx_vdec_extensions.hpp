/*--------------------------------------------------------------------------
Copyright (c) 2017, 2020 The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
--------------------------------------------------------------------------*/

void omx_vdec::init_vendor_extensions (VendorExtensionStore &store) {

    //TODO: add extensions based on Codec, m_platform and/or other capability queries

    ADD_EXTENSION("qti-ext-dec-picture-order", OMX_QcomIndexParamVideoDecoderPictureOrder, OMX_DirOutput)
    ADD_PARAM_END("enable", OMX_AndroidVendorValueInt32)
    ADD_EXTENSION("qti-ext-dec-dither", OMX_QTIIndexParamDitherControl, OMX_DirOutput)
    ADD_PARAM_END("control", OMX_AndroidVendorValueInt32)

    ADD_EXTENSION("qti-ext-dec-caps-vt-driver-version", OMX_QTIIndexParamCapabilitiesVTDriverVersion, OMX_DirOutput)
    ADD_PARAM_END("number", OMX_AndroidVendorValueInt32)

    ADD_EXTENSION("qti-ext-dec-custom-bufferSize", OMX_QcomIndexParamVideoCustomBufferSize, OMX_DirInput)
    ADD_PARAM_END("value", OMX_AndroidVendorValueInt32)

}

OMX_ERRORTYPE omx_vdec::get_vendor_extension_config(
                OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext) {
    if (ext->nIndex >= mVendorExtensionStore.size()) {
        return OMX_ErrorNoMore;
    }

    const VendorExtension& vExt = mVendorExtensionStore[ext->nIndex];
    DEBUG_PRINT_LOW("VendorExt: getConfig: index=%u (%s)", ext->nIndex, vExt.name());

    vExt.copyInfoTo(ext);
    if (ext->nParamSizeUsed < vExt.paramCount()) {
        // this happens during initial getConfig to query only extension-name and param-count
        return OMX_ErrorNone;
    }

    // We now have sufficient params allocated in extension data passed.
    // Following code is to set the extension-specific data

    bool setStatus = true;

    switch ((OMX_U32)vExt.extensionIndex()) {
        case OMX_QcomIndexParamVideoDecoderPictureOrder:
        {
            setStatus &= vExt.setParamInt32(ext, "enable", m_decode_order_mode);
            break;
        }
        case OMX_QTIIndexParamDitherControl:
        {
            setStatus &= vExt.setParamInt32(ext, "control", m_dither_config);
            break;
        }
        case OMX_QTIIndexParamCapabilitiesVTDriverVersion:
        {
            setStatus &= vExt.setParamInt32(ext, "number", 65536);
            break;
        }
        case OMX_QcomIndexParamVideoCustomBufferSize:
        {
            break;
        }
        default:
        {
            return OMX_ErrorNotImplemented;
        }
    }
    return setStatus ? OMX_ErrorNone : OMX_ErrorUndefined;
}

OMX_ERRORTYPE omx_vdec::set_vendor_extension_config(
                OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext) {

    DEBUG_PRINT_LOW("set_vendor_extension_config");
    if (ext->nIndex >= mVendorExtensionStore.size()) {
        DEBUG_PRINT_ERROR("unrecognized vendor extension index (%u) max(%u)",
                ext->nIndex, mVendorExtensionStore.size());
        return OMX_ErrorBadParameter;
    }

    const VendorExtension& vExt = mVendorExtensionStore[ext->nIndex];
    DEBUG_PRINT_LOW("VendorExt: setConfig: index=%u (%s)", ext->nIndex, vExt.name());

    OMX_ERRORTYPE err = OMX_ErrorNone;
    err = vExt.isConfigValid(ext);
    if (err != OMX_ErrorNone) {
        return err;
    }

    // mark this as set, regardless of set_config succeeding/failing.
    // App will know by inconsistent values in output-format
    vExt.set();

    bool valueSet = false;
    switch ((OMX_U32)vExt.extensionIndex()) {
        case OMX_QcomIndexParamVideoDecoderPictureOrder:
        {
            OMX_S32 pic_order_enable = 0;
            valueSet |= vExt.readParamInt32(ext, "enable", &pic_order_enable);
            if (!valueSet) {
                break;
            }

            DEBUG_PRINT_HIGH("VENDOR-EXT: set_config: OMX_QcomIndexParamVideoDecoderPictureOrder : %d",
                    pic_order_enable);

            QOMX_VIDEO_DECODER_PICTURE_ORDER decParam;
            OMX_INIT_STRUCT(&decParam, QOMX_VIDEO_DECODER_PICTURE_ORDER);
            decParam.eOutputPictureOrder =
                    pic_order_enable ? QOMX_VIDEO_DECODE_ORDER : QOMX_VIDEO_DISPLAY_ORDER;

            err = set_parameter(
                    NULL, (OMX_INDEXTYPE)OMX_QcomIndexParamVideoDecoderPictureOrder, &decParam);
            if (err != OMX_ErrorNone) {
                DEBUG_PRINT_ERROR("set_config: OMX_QcomIndexParamVideoDecoderPictureOrder failed !");
            }
            break;
        }
        case OMX_QTIIndexParamDitherControl:
        {
            OMX_S32 dither_control_type = 0;
            valueSet |= vExt.readParamInt32(ext, "control", &dither_control_type);
            if (!valueSet) {
                break;
            }
            DEBUG_PRINT_HIGH("VENDOR-EXT: set_config: OMX_QTIIndexParamDitherControl : %d",
                    dither_control_type);

            QOMX_VIDEO_DITHER_CONTROL decParam;
            OMX_INIT_STRUCT(&decParam, QOMX_VIDEO_DITHER_CONTROL);
            if(dither_control_type == 0 )
                 decParam.eDitherType = QOMX_DITHER_DISABLE;
            else if(dither_control_type == 1)
                decParam.eDitherType = QOMX_DITHER_COLORSPACE_EXCEPT_BT2020;
            else if(dither_control_type == 2)
                decParam.eDitherType = QOMX_DITHER_ALL_COLORSPACE;
            err = set_parameter(
                    NULL, (OMX_INDEXTYPE)OMX_QTIIndexParamDitherControl, &decParam);
            if (err != OMX_ErrorNone) {
                DEBUG_PRINT_ERROR("set_config: OMX_QTIIndexParamDitherControl failed !");
            }
            break;
        }
        case OMX_QcomIndexParamVideoCustomBufferSize:
        {
            OMX_S32 BufferSize = 0;
            valueSet |= vExt.readParamInt32(ext, "value", &BufferSize);
            if (!valueSet) {
                break;
            }
            DEBUG_PRINT_HIGH("VENDOR-EXT: set_config: OMX_QcomIndexParamVideoCustomBufferSize : %d",
                    BufferSize);

            QOMX_VIDEO_CUSTOM_BUFFERSIZE sCustomBufferSize;
            OMX_INIT_STRUCT(&sCustomBufferSize, QOMX_VIDEO_CUSTOM_BUFFERSIZE);
            sCustomBufferSize.nPortIndex  = 0;
            sCustomBufferSize.nBufferSize = BufferSize;

            err = set_parameter(
                    NULL, (OMX_INDEXTYPE)OMX_QcomIndexParamVideoCustomBufferSize, &sCustomBufferSize);
            if (err != OMX_ErrorNone) {
                DEBUG_PRINT_ERROR("set_config: OMX_QcomIndexParamVideoCustomBufferSize failed !");
            }
            break;
        }
        case OMX_QTIIndexParamCapabilitiesVTDriverVersion:
        {
            break;
        }
        default:
        {
            return OMX_ErrorNotImplemented;
        }
    }

    return err;
}
