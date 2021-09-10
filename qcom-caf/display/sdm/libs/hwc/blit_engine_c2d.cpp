/* Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*  * Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above
*    copyright notice, this list of conditions and the following
*    disclaimer in the documentation and/or other materials provided
*    with the distribution.
*  * Neither the name of The Linux Foundation nor the names of its
*    contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
*
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

* Portions formerly licensed under Apache License, Version 2.0, are re licensed
* under section 4 of Apache License, Version 2.0.

* Copyright (C) 2010 The Android Open Source Project

* Not a Contribution.

* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at

* http://www.apache.org/licenses/LICENSE-2.0

* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include <hardware/hardware.h>
#include <sync/sync.h>
#include <copybit.h>
#include <memalloc.h>
#include <alloc_controller.h>
#include <gr.h>

#include <utils/constants.h>
#include <utils/rect.h>
#include <utils/formats.h>
#include <algorithm>

#include "blit_engine_c2d.h"
#include "hwc_debugger.h"

#define __CLASS__ "BlitEngineC2D"

// TODO(user): Remove pragma after fixing sign conversion errors
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif

namespace sdm {


BlitEngineC2d::RegionIterator::RegionIterator(LayerRectArray rect) {
  rect_array = rect;
  r.end = INT(rect.count);
  r.current = 0;
  this->next = iterate;
}

int BlitEngineC2d::RegionIterator::iterate(copybit_region_t const *self, copybit_rect_t *rect) {
  if (!self || !rect) {
    DLOGE("iterate invalid parameters");
    return 0;
  }

  RegionIterator const *me = static_cast<RegionIterator const*>(self);
  if (me->r.current != me->r.end) {
    rect->l = INT(me->rect_array.rect[me->r.current].left);
    rect->t = INT(me->rect_array.rect[me->r.current].top);
    rect->r = INT(me->rect_array.rect[me->r.current].right);
    rect->b = INT(me->rect_array.rect[me->r.current].bottom);
    me->r.current++;
    return 1;
  }
  return 0;
}

BlitEngineC2d::BlitEngineC2d() {
  for (uint32_t i = 0; i < kNumBlitTargetBuffers; i++) {
    blit_target_buffer_[i] = NULL;
    release_fence_fd_[i] = -1;
  }
}

BlitEngineC2d::~BlitEngineC2d() {
  if (blit_engine_c2d_) {
    copybit_close(blit_engine_c2d_);
    blit_engine_c2d_ = NULL;
  }
  FreeBlitTargetBuffers();
}

int BlitEngineC2d::Init() {
  hw_module_t const *module;
  if (hw_get_module("copybit", &module) == 0) {
    if (copybit_open(module, &blit_engine_c2d_) < 0) {
      DLOGI("CopyBitC2D Open failed.");
      return -1;
    }
    DLOGI("Opened Copybit Module");
  } else {
    DLOGI("Copybit HW Module not found");
    return -1;
  }

  return 0;
}

void BlitEngineC2d::DeInit() {
  FreeBlitTargetBuffers();
  if (blit_engine_c2d_) {
    copybit_close(blit_engine_c2d_);
    blit_engine_c2d_ = NULL;
  }
}

int BlitEngineC2d::AllocateBlitTargetBuffers(uint32_t width, uint32_t height, uint32_t format,
                                             uint32_t usage) {
  int status = 0;
  if (width <= 0 || height <= 0) {
    return false;
  }

  if (blit_target_buffer_[0]) {
    // Free and reallocate the buffers if the w/h changes
    if (INT(width) != blit_target_buffer_[0]->width ||
        INT(height) != blit_target_buffer_[0]->height) {
      FreeBlitTargetBuffers();
    }
  }

  for (uint32_t i = 0; i < kNumBlitTargetBuffers; i++) {
    if (blit_target_buffer_[i] == NULL) {
      status = alloc_buffer(&blit_target_buffer_[i], width, height, format, usage);
    }
    if (status < 0) {
      DLOGE("Allocation of Blit target Buffer failed");
      FreeBlitTargetBuffers();
      break;
    }
  }

  return status;
}

void BlitEngineC2d::FreeBlitTargetBuffers() {
  for (uint32_t i = 0; i < kNumBlitTargetBuffers; i++) {
    private_handle_t **target_buffer = &blit_target_buffer_[i];
    if (*target_buffer) {
      // Free the valid fence
      if (release_fence_fd_[i] >= 0) {
        close(release_fence_fd_[i]);
        release_fence_fd_[i] = -1;
      }
      free_buffer(*target_buffer);
      *target_buffer = NULL;
    }
  }
}

int BlitEngineC2d::ClearTargetBuffer(private_handle_t* hnd, const LayerRect& rect) {
  int status = 0;
  copybit_rect_t clear_rect = {INT(rect.left), INT(rect.top), INT(rect.right), INT(rect.bottom)};

  copybit_image_t buffer;
  buffer.w = ALIGN((hnd->width), 32);
  buffer.h = hnd->height;
  buffer.format = hnd->format;
  buffer.base = reinterpret_cast<void *>(hnd->base);
  buffer.handle = reinterpret_cast<native_handle_t *>(hnd);
  int dst_format_mode = COPYBIT_LINEAR;
  if (hnd->flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) {
    dst_format_mode = COPYBIT_UBWC_COMPRESSED;
  }
  blit_engine_c2d_->set_parameter(blit_engine_c2d_, COPYBIT_DST_FORMAT_MODE, dst_format_mode);

  status = blit_engine_c2d_->clear(blit_engine_c2d_, &buffer, &clear_rect);
  return status;
}

void BlitEngineC2d::PostCommit(LayerStack *layer_stack) {
  int fence_fd = -1;
  uint32_t count = 0;
  int fd = -1;

  for (uint32_t i = blit_target_start_index_-2; (i > 0) && (count < num_blit_target_); i--) {
    Layer *layer = layer_stack->layers.at(i);
    LayerBuffer &layer_buffer = layer->input_buffer;
    if (layer->composition == kCompositionBlit) {
      int index = blit_target_start_index_ + count;
      layer_buffer.release_fence_fd =
        layer_stack->layers.at(index)->input_buffer.release_fence_fd;
      fence_fd = layer_buffer.release_fence_fd;
      close(layer_buffer.acquire_fence_fd);
      layer_buffer.acquire_fence_fd = -1;
      layer_stack->layers.at(index)->input_buffer.release_fence_fd = -1;
      fd = layer_stack->layers.at(index)->input_buffer.acquire_fence_fd;
      layer_stack->layers.at(index)->input_buffer.acquire_fence_fd = -1;
      count++;
    }
  }

  if (fd >= 0) {
    // Close the C2D fence FD
    close(fd);
  }
  SetReleaseFence(fence_fd);
}

// Sync wait to close the previous fd
void BlitEngineC2d::SetReleaseFence(int fd) {
  if (release_fence_fd_[current_blit_target_index_] >= 0) {
    int ret = -1;
    ret = sync_wait(release_fence_fd_[current_blit_target_index_], 1000);
    if (ret < 0) {
      DLOGE("sync_wait error! errno = %d, err str = %s", errno, strerror(errno));
    }
    close(release_fence_fd_[current_blit_target_index_]);
  }
  release_fence_fd_[current_blit_target_index_] = dup(fd);
}

bool BlitEngineC2d::BlitActive() {
  return blit_active_;
}

void BlitEngineC2d::SetFrameDumpConfig(uint32_t count) {
  dump_frame_count_ = count;
  dump_frame_index_ = 0;
}

int BlitEngineC2d::Prepare(LayerStack *layer_stack) {
  blit_target_start_index_ = 0;

  uint32_t layer_count = UINT32(layer_stack->layers.size());
  uint32_t gpu_target_index = layer_count - 1;  // default assumption
  uint32_t i = 0;

  for (; i < layer_count; i++) {
    Layer *layer = layer_stack->layers.at(i);

    // No 10 bit support for C2D
    if (Is10BitFormat(layer->input_buffer.format)) {
      return -1;
    }

    if (layer->composition == kCompositionGPUTarget) {
      // Need FBT size for allocating buffers
      gpu_target_index = i;
      break;
    }
  }

  if ((layer_count - 1) == gpu_target_index) {
    // No blit target layer
    return -1;
  }

  blit_target_start_index_ = ++i;
  num_blit_target_ = layer_count - blit_target_start_index_;

  LayerBuffer &layer_buffer = layer_stack->layers.at(gpu_target_index)->input_buffer;
  int fbwidth = INT(layer_buffer.unaligned_width);
  int fbheight = INT(layer_buffer.unaligned_height);
  if ((fbwidth < 0) || (fbheight < 0)) {
    return -1;
  }

  current_blit_target_index_ = (current_blit_target_index_ + 1) % kNumBlitTargetBuffers;
  int k = blit_target_start_index_;

  for (uint32_t j = 0; j < num_blit_target_; j++, k++) {
    Layer *layer = layer_stack->layers.at(k);
    LayerBuffer &layer_buffer = layer->input_buffer;
    int aligned_w = 0;
    int aligned_h = 0;

    // Set the buffer height and width
    AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(fbwidth, fbheight/3,
                   INT(HAL_PIXEL_FORMAT_RGBA_8888), 0, aligned_w, aligned_h);
    layer_buffer.width = aligned_w;
    layer_buffer.height = aligned_h;
    layer_buffer.unaligned_width = fbwidth;
    layer_buffer.unaligned_height = fbheight/3;

    layer->plane_alpha = 0xFF;
    layer->blending = kBlendingOpaque;
    layer->composition = kCompositionBlitTarget;
    layer->frame_rate = layer_stack->layers.at(gpu_target_index)->frame_rate;
  }

  return 0;
}

int BlitEngineC2d::PreCommit(hwc_display_contents_1_t *content_list, LayerStack *layer_stack) {
  int status = 0;
  uint32_t num_app_layers = (uint32_t) content_list->numHwLayers-1;
  int target_width = 0;
  int target_height = 0;
  int target_aligned_width = 0;
  int target_aligned_height = 0;
  uint32_t processed_blit = 0;
  LayerRect dst_rects[kMaxBlitTargetLayers];
  bool blit_needed = false;
  uint32_t usage = 0;

  if (!num_app_layers) {
    return -1;
  }

  for (uint32_t i = num_app_layers-1; (i > 0) && (processed_blit < num_blit_target_); i--) {
    Layer *layer = layer_stack->layers.at(i);
    if (layer->composition != kCompositionBlit) {
      continue;
    }
    blit_needed = true;
    layer_stack->flags.attributes_changed = true;

    Layer *blit_layer = layer_stack->layers.at(blit_target_start_index_ + processed_blit);
    LayerRect &blit_src_rect = blit_layer->src_rect;
    int width = INT(layer->dst_rect.right - layer->dst_rect.left);
    int height = INT(layer->dst_rect.bottom - layer->dst_rect.top);
    int aligned_w = 0;
    int aligned_h = 0;
    usage = GRALLOC_USAGE_PRIVATE_IOMMU_HEAP | GRALLOC_USAGE_HW_TEXTURE;
    if (blit_engine_c2d_->get(blit_engine_c2d_, COPYBIT_UBWC_SUPPORT) > 0) {
      usage |= GRALLOC_USAGE_PRIVATE_ALLOC_UBWC;
    }
    // TODO(user): FrameBuffer is assumed to be RGBA
    target_width = std::max(target_width, width);
    target_height += height;

    AdrenoMemInfo::getInstance().getAlignedWidthAndHeight(width, height,
                                 INT(HAL_PIXEL_FORMAT_RGBA_8888), usage, aligned_w, aligned_h);

    target_aligned_width = std::max(target_aligned_width, aligned_w);
    target_aligned_height += aligned_h;

    // Left will be zero always
    dst_rects[processed_blit].top = FLOAT(target_aligned_height - aligned_h);
    dst_rects[processed_blit].right = dst_rects[processed_blit].left +
                                      (layer->dst_rect.right - layer->dst_rect.left);
    dst_rects[processed_blit].bottom = (dst_rects[processed_blit].top +
                                      (layer->dst_rect.bottom - layer->dst_rect.top));
    blit_src_rect = dst_rects[processed_blit];
    processed_blit++;
  }

  // Allocate a single buffer of RGBA8888 format
  if (blit_needed && (AllocateBlitTargetBuffers(target_width, target_height,
                                                HAL_PIXEL_FORMAT_RGBA_8888, usage) < 0)) {
      status = -1;
      return status;
  }

  if (blit_needed) {
    for (uint32_t j = 0; j < num_blit_target_; j++) {
      Layer *layer = layer_stack->layers.at(j + content_list->numHwLayers);
      private_handle_t *target_buffer = blit_target_buffer_[current_blit_target_index_];
      // Set the fd information
        layer->input_buffer.width = target_aligned_width;
        layer->input_buffer.height = target_aligned_height;
        layer->input_buffer.unaligned_width = target_width;
        layer->input_buffer.unaligned_height = target_height;
      if (target_buffer->flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) {
          layer->input_buffer.format = kFormatRGBA8888Ubwc;
      }
      layer->input_buffer.planes[0].fd = target_buffer->fd;
      layer->input_buffer.planes[0].offset = 0;
      layer->input_buffer.planes[0].stride = target_buffer->width;
    }
  }

  return status;
}

int BlitEngineC2d::Commit(hwc_display_contents_1_t *content_list, LayerStack *layer_stack) {
  int fd = -1;
  int status = 0;
  bool hybrid_present = false;
  uint32_t num_app_layers = (uint32_t) content_list->numHwLayers-1;
  private_handle_t *target_buffer = blit_target_buffer_[current_blit_target_index_];
  blit_active_ = false;

  if (!num_app_layers) {
    return -1;
  }

  // if not Blit Targets return
  for (uint32_t i = 0; i < num_app_layers; i++) {
    Layer *layer = layer_stack->layers.at(i);
    if (layer->composition == kCompositionHybrid || layer->composition == kCompositionBlit) {
      hybrid_present = true;
    }
  }

  if (!hybrid_present) {
    return status;
  }

  // Clear blit target buffer
  LayerRect clear_rect;
  clear_rect.left =  0;
  clear_rect.top = 0;
  clear_rect.right = FLOAT(target_buffer->width);
  clear_rect.bottom = FLOAT(target_buffer->height);
  ClearTargetBuffer(target_buffer, clear_rect);

  int copybit_layer_count = 0;
  uint32_t processed_blit = 0;
  for (uint32_t i = num_app_layers-1; (i > 0) && (processed_blit < num_blit_target_) &&
      (status == 0); i--) {
    Layer *layer = layer_stack->layers.at(i);
    if (layer->composition != kCompositionBlit) {
      continue;
    }

    for (uint32_t k = 0; k <= i; k++) {
      Layer *bottom_layer = layer_stack->layers.at(k);
      LayerBuffer &layer_buffer = bottom_layer->input_buffer;
      // if layer below the blit layer does not intersect, ignore that layer
      LayerRect inter_sect = Intersection(layer->dst_rect, bottom_layer->dst_rect);
      if (bottom_layer->composition != kCompositionHybrid && !IsValid(inter_sect)) {
        continue;
      }
      if (bottom_layer->composition == kCompositionGPU ||
          bottom_layer->composition == kCompositionSDE ||
          bottom_layer->composition == kCompositionGPUTarget) {
        continue;
      }

      // For each layer marked as Hybrid, wait for acquire fence and then blit using the C2D
      if (layer_buffer.acquire_fence_fd >= 0) {
        // Wait for acquire fence on the App buffers.
        if (sync_wait(layer_buffer.acquire_fence_fd, 1000) < 0) {
          DLOGE("sync_wait error!! error no = %d err str = %s", errno, strerror(errno));
        }
        layer_buffer.acquire_fence_fd = -1;
      }
      hwc_layer_1_t *hwc_layer = &content_list->hwLayers[k];
      LayerRect &src_rect = bottom_layer->blit_regions.at(processed_blit);
      Layer *blit_layer = layer_stack->layers.at(blit_target_start_index_ + processed_blit);
      LayerRect dest_rect = blit_layer->src_rect;
      int ret_val = DrawRectUsingCopybit(hwc_layer, bottom_layer, src_rect, dest_rect);
      copybit_layer_count++;
      if (ret_val < 0) {
        copybit_layer_count = 0;
        DLOGE("DrawRectUsingCopyBit failed");
        status = -1;
        break;
      }
    }
    processed_blit++;
  }

  if (copybit_layer_count) {
    blit_active_ = true;
    blit_engine_c2d_->flush_get_fence(blit_engine_c2d_, &fd);
  }

  if (blit_active_) {
    // dump the render buffer
    DumpBlitTargetBuffer(fd);

    // Set the fd to the LayerStack BlitTargets fd
    uint32_t layer_count = UINT32(layer_stack->layers.size());
    for (uint32_t k = blit_target_start_index_; k < layer_count; k++) {
      Layer *layer = layer_stack->layers.at(k);
      LayerBuffer &layer_buffer = layer->input_buffer;
      layer_buffer.acquire_fence_fd = fd;
    }
  }

  return status;
}

int BlitEngineC2d::DrawRectUsingCopybit(hwc_layer_1_t *hwc_layer, Layer *layer,
                                        LayerRect blit_rect, LayerRect blit_dest_Rect) {
  private_handle_t *target_buffer = blit_target_buffer_[current_blit_target_index_];
  const private_handle_t *hnd = static_cast<const private_handle_t *>(hwc_layer->handle);
  LayerBuffer &layer_buffer = layer->input_buffer;

  // Set the Copybit Source
  copybit_image_t src;
  src.handle = const_cast<native_handle_t *>(hwc_layer->handle);
  src.w = hnd->width;
  src.h = hnd->height;
  src.base = reinterpret_cast<void *>(hnd->base);
  src.format = hnd->format;
  src.horiz_padding = 0;
  src.vert_padding = 0;

  // Copybit source rect
  copybit_rect_t src_rect = {INT(blit_rect.left), INT(blit_rect.top), INT(blit_rect.right),
                            INT(blit_rect.bottom)};

  // Copybit destination rect
  copybit_rect_t dst_rect = {INT(blit_dest_Rect.left), INT(blit_dest_Rect.top),
                            INT(blit_dest_Rect.right), INT(blit_dest_Rect.bottom)};

  // Copybit destination buffer
  copybit_image_t dst;
  dst.handle = static_cast<native_handle_t *>(target_buffer);
  dst.w = ALIGN(target_buffer->width, 32);
  dst.h = ALIGN((target_buffer->height), 32);
  dst.base = reinterpret_cast<void *>(target_buffer->base);
  dst.format = target_buffer->format;

  // Copybit region is the destRect
  LayerRect region_rect;
  region_rect.left = FLOAT(dst_rect.l);
  region_rect.top = FLOAT(dst_rect.t);
  region_rect.right = FLOAT(dst_rect.r);
  region_rect.bottom = FLOAT(dst_rect.b);

  LayerRectArray region;
  region.count = 1;
  region.rect  = &region_rect;
  RegionIterator copybitRegion(region);
  int acquireFd = layer_buffer.acquire_fence_fd;

  // FRAMEBUFFER_WIDTH/HEIGHT for c2d is the target buffer w/h
  blit_engine_c2d_->set_parameter(blit_engine_c2d_, COPYBIT_FRAMEBUFFER_WIDTH,
                                  target_buffer->width);
  blit_engine_c2d_->set_parameter(blit_engine_c2d_, COPYBIT_FRAMEBUFFER_HEIGHT,
                                  target_buffer->height);
  int transform = 0;
  if (layer->transform.rotation != 0.0f) transform |= COPYBIT_TRANSFORM_ROT_90;
  if (layer->transform.flip_horizontal) transform |= COPYBIT_TRANSFORM_FLIP_H;
  if (layer->transform.flip_vertical) transform |= COPYBIT_TRANSFORM_FLIP_V;
  blit_engine_c2d_->set_parameter(blit_engine_c2d_, COPYBIT_TRANSFORM, transform);
  blit_engine_c2d_->set_parameter(blit_engine_c2d_, COPYBIT_PLANE_ALPHA, hwc_layer->planeAlpha);
  blit_engine_c2d_->set_parameter(blit_engine_c2d_, COPYBIT_BLEND_MODE, hwc_layer->blending);
  blit_engine_c2d_->set_parameter(blit_engine_c2d_, COPYBIT_DITHER,
    (dst.format == HAL_PIXEL_FORMAT_RGB_565) ? COPYBIT_ENABLE : COPYBIT_DISABLE);

  int src_format_mode = COPYBIT_LINEAR;
  if (hnd->flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) {
    src_format_mode = COPYBIT_UBWC_COMPRESSED;
  }
  blit_engine_c2d_->set_parameter(blit_engine_c2d_, COPYBIT_SRC_FORMAT_MODE, src_format_mode);

  blit_engine_c2d_->set_sync(blit_engine_c2d_, acquireFd);
  int err = blit_engine_c2d_->stretch(blit_engine_c2d_, &dst, &src, &dst_rect, &src_rect,
                                      &copybitRegion);

  if (err < 0) {
    DLOGE("copybit stretch failed");
  }

  return err;
}

void BlitEngineC2d::DumpBlitTargetBuffer(int fd) {
  if (!dump_frame_count_) {
    return;
  }

  private_handle_t *target_buffer = blit_target_buffer_[current_blit_target_index_];

  if (fd >= 0) {
    int error = sync_wait(fd, 1000);
    if (error < 0) {
      DLOGW("sync_wait error errno = %d, desc = %s", errno, strerror(errno));
      return;
    }
  }

  char dump_file_name[PATH_MAX];
  size_t result = 0;
  snprintf(dump_file_name, sizeof(dump_file_name), "/data/misc/display/frame_dump_primary"
           "/blit_target_%d.raw", (dump_frame_index_));
  FILE* fp = fopen(dump_file_name, "w+");
  if (fp) {
    result = fwrite(reinterpret_cast<void *>(target_buffer->base), target_buffer->size, 1, fp);
    fclose(fp);
  }
  dump_frame_count_--;
  dump_frame_index_++;
}

}  // namespace sdm
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

