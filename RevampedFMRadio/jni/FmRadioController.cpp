/*
Copyright (c) 2015,2022 The Linux Foundation. All rights reserved.

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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <utils/Log.h>
#include <cutils/properties.h>
#include <sys/ioctl.h>
#include <math.h>
#include "FmRadioController.h"
#include "FmIoctlsInterface.h"
#include "ConfigFmThs.h"
#include <linux/videodev2.h>

//Reset all variables to default value
static FmIoctlsInterface * FmIoct;
FmRadioController :: FmRadioController
(
)
{
    cur_fm_state = FM_OFF;
    prev_freq = -1;
    seek_scan_canceled = false;
    af_enabled = 0;
    rds_enabled = 0;
    event_listener_canceled = false;
    is_rds_support = false;
    is_ps_event_received = false;
    is_rt_event_received = false;
    is_af_jump_received = false;
    mutex_fm_state = PTHREAD_MUTEX_INITIALIZER;
    mutex_seek_compl_cond = PTHREAD_MUTEX_INITIALIZER;
    mutex_scan_compl_cond = PTHREAD_MUTEX_INITIALIZER;
    mutex_tune_compl_cond = PTHREAD_MUTEX_INITIALIZER;
    mutex_turn_on_cond = PTHREAD_MUTEX_INITIALIZER;
    turn_on_cond = PTHREAD_COND_INITIALIZER;
    seek_compl_cond = PTHREAD_COND_INITIALIZER;
    scan_compl_cond = PTHREAD_COND_INITIALIZER;
    tune_compl_cond = PTHREAD_COND_INITIALIZER;
    event_listener_thread = 0;
    fd_driver = -1;
    FmIoct = new FmIoctlsInterface();
}

/* Turn off FM */
FmRadioController :: ~FmRadioController
(
)
{
    if((cur_fm_state != FM_OFF)) {
        Stop_Scan_Seek();
        set_fm_state(FM_OFF_IN_PROGRESS);
        FmIoctlsInterface::set_control(fd_driver,
                        V4L2_CID_PRV_STATE, FM_DEV_NONE);
    }
    if(event_listener_thread != 0) {
        event_listener_canceled = true;
        pthread_join(event_listener_thread, NULL);
    }
}

int FmRadioController ::open_dev()
{
    int ret = FM_SUCCESS;

    fd_driver = open(FM_DEVICE_PATH, O_RDONLY);

    if (fd_driver < 0) {
        ALOGE("%s failed, [fd=%d] %s\n", __func__, fd_driver, FM_DEVICE_PATH);
        return FM_FAILURE;
    }

    ALOGD("%s, [fd=%d] \n", __func__, fd_driver);
    return ret;
}

int FmRadioController ::close_dev()
{
    int ret = 0;

    if (fd_driver > 0) {
        close(fd_driver);
        fd_driver = -1;
    }
    ALOGD("%s, [fd=%d] [ret=%d]\n", __func__, fd_driver, ret);
    return ret;
}

struct timespec FmRadioController :: set_time_out
(
    int secs
)
{
    struct timespec ts;
    struct timeval tp;

    gettimeofday(&tp, NULL);
    ts.tv_sec = tp.tv_sec;
    ts.tv_nsec = tp.tv_usec * 1000;
    ts.tv_sec += secs;

    return ts;
}

//Get current tuned frequency
//Return -1 if failed to get freq
long FmRadioController :: GetChannel
(
    void
)
{
    long freq = -1;
    int ret;

    if((cur_fm_state != FM_OFF) &&
       (cur_fm_state != FM_ON_IN_PROGRESS)) {
       ret = FmIoctlsInterface::get_cur_freq(fd_driver, freq);
       if(ret == FM_SUCCESS) {
          ALOGI("FM get freq is successfull, freq is: %ld\n", freq);
       }else {
          ALOGE("FM get frequency failed, freq is: %ld\n", freq);
       }
    }else {
       ALOGE("FM get freq is not valid in current state\n");
    }
    return freq;
}

