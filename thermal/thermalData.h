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

#ifndef THERMAL_THERMAL_DATA_H__
#define THERMAL_THERMAL_DATA_H__

#include <vector>
#include <string>
#include <mutex>
#include <cmath>

#include <android/hardware/thermal/2.0/IThermal.h>

#define UNKNOWN_TEMPERATURE (NAN)

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {

using ::android::hardware::hidl_vec;
using ::android::hardware::thermal::V1_0::CpuUsage;
using CoolingDevice_1_0 = ::android::hardware::thermal::V1_0::CoolingDevice;
using Temperature_1_0 = ::android::hardware::thermal::V1_0::Temperature;
using TemperatureType_1_0 = ::android::hardware::thermal::V1_0::TemperatureType;
using ::android::hardware::thermal::V1_0::ThermalStatus;
using ::android::hardware::thermal::V1_0::ThermalStatusCode;

using cdevType = ::android::hardware::thermal::V2_0::CoolingType;
using CoolingDevice = ::android::hardware::thermal::V2_0::CoolingDevice;
using Temperature = ::android::hardware::thermal::V2_0::Temperature;
using TemperatureType = ::android::hardware::thermal::V2_0::TemperatureType;
using TemperatureThreshold =
	::android::hardware::thermal::V2_0::TemperatureThreshold;
using ::android::hardware::thermal::V2_0::ThrottlingSeverity;

	struct target_therm_cfg {
		TemperatureType type;
		std::vector<std::string> sensor_list;
		std::string label;
		int throt_thresh;
		int shutdwn_thresh;
		int vr_thresh;
		bool positive_thresh_ramp;
		ThrottlingSeverity throt_severity = ThrottlingSeverity::SEVERE;
	};

	struct therm_sensor {
		int tzn;
		int mulFactor;
		bool positiveThresh;
		std::string sensor_name;
		ThrottlingSeverity lastThrottleStatus;
		Temperature t;
		TemperatureThreshold thresh;
		ThrottlingSeverity throt_severity;
	};

	struct therm_cdev {
		int cdevn;
		CoolingDevice c;
	};

}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android

#endif  // THERMAL_THERMAL_DATA_H__
