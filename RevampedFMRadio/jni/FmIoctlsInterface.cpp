/*
Copyright (c) 2015, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define LOG_TAG "android_hardware_fm"

#include "FmIoctlsInterface.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <cutils/properties.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <math.h>
#include <utils/Log.h>

int FmIoctlsInterface :: start_fm_patch_dl
(
    UINT fd __unused
)
{
    int ret;
#ifndef QCOM_NO_FM_FIRMWARE
    int init_success = 0;
    char versionStr[MAX_VER_STR_LEN] = {'\0'};
    char prop_value[PROPERTY_VALUE_MAX] = {'\0'};
    struct v4l2_capability cap;

    ALOGI("%s: start_fm_patch_dl = %d\n", __func__, fd);
    ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
    ALOGD("%s: executed cmd\n", __func__);
    if(ret == IOCTL_SUCC) {
        ret = snprintf(versionStr, MAX_VER_STR_LEN, "%d", cap.version);
        if(ret >= MAX_VER_STR_LEN) {
            return FM_FAILURE;
        }else {
            ret = property_set(FM_VERSION_PROP, versionStr);
            ALOGD("set versionStr done");
            if(ret != PROP_SET_SUCC)
               return FM_FAILURE;
            ret = property_set(FM_MODE_PROP, "normal");
            ALOGD("set FM_MODE_PROP done");
            if(ret != PROP_SET_SUCC)
               return FM_FAILURE;
            ret = property_set(FM_INIT_PROP, "0");
            ALOGD("set FM_INIT_PROP done");
            if(ret != PROP_SET_SUCC)
               return FM_FAILURE;
            ret = property_set(SCRIPT_START_PROP, SOC_PATCH_DL_SCRPT);
            if(ret != PROP_SET_SUCC)
               return FM_FAILURE;
            for(int i = 0; i < INIT_LOOP_CNT; i++) {
                property_get(FM_INIT_PROP, prop_value, NULL);
                if (strcmp(prop_value, "1") == 0) {
                    init_success = 1;
                    break;
                }else {
                    usleep(INIT_WAIT_TIMEOUT);
                }
            }
            if(!init_success) {
                property_set(SCRIPT_STOP_PROP, SOC_PATCH_DL_SCRPT);
                return FM_FAILURE;
            }else {
                return FM_SUCCESS;
            }
        }
    }else {
        return FM_FAILURE;
    }
#else
    ret = property_set(FM_INIT_PROP, "1");
    usleep(INIT_WAIT_TIMEOUT);
    if (ret != PROP_SET_SUCC)
        return FM_FAILURE;
    else
        return FM_SUCCESS;
#endif
}

int  FmIoctlsInterface :: close_fm_patch_dl
(
    void
)
{
    int ret;

#ifndef QCOM_NO_FM_FIRMWARE
    ret = property_set(SCRIPT_STOP_PROP, SOC_PATCH_DL_SCRPT);
    if(ret != PROP_SET_SUCC) {
        return FM_FAILURE;
    }else {
        return FM_SUCCESS;
    }
#else
    ret = property_set(FM_INIT_PROP, "0");
    if (ret != PROP_SET_SUCC)
        return FM_FAILURE;
    else
        return FM_SUCCESS;
#endif
}

int  FmIoctlsInterface :: get_cur_freq
(
    UINT fd, long &freq
)
{
    int ret;
    struct v4l2_frequency channel;

    channel.type = V4L2_TUNER_RADIO;
    ret = ioctl(fd, VIDIOC_G_FREQUENCY, &channel);

    if(ret < IOCTL_SUCC) {
        return FM_FAILURE;
    }else {
        freq = (channel.frequency / TUNE_MULT);
        return FM_SUCCESS;
    }
}

int  FmIoctlsInterface :: set_freq
(
    UINT fd, ULINT freq
)
{
    int ret;
    struct v4l2_frequency channel;

    channel.type = V4L2_TUNER_RADIO;
    channel.frequency = (freq * TUNE_MULT);

    ret = ioctl(fd, VIDIOC_S_FREQUENCY, &channel);
    if(ret < IOCTL_SUCC) {
        return FM_FAILURE;
    }else {
        return FM_SUCCESS;
    }
}

int  FmIoctlsInterface :: set_control
(
    UINT fd, UINT id, int val
)
{
    int ret;
    struct v4l2_control control;

    control.value = val;
    control.id = id;

    ret = ioctl(fd, VIDIOC_S_CTRL, &control);
    if(ret < IOCTL_SUCC) {
        return FM_FAILURE;
    }else {
        return FM_SUCCESS;
    }
}

int  FmIoctlsInterface :: set_calibration
(
    UINT fd __unused
)
{
#ifndef QCOM_NO_FM_FIRMWARE
    int ret;
    FILE *cal_fp;
    struct v4l2_ext_control ext_ctl;
    struct v4l2_ext_controls v4l2_ctls;
    char cal_data[CAL_DATA_SIZE] = {0};

    cal_fp = fopen(CALIB_DATA_NAME, "r");
    if(cal_fp != NULL) {
       if(fread(&cal_data[0], 1, CAL_DATA_SIZE, cal_fp)
           < CAL_DATA_SIZE) {
           fclose(cal_fp);
           return FM_FAILURE;
       }
       fclose(cal_fp);
       ext_ctl.string = cal_data;
       ext_ctl.size = CAL_DATA_SIZE;
       v4l2_ctls.ctrl_class = V4L2_CTRL_CLASS_USER;
       v4l2_ctls.count = 1;
       v4l2_ctls.controls = &ext_ctl;
       ret = ioctl(fd, VIDIOC_S_EXT_CTRLS, &v4l2_ctls);
       if(ret < IOCTL_SUCC) {
           return FM_FAILURE;
       }else {
           return FM_SUCCESS;
       }
    }else {
        return FM_FAILURE;
    }
#else
    return FM_SUCCESS;
#endif
}

int  FmIoctlsInterface :: get_control
(
    UINT fd, UINT id, long &val
)
{
    int ret;
    struct v4l2_control control;

    control.id = id;
    ret = ioctl(fd, VIDIOC_G_CTRL, &control);
    if(ret < IOCTL_SUCC) {
        return FM_FAILURE;
    }else {
        val = control.value;
        return FM_SUCCESS;
    }
}

int  FmIoctlsInterface :: start_search
(
    UINT fd, UINT dir
)
{
    int ret;
    struct v4l2_hw_freq_seek hw_seek;

    hw_seek.seek_upward = dir;
    hw_seek.type = V4L2_TUNER_RADIO;

    ret = ioctl(fd, VIDIOC_S_HW_FREQ_SEEK, &hw_seek);
    if(ret < IOCTL_SUCC) {
        return FM_FAILURE;
    }else {
        return FM_SUCCESS;
    }
}

int  FmIoctlsInterface :: set_band
(
    UINT fd, ULINT low, ULINT high
)
{
    int ret;
    struct v4l2_tuner tuner;

    tuner.index = 0;
    tuner.signal = 0;
    tuner.rangelow = (low * TUNE_MULT);
    tuner.rangehigh = (high * TUNE_MULT);

    ret = ioctl(fd, VIDIOC_S_TUNER, &tuner);
    ret = set_control(fd, V4L2_CID_PRV_REGION, 0);
    if(ret < IOCTL_SUCC) {
        return FM_FAILURE;
    }else {
        return FM_SUCCESS;
    }
}

int FmIoctlsInterface :: get_rmssi
(
    UINT fd, long &rmssi
)
{
    struct v4l2_tuner tuner;
    int ret;

    tuner.index = 0;
    tuner.signal = 0;
    ret = ioctl(fd, VIDIOC_G_TUNER, &tuner);
    if(ret < IOCTL_SUCC) {
        ret = FM_FAILURE;
    }else {
        rmssi = tuner.signal;
        ret = FM_SUCCESS;
    }
    return ret;
}

int  FmIoctlsInterface :: get_upperband_limit
(
    UINT fd, ULINT &freq
)
{
    int ret;
    struct v4l2_tuner tuner;

    tuner.index = 0;
    ret = ioctl(fd, VIDIOC_G_TUNER, &tuner);
    if(ret < IOCTL_SUCC) {
        return FM_FAILURE;
    }else {
        freq = (tuner.rangehigh / TUNE_MULT);
        ALOGI("high freq: %lu\n", freq);
        return FM_SUCCESS;
    }
}

int  FmIoctlsInterface :: get_lowerband_limit
(
    UINT fd, ULINT &freq
)
{
    int ret;
    struct v4l2_tuner tuner;

    tuner.index = 0;
    ret = ioctl(fd, VIDIOC_G_TUNER, &tuner);
    if(ret < IOCTL_SUCC) {
        return FM_FAILURE;
    }else {
        freq = (tuner.rangelow / TUNE_MULT);
        ALOGE("low freq: %lu\n",freq);
        return FM_SUCCESS;
    }
}

int  FmIoctlsInterface :: set_audio_mode
(
    UINT fd, enum AUDIO_MODE mode
)
{
    int ret;
    struct v4l2_tuner tuner;

    tuner.index = 0;
    ret = ioctl(fd, VIDIOC_G_TUNER, &tuner);
    if(ret < IOCTL_SUCC) {
        return FM_FAILURE;
    }else {
        tuner.audmode = mode;
        ret = ioctl(fd, VIDIOC_S_TUNER, &tuner);
        if(ret != IOCTL_SUCC) {
            return FM_FAILURE;
        }else {
            return FM_SUCCESS;
        }
    }
}

int  FmIoctlsInterface :: get_buffer
(
     UINT fd, char *buff, UINT len, UINT index
)
{
    int ret;
    struct v4l2_buffer v4l2_buf;

    if((len < STD_BUF_SIZE) || (buff == NULL)) {
        return FM_FAILURE;
    }else {
        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        v4l2_buf.index = index;
        v4l2_buf.type = V4L2_BUF_TYPE_PRIVATE;
        v4l2_buf.length = STD_BUF_SIZE;
        v4l2_buf.m.userptr = (ULINT)buff;
        ret = ioctl(fd, VIDIOC_DQBUF, &v4l2_buf);
        if(ret < IOCTL_SUCC) {
            return FM_FAILURE;
        }else {
            return v4l2_buf.bytesused;
        }
    }
}

int FmIoctlsInterface :: set_ext_control
(
    UINT fd,
    struct v4l2_ext_controls *v4l2_ctls
)
{
    int ret;

    ret = ioctl(fd, VIDIOC_S_EXT_CTRLS, v4l2_ctls);

    if(ret < IOCTL_SUCC) {
       return FM_FAILURE;
    }else {
       return FM_SUCCESS;
    }
}

