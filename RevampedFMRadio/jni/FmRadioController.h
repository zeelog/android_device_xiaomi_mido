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

#ifndef __FM_RADIO_CTRL_H__
#define __FM_RADIO_CTRL_H__

#include <pthread.h>
#include <ctime>

class FmRadioController
{
    private:
        int cur_fm_state;
        char af_enabled;
        bool seek_scan_canceled;
        bool is_rds_support;
        bool is_ps_event_received = false;
        bool is_rt_event_received = false;
        bool is_af_jump_received = false;
        bool event_listener_canceled;
        pthread_mutex_t mutex_fm_state;
        pthread_mutex_t mutex_turn_on_cond;
        pthread_mutex_t mutex_seek_compl_cond;
        pthread_mutex_t mutex_scan_compl_cond;
        pthread_mutex_t mutex_tune_compl_cond;
        pthread_cond_t turn_on_cond;
        pthread_cond_t seek_compl_cond;
        pthread_cond_t scan_compl_cond;
        pthread_cond_t tune_compl_cond;
        char rds_enabled;
        long int prev_freq;
        int fd_driver;
        pthread_t event_listener_thread;
        int SetRdsGrpMask(int mask);
        int SetRdsGrpProcessing(int grps);
        void handle_enabled_event(void);
        void handle_tuned_event(void);
        void handle_seek_next_event(void);
        void handle_seek_complete_event(void);
        void handle_raw_rds_event(void);
        void handle_rt_event(void);
        void handle_ps_event(void);
        void handle_error_event(void);
        void handle_below_th_event(void);
        void handle_above_th_event(void);
        void handle_stereo_event(void);
        void handle_mono_event(void);
        void handle_rds_aval_event(void);
        void handle_rds_not_aval_event(void);
        void handle_srch_list_event(void);
        void handle_af_list_event(void);
        void handle_disabled_event(void);
        void handle_rds_grp_mask_req_event(void);
        void handle_rt_plus_event(void);
        void handle_ert_event(void);
        void handle_af_jmp_event(void);
        void set_fm_state(int state);
        struct timespec set_time_out(int secs);
        int GetStationList(uint16_t *scan_tbl, int *max_cnt);
        int EnableRDS(void);
        int DisableRDS(void);
        int EnableAF(void);
        int DisableAF(void);
        int SetStereo(void);
        int SetMono(void);
        int MuteOn(void);
        int MuteOff(void);
        int get_fm_state(void);
        long GetCurrentRSSI(void);
        bool GetSoftMute(void);
    public:
       FmRadioController();
       ~FmRadioController();
       int open_dev(void);
       int close_dev();
       int Pwr_Up(int freq);
       int Pwr_Down(void);
       long GetChannel(void);
       int TuneChannel(long);
       bool IsRds_support();
       int ScanList(uint16_t *scan_tbl, int *max_cnt);
       int Seek(int dir);
       int ReadRDS(void);
       int Get_ps(char *ps, int *ps_len);
       int Get_rt(char *rt, int *rt_len);
       int Get_AF_freq(uint16_t *ret_freq);
       int SetDeConstant(long );
       int SetSoftMute(bool mode);
       int Set_mute(bool mute);
       int SetBand(long);
       int SetChannelSpacing(long);
       int Stop_Scan_Seek(void);
       int Turn_On_Off_Rds(bool onoff);
       int Antenna_Switch(int antenna);
       int Set_Power_Mode(bool isNormalMode);
       static void* handle_events(void *arg);
       bool process_radio_events(int event);
};

#endif
