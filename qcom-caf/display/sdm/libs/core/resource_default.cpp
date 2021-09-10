/*
* Copyright (c) 2014 - 2016, 2018, The Linux Foundation. All rights reserved.
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

#include <math.h>
#include <utils/constants.h>
#include <utils/debug.h>
#include <utils/rect.h>
#include <utils/formats.h>
#include <utils/sys.h>
#include <dlfcn.h>
#include <algorithm>

#include "resource_default.h"

#define __CLASS__ "ResourceDefault"

namespace sdm {

DisplayError ResourceDefault::CreateResourceDefault(const HWResourceInfo &hw_resource_info,
                                                    ResourceInterface **resource_intf) {
  DisplayError error = kErrorNone;

  ResourceDefault *resource_default = new ResourceDefault(hw_resource_info);
  if (!resource_default) {
    return kErrorNone;
  }

  error = resource_default->Init();
  if (error != kErrorNone) {
    delete resource_default;
  }

  *resource_intf = resource_default;

  return kErrorNone;
}

DisplayError ResourceDefault::DestroyResourceDefault(ResourceInterface *resource_intf) {
  ResourceDefault *resource_default = static_cast<ResourceDefault *>(resource_intf);

  resource_default->Deinit();
  delete resource_default;

  return kErrorNone;
}

ResourceDefault::ResourceDefault(const HWResourceInfo &hw_res_info)
  : hw_res_info_(hw_res_info) {
}

DisplayError ResourceDefault::Init() {
  DisplayError error = kErrorNone;

  num_pipe_ = hw_res_info_.num_vig_pipe + hw_res_info_.num_rgb_pipe + hw_res_info_.num_dma_pipe;

  if (!num_pipe_) {
    DLOGE("Number of H/W pipes is Zero!");
    return kErrorParameters;
  }

  src_pipes_.resize(num_pipe_);

  // Priority order of pipes: VIG, RGB, DMA
  uint32_t vig_index = 0;
  uint32_t rgb_index = hw_res_info_.num_vig_pipe;
  uint32_t dma_index = rgb_index + hw_res_info_.num_rgb_pipe;

  for (uint32_t i = 0; i < num_pipe_; i++) {
    const HWPipeCaps &pipe_caps = hw_res_info_.hw_pipes.at(i);
    if (pipe_caps.type == kPipeTypeVIG) {
      src_pipes_[vig_index].type = kPipeTypeVIG;
      src_pipes_[vig_index].index = i;
      src_pipes_[vig_index].mdss_pipe_id = pipe_caps.id;
      vig_index++;
    } else if (pipe_caps.type == kPipeTypeRGB) {
      src_pipes_[rgb_index].type = kPipeTypeRGB;
      src_pipes_[rgb_index].index = i;
      src_pipes_[rgb_index].mdss_pipe_id = pipe_caps.id;
      rgb_index++;
    } else if (pipe_caps.type == kPipeTypeDMA) {
      src_pipes_[dma_index].type = kPipeTypeDMA;
      src_pipes_[dma_index].index = i;
      src_pipes_[dma_index].mdss_pipe_id = pipe_caps.id;
      dma_index++;
    }
  }

  for (uint32_t i = 0; i < num_pipe_; i++) {
    src_pipes_[i].priority = INT(i);
  }

  DLOGI("hw_rev=%x, DMA=%d RGB=%d VIG=%d", hw_res_info_.hw_revision, hw_res_info_.num_dma_pipe,
    hw_res_info_.num_rgb_pipe, hw_res_info_.num_vig_pipe);

  if (hw_res_info_.max_scale_down < 1 || hw_res_info_.max_scale_up < 1) {
    DLOGE("Max scaling setting is invalid! max_scale_down = %d, max_scale_up = %d",
          hw_res_info_.max_scale_down, hw_res_info_.max_scale_up);
    hw_res_info_.max_scale_down = 1;
    hw_res_info_.max_scale_up = 1;
  }

  // TODO(user): clean it up, query from driver for initial pipe status.
#ifndef SDM_VIRTUAL_DRIVER
  rgb_index = hw_res_info_.num_vig_pipe;
  src_pipes_[rgb_index].owner = kPipeOwnerKernelMode;
  src_pipes_[rgb_index + 1].owner = kPipeOwnerKernelMode;
#endif

  return error;
}

DisplayError ResourceDefault::Deinit() {
  return kErrorNone;
}

DisplayError ResourceDefault::RegisterDisplay(DisplayType type,
                                              const HWDisplayAttributes &display_attributes,
                                              const HWPanelInfo &hw_panel_info,
                                              const HWMixerAttributes &mixer_attributes,
                                              Handle *display_ctx) {
  DisplayError error = kErrorNone;

  HWBlockType hw_block_id = kHWBlockMax;
  switch (type) {
  case kPrimary:
    if (!hw_block_ctx_[kHWPrimary].is_in_use) {
      hw_block_id = kHWPrimary;
    }
    break;

  case kHDMI:
    if (!hw_block_ctx_[kHWHDMI].is_in_use) {
      hw_block_id = kHWHDMI;
    }
    break;

  default:
    DLOGW("RegisterDisplay, invalid type %d", type);
    return kErrorParameters;
  }

  if (hw_block_id == kHWBlockMax) {
    return kErrorResources;
  }

  DisplayResourceContext *display_resource_ctx = new DisplayResourceContext();
  if (!display_resource_ctx) {
    return kErrorMemory;
  }

  hw_block_ctx_[hw_block_id].is_in_use = true;

  display_resource_ctx->display_attributes = display_attributes;
  display_resource_ctx->hw_block_id = hw_block_id;
  display_resource_ctx->mixer_attributes = mixer_attributes;

  *display_ctx = display_resource_ctx;
  return error;
}

DisplayError ResourceDefault::UnregisterDisplay(Handle display_ctx) {
  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);
  Purge(display_ctx);

  hw_block_ctx_[display_resource_ctx->hw_block_id].is_in_use = false;

  delete display_resource_ctx;

  return kErrorNone;
}

DisplayError ResourceDefault::ReconfigureDisplay(Handle display_ctx,
                                                 const HWDisplayAttributes &display_attributes,
                                                 const HWPanelInfo &hw_panel_info,
                                                 const HWMixerAttributes &mixer_attributes) {
  SCOPE_LOCK(locker_);

  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);

  display_resource_ctx->display_attributes = display_attributes;
  display_resource_ctx->mixer_attributes = mixer_attributes;

  return kErrorNone;
}

DisplayError ResourceDefault::Start(Handle display_ctx) {
  locker_.Lock();

  return kErrorNone;
}

DisplayError ResourceDefault::Stop(Handle display_ctx, HWLayers *hw_layers) {
  locker_.Unlock();

  return kErrorNone;
}

DisplayError ResourceDefault::Prepare(Handle display_ctx, HWLayers *hw_layers) {
  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);

  DisplayError error = kErrorNone;
  const struct HWLayersInfo &layer_info = hw_layers->info;
  HWBlockType hw_block_id = display_resource_ctx->hw_block_id;

  DLOGV_IF(kTagResources, "==== Resource reserving start: hw_block = %d ====", hw_block_id);

  if (layer_info.hw_layers.size() > 1) {
    DLOGV_IF(kTagResources, "More than one FB layers");
    return kErrorResources;
  }

  const Layer &layer = layer_info.hw_layers.at(0);

  if (layer.composition != kCompositionGPUTarget) {
    DLOGV_IF(kTagResources, "Not an FB layer");
    return kErrorParameters;
  }

  error = Config(display_resource_ctx, hw_layers);
  if (error != kErrorNone) {
    DLOGV_IF(kTagResources, "Resource config failed");
    return error;
  }

  for (uint32_t i = 0; i < num_pipe_; i++) {
    if (src_pipes_[i].hw_block_id == hw_block_id && src_pipes_[i].owner == kPipeOwnerUserMode) {
      src_pipes_[i].ResetState();
    }
  }

  uint32_t left_index = num_pipe_;
  uint32_t right_index = num_pipe_;
  bool need_scale = false;

  struct HWLayerConfig &layer_config = hw_layers->config[0];

  HWPipeInfo *left_pipe = &layer_config.left_pipe;
  HWPipeInfo *right_pipe = &layer_config.right_pipe;

  // left pipe is needed
  if (left_pipe->valid) {
    need_scale = IsScalingNeeded(left_pipe);
    left_index = GetPipe(hw_block_id, need_scale);
    if (left_index >= num_pipe_) {
      DLOGV_IF(kTagResources, "Get left pipe failed: hw_block_id = %d, need_scale = %d",
               hw_block_id, need_scale);
      ResourceStateLog();
      goto CleanupOnError;
    }
  }

  error = SetDecimationFactor(left_pipe);
  if (error != kErrorNone) {
    goto CleanupOnError;
  }

  if (!right_pipe->valid) {
    // assign single pipe
    if (left_index < num_pipe_) {
      left_pipe->pipe_id = src_pipes_[left_index].mdss_pipe_id;
    }
    DLOGV_IF(kTagResources, "1 pipe acquired for FB layer, left_pipe = %x", left_pipe->pipe_id);
    return kErrorNone;
  }

  need_scale = IsScalingNeeded(right_pipe);

  right_index = GetPipe(hw_block_id, need_scale);
  if (right_index >= num_pipe_) {
    DLOGV_IF(kTagResources, "Get right pipe failed: hw_block_id = %d, need_scale = %d", hw_block_id,
             need_scale);
    ResourceStateLog();
    goto CleanupOnError;
  }

  if (src_pipes_[right_index].priority < src_pipes_[left_index].priority) {
    // Swap pipe based on priority
    std::swap(left_index, right_index);
  }

  // assign dual pipes
  left_pipe->pipe_id = src_pipes_[left_index].mdss_pipe_id;
  right_pipe->pipe_id = src_pipes_[right_index].mdss_pipe_id;

  error = SetDecimationFactor(right_pipe);
  if (error != kErrorNone) {
    goto CleanupOnError;
  }

  DLOGV_IF(kTagResources, "2 pipes acquired for FB layer, left_pipe = %x, right_pipe = %x",
           left_pipe->pipe_id,  right_pipe->pipe_id);

  return kErrorNone;

CleanupOnError:
  DLOGV_IF(kTagResources, "Resource reserving failed! hw_block = %d", hw_block_id);

  return kErrorResources;
}

DisplayError ResourceDefault::PostPrepare(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);

  return kErrorNone;
}

DisplayError ResourceDefault::Commit(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);

  return kErrorNone;
}

DisplayError ResourceDefault::PostCommit(Handle display_ctx, HWLayers *hw_layers) {
  SCOPE_LOCK(locker_);
  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);
  HWBlockType hw_block_id = display_resource_ctx->hw_block_id;
  uint64_t frame_count = display_resource_ctx->frame_count;

  DLOGV_IF(kTagResources, "Resource for hw_block = %d, frame_count = %d", hw_block_id, frame_count);

  // handoff pipes which are used by splash screen
  if ((frame_count == 0) && (hw_block_id == kHWPrimary)) {
    for (uint32_t i = 0; i < num_pipe_; i++) {
      if (src_pipes_[i].hw_block_id == hw_block_id && src_pipes_[i].owner == kPipeOwnerKernelMode) {
        src_pipes_[i].owner = kPipeOwnerUserMode;
      }
    }
  }

  if (hw_layers->info.sync_handle >= 0)
    Sys::close_(hw_layers->info.sync_handle);

  display_resource_ctx->frame_count++;

  return kErrorNone;
}

void ResourceDefault::Purge(Handle display_ctx) {
  SCOPE_LOCK(locker_);

  DisplayResourceContext *display_resource_ctx =
                          reinterpret_cast<DisplayResourceContext *>(display_ctx);
  HWBlockType hw_block_id = display_resource_ctx->hw_block_id;

  for (uint32_t i = 0; i < num_pipe_; i++) {
    if (src_pipes_[i].hw_block_id == hw_block_id && src_pipes_[i].owner == kPipeOwnerUserMode) {
      src_pipes_[i].ResetState();
    }
  }
  DLOGV_IF(kTagResources, "display id = %d", display_resource_ctx->hw_block_id);
}

DisplayError ResourceDefault::SetMaxMixerStages(Handle display_ctx, uint32_t max_mixer_stages) {
  SCOPE_LOCK(locker_);

  return kErrorNone;
}

uint32_t ResourceDefault::SearchPipe(HWBlockType hw_block_id, SourcePipe *src_pipes,
                                uint32_t num_pipe) {
  uint32_t index = num_pipe_;
  SourcePipe *src_pipe;

  // search the pipe being used
  for (uint32_t i = 0; i < num_pipe; i++) {
    src_pipe = &src_pipes[i];
    if (src_pipe->owner == kPipeOwnerUserMode && src_pipe->hw_block_id == kHWBlockMax) {
      index = src_pipe->index;
      src_pipe->hw_block_id = hw_block_id;
      break;
    }
  }

  return index;
}

uint32_t ResourceDefault::NextPipe(PipeType type, HWBlockType hw_block_id) {
  uint32_t num_pipe = 0;
  SourcePipe *src_pipes = NULL;

  switch (type) {
  case kPipeTypeVIG:
    src_pipes = &src_pipes_[0];
    num_pipe = hw_res_info_.num_vig_pipe;
    break;
  case kPipeTypeRGB:
    src_pipes = &src_pipes_[hw_res_info_.num_vig_pipe];
    num_pipe = hw_res_info_.num_rgb_pipe;
    break;
  case kPipeTypeDMA:
  default:
    src_pipes = &src_pipes_[hw_res_info_.num_vig_pipe + hw_res_info_.num_rgb_pipe];
    num_pipe = hw_res_info_.num_dma_pipe;
    break;
  }

  return SearchPipe(hw_block_id, src_pipes, num_pipe);
}

uint32_t ResourceDefault::GetPipe(HWBlockType hw_block_id, bool need_scale) {
  uint32_t index = num_pipe_;

  // The default behavior is to assume RGB and VG pipes have scalars
  if (!need_scale) {
    index = NextPipe(kPipeTypeDMA, hw_block_id);
  }

  if ((index >= num_pipe_) && (!need_scale || !hw_res_info_.has_non_scalar_rgb)) {
    index = NextPipe(kPipeTypeRGB, hw_block_id);
  }

  if (index >= num_pipe_) {
    index = NextPipe(kPipeTypeVIG, hw_block_id);
  }

  return index;
}

bool ResourceDefault::IsScalingNeeded(const HWPipeInfo *pipe_info) {
  const LayerRect &src_roi = pipe_info->src_roi;
  const LayerRect &dst_roi = pipe_info->dst_roi;

  return ((dst_roi.right - dst_roi.left) != (src_roi.right - src_roi.left)) ||
          ((dst_roi.bottom - dst_roi.top) != (src_roi.bottom - src_roi.top));
}

void ResourceDefault::ResourceStateLog() {
  DLOGV_IF(kTagResources, "==== resource manager pipe state ====");
  uint32_t i;
  for (i = 0; i < num_pipe_; i++) {
    SourcePipe *src_pipe = &src_pipes_[i];
    DLOGV_IF(kTagResources, "index = %d, id = %x, hw_block = %d, owner = %s",
                 src_pipe->index, src_pipe->mdss_pipe_id, src_pipe->hw_block_id,
                 (src_pipe->owner == kPipeOwnerUserMode) ? "user mode" : "kernel mode");
  }
}

DisplayError ResourceDefault::SrcSplitConfig(DisplayResourceContext *display_resource_ctx,
                                        const LayerRect &src_rect, const LayerRect &dst_rect,
                                        HWLayerConfig *layer_config) {
  HWPipeInfo *left_pipe = &layer_config->left_pipe;
  HWPipeInfo *right_pipe = &layer_config->right_pipe;
  float src_width = src_rect.right - src_rect.left;
  float dst_width = dst_rect.right - dst_rect.left;

  // Layer cannot qualify for SrcSplit if source or destination width exceeds max pipe width.
  if ((src_width > hw_res_info_.max_pipe_width) || (dst_width > hw_res_info_.max_pipe_width)) {
    SplitRect(src_rect, dst_rect, &left_pipe->src_roi, &left_pipe->dst_roi, &right_pipe->src_roi,
              &right_pipe->dst_roi);
    left_pipe->valid = true;
    right_pipe->valid = true;
  } else {
    left_pipe->src_roi = src_rect;
    left_pipe->dst_roi = dst_rect;
    left_pipe->valid = true;
    *right_pipe = {};
  }

  return kErrorNone;
}

DisplayError ResourceDefault::DisplaySplitConfig(DisplayResourceContext *display_resource_ctx,
                                            const LayerRect &src_rect, const LayerRect &dst_rect,
                                            HWLayerConfig *layer_config) {
  HWMixerAttributes &mixer_attributes = display_resource_ctx->mixer_attributes;

  // for display split case
  HWPipeInfo *left_pipe = &layer_config->left_pipe;
  HWPipeInfo *right_pipe = &layer_config->right_pipe;
  LayerRect scissor_left, scissor_right, dst_left, crop_left, crop_right, dst_right;

  scissor_left.right = FLOAT(mixer_attributes.split_left);
  scissor_left.bottom = FLOAT(mixer_attributes.height);

  scissor_right.left = FLOAT(mixer_attributes.split_left);
  scissor_right.top = 0.0f;
  scissor_right.right = FLOAT(mixer_attributes.width);
  scissor_right.bottom = FLOAT(mixer_attributes.height);

  crop_left = src_rect;
  dst_left = dst_rect;
  crop_right = crop_left;
  dst_right = dst_left;

  bool crop_left_valid = CalculateCropRects(scissor_left, &crop_left, &dst_left);
  bool crop_right_valid = false;

  if (IsValid(scissor_right)) {
    crop_right_valid = CalculateCropRects(scissor_right, &crop_right, &dst_right);
  }

  // Reset left_pipe and right_pipe to invalid by default
  *left_pipe = {};
  *right_pipe = {};

  if (crop_left_valid) {
    // assign left pipe
    left_pipe->src_roi = crop_left;
    left_pipe->dst_roi = dst_left;
    left_pipe->valid = true;
  }

  // assign right pipe if needed
  if (crop_right_valid) {
    right_pipe->src_roi = crop_right;
    right_pipe->dst_roi = dst_right;
    right_pipe->valid = true;
  }

  return kErrorNone;
}

DisplayError ResourceDefault::Config(DisplayResourceContext *display_resource_ctx,
                                HWLayers *hw_layers) {
  HWLayersInfo &layer_info = hw_layers->info;
  DisplayError error = kErrorNone;
  const Layer &layer = layer_info.hw_layers.at(0);

  error = ValidateLayerParams(&layer);
  if (error != kErrorNone) {
    return error;
  }

  struct HWLayerConfig *layer_config = &hw_layers->config[0];
  HWPipeInfo &left_pipe = layer_config->left_pipe;
  HWPipeInfo &right_pipe = layer_config->right_pipe;

  LayerRect src_rect = layer.src_rect;
  LayerRect dst_rect = layer.dst_rect;

  error = ValidateDimensions(src_rect, dst_rect);
  if (error != kErrorNone) {
    return error;
  }

  BufferLayout layout = GetBufferLayout(layer.input_buffer.format);
  error = ValidateScaling(src_rect, dst_rect, false /*rotated90 */, layout,
                          false /* use_rotator_downscale */);
  if (error != kErrorNone) {
    return error;
  }

  if (hw_res_info_.is_src_split) {
    error = SrcSplitConfig(display_resource_ctx, src_rect, dst_rect, layer_config);
  } else {
    error = DisplaySplitConfig(display_resource_ctx, src_rect, dst_rect, layer_config);
  }

  if (error != kErrorNone) {
    return error;
  }

  error = AlignPipeConfig(&layer, &left_pipe, &right_pipe);
  if (error != kErrorNone) {
    return error;
  }

  // set z_order, left_pipe should always be valid
  left_pipe.z_order = 0;

  DLOGV_IF(kTagResources, "==== FB layer Config ====");
  Log(kTagResources, "input layer src_rect", layer.src_rect);
  Log(kTagResources, "input layer dst_rect", layer.dst_rect);
  Log(kTagResources, "cropped src_rect", src_rect);
  Log(kTagResources, "cropped dst_rect", dst_rect);
  Log(kTagResources, "left pipe src", layer_config->left_pipe.src_roi);
  Log(kTagResources, "left pipe dst", layer_config->left_pipe.dst_roi);
  if (right_pipe.valid) {
    right_pipe.z_order = 0;
    Log(kTagResources, "right pipe src", layer_config->right_pipe.src_roi);
    Log(kTagResources, "right pipe dst", layer_config->right_pipe.dst_roi);
  }

  return error;
}

