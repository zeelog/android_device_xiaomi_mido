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

#define LOG_NDEBUG 0
#define LOG_TAG "FingerprintDaemonCallbackProxy"

#include <stdlib.h>
#include <utils/String16.h>
#include <utils/Log.h>
#include <hardware/hardware.h>
#include <hardware/hw_auth_token.h>
#include <hardware/fingerprint.h>
#include "IFingerprintDaemonCallback.h"
#include "FingerprintDaemonCallbackProxy.h"

using namespace android;

fingerprint_notify_t FingerprintDaemonCallbackProxy::mNotify = NULL;

FingerprintDaemonCallbackProxy::FingerprintDaemonCallbackProxy() {
}

FingerprintDaemonCallbackProxy::~FingerprintDaemonCallbackProxy() {
}

status_t FingerprintDaemonCallbackProxy::onEnrollResult(int64_t devId, int32_t fpId, int32_t  gpId,
        int32_t rem) {
    fingerprint_msg_t message;
    message.type = FINGERPRINT_TEMPLATE_ENROLLING;
    message.data.enroll.finger.fid = fpId;
    message.data.enroll.finger.gid = gpId;
    message.data.enroll.samples_remaining = rem;

    if(mNotify != NULL) {
        mNotify(&message);
    } else {
        ALOGE("onEnrollResult mDevice is NULL");
    }

    return 0;
}

status_t FingerprintDaemonCallbackProxy::onAcquired(int64_t devId, int32_t acquiredInfo) {
    fingerprint_msg_t message;
    message.type = FINGERPRINT_ACQUIRED;
    message.data.acquired.acquired_info = (fingerprint_acquired_info_t)acquiredInfo;

    if(mNotify != NULL) {
        mNotify(&message);
    } else {
        ALOGE("onAcquired mDevice is NULL");
    }

    return 0;
}

status_t FingerprintDaemonCallbackProxy::onAuthenticated(int64_t devId, int32_t fingerId,
        int32_t groupId) {
    fingerprint_msg_t message;
    message.type = FINGERPRINT_AUTHENTICATED;
    message.data.authenticated.finger.fid = fingerId;
    message.data.authenticated.finger.gid = groupId;

    if(mNotify != NULL) {
        mNotify(&message);
    } else {
        ALOGE("onAuthenticated mDevice is NULL");
    }

    return 0;
}

status_t FingerprintDaemonCallbackProxy::onError(int64_t devId, int32_t error) {
    fingerprint_msg_t message;
    message.type = FINGERPRINT_ERROR;
    message.data.error = (fingerprint_error_t)error;

    if(mNotify != NULL) {
        mNotify(&message);
    } else {
        ALOGE("onError mDevice is NULL");
    }

    return 0;
}

status_t FingerprintDaemonCallbackProxy::onRemoved(int64_t devId,
        int32_t fingerId, int32_t groupId) {
    fingerprint_msg_t message;
    message.type = FINGERPRINT_TEMPLATE_REMOVED;
    message.data.removed.finger.fid = fingerId;
    message.data.removed.finger.gid = groupId;

    if(mNotify != NULL) {
        mNotify(&message);
    } else {
        ALOGE("onRemoved mDevice is NULL");
    }

    return 0;
}

status_t FingerprintDaemonCallbackProxy::onEnumerate(int64_t devId,
        const int32_t fingerId, const int32_t groupId, int32_t remaining) {

    fingerprint_msg_t message;
    message.type = FINGERPRINT_TEMPLATE_ENUMERATING;
    message.data.enumerated.finger.fid = fingerId;
    message.data.enumerated.finger.gid = groupId;
    message.data.enumerated.remaining_templates = remaining;

    if(mNotify != NULL) {
        mNotify(&message);
    } else {
        ALOGE("onEnumerate mDevice is NULL");
    }

    return 0;
}
