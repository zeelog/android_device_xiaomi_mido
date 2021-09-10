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

#define DEBUG 0

#include <log/log.h>
#include <sync/sync.h>
#include <algorithm>
#include <sstream>
#include <string>

#include "gr_device_impl.h"
#include "gr_buf_descriptor.h"
#include "gralloc_priv.h"
#include "qd_utils.h"
#include "qdMetaData.h"
#include "gr_utils.h"

int gralloc_device_open(const struct hw_module_t *module, const char *name, hw_device_t **device);

int gralloc_device_close(struct hw_device_t *device);

static struct hw_module_methods_t gralloc_module_methods = {.open = gralloc_device_open};

struct gralloc_module_t HAL_MODULE_INFO_SYM = {
  .common = {
    .tag = HARDWARE_MODULE_TAG,
    .module_api_version = GRALLOC_MODULE_API_VERSION_1_0,
    .hal_api_version    = HARDWARE_HAL_API_VERSION,
    .id = GRALLOC_HARDWARE_MODULE_ID,
    .name = "Graphics Memory Module",
    .author = "Code Aurora Forum",
    .methods = &gralloc_module_methods,
    .dso = 0,
    .reserved = {0},
  },
};

int gralloc_device_open(const struct hw_module_t *module, const char *name, hw_device_t **device) {
  int status = -EINVAL;
  if (!strcmp(name, GRALLOC_HARDWARE_MODULE_ID)) {
    gralloc1::GrallocImpl * /*gralloc1_device_t*/ dev = gralloc1::GrallocImpl::GetInstance(module);
    *device = reinterpret_cast<hw_device_t *>(dev);
    if (dev) {
      status = 0;
    } else {
      ALOGE("Fatal error opening gralloc1 device");
    }
  }
  return status;
}

