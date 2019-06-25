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

#define LOG_TAG "android.hardware.biometrics.fingerprint@2.1-service.xiaomi_mido"

#include <binder/ProcessState.h>

#include <android/log.h>
#include <hidl/HidlSupport.h>
#include <hidl/HidlTransportSupport.h>
#include <android/hardware/biometrics/fingerprint/2.1/IBiometricsFingerprint.h>
#include <android/hardware/biometrics/fingerprint/2.1/types.h>

#include "BiometricsFingerprint.h"
#include <cutils/properties.h>
#include <errno.h>
#include <unistd.h>

using android::hardware::biometrics::fingerprint::V2_1::IBiometricsFingerprint;
using android::hardware::biometrics::fingerprint::V2_1::implementation::BiometricsFingerprint;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::sp;

bool is_goodix = false;

static constexpr char kGoodixFpDev[] = "/dev/goodix_fp";

int main() {
    char vend[PROPERTY_VALUE_MAX];
    property_get("ro.hardware.fingerprint", vend, "none");

    if (!strcmp(vend, "none")) {
    	ALOGE("ro.hardware.fingerprint not set! Killing " LOG_TAG " binder service!");
        return 1;
    } else if (!strcmp(vend, "goodix")) {
        ALOGI("is_goodix = true");
        is_goodix = true;
    }

    ALOGI("Start biometrics");
    android::sp<IBiometricsFingerprint> bio = BiometricsFingerprint::getInstance();

    if (is_goodix) {
        if (access(kGoodixFpDev, F_OK) != 0) {
            ALOGE("Cannot access %s (%s)", kGoodixFpDev, strerror(errno));
            return 1;
        }

        // the conventional HAL might start binder services
        android::ProcessState::initWithDriver("/dev/binder");
        android::ProcessState::self()->startThreadPool();
    }

    /* process Binder transaction as a single-threaded program. */
    configureRpcThreadpool(1, true /* callerWillJoin */);

    if (bio != nullptr) {
        if (::android::OK != bio->registerAsService()) {
            return 1;
        }
    } else {
        ALOGE("Can't create instance of BiometricsFingerprint, nullptr");
    }

    joinRpcThreadpool();

    return 0; // should never get here
}
