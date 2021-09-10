/*
* Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <utils/debug.h>
#include "hw_scale.h"

#define __CLASS__ "HWScale"

namespace sdm {

DisplayError HWScale::Create(HWScale **intf, bool has_qseed3) {
  if (has_qseed3) {
    *intf = new HWScaleV2();
  } else {
    *intf = new HWScaleV1();
  }

  return kErrorNone;
}

DisplayError HWScale::Destroy(HWScale *intf) {
  delete intf;

  return kErrorNone;
}

void HWScaleV1::SetHWScaleData(const HWScaleData &scale_data, uint32_t index,
                               mdp_layer_commit_v1 *mdp_commit, HWSubBlockType sub_block_type) {
  if (!scale_data.enable.scale) {
    return;
  }

  if (sub_block_type == kHWDestinationScalar) {
    return;
  }

  mdp_input_layer *mdp_layer = &mdp_commit->input_layers[index];
  mdp_layer->flags |= MDP_LAYER_ENABLE_PIXEL_EXT;
  mdp_scale_data *mdp_scale = &scale_data_v1_.at(index);
  mdp_scale->enable_pxl_ext = scale_data.enable.scale;
  for (int i = 0; i < MAX_PLANES; i++) {
    const HWPlane &plane = scale_data.plane[i];
    mdp_scale->init_phase_x[i] = plane.init_phase_x;
    mdp_scale->phase_step_x[i] = plane.phase_step_x;
    mdp_scale->init_phase_y[i] = plane.init_phase_y;
    mdp_scale->phase_step_y[i] = plane.phase_step_y;

    mdp_scale->num_ext_pxls_left[i] = plane.left.extension;
    mdp_scale->left_ftch[i] = plane.left.overfetch;
    mdp_scale->left_rpt[i] = plane.left.repeat;

    mdp_scale->num_ext_pxls_top[i] = plane.top.extension;
    mdp_scale->top_ftch[i] = plane.top.overfetch;
    mdp_scale->top_rpt[i] = plane.top.repeat;

    mdp_scale->num_ext_pxls_right[i] = plane.right.extension;
    mdp_scale->right_ftch[i] = plane.right.overfetch;
    mdp_scale->right_rpt[i] = plane.right.repeat;

    mdp_scale->num_ext_pxls_btm[i] = plane.bottom.extension;
    mdp_scale->btm_ftch[i] = plane.bottom.overfetch;
    mdp_scale->btm_rpt[i] = plane.bottom.repeat;

    mdp_scale->roi_w[i] = plane.roi_width;
  }

  return;
}

void* HWScaleV1::GetScaleDataRef(uint32_t index, HWSubBlockType sub_block_type) {
  if (sub_block_type != kHWDestinationScalar) {
    return &scale_data_v1_.at(index);
  }

  return NULL;
}

void HWScaleV1::DumpScaleData(void *mdp_scale) {
  if (!mdp_scale) {
    return;
  }

  mdp_scale_data *scale = reinterpret_cast<mdp_scale_data *>(mdp_scale);
  if (scale->enable_pxl_ext) {
    DLOGD_IF(kTagDriverConfig, "Scale Enable = %d", scale->enable_pxl_ext);
    for (int j = 0; j < MAX_PLANES; j++) {
      DLOGV_IF(kTagDriverConfig, "Scale Data[%d] : Phase=[%x %x %x %x] Pixel_Ext=[%d %d %d %d]",
               j, scale->init_phase_x[j], scale->phase_step_x[j], scale->init_phase_y[j],
               scale->phase_step_y[j], scale->num_ext_pxls_left[j], scale->num_ext_pxls_top[j],
               scale->num_ext_pxls_right[j], scale->num_ext_pxls_btm[j]);
      DLOGV_IF(kTagDriverConfig, "Fetch=[%d %d %d %d]  Repeat=[%d %d %d %d]  roi_width = %d",
               scale->left_ftch[j], scale->top_ftch[j], scale->right_ftch[j], scale->btm_ftch[j],
               scale->left_rpt[j], scale->top_rpt[j], scale->right_rpt[j], scale->btm_rpt[j],
               scale->roi_w[j]);
    }
  }

  return;
}

void HWScaleV2::SetHWScaleData(const HWScaleData &scale_data, uint32_t index,
                               mdp_layer_commit_v1 *mdp_commit, HWSubBlockType sub_block_type) {
  if (!scale_data.enable.scale && !scale_data.enable.direction_detection &&
      !scale_data.enable.detail_enhance ) {
    return;
  }

  mdp_scale_data_v2 *mdp_scale;
  if (sub_block_type != kHWDestinationScalar) {
    mdp_input_layer *mdp_layer = &mdp_commit->input_layers[index];
    mdp_layer->flags |= MDP_LAYER_ENABLE_QSEED3_SCALE;
    mdp_scale = &scale_data_v2_.at(index);
  } else {
    mdp_scale_data_v2 mdp_dest_scale = {0};

    dest_scale_data_v2_.insert(std::make_pair(index, mdp_dest_scale));
    mdp_scale = &dest_scale_data_v2_[index];
  }

  mdp_scale->enable = (scale_data.enable.scale ? ENABLE_SCALE : 0) |
                      (scale_data.enable.direction_detection ? ENABLE_DIRECTION_DETECTION : 0) |
                      (scale_data.enable.detail_enhance ? ENABLE_DETAIL_ENHANCE : 0);

  if (sub_block_type == kHWDestinationScalar) {
    mdp_destination_scaler_data *mdp_dest_scalar =
      reinterpret_cast<mdp_destination_scaler_data *>(mdp_commit->dest_scaler);

    mdp_dest_scalar[index].flags = mdp_scale->enable ? MDP_DESTSCALER_ENABLE : 0;
    if (scale_data.enable.detail_enhance) {
      mdp_dest_scalar[index].flags |= MDP_DESTSCALER_ENHANCER_UPDATE;
    }
  }

  for (int i = 0; i < MAX_PLANES; i++) {
    const HWPlane &plane = scale_data.plane[i];
    mdp_scale->init_phase_x[i] = plane.init_phase_x;
    mdp_scale->phase_step_x[i] = plane.phase_step_x;
    mdp_scale->init_phase_y[i] = plane.init_phase_y;
    mdp_scale->phase_step_y[i] = plane.phase_step_y;

    mdp_scale->num_ext_pxls_left[i] = UINT32(plane.left.extension);
    mdp_scale->left_ftch[i] = plane.left.overfetch;
    mdp_scale->left_rpt[i] = plane.left.repeat;

    mdp_scale->num_ext_pxls_top[i] = UINT32(plane.top.extension);
    mdp_scale->top_ftch[i] = UINT32(plane.top.overfetch);
    mdp_scale->top_rpt[i] = UINT32(plane.top.repeat);

    mdp_scale->num_ext_pxls_right[i] = UINT32(plane.right.extension);
    mdp_scale->right_ftch[i] = plane.right.overfetch;
    mdp_scale->right_rpt[i] = plane.right.repeat;

    mdp_scale->num_ext_pxls_btm[i] = UINT32(plane.bottom.extension);
    mdp_scale->btm_ftch[i] = UINT32(plane.bottom.overfetch);
    mdp_scale->btm_rpt[i] = UINT32(plane.bottom.repeat);

    mdp_scale->roi_w[i] = plane.roi_width;

    mdp_scale->preload_x[i] = UINT32(plane.preload_x);
    mdp_scale->preload_y[i] = UINT32(plane.preload_y);

    mdp_scale->src_width[i] = plane.src_width;
    mdp_scale->src_height[i] = plane.src_height;
  }

  mdp_scale->dst_width = scale_data.dst_width;
  mdp_scale->dst_height = scale_data.dst_height;

  mdp_scale->y_rgb_filter_cfg = GetMDPScalingFilter(scale_data.y_rgb_filter_cfg);
  mdp_scale->uv_filter_cfg = GetMDPScalingFilter(scale_data.uv_filter_cfg);
  mdp_scale->alpha_filter_cfg = GetMDPAlphaInterpolation(scale_data.alpha_filter_cfg);
  mdp_scale->blend_cfg = scale_data.blend_cfg;

  mdp_scale->lut_flag = (scale_data.lut_flag.lut_swap ? SCALER_LUT_SWAP : 0) |
                        (scale_data.lut_flag.lut_dir_wr ? SCALER_LUT_DIR_WR : 0) |
                        (scale_data.lut_flag.lut_y_cir_wr ? SCALER_LUT_Y_CIR_WR : 0) |
                        (scale_data.lut_flag.lut_uv_cir_wr ? SCALER_LUT_UV_CIR_WR : 0) |
                        (scale_data.lut_flag.lut_y_sep_wr ? SCALER_LUT_Y_SEP_WR : 0) |
                        (scale_data.lut_flag.lut_uv_sep_wr ? SCALER_LUT_UV_SEP_WR : 0);

  mdp_scale->dir_lut_idx = scale_data.dir_lut_idx;
  mdp_scale->y_rgb_cir_lut_idx = scale_data.y_rgb_cir_lut_idx;
  mdp_scale->uv_cir_lut_idx = scale_data.uv_cir_lut_idx;
  mdp_scale->y_rgb_sep_lut_idx = scale_data.y_rgb_sep_lut_idx;
  mdp_scale->uv_sep_lut_idx = scale_data.uv_sep_lut_idx;

  if (mdp_scale->enable & ENABLE_DETAIL_ENHANCE) {
    mdp_det_enhance_data *mdp_det_enhance = &mdp_scale->detail_enhance;
    mdp_det_enhance->enable = scale_data.detail_enhance.enable;
    mdp_det_enhance->sharpen_level1 = scale_data.detail_enhance.sharpen_level1;
    mdp_det_enhance->sharpen_level2 = scale_data.detail_enhance.sharpen_level2;
    mdp_det_enhance->clip = scale_data.detail_enhance.clip;
    mdp_det_enhance->limit = scale_data.detail_enhance.limit;
    mdp_det_enhance->thr_quiet = scale_data.detail_enhance.thr_quiet;
    mdp_det_enhance->thr_dieout = scale_data.detail_enhance.thr_dieout;
    mdp_det_enhance->thr_low = scale_data.detail_enhance.thr_low;
    mdp_det_enhance->thr_high = scale_data.detail_enhance.thr_high;
    mdp_det_enhance->prec_shift = scale_data.detail_enhance.prec_shift;

    for (int i = 0; i < MAX_DET_CURVES; i++) {
      mdp_det_enhance->adjust_a[i] = scale_data.detail_enhance.adjust_a[i];
      mdp_det_enhance->adjust_b[i] = scale_data.detail_enhance.adjust_b[i];
      mdp_det_enhance->adjust_c[i] = scale_data.detail_enhance.adjust_c[i];
    }
  }

  return;
}

void* HWScaleV2::GetScaleDataRef(uint32_t index, HWSubBlockType sub_block_type) {
  if (sub_block_type != kHWDestinationScalar) {
    return &scale_data_v2_.at(index);
  } else {
    return &dest_scale_data_v2_[index];
  }
}

uint32_t HWScaleV2::GetMDPScalingFilter(ScalingFilterConfig filter_cfg) {
  switch (filter_cfg) {
  case kFilterEdgeDirected:
    return FILTER_EDGE_DIRECTED_2D;
  case kFilterCircular:
    return FILTER_CIRCULAR_2D;
  case kFilterSeparable:
    return FILTER_SEPARABLE_1D;
  case kFilterBilinear:
    return FILTER_BILINEAR;
  default:
    DLOGE("Invalid Scaling Filter");
    return kFilterMax;
  }
}

uint32_t HWScaleV2::GetMDPAlphaInterpolation(HWAlphaInterpolation alpha_filter_cfg) {
  switch (alpha_filter_cfg) {
  case kInterpolationPixelRepeat:
    return FILTER_ALPHA_DROP_REPEAT;
  case kInterpolationBilinear:
    return FILTER_ALPHA_BILINEAR;
  default:
    DLOGE("Invalid Alpha Interpolation");
    return kInterpolationMax;
  }
}

void HWScaleV2::DumpScaleData(void *mdp_scale) {
  if (!mdp_scale) {
    return;
  }

  mdp_scale_data_v2 *scale = reinterpret_cast<mdp_scale_data_v2 *>(mdp_scale);
  if (scale->enable) {
    DLOGD_IF(kTagDriverConfig, "Scale Enable = %d", scale->enable);
    for (int j = 0; j < MAX_PLANES; j++) {
      DLOGV_IF(kTagDriverConfig, "Scale Data[%d]: Phase_init[x y]=[%x %x] Phase_step:[x y]=[%x %x]",
        j, scale->init_phase_x[j], scale->init_phase_y[j], scale->phase_step_x[j],
        scale->phase_step_y[j]);
      DLOGV_IF(kTagDriverConfig, "Preload[x y]=[%x %x], Pixel Ext=[%d %d] Ovfetch=[%d %d %d %d]",
        scale->preload_x[j], scale->preload_y[j], scale->num_ext_pxls_left[j],
        scale->num_ext_pxls_top[j], scale->left_ftch[j], scale->top_ftch[j], scale->right_ftch[j],
        scale->btm_ftch[j]);
      DLOGV_IF(kTagDriverConfig, "Repeat=[%d %d %d %d] Src[w x h]=[%d %d] roi_width = %d",
        scale->left_rpt[j], scale->top_rpt[j], scale->right_rpt[j], scale->btm_rpt[j],
        scale->src_width[j], scale->src_height[j], scale->roi_w[j]);
    }

    DLOGD_IF(kTagDriverConfig, "LUT flags = %d", scale->lut_flag);
    DLOGV_IF(kTagDriverConfig, "y_rgb_filter=%d, uv_filter=%d, alpha_filter=%d, blend_cfg=%d",
      scale->y_rgb_filter_cfg, scale->uv_filter_cfg, scale->alpha_filter_cfg, scale->blend_cfg);
    DLOGV_IF(kTagDriverConfig, "dir_lut=%d, y_rgb_cir=%d, uv_cir=%d, y_rgb_sep=%d, uv_sep=%d",
      scale->dir_lut_idx, scale->y_rgb_cir_lut_idx, scale->uv_cir_lut_idx,
      scale->y_rgb_sep_lut_idx, scale->uv_sep_lut_idx);
    if (scale->enable & ENABLE_DETAIL_ENHANCE) {
      mdp_det_enhance_data *de = &scale->detail_enhance;
      DLOGV_IF(kTagDriverConfig, "Detail Enhance: enable: %d sharpen_level1: %d sharpen_level2: %d",
        de->enable, de->sharpen_level1, de->sharpen_level2);
      DLOGV_IF(kTagDriverConfig, "clip: %d limit:%d thr_quiet: %d thr_dieout: %d",
        de->clip, de->limit, de->thr_quiet, de->thr_dieout);
      DLOGV_IF(kTagDriverConfig, "thr_low: %d thr_high: %d prec_shift: %d", de->thr_low,
        de->thr_high, de->prec_shift);
      for (uint32_t i = 0; i < MAX_DET_CURVES; i++) {
        DLOGV_IF(kTagDriverConfig, "adjust_a[%d]: %d adjust_b[%d]: %d adjust_c[%d]: %d", i,
          de->adjust_a[i], i, de->adjust_b[i], i, de->adjust_c[i]);
      }
    }
  }

  return;
}

}  // namespace sdm
