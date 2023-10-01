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

#ifndef THERMAL_THERMAL_COMMON_H__
#define THERMAL_THERMAL_COMMON_H__

#include "thermalData.h"

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

#define RETRY_CT 3

class ThermalCommon {
	public:
		ThermalCommon();
		~ThermalCommon() = default;

		int readFromFile(std::string_view path, std::string& out);
		int initThermalZones(std::vector<struct target_therm_cfg>& cfg);
		void initThreshold(struct therm_sensor& sens);
		int initCdev();

		int read_cdev_state(struct therm_cdev& cdev);
		int read_temperature(struct therm_sensor& sensor);
		int estimateSeverity(struct therm_sensor& sensor);
		int get_cpu_usages(hidl_vec<CpuUsage>& list);

		std::vector<struct therm_sensor> fetch_sensor_list()
		{
			return sens;
		};
		std::vector<struct therm_cdev> fetch_cdev_list()
		{
			return cdev;
		};

	private:
		int ncpus;
		std::vector<struct target_therm_cfg> cfg;
		std::vector<struct therm_sensor> sens;
		std::vector<struct therm_cdev> cdev;

		int initializeCpuSensor(struct target_therm_cfg& cpu_cfg);
		int initialize_sensor(struct target_therm_cfg& cfg,
					int sens_idx);
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android

#endif  // THERMAL_THERMAL_COMMON_H__
