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

#define LOG_TAG "biometrics.fingerprint@2.0-service"

#include <binder/IPCThreadState.h>
#include <binder/IServiceManager.h>
#include <binder/PermissionCache.h>
#include <binder/ProcessState.h>
#include <utils/String16.h>
#include <keystore/keystore.h> // for error codes

#include <android/log.h>
#include <hidl/HidlSupport.h>
#include <hidl/HidlTransportSupport.h>
#include <android/hardware/biometrics/fingerprint/2.1/IBiometricsFingerprint.h>
#include <android/hardware/biometrics/fingerprint/2.1/types.h>

#include "BiometricsFingerprint.h"
#include "fingerprintd/FingerprintDaemonProxy.h"

using android::hardware::biometrics::fingerprint::V2_1::IBiometricsFingerprint;
using android::hardware::biometrics::fingerprint::V2_1::implementation::BiometricsFingerprint;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::sp;

int main() {
    ALOGE("Start fingerprintd");
    android::sp<android::IServiceManager> serviceManager = android::defaultServiceManager();
    android::sp<android::FingerprintDaemonProxy> proxy =
            android::FingerprintDaemonProxy::getInstance();
    android::status_t ret = serviceManager->addService(
            android::FingerprintDaemonProxy::descriptor, proxy);
    if (ret != android::OK) {
        ALOGE("Couldn't register " LOG_TAG " binder service!");
        return -1;
    }

    ALOGE("Start biometrics");
    android::sp<IBiometricsFingerprint> bio = BiometricsFingerprint::getInstance();
    configureRpcThreadpool(1, false /*callerWillJoin*/);
    if (bio != nullptr) {
        ret = bio->registerAsService();
        if (ret != android::OK) {
            ALOGE("Cannot register BiometricsFingerprint service: %d", ret);
        }
    } else {
        ALOGE("Can't create instance of BiometricsFingerprint, nullptr");
    }

    android::IPCThreadState::self()->joinThreadPool();   // run binder service fingerprintd part

    return 0; // should never get here
}
