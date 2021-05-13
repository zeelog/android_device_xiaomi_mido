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


#define LOG_TAG "ThermalHAL-845"
#include <utils/Log.h>

#include <hardware/hardware.h>
#include <hardware/thermal.h>
#include "thermal_common.h"

static char *cpu_sensors_845[] =
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

static char *misc_sensors_845[] =
{
    "gpu0-usr",
    "battery",
    "xo-therm-adc"
};

static struct target_therm_cfg sensor_cfg_845[] = {
    {
        .type = DEVICE_TEMPERATURE_CPU,
        .sensor_list = cpu_sensors_845,
        .sens_cnt = ARRAY_SIZE(cpu_sensors_845),
        .mult = 0.001,
        .throt_thresh = 60,
        .shutdwn_thresh = 115,
    },
    {
        .type = DEVICE_TEMPERATURE_GPU,
        .sensor_list = &misc_sensors_845[0],
        .sens_cnt = 1,
        .mult = 0.001,
        .label = "GPU",
    },
    {
        .type = DEVICE_TEMPERATURE_BATTERY,
        .sensor_list = &misc_sensors_845[1],
        .sens_cnt = 1,
        .mult = 0.001,
        .shutdwn_thresh = 60,
        .label = "battery",
    },
    {
        .type = DEVICE_TEMPERATURE_SKIN,
        .sensor_list = &misc_sensors_845[2],
        .sens_cnt = 1,
        .mult = 0.001,
        .throt_thresh = 44,
        .shutdwn_thresh = 70,
        .vr_thresh = 58,
        .label = "skin",
    }
};

ssize_t get_temperatures(thermal_module_t *module, temperature_t *list, size_t size) {
    ALOGD("Entering %s",__func__);
    static int thermal_sens_size;

    if (!thermal_sens_size) {
	thermal_sens_size = thermal_zone_init(sensor_cfg_845,
                                ARRAY_SIZE(sensor_cfg_845));
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