int FmRadioController ::Pwr_Up(int freq)
{
    int ret = FM_SUCCESS;
    struct timespec ts;
    ConfigFmThs thsObj;
    char value[PROPERTY_VALUE_MAX] = {'\0'};

    ALOGI("%s,[freq=%d]\n", __func__, freq);
    property_get("vendor.qcom.bluetooth.soc", value, NULL);
    ALOGD("BT soc is '%s'\n", value);
    if (fd_driver < 0) {
        ret = open_dev();
        if (ret != FM_SUCCESS) {
            ALOGE("Dev open failed\n");
            return FM_FAILURE;
        }
    }

    if (cur_fm_state == FM_OFF) {
        ALOGD("%s: cur_fm_state = %d\n", __func__, cur_fm_state);
        if (strcmp(value, "rome") != 0) {
            ret = FmIoctlsInterface::start_fm_patch_dl(fd_driver);
            if (ret != FM_SUCCESS) {
                ALOGE("FM patch downloader failed: %d\n", ret);
                close_dev();
                set_fm_state(FM_OFF);
                return FM_FAILURE;
            }
        }
        if (event_listener_thread == 0) {
            ret = pthread_create(&event_listener_thread, NULL,
                                              handle_events, this);
            if (ret == 0) {
                ALOGI("Lock the mutex for FM turn on cond\n");
                pthread_mutex_lock(&mutex_turn_on_cond);
                ts = set_time_out(READY_EVENT_TIMEOUT);
                ret = FmIoctlsInterface::set_control(fd_driver,
                                             V4L2_CID_PRV_STATE, FM_RX);
                if (ret == FM_SUCCESS) {
                    ALOGI("Waiting for timedout or FM on\n");
                    pthread_cond_timedwait(&turn_on_cond,
                                       &mutex_turn_on_cond, &ts);
                    ALOGI("Unlocked mutex & timedout or condition satisfied\n");
                    pthread_mutex_unlock(&mutex_turn_on_cond);
                    if (cur_fm_state == FM_ON) {//after READY event
                        ret = SetBand(BAND_87500_108000);
                        if (ret != FM_SUCCESS) {
                            ALOGE("set band failed\n");
                            ret = FM_FAILURE;
                            goto exit;
                        }
                        ret = SetChannelSpacing(CHAN_SPACE_100);
                        if (ret != FM_SUCCESS) {
                            ALOGE("set channel spacing failed\n");
                            ret = FM_FAILURE;
                            goto exit;
                        }
                        ret = SetDeConstant(DE_EMP50);
                        if (ret != FM_SUCCESS) {
                            ALOGE("set Emphasis failed\n");
                            ret = FM_FAILURE;
                            goto exit;
                        }
                        thsObj.SetRxSearchAfThs(FM_PERFORMANCE_PARAMS, fd_driver);
                        SetStereo();
                        ret = TuneChannel(freq);
                        if (ret != FM_SUCCESS) {
                            ALOGI("FM set freq command failed\n");
                            ret = FM_FAILURE;
                            goto exit;
                        }

                        if (property_get_bool(FM_INTERNAL_ANTENNA_PROP, false)) {
                            ret = FmIoctlsInterface::set_control(fd_driver,
                                    V4L2_CID_PRV_ANTENNA, 1);
                            ALOGD("Internal antenna set, status : %d\n", ret);
                        }

                        return FM_SUCCESS;
                    } else { //if time out
                        ret = FM_FAILURE;
                        goto exit;
                    }
                } else {
                    ALOGE("Set FM on control failed\n");
                    pthread_mutex_unlock(&mutex_turn_on_cond);
                    ALOGI("Unlocked the FM on cond mutex\n");
                    ret = FM_FAILURE;
                    goto close_fd;
                }
            } else {
                ALOGE("FM event listener thread failed: %d\n", ret);
                set_fm_state(FM_OFF);
                return FM_FAILURE;
            }
        } else {
            ALOGE("FM event listener threadi existed\n");
            return FM_SUCCESS;
        }
    } else if(cur_fm_state != FM_ON_IN_PROGRESS) {
        return FM_SUCCESS;
    } else {
        return FM_FAILURE;
    }

exit:
    FmIoctlsInterface::set_control(fd_driver,
                                     V4L2_CID_PRV_STATE, FM_DEV_NONE);
close_fd:
    event_listener_canceled = true;
    pthread_join(event_listener_thread, NULL);
    if (strcmp(value, "rome") != 0) {
        ret = FmIoctlsInterface::close_fm_patch_dl();
        if (ret != FM_SUCCESS) {
            ALOGE("FM patch downloader close failed: %d\n", ret);
        }
    }
    close(fd_driver);
    fd_driver = -1;
    set_fm_state(FM_OFF);

    ALOGD("%s, [ret=%d]\n", __func__, ret);
    return ret;
}

int FmRadioController ::Pwr_Down()
{
    int ret = 0;
    char value[PROPERTY_VALUE_MAX] = {'\0'};

    property_get("vendor.qcom.bluetooth.soc", value, NULL);

    if((cur_fm_state != FM_OFF)) {
        Stop_Scan_Seek();
        set_fm_state(FM_OFF_IN_PROGRESS);
        FmIoctlsInterface::set_control(fd_driver,
                        V4L2_CID_PRV_STATE, FM_DEV_NONE);
    }
    if(event_listener_thread != 0) {
        ALOGD("%s, event_listener_thread cancelled\n", __func__);
        event_listener_canceled = true;
        pthread_join(event_listener_thread, NULL);
    }
    if (strcmp(value, "rome") != 0) {
        ret = FmIoctlsInterface::close_fm_patch_dl();
        if (ret != FM_SUCCESS) {
            ALOGE("FM patch downloader close failed: %d\n", ret);
        }
    }
    ALOGD("%s, [ret=%d]\n", __func__, ret);
    return ret;
}
//Tune to a Freq
//Return FM_SUCCESS on success FM_FAILURE
//on failure
int FmRadioController :: TuneChannel
(
    long freq
)
{
    int ret = FM_SUCCESS;
    struct timespec ts;

    if((cur_fm_state == FM_ON) &&
        (freq > 0)) {
        set_fm_state(FM_TUNE_IN_PROGRESS);
        ret = FmIoctlsInterface::set_freq(fd_driver,
                                             freq);
        if(ret == FM_SUCCESS) {
           ALOGI("FM set frequency command set successfully\n");
           pthread_mutex_lock(&mutex_tune_compl_cond);
           ts = set_time_out(TUNE_EVENT_TIMEOUT);
           ret = pthread_cond_timedwait(&tune_compl_cond, &mutex_tune_compl_cond, &ts);
           pthread_mutex_unlock(&mutex_tune_compl_cond);
        }else {
           if((cur_fm_state != FM_OFF)) {
              set_fm_state(FM_ON);
           }
           ALOGE("FM set freq command failed\n");
        }
    }else {
        ALOGE("Fm is not in proper state for tuning to a freq\n");
        ret = FM_FAILURE;
    }
    return ret;
}