bool ResourceDefault::CalculateCropRects(const LayerRect &scissor, LayerRect *crop,
                                         LayerRect *dst) {
  float &crop_left = crop->left;
  float &crop_top = crop->top;
  float &crop_right = crop->right;
  float &crop_bottom = crop->bottom;
  float crop_width = crop->right - crop->left;
  float crop_height = crop->bottom - crop->top;

  float &dst_left = dst->left;
  float &dst_top = dst->top;
  float &dst_right = dst->right;
  float &dst_bottom = dst->bottom;
  float dst_width = dst->right - dst->left;
  float dst_height = dst->bottom - dst->top;

  const float &sci_left = scissor.left;
  const float &sci_top = scissor.top;
  const float &sci_right = scissor.right;
  const float &sci_bottom = scissor.bottom;

  float left_cut_ratio = 0.0, right_cut_ratio = 0.0, top_cut_ratio = 0.0, bottom_cut_ratio = 0.0;
  bool need_cut = false;

  if (dst_left < sci_left) {
    left_cut_ratio = (sci_left - dst_left) / dst_width;
    dst_left = sci_left;
    need_cut = true;
  }

  if (dst_right > sci_right) {
    right_cut_ratio = (dst_right - sci_right) / dst_width;
    dst_right = sci_right;
    need_cut = true;
  }

  if (dst_top < sci_top) {
    top_cut_ratio = (sci_top - dst_top) / (dst_height);
    dst_top = sci_top;
    need_cut = true;
  }

  if (dst_bottom > sci_bottom) {
    bottom_cut_ratio = (dst_bottom - sci_bottom) / (dst_height);
    dst_bottom = sci_bottom;
    need_cut = true;
  }

  if (!need_cut)
    return true;

  crop_left += crop_width * left_cut_ratio;
  crop_top += crop_height * top_cut_ratio;
  crop_right -= crop_width * right_cut_ratio;
  crop_bottom -= crop_height * bottom_cut_ratio;
  Normalize(1, 1, crop);
  Normalize(1, 1, dst);
  if (IsValid(*crop) && IsValid(*dst))
    return true;
  else
    return false;
}

