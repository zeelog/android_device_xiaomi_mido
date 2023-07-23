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

#ifndef __CONFIG_FM_THS_H__
#define __CONFIG_FM_THS_H__

#include <cstring>
#include "FM_Const.h"
#include "ConfFileParser.h"

#define MAX_GRPS 4
#define MAX_SRCH_PARAMS 8
#define MAX_AF_PARAMS 3
#define MAX_BAND_PARAMS 3

#define SINR_SAMPLES_CNT_MIN 0
#define SINR_SAMPLES_CNT_MAX 255
#define SINR_FIRST_STAGE_MIN -128
#define SINR_FIRST_STAGE_MAX 127
#define RMSSI_FIRST_STAGE_MIN -128
#define RMSSI_FIRST_STAGE_MAX 127
#define INTF_LOW_TH_MIN 0
#define INTF_LOW_TH_MAX  255
#define INTF_HIGH_TH_MIN 0
#define INTF_HIGH_TH_MAX 255
#define SRCH_ALGO_TYPE_MIN 0
#define SRCH_ALGO_TYPE_MAX 1
#define SINR_FINAL_STAGE_MIN -128
#define SINR_FINAL_STAGE_MAX 127

#define AF_RMSSI_TH_MIN 0
#define AF_RMSSI_TH_MAX 65535
#define AF_RMSSI_SAMPLES_MIN 0
#define AF_RMSSI_SAMPLES_MAX 255
#define GOOD_CH_RMSSI_TH_MIN -128
#define GOOD_CH_RMSSI_TH_MAX 127
#define FM_DE_EMP75  0
#define FM_DE_EMP50  1
#define FM_CHSPACE_200_KHZ  0
#define FM_CHSPACE_100_KHZ  1
#define FM_CHSPACE_50_KHZ  2

const unsigned char MAX_HYBRID_SRCH_PARAMS = 2;

struct NAME_MAP
{
   const char name[50];
   const int num;
};

enum PERFORMANCE_GRPS
{
    AF_THS,
    SRCH_THS,
    HYBRD_SRCH_LIST,
    BAND_CFG,
};

enum BAND_CFG_PARAMS
{
    RADIO_BAND,
    EMPHASIS,
    CHANNEL_SPACING,
};

enum PERFORMANCE_SRCH_PARAMS
{
    SRCH_ALGO_TYPE,
    CF0_TH,
    SINR_FIRST_STAGE,
    SINR,
    RMSSI_FIRST_STAGE,
    INTF_LOW_TH,
    INTF_HIGH_TH,
    SINR_SAMPLES,
};

enum PERFORMANCE_AF_PARAMS
{
    AF_RMSSI_TH,
    AF_RMSSI_SAMPLES,
    GOOD_CH_RMSSI_TH,
};

enum HYBRID_SRCH_PARAMS
{
    FREQ_LIST,
    SINR_LIST,
};

//Keep this list in sorted order (ascending order in terms of "name")
//Don't change the name of GRPS, if changed please also change accordingly
//file: fm_srch_af_th.conf
static struct NAME_MAP GRPS_MAP[] =
{
   {"AFTHRESHOLDS", AF_THS},
   {"BANDCONFIG", BAND_CFG},
   {"HYBRIDSEARCHLIST", HYBRD_SRCH_LIST},
   {"SEARCHTHRESHOLDS", SRCH_THS},
};

static struct NAME_MAP BAND_CFG_MAP[] =
{
    {"ChSpacing", CHANNEL_SPACING},
    {"Emphasis", EMPHASIS},
    {"RadioBand", RADIO_BAND},
};

//Keep this list in sorted order (ascending order in terms of "name")
//Don't change the name of SEARCH thresholds,
//if changed please also change accordingly
//file: fm_srch_af_th.conf
static struct NAME_MAP SEACH_PARAMS_MAP[] =
{
   {"Cf0Th12", CF0_TH},
   {"IntfHighTh", INTF_HIGH_TH},
   {"IntfLowTh", INTF_LOW_TH},
   {"RmssiFirstStage", RMSSI_FIRST_STAGE},
   {"SearchAlgoType", SRCH_ALGO_TYPE},
   {"Sinr", SINR},
   {"SinrFirstStage", SINR_FIRST_STAGE},
   {"SinrSamplesCnt", SINR_SAMPLES},
};

//Keep this list in sorted order (ascending order in terms of "name")
//Don't change the name of SEARCH thresholds,
//if changed please also change accordingly
//file: fm_srch_af_th.conf
static struct NAME_MAP AF_PARAMS_MAP[] =
{
   {"AfRmssiSamplesCnt", AF_RMSSI_SAMPLES},
   {"AfRmssiTh", AF_RMSSI_TH},
   {"GoodChRmssiTh", GOOD_CH_RMSSI_TH},
};

static struct NAME_MAP HYBRD_SRCH_MAP[] =
{
   {"Freqs", FREQ_LIST},
   {"Sinrs", SINR_LIST},
};

class ConfigFmThs {
   private:
          group_table *keyfile;
          void set_srch_ths(UINT fd);
          void set_af_ths(UINT fd);
          unsigned int extract_comma_sep_freqs(char *freqs, unsigned int **freqs_arr, const char *str);
          unsigned int extract_comma_sep_sinrs(char *sinrs, signed char **sinrs_arr, const char *str);
          void set_hybrd_list(UINT fd);
          void set_band_cfgs(UINT fd);
   public:
          ConfigFmThs();
          ~ConfigFmThs();
          void SetRxSearchAfThs(const char *file, UINT fd);
};

#endif //__CONFIG_FM_THS_H__
