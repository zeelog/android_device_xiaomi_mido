/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
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

#define EARLY_WAKEUP_FEATURE 1
#define DYNAMIC_EARLY_WAKEUP_CONFIG 1

namespace composer {

class DisplayExtnIntf {
 public:
  virtual int SetContentFps(uint32_t fps) = 0;
  virtual void RegisterDisplay(uint32_t display_id) = 0;
  virtual void UnregisterDisplay(uint32_t display_id) = 0;
  virtual int SetActiveConfig(uint32_t display_id, uint32_t config_id) = 0;
  virtual int NotifyEarlyWakeUp(bool gpu, bool display) = 0;
  virtual int NotifyDisplayEarlyWakeUp(uint32_t display_id) = 0;
  virtual int SetEarlyWakeUpConfig(uint32_t display_id, bool enable) = 0;

 protected:
  virtual ~DisplayExtnIntf() { }
};

}  // namespace composer

#endif  // __DISP_EXTN_INTF_H__