DisplayError ResourceDefault::ValidateLayerParams(const Layer *layer) {
  const LayerRect &src = layer->src_rect;
  const LayerRect &dst = layer->dst_rect;
  const LayerBuffer &input_buffer = layer->input_buffer;

  if (input_buffer.format == kFormatInvalid) {
    DLOGV_IF(kTagResources, "Invalid input buffer format %d", input_buffer.format);
    return kErrorNotSupported;
  }

  if (!IsValid(src) || !IsValid(dst)) {
    Log(kTagResources, "input layer src_rect", src);
    Log(kTagResources, "input layer dst_rect", dst);
    return kErrorNotSupported;
  }

  // Make sure source in integral only if it is a non secure layer.
  if (!input_buffer.flags.secure &&
      ((src.left - roundf(src.left) != 0.0f) ||
       (src.top - roundf(src.top) != 0.0f) ||
       (src.right - roundf(src.right) != 0.0f) ||
       (src.bottom - roundf(src.bottom) != 0.0f))) {
    DLOGV_IF(kTagResources, "Input ROI is not integral");
    return kErrorNotSupported;
  }

  return kErrorNone;
}

DisplayError ResourceDefault::ValidateDimensions(const LayerRect &crop, const LayerRect &dst) {
  if (!IsValid(crop)) {
    Log(kTagResources, "Invalid crop rect", crop);
    return kErrorNotSupported;
  }

  if (!IsValid(dst)) {
    Log(kTagResources, "Invalid dst rect", dst);
    return kErrorNotSupported;
  }

  float crop_width = crop.right - crop.left;
  float crop_height = crop.bottom - crop.top;
  float dst_width = dst.right - dst.left;
  float dst_height = dst.bottom - dst.top;

  if ((UINT32(crop_width - dst_width) == 1) || (UINT32(crop_height - dst_height) == 1)) {
    DLOGV_IF(kTagResources, "One pixel downscaling detected crop_w = %.0f, dst_w = %.0f, " \
             "crop_h = %.0f, dst_h = %.0f", crop_width, dst_width, crop_height, dst_height);
    return kErrorNotSupported;
  }

  return kErrorNone;
}

