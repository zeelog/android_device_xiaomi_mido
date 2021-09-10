/*
* Copyright (c) 2014 - 2018, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
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

#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <stdint.h>
#include <core/sdm_types.h>
#include <core/debug_interface.h>
#include <core/display_interface.h>
#include <display_properties.h>

#define DLOG(tag, method, format, ...) Debug::Get()->method(tag, __CLASS__ "::%s: " format, \
                                                            __FUNCTION__, ##__VA_ARGS__)

#define DLOGE_IF(tag, format, ...) DLOG(tag, Error, format, ##__VA_ARGS__)
#define DLOGW_IF(tag, format, ...) DLOG(tag, Warning, format, ##__VA_ARGS__)
#define DLOGI_IF(tag, format, ...) DLOG(tag, Info, format, ##__VA_ARGS__)
#define DLOGD_IF(tag, format, ...) DLOG(tag, Debug, format, ##__VA_ARGS__)
#define DLOGV_IF(tag, format, ...) DLOG(tag, Verbose, format, ##__VA_ARGS__)

#define DLOGE(format, ...) DLOGE_IF(kTagNone, format, ##__VA_ARGS__)
#define DLOGD(format, ...) DLOGD_IF(kTagNone, format, ##__VA_ARGS__)
#define DLOGW(format, ...) DLOGW_IF(kTagNone, format, ##__VA_ARGS__)
#define DLOGI(format, ...) DLOGI_IF(kTagNone, format, ##__VA_ARGS__)
#define DLOGV(format, ...) DLOGV_IF(kTagNone, format, ##__VA_ARGS__)

#define DTRACE_BEGIN(custom_string) Debug::Get()->BeginTrace(__CLASS__, __FUNCTION__, custom_string)
#define DTRACE_END() Debug::Get()->EndTrace()
#define DTRACE_SCOPED() ScopeTracer <Debug> scope_tracer(__CLASS__, __FUNCTION__)

namespace sdm {

class Debug {
 public:
  static inline void SetDebugHandler(DebugHandler *debug_handler) {
    debug_.debug_handler_ = debug_handler;
  }
  static inline DebugHandler* Get() { return debug_.debug_handler_; }
  static int GetSimulationFlag();
  static int GetHDMIResolution();
  static void GetIdleTimeoutMs(uint32_t *active_ms, uint32_t *inactive_ms);
  static int GetBootAnimLayerCount();
  static bool IsRotatorDownScaleDisabled();
  static bool IsDecimationDisabled();
  static int GetMaxPipesPerMixer(DisplayType display_type);
  static int GetMaxUpscale();
  static bool IsVideoModeEnabled();
  static bool IsRotatorUbwcDisabled();
  static bool IsRotatorSplitDisabled();
  static bool IsScalarDisabled();
  static bool IsUbwcTiledFrameBuffer();
  static bool IsAVRDisabled();
  static bool IsExtAnimDisabled();
  static bool IsPartialSplitDisabled();
  static DisplayError GetMixerResolution(uint32_t *width, uint32_t *height);
  static int GetExtMaxlayers();
  static bool GetProperty(const char *property_name, char *value);
  static bool SetProperty(const char *property_name, const char *value);

 private:
  Debug();

  // By default, drop any log messages/traces coming from Display manager. It will be overriden by
  // Display manager client when core is successfully initialized.
  class DefaultDebugHandler : public DebugHandler {
   public:
    virtual void Error(DebugTag /*tag*/, const char */*format*/, ...) { }
    virtual void Warning(DebugTag /*tag*/, const char */*format*/, ...) { }
    virtual void Info(DebugTag /*tag*/, const char */*format*/, ...) { }
    virtual void Debug(DebugTag /*tag*/, const char */*format*/, ...) { }
    virtual void Verbose(DebugTag /*tag*/, const char */*format*/, ...) { }
    virtual void BeginTrace(const char */*class_name*/, const char */*function_name*/,
                            const char */*custom_string*/) { }
    virtual void EndTrace() { }
    virtual DisplayError GetProperty(const char */*property_name*/, int */*value*/) {
      return kErrorNotSupported;
    }
    virtual DisplayError GetProperty(const char */*property_name*/, char */*value*/) {
      return kErrorNotSupported;
    }
    virtual DisplayError SetProperty(const char */*property_name*/, const char */*value*/) {
      return kErrorNotSupported;
    }
  };

  DefaultDebugHandler default_debug_handler_;
  DebugHandler *debug_handler_;
  static Debug debug_;
};

}  // namespace sdm

#endif  // __DEBUG_H__

