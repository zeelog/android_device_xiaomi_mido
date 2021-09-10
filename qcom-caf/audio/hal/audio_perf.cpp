/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_primary"

#include <cinttypes>

#include <utils/Log.h>
#include <utils/Mutex.h>

#include <android/hardware/power/1.2/IPower.h>

#include "audio_perf.h"

using android::hardware::power::V1_2::IPower;
using android::hardware::power::V1_2::PowerHint;
using android::hardware::power::V1_2::toString;
using android::hardware::Return;
using android::hardware::Void;
using android::hardware::hidl_death_recipient;
using android::hidl::base::V1_0::IBase;

// Do not use gPowerHAL, use getPowerHal to retrieve a copy instead
static android::sp<IPower> gPowerHal_ = nullptr;
// Protect gPowerHal_
static std::mutex gPowerHalMutex;

// PowerHalDeathRecipient to invalid the client when service dies
struct PowerHalDeathRecipient : virtual public hidl_death_recipient {
    // hidl_death_recipient interface
    virtual void serviceDied(uint64_t, const android::wp<IBase>&) override {
        std::lock_guard<std::mutex> lock(gPowerHalMutex);
        ALOGE("PowerHAL just died");
        gPowerHal_ = nullptr;
    }
};

// Retrieve a copy of client
static android::sp<IPower> getPowerHal() {
    std::lock_guard<std::mutex> lock(gPowerHalMutex);
    static android::sp<PowerHalDeathRecipient> gPowerHalDeathRecipient = nullptr;
    static bool gPowerHalExists = true;

    if (gPowerHalExists && gPowerHal_ == nullptr) {
        gPowerHal_ = IPower::getService();

        if (gPowerHal_ == nullptr) {
            ALOGE("Unable to get Power service");
            gPowerHalExists = false;
        } else {
            if (gPowerHalDeathRecipient == nullptr) {
                gPowerHalDeathRecipient = new PowerHalDeathRecipient();
            }
            Return<bool> linked = gPowerHal_->linkToDeath(
                gPowerHalDeathRecipient, 0 /* cookie */);
            if (!linked.isOk()) {
                ALOGE("Transaction error in linking to PowerHAL death: %s",
                      linked.description().c_str());
                gPowerHal_ = nullptr;
            } else if (!linked) {
                ALOGW("Unable to link to PowerHal death notifications");
                gPowerHal_ = nullptr;
            } else {
                ALOGD("Connect to PowerHAL and link to death "
                      "notification successfully");
            }
        }
    }
    return gPowerHal_;
}

static bool powerHint(PowerHint hint, int32_t data) {
    android::sp<IPower> powerHal = getPowerHal();
    if (powerHal == nullptr) {
        return false;
    }

    auto ret = powerHal->powerHintAsync_1_2(hint, data);

    if (!ret.isOk()) {
        ALOGE("powerHint failed, hint: %s, data: %" PRId32 ",  error: %s",
              toString(hint).c_str(),
              data,
              ret.description().c_str());
    }
    return ret.isOk();
}

int audio_streaming_hint_start() {
    return powerHint(PowerHint::AUDIO_STREAMING, 1);
}

int audio_streaming_hint_end() {
    return powerHint(PowerHint::AUDIO_STREAMING, 0);
}

int audio_low_latency_hint_start() {
    return powerHint(PowerHint::AUDIO_LOW_LATENCY, 1);
}

int audio_low_latency_hint_end() {
    return powerHint(PowerHint::AUDIO_LOW_LATENCY, 0);
}