namespace gralloc1 {

GrallocImpl::GrallocImpl(const hw_module_t *module) {
  common.tag = HARDWARE_DEVICE_TAG;
  common.version = GRALLOC_MODULE_API_VERSION_1_0;
  common.module = const_cast<hw_module_t *>(module);
  common.close = CloseDevice;
  getFunction = GetFunction;
  getCapabilities = GetCapabilities;

  initalized_ = Init();
}

bool GrallocImpl::Init() {
  buf_mgr_ = BufferManager::GetInstance();
  return buf_mgr_ != nullptr;
}

GrallocImpl::~GrallocImpl() {
}

int GrallocImpl::CloseDevice(hw_device_t *device __unused) {
  // No-op since the gralloc device is a singleton
  return 0;
}

void GrallocImpl::GetCapabilities(struct gralloc1_device *device, uint32_t *out_count,
                                  int32_t  /*gralloc1_capability_t*/ *out_capabilities) {
  if (device != nullptr) {
    if (out_capabilities != nullptr && *out_count >= 3) {
      out_capabilities[0] = GRALLOC1_CAPABILITY_TEST_ALLOCATE;
      out_capabilities[1] = GRALLOC1_CAPABILITY_LAYERED_BUFFERS;
      out_capabilities[2] = GRALLOC1_CAPABILITY_RELEASE_IMPLY_DELETE;
    }
    *out_count = 3;
  }
  return;
}

gralloc1_function_pointer_t GrallocImpl::GetFunction(gralloc1_device_t *device, int32_t function) {
  if (!device) {
    return NULL;
  }

  switch (function) {
    case GRALLOC1_FUNCTION_DUMP:
      return reinterpret_cast<gralloc1_function_pointer_t>(Dump);
    case GRALLOC1_FUNCTION_CREATE_DESCRIPTOR:
      return reinterpret_cast<gralloc1_function_pointer_t>(CreateBufferDescriptor);
    case GRALLOC1_FUNCTION_DESTROY_DESCRIPTOR:
      return reinterpret_cast<gralloc1_function_pointer_t>(DestroyBufferDescriptor);
    case GRALLOC1_FUNCTION_SET_CONSUMER_USAGE:
      return reinterpret_cast<gralloc1_function_pointer_t>(SetConsumerUsage);
    case GRALLOC1_FUNCTION_SET_DIMENSIONS:
      return reinterpret_cast<gralloc1_function_pointer_t>(SetBufferDimensions);
    case GRALLOC1_FUNCTION_SET_FORMAT:
      return reinterpret_cast<gralloc1_function_pointer_t>(SetColorFormat);
    case GRALLOC1_FUNCTION_SET_LAYER_COUNT:
      return reinterpret_cast<gralloc1_function_pointer_t>(SetLayerCount);
    case GRALLOC1_FUNCTION_SET_PRODUCER_USAGE:
      return reinterpret_cast<gralloc1_function_pointer_t>(SetProducerUsage);
    case GRALLOC1_FUNCTION_GET_BACKING_STORE:
      return reinterpret_cast<gralloc1_function_pointer_t>(GetBackingStore);
    case GRALLOC1_FUNCTION_GET_CONSUMER_USAGE:
      return reinterpret_cast<gralloc1_function_pointer_t>(GetConsumerUsage);
    case GRALLOC1_FUNCTION_GET_DIMENSIONS:
      return reinterpret_cast<gralloc1_function_pointer_t>(GetBufferDimensions);
    case GRALLOC1_FUNCTION_GET_FORMAT:
      return reinterpret_cast<gralloc1_function_pointer_t>(GetColorFormat);
    case GRALLOC1_FUNCTION_GET_LAYER_COUNT:
      return reinterpret_cast<gralloc1_function_pointer_t>(GetLayerCount);
    case GRALLOC1_FUNCTION_GET_PRODUCER_USAGE:
      return reinterpret_cast<gralloc1_function_pointer_t>(GetProducerUsage);
    case GRALLOC1_FUNCTION_GET_STRIDE:
      return reinterpret_cast<gralloc1_function_pointer_t>(GetBufferStride);
    case GRALLOC1_FUNCTION_ALLOCATE:
      return reinterpret_cast<gralloc1_function_pointer_t>(AllocateBuffers);
    case GRALLOC1_FUNCTION_RETAIN:
      return reinterpret_cast<gralloc1_function_pointer_t>(RetainBuffer);
    case GRALLOC1_FUNCTION_RELEASE:
      return reinterpret_cast<gralloc1_function_pointer_t>(ReleaseBuffer);
    case GRALLOC1_FUNCTION_GET_NUM_FLEX_PLANES:
      return reinterpret_cast<gralloc1_function_pointer_t>(GetNumFlexPlanes);
    case GRALLOC1_FUNCTION_LOCK:
      return reinterpret_cast<gralloc1_function_pointer_t>(LockBuffer);
    case GRALLOC1_FUNCTION_LOCK_FLEX:
      return reinterpret_cast<gralloc1_function_pointer_t>(LockFlex);
    case GRALLOC1_FUNCTION_UNLOCK:
      return reinterpret_cast<gralloc1_function_pointer_t>(UnlockBuffer);
    case GRALLOC1_FUNCTION_PERFORM:
      return reinterpret_cast<gralloc1_function_pointer_t>(Gralloc1Perform);
    case GRALLOC1_FUNCTION_VALIDATE_BUFFER_SIZE:
      return reinterpret_cast<gralloc1_function_pointer_t>(ValidateBufferSize);
    case GRALLOC1_FUNCTION_GET_TRANSPORT_SIZE:
      return reinterpret_cast<gralloc1_function_pointer_t>(GetTransportSize);
    case  GRALLOC1_FUNCTION_IMPORT_BUFFER:
      return reinterpret_cast<gralloc1_function_pointer_t>(ImportBuffer);
    default:
      ALOGE("%s:Gralloc Error. Client Requested for unsupported function", __FUNCTION__);
      return NULL;
  }

  return NULL;
}

gralloc1_error_t GrallocImpl::Dump(gralloc1_device_t *device, uint32_t *out_size,
                                   char *out_buffer) {
  if (!device) {
    ALOGE("Gralloc Error : device=%p", (void *)device);
    return GRALLOC1_ERROR_BAD_DESCRIPTOR;
  }
  const size_t max_dump_size = 8192;
  if (out_buffer == nullptr) {
    *out_size = max_dump_size;
  } else {
    std::ostringstream os;
    os << "-------------------------------" << std::endl;
    os << "QTI gralloc dump:" << std::endl;
    os << "-------------------------------" << std::endl;
    GrallocImpl const *dev = GRALLOC_IMPL(device);
    dev->buf_mgr_->Dump(&os);
    os << "-------------------------------" << std::endl;
    auto copied = os.str().copy(out_buffer, std::min(os.str().size(), max_dump_size), 0);
    *out_size = UINT(copied);
  }

  return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t GrallocImpl::CheckDeviceAndHandle(gralloc1_device_t *device,
                                                   buffer_handle_t buffer) {
  const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
  if (!device || (private_handle_t::validate(hnd) != 0)) {
    ALOGE("Gralloc Error : device= %p, buffer-handle=%p", (void *)device, (void *)buffer);
    return GRALLOC1_ERROR_BAD_HANDLE;
  }

  return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t GrallocImpl::CreateBufferDescriptor(gralloc1_device_t *device,
                                                     gralloc1_buffer_descriptor_t *out_descriptor) {
  if (!device) {
    return GRALLOC1_ERROR_BAD_DESCRIPTOR;
  }
  GrallocImpl const *dev = GRALLOC_IMPL(device);
  return dev->buf_mgr_->CreateBufferDescriptor(out_descriptor);
}

gralloc1_error_t GrallocImpl::DestroyBufferDescriptor(gralloc1_device_t *device,
                                                      gralloc1_buffer_descriptor_t descriptor) {
  if (!device) {
    return GRALLOC1_ERROR_BAD_DESCRIPTOR;
  }
  GrallocImpl const *dev = GRALLOC_IMPL(device);
  return dev->buf_mgr_->DestroyBufferDescriptor(descriptor);
}

gralloc1_error_t GrallocImpl::SetConsumerUsage(gralloc1_device_t *device,
                                               gralloc1_buffer_descriptor_t descriptor,
                                               gralloc1_consumer_usage_t usage) {
  if (!device) {
    return GRALLOC1_ERROR_BAD_DESCRIPTOR;
  } else {
    GrallocImpl const *dev = GRALLOC_IMPL(device);
    return dev->buf_mgr_->CallBufferDescriptorFunction(descriptor,
                                                       &BufferDescriptor::SetConsumerUsage, usage);
  }
}

gralloc1_error_t GrallocImpl::SetBufferDimensions(gralloc1_device_t *device,
                                                  gralloc1_buffer_descriptor_t descriptor,
                                                  uint32_t width, uint32_t height) {
  if (!device) {
    return GRALLOC1_ERROR_BAD_DESCRIPTOR;
  } else {
    GrallocImpl const *dev = GRALLOC_IMPL(device);
    return dev->buf_mgr_->CallBufferDescriptorFunction(descriptor,
                                                       &BufferDescriptor::SetDimensions,
                                                       INT(width), INT(height));
  }
}

gralloc1_error_t GrallocImpl::SetColorFormat(gralloc1_device_t *device,
                                             gralloc1_buffer_descriptor_t descriptor,
                                             int32_t format) {
  if (!device) {
    return GRALLOC1_ERROR_BAD_DESCRIPTOR;
  } else {
    GrallocImpl const *dev = GRALLOC_IMPL(device);
    return dev->buf_mgr_->CallBufferDescriptorFunction(descriptor,
                                                       &BufferDescriptor::SetColorFormat, format);
  }
}

gralloc1_error_t GrallocImpl::SetLayerCount(gralloc1_device_t *device,
                                            gralloc1_buffer_descriptor_t descriptor,
                                            uint32_t layer_count) {
  if (!device) {
    return GRALLOC1_ERROR_BAD_DESCRIPTOR;
  } else {
    GrallocImpl const *dev = GRALLOC_IMPL(device);
    return dev->buf_mgr_->CallBufferDescriptorFunction(descriptor,
                                                       &BufferDescriptor::SetLayerCount,
                                                       layer_count);
  }
}

gralloc1_error_t GrallocImpl::SetProducerUsage(gralloc1_device_t *device,
                                               gralloc1_buffer_descriptor_t descriptor,
                                               gralloc1_producer_usage_t usage) {
  if (!device) {
    return GRALLOC1_ERROR_BAD_DESCRIPTOR;
  } else {
    GrallocImpl const *dev = GRALLOC_IMPL(device);
    return dev->buf_mgr_->CallBufferDescriptorFunction(descriptor,
                                                       &BufferDescriptor::SetProducerUsage, usage);
  }
}

gralloc1_error_t GrallocImpl::GetBackingStore(gralloc1_device_t *device, buffer_handle_t buffer,
                                              gralloc1_backing_store_t *out_backstore) {
  if (!device || !buffer) {
    return GRALLOC1_ERROR_BAD_HANDLE;
  }

  *out_backstore =
      static_cast<gralloc1_backing_store_t>(PRIV_HANDLE_CONST(buffer)->GetBackingstore());

  return GRALLOC1_ERROR_NONE;
}

gralloc1_error_t GrallocImpl::GetConsumerUsage(gralloc1_device_t *device, buffer_handle_t buffer,
                                               gralloc1_consumer_usage_t *outUsage) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE) {
    *outUsage = static_cast<gralloc1_consumer_usage_t> (PRIV_HANDLE_CONST(buffer)->GetUsage());
  }

  return status;
}

gralloc1_error_t GrallocImpl::GetBufferDimensions(gralloc1_device_t *device, buffer_handle_t buffer,
                                                  uint32_t *outWidth, uint32_t *outHeight) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE) {
    const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
    *outWidth = UINT(hnd->GetUnalignedWidth());
    *outHeight = UINT(hnd->GetUnalignedHeight());
  }

  return status;
}

