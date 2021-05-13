/*
 * Copyright (c) 2017, The Linux Foundation. All rights reserved.
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

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "ThermalHAL"
#include <utils/Log.h>

#include <hardware/hardware.h>
#include <hardware/thermal.h>

#define MAX_LENGTH                    50

#define CPU_USAGE_FILE                "/proc/stat"
#define CPU_ONLINE_FILE_FORMAT        "/sys/devices/system/cpu/cpu%d/online"
#define CPU_PRESENT_FILE              "/sys/devices/system/cpu/present"

const char * __attribute__ ((weak)) get_cpu_label(unsigned int cpu_num) {
    ALOGD("Entering %s",__func__);
    static const char * cpu_label = "cpu";
    return cpu_label;
}

size_t __attribute__ ((weak)) get_num_cpus() {
    ALOGD("Entering %s",__func__);
    FILE *file;
    char *line = NULL;
    size_t len = 0;
    static size_t cpus = 0;
    ssize_t read;

    if(cpus) return cpus;

    file = fopen(CPU_PRESENT_FILE, "r");
    if (file == NULL) {
        ALOGE("%s: failed to open: %s", __func__, strerror(errno));
        return 0;
    }

    if ((read = getline(&line, &len, file)) != -1) {
        if (strnlen(line, read) < 3 || strncmp(line, "0-", 2) != 0 || !isdigit(line[2]))
            ALOGE("%s: Incorrect cpu present file format", __func__);
        else
            cpus = atoi(&line[2]) + 1;

        free(line);
    }
    else
        ALOGE("%s: failed to read cpu present file: %s", __func__, strerror(errno));

    fclose(file);
    return cpus;
}

ssize_t __attribute__ ((weak)) get_temperatures(thermal_module_t *module, temperature_t *list, size_t size) {
    ALOGD("Entering %s",__func__);
    return 0;
}

static ssize_t get_cpu_usages(thermal_module_t *module, cpu_usage_t *list) {
    ALOGD("Entering %s",__func__);
    int vals, cpu_num, online;
    ssize_t read;
    uint64_t user, nice, system, idle, active, total;
    char *line = NULL;
    size_t len = 0;
    size_t size = 0;
    size_t cpus = 0;
    char file_name[MAX_LENGTH];
    FILE *file;
    FILE *cpu_file;

    cpus = get_num_cpus();
    if (!cpus)
        return errno ? -errno : -EIO;

    if (list == NULL)
        return cpus;

    file = fopen(CPU_USAGE_FILE, "r");
    if (file == NULL) {
        ALOGE("%s: failed to open: %s", __func__, strerror(errno));
        return -errno;
    }

    while ((read = getline(&line, &len, file)) != -1) {
        if (strnlen(line, read) < 4 || strncmp(line, "cpu", 3) != 0 || !isdigit(line[3])) {
            free(line);
            line = NULL;
            len = 0;
            continue;
        }

        vals = sscanf(line, "cpu%d %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64, &cpu_num, &user,
                &nice, &system, &idle);

        free(line);
        line = NULL;
        len = 0;

        if (vals != 5 || size == cpus) {
            if (vals != 5) {
                ALOGE("%s: failed to read CPU information from file: %s", __func__,
                        strerror(errno));
            } else {
                ALOGE("/proc/stat file has incorrect format.");
            }
            fclose(file);
            return errno ? -errno : -EIO;
        }

        active = user + nice + system;
        total = active + idle;

        // Read online CPU information.
        snprintf(file_name, MAX_LENGTH, CPU_ONLINE_FILE_FORMAT, cpu_num);
        cpu_file = fopen(file_name, "r");
        online = 0;
        if (cpu_file == NULL) {
            ALOGE("%s: failed to open file: %s (%s)", __func__, file_name, strerror(errno));
            fclose(file);
            return -errno;
        }
        if (1 != fscanf(cpu_file, "%d", &online)) {
            ALOGE("%s: failed to read CPU online information from file: %s (%s)", __func__,
                    file_name, strerror(errno));
            fclose(file);
            fclose(cpu_file);
            return errno ? -errno : -EIO;
        }
        fclose(cpu_file);

        list[size] = (cpu_usage_t) {
            .name = get_cpu_label(size),
            .active = active,
            .total = total,
            .is_online = online
        };

        size++;
    }
    fclose(file);

    if (size != cpus) {
        ALOGE("/proc/stat file has incorrect format.");
        return -EIO;
    }

    return cpus;
}

static struct hw_module_methods_t thermal_module_methods = {
    .open = NULL,
};

thermal_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = THERMAL_HARDWARE_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = THERMAL_HARDWARE_MODULE_ID,
        .name = "Thermal HAL",
        .author = "The Android Open Source Project",
        .methods = &thermal_module_methods,
    },
    .getTemperatures = get_temperatures,
    .getCpuUsages = get_cpu_usages,
};
