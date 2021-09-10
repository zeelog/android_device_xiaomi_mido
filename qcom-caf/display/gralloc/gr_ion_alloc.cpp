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

#define DEBUG 0
#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/msm_ion.h>
#if TARGET_ION_ABI_VERSION >= 2
#include <ion/ion.h>
#include <linux/dma-buf.h>
#endif
#include <stdlib.h>
#include <fcntl.h>
#include <log/log.h>
#include <errno.h>
#include <utils/Trace.h>

#include "gralloc_priv.h"
#include "gr_utils.h"
#include "gr_ion_alloc.h"

namespace gralloc1 {

bool IonAlloc::Init() {
  if (ion_dev_fd_ == FD_INIT) {
    ion_dev_fd_ = OpenIonDevice();
  }

  if (ion_dev_fd_ < 0) {
    ALOGE("%s: Failed to open ion device - %s", __FUNCTION__, strerror(errno));
    ion_dev_fd_ = FD_INIT;
    return false;
  }

  return true;
}

#if TARGET_ION_ABI_VERSION >= 2  // Use libion APIs for new ion

int IonAlloc::OpenIonDevice() {
  return ion_open();
}

void IonAlloc::CloseIonDevice() {
  if (ion_dev_fd_ > FD_INIT) {
    ion_close(ion_dev_fd_);
  }

  ion_dev_fd_ = FD_INIT;
}

int IonAlloc::AllocBuffer(AllocData *data) {
  ATRACE_CALL();
  int err = 0;
  int fd = -1;
  unsigned int flags = data->flags;

  flags |= data->uncached ? 0 : ION_FLAG_CACHED;

  std::string tag_name{};
  if (ATRACE_ENABLED()) {
    tag_name = "libion alloc size: " + std::to_string(data->size);
  }

  ATRACE_BEGIN(tag_name.c_str());
  err = ion_alloc_fd(ion_dev_fd_, data->size, data->align, data->heap_id, flags, &fd);
  ATRACE_END();
  if (err) {
    ALOGE("libion alloc failed");
    return err;
  }

  data->fd = fd;
  data->ion_handle = fd;  // For new ion api ion_handle does not exists so reusing fd for now
  ALOGD_IF(DEBUG, "libion: Allocated buffer size:%u fd:%d", data->size, data->fd);

  return 0;
}

int IonAlloc::FreeBuffer(void *base, unsigned int size, unsigned int offset, int fd,
                         int /*ion_handle*/) {
  ATRACE_CALL();
  int err = 0;
  ALOGD_IF(DEBUG, "libion: Freeing buffer base:%p size:%u fd:%d", base, size, fd);

  if (base) {
    err = UnmapBuffer(base, size, offset);
  }

  close(fd);
  return err;
}

int IonAlloc::ImportBuffer(int fd) {
  // For new ion api ion_handle does not exists so reusing fd for now
  return fd;
}

int IonAlloc::CleanBuffer(void */*base*/, unsigned int /*size*/, unsigned int /*offset*/,
                          int /*handle*/, int op, int dma_buf_fd) {
  ATRACE_CALL();
  ATRACE_INT("operation id", op);

  struct dma_buf_sync sync;
  int err = 0;

  switch (op) {
    case CACHE_CLEAN:
      sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
      break;
    case CACHE_INVALIDATE:
      sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;
      break;
    default:
      ALOGE("%s: Invalid operation %d", __FUNCTION__, op);
      return -1;
  }

  if (ioctl(dma_buf_fd, INT(DMA_BUF_IOCTL_SYNC), &sync)) {
    err = -errno;
    ALOGE("%s: DMA_BUF_IOCTL_SYNC failed with error - %s", __FUNCTION__, strerror(errno));
    return err;
  }

  return 0;
}

#else
#ifndef TARGET_ION_ABI_VERSION  // Use old ion apis directly

int IonAlloc::OpenIonDevice() {
  return open(kIonDevice, O_RDONLY);
}

void IonAlloc::CloseIonDevice() {
  if (ion_dev_fd_ > FD_INIT) {
    close(ion_dev_fd_);
  }

  ion_dev_fd_ = FD_INIT;
}

int IonAlloc::AllocBuffer(AllocData *data) {
  ATRACE_CALL();
  int err = 0;
  struct ion_handle_data handle_data;
  struct ion_fd_data fd_data;
  struct ion_allocation_data ion_alloc_data;

  ion_alloc_data.len = data->size;
  ion_alloc_data.align = data->align;
  ion_alloc_data.heap_id_mask = data->heap_id;
  ion_alloc_data.flags = data->flags;
  ion_alloc_data.flags |= data->uncached ? 0 : ION_FLAG_CACHED;

  if (ioctl(ion_dev_fd_, INT(ION_IOC_ALLOC), &ion_alloc_data)) {
    err = -errno;
    ALOGE("ION_IOC_ALLOC failed with error - %s", strerror(errno));
    return err;
  }

  fd_data.handle = ion_alloc_data.handle;
  handle_data.handle = ion_alloc_data.handle;
  if (ioctl(ion_dev_fd_, INT(ION_IOC_MAP), &fd_data)) {
    err = -errno;
    ALOGE("%s: ION_IOC_MAP failed with error - %s", __FUNCTION__, strerror(errno));
    ioctl(ion_dev_fd_, INT(ION_IOC_FREE), &handle_data);
    return err;
  }

  data->fd = fd_data.fd;
  data->ion_handle = handle_data.handle;
  ALOGD_IF(DEBUG, "ion: Allocated buffer size:%zu fd:%d handle:0x%x",
          ion_alloc_data.len, data->fd, data->ion_handle);

  return 0;
}

int IonAlloc::FreeBuffer(void *base, unsigned int size, unsigned int offset, int fd,
                         int ion_handle) {
  ATRACE_CALL();
  int err = 0;
  ALOGD_IF(DEBUG, "ion: Freeing buffer base:%p size:%u fd:%d handle:0x%x", base, size, fd,
           ion_handle);

  if (base) {
    err = UnmapBuffer(base, size, offset);
  }

  if (ion_handle > 0) {
    struct ion_handle_data handle_data;
    handle_data.handle = ion_handle;
    ioctl(ion_dev_fd_, INT(ION_IOC_FREE), &handle_data);
  }
  close(fd);
  return err;
}

int IonAlloc::ImportBuffer(int fd) {
  struct ion_fd_data fd_data;
  int err = 0;
  fd_data.fd = fd;
  if (ioctl(ion_dev_fd_, INT(ION_IOC_IMPORT), &fd_data)) {
    err = -errno;
    ALOGE("%s: ION_IOC_IMPORT failed with error - %s", __FUNCTION__, strerror(errno));
    return err;
  }
  return fd_data.handle;
}

int IonAlloc::CleanBuffer(void *base, unsigned int size, unsigned int offset, int handle, int op,
                          int /*fd*/) {
  ATRACE_CALL();
  ATRACE_INT("operation id", op);
  struct ion_flush_data flush_data;
  int err = 0;

  flush_data.handle = handle;
  flush_data.vaddr = base;
  // offset and length are unsigned int
  flush_data.offset = offset;
  flush_data.length = size;

  struct ion_custom_data d;
  switch (op) {
    case CACHE_CLEAN:
      d.cmd = ION_IOC_CLEAN_CACHES;
      break;
    case CACHE_INVALIDATE:
      d.cmd = ION_IOC_INV_CACHES;
      break;
    case CACHE_CLEAN_AND_INVALIDATE:
    default:
      d.cmd = ION_IOC_CLEAN_INV_CACHES;
  }

  d.arg = (unsigned long)(&flush_data);  // NOLINT
  if (ioctl(ion_dev_fd_, INT(ION_IOC_CUSTOM), &d)) {
    err = -errno;
    ALOGE("%s: ION_IOC_CLEAN_INV_CACHES failed with error - %s", __FUNCTION__, strerror(errno));
    return err;
  }

  return 0;
}

#else  // This ion version is not supported

int IonAlloc::OpenIonDevice() {
  return -EINVAL;
}

void IonAlloc::CloseIonDevice() {
}

int IonAlloc::AllocBuffer(AllocData * /*data*/) {
  return -EINVAL;
}

int IonAlloc::FreeBuffer(void * /*base*/, unsigned int /*size*/, unsigned int /*offset*/,
                         int /*fd*/, int /*ion_handle*/) {
  return -EINVAL;
}

int IonAlloc::ImportBuffer(int /*fd*/) {
  return -EINVAL;
}

int IonAlloc::CleanBuffer(void * /*base*/, unsigned int /*size*/, unsigned int /*offset*/,
                          int /*handle*/, int /*op*/, int /*fd*/) {
  return -EINVAL;
}

#endif
#endif  // TARGET_ION_ABI_VERSION


int IonAlloc::MapBuffer(void **base, unsigned int size, unsigned int offset, int fd) {
  ATRACE_CALL();
  int err = 0;
  void *addr = 0;

  addr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  *base = addr;
  if (addr == MAP_FAILED) {
    err = -errno;
    ALOGE("ion: Failed to map memory in the client: %s", strerror(errno));
  } else {
    ALOGD_IF(DEBUG, "ion: Mapped buffer base:%p size:%u offset:%u fd:%d", addr, size, offset, fd);
  }

  return err;
}

int IonAlloc::UnmapBuffer(void *base, unsigned int size, unsigned int /*offset*/) {
  ATRACE_CALL();
  ALOGD_IF(DEBUG, "ion: Unmapping buffer  base:%p size:%u", base, size);

  int err = 0;
  if (munmap(base, size)) {
    err = -errno;
    ALOGE("ion: Failed to unmap memory at %p : %s", base, strerror(errno));
  }

  return err;
}

}  // namespace gralloc1
