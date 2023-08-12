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


#ifndef ANDROID_HARDWARE_GNSS_V2_1_GNSSANTENNAINFO_H
#define ANDROID_HARDWARE_GNSS_V2_1_GNSSANTENNAINFO_H

#include <android/hardware/gnss/2.1/IGnssAntennaInfo.h>
#include <hidl/Status.h>

namespace android {
namespace hardware {
namespace gnss {
namespace V2_1 {
namespace implementation {

using ::android::hardware::gnss::V2_1::IGnssAntennaInfo;
using ::android::hardware::gnss::V2_1::IGnssAntennaInfoCallback;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::hidl_vec;
using ::android::hardware::hidl_string;
using ::android::sp;

struct Gnss;
struct GnssAntennaInfo : public IGnssAntennaInfo {
    GnssAntennaInfo(Gnss* gnss);
    ~GnssAntennaInfo();

    /*
     * Methods from ::android::hardware::gnss::V1_1::IGnssAntennaInfo follow.
     * These declarations were generated from IGnssAntennaInfo.hal.
     */
    Return<GnssAntennaInfoStatus>
            setCallback(const sp<IGnssAntennaInfoCallback>& callback) override;
    Return<void> close(void) override;

    void gnssAntennaInfoCb(std::vector<GnssAntennaInformation> gnssAntennaInformations);

    static void aiGnssAntennaInfoCb(std::vector<GnssAntennaInformation> gnssAntennaInformations);

 private:
    struct GnssAntennaInfoDeathRecipient : hidl_death_recipient {
        GnssAntennaInfoDeathRecipient(sp<GnssAntennaInfo> gnssAntennaInfo) :
                mGnssAntennaInfo(gnssAntennaInfo) {
        }
        ~GnssAntennaInfoDeathRecipient() = default;
        virtual void serviceDied(uint64_t cookie, const wp<IBase>& who) override;
        sp<GnssAntennaInfo> mGnssAntennaInfo;
    };

 private:
    sp<GnssAntennaInfoDeathRecipient> mGnssAntennaInfoDeathRecipient = nullptr;
    sp<IGnssAntennaInfoCallback> mGnssAntennaInfoCbIface = nullptr;
    Gnss* mGnss = nullptr;
};

}  // namespace implementation
}  // namespace V2_1
}  // namespace gnss
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_GNSS_V2_1_GNSSANTENNAINFO_H
