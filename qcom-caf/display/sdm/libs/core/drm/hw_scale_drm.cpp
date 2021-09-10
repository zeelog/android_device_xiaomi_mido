/*
* Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
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

#include <stdio.h>
#include <utils/debug.h>

#include "hw_scale_drm.h"

#define __CLASS__ "HWScaleDRM"

namespace sdm {

static uint32_t GetScalingFilter(ScalingFilterConfig filter_cfg) {
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

static uint32_t GetAlphaInterpolation(HWAlphaInterpolation alpha_filter_cfg) {
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

void HWScaleDRM::SetPlaneScaler(const HWScaleData &scale_data, SDEScaler *scaler) {
  if (version_ == Version::V2) {
    SetPlaneScalerV2(scale_data, &scaler->scaler_v2);
  }
}

void HWScaleDRM::SetPlaneScalerV2(const HWScaleData &scale_data, sde_drm_scaler_v2 *scaler) {
  if (!scale_data.enable.scale && !scale_data.enable.direction_detection &&
      !scale_data.enable.detail_enhance) {
    return;
  }

  scaler->enable = scale_data.enable.scale;
  scaler->dir_en = scale_data.enable.direction_detection;
  scaler->de.enable = scale_data.detail_enhance.enable;

  for (int i = 0; i < SDE_MAX_PLANES; i++) {
    const HWPlane &plane = scale_data.plane[i];
    scaler->init_phase_x[i] = plane.init_phase_x;
    scaler->phase_step_x[i] = plane.phase_step_x;
    scaler->init_phase_y[i] = plane.init_phase_y;
    scaler->phase_step_y[i] = plane.phase_step_y;

    // TODO(user): Remove right, bottom from HWPlane and rename to LR, TB similar to qseed3
    // Also remove roi_width which is unused.
    scaler->pe.num_ext_pxls_lr[i] = plane.left.extension;
    scaler->pe.num_ext_pxls_tb[i] = plane.top.extension;

    scaler->pe.left_ftch[i] = plane.left.overfetch;
    scaler->pe.top_ftch[i] = plane.top.overfetch;
    scaler->pe.right_ftch[i] = plane.right.overfetch;
    scaler->pe.btm_ftch[i] = plane.bottom.overfetch;

    scaler->pe.left_rpt[i] = plane.left.repeat;
    scaler->pe.top_rpt[i] = plane.top.repeat;
    scaler->pe.right_rpt[i] = plane.right.repeat;
    scaler->pe.btm_rpt[i] = plane.bottom.repeat;

    scaler->preload_x[i] = UINT32(plane.preload_x);
    scaler->preload_y[i] = UINT32(plane.preload_y);

    scaler->src_width[i] = plane.src_width;
    scaler->src_height[i] = plane.src_height;
  }

  scaler->dst_width = scale_data.dst_width;
  scaler->dst_height = scale_data.dst_height;

  scaler->y_rgb_filter_cfg = GetScalingFilter(scale_data.y_rgb_filter_cfg);
  scaler->uv_filter_cfg = GetScalingFilter(scale_data.uv_filter_cfg);
  scaler->alpha_filter_cfg = GetAlphaInterpolation(scale_data.alpha_filter_cfg);
  scaler->blend_cfg = scale_data.blend_cfg;

  scaler->lut_flag = (scale_data.lut_flag.lut_swap ? SCALER_LUT_SWAP : 0) |
                     (scale_data.lut_flag.lut_dir_wr ? SCALER_LUT_DIR_WR : 0) |
                     (scale_data.lut_flag.lut_y_cir_wr ? SCALER_LUT_Y_CIR_WR : 0) |
                     (scale_data.lut_flag.lut_uv_cir_wr ? SCALER_LUT_UV_CIR_WR : 0) |
                     (scale_data.lut_flag.lut_y_sep_wr ? SCALER_LUT_Y_SEP_WR : 0) |
                     (scale_data.lut_flag.lut_uv_sep_wr ? SCALER_LUT_UV_SEP_WR : 0);

  scaler->dir_lut_idx = scale_data.dir_lut_idx;
  scaler->y_rgb_cir_lut_idx = scale_data.y_rgb_cir_lut_idx;
  scaler->uv_cir_lut_idx = scale_data.uv_cir_lut_idx;
  scaler->y_rgb_sep_lut_idx = scale_data.y_rgb_sep_lut_idx;
  scaler->uv_sep_lut_idx = scale_data.uv_sep_lut_idx;

  /* TODO(user): Uncomment when de support is added
  if (scaler->de.enable) {
    sde_drm_de_v1 *det_enhance = &scaler->de;
    det_enhance->sharpen_level1 = scale_data.detail_enhance.sharpen_level1;
    det_enhance->sharpen_level2 = scale_data.detail_enhance.sharpen_level2;
    det_enhance->clip = scale_data.detail_enhance.clip;
    det_enhance->limit = scale_data.detail_enhance.limit;
    det_enhance->thr_quiet = scale_data.detail_enhance.thr_quiet;
    det_enhance->thr_dieout = scale_data.detail_enhance.thr_dieout;
    det_enhance->thr_low = scale_data.detail_enhance.thr_low;
    det_enhance->thr_high = scale_data.detail_enhance.thr_high;
    det_enhance->prec_shift = scale_data.detail_enhance.prec_shift;

    for (int i = 0; i < SDE_MAX_DE_CURVES; i++) {
      det_enhance->adjust_a[i] = scale_data.detail_enhance.adjust_a[i];
      det_enhance->adjust_b[i] = scale_data.detail_enhance.adjust_b[i];
      det_enhance->adjust_c[i] = scale_data.detail_enhance.adjust_c[i];
    }
  }
  */

  return;
}

}  // namespace sdm