int FmRadioController :: Seek(int dir)
{
    int ret = 0;
    int freq = -1;
    struct timespec ts;

    if (cur_fm_state != FM_ON) {
        ALOGE("%s error Fm state: %d\n", __func__, cur_fm_state);
        return FM_FAILURE;
    }

    ALOGI("FM seek started\n");
    set_fm_state(SEEK_IN_PROGRESS);
    ret = FmIoctlsInterface::set_control(fd_driver,
                                  V4L2_CID_PRV_SRCHMODE, SEEK_MODE);
    if (ret != FM_SUCCESS) {
        set_fm_state(FM_ON);
        return FM_FAILURE;
    }

    ret = FmIoctlsInterface::set_control(fd_driver,
                           V4L2_CID_PRV_SCANDWELL, SEEK_DWELL_TIME);
    if (ret != FM_SUCCESS) {
        set_fm_state(FM_ON);
        return FM_FAILURE;
    }

    if (dir == 1) {
        ret = FmIoctlsInterface::start_search(fd_driver,
                                                     SEARCH_UP);
    } else {
        ret = FmIoctlsInterface::start_search(fd_driver,
                                            SEARCH_DOWN);
    }

    if (ret != FM_SUCCESS) {
        set_fm_state(FM_ON);
        return FM_FAILURE;
    }
    pthread_mutex_lock(&mutex_seek_compl_cond);
    ts = set_time_out(SEEK_COMPL_TIMEOUT);
    ret = pthread_cond_timedwait(&seek_compl_cond, &mutex_seek_compl_cond, &ts);
    pthread_mutex_unlock(&mutex_seek_compl_cond);
    if ((cur_fm_state != SEEK_IN_PROGRESS) && !seek_scan_canceled) {
        ALOGI("Seek completed without timeout\n");
        freq = GetChannel();
    }
    seek_scan_canceled = false;
    return freq;
}

bool FmRadioController ::IsRds_support
(
    void
)
{
   is_rds_support = true;
   ALOGI("is_rds_support: %d\n", is_rds_support);
   return is_rds_support;
}

//HardMute both audio channels
int FmRadioController ::MuteOn()
{
    int ret;

    ALOGD("%s: cur_fm_state = %d\n", __func__, cur_fm_state);
    if((cur_fm_state != FM_OFF) &&
       (cur_fm_state != FM_ON_IN_PROGRESS)) {
       ret = FmIoctlsInterface::set_control(fd_driver,
                       V4L2_CID_AUDIO_MUTE, MUTE_L_R_CHAN);
       ALOGI("CMD executed mute\n");
    }else {
       ret = FM_FAILURE;
    }
    return ret;
}

//Unmute both audio channel
int FmRadioController ::MuteOff()
{
    int ret;

    ALOGD("%s: cur_fm_state = %d\n", __func__, cur_fm_state);
    if((cur_fm_state != FM_OFF) &&
       (cur_fm_state != FM_ON_IN_PROGRESS)) {
        ret = FmIoctlsInterface::set_control(fd_driver,
                      V4L2_CID_AUDIO_MUTE, UNMUTE_L_R_CHAN);
        ALOGI("CMD executed for unmute\n");
    }else {
        ret = FM_FAILURE;
    }
    return ret;
}

//
int FmRadioController ::SetSoftMute(bool mode)
{
    int ret;

    if((cur_fm_state != FM_OFF) &&
       (cur_fm_state != FM_ON_IN_PROGRESS)) {
        ret = FmIoctlsInterface::set_control(fd_driver,
                             V4L2_CID_PRV_SOFT_MUTE, mode);
    }else {
        ret = FM_FAILURE;
    }
    return ret;
}

int FmRadioController :: Set_mute(bool mute)
{
    int ret = 0;

    if (mute) {
        ret = MuteOn();
    } else {
        ret = MuteOff();
    }

    if (ret)
        ALOGE("%s failed, %d\n", __func__, ret);
    ALOGD("%s, [mute=%d] [ret=%d]\n", __func__, mute, ret);
    return ret;
}

int FmRadioController :: Stop_Scan_Seek
(
)
{
    int ret;

    ALOGD("%s: cur_fm_state = %d\n", __func__, cur_fm_state);
    if((cur_fm_state == SEEK_IN_PROGRESS) ||
       (cur_fm_state == SCAN_IN_PROGRESS)) {
        ret = FmIoctlsInterface::set_control(fd_driver,
                                       V4L2_CID_PRV_SRCHON, 0);
        if (ret == FM_SUCCESS) {
            ALOGI("FM Seek cancel command set successfully\n");
            seek_scan_canceled = true;
        } else {
            ALOGE("FM Seek cancel command sent failed\n");
        }
    } else {
        ALOGE("FM is not in proper state for cancelling Seek operation\n");
        ret = FM_FAILURE;
    }
    return ret;
}

int FmRadioController :: ReadRDS() //todo define each RDS flag
{
   int ret = 0;

   if (is_ps_event_received)
       ret |= RDS_EVT_PS_UPDATE;
   if (is_rt_event_received)
       ret |= RDS_EVT_RT_UPDATE;
   if (is_af_jump_received)
       ret |= RDS_EVT_AF_JUMP;

   return ret;
}

int FmRadioController :: Get_ps(char *ps, int *ps_len)
{
    int ret = 0;
    int len = 0;
    char raw_rds[STD_BUF_SIZE];

    ret = FmIoctlsInterface::get_buffer(fd_driver,
                                    raw_rds, STD_BUF_SIZE, PS_IND);
    if (ret <= 0) {
        return FM_FAILURE;
    } else {
        if (raw_rds[PS_STR_NUM_IND] > 0) {
            if (ps != NULL) {
                for(int i = 0; i < MAX_PS_LEN; i++) {
                    ps[i] = raw_rds[PS_DATA_OFFSET_IND + i];
                    if (ps[i] == 0) {
                        break;
                    } else if((ps[len] <= LAST_CTRL_CHAR) ||
                              (ps[len] >= FIRST_NON_PRNT_CHAR)) {
                        ps[i] = SPACE_CHAR;
                        continue;
                    }
                    len++;
                }
                if (len < (MAX_PS_LEN - 1)) {
                    ps[len] = '\0';
                    *ps_len = len + 1;
                } else {
                    *ps_len = len;
                }
                ALOGI("PS is: %s\n", ps);
            } else {
                return FM_FAILURE;
            }
        }
    }
    is_ps_event_received = false;
    ALOGD("%s, [ps_len=%d]\n", __func__, *ps_len);
    return FM_SUCCESS;
}

