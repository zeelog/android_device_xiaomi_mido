/*
 * Copyright (C) 2014 The Android Open Source Project
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
#define LOG_TAG "FingerprintHal"
#include <unistd.h>
#include <cutils/log.h>
#include <hardware/fingerprint.h>
#include <binder/IServiceManager.h>

#include "FingerprintDaemonProxy.h"
#include "IFingerprintDaemon.h"
#include "IFingerprintDaemonCallback.h"
#include "FingerprintDaemonCallbackProxy.h"

using namespace android;

sp<IFingerprintDaemon> g_service = NULL;

fingerprint_device_t* getWrapperService();

class BinderDiednotify: public IBinder::DeathRecipient {
    public:
        void binderDied(const wp<IBinder> __unused &who) {
            ALOGE("binderDied");
            g_service = NULL;
        }
};

static sp<BinderDiednotify> gDeathRecipient = new BinderDiednotify();

fingerprint_device_t* getWrapperService(fingerprint_notify_t notify) {
    int64_t ret = 0;
    do {
        if (g_service == NULL) {
            ALOGE("getService g_servie is NULL");

            sp<IServiceManager> sm = defaultServiceManager();
            sp<IBinder> binder = sm->getService(android::FingerprintDaemonProxy::descriptor);
            if (binder == NULL) {
                ALOGE("getService failed");
                sleep(1);
                continue;
            }
            g_service = interface_cast<IFingerprintDaemon>(binder);
            binder->linkToDeath(gDeathRecipient, NULL, 0);

            if (g_service != NULL) {
                ALOGE("getService succeed");
                sp<android::FingerprintDaemonCallbackProxy> callback =
                        new FingerprintDaemonCallbackProxy();
                FingerprintDaemonCallbackProxy::setDevice(notify);
                g_service->init(callback);

                ret = g_service->openHal();
                if (ret == 0) {
                    ALOGE("getService openHal failed!");
                    g_service = NULL;
                }
            }
        }
    } while (0);

    return reinterpret_cast<fingerprint_device_t*>(ret);
}
