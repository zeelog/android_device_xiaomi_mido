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

#define LOG_TAG "LocSvc_MeasurementCorrectionsInterface"

#include <log_util.h>
#include "MeasurementCorrections.h"

namespace android {
namespace hardware {
namespace gnss {
namespace measurement_corrections {
namespace V1_1 {
namespace implementation {

using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::sp;
using ::android::hardware::gnss::measurement_corrections::V1_0::IMeasurementCorrectionsCallback;
using ::android::hardware::gnss::measurement_corrections::V1_0::IMeasurementCorrections;
using MeasurementCorrectionsV1_0 =
        ::android::hardware::gnss::measurement_corrections::V1_0::MeasurementCorrections;
using MeasurementCorrectionsV1_1 =
        ::android::hardware::gnss::measurement_corrections::V1_1::MeasurementCorrections;

static MeasurementCorrections* spMeasurementCorrections = nullptr;

void MeasurementCorrections::GnssMeasurementCorrectionsDeathRecipient::serviceDied(uint64_t cookie,
        const wp<IBase>& who) {
    LOC_LOGe("service died. cookie: %llu, who: %p", static_cast<unsigned long long>(cookie), &who);
    // Gnss::GnssDeathrecipient will stop the session
    // we inform the adapter that service has died
    if (nullptr == spMeasurementCorrections) {
        LOC_LOGe("spMeasurementCorrections is nullptr");
        return;
    }
    if (nullptr == spMeasurementCorrections->mGnss ||
        nullptr == spMeasurementCorrections->mGnss->getGnssInterface()) {
        LOC_LOGe("Null GNSS interface");
        return;
    }
    spMeasurementCorrections->mGnss->getGnssInterface()->measCorrClose();
}

MeasurementCorrections::MeasurementCorrections(Gnss* gnss) : mGnss(gnss) {
    mGnssMeasurementCorrectionsDeathRecipient = new GnssMeasurementCorrectionsDeathRecipient(this);
    spMeasurementCorrections = this;
}

MeasurementCorrections::~MeasurementCorrections() {
    spMeasurementCorrections = nullptr;
}

void MeasurementCorrections::measCorrSetCapabilitiesCb(
        GnssMeasurementCorrectionsCapabilitiesMask capabilities) {
    if (nullptr != spMeasurementCorrections) {
        spMeasurementCorrections->setCapabilitiesCb(capabilities);
    }
}

void MeasurementCorrections::setCapabilitiesCb(
    GnssMeasurementCorrectionsCapabilitiesMask capabilities) {

    if (mMeasurementCorrectionsCbIface != nullptr) {
        uint32_t measCorrCapabilities = 0;

        // Convert from one enum to another
        if (capabilities & GNSS_MEAS_CORR_LOS_SATS) {
            measCorrCapabilities |=
                    IMeasurementCorrectionsCallback::Capabilities::LOS_SATS;
        }
        if (capabilities & GNSS_MEAS_CORR_EXCESS_PATH_LENGTH) {
            measCorrCapabilities |=
                    IMeasurementCorrectionsCallback::Capabilities::EXCESS_PATH_LENGTH;
        }
        if (capabilities & GNSS_MEAS_CORR_REFLECTING_PLANE) {
            measCorrCapabilities |=
                    IMeasurementCorrectionsCallback::Capabilities::REFLECTING_PLANE;
        }

        auto r = mMeasurementCorrectionsCbIface->setCapabilitiesCb(measCorrCapabilities);
        if (!r.isOk()) {
            LOC_LOGw("Error invoking setCapabilitiesCb %s", r.description().c_str());
        }
    } else {
        LOC_LOGw("setCallback has not been called yet");
    }
}

Return<bool> MeasurementCorrections::setCorrections(
        const MeasurementCorrectionsV1_0& corrections) {

    GnssMeasurementCorrections gnssMeasurementCorrections = {};

    V2_1::implementation::convertMeasurementCorrections(corrections, gnssMeasurementCorrections);

    return mGnss->getGnssInterface()->measCorrSetCorrections(gnssMeasurementCorrections);
}

Return<bool> MeasurementCorrections::setCorrections_1_1(
        const MeasurementCorrectionsV1_1& corrections) {

    GnssMeasurementCorrections gnssMeasurementCorrections = {};

    V2_1::implementation::convertMeasurementCorrections(
            corrections.v1_0, gnssMeasurementCorrections);

    gnssMeasurementCorrections.hasEnvironmentBearing = corrections.hasEnvironmentBearing;
    gnssMeasurementCorrections.environmentBearingDegrees =
            corrections.environmentBearingDegrees;
    gnssMeasurementCorrections.environmentBearingUncertaintyDegrees =
            corrections.environmentBearingUncertaintyDegrees;

    for (int i = 0; i < corrections.satCorrections.size(); i++) {
        GnssSingleSatCorrection gnssSingleSatCorrection = {};

        V2_1::implementation::convertSingleSatCorrections(
                corrections.satCorrections[i].v1_0, gnssSingleSatCorrection);
        switch (corrections.satCorrections[i].constellation) {
        case (::android::hardware::gnss::V2_0::GnssConstellationType::GPS):
            gnssSingleSatCorrection.svType = GNSS_SV_TYPE_GPS;
            break;
        case (::android::hardware::gnss::V2_0::GnssConstellationType::SBAS):
            gnssSingleSatCorrection.svType = GNSS_SV_TYPE_SBAS;
            break;
        case (::android::hardware::gnss::V2_0::GnssConstellationType::GLONASS):
            gnssSingleSatCorrection.svType = GNSS_SV_TYPE_GLONASS;
            break;
        case (::android::hardware::gnss::V2_0::GnssConstellationType::QZSS):
            gnssSingleSatCorrection.svType = GNSS_SV_TYPE_QZSS;
            break;
        case (::android::hardware::gnss::V2_0::GnssConstellationType::BEIDOU):
            gnssSingleSatCorrection.svType = GNSS_SV_TYPE_BEIDOU;
            break;
        case (::android::hardware::gnss::V2_0::GnssConstellationType::GALILEO):
            gnssSingleSatCorrection.svType = GNSS_SV_TYPE_GALILEO;
            break;
        case (::android::hardware::gnss::V2_0::GnssConstellationType::IRNSS):
            gnssSingleSatCorrection.svType = GNSS_SV_TYPE_NAVIC;
            break;
        case (::android::hardware::gnss::V2_0::GnssConstellationType::UNKNOWN):
        default:
            gnssSingleSatCorrection.svType = GNSS_SV_TYPE_UNKNOWN;
            break;
        }
        gnssMeasurementCorrections.satCorrections.push_back(gnssSingleSatCorrection);
    }

    return mGnss->getGnssInterface()->measCorrSetCorrections(gnssMeasurementCorrections);
}

Return<bool> MeasurementCorrections::setCallback(
        const sp<V1_0::IMeasurementCorrectionsCallback>& callback) {

    if (nullptr == mGnss || nullptr == mGnss->getGnssInterface()) {
        LOC_LOGe("Null GNSS interface");
        return false;
    }
    mMeasurementCorrectionsCbIface = callback;

    return mGnss->getGnssInterface()->measCorrInit(measCorrSetCapabilitiesCb);
}

}  // namespace implementation
}  // namespace V1_1
}  // namespace measurement_corrections
}  // namespace gnss
}  // namespace hardware
}  // namespace android