int FmRadioController :: Get_rt(char *rt, int *rt_len)
{
    int ret = 0;
    int len = 0;
    char raw_rds[STD_BUF_SIZE];

    ret = FmIoctlsInterface::get_buffer(fd_driver,
                               raw_rds, STD_BUF_SIZE, RT_IND);
    if (ret <= 0) {
        return FM_FAILURE;
    } else {
        if (rt != NULL) {
            if ((raw_rds[RT_LEN_IND] > 0) &&
                (raw_rds[RT_LEN_IND] <= MAX_RT_LEN)) {
                for(len = 0; len < raw_rds[RT_LEN_IND]; len++) {
                   rt[len] = raw_rds[RT_DATA_OFFSET_IND + len];
                   ALOGI("Rt byte[%d]: %d\n", len, rt[len]);
                   if ((rt[len] <= LAST_CTRL_CHAR) ||
                       (rt[len] >= FIRST_NON_PRNT_CHAR)) {
                       rt[len] = SPACE_CHAR;
                       continue;
                   }
                }
                if (len < (MAX_RT_LEN - 1)) {
                    rt[len] = '\0';
                    *rt_len = len + 1;
                } else {
                    *rt_len = len;
                }
                ALOGI("Rt is: %s\n", rt);
                ALOGI("RT text A / B: %d\n", raw_rds[RT_A_B_FLAG_IND]);
            } else {
                return FM_FAILURE;
            }
        } else {
            return FM_FAILURE;
        }
    }
    is_rt_event_received = false;
    ALOGD("%s, [rt_len=%d]\n", __func__, *rt_len);
    return FM_SUCCESS;
}

int FmRadioController :: Get_AF_freq(uint16_t *ret_freq)
{
    int ret =0;
    ULINT lowBand, highBand;
    float real_freq = 0;

    ALOGI("get_AF_freq\n");
    ret = FmIoctlsInterface::get_lowerband_limit(fd_driver,
                                                         lowBand);
    if (ret != FM_SUCCESS) {
        ALOGE("failed to get lowerband: %d\n", ret);
        return FM_FAILURE;
    }
    ALOGI("lowBand = %ld\n",lowBand);
    ret = FmIoctlsInterface::get_upperband_limit(fd_driver,
                                                      highBand);
    if (ret != FM_SUCCESS) {
        ALOGE("failed to getgherband: %d\n", ret);
        return FM_FAILURE;
    }
    ALOGI("highBand = %ld\n",highBand);
    real_freq = GetChannel();
    if ((real_freq < lowBand ) || (real_freq > highBand)) {
        ALOGE("AF freq is not in band limits\ni");
        return FM_FAILURE;
    } else {
        *ret_freq = real_freq/100;
    }
    is_af_jump_received = false;
    return FM_SUCCESS;
}

//Emphasis:
//75microsec: 0, 50 microsec: 1
//return FM_SUCCESS on success, FM_FAILURE
//on failure
int FmRadioController :: SetDeConstant
(
    long emphasis
)
{
    int ret;

    ALOGD("cur_fm_state: %d, emphasis: %ld\n", cur_fm_state, emphasis);
    if(cur_fm_state == FM_ON) {
        switch(emphasis) {
            case DE_EMP75:
            case DE_EMP50:
                ret = FmIoctlsInterface::set_control(fd_driver,
                       V4L2_CID_PRV_EMPHASIS, emphasis);
                break;
            default:
                ALOGE("FM value pass for set Deconstant is invalid\n");
                ret = FM_FAILURE;
                break;
        }
    }else {
        ALOGE("FM is not in proper state to set De constant\n");
        ret = FM_FAILURE;
    }
    return ret;
}

int FmRadioController :: GetStationList
(
    uint16_t *scan_tbl, int *max_cnt
)
{
    char srch_list[STD_BUF_SIZE];
    int ret;
    ULINT lowBand, highBand;
    int station_num = 0;
    int tmpFreqByte1=0;
    int tmpFreqByte2=0;
    int freq = 0;
    float real_freq = 0;
    int i = 0, j = 0;

    ALOGI("getstationList\n");
    ret = FmIoctlsInterface::get_lowerband_limit(fd_driver,
                                                         lowBand);
    if (ret != FM_SUCCESS) {
        ALOGE("failed to get lowerband: %d\n", ret);
        return FM_FAILURE;
    }
    ALOGI("lowBand = %ld\n",lowBand);
    ret = FmIoctlsInterface::get_upperband_limit(fd_driver,
                                                      highBand);
    if (ret != FM_SUCCESS) {
        ALOGE("failed to getgherband: %d\n", ret);
        return FM_FAILURE;
    }
    ALOGI("highBand = %ld\n",highBand);
    ret = FmIoctlsInterface::get_buffer(fd_driver,
                          srch_list, STD_BUF_SIZE, STATION_LIST_IND);
    if ((int)srch_list[0] >0) {
        station_num = (int)srch_list[0];
    }
    ALOGI("station_num: %d ", station_num);
    *max_cnt = station_num;
    for (i=0;i<station_num;i++) {
        freq = 0;
        ALOGI(" Byte1 = %d", srch_list[i * NO_OF_BYTES_EACH_FREQ + 1]);
        ALOGI(" Byte2 = %d", srch_list[i * NO_OF_BYTES_EACH_FREQ + 2]);
        tmpFreqByte1 = srch_list[i * NO_OF_BYTES_EACH_FREQ + 1] & 0xFF;
        tmpFreqByte2 = srch_list[i * NO_OF_BYTES_EACH_FREQ + 2] & 0xFF;
        ALOGI(" tmpFreqByte1 = %d", tmpFreqByte1);
        ALOGI(" tmpFreqByte2 = %d", tmpFreqByte2);
        freq = (tmpFreqByte1 & EXTRACT_FIRST_BYTE) << 8;
        freq |= tmpFreqByte2;
        ALOGI(" freq: %d", freq);
        real_freq  = (freq * FREQ_MULTIPLEX) + lowBand;
        ALOGI("real_freq: %f", real_freq);
        if ( (real_freq < lowBand ) || (real_freq > highBand) ) {
              ALOGI("Frequency out of band limits");
        } else {
            scan_tbl[j] = (real_freq/SRCH_DIV);
            ALOGI(" scan_tbl: %d", scan_tbl[j]);
            j++;
        }
    }
    return FM_SUCCESS;
}

