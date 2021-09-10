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

#include "display_null.h"

#define __CLASS__ "DisplayNull"

namespace sdm {

DisplayError DisplayNull::Commit(LayerStack *layer_stack) {
  for (Layer *layer : layer_stack->layers) {
    if (layer->composition != kCompositionGPUTarget) {
      layer->composition = kCompositionSDE;
      layer->input_buffer.release_fence_fd = -1;
    }
  }
  layer_stack->retire_fence_fd = -1;

  return kErrorNone;
}

DisplayError DisplayNull::GetDisplayState(DisplayState *state) {
  *state = state_;
  return kErrorNone;
}

DisplayError DisplayNull::SetDisplayState(DisplayState state) {
  state_ = state;
  return kErrorNone;
}

DisplayError DisplayNull::SetFrameBufferConfig(const DisplayConfigVariableInfo &variable_info) {
  fb_config_ = variable_info;
  return kErrorNone;
}

DisplayError DisplayNull::GetFrameBufferConfig(DisplayConfigVariableInfo *variable_info) {
  *variable_info = fb_config_;
  return kErrorNone;
}

}  // namespace sdm
