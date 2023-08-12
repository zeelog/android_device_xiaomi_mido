/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
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
 */
#define LOG_TAG "LocSvc_GnssAntennaInfoInterface"

#include <log_util.h>
#include "Gnss.h"
#include "GnssAntennaInfo.h"
#include <android/hardware/gnss/1.0/types.h>

namespace android {
namespace hardware {
namespace gnss {
namespace V2_1 {
namespace implementation {

static GnssAntennaInfo* spGnssAntennaInfo = nullptr;

static void convertGnssAntennaInfo(std::vector<GnssAntennaInformation>& in,
        hidl_vec<IGnssAntennaInfoCallback::GnssAntennaInfo>& antennaInfos);

void GnssAntennaInfo::GnssAntennaInfoDeathRecipient::serviceDied(uint64_t cookie,
                                                                 const wp<IBase>& who) {
    LOC_LOGE("%s] service died. cookie: %llu, who: %p",
            __FUNCTION__, static_cast<unsigned long long>(cookie), &who);
    // we do nothing here
    // Gnss::GnssDeathRecipient will stop the session
    // However, we need to inform the adapter that the service has died
    if (nullptr == spGnssAntennaInfo) {
        LOC_LOGE("%s]: spGnssAntennaInfo is nullptr", __FUNCTION__);
        return;
    }
    if (nullptr == spGnssAntennaInfo->mGnss) {
        LOC_LOGE("%s]: spGnssAntennaInfo->mGnss is nullptr", __FUNCTION__);
        return;
    }

    spGnssAntennaInfo->mGnss->getGnssInterface()->antennaInfoClose();
}

static void convertGnssAntennaInfo(std::vector<GnssAntennaInformation>& in,
        hidl_vec<IGnssAntennaInfoCallback::GnssAntennaInfo>& out) {

    uint32_t vecSize, numberOfRows, numberOfColumns;
    vecSize = in.size();
    out.resize(vecSize);
    for (uint32_t i = 0; i < vecSize; i++) {
        out[i].carrierFrequencyMHz = in[i].carrierFrequencyMHz;
        out[i].phaseCenterOffsetCoordinateMillimeters.x =
                in[i].phaseCenterOffsetCoordinateMillimeters.x;
        out[i].phaseCenterOffsetCoordinateMillimeters.xUncertainty =
                in[i].phaseCenterOffsetCoordinateMillimeters.xUncertainty;
        out[i].phaseCenterOffsetCoordinateMillimeters.y =
                in[i].phaseCenterOffsetCoordinateMillimeters.y;
        out[i].phaseCenterOffsetCoordinateMillimeters.yUncertainty =
                in[i].phaseCenterOffsetCoordinateMillimeters.yUncertainty;
        out[i].phaseCenterOffsetCoordinateMillimeters.z =
                in[i].phaseCenterOffsetCoordinateMillimeters.z;
        out[i].phaseCenterOffsetCoordinateMillimeters.zUncertainty =
                in[i].phaseCenterOffsetCoordinateMillimeters.zUncertainty;

        numberOfRows = in[i].phaseCenterVariationCorrectionMillimeters.size();
        out[i].phaseCenterVariationCorrectionMillimeters.resize(numberOfRows);
        for (uint32_t j = 0; j < numberOfRows; j++) {
            numberOfColumns = in[i].phaseCenterVariationCorrectionMillimeters[j].size();
            out[i].phaseCenterVariationCorrectionMillimeters[j].row.resize(numberOfColumns);
            for (uint32_t k = 0; k < numberOfColumns; k++) {
                out[i].phaseCenterVariationCorrectionMillimeters[j].row[k] =
                        in[i].phaseCenterVariationCorrectionMillimeters[j][k];
            }
        }

        numberOfRows = in[i].phaseCenterVariationCorrectionUncertaintyMillimeters.size();
        out[i].phaseCenterVariationCorrectionUncertaintyMillimeters.resize(numberOfRows);
        for (uint32_t j = 0; j < numberOfRows; j++) {
            numberOfColumns = in[i].phaseCenterVariationCorrectionUncertaintyMillimeters[j].size();
            out[i].phaseCenterVariationCorrectionUncertaintyMillimeters[j].
                    row.resize(numberOfColumns);
            for (uint32_t k = 0; k < numberOfColumns; k++) {
                out[i].phaseCenterVariationCorrectionUncertaintyMillimeters[j].row[k] =
                        in[i].phaseCenterVariationCorrectionUncertaintyMillimeters[j][k];
            }
        }

        numberOfRows = in[i].signalGainCorrectionDbi.size();
        out[i].signalGainCorrectionDbi.resize(numberOfRows);
        for (uint32_t j = 0; j < numberOfRows; j++) {
            numberOfColumns = in[i].signalGainCorrectionDbi[j].size();
            out[i].signalGainCorrectionDbi[j].row.resize(numberOfColumns);
            for (uint32_t k = 0; k < numberOfColumns; k++) {
                out[i].signalGainCorrectionDbi[j].row[k] = in[i].signalGainCorrectionDbi[j][k];
            }
        }

        numberOfRows = in[i].signalGainCorrectionUncertaintyDbi.size();
        out[i].signalGainCorrectionUncertaintyDbi.resize(numberOfRows);
        for (uint32_t j = 0; j < numberOfRows; j++) {
            numberOfColumns = in[i].signalGainCorrectionUncertaintyDbi[j].size();
            out[i].signalGainCorrectionUncertaintyDbi[j].row.resize(numberOfColumns);
            for (uint32_t k = 0; k < numberOfColumns; k++) {
                out[i].signalGainCorrectionUncertaintyDbi[j].row[k] =
                        in[i].signalGainCorrectionUncertaintyDbi[j][k];
            }
        }
    }
}

GnssAntennaInfo::GnssAntennaInfo(Gnss* gnss) : mGnss(gnss) {
    mGnssAntennaInfoDeathRecipient = new GnssAntennaInfoDeathRecipient(this);
    spGnssAntennaInfo = this;
}

GnssAntennaInfo::~GnssAntennaInfo() {
    spGnssAntennaInfo = nullptr;
}

// Methods from ::android::hardware::gnss::V2_1::IGnssAntennaInfo follow.
Return<GnssAntennaInfo::GnssAntennaInfoStatus>
        GnssAntennaInfo::setCallback(const sp<IGnssAntennaInfoCallback>& callback)  {
    uint32_t retValue;
    if (mGnss == nullptr) {
        LOC_LOGE("%s]: mGnss is nullptr", __FUNCTION__);
        return GnssAntennaInfoStatus::ERROR_GENERIC;
    }

    mGnssAntennaInfoCbIface = callback;
    retValue = mGnss->getGnssInterface()->antennaInfoInit(aiGnssAntennaInfoCb);

    switch (retValue) {
    case ANTENNA_INFO_SUCCESS: return GnssAntennaInfoStatus::SUCCESS;
    case ANTENNA_INFO_ERROR_ALREADY_INIT: return GnssAntennaInfoStatus::ERROR_ALREADY_INIT;
    case ANTENNA_INFO_ERROR_GENERIC:
    default: return GnssAntennaInfoStatus::ERROR_GENERIC;
    }
}

Return<void> GnssAntennaInfo::close(void)  {
    if (mGnss == nullptr) {
        LOC_LOGE("%s]: mGnss is nullptr", __FUNCTION__);
        return Void();
    }

    mGnss->getGnssInterface()->antennaInfoClose();

    return Void();
}

void GnssAntennaInfo::aiGnssAntennaInfoCb
        (std::vector<GnssAntennaInformation> gnssAntennaInformations) {
    if (nullptr != spGnssAntennaInfo) {
        spGnssAntennaInfo->gnssAntennaInfoCb(gnssAntennaInformations);
    }
}

void GnssAntennaInfo::gnssAntennaInfoCb
        (std::vector<GnssAntennaInformation> gnssAntennaInformations) {

    if (mGnssAntennaInfoCbIface != nullptr) {
        hidl_vec<IGnssAntennaInfoCallback::GnssAntennaInfo> antennaInfos;

        // Convert from one structure to another
        convertGnssAntennaInfo(gnssAntennaInformations, antennaInfos);

        auto r = mGnssAntennaInfoCbIface->gnssAntennaInfoCb(antennaInfos);
        if (!r.isOk()) {
            LOC_LOGw("Error antenna info cb %s", r.description().c_str());
        }
    } else {
        LOC_LOGw("setCallback has not been called yet");
    }
}

}  // namespace implementation
}  // namespace V2_1
}  // namespace gnss
}  // namespace hardware
}  // namespace android
