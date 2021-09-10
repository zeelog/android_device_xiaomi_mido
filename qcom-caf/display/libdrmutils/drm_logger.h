/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
*
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

#ifndef __DRM_LOGGER_H__
#define __DRM_LOGGER_H__

#include <utility>

namespace drm_utils {

class DRMLogger {
 public:
  virtual ~DRMLogger() {}
  virtual void Error(const char *format, ...) = 0;
  virtual void Warning(const char *format, ...) = 0;
  virtual void Info(const char *format, ...) = 0;
  virtual void Debug(const char *format, ...) = 0;

  static void Set(DRMLogger *logger) { s_instance = logger; }
  static DRMLogger *Get() { return s_instance; }

 private:
  static DRMLogger *s_instance;
};

#define DRM_LOG(method, format, ...)                            \
  if (drm_utils::DRMLogger::Get()) {                            \
    drm_utils::DRMLogger::Get()->method(format, ##__VA_ARGS__); \
  }

#define DRM_LOG_CONTEXT(method, format, ...) \
  DRM_LOG(method, __CLASS__ "::%s: " format, __FUNCTION__, ##__VA_ARGS__);

#define DRM_LOGE(format, ...) DRM_LOG_CONTEXT(Error, format, ##__VA_ARGS__)
#define DRM_LOGW(format, ...) DRM_LOG_CONTEXT(Warning, format, ##__VA_ARGS__)
#define DRM_LOGI(format, ...) DRM_LOG_CONTEXT(Info, format, ##__VA_ARGS__)
#define DRM_LOGD_IF(pred, format, ...) \
  if (pred)                            \
  DRM_LOG_CONTEXT(Debug, format, ##__VA_ARGS__)

}  // namespace drm_utils

#endif  // __DRM_LOGGER_H__
