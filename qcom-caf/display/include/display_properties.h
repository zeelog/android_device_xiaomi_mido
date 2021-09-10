/*
* Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
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

#ifndef __DISPLAY_PROPERTIES_H__
#define __DISPLAY_PROPERTIES_H__

#define DISP_PROP_PREFIX                     "vendor.display."
#define GRALLOC_PROP_PREFIX                  "vendor.gralloc."
#define RO_DISP_PROP_PREFIX                  "ro.vendor.display."
#define PERSIST_DISP_PROP_PREFIX             "persist.vendor.display."

#define DISPLAY_PROP(prop_name)              DISP_PROP_PREFIX prop_name
#define GRALLOC_PROP(prop_name)              GRALLOC_PROP_PREFIX prop_name
#define RO_DISPLAY_PROP(prop_name)           RO_DISP_PROP_PREFIX prop_name
#define PERSIST_DISPLAY_PROP(prop_name)      PERSIST_DISP_PROP_PREFIX prop_name

#define COMPOSITION_MASK_PROP                DISPLAY_PROP("comp_mask")
#define HDMI_CONFIG_INDEX_PROP               DISPLAY_PROP("hdmi_cfg_idx")
#define IDLE_TIME_PROP                       DISPLAY_PROP("idle_time")
#define IDLE_TIME_INACTIVE_PROP              DISPLAY_PROP("idle_time_inactive")
#define BOOT_ANIMATION_LAYER_COUNT_PROP      DISPLAY_PROP("boot_anim_layer_count")
#define DISABLE_ROTATOR_DOWNSCALE_PROP       DISPLAY_PROP("disable_rotator_downscale")
#define DISABLE_DECIMATION_PROP              DISPLAY_PROP("disable_decimation")
#define PRIMARY_MIXER_STAGES_PROP            DISPLAY_PROP("primary_mixer_stages")
#define EXTERNAL_MIXER_STAGES_PROP           DISPLAY_PROP("external_mixer_stages")
#define VIRTUAL_MIXER_STAGES_PROP            DISPLAY_PROP("virtual_mixer_stages")
#define MAX_UPSCALE_PROP                     DISPLAY_PROP("max_upscale")
#define VIDEO_MODE_PANEL_PROP                DISPLAY_PROP("video_mode_panel")
#define DISABLE_ROTATOR_UBWC_PROP            DISPLAY_PROP("disable_rotator_ubwc")
#define DISABLE_ROTATOR_SPLIT_PROP           DISPLAY_PROP("disable_rotator_split")
#define DISABLE_SCALER_PROP                  DISPLAY_PROP("disable_scaler")
#define DISABLE_AVR_PROP                     DISPLAY_PROP("disable_avr")
#define DISABLE_EXTERNAL_ANIMATION_PROP      DISPLAY_PROP("disable_ext_anim")
#define DISABLE_PARTIAL_SPLIT_PROP           DISPLAY_PROP("disable_partial_split")
#define PREFER_SOURCE_SPLIT_PROP             DISPLAY_PROP("prefer_source_split")
#define MIXER_RESOLUTION_PROP                DISPLAY_PROP("mixer_resolution")
#define SIMULATED_CONFIG_PROP                DISPLAY_PROP("simulated_config")
#define MAX_EXTERNAL_LAYERS_PROP             DISPLAY_PROP("max_external_layers")
#define PERF_HINT_WINDOW_PROP                DISPLAY_PROP("perf_hint_window")
#define ENABLE_EXTERNAL_DOWNSCALE_PROP       DISPLAY_PROP("enable_external_downscale")
#define EXTERNAL_ACTION_SAFE_WIDTH_PROP      DISPLAY_PROP("external_action_safe_width")
#define EXTERNAL_ACTION_SAFE_HEIGHT_PROP     DISPLAY_PROP("external_action_safe_height")
#define FB_WIDTH_PROP                        DISPLAY_PROP("fb_width")
#define FB_HEIGHT_PROP                       DISPLAY_PROP("fb_height")
#define DISABLE_METADATA_DYNAMIC_FPS_PROP    DISPLAY_PROP("disable_metadata_dynamic_fps")
#define DISABLE_BLIT_COMPOSITION_PROP        DISPLAY_PROP("disable_blit_comp")
#define DISABLE_SKIP_VALIDATE_PROP           DISPLAY_PROP("disable_skip_validate")
#define HDMI_S3D_MODE_PROP                   DISPLAY_PROP("hdmi_s3d_mode")
#define DISABLE_DESTINATION_SCALER_PROP      DISPLAY_PROP("disable_dest_scaler")
#define ENABLE_PARTIAL_UPDATE_PROP           DISPLAY_PROP("enable_partial_update")
#define ENABLE_ROTATOR_SYNC_ALLOC            DISPLAY_PROP("rotator_sync_alloc")
#define WRITEBACK_SUPPORTED                  DISPLAY_PROP("support_writeback")
#define DISABLE_UBWC_PROP                    GRALLOC_PROP("disable_ubwc")
#define ENABLE_FB_UBWC_PROP                  GRALLOC_PROP("enable_fb_ubwc")
#define MAP_FB_MEMORY_PROP                   GRALLOC_PROP("map_fb_memory")

#define MAX_BLIT_FACTOR_PROP                 DISPLAY_PROP("max_blit_factor")
#define DISABLE_SECURE_INLINE_ROTATOR_PROP   DISPLAY_PROP("disable_secure_inline_rotator")
#define DISABLE_MULTIRECT_PROP               DISPLAY_PROP("disable_multirect")
#define DISABLE_UBWC_FF_VOTING_PROP          DISPLAY_PROP("disable_ubwc_ff_voting")
#define DISABLE_INLINE_ROTATOR_PROP          DISPLAY_PROP("disable_inline_rotator")
#define DISABLE_FB_CROPPING_PROP             DISPLAY_PROP("disable_fb_cropping")
#define PRIORITIZE_CACHE_COMPOSITION_PROP    DISPLAY_PROP("prioritize_cache_comp")

#define DISABLE_HDR_LUT_GEN                  DISPLAY_PROP("disable_hdr_lut_gen")
#define ENABLE_DEFAULT_COLOR_MODE            DISPLAY_PROP("enable_default_color_mode")
#define DISABLE_HDR                          DISPLAY_PROP("hwc_disable_hdr")
#define DISABLE_QTI_BSP                      DISPLAY_PROP("disable_qti_bsp")
#define UPDATE_VSYNC_ON_DOZE                 DISPLAY_PROP("update_vsync_on_doze")
#define PANEL_MOUNTFLIP                      DISPLAY_PROP("panel_mountflip")
#define VDS_ALLOW_HWC                        DISPLAY_PROP("vds_allow_hwc")
#define QDFRAMEWORK_LOGS                     DISPLAY_PROP("qdframework_logs")

#define HDR_CONFIG_PROP                      RO_DISPLAY_PROP("hdr.config")
#define QDCM_PCC_TRANS_PROP                  DISPLAY_PROP("qdcm.pcc_for_trans")
#define QDCM_DIAGONAL_MATRIXMODE_PROP        DISPLAY_PROP("qdcm.diagonal_matrix_mode")
#define QDCM_DISABLE_TIMEOUT_PROP            PERSIST_DISPLAY_PROP("qdcm.disable_timeout")

#define ZERO_SWAP_INTERVAL                   "vendor.debug.egl.swapinterval"

#endif  // __DISPLAY_PROPERTIES_H__
