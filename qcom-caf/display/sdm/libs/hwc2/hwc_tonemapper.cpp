/*
* Copyright (c) 2016 - 2018, The Linux Foundation. All rights reserved.
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

#include <gralloc_priv.h>
#include <sync/sync.h>

#include <TonemapFactory.h>

#include <core/buffer_allocator.h>

#include <utils/constants.h>
#include <utils/debug.h>
#include <utils/formats.h>
#include <utils/rect.h>
#include <utils/utils.h>

#include <vector>

#include "hwc_debugger.h"
#include "hwc_tonemapper.h"

#define __CLASS__ "HWCToneMapper"

namespace sdm {

ToneMapSession::ToneMapSession(HWCBufferAllocator *buffer_allocator)
  : tone_map_task_(*this), buffer_allocator_(buffer_allocator) {
  buffer_info_.resize(kNumIntermediateBuffers);
}

ToneMapSession::~ToneMapSession() {
  tone_map_task_.PerformTask(ToneMapTaskCode::kCodeDestroy, nullptr);
  FreeIntermediateBuffers();
  buffer_info_.clear();
}

void ToneMapSession::OnTask(const ToneMapTaskCode &task_code,
                            SyncTask<ToneMapTaskCode>::TaskContext *task_context) {
  switch (task_code) {
    case ToneMapTaskCode::kCodeGetInstance: {
        ToneMapGetInstanceContext *ctx = static_cast<ToneMapGetInstanceContext *>(task_context);
        Lut3d &lut_3d = ctx->layer->lut_3d;
        Color10Bit *grid_entries = NULL;
        int grid_size = 0;
        if (lut_3d.validGridEntries) {
          grid_entries = lut_3d.gridEntries;
          grid_size = INT(lut_3d.gridSize);
        }
        gpu_tone_mapper_ = TonemapperFactory_GetInstance(tone_map_config_.type,
                                                         lut_3d.lutEntries, lut_3d.dim,
                                                         grid_entries, grid_size,
                                                         tone_map_config_.secure);
      }
      break;

    case ToneMapTaskCode::kCodeBlit: {
        ToneMapBlitContext *ctx = static_cast<ToneMapBlitContext *>(task_context);
        uint8_t buffer_index = current_buffer_index_;
        const void *dst_hnd = reinterpret_cast<const void *>
                                (buffer_info_[buffer_index].private_data);
        const void *src_hnd = reinterpret_cast<const void *>
                                (ctx->layer->input_buffer.buffer_id);
        ctx->fence_fd = gpu_tone_mapper_->blit(dst_hnd, src_hnd, ctx->merged_fd);
      }
      break;

    case ToneMapTaskCode::kCodeDestroy: {
        delete gpu_tone_mapper_;
      }
      break;

    default:
      break;
  }
}

DisplayError ToneMapSession::AllocateIntermediateBuffers(const Layer *layer) {
  DisplayError error = kErrorNone;
  for (uint8_t i = 0; i < kNumIntermediateBuffers; i++) {
    BufferInfo &buffer_info = buffer_info_[i];
    buffer_info.buffer_config.width = layer->request.width;
    buffer_info.buffer_config.height = layer->request.height;
    buffer_info.buffer_config.format = layer->request.format;
    buffer_info.buffer_config.secure = layer->request.flags.secure;
    buffer_info.buffer_config.gfx_client = true;
    error = buffer_allocator_->AllocateBuffer(&buffer_info);
    if (error != kErrorNone) {
      FreeIntermediateBuffers();
      return error;
    }
  }

  return kErrorNone;
}

void ToneMapSession::FreeIntermediateBuffers() {
  for (uint8_t i = 0; i < kNumIntermediateBuffers; i++) {
    // Free the valid fence
    if (release_fence_fd_[i] >= 0) {
      CloseFd(&release_fence_fd_[i]);
    }
    BufferInfo &buffer_info = buffer_info_[i];
    if (buffer_info.private_data) {
      buffer_allocator_->FreeBuffer(&buffer_info);
    }
  }
}

void ToneMapSession::UpdateBuffer(int acquire_fence, LayerBuffer *buffer) {
  // Acquire fence will be closed by HWC Display.
  // Fence returned by GPU will be closed in PostCommit.
  buffer->acquire_fence_fd = acquire_fence;
  buffer->size = buffer_info_[current_buffer_index_].alloc_buffer_info.size;
  buffer->planes[0].fd = buffer_info_[current_buffer_index_].alloc_buffer_info.fd;
}

void ToneMapSession::SetReleaseFence(int fd) {
  CloseFd(&release_fence_fd_[current_buffer_index_]);
  // Used to give to GPU tonemapper along with input layer fd
  release_fence_fd_[current_buffer_index_] = dup(fd);
}

void ToneMapSession::SetToneMapConfig(Layer *layer) {
  // HDR -> SDR is FORWARD and SDR - > HDR is INVERSE
  tone_map_config_.type = layer->input_buffer.flags.hdr ? TONEMAP_FORWARD : TONEMAP_INVERSE;
  tone_map_config_.colorPrimaries = layer->input_buffer.color_metadata.colorPrimaries;
  tone_map_config_.transfer = layer->input_buffer.color_metadata.transfer;
  tone_map_config_.secure = layer->request.flags.secure;
  tone_map_config_.format = layer->request.format;
}

bool ToneMapSession::IsSameToneMapConfig(Layer *layer) {
  LayerBuffer& buffer = layer->input_buffer;
  private_handle_t *handle = static_cast<private_handle_t *>(buffer_info_[0].private_data);
  int tonemap_type = buffer.flags.hdr ? TONEMAP_FORWARD : TONEMAP_INVERSE;

  return ((tonemap_type == tone_map_config_.type) &&
          (buffer.color_metadata.colorPrimaries == tone_map_config_.colorPrimaries) &&
          (buffer.color_metadata.transfer == tone_map_config_.transfer) &&
          (layer->request.flags.secure == tone_map_config_.secure) &&
          (layer->request.format == tone_map_config_.format) &&
          (layer->request.width == UINT32(handle->unaligned_width)) &&
          (layer->request.height == UINT32(handle->unaligned_height)));
}

int HWCToneMapper::HandleToneMap(LayerStack *layer_stack) {
  uint32_t gpu_count = 0;
  DisplayError error = kErrorNone;

  for (uint32_t i = 0; i < layer_stack->layers.size(); i++) {
    uint32_t session_index = 0;
    Layer *layer = layer_stack->layers.at(i);
    if (layer->composition == kCompositionGPU) {
      gpu_count++;
    }

    if (layer->request.flags.tone_map) {
      DLOGV_IF(kTagClient, "Tonemapping for layer at index %d", i);
      switch (layer->composition) {
      case kCompositionGPUTarget:
        if (!gpu_count) {
          // When all layers are on FrameBuffer and if they do not update in the next draw cycle,
          // then SDM marks them for SDE Composition because the cached FB layer gets displayed.
          // GPU count will be 0 in this case. Try to use the existing tone-mapped frame buffer.
          // No ToneMap/Blit is required. Just update the buffer & acquire fence fd of FB layer.
          if (!tone_map_sessions_.empty() && (fb_session_index_ >= 0)) {
            ToneMapSession *fb_tone_map_session = tone_map_sessions_.at(UINT32(fb_session_index_));
            fb_tone_map_session->UpdateBuffer(-1 /* acquire_fence */, &layer->input_buffer);
            fb_tone_map_session->layer_index_ = INT(i);
            fb_tone_map_session->acquired_ = true;
            return 0;
          }
        }
        error = AcquireToneMapSession(layer, &session_index);
        fb_session_index_ = INT(session_index);
        break;
      default:
        error = AcquireToneMapSession(layer, &session_index);
        break;
      }

      if (error != kErrorNone) {
        Terminate();
        return -1;
      }

      ToneMapSession *session = tone_map_sessions_.at(session_index);
      ToneMap(layer, session);
      DLOGI_IF(kTagClient, "Layer %d associated with session index %d", i, session_index);
      session->layer_index_ = INT(i);
    }
  }

  return 0;
}