int FmRadioController ::ScanList
(
    uint16_t *scan_tbl, int *max_cnt
)
{
    int ret;
    struct timespec ts;

    /* Check current state of FM device */
    if (cur_fm_state == FM_ON) {
        ALOGI("FM searchlist started\n");
        set_fm_state(SCAN_IN_PROGRESS);
        ret = FmIoctlsInterface::set_control(fd_driver,
                           V4L2_CID_PRV_SRCHMODE, SRCHLIST_MODE_STRONG);
        if (ret != FM_SUCCESS) {
            set_fm_state(FM_ON);
            return FM_FAILURE;
        }
        ret = FmIoctlsInterface::start_search(fd_driver,
                                                     SEARCH_UP);
        if (ret != FM_SUCCESS) {
            set_fm_state(FM_ON);
            return FM_FAILURE;
        }
        pthread_mutex_lock(&mutex_scan_compl_cond);
        ts = set_time_out(SCAN_COMPL_TIMEOUT);
        ALOGI("Wait for Scan Timeout or scan complete");
        ret = pthread_cond_timedwait(&scan_compl_cond, &mutex_scan_compl_cond, &ts);
        ALOGI("Scan complete or timedout");
        pthread_mutex_unlock(&mutex_scan_compl_cond);
        if (cur_fm_state == FM_ON && !seek_scan_canceled) {
            GetStationList(scan_tbl, max_cnt);
        } else {
            seek_scan_canceled = false;
            return FM_FAILURE;
        }
    } else {
        ALOGI("Scanlist: not proper state %d\n", cur_fm_state);
        return FM_FAILURE;
    }
    return FM_SUCCESS;
}

long FmRadioController :: GetCurrentRSSI
(
    void
)
{
    int ret;
    long rmssi = -129;

    if((cur_fm_state != FM_OFF) &&
       (cur_fm_state != FM_ON_IN_PROGRESS)) {
        ret = FmIoctlsInterface::get_rmssi(fd_driver, rmssi);
    }else {
    }
    return rmssi;
}

//enable, disable value to receive data of a RDS group
//return FM_SUCCESS on success, FM_FAILURE on failure
int FmRadioController :: SetRdsGrpProcessing
(
    int grps
)
{
    int ret;
    long mask;

    if(cur_fm_state == FM_ON) {
       ret = FmIoctlsInterface::get_control(fd_driver,
                     V4L2_CID_PRV_RDSGROUP_PROC, mask);
       if(ret != FM_SUCCESS) {
          return ret;
       }
       ALOGD("%s: mask group: %d\n", __func__, grps);
       mask = ((grps & MASK_PI_LSB));
       ALOGD("%s: new mask group: %ld\n", __func__, mask);
       ret = FmIoctlsInterface::set_control(fd_driver,
                    V4L2_CID_PRV_RDSGROUP_PROC, (int)mask);
    }else {
       ret = FM_FAILURE;
    }
    return ret;
}

//Enable RDS data receiving
//Enable RT, PS, AF Jump, RTPLUS, ERT etc
int FmRadioController :: EnableRDS
(
    void
)
{
    int ret = FM_FAILURE;

    ALOGD("%s: cur_fm_state = %d\n", __func__, cur_fm_state);
    if (cur_fm_state == FM_ON) {
        ret = FmIoctlsInterface::set_control(fd_driver,
                      V4L2_CID_PRV_RDSON, 1);
        if (ret != FM_SUCCESS) {
            ALOGE("RDS ON failed\n");
            return ret;
        }
        ret = SetRdsGrpProcessing(FM_RX_RDS_GRP_RT_EBL |
                                  FM_RX_RDS_GRP_PS_EBL |
                                  FM_RX_RDS_GRP_AF_EBL |
                                  FM_RX_RDS_GRP_PS_SIMPLE_EBL |
                                  FM_RX_RDS_GRP_ECC_EBL |
                                  FM_RX_RDS_GRP_RT_PLUS_EBL);
        if (ret != FM_SUCCESS) {
            ALOGE("Set RDS grp processing\n");
            return ret;
        }
        ret = FM_SUCCESS;
        rds_enabled = 1;
        EnableAF();
    } else {
        ALOGE("%s: not in proper state cur_fm_state = %d\n", __func__, cur_fm_state);
        return ret;
    }
    return ret;
}

