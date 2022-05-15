/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
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

#ifndef __DISP_EXTN_INTF_H__
#define __DISP_EXTN_INTF_H__

#include <vector>
#include <list>
#include <ui/Fence.h>

#define EARLY_WAKEUP_FEATURE 1
#define DYNAMIC_EARLY_WAKEUP_CONFIG 1
#define PASS_COMPOSITOR_TID 1
#define SMART_DISPLAY_CONFIG 1
#define FPS_MITIGATION_ENABLED 1
#define UNIFIED_DRAW_EXT 1

namespace composer {

using FpsMitigationCallback = std::function<void(float)>;

struct LayerFlags {
  bool secure_camera = false;
  bool secure_video = false;
  bool secure_ui = false;
  bool compatible = false;
};

struct FBTLayerInfo {
  int32_t width = 0;
  int32_t height = 0;
  int32_t dataspace = 0;
  int32_t max_buffer_count = 3;
  bool secure = false;

  bool operator != (FBTLayerInfo  &f) {
    return (width != f.width ||
            height != f.height ||
            dataspace != f.dataspace ||
            secure != f.secure);
  }
};

struct FBTSlotInfo {
  int index = -1;
  android::sp<android::Fence> fence = android::Fence::NO_FENCE;
  bool predicted = false;
};

enum PerfHintType {
  kNone = 0,
  kSurfaceFlinger,
  kRenderEngine,
};

class DisplayExtnIntf {
 public:
  virtual int SetContentFps(uint32_t fps) = 0;
  virtual void RegisterDisplay(uint32_t display_id) = 0;
  virtual void UnregisterDisplay(uint32_t display_id) = 0;
  virtual int SetActiveConfig(uint32_t display_id, uint32_t config_id) = 0;
  virtual int NotifyEarlyWakeUp(bool gpu, bool display) = 0;
  virtual int NotifyDisplayEarlyWakeUp(uint32_t display_id) = 0;
  virtual int SetEarlyWakeUpConfig(uint32_t display_id, bool enable) = 0;
  virtual int TryUnifiedDraw(uint32_t display_id, int32_t max_frameBuffer) = 0;
  virtual int BeginDraw(uint32_t display_id, std::vector<LayerFlags> &layers,
                        const FBTLayerInfo fbt_info, const FBTSlotInfo &fbt_current,
                        FBTSlotInfo &fbt_future) = 0;
  virtual int EndDraw(uint32_t display_id, const FBTSlotInfo &fbt_current) = 0;
  virtual int SendCompositorTid(PerfHintType type, int tid) = 0;
  virtual bool IsSmartDisplayConfig(uint32_t display_id) = 0;
  virtual void SetFpsMitigationCallback(const FpsMitigationCallback callback,
                                        std::vector<float> fps_list) = 0;
  virtual void EndUnifiedDraw(uint32_t display_id) = 0;

 protected:
  virtual ~DisplayExtnIntf() { }
};

}  // namespace composer

#endif  // __DISP_EXTN_INTF_H__
