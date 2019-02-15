/*
 * Copyright (C) 2019 The LineageOS Project
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

#define LOG_TAG "vendor.lineage.touch@1.0-service.xiaomi_mido"

#include <android-base/logging.h>
#include <binder/ProcessState.h>
#include <hidl/HidlTransportSupport.h>

#include "GloveMode.h"
#include "KeyDisabler.h"

using android::OK;
using android::sp;
using android::status_t;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;

using ::vendor::lineage::touch::V1_0::IGloveMode;
using ::vendor::lineage::touch::V1_0::implementation::GloveMode;
using ::vendor::lineage::touch::V1_0::IKeyDisabler;
using ::vendor::lineage::touch::V1_0::implementation::KeyDisabler;

int main() {
    sp<IGloveMode> gloveMode;
    sp<KeyDisabler> keyDisabler;
    status_t status;

    LOG(INFO) << "Touch HAL service is starting.";

    gloveMode = new GloveMode();
    if (gloveMode == nullptr) {
        LOG(ERROR) << "Can not create an instance of Touch HAL GloveMode Iface, exiting.";
        goto shutdown;
    }

    keyDisabler = new KeyDisabler();
    if (keyDisabler == nullptr) {
        LOG(ERROR) << "Can not create an instance of Touch HAL KeyDisabler Iface, exiting.";
        goto shutdown;
    }

    configureRpcThreadpool(2, true /*callerWillJoin*/);

    status = gloveMode->registerAsService();
    if (status != OK) {
        LOG(ERROR) << "Could not register service for Touch HAL GloveMode Iface ("
                   << status << ")";
        goto shutdown;
    }

    status = keyDisabler->registerAsService();
    if (status != OK) {
        LOG(ERROR) << "Could not register service for Touch HAL KeyDisabler Iface ("
                   << status << ")";
        goto shutdown;
    }

    LOG(INFO) << "Touch HAL service is ready.";
    joinRpcThreadpool();
    // Should not pass this line

shutdown:
    // In normal operation, we don't expect the thread pool to shutdown
    LOG(ERROR) << "Touch HAL service is shutting down.";
    return 1;
}
