/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
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

#ifndef __TONEMAPPER_EGLIMAGEWRAPPER_H__
#define __TONEMAPPER_EGLIMAGEWRAPPER_H__

#include <utils/LruCache.h>
#include <linux/msm_ion.h>
#include <string>
#include <map>
#include "EGLImageBuffer.h"

using std::string;
using std::map;

class EGLImageWrapper {
 private:
  class DeleteEGLImageCallback : public android::OnEntryRemoved<int, EGLImageBuffer*> {
   public:
     explicit DeleteEGLImageCallback(map<string, int>* mapPtr) { buffStrbuffIntMapPtr = mapPtr; }
     void operator()(int& buffInt, EGLImageBuffer*& eglImage);
     map<string, int>* buffStrbuffIntMapPtr = nullptr;
     bool mapClearPending = false;
   #ifndef TARGET_ION_ABI_VERSION
     int ion_fd = -1;
   #endif
  };

  android::LruCache<int, EGLImageBuffer *>* eglImageBufferCache;
  map<string, int> buffStrbuffIntMap = {};
  DeleteEGLImageCallback* callback = 0;
 #ifndef TARGET_ION_ABI_VERSION
   int ion_fd = -1;
 #else
   uint64_t buffInt = 0;
 #endif

 public:
  EGLImageWrapper();
  ~EGLImageWrapper();
  EGLImageBuffer* wrap(const void *pvt_handle);
};

#endif  // __TONEMAPPER_EGLIMAGEWRAPPER_H__