gralloc1_error_t GrallocImpl::GetColorFormat(gralloc1_device_t *device, buffer_handle_t buffer,
                                             int32_t *outFormat) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE) {
    *outFormat = PRIV_HANDLE_CONST(buffer)->GetColorFormat();
  }

  return status;
}

gralloc1_error_t GrallocImpl::GetLayerCount(gralloc1_device_t *device, buffer_handle_t buffer,
                                            uint32_t *outLayerCount) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE) {
    *outLayerCount = PRIV_HANDLE_CONST(buffer)->GetLayerCount();
  }

  return status;
}

gralloc1_error_t GrallocImpl::GetProducerUsage(gralloc1_device_t *device, buffer_handle_t buffer,
                                               gralloc1_producer_usage_t *outUsage) {
  if (!outUsage) {
    return GRALLOC1_ERROR_BAD_VALUE;
  }

  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE) {
    const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
    *outUsage = static_cast<gralloc1_producer_usage_t> (hnd->GetUsage());
  }

  return status;
}

gralloc1_error_t GrallocImpl::GetBufferStride(gralloc1_device_t *device, buffer_handle_t buffer,
                                              uint32_t *outStride) {
  if (!outStride) {
    return GRALLOC1_ERROR_BAD_VALUE;
  }

  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE) {
    *outStride = UINT(PRIV_HANDLE_CONST(buffer)->GetStride());
  }

  return status;
}

