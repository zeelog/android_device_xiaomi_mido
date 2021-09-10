/*
 * Copyright (c) 2011-2017, 2019, The Linux Foundation. All rights reserved.
 * Not a Contribution
 *
 * Copyright (C) 2008 The Android Open Source Project
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

#ifndef __GR_BUF_MGR_H__
#define __GR_BUF_MGR_H__

#include <pthread.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <mutex>

#include "gralloc_priv.h"
#include "gr_allocator.h"
#include "gr_utils.h"
#include "gr_buf_descriptor.h"

namespace gralloc1 {

class BufferManager {
 public:
  ~BufferManager();
  gralloc1_error_t CreateBufferDescriptor(gralloc1_buffer_descriptor_t *descriptor_id);
  gralloc1_error_t DestroyBufferDescriptor(gralloc1_buffer_descriptor_t descriptor_id);
  gralloc1_error_t AllocateBuffers(uint32_t num_descriptors,
                                   const gralloc1_buffer_descriptor_t *descriptor_ids,
                                   buffer_handle_t *out_buffers);
  gralloc1_error_t RetainBuffer(private_handle_t const *hnd);
  gralloc1_error_t ReleaseBuffer(private_handle_t const *hnd);
  gralloc1_error_t LockBuffer(const private_handle_t *hnd, gralloc1_producer_usage_t prod_usage,
                              gralloc1_consumer_usage_t cons_usage);
  gralloc1_error_t UnlockBuffer(const private_handle_t *hnd);
  gralloc1_error_t Perform(int operation, va_list args);
  gralloc1_error_t GetFlexLayout(const private_handle_t *hnd, struct android_flex_layout *layout);
  gralloc1_error_t GetNumFlexPlanes(const private_handle_t *hnd, uint32_t *out_num_planes);
  gralloc1_error_t Dump(std::ostringstream *os);
  gralloc1_error_t IsBufferImported(const private_handle_t *hnd);
  gralloc1_error_t ValidateBufferSize(private_handle_t const *hnd, BufferInfo info);

  template <typename... Args>
  gralloc1_error_t CallBufferDescriptorFunction(gralloc1_buffer_descriptor_t descriptor_id,
                                                void (BufferDescriptor::*member)(Args...),
                                                Args... args) {
    std::lock_guard<std::mutex> lock(descriptor_lock_);
    const auto map_descriptor = descriptors_map_.find(descriptor_id);
    if (map_descriptor == descriptors_map_.end()) {
      return GRALLOC1_ERROR_BAD_DESCRIPTOR;
    }
    const auto descriptor = map_descriptor->second;
    (descriptor.get()->*member)(std::forward<Args>(args)...);
    return GRALLOC1_ERROR_NONE;
  }

  static BufferManager* GetInstance() {
    static BufferManager *instance = new BufferManager();
    return instance;
  }

 private:
  BufferManager();
  gralloc1_error_t MapBuffer(private_handle_t const *hnd);
  int AllocateBuffer(const BufferDescriptor &descriptor, buffer_handle_t *handle,
                     unsigned int bufferSize = 0);
  uint32_t GetDataAlignment(int format, gralloc1_producer_usage_t prod_usage,
                       gralloc1_consumer_usage_t cons_usage);
  int GetHandleFlags(int format, gralloc1_producer_usage_t prod_usage,
                     gralloc1_consumer_usage_t cons_usage);
  void CreateSharedHandle(buffer_handle_t inbuffer, const BufferDescriptor &descriptor,
                          buffer_handle_t *out_buffer);

  // Imports the ion fds into the current process. Returns an error for invalid handles
  gralloc1_error_t ImportHandleLocked(private_handle_t *hnd);

  // Creates a Buffer from the valid private handle and adds it to the map
  void RegisterHandleLocked(const private_handle_t *hnd, int ion_handle, int ion_handle_meta);

  // Wrapper structure over private handle
  // Values associated with the private handle
  // that do not need to go over IPC can be placed here
  // This structure is also not expected to be ABI stable
  // unlike private_handle_t
  struct Buffer {
    const private_handle_t *handle = nullptr;
    int ref_count = 1;
    // Hold the main and metadata ion handles
    // Freed from the allocator process
    // and unused in the mapping process
    int ion_handle_main = -1;
    int ion_handle_meta = -1;

    Buffer() = delete;
    explicit Buffer(const private_handle_t* h, int ih_main = -1, int ih_meta = -1):
        handle(h),
        ion_handle_main(ih_main),
        ion_handle_meta(ih_meta) {
    }
    void IncRef() { ++ref_count; }
    bool DecRef() { return --ref_count == 0; }
  };

  gralloc1_error_t FreeBuffer(std::shared_ptr<Buffer> buf);

  // Get the wrapper Buffer object from the handle, returns nullptr if handle is not found
  std::shared_ptr<Buffer> GetBufferFromHandleLocked(const private_handle_t *hnd);

  bool map_fb_mem_ = false;
  Allocator *allocator_ = NULL;
  std::mutex buffer_lock_;
  std::mutex descriptor_lock_;
  // TODO(user): The private_handle_t is used as a key because the unique ID generated
  // from next_id_ is not unique across processes. The correct way to resolve this would
  // be to use the allocator over hwbinder
  std::unordered_map<const private_handle_t*, std::shared_ptr<Buffer>> handles_map_ = {};
  std::unordered_map<gralloc1_buffer_descriptor_t,
                     std::shared_ptr<BufferDescriptor>> descriptors_map_ = {};
  std::atomic<uint64_t> next_id_;
};

}  // namespace gralloc1

#endif  // __GR_BUF_MGR_H__
