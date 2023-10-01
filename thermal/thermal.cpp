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

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <cerrno>
#include <mutex>
#include <string>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <hidl/HidlTransportSupport.h>

#include "thermal.h"

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

using ::android::hardware::interfacesEqual;

static const Temperature_1_0 dummy_temp_1_0 = {
	.type = TemperatureType_1_0::SKIN,
	.name = "test sensor",
	.currentValue = 30,
	.throttlingThreshold = 40,
	.shutdownThreshold = 60,
	.vrThrottlingThreshold = 40,
};

static const Temperature dummy_temp_2_0 = {
	.type = TemperatureType::SKIN,
	.name = "test sensor",
	.value = 25.0,
	.throttlingStatus = ThrottlingSeverity::NONE,
};

template <typename A, typename B>
Return<void> exit_hal(A _cb, hidl_vec<B> _data, std::string_view _msg) {
	ThermalStatus _status;

	_status.code = ThermalStatusCode::FAILURE;
	_status.debugMessage = _msg.data();
	LOG(ERROR) << _msg;
	_cb(_status, _data);

	return Void();
}

template <typename A>
Return<void> exit_hal(A _cb, std::string_view _msg) {
	ThermalStatus _status;

	_status.code = ThermalStatusCode::FAILURE;
	_status.debugMessage = _msg.data();
	LOG(ERROR) << _msg;
	_cb(_status);

	return Void();
}

Thermal::Thermal():
	utils(std::bind(&Thermal::sendThrottlingChangeCB, this,
				std::placeholders::_1))
{ }

Return<void> Thermal::getTemperatures(getTemperatures_cb _hidl_cb)
{
	ThermalStatus status;
	hidl_vec<Temperature_1_0> temperatures;

	status.code = ThermalStatusCode::SUCCESS;
	if (!utils.isSensorInitialized()) {
		std::vector<Temperature_1_0> _temp = {dummy_temp_1_0};
		LOG(INFO) << "Returning Dummy Value" << std::endl;
		_hidl_cb(status, _temp);
		return Void();
	}

	if (utils.readTemperatures(temperatures) <= 0)
		return exit_hal(_hidl_cb, temperatures,
				"Sensor Temperature read failure.");

	_hidl_cb(status, temperatures);

	return Void();
}

Return<void> Thermal::getCpuUsages(getCpuUsages_cb _hidl_cb)
{

	ThermalStatus status;
	hidl_vec<CpuUsage> cpu_usages;

	status.code = ThermalStatusCode::SUCCESS;
	if (utils.fetchCpuUsages(cpu_usages) <= 0)
		return exit_hal(_hidl_cb, cpu_usages,
				"CPU usage read failure.");

	_hidl_cb(status, cpu_usages);
	return Void();
}

Return<void> Thermal::getCoolingDevices(getCoolingDevices_cb _hidl_cb)
{
	ThermalStatus status;
	hidl_vec<CoolingDevice_1_0> cdev;

	status.code = ThermalStatusCode::SUCCESS;
	/* V1 Cdev requires only Fan Support. */
	_hidl_cb(status, cdev);
	return Void();
}

Return<void> Thermal::getCurrentCoolingDevices(
				bool filterType,
				cdevType type,
				getCurrentCoolingDevices_cb _hidl_cb)
{
	ThermalStatus status;
	hidl_vec<CoolingDevice> cdev;

	status.code = ThermalStatusCode::SUCCESS;
	if (!utils.isCdevInitialized())
		return exit_hal(_hidl_cb, cdev,
			"ThermalHAL not initialized properly.");
	if (utils.readCdevStates(filterType, type, cdev) <= 0)
		return exit_hal(_hidl_cb, cdev,
			"Failed to read thermal cooling devices.");

	_hidl_cb(status, cdev);
	return Void();
}

