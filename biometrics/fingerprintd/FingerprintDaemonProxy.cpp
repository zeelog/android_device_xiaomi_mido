/*
 * Copyright (C) 2015 The Android Open Source Project
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
#define LOG_NDEBUG 0
#define LOG_TAG "FingerprintDaemonProxy"

#include <cutils/properties.h>
#include <binder/IServiceManager.h>
#include <hardware/hardware.h>
#include <hardware/fingerprint.h>
#include <hardware/hw_auth_token.h>
#include <utils/Log.h>

#include "FingerprintDaemonProxy.h"

namespace android {

FingerprintDaemonProxy* FingerprintDaemonProxy::sInstance = NULL;

// Supported fingerprint HAL version
static const uint16_t kVersion = HARDWARE_MODULE_API_VERSION(2, 0);

FingerprintDaemonProxy::FingerprintDaemonProxy() : mModule(NULL), mDevice(NULL) {

}

FingerprintDaemonProxy::~FingerprintDaemonProxy() {
    closeHal();
}

void FingerprintDaemonProxy::hal_notify_callback(const fingerprint_msg_t *msg) {
    FingerprintDaemonProxy* instance = FingerprintDaemonProxy::getInstance();
    if (instance == NULL) {
        ALOGE("failed to obtain fingerprintd instance");
        return;
    }
    if (msg == NULL) {
        ALOGE("msg is NULL");
        return;
    }
    ALOGD("hal_notify_callback()");
    instance->mCallback(msg);
}

void FingerprintDaemonProxy::init(fingerprint_notify_t notify) {
    mCallback = notify;
}

int32_t FingerprintDaemonProxy::enroll(const uint8_t* token, ssize_t tokenSize, int32_t groupId,
        int32_t timeout) {
    if (tokenSize != sizeof(hw_auth_token_t) ) {
        ALOGE("enroll() : invalid token size %zu\n", tokenSize);
        return -1;
    }
    const hw_auth_token_t* authToken = reinterpret_cast<const hw_auth_token_t*>(token);
    return mDevice->enroll(mDevice, authToken, groupId, timeout);
}

uint64_t FingerprintDaemonProxy::preEnroll() {
    return mDevice->pre_enroll(mDevice);
}

int32_t FingerprintDaemonProxy::postEnroll() {
    return mDevice->post_enroll(mDevice);
}

int32_t FingerprintDaemonProxy::stopEnrollment() {
    return mDevice->cancel(mDevice);
}

int32_t FingerprintDaemonProxy::authenticate(uint64_t sessionId, uint32_t groupId) {
    return mDevice->authenticate(mDevice, sessionId, groupId);
}

int32_t FingerprintDaemonProxy::stopAuthentication() {
    return mDevice->cancel(mDevice);
}

int32_t FingerprintDaemonProxy::remove(int32_t fingerId, int32_t groupId) {
    return mDevice->remove(mDevice, groupId, fingerId);
}

int32_t FingerprintDaemonProxy::enumerate() {
    return mDevice->enumerate(mDevice);
}

int32_t FingerprintDaemonProxy::cancel() {
    int ret = mDevice->cancel(mDevice);
    return ret;
}

uint64_t FingerprintDaemonProxy::getAuthenticatorId() {
    return mDevice->get_authenticator_id(mDevice);
}

int32_t FingerprintDaemonProxy::setActiveGroup(int32_t groupId, const uint8_t* path,
        ssize_t pathlen) {
    if (pathlen >= PATH_MAX || pathlen <= 0) {
        ALOGE("Bad path length: %zd", pathlen);
        return -1;
    }
    // Convert to null-terminated string
    char path_name[PATH_MAX];
    memcpy(path_name, path, pathlen);
    path_name[pathlen] = '\0';
    return mDevice->set_active_group(mDevice, groupId, path_name);
}

int64_t FingerprintDaemonProxy::openHal() {
    int err;
    const hw_module_t *hw_module = NULL;

    if (0 != (err = hw_get_module(FINGERPRINT_HARDWARE_MODULE_ID, &hw_module))) {
        ALOGE("Can't open fingerprint HW Module, error: %d", err);
        return 0;
    }
    if (NULL == hw_module) {
        ALOGE("No valid fingerprint module");
        return 0;
    }

    mModule = reinterpret_cast<const fingerprint_module_t*>(hw_module);

    if (mModule->common.methods->open == NULL) {
        ALOGE("No valid open method");
        return 0;
    }

    hw_device_t *device = NULL;

    if (0 != (err = mModule->common.methods->open(hw_module, NULL, &device))) {
        ALOGE("Can't open fingerprint methods, error: %d", err);
        return 0;
    }

    if (kVersion != device->version) {
        ALOGE("Wrong fp version. Expected %d, got %d", kVersion, device->version);
        // return 0;
    }

    mDevice = reinterpret_cast<fingerprint_device_t*>(device);
    err = mDevice->set_notify(mDevice, hal_notify_callback);
    if (err < 0) {
        ALOGE("Failed in call to set_notify(), err=%d", err);
        return 0;
    }

    // Sanity check - remove
    if (mDevice->notify != hal_notify_callback) {
        ALOGE("NOTIFY not set properly: %p != %p", mDevice->notify, hal_notify_callback);
    }

    ALOGE("fingerprint HAL successfully initialized");
    return reinterpret_cast<int64_t>(mDevice); // This is just a handle
}

int32_t FingerprintDaemonProxy::closeHal() {
    if (mDevice == NULL) {
        ALOGE("No valid device");
        return -ENOSYS;
    }
    int err;
    if (0 != (err = mDevice->common.close(reinterpret_cast<hw_device_t*>(mDevice)))) {
        ALOGE("Can't close fingerprint module, error: %d", err);
        return err;
    }
    mDevice = NULL;
    return 0;
}

void FingerprintDaemonProxy::binderDied(const __unused wp<IBinder>& who) {
    int err;
    if (0 != (err = closeHal())) {
        ALOGE("Can't close fingerprint device, error: %d", err);
    }
}

}
