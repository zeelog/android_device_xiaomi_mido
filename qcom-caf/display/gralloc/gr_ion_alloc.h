/*
 * Copyright (c) 2011-2019, The Linux Foundation. All rights reserved.

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

#ifndef __GR_ION_ALLOC_H__
#define __GR_ION_ALLOC_H__

#include <linux/msm_ion.h>

#define FD_INIT -1

namespace gralloc1 {

enum {
  CACHE_CLEAN = 0x1,
  CACHE_INVALIDATE,
  CACHE_CLEAN_AND_INVALIDATE,
};

struct AllocData {
  void *base = NULL;
  int fd = -1;
  int ion_handle = -1;
  unsigned int offset = 0;
  unsigned int size = 0;
  unsigned int align = 1;
  uintptr_t handle = 0;
  bool uncached = false;
  unsigned int flags = 0x0;
  unsigned int heap_id = 0x0;
  unsigned int alloc_type = 0x0;
};

class IonAlloc {
 public:
  IonAlloc() { ion_dev_fd_ = FD_INIT; }

  ~IonAlloc() { CloseIonDevice(); }

  bool Init();
  int AllocBuffer(AllocData *data);
  int FreeBuffer(void *base, unsigned int size, unsigned int offset, int fd, int ion_handle);
  int MapBuffer(void **base, unsigned int size, unsigned int offset, int fd);
  int ImportBuffer(int fd);
  int UnmapBuffer(void *base, unsigned int size, unsigned int offset);
  int CleanBuffer(void *base, unsigned int size, unsigned int offset, int handle, int op, int fd);
 private:
#ifndef TARGET_ION_ABI_VERSION
  const char *kIonDevice = "/dev/ion";
#endif

  int OpenIonDevice();
  void CloseIonDevice();

  int ion_dev_fd_;
};

}  // namespace gralloc1

#endif  // __GR_ION_ALLOC_H__