Return<void> Thermal::getCurrentTemperatures(
				bool filterType,
				TemperatureType type,
				getCurrentTemperatures_cb _hidl_cb)
{
	ThermalStatus status;
	hidl_vec<Temperature> temperatures;

	status.code = ThermalStatusCode::SUCCESS;
	if (!utils.isSensorInitialized())
		return exit_hal(_hidl_cb, temperatures,
			"ThermalHAL not initialized properly.");

	if (utils.readTemperatures(filterType, type, temperatures) <= 0) {
		if (filterType && type != dummy_temp_2_0.type) {
			status.code = ThermalStatusCode::FAILURE;
			status.debugMessage = "Failed to read dummy temperature value";
		} else {
			temperatures = {dummy_temp_2_0};
			LOG(INFO) << "Returning Dummy Temperature Value" << std::endl;
		}
	}

	_hidl_cb(status, temperatures);

	return Void();
}

Return<void> Thermal::getTemperatureThresholds(
				bool filterType,
				TemperatureType type,
				getTemperatureThresholds_cb _hidl_cb)
{
	ThermalStatus status;
	hidl_vec<TemperatureThreshold> thresh;

	status.code = ThermalStatusCode::SUCCESS;
	if (!utils.isSensorInitialized())
		return exit_hal(_hidl_cb, thresh,
			"ThermalHAL not initialized properly.");

	if (utils.readTemperatureThreshold(filterType, type, thresh) <= 0)
		return exit_hal(_hidl_cb, thresh,
		"Sensor Threshold read failure or type not supported.");

	_hidl_cb(status, thresh);

	return Void();
}

Return<void> Thermal::registerThermalChangedCallback(
				const sp<IThermalChangedCallback> &callback,
				bool filterType,
				TemperatureType type,
				registerThermalChangedCallback_cb _hidl_cb)
{
	ThermalStatus status;
	std::lock_guard<std::mutex> _lock(thermal_cb_mutex);

        status.code = ThermalStatusCode::SUCCESS;
	if (callback == nullptr)
		return exit_hal(_hidl_cb, "Invalid nullptr callback");
	if (type == TemperatureType::BCL_VOLTAGE ||
		type == TemperatureType::BCL_CURRENT)
		return exit_hal(_hidl_cb,
			"BCL current and voltage notification not supported");

	for (CallbackSetting _cb: cb) {
		if (interfacesEqual(_cb.callback, callback))
			return exit_hal(_hidl_cb,
				"Same callback interface registered already");
	}
	cb.emplace_back(callback, filterType, type);
	LOG(DEBUG) << "A callback has been registered to ThermalHAL, isFilter: " << filterType
		<< " Type: " << android::hardware::thermal::V2_0::toString(type);

	_hidl_cb(status);
	return Void();
}

Return<void> Thermal::unregisterThermalChangedCallback(
				const sp<IThermalChangedCallback> &callback,
				unregisterThermalChangedCallback_cb _hidl_cb)
{

	ThermalStatus status;
	bool removed = false;
	std::lock_guard<std::mutex> _lock(thermal_cb_mutex);
	std::vector<CallbackSetting>::iterator it;

        status.code = ThermalStatusCode::SUCCESS;
	if (callback == nullptr)
		return exit_hal(_hidl_cb, "Invalid nullptr callback");

	for (it = cb.begin(); it != cb.end(); it++) {
		if (interfacesEqual(it->callback, callback)) {
			cb.erase(it);
			LOG(DEBUG) << "callback unregistered. isFilter: "
				<< it->is_filter_type << " Type: "
				<< android::hardware::thermal::V2_0::toString(it->type);
			removed = true;
			break;
		}
	}
	if (!removed)
		return exit_hal(_hidl_cb, "The callback was not registered before");
	_hidl_cb(status);
	return Void();
}

void Thermal::sendThrottlingChangeCB(const Temperature &t)
{
	std::lock_guard<std::mutex> _lock(thermal_cb_mutex);
	std::vector<CallbackSetting>::iterator it;

	LOG(DEBUG) << "Throttle Severity change: " << " Type: " << (int)t.type
		<< " Name: " << t.name << " Value: " << t.value <<
		" ThrottlingStatus: " << (int)t.throttlingStatus;
	it = cb.begin();
	while (it != cb.end()) {
		if (!it->is_filter_type || it->type == t.type) {
			Return<void> ret = it->callback->notifyThrottling(t);
			if (!ret.isOk()) {
				LOG(ERROR) << "Notify callback execution error. Removing";
				it = cb.erase(it);
				continue;
			}
		}
		it++;
	}
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android
