/* Copyright (c) 2012 - 2016, The Linux Foundation. All rights reserved.
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

/*! @file blit_engine.h
  @brief Interface file for Blit based compositior.

  @details The client can use this interface to get the blit composition done

*/

#include <hardware/hwcomposer.h>
#include <core/layer_stack.h>
#include <copybit.h>
#include "blit_engine.h"

#ifndef __BLIT_ENGINE_C2D_H__
#define __BLIT_ENGINE_C2D_H__

namespace sdm {

// C2D Blit implemented by the client
// This class implements the BlitEngine Interface which is used to get the
// Blit composition using C2D
class BlitEngineC2d : public BlitEngine {
 public:
  BlitEngineC2d();
  virtual ~BlitEngineC2d();

  virtual int Init();
  virtual void DeInit();
  virtual int Prepare(LayerStack *layer_stack);
  virtual int PreCommit(hwc_display_contents_1_t *content_list, LayerStack *layer_stack);
  virtual int Commit(hwc_display_contents_1_t *content_list, LayerStack *layer_stack);
  virtual void PostCommit(LayerStack *layer_stack);
  virtual bool BlitActive();
  virtual void SetFrameDumpConfig(uint32_t count);


 private:
  static const uint32_t kNumBlitTargetBuffers = 3;

  struct Range {
    int current;
    int end;
  };

  struct RegionIterator : public copybit_region_t {
    explicit RegionIterator(LayerRectArray rect);
   private:
    static int iterate(copybit_region_t const *self, copybit_rect_t *rect);
    LayerRectArray rect_array;
    mutable Range r;
  };

  int AllocateBlitTargetBuffers(uint32_t width, uint32_t height, uint32_t format, uint32_t usage);
  void FreeBlitTargetBuffers();
  int ClearTargetBuffer(private_handle_t* hnd, const LayerRect& rect);
  int DrawRectUsingCopybit(hwc_layer_1_t *hwc_layer, Layer *layer, LayerRect blit_rect,
                           LayerRect blit_dest_Rect);
  void SetReleaseFence(int fence_fd);
  void DumpBlitTargetBuffer(int fd);

  copybit_device_t *blit_engine_c2d_ = NULL;
  private_handle_t *blit_target_buffer_[kNumBlitTargetBuffers];
  uint32_t current_blit_target_index_ = 0;
  int release_fence_fd_[kNumBlitTargetBuffers];
  uint32_t num_blit_target_ = 0;
  int blit_target_start_index_ = 0;
  bool blit_active_ = false;
  uint32_t dump_frame_count_ = 0;
  uint32_t dump_frame_index_ = 0;
};

}  // namespace sdm

#endif  // __BLIT_ENGINE_C2D_H__
