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
 *     * Neither the name of The Linux Foundation, nor the names of its
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
 *
 */
#define LOG_NDEBUG 0
#define LOG_TAG "LocSvc_misc_utils"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <dlfcn.h>
#include <math.h>
#include <log_util.h>
#include <loc_misc_utils.h>
#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>

#ifndef MSEC_IN_ONE_SEC
#define MSEC_IN_ONE_SEC 1000ULL
#endif
#define GET_MSEC_FROM_TS(ts) ((ts.tv_sec * MSEC_IN_ONE_SEC) + (ts.tv_nsec + 500000)/1000000)

int loc_util_split_string(char *raw_string, char **split_strings_ptr,
                          int max_num_substrings, char delimiter)
{
    int raw_string_index=0;
    int num_split_strings=0;
    unsigned char end_string=0;
    int raw_string_length=0;

    if(!raw_string || !split_strings_ptr) {
        LOC_LOGE("%s:%d]: NULL parameters", __func__, __LINE__);
        num_split_strings = -1;
        goto err;
    }
    LOC_LOGD("%s:%d]: raw string: %s\n", __func__, __LINE__, raw_string);
    raw_string_length = strlen(raw_string) + 1;
    split_strings_ptr[num_split_strings] = &raw_string[raw_string_index];
    for(raw_string_index=0; raw_string_index < raw_string_length; raw_string_index++) {
        if(raw_string[raw_string_index] == '\0')
            end_string=1;
        if((raw_string[raw_string_index] == delimiter) || end_string) {
            raw_string[raw_string_index] = '\0';
            if (num_split_strings < max_num_substrings) {
                LOC_LOGD("%s:%d]: split string: %s\n",
                         __func__, __LINE__, split_strings_ptr[num_split_strings]);
            }
            num_split_strings++;
            if(((raw_string_index + 1) < raw_string_length) &&
               (num_split_strings < max_num_substrings)) {
                split_strings_ptr[num_split_strings] = &raw_string[raw_string_index+1];
            }
            else {
                break;
            }
        }
        if(end_string)
            break;
    }
err:
    LOC_LOGD("%s:%d]: num_split_strings: %d\n", __func__, __LINE__, num_split_strings);
    return num_split_strings;
}

void loc_util_trim_space(char *org_string)
{
    char *scan_ptr, *write_ptr;
    char *first_nonspace = NULL, *last_nonspace = NULL;

    if(org_string == NULL) {
        LOC_LOGE("%s:%d]: NULL parameter", __func__, __LINE__);
        goto err;
    }

    scan_ptr = write_ptr = org_string;

    while (*scan_ptr) {
        //Find the first non-space character
        if ( !isspace(*scan_ptr) && first_nonspace == NULL) {
            first_nonspace = scan_ptr;
        }
        //Once the first non-space character is found in the
        //above check, keep shifting the characters to the left
        //to replace the spaces
        if (first_nonspace != NULL) {
            *(write_ptr++) = *scan_ptr;
            //Keep track of which was the last non-space character
            //encountered
            //last_nonspace will not be updated in the case where
            //the string ends with spaces
            if ( !isspace(*scan_ptr)) {
                last_nonspace = write_ptr;
            }
        }
        scan_ptr++;
    }
    //Add NULL terminator after the last non-space character
    if (last_nonspace) { *last_nonspace = '\0'; }
err:
    return;
}

inline void logDlError(const char* failedCall) {
    const char * err = dlerror();
    LOC_LOGe("%s error: %s", failedCall, (nullptr == err) ? "unknown" : err);
}

void* dlGetSymFromLib(void*& libHandle, const char* libName, const char* symName)
{
    void* sym = nullptr;
    if ((nullptr != libHandle || nullptr != libName) && nullptr != symName) {
        if (nullptr == libHandle) {
            libHandle = dlopen(libName, RTLD_NOW);
            if (nullptr == libHandle) {
                logDlError("dlopen");
            }
        }
        // NOT else, as libHandle gets assigned 5 line above
        if (nullptr != libHandle) {
            sym = dlsym(libHandle, symName);
            if (nullptr == sym) {
                logDlError("dlsym");
            }
        }
    } else {
        LOC_LOGe("Either libHandle (%p) or libName (%p) must not be null; "
                 "symName (%p) can not be null.", libHandle, libName, symName);
    }

    return sym;
}

