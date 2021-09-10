/*
 * Copyright (c) 2016-2017, 2019, The Linux Foundation. All rights reserved.

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

#ifndef __GR_DEVICE_IMPL_H__
#define __GR_DEVICE_IMPL_H__

#include <hardware/hardware.h>
#include <hardware/gralloc1.h>
#include "gr_buf_mgr.h"

struct private_module_t {
  hw_module_t base;
};

#define GRALLOC_IMPL(exp) reinterpret_cast<GrallocImpl const *>(exp)

namespace gralloc1 {

class GrallocImpl : public gralloc1_device_t {
 public:
  static int CloseDevice(hw_device_t *device);
  static void GetCapabilities(struct gralloc1_device *device, uint32_t *out_count,
                              int32_t * /*gralloc1_capability_t*/ out_capabilities);
  static gralloc1_function_pointer_t GetFunction(
      struct gralloc1_device *device, int32_t /*gralloc1_function_descriptor_t*/ descriptor);

  static GrallocImpl* GetInstance(const struct hw_module_t *module) {
    static GrallocImpl *instance = new GrallocImpl(module);
    if (instance->IsInitialized()) {
      return instance;
    } else {
      return nullptr;
    }
  }

 private:
  static inline gralloc1_error_t Dump(gralloc1_device_t *device, uint32_t *out_size,
                                      char *out_buffer);
  static inline gralloc1_error_t CheckDeviceAndHandle(gralloc1_device_t *device,
                                                      buffer_handle_t buffer);
  static gralloc1_error_t CreateBufferDescriptor(gralloc1_device_t *device,
                                                 gralloc1_buffer_descriptor_t *out_descriptor);
  static gralloc1_error_t DestroyBufferDescriptor(gralloc1_device_t *device,
                                                  gralloc1_buffer_descriptor_t descriptor);
  static gralloc1_error_t SetConsumerUsage(gralloc1_device_t *device,
                                           gralloc1_buffer_descriptor_t descriptor,
                                           gralloc1_consumer_usage_t usage);
  static gralloc1_error_t SetBufferDimensions(gralloc1_device_t *device,
                                              gralloc1_buffer_descriptor_t descriptor,
                                              uint32_t width, uint32_t height);
  static gralloc1_error_t SetColorFormat(gralloc1_device_t *device,
                                         gralloc1_buffer_descriptor_t descriptor, int32_t format);
  static gralloc1_error_t SetLayerCount(gralloc1_device_t *device,
                                        gralloc1_buffer_descriptor_t descriptor,
                                        uint32_t layer_count);
  static gralloc1_error_t SetProducerUsage(gralloc1_device_t *device,
                                           gralloc1_buffer_descriptor_t descriptor,
                                           gralloc1_producer_usage_t usage);
  static gralloc1_error_t GetBackingStore(gralloc1_device_t *device, buffer_handle_t buffer,
                                          gralloc1_backing_store_t *out_store);
  static gralloc1_error_t GetConsumerUsage(gralloc1_device_t *device, buffer_handle_t buffer,
                                           gralloc1_consumer_usage_t *out_usage);
  static gralloc1_error_t GetBufferDimensions(gralloc1_device_t *device, buffer_handle_t buffer,
                                              uint32_t *out_width, uint32_t *out_height);
  static gralloc1_error_t GetColorFormat(gralloc1_device_t *device, buffer_handle_t descriptor,
                                         int32_t *outFormat);
  static gralloc1_error_t GetLayerCount(gralloc1_device_t *device, buffer_handle_t buffer,
                                        uint32_t *out_layer_count);
  static gralloc1_error_t GetProducerUsage(gralloc1_device_t *device, buffer_handle_t buffer,
                                           gralloc1_producer_usage_t *out_usage);
  static gralloc1_error_t GetBufferStride(gralloc1_device_t *device, buffer_handle_t buffer,
                                          uint32_t *out_stride);
  static gralloc1_error_t AllocateBuffers(gralloc1_device_t *device, uint32_t num_dptors,
                                          const gralloc1_buffer_descriptor_t *descriptors,
                                          buffer_handle_t *out_buffers);
  static gralloc1_error_t RetainBuffer(gralloc1_device_t *device, buffer_handle_t buffer);
  static gralloc1_error_t ReleaseBuffer(gralloc1_device_t *device, buffer_handle_t buffer);
  static gralloc1_error_t GetNumFlexPlanes(gralloc1_device_t *device, buffer_handle_t buffer,
                                           uint32_t *out_num_planes);
  static gralloc1_error_t LockBuffer(gralloc1_device_t *device, buffer_handle_t buffer,
                                     gralloc1_producer_usage_t prod_usage,
                                     gralloc1_consumer_usage_t cons_usage,
                                     const gralloc1_rect_t *region, void **out_data,
                                     int32_t acquire_fence);
  static gralloc1_error_t LockFlex(gralloc1_device_t *device, buffer_handle_t buffer,
                                   gralloc1_producer_usage_t prod_usage,
                                   gralloc1_consumer_usage_t cons_usage,
                                   const gralloc1_rect_t *region,
                                   struct android_flex_layout *out_flex_layout,
                                   int32_t acquire_fence);

  static gralloc1_error_t UnlockBuffer(gralloc1_device_t *device, buffer_handle_t buffer,
                                       int32_t *release_fence);
  static gralloc1_error_t Gralloc1Perform(gralloc1_device_t *device, int operation, ...);
  static gralloc1_error_t ValidateBufferSize(gralloc1_device_t *device,
                                             buffer_handle_t buffer,
                                             gralloc1_buffer_descriptor_info_t &descriptor_info,
                                             int32_t stride);
  static gralloc1_error_t GetTransportSize(gralloc1_device_t *device,
                                           buffer_handle_t buffer,
                                           uint32_t *outNumFds,
                                           uint32_t *outNumInts);
  static gralloc1_error_t ImportBuffer(gralloc1_device_t *device __unused,
                                       const native_handle_t* rawHandle,
                                       const native_handle_t** outBufferHandle);



  explicit GrallocImpl(const hw_module_t *module);
  ~GrallocImpl();
  bool Init();
  bool IsInitialized() const { return initalized_; }

  BufferManager *buf_mgr_ = NULL;
  bool initalized_ = false;
};

}  // namespace gralloc1

#endif  // __GR_DEVICE_IMPL_H__
