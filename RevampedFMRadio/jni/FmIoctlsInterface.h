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

#ifndef __FM_IOCTL_INTERFACE_H__
#define __FM_IOCTL_INTERFACE_H__

#include "FM_Const.h"

#include <linux/videodev2.h>

class FmIoctlsInterface
{
    public:
        static int start_fm_patch_dl(UINT fd);
        static int close_fm_patch_dl(void);
        static int get_cur_freq(UINT fd, long &freq);
        static int set_freq(UINT fd, ULINT freq);
        static int set_control(UINT fd, UINT id, int val);
        static int set_calibration(UINT fd);
        static int get_control(UINT fd, UINT id, long &val);
        static int start_search(UINT fd, UINT dir);
        static int set_band(UINT fd, ULINT low, ULINT high);
        static int get_upperband_limit(UINT fd, ULINT &freq);
        static int get_lowerband_limit(UINT fd, ULINT &freq);
        static int set_audio_mode(UINT fd, enum AUDIO_MODE mode);
        static int get_buffer(UINT fd, char *buff, UINT len, UINT index);
        static int get_rmssi(UINT fd, long &rmssi);
        static int set_ext_control(UINT fd, struct v4l2_ext_controls *v4l2_ctls);
};

#endif //__FM_IOCTL_INTERFACE_H__
