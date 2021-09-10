/*
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
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

#include "EGLImageWrapper.h"
#include <cutils/native_handle.h>
#include <gralloc_priv.h>
#include <ui/GraphicBuffer.h>
#include <fcntl.h>
#include <string>
#include <map>
#include <utility>

using std::string;
using std::map;
using std::pair;

static string pidString = std::to_string(getpid());

#ifndef TARGET_ION_ABI_VERSION
//-----------------------------------------------------------------------------
static void free_ion_cookie(int ion_fd, int cookie)
//-----------------------------------------------------------------------------
{
  if (ion_fd && !ioctl(ion_fd, ION_IOC_FREE, &cookie)) {
  } else {
      ALOGE("ION_IOC_FREE failed: ion_fd = %d, cookie = %d", ion_fd, cookie);
  }
}

//-----------------------------------------------------------------------------
static int get_ion_cookie(int ion_fd, int fd)
//-----------------------------------------------------------------------------
{
  int cookie = fd;

  struct ion_fd_data fdData;
  memset(&fdData, 0, sizeof(fdData));
  fdData.fd = fd;

  if (ion_fd && !ioctl(ion_fd, ION_IOC_IMPORT, &fdData)) {
       cookie = fdData.handle;
  } else {
       ALOGE("ION_IOC_IMPORT failed: ion_fd = %d, fd = %d", ion_fd, fd);
  }

  return cookie;
}
#else
//-----------------------------------------------------------------------------
static string get_ion_buff_str(int buff_fd)
//-----------------------------------------------------------------------------
{
  string retStr = {};
  if (buff_fd >= 0) {
    string fdString = std::to_string(buff_fd);
    string symlinkPath = "/proc/"+pidString+"/fd/"+fdString;
    char buffer[1024] = {};
    ssize_t ret = ::readlink(symlinkPath.c_str(), buffer, sizeof(buffer) - 1);
    if (ret != -1) {
      buffer[ret] = '\0';
      retStr = buffer;
    }
  }

  return retStr;
}
#endif

//-----------------------------------------------------------------------------
void EGLImageWrapper::DeleteEGLImageCallback::operator()(int& buffInt, EGLImageBuffer*& eglImage)
//-----------------------------------------------------------------------------
{
  if (eglImage != 0) {
    delete eglImage;
  }

#ifndef TARGET_ION_ABI_VERSION
  free_ion_cookie(ion_fd, buffInt /* cookie */);
#else
  if (!mapClearPending) {
    for (auto it = buffStrbuffIntMapPtr->begin(); it != buffStrbuffIntMapPtr->end(); it++) {
      if (it->second == buffInt /* counter */) {
        buffStrbuffIntMapPtr->erase(it);
        return;
      }
    }
  }
#endif
}

//-----------------------------------------------------------------------------
EGLImageWrapper::EGLImageWrapper()
//-----------------------------------------------------------------------------
{
  eglImageBufferCache = new android::LruCache<int, EGLImageBuffer*>(32);
  callback = new DeleteEGLImageCallback(&buffStrbuffIntMap);
  eglImageBufferCache->setOnEntryRemovedListener(callback);

#ifndef TARGET_ION_ABI_VERSION
  ion_fd = open("/dev/ion", O_RDONLY);
  callback->ion_fd = ion_fd;
#endif
}

//-----------------------------------------------------------------------------
EGLImageWrapper::~EGLImageWrapper()
//-----------------------------------------------------------------------------
{
  if (eglImageBufferCache != 0) {
    if (callback != 0) {
      callback->mapClearPending = true;
    }
    eglImageBufferCache->clear();
    delete eglImageBufferCache;
    eglImageBufferCache = 0;
    buffStrbuffIntMap.clear();
  }

  if (callback != 0) {
    delete callback;
    callback = 0;
  }

#ifndef TARGET_ION_ABI_VERSION
  if (ion_fd > 0) {
    close(ion_fd);
    ion_fd = -1;
  }
#endif
}

//-----------------------------------------------------------------------------
static EGLImageBuffer* L_wrap(const private_handle_t *src)
//-----------------------------------------------------------------------------
{
  EGLImageBuffer* result = 0;

  native_handle_t *native_handle = const_cast<private_handle_t *>(src);

  int flags = android::GraphicBuffer::USAGE_HW_TEXTURE |
              android::GraphicBuffer::USAGE_SW_READ_NEVER |
              android::GraphicBuffer::USAGE_SW_WRITE_NEVER;

  if (src->flags & private_handle_t::PRIV_FLAGS_SECURE_BUFFER) {
    flags |= android::GraphicBuffer::USAGE_PROTECTED;
  }

  android::sp<android::GraphicBuffer> graphicBuffer =
    new android::GraphicBuffer(src->unaligned_width, src->unaligned_height, src->format,
#ifndef __NOUGAT__
                               1,  // Layer count
#endif
                               flags, src->width /*src->stride*/,
                               native_handle, false);

  result = new EGLImageBuffer(graphicBuffer);

  return result;
}

//-----------------------------------------------------------------------------
EGLImageBuffer *EGLImageWrapper::wrap(const void *pvt_handle)
//-----------------------------------------------------------------------------
{
  const private_handle_t *src = static_cast<const private_handle_t *>(pvt_handle);
  EGLImageBuffer* eglImage = nullptr;
#ifndef TARGET_ION_ABI_VERSION
  int ion_cookie = get_ion_cookie(ion_fd, src->fd);
  eglImage = eglImageBufferCache->get(ion_cookie);
  if (eglImage == 0) {
    eglImage = L_wrap(src);
    eglImageBufferCache->put(ion_cookie, eglImage);
  } else {
    free_ion_cookie(ion_fd, ion_cookie);
  }
#else
  string buffStr = get_ion_buff_str(src->fd);
  if (!buffStr.empty()) {
    auto it = buffStrbuffIntMap.find(buffStr);
    if (it != buffStrbuffIntMap.end()) {
      eglImage = eglImageBufferCache->get(it->second);
    } else {
        eglImage = L_wrap(src);
        buffStrbuffIntMap.insert(pair<string, int>(buffStr, buffInt));
        eglImageBufferCache->put(buffInt, eglImage);
        buffInt++;
    }
  } else {
    ALOGE("Could not provide an eglImage for fd = %d, EGLImageWrapper = %p", src->fd, this);
  }
#endif

  return eglImage;
}
