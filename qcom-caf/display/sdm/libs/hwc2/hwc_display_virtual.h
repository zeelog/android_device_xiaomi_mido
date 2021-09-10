/*
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

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

#ifndef __HWC_DISPLAY_VIRTUAL_H__
#define __HWC_DISPLAY_VIRTUAL_H__

#include <qdMetaData.h>
#include <gralloc_priv.h>
#include "hwc_display.h"

namespace sdm {

class HWCDisplayVirtual : public HWCDisplay {
 public:
  static int Create(CoreInterface *core_intf, HWCBufferAllocator *buffer_allocator,
                    HWCCallbacks *callbacks, uint32_t width,
                    uint32_t height, int32_t *format, HWCDisplay **hwc_display);
  static void Destroy(HWCDisplay *hwc_display);
  virtual int Init();
  virtual int Deinit();
  virtual HWC2::Error Validate(uint32_t *out_num_types, uint32_t *out_num_requests);
  virtual HWC2::Error Present(int32_t *out_retire_fence);
  virtual void SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type);
  HWC2::Error SetOutputBuffer(buffer_handle_t buf, int32_t release_fence);

 private:
  HWCDisplayVirtual(CoreInterface *core_intf, HWCBufferAllocator *buffer_allocator,
                    HWCCallbacks *callbacks);
  int SetConfig(uint32_t width, uint32_t height);

  bool dump_output_layer_ = false;
  LayerBuffer *output_buffer_ = NULL;
  const private_handle_t *output_handle_ = nullptr;
};

}  // namespace sdm

#endif  // __HWC_DISPLAY_VIRTUAL_H__