//Disable all RDS data processing
//RT, ERT, RT PLUS, PS
int FmRadioController :: DisableRDS
(
    void
)
{
    int ret = FM_FAILURE;

    ALOGD("%s: cur_fm_state = %d\n", __func__, cur_fm_state);
    if (cur_fm_state == FM_ON) {
        ret = FmIoctlsInterface::set_control(fd_driver,
                      V4L2_CID_PRV_RDSON, 0);
        if (ret != FM_SUCCESS) {
            ALOGE("Disable RDS failed\n");
            return ret;
        }
        ret = FM_SUCCESS;
        rds_enabled = 0;
        DisableAF();
    } else {
        ALOGE("%s: not in proper state cur_fm_state = %d\n", __func__, cur_fm_state);
        ret = FM_FAILURE;
    }
    return ret;
}

int FmRadioController :: Turn_On_Off_Rds(bool onoff)
{
    int ret = 0;

    if (onoff) {
        ret = EnableRDS();
    } else {
        ret = DisableRDS();
    }

    if (ret) {
        ALOGE("%s, failed\n", __func__);
    }
    ALOGD("%s, [onoff=%d] [ret=%d]\n", __func__, onoff, ret);
    return ret;
}

//Enables Alternate Frequency switching
int FmRadioController :: EnableAF
(
    void
)
{
    int ret;
    long rdsgrps;

    if(cur_fm_state == FM_ON) {
        ret = FmIoctlsInterface::get_control(fd_driver,
                      V4L2_CID_PRV_RDSGROUP_PROC, rdsgrps);
        ret = FmIoctlsInterface::set_control(fd_driver,
                      V4L2_CID_PRV_RDSON, 1);
        if(ret == FM_SUCCESS) {
            ret = FmIoctlsInterface::set_control(fd_driver,
                      V4L2_CID_PRV_AF_JUMP, 1);
            if(ret == FM_SUCCESS) {
               af_enabled = 1;
            }
        } else {
        }
    } else {
        ret = FM_FAILURE;
    }
    return ret;
}

//Disables Alternate Frequency switching
int FmRadioController :: DisableAF
(
    void
)
{
    int ret;
    long rdsgrps;

    if(cur_fm_state == FM_ON) {
        ret = FmIoctlsInterface::get_control(fd_driver,
                      V4L2_CID_PRV_RDSGROUP_PROC, rdsgrps);
        if(ret == FM_SUCCESS) {
            ret = FmIoctlsInterface::set_control(fd_driver,
                      V4L2_CID_PRV_AF_JUMP, 0);
            if(ret == FM_SUCCESS) {
               af_enabled = 0;
            }
        }else {
        }
    }else {
        ret = FM_FAILURE;
    }
    return ret;
}

//Set regional band
int FmRadioController :: SetBand
(
    long band
)
{
    int ret;

    if(cur_fm_state == FM_ON) {
        switch(band) {
            case BAND_87500_108000:
                ret = FmIoctlsInterface::set_band(fd_driver,
                               87500, 108000);
                break;
            case BAND_76000_108000:
                ret = FmIoctlsInterface::set_band(fd_driver,
                               76000, 108000);
                break;
            case BAND_76000_90000:
                ret = FmIoctlsInterface::set_band(fd_driver,
                               76000, 90000);
                break;
            default:
                ALOGE("Band type: %ld is invalid\n", band);
                ret = FM_FAILURE;
                break;
        }
    }else {
        ALOGE("FM is not in proper state to set band type\n");
        ret = FM_FAILURE;
    }
    return ret;
}

//set spacing for successive channels
int FmRadioController :: SetChannelSpacing
(
    long spacing
)
{
    int ret;

    if (cur_fm_state == FM_ON) {
        ret = FmIoctlsInterface::set_control(fd_driver,
                               V4L2_CID_PRV_CHAN_SPACING, spacing);
    } else {
        ALOGE("FM is not in proper state to set the channel spacing\n");
        ret = FM_FAILURE;
    }
    return ret;
}

int FmRadioController :: SetStereo
(
)
{
    int ret;

    if((cur_fm_state != FM_OFF) &&
       (cur_fm_state != FM_ON_IN_PROGRESS)) {
       ret = FmIoctlsInterface::set_audio_mode(fd_driver,
                                           STEREO);
    }else {
       ret = FM_FAILURE;
    }
    return ret;
}

int FmRadioController :: SetMono
(
)
{
    int ret;

    if((cur_fm_state != FM_OFF) &&
       (cur_fm_state != FM_ON_IN_PROGRESS)) {
       ret = FmIoctlsInterface::set_audio_mode(fd_driver,
                                              MONO);
    }else {
       ret = FM_FAILURE;
    }
    return ret;
}

bool FmRadioController :: GetSoftMute
(
)
{
    int ret = FM_SUCCESS;
    long mode = SMUTE_DISABLED;

    if((cur_fm_state != FM_OFF) &&
       (cur_fm_state != FM_ON_IN_PROGRESS)) {
       ret = FmIoctlsInterface::get_control(fd_driver,
                            V4L2_CID_PRV_SOFT_MUTE, mode);
       if(ret == FM_SUCCESS) {
          ALOGI("FM Get soft mute is successful: %ld\n", mode);
       }else {
          ALOGE("FM Get soft mute failed");
       }
    }else {
       ALOGE("FM is not in proper state for getting soft mute\n");
       ret = FM_FAILURE;
    }
    return mode;
}

int FmRadioController :: Antenna_Switch(int antenna)
{
    int ret = 0;

    if (antenna) {
        ret = FmIoctlsInterface::set_control(fd_driver,
                                     V4L2_CID_PRV_ANTENNA, 1);
    } else {
        ret = FmIoctlsInterface::set_control(fd_driver,
                                     V4L2_CID_PRV_ANTENNA, 0);
    }
    ALOGD("%s, antenna type = %d [ret=%d]\n", __func__, antenna, ret);
    return ret;
}

