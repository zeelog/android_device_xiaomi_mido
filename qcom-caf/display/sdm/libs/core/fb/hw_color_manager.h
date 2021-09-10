/* Copyright (c) 2015-2017, The Linux Foundataion. All rights reserved.
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
*
*/

#ifndef __HW_COLOR_MANAGER_H__
#define __HW_COLOR_MANAGER_H__

#include <linux/msm_mdp_ext.h>
#include <linux/msm_mdp.h>

#include <private/color_params.h>

namespace sdm {

class HWColorManager {
 public:
  static DisplayError SetPCC(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params);
  static DisplayError SetIGC(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params);
  static DisplayError SetPGC(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params);
  static DisplayError SetMixerGC(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params);
  static DisplayError SetPAV2(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params);
  static DisplayError SetDither(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params);
  static DisplayError SetGamut(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params);
  static DisplayError SetPADither(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params);
  static DisplayError SetCSCLegacy(const PPFeatureInfo &feature, msmfb_mdp_pp *kernel_params);
  static DisplayError (*SetFeature[kMaxNumPPFeatures])(const PPFeatureInfo &feature,
                                                       msmfb_mdp_pp *kernel_params);

 protected:
  HWColorManager() {}
};

}  // namespace sdm

#endif  // __HW_COLOR_MANAGER_H__
