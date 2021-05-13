/*
 * Copyright (c) 2017-2018 The Linux Foundation. All rights reserved.
 * Not a contribution
 * Copyright (C) 2016 The Android Open Source Project
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


#define LOG_TAG "ThermalHAL-8953"
#include <utils/Log.h>

#include <hardware/hardware.h>
#include <hardware/thermal.h>
#include "thermal_common.h"

static char *cpu_sensors_8953[] =
{
    "tsens_tz_sensor9",
    "tsens_tz_sensor10",
    "tsens_tz_sensor11",
    "tsens_tz_sensor12",
    "tsens_tz_sensor4",
    "tsens_tz_sensor5",
    "tsens_tz_sensor6",
    "tsens_tz_sensor7",
};

static char *misc_sensors_8953[] =
{
    "tsens_tz_sensor15",
    "battery",
    "xo_therm"
};

static struct target_therm_cfg sensor_cfg_8953[] = {
    {
        .type = DEVICE_TEMPERATURE_CPU,
        .sensor_list = cpu_sensors_8953,
        .sens_cnt = ARRAY_SIZE(cpu_sensors_8953),
        .mult = 0.1,
    },
    {
        .type = DEVICE_TEMPERATURE_GPU,
        .sensor_list = &misc_sensors_8953[0],
        .sens_cnt = 1,
        .mult = 0.1,
        .label = "GPU",
    },
    {
        .type = DEVICE_TEMPERATURE_BATTERY,
        .sensor_list = &misc_sensors_8953[1],
        .sens_cnt = 1,
        .mult = 0.001,
        .label = "battery",
    },
    {
        .type = DEVICE_TEMPERATURE_SKIN,
        .sensor_list = &misc_sensors_8953[2],
        .sens_cnt = 1,
        .mult = 1,
        .label = "skin",
    }
};

ssize_t get_temperatures(thermal_module_t *module, temperature_t *list, size_t size) {
    ALOGD("Entering %s",__func__);
    static int thermal_sens_size;

    if (!thermal_sens_size) {
        thermal_sens_size = thermal_zone_init(sensor_cfg_8953,
                                ARRAY_SIZE(sensor_cfg_8953));
        if (thermal_sens_size <= 0) {
            ALOGE("thermal sensor initialization is failed\n");
            thermal_sens_size = 0;
            return 0;
        }
    }

    if (list == NULL)
        return thermal_sens_size;

    return get_temperature_for_all(list, size);
}