int FmRadioController :: Set_Power_Mode(bool isNormalMode)
{
    int ret = 0;

    if (isNormalMode) {
        ret = FmIoctlsInterface::set_control(fd_driver,
                                     V4L2_CID_PRV_LP_MODE, 0);
    } else {
        ret = FmIoctlsInterface::set_control(fd_driver,
                                     V4L2_CID_PRV_LP_MODE, 1);
    }
    ALOGI("%s, enabled %s power mode [ret=%d]\n", __func__,
            (isNormalMode ? "normal" : "low"), ret);
    return ret;
}

int FmRadioController :: get_fm_state
(
)
{
    return cur_fm_state;
}

void FmRadioController :: set_fm_state
(
    int state
)
{
    pthread_mutex_lock(&mutex_fm_state);
    cur_fm_state = state;
    pthread_mutex_unlock(&mutex_fm_state);
}

void* FmRadioController :: handle_events
(
    void *arg
)
{
    int bytesread;
    char event_buff[STD_BUF_SIZE];
    bool status = true;
    FmRadioController *obj_p = static_cast<FmRadioController*>(arg);

    while(status && !obj_p->event_listener_canceled) {
        bytesread = FmIoctlsInterface::get_buffer(obj_p->fd_driver,
                      event_buff, STD_BUF_SIZE, EVENT_IND);
        for(int i = 0; i < bytesread; i++) {
            status = obj_p->process_radio_events(event_buff[i]);
            if(status == false) {
                break;
            }
        }
    }
    return NULL;
}

int FmRadioController :: SetRdsGrpMask
(
    int mask
)
{
    int ret;

    if((cur_fm_state != FM_OFF) &&
       (cur_fm_state != FM_OFF_IN_PROGRESS) &&
       (cur_fm_state != FM_ON_IN_PROGRESS)) {
        ret = FmIoctlsInterface::set_control(fd_driver,
                      V4L2_CID_PRV_RDSGROUP_MASK, mask);
    }else {
        ret = FM_FAILURE;
    }
    return ret;
}

void FmRadioController :: handle_enabled_event
(
     void
)
{
     char value[PROPERTY_VALUE_MAX] = {'\0'};

     ALOGI("FM handle ready Event\n");
     FmIoctlsInterface::set_control(fd_driver,
             V4L2_CID_PRV_AUDIO_PATH, AUDIO_DIGITAL_PATH);
     property_get("vendor.qcom.bluetooth.soc", value, NULL);
     if (strcmp(value, "rome") != 0) {
         FmIoctlsInterface::set_calibration(fd_driver);
     }
     pthread_mutex_lock(&mutex_turn_on_cond);
     set_fm_state(FM_ON);
     pthread_cond_broadcast(&turn_on_cond);
     pthread_mutex_unlock(&mutex_turn_on_cond);
}

void FmRadioController :: handle_tuned_event
(
     void
)
{
     long freq = -1;

     ALOGI("FM handle Tune event\n");
     freq = GetChannel();
     switch(cur_fm_state) {
         case FM_ON:
            if(af_enabled && (freq != prev_freq)
                && (prev_freq > 0)) {
               ALOGI("AF jump happened\n");
               is_af_jump_received = true;
            }
            break;
         case FM_TUNE_IN_PROGRESS:
            pthread_mutex_lock(&mutex_tune_compl_cond);
            set_fm_state(FM_ON);
            pthread_cond_broadcast(&tune_compl_cond);
            pthread_mutex_unlock(&mutex_tune_compl_cond);
            break;
         case SEEK_IN_PROGRESS:
            pthread_mutex_lock(&mutex_seek_compl_cond);
            set_fm_state(FM_ON);
            pthread_cond_broadcast(&seek_compl_cond);
            pthread_mutex_unlock(&mutex_seek_compl_cond);
            break;
         case SCAN_IN_PROGRESS:
            break;
     }
     prev_freq = freq;
}

void FmRadioController :: handle_seek_next_event
(
     void
)
{
     ALOGI("FM handle seek next event\n");
}

void FmRadioController :: handle_seek_complete_event
(
     void
)
{
     ALOGI("FM handle seek complete event\n");
}

void FmRadioController :: handle_raw_rds_event
(
     void
)
{

}

void FmRadioController :: handle_rt_event
(
     void
)
{
     ALOGI("FM handle RT event\n");
     is_rt_event_received = true;
}

void FmRadioController :: handle_ps_event
(
    void
)
{
    ALOGI("FM handle PS event\n");
    is_ps_event_received = true;
}

void FmRadioController :: handle_error_event
(
   void
)
{

}

void FmRadioController :: handle_below_th_event
(
   void
)
{

}

void FmRadioController :: handle_above_th_event
(
   void
)
{

}

void FmRadioController :: handle_stereo_event
(
   void
)
{

}
void FmRadioController :: handle_mono_event
(
  void
)
{

}

void FmRadioController :: handle_rds_aval_event
(
  void
)
{
    ALOGI("Got rds_aval_event\n");
    is_rds_support = true;
}

void FmRadioController :: handle_rds_not_aval_event
(
  void
)
{
    ALOGI("Got rds_not_aval_event\n");
}

void FmRadioController :: handle_srch_list_event
(
  void
)
{
    ALOGI("Got srch list event\n");
    if (cur_fm_state == SCAN_IN_PROGRESS) {
        pthread_mutex_lock(&mutex_scan_compl_cond);
        set_fm_state(FM_ON);
        pthread_cond_broadcast(&scan_compl_cond);
        pthread_mutex_unlock(&mutex_scan_compl_cond);
    }
}

