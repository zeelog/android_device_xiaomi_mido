/*
 * Copyright (c) 2020,2021 The Linux Foundation. All rights reserved.
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
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <unordered_map>
#include <android-base/logging.h>
#include <android/hardware/thermal/2.0/IThermal.h>
#include <utility>

#include "thermalData.h"
#include "thermalConfig.h"

namespace android {
namespace hardware {
namespace thermal {
namespace V2_0 {
namespace implementation {
	constexpr std::string_view socIDPath("/sys/devices/soc0/soc_id");
	constexpr std::string_view hwPlatformPath("/sys/devices/soc0/hw_platform");

	std::vector<std::string> cpu_sensors_439 =
	{
		"apc1-cpu0-usr",
		"apc1-cpu1-usr",
		"apc1-cpu2-usr",
		"apc1-cpu3-usr",
		"cpuss0-usr",
		"cpuss0-usr",
		"cpuss0-usr",
		"cpuss0-usr",
	};

	std::vector<struct target_therm_cfg> sensor_cfg_sdm632 =
	{
		{
			TemperatureType::CPU,
			cpu_sensors_439,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpu0-usr" },
			"GPU",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "xo-therm-adc" },
			"skin",
			40000,
			95000,
			40000,
			true,
		},
		{
			TemperatureType::BCL_VOLTAGE,
			{ "pmi-vbat-lvl0" },
			"vbat",
			3200,
			3000,
			3200,
			false,
		},
		{
			TemperatureType::BCL_CURRENT,
			{ "pmi-ibat-lvl0" },
			"ibat",
			4200,
			4400,
			4200,
			true,
		},
		{
			TemperatureType::BCL_PERCENTAGE,
			{ "soc" },
			"soc",
			10,
			2,
			10,
			false,
		},
	};

	std::vector<struct target_therm_cfg> sensor_cfg_439 =
	{
		{
			TemperatureType::CPU,
			cpu_sensors_439,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpu-usr" },
			"GPU",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "quiet-therm-adc" },
			"skin",
			40000,
			95000,
			40000,
			true,
		},
		{
			TemperatureType::BCL_VOLTAGE,
			{ "vbat_adc" },
			"vbat",
			3200,
			3000,
			3200,
			false,
		},
		{
			TemperatureType::BCL_CURRENT,
			{ "ibat-high" },
			"ibat",
			4200,
			4400,
			4200,
			true,
		},
		{
			TemperatureType::BCL_PERCENTAGE,
			{ "soc" },
			"soc",
			10,
			2,
			10,
			false,
		},
	};

	std::vector<std::string> cpu_sensors_8953 =
	{
		"apc1-cpu0-usr",
		"apc1-cpu1-usr",
		"apc1-cpu2-usr",
		"apc1-cpu3-usr",
		"apc0-cpu0-usr",
		"apc0-cpu1-usr",
		"apc0-cpu2-usr",
		"apc0-cpu3-usr",
	};

	std::vector<struct target_therm_cfg> sensor_cfg_sdm625 =
	{
		{
			TemperatureType::CPU,
			cpu_sensors_8953,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpu0-usr", "gpu1-usr" },
			"GPU",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "xo-therm-adc" },
			"skin",
			55000,
			95000,
			55000,
			true,
		},
	};

	std::vector<std::string> cpu_sensors_sdm845 =
	{
		"cpu0-silver-usr",
		"cpu1-silver-usr",
		"cpu2-silver-usr",
		"cpu3-silver-usr",
		"cpu0-gold-usr",
		"cpu1-gold-usr",
		"cpu2-gold-usr",
		"cpu3-gold-usr",
	};

	std::vector<struct target_therm_cfg> sensor_cfg_sdm845 = {
		{
			TemperatureType::CPU,
			cpu_sensors_sdm845,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{"gpu0-usr"},
			"gpu0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{"gpu1-usr"},
			"gpu1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::SKIN,
			{"xo-therm-adc"},
			"skin",
			55000,
			95000,
			55000,
			true,
		},
		{
			TemperatureType::BCL_PERCENTAGE,
			{"soc"},
			"soc",
			10,
			2,
			10,
			false,
		},
	};

	std::vector<std::string> cpu_sensors_bengal =
	{
		"cpuss-2-usr",
		"cpuss-2-usr",
		"cpuss-2-usr",
		"cpuss-2-usr",
		"cpu-1-0-usr",
		"cpu-1-1-usr",
		"cpu-1-2-usr",
		"cpu-1-3-usr",
	};

	std::vector<struct target_therm_cfg> sensor_cfg_bengal =
	{
		{
			TemperatureType::CPU,
			cpu_sensors_bengal,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpu-usr" },
			"GPU",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "xo-therm-usr" },
			"skin",
			40000,
			95000,
			40000,
			true,
		},
		{
			TemperatureType::BCL_VOLTAGE,
			{ "pmi632-vbat-lvl0" },
			"vbat",
			3000,
			2800,
			3000,
			false,
		},
		{
			TemperatureType::BCL_CURRENT,
			{ "pmi632-ibat-lvl0" },
			"ibat",
			4000,
			4200,
			4000,
			true,
		},
		{
			TemperatureType::BCL_PERCENTAGE,
			{ "soc" },
			"soc",
			10,
			2,
			10,
			false,
		},
	};

	std::vector<std::string> cpu_sensors_khaje =
	{
		"cpuss-2",
		"cpuss-2",
		"cpuss-2",
		"cpuss-2",
		"cpu-1-0",
		"cpu-1-1",
		"cpu-1-2",
		"cpu-1-3",
	};

	std::vector<struct target_therm_cfg> sensor_cfg_khaje =
	{
		{
			TemperatureType::CPU,
			cpu_sensors_khaje,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpu" },
			"GPU",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "xo-therm" },
			"skin",
			40000,
			95000,
			40000,
			true,
		},
		{
			TemperatureType::BCL_VOLTAGE,
			{ "vbat" },
			"vbat",
			3000,
			2800,
			3000,
			false,
		},
		{
			TemperatureType::BCL_CURRENT,
			{ "pm7250b-ibat-lvl0" },
			"ibat",
			5500,
			6000,
			5500,
			true,
		},
		{
			TemperatureType::BCL_PERCENTAGE,
			{ "socd" },
			"socd",
			90,
			99,
			90,
			true,
		},
	};

	std::vector<std::string> cpu_sensors_trinket =
	{
		"cpuss-0-usr",
		"cpuss-0-usr",
		"cpuss-0-usr",
		"cpuss-0-usr",
		"cpu-1-0-usr",
		"cpu-1-1-usr",
		"cpu-1-2-usr",
		"cpu-1-3-usr",
	};

	std::vector<struct target_therm_cfg> sensor_cfg_trinket =
	{
		{
			TemperatureType::CPU,
			cpu_sensors_trinket,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpu-usr" },
			"GPU",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "xo-therm-adc" },
			"skin",
			40000,
			95000,
			40000,
			true,
		},
		{
			TemperatureType::BCL_VOLTAGE,
			{ "pmi632-vbat-lvl0" },
			"vbat",
			3000,
			2800,
			3000,
			false,
		},
		{
			TemperatureType::BCL_CURRENT,
			{ "pmi632-ibat-lvl0" },
			"ibat",
			4000,
			4200,
			4000,
			true,
		},
		{
			TemperatureType::BCL_PERCENTAGE,
			{ "soc" },
			"soc",
			10,
			2,
			10,
			false,
		},
	};

	std::vector<std::string> cpu_sensors_monaco =
	{
		"cpuss-0",
		"cpuss-1",
		"cpuss-0",
		"cpuss-1",
	};

	std::vector<struct target_therm_cfg> sensor_cfg_monaco =
	{
		{
			TemperatureType::CPU,
			cpu_sensors_monaco,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpu" },
			"gpu",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "pa-therm0" },
			"skin",
			40000,
			95000,
			40000,
			true,
		},
		{
			TemperatureType::BCL_CURRENT,
			{ "pm5100-ibat-lvl0" },
			"ibat",
			1100,
			1500,
			1100,
			true,
		},
	};

	std::vector<std::string> cpu_sensors_lito =
	{
		"cpu-0-0-usr",
		"cpu-0-1-usr",
		"cpu-0-2-usr",
		"cpu-0-3-usr",
		"cpu-0-4-usr",
		"cpu-0-5-usr",
		"cpu-1-0-usr",
		"cpu-1-2-usr",
	};

	std::vector<struct target_therm_cfg> sensor_cfg_lito =
	{
		{
			TemperatureType::CPU,
			cpu_sensors_lito,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-0-usr" },
			"GPU",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "xo-therm-usr" },
			"skin",
			55000,
			95000,
			55000,
			true,
		},
		{
			TemperatureType::BCL_CURRENT,
			{ "pm7250b-ibat-lvl0" },
			"ibat",
			4500,
			5000,
			4500,
			true,
		},
		{
			TemperatureType::BCL_VOLTAGE,
			{ "pm7250b-vbat-lvl0" },
			"vbat",
			3200,
			3000,
			3200,
			false,
		},
		{
			TemperatureType::BCL_PERCENTAGE,
			{ "soc" },
			"soc",
			10,
			2,
			10,
			false,
		},
	};

	std::vector<struct target_therm_cfg> sensor_cfg_sdmmagpie =
	{
		{
			TemperatureType::CPU,
			cpu_sensors_lito,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-0-usr" },
			"GPU",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "xo-therm-adc" },
			"skin",
			40000,
			95000,
			40000,
			true,
		},
		{
			TemperatureType::BCL_VOLTAGE,
			{ "pm6150-vbat-lvl0" },
			"vbat",
			3000,
			2800,
			3000,
			false,
		},
		{
			TemperatureType::BCL_CURRENT,
			{ "pm6150-ibat-lvl0" },
			"ibat",
			5500,
			6000,
			5500,
			true,
		},
		{
			TemperatureType::BCL_PERCENTAGE,
			{ "soc" },
			"soc",
			10,
			2,
			10,
			false,
		},
	};

	std::vector<struct target_therm_cfg> sensor_cfg_holi =
	{
		{
			TemperatureType::CPU,
			cpu_sensors_lito,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-0-usr" },
			"gpu0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-1-usr" },
			"gpu1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "quiet-therm-usr" },
			"skin",
			40000,
			95000,
			40000,
			true,
		},
		{
			TemperatureType::BCL_CURRENT,
			{ "pm7250b-ibat-lvl0" },
			"ibat",
			5500,
			6000,
			5500,
			true,
		},
	};

	std::vector<std::string> cpu_sensors_kona =
	{
		"cpu-0-0",
		"cpu-0-1",
		"cpu-0-2",
		"cpu-0-3",
		"cpu-1-0",
		"cpu-1-1",
		"cpu-1-2",
		"cpu-1-3",
	};

	std::vector<struct target_therm_cfg>  sensor_cfg_msmnile = {
		{
			TemperatureType::CPU,
			cpu_sensors_kona,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-0-usr" },
			"gpu0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-1-usr" },
			"gpu1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "xo-therm" },
			"skin",
			40000,
			95000,
			40000,
			true,
		},
		{
			TemperatureType::BCL_CURRENT,
			{ "pm8150b-ibat-lvl0" },
			"ibat",
			4500,
			5000,
			4500,
			true,
		},
		{
			TemperatureType::BCL_VOLTAGE,
			{ "pm8150b-vbat-lvl0" },
			"vbat",
			3200,
			3000,
			3200,
			false,
		},
		{
			TemperatureType::BCL_PERCENTAGE,
			{ "soc" },
			"soc",
			10,
			2,
			10,
			false,
		},
	};

	std::vector<struct target_therm_cfg>  kona_common = {
		{
			TemperatureType::CPU,
			cpu_sensors_kona,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-0" },
			"GPU0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-1" },
			"GPU1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "skin-msm-therm" },
			"skin",
			40000,
			95000,
			40000,
			true,
		}
	};

	std::vector<struct target_therm_cfg>  kona_specific = {
		{
			TemperatureType::BCL_CURRENT,
			{ "pm8150b-ibat-lvl0" },
			"ibat",
			4500,
			5000,
			4500,
			true,
		},
		{
			TemperatureType::BCL_VOLTAGE,
			{ "vbat" },
			"vbat",
			3200,
			3000,
			3200,
			false,
		},
		{
			TemperatureType::BCL_PERCENTAGE,
			{ "socd" },
			"socd",
			90,
			99,
			90,
			false,
		},
		{
			TemperatureType::NPU,
			{ "npu" },
			"npu",
			95000,
			115000,
			95000,
			true,
		},
	};

	std::vector<std::string> cpu_sensors_lahaina =
	{
		"cpu-0-0-usr",
		"cpu-0-1-usr",
		"cpu-0-2-usr",
		"cpu-0-3-usr",
		"cpu-1-0-usr",
		"cpu-1-2-usr",
		"cpu-1-4-usr",
		"cpu-1-6-usr",
	};

	std::vector<struct target_therm_cfg>  lahaina_common = {
		{
			TemperatureType::CPU,
			cpu_sensors_lahaina,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-0-usr" },
			"GPU0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-1-usr" },
			"GPU1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::BCL_CURRENT,
			{ "pm8350b-ibat-lvl0" },
			"ibat",
			6000,
			7500,
			6000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-0-usr" },
			"nsp0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-1-usr" },
			"nsp1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-2-usr" },
			"nsp2",
			95000,
			115000,
			95000,
			true,
		},
	};

	std::vector<struct target_therm_cfg>  lahaina_specific = {
		{
			TemperatureType::SKIN,
			{ "xo-therm-usr" },
			"skin",
			55000,
			95000,
			55000,
			true,
		},
	};

	std::vector<struct target_therm_cfg>  shima_specific = {
		{
			TemperatureType::SKIN,
			{ "quiet-therm-usr" },
			"skin",
			40000,
			95000,
			40000,
			true,
		},
	};

	std::vector<struct target_therm_cfg>  sensor_cfg_yupik = {
		{
			TemperatureType::CPU,
			cpu_sensors_lahaina,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-0-usr" },
			"GPU0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-1-usr" },
			"GPU1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-0-usr" },
			"nsp0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-1-usr" },
			"nsp1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "quiet-therm-usr" },
			"skin",
			40000,
			95000,
			40000,
			true,
		},
	};

	std::vector<std::string> cpu_sensors_waipio =
	{
		"cpu-0-0",
		"cpu-0-1",
		"cpu-0-2",
		"cpu-0-3",
		"cpu-1-0",
		"cpu-1-2",
		"cpu-1-4",
		"cpu-1-6",
	};

	std::vector<struct target_therm_cfg>  waipio_common = {
		{
			TemperatureType::CPU,
			cpu_sensors_waipio,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-0" },
			"GPU0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-1" },
			"GPU1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-0" },
			"nsp0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-1" },
			"nsp1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-2" },
			"nsp2",
			95000,
			115000,
			95000,
			true,
		},
	};

	std::vector<struct target_therm_cfg>  diwali_common = {
		{
			TemperatureType::CPU,
			cpu_sensors_waipio,
			"",
			95000,
			115000,
			95000,
			true,
			ThrottlingSeverity::LIGHT,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-0" },
			"GPU0",
			95000,
			115000,
			95000,
			true,
			ThrottlingSeverity::LIGHT,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-1" },
			"GPU1",
			95000,
			115000,
			95000,
			true,
			ThrottlingSeverity::LIGHT,
		},
		{
			TemperatureType::NPU,
			{ "nspss-0" },
			"nsp0",
			95000,
			115000,
			95000,
			true,
			ThrottlingSeverity::LIGHT,
		},
		{
			TemperatureType::NPU,
			{ "nspss-1" },
			"nsp1",
			95000,
			115000,
			95000,
			true,
			ThrottlingSeverity::LIGHT,
		},
		{
			TemperatureType::NPU,
			{ "nspss-2" },
			"nsp2",
			95000,
			115000,
			95000,
			true,
			ThrottlingSeverity::LIGHT,
		},
	};

	std::vector<struct target_therm_cfg>  waipio_specific = {
		{
			TemperatureType::BCL_CURRENT,
			{ "pm8350b-ibat-lvl0" },
			"ibat",
			6000,
			7500,
			6000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "xo-therm" },
			"skin",
			55000,
			95000,
			55000,
			true,
		},
	};

	std::vector<std::string> cpu_sensors_kalama =
	{
		"cpu-0-0",
		"cpu-0-1",
		"cpu-0-2",
		"cpu-1-0",
		"cpu-1-2",
		"cpu-1-4",
		"cpu-1-6",
		"cpu-1-8",
	};

	std::vector<struct target_therm_cfg>  kalama_common = {
		{
			TemperatureType::CPU,
			cpu_sensors_kalama,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-0" },
			"GPU0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-1" },
			"GPU1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-2" },
			"GPU2",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-3" },
			"GPU3",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-4" },
			"GPU4",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-5" },
			"GPU5",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-6" },
			"GPU6",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-7" },
			"GPU7",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-0" },
			"nsp0",
			100000,
			115000,
			100000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-1" },
			"nsp1",
			100000,
			115000,
			100000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-2" },
			"nsp2",
			100000,
			115000,
			100000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-3" },
			"nsp3",
			100000,
			115000,
			100000,
			true,
		},
	};

	std::vector<struct target_therm_cfg>  kalama_specific = {
		{
			TemperatureType::BCL_CURRENT,
			{ "pm8550b-ibat-lvl0" },
			"ibat",
			9000,
			10000,
			9000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "skin-msm-therm" },
			"skin",
			46500,
			95000,
			46500,
			true,
		},
	};

	std::vector<struct target_therm_cfg>  diwali_specific = {
		{
			TemperatureType::BCL_CURRENT,
			{ "pm7250b-ibat-lvl0" },
			"ibat",
			9000,
			9500,
			9000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "quiet-therm" },
			"skin",
			46000,
			95000,
			46000,
			true,
			ThrottlingSeverity::LIGHT,
		},
	};

	std::vector<std::string> cpu_sensors_neo =
	{
		"cpu-0-0",
		"cpu-0-1",
		"cpu-0-2",
		"cpu-0-3",
	};

	std::vector<struct target_therm_cfg>  neo_common = {
		{
			TemperatureType::CPU,
			cpu_sensors_neo,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-0" },
			"GPU0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-1" },
			"GPU1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-0" },
			"nsp0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-1" },
			"nsp1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-2" },
			"nsp2",
			95000,
			115000,
			95000,
			true,
		},
	};

	std::vector<std::string> cpu_sensors_parrot =
	{
		"cpu-0-0",
		"cpu-0-1",
		"cpu-0-2",
		"cpu-0-3",
		"cpu-1-0",
		"cpu-1-2",
		"cpu-1-4",
		"cpu-1-6",
	};

	std::vector<struct target_therm_cfg>  parrot_common = {
		{
			TemperatureType::CPU,
			cpu_sensors_parrot,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-0" },
			"GPU0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-1" },
			"GPU1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-0" },
			"nsp0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-1" },
			"nsp1",
			95000,
			115000,
			95000,
			true,
		},
	};

	std::vector<struct target_therm_cfg>  parrot_specific = {
		{
			TemperatureType::BCL_CURRENT,
			{ "pm7250b-ibat-lvl0" },
			"ibat",
			6000,
			7500,
			6000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "xo-therm" },
			"skin",
			55000,
			95000,
			55000,
			true,
		},
	};

	std::vector<std::string> cpu_sensors_anorak =
	{
		"cpu-0-0-0",
		"cpu-0-0-1",
		"cpu-0-1-0",
		"cpu-0-1-1",
		"cpu-1-0-0",
		"cpu-1-0-1",
		"cpu-1-1-0",
		"cpu-1-1-1",
		"cpu-1-2-0",
		"cpu-1-2-1",
		"cpu-1-3-0",
		"cpu-1-3-1",
	};

	std::vector<struct target_therm_cfg>  anorak_common = {
		{
			TemperatureType::CPU,
			cpu_sensors_anorak,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-0" },
			"nsp0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-1" },
			"nsp1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::NPU,
			{ "nspss-2" },
			"nsp2",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-0" },
			"GPU0",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-1" },
			"GPU1",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-2" },
			"GPU2",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-3" },
			"GPU3",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-4" },
			"GPU4",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-5" },
			"GPU5",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-6" },
			"GPU6",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss-7" },
			"GPU7",
			95000,
			115000,
			95000,
			true,
		},
	};
	std::vector<struct target_therm_cfg>  anorak_specific = {
		{
			TemperatureType::BCL_CURRENT,
			{ "pm8550b-ibat-lvl0" },
			"ibat",
			9000,
			10000,
			9000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "sys-therm-0" },
			"skin",
			55000,
			95000,
			55000,
			true,
		},
	};

	std::vector<std::string> cpu_sensors_ravelin =
	{
		"cpu-0-0",
		"cpu-0-1",
		"cpu-0-2",
		"cpu-0-3",
		"cpu-0-4",
		"cpu-0-5",
		"cpu-1-0",
		"cpu-1-2",
	};

	std::vector<struct target_therm_cfg>  ravelin_common = {
		{
			TemperatureType::CPU,
			cpu_sensors_ravelin,
			"",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::GPU,
			{ "gpuss" },
			"GPU",
			95000,
			115000,
			95000,
			true,
		},
		{
			TemperatureType::SKIN,
			{ "sys-therm-1" },
			"skin",
			55000,
			95000,
			55000,
			true,
		},
	};

	std::vector<struct target_therm_cfg>  ravelin_specific_qrd = {
		{
			TemperatureType::BCL_CURRENT,
			{ "pmi632-ibat-lvl0" },
			"ibat",
			6000,
			7500,
			6000,
			true,
		},
	};

	std::vector<struct target_therm_cfg>  ravelin_specific_idp = {
		{
			TemperatureType::BCL_CURRENT,
			{ "pm7250b-ibat-lvl0" },
			"ibat",
			6000,
			7500,
			6000,
			true,
		},
	};

	struct target_therm_cfg bat_conf = {
		TemperatureType::BATTERY,
		{ "battery" },
		"battery",
		80000,
		90000,
		80000,
		true,
	};

	std::vector<struct target_therm_cfg> bcl_conf = {
		{
			TemperatureType::BCL_VOLTAGE,
			{ "vbat" },
			"vbat",
			3200,
			3000,
			3200,
			false,
		},
		{
			TemperatureType::BCL_PERCENTAGE,
			{ "socd" },
			"socd",
			90,
			99,
			90,
			true,
		},
	};

	const std::unordered_map<int, std::vector<struct target_therm_cfg>>
		msm_soc_map = {
		{353, sensor_cfg_439},
		{354, sensor_cfg_439},
		{363, sensor_cfg_439},
		{364, sensor_cfg_439},
		{416, sensor_cfg_439},
		{437, sensor_cfg_439},
		{293, sensor_cfg_sdm625},
		{349, sensor_cfg_sdm632},
		{321, sensor_cfg_sdm845},
		{341, sensor_cfg_sdm845},
		{417, sensor_cfg_bengal}, // bengal
		{420, sensor_cfg_bengal},
		{444, sensor_cfg_bengal},
		{445, sensor_cfg_bengal},
		{469, sensor_cfg_bengal},
		{470, sensor_cfg_bengal},
		{518, sensor_cfg_khaje},  //khaje
		{561, sensor_cfg_khaje},  //khajeP
		{562, sensor_cfg_khaje},  //khajeQ
		{584, sensor_cfg_khaje},  //khajeXR
		{585, sensor_cfg_khaje},  //khajeG
		{586, sensor_cfg_khaje},  //khajeIOT
		{394, sensor_cfg_trinket},
		{467, sensor_cfg_trinket},
		{468, sensor_cfg_trinket},
		{400, sensor_cfg_lito}, // lito
		{440, sensor_cfg_lito},
		{407, sensor_cfg_lito}, // atoll
		{365, sensor_cfg_sdmmagpie},
		{366, sensor_cfg_sdmmagpie},
		{434, sensor_cfg_lito}, // lagoon
		{435, sensor_cfg_lito},
		{459, sensor_cfg_lito},
		{476, sensor_cfg_lito}, // orchid
		{339, sensor_cfg_msmnile},
		{361, sensor_cfg_msmnile},
		{362, sensor_cfg_msmnile},
		{367, sensor_cfg_msmnile},
		{356, kona_common}, // kona
		{415, lahaina_common}, // lahaina
		{439, lahaina_common}, // lahainap
		{456, lahaina_common}, // lahaina-atp
		{501, lahaina_common},
		{502, lahaina_common},
		{450, lahaina_common}, // shima
		{454, sensor_cfg_holi}, // holi
		{507, sensor_cfg_holi}, // blair
		{475, sensor_cfg_yupik}, // yupik
		{515, sensor_cfg_yupik}, // YUPIK-LTE
		{486, sensor_cfg_monaco}, // monaco
		{457, waipio_common}, //Waipio
		{482, waipio_common}, //Waipio
		{552, waipio_common}, //Waipio-LTE
		{506, diwali_common}, //diwali
		{547, diwali_common}, //diwali
		{564, diwali_common}, //diwali-LTE
		{530, waipio_common}, // cape
		{531, waipio_common}, // cape
		{540, waipio_common}, // cape
		{525, neo_common},
		{554, neo_common},
		{537, parrot_common}, //Netrani mobile
		{583, parrot_common}, //Netrani mobile without modem
		{613, parrot_common}, //Netrani APQ
		{631, parrot_common},
		{549, anorak_common},
		{568, ravelin_common}, //Clarence Mobile
		{581, ravelin_common}, //Clarence IOT
		{582, ravelin_common}, //Clarence IOT without modem
		{591, waipio_common}, //ukee
		{519, kalama_common}, //Kalama
		{536, kalama_common}, //Kalamap
		{600, kalama_common}, //Kalama_sg
		{601, kalama_common}, //Kalamap_sg
		{603, kalama_common}, //Kalama_qcs
		{604, kalama_common}, //Kalama_qcm
		{486, sensor_cfg_monaco}, // monaco
		{517, sensor_cfg_monaco}, // monaco
	};

	const std::unordered_map<int, std::vector<struct target_therm_cfg>>
		msm_soc_specific = {
		{356, kona_specific}, // kona
		{415, lahaina_specific}, // lahaina
		{439, lahaina_specific}, // lahainap
		{456, lahaina_specific}, // lahaina-atp
		{501, lahaina_specific},
		{502, lahaina_specific},
		{450, shima_specific}, // shima
		{457, waipio_specific}, //Waipio
		{482, waipio_specific}, //Waipio
		{552, waipio_specific}, //Waipio-LTE
		{506, diwali_specific}, //diwali
		{547, diwali_specific}, //diwali
		{564, diwali_specific}, //diwali-LTE
		{530, waipio_specific}, // cape
		{531, waipio_specific}, // cape
		{540, waipio_specific}, // cape
		{537, parrot_specific}, //Netrani mobile
		{583, parrot_specific}, //Netrani mobile without modem
		{613, parrot_specific}, //Netrani APQ
		{631, parrot_specific},
		{549, anorak_specific},
		{591, waipio_specific}, //ukee
		{519, kalama_specific}, //Kalama
		{536, kalama_specific}, //Kalamap
		{600, kalama_specific}, //Kalama_sg
		{601, kalama_specific}, //Kalamap_sg
		{603, kalama_specific}, //Kalama_qcs
		{604, kalama_specific}, //Kalama_qcm
	};

	const std::unordered_multimap<int, std::pair<std::string,
				std::vector<struct target_therm_cfg>>>
		msm_platform_specific = {
		{568, std::make_pair("QRD", ravelin_specific_qrd)},
		{568, std::make_pair("IDP", ravelin_specific_idp)},
	};

	std::vector<struct target_therm_cfg> add_target_config(
			int socID, std::string hwPlatform,
			std::vector<struct target_therm_cfg> conf)
	{
		std::vector<struct target_therm_cfg> targetConf;

		if (msm_soc_specific.find(socID) != msm_soc_specific.end()) {
			targetConf = (msm_soc_specific.find(socID))->second;
			conf.insert(conf.end(), targetConf.begin(),
					targetConf.end());
		}

		auto range = msm_platform_specific.equal_range(socID);
		auto it = range.first;
		for (; it != range.second; ++it) {
			if (it->second.first != hwPlatform)
				continue;

			targetConf = it->second.second;
			conf.insert(conf.end(), targetConf.begin(),
					targetConf.end());
			break;
		}

		return conf;
	}

	ThermalConfig::ThermalConfig():cmnInst()
	{
		std::unordered_map<int, std::vector<struct target_therm_cfg>>::const_iterator it;
		std::vector<struct target_therm_cfg>::iterator it_vec;
		bool bcl_defined = false;
		std::string soc_val;
		int ct = 0;
		bool read_ok = false;

		soc_id = 0;
		do {
			if (cmnInst.readFromFile(socIDPath, soc_val) <= 0) {
				LOG(ERROR) <<"soc ID fetch error";
				return;
			}

			if (cmnInst.readFromFile(hwPlatformPath, hw_platform) <= 0) {
				LOG(ERROR) <<"hw Platform fetch error";
				continue;
			}

			try {
				soc_id = std::stoi(soc_val, nullptr, 0);
				read_ok = true;
			}
			catch (std::exception &err) {
				LOG(ERROR) <<"soc id stoi err:" << err.what()
					<< " buf:" << soc_val;
			}
		} while (ct++ && !read_ok && ct < RETRY_CT);
		if (soc_id <= 0) {
			LOG(ERROR) << "Invalid soc ID: " << soc_id;
			return;
		}
		it = msm_soc_map.find(soc_id);
		if (it == msm_soc_map.end()) {
			LOG(ERROR) << "No config for soc ID: " << soc_id;
			return;
		}
		thermalConfig = add_target_config(soc_id, hw_platform, it->second);
		for (it_vec = thermalConfig.begin();
				it_vec != thermalConfig.end(); it_vec++) {
			if (it_vec->type == TemperatureType::BCL_PERCENTAGE)
				bcl_defined = true;
		}

		thermalConfig.push_back(bat_conf);
		if (!bcl_defined)
			thermalConfig.insert(thermalConfig.end(),
				bcl_conf.begin(), bcl_conf.end());
		LOG(DEBUG) << "Total sensors:" << thermalConfig.size();
	}
}  // namespace implementation
}  // namespace V2_0
}  // namespace thermal
}  // namespace hardware
}  // namespace android