void HWCToneMapper::ToneMap(Layer* layer, ToneMapSession *session) {
  ToneMapBlitContext ctx = {};
  ctx.layer = layer;

  uint8_t buffer_index = session->current_buffer_index_;
  int &release_fence_fd = session->release_fence_fd_[buffer_index];

  // use and close the layer->input_buffer acquire fence fd.
  int acquire_fd = layer->input_buffer.acquire_fence_fd;
  buffer_sync_handler_.SyncMerge(release_fence_fd, acquire_fd, &ctx.merged_fd);

  if (acquire_fd >= 0) {
    CloseFd(&acquire_fd);
  }

  if (release_fence_fd >= 0) {
    CloseFd(&release_fence_fd);
  }

  DTRACE_BEGIN("GPU_TM_BLIT");
  session->tone_map_task_.PerformTask(ToneMapTaskCode::kCodeBlit, &ctx);
  DTRACE_END();

  DumpToneMapOutput(session, &ctx.fence_fd);
  session->UpdateBuffer(ctx.fence_fd, &layer->input_buffer);
}

void HWCToneMapper::PostCommit(LayerStack *layer_stack) {
  auto it = tone_map_sessions_.begin();
  while (it != tone_map_sessions_.end()) {
    uint32_t session_index = UINT32(std::distance(tone_map_sessions_.begin(), it));
    ToneMapSession *session = tone_map_sessions_.at(session_index);
    if (session->acquired_) {
      Layer *layer = layer_stack->layers.at(UINT32(session->layer_index_));
      // Close the fd returned by GPU ToneMapper and set release fence.
      LayerBuffer &layer_buffer = layer->input_buffer;
      CloseFd(&layer_buffer.acquire_fence_fd);
      session->SetReleaseFence(layer_buffer.release_fence_fd);
      session->acquired_ = false;
      it++;
    } else {
      DLOGI_IF(kTagClient, "Tone map session %d closed.", session_index);
      delete session;
      it = tone_map_sessions_.erase(it);
      int deleted_session = INT(session_index);
      // If FB tonemap session gets deleted, reset fb_session_index_, else update it.
      if (deleted_session == fb_session_index_) {
        fb_session_index_ = -1;
      } else if (deleted_session < fb_session_index_) {
        fb_session_index_--;
      }
    }
  }
}

