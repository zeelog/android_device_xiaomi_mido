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
#define LOG_TAG "IFingerprintDaemonCallback"
#include <stdint.h>
#include <sys/types.h>
#include <utils/Log.h>
#include <binder/Parcel.h>

#include "IFingerprintDaemonCallback.h"

namespace android {

status_t BnFingerprintDaemonCallback::onTransact(uint32_t code, const Parcel& data, Parcel* reply,
        uint32_t flags) {
    switch (code) {
    case ON_ENROLL_RESULT: {
        CHECK_INTERFACE(IFingerprintDaemonCallback, data, reply);
        int64_t devId = data.readInt64();
        int32_t fpId = data.readInt32();
        int32_t gpId = data.readInt32();
        int32_t rem = data.readInt32();
        onEnrollResult(devId, fpId, gpId, rem);
        return NO_ERROR;
    }
    case ON_ACQUIRED: {
        CHECK_INTERFACE(IFingerprintDaemonCallback, data, reply);
        int64_t devId = data.readInt64();
        int32_t acquiredInfo = data.readInt32();
        onAcquired(devId, acquiredInfo);
        return NO_ERROR;
    }
    case ON_AUTHENTICATED: {
        CHECK_INTERFACE(IFingerprintDaemonCallback, data, reply);
        int64_t devId = data.readInt64();
        int32_t fpId = data.readInt32();
        int32_t gpId = data.readInt32();
        onAuthenticated(devId, fpId, gpId);
        return NO_ERROR;
    }
    case ON_ERROR: {
        CHECK_INTERFACE(IFingerprintDaemonCallback, data, reply);
        int64_t devId = data.readInt64();
        int32_t error = data.readInt32();
        onError(devId, error);
        return NO_ERROR;
    }
    case ON_REMOVED: {
        CHECK_INTERFACE(IFingerprintDaemonCallback, data, reply);
        int64_t devId = data.readInt64();
        int32_t fpId = data.readInt32();
        int32_t gpId = data.readInt32();
        onRemoved(devId, fpId, gpId);
        return NO_ERROR;
    }
    case ON_ENUMERATE: {
        CHECK_INTERFACE(IFingerprintDaemonCallback, data, reply);
        int64_t devId = data.readInt64();
        int32_t fpId = data.readInt32();
        int32_t gpId = data.readInt32();
        int32_t rem = data.readInt32();
        onEnumerate(devId, fpId, gpId, rem);
        return NO_ERROR;
    }
    default:
        return BBinder::onTransact(code, data, reply, flags);
    }
}

class BpFingerprintDaemonCallback : public BpInterface<IFingerprintDaemonCallback>
{
public:
    BpFingerprintDaemonCallback(const sp<IBinder>& impl) :
            BpInterface<IFingerprintDaemonCallback>(impl) {
    }
    virtual status_t onEnrollResult(int64_t devId, int32_t fpId, int32_t gpId, int32_t rem) {
        Parcel data, reply;
        data.writeInterfaceToken(IFingerprintDaemonCallback::getInterfaceDescriptor());
        data.writeInt64(devId);
        data.writeInt32(fpId);
        data.writeInt32(gpId);
        data.writeInt32(rem);
        return remote()->transact(ON_ENROLL_RESULT, data, &reply, IBinder::FLAG_ONEWAY);
    }

    virtual status_t onAcquired(int64_t devId, int32_t acquiredInfo) {
        Parcel data, reply;
        data.writeInterfaceToken(IFingerprintDaemonCallback::getInterfaceDescriptor());
        data.writeInt64(devId);
        data.writeInt32(acquiredInfo);
        return remote()->transact(ON_ACQUIRED, data, &reply, IBinder::FLAG_ONEWAY);
    }

    virtual status_t onAuthenticated(int64_t devId, int32_t fpId, int32_t gpId) {
        Parcel data, reply;
        data.writeInterfaceToken(IFingerprintDaemonCallback::getInterfaceDescriptor());
        data.writeInt64(devId);
        data.writeInt32(fpId);
        data.writeInt32(gpId);
        return remote()->transact(ON_AUTHENTICATED, data, &reply, IBinder::FLAG_ONEWAY);
    }

    virtual status_t onError(int64_t devId, int32_t error) {
        Parcel data, reply;
        data.writeInterfaceToken(IFingerprintDaemonCallback::getInterfaceDescriptor());
        data.writeInt64(devId);
        data.writeInt32(error);
        return remote()->transact(ON_ERROR, data, &reply, IBinder::FLAG_ONEWAY);
    }

    virtual status_t onRemoved(int64_t devId, int32_t fpId, int32_t gpId) {
        Parcel data, reply;
        data.writeInterfaceToken(IFingerprintDaemonCallback::getInterfaceDescriptor());
        data.writeInt64(devId);
        data.writeInt32(fpId);
        data.writeInt32(gpId);
        return remote()->transact(ON_REMOVED, data, &reply, IBinder::FLAG_ONEWAY);
    }

    virtual status_t onEnumerate(int64_t devId, int32_t fpId, int32_t gpId, int32_t rem) {
        Parcel data, reply;
        data.writeInterfaceToken(IFingerprintDaemonCallback::getInterfaceDescriptor());
        data.writeInt64(devId);
        data.writeInt32(fpId);
        data.writeInt32(gpId);
        data.writeInt32(rem);
        return remote()->transact(ON_ENUMERATE, data, &reply, IBinder::FLAG_ONEWAY);
    }
};

IMPLEMENT_META_INTERFACE(FingerprintDaemonCallback,
        "android.hardware.fingerprint.IFingerprintDaemonCallback");

}; // namespace android
