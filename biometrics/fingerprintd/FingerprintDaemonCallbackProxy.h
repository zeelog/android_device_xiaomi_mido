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

#ifndef FINGERPRINT_DAEMON_CALLBACK_PROXY_H_
#define FINGERPRINT_DAEMON_CALLBACK_PROXY_H_

#include <hardware/fingerprint.h>
#include "IFingerprintDaemonCallback.h"

namespace android {

class FingerprintDaemonCallbackProxy: public BnFingerprintDaemonCallback {
public:
    virtual status_t onEnrollResult(int64_t devId, int32_t fpId, int32_t gpId, int32_t rem);
    virtual status_t onAcquired(int64_t devId, int32_t acquiredInfo);
    virtual status_t onAuthenticated(int64_t devId, int32_t fingerId, int32_t groupId);
    virtual status_t onError(int64_t devId, int32_t error);
    virtual status_t onRemoved(int64_t devId, int32_t fingerId, int32_t groupId);
    virtual status_t onEnumerate(int64_t devId, const int32_t fpId, const int32_t gpId,
            int32_t rem);
    FingerprintDaemonCallbackProxy();
    virtual ~FingerprintDaemonCallbackProxy();

    static void setDevice(fingerprint_notify_t notify) {
        mNotify = notify;
    }
private:
    static fingerprint_notify_t mNotify;
};

}

#endif // FINGERPRINT_DAEMON_CALLBACK_PROXY_H_
