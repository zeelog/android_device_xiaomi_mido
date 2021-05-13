/* Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *  * Neither the name of The Linux Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.

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

#include <hardware/thermal.h>
#define ARRAY_SIZE(x) (int)(sizeof(x)/sizeof(x[0]))

enum therm_msm_id {
    THERM_MSM_UNKNOWN = 0,
    THERM_MSM_8953,
    THERM_SDM_660,
    THERM_SDM_630,
    THERM_SDM_710,
    THERM_QCS_605,
    THERM_SDM_632,
    THERM_SDM_439,
    THERM_MSMNILE,
    THERM_TALOS,
    THERM_SDMMAGPIE,
    THERM_MSM_8917,
    THERM_TRINKET,
    THERM_KONA,
    THERM_LITO,
    THERM_ATOLL,
    THERM_BENGAL,
    THERM_LAGOON,
    THERM_SCUBA,
};

struct target_therm_cfg {
    enum temperature_type type;
    char **sensor_list;
    uint8_t sens_cnt;
    char *label;
    float mult;
    int throt_thresh;
    int shutdwn_thresh;
    int vr_thresh;
};

struct vendor_temperature {
    int tzn;
    float mult;
    temperature_t t;
};


int read_line_from_file(const char *path, char *buf, size_t count);
size_t get_num_cpus();
const char *get_cpu_label(unsigned int cpu_num);
int thermal_zone_init(struct target_therm_cfg *v_sen_t, int cfg_cnt);
ssize_t get_temperature_for_all(temperature_t *list, size_t size);