DisplayError ResourceDefault::ValidatePipeParams(HWPipeInfo *pipe_info, LayerBufferFormat format) {
  DisplayError error = kErrorNone;

  const LayerRect &src_rect = pipe_info->src_roi;
  const LayerRect &dst_rect = pipe_info->dst_roi;

  error = ValidateDimensions(src_rect, dst_rect);
  if (error != kErrorNone) {
    return error;
  }

  BufferLayout layout = GetBufferLayout(format);
  error = ValidateScaling(src_rect, dst_rect, false /* rotated90 */, layout,
                          false /* use_rotator_downscale */);
  if (error != kErrorNone) {
    return error;
  }

  return kErrorNone;
}

DisplayError ResourceDefault::ValidateScaling(const LayerRect &crop, const LayerRect &dst,
                                              bool rotate90, BufferLayout layout,
                                              bool use_rotator_downscale) {
  DisplayError error = kErrorNone;

  float scale_x = 1.0f;
  float scale_y = 1.0f;

  error = GetScaleFactor(crop, dst, &scale_x, &scale_y);
  if (error != kErrorNone) {
    return error;
  }

  error = ValidateDownScaling(scale_x, scale_y, (layout != kLinear));
  if (error != kErrorNone) {
    return error;
  }

  error = ValidateUpScaling(scale_x, scale_y);
  if (error != kErrorNone) {
    return error;
  }

  return kErrorNone;
}

