/*
* Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
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

#include <ctype.h>
#include <math.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <linux/msm_mdp.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include "hw_color_manager.h"

#define __CLASS__ "HWColorManager"

namespace sdm {

DisplayError (*HWColorManager::SetFeature[])(const PPFeatureInfo &, msmfb_mdp_pp *) = {
        [kGlobalColorFeaturePcc] = &HWColorManager::SetPCC,
        [kGlobalColorFeatureIgc] = &HWColorManager::SetIGC,
        [kGlobalColorFeaturePgc] = &HWColorManager::SetPGC,
        [kMixerColorFeatureGc] = &HWColorManager::SetMixerGC,
        [kGlobalColorFeaturePaV2] = &HWColorManager::SetPAV2,
        [kGlobalColorFeatureDither] = &HWColorManager::SetDither,
        [kGlobalColorFeatureGamut] = &HWColorManager::SetGamut,
        [kGlobalColorFeaturePADither] = &HWColorManager::SetPADither,
        [kGlobalColorFeatureCsc] = &HWColorManager::SetCSCLegacy,
};

DisplayError HWColorManager::SetPCC(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params) {
  DisplayError ret = kErrorNone;

  kernel_params->op = mdp_op_pcc_cfg;
  kernel_params->data.pcc_cfg_data.version = feature.feature_version_;
  kernel_params->data.pcc_cfg_data.block = MDP_LOGICAL_BLOCK_DISP_0 + feature.disp_id_;
  kernel_params->data.pcc_cfg_data.ops = feature.enable_flags_;
  kernel_params->data.pcc_cfg_data.cfg_payload = feature.GetConfigData();
  DLOGV_IF(kTagQDCM, "kernel params version = %d, block = %d, flags = %d",
           kernel_params->data.pcc_cfg_data.version, kernel_params->data.pcc_cfg_data.block,
           kernel_params->data.pcc_cfg_data.ops);

  return ret;
}

DisplayError HWColorManager::SetIGC(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params) {
  DisplayError ret = kErrorNone;

  kernel_params->op = mdp_op_lut_cfg;
  kernel_params->data.lut_cfg_data.lut_type = mdp_lut_igc;
  kernel_params->data.lut_cfg_data.data.igc_lut_data.block =
      MDP_LOGICAL_BLOCK_DISP_0 + feature.disp_id_;
  kernel_params->data.lut_cfg_data.data.igc_lut_data.version = feature.feature_version_;
  kernel_params->data.lut_cfg_data.data.igc_lut_data.ops = feature.enable_flags_;
  kernel_params->data.lut_cfg_data.data.igc_lut_data.cfg_payload = feature.GetConfigData();

  return ret;
}

DisplayError HWColorManager::SetPGC(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params) {
  DisplayError ret = kErrorNone;

  kernel_params->op = mdp_op_lut_cfg;
  kernel_params->data.lut_cfg_data.lut_type = mdp_lut_pgc;
  kernel_params->data.lut_cfg_data.data.pgc_lut_data.version = feature.feature_version_;
  kernel_params->data.lut_cfg_data.data.pgc_lut_data.block =
      MDP_LOGICAL_BLOCK_DISP_0 + feature.disp_id_;
  kernel_params->data.lut_cfg_data.data.pgc_lut_data.flags = feature.enable_flags_;
  kernel_params->data.lut_cfg_data.data.pgc_lut_data.cfg_payload = feature.GetConfigData();

  return ret;
}

DisplayError HWColorManager::SetMixerGC(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params) {
  DisplayError ret = kErrorNone;

  kernel_params->op = mdp_op_lut_cfg;
  kernel_params->data.lut_cfg_data.lut_type = mdp_lut_pgc;
  kernel_params->data.lut_cfg_data.data.pgc_lut_data.version = feature.feature_version_;
  kernel_params->data.lut_cfg_data.data.pgc_lut_data.block =
      (MDP_LOGICAL_BLOCK_DISP_0 + feature.disp_id_) | MDSS_PP_LM_CFG;
  kernel_params->data.lut_cfg_data.data.pgc_lut_data.flags = feature.enable_flags_;
  kernel_params->data.lut_cfg_data.data.pgc_lut_data.cfg_payload = feature.GetConfigData();
  return ret;
}

DisplayError HWColorManager::SetPAV2(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params) {
  DisplayError ret = kErrorNone;

  kernel_params->op = mdp_op_pa_v2_cfg;
  kernel_params->data.pa_v2_cfg_data.version = feature.feature_version_;
  kernel_params->data.pa_v2_cfg_data.block = MDP_LOGICAL_BLOCK_DISP_0 + feature.disp_id_;
  kernel_params->data.pa_v2_cfg_data.flags = feature.enable_flags_;
  kernel_params->data.pa_v2_cfg_data.cfg_payload = feature.GetConfigData();
  DLOGV_IF(kTagQDCM, "kernel params version = %d, block = %d, flags = %d",
           kernel_params->data.pa_v2_cfg_data.version, kernel_params->data.pa_v2_cfg_data.block,
           kernel_params->data.pa_v2_cfg_data.flags);

  return ret;
}

DisplayError HWColorManager::SetDither(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params) {
  DisplayError ret = kErrorNone;

  kernel_params->op = mdp_op_dither_cfg;
  kernel_params->data.dither_cfg_data.version = feature.feature_version_;
  kernel_params->data.dither_cfg_data.block = MDP_LOGICAL_BLOCK_DISP_0 + feature.disp_id_;
  kernel_params->data.dither_cfg_data.flags = feature.enable_flags_;
  kernel_params->data.dither_cfg_data.cfg_payload = feature.GetConfigData();

  return ret;
}

DisplayError HWColorManager::SetGamut(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params) {
  DisplayError ret = kErrorNone;

  kernel_params->op = mdp_op_gamut_cfg;
  kernel_params->data.gamut_cfg_data.version = feature.feature_version_;
  kernel_params->data.gamut_cfg_data.block = MDP_LOGICAL_BLOCK_DISP_0 + feature.disp_id_;
  kernel_params->data.gamut_cfg_data.flags = feature.enable_flags_;
  kernel_params->data.gamut_cfg_data.cfg_payload = feature.GetConfigData();

  return ret;
}

DisplayError HWColorManager::SetPADither(const PPFeatureInfo &feature,
                                         msmfb_mdp_pp *kernel_params) {
  DisplayError ret = kErrorNone;
#ifdef PA_DITHER
  kernel_params->op = mdp_op_pa_dither_cfg;
  kernel_params->data.dither_cfg_data.version = feature.feature_version_;
  kernel_params->data.dither_cfg_data.block = MDP_LOGICAL_BLOCK_DISP_0 + feature.disp_id_;
  kernel_params->data.dither_cfg_data.flags = feature.enable_flags_;
  kernel_params->data.dither_cfg_data.cfg_payload = feature.GetConfigData();
#endif
  return ret;
}

DisplayError HWColorManager::SetCSCLegacy(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params) {
  DisplayError ret = kErrorNone;

  kernel_params->op = mdp_op_csc_cfg;
  kernel_params->data.csc_cfg_data.block = MDP_BLOCK_DMA_P;
  std::memcpy(&kernel_params->data.csc_cfg_data.csc_data, feature.GetConfigData(),
          sizeof(mdp_csc_cfg));

  for( int row = 0; row < 3; row++) {
    DLOGV_IF(kTagQDCM, "kernel mv[%d][0]=0x%x  mv[%d][1]=0x%x mv[%d][2]=0x%x\n",
            row, kernel_params->data.csc_cfg_data.csc_data.csc_mv[row*3 + 0],
            row, kernel_params->data.csc_cfg_data.csc_data.csc_mv[row*3 + 1],
            row, kernel_params->data.csc_cfg_data.csc_data.csc_mv[row*3 + 2]);
    DLOGV_IF(kTagQDCM, "kernel pre_bv[%d]=%x\n", row,
            kernel_params->data.csc_cfg_data.csc_data.csc_pre_bv[row]);
    DLOGV_IF(kTagQDCM, "kernel post_bv[%d]=%x\n", row,
            kernel_params->data.csc_cfg_data.csc_data.csc_post_bv[row]);
  }
  return ret;
}

}  // namespace sdm
