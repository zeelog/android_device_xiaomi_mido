/*
* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
Changes from Qualcomm Innovation Center are provided under the following license:

Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted (subject to the limitations in the
disclaimer below) provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "battery_listener.h"
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "LocSvc_BatteryListener"
#define LOG_NDEBUG 0

#include <android/binder_manager.h>
#include <aidl/android/hardware/health/IHealth.h>
#include <aidl/android/hardware/health/BnHealthInfoCallback.h>
#include <android/hardware/health/2.1/IHealth.h>
#include <android/hardware/health/2.1/IHealthInfoCallback.h>
#include <hidl/HidlTransportSupport.h>
#include <thread>
#include <log_util.h>

using android::hardware::interfacesEqual;
using android::hardware::Return;
using android::hardware::Void;
using HidlBatteryStatus = android::hardware::health::V1_0::BatteryStatus;
using HidlBatteryInfo = android::hardware::health::V2_1::HealthInfo;
using HidlHealthInfoCallback = android::hardware::health::V2_1::IHealthInfoCallback;
using HidlHealth = android::hardware::health::V2_1::IHealth;
using android::hardware::health::V2_0::Result;

using aidl::android::hardware::health::BatteryStatus;
using aidl::android::hardware::health::HealthInfo;
using aidl::android::hardware::health::IHealthInfoCallback;
using aidl::android::hardware::health::BnHealthInfoCallback;
using aidl::android::hardware::health::IHealth;
using namespace std::literals::chrono_literals;

static bool sIsBatteryListened = false;
namespace android {

#define GET_HEALTH_SVC_RETRY_CNT 5
#define GET_HEALTH_SVC_WAIT_TIME_MS 500
typedef std::function<void(bool)> cb_fn_t;

struct AidlBatteryListenerImpl;
static std::shared_ptr<AidlBatteryListenerImpl> batteryListenerAidl;
struct HidlBatteryListenerImpl;
static sp<HidlBatteryListenerImpl> batteryListenerHidl;

struct AidlBatteryListenerImpl : public aidl::android::hardware::health::BnHealthInfoCallback {
    AidlBatteryListenerImpl(cb_fn_t cb);
    virtual ~AidlBatteryListenerImpl ();
    virtual ndk::ScopedAStatus healthInfoChanged(const HealthInfo& in_info);
    static void serviceDied(void* cookie);
    bool isCharging() {
        std::lock_guard<std::mutex> _l(mLock);
        return statusToBool(mStatus);
    }
    status_t init();

  private:
    std::shared_ptr<IHealth> mHealth;
    ndk::ScopedAIBinder_DeathRecipient mDeathRecipient;
    BatteryStatus mStatus;
    cb_fn_t mCb;
    std::mutex mLock;
    std::condition_variable mCond;
    std::unique_ptr<std::thread> mThread;
    bool mDone;
    bool statusToBool(const BatteryStatus &s) const {
        return (s == BatteryStatus::CHARGING) ||
               (s ==  BatteryStatus::FULL);
    }
};

status_t AidlBatteryListenerImpl::init()
{
    int tries = 0;
    auto service_name = std::string() + IHealth::descriptor + "/default";

    if (mHealth != NULL)
        return INVALID_OPERATION;

    do {
        mHealth = IHealth::fromBinder(ndk::SpAIBinder(
                AServiceManager_getService(service_name.c_str())));
        if (mHealth != NULL)
            break;
        usleep(GET_HEALTH_SVC_WAIT_TIME_MS * 1000);
        tries++;
    } while (tries < GET_HEALTH_SVC_RETRY_CNT);

    if (mHealth == NULL) {
        LOC_LOGe("no health service found, retries %d", tries);
        return NO_INIT;
    } else {
        LOC_LOGi("Get health service in %d tries", tries);
    }

    mStatus = BatteryStatus::UNKNOWN;
    auto ret = mHealth->getChargeStatus(&mStatus);

    if (!ret.isOk()) {
        LOC_LOGe("batterylistenerAidl: get charge status transaction error");
    }
    if (mStatus == BatteryStatus::UNKNOWN) {
        LOC_LOGw("batterylistenerAidl: init: invalid battery status");
    }

    mDone = false;
    mThread = std::make_unique<std::thread>([this]() {
            std::unique_lock<std::mutex> lock(mLock);
            BatteryStatus local_status = mStatus;
            while (!mDone) {
                if (local_status == mStatus) {
                    mCond.wait(lock);
                    continue;
                }
                local_status = mStatus;
                switch (local_status) {
                    // NOT_CHARGING is a special event that indicates, a battery is connected,
                    // but not charging. This is seen for approx a second
                    // after charger is plugged in. A charging event is eventually received.
                    // We must try to avoid an unnecessary cb to HAL
                    // only to call it again shortly.
                    // An option to deal with this transient event would be to ignore this.
                    // Or process this event with a slight delay (i.e cancel this event
                    // if a different event comes in within a timeout
                    case BatteryStatus::NOT_CHARGING : {
                        auto mStatusnot_ncharging =
                                [this, local_status]() { return mStatus != local_status; };
                        if (mCond.wait_for(lock, 3s, mStatusnot_ncharging)) // i.e event changed
                            break;
                        [[clang::fallthrough]]; //explicit fall-through between switch labels
                    }
                    default:
                        bool c = statusToBool(local_status);
                        LOC_LOGi("healthInfo cb thread: cb %s", c ? "CHARGING" : "NOT CHARGING");
                        lock.unlock();
                        mCb(c);
                        lock.lock();
                        break;
                }
            }
    });
    auto reg = mHealth->registerCallback(batteryListenerAidl);
    if (!reg.isOk()) {
        LOC_LOGe("Transaction error in registerCallback to HealthHAL");
        return NO_INIT;
    }

    binder_status_t binder_status = AIBinder_linkToDeath(
        mHealth->asBinder().get(), mDeathRecipient.get(), this);
    if (binder_status != STATUS_OK) {
        LOC_LOGe("Failed to link to death, status %d", static_cast<int>(binder_status));
        return NO_INIT;
    }
    return NO_ERROR;
}

AidlBatteryListenerImpl::AidlBatteryListenerImpl(cb_fn_t cb) :
    mCb(cb),
    mDeathRecipient(AIBinder_DeathRecipient_new(AidlBatteryListenerImpl::serviceDied)) {
}

AidlBatteryListenerImpl::~AidlBatteryListenerImpl() {
    {
        std::lock_guard<std::mutex> _l(mLock);
        if (mHealth != NULL) {
            AIBinder_unlinkToDeath(mHealth->asBinder().get(), mDeathRecipient.get(), this);
            mHealth->unregisterCallback(batteryListenerAidl);
        }
    }
    mDone = true;
    if (NULL !=  mThread) {
        mThread->join();
    }
}

void AidlBatteryListenerImpl::serviceDied(void* cookie) {
    auto listener = static_cast<AidlBatteryListenerImpl*> (cookie);
    {
        std::lock_guard<std::mutex> _l(listener->mLock);
        if (listener->mHealth == NULL) {
            LOC_LOGe("health not initialized or unknown interface died");
            return;
        }
        LOC_LOGi("health service died, reinit");
        listener->mDone = true;
    }

    listener->mHealth = NULL;
    listener->mCond.notify_one();
    if (NULL !=  listener->mThread) {
        listener->mThread->join();
    }
    std::lock_guard<std::mutex> _l(listener->mLock);
    listener->init();
}

// this callback seems to be a SYNC callback and so
// waits for return before next event is issued.
// therefore we need not have a queue to process
// NOT_CHARGING and CHARGING concurrencies.
// Replace single var by a list if this assumption is broken
ndk::ScopedAStatus AidlBatteryListenerImpl::healthInfoChanged(const HealthInfo& info) {
    std::unique_lock<std::mutex> lock(mLock);
    if (info.batteryStatus != mStatus) {
        LOC_LOGd("batteryStatus changed from %d to %d", mStatus, info.batteryStatus);
        mStatus = info.batteryStatus;
        mCond.notify_one();
    }
    return ndk::ScopedAStatus::ok();
}


struct HidlBatteryListenerImpl : public hardware::health::V2_1::IHealthInfoCallback,
                             public hardware::hidl_death_recipient {
    HidlBatteryListenerImpl(cb_fn_t cb);
    virtual ~HidlBatteryListenerImpl ();
    virtual hardware::Return<void> healthInfoChanged(
            const hardware::health::V2_0::HealthInfo& info);
    virtual hardware::Return<void> healthInfoChanged_2_1(
            const hardware::health::V2_1::HealthInfo& info);
    virtual void serviceDied(uint64_t cookie,
                             const wp<hidl::base::V1_0::IBase>& who);
    bool isCharging() {
        std::lock_guard<std::mutex> _l(mLock);
        return statusToBool(mStatus);
    }
  private:
    sp<hardware::health::V2_1::IHealth> mHealth;
    status_t init();
    HidlBatteryStatus mStatus;
    cb_fn_t mCb;
    std::mutex mLock;
    std::condition_variable mCond;
    std::unique_ptr<std::thread> mThread;
    bool mDone;
    bool statusToBool(const HidlBatteryStatus &s) const {
        return (s == HidlBatteryStatus::CHARGING) ||
               (s ==  HidlBatteryStatus::FULL);
    }
};

status_t HidlBatteryListenerImpl::init()
{
    int tries = 0;

    if (mHealth != NULL)
        return INVALID_OPERATION;

    do {
        mHealth = HidlHealth::getService();
        if (mHealth != NULL)
            break;
        usleep(GET_HEALTH_SVC_WAIT_TIME_MS * 1000);
        tries++;
    } while (tries < GET_HEALTH_SVC_RETRY_CNT);

    if (mHealth == NULL) {
        LOC_LOGe("no health service found, retries %d", tries);
        return NO_INIT;
    } else {
        LOC_LOGi("Get health service in %d tries", tries);
    }
    mStatus = HidlBatteryStatus::UNKNOWN;
    auto ret = mHealth->getChargeStatus([&](Result r, HidlBatteryStatus status) {
        if (r != Result::SUCCESS) {
            LOC_LOGe("batterylistener: cannot get battery status");
            return;
        }
        mStatus = status;
    });
    if (!ret.isOk()) {
        LOC_LOGe("batterylistener: get charge status transaction error");
    }
    if (mStatus == HidlBatteryStatus::UNKNOWN) {
        LOC_LOGw("batterylistener: init: invalid battery status");
    }
    mDone = false;
    mThread = std::make_unique<std::thread>([this]() {
            std::unique_lock<std::mutex> lock(mLock);
            HidlBatteryStatus local_status = mStatus;
            while (!mDone) {
                if (local_status == mStatus) {
                    mCond.wait(lock);
                    continue;
                }
                local_status = mStatus;
                switch (local_status) {
                    // NOT_CHARGING is a special event that indicates, a battery is connected,
                    // but not charging. This is seen for approx a second
                    // after charger is plugged in. A charging event is eventually received.
                    // We must try to avoid an unnecessary cb to HAL
                    // only to call it again shortly.
                    // An option to deal with this transient event would be to ignore this.
                    // Or process this event with a slight delay (i.e cancel this event
                    // if a different event comes in within a timeout
                    case HidlBatteryStatus::NOT_CHARGING : {
                        auto mStatusnot_ncharging =
                                [this, local_status]() { return mStatus != local_status; };
                        if (mCond.wait_for(lock, 3s, mStatusnot_ncharging)) // i.e event changed
                            break;
                        [[clang::fallthrough]]; //explicit fall-through between switch labels
                    }
                    default:
                        bool c = statusToBool(local_status);
                        LOC_LOGi("healthInfo cb thread: cb %s", c ? "CHARGING" : "NOT CHARGING");
                        lock.unlock();
                        mCb(c);
                        lock.lock();
                        break;
                }
            }
    });
    auto reg = mHealth->registerCallback(this);
    if (!reg.isOk()) {
        LOC_LOGe("Transaction error in registeringCb to HealthHAL death: %s",
                reg.description().c_str());
    }

    auto linked = mHealth->linkToDeath(this, 0 /* cookie */);
    if (!linked.isOk() || linked == false) {
        LOC_LOGe("Transaction error in linking to HealthHAL death: %s",
                linked.description().c_str());
    }
    return NO_ERROR;
}

