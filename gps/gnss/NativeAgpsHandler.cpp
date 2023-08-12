/* Copyright (c) 2020, The Linux Foundation. All rights reserved.
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
 *     * Neither the name of The Linux Foundation, nor the names of its
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
#define LOG_TAG "LocSvc_NativeAgpsHandler"

#include <LocAdapterBase.h>
#include <SystemStatus.h>
#include <DataItemId.h>
#include <DataItemsFactoryProxy.h>
#include <DataItemConcreteTypesBase.h>
#include <loc_log.h>
#include <NativeAgpsHandler.h>
#include <GnssAdapter.h>

using namespace loc_core;

// IDataItemObserver overrides
void NativeAgpsHandler::getName(string& name) {
    name = "NativeAgpsHandler";
}

void NativeAgpsHandler::notify(const list<IDataItemCore*>& dlist) {
    for (auto each : dlist) {
        switch (each->getId()) {
            case NETWORKINFO_DATA_ITEM_ID: {
                    NetworkInfoDataItemBase* networkInfo =
                        static_cast<NetworkInfoDataItemBase*>(each);
                    uint64_t mobileBit = (uint64_t )1 << loc_core::TYPE_MOBILE;
                    uint64_t allTypes = networkInfo->mAllTypes;
                    mConnected = ((networkInfo->mAllTypes & mobileBit) == mobileBit);
                    /**
                     * mApn Telephony preferred Access Point Name to use for
                     * carrier data connection when connected to a cellular network.
                     * Empty string, otherwise.
                     */
                    mApn = networkInfo->mApn;
                    LOC_LOGd("updated mConnected:%d, mApn: %s", mConnected, mApn.c_str());
                    break;
            }
            default:
                    break;
        }
    }
}

NativeAgpsHandler* NativeAgpsHandler::sLocalHandle = nullptr;
NativeAgpsHandler::NativeAgpsHandler(IOsObserver* sysStatObs, GnssAdapter& adapter) :
        mSystemStatusObsrvr(sysStatObs), mConnected(false), mAdapter(adapter) {
    sLocalHandle = this;
    list<DataItemId> subItemIdList = {NETWORKINFO_DATA_ITEM_ID};
    mSystemStatusObsrvr->subscribe(subItemIdList, this);
}

NativeAgpsHandler::~NativeAgpsHandler() {
    if (nullptr != mSystemStatusObsrvr) {
        LOC_LOGd("Unsubscribe for network info.");
        list<DataItemId> subItemIdList = {NETWORKINFO_DATA_ITEM_ID};
        mSystemStatusObsrvr->unsubscribe(subItemIdList, this);
    }
    sLocalHandle = nullptr;
    mSystemStatusObsrvr = nullptr;
}


AgpsCbInfo NativeAgpsHandler::getAgpsCbInfo() {
    AgpsCbInfo nativeCbInfo = {};
    nativeCbInfo.statusV4Cb = (void*)agnssStatusIpV4Cb;
    nativeCbInfo.atlType = AGPS_ATL_TYPE_WWAN;
    return nativeCbInfo;
}

void NativeAgpsHandler::agnssStatusIpV4Cb(AGnssExtStatusIpV4 statusInfo) {
    if (nullptr != sLocalHandle) {
        sLocalHandle->processATLRequestRelease(statusInfo);
    } else {
        LOC_LOGe("sLocalHandle is null");
    }
}

void NativeAgpsHandler::processATLRequestRelease(AGnssExtStatusIpV4 statusInfo) {
    if (LOC_AGPS_TYPE_WWAN_ANY == statusInfo.type) {
        LOC_LOGd("status.type = %d status.apnTypeMask = 0x%X", statusInfo.type,
                 statusInfo.apnTypeMask);
        switch (statusInfo.status) {
        case LOC_GPS_REQUEST_AGPS_DATA_CONN:
            if (mConnected) {
                mAdapter.dataConnOpenCommand(LOC_AGPS_TYPE_WWAN_ANY, mApn.c_str(), mApn.size(),
                    AGPS_APN_BEARER_IPV4);
            } else {
                mAdapter.dataConnFailedCommand(LOC_AGPS_TYPE_WWAN_ANY);
            }
            break;
        case LOC_GPS_RELEASE_AGPS_DATA_CONN:
            mAdapter.dataConnClosedCommand(LOC_AGPS_TYPE_WWAN_ANY);
            break;
        default:
            LOC_LOGe("Invalid Request: %d", statusInfo.status);
        }
    } else {
        LOC_LOGe("mAgpsManger is null or invalid request type!");
    }
}
