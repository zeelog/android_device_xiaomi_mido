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

#ifndef _VIDC_VENDOR_ENXTENSIONS_H_
#define _VIDC_VENDOR_ENXTENSIONS_H_

#include <inttypes.h>
#include <string.h>
#include <string>
#include <vector>

/*
 * This class represents a Vendor-Extension (except for the data).
 * A Vendor extension is identified by a unique extension-name and
 * is mapped to a specific OMX-extension. it contains params that
 * signify individual parameter-field
 *    VendorExtension::mName         => similar to OMX extension string.
 *                                      (Name must be unique)
 *    VendorExtension::mId           => similar to OMX extension ID
 *    VendorExtension::mParam[0,1..] => similar to an individual field
 *                                      in OMX extension struct
 *    VendorExtension::mIsSet        => flag that indicates whether this
 *                                      extension was set by the client.
 * This also provides utility methods to:
 *   - copy info(keys/types..) to client's extension strcuture
 *        including copying of param-key and type of each param
 *   - copy data from/to the client's extension structure, given the
 *        param-key (this is type-aware copy)
 *   - sanity checks
 *
 * Extension name - naming convention
 *   - name must be unique
 *   - must be prefixed with "ext-" followed by component-type
 *     Eg: "enc" "dec" "vpp"
 *   - SHOULD NOT contain "."
 *   - keywords SHOULD be separated by "-"
 *   - name may contain feature-name and/or parameter-name
 *   Eg:  "ext-enc-preprocess-rotate"
 *        "ext-dec-picture-order"
 *
 * Overall paramter-key => vendor (dot) extension-name (dot) param-key
*/
struct VendorExtension {

    /*
     * Param represents an individual parameter (field) of a VendorExtension.
     * This is a variant holding values of type [int32, int64 or String].
     * Each Param has a name (unique within the extension) that is appended
     * to the 'extension-name' and prefixed with "vendor." to generate the
     * key that will be exposed to the client.
     *
     * Param name(key) - naming convention
     *   - key must be unique (within the extension)
     *   - SHOULD not contain "."
     *   - Keywords seperated by "-" ONLY if required
     *   Eg: "angle"
     *       "n-idr-period"
     *
     */
    struct Param {
        Param (const std::string &name, OMX_ANDROID_VENDOR_VALUETYPE type)
            : mName(name), mType(type) {}

        const char *name() const {
            return mName.c_str();
        }
        OMX_ANDROID_VENDOR_VALUETYPE type() const {
            return mType;
        }
    private:
        std::string mName;
        OMX_ANDROID_VENDOR_VALUETYPE mType;
    };

    // helper to build a list of variable number or params
    struct ParamListBuilder {
        ParamListBuilder (std::initializer_list<Param> l)
            : mParams(l) {}
    private:
        friend struct VendorExtension;
        std::vector<Param> mParams;
    };

    VendorExtension(OMX_INDEXTYPE id, const char *name, OMX_DIRTYPE dir,
            const ParamListBuilder& p);

    // getters
    OMX_INDEXTYPE extensionIndex() const {
        return (OMX_INDEXTYPE)mId;
    }
    const char *name() const {
        return mName.c_str();
    }
    OMX_U32 paramCount() const {
        return (OMX_U32)mParams.size();
    }
    bool isSet() const {
        return mIsSet;
    }

    // (the only) setter
    void set() const {
        mIsSet = true;
    }

    // copy extension Info to OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE* struct passed (except data)
    OMX_ERRORTYPE copyInfoTo(OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext) const;

    // Type-aware data copy methods
    // (NOTE: data here is passed explicitly to avoid this class having to know all types)
    // returns true if value was written
    bool setParamInt32(OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, const char *paramKey,
            OMX_S32 setInt32) const;
    bool setParamInt64(OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, const char *paramKey,
            OMX_S64 setInt64) const;
    bool setParamString(OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, const char *paramKey,
            const char *setStr) const;

    // read-values are updated ONLY IF the param[paramIndex] is set by client
    // returns true if value was read
    bool readParamInt32(OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, const char *paramKey,
            OMX_S32 *readInt32) const;
    bool readParamInt64(OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, const char *paramKey,
            OMX_S64 *readInt64) const;
    bool readParamInt64(OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, const char *paramKey,
            char *readStr) const;

    // Sanity checkers
    // Check if the extension-name, port-dir, allotted params match
    //    for each param, check if key and type both match
    // Must be called to check whether config data provided with setConfig is valid
    OMX_ERRORTYPE isConfigValid(OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext) const;

    // Compare the keys for correct configuration
    bool isConfigKey(OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, const char *paramKey) const;

    // utils
    static const char* typeString(OMX_ANDROID_VENDOR_VALUETYPE type);
    std::string debugString() const;

private:
    // Id assigned to the extension
    OMX_INDEXTYPE mId;
    // Name of the extension
    std::string mName;
    // Port that this setting applies to
    OMX_DIRTYPE mPortDir;
    // parameters required for this extension
    std::vector<Param> mParams;
    // Flag that indicates client has set this extension.
    mutable bool mIsSet;

    // check if the index is valid, name matches, type matches and is set
    // This must be called to verify config-data passed with setConfig()
    bool _isParamAccessOK(
            OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, int paramIndex) const;

    // check if the index is valid, check against explicit type
    bool _isParamAccessTypeOK(
            OMX_CONFIG_ANDROID_VENDOR_EXTENSIONTYPE *ext, int paramIndex,
            OMX_ANDROID_VENDOR_VALUETYPE type) const;

    int indexOfParam(const char *key) const;
};

/*
 * Store(List) of all vendor extensions *that are supported* by a component.
 * The list is populated (per-component) at init, based on the capabilities.
 * The store is immutable once created, except for setting the flag to indicate
 * -whether the extension was set by the Client
 */
struct VendorExtensionStore {
    VendorExtensionStore()
        : mInvalid(VendorExtension((OMX_INDEXTYPE)-1, "invalid", OMX_DirMax, {{}})) {
    }

    VendorExtensionStore(const VendorExtensionStore&) = delete;
    VendorExtensionStore& operator= (const VendorExtensionStore&) = delete;

    void add(const VendorExtension& _e) {
        mExt.push_back(_e);
    }
    const VendorExtension& operator[] (OMX_U32 index) const {
        return index < mExt.size() ? mExt[index] : mInvalid;
    }
    OMX_U32 size() const {
        return mExt.size();
    }
    void dumpExtensions(const char *prefix) const;

private:
    std::vector<VendorExtension> mExt;
    VendorExtension mInvalid;
};

// Macros to help add extensions
#define ADD_EXTENSION(_name, _extIndex, _dir)                                   \
    store.add(VendorExtension((OMX_INDEXTYPE)_extIndex, _name, _dir, {          \

#define ADD_PARAM(_key, _type)                                                             \
    {_key, _type},

#define ADD_PARAM_END(_key, _type)                                                             \
    {_key, _type} }));

#endif // _VIDC_VENDOR_ENXTENSIONS_H_
