/*
 * Copyright (c) 2011-2017,2019, The Linux Foundation. All rights reserved.
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

#ifndef __GR_ALLOCATOR_H__
#define __GR_ALLOCATOR_H__

#include <vector>

#include "gralloc_priv.h"
#include "gr_buf_descriptor.h"
#include "gr_ion_alloc.h"
#include "gr_utils.h"

namespace gralloc1 {

class Allocator {
 public:
  Allocator();
  ~Allocator();
  bool Init();
  int MapBuffer(void **base, unsigned int size, unsigned int offset, int fd);
  int ImportBuffer(int fd);
  int FreeBuffer(void *base, unsigned int size, unsigned int offset, int fd, int handle);
  int CleanBuffer(void *base, unsigned int size, unsigned int offset, int handle, int op, int fd);
  int AllocateMem(AllocData *data, gralloc1_producer_usage_t prod_usage,
                  gralloc1_consumer_usage_t cons_usage);
  // @return : index of the descriptor with maximum buffer size req
  bool CheckForBufferSharing(uint32_t num_descriptors,
                             const std::vector<std::shared_ptr<BufferDescriptor>>& descriptors,
                             ssize_t *max_index);
  int GetImplDefinedFormat(gralloc1_producer_usage_t prod_usage,
                           gralloc1_consumer_usage_t cons_usage, int format);
  bool UseUncached(gralloc1_producer_usage_t prod_usage, gralloc1_consumer_usage_t cons_usage);

 private:
  void GetIonHeapInfo(gralloc1_producer_usage_t prod_usage, gralloc1_consumer_usage_t cons_usage,
                      unsigned int *ion_heap_id, unsigned int *alloc_type, unsigned int *ion_flags);

  IonAlloc *ion_allocator_ = NULL;
};

}  // namespace gralloc1

#endif  // __GR_ALLOCATOR_H__
