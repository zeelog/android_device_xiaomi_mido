/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *	* Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials provided
 *	  with the distribution.
 *	* Neither the name of The Linux Foundation nor the names of its
 *	  contributors may be used to endorse or promote products derived
 *	  from this software without specific prior written permission.
 *
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

#ifndef ANDROID_QTI_VENDOR_THERMAL_H
#define ANDROID_QTI_VENDOR_THERMAL_H

#include <mutex>
#include <thread>

#include <android/hardware/thermal/2.0/IThermal.h>
#include <hidl/MQDescriptor.h>
#include <android/hardware/thermal/2.0/IThermalChangedCallback.h>

#include <hidl/Status.h>

#ifdef ENABLE_THERMAL_NETLINK
#include "thermalUtilsNetlink.h"
#else
#include "thermalUtils.h"
#endif

#include "thermalData.h"

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

struct CallbackSetting {
	sp<IThermalChangedCallback> callback;
	bool is_filter_type;
	TemperatureType type;

	CallbackSetting(sp<IThermalChangedCallback> callback,
			bool is_filter_type, TemperatureType type)
			: callback(callback),
			is_filter_type(is_filter_type), type(type) {}
};

class Thermal : public IThermal {
	public:
		Thermal();
		~Thermal() = default;

		Thermal(const Thermal &) = delete;
		void operator=(const Thermal &) = delete;

		Return<void> getTemperatures(
				getTemperatures_cb _hidl_cb) override;
		Return<void> getCpuUsages(getCpuUsages_cb _hidl_cb) override;
		Return<void> getCoolingDevices(
				getCoolingDevices_cb _hidl_cb) override;

		Return<void> getCurrentTemperatures(
				bool filterType,
				TemperatureType type,
				getCurrentTemperatures_cb _hidl_cb) override;
		Return<void> getTemperatureThresholds(
				bool filterType,
				TemperatureType type,
				getTemperatureThresholds_cb _hidl_cb) override;
		Return<void> registerThermalChangedCallback(
				const sp<IThermalChangedCallback> &callback,
				bool filterType,
				TemperatureType type,
				registerThermalChangedCallback_cb _hidl_cb)
				override;
		Return<void> unregisterThermalChangedCallback(
				const sp<IThermalChangedCallback> &callback,
				unregisterThermalChangedCallback_cb _hidl_cb)
				override;
		Return<void> getCurrentCoolingDevices(
				bool filterType,
				CoolingType type,
				getCurrentCoolingDevices_cb _hidl_cb) override;

		void sendThrottlingChangeCB(const Temperature &t);

	private:
		std::mutex thermal_cb_mutex;
		std::vector<CallbackSetting> cb;
		ThermalUtils utils;
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android

#endif  // ANDROID_QTI_VENDOR_THERMAL_H