DisplayError ResourceDefault::ValidateDownScaling(float scale_x, float scale_y, bool ubwc_tiled) {
  if ((UINT32(scale_x) > 1) || (UINT32(scale_y) > 1)) {
    float max_scale_down = FLOAT(hw_res_info_.max_scale_down);

    // MDP H/W cannot apply decimation on UBWC tiled framebuffer
    if (!ubwc_tiled && hw_res_info_.has_decimation) {
      max_scale_down *= FLOAT(kMaxDecimationDownScaleRatio);
    }

    if (scale_x > max_scale_down || scale_y > max_scale_down) {
      DLOGV_IF(kTagResources,
               "Scaling down is over the limit: scale_x = %.0f, scale_y = %.0f, " \
               "has_deci = %d", scale_x, scale_y, hw_res_info_.has_decimation);
      return kErrorNotSupported;
    }
  }

  DLOGV_IF(kTagResources, "scale_x = %.4f, scale_y = %.4f", scale_x, scale_y);

  return kErrorNone;
}

DisplayError ResourceDefault::ValidateUpScaling(float scale_x, float scale_y) {
  float max_scale_up = FLOAT(hw_res_info_.max_scale_up);

  if (UINT32(scale_x) < 1 && scale_x > 0.0f) {
    if ((1.0f / scale_x) > max_scale_up) {
      DLOGV_IF(kTagResources, "Scaling up is over limit scale_x = %f", 1.0f / scale_x);
      return kErrorNotSupported;
    }
  }

  if (UINT32(scale_y) < 1 && scale_y > 0.0f) {
    if ((1.0f / scale_y) > max_scale_up) {
      DLOGV_IF(kTagResources, "Scaling up is over limit scale_y = %f", 1.0f / scale_y);
      return kErrorNotSupported;
    }
  }

  DLOGV_IF(kTagResources, "scale_x = %.4f, scale_y = %.4f", scale_x, scale_y);

  return kErrorNone;
}