void FmRadioController :: handle_af_list_event
(
  void
)
{
    char raw_rds[STD_BUF_SIZE];
    int ret;
    int aflist_size;
    ULINT lower_band;
    int AfList[MAX_AF_LIST_SIZE];

    ALOGI("Got af list event\n");
    ret = FmIoctlsInterface::get_buffer(fd_driver,
                     raw_rds, STD_BUF_SIZE, AF_LIST_IND);
    lower_band = FmIoctlsInterface::get_lowerband_limit(fd_driver,
                     lower_band);
    ALOGI("raw_rds[0]: %d\n", (raw_rds[0] & 0xff));
    ALOGI("raw_rds[1]: %d\n", (raw_rds[1] & 0xff));
    ALOGI("raw_rds[2]: %d\n", (raw_rds[2] & 0xff));
    ALOGI("raw_rds[3]: %d\n", (raw_rds[3] & 0xff));
    ALOGI("raw_rds[4]: %d\n", (raw_rds[4] & 0xff));
    ALOGI("raw_rds[5]: %d\n", (raw_rds[5] & 0xff));
    ALOGI("raw_rds[6]: %d\n", (raw_rds[6] & 0xff));

    aflist_size = raw_rds[AF_SIZE_IDX] & 0xff;
    for(int i = 0; i < aflist_size; i++) {
       AfList[i] = (raw_rds[AF_SIZE_IDX + i * NO_OF_BYTES_AF + 1] & 0xFF) |
                   ((raw_rds[AF_SIZE_IDX + i * NO_OF_BYTES_AF + 2] & 0xFF) << 8) |
                   ((raw_rds[AF_SIZE_IDX + i * NO_OF_BYTES_AF + 3] & 0xFF) << 16) |
                   ((raw_rds[AF_SIZE_IDX + i * NO_OF_BYTES_AF + 4] & 0xFF) << 24);
       ALOGI("AF: %d\n", AfList[i]);
    }
}

void FmRadioController :: handle_disabled_event
(
  void
)
{
     //Expected disabled
     if(cur_fm_state == FM_OFF_IN_PROGRESS) {
        ALOGI("Expected disabled event\n");
     }else {//Enexpected disabled
        ALOGI("Unexpected disabled event\n");
     }

     set_fm_state(FM_OFF);
     close(fd_driver);
     fd_driver = -1;

     //allow tune function to exit
     pthread_mutex_lock(&mutex_tune_compl_cond);
     pthread_cond_broadcast(&tune_compl_cond);
     pthread_mutex_unlock(&mutex_tune_compl_cond);
     //allow scan function to exit
     pthread_mutex_lock(&mutex_scan_compl_cond);
     pthread_cond_broadcast(&scan_compl_cond);
     pthread_mutex_unlock(&mutex_scan_compl_cond);
     //Allow seek function to exit
     pthread_mutex_lock(&mutex_seek_compl_cond);
     pthread_cond_broadcast(&seek_compl_cond);
     pthread_mutex_unlock(&mutex_seek_compl_cond);
}

void FmRadioController :: handle_rds_grp_mask_req_event
(
    void
)
{
    SetRdsGrpMask(0);
}

void FmRadioController :: handle_rt_plus_event
(
    void
)
{
    ALOGI("FM handle RT Plus event\n");
}

void FmRadioController :: handle_af_jmp_event
(
    void
)
{
    long freq = -1;

    freq = GetChannel();
    ALOGI("FM handle AF Jumped event\n");
    if(af_enabled && (freq != prev_freq)) {
       ALOGI("AF Jump occured, prevfreq is: %ld, af freq is: %ld\n", prev_freq, freq);
    }
    prev_freq = freq;
}

void FmRadioController :: handle_ert_event
(
    void
)
{
    ALOGI("FM handle ERT event\n");
}

bool FmRadioController :: process_radio_events
(
    int event
)
{
    bool ret = true;

    switch(event) {
        case READY_EVENT:
            handle_enabled_event();
            break;
        case TUNE_EVENT:
            handle_tuned_event();
            break;
        case SEEK_COMPLETE_EVENT:
            handle_seek_complete_event();
            break;
        case SCAN_NEXT_EVENT:
            handle_seek_next_event();
            break;
        case RAW_RDS_EVENT:
            handle_raw_rds_event();
            break;
        case RT_EVENT:
            handle_rt_event();
            break;
        case PS_EVENT:
            handle_ps_event();
            break;
        case ERROR_EVENT:
            handle_error_event();
            break;
        case BELOW_TH_EVENT:
            handle_below_th_event();
            break;
        case ABOVE_TH_EVENT:
            handle_above_th_event();
            break;
        case STEREO_EVENT:
            handle_stereo_event();
            break;
        case MONO_EVENT:
            handle_mono_event();
            break;
        case RDS_AVAL_EVENT:
            handle_rds_aval_event();
            break;
        case RDS_NOT_AVAL_EVENT:
            handle_rds_not_aval_event();
            break;
        case SRCH_LIST_EVENT:
            handle_srch_list_event();
            break;
        case AF_LIST_EVENT:
            handle_af_list_event();
            break;
        case DISABLED_EVENT:
            handle_disabled_event();
            ret = false;
            break;
        case RDS_GRP_MASK_REQ_EVENT:
            handle_rds_grp_mask_req_event();
            break;
        case RT_PLUS_EVENT:
            handle_rt_plus_event();
            break;
        case ERT_EVENT:
            handle_ert_event();
            break;
        case AF_JMP_EVENT:
            handle_af_jmp_event();
            break;
        default:
            break;
    }
    return ret;
}