HidlBatteryListenerImpl::HidlBatteryListenerImpl(cb_fn_t cb) :
        mCb(cb)
{
    init();
}


HidlBatteryListenerImpl::~HidlBatteryListenerImpl()
{
    {
        std::lock_guard<std::mutex> _l(mLock);
        if (mHealth != NULL)
            mHealth->unregisterCallback(this);
            auto r = mHealth->unlinkToDeath(this);
            if (!r.isOk() || r == false) {
                LOC_LOGe("Transaction error in unregister to HealthHAL death: %s",
                        r.description().c_str());
            }
    }
    mDone = true;
    mThread->join();
}

void HidlBatteryListenerImpl::serviceDied(uint64_t cookie __unused,
                                     const wp<hidl::base::V1_0::IBase>& who)
{
    {
        std::lock_guard<std::mutex> _l(mLock);
        if (mHealth == NULL || !interfacesEqual(mHealth, who.promote())) {
            LOC_LOGe("health not initialized or unknown interface died");
            return;
        }
        LOC_LOGi("health service died, reinit");
        mDone = true;
    }
    mHealth = NULL;
    mCond.notify_one();
    mThread->join();
    std::lock_guard<std::mutex> _l(mLock);
    init();
}

// this callback seems to be a SYNC callback and so
// waits for return before next event is issued.
// therefore we need not have a queue to process
// NOT_CHARGING and CHARGING concurrencies.
// Replace single var by a list if this assumption is broken
Return<void> HidlBatteryListenerImpl::healthInfoChanged(
        const hardware::health::V2_0::HealthInfo& info) {
    std::unique_lock<std::mutex> lock(mLock);
    if (info.legacy.batteryStatus != mStatus) {
        LOC_LOGd("batteryStatus changed from %d to %d", mStatus, info.legacy.batteryStatus);
        mStatus = info.legacy.batteryStatus;
        mCond.notify_one();
    }
    return Void();
}