DisplayError ResourceDefault::GetScaleFactor(const LayerRect &crop, const LayerRect &dst,
                                        float *scale_x, float *scale_y) {
  float crop_width = crop.right - crop.left;
  float crop_height = crop.bottom - crop.top;
  float dst_width = dst.right - dst.left;
  float dst_height = dst.bottom - dst.top;

  *scale_x = crop_width / dst_width;
  *scale_y = crop_height / dst_height;

  return kErrorNone;
}

DisplayError ResourceDefault::SetDecimationFactor(HWPipeInfo *pipe) {
  float src_h = pipe->src_roi.bottom - pipe->src_roi.top;
  float dst_h = pipe->dst_roi.bottom - pipe->dst_roi.top;
  float down_scale_h = src_h / dst_h;

  float src_w = pipe->src_roi.right - pipe->src_roi.left;
  float dst_w = pipe->dst_roi.right - pipe->dst_roi.left;
  float down_scale_w = src_w / dst_w;

  pipe->horizontal_decimation = 0;
  pipe->vertical_decimation = 0;

  if (CalculateDecimation(down_scale_w, &pipe->horizontal_decimation) != kErrorNone) {
    return kErrorNotSupported;
  }

  if (CalculateDecimation(down_scale_h, &pipe->vertical_decimation) != kErrorNone) {
    return kErrorNotSupported;
  }

  DLOGI_IF(kTagResources, "horizontal_decimation %d, vertical_decimation %d",
           pipe->horizontal_decimation, pipe->vertical_decimation);

  return kErrorNone;
}