uint64_t getQTimerTickCount()
{
    uint64_t qTimerCount = 0;
#if __aarch64__
    asm volatile("mrs %0, cntvct_el0" : "=r" (qTimerCount));
#elif defined (__i386__) || defined (__x86_64__)
    /* Qtimer not supported in x86 architecture */
    qTimerCount = 0;
#else
    asm volatile("mrrc p15, 1, %Q0, %R0, c14" : "=r" (qTimerCount));
#endif

    return qTimerCount;
}

uint64_t getQTimerDeltaNanos()
{
    char qtimer_val_string[100];
    char *temp;
    uint64_t local_qtimer = 0, remote_qtimer = 0;
    int mdm_fd = -1, wlan_fd = -1, ret = 0;
    uint64_t delta = 0;

    memset(qtimer_val_string, '\0', sizeof(qtimer_val_string));

    char devNode[] = "/sys/bus/mhi/devices/0306_00.01.00/time_us";
    for (; devNode[27] < 3 && mdm_fd < 0; devNode[27]++) {
        mdm_fd = ::open(devNode, O_RDONLY);
        if (mdm_fd < 0) {
            LOC_LOGe("MDM open file: %s error: %s", devNode, strerror(errno));
        }
    }
    if (mdm_fd > 0) {
        ret = read(mdm_fd, qtimer_val_string, sizeof(qtimer_val_string)-1);
        ::close(mdm_fd);
        if (ret < 0) {
            LOC_LOGe("MDM read time_us file error: %s", strerror(errno));
        } else {
            temp = qtimer_val_string;
            temp = strchr(temp, ':');
            temp = temp + 2;
            local_qtimer = atoll(temp);

            temp = strchr(temp, ':');
            temp = temp + 2;
            remote_qtimer = atoll(temp);

            if (local_qtimer >= remote_qtimer) {
                delta = (local_qtimer - remote_qtimer) * 1000;
            }
            LOC_LOGv("qtimer values in microseconds: local:%" PRIi64 " remote:%" PRIi64 ""
                     " delta in nanoseconds:%" PRIi64 "",
                     local_qtimer, remote_qtimer, delta);
        }
    }
    return delta;
}

uint64_t getQTimerFreq()
{
#if __aarch64__
    uint64_t val = 0;
    asm volatile("mrs %0, cntfrq_el0" : "=r" (val));
#elif defined (__i386__) || defined (__x86_64__)
    /* Qtimer not supported in x86 architecture */
    uint64_t val = 0;
#else
    uint32_t val = 0;
    asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r" (val));
#endif
    return val;
}

uint64_t getBootTimeMilliSec()
{
    struct timespec curTs;
    clock_gettime(CLOCK_BOOTTIME, &curTs);
    return (uint64_t)GET_MSEC_FROM_TS(curTs);
}

// Used for convert position/velocity from GSNS antenna based to VRP based
void Matrix_MxV(float a[3][3],  float b[3], float c[3]) {
    int i, j;

    for (i=0; i<3; i++) {
        c[i] = 0.0f;
        for (j=0; j<3; j++)
            c[i] += a[i][j] * b[j];
    }
}

// Used for convert position/velocity from GNSS antenna based to VRP based
void Matrix_Skew(float a[3], float c[3][3]) {
    c[0][0] = 0.0f;
    c[0][1] = -a[2];
    c[0][2] = a[1];
    c[1][0] = a[2];
    c[1][1] = 0.0f;
    c[1][2] = -a[0];
    c[2][0] = -a[1];
    c[2][1] = a[0];
    c[2][2] = 0.0f;
}

