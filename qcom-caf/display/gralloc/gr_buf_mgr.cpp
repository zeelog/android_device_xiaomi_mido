/*
 * Copyright (c) 2011-2019 The Linux Foundation. All rights reserved.
 * Not a Contribution
 *
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define DEBUG 0

#include <iomanip>
#include <utility>
#include <vector>
#include <sstream>

#include "qd_utils.h"
#include "gr_priv_handle.h"
#include "gr_buf_descriptor.h"
#include "gr_utils.h"
#include "gr_buf_mgr.h"
#include "qdMetaData.h"

namespace gralloc1 {
std::atomic<gralloc1_buffer_descriptor_t> BufferDescriptor::next_id_(1);

static BufferInfo GetBufferInfo(const BufferDescriptor &descriptor) {
  return BufferInfo(descriptor.GetWidth(), descriptor.GetHeight(), descriptor.GetFormat(),
                    descriptor.GetProducerUsage(), descriptor.GetConsumerUsage());
}

BufferManager::BufferManager() : next_id_(0) {
  char property[PROPERTY_VALUE_MAX];

  // Map framebuffer memory
  if ((property_get(MAP_FB_MEMORY_PROP, property, NULL) > 0) &&
      (!strncmp(property, "1", PROPERTY_VALUE_MAX) ||
       (!strncasecmp(property, "true", PROPERTY_VALUE_MAX)))) {
    map_fb_mem_ = true;
  }

  handles_map_.clear();
  allocator_ = new Allocator();
  allocator_->Init();
}


gralloc1_error_t BufferManager::CreateBufferDescriptor(
    gralloc1_buffer_descriptor_t *descriptor_id) {
  std::lock_guard<std::mutex> lock(descriptor_lock_);
  auto descriptor = std::make_shared<BufferDescriptor>();
  descriptors_map_.emplace(descriptor->GetId(), descriptor);
  *descriptor_id = descriptor->GetId();
  return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t BufferManager::DestroyBufferDescriptor(
    gralloc1_buffer_descriptor_t descriptor_id) {
  std::lock_guard<std::mutex> lock(descriptor_lock_);
  const auto descriptor = descriptors_map_.find(descriptor_id);
  if (descriptor == descriptors_map_.end()) {
    return GRALLOC1_ERROR_BAD_DESCRIPTOR;
  }
  descriptors_map_.erase(descriptor);
  return GRALLOC1_ERROR_NONE;
}

BufferManager::~BufferManager() {
  if (allocator_) {
    delete allocator_;
  }
}

gralloc1_error_t BufferManager::AllocateBuffers(uint32_t num_descriptors,
                                                const gralloc1_buffer_descriptor_t *descriptor_ids,
                                                buffer_handle_t *out_buffers) {
  bool shared = true;
  gralloc1_error_t status = GRALLOC1_ERROR_NONE;

  // since GRALLOC1_CAPABILITY_TEST_ALLOCATE capability is supported
  // client can ask to test the allocation by passing NULL out_buffers
  bool test_allocate = !out_buffers;

  // Validate descriptors
  std::lock_guard<std::mutex> descriptor_lock(descriptor_lock_);
  std::vector<std::shared_ptr<BufferDescriptor>> descriptors;
  for (uint32_t i = 0; i < num_descriptors; i++) {
    const auto map_descriptor = descriptors_map_.find(descriptor_ids[i]);
    if (map_descriptor == descriptors_map_.end()) {
      return GRALLOC1_ERROR_BAD_DESCRIPTOR;
    } else {
      descriptors.push_back(map_descriptor->second);
    }
  }

  //  Resolve implementation defined formats
  for (auto &descriptor : descriptors) {
    descriptor->SetColorFormat(allocator_->GetImplDefinedFormat(descriptor->GetProducerUsage(),
                                                                descriptor->GetConsumerUsage(),
                                                                descriptor->GetFormat()));
  }

  // Check if input descriptors can be supported AND
  // Find out if a single buffer can be shared for all the given input descriptors
  uint32_t i = 0;
  ssize_t max_buf_index = -1;
  shared = allocator_->CheckForBufferSharing(num_descriptors, descriptors, &max_buf_index);

  if (test_allocate) {
    status = shared ? GRALLOC1_ERROR_NOT_SHARED : status;
    return status;
  }

  std::lock_guard<std::mutex> buffer_lock(buffer_lock_);
  if (shared && (max_buf_index >= 0)) {
    // Allocate one and duplicate/copy the handles for each descriptor
    if (AllocateBuffer(*descriptors[UINT(max_buf_index)], &out_buffers[max_buf_index])) {
      return GRALLOC1_ERROR_NO_RESOURCES;
    }

    for (i = 0; i < num_descriptors; i++) {
      // Create new handle for a given descriptor.
      // Current assumption is even MetaData memory would be same
      // Need to revisit if there is a need for own metadata memory
      if (i != UINT(max_buf_index)) {
        CreateSharedHandle(out_buffers[max_buf_index], *descriptors[i], &out_buffers[i]);
      }
    }
  } else {
    // Buffer sharing is not feasible.
    // Allocate separate buffer for each descriptor
    for (i = 0; i < num_descriptors; i++) {
      if (AllocateBuffer(*descriptors[i], &out_buffers[i])) {
        return GRALLOC1_ERROR_NO_RESOURCES;
      }
    }
  }

  // Allocation is successful. If backstore is not shared inform the client.
  if (!shared) {
    return GRALLOC1_ERROR_NOT_SHARED;
  }

  return status;
}

void BufferManager::CreateSharedHandle(buffer_handle_t inbuffer, const BufferDescriptor &descriptor,
                                       buffer_handle_t *outbuffer) {
  // TODO(user): This path is not verified
  private_handle_t const *input = reinterpret_cast<private_handle_t const *>(inbuffer);

  // Get Buffer attributes or dimension
  unsigned int alignedw = 0, alignedh = 0;
  BufferInfo info = GetBufferInfo(descriptor);

  GetAlignedWidthAndHeight(info, &alignedw, &alignedh);

  // create new handle from input reference handle and given descriptor
  int flags = GetHandleFlags(descriptor.GetFormat(), descriptor.GetProducerUsage(),
                             descriptor.GetConsumerUsage());
  int buffer_type = GetBufferType(descriptor.GetFormat());

  // Duplicate the fds
  // TODO(user): Not sure what to do for fb_id. Use duped fd and new dimensions?
  private_handle_t *out_hnd = new private_handle_t(dup(input->fd),
                                                   dup(input->fd_metadata),
                                                   flags,
                                                   INT(alignedw),
                                                   INT(alignedh),
                                                   descriptor.GetWidth(),
                                                   descriptor.GetHeight(),
                                                   descriptor.GetFormat(),
                                                   buffer_type,
                                                   input->size,
                                                   (descriptor.GetProducerUsage() | descriptor.GetConsumerUsage()));
  out_hnd->id = ++next_id_;
  // TODO(user): Base address of shared handle and ion handles
  RegisterHandleLocked(out_hnd, -1, -1);
  *outbuffer = out_hnd;
}

gralloc1_error_t BufferManager::FreeBuffer(std::shared_ptr<Buffer> buf) {
  auto hnd = buf->handle;
  ALOGD_IF(DEBUG, "FreeBuffer handle:%p", hnd);

  if (private_handle_t::validate(hnd) != 0) {
    ALOGE("FreeBuffer: Invalid handle: %p", hnd);
    return GRALLOC1_ERROR_BAD_HANDLE;
  }

  if (allocator_->FreeBuffer(reinterpret_cast<void *>(hnd->base), hnd->size, hnd->offset,
                             hnd->fd, buf->ion_handle_main) != 0) {
    return GRALLOC1_ERROR_BAD_HANDLE;
  }
  if (hnd->fd_metadata >= 0) {
    unsigned int meta_size = ALIGN((unsigned int)sizeof(MetaData_t), PAGE_SIZE);
    if (allocator_->FreeBuffer(reinterpret_cast<void *>(hnd->base_metadata), meta_size,
                    hnd->offset_metadata, hnd->fd_metadata, buf->ion_handle_meta) != 0) {
      return GRALLOC1_ERROR_BAD_HANDLE;
    }
  }

  private_handle_t * handle = const_cast<private_handle_t *>(hnd);
  handle->fd = -1;
  handle->fd_metadata = -1;
  if (!(handle->flags & private_handle_t::PRIV_FLAGS_CLIENT_ALLOCATED)) {
      delete handle;
  }
  return GRALLOC1_ERROR_NONE;
}

void BufferManager::RegisterHandleLocked(const private_handle_t *hnd,
                                         int ion_handle,
                                         int ion_handle_meta) {
  auto buffer = std::make_shared<Buffer>(hnd, ion_handle, ion_handle_meta);
  handles_map_.emplace(std::make_pair(hnd, buffer));
}

gralloc1_error_t BufferManager::ImportHandleLocked(private_handle_t *hnd) {
  if (private_handle_t::validate(hnd) != 0) {
    ALOGE("ImportHandleLocked: Invalid handle: %p", hnd);
    return GRALLOC1_ERROR_BAD_HANDLE;
  }
  ALOGD_IF(DEBUG, "Importing handle:%p id: %" PRIu64, hnd, hnd->id);
  int ion_handle_meta = -1;
  int ion_handle = allocator_->ImportBuffer(hnd->fd);
  if (ion_handle < 0) {
    ALOGE("Failed to import ion buffer: hnd: %p, fd:%d, id:%" PRIu64, hnd, hnd->fd, hnd->id);
    return GRALLOC1_ERROR_BAD_HANDLE;
  }
  if (hnd->fd_metadata >= 0) {
    ion_handle_meta = allocator_->ImportBuffer(hnd->fd_metadata);
    if (ion_handle_meta < 0) {
      ALOGE("Failed to import ion metadata buffer: hnd: %p, fd:%d, meta_fd:%d, id:%" PRIu64,
          hnd, hnd->fd, hnd->fd_metadata, hnd->id);
      allocator_->FreeBuffer(NULL, hnd->size, hnd->offset, hnd->fd, ion_handle);
      close(hnd->fd_metadata);
      hnd->fd = -1;
      hnd->fd_metadata = -1;
      return GRALLOC1_ERROR_BAD_HANDLE;
    }
  }
  // Initialize members that aren't transported
  hnd->size = static_cast<unsigned int>(lseek(hnd->fd, 0, SEEK_END));
  hnd->offset = 0;
  hnd->offset_metadata = 0;
  hnd->base = 0;
  hnd->base_metadata = 0;
  hnd->gpuaddr = 0;
  RegisterHandleLocked(hnd, ion_handle, ion_handle_meta);
  return GRALLOC1_ERROR_NONE;
}

std::shared_ptr<BufferManager::Buffer>
BufferManager::GetBufferFromHandleLocked(const private_handle_t *hnd) {
  auto it = handles_map_.find(hnd);
  if (it != handles_map_.end()) {
    return it->second;
  } else {
    return nullptr;
  }
}

gralloc1_error_t BufferManager::MapBuffer(private_handle_t const *handle) {
  private_handle_t *hnd = const_cast<private_handle_t *>(handle);
  ALOGD_IF(DEBUG, "Map buffer handle:%p id: %" PRIu64, hnd, hnd->id);

  hnd->base = 0;
  if (allocator_->MapBuffer(reinterpret_cast<void **>(&hnd->base), hnd->size, hnd->offset,
                            hnd->fd) != 0) {
    return GRALLOC1_ERROR_BAD_HANDLE;
  }
  return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t BufferManager::RetainBuffer(private_handle_t const *hnd) {
  ALOGD_IF(DEBUG, "Retain buffer handle:%p id: %" PRIu64, hnd, hnd->id);
  gralloc1_error_t err = GRALLOC1_ERROR_NONE;
  std::lock_guard<std::mutex> lock(buffer_lock_);
  auto buf = GetBufferFromHandleLocked(hnd);
  if (buf != nullptr) {
    buf->IncRef();
  } else {
    private_handle_t *handle = const_cast<private_handle_t *>(hnd);
    err = ImportHandleLocked(handle);
  }
  return err;
}

gralloc1_error_t BufferManager::ReleaseBuffer(private_handle_t const *hnd) {
  ALOGD_IF(DEBUG, "Release buffer handle:%p", hnd);
  std::lock_guard<std::mutex> lock(buffer_lock_);
  auto buf = GetBufferFromHandleLocked(hnd);
  if (buf == nullptr) {
    ALOGE("Could not find handle: %p id: %" PRIu64, hnd, hnd->id);
    return GRALLOC1_ERROR_BAD_HANDLE;
  } else {
    if (buf->DecRef()) {
      handles_map_.erase(hnd);
      // Unmap, close ion handle and close fd
      FreeBuffer(buf);
    }
  }
  return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t BufferManager::LockBuffer(const private_handle_t *hnd,
                                           gralloc1_producer_usage_t prod_usage,
                                           gralloc1_consumer_usage_t cons_usage) {
  std::lock_guard<std::mutex> lock(buffer_lock_);
  gralloc1_error_t err = GRALLOC1_ERROR_NONE;
  ALOGD_IF(DEBUG, "LockBuffer buffer handle:%p id: %" PRIu64, hnd, hnd->id);

  // If buffer is not meant for CPU return err
  if (!CpuCanAccess(prod_usage, cons_usage)) {
    return GRALLOC1_ERROR_BAD_VALUE;
  }

  auto buf = GetBufferFromHandleLocked(hnd);
  if (buf == nullptr) {
    return GRALLOC1_ERROR_BAD_HANDLE;
  }

  if (hnd->base == 0) {
    // we need to map for real
    err = MapBuffer(hnd);
  }

  // todo use handle here
  if (!err && (hnd->flags & private_handle_t::PRIV_FLAGS_USES_ION) &&
      (hnd->flags & private_handle_t::PRIV_FLAGS_CACHED)) {

    // Invalidate if CPU reads in software and there are non-CPU
    // writers. No need to do this for the metadata buffer as it is
    // only read/written in software.
    if ((cons_usage & (GRALLOC1_CONSUMER_USAGE_CPU_READ | GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN))
       && (hnd->flags & private_handle_t::PRIV_FLAGS_NON_CPU_WRITER)) {
      if (allocator_->CleanBuffer(reinterpret_cast<void *>(hnd->base), hnd->size, hnd->offset,
                                  buf->ion_handle_main, CACHE_INVALIDATE, hnd->fd)) {

         return GRALLOC1_ERROR_BAD_HANDLE;
      }
    }
  }

  // Mark the buffer to be flushed after CPU write.
  if (!err && CpuCanWrite(prod_usage)) {
    private_handle_t *handle = const_cast<private_handle_t *>(hnd);
    handle->flags |= private_handle_t::PRIV_FLAGS_NEEDS_FLUSH;
  }

  return err;
}

gralloc1_error_t BufferManager::UnlockBuffer(const private_handle_t *handle) {
  std::lock_guard<std::mutex> lock(buffer_lock_);
  gralloc1_error_t status = GRALLOC1_ERROR_NONE;

  private_handle_t *hnd = const_cast<private_handle_t *>(handle);
  auto buf = GetBufferFromHandleLocked(hnd);
  if (buf == nullptr) {
    return GRALLOC1_ERROR_BAD_HANDLE;
  }

  if (hnd->flags & private_handle_t::PRIV_FLAGS_NEEDS_FLUSH) {
    if (allocator_->CleanBuffer(reinterpret_cast<void *>(hnd->base), hnd->size, hnd->offset,
                                buf->ion_handle_main, CACHE_CLEAN, hnd->fd) != 0) {
      status = GRALLOC1_ERROR_BAD_HANDLE;
    }
    hnd->flags &= ~private_handle_t::PRIV_FLAGS_NEEDS_FLUSH;
  }

  return status;
}

uint32_t BufferManager::GetDataAlignment(int format, gralloc1_producer_usage_t prod_usage,
                                    gralloc1_consumer_usage_t cons_usage) {
  uint32_t align = UINT(getpagesize());
  if (format == HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED) {
    align = 8192;
  }

  if (prod_usage & GRALLOC1_PRODUCER_USAGE_PROTECTED) {
    if ((prod_usage & GRALLOC1_PRODUCER_USAGE_CAMERA) ||
        (cons_usage & GRALLOC1_CONSUMER_USAGE_PRIVATE_SECURE_DISPLAY)) {
      // The alignment here reflects qsee mmu V7L/V8L requirement
      align = SZ_2M;
    } else {
      align = SECURE_ALIGN;
    }
  }

  return align;
}

int BufferManager::GetHandleFlags(int format, gralloc1_producer_usage_t prod_usage,
                                  gralloc1_consumer_usage_t cons_usage) {
  int flags = 0;
  if (cons_usage & GRALLOC1_CONSUMER_USAGE_PRIVATE_EXTERNAL_ONLY) {
    flags |= private_handle_t::PRIV_FLAGS_EXTERNAL_ONLY;
  }

  if (cons_usage & GRALLOC1_CONSUMER_USAGE_PRIVATE_INTERNAL_ONLY) {
    flags |= private_handle_t::PRIV_FLAGS_INTERNAL_ONLY;
  }

  if (cons_usage & GRALLOC1_CONSUMER_USAGE_VIDEO_ENCODER) {
    flags |= private_handle_t::PRIV_FLAGS_VIDEO_ENCODER;
  }

  if (prod_usage & GRALLOC1_PRODUCER_USAGE_CAMERA) {
    flags |= private_handle_t::PRIV_FLAGS_CAMERA_WRITE;
  }

  if (prod_usage & GRALLOC1_CONSUMER_USAGE_CAMERA) {
    flags |= private_handle_t::PRIV_FLAGS_CAMERA_READ;
  }

  if (cons_usage & GRALLOC1_CONSUMER_USAGE_HWCOMPOSER) {
    flags |= private_handle_t::PRIV_FLAGS_HW_COMPOSER;
  }

  if (prod_usage & GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE) {
    flags |= private_handle_t::PRIV_FLAGS_HW_TEXTURE;
  }

  if (cons_usage & GRALLOC1_CONSUMER_USAGE_PRIVATE_SECURE_DISPLAY) {
    flags |= private_handle_t::PRIV_FLAGS_SECURE_DISPLAY;
  }

  if (IsUBwcEnabled(format, prod_usage, cons_usage)) {
    flags |= private_handle_t::PRIV_FLAGS_UBWC_ALIGNED;
  }

  if (prod_usage & (GRALLOC1_PRODUCER_USAGE_CPU_READ | GRALLOC1_PRODUCER_USAGE_CPU_WRITE)) {
    flags |= private_handle_t::PRIV_FLAGS_CPU_RENDERED;
  }

  // TODO(user): is this correct???
  if ((cons_usage &
       (GRALLOC1_CONSUMER_USAGE_VIDEO_ENCODER | GRALLOC1_CONSUMER_USAGE_CLIENT_TARGET)) ||
      (prod_usage & (GRALLOC1_PRODUCER_USAGE_CAMERA | GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET))) {
    flags |= private_handle_t::PRIV_FLAGS_NON_CPU_WRITER;
  }

  if (cons_usage & GRALLOC1_CONSUMER_USAGE_HWCOMPOSER) {
    flags |= private_handle_t::PRIV_FLAGS_DISP_CONSUMER;
  }

  if (!allocator_->UseUncached(prod_usage, cons_usage)) {
    flags |= private_handle_t::PRIV_FLAGS_CACHED;
  }

  return flags;
}

int BufferManager::AllocateBuffer(const BufferDescriptor &descriptor, buffer_handle_t *handle,
                                    unsigned int bufferSize) {
  if (!handle)
    return -EINVAL;

  int format = descriptor.GetFormat();
  gralloc1_producer_usage_t prod_usage = descriptor.GetProducerUsage();
  gralloc1_consumer_usage_t cons_usage = descriptor.GetConsumerUsage();
  uint32_t layer_count = descriptor.GetLayerCount();

  // Check if GPU supports requested hardware buffer usage
  if (!(prod_usage & GRALLOC_USAGE_PRIVATE_SECURE_DISPLAY) &&
        !IsGPUSupportedHwBuffer(prod_usage)) {
    ALOGE("AllocateBuffer - Requested HW Buffer usage not supported by GPU");
    return GRALLOC1_ERROR_UNSUPPORTED;
  }

  // Get implementation defined format
  int gralloc_format = allocator_->GetImplDefinedFormat(prod_usage, cons_usage, format);

  unsigned int size;
  unsigned int alignedw, alignedh;
  int buffer_type = GetBufferType(gralloc_format);
  BufferInfo info = GetBufferInfo(descriptor);
  info.layer_count = static_cast<int>(layer_count);
  info.format = format;

  GraphicsMetadata graphics_metadata = {};
  GetBufferSizeAndDimensions(info, &size, &alignedw, &alignedh, &graphics_metadata);

  size = (bufferSize >= size) ? bufferSize : size;
  int err = 0;
  int flags = 0;
  auto page_size = UINT(getpagesize());
  AllocData data;
  data.align = GetDataAlignment(format, prod_usage, cons_usage);
  data.size = size;
  data.handle = (uintptr_t) handle;
  data.uncached = allocator_->UseUncached(prod_usage, cons_usage);

  // Allocate buffer memory
  err = allocator_->AllocateMem(&data, prod_usage, cons_usage);
  if (err) {
    ALOGE("gralloc failed to allocate err=%s", strerror(-err));
    return err;
  }

  // Allocate memory for MetaData
  AllocData e_data;
  e_data.size = ALIGN(UINT(sizeof(MetaData_t)), page_size);
  e_data.handle = data.handle;
  e_data.align = page_size;

  err =
      allocator_->AllocateMem(&e_data, GRALLOC1_PRODUCER_USAGE_NONE, GRALLOC1_CONSUMER_USAGE_NONE);
  if (err) {
    ALOGE("gralloc failed to allocate metadata error=%s", strerror(-err));
    return err;
  }

  flags = GetHandleFlags(format, prod_usage, cons_usage);
  flags |= data.alloc_type;

  // Create handle
  private_handle_t *hnd = new private_handle_t(data.fd,
                                               e_data.fd,
                                               flags,
                                               INT(alignedw),
                                               INT(alignedh),
                                               descriptor.GetWidth(),
                                               descriptor.GetHeight(),
                                               format,
                                               buffer_type,
                                               data.size,
                                               (prod_usage | cons_usage));

  hnd->id = ++next_id_;
  hnd->base = 0;
  hnd->base_metadata = 0;
  hnd->layer_count = layer_count;
  // set default csc as 709, but for video(yuv) its 601L
  ColorSpace_t colorSpace = (buffer_type == BUFFER_TYPE_VIDEO) ? ITU_R_601 : ITU_R_709;
  setMetaData(hnd, UPDATE_COLOR_SPACE, reinterpret_cast<void *>(&colorSpace));

  bool use_adreno_for_size = CanUseAdrenoForSize(buffer_type, (prod_usage | cons_usage));
  if (use_adreno_for_size) {
    setMetaData(hnd, SET_GRAPHICS_METADATA, reinterpret_cast<void *>(&graphics_metadata));
  }

  *handle = hnd;
  RegisterHandleLocked(hnd, data.ion_handle, e_data.ion_handle);
  ALOGD_IF(DEBUG, "Allocated buffer handle: %p id: %" PRIu64, hnd, hnd->id);
  if (DEBUG) {
    private_handle_t::Dump(hnd);
  }
  return err;
}

gralloc1_error_t BufferManager::Perform(int operation, va_list args) {
  switch (operation) {
    case GRALLOC_MODULE_PERFORM_CREATE_HANDLE_FROM_BUFFER: {
      int fd = va_arg(args, int);
      unsigned int size = va_arg(args, unsigned int);
      unsigned int offset = va_arg(args, unsigned int);
      void *base = va_arg(args, void *);
      int width = va_arg(args, int);
      int height = va_arg(args, int);
      int format = va_arg(args, int);

      native_handle_t **handle = va_arg(args, native_handle_t **);
      if (!handle) {
        return GRALLOC1_ERROR_BAD_HANDLE;
      }

      private_handle_t *hnd = reinterpret_cast<private_handle_t *>(
          native_handle_create(private_handle_t::kNumFds, private_handle_t::NumInts()));
      if (hnd) {
        unsigned int alignedw = 0, alignedh = 0;
        hnd->magic = private_handle_t::kMagic;
        hnd->fd = fd;
        hnd->flags = private_handle_t::PRIV_FLAGS_USES_ION;
        hnd->size = size;
        hnd->offset = offset;
        hnd->base = uint64_t(base);
        hnd->gpuaddr = 0;
        BufferInfo info(width, height, format);
        GetAlignedWidthAndHeight(info, &alignedw, &alignedh);
        hnd->unaligned_width = width;
        hnd->unaligned_height = height;
        hnd->width = INT(alignedw);
        hnd->height = INT(alignedh);
        hnd->format = format;
        *handle = reinterpret_cast<native_handle_t *>(hnd);
      }
    } break;

    case GRALLOC_MODULE_PERFORM_GET_STRIDE: {
      int width = va_arg(args, int);
      int format = va_arg(args, int);
      int *stride = va_arg(args, int *);
      unsigned int alignedw = 0, alignedh = 0;

      if (!stride) {
        return GRALLOC1_ERROR_BAD_VALUE;
      }

      BufferInfo info(width, width, format);
      GetAlignedWidthAndHeight(info, &alignedw, &alignedh);
      *stride = INT(alignedw);
    } break;

    case GRALLOC_MODULE_PERFORM_GET_CUSTOM_STRIDE_FROM_HANDLE: {
      private_handle_t *hnd = va_arg(args, private_handle_t *);
      int *stride = va_arg(args, int *);
      if (private_handle_t::validate(hnd) != 0) {
        return GRALLOC1_ERROR_BAD_HANDLE;
      }

      if (!stride) {
        return GRALLOC1_ERROR_BAD_VALUE;
      }

      BufferDim_t buffer_dim;
      if (getMetaData(hnd, GET_BUFFER_GEOMETRY, &buffer_dim) == 0) {
        *stride = buffer_dim.sliceWidth;
      } else {
        *stride = hnd->width;
      }
    } break;

    // TODO(user) : this alone should be sufficient, ask gfx to get rid of above
    case GRALLOC_MODULE_PERFORM_GET_CUSTOM_STRIDE_AND_HEIGHT_FROM_HANDLE: {
      private_handle_t *hnd = va_arg(args, private_handle_t *);
      int *stride = va_arg(args, int *);
      int *height = va_arg(args, int *);
      if (private_handle_t::validate(hnd) != 0) {
        return GRALLOC1_ERROR_BAD_HANDLE;
      }

      if (!stride || !height) {
        return GRALLOC1_ERROR_BAD_VALUE;
      }

      BufferDim_t buffer_dim;
      if (getMetaData(hnd, GET_BUFFER_GEOMETRY, &buffer_dim) == 0) {
        *stride = buffer_dim.sliceWidth;
        *height = buffer_dim.sliceHeight;
      } else {
        *stride = hnd->width;
        *height = hnd->height;
      }
    } break;

    case GRALLOC_MODULE_PERFORM_GET_ATTRIBUTES: {
      // TODO(user): Usage is split now. take care of it from Gfx client.
      // see if we can directly expect descriptor from gfx client.
      int width = va_arg(args, int);
      int height = va_arg(args, int);
      int format = va_arg(args, int);
      uint64_t producer_usage = va_arg(args, uint64_t);
      uint64_t consumer_usage = va_arg(args, uint64_t);
      gralloc1_producer_usage_t prod_usage = static_cast<gralloc1_producer_usage_t>(producer_usage);
      gralloc1_consumer_usage_t cons_usage = static_cast<gralloc1_consumer_usage_t>(consumer_usage);

      int *aligned_width = va_arg(args, int *);
      int *aligned_height = va_arg(args, int *);
      int *tile_enabled = va_arg(args, int *);
      if (!aligned_width || !aligned_height || !tile_enabled) {
        return GRALLOC1_ERROR_BAD_VALUE;
      }

      unsigned int alignedw, alignedh;
      BufferInfo info(width, height, format, prod_usage, cons_usage);
      *tile_enabled = IsUBwcEnabled(format, prod_usage, cons_usage);
      GetAlignedWidthAndHeight(info, &alignedw, &alignedh);
      *aligned_width = INT(alignedw);
      *aligned_height = INT(alignedh);
    } break;

    case GRALLOC_MODULE_PERFORM_GET_COLOR_SPACE_FROM_HANDLE: {
      private_handle_t *hnd = va_arg(args, private_handle_t *);
      int *color_space = va_arg(args, int *);

      if (private_handle_t::validate(hnd) != 0) {
        return GRALLOC1_ERROR_BAD_HANDLE;
      }

      if (!color_space) {
        return GRALLOC1_ERROR_BAD_VALUE;
      }

      *color_space = 0;
      ColorMetaData color_metadata;
      if (getMetaData(hnd, GET_COLOR_METADATA, &color_metadata) == 0) {
        switch (color_metadata.colorPrimaries) {
          case ColorPrimaries_BT709_5:
            *color_space = HAL_CSC_ITU_R_709;
            break;
          case ColorPrimaries_BT601_6_525:
          case ColorPrimaries_BT601_6_625:
            *color_space = ((color_metadata.range) ? HAL_CSC_ITU_R_601_FR : HAL_CSC_ITU_R_601);
            break;
          case ColorPrimaries_BT2020:
            *color_space = (color_metadata.range) ? HAL_CSC_ITU_R_2020_FR : HAL_CSC_ITU_R_2020;
            break;
          default:
            ALOGE("Unknown Color Space = %d", color_metadata.colorPrimaries);
            break;
        }
        break;
      } else if (getMetaData(hnd, GET_COLOR_SPACE, color_space) != 0) {
          *color_space = 0;
      }
    } break;
    case GRALLOC_MODULE_PERFORM_GET_YUV_PLANE_INFO: {
      private_handle_t *hnd = va_arg(args, private_handle_t *);
      android_ycbcr *ycbcr = va_arg(args, struct android_ycbcr *);
      if (private_handle_t::validate(hnd) != 0) {
        return GRALLOC1_ERROR_BAD_HANDLE;
      }

      if (!ycbcr) {
        return GRALLOC1_ERROR_BAD_VALUE;
      }

      if (GetYUVPlaneInfo(hnd, ycbcr)) {
        return GRALLOC1_ERROR_UNDEFINED;
      }
    } break;

    case GRALLOC_MODULE_PERFORM_GET_MAP_SECURE_BUFFER_INFO: {
      private_handle_t *hnd = va_arg(args, private_handle_t *);
      int *map_secure_buffer = va_arg(args, int *);

      if (private_handle_t::validate(hnd) != 0) {
        return GRALLOC1_ERROR_BAD_HANDLE;
      }

      if (!map_secure_buffer) {
        return GRALLOC1_ERROR_BAD_VALUE;
      }

      if (getMetaData(hnd, GET_MAP_SECURE_BUFFER, map_secure_buffer) == 0) {
        *map_secure_buffer = 0;
      }
    } break;

    case GRALLOC_MODULE_PERFORM_GET_UBWC_FLAG: {
      private_handle_t *hnd = va_arg(args, private_handle_t *);
      int *flag = va_arg(args, int *);

      if (private_handle_t::validate(hnd) != 0) {
        return GRALLOC1_ERROR_BAD_HANDLE;
      }

      if (!flag) {
        return GRALLOC1_ERROR_BAD_VALUE;
      }

      *flag = hnd->flags &private_handle_t::PRIV_FLAGS_UBWC_ALIGNED;
      int linear_format = 0;
      if (getMetaData(hnd, GET_LINEAR_FORMAT, &linear_format) == 0) {
        if (linear_format) {
         *flag = 0;
        }
      }
    } break;

    case GRALLOC_MODULE_PERFORM_GET_RGB_DATA_ADDRESS: {
      private_handle_t *hnd = va_arg(args, private_handle_t *);
      void **rgb_data = va_arg(args, void **);

      if (private_handle_t::validate(hnd) != 0) {
        return GRALLOC1_ERROR_BAD_HANDLE;
      }

      if (!rgb_data) {
        return GRALLOC1_ERROR_BAD_VALUE;
      }

      if (GetRgbDataAddress(hnd, rgb_data)) {
        return GRALLOC1_ERROR_UNDEFINED;
      }
    } break;

    case GRALLOC1_MODULE_PERFORM_GET_BUFFER_SIZE_AND_DIMENSIONS: {
      int width = va_arg(args, int);
      int height = va_arg(args, int);
      int format = va_arg(args, int);
      uint64_t p_usage = va_arg(args, uint64_t);
      uint64_t c_usage = va_arg(args, uint64_t);
      gralloc1_producer_usage_t producer_usage = static_cast<gralloc1_producer_usage_t>(p_usage);
      gralloc1_consumer_usage_t consumer_usage = static_cast<gralloc1_consumer_usage_t>(c_usage);
      uint32_t *aligned_width = va_arg(args, uint32_t *);
      uint32_t *aligned_height = va_arg(args, uint32_t *);
      uint32_t *size = va_arg(args, uint32_t *);

      if (!aligned_width || !aligned_height || !size) {
        return GRALLOC1_ERROR_BAD_VALUE;
      }

      auto info = BufferInfo(width, height, format, producer_usage, consumer_usage);
      GetBufferSizeAndDimensions(info, size, aligned_width, aligned_height);
      // Align size
      auto align = GetDataAlignment(format, producer_usage, consumer_usage);
      *size = ALIGN(*size, align);
    } break;

    case GRALLOC1_MODULE_PERFORM_ALLOCATE_BUFFER: {
      std::lock_guard<std::mutex> lock(buffer_lock_);
      int width = va_arg(args, int);
      int height = va_arg(args, int);
      int format = va_arg(args, int);
      uint64_t p_usage = va_arg(args, uint64_t);
      uint64_t c_usage = va_arg(args, uint64_t);
      buffer_handle_t *hnd = va_arg(args, buffer_handle_t*);
      gralloc1_producer_usage_t producer_usage = static_cast<gralloc1_producer_usage_t>(p_usage);
      gralloc1_consumer_usage_t consumer_usage = static_cast<gralloc1_consumer_usage_t>(c_usage);
      BufferDescriptor descriptor(width, height, format, producer_usage, consumer_usage);
      unsigned int size;
      unsigned int alignedw, alignedh;
      GetBufferSizeAndDimensions(GetBufferInfo(descriptor), &size, &alignedw, &alignedh);
      AllocateBuffer(descriptor, hnd, size);
    } break;

    case GRALLOC1_MODULE_PERFORM_GET_INTERLACE_FLAG: {
      private_handle_t *hnd = va_arg(args, private_handle_t *);
      int *flag = va_arg(args, int *);

      if (private_handle_t::validate(hnd) != 0) {
        return GRALLOC1_ERROR_BAD_HANDLE;
      }

      if (!flag) {
        return GRALLOC1_ERROR_BAD_VALUE;
      }

      if (getMetaData(hnd, GET_PP_PARAM_INTERLACED, flag) != 0) {
        *flag = 0;
      }
    } break;
    case GRALLOC_MODULE_PERFORM_GET_GRAPHICS_METADATA: {
      private_handle_t* hnd = va_arg(args, private_handle_t *);

      if (private_handle_t::validate(hnd) != 0) {
        return GRALLOC1_ERROR_BAD_HANDLE;
      }

      void* graphic_metadata = va_arg(args, void*);

      if (getMetaData(hnd, GET_GRAPHICS_METADATA, graphic_metadata) != 0) {
        graphic_metadata = NULL;
        return GRALLOC1_ERROR_UNSUPPORTED;
      }
    } break;

    default:
      break;
  }
  return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t BufferManager::GetNumFlexPlanes(const private_handle_t *hnd,
                                                 uint32_t *out_num_planes) {
  if (!IsYuvFormat(hnd->format)) {
    return GRALLOC1_ERROR_UNSUPPORTED;
  } else {
    *out_num_planes = 3;
  }
  return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t BufferManager::GetFlexLayout(const private_handle_t *hnd,
                                              struct android_flex_layout *layout) {
  if (!IsYuvFormat(hnd->format)) {
    return GRALLOC1_ERROR_UNSUPPORTED;
  }

  android_ycbcr ycbcr;
  int err = GetYUVPlaneInfo(hnd, &ycbcr);

  if (err != 0) {
    return GRALLOC1_ERROR_BAD_HANDLE;
  }

  layout->format = FLEX_FORMAT_YCbCr;
  layout->num_planes = 3;

  for (uint32_t i = 0; i < layout->num_planes; i++) {
    layout->planes[i].bits_per_component = 8;
    layout->planes[i].bits_used = 8;
    layout->planes[i].h_increment = 1;
    layout->planes[i].v_increment = 1;
    layout->planes[i].h_subsampling = 2;
    layout->planes[i].v_subsampling = 2;
  }

  layout->planes[0].top_left = static_cast<uint8_t *>(ycbcr.y);
  layout->planes[0].component = FLEX_COMPONENT_Y;
  layout->planes[0].v_increment = static_cast<int32_t>(ycbcr.ystride);

  layout->planes[1].top_left = static_cast<uint8_t *>(ycbcr.cb);
  layout->planes[1].component = FLEX_COMPONENT_Cb;
  layout->planes[1].h_increment = static_cast<int32_t>(ycbcr.chroma_step);
  layout->planes[1].v_increment = static_cast<int32_t>(ycbcr.cstride);

  layout->planes[2].top_left = static_cast<uint8_t *>(ycbcr.cr);
  layout->planes[2].component = FLEX_COMPONENT_Cr;
  layout->planes[2].h_increment = static_cast<int32_t>(ycbcr.chroma_step);
  layout->planes[2].v_increment = static_cast<int32_t>(ycbcr.cstride);
  return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t BufferManager::Dump(std::ostringstream *os) {
  std::lock_guard<std::mutex> buffer_lock(buffer_lock_);
  for (auto it : handles_map_) {
    auto buf = it.second;
    auto hnd = buf->handle;
    *os << "handle id: " << std::setw(4) << hnd->id;
    *os << " fd: "       << std::setw(3) << hnd->fd;
    *os << " fd_meta: "  << std::setw(3) << hnd->fd_metadata;
    *os << " wxh: "      << std::setw(4) << hnd->width <<" x " << std::setw(4) <<  hnd->height;
    *os << " uwxuh: "    << std::setw(4) << hnd->unaligned_width << " x ";
    *os << std::setw(4)  <<  hnd->unaligned_height;
    *os << " size: "     << std::setw(9) << hnd->size;
    *os << std::hex << std::setfill('0');
    *os << " priv_flags: " << "0x" << std::setw(8) << hnd->flags;
    *os << " prod_usage: " << "0x" << std::setw(8) << hnd->usage;
    *os << " cons_usage: " << "0x" << std::setw(8) << hnd->usage;
    // TODO(user): get format string from qdutils
    *os << " format: "     << "0x" << std::setw(8) << hnd->format;
    *os << std::dec  << std::setfill(' ') << std::endl;
  }
  return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t BufferManager::IsBufferImported(const private_handle_t *hnd) {
  std::lock_guard<std::mutex> lock(buffer_lock_);
  auto buf = GetBufferFromHandleLocked(hnd);
  if (buf != nullptr) {
    return GRALLOC1_ERROR_NONE;
  }
  return GRALLOC1_ERROR_BAD_VALUE;
}

gralloc1_error_t BufferManager::ValidateBufferSize(private_handle_t const *hnd, BufferInfo info) {
  unsigned int size, alignedw, alignedh;
  info.format = allocator_->GetImplDefinedFormat(info.prod_usage, info.cons_usage, info.format);
  GetBufferSizeAndDimensions(info, &size, &alignedw, &alignedh);
  auto ion_fd_size = static_cast<unsigned int>(lseek(hnd->fd, 0, SEEK_END));
  if (size != ion_fd_size) {
    return GRALLOC1_ERROR_BAD_VALUE;
  }
  return GRALLOC1_ERROR_NONE;
}

}  //  namespace gralloc1
