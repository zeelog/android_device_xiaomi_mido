/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
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

#define __CLASS__ "HWColorManagerDRM"

#ifdef PP_DRM_ENABLE
#include <drm/msm_drm_pp.h>
#endif
#include <utils/debug.h>
#include "hw_color_manager_drm.h"

using sde_drm::kFeaturePcc;
using sde_drm::kFeatureIgc;
using sde_drm::kFeaturePgc;
using sde_drm::kFeatureMixerGc;
using sde_drm::kFeaturePaV2;
using sde_drm::kFeatureDither;
using sde_drm::kFeatureGamut;
using sde_drm::kFeaturePADither;
using sde_drm::kPPFeaturesMax;

#ifdef PP_DRM_ENABLE
static const uint32_t kPgcDataMask = 0x3FF;
static const uint32_t kPgcShift = 16;
#endif

namespace sdm {

DisplayError (*HWColorManagerDrm::GetDrmFeature[])(const PPFeatureInfo &, DRMPPFeatureInfo *) = {
        [kGlobalColorFeaturePcc] = &HWColorManagerDrm::GetDrmPCC,
        [kGlobalColorFeatureIgc] = &HWColorManagerDrm::GetDrmIGC,
        [kGlobalColorFeaturePgc] = &HWColorManagerDrm::GetDrmPGC,
        [kMixerColorFeatureGc] = &HWColorManagerDrm::GetDrmMixerGC,
        [kGlobalColorFeaturePaV2] = &HWColorManagerDrm::GetDrmPAV2,
        [kGlobalColorFeatureDither] = &HWColorManagerDrm::GetDrmDither,
        [kGlobalColorFeatureGamut] = &HWColorManagerDrm::GetDrmGamut,
        [kGlobalColorFeaturePADither] = &HWColorManagerDrm::GetDrmPADither,
};

void HWColorManagerDrm::FreeDrmFeatureData(DRMPPFeatureInfo *feature) {
  if (feature->payload)
    free(feature->payload);
}

uint32_t HWColorManagerDrm::GetFeatureVersion(const DRMPPFeatureInfo &feature) {
  uint32_t version = PPFeatureVersion::kSDEPpVersionInvalid;

  switch (feature.id) {
    case kFeaturePcc:
      break;
    case kFeatureIgc:
      break;
    case kFeaturePgc:
      if (feature.version == 1)
        version = PPFeatureVersion::kSDEPgcV17;
      break;
    case kFeatureMixerGc:
      break;
    case kFeaturePaV2:
      break;
    case kFeatureDither:
      break;
    case kFeatureGamut:
      if (feature.version == 1)
        version = PPFeatureVersion::kSDEGamutV17;
      else if (feature.version == 4)
        version = PPFeatureVersion::kSDEGamutV4;
      break;
    case kFeaturePADither:
      break;
    default:
      break;
  }
  return version;
}

DRMPPFeatureID HWColorManagerDrm::ToDrmFeatureId(uint32_t id) {
  DRMPPFeatureID ret = kPPFeaturesMax;

  switch (id) {
    case kGlobalColorFeaturePcc:
      ret = kFeaturePcc;
      break;
    case kGlobalColorFeatureIgc:
      ret = kFeatureIgc;
      break;
    case kGlobalColorFeaturePgc:
      ret = kFeaturePgc;
      break;
    case kMixerColorFeatureGc:
      ret = kFeatureMixerGc;
      break;
    case kGlobalColorFeaturePaV2:
      ret = kFeaturePaV2;
      break;
    case kGlobalColorFeatureDither:
      ret = kFeatureDither;
      break;
    case kGlobalColorFeatureGamut:
      ret = kFeatureGamut;
      break;
    case kGlobalColorFeaturePADither:
      ret = kFeaturePADither;
      break;
    default:
      break;
  }
  return ret;
}

DisplayError HWColorManagerDrm::GetDrmPCC(const PPFeatureInfo &in_data,
                                          DRMPPFeatureInfo *out_data) {
  DisplayError ret = kErrorNone;
  return ret;
}

DisplayError HWColorManagerDrm::GetDrmIGC(const PPFeatureInfo &in_data,
                                          DRMPPFeatureInfo *out_data) {
  DisplayError ret = kErrorNone;
  return ret;
}

DisplayError HWColorManagerDrm::GetDrmPGC(const PPFeatureInfo &in_data,
                                          DRMPPFeatureInfo *out_data) {
  DisplayError ret = kErrorNone;
#ifdef PP_DRM_ENABLE
  struct SDEPgcLUTData *sde_pgc;
  struct drm_msm_pgc_lut *mdp_pgc;

  if (!out_data) {
    DLOGE("Invalid input parameter for gamut");
    return kErrorParameters;
  }
  sde_pgc = (struct SDEPgcLUTData *)in_data.GetConfigData();
  out_data->id = kFeaturePgc;
  out_data->type = sde_drm::kPropBlob;
  out_data->version = in_data.feature_version_;
  out_data->payload_size = sizeof(struct drm_msm_pgc_lut);

  if (in_data.enable_flags_ & kOpsDisable) {
    /* feature disable case */
    out_data->payload = NULL;
    return ret;
  } else if (!(in_data.enable_flags_ & kOpsEnable)) {
    out_data->payload = NULL;
    return kErrorParameters;
  }

  mdp_pgc = new drm_msm_pgc_lut();
  if (!mdp_pgc) {
    DLOGE("Failed to allocate memory for pgc");
    return kErrorMemory;
  }

  if (in_data.enable_flags_ & kOpsEnable)
    mdp_pgc->flags = PGC_8B_ROUND;
  else
    mdp_pgc->flags = 0;

  for (int i = 0, j = 0; i < PGC_TBL_LEN; i++, j += 2) {
    mdp_pgc->c0[i] = (sde_pgc->c0_data[j] & kPgcDataMask) |
        (sde_pgc->c0_data[j + 1] & kPgcDataMask) << kPgcShift;
    mdp_pgc->c1[i] = (sde_pgc->c1_data[j] & kPgcDataMask) |
        (sde_pgc->c1_data[j + 1] & kPgcDataMask) << kPgcShift;
    mdp_pgc->c2[i] = (sde_pgc->c2_data[j] & kPgcDataMask) |
        (sde_pgc->c2_data[j + 1] & kPgcDataMask) << kPgcShift;
  }
  out_data->payload = mdp_pgc;
#endif
  return ret;
}

DisplayError HWColorManagerDrm::GetDrmMixerGC(const PPFeatureInfo &in_data,
                                              DRMPPFeatureInfo *out_data) {
  DisplayError ret = kErrorNone;
  return ret;
}

DisplayError HWColorManagerDrm::GetDrmPAV2(const PPFeatureInfo &in_data,
                                           DRMPPFeatureInfo *out_data) {
  DisplayError ret = kErrorNone;
  return ret;
}

DisplayError HWColorManagerDrm::GetDrmDither(const PPFeatureInfo &in_data,
                                             DRMPPFeatureInfo *out_data) {
  DisplayError ret = kErrorNone;
  return ret;
}

DisplayError HWColorManagerDrm::GetDrmGamut(const PPFeatureInfo &in_data,
                                            DRMPPFeatureInfo *out_data) {
  DisplayError ret = kErrorNone;
#ifdef PP_DRM_ENABLE
  struct SDEGamutCfg *sde_gamut = NULL;
  struct drm_msm_3d_gamut *mdp_gamut = NULL;
  uint32_t size = 0;

  if (!out_data) {
    DLOGE("Invalid input parameter for gamut");
    return kErrorParameters;
  }
  sde_gamut = (struct SDEGamutCfg *)in_data.GetConfigData();
  out_data->id = kFeatureGamut;
  out_data->type = sde_drm::kPropBlob;
  out_data->version = in_data.feature_version_;
  out_data->payload_size = sizeof(struct drm_msm_3d_gamut);
  if (in_data.enable_flags_ & kOpsDisable) {
    /* feature disable case */
    out_data->payload = NULL;
    return ret;
  } else if (!(in_data.enable_flags_ & kOpsEnable)) {
    out_data->payload = NULL;
    return kErrorParameters;
  }

  mdp_gamut = new drm_msm_3d_gamut();
  if (!mdp_gamut) {
    DLOGE("Failed to allocate memory for gamut");
    return kErrorMemory;
  }

  if (sde_gamut->map_en)
    mdp_gamut->flags = GAMUT_3D_MAP_EN;
  else
    mdp_gamut->flags = 0;

  switch (sde_gamut->mode) {
    case SDEGamutCfgWrapper::GAMUT_FINE_MODE:
      mdp_gamut->mode = GAMUT_3D_MODE_17;
      size = GAMUT_3D_MODE17_TBL_SZ;
      break;
    case SDEGamutCfgWrapper::GAMUT_COARSE_MODE:
      mdp_gamut->mode = GAMUT_3D_MODE_5;
      size = GAMUT_3D_MODE5_TBL_SZ;
      break;
    case SDEGamutCfgWrapper::GAMUT_COARSE_MODE_13:
      mdp_gamut->mode = GAMUT_3D_MODE_13;
      size = GAMUT_3D_MODE13_TBL_SZ;
      break;
    default:
      DLOGE("Invalid gamut mode %d", sde_gamut->mode);
      free(mdp_gamut);
      return kErrorParameters;
  }

  if (sde_gamut->map_en)
    std::memcpy(mdp_gamut->scale_off, sde_gamut->scale_off_data,
                sizeof(uint32_t) * GAMUT_3D_SCALE_OFF_SZ * GAMUT_3D_SCALE_OFF_TBL_NUM);

  for (uint32_t row = 0; row < GAMUT_3D_TBL_NUM; row++) {
    for (uint32_t col = 0; col < size; col++) {
      mdp_gamut->col[row][col].c0 = sde_gamut->c0_data[row][col];
      mdp_gamut->col[row][col].c2_c1 = sde_gamut->c1_c2_data[row][col];
    }
  }
  out_data->payload = mdp_gamut;
#endif
  return ret;
}

DisplayError HWColorManagerDrm::GetDrmPADither(const PPFeatureInfo &in_data,
                                               DRMPPFeatureInfo *out_data) {
  DisplayError ret = kErrorNone;
  return ret;
}

}  // namespace sdm