// Used for convert position/velocity from GNSS antenna based to VRP based
void Euler2Dcm(float euler[3], float dcm[3][3]) {
    float cr = 0.0, sr = 0.0, cp = 0.0, sp = 0.0, ch = 0.0, sh = 0.0;

    cr = cosf(euler[0]);
    sr = sinf(euler[0]);
    cp = cosf(euler[1]);
    sp = sinf(euler[1]);
    ch = cosf(euler[2]);
    sh = sinf(euler[2]);

    dcm[0][0] = cp * ch;
    dcm[0][1] = (sp*sr*ch) - (cr*sh);
    dcm[0][2] = (cr*sp*ch) + (sh*sr);

    dcm[1][0] = cp * sh;
    dcm[1][1] = (sr*sp*sh) + (cr*ch);
    dcm[1][2] = (cr*sp*sh) - (sr*ch);

    dcm[2][0] = -sp;
    dcm[2][1] = sr * cp;
    dcm[2][2] = cr * cp;
}

// Used for convert position from GSNS based to VRP based
// The converted position will be stored in the llaInfo parameter.
#define A6DOF_WGS_A (6378137.0f)
#define A6DOF_WGS_B (6335439.0f)
#define A6DOF_WGS_E2 (0.00669437999014f)
void loc_convert_lla_gnss_to_vrp(double lla[3], float rollPitchYaw[3],
                                 float leverArm[3]) {
    LOC_LOGv("lla: %f, %f, %f, lever arm: %f %f %f, "
             "rollpitchyaw: %f %f %f",
             lla[0], lla[1], lla[2],
             leverArm[0], leverArm[1], leverArm[2],
             rollPitchYaw[0], rollPitchYaw[1], rollPitchYaw[2]);

    float cnb[3][3];
    memset(cnb, 0, sizeof(cnb));
    Euler2Dcm(rollPitchYaw, cnb);

    float sl = sin(lla[0]);
    float cl = cos(lla[0]);
    float sf = 1.0f / (1.0f - A6DOF_WGS_E2 * sl* sl);
    float sfr = sqrtf(sf);

    float rn = A6DOF_WGS_B * sf * sfr + lla[2];
    float re = A6DOF_WGS_A * sfr + lla[2];

    float deltaNEU[3];

    // gps_pos_lla = imu_pos_lla + Cbn*la_b .* [1/geo.Rn; 1/(geo.Re*geo.cL); -1];
    Matrix_MxV(cnb, leverArm, deltaNEU);

    // NED to lla conversion
    lla[0] = lla[0] + deltaNEU[0] / rn;
    lla[1] = lla[1] + deltaNEU[1] / (re * cl);
    lla[2] = lla[2] + deltaNEU[2];
}

// Used for convert velocity from GSNS based to VRP based
// The converted velocity will be stored in the enuVelocity parameter.
void loc_convert_velocity_gnss_to_vrp(float enuVelocity[3], float rollPitchYaw[3],
                                      float rollPitchYawRate[3], float leverArm[3]) {

    LOC_LOGv("enu velocity: %f, %f, %f, lever arm: %f %f %f, roll pitch yaw: %f %f %f,"
             "rollpitchyawRate: %f %f %f",
             enuVelocity[0], enuVelocity[1], enuVelocity[2],
             leverArm[0], leverArm[1], leverArm[2],
             rollPitchYaw[0], rollPitchYaw[1], rollPitchYaw[2],
             rollPitchYawRate[0], rollPitchYawRate[1], rollPitchYawRate[2]);

    float cnb[3][3];
    memset(cnb, 0, sizeof(cnb));
    Euler2Dcm(rollPitchYaw, cnb);

    float skewLA[3][3];
    memset(skewLA, 0, sizeof(skewLA));
    Matrix_Skew(leverArm, skewLA);

    float tmp[3];
    float deltaEnuVelocity[3];
    memset(tmp, 0, sizeof(tmp));
    memset(deltaEnuVelocity, 0, sizeof(deltaEnuVelocity));
    Matrix_MxV(skewLA, rollPitchYawRate, tmp);
    Matrix_MxV(cnb, tmp, deltaEnuVelocity);

    enuVelocity[0] = enuVelocity[0] - deltaEnuVelocity[0];
    enuVelocity[1] = enuVelocity[1] - deltaEnuVelocity[1];
    enuVelocity[2] = enuVelocity[2] - deltaEnuVelocity[2];
}
