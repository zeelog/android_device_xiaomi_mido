/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright 2015 The Android Open Source Project
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

#ifndef __EGLIMAGE_BUFFER_H__
#define __EGLIMAGE_BUFFER_H__

#include <cutils/native_handle.h>
#include <gralloc_priv.h>
#include <ui/GraphicBuffer.h>
#include "engine.h"

class EGLImageBuffer {
  // android::sp<android::GraphicBuffer> graphicBuffer;
  void *eglImageID;
  int width;
  int height;
  uint textureID;
  uint renderbufferID;
  uint framebufferID;

 public:
  int getWidth();
  int getHeight();
  EGLImageBuffer(android::sp<android::GraphicBuffer>);
  unsigned int getTexture();
  unsigned int getFramebuffer();
  void bindAsTexture();
  void bindAsFramebuffer();
  ~EGLImageBuffer();
  static EGLImageBuffer *from(const private_handle_t *src);
  static void clear();
};

#endif  //__EGLIMAGE_BUFFER_H__