void ResourceDefault::SplitRect(const LayerRect &src_rect, const LayerRect &dst_rect,
                           LayerRect *src_left, LayerRect *dst_left, LayerRect *src_right,
                           LayerRect *dst_right) {
  // Split rectangle horizontally and evenly into two.
  float src_width = src_rect.right - src_rect.left;
  float dst_width = dst_rect.right - dst_rect.left;
  float src_width_ori = src_width;
  src_width = ROUND_UP_ALIGN_DOWN(src_width / 2, 1);
  dst_width = ROUND_UP_ALIGN_DOWN(dst_width * src_width / src_width_ori, 1);

  src_left->left = src_rect.left;
  src_left->right = src_rect.left + src_width;
  src_right->left = src_left->right;
  src_right->right = src_rect.right;

  src_left->top = src_rect.top;
  src_left->bottom = src_rect.bottom;
  src_right->top = src_rect.top;
  src_right->bottom = src_rect.bottom;

  dst_left->top = dst_rect.top;
  dst_left->bottom = dst_rect.bottom;
  dst_right->top = dst_rect.top;
  dst_right->bottom = dst_rect.bottom;

  dst_left->left = dst_rect.left;
  dst_left->right = dst_rect.left + dst_width;
  dst_right->left = dst_left->right;
  dst_right->right = dst_rect.right;
}

