/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
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

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#define LOG_TAG "ThermalHAL-UTIL"
#include <utils/Log.h>

#include <hardware/hardware.h>
#include <hardware/thermal.h>
#include "thermal_common.h"

#define MAX_LENGTH    50
#define MAX_PATH      (256)
#define CPU_LABEL      "CPU%d"
#define THERMAL_SYSFS  "/sys/devices/virtual/thermal"
#define TZ_DIR_NAME    "thermal_zone"
#define TZ_DIR_FMT     "thermal_zone%d"
#define THERMAL_TYPE "/sys/devices/virtual/thermal/%s/type"
#define TEMPERATURE_FILE_FORMAT  "/sys/class/thermal/thermal_zone%d/temp"

static char **cpu_label;
static struct vendor_temperature *sensors;
static unsigned int sensor_cnt;

/**
 * Get number of cpus of target.
 *
 * @return number of cpus on success or 0 on error.
 */
size_t get_num_cpus() {
    static int ncpus;

    if (!ncpus) {
        ncpus = (int)sysconf(_SC_NPROCESSORS_CONF);
        if (ncpus < 1)
            ALOGE("%s: Error retrieving number of cores", __func__);
    }
    return ncpus;
}

/**
 * Get cpu label for a given cpu.
 *
 * @param cpu_num: cpu number.
 *
 * @return cpu label string on success or NULL on error.
 */
const char *get_cpu_label(unsigned int cpu_num) {
    unsigned int cpu = 0;

    if (cpu_label == NULL) {
        cpu_label= (char**)calloc(get_num_cpus(), sizeof(char *));
	if (!cpu_label)
		return NULL;
	for(cpu = 0; cpu < get_num_cpus(); cpu++) {
            cpu_label[cpu] = (char *)calloc(sizeof("CPUN"), sizeof(char));
            if(!cpu_label[cpu])
                return NULL;
            snprintf(cpu_label[cpu], sizeof("CPUN"), CPU_LABEL, cpu);
	}
    }
    if(cpu_num >= get_num_cpus())
        return NULL;

    return cpu_label[cpu_num];
}

/**
 * Read data from a target sysfs file.
 *
 * @param path: Absolute path for a file to be read.
 * @param buf: Char buffer to store data from file.
 * @param count: Size of data buffer.
 *
 * @return number of bytes read on success or negative value on error.
 */
int read_line_from_file(const char *path, char *buf, size_t count)
{
    char * fgets_ret;
    FILE * fd;
    int rv;

    fd = fopen(path, "r");
    if (fd == NULL)
        return -1;

    fgets_ret = fgets(buf, (int)count, fd);
    if (NULL != fgets_ret) {
        rv = (int)strlen(buf);
    } else {
        rv = ferror(fd);
    }

    fclose(fd);

    return rv;
}

/**
 * Function to get thermal zone id from sensor name.
 *
 * @param sensor_name: Name of sensor.
 *
 * @return positive integer on success or negative value on error.
 */
static int get_tzn(const char *sensor_name)
{
    DIR *tdir = NULL;
    struct dirent *tdirent = NULL;
    int found = -1;
    int tzn = 0;
    char name[MAX_PATH] = {0};
    char cwd[MAX_PATH] = {0};
    int ret = 0;

    if (!getcwd(cwd, sizeof(cwd)))
        return found;

    /* Change dir to read the entries. Doesnt work otherwise */
    ret = chdir(THERMAL_SYSFS);
    if (ret) {
        ALOGE("Unable to change to %s\n", THERMAL_SYSFS);
        return found;
    }
    tdir = opendir(THERMAL_SYSFS);
    if (!tdir) {
        ALOGE("Unable to open %s\n", THERMAL_SYSFS);
        return found;
    }

    while ((tdirent = readdir(tdir))) {
        char buf[50];
        struct dirent *tzdirent;
        DIR *tzdir = NULL;

        if (strncmp(tdirent->d_name, TZ_DIR_NAME,
            strlen(TZ_DIR_NAME)) != 0)
            continue;

        tzdir = opendir(tdirent->d_name);
        if (!tzdir)
            continue;
        while ((tzdirent = readdir(tzdir))) {
            if (strcmp(tzdirent->d_name, "type"))
                continue;
            snprintf(name, MAX_PATH, THERMAL_TYPE,
                    tdirent->d_name);
            ret = read_line_from_file(name, buf, sizeof(buf));
            if (ret <= 0) {
                ALOGE("%s: sensor name read error for tz:%s\n",
                        __func__, tdirent->d_name);
                break;
            }
            if (buf[ret - 1] == '\n')
                buf[ret - 1] = '\0';
            else
                buf[ret] = '\0';

            if (!strcmp(buf, sensor_name)) {
                found = 1;
		break;
            }
        }
        closedir(tzdir);
        if (found == 1)
            break;
    }

    if (found == 1) {
        sscanf(tdirent->d_name, TZ_DIR_FMT, &tzn);
        ALOGD("Sensor %s found at tz: %d\n",
                sensor_name, tzn);
        found = tzn;
    }

    closedir(tdir);
    /* Restore current working dir */
    ret = chdir(cwd);

    return found;
}

/**
 * Helper function for sensor intialization.
 *
 * @param v_sen_t: pointer to a sensor static config.
 * @param sensor: pointer to a sensor vendor_temperature structure.
 * @param type: Type of sensor ie cpu, battery, gpu, skin etc.
 * @param sens_idx: Index for sensor of same type.
 *
 * @return 0 on success or negative value -errno on error.
 */