Return<void> HidlBatteryListenerImpl::healthInfoChanged_2_1(
        const hardware::health::V2_1::HealthInfo& info) {
    healthInfoChanged(info.legacy);
    return Void();
}

bool batteryPropertiesListenerIsCharging() {
    if (batteryListenerAidl != nullptr) {
        return batteryListenerAidl->isCharging();
    } else {
        return batteryListenerHidl->isCharging();
    }
}

status_t batteryPropertiesListenerInit(cb_fn_t cb) {
    LOC_LOGd("batteryPropertiesListenerInit entry");
    auto service_name = std::string() + IHealth::descriptor + "/default";
    if (AServiceManager_isDeclared(service_name.c_str())) {
        batteryListenerAidl = ndk::SharedRefBase::make<AidlBatteryListenerImpl>(cb);
        status_t ret = batteryListenerAidl->init();
        if (NO_ERROR == ret) {
            bool isCharging = batteryPropertiesListenerIsCharging();
            LOC_LOGd("charging status: %s charging", isCharging ? "" : "not");
            if (isCharging) {
                cb(isCharging);
            }
        }
        return ret;
    } else {
        batteryListenerHidl = new HidlBatteryListenerImpl(cb);
        bool isCharging = batteryPropertiesListenerIsCharging();
        LOC_LOGd("charging status: %s charging", isCharging ? "" : "not");
        if (isCharging) {
            cb(isCharging);
        }
        return NO_ERROR;
    }
}
} // namespace android

void loc_extn_battery_properties_listener_init(battery_status_change_fn_t fn) {
    if (!sIsBatteryListened) {
        std::thread t1(android::batteryPropertiesListenerInit,
                [=](bool charging) { fn(charging); });
        t1.detach();
        sIsBatteryListened = true;
    }
}

bool loc_extn_battery_properties_is_charging() {
    return android::batteryPropertiesListenerIsCharging();
}
