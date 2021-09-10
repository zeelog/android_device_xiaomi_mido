/*
* Copyright (c) 2019, The Linux Foundation. All rights reserved.
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

#include "hwc_display_dummy.h"

#define __CLASS__ "HWCDisplayDummy"

namespace sdm {

int HWCDisplayDummy::Create(CoreInterface *core_intf, BufferAllocator *buffer_allocator,
                            HWCCallbacks *callbacks, qService::QService *qservice,
                            HWCDisplay **hwc_display) {
  HWCDisplay *hwc_display_dummy = new HWCDisplayDummy(core_intf, buffer_allocator,
                                                      callbacks, qservice);
  *hwc_display = hwc_display_dummy;
  return kErrorNone;
}


HWC2::Error HWCDisplayDummy::Validate(uint32_t *out_num_types, uint32_t *out_num_requests) {
  validated_.set(type_);
  PrepareLayerStack(out_num_types, out_num_requests);
  return HWC2::Error::None;
}

HWC2::Error HWCDisplayDummy::Present(int32_t *out_retire_fence) {
   for (auto hwc_layer : layer_set_) {
    hwc_layer->ResetGeometryChanges();
    Layer *layer = hwc_layer->GetSDMLayer();
    LayerBuffer *layer_buffer = &layer->input_buffer;
    hwc_layer->PushReleaseFence(-1);
    layer_buffer->release_fence_fd = -1;
    if (layer_buffer->acquire_fence_fd >= 0) {
      close(layer_buffer->acquire_fence_fd);
      layer_buffer->acquire_fence_fd = -1;
    }
    layer->request.flags = {};
  }

  *out_retire_fence = -1;
  return HWC2::Error::None;
}

HWCDisplayDummy::HWCDisplayDummy(CoreInterface *core_intf, BufferAllocator *buffer_allocator,
                                 HWCCallbacks *callbacks, qService::QService *qservice) :HWCDisplay(core_intf,
                                 callbacks, kPrimary, HWC_DISPLAY_PRIMARY, true, qservice,
                                 DISPLAY_CLASS_NULL, buffer_allocator) {
  DisplayConfigVariableInfo config;
  config.x_pixels = 1920;
  config.y_pixels = 1080;
  config.x_dpi = 200.0f;
  config.y_dpi = 200.0f;
  config.fps = 60;
  config.vsync_period_ns = 16600000;
  display_null_.SetFrameBufferConfig(config);
  num_configs_ = 1;
  display_intf_ = &display_null_;
  client_target_ = new HWCLayer(id_, buffer_allocator_);
  current_refresh_rate_ = max_refresh_rate_ = 60;
}

}  // namespace sdm