gralloc1_error_t GrallocImpl::AllocateBuffers(gralloc1_device_t *device, uint32_t num_descriptors,
                                              const gralloc1_buffer_descriptor_t *descriptors,
                                              buffer_handle_t *out_buffers) {
  if (!num_descriptors || !descriptors) {
    return GRALLOC1_ERROR_BAD_DESCRIPTOR;
  }

  if (!device) {
    return GRALLOC1_ERROR_BAD_VALUE;
  }

  GrallocImpl const *dev = GRALLOC_IMPL(device);
  gralloc1_error_t status = dev->buf_mgr_->AllocateBuffers(num_descriptors, descriptors,
                                                           out_buffers);

  return status;
}

gralloc1_error_t GrallocImpl::RetainBuffer(gralloc1_device_t *device, buffer_handle_t buffer) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE) {
    const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
    GrallocImpl const *dev = GRALLOC_IMPL(device);
    status = dev->buf_mgr_->RetainBuffer(hnd);
  }

  return status;
}

gralloc1_error_t GrallocImpl::ReleaseBuffer(gralloc1_device_t *device, buffer_handle_t buffer) {
  if (!device || !buffer) {
    return GRALLOC1_ERROR_BAD_DESCRIPTOR;
  }

  const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
  GrallocImpl const *dev = GRALLOC_IMPL(device);
  return dev->buf_mgr_->ReleaseBuffer(hnd);
}

