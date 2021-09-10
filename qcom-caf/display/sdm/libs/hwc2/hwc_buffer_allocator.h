/*
* Copyright (c) 2015-2017, The Linux Foundation. All rights reserved.
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
#ifdef USE_GRALLOC1
#ifndef __HWC_BUFFER_ALLOCATOR_H__
#define __HWC_BUFFER_ALLOCATOR_H__

#include <fcntl.h>
#include <sys/mman.h>

#include <hardware/gralloc1.h>
#include "gralloc_priv.h"

namespace sdm {

template <class Type>
inline Type ALIGN(Type x, Type align) {
  return (x + align - 1) & ~(align - 1);
}

class HWCBufferAllocator : public BufferAllocator {
 public:
  DisplayError Init();
  DisplayError Deinit();
  DisplayError AllocateBuffer(BufferInfo *buffer_info);
  DisplayError FreeBuffer(BufferInfo *buffer_info);
  uint32_t GetBufferSize(BufferInfo *buffer_info);

  void GetCustomWidthAndHeight(const private_handle_t *handle, int *width, int *height);
  void GetAlignedWidthAndHeight(int width, int height, int format, uint32_t alloc_type,
                                int *aligned_width, int *aligned_height);
  DisplayError GetAllocatedBufferInfo(const BufferConfig &buffer_config,
                                      AllocatedBufferInfo *allocated_buffer_info);
  DisplayError GetBufferLayout(const AllocatedBufferInfo &buf_info,
                               uint32_t stride[4], uint32_t offset[4],
                               uint32_t *num_planes);
  int SetBufferInfo(LayerBufferFormat format, int *target, uint64_t *flags);
  DisplayError MapBuffer(const private_handle_t *handle, int acquire_fence);
  DisplayError UnmapBuffer(const private_handle_t *handle, int* release_fence);

 private:
  gralloc1_device_t *gralloc_device_ = nullptr;
  const hw_module_t *module_;
  GRALLOC1_PFN_RELEASE ReleaseBuffer_ = nullptr;
  GRALLOC1_PFN_PERFORM Perform_ = nullptr;
  GRALLOC1_PFN_LOCK Lock_ = nullptr;
  GRALLOC1_PFN_UNLOCK Unlock_ = nullptr;
};

}  // namespace sdm
#endif  // __HWC_BUFFER_ALLOCATOR_H__
#else
#include "../hwc/hwc_buffer_allocator.h"
#endif  // __HWC_BUFFER_ALLOCATOR_H__

