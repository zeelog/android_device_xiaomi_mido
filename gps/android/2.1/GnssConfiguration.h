/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 * Not a Contribution
 */

 /* Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2_0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2_0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifndef ANDROID_HARDWARE_GNSS_V2_1_GNSSCONFIGURATION_H
#define ANDROID_HARDWARE_GNSS_V2_1_GNSSCONFIGURATION_H

#include <android/hardware/gnss/2.1/IGnssConfiguration.h>
#include <hidl/Status.h>

namespace android {
namespace hardware {
namespace gnss {
namespace V2_1 {
namespace implementation {

using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::hidl_vec;
using ::android::hardware::hidl_string;
using ::android::hardware::gnss::V2_0::GnssConstellationType;
using ::android::sp;

/*
 * Interface for passing GNSS configuration info from platform to HAL.
 */
struct Gnss;
struct GnssConfiguration : public V2_1::IGnssConfiguration {
    GnssConfiguration(Gnss* gnss);
    ~GnssConfiguration() = default;


    /*
     * Methods from ::android::hardware::gnss::V1_0::IGnssConfiguration follow.
     * These declarations were generated from IGnssConfiguration.hal.
     */
    Return<bool> setSuplVersion(uint32_t version) override;
    Return<bool> setSuplMode(uint8_t mode) override;
    Return<bool> setSuplEs(bool enabled) override;
    Return<bool> setLppProfile(uint8_t lppProfileMask) override;
    Return<bool> setGlonassPositioningProtocol(uint8_t protocol) override;
    Return<bool> setEmergencySuplPdn(bool enable) override;
    Return<bool> setGpsLock(uint8_t lock) override;

    // Methods from ::android::hardware::gnss::V1_1::IGnssConfiguration follow.
    Return<bool> setBlacklist(
            const hidl_vec<V1_1::IGnssConfiguration::BlacklistedSource>& blacklist) override;

    // Methods from ::android::hardware::gnss::V2_0::IGnssConfiguration follow.
    Return<bool> setEsExtensionSec(uint32_t emergencyExtensionSeconds) override;
    // Methods from ::android::hardware::gnss::V2_1::IGnssConfiguration follow.
    Return<bool> setBlacklist_2_1(
            const hidl_vec<V2_1::IGnssConfiguration::BlacklistedSource>& blacklist) override;

 private:
    Gnss* mGnss = nullptr;
    bool setBlacklistedSource(
            GnssSvIdSource& copyToSource,
            const GnssConfiguration::BlacklistedSource& copyFromSource);
    bool setBlacklistedSource(
            GnssSvIdSource& copyToSource, const GnssConstellationType& constellation,
            const int16_t svid);
};

}  // namespace implementation
}  // namespace V2_1
}  // namespace gnss
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_HARDWARE_GNSS_V2_1_GNSSCONFIGURATION_H
