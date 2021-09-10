/*--------------------------------------------------------------------------
Copyright (c) 2017, The Linux Foundation. All rights reserved.

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

#define LOG_TAG "OMX-VENDOR-EXT"
#include <utils/Log.h>
#include "vidc_debug.h"

#include "OMX_Core.h"
#include "OMX_QCOMExtns.h"
#include "OMX_VideoExt.h"
#include "OMX_IndexExt.h"
#include "vidc_vendor_extensions.h"

VendorExtension::VendorExtension(OMX_INDEXTYPE id, const char *name, OMX_DIRTYPE dir,
        const ParamListBuilder& p)
    : mId(id),
      mName(name),
      mPortDir(dir),
      mParams(std::move(p.mParams)),
      mIsSet(false) {
}

// copy extension Info to OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE* struct passed
OMX_ERRORTYPE VendorExtension::copyInfoTo(
        OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext) const {

    // Extension info
    strlcpy((char *)ext->cName, mName.c_str(), OMX_MAX_STRINGNAME_SIZE);
    ext->eDir = mPortDir;
    ext->nParamCount = paramCount();

    // Per-parameter info
    // Must be copied only if there are enough params to fill-in
    if (ext->nParamSizeUsed < ext->nParamCount) {
        return OMX_ErrorNone;
    }

    int i = 0;
    for (const Param& p : mParams) {
        strlcpy((char *)ext->nParam[i].cKey, p.name(), OMX_MAX_STRINGNAME_SIZE);
        ext->nParam[i].bSet = mIsSet ? OMX_TRUE : OMX_FALSE;
        ext->nParam[i].eValueType = p.type();
        ++i;
    }
    return OMX_ErrorNone;
}

bool VendorExtension::setParamInt32(
        OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, const char *paramKey,
        OMX_S32 setInt32) const {
    int paramIndex = indexOfParam(paramKey);
    if (!_isParamAccessTypeOK(ext, paramIndex, OMX_AndroidVendorValueInt32)) {
        return false;
    }
    ext->nParam[paramIndex].nInt32 = setInt32;
    return true;
}

bool VendorExtension::setParamInt64(
        OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, const char *paramKey,
        OMX_S64 setInt64) const {
    int paramIndex = indexOfParam(paramKey);
    if (!_isParamAccessTypeOK(ext, paramIndex, OMX_AndroidVendorValueInt64)) {
        return false;
    }
    ext->nParam[paramIndex].nInt64 = setInt64;
    return true;
}

bool VendorExtension::setParamString(
        OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, const char *paramKey,
        const char *setStr) const {
    int paramIndex = indexOfParam(paramKey);
    if (!_isParamAccessTypeOK(ext, paramIndex, OMX_AndroidVendorValueString)) {
        return false;
    }
    strlcpy((char *)ext->nParam[paramIndex].cString, setStr, OMX_MAX_STRINGVALUE_SIZE);
    return true;
}

bool VendorExtension::readParamInt32(
        OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, const char *paramKey,
        OMX_S32 *readInt32) const {
    int paramIndex = indexOfParam(paramKey);
    if (!_isParamAccessTypeOK(ext, paramIndex, OMX_AndroidVendorValueInt32)) {
        return false;
    }
    if (ext->nParam[paramIndex].bSet == OMX_TRUE) {
        *readInt32 = ext->nParam[paramIndex].nInt32;
        return true;
    }
    return false;
}

bool VendorExtension::readParamInt64(
        OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, const char *paramKey,
        OMX_S64 *readInt64) const {
    int paramIndex = indexOfParam(paramKey);
    if (!_isParamAccessTypeOK(ext, paramIndex, OMX_AndroidVendorValueInt64)) {
        return false;
    }
    if (ext->nParam[paramIndex].bSet == OMX_TRUE) {
        *readInt64 = ext->nParam[paramIndex].nInt64;
        return true;
    }
    return false;
}

bool VendorExtension::readParamInt64(
            OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, const char *paramKey,
            char *readStr) const {
    int paramIndex = indexOfParam(paramKey);
    if (!_isParamAccessTypeOK(ext, paramIndex, OMX_AndroidVendorValueString)) {
        return false;
    }
    if (ext->nParam[paramIndex].bSet == OMX_TRUE) {
        strlcpy(readStr,
                (const char *)ext->nParam[paramIndex].cString, OMX_MAX_STRINGVALUE_SIZE);
        return true;
    }
    return false;
}

// Checkers
OMX_ERRORTYPE VendorExtension::isConfigValid(
    OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext) const {
    ALOGI("isConfigValid");

    if (ext->nParamSizeUsed < ext->nParamCount) {
        DEBUG_PRINT_ERROR("allotted params(%u) < required(%u) for %s",
                ext->nParamSizeUsed, ext->nParamCount, mName.c_str());
        return OMX_ErrorBadParameter;
    }
    if (ext->nParamCount != paramCount()) {
        DEBUG_PRINT_ERROR("incorrect param count(%u) v/s required(%u) for %s",
                ext->nParamCount, paramCount(), mName.c_str());
        return OMX_ErrorBadParameter;
    }
    if (strncmp((char *)ext->cName, mName.c_str(), OMX_MAX_STRINGNAME_SIZE) != 0) {
        DEBUG_PRINT_ERROR("extension name mismatch(%s) v/s expected(%s)",
                (char *)ext->cName, mName.c_str());
        return OMX_ErrorBadParameter;
    }

    for (OMX_U32 i = 0; i < paramCount(); ++i) {
        if (!_isParamAccessOK(ext, i)) {
            ALOGI("_isParamAccessOK failed for %u", i);
            return OMX_ErrorBadParameter;
        }
    }

    return OMX_ErrorNone;
}

bool VendorExtension::isConfigKey(
         OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext,
         const char *paramKey) const {

    bool retStatus = false;
    if (!strncmp((char *)ext->cName, paramKey, OMX_MAX_STRINGNAME_SIZE)) {
        retStatus = true;
    }

    return retStatus;
}

//static
const char* VendorExtension::typeString(OMX_ANDROID_VENDOR_VALUETYPE type) {
    switch (type) {
        case OMX_AndroidVendorValueInt32: return "Int32";
        case OMX_AndroidVendorValueInt64: return "Int64";
        case OMX_AndroidVendorValueString: return "String";
        default: return "InvalidType";
    }
}

std::string VendorExtension::debugString() const {
    std::string str = "vendor." + mName + "{";
    for (const Param& p : mParams) {
        str += "{ ";
        str += p.name();
        str += " : ";
        str += typeString(p.type());
        str += " },  ";
    }
    str += "}";
    return str;
}

bool VendorExtension::_isParamAccessTypeOK(
        OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, int paramIndex,
        OMX_ANDROID_VENDOR_VALUETYPE type) const {
    if (paramIndex < 0
            || paramIndex >= (int)ext->nParamSizeUsed
            || paramIndex >= (int)paramCount()) {
        DEBUG_PRINT_ERROR("Invalid Param index(%d) for %s (max=%u)",
                paramIndex, mName.c_str(), paramCount());
        return false;
    }
    if (type != mParams[paramIndex].type()) {
        DEBUG_PRINT_ERROR("Invalid Type for field(%s) for %s.%s (expected=%s)",
                typeString(type), mName.c_str(), mParams[paramIndex].name(),
                typeString(mParams[paramIndex].type()));
        return false;
    }
    return true;
}

bool VendorExtension::_isParamAccessOK(
        OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, int paramIndex) const {
    if (paramIndex < 0
            || paramIndex >= (int)ext->nParamSizeUsed
            || paramIndex >= (int)paramCount()) {
        DEBUG_PRINT_ERROR("Invalid Param index(%d) for %s (max=%u)",
                paramIndex, mName.c_str(), paramCount());
        return false;
    }
    if (ext->nParam[paramIndex].eValueType != mParams[paramIndex].type()) {
        DEBUG_PRINT_ERROR("Invalid Type for field(%s) for %s.%s (expected=%s)",
                typeString(ext->nParam[paramIndex].eValueType),
                mName.c_str(), mParams[paramIndex].name(),
                typeString(mParams[paramIndex].type()));
        return false;
    }
    if (strncmp((const char *)ext->nParam[paramIndex].cKey,
            mParams[paramIndex].name(), OMX_MAX_STRINGNAME_SIZE) != 0) {
        DEBUG_PRINT_ERROR("Invalid Key for field(%s) for %s.%s (expected=%s)",
                ext->nParam[paramIndex].cKey,
                mName.c_str(), mParams[paramIndex].name(),
                mParams[paramIndex].name());
        return false;
    }
    return true;
}

int VendorExtension::indexOfParam(const char *key) const {
    int i = 0;
    for (const Param& p : mParams) {
        if (!strncmp(key, p.name(), OMX_MAX_STRINGNAME_SIZE)) {
            return i;
        }
        ++i;
    }
    DEBUG_PRINT_ERROR("Failed to lookup param(%s) in extension(%s)",
            key, mName.c_str());
    return -1;
}

void VendorExtensionStore::dumpExtensions(const char *prefix) const {
    DEBUG_PRINT_HIGH("%s : Vendor extensions supported (%u)", prefix, size());
    for (const VendorExtension& v : mExt) {
        DEBUG_PRINT_HIGH("   %s", v.debugString().c_str());
    }
}
