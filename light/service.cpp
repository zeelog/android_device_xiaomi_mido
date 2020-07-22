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

#define LOG_TAG "android.hardware.light@2.0-service.xiaomi_mido"

#include <hidl/HidlLazyUtils.h>
#include <hidl/HidlTransportSupport.h>

#include "Light.h"

using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;

using android::hardware::LazyServiceRegistrar;
using android::hardware::light::V2_0::ILight;
using android::hardware::light::V2_0::implementation::Light;

using android::OK;
using android::sp;
using android::status_t;

int main() {
    sp<ILight> service = new Light();

    configureRpcThreadpool(1, true);

    android::status_t status;
    auto serviceRegistrar = std::make_shared<LazyServiceRegistrar>();
    status = serviceRegistrar->registerService(service);

    if (status != OK) {
        ALOGE("Cannot register Light HAL service.");
        return 1;
    }

    ALOGI("Light HAL service ready.");

    joinRpcThreadpool();

    ALOGI("Light HAL service failed to join thread pool.");
    return 1;
}
