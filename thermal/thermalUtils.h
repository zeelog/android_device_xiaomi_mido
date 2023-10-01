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

#ifndef THERMAL_THERMAL_UTILS_H__
#define THERMAL_THERMAL_UTILS_H__

#include <unordered_map>
#include <mutex>
#include <android/hardware/thermal/2.0/IThermal.h>
#include "thermalConfig.h"
#include "thermalMonitor.h"
#include "thermalCommon.h"
#include "thermalData.h"

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

using ueventCB = std::function<void(Temperature &t)>;

class ThermalUtils {
	public:
		ThermalUtils(const ueventCB &inp_cb);
		~ThermalUtils() = default;
		bool isSensorInitialized()
		{
			return is_sensor_init;
		};
		bool isCdevInitialized()
		{
			return is_cdev_init;
		};
		int readTemperatures(hidl_vec<Temperature_1_0>& temp);
		int readTemperatures(bool filterType, TemperatureType type,
                                            hidl_vec<Temperature>& temperatures);
		int readTemperatureThreshold(bool filterType, TemperatureType type,
                                            hidl_vec<TemperatureThreshold>& thresh);
		int readCdevStates(bool filterType, cdevType type,
                                            hidl_vec<CoolingDevice>& cdev);
		int fetchCpuUsages(hidl_vec<CpuUsage>& cpu_usages);
	private:
		bool is_sensor_init;
		bool is_cdev_init;
		ThermalConfig cfg;
		ThermalCommon cmnInst;
		ThermalMonitor monitor;
		std::unordered_map<std::string, struct therm_sensor>
			thermalConfig;
		std::vector<struct therm_cdev> cdevList;
		std::mutex sens_cb_mutex;
		ueventCB cb;

		void ueventParse(std::string sensor_name, int temp);
		void Notify(struct therm_sensor& sens);
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android

#endif  // THERMAL_THERMAL_UTILS_H__
