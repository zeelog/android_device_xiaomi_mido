/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.

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

#ifndef __GR_BUF_DESCRIPTOR_H__
#define __GR_BUF_DESCRIPTOR_H__

#include <hardware/gralloc1.h>
#include <atomic>

namespace gralloc1 {
class BufferDescriptor {
 public:
  BufferDescriptor() : id_(next_id_++) {}

  BufferDescriptor(int w, int h, int f)
      : width_(w),
        height_(h),
        format_(f),
        producer_usage_(GRALLOC1_PRODUCER_USAGE_NONE),
        consumer_usage_(GRALLOC1_CONSUMER_USAGE_NONE),
        id_(next_id_++) {}

  BufferDescriptor(int w, int h, int f, gralloc1_producer_usage_t prod_usage,
                   gralloc1_consumer_usage_t cons_usage)
      : width_(w),
        height_(h),
        format_(f),
        producer_usage_(prod_usage),
        consumer_usage_(cons_usage),
        id_(next_id_++) {}

  void SetConsumerUsage(gralloc1_consumer_usage_t usage) { consumer_usage_ = usage; }

  void SetProducerUsage(gralloc1_producer_usage_t usage) { producer_usage_ = usage; }

  void SetDimensions(int w, int h) {
    width_ = w;
    height_ = h;
  }

  void SetColorFormat(int format) { format_ = format; }

  void SetLayerCount(uint32_t layer_count) { layer_count_ = layer_count; }

  gralloc1_consumer_usage_t GetConsumerUsage() const { return consumer_usage_; }

  gralloc1_producer_usage_t GetProducerUsage() const { return producer_usage_; }

  int GetWidth() const { return width_; }

  int GetHeight() const { return height_; }

  int GetFormat() const { return format_; }

  uint32_t GetLayerCount() const { return layer_count_; }

  gralloc1_buffer_descriptor_t GetId() const { return id_; }

 private:
  int width_ = -1;
  int height_ = -1;
  int format_ = -1;
  uint32_t layer_count_ = 1;
  gralloc1_producer_usage_t producer_usage_ = GRALLOC1_PRODUCER_USAGE_NONE;
  gralloc1_consumer_usage_t consumer_usage_ = GRALLOC1_CONSUMER_USAGE_NONE;
  const gralloc1_buffer_descriptor_t id_;
  static std::atomic<gralloc1_buffer_descriptor_t> next_id_;
};
};  // namespace gralloc1
#endif  // __GR_BUF_DESCRIPTOR_H__