void HWCToneMapper::Terminate() {
  if (tone_map_sessions_.size()) {
    while (!tone_map_sessions_.empty()) {
      delete tone_map_sessions_.back();
      tone_map_sessions_.pop_back();
    }
    fb_session_index_ = -1;
  }
}

void HWCToneMapper::SetFrameDumpConfig(uint32_t count) {
  DLOGI("Dump FrameConfig count = %d", count);
  dump_frame_count_ = count;
  dump_frame_index_ = 0;
}

void HWCToneMapper::DumpToneMapOutput(ToneMapSession *session, int *acquire_fd) {
  DisplayError error = kErrorNone;
  if (!dump_frame_count_) {
    return;
  }

  BufferInfo &buffer_info = session->buffer_info_[session->current_buffer_index_];
  private_handle_t *target_buffer = static_cast<private_handle_t *>(buffer_info.private_data);

  if (*acquire_fd >= 0) {
    int error = sync_wait(*acquire_fd, 1000);
    if (error < 0) {
      DLOGW("sync_wait error errno = %d, desc = %s", errno, strerror(errno));
      return;
    }
  }

  error = buffer_allocator_->MapBuffer(target_buffer, *acquire_fd);
  if (error != kErrorNone) {
    DLOGE("MapBuffer failed, base addr = %x", target_buffer->base);
    return;
  }

  size_t result = 0;
  char dump_file_name[PATH_MAX];
  snprintf(dump_file_name, sizeof(dump_file_name), "%s/frame_dump_primary"
           "/tonemap_%dx%d_frame%d.raw", HWCDebugHandler::DumpDir(), target_buffer->width,
           target_buffer->height, dump_frame_index_);

  FILE* fp = fopen(dump_file_name, "w+");
  if (fp) {
    DLOGI("base addr = %x", target_buffer->base);
    result = fwrite(reinterpret_cast<void *>(target_buffer->base), target_buffer->size, 1, fp);
    fclose(fp);
  }
  dump_frame_count_--;
  dump_frame_index_++;
  CloseFd(acquire_fd);
}

DisplayError HWCToneMapper::AcquireToneMapSession(Layer *layer, uint32_t *session_index) {
  // When the property vendor.display.disable_hdr_lut_gen is set, the lutEntries and gridEntries in
  // the Lut3d will be NULL, clients needs to allocate the memory and set correct 3D Lut
  // for Tonemapping.
  if (!layer->lut_3d.lutEntries || !layer->lut_3d.dim) {
    // Atleast lutEntries must be valid for GPU Tonemapper.
    DLOGE("Invalid Lut Entries or lut dimension = %d", layer->lut_3d.dim);
    return kErrorParameters;
  }

  // Check if we can re-use an existing tone map session.
  for (uint32_t i = 0; i < tone_map_sessions_.size(); i++) {
    ToneMapSession *tonemap_session = tone_map_sessions_.at(i);
    if (!tonemap_session->acquired_ && tonemap_session->IsSameToneMapConfig(layer)) {
      tonemap_session->current_buffer_index_ = (tonemap_session->current_buffer_index_ + 1) %
                                                ToneMapSession::kNumIntermediateBuffers;
      tonemap_session->acquired_ = true;
      *session_index = i;
      return kErrorNone;
    }
  }

  ToneMapSession *session = new ToneMapSession(buffer_allocator_);
  if (!session) {
    return kErrorMemory;
  }

  session->SetToneMapConfig(layer);

  ToneMapGetInstanceContext ctx;
  ctx.layer = layer;
  session->tone_map_task_.PerformTask(ToneMapTaskCode::kCodeGetInstance, &ctx);

  if (session->gpu_tone_mapper_ == NULL) {
    DLOGE("Get Tonemapper failed!");
    delete session;
    return kErrorNotSupported;
  }
  DisplayError error = session->AllocateIntermediateBuffers(layer);
  if (error != kErrorNone) {
    DLOGE("Allocation of Intermediate Buffers failed!");
    delete session;
    return error;
  }

  session->acquired_ = true;
  tone_map_sessions_.push_back(session);
  *session_index = UINT32(tone_map_sessions_.size() - 1);

  return kErrorNone;
}

}  // namespace sdm