gralloc1_error_t GrallocImpl::GetNumFlexPlanes(gralloc1_device_t *device, buffer_handle_t buffer,
                                               uint32_t *out_num_planes) {
  if (!out_num_planes) {
    return GRALLOC1_ERROR_BAD_VALUE;
  }

  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status == GRALLOC1_ERROR_NONE) {
    GrallocImpl const *dev = GRALLOC_IMPL(device);
    const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
    status = dev->buf_mgr_->GetNumFlexPlanes(hnd, out_num_planes);
  }
  return status;
}

static inline void CloseFdIfValid(int fd) {
  if (fd > 0) {
    close(fd);
  }
}

gralloc1_error_t GrallocImpl::LockBuffer(gralloc1_device_t *device, buffer_handle_t buffer,
                                         gralloc1_producer_usage_t prod_usage,
                                         gralloc1_consumer_usage_t cons_usage,
                                         const gralloc1_rect_t *region, void **out_data,
                                         int32_t acquire_fence) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status != GRALLOC1_ERROR_NONE || !out_data ||
      !region) {  // currently we ignore the region/rect client wants to lock
    CloseFdIfValid(acquire_fence);
    return status;
  }

  if (acquire_fence > 0) {
    int error = sync_wait(acquire_fence, 1000);
    CloseFdIfValid(acquire_fence);
    if (error < 0) {
      ALOGE("%s: sync_wait timedout! error = %s", __FUNCTION__, strerror(errno));
      return GRALLOC1_ERROR_UNDEFINED;
    }
  }

  const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
  GrallocImpl const *dev = GRALLOC_IMPL(device);

  // Either producer usage or consumer usage must be *_USAGE_NONE
  if ((prod_usage != GRALLOC1_PRODUCER_USAGE_NONE) &&
      (cons_usage != GRALLOC1_CONSUMER_USAGE_NONE)) {
    // Current gralloc1 clients do not satisfy this restriction.
    // See b/33588773 for details
    // return GRALLOC1_ERROR_BAD_VALUE;
  }

  // TODO(user): Need to check if buffer was allocated with the same flags
  status = dev->buf_mgr_->LockBuffer(hnd, prod_usage, cons_usage);
  *out_data = reinterpret_cast<void *>(hnd->base);

  return status;
}

gralloc1_error_t GrallocImpl::LockFlex(gralloc1_device_t *device, buffer_handle_t buffer,
                                       gralloc1_producer_usage_t prod_usage,
                                       gralloc1_consumer_usage_t cons_usage,
                                       const gralloc1_rect_t *region,
                                       struct android_flex_layout *out_flex_layout,
                                       int32_t acquire_fence) {
  if (!out_flex_layout) {
    CloseFdIfValid(acquire_fence);
    return GRALLOC1_ERROR_BAD_VALUE;
  }

  void *out_data {};
  gralloc1_error_t status = GrallocImpl::LockBuffer(device, buffer, prod_usage, cons_usage, region,
                                                    &out_data, acquire_fence);
  if (status != GRALLOC1_ERROR_NONE) {
    return status;
  }

  GrallocImpl const *dev = GRALLOC_IMPL(device);
  const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
  dev->buf_mgr_->GetFlexLayout(hnd, out_flex_layout);
  return status;
}

gralloc1_error_t GrallocImpl::UnlockBuffer(gralloc1_device_t *device, buffer_handle_t buffer,
                                           int32_t *release_fence) {
  gralloc1_error_t status = CheckDeviceAndHandle(device, buffer);
  if (status != GRALLOC1_ERROR_NONE) {
    return status;
  }

  if (!release_fence) {
    return GRALLOC1_ERROR_BAD_VALUE;
  }

  const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
  GrallocImpl const *dev = GRALLOC_IMPL(device);

  *release_fence = -1;

  return dev->buf_mgr_->UnlockBuffer(hnd);
}