static int initialize_sensor(struct target_therm_cfg *v_sen_t,
                               struct vendor_temperature *sensor,
                               enum temperature_type type,
                               int sens_idx)
{
    if (v_sen_t == NULL || sensor == NULL ||
        sens_idx < 0) {
         ALOGE("%s:Invalid input, sens_idx%d\n", __func__, sens_idx);
         return -1;
    }

    sensor->tzn = get_tzn(v_sen_t->sensor_list[sens_idx]);
    if (sensor->tzn < 0) {
        ALOGE("No thermal zone for sensor: %s, ret:%d\n",
               v_sen_t->sensor_list[sens_idx], sensor->tzn);
        return -1;
    }
    if (type == DEVICE_TEMPERATURE_CPU)
        sensor->t.name = get_cpu_label(sens_idx);
    else
        sensor->t.name = v_sen_t->label;

    sensor->t.type = v_sen_t->type;
    sensor->mult = v_sen_t->mult;

    if (v_sen_t->throt_thresh != 0)
        sensor->t.throttling_threshold = v_sen_t->throt_thresh;
    else
        sensor->t.throttling_threshold = UNKNOWN_TEMPERATURE;

    if (v_sen_t->shutdwn_thresh != 0)
        sensor->t.shutdown_threshold = v_sen_t->shutdwn_thresh;
    else
        sensor->t.shutdown_threshold = UNKNOWN_TEMPERATURE;

    if (v_sen_t->vr_thresh != 0)
        sensor->t.vr_throttling_threshold = v_sen_t->vr_thresh;
    else
        sensor->t.vr_throttling_threshold = UNKNOWN_TEMPERATURE;

    return 0;
}

/**
 * Initialize all sensors.
 *
 * @param v_sen_t: Base pointer to array of target specific sensor configs.
 * @param cfg_cnt: Number of set of config for a given target.
 *
 * @return number of sensor on success or negative value or zero on error.
 */
int thermal_zone_init(struct target_therm_cfg *v_sen_t, int cfg_cnt)
{
    unsigned int idx = 0, cpu = 0;
    int j = 0;

    if (sensors != NULL && sensor_cnt != 0)
        return sensor_cnt;

    if (v_sen_t == NULL || cfg_cnt == 0) {
        ALOGE("%s:Invalid input\n", __func__);
        return -1;
    }
    sensors = calloc(get_num_cpus() + cfg_cnt - 1,
        sizeof(struct vendor_temperature));

    for (j = 0, idx = 0; j < cfg_cnt &&
                idx < (get_num_cpus() + cfg_cnt - 1); j++) {
        if (v_sen_t[j].type == DEVICE_TEMPERATURE_CPU) {
            /* Initialize cpu thermal zone id */
            for (cpu = 0; cpu < get_num_cpus() &&
                        idx < (get_num_cpus() + cfg_cnt - 1); cpu++, idx++) {
                if (initialize_sensor(&v_sen_t[j], &sensors[idx],
                      v_sen_t[j].type, cpu)) {
                        free(sensors);
                        return -1;
                }
           }
        } else {
            /* Initialize misc thermal zone id */
            if (initialize_sensor(&v_sen_t[j], &sensors[idx],
                  v_sen_t[j].type, 0)) {
                free(sensors);
                return -1;
            }
            idx++;
       }
    }
    sensor_cnt = idx;

    return sensor_cnt;
}

/**
 * Reads sensor temperature.
 *
 * @param sensor_num: thermal zone id for the sensor to be read
 * @param type: Device temperature type.
 * @param name: Device temperature name.
 * @param mult: Multiplier used to translate temperature to Celsius.
 * @param throttling_threshold: Throttling threshold for the sensor.
 * @param shutdown_threshold: Shutdown threshold for the sensor.
 * @param out: Pointer to temperature_t structure that will be filled with
 *     temperature values.
 *
 * @return 0 on success or negative value -errno on error.
 */
static ssize_t read_temperature(int sensor_num, int type, const char *name,
        float mult, float throttling_threshold, float shutdown_threshold,
        float vr_throttling_threshold,
        temperature_t *out) {
    char file_name[MAX_LENGTH];
    float temp;
    char buf[16] = {0};
    int ret = 0;

    snprintf(file_name, sizeof(file_name), TEMPERATURE_FILE_FORMAT, sensor_num);
    ret = read_line_from_file(file_name, buf, sizeof(buf));
    if (ret <= 0) {
        ALOGE("Temperature read error: %d for sensor[%d]:%s\n",
            ret, sensor_num, name);
	return -1;
    }
    temp = atof(buf);

    (*out) = (temperature_t) {
        .type = type,
        .name = name,
        .current_value = temp * mult,
        .throttling_threshold = throttling_threshold,
        .shutdown_threshold = shutdown_threshold,
        .vr_throttling_threshold = vr_throttling_threshold
    };

    return 0;
}

/**
 * Reads all sensor temperature.
 *
 * @param list: Base pointer to array of temperature_t structure that will be
 *     filled with temperature values.
 * @param size: Number of sensor temperature needs to be filled in list.
 *
 * @return number of sensor data filled on success or 0 or negative value
 *     -errno on error.
 */
ssize_t get_temperature_for_all(temperature_t *list, size_t size)
{
    size_t idx;

    if (sensors == NULL) {
        ALOGE("No sensor configured\n");
	return 0;
    }

    for (idx = 0; idx < sensor_cnt && idx < size; idx++) {
        ssize_t result = read_temperature(sensors[idx].tzn, sensors[idx].t.type,
                sensors[idx].t.name, sensors[idx].mult,
                sensors[idx].t.throttling_threshold,
                sensors[idx].t.shutdown_threshold,
                sensors[idx].t.vr_throttling_threshold,
                &list[idx]);
        if (result != 0)
            return result;
    }
    return idx;
}