DisplayError ResourceDefault::AlignPipeConfig(const Layer *layer, HWPipeInfo *left_pipe,
                                              HWPipeInfo *right_pipe) {
  DisplayError error = kErrorNone;
  if (!left_pipe->valid) {
    DLOGE_IF(kTagResources, "left_pipe should not be invalid");
    return kErrorNotSupported;
  }

  error = ValidatePipeParams(left_pipe, layer->input_buffer.format);
  if (error != kErrorNone) {
    goto PipeConfigExit;
  }

  if (right_pipe->valid) {
    // Make sure the  left and right ROI are conjunct
    right_pipe->src_roi.left = left_pipe->src_roi.right;
    right_pipe->dst_roi.left = left_pipe->dst_roi.right;
    error = ValidatePipeParams(right_pipe, layer->input_buffer.format);
  }

PipeConfigExit:
  if (error != kErrorNone) {
    DLOGV_IF(kTagResources, "AlignPipeConfig failed");
  }
  return error;
}

DisplayError ResourceDefault::CalculateDecimation(float downscale, uint8_t *decimation) {
  float max_down_scale = FLOAT(hw_res_info_.max_scale_down);

  if (downscale <= max_down_scale) {
    *decimation = 0;
    return kErrorNone;
  } else if (!hw_res_info_.has_decimation) {
    DLOGE("Downscaling exceeds the maximum MDP downscale limit but decimation not enabled");
    return kErrorNotSupported;
  }

  // Decimation is the remaining downscale factor after doing max SDE downscale.
  // In SDE, decimation is supported in powers of 2.
  // For ex: If a pipe needs downscale of 8 but max_down_scale is 4
  // So decimation = powf(2.0, ceilf(log2f(8 / 4))) = powf(2.0, 1.0) = 2
  *decimation = UINT8(ceilf(log2f(downscale / max_down_scale)));
  return kErrorNone;
}

DisplayError ResourceDefault::ValidateCursorConfig(Handle display_ctx, const Layer *layer,
                                                   bool is_top) {
  return kErrorNotSupported;
}

DisplayError ResourceDefault::ValidateAndSetCursorPosition(Handle display_ctx, HWLayers *hw_layers,
                                                           int x, int y,
                                                           DisplayConfigVariableInfo *fb_config) {
  return kErrorNotSupported;
}

DisplayError ResourceDefault::SetMaxBandwidthMode(HWBwModes mode) {
  return kErrorNotSupported;
}

DisplayError ResourceDefault::GetScaleLutConfig(HWScaleLutInfo *lut_info) {
  return kErrorNone;
}

DisplayError ResourceDefault::SetDetailEnhancerData(Handle display_ctx,
                                                    const DisplayDetailEnhancerData &de_data) {
  return kErrorNotSupported;
}

}  // namespace sdm