gralloc1_error_t GrallocImpl::Gralloc1Perform(gralloc1_device_t *device, int operation, ...) {
  if (!device) {
    return GRALLOC1_ERROR_BAD_VALUE;
  }

  va_list args;
  va_start(args, operation);
  GrallocImpl const *dev = GRALLOC_IMPL(device);
  gralloc1_error_t err = dev->buf_mgr_->Perform(operation, args);
  va_end(args);

  return err;
}

gralloc1_error_t GrallocImpl::ValidateBufferSize(gralloc1_device_t *device,
                                                 buffer_handle_t buffer,
                                                 gralloc1_buffer_descriptor_info_t &descriptor_info,
                                                 int32_t stride __unused) {
  if(!device) {
    return GRALLOC1_ERROR_BAD_VALUE;
  }
  GrallocImpl const *dev = GRALLOC_IMPL(device);

  auto err = GRALLOC1_ERROR_BAD_VALUE;
  const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);
  if (buffer != nullptr && private_handle_t::validate(hnd) == 0) {
    if (dev->buf_mgr_->IsBufferImported(hnd) != GRALLOC1_ERROR_NONE) {
      return GRALLOC1_ERROR_BAD_VALUE;
    }
    auto info = gralloc1::BufferInfo(static_cast<int>(descriptor_info.width),
                                    static_cast<int>(descriptor_info.height),
                                    static_cast<int>(descriptor_info.format),
                                    static_cast<gralloc1_producer_usage_t>(descriptor_info.producerUsage),
                                    static_cast<gralloc1_consumer_usage_t>(descriptor_info.consumerUsage));
    info.layer_count = static_cast<int>(descriptor_info.layerCount);
    err = dev->buf_mgr_->ValidateBufferSize(hnd, info);
  }
  return err;
}

gralloc1_error_t GrallocImpl::GetTransportSize(gralloc1_device_t *device __unused,
                                               buffer_handle_t buffer,
                                               uint32_t *outNumFds,
                                               uint32_t *outNumInts) {
  auto err = GRALLOC1_ERROR_BAD_VALUE;
  const private_handle_t *hnd = PRIV_HANDLE_CONST(buffer);

  if (buffer != nullptr && private_handle_t::validate(hnd) == 0) {
    *outNumFds = 2;
    // TODO(user): reduce to transported values;
    *outNumInts = static_cast<uint32_t >(hnd->numInts);
    err = GRALLOC1_ERROR_NONE;
  }
  ALOGD_IF(DEBUG, "GetTransportSize: num fds: %d num ints: %d err:%d", *outNumFds, *outNumInts, err);
  return err;
}

gralloc1_error_t GrallocImpl::ImportBuffer(gralloc1_device_t *device,
                                           const native_handle_t* handle,
                                           const native_handle_t** outBufferHandle) {
  if(!device) {
    return GRALLOC1_ERROR_BAD_VALUE;
  }
  GrallocImpl const *dev = GRALLOC_IMPL(device);

  if (!handle) {
    ALOGE("%s: Unable to import handle", __FUNCTION__);
    return GRALLOC1_ERROR_BAD_HANDLE;
  }

  native_handle_t *buffer_handle = native_handle_clone(handle);
  if (!buffer_handle) {
    ALOGE("%s: Unable to clone handle", __FUNCTION__);
    return GRALLOC1_ERROR_NO_RESOURCES;
  }

  auto error = dev->buf_mgr_->RetainBuffer(PRIV_HANDLE_CONST(buffer_handle));
  if (error != GRALLOC1_ERROR_NONE) {
    ALOGE("%s: Unable to retain handle: %p", __FUNCTION__, buffer_handle);
    native_handle_close(buffer_handle);
    native_handle_delete(buffer_handle);
    return error;
  }
  ALOGD_IF(DEBUG, "Imported handle: %p id: %" PRIu64, buffer_handle,
           PRIV_HANDLE_CONST(buffer_handle)->id);
  *outBufferHandle = buffer_handle;
  return GRALLOC1_ERROR_NONE;
}

}  // namespace gralloc1
