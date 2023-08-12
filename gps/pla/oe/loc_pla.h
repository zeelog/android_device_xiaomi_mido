/* Copyright (c) 2014, 2020 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
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

#ifndef __LOC_PLA__
#define __LOC_PLA__

#ifdef __cplusplus
#ifndef FEATURE_EXTERNAL_AP
#include <utils/SystemClock.h>
#endif /* FEATURE_EXTERNAL_AP */
#include <inttypes.h>
#include <sys/time.h>
#include <time.h>

#if defined(__GNUC__) && defined(__GNUC_PREREQ)
#if __GNUC_PREREQ(6,0)
    #pragma message "GNU C version is above 6"
#else
    #pragma message "GNU C version is less than 6"
    #define NO_UNORDERED_SET_OR_MAP
#endif
#endif

// use set/map instead of unordered_set/unordered_map for
// older GCC versions
#ifdef NO_UNORDERED_SET_OR_MAP
#define unordered_set set
#define unordered_map map
#endif

inline int64_t sysTimeMillis(int clock)
{
    struct timespec ts;
    int64_t time_ms = 0;
    clock_gettime(clock, &ts);
    time_ms += (ts.tv_sec * 1000000000LL);
    time_ms += ts.tv_nsec + 500000LL;
    return time_ms / 1000000LL;
}

inline int64_t uptimeMillis() {
    return sysTimeMillis(CLOCK_MONOTONIC);
}
inline int64_t elapsedRealtime() {
    return sysTimeMillis(CLOCK_BOOTTIME);
}

extern "C" {
#endif

#ifndef FEATURE_EXTERNAL_AP
#include <cutils/properties.h>
#include <cutils/threads.h>
#include <cutils/sched_policy.h>
#else
#define set_sched_policy(a, b)
#endif /* FEATURE_EXTERNAL_AP */
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#define MAX_COMMAND_STR_LEN (255)
#define BOOT_KPI_FILE "/sys/kernel/debug/bootkpi/kpi_values"
#ifndef OFF_TARGET
#include <glib.h>
#define strlcat g_strlcat
#define strlcpy g_strlcpy
#else
#define strlcat strncat
#define strlcpy strncpy
#endif

#define UID_GPS (1021)
#define GID_GPS (1021)
#define UID_LOCCLIENT (4021)
#define GID_LOCCLIENT (4021)

#define LOC_PATH_GPS_CONF_STR      "/etc/gps.conf"
#define LOC_PATH_IZAT_CONF_STR     "/etc/izat.conf"
#define LOC_PATH_FLP_CONF_STR      "/etc/flp.conf"
#define LOC_PATH_LOWI_CONF_STR     "/etc/lowi.conf"
#define LOC_PATH_SAP_CONF_STR      "/etc/sap.conf"
#define LOC_PATH_APDR_CONF_STR     "/etc/apdr.conf"
#define LOC_PATH_XTWIFI_CONF_STR   "/etc/xtwifi.conf"
#define LOC_PATH_QUIPC_CONF_STR    "/etc/quipc.conf"
#define LOC_PATH_ANT_CORR_STR      "/etc/gnss_antenna_info.conf"
#define LOC_PATH_SLIM_CONF_STR     "/etc/slim.conf"
#define LOC_PATH_VPE_CONF_STR      "/etc/vpeglue.conf"

#ifdef FEATURE_EXTERNAL_AP
#define PROPERTY_VALUE_MAX 92

inline int property_get(const char* key, char* value, const char* default_value)
{
    strlcpy(value, default_value, PROPERTY_VALUE_MAX - 1);
    return strlen(value);
}
#endif /* FEATURE_EXTERNAL_AP */

/*!
 * @brief Function for memory block copy
 *
 * @param[out] p_Dest     Destination buffer.
 * @param[in]  q_DestSize Destination buffer size.
 * @param[in]  p_Src      Source buffer.
 * @param[in]  q_SrcSize  Source buffer size.
 *
 * @return Number of bytes copied.
 */
static inline size_t memscpy (void *p_Dest, size_t q_DestSize, const void *p_Src, size_t q_SrcSize)
{
    size_t res = (q_DestSize < q_SrcSize) ? q_DestSize : q_SrcSize;
    if (p_Dest && p_Src && q_DestSize > 0 && q_SrcSize > 0) {
        memcpy(p_Dest, p_Src, res);
    } else {
        res = 0;
    }
    return res;
}

/*API for boot kpi marker prints  */
static inline int loc_boot_kpi_marker(const char * pFmt, ...)
{
    int result = 0;
    FILE *stream = NULL;
    char data[MAX_COMMAND_STR_LEN] = {};
    char buf[MAX_COMMAND_STR_LEN] = {};

    va_list ap;
    va_start(ap, pFmt);
    vsnprintf(&buf[0], sizeof(buf), pFmt, ap);
    snprintf(data, sizeof(data), "echo -n %s > %s", buf, BOOT_KPI_FILE);
    stream = popen(data, "w" );
    if (NULL == stream) {
        result = -1;
    } else {
        pclose(stream);
    }
    va_end(ap);
    return result;
}

#ifdef __cplusplus
}
#endif /*__cplusplus */

#endif /* __LOC_PLA__ */
